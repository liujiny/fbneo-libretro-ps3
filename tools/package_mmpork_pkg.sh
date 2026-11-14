#!/usr/bin/env bash
set -eo pipefail

ROOT="/home/humor/src/fbneo-libretro-ps3"
cd "$ROOT"

echo "=== Packaging Muchi Muchi Pork! Standalone PKG ==="
mkdir -p packages/mmpork/USRDIR/cores
cp build-cellsdk-mmpork/mmpork.self packages/mmpork/USRDIR/cores/fbneo_libretro_psl1ght.self
cp build-cellsdk-mmpork/mmpork.self packages/mmpork/USRDIR/EBOOT.BIN
cp ps3_dualcore_pkg_20260928/SSNE10001/ICON0.PNG packages/mmpork/

./psl1ght-current-src/tools/sfo_pkg/sfo --fromxml packages/mmpork/sfo.xml packages/mmpork/PARAM.SFO
./psl1ght-current-src/tools/sfo_pkg/pkg --contentid=UP0001-CAVE00002_00-0000000000000001 packages/mmpork packages/MuchiMuchiPork_PS3.pkg

echo "=== Package complete ==="
ls -lh packages/MuchiMuchiPork_PS3.pkg
