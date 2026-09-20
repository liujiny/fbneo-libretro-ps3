#include <stdint.h>

#include <spu_intrinsics.h>
#include <spu_mfcio.h>

#include <sys/spu_thread.h>
#include <sys/spu_event.h>

#include "../ps3_sprite_worker_protocol.h"
#include "../ps3_sprite_color_protocol.h"

#define DMA_TAG 1

static ps3_spu_dma_test_block g_dma_block
    __attribute__((aligned(128)));

static ps3_spu_color_batch g_color_batch
    __attribute__((aligned(128)));


/*
 * H14-A:
 * contiguous decoded result payload returned to PPU.
 *
 * Maximum 64 * 8 = 512 bytes.
 */
static uint8_t g_color_result_batch[
    PS3_SPU_COLOR_RESULT_MAX_BYTES
]
    __attribute__((aligned(128)));


static uint32_t g_v1f1_debug_block[4]
    __attribute__((aligned(16)));

static uint32_t g_v1f1_debug_first_job = 1;


/*
 * Exact 16-byte async completion DMA block.
 */
static uint32_t g_color_done_block[4]
    __attribute__((aligned(16)));

static void wait_for_dma(void)
{
    mfc_write_tag_mask(1u << DMA_TAG);
    (void)mfc_read_tag_status_all();
}


static void write_v1f1_debug_stage(
    uint64_t ea,
    uint32_t stage,
    uint32_t count,
    uint32_t bytes)
{
    g_v1f1_debug_block[0] =
        PS3_SPU_V1F1_DEBUG_MAGIC;

    g_v1f1_debug_block[1] =
        stage;

    g_v1f1_debug_block[2] =
        count;

    g_v1f1_debug_block[3] =
        bytes;

    mfc_put(
        g_v1f1_debug_block,
        ea,
        16,
        DMA_TAG,
        0,
        0);

    wait_for_dma();
}




static void decode_color_item(
    ps3_spu_color_item *item)
{
    uint32_t phase = item->phase;
    uint32_t byte_pos = 0;

    uint32_t word =
        item->packed[0]
        |
        (item->packed[1] << 8);

    uint32_t i;


    for(i = 0; i < 8; i++)
    {
        item->result[i] =
            (word >> (phase * 5))
            & 0x1f;


        phase++;


        if(phase == 3 && i != 7)
        {
            phase = 0;

            byte_pos += 2;

            word =
                item->packed[byte_pos]
                |
                (item->packed[byte_pos+1]
                 << 8);
        }
    }
}

static inline __attribute__((always_inline)) void decode_color8_stream(
    const uint8_t *packed,
    uint32_t phase,
    uint8_t *result)
{
    uint32_t byte_pos = 0;
    uint32_t word = packed[0] | (packed[1] << 8);
    uint32_t i;

    /* Fixed trip count lets spu-gcc fully unroll this hot loop. */
    for (i = 0; i < PS3_SPU_COLOR_RESULT_ITEM_BYTES; i++)
    {
        result[i] = (word >> (phase * 5)) & 0x1f;

        if (++phase == 3 && i + 1 < PS3_SPU_COLOR_RESULT_ITEM_BYTES)
        {
            phase = 0;
            byte_pos += 2;
            word = packed[byte_pos] | (packed[byte_pos + 1] << 8);
        }
    }
}

static void decode_color_stream(
    const ps3_spu_color_stream_batch *stream,
    uint32_t count)
{
    uint32_t phase = stream->phase;
    uint32_t byte_pos = 0;
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        decode_color8_stream(
            stream->packed + byte_pos,
            phase,
            g_color_result_batch +
                i * PS3_SPU_COLOR_RESULT_ITEM_BYTES);

        if (phase == 0)
        {
            byte_pos += 4;
            phase = 2;
        }
        else if (phase == 1)
        {
            byte_pos += 6;
            phase = 0;
        }
        else
        {
            byte_pos += 6;
            phase = 1;
        }
    }
}


static void run_color_batch(
    uint64_t ea)
{
    uint32_t i;
    uint32_t j;

    mfc_get(
        &g_color_batch,
        ea,
        sizeof(g_color_batch),
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    g_color_batch.mismatch = 0;

    for(i = 0;
        i < g_color_batch.count;
        i++)
    {
        decode_color_item(
            &g_color_batch.item[i]);

        /*
         * Legacy validation/shadow semantics.
         */
        if(g_color_batch.item[i].reserved0 == 0)
        {
            for(j = 0;
                j < 8;
                j++)
            {
                if(g_color_batch.item[i].result[j]
                    !=
                   g_color_batch.item[i].expected[j])
                {
                    g_color_batch.mismatch++;
                }
            }
        }
    }

    mfc_put(
        &g_color_batch,
        ea,
        sizeof(g_color_batch),
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    /*
     * Legacy path keeps its original event completion.
     *
     * It deliberately does NOT touch the async done block.
     */
    spu_thread_send_event(
        PS3_SPU_EVENT_PORT,
        PS3_SPU_EVENT_COLOR_DONE,
        0);
}



static void write_async_done(
    uint64_t done_ea,
    uint32_t done,
    uint32_t count,
    uint32_t bytes,
    uint32_t status)
{
    g_color_done_block[0] =
        done;

    g_color_done_block[1] =
        count;

    g_color_done_block[2] =
        bytes;

    g_color_done_block[3] =
        status;

    mfc_put(
        g_color_done_block,
        done_ea,
        16,
        DMA_TAG,
        0,
        0);

    wait_for_dma();
}


/*
 * v1-F compact render-only async path.
 *
 * command_count comes from signal1 low 7 bits.
 *
 * No user event is emitted here.
 * PPU completion polling is DMA-only.
 */
static void run_color_batch_compact(
    uint64_t ea,
    uint64_t done_ea,
    uint64_t debug_ea,
    uint32_t command,
    uint32_t stream_mode)
{
    uint32_t count =
        command & PS3_SPU_COLOR_ASYNC_COUNT_MASK;

    uint32_t bytes;
    uint32_t result_dma_bytes;
    uint32_t i;
    uint32_t j;
    uint32_t debug_this_job =
        g_v1f1_debug_first_job;

    if (g_v1f1_debug_first_job)
        g_v1f1_debug_first_job = 0;

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_ENTER,
            count,
            0);
    }

    if(count == 0 ||
       count >
        PS3_SPU_COLOR_BATCH_SIZE)
    {
        write_async_done(
            done_ea,
            PS3_SPU_COLOR_ASYNC_DONE_ERROR,
            count,
            0,
            PS3_SPU_COLOR_ASYNC_STATUS_BAD_COUNT);

        return;
    }

    if (stream_mode)
    {
        uint32_t phase = (command & PS3_SPU_COLOR_STREAM_PHASE_MASK) >>
            PS3_SPU_COLOR_STREAM_PHASE_SHIFT;
        uint32_t packed_bytes = ((phase + count * 8 + 2) / 3) * 2;
        bytes = PS3_SPU_COLOR_STREAM_DMA_BYTES(packed_bytes);
    }
    else
    {
        bytes = PS3_SPU_COLOR_COMPACT_BYTES(count);
    }

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_BEFORE_GET,
            count,
            bytes);
    }

    /*
     * v1-F1:
     *
     * ea is the proven startup arg1 color batch EA.
     * LS/EA are 128-byte aligned and bytes is 16-byte aligned.
     */
    mfc_get(
        &g_color_batch,
        ea,
        bytes,
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_AFTER_GET,
            count,
            bytes);
    }

    if(g_color_batch.magic !=
            (stream_mode ? PS3_SPU_SIGNAL_COLOR_STREAM :
                           PS3_SPU_SIGNAL_COLOR_BATCH) ||
       g_color_batch.count !=
            count)
    {
        write_async_done(
            done_ea,
            PS3_SPU_COLOR_ASYNC_DONE_ERROR,
            count,
            bytes,
            PS3_SPU_COLOR_ASYNC_STATUS_BAD_HEADER);

        return;
    }

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_HEADER_OK,
            count,
            bytes);
    }

    if (stream_mode)
    {
        ps3_spu_color_stream_batch *stream =
            (ps3_spu_color_stream_batch *)&g_color_batch;
        uint32_t expected_packed =
            ((stream->phase + count * 8 + 2) / 3) * 2;

        if (stream->phase > 2 ||
            stream->packed_bytes != expected_packed ||
            stream->packed_bytes > PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES)
        {
            write_async_done(done_ea, PS3_SPU_COLOR_ASYNC_DONE_ERROR,
                count, bytes, PS3_SPU_COLOR_ASYNC_STATUS_BAD_HEADER);
            return;
        }

        decode_color_stream(stream, count);
    }
    else
    {
        for(i = 0; i < count; i++)
            decode_color_item(&g_color_batch.item[i]);

        for (i = 0; i < count; i++)
            for (j = 0; j < PS3_SPU_COLOR_RESULT_ITEM_BYTES; j++)
                g_color_result_batch[i * PS3_SPU_COLOR_RESULT_ITEM_BYTES + j] =
                    g_color_batch.item[i].result[j];
    }

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_DECODE_DONE,
            count,
            bytes);
    }

    result_dma_bytes =
        PS3_SPU_COLOR_RESULT_DMA_BYTES(
            count);

    /*
     * EA is the same proven 128-byte aligned arg1 buffer.
     * LS source is 128-byte aligned.
     * result_dma_bytes is always a multiple of 16.
     */
    mfc_put(
        g_color_result_batch,
        ea,
        result_dma_bytes,
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_RESULT_PUT,
            count,
            bytes);
    }

    write_async_done(
        done_ea,
        PS3_SPU_COLOR_ASYNC_DONE_OK,
        count,
        bytes,
        PS3_SPU_COLOR_ASYNC_STATUS_OK);

    if (debug_this_job)
    {
        write_v1f1_debug_stage(
            debug_ea,
            PS3_SPU_V1F1_STAGE_DONE_PUT,
            count,
            bytes);
    }
}


static void run_dma_test(uint64_t ea)
{
    uint32_t i;
    uint32_t checksum = 0;

    /*
     * Fetch complete 128-byte block from PPU main memory.
     */
    mfc_get(
        &g_dma_block,
        ea,
        sizeof(g_dma_block),
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    if (g_dma_block.magic != PS3_SPU_DMA_MAGIC ||
        g_dma_block.command != PS3_SPU_DMA_COMMAND_TEST)
    {
        g_dma_block.status =
            PS3_SPU_DMA_STATUS_BAD;
    }
    else
    {
        for (i = 0;
             i < PS3_SPU_DMA_WORDS;
             i++)
        {
            g_dma_block.output[i] =
                g_dma_block.input[i] ^
                0xffffffffu;

            checksum ^=
                g_dma_block.output[i];
        }

        g_dma_block.checksum = checksum;

        g_dma_block.status =
            PS3_SPU_DMA_STATUS_DONE;
    }

    /*
     * Write complete 128-byte block back to PPU main memory.
     */
    mfc_put(
        &g_dma_block,
        ea,
        sizeof(g_dma_block),
        DMA_TAG,
        0,
        0);

    wait_for_dma();

    /*
     * DMA has completed before this event is emitted.
     */
    spu_thread_send_event(
        PS3_SPU_EVENT_PORT,
        PS3_SPU_EVENT_DMA_DONE,
        g_dma_block.sequence);
}

int main(
    uint64_t dma_ea,
    uint64_t color_batch_ea,
    uint64_t color_done_ea,
    uint64_t arg4)
{
    uint32_t command;

    (void)arg4;

    for (;;)
    {
        command = spu_read_signal1();

        if (command ==
            PS3_SPU_SIGNAL_SHUTDOWN)
        {
            break;
        }

        /*
         * Preserve V1-A communication self-test.
         */
        if (command ==
            PS3_SPU_SIGNAL_SELFTEST)
        {
            spu_thread_send_event(
                PS3_SPU_EVENT_PORT,
                command * 3,
                command * 5);

            continue;
        }

        /*
         * V1-B DMA round-trip test.
         */
        if (command ==
            PS3_SPU_SIGNAL_DMA_TEST)
        {
            run_dma_test(dma_ea);
            continue;
        }


        if (command ==
            PS3_SPU_SIGNAL_COLOR_BATCH)
        {
            run_color_batch(
                color_batch_ea);

            continue;
        }


        /*
         * v1-F compact async render command.
         *
         * count is carried directly in signal1.
         */
        if ((command & PS3_SPU_SIGNAL_COLOR_STREAM_MASK) ==
                PS3_SPU_SIGNAL_COLOR_STREAM_BASE)
        {
            run_color_batch_compact(
                color_batch_ea,
                color_done_ea,
                dma_ea,
                command,
                1);

            continue;
        }

        if ((command &
             PS3_SPU_SIGNAL_COLOR_ASYNC_MASK)
            ==
            PS3_SPU_SIGNAL_COLOR_ASYNC_BASE)
        {
            run_color_batch_compact(
                color_batch_ea,
                color_done_ea,
                dma_ea,
                command,
                0);

            continue;
        }

        /*
         * Unknown command.
         */
        spu_thread_send_event(
            PS3_SPU_EVENT_PORT,
            PS3_SPU_EVENT_ERROR,
            command);
    }

    spu_thread_exit(0);

    return 0;
}
