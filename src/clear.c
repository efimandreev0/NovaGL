//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"
#include <string.h>

/* ------------------------------------------------------------------------
 * Asynchronous Quad Clear
 * ------------------------------------------------------------------------
 * A hardware GX memory fill requires a C3D_FrameSplit to ensure previously
 * queued draws finish before the DMA wipes the buffer. If glClear is called
 * mid-frame (g.p3d_pending != 0), this split causes a hard CPU stall.
 *
 * To prevent this, mid-frame color/depth clears are implemented as a
 * fullscreen clip-space quad. The quad goes into the same command buffer
 * as the geometry, avoiding the sync stall entirely and maintaining the
 * pipeline flow.
 * ------------------------------------------------------------------------ */
static void nova_clear_quad(int clear_color, int clear_depth) {
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    for (int i = 1; i < 6; i++) {
        C3D_TexEnvInit(C3D_GetTexEnv(i));
    }

    GPU_WRITEMASK write_mask = 0;
    if (clear_color) write_mask |= GPU_WRITE_ALL;
    if (clear_depth) write_mask |= GPU_WRITE_DEPTH;

    /* Early-Z handling (DMP fbwiper parity):
     *  1. A depth clear must also reset the on-chip early-depth buffer —
     *     citro3d never does, so stale early-Z from before the clear kept
     *     rejecting fresh geometry ("black wedges" artifacts).
     *  2. The clear quad itself must run with early-Z DISABLED: with the
     *     buffer freshly cleared to 0 and e.g. GPU_EARLYDEPTH_GREATER active,
     *     the quad's own fragments (depth 0 = far) would be early-rejected
     *     and the clear would silently not happen. C3D_EarlyDepthTest only
     *     touches the shadow; the C3D_DepthTest below dirties the effect
     *     block, so the disable lands with this quad's state flush — after
     *     nova_clear_early_depth's raw writes, in command order. */
    if (clear_depth) nova_clear_early_depth();
    C3D_EarlyDepthTest(false, g.gpu_early_depth_func, 0);

    C3D_DepthTest(clear_depth, GPU_ALWAYS, write_mask);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);

    /* PICA200 clip-space Z is [-w, 0] (inverted vs GL).
     * GL far plane (1.0) maps to PICA 0.0
     * GL near plane (0.0) maps to PICA -1.0 */
    float pica_z = g.clear_depth - 1.0f;
    float r = g.clear_r, gc = g.clear_g, b = g.clear_b, a = g.clear_a;

    float quad_verts[4 * 8] = {
        /* x,     y,     z,      w,       r, g,  b, a */
        -1.0f, -1.0f, pica_z, 1.0f,  r, gc, b, a,
         1.0f, -1.0f, pica_z, 1.0f,  r, gc, b, a,
         1.0f,  1.0f, pica_z, 1.0f,  r, gc, b, a,
        -1.0f,  1.0f, pica_z, 1.0f,  r, gc, b, a,
    };

    int bytes = sizeof(quad_verts);
    uint8_t *staged = (uint8_t *)linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, bytes, g.client_array_buf_size);
    if (!staged) return;
    memcpy(staged, quad_verts, bytes);
    GSPGPU_FlushDataCache(staged, bytes);

    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 4); /* position */
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 4); /* color */
    C3D_SetAttrInfo(&g.attr_info);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, staged, 8 * sizeof(float), 2, 0x10);

    /* Bypass the heavy MVP transform by using the clipspace passthrough shader */
    if (g.shader_clipspace_dvlb) {
        C3D_BindProgram(&g.shader_clipspace_program);
        C3D_Mtx id;
        Mtx_Identity(&id);
        if (g.uLoc_projection_clipspace >= 0) {
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &id);
        }
    }

    static const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    uint8_t *idx_staged = (uint8_t *)linear_alloc_ring(g.index_buf, &g.index_buf_offset, sizeof(indices), g.index_buf_size);
    if (idx_staged) {
        memcpy(idx_staged, indices, sizeof(indices));
        GSPGPU_FlushDataCache(idx_staged, sizeof(indices));
        C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, idx_staged);
        g.p3d_pending = 1;
    }

    /* The quad clear clobbered TEV, Depth, Blend, and Shader states.
     * Invalidate the cache so the next draw forces a clean re-push. */
    nova_invalidate_state_cache();
}

void glClear(GLbitfield mask) {
    /* Spec: any bit outside the three valid ones is GL_INVALID_VALUE and the
     * clear must not happen. (GL_ACCUM_BUFFER_BIT doesn't exist in ES.) */
    if (mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    C3D_ClearBits bits = (C3D_ClearBits) 0;
    u32 color = 0;
    u32 depth = 0;

    /* By DEFAULT glClear clears every requested buffer regardless of the write
     * masks — matches NovaGL's historical behaviour and what real ports rely on
     * (PD/fast3d's gfx_novagl clears depth at frame start without re-enabling
     * glDepthMask first; gating on a stale depthMask=FALSE left the depth buffer
     * un-cleared and z-rejected the whole world). The strict spec behaviour
     * (clear respects glColorMask/glDepthMask/stencil writemask) is opt-in via
     * -DNOVAGL_CLEAR_RESPECTS_MASK=1. */
#ifdef NOVAGL_CLEAR_RESPECTS_MASK
    int color_writable = g.color_mask_r || g.color_mask_g || g.color_mask_b || g.color_mask_a;
#else
    int color_writable = 1;
#endif
    if ((mask & GL_COLOR_BUFFER_BIT) && color_writable) {
        bits |= C3D_CLEAR_COLOR;
        // Use +0.5f for proper rounding before casting to integer
        color = ((u32) (g.clear_r * 255.0f + 0.5f) << 24) |
                ((u32) (g.clear_g * 255.0f + 0.5f) << 16) |
                ((u32) (g.clear_b * 255.0f + 0.5f) << 8) |
                ((u32) (g.clear_a * 255.0f + 0.5f) << 0);
    }

    /* PICA's depth/stencil buffer is interleaved D24S8 and shares a single
     * clear register, so depth and stencil can't be cleared independently.
     *
     * GL_DEPTH_BUFFER_BIT and/or GL_STENCIL_BUFFER_BIT both map to
     * C3D_CLEAR_DEPTH; we always pack BOTH the depth half (from clear_depth)
     * and the stencil half (from clear_stencil) into the register, so:
     *   - depth-only clear also rewrites stencil to clear_stencil,
     *   - stencil-only clear also rewrites depth to clear_depth.
     * That's a documented hardware compromise: preserving the other channel
     * would require reading the buffer back. Packing both halves is strictly
     * better than the previous behaviour where a stencil-only clear wrote
     * depth=0 (near plane in PICA's inverted convention => everything
     * z-rejected) and dropped the stencil clear value entirely.
     *
     * PICA depth is stored inverted vs GL (see apply_depth_map): GL's
     * glClearDepth(1.0) = far plane = PICA value 0. */
#ifdef NOVAGL_CLEAR_RESPECTS_MASK
    int want_depth   = (mask & GL_DEPTH_BUFFER_BIT)   && g.depth_mask;
    int want_stencil = (mask & GL_STENCIL_BUFFER_BIT) && (g.stencil_write_mask != 0);
#else
    int want_depth   = (mask & GL_DEPTH_BUFFER_BIT)   != 0;
    int want_stencil = (mask & GL_STENCIL_BUFFER_BIT) != 0;
#endif
    if (want_depth || want_stencil) {
        bits |= C3D_CLEAR_DEPTH;
        u32 dval = (u32) ((1.0f - g.clear_depth) * 0xFFFFFF + 0.5f);
        depth = dval | (((u32) g.clear_stencil & 0xFF) << 24);
    }

    if (bits && g.current_target) {
        /* Commit pending deferred draws into the command list BEFORE clearing */
        nova_batch_flush();

        /* If the command buffer already contains draws (a mid-frame clear),
         * a hardware GX memory fill would require a FrameSplit, stalling the CPU.
         * Use a fullscreen quad instead to keep the pipeline flowing.
         *
         * Note: Stencil clears fall back to the hardware fill to guarantee
         * exact interleaved D24S8 semantics without mutating StencilOp state. */
        if (g.p3d_pending && !want_stencil) {
            int do_color = color_writable && (mask & GL_COLOR_BUFFER_BIT);
            nova_clear_quad(do_color, want_depth);
        } else {
            /* Frame start: buffer is empty, so a GX memory fill is stall-free and much faster. */
            C3D_FrameSplit(0);
            C3D_RenderTargetClear(g.current_target, bits, color, depth);
            /* The memory fill wiped the depth buffer in VRAM but not the
             * on-chip early-depth buffer — reset it too (see nova_clear_early_depth). */
            if (bits & C3D_CLEAR_DEPTH) nova_clear_early_depth();
        }
    }
}

void glClearColor(GLclampf r, GLclampf g_, GLclampf b, GLclampf a) {
    // Clamping is important to prevent overflow during bit shifting
    g.clear_r = clampf(r, 0.0f, 1.0f);
    g.clear_g = clampf(g_, 0.0f, 1.0f);
    g.clear_b = clampf(b, 0.0f, 1.0f);
    g.clear_a = clampf(a, 0.0f, 1.0f);
}

void glClearDepthf(GLclampf depth) {
    g.clear_depth = clampf(depth, 0.0f, 1.0f);
}

void glClearDepth(GLclampd depth) {
    g.clear_depth = clampf((GLfloat) depth, 0.0f, 1.0f);
}

void glClearStencil(GLint s) {
    g.clear_stencil = s & 0xFF;
}