# PS3 PGM C1.23 final configuration

C1.23 is the frozen PS3 PGM configuration after hardware validation on KOV2,
DDP2, DDP3, Metal Slug 3, Metal Slug 5, and Metal Slug X.

The PSL1GHT Makefile defaults select the final layout:

- packed PGM sprite colors retained;
- 16 MiB, 2-way packed-color file cache;
- mask ROM resident in memory (no runtime mask disk reads);
- PPU nozoom row-span and row-cursor decode;
- SPU color worker, color prefetch, and performance profiler disabled;
- native PS3 allocations enabled.

Reference core build:

```sh
cd /work/src/burner/libretro
make platform=psl1ght COMMONLV= \
  PS3_MEMORY_DIAGNOSTIC=1 \
  PS3_MEMORY_POOL_SIZE_MB=64 \
  PS3_NATIVE_MEMORY=1 \
  PS3_PGM_MASK_FILE_CACHE=0 \
  PS3_PGM_COLOR_FILE_CACHE=1 \
  PS3_PGM_PERF_PROFILE=0 \
  PS3_PGM_COLOR_CACHE_2WAY=1 \
  PS3_PGM_COLOR_PREFETCH=0 \
  PS3_PGM_SPU_WORKER=0 \
  -j"$(nproc)"
```

KOV2 hardware validation reached approximately 67.2 MiB native peak and
69.9 MiB tracked peak with no native or fallback allocation failures. The
all-resident color+mask experiment is intentionally not used because it ran
out of memory on KOV2.
