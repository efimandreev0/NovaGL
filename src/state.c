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

/* Enable-caps that are VALID GL/GLES enumerants but that NovaGL doesn't act on
 * (PICA has no equivalent, or they're simply unimplemented). glEnable/glDisable
 * must accept these silently (no GL_INVALID_ENUM) — only a genuinely unknown cap
 * is an error. Values fixed by the spec; defined here so we don't widen the
 * public header. */
#ifndef GL_POINT_SMOOTH
#define GL_POINT_SMOOTH          0x0B10
#endif
#ifndef GL_POLYGON_SMOOTH
#define GL_POLYGON_SMOOTH        0x0B41
#endif
#ifndef GL_SAMPLE_ALPHA_TO_ONE
#define GL_SAMPLE_ALPHA_TO_ONE   0x809F
#endif
#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE          0x8861
#endif
#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0           0x3000
#endif
#ifndef GL_TEXTURE_GEN_S
#define GL_TEXTURE_GEN_S         0x0C60
#define GL_TEXTURE_GEN_T         0x0C61
#define GL_TEXTURE_GEN_R         0x0C62
#define GL_TEXTURE_GEN_Q         0x0C63
#endif

static int cap_known_unimpl(GLenum cap) {
    switch (cap) {
        case GL_DITHER:
        case GL_MULTISAMPLE:
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
        case GL_SAMPLE_ALPHA_TO_ONE:
        case GL_SAMPLE_COVERAGE:
        case GL_NORMALIZE:
        case GL_RESCALE_NORMAL:
        case GL_COLOR_MATERIAL:
        case GL_POINT_SMOOTH:
        case GL_POINT_SPRITE:
        case GL_POLYGON_SMOOTH:
        case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T:
        case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE0+1: case GL_CLIP_PLANE0+2:
        case GL_CLIP_PLANE0+3: case GL_CLIP_PLANE0+4: case GL_CLIP_PLANE0+5:
            return 1;
        default:
            return 0;
    }
}

void glEnable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: g.depth_test_enabled = 1; g.state_dirty_bits |= (NOVA_DIRTY_DEPTH_TEST | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_BLEND: g.blend_enabled = 1; g.state_dirty_bits |= (NOVA_DIRTY_BLEND_STATE | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_COLOR_LOGIC_OP: g.color_logic_op_enabled = 1; g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 1; g.state_dirty_bits |= (NOVA_DIRTY_ALPHA_TEST | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_CULL_FACE: g.cull_face_enabled = 1; g.state_dirty_bits |= NOVA_DIRTY_CULLING; break;
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP: /* shares the per-unit sampler enable */
            g.texture_2d_enabled_unit[g.active_texture_unit] = 1;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 1; g.state_dirty_bits |= NOVA_DIRTY_SCISSOR; break;
        case GL_STENCIL_TEST: g.stencil_test_enabled = 1;
            /* Re-push stencil state with enabled flag flipped. This is an EAGER
             * GPU command, so commit any pending clipspace batch first or the
             * deferred run would draw under the new stencil state. */
            nova_batch_flush();
            C3D_StencilTest(1, stencil_func_to_gpu(g.stencil_func),
                            g.stencil_ref, (u8)g.stencil_mask,
                            (u8)g.stencil_write_mask);
            break;
        case GL_FOG:
            if (!g.fog_enabled) { g.fog_enabled = 1; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            /* apply_depth_map() eagerly re-emits C3D_DepthMap (the polygon-offset
             * bias). PD's r_set_depth_mode toggles this per ZMODE between batched
             * draws — flush first so the open run keeps its old bias. */
            nova_batch_flush();
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
        default:
            /* Valid-but-unimplemented caps are accepted silently; only a
             * genuinely unknown enum is GL_INVALID_ENUM (spec §2.5). */
            if (!cap_known_unimpl(cap)) gl_set_error(GL_INVALID_ENUM);
            break;
    }
}

void glDisable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: g.depth_test_enabled = 0; g.state_dirty_bits |= (NOVA_DIRTY_DEPTH_TEST | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_BLEND: g.blend_enabled = 0; g.state_dirty_bits |= (NOVA_DIRTY_BLEND_STATE | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_COLOR_LOGIC_OP: g.color_logic_op_enabled = 0; g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE; break;
        case GL_ALPHA_TEST: g.alpha_test_enabled = 0; g.state_dirty_bits |= (NOVA_DIRTY_ALPHA_TEST | NOVA_DIRTY_EARLY_DEPTH); break;
        case GL_CULL_FACE: g.cull_face_enabled = 0; g.state_dirty_bits |= NOVA_DIRTY_CULLING; break;
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            g.texture_2d_enabled_unit[g.active_texture_unit] = 0;
            g.tev_dirty = 1;
            break;
        case GL_SCISSOR_TEST: g.scissor_test_enabled = 0; g.state_dirty_bits |= NOVA_DIRTY_SCISSOR; break;
        case GL_STENCIL_TEST: g.stencil_test_enabled = 0;
            nova_batch_flush();   /* eager C3D_StencilTest — see glEnable note */
            C3D_StencilTest(0, GPU_ALWAYS, 0, 0xFF, 0xFF);
            break;
        case GL_FOG:
            if (g.fog_enabled) { g.fog_enabled = 0; g.fog_dirty = 1; }
            break;
        case GL_POLYGON_OFFSET_FILL:
            nova_batch_flush();   /* eager C3D_DepthMap — see glEnable note */
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
        default:
            if (!cap_known_unimpl(cap)) gl_set_error(GL_INVALID_ENUM);
            break;
    }
}

GLboolean glIsEnabled(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST: return g.depth_test_enabled;
        case GL_BLEND: return g.blend_enabled;
        case GL_COLOR_LOGIC_OP: return g.color_logic_op_enabled ? GL_TRUE : GL_FALSE;
        case GL_ALPHA_TEST: return g.alpha_test_enabled;
        case GL_CULL_FACE: return g.cull_face_enabled;
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            return g.texture_2d_enabled_unit[g.active_texture_unit] ? GL_TRUE : GL_FALSE;
        case GL_POLYGON_OFFSET_FILL: return g.polygon_offset_fill_enabled ? GL_TRUE : GL_FALSE;
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
        default:
            /* GL_DITHER defaults to enabled in GL; report it true. Other valid-
             * but-unimplemented caps read back false; unknown enum is an error. */
            if (cap == GL_DITHER) return GL_TRUE;
            if (!cap_known_unimpl(cap)) gl_set_error(GL_INVALID_ENUM);
            return GL_FALSE;
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
        case GL_BLEND_COLOR:
            params[0] = g.blend_color[0]; params[1] = g.blend_color[1];
            params[2] = g.blend_color[2]; params[3] = g.blend_color[3];
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
        /* GL2-era texture-unit caps: OSG/engines gate their texturing paths on
         * these. PICA has 3 fixed-function units; report them the same. */
        case 0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS */:
        case 0x8B4C /* GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS (0 would be more honest,
                       but OSG treats it as a texturing capability floor) */:
        case 0x8B4D /* GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS */:
        case 0x8871 /* GL_MAX_TEXTURE_COORDS */:
            params[0] = 3; return;
        case 0x851C /* GL_MAX_CUBE_MAP_TEXTURE_SIZE */:
            params[0] = 1024; return;
        case 0x8869 /* GL_MAX_VERTEX_ATTRIBS */:
            params[0] = 0; return; /* fixed-function: no generic attributes */
        case 0x80E8 /* GL_MAX_ELEMENTS_VERTICES */:
        case 0x80E9 /* GL_MAX_ELEMENTS_INDICES */:
            params[0] = 65536; return;
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
        case GL_FRAMEBUFFER_BINDING: /* == GL_DRAW_FRAMEBUFFER_BINDING (0x8CA6) */
            params[0] = (GLint) g.bound_fbo; return;
        case GL_READ_FRAMEBUFFER_BINDING:
            params[0] = (GLint) g.bound_read_fbo; return;
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
        /* Separate-alpha blend factors + per-channel blend equations (the plain
         * GL_BLEND_SRC/DST above report only the colour factors). */
        case 0x80CB /*GL_BLEND_SRC_ALPHA*/: params[0] = (GLint) g.blend_src_alpha; return;
        case 0x80CA /*GL_BLEND_DST_ALPHA*/: params[0] = (GLint) g.blend_dst_alpha; return;
        case GL_BLEND_EQUATION_RGB:   params[0] = (GLint) g.blend_eq_rgb;   return; /* == GL_BLEND_EQUATION */
        case GL_BLEND_EQUATION_ALPHA: params[0] = (GLint) g.blend_eq_alpha; return;
        case GL_BLEND_COLOR:
            params[0] = (GLint) g.blend_color[0]; params[1] = (GLint) g.blend_color[1];
            params[2] = (GLint) g.blend_color[2]; params[3] = (GLint) g.blend_color[3]; return;
        case 0x0C23 /*GL_COLOR_WRITEMASK*/:
            params[0] = g.color_mask_r ? 1 : 0; params[1] = g.color_mask_g ? 1 : 0;
            params[2] = g.color_mask_b ? 1 : 0; params[3] = g.color_mask_a ? 1 : 0; return;
        /* Enable bits are queryable through glGetIntegerv too (return 0/1). */
        case GL_DEPTH_TEST: case GL_BLEND: case GL_CULL_FACE: case GL_SCISSOR_TEST:
        case GL_STENCIL_TEST: case GL_ALPHA_TEST: case GL_FOG: case GL_LIGHTING:
        case GL_TEXTURE_2D: case GL_DITHER: case GL_POLYGON_OFFSET_FILL:
        case GL_COLOR_LOGIC_OP: case GL_LINE_SMOOTH:
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            params[0] = glIsEnabled(pname) ? 1 : 0; return;
        default:
            /* Spec: an unaccepted pname is GL_INVALID_ENUM, leaving params
             * undefined. We still zero params[0] so a caller that ignores the
             * error reads a deterministic value instead of stack garbage. */
            gl_set_error(GL_INVALID_ENUM);
            params[0] = 0;
            return;
    }
}

const GLubyte *glGetString(GLenum name) {
    if (name == GL_VENDOR) return (const GLubyte *) "NovaGL";
    if (name == GL_RENDERER) return (const GLubyte *) "PICA200 (3DS)";
    if (name == GL_VERSION) return (const GLubyte *) "OpenGL ES-CM 1.1 NovaGL by efimandreev0";
    if (name == GL_EXTENSIONS)
        /* GL_ARB_vertex_buffer_object: OSG gates its VBO path on the ARB
         * name specifically — without it every osg::Geometry draws through
         * client arrays (tens of MB per frame through the vertex rings). */
        return (const GLubyte *) "GL_ARB_vertex_buffer_object GL_OES_vertex_buffer_object GL_OES_matrix_palette";
    /* Spec: an unaccepted name is GL_INVALID_ENUM and the return value is NULL
     * (not an empty string — callers strlen/strstr the result). */
    gl_set_error(GL_INVALID_ENUM);
    return NULL;
}

void glHint(GLenum target, GLenum mode) {
    /* All hints are no-ops on PICA, but the enums must still be validated
     * (spec: GL_INVALID_ENUM for an unrecognized target or mode). */
    switch (target) {
        case GL_PERSPECTIVE_CORRECTION_HINT: /* 0x0C50 */
        case 0x0C51: /* GL_POINT_SMOOTH_HINT */
        case 0x0C52: /* GL_LINE_SMOOTH_HINT */
        case 0x0C53: /* GL_POLYGON_SMOOTH_HINT */
        case 0x0C54: /* GL_FOG_HINT */
        case 0x8192: /* GL_GENERATE_MIPMAP_HINT */
        case 0x84EF: /* GL_TEXTURE_COMPRESSION_HINT */
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    (void) mode;
}

/* glPushAttrib/glPopAttrib now honour `mask`: the full server-state subset
 * NovaGL tracks is snapshotted on push (cheap), the mask is remembered, and pop
 * restores ONLY the attribute groups whose bit was requested. Restoring a group
 * the caller never asked to save (the old behaviour) silently clobbered state
 * the app expected to survive across the push/pop. Push-on-change GPU registers
 * (stencil, polygon offset) are re-emitted on pop since apply_gpu_state doesn't
 * reconcile them from g.* each draw. */
#ifndef GL_CURRENT_BIT
#define GL_CURRENT_BIT    0x00000001
#define GL_POLYGON_BIT    0x00000008
#define GL_LIGHTING_BIT   0x00000040
#define GL_FOG_BIT        0x00000080
#define GL_ENABLE_BIT     0x00002000
#define GL_SCISSOR_BIT    0x00080000
#endif

typedef struct {
    GLbitfield mask;
    /* enables */
    int depth_test, blend, alpha_test, cull_face, scissor_test, fog, stencil_test,
        lighting, color_logic_op, polygon_offset_fill, line_smooth;
    int texture_2d_units[3];
    int lights_enabled[NOVA_MAX_LIGHTS];
    /* depth */
    GLenum depth_func; GLboolean depth_mask;
    /* color buffer */
    GLenum blend_src, blend_dst, blend_src_alpha, blend_dst_alpha, blend_eq_rgb, blend_eq_alpha;
    GLfloat blend_color[4];
    GLenum alpha_func; GLfloat alpha_ref;
    GLenum logic_op;
    GLboolean color_mask[4];
    /* polygon */
    GLenum cull_face_mode, front_face;
    GLfloat poly_off_factor, poly_off_units;
    /* scissor */
    GLint scissor_x, scissor_y; GLsizei scissor_w, scissor_h;
    /* fog */
    GLenum fog_mode; GLfloat fog_color[4], fog_density, fog_start, fog_end;
    /* stencil */
    GLenum stencil_func; GLint stencil_ref; GLuint stencil_mask, stencil_write_mask;
    GLenum stencil_op_fail, stencil_op_zfail, stencil_op_zpass; GLint clear_stencil;
    /* lighting / current */
    GLenum shade_model;
    GLfloat mat_ambient[4], mat_diffuse[4], mat_specular[4], mat_emission[4], mat_shininess;
    GLfloat light_model_ambient[4];
    NovaLight lights[NOVA_MAX_LIGHTS];
    GLfloat cur_color[4], cur_normal[3];
} AttribState;

static AttribState attrib_stack[16];
static int attrib_stack_ptr = 0;

void glPushAttrib(GLbitfield mask) {
    if (attrib_stack_ptr >= 16) {
        gl_set_error(GL_STACK_OVERFLOW);
        return;
    }
    AttribState *s = &attrib_stack[attrib_stack_ptr++];
    s->mask = mask;
    s->depth_test = g.depth_test_enabled; s->blend = g.blend_enabled;
    s->alpha_test = g.alpha_test_enabled; s->cull_face = g.cull_face_enabled;
    s->scissor_test = g.scissor_test_enabled; s->fog = g.fog_enabled;
    s->stencil_test = g.stencil_test_enabled; s->lighting = g.lighting_enabled;
    s->color_logic_op = g.color_logic_op_enabled;
    s->polygon_offset_fill = g.polygon_offset_fill_enabled; s->line_smooth = g.line_smooth_enabled;
    for (int u = 0; u < 3; u++) s->texture_2d_units[u] = g.texture_2d_enabled_unit[u];
    for (int i = 0; i < NOVA_MAX_LIGHTS; i++) s->lights_enabled[i] = g.lights[i].enabled;
    s->depth_func = g.depth_func; s->depth_mask = g.depth_mask;
    s->blend_src = g.blend_src; s->blend_dst = g.blend_dst;
    s->blend_src_alpha = g.blend_src_alpha; s->blend_dst_alpha = g.blend_dst_alpha;
    s->blend_eq_rgb = g.blend_eq_rgb; s->blend_eq_alpha = g.blend_eq_alpha;
    for (int i = 0; i < 4; i++) s->blend_color[i] = g.blend_color[i];
    s->alpha_func = g.alpha_func; s->alpha_ref = g.alpha_ref; s->logic_op = g.logic_op;
    s->color_mask[0] = g.color_mask_r; s->color_mask[1] = g.color_mask_g;
    s->color_mask[2] = g.color_mask_b; s->color_mask[3] = g.color_mask_a;
    s->cull_face_mode = g.cull_face_mode; s->front_face = g.front_face;
    s->poly_off_factor = g.polygon_offset_factor; s->poly_off_units = g.polygon_offset_units;
    s->scissor_x = g.scissor_x; s->scissor_y = g.scissor_y;
    s->scissor_w = g.scissor_w; s->scissor_h = g.scissor_h;
    s->fog_mode = g.fog_mode; s->fog_density = g.fog_density;
    s->fog_start = g.fog_start; s->fog_end = g.fog_end;
    for (int i = 0; i < 4; i++) s->fog_color[i] = g.fog_color[i];
    s->stencil_func = g.stencil_func; s->stencil_ref = g.stencil_ref;
    s->stencil_mask = g.stencil_mask; s->stencil_write_mask = g.stencil_write_mask;
    s->stencil_op_fail = g.stencil_op_fail; s->stencil_op_zfail = g.stencil_op_zfail;
    s->stencil_op_zpass = g.stencil_op_zpass; s->clear_stencil = g.clear_stencil;
    s->shade_model = g.shade_model; s->mat_shininess = g.mat_shininess;
    for (int i = 0; i < 4; i++) { s->mat_ambient[i]=g.mat_ambient[i]; s->mat_diffuse[i]=g.mat_diffuse[i];
        s->mat_specular[i]=g.mat_specular[i]; s->mat_emission[i]=g.mat_emission[i];
        s->light_model_ambient[i]=g.light_model_ambient[i]; }
    for (int i = 0; i < NOVA_MAX_LIGHTS; i++) s->lights[i] = g.lights[i];
    for (int i = 0; i < 4; i++) s->cur_color[i] = g.cur_color[i];
    for (int i = 0; i < 3; i++) s->cur_normal[i] = g.cur_normal[i];
}

void glPopAttrib(void) {
    if (attrib_stack_ptr <= 0) {
        gl_set_error(GL_STACK_UNDERFLOW);
        return;
    }
    AttribState *s = &attrib_stack[--attrib_stack_ptr];
    GLbitfield m = s->mask;
    int touched_stencil = 0, touched_depthmap = 0;

    if (m & GL_ENABLE_BIT) {
        g.depth_test_enabled = s->depth_test; g.blend_enabled = s->blend;
        g.alpha_test_enabled = s->alpha_test; g.cull_face_enabled = s->cull_face;
        g.scissor_test_enabled = s->scissor_test; g.fog_enabled = s->fog;
        g.stencil_test_enabled = s->stencil_test; g.lighting_enabled = s->lighting;
        g.color_logic_op_enabled = s->color_logic_op;
        g.polygon_offset_fill_enabled = s->polygon_offset_fill; g.line_smooth_enabled = s->line_smooth;
        for (int u = 0; u < 3; u++) g.texture_2d_enabled_unit[u] = s->texture_2d_units[u];
        for (int i = 0; i < NOVA_MAX_LIGHTS; i++) g.lights[i].enabled = s->lights_enabled[i];
        touched_stencil = touched_depthmap = 1; g.light_dirty = 1;
    }
    if (m & GL_DEPTH_BUFFER_BIT) {
        g.depth_test_enabled = s->depth_test;
        g.depth_func = s->depth_func;
        g.gpu_depth_func = gl_to_gpu_depth_testfunc(g.depth_func);
        g.gpu_early_depth_func = gl_to_gpu_earlydepthfunc(g.depth_func);
        g.depth_mask = s->depth_mask;
    }
    if (m & GL_COLOR_BUFFER_BIT) {
        g.blend_enabled = s->blend; g.blend_src = s->blend_src; g.blend_dst = s->blend_dst;
        g.blend_src_alpha = s->blend_src_alpha; g.blend_dst_alpha = s->blend_dst_alpha;
        g.gpu_blend_src = gl_to_gpu_blendfactor(g.blend_src);
        g.gpu_blend_dst = gl_to_gpu_blendfactor(g.blend_dst);
        g.gpu_blend_src_alpha = gl_to_gpu_blendfactor(g.blend_src_alpha);
        g.gpu_blend_dst_alpha = gl_to_gpu_blendfactor(g.blend_dst_alpha);
        g.blend_eq_rgb = s->blend_eq_rgb; g.blend_eq_alpha = s->blend_eq_alpha;
        g.gpu_blend_eq_rgb = gl_to_gpu_blendeq(g.blend_eq_rgb);
        g.gpu_blend_eq_alpha = gl_to_gpu_blendeq(g.blend_eq_alpha);
        for (int i = 0; i < 4; i++) g.blend_color[i] = s->blend_color[i];
        g.alpha_test_enabled = s->alpha_test;
        g.alpha_func = s->alpha_func;
        g.gpu_alpha_func = gl_to_gpu_testfunc(g.alpha_func);
        g.alpha_ref = s->alpha_ref;
        g.color_logic_op_enabled = s->color_logic_op; g.logic_op = s->logic_op;
        g.gpu_logic_op = gl_to_gpu_logicop(g.logic_op);
        g.color_mask_r = s->color_mask[0]; g.color_mask_g = s->color_mask[1];
        g.color_mask_b = s->color_mask[2]; g.color_mask_a = s->color_mask[3];
    }
    if (m & GL_POLYGON_BIT) {
        g.cull_face_enabled = s->cull_face; g.cull_face_mode = s->cull_face_mode; g.front_face = s->front_face;
        g.polygon_offset_fill_enabled = s->polygon_offset_fill;
        g.polygon_offset_factor = s->poly_off_factor; g.polygon_offset_units = s->poly_off_units;
        touched_depthmap = 1;
    }
    if (m & GL_SCISSOR_BIT) {
        g.scissor_test_enabled = s->scissor_test;
        g.scissor_x = s->scissor_x; g.scissor_y = s->scissor_y;
        g.scissor_w = s->scissor_w; g.scissor_h = s->scissor_h;
    }
    if (m & GL_FOG_BIT) {
        g.fog_enabled = s->fog; g.fog_mode = s->fog_mode; g.fog_density = s->fog_density;
        g.fog_start = s->fog_start; g.fog_end = s->fog_end;
        for (int i = 0; i < 4; i++) g.fog_color[i] = s->fog_color[i];
        g.fog_dirty = 1;
    }
    if (m & GL_STENCIL_BUFFER_BIT) {
        g.stencil_test_enabled = s->stencil_test; g.stencil_func = s->stencil_func;
        g.gpu_stencil_func = stencil_func_to_gpu(g.stencil_func);
        g.stencil_ref = s->stencil_ref; g.stencil_mask = s->stencil_mask;
        g.stencil_write_mask = s->stencil_write_mask; g.stencil_op_fail = s->stencil_op_fail;
        g.stencil_op_zfail = s->stencil_op_zfail; g.stencil_op_zpass = s->stencil_op_zpass;
        g.gpu_stencil_op_fail = stencil_op_to_gpu(g.stencil_op_fail);
        g.gpu_stencil_op_zfail = stencil_op_to_gpu(g.stencil_op_zfail);
        g.gpu_stencil_op_zpass = stencil_op_to_gpu(g.stencil_op_zpass);
        g.clear_stencil = s->clear_stencil; touched_stencil = 1;
    }
    if (m & GL_LIGHTING_BIT) {
        g.lighting_enabled = s->lighting; g.shade_model = s->shade_model; g.mat_shininess = s->mat_shininess;
        for (int i = 0; i < 4; i++) { g.mat_ambient[i]=s->mat_ambient[i]; g.mat_diffuse[i]=s->mat_diffuse[i];
            g.mat_specular[i]=s->mat_specular[i]; g.mat_emission[i]=s->mat_emission[i];
            g.light_model_ambient[i]=s->light_model_ambient[i]; }
        for (int i = 0; i < NOVA_MAX_LIGHTS; i++) g.lights[i] = s->lights[i];
        g.light_dirty = 1;
    }
    if (m & GL_CURRENT_BIT) {
        for (int i = 0; i < 4; i++) g.cur_color[i] = s->cur_color[i];
        for (int i = 0; i < 3; i++) g.cur_normal[i] = s->cur_normal[i];
    }

    /* Re-emit the immediately-pushed GPU registers the restore touched
     * (apply_gpu_state rebuilds depth/blend/cull/scissor/alpha from g.* per
     * draw, but stencil and polygon-offset are pushed eagerly). */
#ifndef NOVAGL_DISABLE_STENCIL
    if (touched_stencil) {
        C3D_StencilTest(g.stencil_test_enabled, stencil_func_to_gpu(g.stencil_func),
                        g.stencil_ref, (u8)g.stencil_mask, (u8)g.stencil_write_mask);
    }
#endif
    if (touched_depthmap) apply_depth_map();
    g.tev_dirty = 1;
    g.state_dirty_bits = NOVA_DIRTY_ALL;
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