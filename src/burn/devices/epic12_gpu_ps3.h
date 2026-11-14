// PSGL / RSX GPU Hardware Rasterizer for Cave CV1000 on PlayStation 3
#ifndef FBNEO_EPIC12_GPU_PS3_H
#define FBNEO_EPIC12_GPU_PS3_H

#ifdef __PS3__

#include <PSGL/psgl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>

#include "epic12_gpu_batch.h"
#include "epic12_gpu_cache.h"
#include "epic12_gpu_shaders.h"

// Forward declarations of PS3 VRAM access functions from epic12.cpp
static inline UINT16* epic12_get_read_row(UINT32 y);
static inline UINT16* epic12_get_write_row(UINT32 y);
static inline UINT32 epic12_in16_to_argb(UINT16 v);
static inline UINT16 epic12_argb_to_in16(UINT32 p);

static bool g_ps3_gpu_inited = false;
static bool g_ps3_gpu_failed = false;

static CGcontext g_cg_ctx = NULL;
static CGprogram g_vprg = NULL;
static CGprogram g_fprg_fast = NULL;
static CGprogram g_fprg_feedback = NULL;

static CGparameter g_param_p = NULL;
static CGparameter g_param_uv = NULL;
static CGparameter g_param_tint = NULL;
static CGparameter g_param_mode = NULL;
static CGparameter g_param_atlasScale = NULL;

static GLuint g_atlas_tex = 0;
static GLuint g_target_tex = 0;
static GLuint g_snapshot_tex = 0;
static GLuint g_fbo = 0;

static Epic12GpuPageCache g_page_cache;

static Epic12GpuVertex g_vertices[EPIC12_GPU_DRAW_RECTS * 4];
static UINT16 g_indices[EPIC12_GPU_DRAW_RECTS * 6];

// Scratch buffers for uploads and readbacks
static UINT32 g_page_scratch[128 * 128];
static UINT32 g_dest_scratch[512 * 512];
static UINT32 g_readback_scratch[512 * 512];

static void epic12_ps3_init_indices()
{
	for (int i = 0; i < EPIC12_GPU_DRAW_RECTS; ++i) {
		UINT16 base = (UINT16)(i * 4);
		g_indices[i * 6 + 0] = base + 0;
		g_indices[i * 6 + 1] = base + 1;
		g_indices[i * 6 + 2] = base + 2;
		g_indices[i * 6 + 3] = base + 2;
		g_indices[i * 6 + 4] = base + 1;
		g_indices[i * 6 + 5] = base + 3;
	}
}

static bool epic12_ps3_gpu_init()
{
	if (g_ps3_gpu_inited) return true;
	if (g_ps3_gpu_failed) return false;

	PSGLcontext *current_ctx = psglGetCurrentContext();
	if (!current_ctx) {
		bprintf(0, _T("[EPIC12-RSX] Error: No current PSGL context found!\n"));
		g_ps3_gpu_failed = true;
		return false;
	}

	g_cg_ctx = cgCreateContext();
	if (!g_cg_ctx) {
		bprintf(0, _T("[EPIC12-RSX] Failed to create Cg context!\n"));
		g_ps3_gpu_failed = true;
		return false;
	}

	CGprofile vprof = cgGLGetLatestProfile(CG_GL_VERTEX);
	CGprofile fprof = cgGLGetLatestProfile(CG_GL_FRAGMENT);

	g_vprg = cgCreateProgram(g_cg_ctx, CG_BINARY, (const char*)epic12_rsx_vpo, vprof, NULL, NULL);
	g_fprg_fast = cgCreateProgram(g_cg_ctx, CG_BINARY, (const char*)epic12_rsx_fast_fpo, fprof, NULL, NULL);
	g_fprg_feedback = cgCreateProgram(g_cg_ctx, CG_BINARY, (const char*)epic12_rsx_feedback_fpo, fprof, NULL, NULL);

	if (!g_vprg || !g_fprg_fast || !g_fprg_feedback) {
		bprintf(0, _T("[EPIC12-RSX] Failed to load binary Cg programs: vprg=%p fast=%p feedback=%p\n"),
			g_vprg, g_fprg_fast, g_fprg_feedback);
		g_ps3_gpu_failed = true;
		return false;
	}

	cgGLLoadProgram(g_vprg);
	cgGLLoadProgram(g_fprg_fast);
	cgGLLoadProgram(g_fprg_feedback);

	g_param_p = cgGetNamedParameter(g_vprg, "p");
	g_param_uv = cgGetNamedParameter(g_vprg, "uv");
	g_param_tint = cgGetNamedParameter(g_vprg, "tint");
	g_param_mode = cgGetNamedParameter(g_vprg, "mode");
	g_param_atlasScale = cgGetNamedParameter(g_vprg, "atlasScale");

	// Initialize Atlas Texture (2048x1024 ARGB)
	glGenTextures(1, &g_atlas_tex);
	glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ARGB_SCE, 2048, 1024, 0, GL_ARGB_SCE, GL_UNSIGNED_BYTE, NULL);

	// Initialize Target Texture (512x512 ARGB)
	glGenTextures(1, &g_target_tex);
	glBindTexture(GL_TEXTURE_2D, g_target_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ARGB_SCE, 512, 512, 0, GL_ARGB_SCE, GL_UNSIGNED_BYTE, NULL);

	// Initialize Snapshot Texture for Feedback (512x512 ARGB)
	glGenTextures(1, &g_snapshot_tex);
	glBindTexture(GL_TEXTURE_2D, g_snapshot_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ARGB_SCE, 512, 512, 0, GL_ARGB_SCE, GL_UNSIGNED_BYTE, NULL);

#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#endif

	// Initialize Framebuffer Object (FBO)
	glGenFramebuffersOES(1, &g_fbo);
	glBindFramebufferOES(GL_FRAMEBUFFER_OES, g_fbo);
	glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, g_target_tex, 0);

	GLenum status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
	if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
		bprintf(0, _T("[EPIC12-RSX] FBO incomplete: status=0x%04x\n"), status);
		glBindFramebufferOES(GL_FRAMEBUFFER_OES, 0);
		g_ps3_gpu_failed = true;
		return false;
	}
	glBindFramebufferOES(GL_FRAMEBUFFER_OES, 0);

	epic12_ps3_init_indices();
	g_page_cache.reset(128);

	g_ps3_gpu_inited = true;
	bprintf(0, _T("[EPIC12-RSX] Hardware GPU Rasterizer successfully initialized! (Atlas=2048x1024, FBO=512x512)\n"));
	return true;
}

static inline void epic12_ps3_upload_source_page(int page, int slot)
{
	int px = (page & 63) * 128;
	int py = (page >> 6) * 128;

	for (int r = 0; r < 128; ++r) {
		const UINT16 *src_row = epic12_get_read_row((py + r) & 4095) + px;
		UINT32 *dst_row = g_page_scratch + r * 128;
		for (int c = 0; c < 128; ++c) {
			dst_row[c] = epic12_in16_to_argb(src_row[c]);
		}
	}

	glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, (slot & 15) * 128, (slot >> 4) * 128, 128, 128,
		GL_ARGB_SCE, GL_UNSIGNED_BYTE, g_page_scratch);
}

static bool epic12_gpu_render(const Epic12GpuCommand *cmd, int count, const rectangle &bounds)
{
	if (!epic12_ps3_gpu_init()) return false;
	if (count <= 0) return true;

	const int width = bounds.max_x - bounds.min_x + 1;
	const int height = bounds.max_y - bounds.min_y + 1;
	if (width <= 0 || height <= 0 || width > 512 || height > 512) return false;

	// 1. Identify and allocate needed 128x128 source pages
	bool needed[EPIC12_VRAM_PAGES] = {false};
	int slots[EPIC12_VRAM_PAGES];
	int pages = 0;

	for (int i = 0; i < count; ++i) {
		const Epic12GpuCommand &c = cmd[i];
		for (int y = c.sy / 128; y <= (c.sy + c.h - 1) / 128; ++y) {
			for (int x = c.sx / 128; x <= (c.sx + c.w - 1) / 128; ++x) {
				int p = y * 64 + x;
				if (p >= 0 && p < EPIC12_VRAM_PAGES && !needed[p]) {
					needed[p] = true;
					if (++pages > g_page_cache.capacity) return false;
				}
			}
		}
	}

	g_page_cache.begin_batch(needed, slots);

	for (int p = 0; p < EPIC12_VRAM_PAGES; ++p) {
		if (needed[p] && slots[p] < 0) {
			int slot = g_page_cache.allocate(p);
			if (slot < 0) return false;
			slots[p] = slot;
			epic12_ps3_upload_source_page(p, slot);
		}
	}

	// 2. Upload destination background from VRAM to target texture
	for (int r = 0; r < height; ++r) {
		const UINT16 *src_row = epic12_get_read_row((bounds.min_y + r) & 4095) + bounds.min_x;
		UINT32 *dst_row = g_dest_scratch + r * width;
		for (int c = 0; c < width; ++c) {
			dst_row[c] = epic12_in16_to_argb(src_row[c]);
		}
	}
	glBindTexture(GL_TEXTURE_2D, g_target_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_ARGB_SCE, GL_UNSIGNED_BYTE, g_dest_scratch);

	// 3. Setup RSX rendering states
	glBindFramebufferOES(GL_FRAMEBUFFER_OES, g_fbo);
	glViewport(0, 0, 512, 512);

	cgGLEnableProfile(CG_PROFILE_SCE_VP_RSX);
	cgGLEnableProfile(CG_PROFILE_SCE_FP_RSX);

	cgGLBindProgram(g_vprg);
	cgGLSetParameter4f(g_param_atlasScale, 1.0f / 2048.0f, 1.0f / 1024.0f, 0.0f, 0.0f);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_atlas_tex);

	cgGLSetParameterPointer(g_param_p, 2, GL_FLOAT, sizeof(Epic12GpuVertex), &g_vertices[0].x);
	cgGLSetParameterPointer(g_param_uv, 2, GL_FLOAT, sizeof(Epic12GpuVertex), &g_vertices[0].u);
	cgGLSetParameterPointer(g_param_tint, 4, GL_FLOAT, sizeof(Epic12GpuVertex), g_vertices[0].tint);
	cgGLSetParameterPointer(g_param_mode, 4, GL_FLOAT, sizeof(Epic12GpuVertex), g_vertices[0].mode);

	cgGLEnableClientState(g_param_p);
	cgGLEnableClientState(g_param_uv);
	cgGLEnableClientState(g_param_tint);
	cgGLEnableClientState(g_param_mode);

	// 4. Render sprite groups
	int first = 0;
	int snapshot_end = 0;
	rectangle snapshot;

	while (first < count) {
		int end = epic12_gpu_group_end(cmd, first, count, bounds, snapshot, true);
		const Epic12GpuCommand &head = cmd[first];
		bool feedback = epic12_gpu_feedback(head);

		if (feedback) {
			if (first >= snapshot_end) {
				snapshot_end = epic12_gpu_snapshot_end(cmd, first, end, count, bounds, snapshot, true);
				if (snapshot.min_x < 0) snapshot.min_x = 0;
				if (snapshot.min_y < 0) snapshot.min_y = 0;
				if (snapshot.max_x >= width) snapshot.max_x = width - 1;
				if (snapshot.max_y >= height) snapshot.max_y = height - 1;
				int snap_w = snapshot.max_x - snapshot.min_x + 1;
				int snap_h = snapshot.max_y - snapshot.min_y + 1;
				if (snap_w > 0 && snap_h > 0) {
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, g_snapshot_tex);
					glCopyTexSubImage2D(GL_TEXTURE_2D, 0, snapshot.min_x, snapshot.min_y,
						snapshot.min_x, snapshot.min_y, snap_w, snap_h);
				}
			}
			cgGLBindProgram(g_fprg_feedback);
			glDisable(GL_BLEND);
		} else {
			cgGLBindProgram(g_fprg_fast);
			int dm_norm = epic12_gpu_dest_mode(head);
			int da_norm = epic12_gpu_dest_alpha(head);
			if (!head.blend || (dm_norm == 0 && da_norm == 0)) {
				glDisable(GL_BLEND);
			} else if (da_norm == 31) {
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
			} else {
				glDisable(GL_BLEND);
			}
		}

		// Generate vertices for current group
		int vertex_count = 0;
		for (int i = first; i < end; ++i) {
			if (vertex_count + epic12_gpu_vertex_count(cmd[i]) > EPIC12_GPU_DRAW_RECTS * 4) {
				glDrawElements(GL_TRIANGLES, (vertex_count / 4) * 6, GL_UNSIGNED_SHORT, g_indices);
				vertex_count = 0;
			}
			vertex_count += epic12_gpu_vertices(cmd[i], bounds, slots, g_vertices + vertex_count);
		}
		if (vertex_count > 0) {
			glDrawElements(GL_TRIANGLES, (vertex_count / 4) * 6, GL_UNSIGNED_SHORT, g_indices);
		}

		first = end;
	}

	// 5. Readback rendered pixels to VRAM
	glReadPixels(0, 0, width, height, GL_ARGB_SCE, GL_UNSIGNED_BYTE, g_readback_scratch);

	for (int r = 0; r < height; ++r) {
		UINT16 *dst_row = epic12_get_write_row((bounds.min_y + r) & 4095);
		if (!dst_row) continue;
		dst_row += bounds.min_x;
		const UINT32 *src_row = g_readback_scratch + r * width;
		for (int c = 0; c < width; ++c) {
			dst_row[c] = epic12_argb_to_in16(src_row[c]);
		}
	}

	// 6. Cleanup RSX states
	cgGLDisableClientState(g_param_p);
	cgGLDisableClientState(g_param_uv);
	cgGLDisableClientState(g_param_tint);
	cgGLDisableClientState(g_param_mode);

	cgGLDisableProfile(CG_PROFILE_SCE_VP_RSX);
	cgGLDisableProfile(CG_PROFILE_SCE_FP_RSX);

	glDisable(GL_BLEND);
	glBindFramebufferOES(GL_FRAMEBUFFER_OES, 0);

	return true;
}

static inline void epic12_ps3_gpu_invalidate(const rectangle &r)
{
	if (g_ps3_gpu_inited) {
		g_page_cache.invalidate(r.min_x, r.min_y, r.max_x, r.max_y);
	}
}

static inline void epic12_ps3_gpu_reset()
{
	if (g_ps3_gpu_inited) {
		g_page_cache.clear();
	}
}

#endif // __PS3__
#endif // FBNEO_EPIC12_GPU_PS3_H
