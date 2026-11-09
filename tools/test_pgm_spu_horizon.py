#!/usr/bin/env python3
"""Test production mask horizons and scheduler with a simulated async worker.

Run inside ps3dev-gcc13:rsxfix. Simulated call latency is not a PS3 benchmark.
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
if not pathlib.Path("/.dockerenv").exists():
    raise SystemExit("Run inside ps3dev-gcc13:rsxfix.")
text = (root / "src/burn/drv/pgm/pgm_draw.cpp").read_text()


def function(signature):
    start = text.index(signature)
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "src/burn/drv/pgm/spu/ps3_sprite_color_protocol.h"
typedef uint8_t UINT8; typedef uint32_t UINT32;
typedef int32_t INT32; typedef uint64_t UINT64;
#define PS3_MEMORY_DIAGNOSTIC 1
#define PS3_PGM_SPU_SHADOW_INTERVAL 257
#define require(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d\n",__LINE__); abort(); } } while(0)
static UINT64 stat_shadow_compare, stat_shadow_skipped, stat_shadow_bad, stat_shadow_pixel_bad;
static UINT32 tick, ready_tick, job_pixel, job_count, latency;
static UINT32 current_pixel, current_horizon;
static bool busy;
static UINT64 submits, decodes, hits, records, bounded, rejected;
static UINT8 color(UINT32 pixel) { return (pixel ^ (pixel >> 8)) & 31; }
static UINT64 value(const char *s, const char *key) {
    const char *p=strstr(s,key); require(p); return strtoull(p+strlen(key),NULL,10);
}
static void ps3_mem_diag_logf(const char *fmt, ...) {
    char line[8192]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
    require(n>0 && (unsigned)n<sizeof(line));
    require(value(line," submit=")-value(line," done=")==value(line," inflight="));
    require(value(line," decoded=")==value(line," used=")+value(line," abandoned=")+value(line," pending="));
    require(value(line," used=")==value(line," shadow_cmp=")+value(line," shadow_skipped="));
    require(value(line," submit=")==value(line," gate12_submit=")+value(line," mask_early_submit=")+value(line," tail_chain="));
    require(value(line," mask_early_submit=")==0);
    records++;
}
static UINT32 pgm_ps3_spu_prepare_color_batch(ps3_spu_color_batch *b, UINT32 p, UINT32 actual, UINT32 limit) {
    UINT32 available=p<actual ? (actual-p)/8 : 0;
    b->magic=p; b->count=limit<available ? limit : available; return b->count;
}
static INT32 ps3_pgm_spu_color_submit(ps3_spu_color_batch *b) {
    require(!busy && b->count && b->count<=64);
    if (current_horizon!=0xffffffffu) {
        require(b->magic>=current_pixel);
        require(b->magic+(b->count-1)*8 <= current_pixel+(current_horizon-1)*8);
        bounded++;
    }
    if (tick%997==0) { rejected++; return -1; }
    busy=true; job_pixel=b->magic; job_count=b->count;
    ready_tick=tick+latency; submits++; decodes+=job_count; return 0;
}
static INT32 ps3_pgm_spu_color_poll(ps3_spu_color_result_batch *r, UINT32 *count) {
    require(busy);
    if(tick<ready_tick) return 1;
    *count=job_count;
    for(UINT32 n=0;n<job_count;n++) for(UINT32 i=0;i<8;i++) r->result[n][i]=color(job_pixel+n*8+i);
    busy=false; return 0;
}
'''

checks = r'''
static UINT32 rng=0x796a52e1u;
static UINT32 random32() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void call(UINT32 pixel, UINT32 horizon) {
    tick++; current_pixel=pixel; current_horizon=horizon;
    UINT8 out[8];
    if(pgm_ps3_spu_batch_color8(pixel,0x10000000u,out,horizon)) {
        for(UINT32 i=0;i<8;i++) require(out[i]==color(pixel+i));
        hits++; stat_shadow_compare++;
    }
}
int main(int argc, char **) {
    bool hints=argc==1;
    // Exhaust all masks and short mask sequences, including all-transparent rows.
    for(UINT32 length=0;length<=8;length++) {
        UINT32 combinations=1u<<(2*length);
        for(UINT32 v=0;v<combinations;v++) {
            UINT8 masks[8]; UINT32 code=v, expected=0xffffffffu, calls=0;
            for(UINT32 i=0;i<length;i++) {
                const UINT8 choices[4]={0,0xff,1,0xfe}; masks[i]=choices[code&3]; code>>=2;
                if(masks[i]!=0xff) { calls++; if(masks[i]!=0 && expected==0xffffffffu) expected=calls; }
            }
            require(pgm_ps3_mask_color_horizon(masks,length)==expected);
        }
    }
    for(UINT32 m=1;m<255;m++) { UINT8 masks[4]={0,0xff,(UINT8)m,0}; require(pgm_ps3_mask_color_horizon(masks,4)==2); }
    for(UINT32 h=0;h<256;h++) for(UINT32 a=0;a<65;a++) for(UINT32 n=0;n<65;n++) {
        UINT32 got=pgm_ps3_spu_limit_horizon(n,a,h);
        require(got<=n && (got==0 || got+a<=h));
        require(got==(h<=a ? 0 : (n<h-a ? n : h-a)));
        require(pgm_ps3_spu_limit_horizon(n,a,0xffffffffu)==n);
    }
    // Known end, unknown row continuation, stream jumps, delayed/failed submits.
    for(UINT32 pass=0;pass<12000;pass++) {
        const UINT32 delays[6]={0,1,2,3,12,80}; latency=delays[pass%6];
        UINT32 length=1+random32()%192;
        UINT32 base=(pass%2 ? 10000 : 1000000)+random32()%5000;
        bool known=(pass%3)!=0;
        for(UINT32 i=0;i<length;i++) call(base+i*8, hints && known ? length-i : 0xffffffffu);
    }
    // Exercise cached countdown across transparent and partial masks.
    for(UINT32 row=0;row<2000;row++) {
        UINT8 masks[128];
        for(UINT32 i=0;i<128;i++) masks[i]=(random32()%10<8 ? 0 : (UINT8)random32());
        UINT32 horizon=0, pixel=20000+row*1024;
        latency=row%4;
        for(UINT32 i=0;i<128;i++) {
            if(masks[i]==0xff) continue;
            if(!horizon) horizon=pgm_ps3_mask_color_horizon(masks+i,128-i);
            require(horizon==pgm_ps3_mask_color_horizon(masks+i,128-i));
            call(pixel,hints ? horizon : 0xffffffffu);
            if(horizon!=0xffffffffu) horizon--;
            for(UINT32 bit=0;bit<8;bit++) if(!(masks[i]&(1u<<bit))) pixel++;
        }
    }
    require(records>10 && rejected>0 && hits>0);
    if(hints) require(bounded>1000);
    printf("PASS hints=%u calls=%u submits=%llu decoded=%llu used=%llu bounded_checks=%llu accounting_records=%llu rejected=%llu\n",
        hints,tick,(unsigned long long)submits,(unsigned long long)decodes,(unsigned long long)hits,
        (unsigned long long)bounded,(unsigned long long)records,(unsigned long long)rejected);
}
'''
with tempfile.TemporaryDirectory(prefix="pgm-horizon-test-") as temp:
    cpp = pathlib.Path(temp) / "horizon.cpp"
    exe = pathlib.Path(temp) / "horizon"
    cpp.write_text(prelude + function("static inline UINT32 pgm_ps3_mask_color_horizon(") +
                   function("static inline UINT32 pgm_ps3_spu_limit_horizon(") +
                   function("static inline INT32 pgm_ps3_spu_batch_color8(") + checks)
    subprocess.run(["g++", "-std=gnu++98", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(root),
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    subprocess.run([str(exe), "unknown-horizons"], check=True)
