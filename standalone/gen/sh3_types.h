#ifndef SH3_TYPES_H
#define SH3_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sh3VM {
    uint32_t r[16];
    uint32_t pc;
    uint32_t ppc;
    uint32_t pr;
    uint32_t sr;
    uint32_t gbr;
    uint32_t vbr;
    uint32_t mach;
    uint32_t macl;
    uint32_t t;
    uint32_t spc;
    uint32_t ssr;
    int32_t  cycles;
} Sh3VM;

typedef void (*Sh3BlockFn)(Sh3VM *vm);

extern uint8_t *g_main_ram;
extern uint8_t *g_main_rom;
extern uint8_t *g_cache_ram;

extern Sh3BlockFn g_rom_blocks[2097152];
extern Sh3BlockFn g_ram_blocks[2097152];

static inline Sh3BlockFn sh3_find_block(uint32_t pc) {
    uint32_t masked = pc & 0x0fffffff;
    if (masked >= 0x0c000000) {
        uint32_t idx = (masked - 0x0c000000) >> 1;
        return (idx < 2097152) ? g_ram_blocks[idx] : 0;
    } else {
        uint32_t idx = masked >> 1;
        return (idx < 2097152) ? g_rom_blocks[idx] : 0;
    }
}

/* Fast direct memory macros */
#define READ_B(addr)      cv1k_read_b(vm, addr)
#define READ_W(addr)      cv1k_read_w(vm, addr)
#define READ_L(addr)      cv1k_read_l(vm, addr)

#define WRITE_B(addr, v)  cv1k_write_b(vm, addr, v)
#define WRITE_W(addr, v)  cv1k_write_w(vm, addr, v)
#define WRITE_L(addr, v)  cv1k_write_l(vm, addr, v)

#define READ_W_PC(addr)   cv1k_read_w_pc(vm, addr)
#define READ_L_PC(addr)   cv1k_read_l_pc(vm, addr)

uint8_t  cv1k_read_b(Sh3VM *vm, uint32_t addr);
uint16_t cv1k_read_w(Sh3VM *vm, uint32_t addr);
uint32_t cv1k_read_l(Sh3VM *vm, uint32_t addr);

void cv1k_write_b(Sh3VM *vm, uint32_t addr, uint8_t v);
void cv1k_write_w(Sh3VM *vm, uint32_t addr, uint16_t v);
void cv1k_write_l(Sh3VM *vm, uint32_t addr, uint32_t v);

uint16_t cv1k_read_w_pc(Sh3VM *vm, uint32_t addr);
uint32_t cv1k_read_l_pc(Sh3VM *vm, uint32_t addr);

void sh3_interpret_block(Sh3VM *vm);
void sh3_init_dispatch_table(void);

#ifdef __cplusplus
}
#endif

#endif /* SH3_TYPES_H */
