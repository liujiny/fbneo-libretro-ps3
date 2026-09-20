#ifndef FBNEO_PS3_SPRITE_WORKER_H
#define FBNEO_PS3_SPRITE_WORKER_H

#include "spu/ps3_sprite_color_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

int ps3_pgm_spu_worker_init(void);
void ps3_pgm_spu_worker_shutdown(void);
int ps3_pgm_spu_worker_ready(void);

/*
 * v1-E async pipeline API
 *
 * submit:
 *     send job to SPU without waiting
 *
 * poll:
 *     collect completed job
 */

int ps3_pgm_spu_color_submit(
    ps3_spu_color_batch *batch);

int ps3_pgm_spu_color_poll(
    ps3_spu_color_result_batch *results,
    uint32_t *count_out);


/*
 * Legacy synchronous fallback.
 */
int ps3_pgm_spu_color_batch(
    ps3_spu_color_batch *batch);

#ifdef __cplusplus
}
#endif

#endif
