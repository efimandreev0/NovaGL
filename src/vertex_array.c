//
// Created by Notebook on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_vertex.size = size; g.va_vertex.type = type; g.va_vertex.stride = stride; g.va_vertex.pointer = pointer; g.va_vertex.vbo_id = g.bound_array_buffer;
}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_texcoord.size = size; g.va_texcoord.type = type; g.va_texcoord.stride = stride; g.va_texcoord.pointer = pointer; g.va_texcoord.vbo_id = g.bound_array_buffer;
}
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_color.size = size; g.va_color.type = type; g.va_color.stride = stride; g.va_color.pointer = pointer; g.va_color.vbo_id = g.bound_array_buffer;
}
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_normal.type = type; g.va_normal.stride = stride; g.va_normal.pointer = pointer; g.va_normal.vbo_id = g.bound_array_buffer;
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