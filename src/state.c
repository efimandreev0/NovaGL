//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

void glEnable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    g.depth_test_enabled = 1; break;
        case GL_BLEND:         g.blend_enabled = 1; break;
        case GL_ALPHA_TEST:    g.alpha_test_enabled = 1; break;
        case GL_CULL_FACE:     g.cull_face_enabled = 1; break;
        case GL_TEXTURE_2D:    g.tev_dirty = 1; g.texture_2d_enabled = 1; break;
        case GL_SCISSOR_TEST:  g.scissor_test_enabled = 1; break;
        case GL_FOG:
            if (!g.fog_enabled) {
                g.fog_enabled = 1;
                g.fog_dirty = 1;
            }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 1;
            apply_depth_map();
            break;
        default: break;
    }
}

void glDisable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    g.depth_test_enabled = 0; break;
        case GL_BLEND:         g.blend_enabled = 0; break;
        case GL_ALPHA_TEST:    g.alpha_test_enabled = 0; break;
        case GL_CULL_FACE:     g.cull_face_enabled = 0; break;
        case GL_TEXTURE_2D:    g.tev_dirty = 1; g.texture_2d_enabled = 0; break;
        case GL_SCISSOR_TEST:  g.scissor_test_enabled = 0; break;
        case GL_FOG:
            if (g.fog_enabled) {
                g.fog_enabled = 0;
                g.fog_dirty = 1;
            }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 0;
            apply_depth_map();
            break;
        default: break;
    }
}

GLboolean glIsEnabled(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    return g.depth_test_enabled;
        case GL_BLEND:         return g.blend_enabled;
        case GL_ALPHA_TEST:    return g.alpha_test_enabled;
        case GL_CULL_FACE:     return g.cull_face_enabled;
        case GL_TEXTURE_2D:    return g.texture_2d_enabled;
        case GL_SCISSOR_TEST:  return g.scissor_test_enabled;
        case GL_FOG:           return g.fog_enabled;
        default:               return GL_FALSE;
    }
}

void glGetFloatv(GLenum pname, GLfloat *params) {
    C3D_Mtx *src = NULL;
    if (pname == GL_MODELVIEW_MATRIX) src = &g.mv_stack[g.mv_sp];
    else if (pname == GL_PROJECTION_MATRIX) src = &g.proj_stack[g.proj_sp];
    else if (pname == GL_TEXTURE_MATRIX) src = &g.tex_stack[g.tex_sp];
    else { for (int i = 0; i < 16; i++) params[i] = 0.0f; return; }
    for (int r = 0; r < 4; r++) {
        params[0*4 + r] = src->r[r].x; params[1*4 + r] = src->r[r].y;
        params[2*4 + r] = src->r[r].z; params[3*4 + r] = src->r[r].w;
    }
}

void glGetIntegerv(GLenum pname, GLint *params) {
    if (pname == GL_VIEWPORT) {
        params[0] = g.vp_x; params[1] = g.vp_y; params[2] = g.vp_w; params[3] = g.vp_h;
    } else if (pname == GL_MAX_TEXTURE_SIZE) params[0] = 1024;
    else params[0] = 0;
}

const GLubyte* glGetString(GLenum name) {
    if (name == GL_VENDOR) return (const GLubyte*)"NovaGL";
    if (name == GL_RENDERER) return (const GLubyte*)"PICA200 (3DS)";
    if (name == GL_VERSION) return (const GLubyte*)"OpenGL ES-CM 1.1 NovaGL by efimandreev0";
    if (name == GL_EXTENSIONS) return (const GLubyte*)"GL_OES_vertex_buffer_object GL_OES_matrix_palette";
    return (const GLubyte*)"";
}

void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
