//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

/* Spec validation helpers (GL ES 1.1 §2.8): invalid size/type/stride raise
 * an error and leave the array state unchanged. Desktop types (GL_INT,
 * GL_DOUBLE-as-float ports won't hit this) are accepted leniently where the
 * draw path can convert them. */
static int va_validate(GLint size, GLint min_size, GLint max_size, GLsizei stride) {
    if (size < min_size || size > max_size || stride < 0) {
        g.last_error = GL_INVALID_VALUE;
        return 0;
    }
    return 1;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (!va_validate(size, 2, 4, stride)) return;
    if (type != GL_BYTE && type != GL_SHORT && type != GL_FIXED && type != GL_FLOAT &&
        type != GL_INT) { /* GL_INT: desktop-GL leniency */
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.va_vertex.size = size;
    g.va_vertex.type = type;
    g.va_vertex.stride = stride;
    g.va_vertex.pointer = pointer;
    g.va_vertex.vbo_id = g.bound_array_buffer;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (!va_validate(size, 2, 4, stride)) return;
    if (type != GL_BYTE && type != GL_SHORT && type != GL_FIXED && type != GL_FLOAT &&
        type != GL_INT) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.va_texcoord.size = size;
    g.va_texcoord.type = type;
    g.va_texcoord.stride = stride;
    g.va_texcoord.pointer = pointer;
    g.va_texcoord.vbo_id = g.bound_array_buffer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    /* ES 1.1: size must be 4. We accept 3 as a desktop-GL leniency (the draw
     * path pads alpha=255), anything else is GL_INVALID_VALUE. */
    if (size != 3 && size != 4) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (stride < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (type != GL_UNSIGNED_BYTE && type != GL_FIXED && type != GL_FLOAT) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.va_color.size = size;
    g.va_color.type = type;
    g.va_color.stride = stride;
    g.va_color.pointer = pointer;
    g.va_color.vbo_id = g.bound_array_buffer;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (stride < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (type != GL_BYTE && type != GL_SHORT && type != GL_FIXED && type != GL_FLOAT) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    g.va_normal.size = 3;
    g.va_normal.type = type;
    g.va_normal.stride = stride;
    g.va_normal.pointer = pointer;
    g.va_normal.vbo_id = g.bound_array_buffer;
}

void glEnableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 1;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 1;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 1;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 1;
    else g.last_error = GL_INVALID_ENUM;
}

void glDisableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 0;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 0;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 0;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 0;
    else g.last_error = GL_INVALID_ENUM;
}

/* GL 3.0+ VAO stubs */
void glGenVertexArrays(GLsizei n, GLuint *arrays) {
    for (GLsizei i = 0; i < n; i++) arrays[i] = i + 1;
}

void glBindVertexArray(GLuint array) { (void) array; }

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    (void) n;
    (void) arrays;
}

/* GL 2.0+ vertex attrib pointer stubs */
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                           const GLvoid *pointer) {
    (void) index;
    (void) size;
    (void) type;
    (void) normalized;
    (void) stride;
    (void) pointer;
}

void glEnableVertexAttribArray(GLuint index) { (void) index; }

void glDisableVertexAttribArray(GLuint index) { (void) index; }