//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    g.vp_x = x; g.vp_y = y; g.vp_w = width; g.vp_h = height;
    C3D_SetViewport(y, x, height, width);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    g.scissor_x = x; g.scissor_y = y; g.scissor_w = width; g.scissor_h = height;
}

void glDepthFunc(GLenum func) { g.depth_func = func; }

void glDepthMask(GLboolean flag) { g.depth_mask = flag; }

void glDepthRangef(GLclampf near_val, GLclampf far_val) {
    g.depth_near = clampf(near_val, 0.0f, 1.0f);
    g.depth_far  = clampf(far_val, 0.0f, 1.0f);
    apply_depth_map();
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) { g.blend_src = sfactor; g.blend_dst = dfactor; }

void glAlphaFunc(GLenum func, GLclampf ref) { g.alpha_func = func; g.alpha_ref = ref; }

void glCullFace(GLenum mode) { g.cull_face_mode = mode; }

void glFrontFace(GLenum mode) { g.front_face = mode; }

void glColorMask(GLboolean r, GLboolean g_, GLboolean b, GLboolean a) {
    g.color_mask_r = r; g.color_mask_g = g_; g.color_mask_b = b; g.color_mask_a = a;
}

void glShadeModel(GLenum mode) { (void)mode; }

void glPolygonOffset(GLfloat factor, GLfloat units) {
    g.polygon_offset_factor = factor;
    g.polygon_offset_units = units;
    apply_depth_map();
}

void glLineWidth(GLfloat width) { (void)width; }

void glPolygonMode(GLenum face, GLenum mode) { (void)face; (void)mode; }
