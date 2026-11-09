#include "cv1k_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Global SH-3 VM state */
Sh3VM g_vm;
int   g_sh3_static_rec_enabled = 0;

/* Memory blocks */
uint8_t *g_main_ram = NULL;   /* 16 MB */
uint8_t *g_main_rom = NULL;   /* 4 MB */
uint8_t *g_cache_ram = NULL;  /* 16 KB */
uint8_t *g_sound_rom = NULL;  /* 8 MB */
uint8_t *g_flash_rom = NULL;  /* 132 MB or mapped */

uint8_t  g_dips[4]   = { 0, 0, 0, 0 };
uint32_t g_inputs[4] = { 0xff, 0xff, 0xff, 0xff };

uint16_t *g_screen_vram = NULL;
int       g_screen_width  = 320;
int       g_screen_height = 240;
int       g_screen_pitch  = 320 * 2;

#ifdef __cplusplus
}
#include "burnint.h"
#include "devices/epic12.h"
#include "snd/ymz770.h"
#include "devices/rtc9701.h"
#include "devices/serflash.h"
extern "C" {
#else
/* External peripheral interfaces from FBNeo devices */
extern void     epic12_init(int ram_size, uint16_t *ram, uint8_t *dippy);
extern void     epic12_exit(void);
extern void     epic12_reset(void);
extern uint32_t epic12_blitter_read(uint32_t offset);
extern void     epic12_blitter_write(uint32_t offset, uint32_t data);
extern void     epic12_draw_screen(uint8_t *recalc_palette);

extern void     ymz770_init(uint8_t *rom, int rom_length);
extern void     ymz770_exit(void);
extern void     ymz770_reset(void);
extern void     ymz770_write(int offset, uint8_t data);
extern void     ymz770_update(int16_t *output, int samples_len);

extern void     rtc9701_init(void);
extern void     rtc9701_exit(void);
extern void     rtc9701_reset(void);
extern uint8_t  rtc9701_read_bit(void);
extern void     rtc9701_write_bit(uint8_t data);
extern void     rtc9701_set_cs_line(uint8_t data);
extern void     rtc9701_set_clock_line(uint8_t data);

extern void     serflash_init(uint8_t *rom, int length);
extern void     serflash_exit(void);
extern void     serflash_reset(void);
extern uint8_t  serflash_io_read(void);
extern void     serflash_data_write(uint8_t data);
extern void     serflash_cmd_write(uint8_t data);
extern void     serflash_addr_write(uint8_t data);
extern void     serflash_enab_write(uint8_t data);
#endif

#define SH3_PORT_C 0x04000000
#define SH3_PORT_D 0x04000008
#define SH3_PORT_E 0x04000010
#define SH3_PORT_F 0x04000018
#define SH3_PORT_L 0x04000020
#define SH3_PORT_J 0x04000028

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define FROM_BE16(v) __builtin_bswap16(v)
#define FROM_BE32(v) __builtin_bswap32(v)
#define TO_BE16(v)   __builtin_bswap16(v)
#define TO_BE32(v)   __builtin_bswap32(v)
#else
#define FROM_BE16(v) (v)
#define FROM_BE32(v) (v)
#define TO_BE16(v)   (v)
#define TO_BE32(v)   (v)
#endif

/*
 * Memory Read Functions
 */
uint8_t cv1k_read_b(Sh3VM *vm, uint32_t addr) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        return g_main_ram[norm - 0x0c000000];
    }
    if (norm < 0x00400000) {
        return g_main_rom[norm];
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        return g_cache_ram[addr & 0x3fff];
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        return (uint8_t)epic12_blitter_read(norm & 0x7f);
    }
    switch (norm) {
        case 0x10000000: return serflash_io_read();
        case 0x10000001: case 0x10000002: case 0x10000003:
        case 0x10000004: case 0x10000005: case 0x10000006:
        case 0x10000007: return 0xff;
        case 0x10c00001: return (rtc9701_read_bit() & 1) | 0xfe;
        case 0x04000124: return (uint8_t)g_inputs[0];
        case 0x04000125: return (uint8_t)g_inputs[1];
        case 0x04000128: return 0xdf | 0x20; /* flash ready */
        case 0x04000129: return (uint8_t)g_inputs[2];
        case 0x04000130: return 0xff; /* Port J */
        case 0x04000131: return 0xff; /* Port K */
        case 0x04000134: return (uint8_t)g_inputs[3];
        case 0x04000135: return 0xff;
    }
    return 0;
}

uint16_t cv1k_read_w(Sh3VM *vm, uint32_t addr) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        return FROM_BE16(*(uint16_t *)(g_main_ram + (norm - 0x0c000000)));
    }
    if (norm < 0x00400000) {
        return FROM_BE16(*(uint16_t *)(g_main_rom + norm));
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        return FROM_BE16(*(uint16_t *)(g_cache_ram + (addr & 0x3fff)));
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        return (uint16_t)epic12_blitter_read(norm & 0x7f);
    }
    switch (norm) {
        case 0x04000124: return ((uint16_t)(uint8_t)g_inputs[0] << 8) | (uint8_t)g_inputs[1];
        case 0x04000128: return ((uint16_t)(0xdf | 0x20) << 8) | (uint8_t)g_inputs[2];
        case 0x04000130: return 0xffff;
        case 0x04000134: return ((uint16_t)(uint8_t)g_inputs[3] << 8) | 0xff;
    }
    return 0;
}

uint32_t cv1k_read_l(Sh3VM *vm, uint32_t addr) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        /* Speedhack hook: If game polls idle RAM, immediately yield slice cycles */
        if (__builtin_expect(norm == 0x0c1d1346 || norm == 0x0c1de160, 0)) {
            if (vm->cycles > 0) vm->cycles = 0;
        }
        return FROM_BE32(*(uint32_t *)(g_main_ram + (norm - 0x0c000000)));
    }
    if (norm < 0x00400000) {
        return FROM_BE32(*(uint32_t *)(g_main_rom + norm));
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        return FROM_BE32(*(uint32_t *)(g_cache_ram + (addr & 0x3fff)));
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        uint32_t v = epic12_blitter_read(norm & 0x7f);
        /* If blitter is busy (status bit 0 set), fast-forward remaining CPU cycles */
        if ((norm & 0x7f) == 0x10 && (v & 1)) {
            if (vm->cycles > 0) vm->cycles = 0;
        }
        return v;
    }
    switch (norm) {
        case 0x04000124: return ((uint32_t)(uint8_t)g_inputs[0] << 24) | ((uint32_t)(uint8_t)g_inputs[1] << 8);
        case 0x04000128: return ((uint32_t)(0xdf | 0x20) << 24) | ((uint32_t)(uint8_t)g_inputs[2] << 8);
        case 0x04000130: return 0xffffffff;
        case 0x04000134: return ((uint32_t)(uint8_t)g_inputs[3] << 24) | 0x0000ff00;
    }
    return 0;
}

/* PC-relative reads (fast path) */
uint16_t cv1k_read_w_pc(Sh3VM *vm, uint32_t addr) {
    uint32_t norm = addr & 0x0fffffff;
    if (norm >= 0x0c000000 && norm < 0x0d000000) {
        return FROM_BE16(*(uint16_t *)(g_main_ram + (norm - 0x0c000000)));
    }
    if (norm < 0x00400000) {
        return FROM_BE16(*(uint16_t *)(g_main_rom + norm));
    }
    return cv1k_read_w(vm, addr);
}

uint32_t cv1k_read_l_pc(Sh3VM *vm, uint32_t addr) {
    uint32_t norm = addr & 0x0fffffff;
    if (norm >= 0x0c000000 && norm < 0x0d000000) {
        return FROM_BE32(*(uint32_t *)(g_main_ram + (norm - 0x0c000000)));
    }
    if (norm < 0x00400000) {
        return FROM_BE32(*(uint32_t *)(g_main_rom + norm));
    }
    return cv1k_read_l(vm, addr);
}

/*
 * Memory Write Functions
 */
void cv1k_write_b(Sh3VM *vm, uint32_t addr, uint8_t v) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        g_main_ram[norm - 0x0c000000] = v;
        return;
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        g_cache_ram[addr & 0x3fff] = v;
        return;
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        epic12_blitter_write(norm & 0x7f, v);
        return;
    }
    switch (norm) {
        case 0x10000000: serflash_data_write(v); return;
        case 0x10000001: serflash_cmd_write(v); return;
        case 0x10000002: serflash_addr_write(v); return;
        case 0x10400000: case 0x10400001: case 0x10400002: case 0x10400003:
        case 0x10400004: case 0x10400005: case 0x10400006: case 0x10400007:
            ymz770_write(norm & 7, v);
            return;
        case 0x10c00001:
            rtc9701_write_bit(v & 1);
            rtc9701_set_clock_line((v & 2) >> 1);
            rtc9701_set_cs_line((~v & 4) >> 2);
            return;
        case 0x10c00003: serflash_enab_write(v); return;
    }
}

void cv1k_write_w(Sh3VM *vm, uint32_t addr, uint16_t v) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        *(uint16_t *)(g_main_ram + (norm - 0x0c000000)) = TO_BE16(v);
        return;
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        *(uint16_t *)(g_cache_ram + (addr & 0x3fff)) = TO_BE16(v);
        return;
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        epic12_blitter_write(norm & 0x7f, v);
        return;
    }
}

void cv1k_write_l(Sh3VM *vm, uint32_t addr, uint32_t v) {
    uint32_t norm = addr & 0x0fffffff;
    if (__builtin_expect(norm >= 0x0c000000 && norm < 0x0d000000, 1)) {
        *(uint32_t *)(g_main_ram + (norm - 0x0c000000)) = TO_BE32(v);
        return;
    }
    if ((addr & 0xffffc000) == 0xf0000000) {
        *(uint32_t *)(g_cache_ram + (addr & 0x3fff)) = TO_BE32(v);
        return;
    }
    if ((norm & 0xffffff80) == 0x18000000) {
        epic12_blitter_write(norm & 0x7f, v);
        return;
    }
}

/*
 * Runtime Lifecycle
 */
int cv1k_runtime_init(const char *rom_path, const char *ram_path, const char *sound_path) {
    g_main_ram = (uint8_t *)malloc(0x01000000);  /* 16 MB */
    g_main_rom = (uint8_t *)malloc(0x00400000);  /* 4 MB */
    g_cache_ram = (uint8_t *)malloc(0x00004000); /* 16 KB */
    g_sound_rom = (uint8_t *)malloc(0x00800000); /* 8 MB */

    if (!g_main_ram || !g_main_rom || !g_cache_ram || !g_sound_rom) {
        printf("Error: Failed to allocate hardware buffers.\n");
        return -1;
    }

    memset(g_main_ram, 0, 0x01000000);
    memset(g_cache_ram, 0, 0x00004000);
    memset(g_sound_rom, 0, 0x00800000);

    /* Load ROM */
    FILE *f_rom = fopen(rom_path, "rb");
    if (!f_rom) {
        printf("Error: Could not open ROM %s\n", rom_path);
        return -1;
    }
    fread(g_main_rom, 1, 0x00400000, f_rom);
    fclose(f_rom);

    /* Load initial decompressed RAM */
    FILE *f_ram = fopen(ram_path, "rb");
    if (f_ram) {
        fread(g_main_ram, 1, 0x01000000, f_ram);
        fclose(f_ram);
    } else {
        /* Fallback: copy first 0x51000 from ROM into RAM */
        memcpy(g_main_ram, g_main_rom, 0x51000);
    }

    /* Load Sound ROM */
    if (sound_path) {
        FILE *f_snd = fopen(sound_path, "rb");
        if (f_snd) {
            fread(g_sound_rom, 1, 0x00800000, f_snd);
            fclose(f_snd);
        }
    }

    /* Initialize dispatch table */
    sh3_init_dispatch_table();

    /* Initialize peripherals */
    epic12_init(0x01000000, (uint16_t *)g_main_ram, &g_dips[0]);
    ymz770_init(g_sound_rom, 0x00800000);
    rtc9701_init();

    cv1k_runtime_reset();
    printf("Cave CV1000 Hardware Runtime Initialized.\n");
    return 0;
}

void cv1k_runtime_reset(void) {
    memset(&g_vm, 0, sizeof(g_vm));
    g_vm.pc = 0x00000000;
    g_vm.sr = 0x700000f0;
    g_vm.vbr = 0x8c000000;
    g_vm.gbr = 0xffffff00;
    g_vm.r[15] = 0x0d000000;
    g_vm.cycles = 0;

    epic12_reset();
    ymz770_reset();
    rtc9701_reset();
}

static uint32_t s_pc_history[32];
static int s_pc_hist_idx = 0;

void cv1k_runtime_run_frame(void) {
    /* 60 slices per frame: 102.4 MHz / 60 frames / 60 slices = ~28,444 cycles/slice */
    const int SLICES_PER_FRAME = 60;
    const int CYCLES_PER_SLICE = 28444;

    for (int slice = 0; slice < SLICES_PER_FRAME; ++slice) {
        g_vm.cycles += CYCLES_PER_SLICE;

        while (g_vm.cycles > 0) {
            s_pc_history[s_pc_hist_idx++ & 31] = g_vm.pc;
            uint32_t norm = g_vm.pc & 0x0fffffff;
            if (norm >= 0x00400000 && (norm < 0x0c000000 || norm >= 0x0c550000)) {
                printf("FATAL: Out-of-bounds PC: 0x%08x (norm: 0x%08x)!\n", g_vm.pc, norm);
                printf("Recent PCs:\n");
                for (int i = 0; i < 32; ++i) {
                    printf("  [%2d] 0x%08x\n", i, s_pc_history[(s_pc_hist_idx + i) & 31]);
                }
                exit(1);
            }

            if (__builtin_expect(g_vm.pc == 0x00000094, 0)) {
                /* Pre-decompressed RAM already loaded: skip 10-second redundant decompressor */
                g_vm.pc = 0x0000009e;
            }

            Sh3BlockFn block = sh3_find_block(g_vm.pc);
            if (__builtin_expect(block != NULL, 1)) {
                block(&g_vm);
            } else {
                sh3_interpret_block(&g_vm);
            }
        }
    }

    /* Audio update: 44100 Hz / 60 = 735 samples */
    int16_t audio_buf[735 * 2];
    ymz770_update(audio_buf, 735);

    /* Video update: draw active screen */
#ifdef __cplusplus
    UINT8 recalc = 0;
    epic12_draw_screen(recalc);
#else
    uint8_t recalc = 0;
    epic12_draw_screen(&recalc);
#endif
}

void cv1k_runtime_shutdown(void) {
    epic12_exit();
    ymz770_exit();
    rtc9701_exit();

    if (g_main_ram) free(g_main_ram);
    if (g_main_rom) free(g_main_rom);
    if (g_cache_ram) free(g_cache_ram);
    if (g_sound_rom) free(g_sound_rom);
    g_main_ram = NULL;
}

#ifdef __cplusplus
}
#endif
