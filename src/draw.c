//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// === [DIAG] dump first N draw calls to stdout to compare against expected geometry ===
#define NOVA_DIAG_DRAW_LIMIT 10
static int s_diag_draw_count = 0;

static const uint8_t *diag_resolve_attr_base(const void *raw_pointer, GLuint vbo_id) {
    if (vbo_id == 0) {
        return (const uint8_t *)raw_pointer;
    }
    if (vbo_id >= NOVA_MAX_VBOS) return NULL;
    VBOSlot *slot = &g.vbos[vbo_id];
    if (!slot->allocated || !slot->data) return NULL;
    // packed PTC case is hard to inline; warn and bail.
    if (vbo_is_packed_ptc(slot)) return NULL;
    return (const uint8_t *)slot->data + (uintptr_t)raw_pointer;
}

static void diag_read_attr_floats(const void *raw_pointer, GLuint vbo_id, GLsizei stride,
                                  GLint size, GLenum type, int vertex_idx, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    const uint8_t *base = diag_resolve_attr_base(raw_pointer, vbo_id);
    if (!base) return;
    int eff_stride = stride;
    if (eff_stride == 0) {
        int comp_size = (type == GL_FLOAT) ? 4 : (type == GL_SHORT || type == GL_UNSIGNED_SHORT) ? 2 : 1;
        eff_stride = size * comp_size;
    }
    const uint8_t *p = base + (size_t)vertex_idx * (size_t)eff_stride;
    for (int c = 0; c < size && c < 4; c++) {
        if (type == GL_FLOAT) {
            float v;
            memcpy(&v, p + c * 4, sizeof(float));
            out[c] = v;
        } else if (type == GL_SHORT) {
            short v;
            memcpy(&v, p + c * 2, sizeof(short));
            out[c] = (float)v;
        } else if (type == GL_UNSIGNED_SHORT) {
            unsigned short v;
            memcpy(&v, p + c * 2, sizeof(unsigned short));
            out[c] = (float)v;
        } else if (type == GL_UNSIGNED_BYTE) {
            out[c] = (float)p[c];
        }
    }
}

static void diag_log_matrix(const char *tag, const C3D_Mtx *m) {
    if (!m) return;
    printf("[NovaDiag]   %s row0=(%.4f %.4f %.4f %.4f)\n", tag, m->r[0].x, m->r[0].y, m->r[0].z, m->r[0].w);
    printf("[NovaDiag]   %s row1=(%.4f %.4f %.4f %.4f)\n", tag, m->r[1].x, m->r[1].y, m->r[1].z, m->r[1].w);
    printf("[NovaDiag]   %s row2=(%.4f %.4f %.4f %.4f)\n", tag, m->r[2].x, m->r[2].y, m->r[2].z, m->r[2].w);
    printf("[NovaDiag]   %s row3=(%.4f %.4f %.4f %.4f)\n", tag, m->r[3].x, m->r[3].y, m->r[3].z, m->r[3].w);
}

static void diag_log_draw(const char *tag, GLenum mode, GLint first, GLsizei count) {
    if (s_diag_draw_count >= NOVA_DIAG_DRAW_LIMIT) return;
    int idx = s_diag_draw_count++;

    GLuint bound = g.bound_texture[g.active_texture_unit];
    TexSlot *slot = (bound > 0 && bound < NOVA_MAX_TEXTURES) ? &g.textures[bound] : NULL;

    printf("[NovaDiag] draw#%d %s mode=0x%X first=%d count=%d tex_id=%u",
           idx, tag, (unsigned)mode, (int)first, (int)count, (unsigned)bound);
    if (slot && slot->allocated) {
        printf(" slot={w=%d h=%d orig=%dx%d pot=%dx%d fmt=%d solid=%d wrap=(0x%X,0x%X)}",
               slot->width, slot->height, slot->orig_width, slot->orig_height,
               slot->pot_w, slot->pot_h, (int)slot->fmt, slot->is_solid_optimized,
               (unsigned)slot->wrap_s, (unsigned)slot->wrap_t);
    } else {
        printf(" slot=<unallocated>");
    }
    printf(" va_vertex={en=%d size=%d type=0x%X stride=%d ptr=%p vbo=%u}",
           g.va_vertex.enabled, g.va_vertex.size, (unsigned)g.va_vertex.type,
           (int)g.va_vertex.stride, g.va_vertex.pointer, (unsigned)g.va_vertex.vbo_id);
    printf(" va_texcoord={en=%d size=%d type=0x%X stride=%d ptr=%p vbo=%u}\n",
           g.va_texcoord.enabled, g.va_texcoord.size, (unsigned)g.va_texcoord.type,
           (int)g.va_texcoord.stride, g.va_texcoord.pointer, (unsigned)g.va_texcoord.vbo_id);

    printf("[NovaDiag]   viewport=(%d,%d %dx%d) matrix_mode=0x%X proj_sp=%d mv_sp=%d cur_color=(%.2f,%.2f,%.2f,%.2f)\n",
           (int)g.vp_x, (int)g.vp_y, (int)g.vp_w, (int)g.vp_h,
           (unsigned)g.matrix_mode, g.proj_sp, g.mv_sp,
           g.cur_color[0], g.cur_color[1], g.cur_color[2], g.cur_color[3]);

    // Current matrices (top of stack).
    if (g.proj_sp >= 0 && g.proj_sp < NOVA_MATRIX_STACK) {
        diag_log_matrix("proj", &g.proj_stack[g.proj_sp]);
    }
    if (g.mv_sp >= 0 && g.mv_sp < NOVA_MATRIX_STACK) {
        diag_log_matrix("mv  ", &g.mv_stack[g.mv_sp]);
    }

    int verts_to_print = count < 4 ? count : 4;
    if (g.va_texcoord.enabled) {
        VBOSlot *t_slot = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS)
                          ? &g.vbos[g.va_texcoord.vbo_id] : NULL;
        if (t_slot && vbo_is_packed_ptc(t_slot)) {
            printf("[NovaDiag]   uv array in VBO %u is PACKED_PTC (half-floats), not dumped\n",
                   (unsigned)g.va_texcoord.vbo_id);
        } else {
            for (int v = 0; v < verts_to_print; v++) {
                float uv[4];
                diag_read_attr_floats(g.va_texcoord.pointer, g.va_texcoord.vbo_id,
                                      g.va_texcoord.stride, g.va_texcoord.size,
                                      g.va_texcoord.type, first + v, uv);
                printf("[NovaDiag]   vert[%d] uv=(%.6f, %.6f)\n", v, uv[0], uv[1]);
            }
        }
    }
    if (g.va_vertex.enabled) {
        VBOSlot *v_slot = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS)
                          ? &g.vbos[g.va_vertex.vbo_id] : NULL;
        if (v_slot && vbo_is_packed_ptc(v_slot)) {
            printf("[NovaDiag]   pos array in VBO %u is PACKED_PTC (half-floats), not dumped\n",
                   (unsigned)g.va_vertex.vbo_id);
        } else {
            for (int v = 0; v < verts_to_print; v++) {
                float xyz[4];
                diag_read_attr_floats(g.va_vertex.pointer, g.va_vertex.vbo_id,
                                      g.va_vertex.stride, g.va_vertex.size,
                                      g.va_vertex.type, first + v, xyz);
                printf("[NovaDiag]   vert[%d] pos=(%.4f, %.4f, %.4f, %.4f)\n",
                       v, xyz[0], xyz[1], xyz[2], xyz[3]);
            }
        }
    }
    fflush(stdout);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    diag_log_draw("DrawArrays", mode, first, count);
    nova_draw_internal(mode, first, count, 0, 0, NULL);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    diag_log_draw("DrawElements", mode, 0, count);
    nova_draw_internal(mode, 0, count, 1, type, indices);
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                         const GLvoid *indices) {
    /* start/end are an optimization hint about which vertices the indices reference.
     * NovaGL's draw path doesn't pre-transform vertices, so the hint isn't actionable. */
    (void) start;
    (void) end;
    nova_draw_internal(mode, 0, count, 1, type, indices);
}

/* ------------------------------------------------------------------------
 * Base-vertex draw helpers
 * ------------------------------------------------------------------------
 * Desktop GL lets the driver fold `basevertex` into the vertex fetch unit so
 * the index buffer doesn't have to be touched. PICA200 has no such offset, so
 * for basevertex != 0 we materialize a shifted index buffer on the heap and
 * draw out of that. Tiny one-frame allocations — fine for the cases Arx
 * actually hits (low-poly meshes with stitched submeshes), but if profiling
 * later shows a hot path here, switching to a per-frame scratch arena makes
 * sense. */

static void draw_elements_basevertex_impl(GLenum mode, GLsizei count, GLenum type,
                                          const GLvoid *indices, GLint basevertex) {
    if (basevertex == 0 || count <= 0) {
        nova_draw_internal(mode, 0, count, 1, type, indices);
        return;
    }

    size_t element_size;
    switch (type) {
        case GL_UNSIGNED_BYTE:  element_size = 1; break;
        case GL_UNSIGNED_SHORT: element_size = 2; break;
        case GL_UNSIGNED_INT:   element_size = 4; break;
        default:
            /* Unknown index type — fall back to no-offset draw rather than corrupt memory. */
            nova_draw_internal(mode, 0, count, 1, type, indices);
            return;
    }

    size_t bytes = (size_t) count * element_size;
    void *scratch = malloc(bytes);
    if (!scratch) {
        /* Out of memory: skip this draw call rather than crash. */
        return;
    }
    memcpy(scratch, indices, bytes);

    if (type == GL_UNSIGNED_BYTE) {
        uint8_t *p = (uint8_t *) scratch;
        for (GLsizei i = 0; i < count; i++) p[i] = (uint8_t) (p[i] + basevertex);
    } else if (type == GL_UNSIGNED_SHORT) {
        uint16_t *p = (uint16_t *) scratch;
        for (GLsizei i = 0; i < count; i++) p[i] = (uint16_t) (p[i] + basevertex);
    } else { /* GL_UNSIGNED_INT */
        uint32_t *p = (uint32_t *) scratch;
        for (GLsizei i = 0; i < count; i++) p[i] = p[i] + (uint32_t) basevertex;
    }

    nova_draw_internal(mode, 0, count, 1, type, scratch);
    free(scratch);
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const GLvoid *indices, GLint basevertex) {
    draw_elements_basevertex_impl(mode, count, type, indices, basevertex);
}

void glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                   GLenum type, const GLvoid *indices, GLint basevertex) {
    (void) start;
    (void) end;
    draw_elements_basevertex_impl(mode, count, type, indices, basevertex);
}
