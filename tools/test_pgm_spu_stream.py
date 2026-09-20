#!/usr/bin/env python3
"""Differential test for C1.4 contiguous SPU input preparation."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
if not pathlib.Path("/.dockerenv").exists():
    raise SystemExit("Run inside ps3dev-gcc13:rsxfix.")


def extract(path, name):
    text = path.read_text()
    original = "pgm_ps3_spu_prepare_color_batch"
    start = text.index("static inline UINT32\n" + original)
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos].replace(original, name)


old = root / "src/burn/drv/pgm/pgm_draw.cpp.bak_H15C1_1"
new = root / "src/burn/drv/pgm/pgm_draw.cpp"
prelude = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "src/burn/drv/pgm/spu/ps3_sprite_color_protocol.h"
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
#define PGM_PS3_COLOR_PAGE_SIZE 65536u
static UINT32 nPGMSPRColMaskLen;
static UINT8 rom[4 * PGM_PS3_COLOR_PAGE_SIZE];
static bool file_cache;
#define nPGMSPRColFileCacheActive file_cache
static UINT8 page[PGM_PS3_COLOR_PAGE_SIZE];
static UINT32 resident = UINT32(-1), lookups, item_checks;
static unsigned long long cases;
#define require(ok) do { if (!(ok)) { fprintf(stderr, "FAIL line %d cases=%llu\\n", \
    __LINE__, cases); abort(); } } while (0)
static void reset() { resident = UINT32(-1); lookups = 0; item_checks = 0; }
static const UINT8 *pgm_ps3_color_cache_span(UINT32 off, UINT32 bytes) {
    ++lookups;
    if (!file_cache) return rom + off;
    UINT32 in = off & (PGM_PS3_COLOR_PAGE_SIZE - 1);
    if (bytes > PGM_PS3_COLOR_PAGE_SIZE - in) return NULL;
    UINT32 p = off / PGM_PS3_COLOR_PAGE_SIZE;
    if (resident != p) {
        memcpy(page, rom + p * PGM_PS3_COLOR_PAGE_SIZE, sizeof(page));
        resident = p;
    }
    return page + in;
}
static void decode_old(const ps3_spu_color_batch *b, UINT8 *out) {
    for (UINT32 n=0; n<b->count; ++n) {
        UINT32 phase=b->item[n].phase, pos=0;
        UINT32 word=b->item[n].packed[0] | b->item[n].packed[1]<<8;
        for (UINT32 i=0; i<8; ++i) {
            *out++=(word>>(phase*5))&31;
            if (++phase==3 && i!=7) { phase=0; pos+=2;
                word=b->item[n].packed[pos] | b->item[n].packed[pos+1]<<8; }
        }
    }
}
static void decode_stream(const ps3_spu_color_stream_batch *b, UINT8 *out) {
    UINT32 phase=b->phase, pos=0, pixels=b->count*8;
    UINT32 word=b->packed[0] | b->packed[1]<<8;
    for (UINT32 i=0; i<pixels; ++i) {
        *out++=(word>>(phase*5))&31;
        if (++phase==3 && i+1<pixels) { phase=0; pos+=2;
            word=b->packed[pos] | b->packed[pos+1]<<8; }
    }
}
'''
checks = r'''
static void check(UINT32 pixel, UINT32 actual, UINT32 mask, UINT32 limit) {
    ps3_spu_color_batch before, after;
    UINT8 a[PS3_SPU_COLOR_BATCH_SIZE*8], b[PS3_SPU_COLOR_BATCH_SIZE*8];
    memset(&before,0xa5,sizeof(before)); memset(&after,0x5a,sizeof(after));
    memset(a,0,sizeof(a)); memset(b,0,sizeof(b)); nPGMSPRColMaskLen=mask;
    reset(); UINT32 old_count=baseline_builder(&before,pixel,actual,limit);
    UINT32 old_lookups=lookups;
    reset(); UINT32 new_count=current_builder(&after,pixel,actual,limit);
    ps3_spu_color_stream_batch *stream=(ps3_spu_color_stream_batch*)&after;
    if (!(old_count==new_count && stream->count==new_count)) {
        fprintf(stderr,"count pixel=%u actual=%u mask=%u limit=%u old=%u new=%u hdr=%u mode=%u\\n",
                pixel,actual,mask,limit,old_count,new_count,stream->count,file_cache); abort(); }
    require(lookups<=old_lookups);
    if (new_count) {
        require(stream->magic==PS3_SPU_SIGNAL_COLOR_STREAM && stream->phase==pixel%3);
        require(stream->packed_bytes==((stream->phase+new_count*8+2)/3)*2);
        require(PS3_SPU_COLOR_STREAM_DMA_BYTES(stream->packed_bytes)<=sizeof(*stream));
        decode_old(&before,a); decode_stream(stream,b);
        if (memcmp(a,b,new_count*8)!=0) {
            fprintf(stderr,"decode pixel=%u actual=%u mask=%u limit=%u count=%u phase=%u packed=%u mode=%u\\n",
                    pixel,actual,mask,limit,new_count,stream->phase,stream->packed_bytes,file_cache); abort(); }
        for(UINT32 i=0;i<new_count*8;++i) {
            UINT32 p=pixel+i, off=(p/3)*2;
            UINT32 word=rom[off] | rom[off+1]<<8;
            require(b[i]==((word>>((p%3)*5))&31));
        }
    }
    ++cases;
}
int main() {
    UINT32 rng=0x51a7c39u;
    for (UINT32 phase=0; phase<3; ++phase) for (UINT32 count=1; count<=64; ++count) {
        UINT32 command=PS3_SPU_SIGNAL_COLOR_STREAM_BASE |
            ((phase<<PS3_SPU_COLOR_STREAM_PHASE_SHIFT)&PS3_SPU_COLOR_STREAM_PHASE_MASK) |
            (count&PS3_SPU_COLOR_ASYNC_COUNT_MASK);
        require((command&PS3_SPU_SIGNAL_COLOR_STREAM_MASK)==PS3_SPU_SIGNAL_COLOR_STREAM_BASE);
        require((command&PS3_SPU_COLOR_ASYNC_COUNT_MASK)==count);
        require(((command&PS3_SPU_COLOR_STREAM_PHASE_MASK)>>PS3_SPU_COLOR_STREAM_PHASE_SHIFT)==phase);
        require(PS3_SPU_COLOR_STREAM_DMA_BYTES(((phase+count*8+2)/3)*2)<=
                sizeof(ps3_spu_color_stream_batch));
    }
    for(UINT32 i=0;i<sizeof(rom);++i) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; rom[i]=rng; }
    UINT32 actual=(sizeof(rom)/2)*3;
    for(unsigned mode=0;mode<2;++mode) { file_cache=mode;
        for(UINT32 limit=0;limit<=64;++limit) {
            for(UINT32 edge=0;edge<=4;++edge) {
                UINT32 center=edge*(PGM_PS3_COLOR_PAGE_SIZE/2)*3;
                for(int d=-520;d<=24;++d) if((int)center+d>=0) check(center+d,actual,actual-1,limit);
            }
            for(UINT32 end=0;end<520;++end) { check(1,1+end,actual-1,limit); check(2,actual,2+end,limit); }
        }
        for(UINT32 i=0;i<10000;++i) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
            check(rng%actual,actual,actual-1,rng%65); }
    }
    for(UINT32 limit=15;limit<=48;limit+=(limit==15?1:(limit==16?16:8))) {
        ps3_spu_color_batch batch; nPGMSPRColMaskLen=actual-1; file_cache=true; reset();
        current_builder(&batch,120,actual,limit);
        ps3_spu_color_stream_batch *s=(ps3_spu_color_stream_batch*)&batch;
        require(s->count==limit && item_checks==0);
        printf("batch=%u packed=%u input_dma=%u old_dma=%u lookups=%u item_checks=%u\n",limit,s->packed_bytes,
               PS3_SPU_COLOR_STREAM_DMA_BYTES(s->packed_bytes),PS3_SPU_COLOR_COMPACT_BYTES(limit),lookups,item_checks);
    }
    printf("PASS %llu stream differential cases (ASan/UBSan)\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix="pgm-stream-test-") as d:
    cpp = pathlib.Path(d) / "stream.cpp"
    exe = pathlib.Path(d) / "stream-test"
    current = extract(new, "current_builder")
    loop = "while (count < batch_limit)\n        {"
    assert current.count(loop) == 1
    current = current.replace(loop, loop + "\n                ++item_checks;")
    cpp.write_text(prelude + extract(old, "baseline_builder") + current + checks)
    subprocess.run(["g++", "-std=gnu++98", "-O2", "-g", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(root), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
