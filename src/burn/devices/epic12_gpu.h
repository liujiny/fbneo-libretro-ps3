// CV1000 GPU command batching and RSX integration for PS3.
#ifndef FBNEO_EPIC12_GPU_H
#define FBNEO_EPIC12_GPU_H

#include "epic12_gpu_batch.h"
#include "epic12_staging_columns.h"

#ifdef __PS3__
#include "epic12_gpu_ps3.h"
#endif

#define EPIC12_GPU_BINS 1
#define EPIC12_VRAM_PAGES 2176

static Epic12GpuCommand epic12_gpu_commands[EPIC12_GPU_BINS][4096];
static int epic12_gpu_count[EPIC12_GPU_BINS];
static unsigned epic12_gpu_pixels[EPIC12_GPU_BINS];
static bool epic12_gpu_source_pages[EPIC12_GPU_BINS][EPIC12_VRAM_PAGES];
static rectangle epic12_gpu_bounds[EPIC12_GPU_BINS];
static rectangle epic12_gpu_source_bounds[EPIC12_GPU_BINS];
static bool epic12_gpu_enabled = false;

enum Epic12GpuFlushReason {
	EPIC12_GPU_FLUSH_END_LIST = 0,
	EPIC12_GPU_FLUSH_SOURCE_PAGES,
	EPIC12_GPU_FLUSH_REGION,
	EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY,
	EPIC12_GPU_FLUSH_COMMAND_LIMIT,
	EPIC12_GPU_FLUSH_CPU_UPLOAD,
	EPIC12_GPU_FLUSH_CPU_DRAW,
	EPIC12_GPU_FLUSH_OTHER
};

static void epic12_gpu_replay(const Epic12GpuCommand &c)
{
	rectangle clip(c.x, c.x + c.w - 1, c.y, c.y + c.h - 1);
#define GPU_CPU_ARGS &clip, m_bitmaps, c.sx, c.sy, c.x, c.y, c.w, c.h, c.flipy, c.sa, c.da, &c.tint

	if (!c.blend && c.tint.r == 32 && c.tint.g == 32 && c.tint.b == 32) {
		if (c.flipx) {
			if (c.transparent) draw_sprite_f1_ti0_tr1_simple(GPU_CPU_ARGS);
			else draw_sprite_f1_ti0_tr0_simple(GPU_CPU_ARGS);
		} else {
			if (c.transparent) draw_sprite_f0_ti0_tr1_simple(GPU_CPU_ARGS);
			else draw_sprite_f0_ti0_tr0_simple(GPU_CPU_ARGS);
		}
		return;
	}

	const int tinted = (c.tint.r != 32) | (c.tint.g != 32) | (c.tint.b != 32);
	if (c.blend && (c.smode == 1 || c.smode == 2 || c.smode == 6)) {
#define GPU_CPU_SOURCE_DRAW(f,t,s,d) \
		if (tinted) draw_sprite_f##f##_ti1_tr##t##_s##s##_d##d(GPU_CPU_ARGS); \
		else draw_sprite_f##f##_ti0_tr##t##_s##s##_d##d(GPU_CPU_ARGS)
#define GPU_CPU_SOURCE_MODE(f,t,s) \
		if (c.dmode == 0) { GPU_CPU_SOURCE_DRAW(f,t,s,0); } \
		else if (c.dmode == 3) { GPU_CPU_SOURCE_DRAW(f,t,s,3); } \
		else { GPU_CPU_SOURCE_DRAW(f,t,s,7); }
#define GPU_CPU_SOURCE_CASE(f,t) \
		if (c.smode == 1) { GPU_CPU_SOURCE_MODE(f,t,1); } \
		else if (c.smode == 2) { GPU_CPU_SOURCE_MODE(f,t,2); } \
		else { GPU_CPU_SOURCE_MODE(f,t,6); }
		if (c.flipx) { if (c.transparent) { GPU_CPU_SOURCE_CASE(1,1); } else { GPU_CPU_SOURCE_CASE(1,0); } }
		else { if (c.transparent) { GPU_CPU_SOURCE_CASE(0,1); } else { GPU_CPU_SOURCE_CASE(0,0); } }
#undef GPU_CPU_SOURCE_CASE
#undef GPU_CPU_SOURCE_MODE
#undef GPU_CPU_SOURCE_DRAW
		return;
	}

#define GPU_CPU_CASE(f,t) \
	if (!c.blend) draw_sprite_f##f##_ti1_tr##t##_plain(GPU_CPU_ARGS); \
	else if (c.dmode == 1) draw_sprite_f##f##_ti1_tr##t##_s0_d1(GPU_CPU_ARGS); \
	else if (c.dmode == 7) draw_sprite_f##f##_ti1_tr##t##_s0_d7(GPU_CPU_ARGS); \
	else if (c.dmode == 3) draw_sprite_f##f##_ti1_tr##t##_s0_d3(GPU_CPU_ARGS); \
	else draw_sprite_f##f##_ti1_tr##t##_s0_d4(GPU_CPU_ARGS)

	if (c.flipx) {
		if (c.transparent) { GPU_CPU_CASE(1,1); }
		else { GPU_CPU_CASE(1,0); }
	} else {
		if (c.transparent) { GPU_CPU_CASE(0,1); }
		else { GPU_CPU_CASE(0,0); }
	}
#undef GPU_CPU_CASE
#undef GPU_CPU_ARGS
}

static void epic12_gpu_flush_bin(int b, Epic12GpuFlushReason reason)
{
	if (!epic12_gpu_count[b]) return;

	bool rendered = false;
#ifdef __PS3__
	rendered = epic12_gpu_render(epic12_gpu_commands[b], epic12_gpu_count[b], epic12_gpu_bounds[b]);
#endif

	if (!rendered) {
		UINT64 delay = epic12_device_blit_delay;
		for (int i = 0; i < epic12_gpu_count[b]; ++i) {
			epic12_gpu_replay(epic12_gpu_commands[b][i]);
		}
		epic12_device_blit_delay = delay;
	}

#ifdef __PS3__
	epic12_ps3_gpu_invalidate(epic12_gpu_bounds[b]);
#endif

	epic12_gpu_count[b] = 0;
	epic12_gpu_pixels[b] = 0;
	memset(epic12_gpu_source_pages[b], 0, sizeof(epic12_gpu_source_pages[b]));
	epic12_gpu_bounds[b].set(8192, -1, 4096, -1);
	epic12_gpu_source_bounds[b].set(8192, -1, 4096, -1);
}

static inline void epic12_gpu_flush(Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	epic12_gpu_flush_bin(0, reason);
}

static inline void epic12_gpu_reset()
{
	epic12_gpu_count[0] = 0;
	epic12_gpu_pixels[0] = 0;
	memset(epic12_gpu_source_pages[0], 0, sizeof(epic12_gpu_source_pages[0]));
	epic12_gpu_bounds[0].set(8192, -1, 4096, -1);
	epic12_gpu_source_bounds[0].set(8192, -1, 4096, -1);
#ifdef __PS3__
	epic12_ps3_gpu_reset();
#endif
}

static inline void epic12_gpu_exit()
{
	epic12_gpu_reset();
}

static inline void epic12_gpu_diag_reset()
{
	epic12_gpu_reset();
}

static inline void epic12_gpu_cpu_write(int x, int y, int w, int h,
	Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	if (x < 0 || y < 0 || x + w > 8192 || y + h > 4096) {
		epic12_gpu_flush(reason);
		epic12_gpu_reset();
		return;
	}
	if (epic12_gpu_count[0]) {
		if (epic12_gpu_intersects(x, y, w, h, epic12_gpu_bounds[0]) ||
		    epic12_gpu_intersects(x, y, w, h, epic12_gpu_source_bounds[0])) {
			epic12_gpu_flush(reason);
		}
	}
#ifdef __PS3__
	rectangle r(x, x + w - 1, y, y + h - 1);
	epic12_ps3_gpu_invalidate(r);
#endif
}

static inline void epic12_gpu_cpu_draw(int x, int y, int w, int h, const rectangle &clip)
{
	int x0 = x < clip.min_x ? clip.min_x : x;
	int y0 = y < clip.min_y ? clip.min_y : y;
	int x1 = (x + w - 1) > clip.max_x ? clip.max_x : (x + w - 1);
	int y1 = (y + h - 1) > clip.max_y ? clip.max_y : (y + h - 1);
	if (x1 >= x0 && y1 >= y0) {
		epic12_gpu_cpu_write(x0, y0, x1 - x0 + 1, y1 - y0 + 1, EPIC12_GPU_FLUSH_CPU_DRAW);
	}
}

static bool epic12_gpu_submit(int flipx, int transparent, int blend, int smode, int dmode, BLIT_PARAMS)
{
	if (!epic12_gpu_enabled) return false;

	int sm = smode, dm = dmode, sa = s_alpha, da = d_alpha;
	if (blend) {
		if (dm == 4) {
			dm = 0;
			da = 31 - da;
		}
		if (sm == 3 && (dm == 0 || dm == 3 || dm == 7)) {
			sm = 0; sa = 31;
		} else if (sm == 4 && (dm == 0 || dm == 3 || dm == 7)) {
			sm = 0; sa = 31 - sa;
		} else if (sm == 7 && (dm == 0 || dm == 3 || dm == 7)) {
			sm = 0; sa = 31;
		} else if (sm == 1 && (dm == 0 || dm == 3 || dm == 7)) {
			sa = 31;
		}
	}

	const bool smode6_valid = (sm == 6 && (dm == 0 || dm == 3 || dm == 7));
	const bool smode2_valid = (sm == 2 && (dm == 0 || dm == 3 || dm == 7));
	const bool smode1_valid = (sm == 1 && (dm == 0 || dm == 3 || dm == 7));
	const bool smode0_valid = (sm == 0 && (dm == 0 || dm == 1 || dm == 4 || dm == 7 || dm == 3));
	if (blend && !smode0_valid && !smode1_valid && !smode2_valid && !smode6_valid) {
		return false;
	}

	if (src_x + dimx > 8192) return true;

	int x, y, w, h, sx, sy;
	if (dst_x_start >= clip->min_x && dst_y_start >= clip->min_y &&
	    dst_x_start + dimx - 1 <= clip->max_x && dst_y_start + dimy - 1 <= clip->max_y) {
		x = dst_x_start; y = dst_y_start;
		w = dimx; h = dimy;
		sx = src_x; sy = (src_y) & 4095;
	} else {
		int x0 = dst_x_start < clip->min_x ? clip->min_x - dst_x_start : 0;
		int y0 = dst_y_start < clip->min_y ? clip->min_y - dst_y_start : 0;
		int x1 = dimx, y1 = dimy;
		if (dst_x_start + x1 - 1 > clip->max_x) x1 = clip->max_x - dst_x_start + 1;
		if (dst_y_start + y1 - 1 > clip->max_y) y1 = clip->max_y - dst_y_start + 1;
		if (x1 <= x0 || y1 <= y0) return true;
		w = x1 - x0; h = y1 - y0;
		x = dst_x_start + x0; y = dst_y_start + y0;
		sx = src_x + (flipx ? dimx - x1 : x0);
		sy = (src_y + (flipy ? dimy - y1 : y0)) & 4095;
	}

	if (w > 512 || h > 512 || x < 0 || y < 0 || x + w > 8192 || y + h > 4096 || sy + h > 4096) {
		return false;
	}

	// Reject off-screen staging buffers (x >= 768) - staging stream handles them on CPU
	if (x >= 768) return false;

	const rectangle dest(x, x + w - 1, y, y + h - 1);
	if (epic12_gpu_intersects(sx, sy, w, h, dest)) {
		return false; // in-place overlap
	}

	// Check RAW hazard against pending GPU writes
	if (epic12_gpu_count[0] && epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds[0])) {
		epic12_gpu_flush(EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
	}

	// Check bounding box expansion (limit batch to 512x512)
	rectangle next_bounds = epic12_gpu_bounds[0];
	if (epic12_gpu_count[0] == 0) {
		next_bounds = dest;
	} else {
		if (dest.min_x < next_bounds.min_x) next_bounds.min_x = dest.min_x;
		if (dest.min_y < next_bounds.min_y) next_bounds.min_y = dest.min_y;
		if (dest.max_x > next_bounds.max_x) next_bounds.max_x = dest.max_x;
		if (dest.max_y > next_bounds.max_y) next_bounds.max_y = dest.max_y;
	}

	if ((next_bounds.max_x - next_bounds.min_x + 1) > 512 ||
	    (next_bounds.max_y - next_bounds.min_y + 1) > 512 ||
	    epic12_gpu_count[0] >= 4090) {
		epic12_gpu_flush(EPIC12_GPU_FLUSH_REGION);
		next_bounds = dest;
	}

	// Update bounds
	epic12_gpu_bounds[0] = next_bounds;

	if (epic12_gpu_count[0] == 0) {
		epic12_gpu_source_bounds[0].set(sx, sx + w - 1, sy, sy + h - 1);
	} else {
		if (sx < epic12_gpu_source_bounds[0].min_x) epic12_gpu_source_bounds[0].min_x = sx;
		if (sy < epic12_gpu_source_bounds[0].min_y) epic12_gpu_source_bounds[0].min_y = sy;
		if (sx + w - 1 > epic12_gpu_source_bounds[0].max_x) epic12_gpu_source_bounds[0].max_x = sx + w - 1;
		if (sy + h - 1 > epic12_gpu_source_bounds[0].max_y) epic12_gpu_source_bounds[0].max_y = sy + h - 1;
	}

	// Mark source pages
	int spx0 = sx >> 7, spx1 = (sx + w - 1) >> 7;
	int spy0 = sy >> 7, spy1 = (sy + h - 1) >> 7;
	for (int py = spy0; py <= spy1; ++py) {
		int row = py << 6;
		for (int px = spx0; px <= spx1; ++px) {
			epic12_gpu_source_pages[0][row | px] = true;
		}
	}

	// Enqueue command
	int idx = epic12_gpu_count[0]++;
	Epic12GpuCommand &c = epic12_gpu_commands[0][idx];
	c.sx = sx; c.sy = sy;
	c.x = x; c.y = y;
	c.w = w; c.h = h;
	c.flipx = !!flipx; c.flipy = !!flipy;
	c.transparent = !!transparent;
	c.blend = !!blend;
	c.smode = sm; c.dmode = dm;
	c.sa = sa; c.da = da;
	c.tint = *tint_clr;

	epic12_gpu_pixels[0] += (unsigned)(w * h);
	epic12_device_blit_delay += (unsigned)(w * h);

	return true;
}

#endif // FBNEO_EPIC12_GPU_H
