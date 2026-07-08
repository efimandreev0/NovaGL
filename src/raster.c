//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

/* Value fixed by the GL spec; defined locally if NovaGL.h lacks it. */
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif


void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    /* Redundant-set filter (DMP does this in every state setter): engines
     * re-send the same viewport every frame/camera. Skipping the re-apply also
     * skips a nova_batch_flush — a batched PD run survives a no-op glViewport. */
    int fbo0 = (g.bound_fbo == 0);
    if (g.vp_applied_valid && g.vp_applied_fbo0 == fbo0 &&
        g.vp_applied_x == x && g.vp_applied_y == y &&
        g.vp_applied_w == width && g.vp_applied_h == height) {
        g.vp_x = x; g.vp_y = y; g.vp_w = width; g.vp_h = height;
        return;
    }
    /* C3D_SetViewport below records an immediate viewport command; a pending
     * batch must be drawn under the OLD viewport first. */
    nova_batch_flush();
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = width;
    g.vp_h = height;

    if (g.bound_fbo == 0) {
        C3D_SetViewport(y, x, height, width);
    } else {
        C3D_SetViewport(x, y, width, height);
    }
    g.vp_applied_x = x;
    g.vp_applied_y = y;
    g.vp_applied_w = width;
    g.vp_applied_h = height;
    g.vp_applied_fbo0 = fbo0;
    g.vp_applied_valid = 1;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (g.scissor_x == x && g.scissor_y == y &&
        g.scissor_w == width && g.scissor_h == height)
        return;
    g.scissor_x = x;
    g.scissor_y = y;
    g.scissor_w = width;
    g.scissor_h = height;
    g.state_dirty_bits |= NOVA_DIRTY_SCISSOR;
}

/* Shared validator: the eight GL comparison funcs used by depth/alpha/stencil. */
static int is_valid_cmp_func(GLenum f) {
    switch (f) {
        case GL_NEVER: case GL_LESS: case GL_EQUAL: case GL_LEQUAL:
        case GL_GREATER: case GL_NOTEQUAL: case GL_GEQUAL: case GL_ALWAYS:
            return 1;
        default:
            return 0;
    }
}

static int is_valid_blend_factor(GLenum f, int is_src) {
    switch (f) {
        case GL_ZERO: case GL_ONE:
        case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR:
        case GL_DST_COLOR: case GL_ONE_MINUS_DST_COLOR:
        case GL_SRC_ALPHA: case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA: case GL_ONE_MINUS_DST_ALPHA:
            return 1;
        case GL_CONSTANT_COLOR: case GL_ONE_MINUS_CONSTANT_COLOR:
        case GL_CONSTANT_ALPHA: case GL_ONE_MINUS_CONSTANT_ALPHA:
            /* Constant-colour factors (glBlendColor); valid on both ends. */
            return 1;
        case GL_SRC_ALPHA_SATURATE:
            /* ES 1.1: valid for src only. */
            return is_src;
        default:
            return 0;
    }
}

void glDepthFunc(GLenum func) {
    if (!is_valid_cmp_func(func)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.depth_func == func) return;
    g.depth_func = func;
    g.gpu_depth_func = gl_to_gpu_depth_testfunc(func);
    g.gpu_early_depth_func = gl_to_gpu_earlydepthfunc(func);
    g.state_dirty_bits |= (NOVA_DIRTY_DEPTH_TEST | NOVA_DIRTY_EARLY_DEPTH);
}

void glDepthMask(GLboolean flag) {
    GLboolean f = flag ? GL_TRUE : GL_FALSE;
    if (g.depth_mask == f) return;
    g.depth_mask = f;
    g.state_dirty_bits |= NOVA_DIRTY_DEPTH_TEST;
}

void glDepthRangef(GLclampf near_val, GLclampf far_val) {
    GLfloat n = clampf(near_val, 0.0f, 1.0f);
    GLfloat f = clampf(far_val, 0.0f, 1.0f);
    if (g.depth_near == n && g.depth_far == f) return; /* skip flush + re-emit */
    nova_batch_flush();   /* apply_depth_map changes depth state for later draws */
    g.depth_near = n;
    g.depth_far = f;
    apply_depth_map();
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (!is_valid_blend_factor(sfactor, 1) || !is_valid_blend_factor(dfactor, 0)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.blend_src == sfactor && g.blend_dst == dfactor &&
        g.blend_src_alpha == sfactor && g.blend_dst_alpha == dfactor)
        return;
    g.blend_src = sfactor;
    g.blend_dst = dfactor;
    /* Non-separate: alpha factors mirror the colour factors. */
    g.blend_src_alpha = sfactor;
    g.blend_dst_alpha = dfactor;

    g.gpu_blend_src = gl_to_gpu_blendfactor(sfactor);
    g.gpu_blend_dst = gl_to_gpu_blendfactor(dfactor);
    g.gpu_blend_src_alpha = g.gpu_blend_src;
    g.gpu_blend_dst_alpha = g.gpu_blend_dst;
    g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE;
}

/* glBlendFuncSeparate: independent colour/alpha blend factors. PICA supports
 * this directly (C3D_AlphaBlend takes separate srcClr/dstClr and srcAlpha/
 * dstAlpha), so it's a real path, not a degrade to glBlendFunc. */
void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    if (!is_valid_blend_factor(srcRGB, 1) || !is_valid_blend_factor(dstRGB, 0) ||
        !is_valid_blend_factor(srcAlpha, 1) || !is_valid_blend_factor(dstAlpha, 0)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.blend_src == srcRGB && g.blend_dst == dstRGB &&
        g.blend_src_alpha == srcAlpha && g.blend_dst_alpha == dstAlpha)
        return;
    g.blend_src = srcRGB;       g.blend_dst = dstRGB;
    g.blend_src_alpha = srcAlpha; g.blend_dst_alpha = dstAlpha;

    g.gpu_blend_src = gl_to_gpu_blendfactor(srcRGB);
    g.gpu_blend_dst = gl_to_gpu_blendfactor(dstRGB);
    g.gpu_blend_src_alpha = gl_to_gpu_blendfactor(srcAlpha);
    g.gpu_blend_dst_alpha = gl_to_gpu_blendfactor(dstAlpha);
    g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE;
}

/* glBlendColor: the constant for GL_CONSTANT_COLOR/ALPHA factors. Stored as
 * 0..1 floats (clamped per spec); apply_gpu_state packs to PICA 0xAABBGGRR and
 * pushes via C3D_BlendingColor. */
void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    GLclampf r = clampf(red, 0.0f, 1.0f), gr = clampf(green, 0.0f, 1.0f);
    GLclampf b = clampf(blue, 0.0f, 1.0f), a = clampf(alpha, 0.0f, 1.0f);
    if (g.blend_color[0] == r && g.blend_color[1] == gr &&
        g.blend_color[2] == b && g.blend_color[3] == a)
        return;
    g.blend_color[0] = r;
    g.blend_color[1] = gr;
    g.blend_color[2] = b;
    g.blend_color[3] = a;

    // (Format 0xAABBGGRR) — packed names must not shadow the floats above.
    uint32_t pr = (uint32_t)(g.blend_color[0] * 255.0f + 0.5f);
    uint32_t pg = (uint32_t)(g.blend_color[1] * 255.0f + 0.5f);
    uint32_t pb = (uint32_t)(g.blend_color[2] * 255.0f + 0.5f);
    uint32_t pa = (uint32_t)(g.blend_color[3] * 255.0f + 0.5f);
    g.blend_color_packed = pr | (pg << 8) | (pb << 16) | (pa << 24);

    g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE;
}

/* glLogicOp: records the colour logic opcode. Only takes effect while
 * GL_COLOR_LOGIC_OP is enabled (see glEnable). Validated against the 16 GL
 * opcodes; anything else is GL_INVALID_ENUM with no state change. */
void glLogicOp(GLenum opcode) {
    switch (opcode) {
        case GL_CLEAR: case GL_AND: case GL_AND_REVERSE: case GL_COPY:
        case GL_AND_INVERTED: case GL_NOOP: case GL_XOR: case GL_OR:
        case GL_NOR: case GL_EQUIV: case GL_INVERT: case GL_OR_REVERSE:
        case GL_COPY_INVERTED: case GL_OR_INVERTED: case GL_NAND: case GL_SET:
            if (g.logic_op == opcode) break;
            g.logic_op = opcode;
            g.gpu_logic_op = gl_to_gpu_logicop(opcode);
            g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE;
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            break;
    }
}

static int is_valid_blend_equation(GLenum mode) {
    switch (mode) {
        case GL_FUNC_ADD:
        case GL_FUNC_SUBTRACT:
        case GL_FUNC_REVERSE_SUBTRACT:
        case GL_MIN:
        case GL_MAX:
            return 1;
        default:
            return 0;
    }
}

void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    if (!is_valid_blend_equation(modeRGB) || !is_valid_blend_equation(modeAlpha)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.blend_eq_rgb == modeRGB && g.blend_eq_alpha == modeAlpha) return;
    g.blend_eq_rgb = modeRGB;
    g.blend_eq_alpha = modeAlpha;

    g.gpu_blend_eq_rgb = gl_to_gpu_blendeq(modeRGB);
    g.gpu_blend_eq_alpha = gl_to_gpu_blendeq(modeAlpha);
    g.state_dirty_bits |= NOVA_DIRTY_BLEND_STATE;
}

void glBlendEquation(GLenum mode) {
    glBlendEquationSeparate(mode, mode);
}

/* GLES1 OES extension spellings — identical behaviour. */
void glBlendEquationOES(GLenum mode) {
    glBlendEquation(mode);
}

void glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha) {
    glBlendEquationSeparate(modeRGB, modeAlpha);
}

void glAlphaFunc(GLenum func, GLclampf ref) {
    if (!is_valid_cmp_func(func)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    /* Spec: ref is clamped to [0,1] at specification time. */
    GLclampf cref = clampf(ref, 0.0f, 1.0f);
    if (g.alpha_func == func && g.alpha_ref == cref) return;
    g.alpha_func = func;
    g.gpu_alpha_func = gl_to_gpu_testfunc(func);
    g.alpha_ref = cref;
    g.alpha_ref8 = (uint8_t)(g.alpha_ref * 255.0f + 0.5f);

    g.state_dirty_bits |= (NOVA_DIRTY_ALPHA_TEST | NOVA_DIRTY_EARLY_DEPTH);
}

void glCullFace(GLenum mode) {
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.cull_face_mode == mode) return;
    g.cull_face_mode = mode;
    g.state_dirty_bits |= NOVA_DIRTY_CULLING;
}

void glFrontFace(GLenum mode) {
    if (mode != GL_CW && mode != GL_CCW) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (g.front_face == mode) return;
    g.front_face = mode;
    g.state_dirty_bits |= NOVA_DIRTY_CULLING;
}

void glColorMask(GLboolean r, GLboolean g_, GLboolean b, GLboolean a) {
    GLboolean nr = r ? GL_TRUE : GL_FALSE, ng = g_ ? GL_TRUE : GL_FALSE;
    GLboolean nb = b ? GL_TRUE : GL_FALSE, na = a ? GL_TRUE : GL_FALSE;
    if (g.color_mask_r == nr && g.color_mask_g == ng &&
        g.color_mask_b == nb && g.color_mask_a == na)
        return;
    g.color_mask_r = nr;
    g.color_mask_g = ng;
    g.color_mask_b = nb;
    g.color_mask_a = na;
    g.state_dirty_bits |= NOVA_DIRTY_DEPTH_TEST;
}

void glShadeModel(GLenum mode) {
    if (mode == GL_FLAT || mode == GL_SMOOTH)
        g.shade_model = mode;
    else
        gl_set_error(GL_INVALID_ENUM);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    /* Redundant-set filter: PD's r_set_depth_mode re-sends the same offset per
     * ZMODE between batched draws — the unconditional flush here used to break
     * every clipspace batch run for a no-op re-emit. */
    if (g.polygon_offset_factor == factor && g.polygon_offset_units == units)
        return;
    nova_batch_flush();   /* apply_depth_map changes the depth bias for later draws */
    g.polygon_offset_factor = factor;
    g.polygon_offset_units = units;
    apply_depth_map();
}

void glLineWidth(GLfloat width) {
    /* PICA has no programmable line width (always 1px), but a non-positive
     * width is still GL_INVALID_VALUE. */
    if (width <= 0.0f) gl_set_error(GL_INVALID_VALUE);
}

void glPolygonMode(GLenum face, GLenum mode) {
    /* No wireframe/point fill on PICA (no-op), but enums must be validated. */
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (mode != GL_FILL && mode != GL_LINE && mode != GL_POINT) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}


void glDepthRange(GLclampd near_val, GLclampd far_val) {
    glDepthRangef((GLclampf) near_val, (GLclampf) far_val);
}

void glClipPlane(GLenum plane, const GLdouble *equation) {
    (void) plane;
    (void) equation;
}


static int is_valid_stencil_op(GLenum op) {
    switch (op) {
        case GL_KEEP: case GL_ZERO: case GL_REPLACE: case GL_INCR:
        case GL_DECR: case GL_INVERT: case GL_INCR_WRAP: case GL_DECR_WRAP:
            return 1;
        default:
            return 0;
    }
}


/* The real stencil path was wired up in #14 (Group E). If it turns out the
 * upstream caller (e.g. PD's fast3d port) hits a libctru/citro3d assert
 * inside C3D_StencilTest/Op with the values it passes, defining
 * NOVAGL_DISABLE_STENCIL=1 reverts these to no-ops and the buffer just
 * sits unused — same behaviour as before the patch. */
#ifdef NOVAGL_DISABLE_STENCIL
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    if (!is_valid_cmp_func(func)) { gl_set_error(GL_INVALID_ENUM); return; }
    g.stencil_func = func; g.stencil_ref = ref; g.stencil_mask = mask;
}
void glStencilMask(GLuint mask) { g.stencil_write_mask = mask; }
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    if (!is_valid_stencil_op(fail) || !is_valid_stencil_op(zfail) || !is_valid_stencil_op(zpass)) {
        gl_set_error(GL_INVALID_ENUM); return;
    }
    g.stencil_op_fail = fail; g.stencil_op_zfail = zfail; g.stencil_op_zpass = zpass;
}
#else
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    /* Spec: bad func is GL_INVALID_ENUM with no state change / no GPU write. */
    if (!is_valid_cmp_func(func)) { gl_set_error(GL_INVALID_ENUM); return; }
    if (g.stencil_func == func && g.stencil_ref == ref && g.stencil_mask == mask)
        return; /* skip the eager GPU write */
    g.stencil_func = func;
    g.gpu_stencil_func = stencil_func_to_gpu(func);
    g.stencil_ref  = ref;
    g.stencil_mask = mask;
    C3D_StencilTest(g.stencil_test_enabled, stencil_func_to_gpu(func),
                    ref, (u8)mask, (u8)g.stencil_write_mask);
}

void glStencilMask(GLuint mask) {
    if (g.stencil_write_mask == mask) return;
    g.stencil_write_mask = mask;
    C3D_StencilTest(g.stencil_test_enabled, stencil_func_to_gpu(g.stencil_func),
                    g.stencil_ref, (u8)g.stencil_mask, (u8)mask);
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    if (!is_valid_stencil_op(fail) || !is_valid_stencil_op(zfail) || !is_valid_stencil_op(zpass)) {
        gl_set_error(GL_INVALID_ENUM); return;
    }
    if (g.stencil_op_fail == fail && g.stencil_op_zfail == zfail &&
        g.stencil_op_zpass == zpass)
        return;
    g.stencil_op_fail  = fail;
    g.stencil_op_zfail = zfail;
    g.stencil_op_zpass = zpass;

    g.gpu_stencil_op_fail = stencil_op_to_gpu(fail);
    g.gpu_stencil_op_zfail = stencil_op_to_gpu(zfail);
    g.gpu_stencil_op_zpass = stencil_op_to_gpu(zpass);
    C3D_StencilOp(stencil_op_to_gpu(fail), stencil_op_to_gpu(zfail), stencil_op_to_gpu(zpass));
}
#endif

// ===[ GL 2.0+ shader stubs ]===
// Nova is fixed-function only, no GLSL on PICA. default we return 0 so apps that
// do `if (glCreateProgram())` fall back to FFP nicely. some ports (fast3d) dont
// check and call glUseProgram anyway - for them set NOVAGL_GL2_RETURN_DUMMY=1
// and we lie that everything compiled ok. vodka balalaika.
#ifndef NOVAGL_GL2_RETURN_DUMMY
#define NOVAGL_GL2_RETURN_DUMMY 0
#endif

GLuint glCreateShader(GLenum type) {
    (void) type;
    return NOVAGL_GL2_RETURN_DUMMY;
}

void glShaderSource(GLuint shader, GLsizei count, const char **string, const GLint *length) {
    (void) shader;
    (void) count;
    (void) string;
    (void) length;
}

void glCompileShader(GLuint shader) { (void) shader; }

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    (void) shader;
    if (params) {
        if (pname == GL_COMPILE_STATUS)
            *params = NOVAGL_GL2_RETURN_DUMMY ? GL_TRUE : GL_FALSE;
        else *params = 0;
    }
}

void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, char *infoLog) {
    (void) shader;
    if (length) *length = 0;
    if (infoLog && maxLength > 0) infoLog[0] = '\0';
}

GLuint glCreateProgram(void) { return NOVAGL_GL2_RETURN_DUMMY; }

void glAttachShader(GLuint program, GLuint shader) {
    (void) program;
    (void) shader;
}

void glLinkProgram(GLuint program) { (void) program; }

void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    (void) program;
    if (params) {
        if (pname == GL_LINK_STATUS)
            *params = NOVAGL_GL2_RETURN_DUMMY ? GL_TRUE : GL_FALSE;
        else *params = 0;
    }
}

void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, char *infoLog) {
    (void) program;
    if (length) *length = 0;
    if (infoLog && maxLength > 0) infoLog[0] = '\0';
}

void glDeleteShader(GLuint shader) { (void) shader; }
void glDeleteProgram(GLuint program) { (void) program; }

GLint glGetUniformLocation(GLuint program, const char *name) {
    (void) program;
    (void) name;
    return -1;
}

GLint glGetAttribLocation(GLuint program, const char *name) {
    (void) program;
    (void) name;
    return -1;
}

void glUseProgram(GLuint program) { (void) program; }

void glUniform1i(GLint location, GLint v0) {
    (void) location;
    (void) v0;
}

void glUniform1f(GLint location, GLfloat v0) {
    (void) location;
    (void) v0;
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    (void) location;
    (void) v0;
    (void) v1;
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    (void) location;
    (void) v0;
    (void) v1;
    (void) v2;
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    (void) location;
    (void) v0;
    (void) v1;
    (void) v2;
    (void) v3;
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    (void) location;
    (void) count;
    (void) transpose;
    (void) value;
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
    (void) location;
    (void) count;
    (void) value;
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    (void) location;
    (void) count;
    (void) value;
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value) {
    (void) location;
    (void) count;
    (void) value;
}

void glBindAttribLocation(GLuint program, GLuint index, const char *name) {
    (void) program;
    (void) index;
    (void) name;
}

GLuint glGetUniformBlockIndex(GLuint program, const char *uniformBlockName) {
    (void) program;
    (void) uniformBlockName;
    return 0;
}

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
    (void) program;
    (void) uniformBlockIndex;
    (void) uniformBlockBinding;
}