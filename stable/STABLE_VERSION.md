# PS3 FBNeo stable build

- Stable binary: `D:/ps3-work/stable/fbneo_libretro_ps3_stable.SELF`
- Size: `41679560` bytes
- SHA256: `A5FD8721215F336F1F6FFAC4CC9E98E6B26321DCD917FAE8E55CBD19B713A02A`
- Source commit at freeze: `274dae32a2a7c23957d45b28e8c89a0174ecb950`
- Branch at freeze: `source-main`
- Verified games: `mslug3`, `mslug4`, `mslug5`, `mslugx`

## PS3 memory configuration

- `PS3_MEMORY_DIAGNOSTIC=1`
- `PS3_MEMORY_POOL_SIZE_MB=8`
- `PS3_NATIVE_MEMORY=1`
- Neo Geo effective game pool: `0` bytes
- `NeoSpriteROM`: PS3 native allocator, 64 KiB pages
- `NeoCMCDecryptTemp`: PS3 native allocator, 64 KiB pages, freed after decrypt
- `Neo68KROM`: PS3 native allocator, 64 KiB pages
- `NeoADPCM_A_ROM`: PS3 native allocator, 64 KiB pages

The stable SELF is preserved outside the source tree and must not be overwritten by later experimental builds.
