//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

/* GL spec: signed integer color components normalize as (2c+1)/(2^N-1) clamped
 * to [-1, 1]; since framebuffer storage is unsigned [0, 1], we clamp to that
 * range after. Unsigned variants are c/(2^N-1) which is already in [0, 1]. */
static inline GLfloat norm_b(GLbyte c)   { return clampf((2.0f * (float)c + 1.0f) / 255.0f,        0.0f, 1.0f); }
static inline GLfloat norm_s(GLshort c)  { return clampf((2.0f * (float)c + 1.0f) / 65535.0f,      0.0f, 1.0f); }
static inline GLfloat norm_i(GLint c)    { return clampf((2.0f * (float)c + 1.0f) / 4294967295.0f, 0.0f, 1.0f); }
static inline GLfloat norm_ub(GLubyte c) { return (float)c / 255.0f; }
static inline GLfloat norm_us(GLushort c){ return (float)c / 65535.0f; }
static inline GLfloat norm_ui(GLuint c)  { return (float)c / 4294967295.0f; }

void glColor4f(GLfloat r, GLfloat g_, GLfloat b, GLfloat a) {
    g.cur_color[0] = r;
    g.cur_color[1] = g_;
    g.cur_color[2] = b;
    g.cur_color[3] = a;
}

void glColor3f(GLfloat r, GLfloat g_, GLfloat b) {
    if (g.dl_recording >= 0) {
        dl_record_color3f(r, g_, b);
        return;
    }
    g.cur_color[0] = r;
    g.cur_color[1] = g_;
    g.cur_color[2] = b;
    g.cur_color[3] = 1.0f;
}

void glColor4ub(GLubyte r, GLubyte g_, GLubyte b, GLubyte a) {
    g.cur_color[0] = r / 255.0f;
    g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f;
    g.cur_color[3] = a / 255.0f;
}

void glColor3ub(GLubyte r, GLubyte g_, GLubyte b) {
    g.cur_color[0] = r / 255.0f;
    g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f;
    g.cur_color[3] = 1.0f;
}

/* Additional glColor variants */
void glColor3b(GLbyte r, GLbyte g_, GLbyte b) {
    g.cur_color[0] = norm_b(r);
    g.cur_color[1] = norm_b(g_);
    g.cur_color[2] = norm_b(b);
    g.cur_color[3] = 1.0f;
}

void glColor3bv(const GLbyte *v) {
    if (v) glColor3b(v[0], v[1], v[2]);
}

void glColor3d(GLdouble r, GLdouble g_, GLdouble b) {
    g.cur_color[0] = (GLfloat) r;
    g.cur_color[1] = (GLfloat) g_;
    g.cur_color[2] = (GLfloat) b;
    g.cur_color[3] = 1.0f;
}

void glColor3dv(const GLdouble *v) {
    if (v) glColor3d(v[0], v[1], v[2]);
}

void glColor3fv(const GLfloat *v) {
    if (v) glColor3f(v[0], v[1], v[2]);
}

void glColor3i(GLint r, GLint g_, GLint b) {
    g.cur_color[0] = norm_i(r);
    g.cur_color[1] = norm_i(g_);
    g.cur_color[2] = norm_i(b);
    g.cur_color[3] = 1.0f;
}

void glColor3iv(const GLint *v) {
    if (v) glColor3i(v[0], v[1], v[2]);
}

void glColor3s(GLshort r, GLshort g_, GLshort b) {
    g.cur_color[0] = norm_s(r);
    g.cur_color[1] = norm_s(g_);
    g.cur_color[2] = norm_s(b);
    g.cur_color[3] = 1.0f;
}

void glColor3sv(const GLshort *v) {
    if (v) glColor3s(v[0], v[1], v[2]);
}

void glColor3ubv(const GLubyte *v) {
    if (v) glColor3ub(v[0], v[1], v[2]);
}

void glColor3ui(GLuint r, GLuint g_, GLuint b) {
    g.cur_color[0] = norm_ui(r);
    g.cur_color[1] = norm_ui(g_);
    g.cur_color[2] = norm_ui(b);
    g.cur_color[3] = 1.0f;
}

void glColor3uiv(const GLuint *v) {
    if (v) glColor3ui(v[0], v[1], v[2]);
}

void glColor3us(GLushort r, GLushort g_, GLushort b) {
    g.cur_color[0] = norm_us(r);
    g.cur_color[1] = norm_us(g_);
    g.cur_color[2] = norm_us(b);
    g.cur_color[3] = 1.0f;
}

void glColor3usv(const GLushort *v) {
    if (v) glColor3us(v[0], v[1], v[2]);
}

void glColor4b(GLbyte r, GLbyte g_, GLbyte b, GLbyte a) {
    g.cur_color[0] = norm_b(r);
    g.cur_color[1] = norm_b(g_);
    g.cur_color[2] = norm_b(b);
    g.cur_color[3] = norm_b(a);
}

void glColor4bv(const GLbyte *v) {
    if (v) glColor4b(v[0], v[1], v[2], v[3]);
}

void glColor4d(GLdouble r, GLdouble g_, GLdouble b, GLdouble a) {
    g.cur_color[0] = (GLfloat) r;
    g.cur_color[1] = (GLfloat) g_;
    g.cur_color[2] = (GLfloat) b;
    g.cur_color[3] = (GLfloat) a;
}

void glColor4dv(const GLdouble *v) {
    if (v) glColor4d(v[0], v[1], v[2], v[3]);
}

void glColor4fv(const GLfloat *v) {
    if (v) glColor4f(v[0], v[1], v[2], v[3]);
}

void glColor4i(GLint r, GLint g_, GLint b, GLint a) {
    g.cur_color[0] = norm_i(r);
    g.cur_color[1] = norm_i(g_);
    g.cur_color[2] = norm_i(b);
    g.cur_color[3] = norm_i(a);
}

void glColor4iv(const GLint *v) {
    if (v) glColor4i(v[0], v[1], v[2], v[3]);
}

void glColor4s(GLshort r, GLshort g_, GLshort b, GLshort a) {
    g.cur_color[0] = norm_s(r);
    g.cur_color[1] = norm_s(g_);
    g.cur_color[2] = norm_s(b);
    g.cur_color[3] = norm_s(a);
}

void glColor4sv(const GLshort *v) {
    if (v) glColor4s(v[0], v[1], v[2], v[3]);
}

void glColor4ubv(const GLubyte *v) {
    if (v) glColor4ub(v[0], v[1], v[2], v[3]);
}

void glColor4ui(GLuint r, GLuint g_, GLuint b, GLuint a) {
    g.cur_color[0] = norm_ui(r);
    g.cur_color[1] = norm_ui(g_);
    g.cur_color[2] = norm_ui(b);
    g.cur_color[3] = norm_ui(a);
}

void glColor4uiv(const GLuint *v) {
    if (v) glColor4ui(v[0], v[1], v[2], v[3]);
}

void glColor4us(GLushort r, GLushort g_, GLushort b, GLushort a) {
    g.cur_color[0] = norm_us(r);
    g.cur_color[1] = norm_us(g_);
    g.cur_color[2] = norm_us(b);
    g.cur_color[3] = norm_us(a);
}

void glColor4usv(const GLushort *v) {
    if (v) glColor4us(v[0], v[1], v[2], v[3]);
}

void glColorMaterial(GLenum face, GLenum mode) {
    (void) face;
    (void) mode;
    /* Color material not implemented - PICA200 doesn't support it directly */
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    (void) target;
    (void) s;
    (void) t;
    (void) r;
    (void) q;
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    (void) nx;
    (void) ny;
    (void) nz;
}

/* Additional glNormal variants */
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz) {
    (void) nx;
    (void) ny;
    (void) nz;
}

void glNormal3bv(const GLbyte *v) {
    if (v) glNormal3b(v[0], v[1], v[2]);
}

void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) {
    (void) nx;
    (void) ny;
    (void) nz;
}

void glNormal3dv(const GLdouble *v) {
    if (v) glNormal3d(v[0], v[1], v[2]);
}

void glNormal3fv(const GLfloat *v) {
    if (v) glNormal3f(v[0], v[1], v[2]);
}

void glNormal3i(GLint nx, GLint ny, GLint nz) {
    (void) nx;
    (void) ny;
    (void) nz;
}

void glNormal3iv(const GLint *v) {
    if (v) glNormal3i(v[0], v[1], v[2]);
}

void glNormal3s(GLshort nx, GLshort ny, GLshort nz) {
    (void) nx;
    (void) ny;
    (void) nz;
}

void glNormal3sv(const GLshort *v) {
    if (v) glNormal3s(v[0], v[1], v[2]);
}
