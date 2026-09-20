#!/usr/bin/env python3
"""Exercise the production color8 routing/profiler with a mocked SPU in Docker."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
if not pathlib.Path("/.dockerenv").exists():
    raise SystemExit("Run inside ps3dev-gcc13:rsxfix.")
source = (root / "src/burn/drv/pgm/pgm_draw.cpp").read_text()


def function(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


prelude = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint64_t UINT64;
#define bprintf(...) ((void)0)
static UINT32 nPGMSPRColMaskLen = 127;
static UINT32 nPGMSPRColROMLen = 64;
static INT32 nPGMSPRColPacked = 1;
static UINT8 rom[64];
static UINT64 stat_shadow_compare, stat_shadow_skipped;
static UINT64 stat_shadow_bad, stat_shadow_pixel_bad;
static INT32 pgm_perf_in_color_block;
static bool spu_hit, break_span;
static UINT32 reference_calls, recorded_samples, calls;
static struct { struct { unsigned long long io_us; } color; } g_ps3_perf_stats;
enum { PS3_PERF_COLOR_EXPAND, PS3_PERF_COLOR_SPU, PS3_PERF_COLOR_PPU };
static UINT32 recorded_total, recorded_spu, recorded_ppu;
static void require(bool ok) { if (!ok) abort(); }
static unsigned long long ps3_perf_now_us() {
    static unsigned long long clock;
    return clock += 100;
}
static void ps3_perf_record_time(int metric, unsigned long long elapsed) {
    require(elapsed == 6400);
    if (metric == PS3_PERF_COLOR_EXPAND) { ++recorded_samples; ++recorded_total; }
    else if (metric == PS3_PERF_COLOR_SPU) ++recorded_spu;
    else if (metric == PS3_PERF_COLOR_PPU) ++recorded_ppu;
    else require(false);
}
static UINT8 pgm_sprite_color(UINT32 p) {
    p &= nPGMSPRColMaskLen;
    if (p >= 96) return 0;
    UINT32 pos = (p / 3) * 2;
    return ((rom[pos] | (rom[pos + 1] << 8)) >> ((p % 3) * 5)) & 31;
}
static const UINT8 *pgm_ps3_color_cache_span(UINT32 off, UINT32 bytes) {
    require(off + bytes <= sizeof(rom));
    return break_span ? NULL : rom + off;
}
static UINT16 pgm_ps3_color_read_pair(UINT32 pair) {
    require(pair * 2 + 1 < sizeof(rom));
    return rom[pair * 2] | (rom[pair * 2 + 1] << 8);
}
static void pgm_sprite_colors8_ppu_only(UINT32 p, UINT32, UINT8 *out) {
    ++reference_calls;
    for (UINT32 i = 0; i < 8; ++i) out[i] = pgm_sprite_color(p + i);
}
static INT32 pgm_ps3_spu_batch_color8(UINT32 p, UINT32, UINT8 *out) {
    if (!spu_hit) return 0;
    for (UINT32 i = 0; i < 8; ++i) out[i] = pgm_sprite_color(p + i);
    out[2] ^= 1; // Deliberate mismatch must be recorded on sampled hits only.
    return 1;
}
'''

checks = r'''
static void check(UINT32 pixel) {
    UINT8 out[8];
    memset(out, 0xff, sizeof(out));
    pgm_sprite_colors8(pixel, out);
    ++calls;
    require(pgm_perf_in_color_block == 0);
    for (UINT32 i = 0; i < 8; ++i) {
        UINT8 expected = pgm_sprite_color(pixel + i);
        if (PS3_PGM_SPU_WORKER && spu_hit && i == 2) expected ^= 1;
        require(out[i] == expected);
    }
    require(recorded_samples == (PS3_PGM_PERF_PROFILE ? calls / 64 : 0));
	if (PS3_PGM_PERF_PROFILE) require(recorded_total == recorded_spu + recorded_ppu);
}
int main() {
    for (unsigned i = 0; i < sizeof(rom); ++i) rom[i] = UINT8(i * 71 + 13);
#if PS3_PGM_SPU_WORKER
    spu_hit = true;
    for (UINT32 i = 0; i < 1028; ++i) check(i % 3);
    UINT32 samples = (1028 + PS3_PGM_SPU_SHADOW_INTERVAL - 1) /
                     PS3_PGM_SPU_SHADOW_INTERVAL;
    require(reference_calls == samples && stat_shadow_compare == samples);
    require(stat_shadow_skipped + stat_shadow_compare == 1028);
    require(stat_shadow_bad == samples && stat_shadow_pixel_bad == samples);
#endif
    spu_hit = false;
    for (UINT32 i = 0; i < 64; ++i) check(i % 3); // Contiguous PPU fallback.
    break_span = true;
    for (UINT32 i = 0; i < 64; ++i) check(i % 3); // Pair-read fallback.
    nPGMSPRColPacked = 0;
    for (UINT32 i = 0; i < 64; ++i) check(i % 3); // Scalar fallback.
    nPGMSPRColPacked = 1;
    for (UINT32 i = 0; i < 64; ++i) check(92 + i % 3); // ROM-end fallback.
    printf("PASS worker=%d profile=%d interval=%d calls=%u cmp=%llu skipped=%llu samples=%u\n",
           PS3_PGM_SPU_WORKER, PS3_PGM_PERF_PROFILE, PS3_PGM_SPU_SHADOW_INTERVAL,
           calls, (unsigned long long)stat_shadow_compare,
           (unsigned long long)stat_shadow_skipped, recorded_samples);
}
'''

with tempfile.TemporaryDirectory(prefix="pgm-shadow-test-") as temp:
    cpp = pathlib.Path(temp) / "shadow.cpp"
    exe = pathlib.Path(temp) / "shadow-test"
    cpp.write_text(prelude + function("static inline INT32 pgm_ps3_spu_shadow_sample()") +
                   function("static inline void pgm_sprite_colors8_unpack_span(") +
                   function("static inline void pgm_sprite_colors8(UINT32 pixel, UINT8 *out)") +
                   checks)
    for worker, profile, interval in ((1, 1, 1), (1, 1, 257),
                                      (1, 0, 257), (0, 1, 1), (0, 0, 1)):
        subprocess.run(["g++", "-std=gnu++98", "-O2", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-D__PS3__", f"-DPS3_PGM_SPU_WORKER={worker}",
                        f"-DPS3_PGM_PERF_PROFILE={profile}",
                        f"-DPS3_PGM_SPU_SHADOW_INTERVAL={interval}",
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
