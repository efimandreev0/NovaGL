//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glGenFramebuffers(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = i + 1; }

void glDeleteFramebuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    (void)target;
    if (framebuffer == 0 || framebuffer == 1) {
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
    }
}

void glGenRenderbuffers(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = i + 1; }

void glDeleteRenderbuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) { (void)target; (void)renderbuffer; }

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { (void)target; (void)internalformat; (void)width; (void)height; }

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { (void)target; (void)attachment; (void)renderbuffertarget; (void)renderbuffer; }

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    (void)x; (void)y; (void)width; (void)height; (void)format; (void)type;
    if (pixels) memset(pixels, 0, width * height * 4);
}

void glPixelStorei(GLenum pname, GLint param) {
    if (param != 1 && param != 2 && param != 4 && param != 8) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (pname == GL_UNPACK_ALIGNMENT) {
        g.unpack_alignment = param;
    } else if (pname == GL_PACK_ALIGNMENT) {
        g.pack_alignment = param;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) {
    glPixelStorei(pname, (GLint)param);
}

void glDrawBuffer(GLenum mode) {
    (void)mode;
    /* Only single buffer supported on 3DS */
}
