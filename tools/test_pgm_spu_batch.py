#!/usr/bin/env python3
"""Differential test of the production batch builder against a saved baseline.

Run inside ps3dev-gcc13:rsxfix; uses its native compiler and ASan/UBSan,
not PS3 timing. Pass the pre-optimization pgm_draw.cpp as the sole argument.
"""
import pathlib
import re
import subprocess
import sys
import tempfile


def builder(path, name):
    source = path.read_text()
    original = "pgm_ps3_spu_prepare_color_batch"
    start = source.index("static inline UINT32\n" + original)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end].replace(original, name)


root = pathlib.Path(__file__).resolve().parents[1]
if not pathlib.Path("/.dockerenv").exists():
    sys.exit("Run this test inside the requested Docker build environment.")
if len(sys.argv) != 2:
    sys.exit("usage: test_pgm_spu_batch.py BASELINE_PGM_DRAW_CPP")
baseline = pathlib.Path(sys.argv[1])
current = root / "src/burn/drv/pgm/pgm_draw.cpp"

# Check the real variadic log call, whose declaration has no printf attribute.
source = current.read_text()
log = source[source.index('"SPU V1-H15-'):]
log = log[:log.index("stat_shadow_pixel_bad);") + len("stat_shadow_pixel_bad);")]
formats = re.findall(r"%llu|%u", log)
arguments = re.findall(r"\(unsigned(?: long long)?\)", log)
assert len(formats) == len(arguments), (len(formats), len(arguments))
assert all((fmt == "%llu") == ("long long" in arg)
           for fmt, arg in zip(formats, arguments))
for counter in ("stream_short_skip", "gate12_submit"):
    assert f"{counter}=%llu " in log
    assert f"(unsigned long long)stat_{counter}" in log
print(f"STATS format: {len(formats)} arguments PASS", flush=True)

prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "src/burn/drv/pgm/spu/ps3_sprite_color_protocol.h"
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
#define PGM_PS3_COLOR_PAGE_SIZE 65536u
static UINT32 nPGMSPRColMaskLen;
static const UINT32 rom_size = 4 * PGM_PS3_COLOR_PAGE_SIZE;
static UINT8 rom[rom_size];
static UINT8 page_buffer[PGM_PS3_COLOR_PAGE_SIZE];
static bool file_cache;
static UINT32 resident_page, lookups;
static std::vector<UINT32> pages;
static void require(bool ok) { if (!ok) abort(); }
static void reset_cache() {
    resident_page = UINT32(-1);
    lookups = 0;
    pages.clear();
}
static const UINT8 *pgm_ps3_color_cache_span(UINT32 offset, UINT32 bytes) {
    ++lookups;
    require(offset <= rom_size && bytes <= rom_size - offset);
    if (!file_cache) return rom + offset;
    UINT32 in_page = offset & (PGM_PS3_COLOR_PAGE_SIZE - 1);
    if (bytes > PGM_PS3_COLOR_PAGE_SIZE - in_page) return NULL;
    UINT32 page = offset / PGM_PS3_COLOR_PAGE_SIZE;
    if (resident_page != page) {
        // Evict/overwrite storage, exposing stale pointers at transitions.
        memcpy(page_buffer, rom + page * PGM_PS3_COLOR_PAGE_SIZE,
               PGM_PS3_COLOR_PAGE_SIZE);
        resident_page = page;
        pages.push_back(page);
    }
    return page_buffer + in_page;
}
'''

checks = r'''
static unsigned long long cases;
static void check(UINT32 pixel, UINT32 actual, UINT32 mask, UINT32 limit) {
    ps3_spu_color_batch before, after;
    nPGMSPRColMaskLen = mask;
    memset(&before, 0xa5, sizeof(before));
    memset(&after, 0x5a, sizeof(after));
    reset_cache();
    UINT32 old_count = baseline_builder(&before, pixel, actual, limit);
    UINT32 old_lookups = lookups;
    std::vector<UINT32> old_pages = pages;
    reset_cache();
    UINT32 new_count = current_builder(&after, pixel, actual, limit);
    require(old_count == new_count && after.count == new_count);
    require(lookups <= old_lookups && pages == old_pages);
    require(memcmp(&before, &after,
                   PS3_SPU_COLOR_COMPACT_BYTES(new_count)) == 0);
    // Independent per-pixel oracle for all three phases and bit 15 ignored.
    for (UINT32 i = 0; i < new_count; ++i) {
        for (UINT32 j = 0; j < 8; ++j) {
            UINT32 p = pixel + i * 8 + j;
            UINT32 off = (p / 3) * 2;
            UINT32 word = rom[off] | (rom[off + 1] << 8);
            UINT32 item_pixel = after.item[i].phase + j;
            UINT32 item_off = (item_pixel / 3) * 2;
            UINT32 packed = after.item[i].packed[item_off] |
                           (after.item[i].packed[item_off + 1] << 8);
            require(((word >> ((p % 3) * 5)) & 31) ==
                    ((packed >> ((item_pixel % 3) * 5)) & 31));
        }
    }
    ++cases;
}
int main() {
    UINT32 rng = 0x915ce11u;
    for (UINT32 i = 0; i < rom_size; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        rom[i] = UINT8(rng);
    }
    const UINT32 actual = (rom_size / 2) * 3;
    for (unsigned mode = 0; mode < 2; ++mode) {
        file_cache = mode != 0;
        for (UINT32 limit = 0; limit <= PS3_SPU_COLOR_BATCH_SIZE; ++limit) {
            // Every phase around start, ROM end and each 64 KiB boundary.
            for (UINT32 edge = 0; edge <= 4; ++edge) {
                UINT32 center = edge * (PGM_PS3_COLOR_PAGE_SIZE / 2) * 3;
                for (int delta = -520; delta <= 24; ++delta) {
                    if ((int)center + delta < 0) continue;
                    check(center + delta, actual, actual - 1, limit);
                }
            }
            // Independent actual/mask truncation at every partial item.
            for (UINT32 end = 0; end < 520; ++end) {
                check(1, 1 + end, actual - 1, limit);
                check(2, actual, 2 + end, limit);
            }
        }
        for (UINT32 i = 0; i < 10000; ++i) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            check(rng % actual, actual, actual - 1, rng % 65);
        }
    }
    file_cache = true;
    nPGMSPRColMaskLen = actual - 1;
    const UINT32 limits[] = {15, 16, 32, 40, 48};
    for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
        ps3_spu_color_batch batch;
        reset_cache(); baseline_builder(&batch, 120, actual, limits[i]);
        UINT32 old_lookups = lookups;
        reset_cache(); current_builder(&batch, 120, actual, limits[i]);
        printf("batch=%u span_lookups=%u->%u zero_bytes=%u->%u\n",
               limits[i], old_lookups, lookups, (unsigned)sizeof(batch),
               PS3_SPU_COLOR_COMPACT_BYTES(limits[i]));
    }
    printf("PASS %llu differential cases (ASan/UBSan)\n", cases);
}
'''

with tempfile.TemporaryDirectory(prefix="pgm-batch-test-") as temp:
    cpp = pathlib.Path(temp) / "batch.cpp"
    exe = pathlib.Path(temp) / "batch-test"
    cpp.write_text(prelude + builder(baseline, "baseline_builder") +
                   builder(current, "current_builder") + checks)
    subprocess.run(["g++", "-std=c++98", "-O2", "-g", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I", str(root),
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
