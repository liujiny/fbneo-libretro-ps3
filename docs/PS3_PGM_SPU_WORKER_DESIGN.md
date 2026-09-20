# PS3 PGM packed-color SPU worker design

This is an implementation design only. The software PPU renderer remains the
reference and fallback; an SPU worker must be separately switchable and
disabled by default until pixel-equivalence tests pass.

## First candidate boundary

Batch packed-color unpacking and endian conversion around the PGM sprite color
reader in `src/burn/drv/pgm/pgm_draw.cpp`. The useful unit is a collection of
contiguous source spans prepared while a sprite row is decoded, not a single
sprite or individual pixel. The PPU continues to choose sprite ordering,
clipping, priority, and fallback behavior. An SPU batch converts packed source
words into palette indices or the existing intermediate color representation;
the current PPU code then performs any behavior-sensitive composition.

Proposed ABI (fixed-width fields, 16-byte aligned):

```c
struct PgmColorSpan {
    uint32_t source_offset;  // byte offset in the packed-color page/window
    uint32_t output_offset;  // byte offset in the batch output buffer
    uint16_t pixel_count;
    uint16_t palette_base;
    uint16_t flags;          // source endian/packing variant, no priority policy
    uint16_t reserved;
};

struct PgmColorBatch {
    uint32_t span_count;
    uint32_t input_bytes;
    uint32_t output_bytes;
    uint32_t generation;
    struct PgmColorSpan spans[];
};
```

PPU job producer groups enough adjacent row spans to amortize job and DMA
setup. One SPU job receives a descriptor list and source window, converts all
spans, and writes an output window. Use two aligned input/output buffer pairs:
while the SPU computes on pair 0, DMA can fill pair 1; completion fences before
the PPU consumes output or the SPU reuses either pair. DMA lists should be
coalesced by adjacent source ranges, with a bounded maximum span count per job.

The local-store budget is capped at 256 KiB. Initial target budget: code and
stack 40 KiB, two input buffers 2×32 KiB, two output buffers 2×32 KiB,
descriptors and constants 16 KiB, leaving about 72 KiB headroom. Confirm the
linked image and stack with the SPU map before increasing any buffers. No large
PPU-resident cache is introduced. If job setup fails, a source span crosses an
unsupported boundary, or a batch exceeds its bounded descriptor/input budget,
run the existing PPU unpack path for that work.

Potential next steps after color expansion are mask decoding and pixel
unpacking, but only if profiling shows material PPU cost. Each must preserve
the same clipping, palette, and output ordering checks as the reference path.

## RSX integration boundary (future work only)

The FBNeo core receives a CPU framebuffer (`pBurnDraw`) and has no RSX command
context. A future hardware sprite path therefore needs a frontend-owned
interface in the separate RetroArch PS3 video driver, with frame-scoped buffer
ownership and synchronization. Do not call RSX APIs from the core.

The likely exchange is an ordered frame batch of opaque records containing
quad geometry, source texture/page reference, palette reference, priority,
blend/shadow flags, and sequence order. The frontend can batch compatible
simple records by texture and state. The core must retain software output for
complex priority, shadow, blend, clipping, or unsupported sprites. A
driver-owned upload/texture pool and explicit submit/fence/release callbacks
will be needed; exact ABI should be designed with the RetroArch PS3 driver
before implementation. No such interface or RSX renderer is implemented here.
