#ifndef FBNEO_PS3_SPRITE_COLOR_SHADOW_H
#define FBNEO_PS3_SPRITE_COLOR_SHADOW_H


#include "burnint.h"


#ifdef __cplusplus
extern "C" {
#endif


void ps3_pgm_color_shadow_init();

void ps3_pgm_color_shadow_exit();


void ps3_pgm_color_shadow_submit(
    const UINT8 *packed,
    UINT32 span_bytes,
    UINT32 phase,
    const UINT8 *expected);


void ps3_pgm_color_shadow_flush();


#ifdef __cplusplus
}
#endif


#endif
