# PS3 memory optimizations

## Why PGM runs out of memory

PGM games can keep several large images resident at once: packed sprite colors, sprite masks, tile ROMs and expanded background tiles, sound samples, and the emulated main-memory block. On PS3, the XDR heap and the process memory budget are limited. The original PGM paths also expanded sprite colors and maintained separate tile allocations, which raised the peak well beyond the size of any single ROM.

## Allocation strategy

The PS3 allocator supports native 64 KiB page allocations through the platform memory API. Large Neo Geo and PGM buffers can be assigned to native memory while ordinary allocations continue to use the system pool. Optional memory diagnostics report allocation labels, live and peak totals, failures, and unload balance. PSL1GHT does not provide the user-memory query used by the other PS3 API, so that query is compiled out for PSL1GHT.

PGM keeps packed sprite-color data in its 16-bit packed representation instead of expanding it to three bytes per pixel. Its tile and expanded-background data share an allocation, and background tiles are generated lazily into a bounded cache. KOV2 keeps the ICS2115 logical 16 MiB address space while storing only 10 MiB physically: the unmapped 6 MiB hole is represented by address remapping in the sample reader.

## File-backed sprite caches

When enabled at build time, large PGM sprite images are copied to temporary files and accessed through 64 KiB pages:

- Mask: 64 pages, 4 MiB resident cache, backing file `/dev_hdd0/tmp/fbneo-pgm-mask.cache`.
- Packed color: 128 pages, 8 MiB resident cache, backing file `/dev_hdd0/tmp/fbneo-pgm-color.cache`.

The mask cache activates for mask images larger than 4 MiB when the image size is a power of two. Non-power-of-two mask ROMs remain resident because the current address wrapping uses a power-of-two mask. The packed-color cache activates only for packed color data that saves at least 4 MiB after reserving its 8 MiB cache (a 12 MiB minimum image size). Unpacked color paths remain resident. Each cache can fall back to a full resident allocation if its page-cache allocation fails. Backing files are removed when the game unloads.

Build with `PS3_PGM_MASK_FILE_CACHE=1` and `PS3_PGM_COLOR_FILE_CACHE=1` to enable both features. The defaults are off.

## RPCS3 regression results

**RPCS3 validated. Real PS3 hardware validation pending.** These results are emulator runs; they do not establish equivalent behavior or performance on physical hardware.

| Game | Packed color | Sprite mask |
| --- | --- | --- |
| KOV2 | About 34 MiB resident to 8 MiB cache | 16 MiB resident to 4 MiB cache |
| KOV | About 28 MiB resident to 8 MiB cache | 12 MiB remains resident because its size is non-power-of-two |
| DDP2 | 16 MiB resident to 8 MiB cache | 8 MiB resident to 4 MiB cache |
| Puzzli2 | About 4 MiB remains resident | 2 MiB remains resident |

Observed KOV2 allocator measurements:

```text
BurnMalloc_tracked_live_peak = 60144256
native_peak                  = 58851328
first_frame native_live      = 46268416
first_frame system_live      = 2644096
native_failures              = 0
allocation_failure_count     = 0
unload native_live           = 0
unload system_live           = 0
```

## Reproducible PS3 build

`Dockerfile.ps3dev-gcc13` pins these upstream revisions:

| Component | Revision |
| --- | --- |
| ps3dev | `bc09c6292f1be95be874cfda9226ea9ba9ceaef3` |
| ps3toolchain | `e13aa539c07abe86cdbcd095dc1340492dbca213` |
| PSL1GHT | `f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe` |
| ps3libraries | `df6a3a867f55be032a9df30cbdf23664e197d430` |

The GCC 13 librsx callback fix is maintained as `patches/psl1ght-rsx-context-callback-memory-clobber.patch`. It is based on the pinned PSL1GHT revision above and adds a `memory` clobber to the inline assembly constraints for `rsxContextCallback`. GCC 13 otherwise may move memory operations across the hidden `bctrl`, which can freeze the RSX callback path. Apply this patch to that PSL1GHT source revision before rebuilding applications that use librsx; this repository does not vendor or fork PSL1GHT.

The complete FBNeo core build configuration used for validation is:

```sh
cd src/burner/libretro
make platform=psl1ght COMMONLV= clean
make platform=psl1ght COMMONLV= \
  PS3_MEMORY_DIAGNOSTIC=1 \
  PS3_MEMORY_POOL_SIZE_MB=64 \
  PS3_NATIVE_MEMORY=1 \
  PS3_PGM_MASK_FILE_CACHE=1 \
  PS3_PGM_COLOR_FILE_CACHE=1 \
  -j"$(nproc)"
```

For routine builds, `build-ps3-psl1ght.sh` fingerprints these options and selects a clean or incremental build. Set the same environment variables before running it to reproduce the full memory-validation configuration.
