#include "gen/sh3_types.h"
#include "runtime/cv1k_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Mock peripherals for fast CPU-core profiling on host */
void epic12_init(int ram_size, uint16_t *ram, uint8_t *dippy) { (void)ram_size; (void)ram; (void)dippy; }
void epic12_exit(void) {}
void epic12_reset(void) {}
uint32_t epic12_blitter_read(uint32_t offset) { (void)offset; return 0; }
void epic12_blitter_write(uint32_t offset, uint32_t data) { (void)offset; (void)data; }
void epic12_draw_screen(uint8_t *recalc_palette) { (void)recalc_palette; }

void ymz770_init(uint8_t *rom, int rom_length) { (void)rom; (void)rom_length; }
void ymz770_exit(void) {}
void ymz770_reset(void) {}
void ymz770_write(int offset, uint8_t data) { (void)offset; (void)data; }
void ymz770_update(int16_t *output, int samples_len) { (void)output; (void)samples_len; }

void rtc9701_init(void) {}
void rtc9701_exit(void) {}
void rtc9701_reset(void) {}
uint8_t rtc9701_read_bit(void) { return 1; }
void rtc9701_write_bit(uint8_t data) { (void)data; }
void rtc9701_set_cs_line(uint8_t data) { (void)data; }
void rtc9701_set_clock_line(uint8_t data) { (void)data; }

void serflash_init(uint8_t *rom, int length) { (void)rom; (void)length; }
void serflash_exit(void) {}
void serflash_reset(void) {}
uint8_t serflash_io_read(void) { return 0xff; }
void serflash_data_write(uint8_t data) { (void)data; }
void serflash_cmd_write(uint8_t data) { (void)data; }
void serflash_addr_write(uint8_t data) { (void)data; }
void serflash_enab_write(uint8_t data) { (void)data; }

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char *argv[]) {
    printf("===================================================\n");
    printf(" Cave CV1000 Static Recompiler Benchmark Host\n");
    printf(" Game: DoDonPachi DaiFukkatsu (ddpdfk)\n");
    printf("===================================================\n");

    int res = cv1k_runtime_init(
        "roms/ddpdfk/ddpdfk_program.bin",
        "roms/ddpdfk/ddpdfk_ram_init.bin",
        NULL
    );
    if (res != 0) {
        printf("Failed to initialize runtime.\n");
        return 1;
    }

    printf("Runtime successfully initialized.\n");
    printf("Boot PC: 0x%08x, SP: 0x%08x\n", g_vm.pc, g_vm.r[15]);

    int total_frames = (argc > 1) ? atoi(argv[1]) : 300;
    printf("Running %d frames benchmark...\n", total_frames);

    double t0 = get_time_sec();
    for (int f = 0; f < total_frames; ++f) {
        cv1k_runtime_run_frame();
        if ((f + 1) % 60 == 0) {
            double now = get_time_sec();
            double elapsed = now - t0;
            double fps = (f + 1) / elapsed;
            printf("Frame %4d: %.2f FPS (PC: 0x%08x, cycles: %d)\n", f + 1, fps, g_vm.pc, g_vm.cycles);
        }
    }
    double t1 = get_time_sec();
    double total_time = t1 - t0;
    double avg_fps = total_frames / total_time;

    printf("\n===================================================\n");
    printf(" Benchmark Completed!\n");
    printf(" Total Frames: %d\n", total_frames);
    printf(" Total Time:   %.4f seconds\n", total_time);
    printf(" Average FPS:  %.2f FPS (%.2f ms/frame)\n", avg_fps, (total_time / total_frames) * 1000.0);
    printf(" Final PC:     0x%08x\n", g_vm.pc);
    for (int i = 0; i < 16; ++i) {
        printf(" R%-2d: 0x%08x%s", i, g_vm.r[i], (i % 4 == 3) ? "\n" : "  ");
    }
    printf("===================================================\n");

    cv1k_runtime_shutdown();
    return 0;
}
