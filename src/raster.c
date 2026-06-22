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
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    g.vp_x = x;
    g.vp_y = y;
    g.vp_w = width;
    g.vp_h = height;

    if (g.bound_fbo == 0) {
        C3D_SetViewport(y, x, height, width);
    } else {
        C3D_SetViewport(x, y, width, height);
    }
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    g.scissor_x = x;
    g.scissor_y = y;
    g.scissor_w = width;
    g.scissor_h = height;
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
        case GL_SRC_ALPHA_SATURATE:
            /* ES 1.1: valid for src only. */
            return is_src;
        default:
            return 0;
    }
}

void glDepthFunc(GLenum func) {
    if (!is_valid_cmp_func(func)) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.depth_func = func;
}

void glDepthMask(GLboolean flag) { g.depth_mask = flag; }

void glDepthRangef(GLclampf near_val, GLclampf far_val) {
    g.depth_near = clampf(near_val, 0.0f, 1.0f);
    g.depth_far = clampf(far_val, 0.0f, 1.0f);
    apply_depth_map();
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (!is_valid_blend_factor(sfactor, 1) || !is_valid_blend_factor(dfactor, 0)) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.blend_src = sfactor;
    g.blend_dst = dfactor;
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
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.blend_eq_rgb = modeRGB;
    g.blend_eq_alpha = modeAlpha;
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
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.alpha_func = func;
    /* Spec: ref is clamped to [0,1] at specification time. */
    g.alpha_ref = clampf(ref, 0.0f, 1.0f);
}

void glCullFace(GLenum mode) {
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.cull_face_mode = mode;
}

void glFrontFace(GLenum mode) {
    if (mode != GL_CW && mode != GL_CCW) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.front_face = mode;
}

void glColorMask(GLboolean r, GLboolean g_, GLboolean b, GLboolean a) {
    g.color_mask_r = r;
    g.color_mask_g = g_;
    g.color_mask_b = b;
    g.color_mask_a = a;
}

void glShadeModel(GLenum mode) {
    if (mode == GL_FLAT || mode == GL_SMOOTH)
        g.shade_model = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    g.polygon_offset_factor = factor;
    g.polygon_offset_units = units;
    apply_depth_map();
}

void glLineWidth(GLfloat width) { (void) width; }

void glPolygonMode(GLenum face, GLenum mode) {
    (void) face;
    (void) mode;
}


void glDepthRange(GLclampd near_val, GLclampd far_val) {
    g.depth_near = clampf((GLfloat) near_val, 0.0f, 1.0f);
    g.depth_far = clampf((GLfloat) far_val, 0.0f, 1.0f);
    apply_depth_map();
}

void glClipPlane(GLenum plane, const GLdouble *equation) {
    (void) plane;
    (void) equation;
}

GPU_TESTFUNC stencil_func_to_gpu(GLenum f) {
    switch (f) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_LESS;
        case GL_LEQUAL:   return GPU_LEQUAL;
        case GL_GREATER:  return GPU_GREATER;
        case GL_GEQUAL:   return GPU_GEQUAL;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_ALWAYS:
        default:          return GPU_ALWAYS;
    }
}

static GPU_STENCILOP stencil_op_to_gpu(GLenum op) {
    switch (op) {
        case GL_ZERO:      return GPU_STENCIL_ZERO;
        case GL_REPLACE:   return GPU_STENCIL_REPLACE;
        case GL_INCR:      return GPU_STENCIL_INCR;
        case GL_DECR:      return GPU_STENCIL_DECR;
        /* PICA has no saturating-vs-wrapping distinction exposed here; the
         * wrap variants behave like plain INCR/DECR. Good enough for re3. */
        case GL_INCR_WRAP: return GPU_STENCIL_INCR;
        case GL_DECR_WRAP: return GPU_STENCIL_DECR;
        case GL_INVERT:    return GPU_STENCIL_INVERT;
        case GL_KEEP:
        default:           return GPU_STENCIL_KEEP;
    }
}

/* The real stencil path was wired up in #14 (Group E). If it turns out the
 * upstream caller (e.g. PD's fast3d port) hits a libctru/citro3d assert
 * inside C3D_StencilTest/Op with the values it passes, defining
 * NOVAGL_DISABLE_STENCIL=1 reverts these to no-ops and the buffer just
 * sits unused — same behaviour as before the patch. */
#ifdef NOVAGL_DISABLE_STENCIL
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    g.stencil_func = func; g.stencil_ref = ref; g.stencil_mask = mask;
}
void glStencilMask(GLuint mask) { g.stencil_write_mask = mask; }
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    g.stencil_op_fail = fail; g.stencil_op_zfail = zfail; g.stencil_op_zpass = zpass;
}
#else
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    g.stencil_func = func;
    g.stencil_ref  = ref;
    g.stencil_mask = mask;
    C3D_StencilTest(g.stencil_test_enabled, stencil_func_to_gpu(func),
                    ref, (u8)mask, (u8)g.stencil_write_mask);
}

void glStencilMask(GLuint mask) {
    g.stencil_write_mask = mask;
    C3D_StencilTest(g.stencil_test_enabled, stencil_func_to_gpu(g.stencil_func),
                    g.stencil_ref, (u8)g.stencil_mask, (u8)mask);
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    g.stencil_op_fail  = fail;
    g.stencil_op_zfail = zfail;
    g.stencil_op_zpass = zpass;
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