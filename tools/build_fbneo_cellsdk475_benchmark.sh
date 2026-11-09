#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build-cellsdk"
SDK_HOST="$REPO_DIR/.cellsdk-4.75-full/4.75/cell"
BASELINE_ARCHIVE="/home/humor/src/retroarch-ps3-cell/libretro_ps3.a"
FRONTEND_OBJS_DIR="/home/humor/src/retroarch-psx-ce-upgrade/RetroArch_Orig/objs/snc"

mkdir -p "$BUILD_DIR"

echo "=== Building FBNeo with Official Cell SDK 4.75 ==="
echo "Repo: $REPO_DIR"
echo "Build Dir: $BUILD_DIR"

# Common compiler flags for Cell PPU
COMMON_FLAGS="-O3 -mcpu=cell -mtune=cell -maltivec -mabi=altivec -fstrict-aliasing -fomit-frame-pointer -ffast-math -mminimal-toc -std=gnu++98"
DEFINES="-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN -DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100 -DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1 -DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1"
INCLUDES="-Isrc/burner/win32 -Isrc/burner/libretro/libretro-common/include -Isrc/burner/libretro -Isrc/burn -Isrc/cpu -Isrc/cpu/sh4 -Isrc/burn/snd -Isrc/burn/devices -Isrc/intf -Isrc/intf/input -Isrc/intf/cd -Isrc/intf/audio -Isrc/burner -Isrc/dep/libs/zlib"

echo "1. Compiling ps3_memory_pool.c..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-gcc.exe \
  -c -o build-cellsdk/ps3_memory_pool.o src/burn/ps3_memory_pool.c \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "2. Compiling ps3_benchmark.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/ps3_benchmark.o src/burner/libretro/ps3_benchmark.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3. Compiling libretro.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/libretro.o src/burner/libretro/libretro.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3b. Compiling d_cv1k.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/d_cv1k.o src/burn/drv/cave/d_cv1k.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3c. Compiling serflash.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/serflash.o src/burn/devices/serflash.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3d. Compiling epic12.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/epic12.o src/burn/devices/epic12.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3e. Compiling sh4.cpp..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -c -o build-cellsdk/sh4.o src/cpu/sh4/sh4.cpp \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "3f. Compiling ps3_boot_hook.c..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-gcc.exe \
  -c -o build-cellsdk/ps3_boot_hook.o src/burn/ps3_boot_hook.c \
  $COMMON_FLAGS $DEFINES $INCLUDES

echo "4. Updating archive with benchmark objects..."
rm -f "$BUILD_DIR/fbneo_libretro_cellsdk475.a"
cp "$BASELINE_ARCHIVE" "$BUILD_DIR/fbneo_libretro_cellsdk475.a"

docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$BUILD_DIR:/build" \
  -w /build \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-ar.exe \
  r fbneo_libretro_cellsdk475.a ps3_memory_pool.o ps3_benchmark.o libretro.o d_cv1k.o serflash.o epic12.o sh4.o ps3_boot_hook.o

echo "5. Linking ELF with RetroArch frontend..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -v "$FRONTEND_OBJS_DIR:/frontend_objs:ro" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/ppu/bin/ppu-lv2-g++.exe \
  -oformat=elf \
  build-cellsdk/griffin_rgui.ppu.o \
  /frontend_objs/gfx/drivers_shader/ps3_stock_shader.ppu.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.fpo.o \
  /frontend_objs/gfx/drivers/gl_shaders/stock_ps3.vpo.o \
  -Wl,--wrap=cellGameBootCheck -Wl,--wrap=cellGameContentPermit \
  -L/usr/local/cell/target/ppu/lib/PSGL/RSX/ultra-opt -lPSGL -lPSGLcgc \
  build-cellsdk/fbneo_libretro_cellsdk475.a \
  -lcgc -lgcm_cmdasm -lgcm_sys_stub -lresc_stub -lm -lio_stub -lfs_stub \
  -lsysutil_stub -lsysutil_game_stub -lsysutil_screenshot_stub -lsysutil_np_stub \
  -lpngdec_stub -lsysmodule_stub -laudio_stub -lnet_stub -lnetctl_stub -lpthread \
  -o build-cellsdk/RetroArch_FBNeo_CellSDK475_benchmark.elf

echo "6. Creating signed SELF via make_fself_npdrm..."
docker run --rm \
  -v "$SDK_HOST:/usr/local/cell:ro" \
  -v "$REPO_DIR:/work" \
  -w /work \
  cellsdk-4.75-wine:bookworm \
  /usr/lib/wine/wine /usr/local/cell/host-win32/bin/make_fself_npdrm.exe \
  build-cellsdk/RetroArch_FBNeo_CellSDK475_benchmark.elf \
  build-cellsdk/RetroArch_FBNeo_CellSDK475_benchmark.self

# Also create deploy copy for PS3 SSNE10001 Title ID
cp build-cellsdk/RetroArch_FBNeo_CellSDK475_benchmark.self build-cellsdk/fbneo_libretro_psl1ght.self
chmod 777 -R build-cellsdk 2>/dev/null || true

echo "=== Build Complete! ==="
ls -lh build-cellsdk/RetroArch_FBNeo_CellSDK475_benchmark.self
ls -lh build-cellsdk/fbneo_libretro_psl1ght.self
