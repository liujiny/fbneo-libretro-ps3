#ifndef FBNEO_PS3_MEMORY_POOL_H
#define FBNEO_PS3_MEMORY_POOL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int pool_init(void);
void pool_destroy(void);
void *pool_malloc(size_t size);
void *pool_malloc_tagged(size_t allocator_size, size_t requested_size);
void pool_free(void *ptr);
void ps3_mem_diag_reset(void);
void ps3_mem_diag_set_next_label(const char *label);
void ps3_mem_diag_set_next_site(const char *file, int line);
void ps3_mem_diag_stage(const char *stage);
void ps3_mem_diag_summary(const char *reason);
void ps3_mem_diag_game_info(const char *path, const void *data, size_t size);
void ps3_mem_diag_note(const char *message, size_t value);
void ps3_mem_diag_frame_event(const char *event, unsigned frame, long value);
void ps3_mem_diag_frame_enabled(void);
void ps3_mem_diag_probe_enabled(void);
void ps3_mem_diag_life(const char *event);
void ps3_mem_diag_video(const void *data, unsigned width, unsigned height, size_t pitch);
void ps3_mem_diag_audio(const void *data, size_t frames);
void ps3_mem_diag_direct_alloc(const char *purpose, size_t size, const void *ptr, const char *file, int line);
#ifdef __cplusplus
}
#endif
#endif
