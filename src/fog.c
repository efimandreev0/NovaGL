//
// created by efimandreev0 on 05.04.2026.
//

#include <stdio.h>

#include "NovaGL.h"
#include "utils.h"

void glFogf(GLenum pname, GLfloat param) {
    switch (pname) {
        case GL_FOG_MODE: {
            // cast to enum first then compare, so we dont dirty fog every frame
            GLenum new_mode = (GLenum) param;
            if (new_mode != GL_LINEAR && new_mode != GL_EXP && new_mode != GL_EXP2) {
                gl_set_error(GL_INVALID_ENUM); /* spec: invalid mode value, unchanged */
                break;
            }
            if (g.fog_mode != new_mode) {
                g.fog_mode = new_mode;
                g.fog_dirty = 1;
            }
        } break;
        case GL_FOG_START: if (g.fog_start != param) {
                g.fog_start = param;
                g.fog_dirty = 1;
            }
            break;
        case GL_FOG_END: if (g.fog_end != param) {
                g.fog_end = param;
                g.fog_dirty = 1;
            }
            break;
        case GL_FOG_DENSITY:
            /* Spec: negative density is GL_INVALID_VALUE, state unchanged. */
            if (param < 0.0f) {
                gl_set_error(GL_INVALID_VALUE);
                break;
            }
            if (g.fog_density != param) {
                g.fog_density = param;
                g.fog_dirty = 1;
            }
            break;
        default: gl_set_error(GL_INVALID_ENUM); break;
    }
}

void glFogfv(GLenum pname, const GLfloat *params) {
    if (pname == GL_FOG_COLOR && params) {
        /* Spec: fog colour is clamped to [0,1] at specification time. */
        g.fog_color[0] = clampf(params[0], 0.0f, 1.0f);
        g.fog_color[1] = clampf(params[1], 0.0f, 1.0f);
        g.fog_color[2] = clampf(params[2], 0.0f, 1.0f);
        g.fog_color[3] = clampf(params[3], 0.0f, 1.0f);
        g.fog_dirty = 1;
    } else if (params) { glFogf(pname, params[0]); }
}

void glFogi(GLenum pname, GLint param) {
    switch (pname) {
        case GL_FOG_MODE: {
            GLenum new_mode = (GLenum) param;
            if (new_mode != GL_LINEAR && new_mode != GL_EXP && new_mode != GL_EXP2) {
                gl_set_error(GL_INVALID_ENUM);
                break;
            }
            if (g.fog_mode != new_mode) {
                g.fog_mode = new_mode;
                g.fog_dirty = 1;
            }
        } break;
        case GL_FOG_START: if (g.fog_start != (GLfloat) param) {
                g.fog_start = (GLfloat) param;
                g.fog_dirty = 1;
            }
            break;
        case GL_FOG_END: if (g.fog_end != (GLfloat) param) {
                g.fog_end = (GLfloat) param;
                g.fog_dirty = 1;
            }
            break;
        case GL_FOG_DENSITY:
            if (param < 0) {
                gl_set_error(GL_INVALID_VALUE);
                break;
            }
            if (g.fog_density != (GLfloat) param) {
                g.fog_density = (GLfloat) param;
                g.fog_dirty = 1;
            }
            break;
        default: gl_set_error(GL_INVALID_ENUM); break;
    }
}

void glFogiv(GLenum pname, const GLint *params) {
    if (pname == GL_FOG_COLOR && params) {
        /* int colour components map to [-1,1] then clamp to [0,1] at spec time. */
        g.fog_color[0] = clampf(params[0] / 2147483647.0f, 0.0f, 1.0f);
        g.fog_color[1] = clampf(params[1] / 2147483647.0f, 0.0f, 1.0f);
        g.fog_color[2] = clampf(params[2] / 2147483647.0f, 0.0f, 1.0f);
        g.fog_color[3] = clampf(params[3] / 2147483647.0f, 0.0f, 1.0f);
        g.fog_dirty = 1;
    } else if (params) {
        glFogi(pname, params[0]);
    }
}

void glFogx(GLenum pname, GLfixed param) { glFogf(pname, (float) param / 65536.0f); }

// GL_EXT_fog_coord. we make fog from depth in the shader so per-vertex fog coord
// is droped. keep the symbol so apps that just need it to link are happy, but
// yell once so nobody think the pointer is actualy used.
void glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    (void) type;
    (void) stride;
    (void) pointer;
    static int warned = 0;
    if (!warned) {
        printf("[Nova]: glFogCoordPointer ignored, fog come from depth\n");
        warned = 1;
    }
}