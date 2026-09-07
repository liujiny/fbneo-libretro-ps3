#include "ps3_memory_pool.h"

#if defined(__PS3__)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE      (64u * 1024u * 1024u)
#define POOL_ALIGNMENT 128u
#define POOL_MAGIC     0x504f4f4cu
#define FALLBACK_MAGIC 0x46414c4cu

typedef struct pool_block {
	size_t size;
	struct pool_block *prev;
	struct pool_block *next;
	void *allocation;
	uint32_t magic;
	uint32_t is_free;
	uint8_t padding[POOL_ALIGNMENT - (sizeof(size_t) + 3 * sizeof(void *) + 2 * sizeof(uint32_t))];
} pool_block;

typedef char pool_header_must_be_128_bytes[(sizeof(pool_block) == POOL_ALIGNMENT) ? 1 : -1];

static uint8_t *g_pool;
static pool_block *g_first;

static size_t align_128(size_t value)
{
	return (value + (POOL_ALIGNMENT - 1u)) & ~(size_t)(POOL_ALIGNMENT - 1u);
}

int pool_init(void)
{
	if (g_pool != NULL) return 1;
	g_pool = (uint8_t *)memalign(POOL_ALIGNMENT, POOL_SIZE);
	if (g_pool == NULL) return 0;
	memset(g_pool, 0, POOL_SIZE);
	g_first = (pool_block *)g_pool;
	g_first->size = POOL_SIZE - sizeof(pool_block);
	g_first->allocation = g_pool;
	g_first->magic = POOL_MAGIC;
	g_first->is_free = 1;
	return 1;
}

void pool_destroy(void)
{
	if (g_pool != NULL) free(g_pool);
	g_pool = NULL;
	g_first = NULL;
}

static void *fallback_malloc(size_t size)
{
	size_t payload = align_128(size ? size : 1u);
	pool_block *block = (pool_block *)memalign(POOL_ALIGNMENT, sizeof(pool_block) + payload);
	if (block == NULL) return NULL;
	block->size = payload;
	block->allocation = block;
	block->magic = FALLBACK_MAGIC;
	block->is_free = 0;
	return (uint8_t *)block + sizeof(pool_block);
}

void *pool_malloc(size_t size)
{
	pool_block *block;
	size_t wanted = align_128(size ? size : 1u);

	/* CPS3 and similar boards allocate one block larger than the entire pool.
	 * Keeping an unused 64 MiB reserve beside that fallback allocation causes
	 * avoidable OOM on PS3. Release it only while it is still wholly unused;
	 * subsequent allocations continue through the aligned fallback path. */
	if (wanted > POOL_SIZE - sizeof(pool_block) && g_first != NULL &&
		g_first->is_free && g_first->prev == NULL && g_first->next == NULL) {
		free(g_pool);
		g_pool = NULL;
		g_first = NULL;
	}
	for (block = g_first; block != NULL; block = block->next) {
		if (!block->is_free || block->size < wanted) continue;
		if (block->size >= wanted + sizeof(pool_block) + POOL_ALIGNMENT) {
			pool_block *split = (pool_block *)((uint8_t *)block + sizeof(pool_block) + wanted);
			split->size = block->size - wanted - sizeof(pool_block);
			split->prev = block;
			split->next = block->next;
			split->allocation = g_pool;
			split->magic = POOL_MAGIC;
			split->is_free = 1;
			if (split->next != NULL) split->next->prev = split;
			block->next = split;
			block->size = wanted;
		}
		block->is_free = 0;
		memset((uint8_t *)block + sizeof(pool_block), 0, block->size);
		return (uint8_t *)block + sizeof(pool_block);
	}
	return fallback_malloc(size);
}

void pool_free(void *ptr)
{
	pool_block *block;
	if (ptr == NULL) return;
	block = (pool_block *)((uint8_t *)ptr - sizeof(pool_block));
	if (block->magic == FALLBACK_MAGIC) {
		free(block->allocation);
		return;
	}
	if (block->magic != POOL_MAGIC || g_pool == NULL ||
		(uint8_t *)block < g_pool || (uint8_t *)block >= g_pool + POOL_SIZE) return;
	block->is_free = 1;
	if (block->next != NULL && block->next->is_free && block->next->magic == POOL_MAGIC) {
		pool_block *next = block->next;
		block->size += sizeof(pool_block) + next->size;
		block->next = next->next;
		if (block->next != NULL) block->next->prev = block;
	}
	if (block->prev != NULL && block->prev->is_free && block->prev->magic == POOL_MAGIC) {
		pool_block *prev = block->prev;
		prev->size += sizeof(pool_block) + block->size;
		prev->next = block->next;
		if (prev->next != NULL) prev->next->prev = prev;
	}
}
#endif
