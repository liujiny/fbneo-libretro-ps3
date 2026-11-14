// Row-major execution of opaque, neutral two-pixel staging columns. Keeping
// one source/destination row hot avoids revisiting it after a 32 KiB stride.
#ifndef FBNEO_EPIC12_STAGING_COLUMNS_H
#define FBNEO_EPIC12_STAGING_COLUMNS_H
#include <stdint.h>

struct Epic12StagingColumns {
    enum { MAX_COMMANDS = 128 };
    unsigned count, width, height;
    unsigned source[MAX_COMMANDS];
    unsigned source_sx[MAX_COMMANDS];
    unsigned source_sy[MAX_COMMANDS];
    bool same_sy;
    unsigned source_x, source_y, source_width, source_height;
};

// Reject partial clipping, wrapping, flags and source/destination aliasing
// before any write. The command copy is immutable during blitter execution.
static unsigned epic12_staging_collect_columns(const uint16_t *ram,
    unsigned mask, unsigned address, int clip_x0, int clip_y0,
    int clip_x1, int clip_y1, Epic12StagingColumns &out)
{
    out.count = 0;
    out.same_sy = true;
    if (mask == ~0u) return 0;
    const unsigned bytes = mask + 1;
    int x = 0, y = 0, height = 0, previous_x = 0;
    unsigned sx0 = 8192, sy0 = 4096, sx1 = 0, sy1 = 0;
    unsigned first_sy = 0;
    for (unsigned i = 0; i < Epic12StagingColumns::MAX_COMMANDS; ++i) {
        unsigned off = (address + i * 20) & mask;
        if ((off & 1u) || off > bytes || bytes - off < 20) break;
        const uint16_t *p = ram + (off >> 1);
        if ((p[0] & 0xff00) != 0x1000 || (p[6] & 8191) != 1 ||
            p[8] != 0x0080 || p[9] != 0x8080) break;
        const int nx = (int16_t)p[4], ny = (int16_t)p[5];
        const unsigned sx = p[2] & 8191, sy = p[3] & 4095;
        const int h = (p[7] & 4095) + 1;
        if (nx < 768 || nx + 2 > 8192 || ny < 0 || ny + h > 4096 ||
            sx + 2 > 8192 || sy + h > 4096 || h < 8 ||
            nx < clip_x0 || nx + 1 > clip_x1 ||
            ny < clip_y0 || ny + h - 1 > clip_y1) break;
        if (i == 0) { x = nx; y = ny; height = h; first_sy = sy; }
        else if (ny != y || h != height || nx != previous_x + 1) break;
        out.source[i] = sy * 8192 + sx;
        out.source_sx[i] = sx;
        out.source_sy[i] = sy;
        if (sy != first_sy) out.same_sy = false;
        if (sx < sx0) sx0 = sx;
        if (sy < sy0) sy0 = sy;
        if (sx + 1 > sx1) sx1 = sx + 1;
        if (sy + h - 1 > sy1) sy1 = sy + h - 1;
        previous_x = nx;
        ++out.count;
        if (!(sx1 < (unsigned)x || sx0 > (unsigned)(previous_x + 1) ||
            sy1 < (unsigned)y || sy0 > (unsigned)(y + height - 1))) {
            out.count = 0;
            return 0;
        }
    }
    if (out.count < 4) {
        out.count = 0;
        return 0;
    }
    out.width = (unsigned)(previous_x - x + 2);
    out.height = (unsigned)height;
    out.source_x = sx0; out.source_y = sy0;
    out.source_width = sx1 - sx0 + 1; out.source_height = sy1 - sy0 + 1;
    return out.count;
}

#ifdef __PS3__
// Forward declarations for PS3 VRAM access
static inline UINT16* epic12_get_read_row(UINT32 y);
static inline UINT16* epic12_get_write_row(UINT32 y);

static void epic12_staging_draw_columns_ps3(int dst_x, int dst_y,
    const Epic12StagingColumns &columns)
{
    if (!columns.count) return;
    for (unsigned row = 0; row < columns.height; ++row) {
        UINT16 *dst = epic12_get_write_row((dst_y + row) & 4095);
        if (!dst) continue;
        dst += dst_x;

        if (columns.same_sy) {
            const UINT16 *src = epic12_get_read_row((columns.source_sy[0] + row) & 4095);
            unsigned i = 0;
            for (; i + 3 < columns.count; i += 4) {
                UINT16 p0 = src[columns.source_sx[i + 0]];
                UINT16 p1 = src[columns.source_sx[i + 1]];
                UINT16 p2 = src[columns.source_sx[i + 2]];
                UINT16 p3 = src[columns.source_sx[i + 3]];
                dst[i + 0] = p0;
                dst[i + 1] = p1;
                dst[i + 2] = p2;
                dst[i + 3] = p3;
            }
            for (; i < columns.count; ++i) {
                dst[i] = src[columns.source_sx[i]];
            }
            dst[columns.count] = src[columns.source_sx[columns.count - 1] + 1];
        } else {
            unsigned i = 0;
            for (; i + 3 < columns.count; i += 4) {
                const UINT16 *src0 = epic12_get_read_row((columns.source_sy[i + 0] + row) & 4095);
                const UINT16 *src1 = epic12_get_read_row((columns.source_sy[i + 1] + row) & 4095);
                const UINT16 *src2 = epic12_get_read_row((columns.source_sy[i + 2] + row) & 4095);
                const UINT16 *src3 = epic12_get_read_row((columns.source_sy[i + 3] + row) & 4095);
                UINT16 p0 = src0[columns.source_sx[i + 0]];
                UINT16 p1 = src1[columns.source_sx[i + 1]];
                UINT16 p2 = src2[columns.source_sx[i + 2]];
                UINT16 p3 = src3[columns.source_sx[i + 3]];
                dst[i + 0] = p0;
                dst[i + 1] = p1;
                dst[i + 2] = p2;
                dst[i + 3] = p3;
            }
            for (; i < columns.count; ++i) {
                const UINT16 *src = epic12_get_read_row((columns.source_sy[i] + row) & 4095);
                dst[i] = src[columns.source_sx[i]];
            }
            const UINT16 *src_last = epic12_get_read_row((columns.source_sy[columns.count - 1] + row) & 4095);
            dst[columns.count] = src_last[columns.source_sx[columns.count - 1] + 1];
        }
    }
}
#endif

static void epic12_staging_draw_columns(uint32_t *vram, unsigned destination,
    const Epic12StagingColumns &columns)
{
    if (!columns.count) return;
    for (unsigned row = 0; row < columns.height; ++row) {
        uint32_t *dst = vram + destination + row * 8192;
        const uint32_t *src = vram + row * 8192;
        unsigned i = 0;
        for (; i + 3 < columns.count; i += 4) {
            uint32_t p0 = src[columns.source[i + 0]];
            uint32_t p1 = src[columns.source[i + 1]];
            uint32_t p2 = src[columns.source[i + 2]];
            uint32_t p3 = src[columns.source[i + 3]];
            dst[i + 0] = p0;
            dst[i + 1] = p1;
            dst[i + 2] = p2;
            dst[i + 3] = p3;
        }
        for (; i < columns.count; ++i) {
            dst[i] = src[columns.source[i]];
        }
        dst[columns.count] = src[columns.source[columns.count - 1] + 1];
    }
}

#endif // FBNEO_EPIC12_STAGING_COLUMNS_H
