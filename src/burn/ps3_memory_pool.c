#include "ps3_memory_pool.h"

#ifndef PS3_FBNEO_VERBOSE_DIAGNOSTICS
#define PS3_FBNEO_VERBOSE_DIAGNOSTICS 0
#endif

#if defined(__PS3__)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/memory.h>
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
#include <stdio.h>
#include <stdarg.h>
#endif

#ifndef PS3_MEMORY_POOL_SIZE_MB
#define PS3_MEMORY_POOL_SIZE_MB 64u
#endif
#define POOL_SIZE      ((size_t)PS3_MEMORY_POOL_SIZE_MB * 1024u * 1024u)
#define POOL_ALIGNMENT 128u
#define POOL_MAGIC     0x504f4f4cu
#define FALLBACK_MAGIC 0x46414c4cu

#ifndef PS3_NATIVE_MEMORY
#define PS3_NATIVE_MEMORY 0
#endif

#define NATIVE_ALLOCATION_MAX 32

typedef struct native_allocation {
	void *ptr;
	size_t allocated_size;
	size_t requested_size;
	uint64_t page_flag;
	const char *label;
} native_allocation;

#define PS3_MEM_STRINGIFY_INNER(value) #value
#define PS3_MEM_STRINGIFY(value) PS3_MEM_STRINGIFY_INNER(value)

#if PS3_MEMORY_POOL_SIZE_MB == 96
#define PS3_MEMORY_POOL_BUILD_LABEL "96"
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=96\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=100663296\n"
typedef char ps3_pool_96_mib_compile_check[(POOL_SIZE == 100663296u) ? 1 : -1];
#elif PS3_MEMORY_POOL_SIZE_MB == 84
#define PS3_MEMORY_POOL_BUILD_LABEL "84"
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=84\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=88080384\n"
typedef char ps3_pool_84_mib_compile_check[(POOL_SIZE == 88080384u) ? 1 : -1];
#elif PS3_MEMORY_POOL_SIZE_MB == 68
#define PS3_MEMORY_POOL_BUILD_LABEL "68"
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=68\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=71303168\n"
typedef char ps3_pool_68_mib_compile_check[(POOL_SIZE == 71303168u) ? 1 : -1];
#elif PS3_MEMORY_POOL_SIZE_MB == 64
#define PS3_MEMORY_POOL_BUILD_LABEL "64"
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=64\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=67108864\n"
typedef char ps3_pool_64_mib_compile_check[(POOL_SIZE == 67108864u) ? 1 : -1];
#elif PS3_MEMORY_POOL_SIZE_MB == 8
#define PS3_MEMORY_POOL_BUILD_LABEL "8"
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=8\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=8388608\n"
typedef char ps3_pool_8_mib_compile_check[(POOL_SIZE == 8388608u) ? 1 : -1];
#else
#define PS3_MEMORY_POOL_BUILD_LABEL PS3_MEM_STRINGIFY(PS3_MEMORY_POOL_SIZE_MB)
#define PS3_MEMORY_BUILD_CONFIG_LINE "[PS3 MEM] BUILD CONFIG diagnostic=1 pool_size_mb=" PS3_MEMORY_POOL_BUILD_LABEL "\n"
#define PS3_MEMORY_BUILD_TOTAL_LINE  "[PS3 MEM] pool_total=" PS3_MEMORY_POOL_BUILD_LABEL " MiB (configured)\n"
#endif

typedef struct pool_block {
	size_t size;
	struct pool_block *prev;
	struct pool_block *next;
	void *allocation;
	uint32_t magic;
	uint32_t is_free;
	uint8_t padding[POOL_ALIGNMENT - (sizeof(size_t) + 3 * sizeof(void *) + 2 * sizeof(uint32_t))];
} pool_block;

typedef char pool_header_must_be_128_bytes[(sizeof(pool_block) == POOL_ALIGNMENT) ? 1 : -1];

static uint8_t *g_pool;
static pool_block *g_first;
static size_t g_pool_used, g_pool_peak, g_largest_pool_allocation;
static size_t g_system_live, g_system_peak, g_largest_system_fallback;
static size_t g_tracked_live_peak;
static size_t g_pool_success_count, g_pool_success_bytes;
static size_t g_system_fallback_count, g_system_fallback_bytes;
static size_t g_allocation_failure_count, g_allocation_failure_bytes;
static const char *g_next_allocation_label;
static const char *g_next_allocation_file;
static int g_next_allocation_line;
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
typedef struct diag_live_allocation {
	void *ptr;
	size_t requested_size;
	size_t allocated_size;
	const char *label;
	const char *file;
	int line;
} diag_live_allocation;
#define DIAG_LIVE_MAX 2048
static diag_live_allocation g_diag_live[DIAG_LIVE_MAX];
static void diag_live_add(void *ptr, size_t requested, size_t allocated, const char *label);
static void diag_live_remove(void *ptr);
#else
static void diag_live_add(void *ptr, size_t requested, size_t allocated, const char *label) { (void)ptr; (void)requested; (void)allocated; (void)label; }
static void diag_live_remove(void *ptr) { (void)ptr; }
static void diag_live_top20(void) { }
#endif
static native_allocation g_native_allocations[NATIVE_ALLOCATION_MAX];
static size_t g_native_live, g_native_peak, g_native_count, g_native_bytes;
static size_t g_native_failure_count, g_native_failure_bytes;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
#define PS3_MEMORY_DIAGNOSTIC_LOG_PATH "/dev_hdd0/game/ARCD00001/USRDIR/fbneo-ps3-memory.log"
static FILE *g_diag_file;
static unsigned long g_last_available_user_memory;
static int g_last_available_user_memory_valid;

static void diag_print(const char *format, ...)
{
	va_list args;
	FILE *output = g_diag_file ? g_diag_file : stderr;
	va_start(args, format);
	vfprintf(output, format, args);
	va_end(args);
	fflush(output);
}

#define DIAG_PRINT(...) diag_print(__VA_ARGS__)
#define DIAG_STAT(statement) do { statement; } while (0)
#else
#define DIAG_PRINT(...) ((void)0)
#define DIAG_STAT(statement) ((void)0)
#endif

static void diag_native_user_memory(const char *stage)
{
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
	sys_memory_info_t info;
	int result = sys_memory_get_user_memory_size(&info);
	if (result == 0) {
		long delta = g_last_available_user_memory_valid ?
			(long)info.available_user_memory - (long)g_last_available_user_memory : 0;
		DIAG_PRINT("[PS3 MEM NATIVE] stage=%s total_user_memory=%lu available_user_memory=%lu delta_from_previous=%ld\n",
			stage ? stage : "unknown", (unsigned long)info.total_user_memory,
			(unsigned long)info.available_user_memory, delta);
		g_last_available_user_memory = (unsigned long)info.available_user_memory;
		g_last_available_user_memory_valid = 1;
	} else {
		DIAG_PRINT("[PS3 MEM NATIVE] stage=%s query_fail=0x%08x\n",
			stage ? stage : "unknown", (unsigned)result);
	}
#else
	(void)stage;
#endif
}

void ps3_mem_diag_game_info(const char *path, const void *data, size_t size)
{
	DIAG_PRINT("[PS3 MEM] GAME INFO path=%s data=%p size=%lu\n",
		path ? path : "(null)", data, (unsigned long)size);
}

void ps3_mem_diag_note(const char *message, size_t value)
{
	DIAG_PRINT("[PS3 MEM] %s=%lu\n", message ? message : "value", (unsigned long)value);
}

void ps3_mem_diag_frame_event(const char *event, unsigned frame, long value)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	if (event && strcmp(event, "retro_run ENTER") == 0) {
		DIAG_PRINT("[PS3 FRAME] retro_run ENTER frame=%u\n", frame);
		return;
	}
	DIAG_PRINT("[PS3 FRAME] %s frame=%u value=%ld\n", event, frame, value);
#else
	(void)event; (void)frame; (void)value;
#endif
}

void ps3_mem_diag_frame_enabled(void)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	DIAG_PRINT("[PS3 FRAME] diagnostics_enabled=1\n");
#endif
}

void ps3_mem_diag_probe_enabled(void)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	DIAG_PRINT("[PS3 FRAME] diagnostics_enabled=1\n");
	DIAG_PRINT("[PS3 LIFE] diagnostics_enabled=1\n");
#endif
}

void ps3_mem_diag_life(const char *event)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	DIAG_PRINT("[PS3 LIFE] %s\n", event ? event : "unknown");
#else
	(void)event;
#endif
}

static void diag_sample_bytes(const void *data, size_t size, size_t *nonzero, unsigned long *checksum)
{
	const unsigned char *p = (const unsigned char*)data;
	size_t i, limit = size < 16384u ? size : 16384u;
	*nonzero = 0; *checksum = 2166136261u;
	if (!p) return;
	for (i = 0; i < limit; i++) {
		if (p[i]) (*nonzero)++;
		*checksum = (*checksum ^ p[i]) * 16777619u;
	}
}

void ps3_mem_diag_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	size_t nonzero; unsigned long checksum;
	size_t size = pitch * height;
	diag_sample_bytes(data, size, &nonzero, &checksum);
	DIAG_PRINT("[PS3 VIDEO] ptr=%p width=%u height=%u pitch=%lu size=%lu sample_bytes=%lu nonzero=%lu checksum=%08lx\n",
		data, width, height, (unsigned long)pitch, (unsigned long)size,
		(unsigned long)(size < 16384u ? size : 16384u), (unsigned long)nonzero, checksum);
#else
	(void)data; (void)width; (void)height; (void)pitch;
#endif
}

void ps3_mem_diag_audio(const void *data, size_t frames)
{
#if PS3_FBNEO_VERBOSE_DIAGNOSTICS
	size_t nonzero; unsigned long checksum;
	size_t size = frames * 2u * sizeof(short);
	diag_sample_bytes(data, size, &nonzero, &checksum);
	DIAG_PRINT("[PS3 AUDIO] ptr=%p audio_frames=%lu bytes=%lu nonzero_bytes=%lu checksum=%08lx\n",
		data, (unsigned long)frames, (unsigned long)size, (unsigned long)nonzero, checksum);
#else
	(void)data; (void)frames;
#endif
}

void ps3_mem_diag_direct_alloc(const char *purpose, size_t size, const void *ptr, const char *file, int line)
{
	if (size >= (8u * 1024u * 1024u) || ptr == NULL) {
		DIAG_PRINT("[PS3 DIRECT ALLOC] purpose=%s size=%lu ptr=%p result=%s file=%s line=%d\n",
			purpose, (unsigned long)size, ptr, ptr ? "SUCCESS" : "FAIL", file, line);
		diag_native_user_memory(purpose);
	}
}

static void pool_free_space(size_t *total_free, size_t *largest_free)
{
	pool_block *block;
	*total_free = *largest_free = 0;
	for (block = g_first; block != NULL; block = block->next) {
		if (block->is_free && block->magic == POOL_MAGIC) {
			*total_free += block->size;
			if (block->size > *largest_free) *largest_free = block->size;
		}
	}
}

static void update_tracked_peak(void)
{
	size_t live = g_pool_used + g_system_live + g_native_live;
	if (live > g_tracked_live_peak) g_tracked_live_peak = live;
}

void ps3_mem_diag_reset(void)
{
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
	if (g_diag_file != NULL) fclose(g_diag_file);
	g_diag_file = fopen(PS3_MEMORY_DIAGNOSTIC_LOG_PATH, "ab");
	if (g_diag_file == NULL) {
		fprintf(stderr, "[PS3 MEM] unable to open diagnostic log: %s\n", PS3_MEMORY_DIAGNOSTIC_LOG_PATH);
		fflush(stderr);
	}
#endif
	g_pool_used = g_pool_peak = g_largest_pool_allocation = 0;
	g_system_live = g_system_peak = g_largest_system_fallback = 0;
	g_tracked_live_peak = 0;
	g_pool_success_count = g_pool_success_bytes = 0;
	g_system_fallback_count = g_system_fallback_bytes = 0;
	g_allocation_failure_count = g_allocation_failure_bytes = 0;
	g_native_live = g_native_peak = g_native_count = g_native_bytes = 0;
	g_native_failure_count = g_native_failure_bytes = 0;
	memset(g_native_allocations, 0, sizeof(g_native_allocations));
	g_next_allocation_label = NULL;
	g_next_allocation_file = NULL;
	g_next_allocation_line = 0;
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
	memset(g_diag_live, 0, sizeof(g_diag_live));
#endif
	DIAG_PRINT("\n[PS3 MEM] === new game diagnostic session ===\n");
	DIAG_PRINT(PS3_MEMORY_BUILD_CONFIG_LINE);
	DIAG_PRINT(PS3_MEMORY_BUILD_TOTAL_LINE);
	DIAG_PRINT("[PS3 MEM] NATIVE CONFIG enabled=%d\n", (int)PS3_NATIVE_MEMORY);
	diag_native_user_memory("diagnostic_reset");
}

void ps3_mem_diag_set_next_label(const char *label)
{
	g_next_allocation_label = label;
}

void ps3_mem_diag_set_next_site(const char *file, int line)
{
	g_next_allocation_file = file;
	g_next_allocation_line = line;
}

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
static void diag_live_add(void *ptr, size_t requested, size_t allocated, const char *label)
{
	int i;
	if (requested < 65536u) return;
	for (i = 0; i < DIAG_LIVE_MAX; i++) if (g_diag_live[i].ptr == NULL) {
		g_diag_live[i].ptr = ptr; g_diag_live[i].requested_size = requested;
		g_diag_live[i].allocated_size = allocated; g_diag_live[i].label = label;
		g_diag_live[i].file = g_next_allocation_file; g_diag_live[i].line = g_next_allocation_line;
		return;
	}
}

static void diag_live_remove(void *ptr)
{
	int i;
	for (i = 0; i < DIAG_LIVE_MAX; i++) if (g_diag_live[i].ptr == ptr) {
		memset(&g_diag_live[i], 0, sizeof(g_diag_live[i])); return;
	}
}

static void diag_live_top20(void)
{
	int rank, i, best; size_t floor = (size_t)-1;
	DIAG_PRINT("[PS3 MEM LIVE TOP20] tracked allocations >=64KiB (BurnMalloc/native only)\n");
	for (rank = 0; rank < 20; rank++) {
		best = -1;
		for (i = 0; i < DIAG_LIVE_MAX; i++) if (g_diag_live[i].ptr != NULL && g_diag_live[i].allocated_size < floor && (best < 0 || g_diag_live[i].allocated_size > g_diag_live[best].allocated_size)) best = i;
		if (best < 0) break;
		DIAG_PRINT("[PS3 MEM LIVE] rank=%d label=%s requested_size=%lu allocator_size=%lu ptr=%p source_site=%s:%d\n", rank + 1,
			g_diag_live[best].label ? g_diag_live[best].label : "unlabeled", (unsigned long)g_diag_live[best].requested_size,
			(unsigned long)g_diag_live[best].allocated_size, g_diag_live[best].ptr,
			g_diag_live[best].file ? g_diag_live[best].file : "unknown", g_diag_live[best].line);
		floor = g_diag_live[best].allocated_size;
	}
}
#endif

void ps3_mem_diag_stage(const char *stage)
{
#if !PS3_FBNEO_VERBOSE_DIAGNOSTICS
	if (stage && strcmp(stage, "retro_load_game_begin") != 0 &&
		strcmp(stage, "load_success") != 0 &&
	strcmp(stage, "game_load_failure") != 0 &&
	strcmp(stage, "allocation_failure") != 0 &&
	strcmp(stage, "BurnDrvInit_end") != 0)
		return;
#endif
	size_t total_free, largest_free;
	pool_free_space(&total_free, &largest_free);
	DIAG_PRINT("[PS3 MEM SNAPSHOT] stage=%s pool_used=%lu pool_free=%lu largest_free_block=%lu pool_peak=%lu system_live=%lu system_peak=%lu tracked_live_peak=%lu fallback_count=%lu failures=%lu\n",
		stage ? stage : "unknown", (unsigned long)g_pool_used, (unsigned long)total_free,
		(unsigned long)largest_free, (unsigned long)g_pool_peak, (unsigned long)g_system_live,
		(unsigned long)g_system_peak, (unsigned long)g_tracked_live_peak,
		(unsigned long)g_system_fallback_count, (unsigned long)g_allocation_failure_count);
	DIAG_PRINT("[PS3 MEM SNAPSHOT] stage=%s native_live=%lu native_peak=%lu native_count=%lu native_failures=%lu\n",
		stage ? stage : "unknown", (unsigned long)g_native_live, (unsigned long)g_native_peak,
		(unsigned long)g_native_count, (unsigned long)g_native_failure_count);
	diag_native_user_memory(stage);
}

void ps3_mem_diag_summary(const char *reason)
{
	size_t total_free, largest_free;
	pool_free_space(&total_free, &largest_free);
	DIAG_PRINT("[PS3 MEM SUMMARY] reason=%s pool_total=%lu pool_used=%lu pool_free=%lu largest_free_block=%lu pool_peak_used=%lu largest_pool_allocation=%lu pool_success_count=%lu pool_success_bytes=%lu system_live=%lu system_peak=%lu largest_system_fallback=%lu system_fallback_count=%lu system_fallback_bytes=%lu allocation_failure_count=%lu allocation_failure_bytes=%lu BurnMalloc_tracked_live_peak=%lu\n",
		reason ? reason : "unknown", (unsigned long)POOL_SIZE, (unsigned long)g_pool_used,
		(unsigned long)total_free, (unsigned long)largest_free, (unsigned long)g_pool_peak,
		(unsigned long)g_largest_pool_allocation, (unsigned long)g_pool_success_count,
		(unsigned long)g_pool_success_bytes, (unsigned long)g_system_live,
		(unsigned long)g_system_peak, (unsigned long)g_largest_system_fallback,
		(unsigned long)g_system_fallback_count, (unsigned long)g_system_fallback_bytes,
		(unsigned long)g_allocation_failure_count, (unsigned long)g_allocation_failure_bytes,
		(unsigned long)g_tracked_live_peak);
	DIAG_PRINT("[PS3 MEM SUMMARY] reason=%s native_live=%lu native_peak=%lu native_allocations=%lu native_bytes=%lu native_failures=%lu native_failure_bytes=%lu\n",
		reason ? reason : "unknown", (unsigned long)g_native_live, (unsigned long)g_native_peak,
		(unsigned long)g_native_count, (unsigned long)g_native_bytes,
		(unsigned long)g_native_failure_count, (unsigned long)g_native_failure_bytes);
	diag_live_top20();
	diag_native_user_memory(reason);
}

static size_t align_128(size_t value)
{
	return (value + (POOL_ALIGNMENT - 1u)) & ~(size_t)(POOL_ALIGNMENT - 1u);
}

static size_t align_up(size_t value, size_t alignment)
{
	return (value + alignment - 1u) & ~(alignment - 1u);
}

static int is_native_label(const char *label)
{
#if PS3_NATIVE_MEMORY
	return !strcmp(label, "NeoSpriteROM") ||
		!strcmp(label, "NeoCMCDecryptTemp") ||
		!strcmp(label, "NeoPCBDecryptTemp") ||
		!strcmp(label, "Neo68KROM") ||
		!strcmp(label, "NeoADPCM_A_ROM") ||
		!strcmp(label, "NeoADPCM_B_ROM");
#else
	(void)label;
	return 0;
#endif
}

static void *native_malloc(size_t size, size_t requested_size, const char *label)
{
#if PS3_NATIVE_MEMORY
	size_t page_size = 0x10000u;
	uint64_t page_flag = (page_size == 0x100000u) ? SYS_MEMORY_PAGE_SIZE_1M : SYS_MEMORY_PAGE_SIZE_64K;
	size_t allocated_size = align_up(size, page_size);
	sys_addr_t address = 0;
	int result;
	int slot;

	diag_native_user_memory("before_native_allocate");
	result = sys_memory_allocate(allocated_size, page_flag, &address);
	if (result != 0 && page_flag == SYS_MEMORY_PAGE_SIZE_1M) {
		DIAG_PRINT("[PS3 MEM] NATIVE RETRY label=%s requested_size=%lu first_page_size=1048576 result=0x%08x\n",
			label, (unsigned long)requested_size, (unsigned)result);
		page_size = 0x10000u;
		page_flag = SYS_MEMORY_PAGE_SIZE_64K;
		allocated_size = align_up(size, page_size);
		result = sys_memory_allocate(allocated_size, page_flag, &address);
	}
	if (result != 0) {
		g_native_failure_count++;
		g_native_failure_bytes += requested_size;
		DIAG_PRINT("[PS3 MEM] NATIVE FAIL label=%s requested_size=%lu allocator_size=%lu page_size=%lu result=0x%08x\n",
			label, (unsigned long)requested_size, (unsigned long)size,
			(unsigned long)page_size, (unsigned)result);
		diag_native_user_memory("native_allocate_failure");
		return NULL;
	}

	for (slot = 0; slot < NATIVE_ALLOCATION_MAX; slot++) {
		if (g_native_allocations[slot].ptr == NULL) break;
	}
	if (slot == NATIVE_ALLOCATION_MAX) {
		sys_memory_free(address);
		g_native_failure_count++;
		g_native_failure_bytes += requested_size;
		DIAG_PRINT("[PS3 MEM] NATIVE FAIL label=%s reason=tracking_table_full\n", label);
		return NULL;
	}

	g_native_allocations[slot].ptr = (void *)(uintptr_t)address;
	g_native_allocations[slot].allocated_size = allocated_size;
	g_native_allocations[slot].requested_size = requested_size;
	g_native_allocations[slot].page_flag = page_flag;
	g_native_allocations[slot].label = label;
	g_native_live += allocated_size;
	if (g_native_live > g_native_peak) g_native_peak = g_native_live;
	g_native_count++;
	g_native_bytes += requested_size;
	update_tracked_peak();
	DIAG_PRINT("[PS3 MEM] NATIVE SUCCESS label=%s requested_size=%lu allocator_size=%lu allocated_size=%lu page_size=%lu ptr=%p source=PS3_NATIVE\n",
		label, (unsigned long)requested_size, (unsigned long)size,
		(unsigned long)allocated_size, (unsigned long)page_size,
		(void *)(uintptr_t)address);
	diag_live_add((void *)(uintptr_t)address, requested_size, allocated_size, label);
	diag_native_user_memory("after_native_allocate");
	return (void *)(uintptr_t)address;
#else
	(void)size;
	(void)requested_size;
	(void)label;
	return NULL;
#endif
}

int pool_init(void)
{
	if (g_pool != NULL) return 1;
	diag_native_user_memory("before_pool_allocate");
	g_pool = (uint8_t *)memalign(POOL_ALIGNMENT, POOL_SIZE);
	if (g_pool == NULL) {
		DIAG_PRINT("[PS3 MEM] POOL BACKING FAIL size=%lu align=%u\n", (unsigned long)POOL_SIZE, (unsigned)POOL_ALIGNMENT);
		return 0;
	}
	memset(g_pool, 0, POOL_SIZE);
	g_first = (pool_block *)g_pool;
	g_first->size = POOL_SIZE - sizeof(pool_block);
	g_first->allocation = g_pool;
	g_first->magic = POOL_MAGIC;
	g_first->is_free = 1;
	DIAG_PRINT(PS3_MEMORY_BUILD_CONFIG_LINE);
	DIAG_PRINT(PS3_MEMORY_BUILD_TOTAL_LINE);
	DIAG_PRINT("[PS3 MEM] POOL BACKING SUCCESS size=%lu align=%u ptr=%p\n", (unsigned long)POOL_SIZE, (unsigned)POOL_ALIGNMENT, g_pool);
	diag_native_user_memory("after_pool_allocate");
	return 1;
}

void pool_destroy(void)
{
	int i;
	ps3_mem_diag_summary("pool_destroy");
	for (i = 0; i < NATIVE_ALLOCATION_MAX; i++) {
		if (g_native_allocations[i].ptr != NULL) {
			sys_memory_free((sys_addr_t)(uintptr_t)g_native_allocations[i].ptr);
			g_native_allocations[i].ptr = NULL;
		}
	}
	g_native_live = 0;
	if (g_pool != NULL) free(g_pool);
	g_pool = NULL;
	g_first = NULL;
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
	if (g_diag_file != NULL) fclose(g_diag_file);
	g_diag_file = NULL;
#endif
}

static void *fallback_malloc(size_t size, size_t requested_size, const char *label)
{
	size_t payload = align_128(size ? size : 1u);
	pool_block *block = (pool_block *)memalign(POOL_ALIGNMENT, sizeof(pool_block) + payload);
	if (block == NULL) {
		g_allocation_failure_count++;
		g_allocation_failure_bytes += requested_size;
		DIAG_PRINT("[PS3 MEM] SYSTEM FAIL label=%s requested_size=%lu allocator_size=%lu align=%u result=TOTAL_FAILURE\n", label, (unsigned long)requested_size, (unsigned long)size, (unsigned)POOL_ALIGNMENT);
		ps3_mem_diag_stage("allocation_failure");
		return NULL;
	}
	block->size = payload;
	block->allocation = block;
	block->magic = FALLBACK_MAGIC;
	block->is_free = 0;
	DIAG_STAT(g_system_fallback_count++);
	DIAG_STAT(g_system_fallback_bytes += requested_size);
	DIAG_STAT(g_system_live += payload);
	DIAG_STAT(if (g_system_live > g_system_peak) g_system_peak = g_system_live);
	DIAG_STAT(if (payload > g_largest_system_fallback) g_largest_system_fallback = payload);
	DIAG_STAT(update_tracked_peak());
	if (requested_size >= 256u * 1024u)
		DIAG_PRINT("[PS3 MEM] SYSTEM SUCCESS label=%s requested_size=%lu allocator_size=%lu align=%u ptr=%p result=SYSTEM_FALLBACK_SUCCESS\n", label, (unsigned long)requested_size, (unsigned long)size, (unsigned)POOL_ALIGNMENT, (uint8_t *)block + sizeof(pool_block));
	diag_live_add((uint8_t *)block + sizeof(pool_block), requested_size, payload, label);
	return (uint8_t *)block + sizeof(pool_block);
}

void *pool_malloc(size_t size)
{
	return pool_malloc_tagged(size, size);
}

void *pool_malloc_tagged(size_t size, size_t requested_size)
{
	pool_block *block;
	size_t wanted = align_128(size ? size : 1u);
	size_t total_free, largest_free;
	const char *label = g_next_allocation_label ? g_next_allocation_label : "unlabeled";
	g_next_allocation_label = NULL;
	g_next_allocation_file = NULL;
	g_next_allocation_line = 0;

	if (is_native_label(label)) {
		void *native_ptr = native_malloc(size, requested_size, label);
		if (native_ptr != NULL) return native_ptr;
		DIAG_PRINT("[PS3 MEM] NATIVE fallback -> legacy allocator label=%s\n", label);
	}

	/* CPS3 and similar boards allocate one block larger than the entire pool.
	 * Keeping an unused 64 MiB reserve beside that fallback allocation causes
	 * avoidable OOM on PS3. Release it only while it is still wholly unused;
	 * subsequent allocations continue through the aligned fallback path. */
	if (wanted > POOL_SIZE - sizeof(pool_block) && g_first != NULL &&
		g_first->is_free && g_first->prev == NULL && g_first->next == NULL) {
		free(g_pool);
		g_pool = NULL;
		g_first = NULL;
	}
	for (block = g_first; block != NULL; block = block->next) {
		if (!block->is_free || block->size < wanted) continue;
		if (block->size >= wanted + sizeof(pool_block) + POOL_ALIGNMENT) {
			pool_block *split = (pool_block *)((uint8_t *)block + sizeof(pool_block) + wanted);
			split->size = block->size - wanted - sizeof(pool_block);
			split->prev = block;
			split->next = block->next;
			split->allocation = g_pool;
			split->magic = POOL_MAGIC;
			split->is_free = 1;
			if (split->next != NULL) split->next->prev = split;
			block->next = split;
			block->size = wanted;
		}
		block->is_free = 0;
		memset((uint8_t *)block + sizeof(pool_block), 0, block->size);
		DIAG_STAT(g_pool_used += block->size);
		DIAG_STAT(if (g_pool_used > g_pool_peak) g_pool_peak = g_pool_used);
		DIAG_STAT(if (wanted > g_largest_pool_allocation) g_largest_pool_allocation = wanted);
		DIAG_STAT(g_pool_success_count++);
		DIAG_STAT(g_pool_success_bytes += requested_size);
		DIAG_STAT(update_tracked_peak());
		if (requested_size >= 256u * 1024u)
			DIAG_PRINT("[PS3 MEM] ALLOC label=%s requested_size=%lu allocator_size=%lu align=%u source=POOL ptr=%p result=POOL_SUCCESS\n", label, (unsigned long)requested_size, (unsigned long)size, (unsigned)POOL_ALIGNMENT, (uint8_t *)block + sizeof(pool_block));
		diag_live_add((uint8_t *)block + sizeof(pool_block), requested_size, block->size, label);
		return (uint8_t *)block + sizeof(pool_block);
	}
	#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
	pool_free_space(&total_free, &largest_free);
	if (requested_size >= 256u * 1024u) {
		DIAG_PRINT("[PS3 MEM] POOL MISS label=%s requested_size=%lu allocator_size=%lu pool_used=%lu total_free=%lu largest_free_block=%lu\n", label, (unsigned long)requested_size, (unsigned long)size, (unsigned long)g_pool_used, (unsigned long)total_free, (unsigned long)largest_free);
		DIAG_PRINT("[PS3 MEM] fallback -> SYSTEM\n");
	}
	#endif
	return fallback_malloc(size, requested_size, label);
}

void pool_free(void *ptr)
{
	pool_block *block;
	int i;
	if (ptr == NULL) return;
	for (i = 0; i < NATIVE_ALLOCATION_MAX; i++) {
		if (g_native_allocations[i].ptr == ptr) {
			int result;
			diag_native_user_memory("before_native_free");
			result = sys_memory_free((sys_addr_t)(uintptr_t)ptr);
			DIAG_PRINT("[PS3 MEM] NATIVE FREE label=%s ptr=%p allocated_size=%lu result=0x%08x\n",
				g_native_allocations[i].label, ptr,
				(unsigned long)g_native_allocations[i].allocated_size, (unsigned)result);
			if (result == 0) {
				diag_live_remove(ptr);
				if (g_native_live >= g_native_allocations[i].allocated_size)
					g_native_live -= g_native_allocations[i].allocated_size;
				memset(&g_native_allocations[i], 0, sizeof(g_native_allocations[i]));
			}
			diag_native_user_memory("after_native_free");
			return;
		}
	}
	block = (pool_block *)((uint8_t *)ptr - sizeof(pool_block));
	if (block->magic == FALLBACK_MAGIC) {
		diag_live_remove(ptr);
		DIAG_STAT(if (g_system_live >= block->size) g_system_live -= block->size);
		free(block->allocation);
		return;
	}
	if (block->magic != POOL_MAGIC || g_pool == NULL ||
		(uint8_t *)block < g_pool || (uint8_t *)block >= g_pool + POOL_SIZE) return;
	diag_live_remove(ptr);
	DIAG_STAT(if (!block->is_free && g_pool_used >= block->size) g_pool_used -= block->size);
	block->is_free = 1;
	if (block->next != NULL && block->next->is_free && block->next->magic == POOL_MAGIC) {
		pool_block *next = block->next;
		block->size += sizeof(pool_block) + next->size;
		block->next = next->next;
		if (block->next != NULL) block->next->prev = block;
	}
	if (block->prev != NULL && block->prev->is_free && block->prev->magic == POOL_MAGIC) {
		pool_block *prev = block->prev;
		prev->size += sizeof(pool_block) + block->size;
		prev->next = block->next;
		if (prev->next != NULL) prev->next->prev = prev;
	}
}
#endif
