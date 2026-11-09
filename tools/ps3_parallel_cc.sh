#!/usr/bin/env bash
# Parallel Cell-SDK compile helper: every translation unit gets its own
# container, all of them run at once (the host has 12 cores), then one link.
set -uo pipefail

ROOT="${ROOT:-/home/humor/src/fbneo-libretro-ps3}"
OUT="${OUT:-$ROOT/build-cellsdk-par}"
SDK="$ROOT/.cellsdk-4.75-full/4.75/cell"
IMAGE=cellsdk-4.75-wine:bookworm
WINE=/usr/lib/wine/wine
PPU="$WINE /usr/local/cell/host-win32/ppu/bin"
JOBS="${JOBS:-$(nproc)}"

CF="-O3 -mcpu=cell -mtune=cell -maltivec -mabi=altivec -fstrict-aliasing -fomit-frame-pointer -ffast-math -mminimal-toc -std=gnu++98"
BASEINC="-Istandalone/gen -Istandalone/runtime -Isrc/burner/win32 -Isrc/burner/libretro/libretro-common/include -Isrc/burner/libretro -Isrc/burn -Isrc/cpu -Isrc/cpu/sh4 -Isrc/burn/snd -Isrc/burn/devices -Isrc/intf -Isrc/intf/input -Isrc/intf/cd -Isrc/intf/audio -Isrc/burner -Isrc/dep/libs/zlib"
INC="${INC:-$BASEINC}"
BASEDEF="-D__PS3__ -D__LIBRETRO__ -DWORDS_BIGENDIAN -DFRONTEND_SUPPORTS_RGB565=1 -DFBNEO_DEFAULT_AUDIO_RATE=44100 -DPS3_MEMORY_DIAGNOSTIC=1 -DPS3_MEMORY_POOL_SIZE_MB=8 -DPS3_NATIVE_MEMORY=1 -DPS3_PGM_MASK_FILE_CACHE=1 -DPS3_PGM_COLOR_FILE_CACHE=1"
DEF="${DEF:-$BASEDEF}"

mkdir -p "$OUT"

compile_one() {
  local src="$1" obj="$2"
  docker run --rm -v "$SDK:/usr/local/cell:ro" -v "$ROOT:/work" -w /work "$IMAGE" \
    $PPU/ppu-lv2-g++.exe -c -o "$OUT/$obj" "$src" $CF $DEF $INC \
    > "$OUT/$obj.log" 2>&1
  local rc=$?
  echo "$obj rc=$rc $(grep -c 'error:' "$OUT/$obj.log" 2>/dev/null || echo 0) errors"
  return $rc
}
export -f compile_one 2>/dev/null || true

echo "=== compiling with $JOBS parallel jobs ==="
start=$(date +%s)
# one container per TU, all at once
declare -a pids=()
for spec in "$@"; do
  src="${spec%%:*}"; obj="${spec##*:}"
  ( compile_one "$src" "$obj" ) &
  pids+=($!)
  # throttle to JOBS
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
end=$(date +%s)
echo "=== compile wall time: $((end-start))s, failures: $fail ==="
exit $fail
