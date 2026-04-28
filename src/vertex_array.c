//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_vertex.size = size;
    g.va_vertex.type = type;
    g.va_vertex.stride = stride;
    g.va_vertex.pointer = pointer;
    g.va_vertex.vbo_id = g.bound_array_buffer;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_texcoord.size = size;
    g.va_texcoord.type = type;
    g.va_texcoord.stride = stride;
    g.va_texcoord.pointer = pointer;
    g.va_texcoord.vbo_id = g.bound_array_buffer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_color.size = size;
    g.va_color.type = type;
    g.va_color.stride = stride;
    g.va_color.pointer = pointer;
    g.va_color.vbo_id = g.bound_array_buffer;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
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
}

void glDisableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 0;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 0;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 0;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 0;
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
