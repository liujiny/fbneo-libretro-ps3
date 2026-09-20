#include <string.h>

#include "burnint.h"

#include "ps3_sprite_color_shadow.h"

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
extern "C" void ps3_mem_diag_logf(
    const char *fmt,
    ...
);
#endif
#include "spu/ps3_sprite_color_protocol.h"

#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
#include "ps3_sprite_worker.h"
#endif

static ps3_spu_color_batch g_color_batch __attribute__((aligned(128)));
static UINT32 g_color_count;
static UINT32 g_submit_count;
static UINT32 g_batch_count;

void ps3_pgm_color_shadow_init()
{
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "COLOR SHADOW INIT\n");
#endif

    memset(&g_color_batch, 0, sizeof(g_color_batch));
    g_color_count = 0;
    g_submit_count = 0;
    g_batch_count = 0;
}

static void shadow_decode()
{
	for (UINT32 i = 0; i < g_color_batch.count; i++) {
		ps3_spu_color_item *item = &g_color_batch.item[i];
		UINT32 pair = 0;
		UINT32 phase = item->phase;
		UINT16 packed = (UINT16)(item->packed[0] |
			((UINT16)item->packed[1] << 8));

		for (UINT32 j = 0; j < 8; j++) {
			item->result[j] = (packed >> (phase * 5)) & 0x1f;
			if (++phase == 3 && j != 7) {
				phase = 0;
				pair++;
				packed = (UINT16)(item->packed[pair * 2] |
					((UINT16)item->packed[pair * 2 + 1] << 8));
			}
		}
	}
}

static void shadow_compare()
{
	UINT32 mismatch = 0;

	for (UINT32 i = 0; i < g_color_batch.count; i++) {
		for (UINT32 j = 0; j < 8; j++) {
			if (g_color_batch.item[i].expected[j] !=
				g_color_batch.item[i].result[j]) {
				mismatch++;
			}
		}
	}

	if (mismatch || g_batch_count == 1 || (g_batch_count & 0xff) == 0) {
		#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "COLOR SHADOW mismatch=%u\\n",
            mismatch);
#endif
	}
}

void ps3_pgm_color_shadow_submit(
	const UINT8 *packed, UINT32 span_bytes, UINT32 phase,
	const UINT8 *expected)
{
	if (g_color_count >= PS3_SPU_COLOR_BATCH_SIZE) {
		ps3_pgm_color_shadow_flush();
	}

	g_submit_count++;
	if (g_submit_count == 1 || (g_submit_count & 0xfffff) == 0) {
		#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "COLOR SHADOW submit=%u\\n",
            g_submit_count);
#endif
	}

	ps3_spu_color_item *item = &g_color_batch.item[g_color_count];
	memset(item, 0, sizeof(*item));
	memcpy(item->packed, packed, span_bytes);
	item->phase = phase;
	item->span_bytes = span_bytes;
	memcpy(item->expected, expected, 8);
	g_color_count++;
}

void ps3_pgm_color_shadow_flush()
{
#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER
	if (g_color_count == 0) return;

	g_color_batch.magic = PS3_SPU_SIGNAL_COLOR_BATCH;
	g_color_batch.count = g_color_count;
	g_batch_count++;

	if (g_batch_count == 1 || (g_batch_count & 0xff) == 0) {
		#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "COLOR SHADOW batch=%u batches=%u submit=%u\\n",
            g_color_count,
            g_batch_count,
            g_submit_count);
#endif
	}

	/*
	 * v1-C:
	 * SPU performs packed-color decode.
	 * CPU keeps compare only.
	 */
        if (ps3_pgm_spu_color_batch(
                &g_color_batch) == 0) {
                shadow_compare();
        }
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        else {
                ps3_mem_diag_logf(
                        "COLOR SHADOW SPU submit FAIL\n");
        }
#endif

#endif

	g_color_count = 0;
	memset(&g_color_batch, 0, sizeof(g_color_batch));
}

void ps3_pgm_color_shadow_exit()
{
	ps3_pgm_color_shadow_flush();
}
