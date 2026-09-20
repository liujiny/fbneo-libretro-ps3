#!/usr/bin/env python3
"""Randomized oracle test for the production three-phase PGM color unpack helper."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
if not pathlib.Path("/.dockerenv").exists():
    raise SystemExit("Run inside ps3dev-gcc13:rsxfix.")
text = (root / "src/burn/drv/pgm/pgm_draw.cpp").read_text()
signature = "static inline void pgm_sprite_colors8_unpack_span("
start = text.index(signature)
pos = text.index("{", start) + 1
depth = 1
while depth:
    depth += (text[pos] == "{") - (text[pos] == "}")
    pos += 1
helper = text[start:pos]

source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t UINT8; typedef uint32_t UINT32;
'''+helper+r'''
static void require(int ok) { if (!ok) abort(); }
static uint32_t rng = 0x718ac35u;
int main() {
    unsigned long long cases=0;
    for (unsigned phase=0; phase<3; ++phase) {
        for (unsigned n=0; n<1000000; ++n) {
            UINT8 span[8], out[8];
            for (unsigned i=0; i<8; ++i) {
                rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; span[i]=rng;
            }
            pgm_sprite_colors8_unpack_span(span,phase,out);
            for (unsigned i=0; i<8; ++i) {
                unsigned p=phase+i, off=(p/3)*2;
                unsigned word=span[off] | span[off+1]<<8;
                require(out[i]==((word>>((p%3)*5))&31));
            }
            ++cases;
        }
    }
    printf("PASS %llu unpack cases\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix="pgm-unpack-test-") as d:
    cpp = pathlib.Path(d) / "unpack.cpp"
    exe = pathlib.Path(d) / "unpack-test"
    cpp.write_text(source)
    subprocess.run(["g++", "-std=gnu++98", "-O3", "-g", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
