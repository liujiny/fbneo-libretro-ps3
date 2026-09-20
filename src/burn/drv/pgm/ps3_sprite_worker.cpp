#if defined(__PS3__) && defined(PS3_PGM_SPU_WORKER) && PS3_PGM_SPU_WORKER

#include <stdint.h>
#include <string.h>

#include <sys/spu.h>
#include <sys/event_queue.h>

#include "ps3_sprite_worker.h"
#include "spu/ps3_sprite_color_protocol.h"
#include "spu/ps3_sprite_worker_protocol.h"
#include "spu/generated/ps3_sprite_worker_spu_bin.h"

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
extern "C" void ps3_mem_diag_logf(
    const char *format, ...);
#endif

static sysSpuImage g_ps3_spu_image;

static sys_spu_group_t g_ps3_spu_group;
static sys_spu_thread_t g_ps3_spu_thread;
static sys_event_queue_t g_ps3_spu_queue;

static int g_ps3_spu_ready = 0;
static int g_ps3_spu_attempted = 0;

static ps3_spu_dma_test_block g_ps3_spu_dma_block
    __attribute__((aligned(128)));



/*
 * v1-F compact async render path.
 *
 * Slot 0 is used by the current single-inflight pipeline.
 * Slot 1 stays reserved for a later ping-pong stage.
 */

#define PS3_SPU_ASYNC_COLOR_BUFFERS 2

static ps3_spu_color_batch
    g_ps3_spu_async_batch[
        PS3_SPU_ASYNC_COLOR_BUFFERS]
        __attribute__((aligned(128)));

static volatile int
    g_ps3_spu_async_busy[
        PS3_SPU_ASYNC_COLOR_BUFFERS];

static uint32_t
    g_ps3_spu_async_count;

static uint32_t
    g_ps3_spu_async_bytes;


/*
 * Exact 16-byte payload written by SPU.
 */
struct ps3_spu_color_done_flag
{
    volatile uint32_t done;
    volatile uint32_t count;
    volatile uint32_t bytes;
    volatile uint32_t status;
};

static ps3_spu_color_done_flag
    g_ps3_spu_color_done[
        PS3_SPU_ASYNC_COLOR_BUFFERS]
        __attribute__((aligned(128)));


static ps3_spu_color_batch g_ps3_spu_color_batch
    __attribute__((aligned(128)));

static int ps3_spu_wait_event(
    uint32_t expected_data0,
    uint32_t expected_data1)
{
    sys_event_t event;
    s32 ret;

    ret = sysEventQueueReceive(
        g_ps3_spu_queue,
        &event,
        0);

    if (ret != 0)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU EventReceive failed "
            "ret=0x%08x\n",
            (unsigned)ret);
#endif
        return -1;
    }

    if (event.source !=
            SPU_THREAD_EVENT_USER_KEY ||
        event.data_1 !=
            g_ps3_spu_thread ||
        (event.data_2 >> 32) !=
            PS3_SPU_EVENT_PORT ||
        (uint32_t)(
            event.data_2 &
            0x00ffffffu) !=
            (expected_data0 &
             0x00ffffffu) ||
        (uint32_t)event.data_3 !=
            expected_data1)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU event mismatch "
            "source=%llx thread=%llx "
            "data2=%llx data3=%llx "
            "expected0=%08x expected1=%08x\n",
            (unsigned long long)event.source,
            (unsigned long long)event.data_1,
            (unsigned long long)event.data_2,
            (unsigned long long)event.data_3,
            (unsigned)expected_data0,
            (unsigned)expected_data1);
#endif
        return -1;
    }

    return 0;
}

static int ps3_spu_dma_selftest(void)
{
    static const uint32_t test_input[
        PS3_SPU_DMA_WORDS] =
    {
        0x00000000u,
        0xffffffffu,
        0x12345678u,
        0xa5a5a5a5u,
        0x0f0f0f0fu,
        0xdeadbeefu,
        0x13579bdfu,
        0x2468ace0u
    };

    uint32_t expected;
    uint32_t checksum = 0;
    uint32_t mismatches = 0;
    uint32_t i;

    const uint32_t sequence = 1;

    memset(
        &g_ps3_spu_dma_block,
        0,
        sizeof(g_ps3_spu_dma_block));

    g_ps3_spu_dma_block.magic =
        PS3_SPU_DMA_MAGIC;

    g_ps3_spu_dma_block.command =
        PS3_SPU_DMA_COMMAND_TEST;

    g_ps3_spu_dma_block.sequence =
        sequence;

    g_ps3_spu_dma_block.status =
        PS3_SPU_DMA_STATUS_PENDING;

    for (i = 0;
         i < PS3_SPU_DMA_WORDS;
         i++)
    {
        g_ps3_spu_dma_block.input[i] =
            test_input[i];
    }

    /*
     * Ensure all PPU stores are committed before the SPU
     * receives the command.
     */
    __sync_synchronize();

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU DMA selftest begin "
        "ea=%llx bytes=%u\n",
        (unsigned long long)
            (u64)(uintptr_t)
            &g_ps3_spu_dma_block,
        (unsigned)
            sizeof(g_ps3_spu_dma_block));
#endif

    s32 ret = sysSpuThreadWriteSignal(
        g_ps3_spu_thread,
        0,
        PS3_SPU_SIGNAL_DMA_TEST);

    if (ret != 0)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU DMA WriteSignal failed "
            "ret=0x%08x\n",
            (unsigned)ret);
#endif
        return -1;
    }

    if (ps3_spu_wait_event(
            PS3_SPU_EVENT_DMA_DONE,
            sequence) != 0)
    {
        return -1;
    }

    /*
     * SPU waited for mfc_put completion before posting event.
     * Force the PPU compiler to reload the shared block.
     */
    __sync_synchronize();

    if (g_ps3_spu_dma_block.magic !=
            PS3_SPU_DMA_MAGIC ||
        g_ps3_spu_dma_block.command !=
            PS3_SPU_DMA_COMMAND_TEST ||
        g_ps3_spu_dma_block.sequence !=
            sequence ||
        g_ps3_spu_dma_block.status !=
            PS3_SPU_DMA_STATUS_DONE)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU DMA block header FAIL "
            "magic=%08x cmd=%08x seq=%u "
            "status=%08x\n",
            (unsigned)
                g_ps3_spu_dma_block.magic,
            (unsigned)
                g_ps3_spu_dma_block.command,
            (unsigned)
                g_ps3_spu_dma_block.sequence,
            (unsigned)
                g_ps3_spu_dma_block.status);
#endif
        return -1;
    }

    for (i = 0;
         i < PS3_SPU_DMA_WORDS;
         i++)
    {
        expected =
            test_input[i] ^
            0xffffffffu;

        if (g_ps3_spu_dma_block.output[i] !=
            expected)
        {
            mismatches++;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
            ps3_mem_diag_logf(
                "SPU DMA mismatch index=%u "
                "input=%08x got=%08x "
                "expected=%08x\n",
                (unsigned)i,
                (unsigned)test_input[i],
                (unsigned)
                    g_ps3_spu_dma_block.output[i],
                (unsigned)expected);
#endif
        }

        checksum ^=
            g_ps3_spu_dma_block.output[i];
    }

    if (mismatches != 0 ||
        checksum !=
            g_ps3_spu_dma_block.checksum)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU DMA verify FAIL "
            "mismatches=%u "
            "checksum=%08x spu_checksum=%08x\n",
            (unsigned)mismatches,
            (unsigned)checksum,
            (unsigned)
                g_ps3_spu_dma_block.checksum);
#endif
        return -1;
    }

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU DMA get/put PASS bytes=%u\n",
        (unsigned)
            sizeof(g_ps3_spu_dma_block));

    ps3_mem_diag_logf(
        "SPU DMA verify PASS "
        "mismatches=0 checksum=%08x\n",
        (unsigned)checksum);
#endif

    return 0;
}

extern "C"

int ps3_pgm_spu_worker_ready(void)
{
    return g_ps3_spu_ready;
}

extern "C"
int ps3_pgm_spu_worker_init(void)
{
    sysSpuThreadArgument argument = {
        0, 0, 0, 0
    };

    sysSpuThreadGroupAttribute group_attr = {
        sizeof("fbspug"),
        "fbspug",
        0,
        { 0 }
    };

    sysSpuThreadAttribute thread_attr = {
        "fbspu",
        sizeof("fbspu"),
        SPU_THREAD_ATTR_NONE
    };

    sys_event_queue_attr_t queue_attr = {
        SYS_EVENT_QUEUE_FIFO,
        SYS_EVENT_QUEUE_PPU,
        "fbspuq"
    };

    s32 ret;

    if (g_ps3_spu_ready)
        return 0;

    if (g_ps3_spu_attempted)
        return -1;

    g_ps3_spu_attempted = 1;

    memset(
        &g_ps3_spu_image,
        0,
        sizeof(g_ps3_spu_image));

    /*
     * Permanent EA passed to SPU main(arg0).
     */
    argument.arg0 =
        (u64)(uintptr_t)
        &g_ps3_spu_dma_block;

    argument.arg1 =
        (u64)(uintptr_t)
        &g_ps3_spu_color_batch;


    /*
     * v1-E2 async completion flag EA.
     *
     * SPU writes this after DMA result return.
     */
    argument.arg2 =
        (u64)(uintptr_t)
        &g_ps3_spu_color_done[0];


    /*
     * v1-F1:
     *
     * Fourth startup argument is no longer used.
     * Compact async shares the proven arg1 color-batch EA.
     */
    argument.arg3 = 0;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU worker init begin "
        "elf_bytes=%u dma_ea=%llx\n",
        ps3_sprite_worker_spu_bin_size,
        (unsigned long long)
            argument.arg0);
#endif

    ret = sysSpuInitialize(6, 0);
    if (ret != 0)
        return -1;

    ret = sysSpuImageImport(
        &g_ps3_spu_image,
        ps3_sprite_worker_spu_bin,
        0);

    if (ret != 0)
        return -1;

    ret = sysSpuThreadGroupCreate(
        &g_ps3_spu_group,
        1,
        100,
        &group_attr);

    if (ret != 0)
        return -1;

    ret = sysEventQueueCreate(
        &g_ps3_spu_queue,
        &queue_attr,
        0x46425350,
        16);

    if (ret != 0)
        return -1;

    ret = sysSpuThreadInitialize(
        &g_ps3_spu_thread,
        g_ps3_spu_group,
        0,
        &g_ps3_spu_image,
        &thread_attr,
        &argument);

    if (ret != 0)
        return -1;

    ret = sysSpuThreadSetConfiguration(
        g_ps3_spu_thread,
        SPU_SIGNAL1_OVERWRITE |
        SPU_SIGNAL2_OVERWRITE);

    if (ret != 0)
        return -1;

    ret = sysSpuThreadConnectEvent(
        g_ps3_spu_thread,
        g_ps3_spu_queue,
        SPU_THREAD_EVENT_USER,
        PS3_SPU_EVENT_PORT);

    if (ret != 0)
        return -1;

    ret = sysSpuThreadGroupStart(
        g_ps3_spu_group);

    if (ret != 0)
        return -1;

    /*
     * Preserve v1-A communication self-test.
     */
    ret = sysSpuThreadWriteSignal(
        g_ps3_spu_thread,
        0,
        PS3_SPU_SIGNAL_SELFTEST);

    if (ret != 0)
        return -1;

    if (ps3_spu_wait_event(
            33u,
            55u) != 0)
    {
        return -1;
    }

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU worker selftest PASS "
        "input=11 data0=33 data1=55\n");
#endif

    /*
     * New v1-B DMA self-test.
     */
    if (ps3_spu_dma_selftest() != 0)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU worker DMA selftest FAIL\n");
#endif
        return -1;
    }

    g_ps3_spu_ready = 1;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU worker V1-F1 compact async ready\n");
#endif

    return 0;
}




/*
 * v1-E1 async submit
 *
 * This stage submits into double buffer.
 * Event polling will be added separately.
 */

int ps3_pgm_spu_color_submit(
    ps3_spu_color_batch *batch)
{
    uint32_t count;
    uint32_t bytes;
    uint32_t command;
    s32 ret;

    if (!g_ps3_spu_ready ||
        batch == NULL)
    {
        return -1;
    }

    count =
        batch->count;

    if (count == 0 ||
        count >
            PS3_SPU_COLOR_BATCH_SIZE)
    {
        return -1;
    }

    if (batch->magic == PS3_SPU_SIGNAL_COLOR_STREAM)
    {
        ps3_spu_color_stream_batch *stream =
            (ps3_spu_color_stream_batch *)batch;

        if (stream->phase > 2 ||
            stream->packed_bytes == 0 ||
            stream->packed_bytes > PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES)
            return -1;

        bytes = PS3_SPU_COLOR_STREAM_DMA_BYTES(stream->packed_bytes);
    }
    else
    {
        bytes = PS3_SPU_COLOR_COMPACT_BYTES(count);
    }

    if ((bytes & 15u) != 0 ||
        bytes >
            sizeof(ps3_spu_color_batch))
    {
        return -1;
    }

    /*
     * Keep v1-F single-inflight.
     */
    if (g_ps3_spu_async_busy[0])
        return 1;

    /*
     * PPU also copies only the compact prefix.
     */
    memcpy(
        &g_ps3_spu_color_batch,
        batch,
        bytes);

    g_ps3_spu_async_count =
        count;

    g_ps3_spu_async_bytes =
        bytes;

    /*
     * Clear all 16 completion bytes before publishing
     * the command.
     */
    g_ps3_spu_color_done[0].done =
        0;

    g_ps3_spu_color_done[0].count =
        0;

    g_ps3_spu_color_done[0].bytes =
        0;

    g_ps3_spu_color_done[0].status =
        0;

    __sync_synchronize();

    g_ps3_spu_async_busy[0] =
        1;

    if (batch->magic == PS3_SPU_SIGNAL_COLOR_STREAM)
    {
        ps3_spu_color_stream_batch *stream =
            (ps3_spu_color_stream_batch *)batch;
        command = PS3_SPU_SIGNAL_COLOR_STREAM_BASE |
            ((stream->phase << PS3_SPU_COLOR_STREAM_PHASE_SHIFT) &
             PS3_SPU_COLOR_STREAM_PHASE_MASK) |
            (count & PS3_SPU_COLOR_ASYNC_COUNT_MASK);
    }
    else
    {
        command = PS3_SPU_SIGNAL_COLOR_ASYNC_BASE |
            (count & PS3_SPU_COLOR_ASYNC_COUNT_MASK);
    }

    ret =
        sysSpuThreadWriteSignal(
            g_ps3_spu_thread,
            0,
            command);

    if (ret != 0)
    {
        g_ps3_spu_async_busy[0] =
            0;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU V1-F ASYNC signal FAIL "
            "ret=0x%08x count=%u bytes=%u\n",
            (unsigned)ret,
            (unsigned)count,
            (unsigned)bytes);
#endif

        return -1;
    }

    return 0;
}


int ps3_pgm_spu_color_poll(
    ps3_spu_color_result_batch *results,
    uint32_t *count_out)
{
    uint32_t done;
    uint32_t count;
    uint32_t bytes;
    uint32_t status;

    if (!g_ps3_spu_ready ||
        results == NULL ||
        count_out == NULL)
    {
        return -1;
    }

    if (!g_ps3_spu_async_busy[0])
        return 1;

    /*
     * Pure nonblocking poll.
     *
     * v1-F async SPU path emits no event.
     */
    /*
     * H11-C:
     * Speculative volatile completion read.
     *
     * If done is still zero, execute a full visibility fence
     * and re-read before returning pending.
     *
     * If done is already visible, skip the pre-check fence.
     * The original full completion-path fence remains below
     * before count/bytes/status/result are consumed.
     */

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    {
        static uint32_t last_stage =
            0xffffffffu;

        static uint32_t pending_polls;

        volatile uint32_t *dbg =
            (volatile uint32_t *)
            &g_ps3_spu_dma_block;

        pending_polls++;

        if (dbg[0] ==
            PS3_SPU_V1F1_DEBUG_MAGIC)
        {
            uint32_t stage =
                dbg[1];

            if (stage != last_stage ||
                (pending_polls &
                 0x0fffffu) == 0)
            {
                ps3_mem_diag_logf(
                    "SPU V1-F1 DEBUG "
                    "stage=%u count=%u bytes=%u "
                    "pending_polls=%u\n",
                    (unsigned)stage,
                    (unsigned)dbg[2],
                    (unsigned)dbg[3],
                    (unsigned)pending_polls);

                last_stage =
                    stage;
            }
        }
    }
#endif

    done =
        g_ps3_spu_color_done[0].done;

    if (done == 0)
    {
        /*
         * A zero may be an actually pending job or a stale
         * observation of an SPU DMA completion.  Before we
         * return pending, force full visibility and try once
         * more.
         */
        __sync_synchronize();

        done =
            g_ps3_spu_color_done[0].done;

        if (done == 0)
            return 1;
    }

    /*
     * Completion observed.  Preserve the existing full fence
     * before consuming completion metadata and DMA results.
     */
    __sync_synchronize();

    count =
        g_ps3_spu_color_done[0].count;

    bytes =
        g_ps3_spu_color_done[0].bytes;

    status =
        g_ps3_spu_color_done[0].status;

    if (done !=
            PS3_SPU_COLOR_ASYNC_DONE_OK ||
        status !=
            PS3_SPU_COLOR_ASYNC_STATUS_OK ||
        count !=
            g_ps3_spu_async_count ||
        bytes !=
            g_ps3_spu_async_bytes)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU V1-F ASYNC completion FAIL "
            "done=%u count=%u/%u "
            "bytes=%u/%u status=%u\n",
            (unsigned)done,
            (unsigned)count,
            (unsigned)
                g_ps3_spu_async_count,
            (unsigned)bytes,
            (unsigned)
                g_ps3_spu_async_bytes,
            (unsigned)status);
#endif

        g_ps3_spu_color_done[0].done =
            0;

        g_ps3_spu_async_busy[0] =
            0;

        return -1;
    }

    /*
     * H14-A:
     * SPU has already packed result[8] for all active items
     * into the beginning of the shared arg1 buffer.
     *
     * Copy one contiguous payload into the renderer-owned
     * result cache before allowing the next submit to reuse
     * the shared buffer.
     */
    memcpy(
        results->result,
        &g_ps3_spu_color_batch,
        PS3_SPU_COLOR_RESULT_DATA_BYTES(
            count));

    *count_out =
        count;

    g_ps3_spu_color_done[0].done =
        0;

    g_ps3_spu_async_busy[0] =
        0;

    return 0;
}


int ps3_pgm_spu_color_batch(
    ps3_spu_color_batch *batch)
{
    s32 ret;

    if (!g_ps3_spu_ready || batch == NULL)
        return -1;


    /*
     * signal1 is configured OVERWRITE.
     *
     * Never submit a legacy command while an async command
     * may still be pending or executing.
     */
    if (g_ps3_spu_async_busy[0])
        return -1;

    /*
     * Copy caller batch into the permanent 128-byte aligned
     * PPU/SPU shared buffer whose EA was passed at SPU startup.
     */
    memcpy(
        &g_ps3_spu_color_batch,
        batch,
        sizeof(g_ps3_spu_color_batch));

    __sync_synchronize();

    ret = sysSpuThreadWriteSignal(
        g_ps3_spu_thread,
        0,
        PS3_SPU_SIGNAL_COLOR_BATCH);

    if (ret != 0)
    {
#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
        ps3_mem_diag_logf(
            "SPU COLOR signal FAIL ret=0x%08x\n",
            (unsigned)ret);
#endif
        return -1;
    }

    /*
     * COLOR_DONE data3 is transport status only.
     * Decode mismatch count comes back in the shared batch.
     */
    if (ps3_spu_wait_event(
            PS3_SPU_EVENT_COLOR_DONE,
            0) != 0)
    {
        return -1;
    }

    __sync_synchronize();

    memcpy(
        batch,
        &g_ps3_spu_color_batch,
        sizeof(g_ps3_spu_color_batch));

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    static uint32_t color_done_count;

    color_done_count++;

    if (color_done_count == 1 ||
        (color_done_count & 0xffu) == 0)
    {
        ps3_mem_diag_logf(
            "SPU COLOR DONE batches=%u count=%u mismatch=%u\n",
            (unsigned)color_done_count,
            (unsigned)batch->count,
            (unsigned)batch->mismatch);
    }
#endif

    return 0;
}


extern "C"
void ps3_pgm_spu_worker_shutdown(void)
{
    u32 cause = 0;
    u32 status = 0;

    if (!g_ps3_spu_ready)
    {
        g_ps3_spu_attempted = 0;
        return;
    }

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU worker shutdown begin\n");
#endif

    sysSpuThreadWriteSignal(
        g_ps3_spu_thread,
        0,
        PS3_SPU_SIGNAL_SHUTDOWN);

    sysSpuThreadGroupJoin(
        g_ps3_spu_group,
        &cause,
        &status);

    sysSpuThreadDisconnectEvent(
        g_ps3_spu_thread,
        SPU_THREAD_EVENT_USER,
        PS3_SPU_EVENT_PORT);

    sysSpuThreadGroupDestroy(
        g_ps3_spu_group);

    sysSpuImageClose(
        &g_ps3_spu_image);

    sysEventQueueDestroy(
        g_ps3_spu_queue,
        0);

    g_ps3_spu_ready = 0;
    g_ps3_spu_attempted = 0;

#if defined(PS3_MEMORY_DIAGNOSTIC) && PS3_MEMORY_DIAGNOSTIC
    ps3_mem_diag_logf(
        "SPU worker shutdown OK "
        "cause=%08x status=%08x\n",
        (unsigned)cause,
        (unsigned)status);
#endif
}

#endif
