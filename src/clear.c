//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glClear(GLbitfield mask) {
    /* Spec: any bit outside the three valid ones is GL_INVALID_VALUE and the
     * clear must not happen. (GL_ACCUM_BUFFER_BIT doesn't exist in ES.) */
    if (mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }

    C3D_ClearBits bits = (C3D_ClearBits) 0;
    u32 color = 0;
    u32 depth = 0;

    if (mask & GL_COLOR_BUFFER_BIT) {
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
    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        bits |= C3D_CLEAR_DEPTH;
        u32 dval = (u32) ((1.0f - g.clear_depth) * 0xFFFFFF);
        depth = dval | (((u32) g.clear_stencil & 0xFF) << 24);
    }

    if (bits && g.current_target) {
        /* C3D_RenderTargetClear is an IMMEDIATE GX memory-fill, while the
         * draws recorded so far only reach the GPU at C3D_FrameEnd. Without
         * a split, a mid-frame clear lands BEFORE this frame's earlier draws
         * on the GPU timeline. PD's gun pass depends on the ordering:
         * viPrepareZbuf clears depth BETWEEN the world and the viewmodel,
         * and with the clear hoisted to frame start the gun was depth-tested
         * against the world (gun rendered under the floor). The split pushes
         * the recorded command list into the GX queue ahead of the fill —
         * the queue executes FIFO, so no CPU-side wait is needed. */
        C3D_FrameSplit(0);
        C3D_RenderTargetClear(g.current_target, bits, color, depth);
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