//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

/* Standard GL enum values used by the expanded glGet* below. Values are
 * fixed by the GL spec, so defining missing ones locally is safe even if
 * NovaGL.h doesn't expose them yet. */
#ifndef GL_MATRIX_MODE
#define GL_MATRIX_MODE 0x0BA0
#endif
#ifndef GL_MODELVIEW_STACK_DEPTH
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#endif
#ifndef GL_PROJECTION_STACK_DEPTH
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#endif
#ifndef GL_TEXTURE_STACK_DEPTH
#define GL_TEXTURE_STACK_DEPTH 0x0BA5
#endif
#ifndef GL_MAX_MODELVIEW_STACK_DEPTH
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#endif
#ifndef GL_MAX_PROJECTION_STACK_DEPTH
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38
#endif
#ifndef GL_MAX_TEXTURE_STACK_DEPTH
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0D39
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#endif
#ifndef GL_BLEND_SRC
#define GL_BLEND_SRC 0x0BE1
#endif
#ifndef GL_BLEND_DST
#define GL_BLEND_DST 0x0BE0
#endif
#ifndef GL_DEPTH_FUNC
#define GL_DEPTH_FUNC 0x0B74
#endif
#ifndef GL_DEPTH_WRITEMASK
#define GL_DEPTH_WRITEMASK 0x0B72
#endif
#ifndef GL_CULL_FACE_MODE
#define GL_CULL_FACE_MODE 0x0B45
#endif
#ifndef GL_FRONT_FACE_MODE_QUERY
#define GL_FRONT_FACE_MODE_QUERY 0x0B46 /* GL_FRONT_FACE pname */
#endif
#ifndef GL_SCISSOR_BOX
#define GL_SCISSOR_BOX 0x0C10
#endif
#ifndef GL_ALPHA_TEST_FUNC
#define GL_ALPHA_TEST_FUNC 0x0BC1
#endif
#ifndef GL_ALPHA_TEST_REF
#define GL_ALPHA_TEST_REF 0x0BC2
#endif
#ifndef GL_SHADE_MODEL_QUERY
#define GL_SHADE_MODEL_QUERY 0x0B54 /* GL_SHADE_MODEL pname */
#endif
#ifndef GL_CURRENT_COLOR
#define GL_CURRENT_COLOR 0x0B00
#endif
#ifndef GL_DEPTH_RANGE
#define GL_DEPTH_RANGE 0x0B70
#endif
#ifndef GL_COLOR_CLEAR_VALUE
#define GL_COLOR_CLEAR_VALUE 0x0C22
#endif
#ifndef GL_DEPTH_CLEAR_VALUE
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#endif
#ifndef GL_STENCIL_CLEAR_VALUE
#define GL_STENCIL_CLEAR_VALUE 0x0B91
#endif
#ifndef GL_STENCIL_FUNC_QUERY
#define GL_STENCIL_FUNC_QUERY 0x0B92 /* GL_STENCIL_FUNC pname */
#endif
#ifndef GL_STENCIL_REF_QUERY
#define GL_STENCIL_REF_QUERY 0x0B97 /* GL_STENCIL_REF pname */
#endif
#ifndef GL_STENCIL_VALUE_MASK
#define GL_STENCIL_VALUE_MASK 0x0B93
#endif
#ifndef GL_STENCIL_WRITEMASK
#define GL_STENCIL_WRITEMASK 0x0B98
#endif
#ifndef GL_RED_BITS
#define GL_RED_BITS 0x0D52
#define GL_GREEN_BITS 0x0D53
#define GL_BLUE_BITS 0x0D54
#define GL_ALPHA_BITS 0x0D55
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#endif
#ifndef GL_SUBPIXEL_BITS
#define GL_SUBPIXEL_BITS 0x0D50
#endif
#ifndef GL_MAX_VIEWPORT_DIMS
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#endif
#ifndef GL_POLYGON_OFFSET_FACTOR
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#endif
#ifndef GL_POLYGON_OFFSET_UNITS
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#endif
#ifndef GL_NUM_COMPRESSED_TEXTURE_FORMATS
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#endif
#ifndef GL_COMPRESSED_TEXTURE_FORMATS
#define GL_COMPRESSED_TEXTURE_FORMATS 0x86A3
#endif
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

/* Defined in raster.c so it can be shared between glStencilFunc and the
 * GL_STENCIL_TEST enable/disable hook below. */
GPU_TESTFUNC stencil_func_to_gpu(GLenum f);

void glEnable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: g.depth_test_enabled = 1; break;
        case GL_BLEND: g.blend_enabled = 1; break;
        case GL_COLOR_LOGIC_OP: g.color_logic_op_enabled = 1; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 1; break;
        case GL_CULL_FACE: g.cull_face_enabled = 1; break;
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP: /* shares the per-unit sampler enable */
            g.texture_2d_enabled_unit[g.active_texture_unit] = 1;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 1; break;
        case GL_STENCIL_TEST: g.stencil_test_enabled = 1;
            /* Re-push stencil state with enabled flag flipped. */
            C3D_StencilTest(1, stencil_func_to_gpu(g.stencil_func),
                            g.stencil_ref, (u8)g.stencil_mask,
                            (u8)g.stencil_write_mask);
            break;
        case GL_FOG:
            if (!g.fog_enabled) { g.fog_enabled = 1; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 1;
            apply_depth_map();
            break;
        case GL_LINE_SMOOTH: g.line_smooth_enabled = 1; break;
        case GL_LIGHTING: g.lighting_enabled = 1; g.light_dirty = 1; break;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            g.lights[cap - GL_LIGHT0].enabled = 1; g.light_dirty = 1; break;
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
        case GL_COLOR_LOGIC_OP: g.color_logic_op_enabled = 0; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 0; break;
        case GL_CULL_FACE: g.cull_face_enabled = 0; break;
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            g.texture_2d_enabled_unit[g.active_texture_unit] = 0;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 0; break;
        case GL_STENCIL_TEST: g.stencil_test_enabled = 0;
            C3D_StencilTest(0, GPU_ALWAYS, 0, 0xFF, 0xFF);
            break;
        case GL_FOG:
            if (g.fog_enabled) { g.fog_enabled = 0; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 0;
            apply_depth_map();
            break;
        case GL_LINE_SMOOTH: g.line_smooth_enabled = 0; break;
        case GL_LIGHTING: g.lighting_enabled = 0; g.light_dirty = 1; break;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            g.lights[cap - GL_LIGHT0].enabled = 0; g.light_dirty = 1; break;
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
        case GL_COLOR_LOGIC_OP: return g.color_logic_op_enabled ? GL_TRUE : GL_FALSE;
        case GL_ALPHA_TEST: return g.alpha_test_enabled;
        case GL_CULL_FACE: return g.cull_face_enabled;
        case GL_TEXTURE_2D: return g.texture_2d_enabled_unit[g.active_texture_unit];
        case GL_SCISSOR_TEST: return g.scissor_test_enabled;
        case GL_STENCIL_TEST: return g.stencil_test_enabled ? GL_TRUE : GL_FALSE;
        case GL_FOG: return g.fog_enabled;
        case GL_LINE_SMOOTH: return g.line_smooth_enabled ? GL_TRUE : GL_FALSE;
        case GL_LIGHTING: return g.lighting_enabled ? GL_TRUE : GL_FALSE;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            return g.lights[cap - GL_LIGHT0].enabled ? GL_TRUE : GL_FALSE;
        case GL_VERTEX_ARRAY: return g.va_vertex.enabled ? GL_TRUE : GL_FALSE;
        case GL_COLOR_ARRAY: return g.va_color.enabled ? GL_TRUE : GL_FALSE;
        case GL_TEXTURE_COORD_ARRAY: return g.va_texcoord.enabled ? GL_TRUE : GL_FALSE;
        case GL_NORMAL_ARRAY: return g.va_normal.enabled ? GL_TRUE : GL_FALSE;
        default: return GL_FALSE;
    }
}

void glGetFloatv(GLenum pname, GLfloat *params) {
    if (!params) return;

    switch (pname) {
        case GL_SMOOTH_LINE_WIDTH_RANGE: /* == GL_LINE_WIDTH_RANGE */
        case GL_ALIASED_LINE_WIDTH_RANGE:
            params[0] = 1.0f;
            params[1] = 1.0f;
            return;
        case GL_LINE_WIDTH:
            params[0] = 1.0f;
            return;
        case GL_CURRENT_COLOR:
            params[0] = g.cur_color[0];
            params[1] = g.cur_color[1];
            params[2] = g.cur_color[2];
            params[3] = g.cur_color[3];
            return;
        case GL_DEPTH_RANGE:
            params[0] = g.depth_near;
            params[1] = g.depth_far;
            return;
        case GL_COLOR_CLEAR_VALUE:
            params[0] = g.clear_r; params[1] = g.clear_g;
            params[2] = g.clear_b; params[3] = g.clear_a;
            return;
        case GL_DEPTH_CLEAR_VALUE:
            params[0] = g.clear_depth;
            return;
        case GL_FOG_COLOR:
            params[0] = g.fog_color[0]; params[1] = g.fog_color[1];
            params[2] = g.fog_color[2]; params[3] = g.fog_color[3];
            return;
        case GL_FOG_DENSITY: params[0] = g.fog_density; return;
        case GL_FOG_START:   params[0] = g.fog_start;   return;
        case GL_FOG_END:     params[0] = g.fog_end;     return;
        case GL_FOG_MODE:    params[0] = (GLfloat) g.fog_mode; return;
        case GL_ALPHA_TEST_REF: params[0] = g.alpha_ref; return;
        case GL_POLYGON_OFFSET_FACTOR: params[0] = g.polygon_offset_factor; return;
        case GL_POLYGON_OFFSET_UNITS:  params[0] = g.polygon_offset_units;  return;
        default:
            break;
    }

    C3D_Mtx *src = NULL;
    if (pname == GL_MODELVIEW_MATRIX) src = &g.mv_stack[g.mv_sp];
    else if (pname == GL_PROJECTION_MATRIX) src = &g.proj_stack[g.proj_sp];
    else if (pname == GL_TEXTURE_MATRIX) src = &g.tex_stack[g.tex_sp];
    else {
        /* Fall through to the integer-state path so float queries of integer
         * pnames (a very common pattern: glGetFloatv(GL_VIEWPORT, ...)) work
         * per spec instead of returning zeroes. */
        GLint iv[4] = {0, 0, 0, 0};
        glGetIntegerv(pname, iv);
        params[0] = (GLfloat) iv[0];
        params[1] = (GLfloat) iv[1];
        params[2] = (GLfloat) iv[2];
        params[3] = (GLfloat) iv[3];
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

    switch (pname) {
        case GL_VIEWPORT:
            params[0] = g.vp_x; params[1] = g.vp_y;
            params[2] = g.vp_w; params[3] = g.vp_h;
            return;
        case GL_SCISSOR_BOX:
            params[0] = g.scissor_x; params[1] = g.scissor_y;
            params[2] = g.scissor_w; params[3] = g.scissor_h;
            return;
        case GL_MAX_TEXTURE_SIZE:     params[0] = 1024; return; /* NovaGL downscales >1024 anyway */
        case GL_MAX_TEXTURE_UNITS:    params[0] = 3; return;
        case GL_MAX_LIGHTS:           params[0] = NOVA_MAX_LIGHTS; return;
        case GL_UNPACK_ALIGNMENT:     params[0] = g.unpack_alignment; return;
        case GL_PACK_ALIGNMENT:       params[0] = g.pack_alignment; return;
        case GL_MATRIX_MODE:          params[0] = g.matrix_mode; return;
        case GL_MODELVIEW_STACK_DEPTH:   params[0] = g.mv_sp + 1; return;
        case GL_PROJECTION_STACK_DEPTH:  params[0] = g.proj_sp + 1; return;
        case GL_TEXTURE_STACK_DEPTH:     params[0] = g.tex_sp + 1; return;
        case GL_MAX_MODELVIEW_STACK_DEPTH:
        case GL_MAX_PROJECTION_STACK_DEPTH:
        case GL_MAX_TEXTURE_STACK_DEPTH:
            params[0] = NOVA_MATRIX_STACK; return;
        case GL_TEXTURE_BINDING_2D:
            params[0] = (GLint) g.bound_texture[g.active_texture_unit]; return;
        case GL_ARRAY_BUFFER_BINDING:
            params[0] = (GLint) g.bound_array_buffer; return;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
            params[0] = (GLint) g.bound_element_array_buffer; return;
        case GL_FRAMEBUFFER_BINDING:
            params[0] = (GLint) g.bound_fbo; return;
        case GL_ACTIVE_TEXTURE:
            params[0] = GL_TEXTURE0 + g.active_texture_unit; return;
        case GL_CLIENT_ACTIVE_TEXTURE:
            params[0] = GL_TEXTURE0 + g.client_active_texture_unit; return;
        case GL_BLEND_SRC: params[0] = (GLint) g.blend_src; return;
        case GL_BLEND_DST: params[0] = (GLint) g.blend_dst; return;
        case GL_DEPTH_FUNC: params[0] = (GLint) g.depth_func; return;
        case GL_DEPTH_WRITEMASK: params[0] = g.depth_mask ? 1 : 0; return;
        case GL_CULL_FACE_MODE: params[0] = (GLint) g.cull_face_mode; return;
        case GL_FRONT_FACE_MODE_QUERY: params[0] = (GLint) g.front_face; return;
        case GL_ALPHA_TEST_FUNC: params[0] = (GLint) g.alpha_func; return;
        case GL_SHADE_MODEL_QUERY: params[0] = (GLint) g.shade_model; return;
        case GL_STENCIL_FUNC_QUERY: params[0] = (GLint) g.stencil_func; return;
        case GL_STENCIL_REF_QUERY: params[0] = g.stencil_ref; return;
        case GL_STENCIL_VALUE_MASK: params[0] = (GLint) g.stencil_mask; return;
        case GL_STENCIL_WRITEMASK: params[0] = (GLint) g.stencil_write_mask; return;
        case GL_STENCIL_CLEAR_VALUE: params[0] = g.clear_stencil; return;
        case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS:
            params[0] = 8; return; /* RGBA8 render targets */
        case GL_DEPTH_BITS: params[0] = 24; return;
        case GL_STENCIL_BITS: params[0] = 8; return; /* D24S8 */
        case GL_SUBPIXEL_BITS: params[0] = 4; return;
        case GL_MAX_VIEWPORT_DIMS:
            params[0] = 1024; params[1] = 1024; return;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS: params[0] = 1; return;
        case GL_COMPRESSED_TEXTURE_FORMATS: params[0] = GL_ETC1_RGB8_OES; return;
        case GL_SMOOTH_LINE_WIDTH_RANGE: /* == GL_LINE_WIDTH_RANGE */
        case GL_ALIASED_LINE_WIDTH_RANGE:
            params[0] = 1; params[1] = 1; return;
        case GL_LINE_WIDTH: params[0] = 1; return;
        default:
            params[0] = 0;
            return;
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
    GLenum blend_src_alpha, blend_dst_alpha;
    int color_logic_op; GLenum logic_op;
    GLfloat blend_color[4];
    GLfloat alpha_ref;
    GLenum cull_face_mode, front_face;
} AttribState;

static AttribState attrib_stack[16];
static int attrib_stack_ptr = 0;

void glPushAttrib(GLbitfield mask) {
    (void) mask;
    if (attrib_stack_ptr >= 16) {
        g.last_error = GL_STACK_OVERFLOW;
        return;
    }
    {
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
        s->blend_src_alpha = g.blend_src_alpha;
        s->blend_dst_alpha = g.blend_dst_alpha;
        s->color_logic_op = g.color_logic_op_enabled;
        s->logic_op = g.logic_op;
        for (int i = 0; i < 4; i++) s->blend_color[i] = g.blend_color[i];
        s->alpha_func = g.alpha_func;
        s->alpha_ref = g.alpha_ref;
        s->cull_face_mode = g.cull_face_mode;
        s->front_face = g.front_face;
    }
}

void glPopAttrib(void) {
    if (attrib_stack_ptr <= 0) {
        g.last_error = GL_STACK_UNDERFLOW;
        return;
    }
    {
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
        g.blend_src_alpha = s->blend_src_alpha;
        g.blend_dst_alpha = s->blend_dst_alpha;
        g.color_logic_op_enabled = s->color_logic_op;
        g.logic_op = s->logic_op;
        for (int i = 0; i < 4; i++) g.blend_color[i] = s->blend_color[i];
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