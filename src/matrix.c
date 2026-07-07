//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"
#include "vfp_math.h"
#include <math.h>

/* Fast Inverse Square Root (Quake 3 style) for 3DS
 * Avoids the slow VFPv2 hardware division and square root. */
static inline float q_rsqrt(float number) {
    int32_t i;
    float x2, y;
    const float threehalfs = 1.5f;

    x2 = number * 0.5f;
    y  = number;
    i  = *(int32_t*)&y;
    i  = 0x5f3759df - (i >> 1);
    y  = *(float*)&i;
    y  = y * (threehalfs - (x2 * y * y)); // 1st iteration
    return y;
}

/* Mark only the stack of the currently active matrix mode as dirty, plus the
 * union flag for code paths that just check `matrices_dirty`.
 *
 * If we're mutating the GL_TEXTURE stack with anything other than identity,
 * clear g.tex_mtx_is_identity so the shader selector falls back to the full
 * shader (which actually applies the texmtx). glLoadIdentity sets it back. */
static inline void mark_cur_dirty(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: g.proj_dirty = 1;    break;
        case GL_TEXTURE:
            g.tex_mtx_dirty = 1;
            g.tex_mtx_is_identity = 0;
            g.tex_mtx_identity_stack[g.tex_sp] = 0;
            break;
        default:            g.mv_dirty = 1;      break;
    }
    g.matrices_dirty = 1;
}

void glMatrixMode(GLenum mode) {
    /* Reject unknown modes so cur_mtx()/cur_sp()/cur_stack() never index out of
     * bounds. Spec says GL_INVALID_ENUM in this case. */
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    g.matrix_mode = mode;
}

void glLoadIdentity(void) {
    Mtx_Identity(cur_mtx());
    mark_cur_dirty();
    /* The mark_cur_dirty above clears tex_mtx_is_identity for GL_TEXTURE
     * mutations, but glLoadIdentity is the one mutation that restores it. */
    if (g.matrix_mode == GL_TEXTURE) {
        g.tex_mtx_is_identity = 1;
        g.tex_mtx_identity_stack[g.tex_sp] = 1;
    }
}

void glPushMatrix(void) {
    int *sp = cur_sp();
    C3D_Mtx *stack = cur_stack();
    if (*sp < NOVA_MATRIX_STACK - 1) {
        Mtx_Copy(&stack[*sp + 1], &stack[*sp]);
        if (g.matrix_mode == GL_TEXTURE) {
            g.tex_mtx_identity_stack[*sp + 1] = g.tex_mtx_identity_stack[*sp];
        }
        (*sp)++;
    } else {
        gl_set_error(GL_STACK_OVERFLOW);
    }
}

void glPopMatrix(void) {
    int *sp = cur_sp();
    if (*sp > 0) (*sp)--;
    else { gl_set_error(GL_STACK_UNDERFLOW); return; }
    mark_cur_dirty();
    /* mark_cur_dirty force-clears tex_mtx_is_identity on GL_TEXTURE mode;
     * restore from the per-slot stack after, so popping back to identity
     * keeps the basic shader path available. */
    if (g.matrix_mode == GL_TEXTURE) {
        g.tex_mtx_is_identity = g.tex_mtx_identity_stack[*sp];
    }
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (g.dl_recording >= 0) {
        dl_record_translate(x, y, z);
        return;
    }
    vfp_translate(cur_mtx(), x, y, z);
    mark_cur_dirty();
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float rad = angle * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);

    // Fast path: Single-axis rotations (No sqrt or division needed)
    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        vfp_rotate_x(cur_mtx(), c, s);
        mark_cur_dirty();
        return;
    }
    if (x == 0.0f && y == 1.0f && z == 0.0f) {
        vfp_rotate_y(cur_mtx(), c, s);
        mark_cur_dirty();
        return;
    }
    if (x == 0.0f && y == 0.0f && z == 1.0f) {
        vfp_rotate_z(cur_mtx(), c, s);
        mark_cur_dirty();
        return;
    }

    // Generic path: Arbitrary axis rotation
    float len_sq = x * x + y * y + z * z;
    if (len_sq < 0.0001f * 0.0001f)
        return; // degenerate vector

    float inv_len = q_rsqrt(len_sq);
    x *= inv_len;
    y *= inv_len;
    z *= inv_len;

    float ic = 1.0f - c;
    C3D_Mtx rot;
    Mtx_Identity(&rot);
    rot.r[0].x = x * x * ic + c;
    rot.r[0].y = x * y * ic - z * s;
    rot.r[0].z = x * z * ic + y * s;
    rot.r[1].x = y * x * ic + z * s;
    rot.r[1].y = y * y * ic + c;
    rot.r[1].z = y * z * ic - x * s;
    rot.r[2].x = z * x * ic - y * s;
    rot.r[2].y = z * y * ic + x * s;
    rot.r[2].z = z * z * ic + c;
    vfp_mult_mtx_mtx(cur_mtx(), &rot);
    mark_cur_dirty();
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    vfp_scale(cur_mtx(), x, y, z);
    mark_cur_dirty();
}

void glMultMatrixf(const GLfloat *m) {
    if (!m) return;
    vfp_mult_gl_matrix(cur_mtx(), m);
    mark_cur_dirty();
}

void glLoadMatrixf(const GLfloat *m) {
    if (!m) return;
    vfp_load_matrix(cur_mtx(), m);
    mark_cur_dirty();
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    float inv_w = 1.0f / (right - left);
    float inv_h = 1.0f / (top - bottom);
    float inv_d = 1.0f / (far_val - near_val);

    vfp_scale_translate(cur_mtx(),
                        2.0f * inv_w, 2.0f * inv_h, -2.0f * inv_d,
                        -(right + left) * inv_w, -(top + bottom) * inv_h, -(far_val + near_val) * inv_d);
    mark_cur_dirty();
}

void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    float n = near_val, f = far_val;
    float inv_w = 1.0f / (right - left);
    float inv_h = 1.0f / (top - bottom);
    float inv_d = 1.0f / (f - n);

    vfp_apply_frustum(cur_mtx(),
                      2.0f * n * inv_w,         // A
                      2.0f * n * inv_h,         // B
                      (right + left) * inv_w,   // C
                      (top + bottom) * inv_h,   // D
                      -(f + n) * inv_d,         // E
                      -(2.0f * f * n) * inv_d); // F
    mark_cur_dirty();
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    glFrustumf((GLfloat) left, (GLfloat) right, (GLfloat) bottom, (GLfloat) top, (GLfloat) near_val, (GLfloat) far_val);
}

void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed near_val, GLfixed far_val) {
    glFrustumf(left / 65536.0f, right / 65536.0f, bottom / 65536.0f, top / 65536.0f, near_val / 65536.0f,
               far_val / 65536.0f);
}

/* Double-precision matrix functions */
void glLoadMatrixd(const GLdouble *m) {
    if (!m) return;
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (GLfloat)m[i];
    vfp_load_matrix(cur_mtx(), f);
    mark_cur_dirty();
}

void glMultMatrixd(const GLdouble *m) {
    if (!m) return;
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (GLfloat)m[i];
    vfp_mult_gl_matrix(cur_mtx(), f);
    mark_cur_dirty();
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
    glRotatef((GLfloat) angle, (GLfloat) x, (GLfloat) y, (GLfloat) z);
}

void glScaled(GLdouble x, GLdouble y, GLdouble z) {
    glScalef((GLfloat) x, (GLfloat) y, (GLfloat) z);
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    glTranslatef((GLfloat) x, (GLfloat) y, (GLfloat) z);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    glOrthof((GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
}