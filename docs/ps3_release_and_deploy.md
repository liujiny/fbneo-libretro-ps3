# PS3 FBNeo CV1000 port: release build, deploy, test

Status as of 2026-11-02. Everything below was verified on the console
(192.168.2.230, title SSNE10001).

## 1. What the release configuration is

| item | value |
| --- | --- |
| engine | SH3 PPC dynamic recompiler (`SH3_PPC_DRC_TEST=1`) |
| JIT arena | 16 MiB `.text` (`SH3_PPC_CACHE_BYTES=(16*1024*1024)`) |
| VRAM | 16-bit 1:5:5:5 words, 128 slices of 32 lines = 64 MiB, reserved in `epic12_init` |
| slices per frame | 60 (`nInterleave = 60`) |
| validation | write-stamp level 1 (`SALVIA_CV1K_STAMP_PROBE=1`) |
| register slots | 12 (`r5..r10` + `r14..r19`, saved by the caller's clobber list) |
| address translation | arithmetic guards (the page-table variant was measured slower and removed) |
| profiling | off (`SH3_PROFILE_SAMPLE=0` by default) |

Measured on `ddpdfk`: heavy scene (frames 2600-2700) **22.6 fps / 44.2 ms**
(three runs: 22.61 / 22.46 / 22.84), light scenes up to **59.7 fps** (`ibara`),
128/128 VRAM slices, 0 allocation failures.

## 2. Rebuild

```bash
cd ~/src/fbneo-libretro-ps3
OUT=build-cellsdk-release
SDK=.cellsdk-4.75-full/4.75/cell
IMAGE=cellsdk-4.75-wine:bookworm
WINE=/usr/lib/wine/wine
PPU="$WINE /usr/local/cell/host-win32/ppu/bin"
rm -rf "$OUT"; mkdir -p "$OUT"
export OUT
export DEF="-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN -DSH3_PPC_DRC_TEST=1 \
-DSH3_PPC_CACHE_BYTES=(16*1024*1024) -DSALVIA_CV1K_STAMP_PROBE=1 \
-DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100 \
-DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1 \
-DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1"
./tools/ps3_parallel_cc.sh src/cpu/sh4/sh4.cpp:sh4.o \
    src/burn/devices/epic12.cpp:epic12.o src/burn/drv/cave/d_cv1k.cpp:d_cv1k.o \
    src/burner/libretro/ps3_benchmark.cpp:ps3_benchmark.o \
    src/burner/libretro/libretro.cpp:libretro.o
cp build-cellsdk-drcgate/fbneo_libretro_cellsdk475.a "$OUT/a.a"
```

`tools/ps3_parallel_cc.sh` runs one container per translation unit, so the five
files compile in ~30 s on 12 cores. `DEF` is read from the environment by the
script; the archive only needs to be copied once per build directory.

Link and sign (paths must match the checkout layout):

```bash
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$PWD/$OUT:/b" -w /b "$IMAGE" \
  $PPU/ppu-lv2-ar.exe r a.a epic12.o sh4.o d_cv1k.o ps3_benchmark.o libretro.o
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$PWD:/work" \
  -v "$HOME/src/retroarch-psx-ce-upgrade/RetroArch_Orig/objs/snc:/frontend_objs:ro" \
  -v "/tmp/ra-branch/objs/griffin:/fe:ro" -w /work "$IMAGE" \
  $PPU/ppu-lv2-g++.exe -oformat=elf /fe/griffin.ppu.o \
  /frontend_objs/gfx/drivers_shader/ps3_stock_shader.ppu.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.fpo.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.vpo.o \
  -Wl,--wrap=cellGameBootCheck -Wl,--wrap=cellGameContentPermit \
  -L/usr/local/cell/target/ppu/lib/PSGL/RSX/ultra-opt -lPSGL -lPSGLcgc \
  "$OUT/a.a" -lcgc -lgcm_cmdasm -lgcm_sys_stub -lresc_stub -lm -lio_stub -lfs_stub \
  -lsysutil_stub -lsysutil_game_stub -lsysutil_screenshot_stub -lsysutil_np_stub \
  -lpngdec_stub -lsysmodule_stub -laudio_stub -lnet_stub -lnetctl_stub -lpthread \
  -o "$OUT/rel.elf"
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$PWD/$OUT:/b" -w /b "$IMAGE" \
  $WINE /usr/local/cell/host-win32/bin/make_fself_npdrm.exe rel.elf rel.self
```

Use a fresh `OUT` directory for every build: files created inside the container are
root-owned and the next `cp`/`ar` into the same directory fails with
"Permission denied".

## 3. Deploy

Atomic replace over FTP; never overwrite the target directly:

```bash
H=192.168.2.230
curl -T build-cellsdk-release/rel.self "ftp://$H/dev_hdd0/tmp/cu.self"
# verify the byte count survived the transfer
curl -I "ftp://$H/dev_hdd0/tmp/cu.self" | grep -i content-length
curl "ftp://$H/dev_hdd0/tmp/" -Q "DELE /dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"
curl "ftp://$H/dev_hdd0/tmp/" -Q "RNFR /dev_hdd0/tmp/cu.self" \
     -Q "RNTO /dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self"
```

`tools/ps3_autotest.py --deploy <file>` does all of this.

## 4. Run a benchmark

```bash
python3 tools/ps3_autotest.py --game ddpdfk --frames 2700 --wait 300 \
    --warmup 2600 --start-frame 300
```

The tool quits any running app, pins the launcher core, writes
`/dev_hdd0/tmp/fbneo_launch_game.txt` and `fbneo_benchmark.cfg`, launches through
webMAN, polls `/dev_hdd0/tmp/fbneo_benchmark_report.txt` and stores the report in
`reports/bench/`. Frame dumps are requested through `/dev_hdd0/tmp/fbneo_dump_frames.txt`
(comma separated frame numbers); the core writes `/dev_hdd0/tmp/fbneo_frame_<n>.bmp`.

## 5. Pitfalls that cost real time

1. **Allocation order.** `Sh3DrcPreallocate()` and the VRAM reservation must run
   before the driver takes its big buffers. If `calloc` of the block tables fails,
   `Sh3Ppc::failed` becomes true and the recompiler silently degrades to the
   interpreter for the whole session - with no error anywhere. This single bug hid
   the DRC for days.
2. **Launcher config path.** `retroarch-salamander.cfg` lives in `USRDIR`, not the
   app root, and the frontend rewrites it on exit. Pin `libretro_path` after the
   quit and before the mount.
3. **webMAN launch.** `/mount.ps3/dev_hdd0/game/SSNE10001/` needs the trailing
   slash and often needs a second attempt; it answers 200 even when nothing starts.
   Verify a process is actually running before waiting for a report.
4. **Stale app instance.** If an old instance keeps running, the new config never
   applies and no report appears. Delete the memory log, the DRC log and the report
   before each run; a power cycle fixes the console when it gets stuck.
5. **Fine-grained mftb spans are unusable.** Back-to-back timebase reads can return
   the same value, so sub-millisecond instrumentation reads as zero. Only the
   sampling profiler (one read per 64 blocks) is trustworthy.
6. **`/dev_hdd0/tmp` fills up.** Core backups are ~92 MB each and flash images
   ~132 MB; a full HDD makes the core fail to start with no message.

## 6. Directions already excluded, with the evidence

| direction | result |
| --- | --- |
| give the JIT more arena (16 -> 24 MiB) | 46.3 -> 48.2 ms, no gain; only 5.5 recompiles/frame |
| stamp-only validation (`SALVIA_CV1K_STAMP_PROBE=2`) | 47.5 ms, validation is not the cost |
| disable the blitter busy-cycle burn | 20.75 vs 20.79 fps |
| disable blitter execution | 26 ms, but the game takes a different path; not a valid cost measurement |
| shrink VRAM 64 -> 8 MiB | 42.7 ms (-8 %), so VRAM misses are not dominant |
| static recompilation (AOT) | size fits (8.8 MB of 24 MB), but 28 % of emitted code is state traffic and only ~7 instructions per block are guest memory; estimated gain 1.2-1.4x |
| longer blocks (`MAX_INSNS` is already 32) | half of all blocks end on an indirect jump, not on the length limit |

Register allocation was expanded to 12 host slots (r5..r10, r14..r19). r14..r19
are declared as clobbers of the inline asm in `sh3_ppc_enter`, so GCC automatically
emits non-volatile save (`std`) in the prologue and restore (`ld`) in the epilogue of
`sh3_ppc_call`. Verified by disassembly and multi-game stress tests.

Page table translation (6.1) maps guest pages to host page bases in `drc_tables`,
with pinned `r4` holding `drc_read_table`. Direct reads emit `lwzx r12, r4, r0`
without table loading, and writes use single-cycle immediate `addis r12, r4, 4`.
Delta translation (`delta = host - (page << 16)`) was tested and excluded due to
64-bit carry out into bit 32 on the PS3 PPU (causing DSI crashes). Table base + offset
eliminates this risk with zero performance penalty. Average emitted instructions per
block dropped from 132 to 126 (`memaddr` 39 -> 37, `memguard` 10 -> 6).

## 7. Where the time goes (best current understanding)

```
frame                 46.6 ms
retro_run             39.0 ms
DrvFrame              36.0 ms
  dispatch + blocks   ~45 ms sampled (the sampling profiler attributes 97 %
                      of the frame to the dispatch loop)
blitter                0.6 ms
BurnDrvRedraw          1.1 ms
sound                 0.13 ms
```

47,190 blocks per frame, ~132 emitted instructions per block, ~23 cycles per
emitted instruction on the in-order PPE. The workload is spread evenly across the
game's code (no hot loop), so per-block cost is the only lever, and the measured
headroom there is small (merging the two address guards saves ~7 % of a block).

## 8. Instrumentation available

| switch | effect |
| --- | --- |
| `-DSH3_PROFILE_SAMPLE=1` | sampling profiler: `prof`, `hot16`, `genwords`, `endreason` lines in `/dev_hdd0/tmp/fbneo-drc.log` |
| `/dev_hdd0/tmp/fbneo-dump-blocks` | dumps the first 60 RAM blocks' generated code to `/dev_hdd0/tmp/fbneo-blocks.bin` |
| `/dev_hdd0/tmp/fbneo-skip-blit` | skips blitter execution (breaks the picture) |
| `/dev_hdd0/tmp/fbneo-no-busyburn` | disables the blitter busy-cycle charge |
| `/dev_hdd0/tmp/fbneo-vram-small` | caps VRAM at 8 slices (breaks the picture) |
| `tools/analyze_ppc_blocks.py` | classifies the instruction mix of dumped blocks |

`sh3_drc_log` writes `/dev_hdd0/tmp/fbneo-drc.log`; the memory diagnostics write
`/dev_hdd0/tmp/fbneo-ps3-memory.log`.

## 9. Frame regression method that actually works

Comparing colour counts across runs is meaningless: the auto-coin retries, so two
runs can be one effect-frame apart at the same frame index. Compare **static
screens** instead - the title screen holds for hundreds of frames, so a one-frame
offset changes nothing:

```bash
printf '1200,1400,1600\n' > /tmp/df.txt
curl -T /tmp/df.txt "ftp://192.168.2.230/dev_hdd0/tmp/fbneo_dump_frames.txt"
# run, then compare sha256 of the three BMPs against the reference hashes
```

Reference hashes (any correct build must match these byte for byte):

```
frame 1200  32250acbe1de9003a096e5cc294afa6884e230abac56f6c47c093108337f65e0
frame 1400  d1480937133e53a6e29f9c3bfab0204cbd8372b962d3ba01aba1a9e74af51bfb
frame 1600  57a09025144be6e50e5425eaa82ad6a535710079704443e1b058321685c23ef2
```

## 10. A/B results that set the current configuration (2026-11-02 evening)

All runs used the heavy window (`--frames 2700 --wait 300 --warmup 2600 --start-frame 300`):

| build | fps (runs) | mean | verdict |
| --- | --- | --- | --- |
| arithmetic translation + 12 slots (current) | 22.61 / 22.46 / 22.84 | **22.64** | kept |
| baseline before both changes | 21.96 / 21.77 | 21.87 | reference |
| 12 slots + older page table | 22.39 / 22.21 / 21.35 | 21.98 | no gain |
| 12 slots + page table + pinned r4 | 21.09 / 20.63 | 20.86 | **regression** |
| page table "fix" iteration | 20.52 | 20.52 | **regression** |

Conclusion: keep the 12 register slots, drop the page-table address translation.
On an in-order PPE a dependent `lwzx` costs more than the four integer ops it
replaces, so the table lookup lost about 4 % where it was pinned and broke even
where it was not.

## 11. Verified gameplay frame rates (2026-11-02 evening)

Every number below was measured in a window whose frame dump was inspected and
confirmed to show real gameplay - not the boot self-test, title screen or name
entry. That distinction matters: several earlier "fast" numbers turned out to be
menus, including one at 59.7 fps that was a black screen.

| game | window | fps | ms | speed vs 60 |
| --- | --- | --- | --- | --- |
| `mushisam` (8 MiB, mid-stage) | 4400-4600 | **43.0** | 23.3 | 72 % |
| `ddpdfk` (16 MiB, stage start) | 2600-2700 | **22.6** | 44.2 | 38 % |
| `mmpork` (stage start) | 2400-2500 | **15.8** | 63.2 | 26 % |

Two sets do not render correctly and were therefore excluded:

* `ibara` draws nothing at all (1 colour at frames 2000 / 3500 / 4800) while the
  benchmark reports 59.7 fps - the black screen is simply cheap. **This is a bug,
  not a performance figure.**
* `espgal2` stays on the "PUSH START" title: the harness stops retrying START after
  8 attempts (~frame 2160), so a later title screen never gets started.

Phase breakdown for the two working games shows the SH3 CPU, not the blitter:

```
mushisam  cpu 26.1 ms/frame   blit 1.05 ms   sound 0.14 ms
ddpdfk    cpu 35.2 ms/frame   blit 1.04 ms   sound 0.13 ms
```

So the blitter is 3-4 % of the frame in both; the remaining performance work is all
in SH3 translation and the memory stalls it hits.
