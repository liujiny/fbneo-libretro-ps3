#!/usr/bin/env bash
set -eo pipefail

ROOT="/home/humor/src/fbneo-libretro-ps3"
cd "$ROOT"

OUT="build-cellsdk-mmpork"
SDK="$ROOT/.cellsdk-4.75-full/4.75/cell"
IMAGE="cellsdk-4.75-wine:bookworm"
WINE="/usr/lib/wine/wine"
PPU="$WINE /usr/local/cell/host-win32/ppu/bin"

echo "=== Cleaning and setting up $OUT ==="
rm -rf "$OUT"
mkdir -p "$OUT"
export OUT

export INC="-I/usr/local/cell/target/ppu/include -I/usr/local/cell/target/ppu/include/PSGL -Istandalone/gen_mmpork -Istandalone/runtime -Isrc/burner/win32 -Isrc/burner/libretro/libretro-common/include -Isrc/burner/libretro -Isrc/burn -Isrc/cpu -Isrc/cpu/sh4 -Isrc/burn/snd -Isrc/burn/devices -Isrc/intf -Isrc/intf/input -Isrc/intf/cd -Isrc/intf/audio -Isrc/burner -Isrc/dep/libs/zlib"

export DEF="-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN -DSH3_STATIC_REC=1 -DSH3_PPC_DRC_TEST=1 -DCV1K_GAME_MMPORK=1 \
-DSH3_PPC_CACHE_BYTES=(16*1024*1024) -DSALVIA_CV1K_STAMP_PROBE=1 \
-DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100 \
-DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1 \
-DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1"

echo "=== Compiling modified FBNeo translation units and mmpork static blocks in parallel ==="
./tools/ps3_parallel_cc.sh \
  src/cpu/sh4/sh4.cpp:sh4.o \
  src/burn/devices/epic12.cpp:epic12.o \
  src/burn/drv/cave/d_cv1k.cpp:d_cv1k.o \
  src/burn/snd/ymz770.cpp:ymz770.o \
  src/burn/ps3_memory_pool.c:ps3_memory_pool.o \
  src/burner/libretro/ps3_benchmark.cpp:ps3_benchmark.o \
  src/burner/libretro/libretro.cpp:libretro.o \
  standalone/runtime/cv1k_runtime.c:cv1k_runtime.o \
  standalone/runtime/sh3_interpret.c:sh3_interpret.o \
  standalone/gen_mmpork/sh3_dispatch_table.c:sh3_dispatch_table.o \
  standalone/gen_mmpork/sh3_code_0.c:sh3_code_0.o \
  standalone/gen_mmpork/sh3_code_1.c:sh3_code_1.o \
  standalone/gen_mmpork/sh3_code_2.c:sh3_code_2.o \
  standalone/gen_mmpork/sh3_code_3.c:sh3_code_3.o \
  standalone/gen_mmpork/sh3_code_4.c:sh3_code_4.o \
  standalone/gen_mmpork/sh3_code_5.c:sh3_code_5.o \
  standalone/gen_mmpork/sh3_code_6.c:sh3_code_6.o \
  standalone/gen_mmpork/sh3_code_7.c:sh3_code_7.o \
  standalone/gen_mmpork/sh3_code_8.c:sh3_code_8.o \
  standalone/gen_mmpork/sh3_code_9.c:sh3_code_9.o \
  standalone/gen_mmpork/sh3_code_10.c:sh3_code_10.o \
  standalone/gen_mmpork/sh3_code_11.c:sh3_code_11.o \
  standalone/gen_mmpork/sh3_code_12.c:sh3_code_12.o \
  standalone/gen_mmpork/sh3_code_13.c:sh3_code_13.o \
  standalone/gen_mmpork/sh3_code_14.c:sh3_code_14.o \
  standalone/gen_mmpork/sh3_code_15.c:sh3_code_15.o

echo "=== Copying base archive fbneo_libretro_cellsdk475.a ==="
cp build-cellsdk-release/a.a "$OUT/a.a"

echo "=== Updating archive a.a with mmpork objects ==="
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$ROOT/$OUT:/b" -w /b "$IMAGE" \
  sh -c "$PPU/ppu-lv2-ar.exe r a.a epic12.o sh4.o d_cv1k.o ymz770.o ps3_benchmark.o libretro.o ps3_memory_pool.o \
  cv1k_runtime.o sh3_interpret.o sh3_dispatch_table.o \
  sh3_code_0.o sh3_code_1.o sh3_code_2.o sh3_code_3.o sh3_code_4.o sh3_code_5.o \
  sh3_code_6.o sh3_code_7.o sh3_code_8.o sh3_code_9.o sh3_code_10.o sh3_code_11.o \
  sh3_code_12.o sh3_code_13.o sh3_code_14.o sh3_code_15.o"

echo "=== Linking rel.elf for mmpork ==="
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$ROOT:/work" \
  -v "$HOME/src/retroarch-psx-ce-upgrade/RetroArch_Orig/objs/snc:/frontend_objs:ro" \
  -w /work "$IMAGE" \
  $PPU/ppu-lv2-g++.exe -oformat=elf /frontend_objs/griffin/griffin.ppu.o \
  /frontend_objs/gfx/drivers_shader/ps3_stock_shader.ppu.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.fpo.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.vpo.o \
  -Wl,--wrap=cellGameBootCheck -Wl,--wrap=cellGameContentPermit -Wl,--wrap=main \
  -L/usr/local/cell/target/ppu/lib/PSGL/RSX/ultra-opt -lPSGL -lPSGLcgc \
  "$OUT/a.a" -lcgc -lgcm_cmdasm -lgcm_sys_stub -lresc_stub -lm -lio_stub -lfs_stub \
  -lsysutil_stub -lsysutil_game_stub -lsysutil_screenshot_stub -lsysutil_np_stub \
  -lpngdec_stub -lsysmodule_stub -laudio_stub -lnet_stub -lnetctl_stub -lpthread \
  -o "$OUT/rel.elf"

echo "=== Signing rel.self ==="
docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$ROOT/$OUT:/b" -w /b "$IMAGE" \
  $WINE /usr/local/cell/host-win32/bin/make_fself_npdrm.exe rel.elf rel.self

cp "$OUT/rel.self" "$OUT/mmpork.self"

echo "=== Build Complete! ==="
ls -lh "$OUT/rel.elf" "$OUT/rel.self" "$OUT/mmpork.self"
