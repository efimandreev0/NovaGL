//
// created by efimandreev0 on 05.04.2026.
//

#include <stdio.h>

#include "NovaGL.h"
#include "utils.h"

// bad size/type/stride -> error and dont touch the array. GL_INT we let pass
// becouse some desktop ports give it and draw path can eat it
static int va_validate(GLint size, GLint min_size, GLint max_size, GLsizei stride) {
    if (size < min_size || size > max_size || stride < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return 0;
    }
    return 1;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    if (!va_validate(size, 2, 4, stride)) return;
    if (type != GL_BYTE && type != GL_SHORT && type != GL_FIXED && type != GL_FLOAT &&
        type != GL_INT) { /* GL_INT: desktop-GL leniency */
        gl_set_error(GL_INVALID_ENUM);
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
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    g.va_texcoord.size = size;
    g.va_texcoord.type = type;
    g.va_texcoord.stride = stride;
    g.va_texcoord.pointer = pointer;
    g.va_texcoord.vbo_id = g.bound_array_buffer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    // spec want 4 but we also take 3 (draw path put alpha=255), other is error
    if (size != 3 && size != 4) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (stride < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    /* All GL colour-array types are accepted; the draw path normalizes each per
     * the GL fixed->float rules (signed -> [-1,1], unsigned -> [0,1]). */
    if (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_INT && type != GL_UNSIGNED_INT &&
        type != GL_FIXED && type != GL_FLOAT) {
        gl_set_error(GL_INVALID_ENUM);
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
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_BYTE && type != GL_SHORT && type != GL_FIXED && type != GL_FLOAT) {
        gl_set_error(GL_INVALID_ENUM);
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
    else gl_set_error(GL_INVALID_ENUM);
}

void glDisableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 0;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 0;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 0;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 0;
    else gl_set_error(GL_INVALID_ENUM);
}

// ---- VAO ----
// real now, not a lie. a VAO just hold the 4 client arrays + index buffer.
// live state always sit in g.va_* , bind = save old slot, load new slot.

// copy the live arrays into a slot (and the element buffer binding too)
static void vao_save_live(VAOSlot *slot) {
    slot->vertex   = g.va_vertex;
    slot->texcoord = g.va_texcoord;
    slot->color    = g.va_color;
    slot->normal   = g.va_normal;
    slot->element_buffer = g.bound_element_array_buffer;
}

// load slot back into live state
static void vao_load_live(const VAOSlot *slot) {
    g.va_vertex   = slot->vertex;
    g.va_texcoord = slot->texcoord;
    g.va_color    = slot->color;
    g.va_normal   = slot->normal;
    g.bound_element_array_buffer = slot->element_buffer;
}

void glGenVertexArrays(GLsizei n, GLuint *arrays) {
    if (n < 0 || !arrays) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1; // 0 is reserved for default VAO
        while (id < NOVA_MAX_VAOS && g.vaos[id].in_use) id++;
        if (id == NOVA_MAX_VAOS) {
            gl_set_error(GL_OUT_OF_MEMORY);
            arrays[i] = 0;
            break;
        }
        memset(&g.vaos[id], 0, sizeof(g.vaos[id]));
        g.vaos[id].in_use = 1;
        arrays[i] = id;
    }
}

void glBindVertexArray(GLuint array) {
    /* Spec: glBindVertexArray takes the default VAO (0) or a name returned by
     * glGenVertexArrays and not since deleted; anything else (out of range OR a
     * never-generated name) is GL_INVALID_OPERATION. */
    if (array != 0 && (array >= NOVA_MAX_VAOS || !g.vaos[array].in_use)) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (array == g.bound_vao) return; // already there, dont thrash
    // stash what we have now, then bring the new one in
    vao_save_live(&g.vaos[g.bound_vao]);
    g.bound_vao = array;
    vao_load_live(&g.vaos[array]);
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    if (n < 0 || !arrays) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = arrays[i];
        if (id == 0 || id >= NOVA_MAX_VAOS || !g.vaos[id].in_use) continue;
        // killing the bound one -> spec say go back to default (0)
        if (id == g.bound_vao) {
            g.bound_vao = 0;
            vao_load_live(&g.vaos[0]);
        }
        memset(&g.vaos[id], 0, sizeof(g.vaos[id]));
    }
}

// ---- generic vertex attribs (GL2 style) ----
// PICA have no generic attrib slots, BUT we can still make the common apps work
// by aliasing the classic ARB_vertex_program index layout onto the fixed-function
// arrays: 0=position, 2=normal, 3=color, 8=texcoord0. apps that use weird custom
// indexes (from glGetAttribLocation, wich we return -1 anyway) fall trough to the
// warn. not perfect but cover the typical case.
#define NOVA_ATTR_POSITION 0
#define NOVA_ATTR_NORMAL   2
#define NOVA_ATTR_COLOR    3
#define NOVA_ATTR_TEXCOORD 8

static void warn_unknown_attrib(GLuint index) {
    static int warned = 0;
    if (!warned) {
        printf("[Nova]: generic vertex attrib %u has no fixed-function slot, ignored\n", (unsigned) index);
        warned = 1;
    }
}

// turn a generic index into the matching GL client-state cap. 0 = no match.
static GLenum generic_attrib_cap(GLuint index) {
    switch (index) {
        case NOVA_ATTR_POSITION: return GL_VERTEX_ARRAY;
        case NOVA_ATTR_NORMAL:   return GL_NORMAL_ARRAY;
        case NOVA_ATTR_COLOR:    return GL_COLOR_ARRAY;
        case NOVA_ATTR_TEXCOORD: return GL_TEXTURE_COORD_ARRAY;
        default:                 return 0;
    }
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                           const GLvoid *pointer) {
    // normalized flag we cant fully honor, but the conventional layout lines up
    // (color = normalized ubyte, position/texcoord = float) so it mostly fine.
    (void) normalized;
    switch (index) {
        case NOVA_ATTR_POSITION: glVertexPointer(size, type, stride, pointer);   break;
        case NOVA_ATTR_NORMAL:   glNormalPointer(type, stride, pointer);         break;
        case NOVA_ATTR_COLOR:    glColorPointer(size, type, stride, pointer);    break;
        case NOVA_ATTR_TEXCOORD: glTexCoordPointer(size, type, stride, pointer); break;
        default:                 warn_unknown_attrib(index);                     break;
    }
}

void glEnableVertexAttribArray(GLuint index) {
    GLenum cap = generic_attrib_cap(index);
    if (cap) glEnableClientState(cap);
    else warn_unknown_attrib(index);
}

void glDisableVertexAttribArray(GLuint index) {
    GLenum cap = generic_attrib_cap(index);
    if (cap) glDisableClientState(cap);
}