//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

void glEnable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: g.depth_test_enabled = 1; break;
        case GL_BLEND: g.blend_enabled = 1; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 1; break;
        case GL_CULL_FACE: g.cull_face_enabled = 1; break;
        case GL_TEXTURE_2D:
            g.texture_2d_enabled_unit[g.active_texture_unit] = 1;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 1; break;
        case GL_FOG:
            if (!g.fog_enabled) { g.fog_enabled = 1; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 1;
            apply_depth_map();
            break;
        case GL_LINE_SMOOTH: g.line_smooth_enabled = 1; break;
        case GL_VERTEX_ARRAY: g.va_vertex.enabled = 1; break;
        case GL_COLOR_ARRAY: g.va_color.enabled = 1; break;
        case GL_TEXTURE_COORD_ARRAY: g.va_texcoord.enabled = 1; break;
        case GL_NORMAL_ARRAY: g.va_normal.enabled = 1; break;
        default: break;
    }
}

void glDisable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: g.depth_test_enabled = 0; break;
        case GL_BLEND: g.blend_enabled = 0; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 0; break;
        case GL_CULL_FACE: g.cull_face_enabled = 0; break;
        case GL_TEXTURE_2D:
            g.texture_2d_enabled_unit[g.active_texture_unit] = 0;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 0; break;
        case GL_FOG:
            if (g.fog_enabled) { g.fog_enabled = 0; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 0;
            apply_depth_map();
            break;
        case GL_LINE_SMOOTH: g.line_smooth_enabled = 0; break;
        case GL_VERTEX_ARRAY: g.va_vertex.enabled = 0; break;
        case GL_COLOR_ARRAY: g.va_color.enabled = 0; break;
        case GL_TEXTURE_COORD_ARRAY: g.va_texcoord.enabled = 0; break;
        case GL_NORMAL_ARRAY: g.va_normal.enabled = 0; break;
        default: break;
    }
}

GLboolean glIsEnabled(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: return g.depth_test_enabled;
        case GL_BLEND: return g.blend_enabled;
        case GL_ALPHA_TEST: return g.alpha_test_enabled;
        case GL_CULL_FACE: return g.cull_face_enabled;
        case GL_TEXTURE_2D: return g.texture_2d_enabled_unit[g.active_texture_unit];
        case GL_SCISSOR_TEST: return g.scissor_test_enabled;
        case GL_FOG: return g.fog_enabled;
        case GL_LINE_SMOOTH: return g.line_smooth_enabled ? GL_TRUE : GL_FALSE;
        case GL_VERTEX_ARRAY: return g.va_vertex.enabled ? GL_TRUE : GL_FALSE;
        case GL_COLOR_ARRAY: return g.va_color.enabled ? GL_TRUE : GL_FALSE;
        case GL_TEXTURE_COORD_ARRAY: return g.va_texcoord.enabled ? GL_TRUE : GL_FALSE;
        case GL_NORMAL_ARRAY: return g.va_normal.enabled ? GL_TRUE : GL_FALSE;
        default: return GL_FALSE;
    }
}

void glGetFloatv(GLenum pname, GLfloat *params) {
    if (!params) return;

    if (pname == GL_SMOOTH_LINE_WIDTH_RANGE || pname == GL_ALIASED_LINE_WIDTH_RANGE) {
        params[0] = 1.0f; // min
        params[1] = 1.0f; // max
        return;
    } else if (pname == GL_LINE_WIDTH) {
        params[0] = 1.0f;
        return;
    }

    C3D_Mtx *src = NULL;
    if (pname == GL_MODELVIEW_MATRIX) src = &g.mv_stack[g.mv_sp];
    else if (pname == GL_PROJECTION_MATRIX) src = &g.proj_stack[g.proj_sp];
    else if (pname == GL_TEXTURE_MATRIX) src = &g.tex_stack[g.tex_sp];
    else {
        for (int i = 0; i < 16; i++) params[i] = 0.0f;
        return;
    }

    for (int r = 0; r < 4; r++) {
        params[0 * 4 + r] = src->r[r].x;
        params[1 * 4 + r] = src->r[r].y;
        params[2 * 4 + r] = src->r[r].z;
        params[3 * 4 + r] = src->r[r].w;
    }
}

void glGetIntegerv(GLenum pname, GLint *params) {
    if (!params) return;

    if (pname == GL_VIEWPORT) {
        params[0] = g.vp_x; params[1] = g.vp_y;
        params[2] = g.vp_w; params[3] = g.vp_h;
    } else if (pname == GL_MAX_TEXTURE_SIZE) {
        params[0] = 4096;
    } else if (pname == GL_UNPACK_ALIGNMENT) {
        params[0] = g.unpack_alignment;
    } else if (pname == GL_PACK_ALIGNMENT) {
        params[0] = g.pack_alignment;
    } else if (pname == GL_SMOOTH_LINE_WIDTH_RANGE || pname == GL_ALIASED_LINE_WIDTH_RANGE) {
        params[0] = 1; params[1] = 1;
    } else if (pname == GL_LINE_WIDTH) {
        params[0] = 1;
    } else {
        params[0] = 0;
    }
}

const GLubyte *glGetString(GLenum name) {
    if (name == GL_VENDOR) return (const GLubyte *) "NovaGL";
    if (name == GL_RENDERER) return (const GLubyte *) "PICA200 (3DS)";
    if (name == GL_VERSION) return (const GLubyte *) "OpenGL ES-CM 1.1 NovaGL by efimandreev0";
    if (name == GL_EXTENSIONS) return (const GLubyte *) "GL_OES_vertex_buffer_object GL_OES_matrix_palette";
    return (const GLubyte *) "";
}

void glHint(GLenum target, GLenum mode) { (void) target; (void) mode; }

typedef struct {
    GLboolean depth_test, blend, alpha_test, cull_face;
    int texture_2d_units[3];
    GLboolean scissor_test, fog;
    GLenum depth_func, blend_src, blend_dst, alpha_func;
    GLfloat alpha_ref;
    GLenum cull_face_mode, front_face;
} AttribState;

static AttribState attrib_stack[16];
static int attrib_stack_ptr = 0;

void glPushAttrib(GLbitfield mask) {
    (void) mask;
    if (attrib_stack_ptr < 16) {
        AttribState *s = &attrib_stack[attrib_stack_ptr++];
        s->depth_test = g.depth_test_enabled;
        s->blend = g.blend_enabled;
        s->alpha_test = g.alpha_test_enabled;
        s->cull_face = g.cull_face_enabled;
        for (int u = 0; u < 3; u++) s->texture_2d_units[u] = g.texture_2d_enabled_unit[u];
        s->scissor_test = g.scissor_test_enabled;
        s->fog = g.fog_enabled;
        s->depth_func = g.depth_func;
        s->blend_src = g.blend_src;
        s->blend_dst = g.blend_dst;
        s->alpha_func = g.alpha_func;
        s->alpha_ref = g.alpha_ref;
        s->cull_face_mode = g.cull_face_mode;
        s->front_face = g.front_face;
    }
}

void glPopAttrib(void) {
    if (attrib_stack_ptr > 0) {
        AttribState *s = &attrib_stack[--attrib_stack_ptr];
        g.depth_test_enabled = s->depth_test;
        g.blend_enabled = s->blend;
        g.alpha_test_enabled = s->alpha_test;
        g.cull_face_enabled = s->cull_face;
        for (int u = 0; u < 3; u++) g.texture_2d_enabled_unit[u] = s->texture_2d_units[u];
        g.scissor_test_enabled = s->scissor_test;
        g.fog_enabled = s->fog;
        g.depth_func = s->depth_func;
        g.blend_src = s->blend_src;
        g.blend_dst = s->blend_dst;
        g.alpha_func = s->alpha_func;
        g.alpha_ref = s->alpha_ref;
        g.cull_face_mode = s->cull_face_mode;
        g.front_face = s->front_face;
        g.tev_dirty = 1;
        g.fog_dirty = 1;
    }
}

typedef struct {
    int va_vertex_enabled, va_color_enabled, va_texcoord_enabled, va_normal_enabled;
} ClientAttribState;

static ClientAttribState client_attrib_stack[16];
static int client_attrib_stack_ptr = 0;

void glPushClientAttrib(GLbitfield mask) {
    (void) mask;
    if (client_attrib_stack_ptr < 16) {
        ClientAttribState *s = &client_attrib_stack[client_attrib_stack_ptr++];
        s->va_vertex_enabled = g.va_vertex.enabled;
        s->va_color_enabled = g.va_color.enabled;
        s->va_texcoord_enabled = g.va_texcoord.enabled;
        s->va_normal_enabled = g.va_normal.enabled;
    }
}

void glPopClientAttrib(void) {
    if (client_attrib_stack_ptr > 0) {
        ClientAttribState *s = &client_attrib_stack[--client_attrib_stack_ptr];
        g.va_vertex.enabled = s->va_vertex_enabled;
        g.va_color.enabled = s->va_color_enabled;
        g.va_texcoord.enabled = s->va_texcoord_enabled;
        g.va_normal.enabled = s->va_normal_enabled;
    }
}
