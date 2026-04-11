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

void glShadeModel(GLenum mode) {
    if (mode == GL_FLAT || mode == GL_SMOOTH)
        g.shade_model = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    g.polygon_offset_factor = factor;
    g.polygon_offset_units = units;
    apply_depth_map();
}

void glLineWidth(GLfloat width) { (void)width; }

void glPolygonMode(GLenum face, GLenum mode) { (void)face; (void)mode; }


void glDepthRange(GLclampd near_val, GLclampd far_val) {
    g.depth_near = clampf((GLfloat)near_val, 0.0f, 1.0f);
    g.depth_far  = clampf((GLfloat)far_val, 0.0f, 1.0f);
    apply_depth_map();
}

void glClipPlane(GLenum plane, const GLdouble *equation) {
    (void)plane; (void)equation;
    /* Clip planes not supported on PICA200 */
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    (void)func; (void)ref; (void)mask;
    /* Stencil testing not fully implemented */
}

void glStencilMask(GLuint mask) {
    (void)mask;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    (void)fail; (void)zfail; (void)zpass;
}

/* ===[ GL 2.0+ Shader pipeline stubs ]=== */
/* PICA200 uses fixed-function; these exist for source compatibility only */

GLuint glCreateShader(GLenum type) { (void)type; return 1; }

void glShaderSource(GLuint shader, GLsizei count, const char **string, const GLint *length) {
    (void)shader; (void)count; (void)string; (void)length;
}

void glCompileShader(GLuint shader) { (void)shader; }

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    (void)shader;
    if (params) {
        if (pname == GL_COMPILE_STATUS) *params = GL_TRUE;
        else *params = 0;
    }
}

void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, char *infoLog) {
    (void)shader;
    if (length) *length = 0;
    if (infoLog && maxLength > 0) infoLog[0] = '\0';
}

GLuint glCreateProgram(void) { return 1; }

void glAttachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }

void glLinkProgram(GLuint program) { (void)program; }

void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    (void)program;
    if (params) {
        if (pname == GL_LINK_STATUS) *params = GL_TRUE;
        else *params = 0;
    }
}

void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, char *infoLog) {
    (void)program;
    if (length) *length = 0;
    if (infoLog && maxLength > 0) infoLog[0] = '\0';
}

void glDeleteShader(GLuint shader) { (void)shader; }

void glDeleteProgram(GLuint program) { (void)program; }

GLint glGetUniformLocation(GLuint program, const char *name) { (void)program; (void)name; return -1; }

GLint glGetAttribLocation(GLuint program, const char *name) { (void)program; (void)name; return -1; }

void glUseProgram(GLuint program) { (void)program; }

void glUniform1i(GLint location, GLint v0) { (void)location; (void)v0; }

void glUniform1f(GLint location, GLfloat v0) { (void)location; (void)v0; }

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { (void)location; (void)v0; (void)v1; }

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { (void)location; (void)v0; (void)v1; (void)v2; }

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { (void)location; (void)v0; (void)v1; (void)v2; (void)v3; }

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    (void)location; (void)count; (void)transpose; (void)value;
}
