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
    FILE *fp = fopen("/dev_hdd0/tmp/fbneo_launch_game.txt", "r");
    if (fp) {
        if (fgets(game_path, sizeof(game_path), fp)) {
            char *p = strchr(game_path, '\r'); if (p) *p = 0;
            p = strchr(game_path, '\n'); if (p) *p = 0;
        }
        fclose(fp);
    }

    if (log) {
        fprintf(log, "[LAUNCHER] Target game: %s\n", game_path);
        fflush(log);
    }

    const char *candidates[] = {
        "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.self",
        "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_psl1ght.SELF",
        "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_ps3.self",
        "/dev_hdd0/game/SSNE10001/USRDIR/cores/fbneo_libretro_ps3.SELF",
        "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_psl1ght.self",
        "/dev_hdd0/game/ARCD00001/USRDIR/cores/fbneo_libretro_psl1ght.SELF",
        NULL
    };

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

    // If we reach here, spawn failed!
    log = fopen("/dev_hdd0/tmp/launcher.log", "a");
    if (log) {
        fprintf(log, "[LAUNCHER] ERROR: sys_game_process_exitspawn2 failed! errno=%d\n", errno);
        fclose(log);
    }
    return 1;
}
