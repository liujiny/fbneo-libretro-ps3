#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/process.h>

SYS_PROCESS_PARAM(1001, 0x10000)

int main(int argc, char *argv[])
{
    FILE *log = fopen("/dev_hdd0/tmp/launcher.log", "w");
    if (log) {
        fprintf(log, "[LAUNCHER] Started, argc=%d\n", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(log, "  argv[%d] = %s\n", i, argv[i]);
        }
        fflush(log);
    }

    char game_path[256] = "/dev_hdd0/ROMS/ddpdfk.zip";
    const char *self_core = NULL;

    if (argc > 0 && argv[0]) {
        if (strstr(argv[0], "CAVE00002")) {
            strcpy(game_path, "/dev_hdd0/ROMS/mmpork.zip");
            self_core = "/dev_hdd0/game/CAVE00002/USRDIR/cores/fbneo_libretro_psl1ght.self";
        } else if (strstr(argv[0], "CAVE00001")) {
            strcpy(game_path, "/dev_hdd0/ROMS/ddpdfk.zip");
            self_core = "/dev_hdd0/game/CAVE00001/USRDIR/cores/fbneo_libretro_psl1ght.self";
        }
    }

    FILE *fp = fopen("/dev_hdd0/tmp/fbneo_launch_game.txt", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            char *p = strchr(buf, '\r'); if (p) *p = 0;
            p = strchr(buf, '\n'); if (p) *p = 0;
            if (buf[0]) {
                strncpy(game_path, buf, sizeof(game_path) - 1);
                game_path[sizeof(game_path) - 1] = 0;
            }
        }
        fclose(fp);
    }

    if (log) {
        fprintf(log, "[LAUNCHER] Target game: %s\n", game_path);
        fflush(log);
    }

    const char *candidates[10] = { 0 };
    int cidx = 0;
    if (self_core) candidates[cidx++] = self_core;
    candidates[cidx++] = "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self";
    candidates[cidx++] = "/dev_hdd0/game/CAVE00001/USRDIR/cores/fbneo_libretro_psl1ght.self";
    candidates[cidx++] = "/dev_hdd0/game/CAVE00002/USRDIR/cores/fbneo_libretro_psl1ght.self";
    candidates[cidx++] = "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_psl1ght.self";
    candidates[cidx] = NULL;

    const char *core_path = NULL;
    for (int i = 0; candidates[i]; i++) {
        FILE *cfile = fopen(candidates[i], "rb");
        if (cfile) {
            fseek(cfile, 0, SEEK_END);
            long csize = ftell(cfile);
            fclose(cfile);
            if (csize > 10000000) { // Valid core > 10 MB
                core_path = candidates[i];
                if (log) {
                    fprintf(log, "[LAUNCHER] Selected core: %s (size=%ld)\n", core_path, csize);
                    fflush(log);
                }
                break;
            }
        }
    }

    if (!core_path) {
        if (log) {
            fprintf(log, "[LAUNCHER] ERROR: No valid core found in candidate paths!\n");
            fclose(log);
        }
        return 1;
    }

    // Both argv[0] and argv[1] hold game_path so regardless of kernel argv[0] prepending,
    // argv[1] in RetroArch will always be game_path!
    const char *spawn_argv[] = {
        game_path,
        game_path,
        NULL
    };

    char spawn_data[256];
    for (unsigned i = 0; i < sizeof(spawn_data); ++i)
        spawn_data[i] = (char)(i & 0xff);

    if (log) {
        fprintf(log, "[LAUNCHER] Spawning core: %s with game: %s\n", core_path, game_path);
        fflush(log);
        fclose(log);
    }

    sys_game_process_exitspawn2(core_path, spawn_argv, NULL,
                                (sys_addr_t)spawn_data, sizeof(spawn_data),
                                1000, SYS_PROCESS_PRIMARY_STACK_SIZE_1M);

    // Fallback attempt without spawn_data
    sys_game_process_exitspawn2(core_path, spawn_argv, NULL,
                                0, 0,
                                1000, SYS_PROCESS_PRIMARY_STACK_SIZE_1M);

    log = fopen("/dev_hdd0/tmp/launcher.log", "a");
    if (log) {
        fprintf(log, "[LAUNCHER] ERROR: sys_game_process_exitspawn2 failed! errno=%d\n", errno);
        fclose(log);
    }
    return 1;
}
