#ifndef FBNEO_PS3_SPRITE_WORKER_PROTOCOL_H
#define FBNEO_PS3_SPRITE_WORKER_PROTOCOL_H

#include <stdint.h>

#define PS3_SPU_EVENT_PORT          10

#define PS3_SPU_SIGNAL_SELFTEST     11u
#define PS3_SPU_SIGNAL_DMA_TEST     0x444d4101u
#define PS3_SPU_SIGNAL_SHUTDOWN     0xffffffffu

#define PS3_SPU_EVENT_DMA_DONE      0x00444d41u
#define PS3_SPU_EVENT_ERROR         0x00455252u

/*
 * v1-F1 first-job diagnostic marker.
 *
 * Written into the already existing DMA selftest block after
 * selftest has completed.
 */
#define PS3_SPU_V1F1_DEBUG_MAGIC          0x46314442u

#define PS3_SPU_V1F1_STAGE_ENTER          1u
#define PS3_SPU_V1F1_STAGE_BEFORE_GET     2u
#define PS3_SPU_V1F1_STAGE_AFTER_GET      3u
#define PS3_SPU_V1F1_STAGE_HEADER_OK      4u
#define PS3_SPU_V1F1_STAGE_DECODE_DONE    5u
#define PS3_SPU_V1F1_STAGE_RESULT_PUT     6u
#define PS3_SPU_V1F1_STAGE_DONE_PUT       7u


#define PS3_SPU_DMA_MAGIC           0x53505544u
#define PS3_SPU_DMA_COMMAND_TEST    0x00000001u

#define PS3_SPU_DMA_STATUS_PENDING  0x00000000u
#define PS3_SPU_DMA_STATUS_DONE     0x600d0001u
#define PS3_SPU_DMA_STATUS_BAD      0xbad00001u

#define PS3_SPU_DMA_WORDS           8

typedef struct ps3_spu_dma_test_block
{
    uint32_t magic;
    uint32_t command;
    uint32_t sequence;
    uint32_t status;

    uint32_t input[PS3_SPU_DMA_WORDS];
    uint32_t output[PS3_SPU_DMA_WORDS];

    uint32_t checksum;

    /*
     * 32 total uint32_t words = 128 bytes.
     */
    uint32_t reserved[11];

} ps3_spu_dma_test_block;

typedef char ps3_spu_dma_block_must_be_128_bytes[
    (sizeof(ps3_spu_dma_test_block) == 128) ? 1 : -1
];

#endif
