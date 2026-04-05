//
// Created by Notebook on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

GLuint glGenLists(GLsizei range) {
    GLuint base = g.dl_next_base;
    g.dl_next_base += range;
    if (g.dl_next_base >= NOVA_DISPLAY_LISTS) g.dl_next_base = 1;
    for (GLsizei i = 0; i < range && (base + i) < NOVA_DISPLAY_LISTS; i++) {
        g.dl_store[base + i].count = 0; g.dl_store[base + i].used = 1;
    }
    return base;
}

void glNewList(GLuint list, GLenum mode) {
    (void)mode;
    if (list < NOVA_DISPLAY_LISTS) { g.dl_recording = list; g.dl_store[list].count = 0; }
}

void glEndList(void) { g.dl_recording = -1; }

void glCallList(GLuint list) { dl_execute(list); }

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 0;
        if (type == GL_UNSIGNED_INT) id = ((const GLuint*)lists)[i];
        else if (type == GL_UNSIGNED_BYTE) id = ((const GLubyte*)lists)[i];
        else if (type == GL_UNSIGNED_SHORT) id = ((const GLushort*)lists)[i];
        dl_execute(id);
    }
}

void glDeleteLists(GLuint list, GLsizei range) {
    for (GLsizei i = 0; i < range && (list + i) < NOVA_DISPLAY_LISTS; i++) {
        g.dl_store[list + i].used = 0; g.dl_store[list + i].count = 0;
    }
}