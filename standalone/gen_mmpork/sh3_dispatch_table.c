#include "sh3_types.h"

Sh3BlockFn g_rom_blocks[2097152];
Sh3BlockFn g_ram_blocks[2097152];

void sh3_register_blocks_0(void);
void sh3_register_blocks_1(void);
void sh3_register_blocks_2(void);
void sh3_register_blocks_3(void);
void sh3_register_blocks_4(void);
void sh3_register_blocks_5(void);
void sh3_register_blocks_6(void);
void sh3_register_blocks_7(void);
void sh3_register_blocks_8(void);
void sh3_register_blocks_9(void);
void sh3_register_blocks_10(void);
void sh3_register_blocks_11(void);
void sh3_register_blocks_12(void);
void sh3_register_blocks_13(void);
void sh3_register_blocks_14(void);
void sh3_register_blocks_15(void);

void sh3_init_dispatch_table(void) {
    memset(g_rom_blocks, 0, sizeof(g_rom_blocks));
    memset(g_ram_blocks, 0, sizeof(g_ram_blocks));
    sh3_register_blocks_0();
    sh3_register_blocks_1();
    sh3_register_blocks_2();
    sh3_register_blocks_3();
    sh3_register_blocks_4();
    sh3_register_blocks_5();
    sh3_register_blocks_6();
    sh3_register_blocks_7();
    sh3_register_blocks_8();
    sh3_register_blocks_9();
    sh3_register_blocks_10();
    sh3_register_blocks_11();
    sh3_register_blocks_12();
    sh3_register_blocks_13();
    sh3_register_blocks_14();
    sh3_register_blocks_15();
}
