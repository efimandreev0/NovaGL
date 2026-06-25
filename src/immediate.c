/*
 * immediate.c - OpenGL Immediate Mode Emulation
 *
 * Emulates deprecated OpenGL 1.x immediate mode using internal vertex arrays.
 * This allows running older OpenGL code on OpenGL ES 1.1 compatible hardware.
 */

#include "NovaGL.h"
#include "utils.h"
#include <math.h>

static struct {
    GLenum mode;
    int in_begin;
    int vertex_count;

    uint8_t *mapped_ptr;
    int mapped_max_verts;
    int start_offset;

    GLfloat current_texcoord[4]; // [s, t, r, q]
} imm;

void glBegin(GLenum mode) {
    /* Validate against the full GL primitive set (points/lines stay legal even
     * though PICA degrades them — see README). */
    switch (mode) {
        case GL_POINTS: case GL_LINES: case GL_LINE_LOOP: case GL_LINE_STRIP:
        case GL_TRIANGLES: case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN:
        case GL_QUADS:
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    if (imm.in_begin) {
        /* Nested glBegin is GL_INVALID_OPERATION. */
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    imm.mode = mode;
    imm.in_begin = 1;
    imm.vertex_count = 0;

    imm.current_texcoord[0] = 0.0f;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;

    /* Start small (256 verts = 6KB) and grow on-demand via add_vertex's
     * overflow path. Pre-reserving 4096 verts (96KB) every glBegin used to
     * exhaust the 2MB client_array_buf in ~22 calls, triggering a mid-frame
     * FrameSplit + GPU wait — fatal for HUD-heavy scenes. */
    imm.mapped_max_verts = 256;
    int req_size = imm.mapped_max_verts * 24;

    if (g.client_array_buf_offset + req_size > g.client_array_buf_size) {
        C3D_FrameSplit(0);
        g.client_array_buf_offset = 0;
    }

    imm.start_offset = g.client_array_buf_offset;
    imm.mapped_ptr = (uint8_t *) g.client_array_buf + g.client_array_buf_offset;
}

/* If the caller pushes more verts than the current reserve, double the reserve
 * by allocating fresh space at the next free position in the ring and
 * memcpy-ing the verts so far. Cheap: glBegin runs are typically dozens of
 * verts, rare runs scale to thousands and rebuild once or twice. */
static int imm_grow_reserve(void) {
    int new_max = imm.mapped_max_verts * 2;
    int new_size = new_max * 24;
    if (new_size > g.client_array_buf_size / 2) {
        /* Don't take more than half the ring for a single glBegin — better to
         * truncate than evict everything else for one immediate batch. */
        return 0;
    }
    int new_offset = imm.start_offset + new_size;
    if (new_offset > g.client_array_buf_size) {
        /* Out of space in the current ring window. Splitting here would stall;
         * just stop accepting verts. */
        return 0;
    }
    /* Old reserve happens to still be contiguous and live (no draws yet), so
     * widening it in place is enough — no copy needed. We only need to make
     * sure g.client_array_buf_offset is far enough out that future allocations
     * don't tread on us before glEnd; bump it tentatively. */
    g.client_array_buf_offset = new_offset;
    imm.mapped_max_verts = new_max;
    return 1;
}

static void imm_draw_packed_run(GLenum mode, GPU_Primitive_t prim, uint8_t *base, int count) {
    nova_setup_attr_info(3);
    nova_setup_buf_info(base, 24);

    if (mode == GL_QUADS) {
        #ifdef NOVAGL_QUAD_AS_FAN
        if (count == 4) {
            C3D_DrawArrays(GPU_TRIANGLE_FAN, 0, 4);
        } else {
            draw_emulated_quads(count);
        }
        #else
        draw_emulated_quads(count);
        #endif
    } else {
        C3D_DrawArrays(prim, 0, count);
    }
}

void glEnd(void) {
    if (!imm.in_begin) {
        /* glEnd without a matching glBegin is GL_INVALID_OPERATION. */
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    imm.in_begin = 0;

    if (imm.vertex_count == 0) return;

    g.client_array_buf_offset = imm.start_offset + (imm.vertex_count * 24);

    apply_gpu_state();

    GPU_Primitive_t prim = gl_to_gpu_primitive(imm.mode);

    GSPGPU_FlushDataCache(imm.mapped_ptr, imm.vertex_count * 24);
    imm_draw_packed_run(imm.mode, prim, imm.mapped_ptr, imm.vertex_count);
}

/* Helper function to add a vertex to the mapped ring buffer */
static inline void add_vertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    (void) w;
    if (!imm.in_begin) return;
    if (imm.vertex_count >= imm.mapped_max_verts) {
        if (!imm_grow_reserve()) return; /* truncate at hard limit */
    }

    float *out_f = (float *) (imm.mapped_ptr + imm.vertex_count * 24);
    uint8_t *out_c = (uint8_t *) (out_f + 5);

    out_f[0] = x;
    out_f[1] = y;
    out_f[2] = z;
    out_f[3] = imm.current_texcoord[0]; // s
    out_f[4] = imm.current_texcoord[1]; // t

    // Read color from global state directly so glColor* calls between vertices
    // take effect within the current glBegin/glEnd block. Clamp+round to match
    // the vertex-array draw path (out-of-[0,1] colours no longer wrap/garble).
    out_c[0] = (uint8_t) (clampf(g.cur_color[0], 0.0f, 1.0f) * 255.0f + 0.5f);
    out_c[1] = (uint8_t) (clampf(g.cur_color[1], 0.0f, 1.0f) * 255.0f + 0.5f);
    out_c[2] = (uint8_t) (clampf(g.cur_color[2], 0.0f, 1.0f) * 255.0f + 0.5f);
    out_c[3] = (uint8_t) (clampf(g.cur_color[3], 0.0f, 1.0f) * 255.0f + 0.5f);

    imm.vertex_count++;
}

void glArrayElement(GLint i) {
    if (!imm.in_begin) return;

    if (g.va_color.enabled && g.va_color.pointer) {
        float c[4] = {0, 0, 0, 1};
        read_vertex_attrib_float(
            c, (const uint8_t *) g.va_color.pointer + i * calc_stride(g.va_color.stride, g.va_color.size,
                                                                      g.va_color.type), g.va_color.size,
            g.va_color.type);
        g.cur_color[0] = c[0];
        g.cur_color[1] = c[1];
        g.cur_color[2] = c[2];
        g.cur_color[3] = c[3];
    }

    if (g.va_texcoord.enabled && g.va_texcoord.pointer) {
        float t[4] = {0, 0, 0, 1};
        read_vertex_attrib_float(
            t, (const uint8_t *) g.va_texcoord.pointer + i * calc_stride(
                   g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type), g.va_texcoord.size,
            g.va_texcoord.type);
        imm.current_texcoord[0] = t[0];
        imm.current_texcoord[1] = t[1];
    }

    if (g.va_vertex.enabled && g.va_vertex.pointer) {
        float v[4] = {0, 0, 0, 1};
        read_vertex_attrib_float(
            v, (const uint8_t *) g.va_vertex.pointer + i * calc_stride(g.va_vertex.stride, g.va_vertex.size,
                                                                       g.va_vertex.type), g.va_vertex.size,
            g.va_vertex.type);
        add_vertex(v[0], v[1], v[2], v[3]);
    }
}

/* Vertex functions */
void glVertex2d(GLdouble x, GLdouble y) { add_vertex((GLfloat) x, (GLfloat) y, 0.0f, 1.0f); }
void glVertex2dv(const GLdouble *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], 0.0f, 1.0f); }
void glVertex2f(GLfloat x, GLfloat y) { add_vertex(x, y, 0.0f, 1.0f); }
void glVertex2fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], 0.0f, 1.0f); }
void glVertex2i(GLint x, GLint y) { add_vertex((GLfloat) x, (GLfloat) y, 0.0f, 1.0f); }
void glVertex2iv(const GLint *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], 0.0f, 1.0f); }
void glVertex2s(GLshort x, GLshort y) { add_vertex((GLfloat) x, (GLfloat) y, 0.0f, 1.0f); }
void glVertex2sv(const GLshort *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], 0.0f, 1.0f); }

void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, 1.0f); }
void glVertex3dv(const GLdouble *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], 1.0f); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { add_vertex(x, y, z, 1.0f); }
void glVertex3fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], v[2], 1.0f); }
void glVertex3i(GLint x, GLint y, GLint z) { add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, 1.0f); }
void glVertex3iv(const GLint *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], 1.0f); }
void glVertex3s(GLshort x, GLshort y, GLshort z) { add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, 1.0f); }
void glVertex3sv(const GLshort *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], 1.0f); }

void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) {
    add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, (GLfloat) w);
}

void glVertex4dv(const GLdouble *v) {
    if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], (GLfloat) v[3]);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { add_vertex(x, y, z, w); }
void glVertex4fv(const GLfloat *v) { if (v) add_vertex(v[0], v[1], v[2], v[3]); }
void glVertex4i(GLint x, GLint y, GLint z, GLint w) { add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, (GLfloat) w); }
void glVertex4iv(const GLint *v) { if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], (GLfloat) v[3]); }

void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) {
    add_vertex((GLfloat) x, (GLfloat) y, (GLfloat) z, (GLfloat) w);
}

void glVertex4sv(const GLshort *v) {
    if (v) add_vertex((GLfloat) v[0], (GLfloat) v[1], (GLfloat) v[2], (GLfloat) v[3]);
}

/* TexCoord functions - update current texcoord */
void glTexCoord1d(GLdouble s) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord1dv(const GLdouble *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = 0.0f;
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord1f(GLfloat s) {
    imm.current_texcoord[0] = s;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord1fv(const GLfloat *v) {
    if (v) {
        imm.current_texcoord[0] = v[0];
        imm.current_texcoord[1] = 0.0f;
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord1i(GLint s) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord1iv(const GLint *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = 0.0f;
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord1s(GLshort s) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord1sv(const GLshort *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = 0.0f;
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord2d(GLdouble s, GLdouble t) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord2dv(const GLdouble *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord2f(GLfloat s, GLfloat t) {
    imm.current_texcoord[0] = s;
    imm.current_texcoord[1] = t;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord2fv(const GLfloat *v) {
    if (v) {
        imm.current_texcoord[0] = v[0];
        imm.current_texcoord[1] = v[1];
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord2i(GLint s, GLint t) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord2iv(const GLint *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord2s(GLshort s, GLshort t) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord2sv(const GLshort *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = 0.0f;
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord3dv(const GLdouble *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) {
    imm.current_texcoord[0] = s;
    imm.current_texcoord[1] = t;
    imm.current_texcoord[2] = r;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord3fv(const GLfloat *v) {
    if (v) {
        imm.current_texcoord[0] = v[0];
        imm.current_texcoord[1] = v[1];
        imm.current_texcoord[2] = v[2];
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord3i(GLint s, GLint t, GLint r) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord3iv(const GLint *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord3s(GLshort s, GLshort t, GLshort r) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = 1.0f;
}

void glTexCoord3sv(const GLshort *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = 1.0f;
    }
}

void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = (GLfloat) q;
}

void glTexCoord4dv(const GLdouble *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = (GLfloat) v[3];
    }
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    imm.current_texcoord[0] = s;
    imm.current_texcoord[1] = t;
    imm.current_texcoord[2] = r;
    imm.current_texcoord[3] = q;
}

void glTexCoord4fv(const GLfloat *v) {
    if (v) {
        imm.current_texcoord[0] = v[0];
        imm.current_texcoord[1] = v[1];
        imm.current_texcoord[2] = v[2];
        imm.current_texcoord[3] = v[3];
    }
}

void glTexCoord4i(GLint s, GLint t, GLint r, GLint q) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = (GLfloat) q;
}

void glTexCoord4iv(const GLint *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = (GLfloat) v[3];
    }
}

void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) {
    imm.current_texcoord[0] = (GLfloat) s;
    imm.current_texcoord[1] = (GLfloat) t;
    imm.current_texcoord[2] = (GLfloat) r;
    imm.current_texcoord[3] = (GLfloat) q;
}

void glTexCoord4sv(const GLshort *v) {
    if (v) {
        imm.current_texcoord[0] = (GLfloat) v[0];
        imm.current_texcoord[1] = (GLfloat) v[1];
        imm.current_texcoord[2] = (GLfloat) v[2];
        imm.current_texcoord[3] = (GLfloat) v[3];
    }
}
