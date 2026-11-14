#ifdef __PS3__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int __real_main(int argc, char *argv[]);

static void boot_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    int fd = open("/dev_hdd0/tmp/cv1k_boot.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        write(fd, buf, (size_t)n);
        close(fd);
    }
}

int __wrap_main(int argc, char *argv[])
{
    static char target_game[256];
    static char *fake_argv[4];

    // Truncate previous boot log
    int lfd = open("/dev_hdd0/tmp/cv1k_boot.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (lfd >= 0) close(lfd);

    boot_log("[BOOT] __wrap_main entered, argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        boot_log("  argv[%d] = %s\n", i, (argv && argv[i]) ? argv[i] : "(null)");
    }

    /* Default game path based on build target */
#if defined(CV1K_GAME_MMPORK)
    strcpy(target_game, "/dev_hdd0/ROMS/mmpork.zip");
#else
    strcpy(target_game, "/dev_hdd0/ROMS/ddpdfk.zip");
#endif

    /* Check if an override target game is set (e.g. from benchmark or autotest) */
    int tfd = open("/dev_hdd0/tmp/fbneo_launch_game.txt", O_RDONLY);
    if (tfd >= 0) {
        char buf[256];
        int rd = read(tfd, buf, sizeof(buf) - 1);
        close(tfd);
        if (rd > 0) {
            buf[rd] = 0;
            char *p = strchr(buf, '\r'); if (p) *p = 0;
            p = strchr(buf, '\n'); if (p) *p = 0;
            if (buf[0]) {
                strncpy(target_game, buf, sizeof(target_game) - 1);
                target_game[sizeof(target_game) - 1] = 0;
            }
        }
    }

    boot_log("[BOOT] target_game=%s\n", target_game);

    /* If no content argument was provided (e.g. started from XMB or Salamander),
     * inject target_game as argv[1] so RetroArch directly autoboots the game! */
    if (argc <= 1 || !argv[1] || !argv[1][0]) {
        fake_argv[0] = (argv && argv[0]) ? argv[0] : (char*)"retroarch";
        fake_argv[1] = target_game;
        fake_argv[2] = NULL;
        boot_log("[BOOT] Invoking __real_main(2, [\"%s\", \"%s\"])\n", fake_argv[0], fake_argv[1]);
        return __real_main(2, fake_argv);
    }

    boot_log("[BOOT] Invoking __real_main with original argv\n");
    return __real_main(argc, argv);
}

#ifdef __cplusplus
}
#endif
#endif
