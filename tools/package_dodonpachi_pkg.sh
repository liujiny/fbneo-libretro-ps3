#!/usr/bin/env bash
set -eo pipefail

ROOT="/home/humor/src/fbneo-libretro-ps3"
cd "$ROOT"

echo "=== Packaging DoDonPachi Dai-Fukkatsu Standalone PKG ==="
mkdir -p packages/ddpdfk/USRDIR/cores
cp build-cellsdk-sh3rec/rel.self packages/ddpdfk/USRDIR/cores/fbneo_libretro_psl1ght.self
cp build-cellsdk-sh3rec/rel.self packages/ddpdfk/USRDIR/EBOOT.BIN
cp ps3_dualcore_pkg_20260928/SSNE10001/ICON0.PNG packages/ddpdfk/

./psl1ght-current-src/tools/sfo_pkg/sfo --fromxml packages/ddpdfk/sfo.xml packages/ddpdfk/PARAM.SFO
./psl1ght-current-src/tools/sfo_pkg/pkg --contentid=UP0001-CAVE00001_00-0000000000000001 packages/ddpdfk packages/DoDonPachi_DaiFukkatsu_PS3.pkg

echo "=== Package complete ==="
ls -lh packages/DoDonPachi_DaiFukkatsu_PS3.pkg
