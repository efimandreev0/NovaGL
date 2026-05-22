//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

/* Mark only the stack of the currently active matrix mode as dirty, plus the
 * union flag for code paths that just check `matrices_dirty`. */
static inline void mark_cur_dirty(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: g.proj_dirty = 1;    break;
        case GL_TEXTURE:    g.tex_mtx_dirty = 1; break;
        default:            g.mv_dirty = 1;      break;
    }
    g.matrices_dirty = 1;
}

void glMatrixMode(GLenum mode) { g.matrix_mode = mode; }

void glLoadIdentity(void) {
    Mtx_Identity(cur_mtx());
    mark_cur_dirty();
}

void glPushMatrix(void) {
    int *sp = cur_sp();
    C3D_Mtx *stack = cur_stack();
    if (*sp < NOVA_MATRIX_STACK - 1) {
        Mtx_Copy(&stack[*sp + 1], &stack[*sp]);
        (*sp)++;
    }
}

void glPopMatrix(void) {
    int *sp = cur_sp();
    if (*sp > 0) (*sp)--;
    mark_cur_dirty();
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (g.dl_recording >= 0) {
        dl_record_translate(x, y, z);
        return;
    }
    /* vitaGL trick: a full M*T multiply is wasteful — T touches only the
     * translation column. M*T leaves rows 0..2 columns 0..2 unchanged and
     * shifts the translation column by M.xyz . (x,y,z). Skips 64 muls. */
    C3D_Mtx *m = cur_mtx();
    for (int i = 0; i < 4; i++) {
        m->r[i].w = m->r[i].x * x + m->r[i].y * y + m->r[i].z * z + m->r[i].w;
    }
    mark_cur_dirty();
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float rad = angle * M_PI / 180.0f;
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 0.0001f) return;
    x /= len;
    y /= len;
    z /= len;
    C3D_Mtx rot;
    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        Mtx_Identity(&rot);
        float c = cosf(rad), s = sinf(rad);
        rot.r[1].y = c;
        rot.r[1].z = -s;
        rot.r[2].y = s;
        rot.r[2].z = c;
    } else if (x == 0.0f && y == 1.0f && z == 0.0f) {
        Mtx_Identity(&rot);
        float c = cosf(rad), s = sinf(rad);
        rot.r[0].x = c;
        rot.r[0].z = s;
        rot.r[2].x = -s;
        rot.r[2].z = c;
    } else if (x == 0.0f && y == 0.0f && z == 1.0f) {
        Mtx_Identity(&rot);
        float c = cosf(rad), s = sinf(rad);
        rot.r[0].x = c;
        rot.r[0].y = -s;
        rot.r[1].x = s;
        rot.r[1].y = c;
    } else {
        float c = cosf(rad), s = sinf(rad), ic = 1.0f - c;
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
    }
    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &rot);
    Mtx_Copy(cur_mtx(), &result);
    mark_cur_dirty();
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    /* M*S where S is diag(x,y,z,1) just multiplies columns 0..2 by sx/sy/sz.
     * Cheaper than running Mtx_Multiply (which is 64 muls + 48 adds). */
    C3D_Mtx *m = cur_mtx();
    for (int i = 0; i < 4; i++) {
        m->r[i].x *= x;
        m->r[i].y *= y;
        m->r[i].z *= z;
    }
    mark_cur_dirty();
}

void glMultMatrixf(const GLfloat *m) {
    C3D_Mtx tmp;
    for (int r = 0; r < 4; r++) {
        tmp.r[r].x = m[0 * 4 + r];
        tmp.r[r].y = m[1 * 4 + r];
        tmp.r[r].z = m[2 * 4 + r];
        tmp.r[r].w = m[3 * 4 + r];
    }
    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &tmp);
    Mtx_Copy(cur_mtx(), &result);
    mark_cur_dirty();
}

void glLoadMatrixf(const GLfloat *m) {
    C3D_Mtx *dst = cur_mtx();
    for (int r = 0; r < 4; r++) {
        dst->r[r].x = m[0 * 4 + r];
        dst->r[r].y = m[1 * 4 + r];
        dst->r[r].z = m[2 * 4 + r];
        dst->r[r].w = m[3 * 4 + r];
    }
    mark_cur_dirty();
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    C3D_Mtx ortho;
    Mtx_Identity(&ortho);
    ortho.r[0].x = 2.0f / (right - left);
    ortho.r[0].w = -(right + left) / (right - left);
    ortho.r[1].y = 2.0f / (top - bottom);
    ortho.r[1].w = -(top + bottom) / (top - bottom);

    ortho.r[2].z = -2.0f / (far_val - near_val);
    ortho.r[2].w = -(far_val + near_val) / (far_val - near_val);

    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &ortho);
    Mtx_Copy(cur_mtx(), &result);
    mark_cur_dirty();
}

void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    float n = near_val;
    float f = far_val;
    C3D_Mtx frustum;
    memset(&frustum, 0, sizeof(C3D_Mtx));
    frustum.r[0].x = (2.0f * n) / (right - left);
    frustum.r[0].z = (right + left) / (right - left);
    frustum.r[1].y = (2.0f * n) / (top - bottom);
    frustum.r[1].z = (top + bottom) / (top - bottom);

    // Standard OpenGL: z_ndc maps to [-1, +1]
    frustum.r[2].z = -(f + n) / (f - n);
    frustum.r[2].w = -(2.0f * f * n) / (f - n);
    frustum.r[3].z = -1.0f;

    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &frustum);
    Mtx_Copy(cur_mtx(), &result);
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
    C3D_Mtx *dst = cur_mtx();
    for (int r = 0; r < 4; r++) {
        dst->r[r].x = (GLfloat) m[0 * 4 + r];
        dst->r[r].y = (GLfloat) m[1 * 4 + r];
        dst->r[r].z = (GLfloat) m[2 * 4 + r];
        dst->r[r].w = (GLfloat) m[3 * 4 + r];
    }
    mark_cur_dirty();
}

void glMultMatrixd(const GLdouble *m) {
    if (!m) return;
    C3D_Mtx tmp;
    for (int r = 0; r < 4; r++) {
        tmp.r[r].x = (GLfloat) m[0 * 4 + r];
        tmp.r[r].y = (GLfloat) m[1 * 4 + r];
        tmp.r[r].z = (GLfloat) m[2 * 4 + r];
        tmp.r[r].w = (GLfloat) m[3 * 4 + r];
    }
    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &tmp);
    Mtx_Copy(cur_mtx(), &result);
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
    C3D_Mtx ortho;
    Mtx_Identity(&ortho);
    ortho.r[0].x = (GLfloat) (2.0 / (right - left));
    ortho.r[0].w = (GLfloat) (-(right + left) / (right - left));
    ortho.r[1].y = (GLfloat) (2.0 / (top - bottom));
    ortho.r[1].w = (GLfloat) (-(top + bottom) / (top - bottom));
    ortho.r[2].z = (GLfloat) (-2.0 / (far_val - near_val));
    ortho.r[2].w = (GLfloat) (-(far_val + near_val) / (far_val - near_val));
    C3D_Mtx result;
    Mtx_Multiply(&result, cur_mtx(), &ortho);
    Mtx_Copy(cur_mtx(), &result);
    mark_cur_dirty();
}
