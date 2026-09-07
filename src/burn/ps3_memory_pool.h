#ifndef FBNEO_PS3_MEMORY_POOL_H
#define FBNEO_PS3_MEMORY_POOL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int pool_init(void);
void pool_destroy(void);
void *pool_malloc(size_t size);
void pool_free(void *ptr);
#ifdef __cplusplus
}
#endif
#endif
