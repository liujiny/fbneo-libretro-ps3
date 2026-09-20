#ifndef FBNEO_PS3_MEMORY_POOL_H
#define FBNEO_PS3_MEMORY_POOL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int pool_init(void);
void pool_destroy(void);
void *pool_malloc(size_t size);
void *pool_malloc_tagged(size_t allocator_size, size_t requested_size);
void pool_free(void *ptr);
void ps3_mem_diag_reset(void);
void ps3_mem_diag_set_next_label(const char *label);
void ps3_mem_diag_set_next_site(const char *file, int line);
void ps3_mem_diag_stage(const char *stage);
void ps3_mem_diag_summary(const char *reason);
void ps3_mem_diag_game_info(const char *path, const void *data, size_t size);
void ps3_mem_diag_note(const char *message, size_t value);
void ps3_mem_diag_logf(const char *format, ...);
void ps3_mem_diag_frame_event(const char *event, unsigned frame, long value);
void ps3_mem_diag_frame_enabled(void);
void ps3_mem_diag_probe_enabled(void);
void ps3_mem_diag_life(const char *event);
void ps3_mem_diag_video(const void *data, unsigned width, unsigned height, size_t pitch);
void ps3_mem_diag_audio(const void *data, size_t frames);
void ps3_mem_diag_direct_alloc(const char *purpose, size_t size, const void *ptr, const char *file, int line);

/* Optional low-overhead PS3 frame/cache profiler. */
enum ps3_perf_metric {
	PS3_PERF_SPRITE_DRAW,
	PS3_PERF_SPRITE_DECODE,
	PS3_PERF_SPRITE_RASTER,
	PS3_PERF_SPRITE_NOZOOM,
	PS3_PERF_SPRITE_ZOOM,
	PS3_PERF_COLOR_EXPAND,
	PS3_PERF_COLOR_SPU,
	PS3_PERF_COLOR_PPU,
	PS3_PERF_AUDIO,
	PS3_PERF_VIDEO
};
enum ps3_perf_cache { PS3_PERF_MASK_CACHE, PS3_PERF_COLOR_CACHE };
typedef struct ps3_perf_cache_stats {
	unsigned int hits, misses, seeks, reads;
	unsigned long long bytes_read, io_us, async_io_us;
	unsigned int repeated_page_reads, adjacent_page_reads, sequential_page_reads;
	unsigned int way0_hits, way1_hits, evictions, conflict_replacements;
} ps3_perf_cache_stats;
typedef struct ps3_perf_stats {
	unsigned long long frames, frame_total_us, frame_max_us, sprite_count_total;
	unsigned long long sprite_draw_us, sprite_decode_us, sprite_raster_sampled_us;
	unsigned long long sprite_nozoom_us, sprite_zoom_us, sprite_nozoom_count, sprite_zoom_count;
	unsigned long long color_expand_sampled_us, color_spu_sampled_us, color_ppu_sampled_us;
	unsigned long long color_spu_samples, color_ppu_samples, audio_us, video_us;
	unsigned int prefetch_issued, prefetch_hits, prefetch_wasted;
	unsigned int prefetch_bytes, avoided_sync_reads;
	ps3_perf_cache_stats mask, color;
} ps3_perf_stats;

#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
extern ps3_perf_stats g_ps3_perf_stats;
unsigned long long ps3_perf_now_us(void);
void ps3_perf_record_time(enum ps3_perf_metric metric, unsigned long long us);
void ps3_perf_record_sprite_count(unsigned int count);
void ps3_perf_record_cache_page(enum ps3_perf_cache cache, unsigned int page);
void ps3_perf_reset(void);
static inline void ps3_perf_record_cache_access(enum ps3_perf_cache cache, int hit)
{
	ps3_perf_cache_stats *stats = cache == PS3_PERF_MASK_CACHE ?
		&g_ps3_perf_stats.mask : &g_ps3_perf_stats.color;
	if (hit) stats->hits++;
	else stats->misses++;
}
static inline void ps3_perf_record_color_way_hit(unsigned int way)
{
	if (way) g_ps3_perf_stats.color.way1_hits++;
	else g_ps3_perf_stats.color.way0_hits++;
}
static inline void ps3_perf_record_color_eviction(int conflict)
{
	g_ps3_perf_stats.color.evictions++;
	if (conflict) g_ps3_perf_stats.color.conflict_replacements++;
}
void ps3_perf_record_prefetch(unsigned int issued, unsigned int hits,
	unsigned int wasted, unsigned int bytes, unsigned int avoided);
void ps3_perf_record_cache_io(enum ps3_perf_cache cache, unsigned seeks, unsigned reads,
	unsigned long long bytes, unsigned long long us);
void ps3_perf_record_prefetch_io(unsigned seeks, unsigned reads,
	unsigned long long bytes, unsigned long long us);
void ps3_perf_frame_sample(unsigned long long frame_us, ps3_perf_stats *out);
#else
#define ps3_perf_now_us() (0ULL)
#define ps3_perf_record_time(metric, us) ((void)0)
#define ps3_perf_record_sprite_count(count) ((void)0)
#define ps3_perf_record_cache_page(cache, page) ((void)0)
#define ps3_perf_reset() ((void)0)
#define ps3_perf_record_cache_access(cache, hit) ((void)0)
#define ps3_perf_record_color_way_hit(way) ((void)0)
#define ps3_perf_record_color_eviction(conflict) ((void)0)
#define ps3_perf_record_prefetch(issued, hits, wasted, bytes, avoided) ((void)0)
#define ps3_perf_record_cache_io(cache, seeks, reads, bytes, us) ((void)0)
#define ps3_perf_record_prefetch_io(seeks, reads, bytes, us) ((void)0)
#define ps3_perf_frame_sample(frame_us, out) ((void)0)
#endif

#if defined(__PSL1GHT__) && defined(PS3_PGM_COLOR_PREFETCH) && PS3_PGM_COLOR_PREFETCH
void pgm_ps3_color_cache_prefetch_drain(void);
#else
#define pgm_ps3_color_cache_prefetch_drain() ((void)0)
#endif
#ifdef __cplusplus
}
#endif
#endif
