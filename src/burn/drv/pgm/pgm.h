#include "tiles_generic.h"
#include "m68000_intf.h"
#include "z80_intf.h"
#include "arm7_intf.h"


#define HARDWARE_IGS_JAMMAPCB		0x0002

// d_pgm
extern UINT8 HackCodeDip;

// pgm_run
extern INT32 nPGM68KROMLen;
extern INT32 nPGMSPRColMaskLen;
extern INT32 nPGMSPRColROMLen;
extern INT32 nPGMSPRColPacked;
extern INT32 nPGMSPRMaskMaskLen;
extern INT32 nPGMTileROMLen;
extern INT32 nPGMExternalARMLen;

extern UINT8 *PGM68KBIOS;
extern UINT8 *PGM68KRAM;
extern UINT8 *PGM68KROM;
extern UINT8 *PGMTileROM;
extern UINT8 *PGMTileROMExp;
extern UINT8 *PGMSPRColROM;
extern UINT8 *PGMSPRMaskROM;

#ifdef __PS3__
#include "../../ps3_memory_pool.h"
#define PGM_PS3_MASK_PAGE_SHIFT 16
#define PGM_PS3_MASK_PAGE_SIZE  (1u << PGM_PS3_MASK_PAGE_SHIFT)
#define PGM_PS3_MASK_CACHE_PAGES 64
#define PGM_PS3_MASK_CACHE_MASK  (PGM_PS3_MASK_CACHE_PAGES - 1)

extern UINT8 *PGMSPRMaskPageCache;
extern INT32 PGMSPRMaskPageTag[PGM_PS3_MASK_CACHE_PAGES];
extern INT32 nPGMSPRMaskFileCacheActive;

UINT8 *pgm_ps3_mask_cache_miss(UINT32 page);


/* PS3_MASK_SPAN_V1
 *
 * Return a contiguous mask span after one cache lookup.
 * Never cross logical ROM wrap or a 64 KiB cache page.
 */
static inline const UINT8 *pgm_ps3_mask_cache_span(
    UINT32 offset,
    UINT32 bytes)
{
    offset &= (UINT32)nPGMSPRMaskMaskLen;

    UINT32 logical_size =
        (UINT32)nPGMSPRMaskMaskLen + 1u;

    if (bytes == 0)
        return NULL;

    if (bytes > logical_size - offset)
        return NULL;

    if (!nPGMSPRMaskFileCacheActive)
        return PGMSPRMaskROM + offset;

    UINT32 in_page =
        offset & (PGM_PS3_MASK_PAGE_SIZE - 1);

    if (bytes > PGM_PS3_MASK_PAGE_SIZE - in_page)
        return NULL;

    UINT32 page =
        offset >> PGM_PS3_MASK_PAGE_SHIFT;

    UINT32 slot =
        page & PGM_PS3_MASK_CACHE_MASK;

    if (PGMSPRMaskPageTag[slot] != (INT32)page) {
        ps3_perf_record_cache_access(
            PS3_PERF_MASK_CACHE, 0);

        pgm_ps3_mask_cache_miss(page);
    }
    else {
        ps3_perf_record_cache_access(
            PS3_PERF_MASK_CACHE, 1);
    }

    return
        PGMSPRMaskPageCache +
        (slot << PGM_PS3_MASK_PAGE_SHIFT) +
        in_page;
}

static inline UINT8 pgm_ps3_mask_read(UINT32 offset)
{
	offset &= (UINT32)nPGMSPRMaskMaskLen;

	if (!nPGMSPRMaskFileCacheActive) {
		return PGMSPRMaskROM[offset];
	}

	UINT32 page = offset >> PGM_PS3_MASK_PAGE_SHIFT;
	UINT32 slot = page & PGM_PS3_MASK_CACHE_MASK;

	if (PGMSPRMaskPageTag[slot] != (INT32)page) {
		ps3_perf_record_cache_access(PS3_PERF_MASK_CACHE, 0);
		pgm_ps3_mask_cache_miss(page);
	} else {
		ps3_perf_record_cache_access(PS3_PERF_MASK_CACHE, 1);
	}

	return PGMSPRMaskPageCache[
		(slot << PGM_PS3_MASK_PAGE_SHIFT) +
		(offset & (PGM_PS3_MASK_PAGE_SIZE - 1))
	];
}

#define PGM_PS3_COLOR_PAGE_SHIFT 16
#define PGM_PS3_COLOR_PAGE_SIZE  (1u << PGM_PS3_COLOR_PAGE_SHIFT)
#define PGM_PS3_COLOR_CACHE_PAGES 256
#define PGM_PS3_COLOR_CACHE_MASK  (PGM_PS3_COLOR_CACHE_PAGES - 1)
#ifndef PS3_PGM_COLOR_CACHE_2WAY
#define PS3_PGM_COLOR_CACHE_2WAY 1
#endif

extern UINT8 *PGMSPRColPageCache;
extern INT32 PGMSPRColPageTag[PGM_PS3_COLOR_CACHE_PAGES];
#if PS3_PGM_COLOR_CACHE_2WAY
extern UINT8 PGMSPRColCacheVictim[PGM_PS3_COLOR_CACHE_PAGES / 2];
#endif
extern INT32 nPGMSPRColFileCacheActive;

UINT8 *pgm_ps3_color_cache_miss(UINT32 page);
void pgm_ps3_color_cache_note_page(UINT32 page);

/*
 * Packed PGM color ROM stores 3 x 5-bit pixels in each 16-bit word.
 * pair<<1 is always even, so the two bytes cannot straddle a 64 KiB
 * page boundary.
 */

/* PS3_COLOR_BLOCKREAD_V1
 * Return a contiguous span from the packed color ROM.
 * A cached span must remain entirely inside one 64 KiB page.
 */
static inline const UINT8 *pgm_ps3_color_cache_span(UINT32 offset, UINT32 bytes)
{
    if (!nPGMSPRColFileCacheActive) {
        return PGMSPRColROM + offset;
    }

    UINT32 in_page =
        offset & (PGM_PS3_COLOR_PAGE_SIZE - 1);

    if (bytes > PGM_PS3_COLOR_PAGE_SIZE - in_page) {
        return NULL;
    }

    UINT32 page =
        offset >> PGM_PS3_COLOR_PAGE_SHIFT;

#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
    static UINT32 last_observed_page = 0xffffffffu;

    if (page != last_observed_page) {
        pgm_ps3_color_cache_note_page(page);
        last_observed_page = page;
    }
#endif

#if PS3_PGM_COLOR_CACHE_2WAY
    UINT32 set =
        page & ((PGM_PS3_COLOR_CACHE_PAGES / 2) - 1);

    UINT32 slot0 = set * 2;
    UINT32 slot1 = slot0 + 1;
    UINT32 slot;

    if (PGMSPRColPageTag[slot0] == (INT32)page) {
        slot = slot0;

        ps3_perf_record_cache_access(
            PS3_PERF_COLOR_CACHE, 1);

        ps3_perf_record_color_way_hit(0);

        PGMSPRColCacheVictim[set] = 1;
    }
    else if (PGMSPRColPageTag[slot1] == (INT32)page) {
        slot = slot1;

        ps3_perf_record_cache_access(
            PS3_PERF_COLOR_CACHE, 1);

        ps3_perf_record_color_way_hit(1);

        PGMSPRColCacheVictim[set] = 0;
    }
    else {
        ps3_perf_record_cache_access(
            PS3_PERF_COLOR_CACHE, 0);

        UINT8 *installed =
            pgm_ps3_color_cache_miss(page);

        slot = (UINT32)(
            (installed - PGMSPRColPageCache)
            >> PGM_PS3_COLOR_PAGE_SHIFT);
    }

#else

    UINT32 slot =
        page & PGM_PS3_COLOR_CACHE_MASK;

    if (PGMSPRColPageTag[slot] != (INT32)page) {
        ps3_perf_record_cache_access(
            PS3_PERF_COLOR_CACHE, 0);

        pgm_ps3_color_cache_miss(page);
    }
    else {
        ps3_perf_record_cache_access(
            PS3_PERF_COLOR_CACHE, 1);
    }

#endif

    return
        PGMSPRColPageCache +
        (slot << PGM_PS3_COLOR_PAGE_SHIFT) +
        in_page;
}

static inline UINT16 pgm_ps3_color_read_pair(UINT32 pair)
{
    UINT32 offset = pair << 1;

    if (!nPGMSPRColFileCacheActive) {
        return (UINT16)(
            PGMSPRColROM[offset] |
            (PGMSPRColROM[offset + 1] << 8));
    }

    UINT32 page = offset >> PGM_PS3_COLOR_PAGE_SHIFT;
#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
    static UINT32 last_observed_page = 0xffffffffu;
    if (page != last_observed_page) {
        pgm_ps3_color_cache_note_page(page);
        last_observed_page = page;
    }
#endif
#if PS3_PGM_COLOR_CACHE_2WAY
    UINT32 set = page & ((PGM_PS3_COLOR_CACHE_PAGES / 2) - 1);
    UINT32 slot0 = set * 2;
    UINT32 slot1 = slot0 + 1;
    UINT32 slot;
    if (PGMSPRColPageTag[slot0] == (INT32)page) {
        slot = slot0;
        ps3_perf_record_cache_access(PS3_PERF_COLOR_CACHE, 1);
        ps3_perf_record_color_way_hit(0);
        PGMSPRColCacheVictim[set] = 1;
    } else if (PGMSPRColPageTag[slot1] == (INT32)page) {
        slot = slot1;
        ps3_perf_record_cache_access(PS3_PERF_COLOR_CACHE, 1);
        ps3_perf_record_color_way_hit(1);
        PGMSPRColCacheVictim[set] = 0;
    } else {
        ps3_perf_record_cache_access(PS3_PERF_COLOR_CACHE, 0);
        UINT8 *installed = pgm_ps3_color_cache_miss(page);
        slot = (UINT32)((installed - PGMSPRColPageCache) >> PGM_PS3_COLOR_PAGE_SHIFT);
    }
#else
    UINT32 slot = page & PGM_PS3_COLOR_CACHE_MASK;

    if (PGMSPRColPageTag[slot] != (INT32)page) {
        ps3_perf_record_cache_access(PS3_PERF_COLOR_CACHE, 0);
        pgm_ps3_color_cache_miss(page);
    } else {
        ps3_perf_record_cache_access(PS3_PERF_COLOR_CACHE, 1);
    }
#endif

    const UINT8 *src =
        PGMSPRColPageCache +
        (slot << PGM_PS3_COLOR_PAGE_SHIFT) +
        (offset & (PGM_PS3_COLOR_PAGE_SIZE - 1));

    return (UINT16)(src[0] | (src[1] << 8));
}

#endif
extern UINT8 *PGMARMROM;
extern UINT8 *PGMUSER0;
extern UINT8 *PGMProtROM;
extern UINT8 *ICSSNDROM;
extern UINT8 *PGMARMRAM0;
extern UINT8 *PGMARMRAM1;
extern UINT8 *PGMARMRAM2;
extern UINT8 *PGMARMShareRAM;
extern UINT8 *PGMARMShareRAM2;
extern UINT16 *PGMRowRAM;
extern UINT16 *PGMPalRAM;
extern UINT16 *PGMVidReg;
extern UINT16 *PGMZoomRAM;
extern UINT16 *PGMSprBuf;
extern UINT32 *PGMBgRAM;
extern UINT32 *PGMTxtRAM;
extern UINT32 *RamCurPal;
extern UINT8 nPgmPalRecalc;

extern UINT16 pgm_bg_scrollx;
extern UINT16 pgm_bg_scrolly;
extern UINT16 pgm_fg_scrollx;
extern UINT16 pgm_fg_scrolly;
extern UINT16 pgm_video_control;

extern UINT8 PgmJoy1[];
extern UINT8 PgmJoy2[];
extern UINT8 PgmJoy3[];
extern UINT8 PgmJoy4[];
extern UINT8 PgmBtn1[];
extern UINT8 PgmBtn2[];
extern UINT8 PgmInput[];
extern UINT8 PgmReset;

extern void (*pPgmInitCallback)();
extern void (*pPgmResetCallback)();
extern INT32 (*pPgmScanCallback)(INT32, INT32*);
extern void (*pPgmProtCallback)();
extern void (*pPgmTileDecryptCallback)(UINT8 *gfx, INT32 len);
extern void (*pPgmColorDataDecryptcallback)(UINT8 *gfx, INT32 len);

extern INT32 nPGMDisableIRQ4;
extern UINT32 nPgmAsicRegionHackAddress;
extern INT32 nPGMSpriteBufferHack;
extern INT32 nPGMMapperHack;
extern INT32 OldCodeMode;

INT32 pgmInit();
INT32 pgmExit();
INT32 pgmFrame();
INT32 pgmScan(INT32 nAction, INT32 *pnMin);

// pgm_draw
void pgmInitDraw();
void pgmExitDraw();
INT32 pgmDraw();

// pgm_prot
void install_protection_asic3_orlegend();
void install_protection_asic25_asic12_dw2();
void install_protection_asic25_asic22_killbld();
void install_protection_asic25_asic22_drgw3();
void install_protection_asic25_asic28_olds();
void install_protection_asic27_kov();
void install_protection_asic27a_kovsh();
void install_protection_asic27a_martmast();
void install_protection_asic27a_oldsplus();
void install_protection_asic27a_puzlstar();
void install_protection_asic27a_svg();
void install_protection_asic27a_ketsui();
void install_protection_asic27a_ddp3();
void install_protection_asic27a_puzzli2();
void install_protection_asic27a_kovshp();
void install_protection_asic27a_py2k2();
void install_protection_asic27a_kovgsyx();

// pgm_crypt
void pgm_decrypt_kov();
void pgm_decrypt_kovsh();
void pgm_decrypt_kovshp();
void pgm_decrypt_puzzli2();
void pgm_decrypt_dw2();
void pgm_decrypt_photoy2k();
void pgm_decrypt_puzlstar();
void pgm_decrypt_dw3();
void pgm_decrypt_killbld();
void pgm_decrypt_dfront();
void pgm_decrypt_ddp2();
void pgm_decrypt_martmast();
void pgm_decrypt_dwpc();
void pgm_decrypt_kov2();
void pgm_decrypt_kov2p();
void pgm_decrypt_theglad();
void pgm_decrypt_killbldp();
void pgm_decrypt_oldsplus();
void pgm_decrypt_svg();
void pgm_decrypt_svgpcb();
void pgm_decrypt_happy6();
void pgm_decrypt_dw2001();
void pgm_decrypt_py2k2();
void pgm_decrypt_espgaluda();
void pgm_decrypt_ketsui();
void pgm_decrypt_pgm3in1();

void pgm_descramble_happy6_data(UINT8 *gfx, INT32 len);

void pgm_decode_kovqhsgs_gfx(UINT8 *gfx, INT32 len);
void pgm_decode_kovqhsgs_tile_data(UINT8 *gfx, INT32 len);
void pgm_decrypt_kovqhsgs();
void pgm_decrypt_kovlsqh2();
void pgm_decrypt_kovassgplus();
void pgm_decrypt_kovassge();
void pgm_decrypt_kovassgn();
void pgm_decrypt_kovlsqho();
void pgm_decrypt_kovgsyx();
