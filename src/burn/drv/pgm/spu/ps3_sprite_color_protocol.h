#ifndef FBNEO_PS3_SPRITE_COLOR_PROTOCOL_H
#define FBNEO_PS3_SPRITE_COLOR_PROTOCOL_H

#include <stdint.h>

#define PS3_SPU_COLOR_BATCH_SIZE 64

#define PS3_SPU_SIGNAL_COLOR_BATCH 0x434f4c52u
#define PS3_SPU_SIGNAL_COLOR_STREAM 0x43535452u
#define PS3_SPU_EVENT_COLOR_DONE   0x434f4c44u

/*
 * v1-F compact render-only async command.
 *
 * Low 7 bits carry active item count, 1..64.
 */
#define PS3_SPU_SIGNAL_COLOR_ASYNC_BASE       0x43410000u
#define PS3_SPU_SIGNAL_COLOR_ASYNC_MASK       0xffffff80u
#define PS3_SPU_COLOR_ASYNC_COUNT_MASK        0x0000007fu
#define PS3_SPU_SIGNAL_COLOR_STREAM_BASE      0x43410200u
#define PS3_SPU_SIGNAL_COLOR_STREAM_MASK      0xfffffe00u
#define PS3_SPU_COLOR_STREAM_PHASE_SHIFT      7u
#define PS3_SPU_COLOR_STREAM_PHASE_MASK       0x00000180u

#define PS3_SPU_COLOR_ASYNC_DONE_OK           1u
#define PS3_SPU_COLOR_ASYNC_DONE_ERROR        2u

#define PS3_SPU_COLOR_ASYNC_STATUS_OK         0u
#define PS3_SPU_COLOR_ASYNC_STATUS_BAD_COUNT  1u
#define PS3_SPU_COLOR_ASYNC_STATUS_BAD_HEADER 2u

/*
 * Each item is fixed at 32 bytes.
 *
 * Batch header is 8 bytes.
 *
 * Compact DMA starts from the aligned batch base and
 * deliberately transfers another 8-byte guard after the
 * final active item.  Therefore every transfer remains
 * 16-byte aligned without using item[0]'s +8 EA.
 *
 * count 16 ->  528 bytes
 * count 32 -> 1040 bytes
 * count 64 -> 2064 bytes
 */
#define PS3_SPU_COLOR_ITEM_BYTES 32u

#define PS3_SPU_COLOR_COMPACT_BYTES(count) \
    (16u + \
    ((uint32_t)(count) * \
    PS3_SPU_COLOR_ITEM_BYTES))

#define PS3_SPU_COLOR_STREAM_HEADER_BYTES 16u
#define PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES 344u
#define PS3_SPU_COLOR_STREAM_DMA_BYTES(packed_bytes) \
    ((PS3_SPU_COLOR_STREAM_HEADER_BYTES + (uint32_t)(packed_bytes) + 15u) & ~15u)

typedef struct
{
    uint32_t magic;
    uint32_t count;
    uint32_t phase;
    uint32_t packed_bytes;
    uint8_t packed[PS3_SPU_COLOR_STREAM_MAX_PACKED_BYTES];
    uint8_t padding[8];
} ps3_spu_color_stream_batch;

typedef char ps3_spu_color_stream_batch_size_check[
    (sizeof(ps3_spu_color_stream_batch) == 368u) ? 1 : -1
];



/*
 * H14-A compact async result return.
 *
 * SPU input remains the existing 32-byte/item compact batch.
 * Output is packed as contiguous 8-byte decoded results.
 *
 * DMA size is rounded up to 16 bytes.
 */
#define PS3_SPU_COLOR_RESULT_ITEM_BYTES 8u

#define PS3_SPU_COLOR_RESULT_MAX_BYTES \
    (PS3_SPU_COLOR_BATCH_SIZE * \
     PS3_SPU_COLOR_RESULT_ITEM_BYTES)

#define PS3_SPU_COLOR_RESULT_DATA_BYTES(count) \
    ((uint32_t)(count) * \
     PS3_SPU_COLOR_RESULT_ITEM_BYTES)

#define PS3_SPU_COLOR_RESULT_DMA_BYTES(count) \
    ((PS3_SPU_COLOR_RESULT_DATA_BYTES(count) + 15u) & ~15u)


typedef struct
{
    uint8_t result[
        PS3_SPU_COLOR_BATCH_SIZE
    ][
        PS3_SPU_COLOR_RESULT_ITEM_BYTES
    ];

} ps3_spu_color_result_batch;


typedef char ps3_spu_color_result_batch_size_check[
    (sizeof(ps3_spu_color_result_batch) ==
     PS3_SPU_COLOR_RESULT_MAX_BYTES)
    ? 1 : -1
];


typedef struct
{
    uint8_t packed[8];

    uint8_t phase;
    uint8_t span_bytes;

    uint8_t reserved0;
    uint8_t reserved1;

    uint8_t expected[8];

    uint8_t result[8];

    /*
     * Pad item to 32 bytes.
     * Keeps every item DMA friendly.
     */
    uint8_t padding[4];

} ps3_spu_color_item;


typedef char ps3_spu_color_item_size_check[
    (sizeof(ps3_spu_color_item) ==
     PS3_SPU_COLOR_ITEM_BYTES)
    ? 1 : -1
];


typedef struct
{
    uint32_t magic;
    uint32_t count;

    ps3_spu_color_item item[
        PS3_SPU_COLOR_BATCH_SIZE
    ];

    uint32_t mismatch;

    /*
     * Align total structure size.
     */
    uint8_t tail_padding[4];

} ps3_spu_color_batch;


typedef char ps3_spu_color_batch_align_check[
    ((sizeof(ps3_spu_color_batch) & 15) == 0)
    ? 1 : -1
];


typedef char ps3_spu_color_batch_size_check[
    (sizeof(ps3_spu_color_batch) == 2064)
    ? 1 : -1
];


#endif
