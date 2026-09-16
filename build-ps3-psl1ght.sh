#!/usr/bin/env bash

set -o pipefail

ROOT=/work
LOGDIR="$ROOT/build-logs"
STATEDIR="$ROOT/.build-state"

mkdir -p "$LOGDIR"
mkdir -p "$STATEDIR"

FBNEO_LOG="$LOGDIR/fbneo-build.log"
FBNEO_ERRORS="$LOGDIR/fbneo-errors.log"

FBNEO_DIR="$ROOT/src/burner/libretro"
FBNEO_STATE="$STATEDIR/fbneo-psl1ght.signature"

PS3_MEMORY_DIAGNOSTIC="${PS3_MEMORY_DIAGNOSTIC:-0}"
PS3_MEMORY_POOL_SIZE_MB="${PS3_MEMORY_POOL_SIZE_MB:-64}"
PS3_NATIVE_MEMORY="${PS3_NATIVE_MEMORY:-0}"
PS3_PGM_MASK_FILE_CACHE="${PS3_PGM_MASK_FILE_CACHE:-0}"
PS3_PGM_COLOR_FILE_CACHE="${PS3_PGM_COLOR_FILE_CACHE:-0}"

cd "$FBNEO_DIR"

echo "========================================"
echo "FBNeo PSL1GHT build"
echo "========================================"

#
# 生成当前构建配置指纹
#
CURRENT_SIGNATURE="$(
    {
        echo "platform=psl1ght"
        echo "COMMONLV="
        echo "PS3_MEMORY_DIAGNOSTIC=$PS3_MEMORY_DIAGNOSTIC"
        echo "PS3_MEMORY_POOL_SIZE_MB=$PS3_MEMORY_POOL_SIZE_MB"
        echo "PS3_NATIVE_MEMORY=$PS3_NATIVE_MEMORY"
        echo "PS3_PGM_MASK_FILE_CACHE=$PS3_PGM_MASK_FILE_CACHE"
        echo "PS3_PGM_COLOR_FILE_CACHE=$PS3_PGM_COLOR_FILE_CACHE"

        echo "CC=$(command -v ppu-gcc)"
        ppu-gcc --version | head -1

        sha256sum Makefile 2>/dev/null || true
    } | sha256sum | awk '{print $1}'
)"

OLD_SIGNATURE=""

if [ -f "$FBNEO_STATE" ]; then
    OLD_SIGNATURE="$(cat "$FBNEO_STATE")"
fi

NEED_CLEAN=0

if [ "${CLEAN:-0}" = "1" ]; then
    echo "Forced clean requested."
    NEED_CLEAN=1

elif [ ! -f "$FBNEO_STATE" ]; then
    echo "No previous build signature."
    NEED_CLEAN=1

elif [ "$CURRENT_SIGNATURE" != "$OLD_SIGNATURE" ]; then
    echo "Build configuration changed."
    NEED_CLEAN=1

else
    echo "Build configuration unchanged."
    echo "Using incremental build."
fi

if [ "$NEED_CLEAN" = "1" ]; then
    echo
    echo "Cleaning FBNeo..."

    make \
        platform=psl1ght \
        COMMONLV= \
        PS3_MEMORY_DIAGNOSTIC="$PS3_MEMORY_DIAGNOSTIC" \
        PS3_MEMORY_POOL_SIZE_MB="$PS3_MEMORY_POOL_SIZE_MB" \
        PS3_NATIVE_MEMORY="$PS3_NATIVE_MEMORY" \
        PS3_PGM_MASK_FILE_CACHE="$PS3_PGM_MASK_FILE_CACHE" \
        PS3_PGM_COLOR_FILE_CACHE="$PS3_PGM_COLOR_FILE_CACHE" \
        clean

    echo "$CURRENT_SIGNATURE" > "$FBNEO_STATE"
fi

echo
echo "========================================"
echo "Compile FBNeo"
echo "========================================"

make \
    platform=psl1ght \
    COMMONLV= \
    PS3_MEMORY_DIAGNOSTIC="$PS3_MEMORY_DIAGNOSTIC" \
    PS3_MEMORY_POOL_SIZE_MB="$PS3_MEMORY_POOL_SIZE_MB" \
    PS3_NATIVE_MEMORY="$PS3_NATIVE_MEMORY" \
    PS3_PGM_MASK_FILE_CACHE="$PS3_PGM_MASK_FILE_CACHE" \
    PS3_PGM_COLOR_FILE_CACHE="$PS3_PGM_COLOR_FILE_CACHE" \
    --output-sync=target \
    -j"$(nproc)" \
    2>&1 | tee "$FBNEO_LOG"

BUILD_RESULT=${PIPESTATUS[0]}

if [ "$BUILD_RESULT" -ne 0 ]; then

    echo
    echo "========================================"
    echo "FBNeo BUILD FAILED"
    echo "========================================"

    grep \
        -n \
        -E \
        -C 5 \
        'fatal error:|error:|undefined reference|collect2: error|ld: |make(\[[0-9]+\])?: \*\*\*' \
        "$FBNEO_LOG" \
        > "$FBNEO_ERRORS" || true

    cat "$FBNEO_ERRORS"

    exit "$BUILD_RESULT"
fi

echo
echo "========================================"
echo "FBNeo core SUCCESS"
echo "========================================"

test -s fbneo_libretro_psl1ght.a
ls -lh fbneo_libretro_psl1ght.a
