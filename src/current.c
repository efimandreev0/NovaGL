//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

static inline void update_packed_color() {
    uint32_t r = (uint32_t)(clampf(g.cur_color[0], 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t gc = (uint32_t)(clampf(g.cur_color[1], 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(clampf(g.cur_color[2], 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t a = (uint32_t)(clampf(g.cur_color[3], 0.0f, 1.0f) * 255.0f + 0.5f);
    // Для вершинных буферов PICA ожидается порядок RGBA в памяти
    g.cur_color_packed = r | (gc << 8) | (b << 16) | (a << 24);
}

// signed color -> (2c+1)/max then clamp to [0,1] becouse our framebuffer is
// unsigned. unsigned variants are already 0..1 so no clamp needed.
static inline GLfloat norm_b(GLbyte c)   { return clampf((2.0f * (float)c + 1.0f) / 255.0f,        0.0f, 1.0f); }
static inline GLfloat norm_s(GLshort c)  { return clampf((2.0f * (float)c + 1.0f) / 65535.0f,      0.0f, 1.0f); }
static inline GLfloat norm_i(GLint c)    { return clampf((2.0f * (float)c + 1.0f) / 4294967295.0f, 0.0f, 1.0f); }
static inline GLfloat norm_ub(GLubyte c) { return (float)c / 255.0f; }
static inline GLfloat norm_us(GLushort c){ return (float)c / 65535.0f; }
static inline GLfloat norm_ui(GLuint c)  { return (float)c / 4294967295.0f; }

void glColor4f(GLfloat r, GLfloat g_, GLfloat b, GLfloat a) {
    if (g.dl_recording >= 0) {
        dl_record_color4f(r, g_, b, a);
        return;
    }
    g.cur_color[0] = r;
    g.cur_color[1] = g_;
    g.cur_color[2] = b;
    g.cur_color[3] = a;
    update_packed_color();
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
    update_packed_color();
}

void glColor4ub(GLubyte r, GLubyte g_, GLubyte b, GLubyte a) {
    if (g.dl_recording >= 0) {
        dl_record_color4f(r / 255.0f, g_ / 255.0f, b / 255.0f, a / 255.0f);
        return;
    }
    g.cur_color[0] = r / 255.0f;
    g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f;
    g.cur_color[3] = a / 255.0f;
    update_packed_color();
}

void glColor3ub(GLubyte r, GLubyte g_, GLubyte b) {
    if (g.dl_recording >= 0) {
        dl_record_color3f(r / 255.0f, g_ / 255.0f, b / 255.0f);
        return;
    }
    g.cur_color[0] = r / 255.0f;
    g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f;
    g.cur_color[3] = 1.0f;
    update_packed_color();
}

/* Additional glColor variants */
void glColor3b(GLbyte r, GLbyte g_, GLbyte b) {
    g.cur_color[0] = norm_b(r);
    g.cur_color[1] = norm_b(g_);
    g.cur_color[2] = norm_b(b);
    g.cur_color[3] = 1.0f;
    update_packed_color();
}

void glColor3bv(const GLbyte *v) {
    if (v) glColor3b(v[0], v[1], v[2]);
}

void glColor3d(GLdouble r, GLdouble g_, GLdouble b) {
    g.cur_color[0] = (GLfloat) r;
    g.cur_color[1] = (GLfloat) g_;
    g.cur_color[2] = (GLfloat) b;
    g.cur_color[3] = 1.0f;
    update_packed_color();
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
    update_packed_color();
}

void glColor3iv(const GLint *v) {
    if (v) glColor3i(v[0], v[1], v[2]);
}

void glColor3s(GLshort r, GLshort g_, GLshort b) {
    g.cur_color[0] = norm_s(r);
    g.cur_color[1] = norm_s(g_);
    g.cur_color[2] = norm_s(b);
    g.cur_color[3] = 1.0f;
    update_packed_color();
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
    update_packed_color();
}

void glColor3uiv(const GLuint *v) {
    if (v) glColor3ui(v[0], v[1], v[2]);
}

void glColor3us(GLushort r, GLushort g_, GLushort b) {
    g.cur_color[0] = norm_us(r);
    g.cur_color[1] = norm_us(g_);
    g.cur_color[2] = norm_us(b);
    g.cur_color[3] = 1.0f;
    update_packed_color();
}

void glColor3usv(const GLushort *v) {
    if (v) glColor3us(v[0], v[1], v[2]);
}

void glColor4b(GLbyte r, GLbyte g_, GLbyte b, GLbyte a) {
    g.cur_color[0] = norm_b(r);
    g.cur_color[1] = norm_b(g_);
    g.cur_color[2] = norm_b(b);
    g.cur_color[3] = norm_b(a);
    update_packed_color();
}

void glColor4bv(const GLbyte *v) {
    if (v) glColor4b(v[0], v[1], v[2], v[3]);
}

void glColor4d(GLdouble r, GLdouble g_, GLdouble b, GLdouble a) {
    g.cur_color[0] = (GLfloat) r;
    g.cur_color[1] = (GLfloat) g_;
    g.cur_color[2] = (GLfloat) b;
    g.cur_color[3] = (GLfloat) a;
    update_packed_color();
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
    update_packed_color();
}

void glColor4iv(const GLint *v) {
    if (v) glColor4i(v[0], v[1], v[2], v[3]);
}

void glColor4s(GLshort r, GLshort g_, GLshort b, GLshort a) {
    g.cur_color[0] = norm_s(r);
    g.cur_color[1] = norm_s(g_);
    g.cur_color[2] = norm_s(b);
    g.cur_color[3] = norm_s(a);
    update_packed_color();
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
    update_packed_color();
}

void glColor4uiv(const GLuint *v) {
    if (v) glColor4ui(v[0], v[1], v[2], v[3]);
}

void glColor4us(GLushort r, GLushort g_, GLushort b, GLushort a) {
    g.cur_color[0] = norm_us(r);
    g.cur_color[1] = norm_us(g_);
    g.cur_color[2] = norm_us(b);
    g.cur_color[3] = norm_us(a);
    update_packed_color();
}

void glColor4usv(const GLushort *v) {
    if (v) glColor4us(v[0], v[1], v[2], v[3]);
}

void glColorMaterial(GLenum face, GLenum mode) {
    (void) face;
    (void) mode;
    // no lighting in Nova so color material do nothing
}

/* glMaterial* — front material is the only one PICA could ever light (no
 * separate back material), so `face` is accepted but the same store is used
 * for GL_FRONT / GL_BACK / GL_FRONT_AND_BACK. These fully record the material;
 * the lighting maths that consumes them is the GPU light path (not built yet). */
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) {
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (!params) return;
    switch (pname) {
        case GL_AMBIENT:
            g.mat_ambient[0] = params[0]; g.mat_ambient[1] = params[1];
            g.mat_ambient[2] = params[2]; g.mat_ambient[3] = params[3];
            break;
        case GL_DIFFUSE:
            g.mat_diffuse[0] = params[0]; g.mat_diffuse[1] = params[1];
            g.mat_diffuse[2] = params[2]; g.mat_diffuse[3] = params[3];
            break;
        case GL_SPECULAR:
            g.mat_specular[0] = params[0]; g.mat_specular[1] = params[1];
            g.mat_specular[2] = params[2]; g.mat_specular[3] = params[3];
            break;
        case GL_EMISSION:
            g.mat_emission[0] = params[0]; g.mat_emission[1] = params[1];
            g.mat_emission[2] = params[2]; g.mat_emission[3] = params[3];
            break;
        case GL_AMBIENT_AND_DIFFUSE:
            g.mat_ambient[0] = g.mat_diffuse[0] = params[0];
            g.mat_ambient[1] = g.mat_diffuse[1] = params[1];
            g.mat_ambient[2] = g.mat_diffuse[2] = params[2];
            g.mat_ambient[3] = g.mat_diffuse[3] = params[3];
            break;
        case GL_SHININESS:
            g.mat_shininess = params[0];
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    g.light_dirty = 1;
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    // only GL_SHININESS is a scalar; the colour names are an error in glMaterialf.
    if (pname == GL_SHININESS) {
        g.mat_shininess = param;
        g.light_dirty = 1;
    } else {
        gl_set_error(GL_INVALID_ENUM);
    }
}

void glMateriali(GLenum face, GLenum pname, GLint param) {
    glMaterialf(face, pname, (GLfloat) param);
}

void glMaterialiv(GLenum face, GLenum pname, const GLint *params) {
    if (!params) return;
    if (pname == GL_SHININESS) {
        glMaterialf(face, pname, (GLfloat) params[0]);
        return;
    }
    // colour names: ints are taken as-is (GL does a scaled map, but the engine
    // only ever uses the float path, so keep it simple and lossless enough).
    GLfloat c[4] = { (GLfloat) params[0], (GLfloat) params[1],
                     (GLfloat) params[2], (GLfloat) params[3] };
    glMaterialfv(face, pname, c);
}

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params) {
    (void) face;
    if (!params) return;
    switch (pname) {
        case GL_AMBIENT:
            params[0] = g.mat_ambient[0]; params[1] = g.mat_ambient[1];
            params[2] = g.mat_ambient[2]; params[3] = g.mat_ambient[3];
            break;
        case GL_DIFFUSE:
            params[0] = g.mat_diffuse[0]; params[1] = g.mat_diffuse[1];
            params[2] = g.mat_diffuse[2]; params[3] = g.mat_diffuse[3];
            break;
        case GL_SPECULAR:
            params[0] = g.mat_specular[0]; params[1] = g.mat_specular[1];
            params[2] = g.mat_specular[2]; params[3] = g.mat_specular[3];
            break;
        case GL_EMISSION:
            params[0] = g.mat_emission[0]; params[1] = g.mat_emission[1];
            params[2] = g.mat_emission[2]; params[3] = g.mat_emission[3];
            break;
        case GL_SHININESS:
            params[0] = g.mat_shininess;
            break;
        default:
            break;
    }
}

void glGetMaterialiv(GLenum face, GLenum pname, GLint *params) {
    if (!params) return;
    GLfloat c[4] = {0, 0, 0, 0};
    glGetMaterialfv(face, pname, c);
    params[0] = (GLint) c[0];
    if (pname != GL_SHININESS) {
        params[1] = (GLint) c[1];
        params[2] = (GLint) c[2];
        params[3] = (GLint) c[3];
    }
}

/* glLight* — records the full light source state. `light` is GL_LIGHT0..N.
 * Out-of-range index is a GL_INVALID_ENUM, no state change. */
static NovaLight *nova_light(GLenum light) {
    int idx = (int) light - GL_LIGHT0;
    if (idx < 0 || idx >= NOVA_MAX_LIGHTS) {
        gl_set_error(GL_INVALID_ENUM);
        return 0;
    }
    return &g.lights[idx];
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params) {
    NovaLight *L = nova_light(light);
    if (!L || !params) return;
    switch (pname) {
        case GL_AMBIENT:
            L->ambient[0] = params[0]; L->ambient[1] = params[1];
            L->ambient[2] = params[2]; L->ambient[3] = params[3];
            break;
        case GL_DIFFUSE:
            L->diffuse[0] = params[0]; L->diffuse[1] = params[1];
            L->diffuse[2] = params[2]; L->diffuse[3] = params[3];
            break;
        case GL_SPECULAR:
            L->specular[0] = params[0]; L->specular[1] = params[1];
            L->specular[2] = params[2]; L->specular[3] = params[3];
            break;
        case GL_POSITION: {
            /* Spec says GL_POSITION is transformed by the current MODELVIEW into
             * eye space at call time. NovaGL stores it RAW by default (the
             * historical behaviour the HW light env was tuned against — see
             * [[novagl-hw-lighting]]); the spec-correct modelview transform is
             * opt-in via -DNOVAGL_GL_LIGHT_EYE_SPACE=1, since flipping it can
             * shift where existing ports' lights land. */
#ifdef NOVAGL_GL_LIGHT_EYE_SPACE
            const C3D_Mtx *mv = &g.mv_stack[g.mv_sp];
            for (int i = 0; i < 4; i++) {
                L->position[i] = mv->r[i].x * params[0] + mv->r[i].y * params[1] +
                                 mv->r[i].z * params[2] + mv->r[i].w * params[3];
            }
#else
            L->position[0] = params[0]; L->position[1] = params[1];
            L->position[2] = params[2]; L->position[3] = params[3];
#endif
            break;
        }
        case GL_SPOT_DIRECTION: {
#ifdef NOVAGL_GL_LIGHT_EYE_SPACE
            /* Transformed by the modelview's upper-left 3x3 (direction, no
             * translation). */
            const C3D_Mtx *mv = &g.mv_stack[g.mv_sp];
            for (int i = 0; i < 3; i++) {
                L->spot_direction[i] = mv->r[i].x * params[0] + mv->r[i].y * params[1] +
                                       mv->r[i].z * params[2];
            }
#else
            L->spot_direction[0] = params[0]; L->spot_direction[1] = params[1];
            L->spot_direction[2] = params[2];
#endif
            break;
        }
        case GL_SPOT_EXPONENT:   L->spot_exponent   = params[0]; break;
        case GL_SPOT_CUTOFF:     L->spot_cutoff     = params[0]; break;
        case GL_CONSTANT_ATTENUATION:  L->atten_constant  = params[0]; break;
        case GL_LINEAR_ATTENUATION:    L->atten_linear    = params[0]; break;
        case GL_QUADRATIC_ATTENUATION: L->atten_quadratic = params[0]; break;
        default: gl_set_error(GL_INVALID_ENUM); break;
    }
    g.light_dirty = 1;
}

void glLightf(GLenum light, GLenum pname, GLfloat param) {
    // only the scalar params (spot exponent/cutoff, attenuations) are valid here
    GLfloat v[4] = { param, 0.0f, 0.0f, 0.0f };
    glLightfv(light, pname, v);
}

void glLighti(GLenum light, GLenum pname, GLint param) {
    glLightf(light, pname, (GLfloat) param);
}

void glLightiv(GLenum light, GLenum pname, const GLint *params) {
    if (!params) return;
    GLfloat v[4] = { (GLfloat) params[0], (GLfloat) params[1],
                     (GLfloat) params[2], (GLfloat) params[3] };
    glLightfv(light, pname, v);
}

void glGetLightfv(GLenum light, GLenum pname, GLfloat *params) {
    NovaLight *L = nova_light(light);
    if (!L || !params) return;
    switch (pname) {
        case GL_AMBIENT:
            params[0]=L->ambient[0]; params[1]=L->ambient[1]; params[2]=L->ambient[2]; params[3]=L->ambient[3]; break;
        case GL_DIFFUSE:
            params[0]=L->diffuse[0]; params[1]=L->diffuse[1]; params[2]=L->diffuse[2]; params[3]=L->diffuse[3]; break;
        case GL_SPECULAR:
            params[0]=L->specular[0]; params[1]=L->specular[1]; params[2]=L->specular[2]; params[3]=L->specular[3]; break;
        case GL_POSITION:
            params[0]=L->position[0]; params[1]=L->position[1]; params[2]=L->position[2]; params[3]=L->position[3]; break;
        case GL_SPOT_DIRECTION:
            params[0]=L->spot_direction[0]; params[1]=L->spot_direction[1]; params[2]=L->spot_direction[2]; break;
        case GL_SPOT_EXPONENT:   params[0] = L->spot_exponent;   break;
        case GL_SPOT_CUTOFF:     params[0] = L->spot_cutoff;     break;
        case GL_CONSTANT_ATTENUATION:  params[0] = L->atten_constant;  break;
        case GL_LINEAR_ATTENUATION:    params[0] = L->atten_linear;    break;
        case GL_QUADRATIC_ATTENUATION: params[0] = L->atten_quadratic; break;
        default: gl_set_error(GL_INVALID_ENUM); break;
    }
}

/* glLightModel* — only GL_LIGHT_MODEL_AMBIENT carries state we keep; the
 * local-viewer / two-side flags don't map to anything NovaGL renders yet. */
void glLightModelfv(GLenum pname, const GLfloat *params) {
    if (!params) return;
    if (pname == GL_LIGHT_MODEL_AMBIENT) {
        g.light_model_ambient[0] = params[0]; g.light_model_ambient[1] = params[1];
        g.light_model_ambient[2] = params[2]; g.light_model_ambient[3] = params[3];
        g.light_dirty = 1;
    }
}

void glLightModelf(GLenum pname, GLfloat param) {
    (void) pname; (void) param; // scalar light-model params are no-ops here
}

void glLightModeli(GLenum pname, GLint param) {
    (void) pname; (void) param;
}

void glLightModeliv(GLenum pname, const GLint *params) {
    if (!params) return;
    if (pname == GL_LIGHT_MODEL_AMBIENT) {
        GLfloat c[4] = { (GLfloat) params[0], (GLfloat) params[1],
                         (GLfloat) params[2], (GLfloat) params[3] };
        glLightModelfv(pname, c);
    }
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    // PICA pipeline sample only one texcoord set, so unit 0 is the one that
    // realy do something. route it to the normal texcoord, drop higher units.
    if (target == GL_TEXTURE0) glTexCoord4f(s, t, r, q);
}

// integer normals map to [-1,1] by dividing on the type max (spec rule)
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    g.cur_normal[0] = nx;
    g.cur_normal[1] = ny;
    g.cur_normal[2] = nz;
}

/* Additional glNormal variants */
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz) {
    glNormal3f((float) nx / 127.0f, (float) ny / 127.0f, (float) nz / 127.0f);
}

void glNormal3bv(const GLbyte *v) {
    if (v) glNormal3b(v[0], v[1], v[2]);
}

void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) {
    glNormal3f((GLfloat) nx, (GLfloat) ny, (GLfloat) nz);
}

void glNormal3dv(const GLdouble *v) {
    if (v) glNormal3d(v[0], v[1], v[2]);
}

void glNormal3fv(const GLfloat *v) {
    if (v) glNormal3f(v[0], v[1], v[2]);
}

void glNormal3i(GLint nx, GLint ny, GLint nz) {
    glNormal3f((float) nx / 2147483647.0f, (float) ny / 2147483647.0f, (float) nz / 2147483647.0f);
}

void glNormal3iv(const GLint *v) {
    if (v) glNormal3i(v[0], v[1], v[2]);
}

void glNormal3s(GLshort nx, GLshort ny, GLshort nz) {
    glNormal3f((float) nx / 32767.0f, (float) ny / 32767.0f, (float) nz / 32767.0f);
}

void glNormal3sv(const GLshort *v) {
    if (v) glNormal3s(v[0], v[1], v[2]);
}
