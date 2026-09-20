#!/usr/bin/env python3

import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit(
        "usage: bin2c.py input.elf output.h"
    )

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

data = src.read_bytes()

lines = []

lines.append("#ifndef FBNEO_PS3_SPRITE_WORKER_SPU_BIN_H")
lines.append("#define FBNEO_PS3_SPRITE_WORKER_SPU_BIN_H")
lines.append("")
lines.append(
    "static const unsigned char "
    "ps3_sprite_worker_spu_bin[] "
    "__attribute__((aligned(64))) = {"
)

for pos in range(0, len(data), 12):
    chunk = data[pos:pos + 12]

    lines.append(
        "    " +
        ", ".join("0x%02x" % b for b in chunk) +
        ","
    )

lines.append("};")
lines.append("")
lines.append(
    "static const unsigned int "
    "ps3_sprite_worker_spu_bin_size = "
    "sizeof(ps3_sprite_worker_spu_bin);"
)
lines.append("")
lines.append("#endif")
lines.append("")

dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text("\n".join(lines))

print(
    "generated %s (%d bytes)"
    % (dst, len(data))
)
