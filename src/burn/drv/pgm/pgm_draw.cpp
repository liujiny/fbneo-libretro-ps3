#include <string.h>
#include "pgm.h"

#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
#include "ps3_sprite_color_shadow.h"
#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
#include "ps3_sprite_worker.h"
#endif
#endif
#include "pgm_sprite.h"

#ifdef __PS3__
#include "../../ps3_memory_pool.h"
#define PS3_PGM_MEM_LABEL(x) ps3_mem_diag_set_next_label(x)
#else
#define PS3_PGM_MEM_LABEL(x) ((void)0)
#endif

/*
	Video flag notes (b0e000 writes)

	0000 0000 0000 000x - sprite dma enable - pulse 0->1 to trigger (does 1->0 work?)
	0000 0000 0000 00x0 - nothing?
	0000 0000 0000 0x00 - IRQ4 ACK - pulse to trigger (0->1 and 1->0 work?)
	0000 0000 0000 x000 - IRQ6 ACK - pulse to trigger (0->1 and 1->0 work?)
	0000 0000 0xx0 0000 - all games except CAVE, but seems to serve no purpose when set?
	0000 0000 x000 0000	- loses refresh rate?
	0000 000x 0000 0000 - shows garbage on screen for all except background?
	0000 00x0 0000 0000 - disable everything except background layer?
	0000 0x00 0000 0000 - nothing?
	0000 x000 0000 0000 - disable text layer
	000x 0000 0000 0000 - disable background layer
	00x0 0000 0000 0000 - disable high priority sprites
	xx00 0000 0000 0000 - nothing?


	Sprite zooming notes:
	
	Dragon World II - set to English language has horrible zoom problems - this happens on real hardware
*/

//#define DUMP_SPRITE_BITMAPS
//#define DRAW_SPRITE_NUMBER

static INT32 enable_blending = 0;
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
static INT32 pgm_perf_sample_sprite;
static INT32 pgm_perf_in_color_block;
#endif

static inline UINT8 pgm_sprite_color(UINT32 pixel)
{
	pixel &= nPGMSPRColMaskLen;
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	static UINT32 perf_sample_counter;
	INT32 perf_sample = !pgm_perf_in_color_block && ((++perf_sample_counter & 255) == 0);
	unsigned long long perf_start = perf_sample ? ps3_perf_now_us() : 0;
	unsigned long long io_before = perf_sample ? g_ps3_perf_stats.color.io_us : 0;
	UINT8 color;
	if (!nPGMSPRColPacked) {
		color = PGMSPRColROM[pixel];
	} else {
		UINT32 actual = (nPGMSPRColROMLen / 2) * 3;
		if (pixel >= actual) color = 0;
		else {
			UINT32 pair = pixel / 3;
			UINT32 shift = (pixel % 3) * 5;
			UINT16 packed = pgm_ps3_color_read_pair(pair);
			color = (packed >> shift) & 0x1f;
		}
	}
	if (perf_sample) {
		unsigned long long elapsed = ps3_perf_now_us() - perf_start;
		unsigned long long io_elapsed = g_ps3_perf_stats.color.io_us - io_before;
		ps3_perf_record_time(PS3_PERF_COLOR_EXPAND,
			(elapsed > io_elapsed ? elapsed - io_elapsed : 0) * 256);
	}
	return color;
#elif defined(__PS3__)
	if (!nPGMSPRColPacked) return PGMSPRColROM[pixel];
	UINT32 actual = (nPGMSPRColROMLen / 2) * 3;
	if (pixel >= actual) return 0;
	UINT32 pair = pixel / 3;
	UINT32 shift = (pixel % 3) * 5;
	UINT16 packed = pgm_ps3_color_read_pair(pair);
	return (packed >> shift) & 0x1f;
#else
	return PGMSPRColROM[pixel];
#endif
}





#if defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER

#ifndef PS3_PGM_SPU_SHADOW_INTERVAL
#define PS3_PGM_SPU_SHADOW_INTERVAL 1
#endif
#if PS3_PGM_SPU_SHADOW_INTERVAL < 1
#error PS3_PGM_SPU_SHADOW_INTERVAL must be at least 1
#endif

/* Long diagnostic records are written to disk.  At roughly 11k color8
 * calls/frame, the old 64k cadence logged about once every six frames.
 */
#define PS3_PGM_SPU_STATS_CALL_INTERVAL 0x400000u

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
extern "C" void ps3_mem_diag_logf(
        const char *format, ...);
#endif

/*
 * SPU v1-H4 tail10 fresh4 lag-diagnostic compact async pipeline.
 *
 * Current call is decoded by PPU while SPU speculates
 * future +8 color blocks.  Later calls poll without waiting.
 *
 * One SPU job is in flight at a time in E1.  This preserves
 * the proven shared-EA protocol while overlapping PPU work
 * with SPU packed-color decode.
 */
/*
 * v1-G batch builder.
 *
 * Build a render-only packed-color batch beginning at
 * start_pixel.
 */
static inline UINT32
pgm_ps3_spu_prepare_color_batch(
        ps3_spu_color_batch *batch,
        UINT32 start_pixel,
        UINT32 actual,
        UINT32 batch_limit)
{
        UINT32 count = 0;
        UINT32 pair = start_pixel / 3;
        UINT32 phase = start_pixel - pair * 3;
        UINT32 offset = pair << 1;
        ps3_spu_color_stream_batch *stream =
                (ps3_spu_color_stream_batch *)batch;

        stream->magic = PS3_SPU_SIGNAL_COLOR_STREAM;
        stream->count = 0;
        stream->phase = phase;
        stream->packed_bytes = 0;

        /* C1.7: a complete in-page batch needs only one range check.
         * Keep the item-wise path for ROM/mask tails and page boundaries:
         * its stopping point is part of the existing pipeline behavior.
         */
        if (batch_limit && batch_limit <= PS3_SPU_COLOR_BATCH_SIZE &&
            start_pixel < actual &&
            start_pixel <= (UINT32)nPGMSPRColMaskLen)
        {
                UINT32 last_delta = batch_limit * 8 - 1;
                UINT32 packed_bytes = ((phase + batch_limit * 8 + 2) / 3) * 2;

                if (last_delta < actual - start_pixel &&
                    last_delta <= (UINT32)nPGMSPRColMaskLen - start_pixel &&
                    packed_bytes <= PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES &&
                    (!nPGMSPRColFileCacheActive ||
                     packed_bytes <= PGM_PS3_COLOR_PAGE_SIZE -
                         (offset & (PGM_PS3_COLOR_PAGE_SIZE - 1))))
                        count = batch_limit;
        }

        while (count < batch_limit)
        {
                UINT32 p =
                        start_pixel +
                        count * 8;

                if (p + 7 >= actual ||
                    p + 7 >
                        (UINT32)nPGMSPRColMaskLen)
                {
                        break;
                }

                UINT32 end_pixel = phase + (count + 1) * 8;
                UINT32 packed_bytes = ((end_pixel + 2) / 3) * 2;
                UINT32 item_pair = p / 3;
                UINT32 item_phase = p - item_pair * 3;
                UINT32 item_offset = item_pair << 1;
                UINT32 item_bytes = item_phase == 2 ? 8 : 6;

                if (packed_bytes > PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES ||
                    (nPGMSPRColFileCacheActive &&
                    item_bytes > PGM_PS3_COLOR_PAGE_SIZE -
                        (item_offset & (PGM_PS3_COLOR_PAGE_SIZE - 1)))
                    )
                        break;

                count++;
        }

        if (count)
        {
                UINT32 end_pixel = phase + count * 8;
                UINT32 packed_bytes = ((end_pixel + 2) / 3) * 2;
                UINT32 first_bytes = PGM_PS3_COLOR_PAGE_SIZE -
                        (offset & (PGM_PS3_COLOR_PAGE_SIZE - 1));
                const UINT8 *span;

                if (!nPGMSPRColFileCacheActive || first_bytes > packed_bytes)
                        first_bytes = packed_bytes;

                span = pgm_ps3_color_cache_span(offset, first_bytes);

                if (!span)
                        count = 0;
                else
                {
                        UINT32 dma_bytes;
                        UINT32 used_bytes;

                        stream->count = count;
                        stream->packed_bytes = packed_bytes;
                        memcpy(stream->packed, span, first_bytes);

                        if (first_bytes < packed_bytes)
                        {
                                span = pgm_ps3_color_cache_span(
                                        offset + first_bytes,
                                        packed_bytes - first_bytes);
                                if (!span)
                                {
                                        stream->count = 0;
                                        count = 0;
                                        return 0;
                                }
                                memcpy(stream->packed + first_bytes, span,
                                        packed_bytes - first_bytes);
                        }
                        dma_bytes = PS3_SPU_COLOR_STREAM_DMA_BYTES(packed_bytes);
                        used_bytes = PS3_SPU_COLOR_STREAM_HEADER_BYTES + packed_bytes;
                        memset((UINT8 *)stream + used_bytes, 0,
                                dma_bytes - used_bytes);
                }
        }

        return count;
}



static UINT64 stat_shadow_compare;
static UINT64 stat_shadow_skipped;
static UINT64 stat_shadow_bad;
static UINT64 stat_shadow_pixel_bad;

static inline INT32 pgm_ps3_spu_shadow_sample()
{
#if PS3_PGM_SPU_SHADOW_INTERVAL == 1
        return 1;
#else
        static UINT32 remaining;
        if (remaining == 0)
        {
                remaining = PS3_PGM_SPU_SHADOW_INTERVAL - 1;
                return 1;
        }
        remaining--;
        stat_shadow_skipped++;
        return 0;
#endif
}


static inline INT32 pgm_ps3_spu_batch_color8(
        UINT32 pixel,
        UINT32 actual,
        UINT8 *out)
{
        /*
         * v1-G:
         *
         * cache_results holds the completed batch currently
         * being consumed by the renderer.
         *
         * submit_batch is independent staging storage for
         * the NEXT SPU job.
         *
         * This allows:
         *
         *   PPU consumes batch A
         *        while
         *   SPU decodes batch B
         */
        static ps3_spu_color_result_batch cache_results
                __attribute__((aligned(128)));

        static ps3_spu_color_batch submit_batch
                __attribute__((aligned(128)));

        static UINT32 cache_pixel;
        static UINT32 cache_count;
        static UINT32 cache_index;
        static UINT32 cache_generation;
        static UINT32 cache_origin;

        static UINT32 inflight_pixel;
        static UINT32 inflight_count;
        static UINT32 inflight_generation;
        static UINT32 inflight_origin;
        static INT32 inflight;

        static UINT32 last_pixel;
        static UINT32 sequential_run;
        static UINT32 stream_generation;
        static INT32 have_last;

        static UINT32 stat_calls;
        static UINT32 stat_submit;
        static UINT32 stat_done;
        static UINT32 stat_decoded;
        static UINT32 stat_used;
        static UINT32 stat_cache_hits;
        static UINT32 stat_abandoned;
        static UINT32 stat_pending_poll;
        static UINT32 stat_ppu;

        static UINT32 stat_on_time_jobs;
        static UINT32 stat_early_ready_jobs;
        static UINT32 stat_late_hit_jobs;
        static UINT32 stat_late_miss_jobs;
        static UINT32 stat_stream_break_jobs;

        static UINT64 stat_late_skip_items;
        static UINT64 stat_lag_index_sum;
        static UINT32 stat_lag_index_max;

        static UINT32 stat_lag_0;
        static UINT32 stat_lag_1_3;
        static UINT32 stat_lag_4_7;
        static UINT32 stat_lag_8_15;
        static UINT32 stat_lag_16_31;
        static UINT32 stat_lag_32_plus;

        static UINT32 stat_lead1_submit;
        static UINT32 stat_lead2_submit;
        static UINT32 stat_lead4_submit;

static UINT64 stat_stream_short_skip;
static UINT64 stat_gate12_submit;

        /*
         * v1-G chained-pipeline statistics.
         */
        static UINT32 stat_chain_submit;
        static UINT32 stat_chain_end;
        static UINT32 stat_chain_fail;

        /* v1-H0 origin accounting. */
        static UINT64 stat_fresh_decoded;
        static UINT64 stat_fresh_used;
        static UINT64 stat_fresh_abandoned;

        static UINT64 stat_chain_decoded;
static UINT64 stat_chain_used;
        static UINT64 stat_chain_abandoned;

        /* H4 fresh completion lag diagnostic. */
        static UINT64 stat_fresh_early;
        static UINT64 stat_fresh_lag0;
        static UINT64 stat_fresh_lag1;
        static UINT64 stat_fresh_lag2;
        static UINT64 stat_fresh_lag3;
        static UINT64 stat_fresh_lag4p;
        static UINT64 stat_poll_fresh;
        static UINT64 stat_poll_chain;
        static UINT64 stat_chain_early;
        static UINT64 stat_chain_lag0;
        static UINT64 stat_chain_lag1;
        static UINT64 stat_chain_lag2;
        static UINT64 stat_chain_lag3;
        static UINT64 stat_chain_lag4p;

        INT32 continued;

        stat_calls++;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        if ((stat_calls & (PS3_PGM_SPU_STATS_CALL_INTERVAL - 1u)) == 0)
        {
                UINT32 pending =
                        (cache_index < cache_count)
                        ? (cache_count - cache_index)
                        : 0;

                ps3_mem_diag_logf(
                        "SPU V1-H15-C1.13 color-split stats4m gate12 batch48 lead3 "
                        "calls=%u "
                        "submit=%u "
                        "done=%u "
                        "decoded=%u "
                        "used=%u "
                        "cache_hit=%u "
                        "abandoned=%u "
                        "inflight=%u "
                        "pending=%u "
                        "poll_pending=%u "
                        "ppu=%u "
                        "ontime=%u "
                        "early=%u "
                        "late_hit=%u "
                        "late_miss=%u "
                        "stream_break=%u "
                        "stream_short_skip=%llu "
                        "gate12_submit=%llu "
                        "late_skip=%llu "
                        "lag_sum=%llu "
                        "lag_max=%u "
                        "lag0=%u "
                        "lag1_3=%u "
                        "lag4_7=%u "
                        "lag8_15=%u "
                        "lag16_31=%u "
                        "lag32p=%u "
                        "lead1=%u "
                        "lead2=%u "
                        "lead4=%u "
                        "tail_chain=%u "
                        "chain_end=%u "
                        "chain_fail=%u "
                        "fresh_dec=%llu "
                        "fresh_use=%llu "
                        "fresh_abn=%llu "
                        "chain_dec=%llu "
                        "chain_use=%llu "
                        "chain_abn=%llu "
                        "fresh_early=%llu "
                        "fresh_lag0=%llu "
                        "fresh_lag1=%llu "
                        "fresh_lag2=%llu "
                        "fresh_lag3=%llu "
                        "fresh_lag4p=%llu "
                        "poll_fresh=%llu "
                        "poll_chain=%llu "
                        "chain_early=%llu "
                        "chain_lag0=%llu "
                        "chain_lag1=%llu "
                        "chain_lag2=%llu "
                        "chain_lag3=%llu "
                        "chain_lag4p=%llu "
                        "shadow_interval=%u "
                        "shadow_skipped=%llu "
                        "shadow_cmp=%llu "
                        "shadow_bad=%llu "
                        "shadow_pixel_bad=%llu\n",
                        (unsigned)stat_calls,
                        (unsigned)stat_submit,
                        (unsigned)stat_done,
                        (unsigned)stat_decoded,
                        (unsigned)stat_used,
                        (unsigned)stat_cache_hits,
                        (unsigned)stat_abandoned,
                        (unsigned)(inflight ? 1 : 0),
                        (unsigned)pending,
                        (unsigned)stat_pending_poll,
                        (unsigned)stat_ppu,
                        (unsigned)stat_on_time_jobs,
                        (unsigned)stat_early_ready_jobs,
                        (unsigned)stat_late_hit_jobs,
                        (unsigned)stat_late_miss_jobs,
                        (unsigned)stat_stream_break_jobs,
                        (unsigned long long)stat_stream_short_skip,
                        (unsigned long long)stat_gate12_submit,
                        (unsigned long long)
                                stat_late_skip_items,
                        (unsigned long long)
                                stat_lag_index_sum,
                        (unsigned)stat_lag_index_max,
                        (unsigned)stat_lag_0,
                        (unsigned)stat_lag_1_3,
                        (unsigned)stat_lag_4_7,
                        (unsigned)stat_lag_8_15,
                        (unsigned)stat_lag_16_31,
                        (unsigned)stat_lag_32_plus,
                        (unsigned)stat_lead1_submit,
                        (unsigned)stat_lead2_submit,
                        (unsigned)stat_lead4_submit,
                        (unsigned)stat_chain_submit,
                        (unsigned)stat_chain_end,
                        (unsigned)stat_chain_fail,
                        (unsigned long long)stat_fresh_decoded,
                        (unsigned long long)stat_fresh_used,
                        (unsigned long long)stat_fresh_abandoned,
                        (unsigned long long)stat_chain_decoded,
                        (unsigned long long)stat_chain_used,
                        (unsigned long long)stat_chain_abandoned,
                        (unsigned long long)stat_fresh_early,
                        (unsigned long long)stat_fresh_lag0,
                        (unsigned long long)stat_fresh_lag1,
                        (unsigned long long)stat_fresh_lag2,
                        (unsigned long long)stat_fresh_lag3,
                        (unsigned long long)stat_fresh_lag4p,
                        (unsigned long long)stat_poll_fresh,
                        (unsigned long long)stat_poll_chain,
                        (unsigned long long)stat_chain_early,
                        (unsigned long long)stat_chain_lag0,
                        (unsigned long long)stat_chain_lag1,
                        (unsigned long long)stat_chain_lag2,
                        (unsigned long long)stat_chain_lag3,
                        (unsigned long long)stat_chain_lag4p,
                        (unsigned)PS3_PGM_SPU_SHADOW_INTERVAL,
                        (unsigned long long)stat_shadow_skipped,
                        (unsigned long long)stat_shadow_compare,
                        (unsigned long long)stat_shadow_bad,
                        (unsigned long long)stat_shadow_pixel_bad);

        }
#endif

        /*
         * Track renderer +8 stream.
         */
        continued =
                have_last &&
                pixel ==
                last_pixel + 8;

        if (continued)
        {
                if (sequential_run !=
                    0xffffffffu)
                {
                        sequential_run++;
                }
        }
        else
        {
                sequential_run = 1;
                stream_generation++;
        }

        last_pixel =
                pixel;

        have_last = 1;


        /*
         * First consume an already-completed batch.
         *
         * An SPU job may already be running in parallel
         * while this cache is consumed.
         */
        if (cache_index < cache_count)
        {
                UINT32 next_pixel =
                        cache_pixel +
                        cache_index * 8;

                if (cache_generation ==
                    stream_generation)
                {
                        if (pixel ==
                            next_pixel)
                        {
                                UINT32 remaining;

                                memcpy(
                                        out,
                                        cache_results.result[
                                                cache_index
                                        ],
                                        8);

                                cache_index++;

                                stat_used++;
                                stat_cache_hits++;

                                if (cache_origin == 1)
                                {
                                        stat_fresh_used++;
                                }
                                else if (cache_origin == 2)
                                {
                                        stat_chain_used++;
                                }

                                remaining =
                                        cache_count -
                                        cache_index;

                                /*
                                 * v1-G1 tail-triggered chain.
                                 *
                                 * Do not launch B as soon as A
                                 * completes.
                                 *
                                 * Wait until only eight items of
                                 * A remain.  Measured compact-SPU
                                 * completion lag is <=7 blocks,
                                 * with v1-G normally <=3.
                                 */
                                if (!inflight &&
                                    cache_generation ==
                                        stream_generation &&
                                    remaining == 8)
                                {
                                        UINT32 chain_limit =
                                                16;

                                        UINT32 chain_start;
                                        UINT32 chain_count;

                                        if (sequential_run >=
                                            48)
                                        {
                                                chain_limit =
                                                        40;
                                        }
                                        else if (
                                            sequential_run >=
                                            16)
                                        {
                                                chain_limit =
                                                        32;
                                        }

                                        chain_start =
                                                cache_pixel +
                                                cache_count *
                                                8;

                                        chain_count =
                                                pgm_ps3_spu_prepare_color_batch(
                                                        &submit_batch,
                                                        chain_start,
                                                        actual,
                                                        chain_limit);

                                        if (chain_count)
                                        {
                                                INT32 submit_ret =
                                                        ps3_pgm_spu_color_submit(
                                                                &submit_batch);

                                                if (submit_ret ==
                                                    0)
                                                {
                                                        inflight =
                                                                1;

                                                        inflight_pixel =
                                                                chain_start;

                                                        inflight_count =
                                                                chain_count;

                                                        inflight_generation =
                                                                stream_generation;

                                                        inflight_origin =
                                                                2;

                                                        stat_submit++;
                                                        stat_chain_submit++;
                                                }
                                                else
                                                {
                                                        stat_chain_fail++;
                                                }
                                        }
                                        else
                                        {
                                                stat_chain_end++;
                                        }
                                }

                                return 1;
                        }

                        if (pixel <
                            next_pixel)
                        {
                                stat_ppu++;
                                return 0;
                        }
                }

                /*
                 * Stream changed or renderer jumped past
                 * this completed cache.
                 */
                {
                        UINT32 drop =
                                cache_count -
                                cache_index;

                        stat_abandoned +=
                                drop;

                        if (cache_origin == 1)
                        {
                                stat_fresh_abandoned +=
                                        drop;
                        }
                        else if (cache_origin == 2)
                        {
                                stat_chain_abandoned +=
                                        drop;
                        }
                }

                cache_count = 0;
                cache_index = 0;
                cache_origin = 0;
        }


        /*
         * No consumable completed cache remains.
         *
         * Poll the next SPU job.
         */
        if (inflight)
        {
                UINT32 completed_count = 0;

                INT32 poll_ret =
                        ps3_pgm_spu_color_poll(
                                &cache_results,
                                &completed_count);

                if (poll_ret == 0)
                {
                        UINT32 completed_pixel =
                                inflight_pixel;

                        UINT32 completed_generation =
                                inflight_generation;

                        UINT32 completed_origin =
                                inflight_origin;

                        UINT32 index = 0;

                        INT32 usable = 0;
                        INT32 cache_installed = 0;
                        INT32 serve_now = 0;

                        inflight = 0;
                        inflight_origin = 0;

                        stat_done++;
                        stat_decoded +=
                                completed_count;

                        if (completed_origin == 1)
                        {
                                stat_fresh_decoded +=
                                        completed_count;
                        }
                        else if (completed_origin == 2)
                        {
                                stat_chain_decoded +=
                                        completed_count;
                        }

                        if (completed_generation ==
                            stream_generation)
                        {
                                /*
                                 * Result became ready before
                                 * renderer reached its first
                                 * speculative item.
                                 */
                                if (pixel <
                                    completed_pixel)
                                {
                                        cache_pixel =
                                                completed_pixel;

                                        cache_count =
                                                completed_count;

                                        cache_index = 0;

                                        cache_generation =
                                                completed_generation;

                                        cache_origin =
                                                completed_origin;

                                        cache_installed = 1;

                                        stat_early_ready_jobs++;

                                        if (completed_origin == 1)
                                        {
                                                stat_fresh_early++;
                                        }
                                        else if (completed_origin == 2)
                                        {
                                                stat_chain_early++;
                                        }
                                }
                                else
                                {
                                        UINT32 delta =
                                                pixel -
                                                completed_pixel;

                                        if ((delta & 7u) == 0)
                                        {
                                                index =
                                                        delta >> 3;

                                                stat_lag_index_sum +=
                                                        (UINT64)index;

                                                if (index >
                                                    stat_lag_index_max)
                                                {
                                                        stat_lag_index_max =
                                                                index;
                                                }

                                                if (index == 0)
                                                {
                                                        stat_lag_0++;
                                                }
                                                else if (index <= 3)
                                                {
                                                        stat_lag_1_3++;
                                                }
                                                else if (index <= 7)
                                                {
                                                        stat_lag_4_7++;
                                                }
                                                else if (index <= 15)
                                                {
                                                        stat_lag_8_15++;
                                                }
                                                else if (index <= 31)
                                                {
                                                        stat_lag_16_31++;
                                                }
                                                else
                                                {
                                                        stat_lag_32_plus++;
                                                }

                                                /* H4: fresh-origin completion lag. */
                                                if (completed_origin == 1)
                                                {
                                                        if (index == 0)
                                                        {
                                                                stat_fresh_lag0++;
                                                        }
                                                        else if (index == 1)
                                                        {
                                                                stat_fresh_lag1++;
                                                        }
                                                        else if (index == 2)
                                                        {
                                                                stat_fresh_lag2++;
                                                        }
                                                        else if (index == 3)
                                                        {
                                                                stat_fresh_lag3++;
                                                        }
                                                        else
                                                        {
                                                                stat_fresh_lag4p++;
                                                        }
                                                }

                                                /* H9: chain-origin completion lag. */
                                                if (completed_origin == 2)
                                                {
                                                        if (index == 0)
                                                        {
                                                                stat_chain_lag0++;
                                                        }
                                                        else if (index == 1)
                                                        {
                                                                stat_chain_lag1++;
                                                        }
                                                        else if (index == 2)
                                                        {
                                                                stat_chain_lag2++;
                                                        }
                                                        else if (index == 3)
                                                        {
                                                                stat_chain_lag3++;
                                                        }
                                                        else
                                                        {
                                                                stat_chain_lag4p++;
                                                        }
                                                }

                                                if (index <
                                                    completed_count)
                                                {
                                                        usable = 1;

                                                        if (index == 0)
                                                        {
                                                                stat_on_time_jobs++;
                                                        }
                                                        else
                                                        {
                                                                stat_late_hit_jobs++;

                                                                stat_late_skip_items +=
                                                                        (UINT64)index;
                                                        }
                                                }
                                                else
                                                {
                                                        stat_late_miss_jobs++;
                                                }
                                        }
                                        else
                                        {
                                                stat_stream_break_jobs++;
                                        }
                                }
                        }
                        else
                        {
                                stat_stream_break_jobs++;
                        }

                        if (usable)
                        {
                                stat_abandoned +=
                                        index;

                                if (completed_origin == 1)
                                {
                                        stat_fresh_abandoned +=
                                                index;
                                }
                                else if (completed_origin == 2)
                                {
                                        stat_chain_abandoned +=
                                                index;
                                }

                                cache_pixel =
                                        completed_pixel;

                                cache_count =
                                        completed_count;

                                cache_index =
                                        index + 1;

                                cache_generation =
                                        completed_generation;

                                cache_origin =
                                        completed_origin;

                                memcpy(
                                        out,
                                        cache_results.result[
                                                index
                                        ],
                                        8);

                                stat_used++;

                                if (completed_origin == 1)
                                {
                                        stat_fresh_used++;
                                }
                                else if (completed_origin == 2)
                                {
                                        stat_chain_used++;
                                }

                                cache_installed = 1;
                                serve_now = 1;
                        }


                        /*
                         * v1-G chaining.
                         *
                         * Once a completed batch has been
                         * copied into cache_results, worker's
                         * shared SPU buffer is free.
                         *
                         * Immediately submit the next
                         * contiguous batch so SPU computes
                         * while PPU consumes this cache.
                         */
                        if (cache_installed)
                        {
                                /*
                                 * v1-G1:
                                 *
                                 * Do not immediately submit the
                                 * next batch here.
                                 *
                                 * Tail-triggering occurs while
                                 * consuming cache_results, when
                                 * exactly eight items remain.
                                 */
                                if (serve_now)
                                {
                                        return 1;
                                }

                                stat_ppu++;
                                return 0;
                        }


                        /*
                         * Completed job cannot serve the
                         * current stream.
                         */
                        stat_abandoned +=
                                completed_count;

                        if (completed_origin == 1)
                        {
                                stat_fresh_abandoned +=
                                        completed_count;
                        }
                        else if (completed_origin == 2)
                        {
                                stat_chain_abandoned +=
                                        completed_count;
                        }

                        cache_count = 0;
                        cache_index = 0;
                        cache_origin = 0;
                }
                else if (poll_ret > 0)
                {
                        stat_pending_poll++;

                        if (inflight_origin == 1)
                        {
                                stat_poll_fresh++;
                        }
                        else if (inflight_origin == 2)
                        {
                                stat_poll_chain++;
                        }

                        stat_ppu++;

                        return 0;
                }
                else
                {
                        inflight = 0;
                        inflight_origin = 0;

                        cache_count = 0;
                        cache_index = 0;
                        cache_origin = 0;

                        stat_ppu++;

                        return 0;
                }
        }


        /*
         * Need a minimum sequential stream before starting
         * a fresh speculative chain.
         */
        if (sequential_run < 12)
        {
                stat_stream_short_skip++;
                stat_ppu++;
                return 0;
        }


        UINT32 batch_limit = 15;

        if (sequential_run >= 48)
        {
                batch_limit = 48;
        }
        else if (sequential_run >= 16)
        {
                batch_limit = 32;
        }


        /*
         * Keep F1 lead tuning unchanged.
         *
         * Chained jobs themselves have no extra lead:
         * they begin directly after the previous cache.
         */
        UINT32 lead_blocks = 1;

        if (sequential_run >= 48)
        {
                lead_blocks = 3;
        }
        else if (sequential_run >= 16)
        {
                lead_blocks = 2;
        }

        UINT32 start_pixel =
                pixel +
                lead_blocks * 8;

        UINT32 count =
                pgm_ps3_spu_prepare_color_batch(
                        &submit_batch,
                        start_pixel,
                        actual,
                        batch_limit);

        if (!count)
        {
                stat_ppu++;
                return 0;
        }


        {
                INT32 submit_ret =
                        ps3_pgm_spu_color_submit(
                                &submit_batch);

                if (submit_ret == 0)
                {
                        inflight = 1;

                        inflight_pixel =
                                start_pixel;

                        inflight_count =
                                count;

                        inflight_generation =
                                stream_generation;

                        inflight_origin =
                                1;

                        stat_submit++;
                        stat_gate12_submit++;

                        if (lead_blocks == 1)
                        {
                                stat_lead1_submit++;
                        }
                        else if (lead_blocks == 2)
                        {
                                stat_lead2_submit++;
                        }
                        else
                        {
                                stat_lead4_submit++;
                        }
                }
        }

        stat_ppu++;

        (void)inflight_count;

        return 0;
}
#endif




/* H15_SHADOW_STAGE2_FIXED */
static inline void pgm_sprite_colors8_unpack_span(
        const UINT8 *span,
        UINT32 phase,
        UINT8 *out)
{
        UINT32 w0 = span[0] | (span[1] << 8);
        UINT32 w1 = span[2] | (span[3] << 8);
        UINT32 w2 = span[4] | (span[5] << 8);

        if (phase == 0)
        {
                out[0] = w0 & 0x1f;
                out[1] = (w0 >> 5) & 0x1f;
                out[2] = (w0 >> 10) & 0x1f;
                out[3] = w1 & 0x1f;
                out[4] = (w1 >> 5) & 0x1f;
                out[5] = (w1 >> 10) & 0x1f;
                out[6] = w2 & 0x1f;
                out[7] = (w2 >> 5) & 0x1f;
        }
        else if (phase == 1)
        {
                out[0] = (w0 >> 5) & 0x1f;
                out[1] = (w0 >> 10) & 0x1f;
                out[2] = w1 & 0x1f;
                out[3] = (w1 >> 5) & 0x1f;
                out[4] = (w1 >> 10) & 0x1f;
                out[5] = w2 & 0x1f;
                out[6] = (w2 >> 5) & 0x1f;
                out[7] = (w2 >> 10) & 0x1f;
        }
        else
        {
                UINT32 w3 = span[6] | (span[7] << 8);
                out[0] = (w0 >> 10) & 0x1f;
                out[1] = w1 & 0x1f;
                out[2] = (w1 >> 5) & 0x1f;
                out[3] = (w1 >> 10) & 0x1f;
                out[4] = w2 & 0x1f;
                out[5] = (w2 >> 5) & 0x1f;
                out[6] = (w2 >> 10) & 0x1f;
                out[7] = w3 & 0x1f;
        }
}

static inline void pgm_sprite_colors8_ppu_only(
        UINT32 pixel,
        UINT32 actual,
        UINT8 *out)
{
        if (nPGMSPRColPacked &&
            pixel + 7 < actual &&
            pixel + 7 <=
                (UINT32)nPGMSPRColMaskLen)
        {
                UINT32 pair = pixel / 3;
                INT32 phase =
                    pixel - pair * 3;

                UINT32 span_bytes =
                    (phase == 2) ? 8 : 6;

                const UINT8 *span =
                    pgm_ps3_color_cache_span(
                            pair << 1,
                            span_bytes);

                if (span != NULL)
                {
                        pgm_sprite_colors8_unpack_span(
                                span, phase, out);

                        return;
                }
                else
                {
                        UINT16 packed =
                            pgm_ps3_color_read_pair(
                                    pair);

                        for (INT32 i = 0;
                             i < 8;
                             i++)
                        {
                                out[i] =
                                    (packed >>
                                    (phase * 5))
                                    & 0x1f;

                                if (++phase == 3 &&
                                    i != 7)
                                {
                                        phase = 0;
                                        pair++;

                                        packed =
                                            pgm_ps3_color_read_pair(
                                                    pair);
                                }
                        }

                        return;
                }
        }


        for (INT32 i = 0;
             i < 8;
             i++)
        {
                out[i] =
                    pgm_sprite_color(
                            pixel + i);
        }
}


static inline void pgm_sprite_colors8(UINT32 pixel, UINT8 *out)
{
#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
	static UINT32 color8_counter;
	color8_counter++;
	if (color8_counter == 1 || (color8_counter & 0xfffff) == 0) {
		bprintf(PRINT_IMPORTANT,
			_T("[PS3 PGM PERF] COLOR8 calls=%u\n"), color8_counter);
	}
#endif
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	static UINT32 perf_sample_counter;
	INT32 perf_sample = ((++perf_sample_counter & 63) == 0);
	unsigned long long perf_start = perf_sample ? ps3_perf_now_us() : 0;
	unsigned long long io_before = perf_sample ? g_ps3_perf_stats.color.io_us : 0;
	INT32 perf_spu_hit = 0;
	pgm_perf_in_color_block = 1;
#endif
#ifdef __PS3__
	pixel &= nPGMSPRColMaskLen;
	UINT32 actual = (nPGMSPRColROMLen / 2) * 3;


#if defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
	
if (nPGMSPRColPacked)
{
        UINT8 shadow_spu_out[8];

        if (pgm_ps3_spu_batch_color8(
                pixel,
                actual,
                shadow_spu_out))
        {
		#if defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
		perf_spu_hit = 1;
		#endif
if (pgm_ps3_spu_shadow_sample())
{
        UINT8 shadow_ppu_out[8];

        pgm_sprite_colors8_ppu_only(
                pixel,
                actual,
                shadow_ppu_out);


        stat_shadow_compare++;


        for (INT32 i = 0;
             i < 8;
             i++)
        {
                if (shadow_spu_out[i] !=
                    shadow_ppu_out[i])
                {
                        stat_shadow_bad++;
                        stat_shadow_pixel_bad++;
                }
        }
}


memcpy(
                        out,
                        shadow_spu_out,
                        8);

                goto color8_done;
        }
}

#endif
	if (nPGMSPRColPacked && pixel + 7 < actual && pixel + 7 <= (UINT32)nPGMSPRColMaskLen) {
		UINT32 pair = pixel / 3;
		INT32 phase = pixel - pair * 3;
		
		UINT32 span_bytes = (phase == 2) ? 8 : 6;
		
		
                INT32 shadow_phase = phase;

const UINT8 *span =
			pgm_ps3_color_cache_span(pair << 1, span_bytes);
		
		if (span != NULL) {
			pgm_sprite_colors8_unpack_span(span, phase, out);

		}
		else {
			/* Rare 64 KiB page-boundary case. */
			UINT16 packed = pgm_ps3_color_read_pair(pair);
		
			for (INT32 i = 0; i < 8; i++) {
				out[i] =
					(packed >> (phase * 5)) & 0x1f;
		
				if (++phase == 3 && i != 7) {
					phase = 0;
					pair++;
					packed = pgm_ps3_color_read_pair(pair);
				}
			}
		}
		goto color8_done;
	}
#endif
	for (INT32 i = 0; i < 8; i++) out[i] = pgm_sprite_color(pixel + i);
#ifdef __PS3__
color8_done:
#endif
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	pgm_perf_in_color_block = 0;
	if (perf_sample) {
		unsigned long long elapsed = ps3_perf_now_us() - perf_start;
		unsigned long long io_elapsed = g_ps3_perf_stats.color.io_us - io_before;
		ps3_perf_record_time(PS3_PERF_COLOR_EXPAND,
			(elapsed > io_elapsed ? elapsed - io_elapsed : 0) * 64);
		ps3_perf_record_time(perf_spu_hit ? PS3_PERF_COLOR_SPU : PS3_PERF_COLOR_PPU,
			(elapsed > io_elapsed ? elapsed - io_elapsed : 0) * 64);
	}
#endif
	return;
}

static INT32   nTileMask = 0;
static UINT8   sprmsktab[0x100];
static UINT8  *SpritePrio;		// sprite priorities
#ifdef __PS3__
#define PGM_BG_CACHE_TILES 512
static UINT8 *pgmBgTileCache;
static INT32 pgmBgTileTag[PGM_BG_CACHE_TILES];

static UINT8 *pgm_bg_tile(INT32 code)
{
	INT32 slot = code & (PGM_BG_CACHE_TILES - 1);
	UINT8 *base = pgmBgTileCache + (slot << 10);
	if (pgmBgTileTag[slot] != code) {
		const UINT8 *src = PGMTileROMExp + code * 640;
		UINT8 *dst = base;
		for (INT32 i = 0; i < 128; i++, src += 5, dst += 8) {
			dst[0] = src[0] & 0x1f;
			dst[1] = (src[0] >> 5) | ((src[1] << 3) & 0x18);
			dst[2] = (src[1] >> 2) & 0x1f;
			dst[3] = (src[1] >> 7) | ((src[2] << 1) & 0x1e);
			dst[4] = (src[2] >> 4) | ((src[3] << 4) & 0x10);
			dst[5] = (src[3] >> 1) & 0x1f;
			dst[6] = (src[3] >> 6) | ((src[4] << 2) & 0x1c);
			dst[7] = (src[4] >> 3) & 0x1f;
		}
		pgmBgTileTag[slot] = code;
	}
	return base;
}
#endif
static UINT16 *pTempScreen;		// sprites
static UINT16 *pTempDraw;		// pre-zoomed sprites
static UINT8  *tiletrans;		// tile transparency table
static UINT8  *texttrans;		// text transparency table
static UINT32 *pTempDraw32;		// 32 bit temporary bitmap (blending!)
static UINT8  *pSpriteBlendTable;	// if blending is available, allocate this.

static inline UINT32 alpha_blend(UINT32 d, UINT32 s, UINT32 p)
{
	INT32 a = 255 - p;

	return (((((s & 0xff00ff) * p) + ((d & 0xff00ff) * a)) & 0xff00ff00) +
		((((s & 0x00ff00) * p) + ((d & 0x00ff00) * a)) & 0x00ff0000)) >> 8;
}

#ifdef DRAW_SPRITE_NUMBER

static UINT8 font_pixels[16][7*5] = {
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,0,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// 0
	{ 0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,},// 1
	{ 0,0,0,0,0,0,0,1,1,0,0,1,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,1,0,0,0,0,0,0,},// 2
	{ 0,0,0,0,0,0,1,1,1,0,0,0,0,1,0,0,1,1,1,0,0,0,0,1,0,0,1,1,1,0,0,0,0,0,0,},// 3
	{ 0,0,0,0,0,0,1,0,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,},// 4
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,0,1,1,0,0,0,0,0,1,0,0,1,1,1,0,0,0,0,0,0,},// 5
	{ 0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// 6
	{ 0,0,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,},// 7
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// 8
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,},// 9
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,1,0,1,0,0,1,0,1,0,0,0,0,0,0,},// A
//	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,0,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// B (looks like '8')
	{ 0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// b
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,0,0,0,},// C
//	{ 0,0,0,0,0,0,1,1,0,0,0,1,0,1,0,0,1,0,1,0,0,1,0,1,0,0,1,1,0,0,0,0,0,0,0,},// D (looks like '0')
	{ 0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,1,0,0,1,0,1,0,0,1,1,1,0,0,0,0,0,0,},// d
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,0,1,1,1,0,0,1,0,0,0,0,1,1,1,0,0,0,0,0,0,},// E
	{ 0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,0,1,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,} // F
};

static void draw_font(INT32 sx, INT32 sy, UINT32 code)
{
	INT32 enable1 = 0;

	for (INT32 z = 7; z >= 0; z--)
	{
		INT32 chr = (code >> (z*4))&0xf;

		if (enable1 == 0 && chr == 0) continue; // skip leading 0's
		enable1 = 1;

		UINT8 *gfx = font_pixels[chr];

		if (enable_blending) {
			for (INT32 y = 0; y < 7; y++) {
				if ((sy+y)<0 || (sy+y)>=nScreenHeight) continue; // clip
	
				UINT32 *dst = pTempDraw32 + (sy + y) * nScreenWidth;
	
				for (INT32 x = 0; x < 5; x++) {
					if ((sx+x)<0 || (sx+x)>=nScreenWidth) continue; // clip
	
					INT32 pxl = gfx[(y*5)+x];
					if (pxl) {
						dst[(sx+x)] = 0xffffff;
					} else {
						dst[(sx+x)] = 0x000000;
					}
				}
			}
		} else {
			for (INT32 y = 0; y < 7; y++) {
				if ((sy+y)<0 || (sy+y)>=nScreenHeight) continue; // clip
	
				UINT16 *dst = pTransDraw + (sy + y) * nScreenWidth;
	
				for (INT32 x = 0; x < 5; x++) {
					if ((sx+x)<0 || (sx+x)>=nScreenWidth) continue; // clip
	
					INT32 pxl = gfx[(y*5)+x];
					if (pxl) {
						dst[(sx+x)] = 0x901;
					} else {
						dst[(sx+x)] = 0x900;
					}
				}
			}
		}

		sx += 5;
	}
}

static void pgm_drawsprites_fonts(INT32 priority)
{
	UINT16 *source = PGMSprBuf;
	UINT16 *finish = PGMSprBuf + 0xa00/2;

	while (finish > source)
	{
		if (source[4] == 0) break;

		INT32 xpos =  BURN_ENDIAN_SWAP_INT16(source[0]) & 0x07ff;
		INT32 ypos =  BURN_ENDIAN_SWAP_INT16(source[1]) & 0x03ff;
		INT32 prio = (BURN_ENDIAN_SWAP_INT16(source[2]) & 0x0080) >> 7;
		INT32 boff =((BURN_ENDIAN_SWAP_INT16(source[2]) & 0x007f) << 16) | (BURN_ENDIAN_SWAP_INT16(source[3]) & 0xffff);

		if (priority==prio)
			draw_font(xpos, ypos, boff);

		source += 5;
	}
}
#endif

static inline UINT8 pgm_mask_byte(UINT32 offset)
{
#ifdef __PS3__
	return pgm_ps3_mask_read(offset);
#else
	return PGMSPRMaskROM[offset & nPGMSPRMaskMaskLen];
#endif
}

/* PS3_MASK_SPAN_V1 */
static inline UINT32 pgm_mask_u32(UINT32 offset)
{
#ifdef __PS3__
    const UINT8 *p =
        pgm_ps3_mask_cache_span(offset, 4);

    if (p != NULL) {
        return
            ((UINT32)p[0]      ) |
            ((UINT32)p[1] <<  8) |
            ((UINT32)p[2] << 16) |
            ((UINT32)p[3] << 24);
    }
#endif

    return
        ((UINT32)pgm_mask_byte(offset + 0)      ) |
        ((UINT32)pgm_mask_byte(offset + 1) <<  8) |
        ((UINT32)pgm_mask_byte(offset + 2) << 16) |
        ((UINT32)pgm_mask_byte(offset + 3) << 24);
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

	if (enable_blending) return (r<<16)|(g<<8)|(b<<0);

	return BurnHighCol(r, g, b, 0);
}

static void pgm_prepare_sprite(INT32 wide, INT32 high, INT32 palt, INT32 boffset)
{
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	unsigned long long perf_start = ps3_perf_now_us();
#endif
	UINT16* dest = pTempDraw;
	UINT8 * bdata = PGMSPRMaskROM;
	INT32 bdatasize = nPGMSPRMaskMaskLen;

	wide *= 16;
	palt *= 32;

	UINT32 aoffset = pgm_mask_u32(boffset);
	aoffset = (aoffset >> 2) * 3;

	boffset += 4;

	for (INT32 ycnt = 0; ycnt < high; ycnt++)
	{
		#ifdef __PS3__
		const UINT8 *maskrow =
			pgm_ps3_mask_cache_span(
				boffset, (UINT32)wide >> 3);
		#endif
		for (INT32 xcnt = 0; xcnt < wide; xcnt+=8)
		{
			#ifdef __PS3__
			/* PS3_COLORFAST_V2 */
			UINT8 mask = maskrow ? maskrow[xcnt >> 3] : pgm_mask_byte(boffset);
			
			if (mask == 0xff) {
				dest[xcnt + 0] = 0x8000;
				dest[xcnt + 1] = 0x8000;
				dest[xcnt + 2] = 0x8000;
				dest[xcnt + 3] = 0x8000;
				dest[xcnt + 4] = 0x8000;
				dest[xcnt + 5] = 0x8000;
				dest[xcnt + 6] = 0x8000;
				dest[xcnt + 7] = 0x8000;
			} else {
				UINT8 colors[8];
				pgm_sprite_colors8(aoffset, colors);
			
				if (mask == 0x00) {
					dest[xcnt + 0] = colors[0] + palt;
					dest[xcnt + 1] = colors[1] + palt;
					dest[xcnt + 2] = colors[2] + palt;
					dest[xcnt + 3] = colors[3] + palt;
					dest[xcnt + 4] = colors[4] + palt;
					dest[xcnt + 5] = colors[5] + palt;
					dest[xcnt + 6] = colors[6] + palt;
					dest[xcnt + 7] = colors[7] + palt;
					aoffset += 8;
				} else {
					aoffset += zoom_draw_table[mask](dest + xcnt, colors, palt);
				}
			}
#else
			aoffset += zoom_draw_table[pgm_mask_byte(boffset)](dest + xcnt, PGMSPRColROM + (aoffset & nPGMSPRColMaskLen), palt);
#endif

			boffset++;
		}

		dest += wide;
	}
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	ps3_perf_record_time(PS3_PERF_SPRITE_DECODE, ps3_perf_now_us() - perf_start);
#endif
}

static inline void draw_sprite_line(INT32 wide, UINT16* dest, UINT8 *pdest, INT32 xzoom, INT32 xgrow, INT32 yoffset, INT32 flip, INT32 xpos, INT32 prio)
{
	INT32 xzoombit;
	INT32 xoffset;
	INT32 xcnt = 0, xcntdraw = 0;
	INT32 xdrawpos = 0;

	wide *= 16;
	flip &= 1;

	while (xcnt < wide)
	{
		if (flip) xoffset = wide - xcnt - 1;
		else	  xoffset = xcnt;

		UINT32 srcdat = pTempDraw[yoffset + xoffset];
		xzoombit = (xzoom >> (xcnt & 0x1f)) & 1;

		if (xzoombit == 1 && xgrow == 1)
		{
			xdrawpos = xpos + xcntdraw;

			if (!(srcdat & 0x8000))
			{
				if ((xdrawpos >= 0) && (xdrawpos < nScreenWidth)) {
					dest[xdrawpos] = srcdat;
					pdest[xdrawpos] = prio;
				}

				xdrawpos = xpos + xcntdraw + 1;

				if ((xdrawpos >= 0) && (xdrawpos < nScreenWidth)) {
					dest[xdrawpos] = srcdat;
					pdest[xdrawpos] = prio;
				}
			}

			xcntdraw+=2;
		}
		else if (xzoombit == 1 && xgrow == 0)
		{
			// skip this column
		}
		else //normal column
		{
			xdrawpos = xpos + xcntdraw;

			if (!(srcdat & 0x8000))
			{
				if ((xdrawpos >= 0) && (xdrawpos < nScreenWidth)) {
					dest[xdrawpos] = srcdat;
					pdest[xdrawpos] = prio;
				}
			}

			xcntdraw++;
		}

		xcnt++;

		if (xdrawpos == nScreenWidth) break;
	}
}


#ifdef __PS3__
/* PS3_COLOR_CLIPPED_BLOCK_V1 */
static inline UINT32 pgm_ps3_draw_clipped8(
    UINT16 *dest,
    UINT8 *pdest,
    INT32 xoff,
    UINT16 msk,
    UINT32 aoffset,
    INT32 palt,
    INT32 prio,
    INT32 xstep)
{
    UINT32 color_count = sprmsktab[msk];
    UINT8 colors[8];
    UINT32 color_index = 0;

    pgm_sprite_colors8(aoffset, colors);

    for (INT32 x = 0; x < 8; x++, xoff += xstep)
    {
        if (msk & 0x0001)
        {
            if (xoff >= 0 && xoff < nScreenWidth) {
                dest[xoff] =
                    colors[color_index] | palt;

                pdest[xoff] = prio;
            }

            color_index++;
        }

        msk >>= 1;

        if (!msk)
            break;
    }

    return aoffset + color_count;
}
#endif

static void pgm_draw_sprite_nozoom(INT32 wide, INT32 high, INT32 palt, INT32 boffset, INT32 xpos, INT32 ypos, INT32 flipx, INT32 flipy, INT32 prio)
{
	UINT16 *dest = pTempScreen;
	UINT8 *pdest = SpritePrio;
	UINT8 * bdata = PGMSPRMaskROM;
	UINT8 * adata = PGMSPRColROM;
	INT32 bdatasize = nPGMSPRMaskMaskLen;
	INT32 adatasize = nPGMSPRColMaskLen;
	INT32 yoff, xoff;

	UINT16 msk;

	UINT32 aoffset = pgm_mask_u32(boffset);
	aoffset = (aoffset >> 2) * 3;
	aoffset &= adatasize;

	boffset += 4;
	wide <<= 4;

	palt <<= 5;

	sprite_draw_nozoom_function *drawsprite = nozoom_draw_table[flipx ? 1 : 0];

	for (INT32 ycnt = 0; ycnt < high; ycnt++) {
		if (flipy) {
			yoff = ypos + ((high-1) - ycnt);
			if (yoff < 0) break;
			if (yoff < nScreenHeight) {
				dest = pTempScreen + (yoff * nScreenWidth);
				pdest = SpritePrio + (yoff * nScreenWidth);
			}
		} else {
			yoff = ypos + ycnt;
			if (yoff >= nScreenHeight) break;
			if (yoff >= 0)  {
				dest = pTempScreen + (yoff * nScreenWidth);
				pdest = SpritePrio + (yoff * nScreenWidth);
			}
		}

		#ifdef __PS3__
		const UINT8 *maskrow =
			pgm_ps3_mask_cache_span(
				boffset, (UINT32)wide >> 3);
		const UINT8 *colorrow = NULL;
		UINT32 colorrow_phase = 0;
#if !defined(PS3_PGM_PERF_PROFILE) || !PS3_PGM_PERF_PROFILE
		/* C1.19: reserve the maximum color consumption for this row.  A
		 * successful span lets all colors8 blocks reuse one cache lookup.
		 */
		if (nPGMSPRColPacked && yoff >= 0 && yoff < nScreenHeight &&
			xpos >= 0 && (xpos + wide) < nScreenWidth) {
			UINT32 row_pixel = aoffset & (UINT32)nPGMSPRColMaskLen;
			UINT32 actual = ((UINT32)nPGMSPRColROMLen / 2u) * 3u;
			if (row_pixel < actual && (UINT32)wide <= actual - row_pixel &&
				(UINT32)wide - 1u <= (UINT32)nPGMSPRColMaskLen - row_pixel) {
				UINT32 pair = row_pixel / 3u;
				colorrow_phase = row_pixel - pair * 3u;
				UINT32 bytes = ((colorrow_phase + (UINT32)wide + 2u) / 3u) * 2u;
				colorrow = pgm_ps3_color_cache_span(pair << 1, bytes);
			}
		}
#endif
		#endif

		if (yoff >= 0 && yoff < nScreenHeight && xpos >= 0 && (xpos + wide) < nScreenWidth)
		{
			for (INT32 xcnt = 0; xcnt < wide; xcnt+=8)
			{
				if (flipx) {
					xoff = xpos + ((wide - 8) - xcnt);
				} else {
					xoff = xpos + xcnt;
				}

				#ifdef __PS3__
				/* PS3_COLORFAST_V2 */
				UINT8 mask = maskrow ? maskrow[xcnt >> 3] : pgm_mask_byte(boffset);
				
				if (mask != 0xff) {
					UINT8 colors[8];
					if (colorrow) {
						pgm_sprite_colors8_unpack_span(colorrow, colorrow_phase, colors);
					} else {
						pgm_sprite_colors8(aoffset, colors);
					}
					UINT32 consumed;

					if (mask == 0x00 && !flipx) {
						dest[xoff + 0] = colors[0] + palt;
						dest[xoff + 1] = colors[1] + palt;
						dest[xoff + 2] = colors[2] + palt;
						dest[xoff + 3] = colors[3] + palt;
						dest[xoff + 4] = colors[4] + palt;
						dest[xoff + 5] = colors[5] + palt;
						dest[xoff + 6] = colors[6] + palt;
						dest[xoff + 7] = colors[7] + palt;
				
						pdest[xoff + 0] = prio;
						pdest[xoff + 1] = prio;
						pdest[xoff + 2] = prio;
						pdest[xoff + 3] = prio;
						pdest[xoff + 4] = prio;
						pdest[xoff + 5] = prio;
						pdest[xoff + 6] = prio;
						pdest[xoff + 7] = prio;
				
						consumed = 8;
					} else {
						consumed = drawsprite[mask](dest + xoff, pdest + xoff, colors, palt, prio);
					}

					aoffset += consumed;
					if (colorrow) {
						static const UINT8 word_advance[11] =
							{ 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3 };
						static const UINT8 next_phase[11] =
							{ 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1 };
						UINT32 total = colorrow_phase + consumed;
						colorrow += (UINT32)word_advance[total] << 1;
						colorrow_phase = next_phase[total];
					}
				}
#else
				aoffset += drawsprite[pgm_mask_byte(boffset)](dest + xoff, pdest + xoff, PGMSPRColROM + (aoffset & nPGMSPRColMaskLen), palt, prio);
#endif
				boffset++;
			}
		} else {
			for (INT32 xcnt = 0; xcnt < wide; xcnt+=8)
			{
				#ifdef __PS3__
				msk = (maskrow ? maskrow[xcnt >> 3] : pgm_mask_byte(boffset)) ^ 0xff;
				#else
				msk = pgm_mask_byte(boffset) ^ 0xff;
				#endif
				boffset++;
				aoffset &= adatasize;

				if (yoff < 0 || yoff >= nScreenHeight || msk == 0) {
					aoffset += sprmsktab[msk];

					continue;
				}

				if (flipx) {
					xoff = xpos + (wide - xcnt) - 1;

					if (xoff < -7 || xoff >= nScreenWidth+8) {
						aoffset += sprmsktab[msk];
						continue;
					}

					#ifdef __PS3__
					/* PS3_COLOR_CLIPPED_BLOCK_V1 */
					aoffset = pgm_ps3_draw_clipped8(
						dest, pdest, xoff, msk,
						aoffset, palt, prio, -1);
					#else
					for (INT32 x = 0; x < 8; x++, xoff--)
					{
						if (msk & 0x0001)
						{
							if (xoff >= 0 && xoff < nScreenWidth) {
								dest[xoff] = pgm_sprite_color(aoffset) | palt;
								pdest[xoff] = prio;
							}
		
							aoffset++;
						}
		
						msk >>= 1;
						if (!msk) break;
					}
					#endif
				} else {
					xoff = xpos + xcnt;

					if (xoff < -7 || xoff >= nScreenWidth) {
						aoffset += sprmsktab[msk];
						continue;
					}

					#ifdef __PS3__
					/* PS3_COLOR_CLIPPED_BLOCK_V1 */
					aoffset = pgm_ps3_draw_clipped8(
						dest, pdest, xoff, msk,
						aoffset, palt, prio, 1);
					#else
					for (INT32 x = 0; x < 8; x++, xoff++)
					{
						if (msk & 0x0001)
						{
							if (xoff >= 0 && xoff < nScreenWidth) {
								dest[xoff] = pgm_sprite_color(aoffset) | palt;
								pdest[xoff] = prio;
							}
		
							aoffset++;
						}
		
						msk >>= 1;
						if (!msk) break;
					}
					#endif
				}
			}
		}
	}
}

#ifdef DUMP_SPRITE_BITMAPS
static void pgm_dump_sprite(INT32 wide, INT32 high, INT32 palt, INT32 boffset, INT32 xpos, INT32 ypos, INT32 flipx, INT32 flipy, INT32 prio)
{
	if (nBurnBPP < 3) return;

	UINT32 *dest = pTempDraw32;
	UINT8 * bdata = PGMSPRMaskROM;
	UINT8 * adata = PGMSPRColROM;
	INT32 bdatasize = nPGMSPRMaskMaskLen;
	INT32 adatasize = nPGMSPRColMaskLen;
	INT32 yoff, xoff;

	UINT16 msk;

	UINT32 boffset_initial = boffset/2;

	UINT32 aoffset = (pgm_mask_byte(boffset + 3) << 24) | (pgm_mask_byte(boffset + 2) << 16) | (pgm_mask_byte(boffset + 1) << 8) | (pgm_mask_byte(boffset));
	aoffset = (aoffset >> 2) * 3;
	aoffset &= adatasize;

	xpos = 0;
	ypos = 0;

	boffset += 4;
	wide <<= 4;

	palt <<= 5;

	UINT32 tcolor = BurnHighCol(0xff,0,0xff,0);

	for (INT32 y = 0; y < high; y++) {
		for (INT32 x = 0; x < wide; x++) {
			dest[y*1024+x] = tcolor;
		}
	}

	for (INT32 ycnt = 0; ycnt < high; ycnt++) {
		if (flipy) {
			yoff = ypos + ((high-1) - ycnt);
			if (yoff < 0) break;
			if (yoff < 512) {
				dest = pTempDraw32 + (yoff * 1024);
			}
		} else {
			yoff = ypos + ycnt;
			if (yoff >= 512) break;
			if (yoff >= 0)  {
				dest = pTempDraw32 + (yoff * 1024);
			}
		}

		{
			for (INT32 xcnt = 0; xcnt < wide; xcnt+=8)
			{
				msk = pgm_mask_byte(boffset) ^ 0xff;
				boffset++;
				aoffset &= adatasize;

				if (yoff < 0 || yoff >= 512 || msk == 0) {
					aoffset += sprmsktab[msk];

					continue;
				}

				if (flipx) {
					xoff = xpos + (wide - xcnt) - 1;

					if (xoff < -7 || xoff >= 1024+8) {
						aoffset += sprmsktab[msk];
						continue;
					}

					for (INT32 x = 0; x < 8; x++, xoff--)
					{
						if (msk & 0x0001)
						{
							if (xoff >= 0 && xoff < 1024) {
								dest[xoff] = RamCurPal[pgm_sprite_color(aoffset) | palt];
							}
		
							aoffset++;
						}
		
						msk >>= 1;
						if (!msk) break;
					}
				} else {
					xoff = xpos + xcnt;

					if (xoff < -7 || xoff >= 1024) {
						aoffset += sprmsktab[msk];
						continue;
					}

					for (INT32 x = 0; x < 8; x++, xoff++)
					{
						if (msk & 0x0001)
						{
							if (xoff >= 0 && xoff < 1024) {
								dest[xoff] = RamCurPal[pgm_sprite_color(aoffset) | palt];
							}
		
							aoffset++;
						}
		
						msk >>= 1;
						if (!msk) break;
					}
				}
			}
		}
	}

#define SET_FILE_SIZE(x)	\
	bmp_data[2] = (x);	\
	bmp_data[3] = (x)>>8;	\
	bmp_data[4] = (x)>>16

#define SET_BITMAP_SIZE(x)	\
	bmp_data[0x22] = (x);	\
	bmp_data[0x23] = (x)>>8;	\
	bmp_data[0x24] = (x)>>16

#define SET_BITMAP_WIDTH(x)	\
	bmp_data[0x12] = (x);	\
	bmp_data[0x13] = (x)>>8;	\
	bmp_data[0x14] = (x)>>16

#define SET_BITMAP_HEIGHT(x)	\
	bmp_data[0x16] = (x);	\
	bmp_data[0x17] = (x)>>8;	\
	bmp_data[0x18] = (x)>>16

	UINT8 bmp_data[0x36] = {
		0x42, 0x4D, 		// 'BM' (leave alone)
		0x00, 0x00, 0x00, 0x00, // file size
		0x00, 0x00, 0x00, 0x00, // padding
		0x36, 0x00, 0x00, 0x00, // offset to data (leave alone)
		0x28, 0x00, 0x00, 0x00, // windows mode (leave alone)
		0x00, 0x00, 0x00, 0x00, // bitmap width
		0x00, 0x00, 0x00, 0x00, // bitmap height
		0x01, 0x00,		// planes (1) always!
		0x20, 0x00, 		// bits per pixel (let's do 32 so no conversion!)
		0x00, 0x00, 0x00, 0x00, // compression (none)
		0x00, 0x00, 0x00, 0x00, // size of bitmap data
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};

	SET_FILE_SIZE((wide*high*4)+54);
	SET_BITMAP_SIZE(wide*high*4);
	SET_BITMAP_WIDTH(wide);
	SET_BITMAP_HEIGHT(high);

	char output[256];
	sprintf (output, "blendbmp/%8.8x_%dx%d.bmp", boffset_initial,wide,high);
	FILE *fa;
	fa = fopen(output, "rb");
	if (fa) {
		fclose (fa);
		return;
	}
	fa = fopen(output, "wb");
	fwrite (bmp_data,0x36,1,fa);
	for (INT32 y = high-1; y >= 0; y--) { // bitmap format is flipped
		fwrite (pTempDraw32 + 1024 * y,wide*4,1,fa);
	}
	fclose (fa);
}
#endif

static void draw_sprite_new_zoomed(INT32 wide, INT32 high, INT32 xpos, INT32 ypos, INT32 palt, INT32 boffset, INT32 flip, UINT32 xzoom, INT32 xgrow, UINT32 yzoom, INT32 ygrow, INT32 prio )
{
	if (!wide) return;

#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	unsigned long long perf_path_start = ps3_perf_now_us();
#endif

#ifdef DUMP_SPRITE_BITMAPS
	pgm_dump_sprite(wide, high, palt, boffset, xpos, ypos, flip & 1, flip & 2, prio);
#endif

	if (yzoom == 0 && xzoom == 0) {
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
		unsigned long long perf_raster_start = pgm_perf_sample_sprite ? ps3_perf_now_us() : 0;
#endif
		pgm_draw_sprite_nozoom(wide, high, palt, boffset, xpos, ypos, flip & 1, flip & 2, prio);
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
		if (pgm_perf_sample_sprite)
			ps3_perf_record_time(PS3_PERF_SPRITE_RASTER,
				(ps3_perf_now_us() - perf_raster_start) * 16);
		ps3_perf_record_time(PS3_PERF_SPRITE_NOZOOM,
			ps3_perf_now_us() - perf_path_start);
#endif
		return;
	}

	INT32 ycnt;
	INT32 ydrawpos;
	UINT16 *dest;
	UINT8 *pdest;
	INT32 yoffset;
	INT32 ycntdraw;
	INT32 yzoombit;

	pgm_prepare_sprite(wide, high, palt, boffset);
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	unsigned long long perf_raster_start = pgm_perf_sample_sprite ? ps3_perf_now_us() : 0;
#endif

	ycnt = 0;
	ycntdraw = 0;
	while (ycnt < high)
	{
		yzoombit = (yzoom >> (ycnt&0x1f))&1;

		if (yzoombit == 1 && ygrow == 1) // double this line
		{
			ydrawpos = ypos + ycntdraw;

			if (!(flip&0x02)) yoffset = (ycnt*(wide*16));
			else yoffset = ( (high-ycnt-1)*(wide*16));
			if ((ydrawpos >= 0) && (ydrawpos < 224))
			{
				dest = pTempScreen + ydrawpos * nScreenWidth;
				pdest = SpritePrio + ydrawpos * nScreenWidth;
				draw_sprite_line(wide, dest, pdest, xzoom, xgrow, yoffset, flip, xpos, prio);
			}
			ycntdraw++;

			ydrawpos = ypos + ycntdraw;
			if (!(flip&0x02)) yoffset = (ycnt*(wide*16));
			else yoffset = ( (high-ycnt-1)*(wide*16));
			if ((ydrawpos >= 0) && (ydrawpos < 224))
			{
				dest = pTempScreen + ydrawpos * nScreenWidth;
				pdest = SpritePrio + ydrawpos * nScreenWidth;
				draw_sprite_line(wide, dest, pdest, xzoom, xgrow, yoffset, flip, xpos, prio);
			}
			ycntdraw++;

			if (ydrawpos == 224) ycnt = high;
		}
		else if (yzoombit ==1 && ygrow == 0)
		{
			// skip this line
			// we should process anyway if we don't do the pre-decode...
		}
		else // normal line
		{
			ydrawpos = ypos + ycntdraw;

			if (!(flip&0x02)) yoffset = (ycnt*(wide*16));
			else yoffset = ( (high-ycnt-1)*(wide*16));
			if ((ydrawpos >= 0) && (ydrawpos < 224))
			{
				dest = pTempScreen + ydrawpos * nScreenWidth;
				pdest = SpritePrio + ydrawpos * nScreenWidth;
				draw_sprite_line(wide, dest, pdest, xzoom, xgrow, yoffset, flip, xpos, prio);
			}
			ycntdraw++;

			if (ydrawpos == 224) ycnt = high;
		}

		ycnt++;
	}
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	if (pgm_perf_sample_sprite)
		ps3_perf_record_time(PS3_PERF_SPRITE_RASTER,
			(ps3_perf_now_us() - perf_raster_start) * 16);
	ps3_perf_record_time(PS3_PERF_SPRITE_ZOOM,
		ps3_perf_now_us() - perf_path_start);
#endif
}

static void pgm_drawsprites()
{
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	unsigned long long perf_start = ps3_perf_now_us();
#endif
	UINT16 *source		= PGMSprBuf;
	UINT16 *finish		= PGMSprBuf + ((OldCodeMode) ? 0x0a00 : 0x1000)/2;
	UINT16 *zoomtable	= (OldCodeMode) ? &PGMVidReg[0x1000/2] : &PGMZoomRAM[0];
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	unsigned int perf_sprite_count = 0;
#endif
	while (source < finish)
	{
		const UINT16 s0 = BURN_ENDIAN_SWAP_INT16(source[0]);
		const UINT16 s1 = BURN_ENDIAN_SWAP_INT16(source[1]);
		const UINT16 s2 = BURN_ENDIAN_SWAP_INT16(source[2]);
		const UINT16 s3 = BURN_ENDIAN_SWAP_INT16(source[3]);
		const UINT16 s4 = BURN_ENDIAN_SWAP_INT16(source[4]);

		if (!OldCodeMode) {
			if ((s4 & 0x7fff) == 0) break;	// verified on hardware
		} else {
			if (s4 == 0) break;				// right?
		}
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
		perf_sprite_count++;
		pgm_perf_sample_sprite = ((perf_sprite_count & 15) == 0);
#endif

		INT32 xpos =  s0 & 0x07ff;
		INT32 ypos =  s1 & 0x03ff;
		INT32 xzom = (s0 & 0x7800) >> 11;
		INT32 xgrow= (s0 & 0x8000) >> 15;
		INT32 yzom = (s1 & 0x7800) >> 11;
		INT32 ygrow= (s1 & 0x8000) >> 15;
		INT32 palt = (s2 & 0x1f00) >> 8;
		INT32 flip = (s2 & 0x6000) >> 13;
		INT32 boff =((s2 & 0x007f) << 16) | (s3 & 0xffff);
		INT32 wide = (s4 & 0x7e00) >> 9;
		INT32 prio = (s2 & 0x0080) >> 7;
		INT32 high =  s4 & 0x01ff;

		if ((0 != nPGMSpriteBufferHack) || (OldCodeMode)) {
			if (s2 & 0x8000) boff += 0x800000; // Real hardware does not have this! Useful for some rom hacks.
		}

		if (xgrow) xzom = 0x10-xzom;
		if (ygrow) yzom = 0x10-yzom;

		UINT32 xzoom = (xzom & 0x10) ? 0 : ((zoomtable[xzom * 2] << 16) | zoomtable[xzom * 2 + 1]);
		UINT32 yzoom = (yzom & 0x10) ? 0 : ((zoomtable[yzom * 2] << 16) | zoomtable[yzom * 2 + 1]);

		if (xpos > 0x3ff) xpos -=0x800;
		if (ypos > 0x1ff) ypos -=0x400;

		if (enable_blending) {
			palt |= pSpriteBlendTable[boff] << 7;
		}

		draw_sprite_new_zoomed(wide, high, xpos, ypos, palt, boff * 2, flip, xzoom, xgrow, yzoom, ygrow, prio);

		source += (OldCodeMode) ? 5 : 8;
	}
#if defined(__PS3__) && defined(PS3_PGM_PERF_PROFILE) && PS3_PGM_PERF_PROFILE
	pgm_perf_sample_sprite = 0;
	ps3_perf_record_sprite_count(perf_sprite_count);
	ps3_perf_record_time(PS3_PERF_SPRITE_DRAW, ps3_perf_now_us() - perf_start);
#endif
}

static void copy_sprite_priority(INT32 prio)
{
	UINT16 *src = pTempScreen;
	UINT8 *pri = SpritePrio;

	if (enable_blending) {
		UINT32 *dest = pTempDraw32;
		INT32 blend_levels[16] = { 0x00, 0x1f, 0x2f, 0x3f, 0x4f, 0x5f, 0x6f, 0x7f, 0x8f, 0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xff };

		for (INT32 i = 0; i < nScreenWidth * nScreenHeight; i++)
		{
			if (pri[i] == prio) {
				if (src[i]&0xf000) {
					dest[i] = alpha_blend(dest[i], RamCurPal[src[i]&0xfff], blend_levels[src[i]/0x1000]);
				} else {
					dest[i] = RamCurPal[src[i]];
				}
			}
		}
	} else {
		UINT16 *dest = pTransDraw;
		for (INT32 i = 0; i < nScreenWidth * nScreenHeight; i++)
		{
			if (pri[i] == prio) {
				dest[i] = src[i];
			}
		}
	}
}

static void draw_text()
{
	UINT16 *vram = (UINT16*)PGMTxtRAM;

	INT32 scrollx = ((OldCodeMode) ? ((INT16)BURN_ENDIAN_SWAP_INT16(PGMVidReg[0x6000 / 2])) : pgm_fg_scrollx) & 0x1ff;
	INT32 scrolly = ((OldCodeMode) ? ((INT16)BURN_ENDIAN_SWAP_INT16(PGMVidReg[0x5000 / 2])) : pgm_fg_scrolly) & 0x0ff;

	for (INT32 offs = 0; offs < 64 * 32; offs++)
	{
		INT32 code = BURN_ENDIAN_SWAP_INT16(vram[offs * 2]);
		if (texttrans[code] == 0) continue; // transparent

		INT32 sx = (offs & 0x3f) << 3;
		INT32 sy = (offs >> 6) << 3;

		sx -= scrollx;
		if (sx < -7) sx += 512;
		sy -= scrolly;
		if (sy < -7) sy += 256;

		if (sx >= nScreenWidth || sy >= nScreenHeight) continue;

		INT32 attr  = BURN_ENDIAN_SWAP_INT16(vram[offs * 2 + 1]);
		INT32 color = ((attr & 0x3e) >> 1) | 0x80;
		INT32 flipx =  (attr & 0x40);
		INT32 flipy =  (attr & 0x80);

		if (enable_blending) 
		{
			UINT8 *gfx = PGMTileROM + (code * 0x40);
			INT32 flip = (flipx ? 0x07 : 0) | (flipy ? 0x38 : 0);
			UINT32 *pal = RamCurPal + color * 0x10;
			UINT32 *dst = pTempDraw32 + (sy * nScreenWidth) + sx;

			for (INT32 y = 0; y < 8; y++, dst += nScreenWidth) {
				if ((sy+y) >= 0 && (sy+y)<nScreenHeight) {
					for (INT32 x = 0; x < 8; x++) {
						INT32 pxl = gfx[((y*8)+x)^flip];

						if (pxl != 0xf) {
							if ((sx+x)>=0 && (sx+x)<nScreenWidth) {
								dst[x] = pal[pxl];
							}
						}
					}
				}
			}
		}
		else
		{
			if (sx < 0 || sy < 0 || sx >= nScreenWidth - 8 || sy >= nScreenHeight - 8)
			{
				if (texttrans[code] & 2) { // opaque
					if (flipy) {
						if (flipx) {
							Render8x8Tile_FlipXY_Clip(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						} else {
							Render8x8Tile_FlipY_Clip(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						}
					} else {
						if (flipx) {
							Render8x8Tile_FlipX_Clip(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						} else {
							Render8x8Tile_Clip(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						}
					}
				} else {
					if (flipy) {
						if (flipx) {
							Render8x8Tile_Mask_FlipXY_Clip(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						} else {
							Render8x8Tile_Mask_FlipY_Clip(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						}
					} else {
						if (flipx) {
							Render8x8Tile_Mask_FlipX_Clip(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						} else {
							Render8x8Tile_Mask_Clip(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						}
					}
				}
			}
			else
			{
				if (texttrans[code] & 2) { // opaque
					if (flipy) {
						if (flipx) {
							Render8x8Tile_FlipXY(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						} else {
							Render8x8Tile_FlipY(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						}
					} else {
						if (flipx) {
							Render8x8Tile_FlipX(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						} else {
							Render8x8Tile(pTransDraw, code, sx, sy, color, 4, 0, PGMTileROM);
						}
					}
				} else {
					if (flipy) {
						if (flipx) {
							Render8x8Tile_Mask_FlipXY(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						} else {
							Render8x8Tile_Mask_FlipY(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						}
					} else {
						if (flipx) {
							Render8x8Tile_Mask_FlipX(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						} else {
							Render8x8Tile_Mask(pTransDraw, code, sx, sy, color, 4, 15, 0, PGMTileROM);
						}
					}
				}
			}
		}
	}
}

static void draw_background()
{
	UINT16 *vram = (UINT16*)PGMBgRAM;

	UINT16 *rowscroll = PGMRowRAM;
	INT32 yscroll = ((OldCodeMode) ? ((INT16)BURN_ENDIAN_SWAP_INT16(PGMVidReg[0x2000 / 2])) : pgm_bg_scrolly);
	INT32 xscroll = ((OldCodeMode) ? ((INT16)BURN_ENDIAN_SWAP_INT16(PGMVidReg[0x3000 / 2])) : pgm_bg_scrollx);

	// check to see if we need to do line scroll
	INT32 t = 0;
	{
		UINT16 *rs = rowscroll;
		for (INT32 i = 0; i < 224; i++) {
			if (BURN_ENDIAN_SWAP_INT16(rs[0]) != BURN_ENDIAN_SWAP_INT16(rs[i])) {
				t = 1;
				break;
			}
		}
	}

	// no line scroll (fast)
	if (t == 0)
	{
		yscroll &= 0x1ff;
		xscroll &= 0x7ff;

		for (INT32 offs = 0; offs < 64 * 16; offs++)
		{
			INT32 sx = (offs & 0x3f) << 5;
			INT32 sy = (offs >> 6) << 5;

			sx -= xscroll;
			if (sx < -31) sx += 2048;
			sy -= yscroll;
			if (sy < -31) sy += 512;

			if (sx >= nScreenWidth || sy >= nScreenHeight) continue;

			INT32 code = BURN_ENDIAN_SWAP_INT16(vram[offs * 2]);
			if (code >= nTileMask) continue;
			if (tiletrans[code] == 0) continue; // transparent
			INT32 color = ((BURN_ENDIAN_SWAP_INT16(vram[offs*2+1]) & 0x3e) >> 1) | 0x20;
			INT32 flipy = BURN_ENDIAN_SWAP_INT16(vram[offs*2+1]) & 0x80;
			INT32 flipx = BURN_ENDIAN_SWAP_INT16(vram[offs*2+1]) & 0x40;
			INT32 renderCode = code;
			UINT8 *renderGfx = PGMTileROMExp;
#ifdef __PS3__
			renderCode = 0;
			renderGfx = pgm_bg_tile(code);
#endif

			if (enable_blending) 
			{
				UINT8 *gfx =
#ifdef __PS3__
					pgm_bg_tile(code);
#else
					PGMTileROMExp + (code * 0x400);
#endif
				INT32 flip = (flipx ? 0x1f : 0) | (flipy ? 0x3e0 : 0);
				UINT32 *pal = RamCurPal + color * 0x20;
				UINT32 *dst = pTempDraw32 + (sy * nScreenWidth) + sx;
	
				for (INT32 y = 0; y < 32; y++, dst += nScreenWidth) {
					if ((sy+y) >= 0 && (sy+y)<nScreenHeight) {
						for (INT32 x = 0; x < 32; x++) {
							INT32 pxl = gfx[((y*32)+x)^flip];
	
							if (pxl != 0x1f) {
								if ((sx+x)>=0 && (sx+x)<nScreenWidth) {
									dst[x] = pal[pxl];
								}
							}
						}
					}
				}
			}
			else
			{
				if (sx < 0 || sy < 0 || sx >= nScreenWidth - 32 || sy >= nScreenHeight - 32)
				{
					if (tiletrans[code] & 2) { // opaque
						if (flipy) {
							if (flipx) {
								Render32x32Tile_FlipXY_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							} else {
								Render32x32Tile_FlipY_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							}
						} else {
							if (flipx) {
								Render32x32Tile_FlipX_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							} else {
								Render32x32Tile_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							}
						}
					} else {
						if (flipy) {
							if (flipx) {
								Render32x32Tile_Mask_FlipXY_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							} else {
								Render32x32Tile_Mask_FlipY_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							}
						} else {
							if (flipx) {
								Render32x32Tile_Mask_FlipX_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							} else {
								Render32x32Tile_Mask_Clip(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							}
						}
					}
				}
				else
				{
					if (tiletrans[code] & 2) { // opaque
						if (flipy) {
							if (flipx) {
								Render32x32Tile_FlipXY(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							} else {
								Render32x32Tile_FlipY(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							}
						} else {
							if (flipx) {
								Render32x32Tile_FlipX(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							} else {
								Render32x32Tile(pTransDraw, renderCode, sx, sy, color, 5, 0, renderGfx);
							}
						}
					} else {
						if (flipy) {
							if (flipx) {
								Render32x32Tile_Mask_FlipXY(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							} else {
								Render32x32Tile_Mask_FlipY(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							}
						} else {
							if (flipx) {
								Render32x32Tile_Mask_FlipX(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							} else {
								Render32x32Tile_Mask(pTransDraw, renderCode, sx, sy, color, 5, 0x1f, 0, renderGfx);
							}
						}
					}
				}
			}
		}

		return;
	}

	// do line scroll (slow)
	for (INT32 y = 0; y < 224; y++)
	{
		INT32 scrollx = (xscroll + BURN_ENDIAN_SWAP_INT16(rowscroll[y])) & 0x7ff;
		INT32 scrolly = (yscroll + y) & 0x7ff;

		for (INT32 x = 0; x < 480; x+=32)
		{
			INT32 sx = x - (scrollx & 0x1f);
			if (sx >= nScreenWidth) break;

			INT32 offs = ((scrolly & 0x1e0) << 2) | (((scrollx + x) & 0x7e0) >> 4);

			INT32 code  = BURN_ENDIAN_SWAP_INT16(vram[offs]);
			if (code >= nTileMask) continue;
			if (tiletrans[code] == 0) continue;

			INT32 attr  = BURN_ENDIAN_SWAP_INT16(vram[offs + 1]);
			INT32 color = ((attr & 0x3e) << 4) | 0x400;
			INT32 flipx = ((attr & 0x40) >> 6) * 0x1f;
			INT32 flipy = ((attr & 0x80) >> 7) * 0x1f;

			UINT8 *src =
#ifdef __PS3__
				pgm_bg_tile(code) + (((scrolly ^ flipy) & 0x1f) << 5);
#else
				PGMTileROMExp + (code * 1024) + (((scrolly ^ flipy) & 0x1f) << 5);
#endif

			if (enable_blending)
			{
				UINT32 *dst = pTempDraw32 + (y * nScreenWidth);
				UINT32 *pal = RamCurPal + color;
	
				if (sx >= 0 && sx <= 415) {
					for (INT32 xx = 0; xx < 32; xx++, sx++) {
						INT32 pxl = src[xx^flipx];
		
						if (pxl != 0x1f) {
							dst[sx] = pal[pxl];
						}
					}
				} else {
					for (INT32 xx = 0; xx < 32; xx++, sx++) {
						if (sx < 0) continue;
						if (sx >= nScreenWidth) break;
		
						INT32 pxl = src[xx^flipx];
		
						if (pxl != 0x1f) {
							dst[sx] = pal[pxl];
						}
					}
				}
			} else {
				UINT16 *dst = pTransDraw + (y * nScreenWidth);

				if (sx >= 0 && sx <= 415) {
					for (INT32 xx = 0; xx < 32; xx++, sx++) {
						INT32 pxl = src[xx^flipx];
		
						if (pxl != 0x1f) {
							dst[sx] = pxl | color;
						}
					}
				} else {
					for (INT32 xx = 0; xx < 32; xx++, sx++) {
						if (sx < 0) continue;
						if (sx >= nScreenWidth) break;
		
						INT32 pxl = src[xx^flipx];
		
						if (pxl != 0x1f) {
							dst[sx] = pxl | color;
						}
					}
				}
			}
		}
	}
}

static void pgmBlendCopy()
{
	pBurnDrvPalette = RamCurPal;

	UINT32 *bmp = pTempDraw32;

	for (INT32 i = 0; i < nScreenWidth * nScreenHeight; i++) {
		PutPix(pBurnDraw + (i * nBurnBpp), BurnHighCol(bmp[i]>>16, (bmp[i]>>8)&0xff, bmp[i]&0xff, 0));
	}
}

INT32 pgmDraw()
{
	if (pTempDraw == NULL) {
		PS3_PGM_MEM_LABEL("PGMTempDraw");
		pTempDraw = (UINT16*)BurnMalloc(0x400 * 0x200 * sizeof(INT16));
		PS3_PGM_MEM_LABEL("PGMSpritePrio");
		SpritePrio = (UINT8*)BurnMalloc(nScreenWidth * nScreenHeight);
		PS3_PGM_MEM_LABEL("PGMTempScreen");
		pTempScreen = (UINT16*)BurnMalloc(nScreenWidth * nScreenHeight * sizeof(INT16));
		if (pTempDraw == NULL || SpritePrio == NULL || pTempScreen == NULL) {
			bprintf(PRINT_ERROR, _T("[FBNeo] PGM lazy draw-buffer allocation failed: draw=%p prio=%p screen=%p\n"), (void*)pTempDraw, (void*)SpritePrio, (void*)pTempScreen);
			return 1;
		}
#ifdef __PS3__
		bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM lazy draw buffers ready: draw=%p prio=%p screen=%p\n"), pTempDraw, SpritePrio, pTempScreen);
#endif
	}
	if (enable_blending) nPgmPalRecalc = 1; // force recalc.

	if (nPgmPalRecalc) {
		for (INT32 i = 0; i < 0x1200 / 2; i++) {
			RamCurPal[i] = CalcCol(BURN_ENDIAN_SWAP_INT16(PGMPalRAM[i]));
		}
		nPgmPalRecalc = 0;
	}
	
	INT32 nTemp = (OldCodeMode) ? 0x1200 : 0x2000;

	{
		// black / magenta
		RamCurPal[(nTemp+0)/2]	= (nBurnLayer & 1) ? RamCurPal[0x3ff] : BurnHighCol(0xff, 0, 0xff, 0);
		RamCurPal[(nTemp+2)/2]	= BurnHighCol(0xff,0x00,0xff,0);
	}

	// Fill in background color (0x2000/2)
	// also, clear buffers
	{
		nTemp = (OldCodeMode) ? 0x0900 : 0x1000;

		for (INT32 i = 0; i < nScreenWidth * nScreenHeight; i++) {
			if (enable_blending) pTempDraw32[i] = RamCurPal[nTemp];
			pTransDraw[i]	= nTemp;
			pTempScreen[i]	= 0;
			SpritePrio[i]	= 0xff;
		}
	}

	pgm_drawsprites();

#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
	/* Submit partial batches once per rendered frame. */
	ps3_pgm_color_shadow_flush();
#endif

	if (nSpriteEnable & 1) copy_sprite_priority(1);

#ifdef DRAW_SPRITE_NUMBER
	pgm_drawsprites_fonts(1);
#endif

//	if ((pgm_video_control & 0x1000) == 0 && (nBurnLayer & 1)) draw_background();
	if (nBurnLayer & 1) {
		draw_background();
#if 0
		if (!OldCodeMode) {
			if ((pgm_video_control & 0x1000) == 0)
				draw_background();
		} else {draw_background();}
#endif
	}

//	if ((pgm_video_control & 0x2000) == 0 && (nSpriteEnable & 2)) copy_sprite_priority(0);
	if (nBurnLayer & 2) {
		if (!OldCodeMode) {
			if ((pgm_video_control & 0x2000) == 0)
				copy_sprite_priority(0);
		} else {copy_sprite_priority(0);}
	}

#ifdef DRAW_SPRITE_NUMBER
	pgm_drawsprites_fonts(0);
#endif

//	if ((pgm_video_control & 0x0800) == 0 && (nBurnLayer & 2)) draw_text();
	if (nBurnLayer & 2) {
		if (!OldCodeMode) {
			if ((pgm_video_control & 0x0800) == 0)
				draw_text();
		} else {draw_text();}
	}

	if (enable_blending) {
		pgmBlendCopy();
	} else {
		BurnTransferCopy(RamCurPal);
	}

	return 0;
}

static void pgmBlendInit()
{
	enable_blending = 0;

	TCHAR filename[MAX_PATH];

	_stprintf(filename, _T("%s%s.bld"), szAppBlendPath, BurnDrvGetText(DRV_NAME));
	
	FILE *fa = _tfopen(filename, _T("rt"));

	if (fa == NULL) {
		bprintf (0, _T("can't find: %s\n"), filename);
		_stprintf(filename, _T("%s%s.bld"), szAppBlendPath, BurnDrvGetText(DRV_PARENT));

		fa = _tfopen(filename, _T("rt"));

		if (fa == NULL) {
			bprintf (0, _T("can't find: %s\n"), filename);
			return;
		}
	}

	if (pSpriteBlendTable == NULL) {
		pSpriteBlendTable = (UINT8*)BurnMalloc(0x800000);
		if (pSpriteBlendTable == NULL) {
		bprintf (0, _T("can't allocate blend table\n"));
			return;
		}
	}

	bprintf (PRINT_IMPORTANT, _T("Using sprite blending (.bld) table!\n"));

	char szLine[64];

	while (1)
	{
		if (fgets (szLine, 64, fa) == NULL) break;

		if (strncmp ("Game", szLine, 4) == 0) continue; 	// don't care
		if (strncmp ("Name", szLine, 4) == 0) continue; 	// don't care
		if (szLine[0] == ';') continue;				// comment (also don't care)

		INT32 single_entry = -1;
		UINT32 type,min,max,k;

		for (k = 0; k < strlen(szLine); k++) {
			if (szLine[k] == '-') { single_entry = k+1; break; }
		}

		if (single_entry < 0) {
			sscanf(szLine,"%x %x",&max,&type);
			min = max;
		} else {
			sscanf(szLine,"%x",&min);
			sscanf(szLine+single_entry,"%x %x",&max,&type);
		}

		for (k = min; k <= max && k < 0x800000; k++) {
			pSpriteBlendTable[k] = type&0xf;
		}
	}

	fclose (fa);

	enable_blending = 1;
}

void pgmInitDraw() // preprocess some things...
{
	GenericTilesInit();
#ifdef __PS3__
	PS3_PGM_MEM_LABEL("PGMBgTileCache");
	pgmBgTileCache = (UINT8*)BurnMalloc(PGM_BG_CACHE_TILES * 0x400);
	for (INT32 i = 0; i < PGM_BG_CACHE_TILES; i++) pgmBgTileTag[i] = -1;
#endif

	pTempDraw32 = NULL;
	pTempDraw = NULL;
	SpritePrio = NULL;
	pTempScreen = NULL;

	if (bBurnUseBlend) pgmBlendInit();
	if (enable_blending) {
		pTempDraw32 = (UINT32*)BurnMalloc(0x448 * 0x224 * 4);
		if (pTempDraw32 == NULL) enable_blending = 0;
	}
#ifdef __PS3__
	bprintf(PRINT_IMPORTANT, _T("[FBNeo] PGM draw buffers: blend=%d draw32=%p draw=%p prio=%p screen=%p\n"), enable_blending, pTempDraw32, pTempDraw, SpritePrio, pTempScreen);
#endif

	// Find transparent tiles so we can skip them
	{
		nTileMask = ((nPGMTileROMLen / 5) * 8) / 0x400; // also used to set max. tile

		// background tiles
		tiletrans = (UINT8*)BurnMalloc(nTileMask);
		memset (tiletrans, 0, nTileMask);
	
		for (INT32 i = 0; i < nTileMask; i++)
		{
			UINT8 *tile =
#ifdef __PS3__
				pgm_bg_tile(i);
#else
				PGMTileROMExp + (i << 10);
#endif
			INT32 k = 0x1f;
			for (INT32 j = 0; j < 0x400; j++)
			{
				if (tile[j] != 0x1f) {
					tiletrans[i] = 1;
				}
				k &= (tile[j] ^ 0x1f);
			}
			if (k) tiletrans[i] |= 2;
		}

		// character tiles
		texttrans = (UINT8*)BurnMalloc(0x10000);
		memset (texttrans, 0, 0x10000);

		for (INT32 i = 0; i < 0x400000; i += 0x40)
		{
			INT32 k = 0xf;
			for (INT32 j = 0; j < 0x40; j++)
			{
				if (PGMTileROM[i+j] != 0xf) {
					texttrans[i/0x40] = 1;
				}
				k &= (PGMTileROM[i+j] ^ 0xf);
			}
			if (k) texttrans[i/0x40] |= 2;
		}
	}

	// set up table to count bits in sprite mask data
	// gives a good speedup in sprite drawing. ^^
	{
		memset (sprmsktab, 0, 0x100);
		for (INT32 i = 0; i < 0x100; i++) {
			for (INT32 j = 0; j < 8; j++) {
				if (i & (1 << j)) {
					sprmsktab[i]++;
				}
			}
		}
	}
}

void pgmExitDraw()
{
	nTileMask = 0;

	BurnFree (pTempDraw32);
	BurnFree (pTempDraw);
	BurnFree (tiletrans);
	BurnFree (texttrans);
	BurnFree (pTempScreen);
	BurnFree (SpritePrio);
#ifdef __PS3__
	BurnFree (pgmBgTileCache);
#endif

	if (pSpriteBlendTable) {
		BurnFree(pSpriteBlendTable);
	}

	enable_blending = 0;

	GenericTilesExit();
}
