#ifndef FBNEO_PS3_BENCHMARK_H
#define FBNEO_PS3_BENCHMARK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int enabled;
	unsigned max_frames;
	unsigned warmup_frames;
	int auto_coin;
	unsigned coin_frame;
	unsigned coin_duration;
	unsigned coin_retry_interval;
	int auto_start;
	unsigned start_frame;
	unsigned start_duration;
	unsigned start_retry_interval;
	unsigned start_retry_count;
	int auto_action;
	unsigned action_start_frame;
	unsigned action_interval;
	int exit_after_frames;
	unsigned log_window_frames;
	unsigned save_state_frame;   /* 0 = disabled */
	int load_state;              /* restore the saved state at startup */
} ps3_benchmark_config_t;

typedef struct {
	int active;
	ps3_benchmark_config_t cfg;
	unsigned current_frame;
	unsigned benchmarked_frames;
	unsigned long long wall_start_us;
	unsigned long long wall_bench_start_us;
	unsigned long long wall_end_us;
	unsigned long long frame_start_us;
	unsigned long long total_emulation_time_us;
	unsigned long long min_frame_us;
	unsigned long long max_frame_us;
	unsigned long long window_time_us;
	unsigned window_frames;
	uint8_t *p_coin;
	uint8_t *p_start;
	uint8_t *p_btn1;
	uint8_t *p_btn2;
	uint8_t *p_btn3;
	uint8_t *p_up;
	uint8_t *p_down;
	uint8_t *p_left;
	uint8_t *p_right;
	unsigned long initial_available_user_memory;
	int memory_valid;
	unsigned image_seen;   /* first frame that actually contains video */
	unsigned image_frame;
	/* per phase timing: 0=cpu 1=sound 2=blit 3=video 4=other */
	unsigned long long phase_us[8];
	unsigned long long phase_start_us[8];
	int phase_active[8];
} ps3_benchmark_state_t;

extern ps3_benchmark_state_t g_ps3_bench;

void ps3_benchmark_init(void);
void ps3_benchmark_input_step(unsigned frame);
void ps3_benchmark_state_restore(void);
void ps3_benchmark_state_tick(unsigned frame);
void ps3_benchmark_note_image(unsigned frame, const void *buf, unsigned w, unsigned h, size_t pitch);
void ps3_benchmark_frame_before(unsigned frame);
void ps3_benchmark_frame_after(unsigned frame);
int  ps3_benchmark_check_exit(unsigned frame);

void ps3_bench_phase_begin(int idx);
void ps3_bench_phase_end(int idx);

#ifdef __cplusplus
}
#endif

#endif // FBNEO_PS3_BENCHMARK_H
