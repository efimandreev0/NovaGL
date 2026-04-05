//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glClear(GLbitfield mask) {
    C3D_ClearBits bits = 0;
    u32 color = 0;
    u32 depth = 0;

    if (mask & GL_COLOR_BUFFER_BIT) {
        bits |= C3D_CLEAR_COLOR;
        color = ((u32)(g.clear_r * 255.0f) << 24) |
                ((u32)(g.clear_g * 255.0f) << 16) |
                ((u32)(g.clear_b * 255.0f) << 8)  |
                ((u32)(g.clear_a * 255.0f) << 0);
    }

    if (mask & GL_DEPTH_BUFFER_BIT) {
        bits |= C3D_CLEAR_DEPTH;
        depth = (u32)(g.clear_depth * 0xFFFFFF);
    }

    if (bits && g.current_target) {
        C3D_RenderTargetClear(g.current_target, bits, color, depth);
    }
}

void glClearColor(GLclampf r, GLclampf g_, GLclampf b, GLclampf a) {
    g.clear_r = clampf(r, 0.0f, 1.0f); g.clear_g = clampf(g_, 0.0f, 1.0f);
    g.clear_b = clampf(b, 0.0f, 1.0f); g.clear_a = clampf(a, 0.0f, 1.0f);
}

void glClearDepthf(GLclampf depth) { g.clear_depth = clampf(depth, 0.0f, 1.0f); }