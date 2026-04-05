//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glFogf(GLenum pname, GLfloat param) {
    switch (pname) {
        case GL_FOG_MODE:    if (g.fog_mode != param) { g.fog_mode = param; g.fog_dirty = 1; } break;
        case GL_FOG_START:   if (g.fog_start != param) { g.fog_start = param; g.fog_dirty = 1; } break;
        case GL_FOG_END:     if (g.fog_end != param) { g.fog_end = param; g.fog_dirty = 1; } break;
        case GL_FOG_DENSITY: if (g.fog_density != param) { g.fog_density = param; g.fog_dirty = 1; } break;
        default: break;
    }
}
void glFogfv(GLenum pname, const GLfloat *params) {
    if (pname == GL_FOG_COLOR && params) {
        g.fog_color[0] = params[0]; g.fog_color[1] = params[1]; g.fog_color[2] = params[2]; g.fog_color[3] = params[3]; g.fog_dirty = 1;
    } else if (params) { glFogf(pname, params[0]); }
}
void glFogx(GLenum pname, GLfixed param) { glFogf(pname, (float)param / 65536.0f); }