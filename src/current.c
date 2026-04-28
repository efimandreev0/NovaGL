//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

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
    g.cur_color[0] = (r + 128) / 255.0f;
    g.cur_color[1] = (g_ + 128) / 255.0f;
    g.cur_color[2] = (b + 128) / 255.0f;
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
    g.cur_color[0] = r / 2147483647.0f;
    g.cur_color[1] = g_ / 2147483647.0f;
    g.cur_color[2] = b / 2147483647.0f;
    g.cur_color[3] = 1.0f;
}

void glColor3iv(const GLint *v) {
    if (v) glColor3i(v[0], v[1], v[2]);
}

void glColor3s(GLshort r, GLshort g_, GLshort b) {
    g.cur_color[0] = r / 32767.0f;
    g.cur_color[1] = g_ / 32767.0f;
    g.cur_color[2] = b / 32767.0f;
    g.cur_color[3] = 1.0f;
}

void glColor3sv(const GLshort *v) {
    if (v) glColor3s(v[0], v[1], v[2]);
}

void glColor3ubv(const GLubyte *v) {
    if (v) glColor3ub(v[0], v[1], v[2]);
}

void glColor3ui(GLuint r, GLuint g_, GLuint b) {
    g.cur_color[0] = r / 4294967295.0f;
    g.cur_color[1] = g_ / 4294967295.0f;
    g.cur_color[2] = b / 4294967295.0f;
    g.cur_color[3] = 1.0f;
}

void glColor3uiv(const GLuint *v) {
    if (v) glColor3ui(v[0], v[1], v[2]);
}

void glColor3us(GLushort r, GLushort g_, GLushort b) {
    g.cur_color[0] = r / 65535.0f;
    g.cur_color[1] = g_ / 65535.0f;
    g.cur_color[2] = b / 65535.0f;
    g.cur_color[3] = 1.0f;
}

void glColor3usv(const GLushort *v) {
    if (v) glColor3us(v[0], v[1], v[2]);
}

void glColor4b(GLbyte r, GLbyte g_, GLbyte b, GLbyte a) {
    g.cur_color[0] = (r + 128) / 255.0f;
    g.cur_color[1] = (g_ + 128) / 255.0f;
    g.cur_color[2] = (b + 128) / 255.0f;
    g.cur_color[3] = (a + 128) / 255.0f;
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
    g.cur_color[0] = r / 2147483647.0f;
    g.cur_color[1] = g_ / 2147483647.0f;
    g.cur_color[2] = b / 2147483647.0f;
    g.cur_color[3] = a / 2147483647.0f;
}

void glColor4iv(const GLint *v) {
    if (v) glColor4i(v[0], v[1], v[2], v[3]);
}

void glColor4s(GLshort r, GLshort g_, GLshort b, GLshort a) {
    g.cur_color[0] = r / 32767.0f;
    g.cur_color[1] = g_ / 32767.0f;
    g.cur_color[2] = b / 32767.0f;
    g.cur_color[3] = a / 32767.0f;
}

void glColor4sv(const GLshort *v) {
    if (v) glColor4s(v[0], v[1], v[2], v[3]);
}

void glColor4ubv(const GLubyte *v) {
    if (v) glColor4ub(v[0], v[1], v[2], v[3]);
}

void glColor4ui(GLuint r, GLuint g_, GLuint b, GLuint a) {
    g.cur_color[0] = r / 4294967295.0f;
    g.cur_color[1] = g_ / 4294967295.0f;
    g.cur_color[2] = b / 4294967295.0f;
    g.cur_color[3] = a / 4294967295.0f;
}

void glColor4uiv(const GLuint *v) {
    if (v) glColor4ui(v[0], v[1], v[2], v[3]);
}

void glColor4us(GLushort r, GLushort g_, GLushort b, GLushort a) {
    g.cur_color[0] = r / 65535.0f;
    g.cur_color[1] = g_ / 65535.0f;
    g.cur_color[2] = b / 65535.0f;
    g.cur_color[3] = a / 65535.0f;
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
