#include "ps3_benchmark.h"
#include "burnint.h"
#include "ps3_memory_pool.h"
#include "libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__PS3__)
#include <sys/memory.h>
#if defined(__PSL1GHT__)
#include <ppu-lv2.h>
#include <ppu-types.h>
#else
#include <sys/process.h>
#endif
#endif

extern retro_environment_t environ_cb;
extern char g_driver_name[128];
extern UINT32 nBurnDrvActive;
extern UINT32 nBurnDrvCount;
extern INT32 nGameInpCount;
extern struct GameInp* GameInp;

ps3_benchmark_state_t g_ps3_bench;

#if defined(__PS3__)
#define PS3_BENCHMARK_CONFIG_PATH "/dev_hdd0/tmp/fbneo_benchmark.cfg"
#define PS3_BENCHMARK_REPORT_PATH "/dev_hdd0/tmp/fbneo_benchmark_report.txt"
#else
#define PS3_BENCHMARK_CONFIG_PATH "/tmp/fbneo_benchmark.cfg"
#define PS3_BENCHMARK_REPORT_PATH "/tmp/fbneo_benchmark_report.txt"
#endif

static inline unsigned long long ps3_bench_now_us(void)
{
#if defined(__PS3__)
	unsigned long long tb;
#if defined(__PSL1GHT__)
	__asm__ __volatile__("mftb %0" : "=r"(tb));
#else
	tb = __mftb();
#endif
	return (tb * 10ULL) / 798ULL;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
#endif
}

static unsigned long ps3_bench_get_user_memory(void)
{
#if defined(__PS3__) && !defined(__PSL1GHT__)
	sys_memory_info_t info;
	if (sys_memory_get_user_memory_size(&info) == 0) {
		return (unsigned long)info.available_user_memory;
	}
#elif defined(__PS3__) && defined(__PSL1GHT__)
	struct {
		uint32_t total;
		uint32_t available;
	} info;
	lv2syscall1(352, (uint64_t)&info);
	return (unsigned long)info.available;
#endif
	return 0;
}

static void trim_whitespace(char *str)
{
	char *end;
	char *start = str;
	while (isspace((unsigned char)*start)) start++;
	if (start != str) memmove(str, start, strlen(start) + 1);
	end = str + strlen(str) - 1;
	while (end >= str && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}
}

static void parse_config_file(const char *path, ps3_benchmark_config_t *cfg)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	char buf[4096];
	int rd = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (rd <= 0) return;
	buf[rd] = '\0';

	char *line = buf;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next) *next = '\0';
		char *p_cr = strchr(line, '\r');
		if (p_cr) *p_cr = '\0';

		char *eq = strchr(line, '=');
		if (eq) {
			*eq = '\0';
			char *key = line;
			char *val = eq + 1;
			trim_whitespace(key);
			trim_whitespace(val);

			if (strcmp(key, "enabled") == 0 || strcmp(key, "benchmark_enabled") == 0) {
				cfg->enabled = atoi(val);
			} else if (strcmp(key, "max_frames") == 0 || strcmp(key, "frames") == 0) {
				cfg->max_frames = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "warmup_frames") == 0) {
				cfg->warmup_frames = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "auto_coin") == 0) {
				cfg->auto_coin = atoi(val);
			} else if (strcmp(key, "coin_frame") == 0) {
				cfg->coin_frame = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "coin_duration") == 0) {
				cfg->coin_duration = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "coin_retry_interval") == 0) {
				cfg->coin_retry_interval = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "auto_start") == 0) {
				cfg->auto_start = atoi(val);
			} else if (strcmp(key, "start_frame") == 0) {
				cfg->start_frame = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "start_duration") == 0) {
				cfg->start_duration = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "start_retry_interval") == 0) {
				cfg->start_retry_interval = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "start_retry_count") == 0) {
				cfg->start_retry_count = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "auto_action") == 0) {
				cfg->auto_action = atoi(val);
			} else if (strcmp(key, "action_start_frame") == 0) {
				cfg->action_start_frame = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "action_interval") == 0) {
				cfg->action_interval = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "save_state_frame") == 0) {
				cfg->save_state_frame = (unsigned)strtoul(val, NULL, 10);
			} else if (strcmp(key, "load_state") == 0) {
				cfg->load_state = (int)strtol(val, NULL, 10);
			} else if (strcmp(key, "exit_after_frames") == 0) {
				cfg->exit_after_frames = atoi(val);
			} else if (strcmp(key, "log_window_frames") == 0) {
				cfg->log_window_frames = (unsigned)strtoul(val, NULL, 10);
			}
		}
		if (next) line = next + 1;
		else break;
	}
}

static void ps3_benchmark_scan_inputs(void)
{
	g_ps3_bench.p_coin = NULL;
	g_ps3_bench.p_start = NULL;
	g_ps3_bench.p_btn1 = NULL;
	g_ps3_bench.p_btn2 = NULL;
	g_ps3_bench.p_btn3 = NULL;
	g_ps3_bench.p_up = NULL;
	g_ps3_bench.p_down = NULL;
	g_ps3_bench.p_left = NULL;
	g_ps3_bench.p_right = NULL;

	for (INT32 i = 0; i < nGameInpCount; i++) {
		struct BurnInputInfo bii;
		memset(&bii, 0, sizeof(bii));
		if (BurnDrvGetInputInfo(&bii, i) != 0 || !bii.pVal) continue;
		if (bii.nType & BIT_GROUP_CONSTANT) continue;

		const char *info = bii.szInfo ? bii.szInfo : "";
		const char *name = bii.szName ? bii.szName : "";

		// Coin 1
		if (!g_ps3_bench.p_coin) {
			if (strstr(info, "coin 1") || strstr(info, "coin") ||
			    strstr(name, "Coin 1") || strstr(name, "Coin")) {
				g_ps3_bench.p_coin = bii.pVal;
			}
		}

		// Start 1
		if (!g_ps3_bench.p_start) {
			if (strstr(info, "start 1") || strstr(info, "start") ||
			    strstr(name, "Start 1") || strstr(name, "Start")) {
				g_ps3_bench.p_start = bii.pVal;
			}
		}

		// Button 1 (Attack / Primary Fire)
		if (!g_ps3_bench.p_btn1) {
			if (strstr(info, "fire 1") || strstr(name, "Button 1") ||
			    strstr(name, "Fire 1") || strstr(name, "Button A") ||
			    strstr(info, "fire")) {
				g_ps3_bench.p_btn1 = bii.pVal;
			}
		}

		// Button 2 (Bomb / Secondary Weapon)
		if (!g_ps3_bench.p_btn2) {
			if (strstr(info, "fire 2") || strstr(name, "Button 2") ||
			    strstr(name, "Fire 2") || strstr(name, "Button B")) {
				g_ps3_bench.p_btn2 = bii.pVal;
			}
		}

		// Button 3
		if (!g_ps3_bench.p_btn3) {
			if (strstr(info, "fire 3") || strstr(name, "Button 3") ||
			    strstr(name, "Button C")) {
				g_ps3_bench.p_btn3 = bii.pVal;
			}
		}

		// Direction: Down
		if (!g_ps3_bench.p_down) {
			if (strstr(info, "down") || strstr(name, "Down")) {
				g_ps3_bench.p_down = bii.pVal;
			}
		}

		// Direction: Right
		if (!g_ps3_bench.p_right) {
			if (strstr(info, "right") || strstr(name, "Right")) {
				g_ps3_bench.p_right = bii.pVal;
			}
		}

		// Direction: Up
		if (!g_ps3_bench.p_up) {
			if (strstr(info, "up") || strstr(name, "Up")) {
				g_ps3_bench.p_up = bii.pVal;
			}
		}

		// Direction: Left
		if (!g_ps3_bench.p_left) {
			if (strstr(info, "left") || strstr(name, "Left")) {
				g_ps3_bench.p_left = bii.pVal;
			}
		}
	}
}

void ps3_benchmark_init(void)
{
	memset(&g_ps3_bench, 0, sizeof(g_ps3_bench));

	// Default benchmark settings
	g_ps3_bench.cfg.enabled = 0;
	g_ps3_bench.cfg.max_frames = 2400; // ~40s budget
	g_ps3_bench.cfg.warmup_frames = 300; // ~5s past boot screens
	g_ps3_bench.cfg.auto_coin = 1;
	g_ps3_bench.cfg.coin_frame = 900; // ~15s (title screen active)
	g_ps3_bench.cfg.coin_duration = 30; // 0.5s hold
	g_ps3_bench.cfg.coin_retry_interval = 0;
	g_ps3_bench.cfg.auto_start = 1;
	g_ps3_bench.cfg.start_frame = 1020; // ~17s (start game)
	g_ps3_bench.cfg.start_duration = 20; // 0.33s hold
	g_ps3_bench.cfg.start_retry_interval = 0;
	g_ps3_bench.cfg.start_retry_count = 0;
	g_ps3_bench.cfg.auto_action = 1;
	g_ps3_bench.cfg.action_start_frame = 1140; // ~19s (character select & gameplay)
	g_ps3_bench.cfg.action_interval = 4;
	g_ps3_bench.cfg.exit_after_frames = 1;
	g_ps3_bench.cfg.log_window_frames = 300;

	// Check if config file exists
	int cfd = open(PS3_BENCHMARK_CONFIG_PATH, O_RDONLY);
	if (cfd >= 0) {
		close(cfd);
		g_ps3_bench.cfg.enabled = 1; // Default to enabled if config file is present
		parse_config_file(PS3_BENCHMARK_CONFIG_PATH, &g_ps3_bench.cfg);
	}

	if (!g_ps3_bench.cfg.enabled) {
		g_ps3_bench.active = 0;
		return;
	}

	g_ps3_bench.active = 1;
	g_ps3_bench.min_frame_us = (unsigned long long)-1;
	g_ps3_bench.max_frame_us = 0;
	g_ps3_bench.total_emulation_time_us = 0;
	g_ps3_bench.benchmarked_frames = 0;
	g_ps3_bench.window_time_us = 0;
	g_ps3_bench.window_frames = 0;

	ps3_benchmark_scan_inputs();

	g_ps3_bench.initial_available_user_memory = ps3_bench_get_user_memory();
	g_ps3_bench.memory_valid = (g_ps3_bench.initial_available_user_memory > 0);

	g_ps3_bench.wall_start_us = ps3_bench_now_us();
	g_ps3_bench.wall_bench_start_us = g_ps3_bench.wall_start_us;

	const char *hw_str = "other";
	UINT32 hw = BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK;
	if (hw == HARDWARE_CAVE_CV1000) hw_str = "Cave CV1000";
	else if (hw == HARDWARE_IGS_PGM) hw_str = "IGS PGM";
	else if (hw == HARDWARE_SNK_NEOGEO) hw_str = "SNK NeoGeo";
	else if (hw == HARDWARE_CAPCOM_CPS3) hw_str = "Capcom CPS3";

	ps3_mem_diag_print(
		"\n[PS3 BENCHMARK START] game=%s driver=\"%s\" hardware=\"%s\" max_frames=%u warmup=%u auto_coin=%u(%u) auto_start=%u(%u) auto_action=%u(%u) exit=%u\n",
		g_driver_name,
		BurnDrvGetTextA(DRV_FULLNAME) ? BurnDrvGetTextA(DRV_FULLNAME) : "unknown",
		hw_str,
		g_ps3_bench.cfg.max_frames,
		g_ps3_bench.cfg.warmup_frames,
		g_ps3_bench.cfg.auto_coin, g_ps3_bench.cfg.coin_frame,
		g_ps3_bench.cfg.auto_start, g_ps3_bench.cfg.start_frame,
		g_ps3_bench.cfg.auto_action, g_ps3_bench.cfg.action_start_frame,
		g_ps3_bench.cfg.exit_after_frames
	);
}

void ps3_bench_phase_begin(int idx)
{
	if (!g_ps3_bench.active || idx < 0 || idx >= 8 || g_ps3_bench.phase_active[idx])
		return;
	g_ps3_bench.phase_active[idx]   = 1;
	g_ps3_bench.phase_start_us[idx] = ps3_bench_now_us();
}

void ps3_bench_phase_end(int idx)
{
	unsigned long long now;
	if (!g_ps3_bench.active || idx < 0 || idx >= 8 || !g_ps3_bench.phase_active[idx])
		return;
	now = ps3_bench_now_us();
	g_ps3_bench.phase_us[idx] += now - g_ps3_bench.phase_start_us[idx];
	g_ps3_bench.phase_active[idx] = 0;
}

void ps3_benchmark_input_step(unsigned frame)
{
	if (!g_ps3_bench.active) return;

	// 1. Auto Coin
	if (g_ps3_bench.cfg.auto_coin && g_ps3_bench.p_coin) {
		unsigned start    = g_ps3_bench.cfg.coin_frame;
		unsigned duration = g_ps3_bench.cfg.coin_duration ? g_ps3_bench.cfg.coin_duration : 1;
		unsigned interval = g_ps3_bench.cfg.coin_retry_interval;

		if (frame >= start) {
			unsigned rel = frame - start;
			/* Retry mode repeats the pulse every 'interval' frames. The game
			 * ignores input while it boots, so an early start costs nothing and
			 * the coin lands as soon as the title screen accepts it - no
			 * per-game tuning needed. */
			if (interval == 0) {
				if (rel < duration)
					*g_ps3_bench.p_coin = 1;
			} else if ((rel % interval) < duration) {
				*g_ps3_bench.p_coin = 1;
			}
		}
	}

	// 2. Auto Start
	if (g_ps3_bench.cfg.auto_start && g_ps3_bench.p_start) {
		unsigned start    = g_ps3_bench.cfg.start_frame;
		unsigned duration = g_ps3_bench.cfg.start_duration ? g_ps3_bench.cfg.start_duration : 1;
		unsigned interval = g_ps3_bench.cfg.start_retry_interval;
		unsigned count    = g_ps3_bench.cfg.start_retry_count;

		if (frame >= start) {
			unsigned rel = frame - start;
			if (interval == 0) {
				if (rel < duration)
					*g_ps3_bench.p_start = 1;
			} else {
				unsigned idx = rel / interval;
				/* count == 0 means "keep trying"; a finite count avoids
				 * spamming START once the game is already running. */
				if ((count == 0 || idx < count) && (rel % interval) < duration)
					*g_ps3_bench.p_start = 1;
			}
		}
	}

	// 3. Auto Action & Navigation
	if (g_ps3_bench.cfg.auto_action && frame >= g_ps3_bench.cfg.action_start_frame) {
		unsigned rel = frame - g_ps3_bench.cfg.action_start_frame;
		unsigned interval = g_ps3_bench.cfg.action_interval ? g_ps3_bench.cfg.action_interval : 4;

		// Mash Button 1 (Attack/Shot)
		if ((rel % interval) < (interval / 2)) {
			if (g_ps3_bench.p_btn1) *g_ps3_bench.p_btn1 = 1;
		}

		// Character selection phase (~frames 240 to 450):
		// Alternate down/right to advance character/difficulty select
		if (rel < 250) {
			if ((rel % 40) < 10) {
				if (g_ps3_bench.p_down) *g_ps3_bench.p_down = 1;
			}
		} else {
			// In-game movement & secondary actions
			if ((rel % 60) < 30) {
				if (g_ps3_bench.p_right) *g_ps3_bench.p_right = 1;
			} else {
				if (g_ps3_bench.p_left) *g_ps3_bench.p_left = 1;
			}
			// Occasional Bomb / Special (Button 2)
			if ((rel % 240) >= 120 && (rel % 240) < 125) {
				if (g_ps3_bench.p_btn2) *g_ps3_bench.p_btn2 = 1;
			}
		}
	}
}

/* Called with the framebuffer before presentation: remembers the first frame
 * whose pixels are not all zero, so the boot self-test is excluded from the
 * statistics instead of dominating them. */
void ps3_benchmark_note_image(unsigned frame, const void *buf, unsigned w, unsigned h, size_t pitch)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned y, x, hits = 0;
	if (!g_ps3_bench.active || g_ps3_bench.image_seen || !p || !w || !h) return;
	for (y = 0; y < h; y += 8) {
		const unsigned char *row = p + (size_t)y * pitch;
		for (x = 0; x < w; x += 4) {
			if (row[x]) { ++hits; if (hits > 8) break; }
		}
		if (hits > 8) break;
	}
	if (hits > 8) {
		g_ps3_bench.image_seen = 1;
		g_ps3_bench.image_frame = frame;
	}
}


/* Core-level save state: the game's boot self-test takes well over a thousand
 * frames, so a run can save a state once the game is actually playable and
 * every later run restores it instead of re-running the self test. */
#define BENCH_STATE_PATH "/dev_hdd0/tmp/fbneo_bench_state.bin"

extern "C" size_t retro_serialize_size(void);
extern "C" bool retro_serialize(void *data, size_t size);
extern "C" bool retro_unserialize(const void *data, size_t size);

void ps3_benchmark_state_tick(unsigned frame)
{
	size_t sz;
	void *buf;
	FILE *f;
	if (!g_ps3_bench.active || !g_ps3_bench.cfg.save_state_frame) return;
	if (frame != g_ps3_bench.cfg.save_state_frame) return;
	sz = retro_serialize_size();
	if (!sz) return;
	buf = malloc(sz);
	if (retro_serialize(buf, sz)) {
		f = fopen(BENCH_STATE_PATH, "wb");
		if (f) {
			if (fwrite(buf, 1, sz, f) == sz)
			fclose(f);
		}
	}
	free(buf);
}

void ps3_benchmark_state_restore(void)
{
	static int done;
	size_t sz;
	void *buf;
	FILE *f;
	long len;
	if (done) return;
	done = 1;
	if (!g_ps3_bench.active || !g_ps3_bench.cfg.load_state) return;
	f = fopen(BENCH_STATE_PATH, "rb");
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	sz = retro_serialize_size();
	if (len <= 0 || (size_t)len != sz) {
		fclose(f);
		return;
	}
	buf = malloc(sz);
	if (!buf) { fclose(f); return; }
	if (fread(buf, 1, sz, f) == sz)
	fclose(f);
	free(buf);
}

void ps3_benchmark_frame_before(unsigned frame)
{
	(void)frame;
	if (!g_ps3_bench.active) return;
	g_ps3_bench.frame_start_us = ps3_bench_now_us();
}

void ps3_benchmark_frame_after(unsigned frame)
{
	if (!g_ps3_bench.active) return;

	unsigned long long now = ps3_bench_now_us();
	unsigned long long delta_us = now - g_ps3_bench.frame_start_us;

	unsigned warm = g_ps3_bench.cfg.warmup_frames;
	if (g_ps3_bench.image_seen) {
		unsigned from_image = g_ps3_bench.image_frame + 30;
		if (from_image > warm) warm = from_image;
	}
	if (frame >= warm) {
		if (frame == warm) {
			g_ps3_bench.wall_bench_start_us = now;
		}

		g_ps3_bench.total_emulation_time_us += delta_us;
		g_ps3_bench.benchmarked_frames++;

		if (delta_us < g_ps3_bench.min_frame_us) g_ps3_bench.min_frame_us = delta_us;
		if (delta_us > g_ps3_bench.max_frame_us) g_ps3_bench.max_frame_us = delta_us;

		g_ps3_bench.window_time_us += delta_us;
		g_ps3_bench.window_frames++;

		if (g_ps3_bench.cfg.log_window_frames > 0 &&
		    g_ps3_bench.window_frames >= g_ps3_bench.cfg.log_window_frames) {
			double win_fps = (double)g_ps3_bench.window_frames * 1000000.0 / (double)g_ps3_bench.window_time_us;
			double avg_ms = (double)g_ps3_bench.window_time_us / (double)g_ps3_bench.window_frames / 1000.0;

			ps3_memory_stats_t mem;
			ps3_mem_get_stats(&mem);

			ps3_mem_diag_print(
				"[PS3 BENCHMARK WINDOW] frame=%u window_fps=%.2f avg_ms=%.3f pool_used=%lu system_live=%lu native_live=%lu\n",
				frame, win_fps, avg_ms,
				(unsigned long)mem.pool_used,
				(unsigned long)mem.system_live,
				(unsigned long)mem.native_live
			);

			g_ps3_bench.window_time_us = 0;
			g_ps3_bench.window_frames = 0;
		}
	}
}

int ps3_benchmark_check_exit(unsigned frame)
{
	if (!g_ps3_bench.active) return 0;
	if (frame < g_ps3_bench.cfg.max_frames) return 0;

	g_ps3_bench.wall_end_us = ps3_bench_now_us();

	unsigned long final_user_memory = ps3_bench_get_user_memory();
	long memory_consumed = 0;
	if (g_ps3_bench.memory_valid && final_user_memory > 0) {
		memory_consumed = (long)g_ps3_bench.initial_available_user_memory - (long)final_user_memory;
	}

	unsigned long long bench_wall_us = g_ps3_bench.wall_end_us - g_ps3_bench.wall_bench_start_us;
	if (bench_wall_us == 0) bench_wall_us = 1;
	unsigned long long total_emu_us = g_ps3_bench.total_emulation_time_us;
	if (total_emu_us == 0) total_emu_us = 1;
	unsigned b_frames = g_ps3_bench.benchmarked_frames ? g_ps3_bench.benchmarked_frames : 1;

	double wall_fps = (double)b_frames * 1000000.0 / (double)bench_wall_us;
	double emu_fps = (double)b_frames * 1000000.0 / (double)total_emu_us;
	double avg_ms = (double)total_emu_us / (double)b_frames / 1000.0;
	double min_ms = (double)g_ps3_bench.min_frame_us / 1000.0;
	double max_ms = (double)g_ps3_bench.max_frame_us / 1000.0;

	ps3_memory_stats_t mem;
	ps3_mem_get_stats(&mem);

	const char *hw_str = "other";
	UINT32 hw = BurnDrvGetHardwareCode() & HARDWARE_PUBLIC_MASK;
	if (hw == HARDWARE_CAVE_CV1000) hw_str = "Cave CV1000";
	else if (hw == HARDWARE_IGS_PGM) hw_str = "IGS PGM";
	else if (hw == HARDWARE_SNK_NEOGEO) hw_str = "SNK NeoGeo";
	else if (hw == HARDWARE_CAPCOM_CPS3) hw_str = "Capcom CPS3";

	// Write structured report to memory log
	ps3_mem_diag_print("\n[PS3 BENCHMARK REPORT BEGIN]\n");
	ps3_mem_diag_print("game=%s\n", g_driver_name);
	ps3_mem_diag_print("driver=%s\n", BurnDrvGetTextA(DRV_FULLNAME) ? BurnDrvGetTextA(DRV_FULLNAME) : "unknown");
	ps3_mem_diag_print("hardware=%s\n", hw_str);
	ps3_mem_diag_print("total_frames=%u\n", frame + 1);
	ps3_mem_diag_print("benchmarked_frames=%u\n", b_frames);
	ps3_mem_diag_print("wall_time_us=%llu\n", bench_wall_us);
	ps3_mem_diag_print("wall_fps=%.2f\n", wall_fps);
	ps3_mem_diag_print("emulation_time_us=%llu\n", total_emu_us);
	ps3_mem_diag_print("emulation_fps=%.2f\n", emu_fps);
	ps3_mem_diag_print("frame_time_avg_ms=%.3f\n", avg_ms);
	ps3_mem_diag_print("frame_time_min_ms=%.3f\n", min_ms);
	ps3_mem_diag_print("frame_time_max_ms=%.3f\n", max_ms);
	ps3_mem_diag_print("user_memory_initial=%lu\n", g_ps3_bench.initial_available_user_memory);
	ps3_mem_diag_print("user_memory_final=%lu\n", final_user_memory);
	ps3_mem_diag_print("user_memory_consumed=%ld\n", memory_consumed);
	ps3_mem_diag_print("pool_total=%lu\n", (unsigned long)mem.pool_total);
	ps3_mem_diag_print("pool_used=%lu\n", (unsigned long)mem.pool_used);
	ps3_mem_diag_print("pool_peak=%lu\n", (unsigned long)mem.pool_peak);
	ps3_mem_diag_print("system_live=%lu\n", (unsigned long)mem.system_live);
	ps3_mem_diag_print("system_peak=%lu\n", (unsigned long)mem.system_peak);
	ps3_mem_diag_print("native_live=%lu\n", (unsigned long)mem.native_live);
	ps3_mem_diag_print("native_peak=%lu\n", (unsigned long)mem.native_peak);
	ps3_mem_diag_print("tracked_live_peak=%lu\n", (unsigned long)mem.tracked_live_peak);
	ps3_mem_diag_print("fallback_count=%lu\n", (unsigned long)mem.fallback_count);
	ps3_mem_diag_print("allocation_failures=%lu\n", (unsigned long)mem.allocation_failure_count);
	ps3_mem_diag_print("[PS3 BENCHMARK REPORT END]\n");

	// Also write standalone report file
	int rfd = open(PS3_BENCHMARK_REPORT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (rfd >= 0) {
		char rpt[2048];
		int n = snprintf(rpt, sizeof(rpt),
			"[PS3 BENCHMARK REPORT BEGIN]\n"
			"game=%s\n"
			"driver=%s\n"
			"hardware=%s\n"
			"total_frames=%u\n"
			"benchmarked_frames=%u\n"
			"wall_time_us=%llu\n"
			"wall_fps=%.2f\n"
			"emulation_time_us=%llu\n"
			"emulation_fps=%.2f\n"
			"frame_time_avg_ms=%.3f\n"
			"frame_time_min_ms=%.3f\n"
			"frame_time_max_ms=%.3f\n"
			"user_memory_initial=%lu\n"
			"user_memory_final=%lu\n"
			"user_memory_consumed=%ld\n"
			"pool_total=%lu\n"
			"pool_used=%lu\n"
			"pool_peak=%lu\n"
			"system_live=%lu\n"
			"system_peak=%lu\n"
			"native_live=%lu\n"
			"native_peak=%lu\n"
			"tracked_live_peak=%lu\n"
			"fallback_count=%lu\n"
			"allocation_failures=%lu\n"
			"phase_cpu_ms=%u\n"
			"phase_sound_ms=%u\n"
			"phase_blit_ms=%u\n"
			"phase_video_ms=%u\n"
			"phase_retrorun_ms=%u\n"
			"phase_cpu_total_ms=%u\n"
			"phase_total_ms=%u\n"
			"[PS3 BENCHMARK REPORT END]\n",
			g_driver_name,
			BurnDrvGetTextA(DRV_FULLNAME) ? BurnDrvGetTextA(DRV_FULLNAME) : "unknown",
			hw_str,
			frame + 1,
			b_frames,
			bench_wall_us,
			wall_fps,
			total_emu_us,
			emu_fps,
			avg_ms,
			min_ms,
			max_ms,
			g_ps3_bench.initial_available_user_memory,
			final_user_memory,
			memory_consumed,
			(unsigned long)mem.pool_total,
			(unsigned long)mem.pool_used,
			(unsigned long)mem.pool_peak,
			(unsigned long)mem.system_live,
			(unsigned long)mem.system_peak,
			(unsigned long)mem.native_live,
			(unsigned long)mem.native_peak,
			(unsigned long)mem.tracked_live_peak,
			(unsigned long)mem.fallback_count,
			(unsigned long)mem.allocation_failure_count,
			(unsigned)(g_ps3_bench.phase_us[0] / 1000ULL),
			(unsigned)(g_ps3_bench.phase_us[1] / 1000ULL),
			(unsigned)(g_ps3_bench.phase_us[2] / 1000ULL),
			(unsigned)(g_ps3_bench.phase_us[3] / 1000ULL),
			(unsigned)(g_ps3_bench.phase_us[4] / 1000ULL),
			(unsigned)(g_ps3_bench.phase_us[0] / 1000ULL),
			(unsigned)((g_ps3_bench.phase_us[0] + g_ps3_bench.phase_us[1] + g_ps3_bench.phase_us[2] + g_ps3_bench.phase_us[3]) / 1000ULL)
		);
		if (n > 0) write(rfd, rpt, (size_t)n);
		close(rfd);
	}

	ps3_mem_diag_summary("benchmark_completed");

	if (g_ps3_bench.cfg.exit_after_frames) {
		static int s_exit_counter = 0;
		if (s_exit_counter == 0) {
			s_exit_counter = 1;
			ps3_mem_diag_print("[PS3 BENCHMARK] requesting frontend clean shutdown\n");
			environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
		} else {
			s_exit_counter++;
			if (s_exit_counter > 20) {
				ps3_mem_diag_print("[PS3 BENCHMARK] frontend shutdown fallback -> sys_process_exit\n");
#if defined(__PS3__) && !defined(__PSL1GHT__)
				sys_process_exit(0);
#endif
			}
		}
		return 1;
	}

	return 0;
}

