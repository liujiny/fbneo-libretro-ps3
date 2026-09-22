
#include "pgm.h"
#include "ps3_sprite_worker.h"
#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
#include "ps3_sprite_color_shadow.h"
#endif
#include "v3021.h"
#include "ics2115.h"
#include "timer.h"

#ifdef __PS3__
#include <stdio.h>
#endif
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/cond.h>
#endif
#ifdef __PS3__
#include "../../ps3_memory_pool.h"
#define PS3_PGM_MEM_LABEL(x) ps3_mem_diag_set_next_label(x)
#else
#define PS3_PGM_MEM_LABEL(x) ((void)0)
#endif

UINT8 PgmJoy1[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmJoy2[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmJoy3[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmJoy4[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmBtn1[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmBtn2[8] = {0,0,0,0,0,0,0,0};
UINT8 PgmInput[9] = {0,0,0,0,0,0,0,0,0};
UINT8 PgmReset = 0;
static HoldCoin<4> hold_coin;
static ClearOpposite<4, UINT8> clear_opposite;

INT32 nPGM68KROMLen = 0;
INT32 nPGMTileROMLen = 0;
INT32 nPGMSPRColROMLen = 0;
INT32 nPGMSPRMaskROMLen = 0;
INT32 nPGMSNDROMLen = 0;
INT32 nPGMSPRColMaskLen = 0;
INT32 nPGMSPRColPacked = 0;
INT32 nPGMSPRMaskMaskLen = 0;
INT32 nPGMExternalARMLen = 0;

UINT16 pgm_bg_scrollx;
UINT16 pgm_bg_scrolly;
UINT16 pgm_fg_scrollx;
UINT16 pgm_fg_scrolly;
UINT16 pgm_video_control;
static UINT16 pgm_unk_video_flags;
static INT32 pgm_z80_connect_bus;

UINT16 *PGMZoomRAM;
UINT32 *PGMBgRAM;
UINT32 *PGMTxtRAM;
UINT32 *RamCurPal;
UINT16 *PGMRowRAM;
UINT16 *PGMPalRAM;
UINT16 *PGMVidReg;
UINT16 *PGMSprBuf;
static UINT8 *RamZ80;
UINT8 *PGM68KRAM;

static UINT16 nSoundlatch[3] = {0, 0, 0};
static UINT8 bSoundlatchRead[3] = {0, 0, 0};

static UINT8 *Mem = NULL, *MemEnd = NULL;
static UINT8 *RamStart, *RamEnd;

UINT8 *PGM68KBIOS, *PGM68KROM, *PGMTileROM, *PGMTileROMExp, *PGMSPRColROM, *PGMSPRMaskROM, *PGMARMROM;
#ifdef __PS3__
static UINT8 *PGMTileSharedAlloc = NULL;
#endif
UINT8 *PGMARMRAM0, *PGMUSER0, *PGMARMRAM1, *PGMARMRAM2, *PGMARMShareRAM, *PGMARMShareRAM2, *PGMProtROM;

UINT8 *ICSSNDROM;

#ifdef __PS3__
// PS3_KOV2_COMPACT_SOUND: keep KOV2 logical ICS2115 addresses while removing the 6 MiB XDR hole.
static INT32 pgm_ps3_kov2_compact_sound()
{
	return (!bDoIpsPatch && strcmp(BurnDrvGetTextA(DRV_NAME), "kov2") == 0);
}
static INT32 nPGMSNDROMAllocLen = 0;

#ifndef PS3_PGM_MASK_FILE_CACHE
#define PS3_PGM_MASK_FILE_CACHE 0
#endif

#define PS3_PGM_MASK_CACHE_PATH "/dev_hdd0/tmp/fbneo-pgm-mask.cache"
#include "ps3_pgm_cache_option.h"

UINT8 *PGMSPRMaskPageCache = NULL;
INT32 PGMSPRMaskPageTag[PGM_PS3_MASK_CACHE_PAGES];
INT32 nPGMSPRMaskFileCacheActive = 0;

static FILE *g_ps3_mask_cache_file = NULL;
static UINT32 g_ps3_mask_cache_misses = 0;
static UINT32 g_ps3_mask_cache_errors = 0;

UINT8 *pgm_ps3_mask_cache_miss(UINT32 page)
{
    UINT32 slot = page & PGM_PS3_MASK_CACHE_MASK;
    UINT8 *dst = PGMSPRMaskPageCache +
        (slot << PGM_PS3_MASK_PAGE_SHIFT);
    long file_offset = (long)(page << PGM_PS3_MASK_PAGE_SHIFT);
    unsigned long long io_start = ps3_perf_now_us();

    g_ps3_mask_cache_misses++;

    if (g_ps3_mask_cache_file == NULL) {
        memset(dst, 0, PGM_PS3_MASK_PAGE_SIZE);
        g_ps3_mask_cache_errors++;
        ps3_perf_record_cache_io(PS3_PERF_MASK_CACHE, 0, 0, 0,
            ps3_perf_now_us() - io_start);
        PGMSPRMaskPageTag[slot] = (INT32)page;
        return dst;
    }

    if (fseek(g_ps3_mask_cache_file, file_offset, SEEK_SET) != 0) {
        memset(dst, 0, PGM_PS3_MASK_PAGE_SIZE);
        g_ps3_mask_cache_errors++;
        ps3_perf_record_cache_io(PS3_PERF_MASK_CACHE, 1, 0, 0,
            ps3_perf_now_us() - io_start);
        PGMSPRMaskPageTag[slot] = (INT32)page;
        return dst;
    }

    size_t got = fread(
        dst, 1, PGM_PS3_MASK_PAGE_SIZE, g_ps3_mask_cache_file);
    ps3_perf_record_cache_io(PS3_PERF_MASK_CACHE, 1, 1, got,
        ps3_perf_now_us() - io_start);
    ps3_perf_record_cache_page(PS3_PERF_MASK_CACHE, page);

    if (got < PGM_PS3_MASK_PAGE_SIZE) {
        memset(dst + got, 0, PGM_PS3_MASK_PAGE_SIZE - got);

        if ((UINT32)file_offset + (UINT32)got <
            (UINT32)nPGMSPRMaskROMLen) {
            g_ps3_mask_cache_errors++;
        }
    }

    PGMSPRMaskPageTag[slot] = (INT32)page;
    return dst;
}

static void pgm_ps3_mask_cache_close_file()
{
    if (g_ps3_mask_cache_file != NULL) {
        fclose(g_ps3_mask_cache_file);
        g_ps3_mask_cache_file = NULL;
    }
}

static INT32 pgm_ps3_mask_cache_restore_full()
{
    PS3_PGM_MEM_LABEL("PGMSPRMaskROM");
    PGMSPRMaskROM = (UINT8*)BurnMalloc(nPGMSPRMaskROMLen);

    if (PGMSPRMaskROM == NULL) {
        bprintf(PRINT_ERROR,
            _T("[FBNeo] PS3 mask cache fallback: unable to restore full mask ROM\n"));
        return 1;
    }

    if (fseek(g_ps3_mask_cache_file, 0, SEEK_SET) != 0 ||
        fread(PGMSPRMaskROM, 1, nPGMSPRMaskROMLen,
              g_ps3_mask_cache_file) !=
              (size_t)nPGMSPRMaskROMLen) {
        bprintf(PRINT_ERROR,
            _T("[FBNeo] PS3 mask cache fallback: reload failed\n"));
        return 1;
    }

    pgm_ps3_mask_cache_close_file();
    remove(PS3_PGM_MASK_CACHE_PATH);

    bprintf(PRINT_IMPORTANT,
        _T("[FBNeo] PS3 mask cache fallback: restored full mask ROM\n"));

    return 0;
}

static INT32 pgm_ps3_mask_cache_build()
{
#if !PS3_PGM_MASK_FILE_CACHE
    return 0;
#else
#ifdef __LIBRETRO__
    if (!ps3_pgm_hdd_cache_requested) {
        bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 PGM mask HDD cache OFF: keeping ROM in RAM\n"));
        return 0;
    }
#endif
    /*
     * Generic PS3 PGM sprite-mask cache.
     * Only sufficiently large power-of-two mask images are paged.
     */
    if (PGMSPRMaskROM == NULL ||
        nPGMSPRMaskROMLen <=
        (INT32)(PGM_PS3_MASK_CACHE_PAGES *
                PGM_PS3_MASK_PAGE_SIZE)) {
        return 0;
    }

    /*
     * First implementation requires a power-of-two mask image,
     * matching the existing mask-address wrap logic.
     */
    if ((nPGMSPRMaskROMLen &
        (nPGMSPRMaskROMLen - 1)) != 0) {
        bprintf(PRINT_IMPORTANT,
            _T("[FBNeo] PS3 mask cache skipped: non-power-of-two mask size=%d\n"),
            nPGMSPRMaskROMLen);
        return 0;
    }

    FILE *out = fopen(PS3_PGM_MASK_CACHE_PATH, "wb");
    if (out == NULL) {
        bprintf(PRINT_IMPORTANT,
            _T("[FBNeo] PS3 mask cache disabled: cannot create backing file\n"));
        return 0;
    }

    size_t written =
        fwrite(PGMSPRMaskROM, 1, nPGMSPRMaskROMLen, out);

    fflush(out);
    fclose(out);

    if (written != (size_t)nPGMSPRMaskROMLen) {
        remove(PS3_PGM_MASK_CACHE_PATH);

        bprintf(PRINT_IMPORTANT,
            _T("[FBNeo] PS3 mask cache disabled: short write %lu/%d\n"),
            (unsigned long)written,
            nPGMSPRMaskROMLen);

        return 0;
    }

    g_ps3_mask_cache_file =
        fopen(PS3_PGM_MASK_CACHE_PATH, "rb");

    if (g_ps3_mask_cache_file == NULL) {
        remove(PS3_PGM_MASK_CACHE_PATH);
        return 0;
    }

    /*
     * Backing file now contains the final post-load,
     * post-decrypt, post-patch mask image.
     */
    BurnFree(PGMSPRMaskROM);
    PGMSPRMaskROM = NULL;

    PS3_PGM_MEM_LABEL("PGMMaskPageCache");

    PGMSPRMaskPageCache = (UINT8*)BurnMalloc(
        PGM_PS3_MASK_CACHE_PAGES *
        PGM_PS3_MASK_PAGE_SIZE);

    if (PGMSPRMaskPageCache == NULL) {
        bprintf(PRINT_IMPORTANT,
            _T("[FBNeo] PS3 mask cache allocation failed; restoring full mask ROM\n"));

        return pgm_ps3_mask_cache_restore_full();
    }

    memset(
        PGMSPRMaskPageCache,
        0,
        PGM_PS3_MASK_CACHE_PAGES *
        PGM_PS3_MASK_PAGE_SIZE);

    for (INT32 i = 0;
         i < PGM_PS3_MASK_CACHE_PAGES;
         i++) {
        PGMSPRMaskPageTag[i] = -1;
    }

    g_ps3_mask_cache_misses = 0;
    g_ps3_mask_cache_errors = 0;
    nPGMSPRMaskFileCacheActive = 1;

    bprintf(PRINT_IMPORTANT,
        _T("[FBNeo] PS3 PGM mask file cache enabled: backing=%d cache=%u page=%u slots=%u saved=%u\n"),
        nPGMSPRMaskROMLen,
        (unsigned)(PGM_PS3_MASK_CACHE_PAGES *
                   PGM_PS3_MASK_PAGE_SIZE),
        (unsigned)PGM_PS3_MASK_PAGE_SIZE,
        (unsigned)PGM_PS3_MASK_CACHE_PAGES,
        (unsigned)(nPGMSPRMaskROMLen -
                   PGM_PS3_MASK_CACHE_PAGES *
                   PGM_PS3_MASK_PAGE_SIZE));

    return 0;
#endif
}

static void pgm_ps3_mask_cache_exit()
{
    if (nPGMSPRMaskFileCacheActive) {
        bprintf(PRINT_IMPORTANT,
            _T("[FBNeo] PS3 PGM mask cache stats: misses=%u read_errors=%u\n"),
            g_ps3_mask_cache_misses,
            g_ps3_mask_cache_errors);
    }

    nPGMSPRMaskFileCacheActive = 0;

    if (PGMSPRMaskPageCache != NULL) {
        BurnFree(PGMSPRMaskPageCache);
        PGMSPRMaskPageCache = NULL;
    }

    for (INT32 i = 0;
         i < PGM_PS3_MASK_CACHE_PAGES;
         i++) {
        PGMSPRMaskPageTag[i] = -1;
    }

    pgm_ps3_mask_cache_close_file();
    remove(PS3_PGM_MASK_CACHE_PATH);
}

#ifndef PS3_PGM_COLOR_FILE_CACHE
#define PS3_PGM_COLOR_FILE_CACHE 0
#endif

#define PS3_PGM_COLOR_CACHE_PATH "/dev_hdd0/tmp/fbneo-pgm-color.cache"

UINT8 *PGMSPRColPageCache = NULL;
INT32 PGMSPRColPageTag[PGM_PS3_COLOR_CACHE_PAGES];
#if PS3_PGM_COLOR_CACHE_2WAY
UINT8 PGMSPRColCacheVictim[PGM_PS3_COLOR_CACHE_PAGES / 2];
#endif
INT32 nPGMSPRColFileCacheActive = 0;

static FILE *g_ps3_color_cache_file = NULL;
static UINT32 g_ps3_color_cache_misses = 0;
static UINT32 g_ps3_color_cache_errors = 0;

#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
/* One worker request plus at most one completed page; never expands the 8 MiB cache. */
enum { PGM_PREFETCH_FREE, PGM_PREFETCH_LOADING, PGM_PREFETCH_READY };
static UINT8 g_ps3_color_prefetch_data[2][PGM_PS3_COLOR_PAGE_SIZE] __attribute__((aligned(128)));
static INT32 g_ps3_color_prefetch_state[2];
static UINT32 g_ps3_color_prefetch_page[2];
static INT32 g_ps3_color_prefetch_cancel[2];
static FILE *g_ps3_color_prefetch_file = NULL;
static sys_ppu_thread_t g_ps3_color_prefetch_thread;
static sys_mutex_t g_ps3_color_prefetch_mutex;
static sys_cond_t g_ps3_color_prefetch_cond;
static INT32 g_ps3_color_prefetch_started = 0;
static INT32 g_ps3_color_prefetch_stop = 0;
static INT32 g_ps3_color_prefetch_request = -1;
static UINT32 g_ps3_color_prefetch_last_page;
static INT32 g_ps3_color_prefetch_last_valid = 0;
static UINT32 g_ps3_color_prefetch_seq = 0;
static unsigned int g_ps3_color_prefetch_issued_pending, g_ps3_color_prefetch_hits_pending;
static unsigned int g_ps3_color_prefetch_wasted_pending, g_ps3_color_prefetch_avoided_pending;
static unsigned int g_ps3_color_prefetch_bytes_pending;
static unsigned int g_ps3_color_prefetch_seeks_pending, g_ps3_color_prefetch_reads_pending;
static unsigned long long g_ps3_color_prefetch_io_us_pending;
static UINT32 g_ps3_color_prefetch_pages_pending[64];
static unsigned int g_ps3_color_prefetch_page_count_pending;

static void pgm_ps3_color_prefetch_worker(void *unused)
{
	(void)unused;
	for (;;) {
		sysMutexLock(g_ps3_color_prefetch_mutex, 0);
		while (g_ps3_color_prefetch_request < 0 && !g_ps3_color_prefetch_stop) {
			sysCondWait(g_ps3_color_prefetch_cond, 0);
		}
		if (g_ps3_color_prefetch_stop) {
			sysMutexUnlock(g_ps3_color_prefetch_mutex);
			break;
		}
		INT32 slot = g_ps3_color_prefetch_request;
		g_ps3_color_prefetch_request = -1;
		UINT32 page = g_ps3_color_prefetch_page[slot];
		sysMutexUnlock(g_ps3_color_prefetch_mutex);

		unsigned long long io_start = ps3_perf_now_us();
		int seek_ok = fseek(g_ps3_color_prefetch_file,
			(long)(page << PGM_PS3_COLOR_PAGE_SHIFT), SEEK_SET) == 0;
		size_t got = seek_ok ? fread(g_ps3_color_prefetch_data[slot], 1,
			PGM_PS3_COLOR_PAGE_SIZE, g_ps3_color_prefetch_file) : 0;
		unsigned long long io_us = ps3_perf_now_us() - io_start;

		sysMutexLock(g_ps3_color_prefetch_mutex, 0);
		g_ps3_color_prefetch_seeks_pending += seek_ok ? 1 : 0;
		g_ps3_color_prefetch_reads_pending += seek_ok ? 1 : 0;
		g_ps3_color_prefetch_bytes_pending += (unsigned int)got;
		g_ps3_color_prefetch_io_us_pending += io_us;
		if (g_ps3_color_prefetch_page_count_pending < 64)
			g_ps3_color_prefetch_pages_pending[g_ps3_color_prefetch_page_count_pending++] = page;
		if (got < PGM_PS3_COLOR_PAGE_SIZE)
			memset(g_ps3_color_prefetch_data[slot] + got, 0,
				PGM_PS3_COLOR_PAGE_SIZE - got);
		UINT32 page_offset = page << PGM_PS3_COLOR_PAGE_SHIFT;
		size_t expected = page_offset < (UINT32)nPGMSPRColROMLen ?
			((UINT32)nPGMSPRColROMLen - page_offset < PGM_PS3_COLOR_PAGE_SIZE ?
			 (UINT32)nPGMSPRColROMLen - page_offset : PGM_PS3_COLOR_PAGE_SIZE) : 0;
		if (g_ps3_color_prefetch_cancel[slot] || g_ps3_color_prefetch_stop ||
			!seek_ok || got != expected) {
			g_ps3_color_prefetch_wasted_pending++;
			g_ps3_color_prefetch_state[slot] = PGM_PREFETCH_FREE;
		} else {
			g_ps3_color_prefetch_state[slot] = PGM_PREFETCH_READY;
		}
		sysMutexUnlock(g_ps3_color_prefetch_mutex);
	}
}

static INT32 pgm_ps3_color_prefetch_start(void)
{
	sys_mutex_attr_t ma; sys_cond_attr_t ca;
	sysMutexAttrInitialize(ma); sysCondAttrInitialize(ca);
	if (sysMutexCreate(&g_ps3_color_prefetch_mutex, &ma) != 0) return 0;
	if (sysCondCreate(&g_ps3_color_prefetch_cond, g_ps3_color_prefetch_mutex, &ca) != 0) {
		sysMutexDestroy(g_ps3_color_prefetch_mutex); return 0;
	}
	g_ps3_color_prefetch_file = fopen(PS3_PGM_COLOR_CACHE_PATH, "rb");
	if (!g_ps3_color_prefetch_file) {
		sysCondDestroy(g_ps3_color_prefetch_cond); sysMutexDestroy(g_ps3_color_prefetch_mutex); return 0;
	}
	g_ps3_color_prefetch_stop = 0;
	g_ps3_color_prefetch_request = -1;
	memset(g_ps3_color_prefetch_state, 0, sizeof(g_ps3_color_prefetch_state));
	if (sysThreadCreate(&g_ps3_color_prefetch_thread, pgm_ps3_color_prefetch_worker,
		NULL, 1500, 32 * 1024, THREAD_JOINABLE, (char*)"pgm-color-io") != 0) {
		fclose(g_ps3_color_prefetch_file); g_ps3_color_prefetch_file = NULL;
		sysCondDestroy(g_ps3_color_prefetch_cond); sysMutexDestroy(g_ps3_color_prefetch_mutex); return 0;
	}
	g_ps3_color_prefetch_started = 1;
	return 1;
}

static void pgm_ps3_color_prefetch_stop_worker(void)
{
	if (!g_ps3_color_prefetch_started) return;
	sysMutexLock(g_ps3_color_prefetch_mutex, 0);
	g_ps3_color_prefetch_stop = 1;
	sysCondSignal(g_ps3_color_prefetch_cond);
	sysMutexUnlock(g_ps3_color_prefetch_mutex);
	u64 retval = 0; sysThreadJoin(g_ps3_color_prefetch_thread, &retval);
	fclose(g_ps3_color_prefetch_file); g_ps3_color_prefetch_file = NULL;
	sysCondDestroy(g_ps3_color_prefetch_cond); sysMutexDestroy(g_ps3_color_prefetch_mutex);
	g_ps3_color_prefetch_started = 0;
}
#endif

void pgm_ps3_color_cache_note_page(UINT32 page)
{
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
	if (!g_ps3_color_prefetch_started) return;
	if (sysMutexTryLock(g_ps3_color_prefetch_mutex) != 0) return;
	if (g_ps3_color_prefetch_last_valid && page == g_ps3_color_prefetch_last_page + 1)
		g_ps3_color_prefetch_seq++;
	else {
		g_ps3_color_prefetch_seq = 0;
		for (INT32 i = 0; i < 2; i++) {
			if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_READY) {
				g_ps3_color_prefetch_state[i] = PGM_PREFETCH_FREE;
				g_ps3_color_prefetch_wasted_pending++;
			} else if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_LOADING &&
				g_ps3_color_prefetch_request == i) {
				g_ps3_color_prefetch_request = -1;
				g_ps3_color_prefetch_state[i] = PGM_PREFETCH_FREE;
				g_ps3_color_prefetch_wasted_pending++;
			} else if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_LOADING)
				g_ps3_color_prefetch_cancel[i] = 1;
		}
	}
	g_ps3_color_prefetch_last_page = page;
	g_ps3_color_prefetch_last_valid = 1;
	if (g_ps3_color_prefetch_seq >= 2) {
		UINT32 next = page + 1;
		INT32 found = 0, resident = 0, free_slot = -1, loading = 0;
#if PS3_PGM_COLOR_CACHE_2WAY
		UINT32 next_set = next & ((PGM_PS3_COLOR_CACHE_PAGES / 2) - 1);
		resident = PGMSPRColPageTag[next_set * 2] == (INT32)next ||
			PGMSPRColPageTag[next_set * 2 + 1] == (INT32)next;
#else
		resident = PGMSPRColPageTag[next & PGM_PS3_COLOR_CACHE_MASK] == (INT32)next;
#endif
		for (INT32 i = 0; i < 2; i++) {
			if (g_ps3_color_prefetch_state[i] != PGM_PREFETCH_FREE &&
				g_ps3_color_prefetch_page[i] == next) found = 1;
			if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_FREE) free_slot = i;
			if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_LOADING) loading = 1;
		}
		if (!found && !resident && next < ((UINT32)nPGMSPRColROMLen + PGM_PS3_COLOR_PAGE_SIZE - 1) / PGM_PS3_COLOR_PAGE_SIZE &&
			!loading && g_ps3_color_prefetch_request < 0 && free_slot >= 0) {
			g_ps3_color_prefetch_page[free_slot] = next;
			g_ps3_color_prefetch_cancel[free_slot] = 0;
			g_ps3_color_prefetch_state[free_slot] = PGM_PREFETCH_LOADING;
			g_ps3_color_prefetch_request = free_slot;
			g_ps3_color_prefetch_issued_pending++;
			sysCondSignal(g_ps3_color_prefetch_cond);
		}
	}
	sysMutexUnlock(g_ps3_color_prefetch_mutex);
#else
	(void)page;
#endif
}

#if defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
void pgm_ps3_color_cache_prefetch_drain(void)
{
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
	if (!g_ps3_color_prefetch_started) return;
	if (sysMutexTryLock(g_ps3_color_prefetch_mutex) != 0) return;
	unsigned int issued = g_ps3_color_prefetch_issued_pending;
	unsigned int hits = g_ps3_color_prefetch_hits_pending;
	unsigned int wasted = g_ps3_color_prefetch_wasted_pending;
	unsigned int avoided = g_ps3_color_prefetch_avoided_pending;
	unsigned int bytes = g_ps3_color_prefetch_bytes_pending;
	unsigned int seeks = g_ps3_color_prefetch_seeks_pending;
	unsigned int reads = g_ps3_color_prefetch_reads_pending;
	unsigned long long io_us = g_ps3_color_prefetch_io_us_pending;
	unsigned int pages = g_ps3_color_prefetch_page_count_pending;
	g_ps3_color_prefetch_issued_pending = g_ps3_color_prefetch_hits_pending = 0;
	g_ps3_color_prefetch_wasted_pending = g_ps3_color_prefetch_avoided_pending = 0;
	g_ps3_color_prefetch_bytes_pending = g_ps3_color_prefetch_seeks_pending = 0;
	g_ps3_color_prefetch_reads_pending = g_ps3_color_prefetch_page_count_pending = 0;
	g_ps3_color_prefetch_io_us_pending = 0;
	UINT32 local_pages[64];
	memcpy(local_pages, g_ps3_color_prefetch_pages_pending, pages * sizeof(UINT32));
	sysMutexUnlock(g_ps3_color_prefetch_mutex);
	#if defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	ps3_perf_record_prefetch(issued, hits, wasted, bytes, avoided);
	ps3_perf_record_prefetch_io(seeks, reads, bytes, io_us);
	for (unsigned int i = 0; i < pages; i++) ps3_perf_record_cache_page(PS3_PERF_COLOR_CACHE, local_pages[i]);
	#else
	(void)issued; (void)hits; (void)wasted; (void)bytes; (void)avoided;
	(void)seeks; (void)reads; (void)io_us; (void)local_pages; (void)pages;
	#endif
#endif
}
#endif

UINT8 *pgm_ps3_color_cache_miss(UINT32 page)
{
#if PS3_PGM_COLOR_CACHE_2WAY
    UINT32 set = page & ((PGM_PS3_COLOR_CACHE_PAGES / 2) - 1);
    UINT32 way = PGMSPRColCacheVictim[set] & 1;
    UINT32 slot = set * 2 + way;
#else
    UINT32 slot = page & PGM_PS3_COLOR_CACHE_MASK;
#endif

    UINT8 *dst =
        PGMSPRColPageCache +
        (slot << PGM_PS3_COLOR_PAGE_SHIFT);

    long file_offset =
        (long)(page << PGM_PS3_COLOR_PAGE_SHIFT);
    unsigned long long io_start = ps3_perf_now_us();

    g_ps3_color_cache_misses++;

#if PS3_PGM_COLOR_CACHE_2WAY
    if (PGMSPRColPageTag[slot] >= 0)
        ps3_perf_record_color_eviction(1);
    PGMSPRColCacheVictim[set] = (UINT8)(way ^ 1);
#else
    if (PGMSPRColPageTag[slot] >= 0)
        ps3_perf_record_color_eviction(0);
#endif

#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
	/* Never wait for the worker: a ready page is copied only under a try-lock. */
	if (g_ps3_color_prefetch_started &&
		sysMutexTryLock(g_ps3_color_prefetch_mutex) == 0) {
		for (INT32 i = 0; i < 2; i++) {
			if (g_ps3_color_prefetch_state[i] == PGM_PREFETCH_READY &&
				g_ps3_color_prefetch_page[i] == page) {
				memcpy(dst, g_ps3_color_prefetch_data[i], PGM_PS3_COLOR_PAGE_SIZE);
				g_ps3_color_prefetch_state[i] = PGM_PREFETCH_FREE;
				g_ps3_color_prefetch_hits_pending++;
				g_ps3_color_prefetch_avoided_pending++;
				sysMutexUnlock(g_ps3_color_prefetch_mutex);
				PGMSPRColPageTag[slot] = (INT32)page;
				return dst;
			}
		}
		sysMutexUnlock(g_ps3_color_prefetch_mutex);
	}
#endif

    if (g_ps3_color_cache_file == NULL) {

        memset(dst, 0, PGM_PS3_COLOR_PAGE_SIZE);
        g_ps3_color_cache_errors++;
        ps3_perf_record_cache_io(PS3_PERF_COLOR_CACHE, 0, 0, 0,
            ps3_perf_now_us() - io_start);
        PGMSPRColPageTag[slot] = (INT32)page;
        return dst;
    }

    if (fseek(g_ps3_color_cache_file, file_offset, SEEK_SET) != 0) {
        memset(dst, 0, PGM_PS3_COLOR_PAGE_SIZE);
        g_ps3_color_cache_errors++;
        ps3_perf_record_cache_io(PS3_PERF_COLOR_CACHE, 1, 0, 0,
            ps3_perf_now_us() - io_start);
        PGMSPRColPageTag[slot] = (INT32)page;
        return dst;
    }

    size_t got =
        fread(dst, 1,
              PGM_PS3_COLOR_PAGE_SIZE,
              g_ps3_color_cache_file);
    ps3_perf_record_cache_io(PS3_PERF_COLOR_CACHE, 1, 1, got,
        ps3_perf_now_us() - io_start);
    ps3_perf_record_cache_page(PS3_PERF_COLOR_CACHE, page);

    if (got < PGM_PS3_COLOR_PAGE_SIZE) {
        memset(
            dst + got,
            0,
            PGM_PS3_COLOR_PAGE_SIZE - got);

        if ((UINT32)file_offset + (UINT32)got <
            (UINT32)nPGMSPRColROMLen) {
            g_ps3_color_cache_errors++;
        }
    }

    PGMSPRColPageTag[slot] = (INT32)page;
    return dst;
}

static void pgm_ps3_color_cache_close_file()
{
    if (g_ps3_color_cache_file != NULL) {
        fclose(g_ps3_color_cache_file);
        g_ps3_color_cache_file = NULL;
    }
}

static INT32 pgm_ps3_color_cache_restore_full()
{
    PS3_PGM_MEM_LABEL("PGMSPRColROM");

    PGMSPRColROM = (UINT8*)BurnMalloc(
        nPGMSPRColROMLen + 128);

    if (PGMSPRColROM == NULL) {
        bprintf(
            PRINT_ERROR,
            _T("[FBNeo] PS3 color cache fallback: unable to restore full color ROM\n"));
        return 1;
    }

    if (fseek(g_ps3_color_cache_file, 0, SEEK_SET) != 0 ||
        fread(PGMSPRColROM, 1,
              nPGMSPRColROMLen,
              g_ps3_color_cache_file) !=
              (size_t)nPGMSPRColROMLen) {

        bprintf(
            PRINT_ERROR,
            _T("[FBNeo] PS3 color cache fallback: reload failed\n"));
        return 1;
    }

    memset(
        PGMSPRColROM + nPGMSPRColROMLen,
        0,
        128);

    pgm_ps3_color_cache_close_file();
    remove(PS3_PGM_COLOR_CACHE_PATH);

    bprintf(
        PRINT_IMPORTANT,
        _T("[FBNeo] PS3 color cache fallback: restored full color ROM\n"));

    return 0;
}

static INT32 pgm_ps3_color_cache_build()
{
#if !PS3_PGM_COLOR_FILE_CACHE
    return 0;
#else
#ifdef __LIBRETRO__
    if (!ps3_pgm_hdd_cache_requested) {
        bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 PGM color HDD cache OFF: keeping ROM in RAM\n"));
        return 0;
    }
#endif
    /*
     * Generic PS3 PGM packed sprite-color cache.
     * Any unpacked-color path remains resident.
     */
    if (!nPGMSPRColPacked ||
        PGMSPRColROM == NULL) {
        return 0;
    }

    const UINT32 cache_bytes =
        PGM_PS3_COLOR_CACHE_PAGES *
        PGM_PS3_COLOR_PAGE_SIZE;

    /*
     * Avoid paying file-I/O cost for a tiny memory saving.
     * Require at least 4 MiB of resident-memory reduction.
     */
    if ((UINT32)nPGMSPRColROMLen <
        cache_bytes + (4u * 1024u * 1024u)) {
        return 0;
    }

    FILE *out =
        fopen(PS3_PGM_COLOR_CACHE_PATH, "wb");

    if (out == NULL) {
        bprintf(
            PRINT_IMPORTANT,
            _T("[FBNeo] PS3 color cache disabled: cannot create backing file\n"));
        return 0;
    }

    size_t written =
        fwrite(
            PGMSPRColROM,
            1,
            nPGMSPRColROMLen,
            out);

    fflush(out);
    fclose(out);

    if (written != (size_t)nPGMSPRColROMLen) {
        remove(PS3_PGM_COLOR_CACHE_PATH);

        bprintf(
            PRINT_IMPORTANT,
            _T("[FBNeo] PS3 color cache disabled: short write %lu/%d\n"),
            (unsigned long)written,
            nPGMSPRColROMLen);

        return 0;
    }

    g_ps3_color_cache_file =
        fopen(PS3_PGM_COLOR_CACHE_PATH, "rb");

    if (g_ps3_color_cache_file == NULL) {
        remove(PS3_PGM_COLOR_CACHE_PATH);
        return 0;
    }

    /*
     * The file contains the final packed, decrypted color image.
     * From this point forward PS3 drawing goes through the accessor.
     */
    BurnFree(PGMSPRColROM);
    PGMSPRColROM = NULL;

    PS3_PGM_MEM_LABEL("PGMColorPageCache");

    PGMSPRColPageCache =
        (UINT8*)BurnMalloc(cache_bytes);

    if (PGMSPRColPageCache == NULL) {
        bprintf(
            PRINT_IMPORTANT,
            _T("[FBNeo] PS3 color cache allocation failed; restoring full color ROM\n"));

        return pgm_ps3_color_cache_restore_full();
    }

    memset(
        PGMSPRColPageCache,
        0,
        cache_bytes);

    for (INT32 i = 0;
         i < PGM_PS3_COLOR_CACHE_PAGES;
         i++) {
        PGMSPRColPageTag[i] = -1;
    }
#if PS3_PGM_COLOR_CACHE_2WAY
    memset(PGMSPRColCacheVictim, 0, sizeof(PGMSPRColCacheVictim));
#endif

    g_ps3_color_cache_misses = 0;
    g_ps3_color_cache_errors = 0;
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
	g_ps3_color_prefetch_last_valid = 0;
	g_ps3_color_prefetch_seq = 0;
	if (!pgm_ps3_color_prefetch_start())
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 color cache: async read-ahead unavailable; using synchronous reads\n"));
#endif
    nPGMSPRColFileCacheActive = 1;

    bprintf(
        PRINT_IMPORTANT,
        _T("[FBNeo] PS3 PGM color file cache enabled: backing=%d cache=%u page=%u slots=%u sets=%u ways=%u saved=%u\n"),
        nPGMSPRColROMLen,
        (unsigned)cache_bytes,
        (unsigned)PGM_PS3_COLOR_PAGE_SIZE,
        (unsigned)PGM_PS3_COLOR_CACHE_PAGES,
#if PS3_PGM_COLOR_CACHE_2WAY
        (unsigned)(PGM_PS3_COLOR_CACHE_PAGES / 2),
        2u,
#else
        (unsigned)PGM_PS3_COLOR_CACHE_PAGES,
        1u,
#endif
        (unsigned)(nPGMSPRColROMLen -
                   cache_bytes));

    return 0;
#endif
}

static void pgm_ps3_color_cache_exit()
{
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
	pgm_ps3_color_prefetch_stop_worker();
#endif
    if (nPGMSPRColFileCacheActive) {
        bprintf(
            PRINT_IMPORTANT,
            _T("[FBNeo] PS3 PGM color cache stats: misses=%u read_errors=%u\n"),
            g_ps3_color_cache_misses,
            g_ps3_color_cache_errors);
    }

    nPGMSPRColFileCacheActive = 0;

    if (PGMSPRColPageCache != NULL) {
        BurnFree(PGMSPRColPageCache);
        PGMSPRColPageCache = NULL;
    }

    for (INT32 i = 0;
         i < PGM_PS3_COLOR_CACHE_PAGES;
         i++) {
        PGMSPRColPageTag[i] = -1;
    }

    pgm_ps3_color_cache_close_file();
    remove(PS3_PGM_COLOR_CACHE_PATH);
}

#endif

UINT8 nPgmPalRecalc = 0;
static INT32 nPgmCurrentBios = -1;

void (*pPgmResetCallback)() = NULL;
void (*pPgmInitCallback)() = NULL;
void (*pPgmTileDecryptCallback)(UINT8 *gfx, INT32 len) = NULL;
void (*pPgmColorDataDecryptcallback)(UINT8 *gfx, INT32 len) = NULL;
void (*pPgmProtCallback)() = NULL;
INT32 (*pPgmScanCallback)(INT32, INT32*) = NULL;

static INT32 nEnableArm7 = 0;
INT32 nPGMDisableIRQ4 = 0;
INT32 nPGMArm7Type = 0;
UINT32 nPgmAsicRegionHackAddress = 0;
INT32 nPGMSpriteBufferHack = 0;
INT32 nPGMMapperHack = 0;
INT32 OldCodeMode = 0;

#define Z80_FREQ	8467200

#define M68K_CYCS_PER_FRAME	((20000000 * 100) / nBurnFPS)
#define ARM7_CYCS_PER_FRAME	((20000000 * 100) / nBurnFPS)
#define Z80_CYCS_PER_FRAME	((Z80_FREQ * 100) / nBurnFPS)

static INT32 nCyclesDone[3];
static INT32 nCyclesTotal[3];

static INT32 pgmMemIndex()
{
	UINT8 *Next; Next = Mem;
	PGM68KBIOS			= Next;	Next += 0x0080000;
	PGM68KROM			= Next;	Next += nPGM68KROMLen;

	PGMUSER0			= Next;	Next += nPGMExternalARMLen;

	PGMProtROM			= PGMUSER0 + 0x10000; // Olds, Killbld, drgw3

	PGMARMROM			= Next;	Next += (bDoIpsPatch || (0 != nPGMSpriteBufferHack)) ? 0x0008000 : 0x0004000;	// Just always allocate this - only 16kb

	RamCurPal			= (UINT32 *) Next;
	if (OldCodeMode)			Next += (0x0001204 / 2) * sizeof(UINT32);
	else						Next += (0x0002004 / 2) * sizeof(UINT32);

	RamStart			= Next;

	PGM68KRAM			= Next;	Next += 0x0020000;
	RamZ80				= Next;	Next += 0x0010000;

	if (nEnableArm7) {
		PGMARMShareRAM	= Next;	Next += 0x0020000;
		if (OldCodeMode)		Next -= 0x0010000;
		PGMARMShareRAM2	= Next;	Next += 0x0020000;
		if (OldCodeMode)		Next -= 0x0010000;
		PGMARMRAM0		= Next;	Next += 0x0001000; // minimum page size in arm7 is 0x1000
		PGMARMRAM1		= Next;	Next += 0x0040000;
		PGMARMRAM2		= Next;	Next += 0x0001000; // minimum page size in arm7 is 0x1000
	}

	PGMZoomRAM			= (UINT16 *) Next; 	Next += 0x0000040;

	PGMBgRAM			= (UINT32 *) Next;	Next += 0x0001000;
	PGMTxtRAM			= (UINT32 *) Next;	Next += 0x0002000;

	PGMRowRAM			= (UINT16 *) Next;	Next += 0x0001000;	// Row Scroll
	PGMPalRAM			= (UINT16 *) Next;	Next += 0x0002000;	// Palette R5G5B5
	if (OldCodeMode)						Next -= 0x0000c00;
	PGMVidReg			= (UINT16 *) Next;	Next += 0x0010000;	// Video Regs inc. Zoom Table
	PGMSprBuf			= (UINT16 *) Next;	Next += 0x0001000;

	RamEnd				= Next;

	MemEnd				= Next;

	return 0;
}

static INT32 pgmGetRoms(bool bLoad)
{
	INT32 kov2 = (strncmp(BurnDrvGetTextA(DRV_NAME), "kov2", 4) == 0) ? 1 : 0;

	char* pRomName;
	struct BurnRomInfo ri;
	struct BurnRomInfo pi;

	UINT8 *PGM68KROMLoad = PGM68KROM;
	UINT8 *PGMUSER0Load = PGMUSER0;
	UINT8 *PGMTileROMLoad = PGMTileROM + 0x180000;
	UINT8 *PGMSPRMaskROMLoad = PGMSPRMaskROM;
	UINT8 *PGMSNDROMLoad = ICSSNDROM + (kov2 ? 0x800000 : 0x400000);
#ifdef __PS3__
	// Physical KOV2 layout is compact on PS3: BIOS 2 MiB followed immediately by game samples.
	// The ICS2115 reader remaps logical >= 0x800000 back by 0x600000.
	if (bLoad && pgm_ps3_kov2_compact_sound()) {
		PGMSNDROMLoad = ICSSNDROM + 0x200000;
	}
#endif

	if (bLoad) {
		if (nPGM68KROMLen == 0x80000 && nPGMSNDROMLen == 0x600000) { // dw2001 & dwpc
			PGMSNDROMLoad -= 0x200000;
		} else if ((!bDoIpsPatch && ((0 == strcmp(BurnDrvGetTextA(DRV_NAME), "kov2pshjz")) || (0 == strcmp(BurnDrvGetTextA(DRV_NAME), "kov2dzxx")))) || (nIpsDrvDefine & IPS_PGM_SNDOFFS)) { // kov2pshjz, kov2dzxx
			PGMSNDROMLoad -= 0x600000;
		}
	}

	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {

	//	bprintf (0, _T("Loading ROM #%d\n"), i);

		BurnDrvGetRomInfo(&ri, i);

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 1)
		{
			if (bLoad) {
				BurnDrvGetRomInfo(&pi, i+1);

				if (ri.nLen == 0x80000 && pi.nLen == 0x80000)
				{
					BurnLoadRom(PGM68KROMLoad + 0, i + 0, 2);
					BurnLoadRom(PGM68KROMLoad + 1, i + 1, 2);
					PGM68KROMLoad += pi.nLen;
					i += 1;
				} else {
					BurnLoadRom(PGM68KROMLoad, i, 1);
				}
			}
			PGM68KROMLoad += ri.nLen;
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 2)
		{
			if (bLoad) BurnLoadRom(PGMTileROMLoad, i, 1);
			PGMTileROMLoad += ri.nLen;
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 3)
		{
			if (!bLoad) nPGMSPRColROMLen += ri.nLen;
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 4)
		{
			if ((PGMSPRMaskROMLoad - PGMSPRMaskROM) == 0x1000000 && ri.nLen == 0x200000) { // pgm3in1
				PGMSPRMaskROMLoad -= 0x100000;
			}

			if (bLoad) BurnLoadRom(PGMSPRMaskROMLoad, i, 1);
			PGMSPRMaskROMLoad += ri.nLen;

			if ((PGMSPRMaskROMLoad - PGMSPRMaskROM) == 0x1000000 && ri.nLen == 0x200000) { // pgm3in1
				PGMSPRMaskROMLoad -= 0x100000;
			}

			continue;
		}

		if ((ri.nType & BRF_SND) && (ri.nType & 0x0f) == 5)
		{
			if (bLoad) BurnLoadRom(PGMSNDROMLoad, i, 1);
			PGMSNDROMLoad += ri.nLen;
			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 7)
		{
			if (bLoad) {
				if (nEnableArm7) {
					BurnLoadRom(PGMARMROM + ((ri.nLen == 0x3e78) ? 0x188 : 0), i, 1);
				}
			}
			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 8)
		{
			if (nEnableArm7) {
				if (bLoad) BurnLoadRom(PGMUSER0Load, i, 1);
				PGMUSER0Load += ri.nLen;
			}
			continue;
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 9)
		{
			if (bLoad) {
				BurnLoadRom(PGMProtROM, i, 1);
			}
		}

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 0xa)
		{ // nvram
			if (bLoad) {
				BurnLoadRom(PGM68KRAM, i, 1);
			}
		}
	}

	if (!bLoad) {
		nPGM68KROMLen = PGM68KROMLoad - PGM68KROM;

		nPGMTileROMLen = PGMTileROMLoad - PGMTileROM;
		if (nPGMTileROMLen < 0x400000) nPGMTileROMLen = 0x400000;

		nPGMSPRMaskROMLen = PGMSPRMaskROMLoad - PGMSPRMaskROM;

		nPGMSNDROMLen = (((PGMSNDROMLoad - ICSSNDROM) - 1) | 0xfffff) + 1;

		// Round the soundrom length to the next power of 2 so the soundcore can make a proper mask from it.
		UINT32 Pages = 0;
		for (Pages = 1; Pages < (UINT32)nPGMSNDROMLen; Pages <<= 1); // Calculate nearest power of 2 of len
		//bprintf(0, _T("pgm_run: sndlen %x  pow2 %x\n"), nPGMSNDROMLen, Pages);
		nPGMSNDROMLen = Pages;

		nPGMExternalARMLen = (PGMUSER0Load - PGMUSER0) + 0x100000;

		if (bDoIpsPatch) {
			nPGM68KROMLen		+= nIpsMemExpLen[PRG1_ROM];
			nPGMExternalARMLen	+= nIpsMemExpLen[PRG2_ROM];
			nPGMTileROMLen		+= nIpsMemExpLen[GRA1_ROM];
			nPGMSNDROMLen		+= nIpsMemExpLen[SND1_ROM];
			nPGMSPRColROMLen	+= nIpsMemExpLen[GRA2_ROM];
			nPGMSPRMaskROMLen	+= nIpsMemExpLen[GRA3_ROM];
		}

	//	bprintf (0, _T("68k: %x, tile: %x, sprmask: %x, sndrom: %x, arm7: %x\n"), nPGM68KROMLen, nPGMTileROMLen, nPGMSPRMaskROMLen, nPGMSNDROMLen, nPGMExternalARMLen);
	}

	return 0;
}

static inline void pgmSynchroniseZ80(INT32 extra_cycles)
{
	INT32 cycles = (UINT64)(SekTotalCycles()) * nCyclesTotal[1] / nCyclesTotal[0] + extra_cycles;

	if (cycles <= ZetTotalCycles())
		return;

	INT32 i = 0;
	while (ZetTotalCycles() < cycles && i++ < 5)
		BurnTimerUpdate(cycles);
}

static UINT16 ics2115_soundlatch_r(INT32 i)
{
	bSoundlatchRead[i] = 1;
	return nSoundlatch[i];
}

static void ics2115_soundlatch_w(INT32 i, UINT16 d)
{
	nSoundlatch[i] = d;
	bSoundlatchRead[i] = 0;
}

static inline INT32 get_current_scanline()
{
	UINT32 ret = (SekTotalCycles() * 264) / (M68K_CYCS_PER_FRAME);

	return (ret > 263) ? 263 : ret;
}

static void __fastcall PgmVideoControllerWriteWord(UINT32 sekAddress, UINT16 wordValue)
{
	switch (sekAddress & 0x0f000)
	{
		case 0x0000: PGMSprBuf[(sekAddress >> 1) & 0x7ff] = wordValue; bprintf(0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // Sprite buffer is not writeable by the 68K, but the BIOS tries anyway
		case 0x1000: PGMZoomRAM[(sekAddress >> 1) & 0x1f] = wordValue; break; // size is guessed
		case 0x2000: pgm_bg_scrolly = wordValue; break;
		case 0x3000: pgm_bg_scrollx = wordValue; break;
		case 0x4000: /*bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue);*/ pgm_unk_video_flags = wordValue; break; // 0610 is always written, but changing this seems to have no effect
		case 0x5000: pgm_fg_scrolly = wordValue; break;
		case 0x6000: pgm_fg_scrollx = wordValue; break;
		case 0x7000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0x8000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0x9000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0xa000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0xb000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0xc000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0xd000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
		case 0xe000: pgm_video_control = wordValue; break;
		case 0xf000: bprintf (0, _T("VideoController write word: %5.5x, %4.4x\n"), sekAddress, wordValue); break; // ?
	}
}

static void __fastcall PgmVideoControllerWriteByte(UINT32 sekAddress, UINT8 byteValue)
{
	bprintf (0, _T("VideoController Write Byte: %5.5x, %2.2x PC(%5.5x)\n"), sekAddress, byteValue, SekGetPC(-1));
}

static UINT16 __fastcall PgmVideoControllerReadWord(UINT32 sekAddress)
{
	bprintf (0, _T("VideoController Read Word: %5.5x, PC(%5.5x)\n"), sekAddress, SekGetPC(-1));

	// ddp2 seems to read from the sprite buffer?
	switch (sekAddress & 0x0f000)
	{
		case 0x0000: return PGMSprBuf[(sekAddress >> 1) & 0x7ff];
		case 0x1000: return 0; // zoom ram is not readable by the 68K
		case 0x2000: return pgm_bg_scrolly;
		case 0x3000: return pgm_bg_scrollx;
		case 0x4000: return pgm_unk_video_flags;
		case 0x5000: return pgm_fg_scrolly;
		case 0x6000: return pgm_fg_scrollx;
		case 0x7000: return get_current_scanline(); // scanline counter? 0 - 107
		case 0x8000: return 0; // ?
		case 0x9000: return 0; // ?
		case 0xa000: return 0; // ?
		case 0xb000: return 0; // ?
		case 0xc000: return 0; // accesses here cause video to refresh?
		case 0xd000: return 0; // accesses here cause video to refresh?
		case 0xe000: return pgm_video_control;
		case 0xf000: return 0;
	}
	
	return 0;
}

static UINT8 __fastcall PgmVideoControllerReadByte(UINT32 sekAddress)
{
	switch (sekAddress & 0x0f000)
	{
		case 0x0000:
			return PGMSprBuf[(sekAddress >> 1) & 0x7ff] >> ((~sekAddress & 1) * 8);
	}

	bprintf (0, _T("VideoController Read Byte: %5.5x, PC(%5.5x)\n"), sekAddress, SekGetPC(-1));

	return 0;
}

static UINT8 __fastcall PgmReadByte(UINT32 sekAddress)
{
	UINT32 u32Addr = sekAddress;
	if (!OldCodeMode) u32Addr &= ~0xe7ff8;

	switch (u32Addr)
	{
		case 0xC00007:
			return v3021Read();

		case 0xC08007: // dipswitches - (ddp2)
			return ~(PgmInput[6]);// | 0xe0;

	//	default:
	//		bprintf(PRINT_NORMAL, _T("Attempt to read byte value of location %x (PC: %5.5x)\n"), sekAddress, SekGetPC(-1));
	}

	return 0;
}

static UINT16 __fastcall PgmReadWord(UINT32 sekAddress)
{
	UINT32 u32Addr = sekAddress;
	if (!OldCodeMode) u32Addr &= ~0xe7ff8;

	switch (u32Addr)
	{
		case 0xC00004:
			pgmSynchroniseZ80(0);
			return ics2115_soundlatch_r(1);
			
		case 0xC00006:	// ketsui wants this
			return v3021Read();

		case 0xC08000:	// p1+p2 controls
			return ~(PgmInput[0] | (PgmInput[1] << 8));

		case 0xC08002:  // p3+p4 controls
			return ~(PgmInput[2] | (PgmInput[3] << 8));

		case 0xC08004:  // extra controls
			return ~(PgmInput[4] | (PgmInput[5] << 8));

		case 0xC08006: // dipswitches
			return ~(PgmInput[6]) | 0xff00; // 0xffe0;

	//	default:
	//		bprintf(PRINT_NORMAL, _T("Attempt to read word value of location %x (PC: %5.5x)\n"), sekAddress, SekGetPC(-1));
	}

	return 0;
}

static void __fastcall PgmWriteByte(UINT32 sekAddress, UINT8 byteValue)
{
	byteValue=byteValue; // fix warning

	switch (sekAddress)
	{
	//	default:
	//		bprintf(PRINT_NORMAL, _T("Attempt to write byte value %x to location %x (PC: %5.5x)\n"), byteValue, sekAddress, SekGetPC(-1));
	}
}

static void __fastcall PgmWriteWord(UINT32 sekAddress, UINT16 wordValue)
{
	static INT32 coin_counter_previous;

	switch (sekAddress)
	{
		case 0x700006:	// Watchdog?
			break;
	}

	UINT32 u32Addr = sekAddress;
	if (!OldCodeMode) u32Addr &= ~0xe7ff0;
	
	switch (u32Addr)
	{
		case 0xC00002:
			pgmSynchroniseZ80(0);
			ics2115_soundlatch_w(0, wordValue);
			ZetNmi();
			break;

		case 0xC00004:
			pgmSynchroniseZ80(0);
			ics2115_soundlatch_w(1, wordValue);
			break;

		case 0xC00006:
			v3021Write(wordValue);
			break;

		case 0xC00008:
			pgmSynchroniseZ80(0);

			if (wordValue == 0x5050)
			{
				ics2115_reset();
				ZetSetBUSREQLine(0);
				ZetReset();
			} else {
				ZetSetBUSREQLine(1);
			}

			break;

		case 0xC0000A:	// z80 controller
			if (!OldCodeMode) {
				if (wordValue == 0x45d3) pgm_z80_connect_bus = 1;
				if (wordValue == 0x0a0a) pgm_z80_connect_bus = 0;
			}
			break;

		case 0xC0000C:
			pgmSynchroniseZ80(0);
			ics2115_soundlatch_w(2, wordValue);
			break;

		case 0xC08006: // coin counter
			if (coin_counter_previous == 0xf && wordValue == 0) {
			//	bprintf (0, _T("increment coin counter!\n"));
			}
			coin_counter_previous = wordValue & 0x0f;
			break;

	//	default:
	//		bprintf(PRINT_NORMAL, _T("Attempt to write word value %x to location %x (PC: %5.5x)\n"), wordValue, sekAddress, SekGetPC(-1));
	}
}

static UINT8 __fastcall PgmZ80ReadByte(UINT32 sekAddress)
{
	switch (sekAddress)
	{
		default:
			bprintf(PRINT_NORMAL, _T("Attempt to read byte value of location %x\n"), sekAddress);
	}

	return 0;
}

static UINT16 __fastcall PgmZ80ReadWord(UINT32 sekAddress)
{
	pgmSynchroniseZ80(0);

	if (!OldCodeMode)
		if (pgm_z80_connect_bus == 0) return 0;

	sekAddress &= 0xffff;
	return (RamZ80[sekAddress] << 8) | RamZ80[sekAddress + 1];
}

static void __fastcall PgmZ80WriteByte(UINT32 sekAddress, UINT8 byteValue)
{
	switch (sekAddress)
	{
		default:
			bprintf(PRINT_NORMAL, _T("Attempt to write byte value (%2.2x) of location %x\n"), byteValue, sekAddress);
	}
}

static void __fastcall PgmZ80WriteWord(UINT32 sekAddress, UINT16 wordValue)
{
	pgmSynchroniseZ80(0);

	if (!OldCodeMode)
		if (pgm_z80_connect_bus == 0) return;

	sekAddress &= 0xffff;
	RamZ80[sekAddress    ] = wordValue >> 8;
	RamZ80[sekAddress + 1] = wordValue & 0xFF;
}

inline static UINT32 CalcCol(UINT16 nColour)
{
	INT32 r, g, b;

	r = (nColour & 0x7C00) >> 7;	// Red
	r |= r >> 5;
	g = (nColour & 0x03E0) >> 2;	// Green
	g |= g >> 5;
	b = (nColour & 0x001F) << 3;	// Blue
	b |= b >> 5;

	return BurnHighCol(r, g, b, 0);
}

static void __fastcall PgmPaletteWriteWord(UINT32 sekAddress, UINT16 wordValue)
{
	sekAddress = (sekAddress & 0x001ffe) >> 1;

	PGMPalRAM[sekAddress] = BURN_ENDIAN_SWAP_INT16(wordValue);
	RamCurPal[sekAddress] = CalcCol(wordValue);
}

static void __fastcall PgmPaletteWriteByte(UINT32 sekAddress, UINT8 byteValue)
{
	sekAddress &= 0x001fff;

	UINT8 *pal = (UINT8*)PGMPalRAM;
	pal[sekAddress ^ 1] = byteValue;
	RamCurPal[sekAddress >> 1] = CalcCol(PGMPalRAM[sekAddress >> 1]);
}

static UINT8 __fastcall PgmZ80PortRead(UINT16 port)
{
	switch (port >> 8)
	{
		case 0x80:
			return ics2115read(port & 0xff);

		case 0x81:
			return ics2115_soundlatch_r(2) & 0xff;

		case 0x82:
			return ics2115_soundlatch_r(0) & 0xff;

		case 0x84:
			return ics2115_soundlatch_r(1) & 0xff;

//		default:
//			bprintf(PRINT_NORMAL, _T("Z80 Attempt to read port %04x\n"), port);
	}
	return 0;
}

static void __fastcall PgmZ80PortWrite(UINT16 port, UINT8 data)
{
	switch (port >> 8)
	{
		case 0x80:
			ics2115write(port & 0xff, data);
			break;

		case 0x81:
			ics2115_soundlatch_w(2, data);
			break;

		case 0x82:
			ics2115_soundlatch_w(0, data);
			break;	

		case 0x84:
			ics2115_soundlatch_w(1, data);
			break;

//		default:
//			bprintf(PRINT_NORMAL, _T("Z80 Attempt to write %02x to port %04x\n"), data, port);
	}
}

static void OrlegendRegionHack()
{
	const char* ol_name[] = {
		"orlegend",
		"orlegende",
		"orlegendea",
		"orlegendc",
		"orlegendca",
		"orlegend105k",
		"orlegend105t",
		"orlegend111c",
		"orlegend111k",
		"orlegend111t"
	};

	UINT32 reg_addr[][5] = {
		{ 0x046ae4, 0x0d1725, 0x0d1749, 0x0d176d, 0x0d177f },	// orlegend
		{ 0x046af4, 0x0d1735, 0x0d1759, 0x0d177d, 0x0d178f },	// orlegende
		{ 0x046af4, 0x0d15ab, 0x0d15cf, 0x0d15f3, 0x0d1605 },	// orlegendea
		{ 0x046af4, 0x0d15ab, 0x0d15cf, 0x0d15f3, 0x0d1605 },	// orlegendc
		{ 0x046aba, 0x0459fc, 0x045a00, 0x000000, 0x000000 },	// orlegendca
		{ 0x046450, 0x0d0e47, 0x000000, 0x000000, 0x000000 },	// orlegend105k
		{ 0x046450, 0x0d0e11, 0x000000, 0x000000, 0x000000 },	// orlegend105t
		{ 0x0468b2, 0x0457f4, 0x0457f8, 0x000000, 0x000000 },	// orlegend111c
		{ 0x0468a8, 0x0d12bd, 0x000000, 0x000000, 0x000000 },	// orlegend111k
		{ 0x0468a8, 0x0d1287, 0x000000, 0x000000, 0x000000 },	// orlegend111t
	};

	for (INT32 nGame = 0; nGame < sizeof(ol_name) / sizeof(char*); nGame++) {
		if (0 == strcmp(BurnDrvGetTextA(DRV_NAME), ol_name[nGame])) {
			*((UINT16*)(PGM68KROM + reg_addr[nGame][0] + 0)) = BURN_ENDIAN_SWAP_INT16(0x4e71);
			*((UINT16*)(PGM68KROM + reg_addr[nGame][0] + 2)) = BURN_ENDIAN_SWAP_INT16(0x4e71);

			for (INT32 nAddr = 1; nAddr <= 4; nAddr++) {
				if (0x000000 != reg_addr[nGame][nAddr]) {
					PGM68KROM[reg_addr[nGame][nAddr]] = PgmInput[7];
				}
			}
		}
	}
}

static INT32 PgmDoReset()
{
	if (nPgmCurrentBios != PgmInput[8]) {	// Load the 68k bios
		if (!(BurnDrvGetHardwareCode() & HARDWARE_IGS_JAMMAPCB)) {
			nPgmCurrentBios = PgmInput[8];
			BurnLoadRom(PGM68KBIOS, 0x00082 + nPgmCurrentBios, 1);	// 68k bios
		}
	}

	SekReset(0);

	if (nEnableArm7) {
		Arm7Open(0);
		Arm7Reset();
		Arm7Close();

		// region hack
		if (strncmp(BurnDrvGetTextA(DRV_NAME), "dmnfrnt", 7) == 0) {
			PGMARMShareRAM[0x158] = PgmInput[7];
			PGMARMShareRAM2[0x158] = PgmInput[7];

			// dmnfrntpcb - requires this - set internal rom version
			PGMARMShareRAM[0x164] = '1'; // S101KR (101 Korea) - $69be8 in ROM
			PGMARMShareRAM[0x165] = 'S';
			PGMARMShareRAM[0x166] = '1';
			PGMARMShareRAM[0x167] = '0';
			PGMARMShareRAM[0x168] = 'R';
			PGMARMShareRAM[0x169] = 'K';
		} else if (nPgmAsicRegionHackAddress) {
			PGMARMROM[nPgmAsicRegionHackAddress] = PgmInput[7];
		}
	}

	ZetOpen(0);
	ZetSetBUSREQLine(0);
	ZetReset();
	ZetClose();

	ics2115_reset();

	if (pPgmResetCallback) {
		pPgmResetCallback();

		// Orlegend series of regions hack
		if (0 == strncmp(BurnDrvGetTextA(DRV_NAME), "orlegend", 8)) {
			OrlegendRegionHack();
		}
	}

	hold_coin.reset();
	clear_opposite.reset();

	nCyclesDone[0] = nCyclesDone[1] = nCyclesDone[2] = 0;

	HiscoreReset();

	pgm_bg_scrollx = 0;
	pgm_bg_scrolly = 0;
	pgm_fg_scrollx = 0;
	pgm_fg_scrolly = 0;
	pgm_video_control = 0;
	pgm_unk_video_flags = 0;
	pgm_z80_connect_bus = 1;

	return 0;
}

static void expand_tile_gfx()
{
#ifdef __PS3__
	UINT8 *shared = PGMTileSharedAlloc;
	UINT8 *src = shared;
	UINT8 *text = shared + nPGMTileROMLen;
	if (pPgmTileDecryptCallback) pPgmTileDecryptCallback(src + 0x180000, nPGMTileROMLen - 0x180000);
	memcpy(text, src, 0x200000);
	for (INT32 i = 0x200000 - 1; i >= 0; i--) {
		INT32 d = text[i];
		text[i * 2 + 0] = d & 0x0f;
		text[i * 2 + 1] = d >> 4;
	}
	// Keep the 5bpp background data packed on PS3. pgm_draw.cpp expands
	// individual 32x32 tiles through a small cache on demand.
	PGMTileROMExp = src;
	PGMTileROM = text;
#else
	UINT8 *src = PGMTileROM;
	UINT8 *dst = PGMTileROMExp;

	if (pPgmTileDecryptCallback) {
		pPgmTileDecryptCallback(PGMTileROM + 0x180000, nPGMTileROMLen - 0x180000);
	}

	for (INT32 i = nPGMTileROMLen/5-1; i >= 0 ; i --) {
		dst[0+8*i] = ((src[0+5*i] >> 0) & 0x1f);
		dst[1+8*i] = ((src[0+5*i] >> 5) & 0x07) | ((src[1+5*i] << 3) & 0x18);
		dst[2+8*i] = ((src[1+5*i] >> 2) & 0x1f );
		dst[3+8*i] = ((src[1+5*i] >> 7) & 0x01) | ((src[2+5*i] << 1) & 0x1e);
		dst[4+8*i] = ((src[2+5*i] >> 4) & 0x0f) | ((src[3+5*i] << 4) & 0x10);
		dst[5+8*i] = ((src[3+5*i] >> 1) & 0x1f );
		dst[6+8*i] = ((src[3+5*i] >> 6) & 0x03) | ((src[4+5*i] << 2) & 0x1c);
		dst[7+8*i] = ((src[4+5*i] >> 3) & 0x1f );
	}

	for (INT32 i = 0x200000-1; i >= 0; i--) {
		INT32 d = PGMTileROM[i];
		PGMTileROM[i * 2 + 0] = d & 0x0f;
		PGMTileROM[i * 2 + 1] = d >> 4;
	}

	PGMTileROM = (UINT8*)BurnRealloc(PGMTileROM, 0x400000);
#endif
}

static INT32 expand_colourdata()
{
	INT32 expandedLen = (nPGMSPRColROMLen / 2) * 3;
	nPGMSPRColMaskLen = 1;
	while (nPGMSPRColMaskLen < expandedLen) nPGMSPRColMaskLen <<= 1;
	nPGMSPRColMaskLen--;
	nPGMSPRMaskMaskLen = 1;
	while (nPGMSPRMaskMaskLen < nPGMSPRMaskROMLen) nPGMSPRMaskMaskLen <<= 1;
	nPGMSPRMaskMaskLen--;

#ifdef __PS3__
	nPGMSPRColPacked = 1;
	PS3_PGM_MEM_LABEL("PGMSPRColROM");
	PGMSPRColROM = (UINT8*)BurnMalloc((nPGMSPRColPacked ? nPGMSPRColROMLen : expandedLen) + 128);
#else
	PGMSPRColROM = (UINT8*)BurnMalloc(expandedLen + 128);
#endif
	if (PGMSPRColROM == NULL) return 1;

	char* pRomName;
	struct BurnRomInfo ri;
	UINT8 *load = PGMSPRColROM;
	INT32 prev_len = 0;
	for (INT32 i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {
		BurnDrvGetRomInfo(&ri, i);
		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 3) {
			if ((ri.nLen >= 0x400000) && (prev_len == 0x400000) && (nPGMSPRColROMLen >= 0x2000000)) load -= 0x200000;
			if (BurnLoadRom(load, i, 1)) return 1;
			load += ri.nLen;
			prev_len = ri.nLen;
		}
	}

	if (pPgmColorDataDecryptcallback) pPgmColorDataDecryptcallback(PGMSPRColROM, nPGMSPRColROMLen);

#ifndef __PS3__
	for (INT32 cnt = nPGMSPRColROMLen / 2 - 1; cnt >= 0; cnt--) {
		UINT16 colpack = PGMSPRColROM[cnt * 2] | (PGMSPRColROM[cnt * 2 + 1] << 8);
		PGMSPRColROM[cnt * 3 + 0] = (colpack >> 0) & 0x1f;
		PGMSPRColROM[cnt * 3 + 1] = (colpack >> 5) & 0x1f;
		PGMSPRColROM[cnt * 3 + 2] = (colpack >> 10) & 0x1f;
	}
#else
	if (!nPGMSPRColPacked) {
		for (INT32 cnt = nPGMSPRColROMLen / 2 - 1; cnt >= 0; cnt--) {
			UINT16 colpack = PGMSPRColROM[cnt * 2] | (PGMSPRColROM[cnt * 2 + 1] << 8);
			PGMSPRColROM[cnt * 3 + 0] = (colpack >> 0) & 0x1f;
			PGMSPRColROM[cnt * 3 + 1] = (colpack >> 5) & 0x1f;
			PGMSPRColROM[cnt * 3 + 2] = (colpack >> 10) & 0x1f;
		}
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 DDP2: using original fully-expanded sprite colors.\n"));
	} else {
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM packed sprite colors retained: packed=%d virtual-expanded=%d saved=%d\n"), nPGMSPRColROMLen, expandedLen, expandedLen - nPGMSPRColROMLen);
	}
#endif
	return 0;
}

static void ics2115_sound_irq(INT32 nState)
{
	ZetSetIRQLine(0, nState);
}

INT32 pgmInit()
{
	BurnSetRefreshRate(59.185606);

	nEnableArm7 = (BurnDrvGetHardwareCode() / HARDWARE_IGS_USE_ARM_CPU) & 1;
	OldCodeMode = ((HackCodeDip & 1) || (bDoIpsPatch) || (NULL != pDataRomDesc)) ? 1 : 0;

	if (0 == nPGMSpriteBufferHack) {
		nPGMSpriteBufferHack = (nIpsDrvDefine & IPS_PGM_SPRHACK) ? 1 : 0;
	}

	Mem = NULL;

	pgmGetRoms(false);

#ifdef __PS3__
	if (!strcmp(BurnDrvGetTextA(DRV_NAME), "kov") && !nEnableArm7) {
		nPGMExternalARMLen = 0;
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 KOV: unused external ARM reserve disabled.\\n"));
	}
#endif

	#ifdef __PS3__
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM sizes: 68K=%d tile=%d sprite-color-packed=%d sprite-mask=%d sound=%d ARM=%d\n"), nPGM68KROMLen, nPGMTileROMLen, nPGMSPRColROMLen, nPGMSPRMaskROMLen, nPGMSNDROMLen, nPGMExternalARMLen);
	#endif
	if (expand_colourdata()) {
		bprintf(PRINT_ERROR, _T("[FBNeo] PGM sprite color expansion allocation FAILED\n"));
		return 1;
	}

#ifdef __PS3__
	if (pgm_ps3_color_cache_build()) {
		return 1;
	}
#endif

#ifdef __PS3__
	PS3_PGM_MEM_LABEL("PGMTileSharedAlloc");
	PGMTileSharedAlloc = (UINT8*)BurnMalloc(nPGMTileROMLen + 0x400000);
	PGMTileROM = PGMTileSharedAlloc;
	PGMTileROMExp = PGMTileSharedAlloc;
#else
	PGMTileROM      = (UINT8*)BurnMalloc(nPGMTileROMLen);			// 8x8 Text Tiles + 32x32 BG Tiles
	PGMTileROMExp   = (UINT8*)BurnMalloc((nPGMTileROMLen / 5) * 8);	// Expanded 8x8 Text Tiles and 32x32 BG Tiles
#endif
	PS3_PGM_MEM_LABEL("PGMSPRMaskROM");
	PGMSPRMaskROM	= (UINT8*)BurnMalloc(nPGMSPRMaskROMLen);
#ifdef __PS3__
	nPGMSNDROMAllocLen = nPGMSNDROMLen;
	if (pgm_ps3_kov2_compact_sound() && nPGMSNDROMLen >= 0x800000) {
		nPGMSNDROMAllocLen = nPGMSNDROMLen - 0x600000;
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PS3 KOV2 compact sound: logical=%x physical=%x saved=%x\n"),
			nPGMSNDROMLen, nPGMSNDROMAllocLen, nPGMSNDROMLen - nPGMSNDROMAllocLen);
	}
	PS3_PGM_MEM_LABEL("ICSSNDROM");
	ICSSNDROM		= (UINT8*)BurnMalloc(nPGMSNDROMAllocLen);
#else
	PS3_PGM_MEM_LABEL("ICSSNDROM");
	ICSSNDROM		= (UINT8*)BurnMalloc(nPGMSNDROMLen);
#endif

	pgmMemIndex();
	INT32 nLen = MemEnd - (UINT8 *)0;
#ifdef __PS3__
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM allocations: color=%p tile=%p expanded=%p mask=%p sound=%p main-bytes=%d\n"), PGMSPRColROM, PGMTileROM, PGMTileROMExp, PGMSPRMaskROM, ICSSNDROM, nLen);
#endif
	PS3_PGM_MEM_LABEL("PGMMainMem");
	Mem = (UINT8 *)BurnMalloc(nLen);
	if (PGMTileROM == NULL || PGMTileROMExp == NULL || PGMSPRMaskROM == NULL || ICSSNDROM == NULL || Mem == NULL) {
		bprintf(PRINT_ERROR, _T("[FBNeo] PGM allocation FAILED: tile=%p expanded=%p mask=%p sound=%p main=%p\n"), PGMTileROM, PGMTileROMExp, PGMSPRMaskROM, ICSSNDROM, Mem);
		BurnFree(Mem);
#ifdef __PS3__
		BurnFree(PGMTileSharedAlloc); PGMTileROM = PGMTileROMExp = NULL;
#else
		BurnFree(PGMTileROM); BurnFree(PGMTileROMExp);
#endif
		BurnFree(PGMSPRColROM); BurnFree(PGMSPRMaskROM); BurnFree(ICSSNDROM);
		return 1;
	}
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: main allocation OK, clearing main\n"));
	memset(Mem, 0, nLen);
	pgmMemIndex();
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: memory index ready\n"));

	// load bios roms (68k bios loaded in reset routine)
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: loading tile BIOS\n"));
	if (BurnLoadRom(PGMTileROM, 0x80, 1)) return 1;	// Bios Text and Tiles
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: loading sound BIOS\n"));
	if (BurnLoadRom(ICSSNDROM, 0x81, 1)) return 1;	     	// Bios Intro Sounds

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: loading game ROMs\n"));
	if (pgmGetRoms(true)) return 1;
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: game ROMs loaded, expanding tiles\n"));
	expand_tile_gfx();
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: tile expansion complete\n"));	// expand graphics

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: initializing 68000\n"));
	{
		SekInit(0, 0x68000);											// Allocate 68000
		SekOpen(0);

		// ketsui and espgaluda
		if (BurnDrvGetHardwareCode() & HARDWARE_IGS_JAMMAPCB)
		{
			SekMapMemory(PGM68KROM,				0x000000, (nPGM68KROMLen-1), MAP_ROM);			// 68000 ROM (no bios)
		} else {
			// if a cart is mapped at 100000+, the BIOS is mapped from 0-fffff, if no cart inserted, the BIOS is mapped to 7fffff!
			// note: kov2dzxx, kovplus 68K BIOS, Mapped addresses other than 7fffff will fail.
			SekMapMemory(PGM68KBIOS, 0x000000, 0x07ffff, MAP_ROM);

			if ((!bDoIpsPatch && nPGMMapperHack) || (nIpsDrvDefine & IPS_PGM_MAPHACK)) {
				SekMapMemory((PGM68KROM + 0x300000), 0x600000, 0x6fffff, MAP_ROM); // Adds a mapping for the specified game/ips.
			}

			if (nPGM68KROMLen == 0x1000000) // banked 68k rom
				SekMapMemory(PGM68KROM,				0x100000, 0x4fffff, MAP_ROM);
			else
				SekMapMemory(PGM68KROM,				0x100000, (nPGM68KROMLen-1)+0x100000, MAP_ROM);		// 68000 ROM

			// from 0 to 7fffff is completely mappable by the cartridge (it can cover the bios!)
		}

        for (INT32 i = 0; i < 0x100000; i+=0x20000) {		// Main Ram + Mirrors...
			SekMapMemory(PGM68KRAM,            	0x800000 | i, 0x81ffff | i, MAP_RAM);
		}

		for (INT32 i = 0; i < 0x100000; i+=0x08000) {		// Video Ram + Mirrors...
			SekMapMemory((UINT8 *)PGMBgRAM,		0x900000 | i, 0x900fff | i, MAP_RAM);
			SekMapMemory((UINT8 *)PGMBgRAM,		0x901000 | i, 0x901fff | i, MAP_RAM); // mirror
 			SekMapMemory((UINT8 *)PGMBgRAM,		0x902000 | i, 0x902fff | i, MAP_RAM); // mirror
			SekMapMemory((UINT8 *)PGMBgRAM,		0x903000 | i, 0x904fff | i, MAP_RAM); // mirror
			SekMapMemory((UINT8 *)PGMTxtRAM,	0x904000 | i, 0x905fff | i, MAP_RAM);
			SekMapMemory((UINT8 *)PGMTxtRAM,	0x906000 | i, 0x906fff | i, MAP_RAM); // mirror
			SekMapMemory((UINT8 *)PGMRowRAM,	0x907000 | i, 0x907fff | i, MAP_RAM);
		}
		
		if (OldCodeMode) {
			SekMapMemory((UINT8 *)PGMPalRAM,	0xa00000, 0xa013ff, MAP_ROM); // palette
			SekMapMemory((UINT8 *)PGMVidReg,	0xb00000, 0xb0ffff, MAP_RAM); // should be mirrored?
			SekMapHandler(1,					0xa00000, 0xa013ff, MAP_WRITE);
			SekMapHandler(2,					0xc10000, 0xc1ffff, MAP_READ | MAP_WRITE);
		} else {
			for (INT32 i = 0; i < 0x100000; i+= 0x02000) { // mirror
				SekMapMemory((UINT8 *)PGMPalRAM,0xa00000 | i, 0xa01fff | i, MAP_ROM); // palette
			}
			SekMapHandler(1,					0xa00000, 0xafffff, MAP_WRITE);
			SekMapHandler(2,					0xb00000, 0xbfffff, MAP_READ | MAP_WRITE);
			for (INT32 i = 0; i < 0x100000; i += 0x20000) { // mirror
				SekMapHandler(3,				0xc10000 | i, 0xc1ffff | i, MAP_READ | MAP_WRITE);
			}
		}

		// from d00000 to ffffff is completely mappable by the cartridge

		SekSetReadWordHandler(0,		PgmReadWord);
		SekSetReadByteHandler(0,		PgmReadByte);
		SekSetWriteWordHandler(0,		PgmWriteWord);
		SekSetWriteByteHandler(0,		PgmWriteByte);

		SekSetWriteByteHandler(1,		PgmPaletteWriteByte);
		SekSetWriteWordHandler(1,		PgmPaletteWriteWord);
		
		if (OldCodeMode) {
			SekSetReadWordHandler(2,	PgmZ80ReadWord);
			SekSetReadByteHandler(2,	PgmZ80ReadByte);
			SekSetWriteWordHandler(2,	PgmZ80WriteWord);
			SekSetWriteByteHandler(2,	PgmZ80WriteByte);
		} else {
			SekSetReadWordHandler(2,	PgmVideoControllerReadWord);
			SekSetReadByteHandler(2,	PgmVideoControllerReadByte);
			SekSetWriteWordHandler(2,	PgmVideoControllerWriteWord);
			SekSetWriteByteHandler(2,	PgmVideoControllerWriteByte);

			SekSetReadWordHandler(3,	PgmZ80ReadWord);
			SekSetReadByteHandler(3,	PgmZ80ReadByte);
			SekSetWriteWordHandler(3,	PgmZ80WriteWord);
			SekSetWriteByteHandler(3,	PgmZ80WriteByte);
		}

		SekClose();
	}

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: 68000 ready, initializing Z80\n"));
	{
		ZetInit(0);
		ZetOpen(0);
		ZetMapMemory(RamZ80, 0x0000, 0xffff, MAP_RAM);
		ZetSetOutHandler(PgmZ80PortWrite);
		ZetSetInHandler(PgmZ80PortRead);
		ZetClose();
	}

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: Z80 ready, initializing draw/audio\n"));
	pgmInitDraw();

#if defined(__PS3__) && \
    defined(__PSL1GHT__) && \
    defined(PS3_PGM_SPU_WORKER) && \
    PS3_PGM_SPU_WORKER
	/* PS3_PGM_SPU_WORKER_V1A_INIT */
	{

	        INT32 spu_ret = ps3_pgm_spu_worker_init();


	        bprintf(PRINT_IMPORTANT,

	                _T("[PS3 PGM PERF] SPU worker init return=%d\\n"),

	                spu_ret);


	        if (spu_ret == 0) {

	                ps3_pgm_color_shadow_init();

	        }

	}
#endif

	v3021Init();
	ics2115_init(ics2115_sound_irq, ICSSNDROM, nPGMSNDROMLen);
#ifdef __PS3__
	if (pgm_ps3_kov2_compact_sound()) {
		ics2115_set_rom_hole(0x200000, 0x800000);
	}
#endif
	ics_2115_set_volume(2.0);
	BurnTimerAttachZet(Z80_FREQ);

	pBurnDrvPalette = (UINT32*)PGMPalRAM;
    
    if (strncmp(BurnDrvGetTextA(DRV_NAME), "pgm3in1", 7) == 0) {//load pgm3in1 mask rom and sound rom before pgm3in1's decrypt
        UINT8 *maskROM = (UINT8 *)malloc(0x200000);
        BurnLoadRom(maskROM,9,1);
        memcpy((void *)(PGMSPRMaskROM + 0xf00000),maskROM,0x100000);
        free(maskROM);
        BurnLoadRom(ICSSNDROM + 0x800000,0x0b,1);
    }

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: base hardware ready, running game decrypt callback\n"));
	if (pPgmInitCallback) {
		pPgmInitCallback();
	}

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: decrypt complete, installing protection\n"));
	if (pPgmProtCallback) {
		pPgmProtCallback();
	}

	#ifdef __PS3__
	if (pgm_ps3_mask_cache_build()) {
	        return 1;
	}
	#endif

	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM stage: protection ready, reset\n"));
	PgmDoReset();
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM initialization complete\n"));

	return 0;
}

INT32 pgmExit()
{
	#if defined(__PS3__) && \
    defined(__PSL1GHT__) && \
    defined(PS3_PGM_SPU_WORKER) && \
    PS3_PGM_SPU_WORKER
	/* PS3_PGM_SPU_WORKER_V1A_EXIT */
	ps3_pgm_color_shadow_exit();
	ps3_pgm_spu_worker_shutdown();
#endif

	pgmExitDraw();

#ifdef __PS3__
        pgm_ps3_mask_cache_exit();
        pgm_ps3_color_cache_exit();
#endif

	SekExit();
	ZetExit();

	if (nEnableArm7) {
		Arm7Exit();
	}

	if (ICSSNDROM) {
		BurnFree(ICSSNDROM);
	}

	BurnFree(Mem);

	v3021Exit();
	ics2115_exit();

#ifdef __PS3__
	BurnFree (PGMTileSharedAlloc);
	PGMTileROM = PGMTileROMExp = NULL;
#else
	BurnFree (PGMTileROM);
	BurnFree (PGMTileROMExp);
#endif
	BurnFree (PGMSPRColROM);
	BurnFree (PGMSPRMaskROM);

	nPGM68KROMLen = 0;
	nPGMTileROMLen = 0;
	nPGMSPRColROMLen = 0;
	nPGMSPRColPacked = 0;
	nPGMSPRMaskROMLen = 0;
	nPGMSNDROMLen = 0;
	nPGMExternalARMLen = 0;

	pPgmInitCallback = NULL;
	pPgmTileDecryptCallback = NULL;
	pPgmColorDataDecryptcallback = NULL;
	pPgmProtCallback = NULL;
	pPgmScanCallback = NULL;
	pPgmResetCallback = NULL;

	nEnableArm7 = 0;
	nPGMDisableIRQ4 = 0;
	nPGMArm7Type = 0;
	nPgmAsicRegionHackAddress = 0;

	nPgmCurrentBios = -1;

	nPGMSpriteBufferHack = 0;
	nPGMMapperHack = 0;

	return 0;
}

static void pgm_sprite_buffer()
{
	if (pgm_video_control & 0x0001) // verified
	{
		UINT16 *ram16 = (UINT16*)PGM68KRAM;
		
		UINT16 mask[2][5] = { 
			{ 0xffff, 0xfbff, 0x7fff, 0xffff, 0xffff }, // The sprite buffer hardware masks these bits!
			{ 0xffff, 0xffff, 0xffff, 0xffff, 0xffff }	// Some hacks rely on poor emulation
		};
		
		for (INT32 i = 0; i < 0xa00/2; i+= 10/2)
		{
			for (INT32 j = 0; j < 10 / 2; j++)
			{
				// The mask values describe logical 68K bits. On a big-endian
				// host such as PS3, ram16[] is still in the emulator's stored
				// byte order, so masking it directly clears the wrong bits.
				UINT16 word = BURN_ENDIAN_SWAP_INT16(ram16[i + j]);
				word &= mask[nPGMSpriteBufferHack][j];
				PGMSprBuf[(i / (10 / 2)) * (16 / 2) + j] =
					BURN_ENDIAN_SWAP_INT16(word);
			} 

			const UINT16 sizeword = BURN_ENDIAN_SWAP_INT16(ram16[i + 4]);
			if ((sizeword & 0x7fff) == 0) break; // verified on hardware
		}
	}
}

INT32 pgmFrame()
{
	if (PgmReset) {
		PgmDoReset();
	}

	// compile inputs
	{
		memset (PgmInput, 0, 6); // 6 is correct! Regions are stored in 7!

		for (INT32 i = 0; i < sizeof(PgmJoy1); i++) {
			PgmInput[0] |= (PgmJoy1[i] & 1) << i;
			PgmInput[1] |= (PgmJoy2[i] & 1) << i;
			PgmInput[2] |= (PgmJoy3[i] & 1) << i;
			PgmInput[3] |= (PgmJoy4[i] & 1) << i;
			PgmInput[4] |= (PgmBtn1[i] & 1) << i;
			PgmInput[5] |= (PgmBtn2[i] & 1) << i;
		}

		// clear opposites & hold coin
		for (INT32 i = 0; i < 4; i++) {
			clear_opposite.check(i, PgmInput[i], 0x02, 0x4, 0x08, 0x10, nSocd[i]);
			hold_coin.check(i, PgmInput[4], 1 << i, 7);
		}
	}

	SekNewFrame();
	ZetNewFrame();

	SekOpen(0);
	ZetOpen(0);

	SekIdle(nCyclesDone[0]);
	ZetIdle(nCyclesDone[1]);

	if (OldCodeMode)
		SekSetIRQLine(6, CPU_IRQSTATUS_AUTO);

	if (nEnableArm7)
	{
		Arm7NewFrame();
		Arm7Open(0);
		Arm7Idle(nCyclesDone[2]);

		if (OldCodeMode) {
			nCyclesTotal[0] = M68K_CYCS_PER_FRAME;
			nCyclesTotal[1] = Z80_CYCS_PER_FRAME;
			nCyclesTotal[2] = ARM7_CYCS_PER_FRAME;

			while (SekTotalCycles() < nCyclesTotal[0] / 2)
				SekRun(nCyclesTotal[0] / 2 - SekTotalCycles());

			if (!nPGMDisableIRQ4)
				SekSetIRQLine(4, CPU_IRQSTATUS_AUTO);

			while (SekTotalCycles() < nCyclesTotal[0])
				SekRun(nCyclesTotal[0] - SekTotalCycles());

			while (Arm7TotalCycles() < nCyclesTotal[2])
				Arm7Run(nCyclesTotal[2] - Arm7TotalCycles());

			nCyclesDone[2] = Arm7TotalCycles() - nCyclesTotal[2];
			Arm7Close();
		}
	} else {
		if (OldCodeMode) {
			nCyclesTotal[0] = (UINT32)((UINT64)(20000000) * nBurnCPUSpeedAdjust * 100 / (0x0100 * nBurnFPS));
			nCyclesTotal[1] = Z80_CYCS_PER_FRAME;

			while (SekTotalCycles() < nCyclesTotal[0] / 2)
				SekRun(nCyclesTotal[0] / 2 - SekTotalCycles());

			if (!nPGMDisableIRQ4)
				SekSetIRQLine(4, CPU_IRQSTATUS_AUTO);

			while (SekTotalCycles() < nCyclesTotal[0])
				SekRun(nCyclesTotal[0] - SekTotalCycles());
		}
	}

	if (!OldCodeMode) {
		INT32 nInterleave = 262; // 262 scanlines
		nCyclesTotal[0] = M68K_CYCS_PER_FRAME;
		nCyclesTotal[1] = Z80_CYCS_PER_FRAME;
		nCyclesTotal[2] = ARM7_CYCS_PER_FRAME;

		for (INT32 i = 0; i < nInterleave; i++)
		{
			if (i == 224) {
				SekSetIRQLine(6, CPU_IRQSTATUS_AUTO); // vblank - cart-controlled!
				pgm_sprite_buffer();
			}
			if (i == 218 && !nPGMDisableIRQ4) SekSetIRQLine(4, CPU_IRQSTATUS_AUTO); // verified on Dragon World II cart - Cart-controlled! 

			CPU_RUN(0, Sek);
		//	CPU_IDLE_SYNCINT(1, Zet); // sync'd on reads and writes and at the end of the frame
			if (nEnableArm7) CPU_RUN_SYNCINT(2, Arm7);
		}
	}

	BurnTimerEndFrame(nCyclesTotal[1]);
	ics2115_update(nBurnSoundLen);

	nCyclesDone[0]			= SekTotalCycles() - nCyclesTotal[0];
	nCyclesDone[1]			= ZetTotalCycles() - nCyclesTotal[1];

	if (!OldCodeMode) {
		if (nEnableArm7) {
			nCyclesDone[2]	= Arm7TotalCycles() - nCyclesTotal[2];
			Arm7Close();
		}
	}
	ZetClose();
	SekClose();

	if (pBurnDraw) {
		BurnDrvRedraw();
	}

	if (OldCodeMode)
		memcpy(PGMSprBuf, PGM68KRAM /* Sprite RAM 0-bff */, 0xa00); // buffer sprites
	
	return 0;
}

INT32 pgmScan(INT32 nAction,INT32 *pnMin)
{
	struct BurnArea ba;

	if (pnMin) {
		*pnMin =  0x029702;
	}

	nPgmPalRecalc = 1;

	if (nAction & ACB_MEMORY_ROM) {	
		if (BurnDrvGetHardwareCode() & HARDWARE_IGS_JAMMAPCB) {
			ba.Data		= PGM68KROM;
			ba.nLen		= nPGM68KROMLen;
			ba.nAddress	= 0;
			ba.szName	= "68K ROM";
			BurnAcb(&ba);
		} else {
			ba.Data		= PGM68KBIOS;
			ba.nLen		= 0x0020000;
			ba.nAddress	= 0;
			ba.szName	= "BIOS ROM";
			BurnAcb(&ba);

			ba.Data		= PGM68KROM;
			ba.nLen		= nPGM68KROMLen;
			ba.nAddress	= 0x100000;
			ba.szName	= "68K ROM";
			BurnAcb(&ba);
		}
	}

	if (nAction & ACB_MEMORY_RAM) {	
		ba.Data			= PGMBgRAM;
		ba.nLen			= 0x004000;
		ba.nAddress		= 0x900000;
		ba.szName		= "Bg RAM";
		BurnAcb(&ba);

		ba.Data			= PGMTxtRAM;
		ba.nLen			= 0x003000;
		ba.nAddress		= 0x904000;
		ba.szName		= "Tx RAM";
		BurnAcb(&ba);

		ba.Data			= PGMRowRAM;
		ba.nLen			= 0x001000;
		ba.nAddress		= 0x907000;
		ba.szName		= "Row Scroll";
		BurnAcb(&ba);

		if (!OldCodeMode) {
			ba.Data		= PGMPalRAM;
			ba.nLen		= 0x002000;
			ba.nAddress	= 0xA00000;
			ba.szName	= "Palette RAM";
			BurnAcb(&ba);

			ba.Data		= PGMSprBuf;
			ba.nLen		= 0x001000;
			ba.nAddress	= 0xB00000;
			ba.szName	= "Sprite Buffer";
			BurnAcb(&ba);
		} else {
			ba.Data		= PGMPalRAM;
			ba.nLen		= 0x001400;
			ba.nAddress	= 0xA00000;
			ba.szName	= "Palette RAM";
			BurnAcb(&ba);

			ba.Data		= PGMVidReg;
			ba.nLen		= 0x010000;
			ba.nAddress	= 0xB00000;
			ba.szName	= "Video Regs";
			BurnAcb(&ba);
		}
		
		ba.Data			= PGMZoomRAM;
		ba.nLen			= 0x000040;
		ba.nAddress		= 0xB01000;
		ba.szName		= "Zoom Regs";
		BurnAcb(&ba);
		
		ba.Data			= RamZ80;
		ba.nLen			= 0x010000;
		ba.nAddress		= 0xC10000;
		ba.szName		= "Z80 RAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_NVRAM) {
		ba.Data			= PGM68KRAM;
		ba.nLen			= 0x020000;
		ba.nAddress		= 0x800000;
		ba.szName		= "68K RAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_DRIVER_DATA) {
	
		SekScan(nAction);
		ZetScan(nAction);

		v3021Scan();

		hold_coin.scan();
		clear_opposite.scan();

		SCAN_VAR(nPgmCurrentBios);

		SCAN_VAR(nSoundlatch);
		SCAN_VAR(bSoundlatchRead);

		SCAN_VAR(pgm_bg_scrollx);
		SCAN_VAR(pgm_bg_scrolly);
		SCAN_VAR(pgm_fg_scrollx);
		SCAN_VAR(pgm_fg_scrolly);
		SCAN_VAR(pgm_video_control);
		SCAN_VAR(pgm_unk_video_flags);
		SCAN_VAR(pgm_z80_connect_bus);

		ics2115_scan(nAction, pnMin);
	}

	if (pPgmScanCallback) {
		pPgmScanCallback(nAction, pnMin);
	}

 	return 0;
}
