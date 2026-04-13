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

    uint8_t* mapped_ptr;
    int mapped_max_verts;
    int start_offset;

    GLfloat current_color[4];
    GLfloat current_texcoord[4]; // [s, t, r, q]
} imm;

void glBegin(GLenum mode) {
    if (imm.in_begin) return;
    imm.mode = mode;
    imm.in_begin = 1;
    imm.vertex_count = 0;

    imm.current_color[0] = g.cur_color[0];
    imm.current_color[1] = g.cur_color[1];
    imm.current_color[2] = g.cur_color[2];
    imm.current_color[3] = g.cur_color[3];

    imm.current_texcoord[0] = 0.0f;
    imm.current_texcoord[1] = 0.0f;
    imm.current_texcoord[2] = 0.0f;
    imm.current_texcoord[3] = 1.0f;

    imm.mapped_max_verts = 4096;
    int req_size = imm.mapped_max_verts * 24;

    if (g.client_array_buf_offset + req_size > g.client_array_buf_size) {
        C3D_FrameSplit(0);
        g.client_array_buf_offset = 0;
    }

    imm.start_offset = g.client_array_buf_offset;
    imm.mapped_ptr = (uint8_t*)g.client_array_buf + g.client_array_buf_offset;
}

/* --- Tiled texture helpers (shared with NovaGL.c) --- */

static int imm_tiled_page_from_uv(const TexSlot *slot, float u, float v) {
    int tx = (int)floorf((u * (float)slot->width) / (float)slot->tile_w);
    int ty = (int)floorf((v * (float)slot->height) / (float)slot->tile_h);
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= slot->tiles_x) tx = slot->tiles_x - 1;
    if (ty >= slot->tiles_y) ty = slot->tiles_y - 1;
    return ty * slot->tiles_x + tx;
}

static int imm_tiled_page_for_primitive(const TexSlot *slot, const uint8_t *base, int prim_start, int prim_verts) {
    const float *uv0 = (const float*)(base + prim_start * 24 + 12);
    int page = imm_tiled_page_from_uv(slot, uv0[0], uv0[1]);
    for (int i = 1; i < prim_verts; i++) {
        const float *uv = (const float*)(base + (prim_start + i) * 24 + 12);
        if (imm_tiled_page_from_uv(slot, uv[0], uv[1]) != page)
            return page;
    }
    return page;
}

static void imm_remap_tiled_uvs(const TexSlot *slot, int page_index, uint8_t *base, int vert_count) {
    int tx = page_index % slot->tiles_x;
    int ty = page_index / slot->tiles_x;
    const TexPage *page = &slot->pages[page_index];
    const float page_x0 = (float)(tx * slot->tile_w);
    const float page_y0 = (float)(ty * slot->tile_h);

    for (int i = 0; i < vert_count; i++) {
        float *uv = (float*)(base + i * 24 + 12);
        float px = uv[0] * (float)slot->width;
        float py = uv[1] * (float)slot->height;
        uv[0] = (px - page_x0) / (float)page->pot_w;
        uv[1] = (py - page_y0) / (float)page->pot_h;
    }
}

static TexSlot* imm_get_active_tiled_texture(void) {
    if (!g.texture_2d_enabled_unit[0]) return NULL;
    GLuint tex_id = g.bound_texture[0];
    if (tex_id == 0 || tex_id >= NOVA_MAX_TEXTURES) return NULL;
    TexSlot *slot = &g.textures[tex_id];
    if (!slot->allocated || !slot->is_tiled || !slot->pages) return NULL;
    return slot;
}

static void imm_draw_packed_run(GLenum mode, GPU_Primitive_t prim, uint8_t *base, int count) {
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, base, 24, 3, 0x210);
    if (mode == GL_QUADS) draw_emulated_quads(count);
    else C3D_DrawArrays(prim, 0, count);
}

static void imm_draw_tiled_batches(GLenum mode, GPU_Primitive_t prim, TexSlot *slot, uint8_t *base, int count) {
    int prim_verts;
    switch (mode) {
        case GL_TRIANGLES: prim_verts = 3; break;
        case GL_QUADS:     prim_verts = 4; break;
        case GL_LINES:     prim_verts = 2; break;
        default:           prim_verts = 0; break;
    }

    if (prim_verts <= 0) {
        C3D_TexBind(0, &slot->pages[0].tex);
        GSPGPU_FlushDataCache(base, count * 24);
        imm_draw_packed_run(mode, prim, base, count);
        return;
    }

    for (int prim_start = 0; prim_start + prim_verts <= count; ) {
        int page_index = imm_tiled_page_for_primitive(slot, base, prim_start, prim_verts);
        int run_start = prim_start;
        int run_end = prim_start + prim_verts;

        while (run_end + prim_verts <= count &&
               imm_tiled_page_for_primitive(slot, base, run_end, prim_verts) == page_index) {
            run_end += prim_verts;
        }

        uint8_t *run_base = base + run_start * 24;
        int run_count = run_end - run_start;
        imm_remap_tiled_uvs(slot, page_index, run_base, run_count);
        GSPGPU_FlushDataCache(run_base, run_count * 24);
        C3D_TexBind(0, &slot->pages[page_index].tex);
        imm_draw_packed_run(mode, prim, run_base, run_count);

        prim_start = run_end;
    }
}

void glEnd(void) {
    if (!imm.in_begin) return;
    imm.in_begin = 0;

    if (imm.vertex_count == 0) return;

    g.client_array_buf_offset = imm.start_offset + (imm.vertex_count * 24);

    apply_gpu_state();

    GPU_Primitive_t prim = gl_to_gpu_primitive(imm.mode);

    TexSlot *tiled_slot = imm_get_active_tiled_texture();
    if (tiled_slot) {
        imm_draw_tiled_batches(imm.mode, prim, tiled_slot, imm.mapped_ptr, imm.vertex_count);
    } else {
        GSPGPU_FlushDataCache(imm.mapped_ptr, imm.vertex_count * 24);
        imm_draw_packed_run(imm.mode, prim, imm.mapped_ptr, imm.vertex_count);
    }
}

/* Helper function to add a vertex to the mapped ring buffer */
static inline void add_vertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    (void)w;
    if (!imm.in_begin || imm.vertex_count >= imm.mapped_max_verts) return;

    float* out_f = (float*)(imm.mapped_ptr + imm.vertex_count * 24);
    uint8_t* out_c = (uint8_t*)(out_f + 5);

    out_f[0] = x; out_f[1] = y; out_f[2] = z;
    out_f[3] = imm.current_texcoord[0]; // s
    out_f[4] = imm.current_texcoord[1]; // t

    out_c[0] = (uint8_t)(imm.current_color[0] * 255.0f);
    out_c[1] = (uint8_t)(imm.current_color[1] * 255.0f);
    out_c[2] = (uint8_t)(imm.current_color[2] * 255.0f);
    out_c[3] = (uint8_t)(imm.current_color[3] * 255.0f);

    imm.vertex_count++;
}

void glArrayElement(GLint i) {
    if (!imm.in_begin) return;

    if (g.va_color.enabled && g.va_color.pointer) {
        float c[4] = {0,0,0,1};
        read_vertex_attrib_float(c, (const uint8_t*)g.va_color.pointer + i * calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type), g.va_color.size, g.va_color.type);
        imm.current_color[0] = c[0]; imm.current_color[1] = c[1]; imm.current_color[2] = c[2]; imm.current_color[3] = c[3];
    }

    if (g.va_texcoord.enabled && g.va_texcoord.pointer) {
        float t[4] = {0,0,0,1};
        read_vertex_attrib_float(t, (const uint8_t*)g.va_texcoord.pointer + i * calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type), g.va_texcoord.size, g.va_texcoord.type);
        imm.current_texcoord[0] = t[0]; imm.current_texcoord[1] = t[1];
    }

    if (g.va_vertex.enabled && g.va_vertex.pointer) {
        float v[4] = {0,0,0,1};
        read_vertex_attrib_float(v, (const uint8_t*)g.va_vertex.pointer + i * calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type), g.va_vertex.size, g.va_vertex.type);
        add_vertex(v[0], v[1], v[2], v[3]);
    }
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
void glTexCoord1d(GLdouble s) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord1dv(const GLdouble *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord1f(GLfloat s) { imm.current_texcoord[0] = s; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord1fv(const GLfloat *v) { if (v) { imm.current_texcoord[0] = v[0]; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord1i(GLint s) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord1iv(const GLint *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord1s(GLshort s) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord1sv(const GLshort *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = 0.0f; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }

void glTexCoord2d(GLdouble s, GLdouble t) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord2dv(const GLdouble *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord2f(GLfloat s, GLfloat t) { imm.current_texcoord[0] = s; imm.current_texcoord[1] = t; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord2fv(const GLfloat *v) { if (v) { imm.current_texcoord[0] = v[0]; imm.current_texcoord[1] = v[1]; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord2i(GLint s, GLint t) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord2iv(const GLint *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord2s(GLshort s, GLshort t) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; }
void glTexCoord2sv(const GLshort *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = 0.0f; imm.current_texcoord[3] = 1.0f; } }

void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = 1.0f; }
void glTexCoord3dv(const GLdouble *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { imm.current_texcoord[0] = s; imm.current_texcoord[1] = t; imm.current_texcoord[2] = r; imm.current_texcoord[3] = 1.0f; }
void glTexCoord3fv(const GLfloat *v) { if (v) { imm.current_texcoord[0] = v[0]; imm.current_texcoord[1] = v[1]; imm.current_texcoord[2] = v[2]; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord3i(GLint s, GLint t, GLint r) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = 1.0f; }
void glTexCoord3iv(const GLint *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = 1.0f; } }
void glTexCoord3s(GLshort s, GLshort t, GLshort r) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = 1.0f; }
void glTexCoord3sv(const GLshort *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = 1.0f; } }

void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = (GLfloat)q; }
void glTexCoord4dv(const GLdouble *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = (GLfloat)v[3]; } }
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { imm.current_texcoord[0] = s; imm.current_texcoord[1] = t; imm.current_texcoord[2] = r; imm.current_texcoord[3] = q; }
void glTexCoord4fv(const GLfloat *v) { if (v) { imm.current_texcoord[0] = v[0]; imm.current_texcoord[1] = v[1]; imm.current_texcoord[2] = v[2]; imm.current_texcoord[3] = v[3]; } }
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = (GLfloat)q; }
void glTexCoord4iv(const GLint *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = (GLfloat)v[3]; } }
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { imm.current_texcoord[0] = (GLfloat)s; imm.current_texcoord[1] = (GLfloat)t; imm.current_texcoord[2] = (GLfloat)r; imm.current_texcoord[3] = (GLfloat)q; }
void glTexCoord4sv(const GLshort *v) { if (v) { imm.current_texcoord[0] = (GLfloat)v[0]; imm.current_texcoord[1] = (GLfloat)v[1]; imm.current_texcoord[2] = (GLfloat)v[2]; imm.current_texcoord[3] = (GLfloat)v[3]; } }