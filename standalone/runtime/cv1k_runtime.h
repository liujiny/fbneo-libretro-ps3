#ifndef CV1K_RUNTIME_H
#define CV1K_RUNTIME_H

#include "../gen/sh3_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global hardware registers and buffers */
extern Sh3VM   g_vm;
extern uint8_t g_dips[4];
extern uint32_t g_inputs[4];

/* CV1000 Hardware Lifecycle */
int  cv1k_runtime_init(const char *rom_path, const char *ram_path, const char *sound_path);
void cv1k_runtime_reset(void);
void cv1k_runtime_run_frame(void);
void cv1k_runtime_shutdown(void);

/* Video output texture presentation buffer */
extern uint16_t *g_screen_vram;
extern int       g_screen_width;
extern int       g_screen_height;
extern int       g_screen_pitch;

#ifdef __cplusplus
}
#endif

#endif /* CV1K_RUNTIME_H */
