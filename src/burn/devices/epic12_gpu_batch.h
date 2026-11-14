// Shared GPU submission planner and atlas geometry for FBNeo PS3.
#ifndef FBNEO_EPIC12_GPU_BATCH_H
#define FBNEO_EPIC12_GPU_BATCH_H

#include <stdint.h>
#include <string.h>

struct Epic12GpuCommand {
	int sx, sy, x, y, w, h;
	int flipx, flipy, transparent, blend, smode, dmode, sa, da;
	clr_t tint;
};

static inline int epic12_gpu_dest_mode(const Epic12GpuCommand &c)
{
	if (c.smode == 6 && (c.dmode == 0 || c.dmode == 3 || c.dmode == 4 || c.dmode == 7)) return 4;
	if (c.smode == 2 && (c.dmode == 0 || c.dmode == 2 || c.dmode == 3 || c.dmode == 7)) return 2;
	if (c.smode == 1 && (c.dmode == 0 || c.dmode == 3 || c.dmode == 7)) return 3;
	return (c.dmode == 4 || c.dmode == 7 || c.dmode == 3) ? 0 : c.dmode;
}

static inline int epic12_gpu_dest_alpha(const Epic12GpuCommand &c)
{
	if (c.smode == 6) return (c.dmode == 7 || c.dmode == 3) ? 31 : c.da;
	if (c.dmode == 7 || c.dmode == 3) return 31;
	return c.dmode == 4 ? 31 - c.da : c.da;
}

static inline bool epic12_gpu_additive(const Epic12GpuCommand &c)
{
	return c.blend && (epic12_gpu_dest_mode(c) == 0 || epic12_gpu_dest_mode(c) == 3) && epic12_gpu_dest_alpha(c) == 31;
}

static inline bool epic12_gpu_feedback(const Epic12GpuCommand &c)
{
	return c.blend && (epic12_gpu_dest_mode(c) == 2 || epic12_gpu_dest_mode(c) == 4 || (epic12_gpu_dest_mode(c) == 1 && c.sa != 0));
}

static inline int epic12_gpu_blend_state(const Epic12GpuCommand &c)
{
	if (!c.blend) return 0;
	if (epic12_gpu_dest_mode(c) == 1 && c.sa == 0) return 200;
	int dm = epic12_gpu_dest_mode(c);
	if (dm == 1 || dm == 2 || dm == 4) return 100;
	int da = epic12_gpu_dest_alpha(c);
	if (da == 0) return 0;
	if (da == 31) return (dm == 3) ? 50 : 1;
	return (dm == 3 ? 60 : 2) + da;
}

static inline int epic12_gpu_command_blend_type(const Epic12GpuCommand &c)
{
	return epic12_gpu_blend_state(c);
}

static inline bool epic12_gpu_intersects(int x, int y, int w, int h, const rectangle &r)
{
	return x <= r.max_x && x + w > r.min_x && y <= r.max_y && y + h > r.min_y;
}

static inline rectangle epic12_gpu_resolve_rect(const Epic12GpuCommand &c, const rectangle &bounds)
{
	int x = c.x - bounds.min_x, y = c.y - bounds.min_y;
	return rectangle(x & ~7, ((x + c.w + 7) & ~7) - 1,
		y & ~7, ((y + c.h + 7) & ~7) - 1);
}

static inline unsigned epic12_gpu_rect_area(const rectangle &r)
{
	return (unsigned)((r.max_x - r.min_x + 1) * (r.max_y - r.min_y + 1));
}

static inline bool epic12_gpu_same_state(const Epic12GpuCommand &a, const Epic12GpuCommand &b)
{
	if (a.transparent != b.transparent || a.blend != b.blend ||
		a.tint.r != b.tint.r || a.tint.g != b.tint.g || a.tint.b != b.tint.b) return false;
	if (!a.blend) return true;
	if (a.sa != b.sa || epic12_gpu_dest_mode(a) != epic12_gpu_dest_mode(b)) return false;
	return epic12_gpu_dest_mode(a) == 1 || epic12_gpu_dest_alpha(a) == epic12_gpu_dest_alpha(b);
}

static int epic12_gpu_group_end(const Epic12GpuCommand *cmd, int first, int count,
	const rectangle &bounds, rectangle &snapshot, bool attributes = true)
{
	const Epic12GpuCommand &head = cmd[first];
	bool feedback = epic12_gpu_feedback(head);
	int headBlend = attributes ? epic12_gpu_blend_state(head) : 0;
	snapshot = epic12_gpu_resolve_rect(head, bounds);
	unsigned area = epic12_gpu_rect_area(snapshot);
	int end = first + 1;
	for (; end < count; ++end) {
		const Epic12GpuCommand &c = cmd[end];
		if (attributes) {
			if (headBlend != epic12_gpu_blend_state(c)) break;
		} else if (!epic12_gpu_same_state(head, c)) break;
		if (!feedback) continue;
		if (end - first == 64) break;
		rectangle dest(c.x, c.x + c.w - 1, c.y, c.y + c.h - 1);
		bool overlap = false;
		for (int j = first; j < end; ++j) {
			const Epic12GpuCommand &p = cmd[j];
			if (epic12_gpu_intersects(p.x, p.y, p.w, p.h, dest)) { overlap = true; break; }
		}
		if (overlap) break;
		rectangle r = epic12_gpu_resolve_rect(c, bounds), merged = snapshot;
		if (r.min_x < merged.min_x) merged.min_x = r.min_x;
		if (r.min_y < merged.min_y) merged.min_y = r.min_y;
		if (r.max_x > merged.max_x) merged.max_x = r.max_x;
		if (r.max_y > merged.max_y) merged.max_y = r.max_y;
		unsigned nextArea = area + epic12_gpu_rect_area(r);
		if (epic12_gpu_rect_area(merged) > nextArea * 2) break;
		snapshot = merged; area = nextArea;
	}
	return end;
}

static int epic12_gpu_snapshot_end(const Epic12GpuCommand *cmd, int first,
	int groupEnd, int count, const rectangle &bounds, rectangle &snapshot,
	bool attributes = true)
{
	if (!epic12_gpu_feedback(cmd[first])) return groupEnd;
	int limit = first + 32; if (limit > count) limit = count;
	int coveredEnd = groupEnd, scan = groupEnd;
	unsigned sumArea = epic12_gpu_rect_area(snapshot);
	while (scan < limit) {
		while (scan < limit && !epic12_gpu_feedback(cmd[scan])) ++scan;
		if (scan >= limit) break;
		rectangle next;
		int end = epic12_gpu_group_end(cmd, scan, count, bounds, next, attributes);
		if (end > limit) break;
		bool overlap = false;
		for (int i = scan; i < end && !overlap; ++i) {
			const Epic12GpuCommand &c = cmd[i];
			rectangle dest(c.x, c.x + c.w - 1, c.y, c.y + c.h - 1);
			for (int j = first; j < scan; ++j) {
				const Epic12GpuCommand &p = cmd[j];
				if (epic12_gpu_intersects(p.x, p.y, p.w, p.h, dest)) { overlap = true; break; }
			}
		}
		if (overlap) break;
		rectangle merged = snapshot;
		if (next.min_x < merged.min_x) merged.min_x = next.min_x;
		if (next.min_y < merged.min_y) merged.min_y = next.min_y;
		if (next.max_x > merged.max_x) merged.max_x = next.max_x;
		if (next.max_y > merged.max_y) merged.max_y = next.max_y;
		unsigned nextArea = sumArea + epic12_gpu_rect_area(next);
		if (epic12_gpu_rect_area(merged) > nextArea * 2) break;
		snapshot = merged; sumArea = nextArea; coveredEnd = scan = end;
	}
	return coveredEnd;
}

static unsigned epic12_gpu_reorder(const Epic12GpuCommand *input, int count,
	Epic12GpuCommand *output)
{
	if (input != output) {
		memcpy(output, input, count * sizeof(Epic12GpuCommand));
	}
	unsigned moved = 0;
	for (int pos = 1; pos < count; ++pos) {
		const Epic12GpuCommand &previous = output[pos - 1];
		int want = epic12_gpu_command_blend_type(previous);
		const Epic12GpuCommand &current = output[pos];
		int currentType = epic12_gpu_command_blend_type(current);
		if (currentType == want) continue;
		int limit = pos + 8; if (limit > count) limit = count;
		for (int candidate = pos + 1; candidate < limit; ++candidate) {
			const Epic12GpuCommand &c = output[candidate];
			int type = epic12_gpu_command_blend_type(c);
			if (type != want) continue;
			rectangle dest(c.x, c.x + c.w - 1, c.y, c.y + c.h - 1);
			bool overlap = false;
			for (int i = pos; i < candidate; ++i) {
				const Epic12GpuCommand &p = output[i];
				if (epic12_gpu_intersects(p.x, p.y, p.w, p.h, dest)) { overlap = true; break; }
			}
			if (overlap) continue;
			Epic12GpuCommand saved = c;
			memmove(&output[pos + 1], &output[pos], (candidate - pos) * sizeof(Epic12GpuCommand));
			output[pos] = saved;
			++moved;
			break;
		}
	}
	return moved;
}

// 4 vertices per quad (indexed drawing with standard 0,1,2, 2,1,3 triangles)
struct Epic12GpuVertex {
	float x, y;
	float u, v;
	float tint[4]; // r, g, b, sa
	float mode[4]; // transparent, blend, dmode, da
};

static const int EPIC12_GPU_DRAW_RECTS = 256;

static inline int epic12_gpu_vertex_count(const Epic12GpuCommand &c)
{
	return (((c.sx + c.w - 1) >> 7) - (c.sx >> 7) + 1) *
		(((c.sy + c.h - 1) >> 7) - (c.sy >> 7) + 1) * 4;
}

// Emits 4 vertices per quad: TL (0), TR (1), BL (2), BR (3)
static inline void epic12_gpu_emit_quad(float dx, float dy, float dw, float dh,
	float u0, float v0, float u1, float v1,
	float tr, float tg, float tb, float tsa,
	float mtrans, float mblend, float mdmode, float mda,
	Epic12GpuVertex *out)
{
	// TL
	out[0].x = dx;      out[0].y = dy;
	out[0].u = u0;      out[0].v = v0;
	out[0].tint[0] = tr; out[0].tint[1] = tg; out[0].tint[2] = tb; out[0].tint[3] = tsa;
	out[0].mode[0] = mtrans; out[0].mode[1] = mblend; out[0].mode[2] = mdmode; out[0].mode[3] = mda;

	// TR
	out[1].x = dx + dw; out[1].y = dy;
	out[1].u = u1;      out[1].v = v0;
	out[1].tint[0] = tr; out[1].tint[1] = tg; out[1].tint[2] = tb; out[1].tint[3] = tsa;
	out[1].mode[0] = mtrans; out[1].mode[1] = mblend; out[1].mode[2] = mdmode; out[1].mode[3] = mda;

	// BL
	out[2].x = dx;      out[2].y = dy + dh;
	out[2].u = u0;      out[2].v = v1;
	out[2].tint[0] = tr; out[2].tint[1] = tg; out[2].tint[2] = tb; out[2].tint[3] = tsa;
	out[2].mode[0] = mtrans; out[2].mode[1] = mblend; out[2].mode[2] = mdmode; out[2].mode[3] = mda;

	// BR
	out[3].x = dx + dw; out[3].y = dy + dh;
	out[3].u = u1;      out[3].v = v1;
	out[3].tint[0] = tr; out[3].tint[1] = tg; out[3].tint[2] = tb; out[3].tint[3] = tsa;
	out[3].mode[0] = mtrans; out[3].mode[1] = mblend; out[3].mode[2] = mdmode; out[3].mode[3] = mda;
}

static int epic12_gpu_vertices(const Epic12GpuCommand &c, const rectangle &bounds,
	const int *slots, Epic12GpuVertex *out)
{
	const float tr = (float)c.tint.r, tg = (float)c.tint.g, tb = (float)c.tint.b, tsa = (float)c.sa;
	const float mtrans = (float)c.transparent, mblend = (float)c.blend;
	const float mdmode = (float)epic12_gpu_dest_mode(c), mda = (float)epic12_gpu_dest_alpha(c);

	const int spx0 = c.sx >> 7, spx1 = (c.sx + c.w - 1) >> 7;
	const int spy0 = c.sy >> 7, spy1 = (c.sy + c.h - 1) >> 7;

	// Single-page fast path (>95% of all sprites)
	if (spx0 == spx1 && spy0 == spy1) {
		int slot = slots[(spy0 << 6) | spx0];
		float u0 = (float)((slot & 15) * 128 + (c.sx & 127)), u1 = u0 + (float)c.w;
		float v0 = (float)((slot >> 4) * 128 + (c.sy & 127)), v1 = v0 + (float)c.h;
		float dx = (float)(c.x - bounds.min_x);
		float dy = (float)(c.y - bounds.min_y);
		epic12_gpu_emit_quad(dx, dy, (float)c.w, (float)c.h,
			c.flipx ? u1 : u0, c.flipy ? v1 : v0,
			c.flipx ? u0 : u1, c.flipy ? v0 : v1,
			tr, tg, tb, tsa, mtrans, mblend, mdmode, mda, out);
		return 4;
	}

	// Multi-page slicing
	int n = 0;
	for (int sy = c.sy; sy < c.sy + c.h;) {
		int ey = ((sy >> 7) + 1) * 128; if (ey > c.sy + c.h) ey = c.sy + c.h;
		int slot_row = (sy >> 7) * 64;
		int dy = c.y + (c.flipy ? c.h - (ey - c.sy) : sy - c.sy) - bounds.min_y;
		int h_span = ey - sy;
		for (int sx = c.sx; sx < c.sx + c.w;) {
			int ex = ((sx >> 7) + 1) * 128; if (ex > c.sx + c.w) ex = c.sx + c.w;
			int slot = slots[slot_row + (sx >> 7)];
			float u0 = (float)((slot & 15) * 128 + (sx & 127)), u1 = u0 + (float)(ex - sx);
			float v0 = (float)((slot >> 4) * 128 + (sy & 127)), v1 = v0 + (float)h_span;
			float dx = (float)(c.x + (c.flipx ? c.w - (ex - c.sx) : sx - c.sx) - bounds.min_x);
			float w_span = (float)(ex - sx);
			epic12_gpu_emit_quad(dx, (float)dy, w_span, (float)h_span,
				c.flipx ? u1 : u0, c.flipy ? v1 : v0,
				c.flipx ? u0 : u1, c.flipy ? v0 : v1,
				tr, tg, tb, tsa, mtrans, mblend, mdmode, mda, out + n);
			n += 4;
			sx = ex;
		}
		sy = ey;
	}
	return n;
}

#endif // FBNEO_EPIC12_GPU_BATCH_H
