//
// compat.c — GLES 1.1 / GLU compatibility entry points that map cleanly onto
// the existing NovaGL fixed-function path. Grouped here (rather than scattered)
// so the "thin wrapper" nature stays obvious. Nothing here is a new GPU path —
// each forwards to an already-implemented float/core function (the one real
// exception, glBlendFuncSeparate, lives in raster.c since it needs blend state).
//

#include "NovaGL.h"
#include "utils.h"

#include <math.h>

/* 16.16 fixed-point -> float. */
#define FX(x) ((GLfloat)(GLfixed)(x) / 65536.0f)

#ifndef GL_PI
#define GL_PI 3.14159265358979323846
#endif

/* ── Fixed-point (GLfixed) variants ───────────────────────────────────────
 * GLES 1.1 ES-CL profile spellings. Each converts 16.16 args to float and
 * forwards to the float entry point. Enum-valued args (mode/func selectors)
 * are passed through verbatim, NOT scaled. */

void glClearColorx(GLfixed r, GLfixed g_, GLfixed b, GLfixed a) { glClearColor(FX(r), FX(g_), FX(b), FX(a)); }
void glClearDepthx(GLfixed depth)                               { glClearDepthf(FX(depth)); }
void glColor4x(GLfixed r, GLfixed g_, GLfixed b, GLfixed a)     { glColor4f(FX(r), FX(g_), FX(b), FX(a)); }
void glDepthRangex(GLfixed n, GLfixed f)                        { glDepthRangef(FX(n), FX(f)); }
void glAlphaFuncx(GLenum func, GLfixed ref)                     { glAlphaFunc(func, FX(ref)); }
void glLineWidthx(GLfixed width)                                { glLineWidth(FX(width)); }
void glPolygonOffsetx(GLfixed factor, GLfixed units)            { glPolygonOffset(FX(factor), FX(units)); }
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz)             { glNormal3f(FX(nx), FX(ny), FX(nz)); }
void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { glOrthof(FX(l), FX(r), FX(b), FX(t), FX(n), FX(f)); }
void glRotatex(GLfixed a, GLfixed x, GLfixed y, GLfixed z)      { glRotatef(FX(a), FX(x), FX(y), FX(z)); }
void glScalex(GLfixed x, GLfixed y, GLfixed z)                  { glScalef(FX(x), FX(y), FX(z)); }
void glTranslatex(GLfixed x, GLfixed y, GLfixed z)             { glTranslatef(FX(x), FX(y), FX(z)); }

void glLoadMatrixx(const GLfixed *m) {
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = FX(m[i]);
    glLoadMatrixf(f);
}
void glMultMatrixx(const GLfixed *m) {
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = FX(m[i]);
    glMultMatrixf(f);
}

void glMaterialx(GLenum face, GLenum pname, GLfixed param) { glMaterialf(face, pname, FX(param)); }
void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params) {
    GLfloat f[4] = {0, 0, 0, 0};
    int n = (pname == GL_SHININESS) ? 1 : 4;
    for (int i = 0; i < n; i++) f[i] = FX(params[i]);
    glMaterialfv(face, pname, f);
}

void glLightxv(GLenum light, GLenum pname, const GLfixed *params) {
    GLfloat f[4] = {0, 0, 0, 0};
    int n = 4;
    if (pname == GL_SPOT_DIRECTION) n = 3;
    else if (pname == GL_SPOT_EXPONENT || pname == GL_SPOT_CUTOFF ||
             pname == GL_CONSTANT_ATTENUATION || pname == GL_LINEAR_ATTENUATION ||
             pname == GL_QUADRATIC_ATTENUATION) n = 1;
    for (int i = 0; i < n; i++) f[i] = FX(params[i]);
    glLightfv(light, pname, f);
}

void glLightModelxv(GLenum pname, const GLfixed *params) {
    GLfloat f[4] = {0, 0, 0, 0};
    int n = (pname == GL_LIGHT_MODEL_AMBIENT) ? 4 : 1;
    for (int i = 0; i < n; i++) f[i] = FX(params[i]);
    glLightModelfv(pname, f);
}

void glTexParameterx(GLenum target, GLenum pname, GLfixed param) {
    /* Standard ES 1.1 tex params (filters/wrap/max_level) are enum/int valued,
     * not scaled — pass through. */
    glTexParameteri(target, pname, (GLint) param);
}

void glTexEnvx(GLenum target, GLenum pname, GLfixed param) {
    /* Only the scale factors are fixed-point; mode/combine/src/operand are enums. */
    if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE)
        glTexEnvi(target, pname, (GLint) FX(param));
    else
        glTexEnvi(target, pname, (GLint) param);
}
void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params) {
    if (pname == GL_TEXTURE_ENV_COLOR) {
        GLfloat f[4] = { FX(params[0]), FX(params[1]), FX(params[2]), FX(params[3]) };
        glTexEnvfv(target, pname, f);
    } else {
        glTexEnvx(target, pname, params[0]);
    }
}

void glFogxv(GLenum pname, const GLfixed *params) {
    if (pname == GL_FOG_MODE) {            /* enum, not scaled */
        glFogi(pname, (GLint) params[0]);
    } else if (pname == GL_FOG_COLOR) {
        GLfloat f[4] = { FX(params[0]), FX(params[1]), FX(params[2]), FX(params[3]) };
        glFogfv(pname, f);
    } else {                              /* density / start / end */
        glFogf(pname, FX(params[0]));
    }
}

/* ── Separate stencil faces ───────────────────────────────────────────────
 * PICA has a single (front=back) stencil unit, so the per-face variants
 * collapse to the unified path — best-effort but spec-correct for the common
 * "same state on both faces" usage. */
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) { (void) face; glStencilFunc(func, ref, mask); }
void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { (void) face; glStencilOp(sfail, dpfail, dppass); }
void glStencilMaskSeparate(GLenum face, GLuint mask) { (void) face; glStencilMask(mask); }

/* ── Multitexture coord convenience forms ────────────────────────────────
 * Forward to the canonical glMultiTexCoord4f (r=0, q=1). */
void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)  { glMultiTexCoord4f(target, s, t, 0.0f, 1.0f); }
void glMultiTexCoord2fv(GLenum target, const GLfloat *v)     { glMultiTexCoord4f(target, v[0], v[1], 0.0f, 1.0f); }
void glMultiTexCoord2i(GLenum target, GLint s, GLint t)      { glMultiTexCoord4f(target, (GLfloat) s, (GLfloat) t, 0.0f, 1.0f); }

/* ── glRect — immediate-mode rectangle in the z=0 plane ───────────────────
 * Per spec, equivalent to a quad with the given corners. */
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) {
    glRectf((GLfloat) x1, (GLfloat) y1, (GLfloat) x2, (GLfloat) y2);
}

/* ── glMultiDrawArrays — just a loop over glDrawArrays ────────────────────*/
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) {
    if (primcount < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!first || !count) return;
    for (GLsizei i = 0; i < primcount; i++) {
        glDrawArrays(mode, first[i], count[i]);
    }
}

/* ── Object existence queries ─────────────────────────────────────────────*/
GLboolean glIsFramebuffer(GLuint framebuffer) {
    if (framebuffer > 0 && framebuffer < NOVA_MAX_FBOS && g.fbos[framebuffer].in_use) return GL_TRUE;
    return GL_FALSE;
}

/* ── Buffer object parameter query ────────────────────────────────────────*/
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (!params) return;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    switch (pname) {
        case GL_BUFFER_SIZE: case GL_BUFFER_USAGE:
        case GL_BUFFER_ACCESS: case GL_BUFFER_MAPPED:
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    GLuint id = (target == GL_ELEMENT_ARRAY_BUFFER) ? g.bound_element_array_buffer : g.bound_array_buffer;
    if (id == 0 || id >= NOVA_MAX_VBOS || !g.vbos[id].in_use) {
        /* Spec: querying with no buffer bound to target is GL_INVALID_OPERATION. */
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    VBOSlot *slot = &g.vbos[id];
    switch (pname) {
        case GL_BUFFER_SIZE:   params[0] = slot->size; break;

        case GL_BUFFER_USAGE:  params[0] = (GLint) (slot->usage ? slot->usage : GL_STATIC_DRAW); break;
        case GL_BUFFER_ACCESS: params[0] = GL_READ_WRITE; break;
        case GL_BUFFER_MAPPED: params[0] = slot->mapped ? GL_TRUE : GL_FALSE; break;
    }
}

/* ── GLU helpers ───────────────────────────────────────────────────────────
 * Commonly used by desktop ports for camera/projection setup. */
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar) {
    float rad = (float)(fovy * (NOVA_PI / 360.0));
    float s, c;
    nova_fast_sincosf(rad, &s, &c);

    GLdouble ymax = zNear * (GLdouble)(s / c);
    GLdouble ymin = -ymax;
    glFrustum(ymin * aspect, ymax * aspect, ymin, ymax, zNear, zFar);
}

void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
               GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ) {
    GLfloat fwd[3] = { (GLfloat)(centerX - eyeX), (GLfloat)(centerY - eyeY), (GLfloat)(centerZ - eyeZ) };

    // Fast normalization
    GLfloat fl_sq = fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2];
    if (fl_sq > 1e-8f) {
        GLfloat fl_inv = nova_fast_rsqrt(fl_sq);
        fwd[0] *= fl_inv; fwd[1] *= fl_inv; fwd[2] *= fl_inv;
    } else {
        fwd[0] = 0.0f; fwd[1] = 0.0f; fwd[2] = 1.0f; // fallback
    }

    GLfloat up[3] = { (GLfloat) upX, (GLfloat) upY, (GLfloat) upZ };

    /* side = fwd x up */
    GLfloat side[3] = {
        fwd[1]*up[2] - fwd[2]*up[1],
        fwd[2]*up[0] - fwd[0]*up[2],
        fwd[0]*up[1] - fwd[1]*up[0]
    };

    // Fast normalization
    GLfloat sl_sq = side[0]*side[0] + side[1]*side[1] + side[2]*side[2];
    if (sl_sq > 1e-8f) {
        GLfloat sl_inv = nova_fast_rsqrt(sl_sq);
        side[0] *= sl_inv; side[1] *= sl_inv; side[2] *= sl_inv;
    } else {
        side[0] = 1.0f; side[1] = 0.0f; side[2] = 0.0f; // fallback
    }

    /* recomputed up = side x fwd */
    GLfloat u[3] = {
        side[1]*fwd[2] - side[2]*fwd[1],
        side[2]*fwd[0] - side[0]*fwd[2],
        side[0]*fwd[1] - side[1]*fwd[0]
    };

    /* Column-major (GL layout), rows = side / up / -fwd. */
    GLfloat m[16] = {
        side[0], u[0], -fwd[0], 0.0f,
        side[1], u[1], -fwd[1], 0.0f,
        side[2], u[2], -fwd[2], 0.0f,
        0.0f,    0.0f,  0.0f,   1.0f
    };
    glMultMatrixf(m);
    glTranslatef((GLfloat)-eyeX, (GLfloat)-eyeY, (GLfloat)-eyeZ);
}

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const void *data) {
    /* NovaGL builds the pyramid at upload time when GL_GENERATE_MIPMAP is set. */
    glTexParameteri(target, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
    return 0;
}
