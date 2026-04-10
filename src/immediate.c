/*
 * immediate.c - OpenGL Immediate Mode Emulation
 *
 * Emulates deprecated OpenGL 1.x immediate mode using internal vertex arrays.
 * This allows running older OpenGL code on OpenGL ES 1.1 compatible hardware.
 */

#include "NovaGL.h"
#include "utils.h"

#define NOVA_MAX_IMMEDIATE_VERTICES 4096

typedef struct {
    GLfloat x, y, z, w;
} ImmediateVertex;

typedef struct {
    GLfloat r, g, b, a;
} ImmediateColor;

typedef struct {
    GLfloat s, t, r, q;
} ImmediateTexCoord;

typedef struct {
    GLfloat nx, ny, nz;
} ImmediateNormal;

static struct {
    GLenum mode;
    int in_begin;
    int vertex_count;
    
    ImmediateVertex vertices[NOVA_MAX_IMMEDIATE_VERTICES];
    ImmediateColor colors[NOVA_MAX_IMMEDIATE_VERTICES];
    ImmediateTexCoord texcoords[NOVA_MAX_IMMEDIATE_VERTICES];
    ImmediateNormal normals[NOVA_MAX_IMMEDIATE_VERTICES];
    
    ImmediateColor current_color;
    ImmediateTexCoord current_texcoord;
    ImmediateNormal current_normal;
} imm;

void glBegin(GLenum mode) {
    if (imm.in_begin) return;
    
    imm.mode = mode;
    imm.in_begin = 1;
    imm.vertex_count = 0;
    
    /* Initialize current values from global state */
    imm.current_color.r = g.cur_color[0];
    imm.current_color.g = g.cur_color[1];
    imm.current_color.b = g.cur_color[2];
    imm.current_color.a = g.cur_color[3];
    
    imm.current_texcoord.s = 0.0f;
    imm.current_texcoord.t = 0.0f;
    imm.current_texcoord.r = 0.0f;
    imm.current_texcoord.q = 1.0f;
    
    imm.current_normal.nx = 0.0f;
    imm.current_normal.ny = 0.0f;
    imm.current_normal.nz = 1.0f;
}

void glEnd(void) {
    if (!imm.in_begin || imm.vertex_count == 0) {
        imm.in_begin = 0;
        return;
    }
    
    /* Save current vertex array state */
    int saved_va_vertex_enabled = g.va_vertex.enabled;
    int saved_va_color_enabled = g.va_color.enabled;
    int saved_va_texcoord_enabled = g.va_texcoord.enabled;
    int saved_va_normal_enabled = g.va_normal.enabled;
    
    /* Set up vertex array pointers to our immediate mode data */
    g.va_vertex.enabled = 1;
    g.va_vertex.size = 4;
    g.va_vertex.type = GL_FLOAT;
    g.va_vertex.stride = 0;
    g.va_vertex.pointer = imm.vertices;
    g.va_vertex.vbo_id = 0;
    
    g.va_color.enabled = 1;
    g.va_color.size = 4;
    g.va_color.type = GL_FLOAT;
    g.va_color.stride = 0;
    g.va_color.pointer = imm.colors;
    g.va_color.vbo_id = 0;
    
    g.va_texcoord.enabled = 1;
    g.va_texcoord.size = 4;
    g.va_texcoord.type = GL_FLOAT;
    g.va_texcoord.stride = 0;
    g.va_texcoord.pointer = imm.texcoords;
    g.va_texcoord.vbo_id = 0;
    
    /* Convert and draw based on mode */
    GLenum draw_mode;
    int draw_count = imm.vertex_count;
    
    switch (imm.mode) {
        case GL_POINTS:
            draw_mode = GL_POINTS;
            break;
        case GL_LINES:
            draw_mode = GL_LINES;
            break;
        case GL_LINE_LOOP:
            /* Line loop needs special handling - draw as line strip plus closing line */
            if (imm.vertex_count >= 2) {
                /* For now, draw as line strip */
                draw_mode = GL_LINE_STRIP;
            } else {
                draw_mode = GL_LINES;
            }
            break;
        case GL_LINE_STRIP:
            draw_mode = GL_LINE_STRIP;
            break;
        case GL_TRIANGLES:
            draw_mode = GL_TRIANGLES;
            break;
        case GL_TRIANGLE_STRIP:
            draw_mode = GL_TRIANGLE_STRIP;
            break;
        case GL_TRIANGLE_FAN:
            draw_mode = GL_TRIANGLE_FAN;
            break;
        case GL_QUADS:
            draw_mode = GL_QUADS;
            break;
        default:
            draw_mode = GL_TRIANGLES;
            break;
    }
    
    /* Draw the primitives */
    nova_draw_internal(draw_mode, 0, draw_count, 0, 0, NULL);
    
    /* Restore vertex array state */
    g.va_vertex.enabled = saved_va_vertex_enabled;
    g.va_color.enabled = saved_va_color_enabled;
    g.va_texcoord.enabled = saved_va_texcoord_enabled;
    g.va_normal.enabled = saved_va_normal_enabled;
    
    imm.in_begin = 0;
}

void glArrayElement(GLint i) {
    if (!imm.in_begin || i < 0 || i >= imm.vertex_count) return;
    
    /* Add vertex at index i to the current primitive */
    if (imm.vertex_count < NOVA_MAX_IMMEDIATE_VERTICES) {
        imm.vertices[imm.vertex_count] = imm.vertices[i];
        imm.colors[imm.vertex_count] = imm.colors[i];
        imm.texcoords[imm.vertex_count] = imm.texcoords[i];
        imm.normals[imm.vertex_count] = imm.normals[i];
        imm.vertex_count++;
    }
}

/* Helper function to add a vertex */
static void add_vertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (!imm.in_begin || imm.vertex_count >= NOVA_MAX_IMMEDIATE_VERTICES) return;
    
    imm.vertices[imm.vertex_count].x = x;
    imm.vertices[imm.vertex_count].y = y;
    imm.vertices[imm.vertex_count].z = z;
    imm.vertices[imm.vertex_count].w = w;
    
    imm.colors[imm.vertex_count] = imm.current_color;
    imm.texcoords[imm.vertex_count] = imm.current_texcoord;
    imm.normals[imm.vertex_count] = imm.current_normal;
    
    imm.vertex_count++;
}

/* Vertex functions */
void glVertex2d(GLdouble x, GLdouble y) { add_vertex((GLfloat)x, (GLfloat)y, 0.0f, 1.0f); }
void glVertex2dv(const GLdouble *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], 0.0f, 1.0f); }
void glVertex2f(GLfloat x, GLfloat y) { add_vertex(x, y, 0.0f, 1.0f); }
void glVertex2fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], 0.0f, 1.0f); }
void glVertex2i(GLint x, GLint y) { add_vertex((GLfloat)x, (GLfloat)y, 0.0f, 1.0f); }
void glVertex2iv(const GLint *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], 0.0f, 1.0f); }
void glVertex2s(GLshort x, GLshort y) { add_vertex((GLfloat)x, (GLfloat)y, 0.0f, 1.0f); }
void glVertex2sv(const GLshort *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], 0.0f, 1.0f); }

void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f); }
void glVertex3dv(const GLdouble *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1.0f); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { add_vertex(x, y, z, 1.0f); }
void glVertex3fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], v[2], 1.0f); }
void glVertex3i(GLint x, GLint y, GLint z) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f); }
void glVertex3iv(const GLint *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1.0f); }
void glVertex3s(GLshort x, GLshort y, GLshort z) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f); }
void glVertex3sv(const GLshort *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1.0f); }

void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void glVertex4dv(const GLdouble *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { add_vertex(x, y, z, w); }
void glVertex4fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], v[2], v[3]); }
void glVertex4i(GLint x, GLint y, GLint z, GLint w) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void glVertex4iv(const GLint *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }
void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { add_vertex((GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w); }
void glVertex4sv(const GLshort *v) { if (v) add_vertex((GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]); }

/* TexCoord functions - update current texcoord */
void glTexCoord1d(GLdouble s) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord1dv(const GLdouble *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord1f(GLfloat s) { imm.current_texcoord.s = s; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord1fv(const GLfloat *v) { if (v) { imm.current_texcoord.s = v[0]; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord1i(GLint s) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord1iv(const GLint *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord1s(GLshort s) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord1sv(const GLshort *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = 0.0f; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }

void glTexCoord2d(GLdouble s, GLdouble t) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord2dv(const GLdouble *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord2f(GLfloat s, GLfloat t) { imm.current_texcoord.s = s; imm.current_texcoord.t = t; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord2fv(const GLfloat *v) { if (v) { imm.current_texcoord.s = v[0]; imm.current_texcoord.t = v[1]; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord2i(GLint s, GLint t) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord2iv(const GLint *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }
void glTexCoord2s(GLshort s, GLshort t) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; }
void glTexCoord2sv(const GLshort *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = 0.0f; imm.current_texcoord.q = 1.0f; } }

void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = 1.0f; }
void glTexCoord3dv(const GLdouble *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = 1.0f; } }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { imm.current_texcoord.s = s; imm.current_texcoord.t = t; imm.current_texcoord.r = r; imm.current_texcoord.q = 1.0f; }
void glTexCoord3fv(const GLfloat *v) { if (v) { imm.current_texcoord.s = v[0]; imm.current_texcoord.t = v[1]; imm.current_texcoord.r = v[2]; imm.current_texcoord.q = 1.0f; } }
void glTexCoord3i(GLint s, GLint t, GLint r) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = 1.0f; }
void glTexCoord3iv(const GLint *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = 1.0f; } }
void glTexCoord3s(GLshort s, GLshort t, GLshort r) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = 1.0f; }
void glTexCoord3sv(const GLshort *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = 1.0f; } }

void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = (GLfloat)q; }
void glTexCoord4dv(const GLdouble *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = (GLfloat)v[3]; } }
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { imm.current_texcoord.s = s; imm.current_texcoord.t = t; imm.current_texcoord.r = r; imm.current_texcoord.q = q; }
void glTexCoord4fv(const GLfloat *v) { if (v) { imm.current_texcoord.s = v[0]; imm.current_texcoord.t = v[1]; imm.current_texcoord.r = v[2]; imm.current_texcoord.q = v[3]; } }
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = (GLfloat)q; }
void glTexCoord4iv(const GLint *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = (GLfloat)v[3]; } }
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { imm.current_texcoord.s = (GLfloat)s; imm.current_texcoord.t = (GLfloat)t; imm.current_texcoord.r = (GLfloat)r; imm.current_texcoord.q = (GLfloat)q; }
void glTexCoord4sv(const GLshort *v) { if (v) { imm.current_texcoord.s = (GLfloat)v[0]; imm.current_texcoord.t = (GLfloat)v[1]; imm.current_texcoord.r = (GLfloat)v[2]; imm.current_texcoord.q = (GLfloat)v[3]; } }
