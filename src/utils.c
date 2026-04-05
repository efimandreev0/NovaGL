//
// Created by Notebook on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

static inline unsigned int next_pow2(unsigned int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static void* linear_alloc_ring(void *base, int *offset, int size, int capacity) {
    size = (size + 0x7F) & ~0x7F;

    if (*offset + size > capacity) {
        *offset = 0; // wrap
    }

    void *ptr = (uint8_t*)base + *offset;
    *offset += size;

    return ptr;
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static void dl_record_translate(float x, float y, float z) {
    if (g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_TRANSLATE; op->args[0] = x; op->args[1] = y; op->args[2] = z;
        }
    }
}

static void dl_record_color3f(float r, float g_, float b) {
    if (g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR3F; op->args[0] = r; op->args[1] = g_; op->args[2] = b;
        }
    }
}

static void dl_execute(GLuint list) {
    if (list >= NOVA_DISPLAY_LISTS) return;
    DisplayList *dl = &g.dl_store[list];
    if (!dl->used) return;
    for (int i = 0; i < dl->count; i++) {
        DLOp *op = &dl->ops[i];
        if (op->type == DL_OP_TRANSLATE) glTranslatef(op->args[0], op->args[1], op->args[2]);
        else if (op->type == DL_OP_COLOR3F) glColor3f(op->args[0], op->args[1], op->args[2]);
    }
}

static GPU_TESTFUNC gl_to_gpu_alpha_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_LESS;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_LEQUAL;
        case GL_GREATER:  return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_GEQUAL;
        case GL_ALWAYS:   return GPU_ALWAYS;
        default:          return GPU_ALWAYS;
    }
}
static GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_GREATER;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_GEQUAL;
        case GL_GREATER:  return GPU_LESS;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_LEQUAL;
        case GL_ALWAYS:   return GPU_ALWAYS;
        default:          return GPU_ALWAYS;
    }
}
static GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_LESS;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_LEQUAL;
        case GL_GREATER:  return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_GEQUAL;
        case GL_ALWAYS:   return GPU_ALWAYS;
        default:          return GPU_ALWAYS;
    }
}

static GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor) {
    switch (factor) {
        case GL_ZERO:                return GPU_ZERO;
        case GL_ONE:                 return GPU_ONE;
        case GL_SRC_COLOR:           return GPU_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return GPU_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR:           return GPU_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return GPU_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA:           return GPU_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return GPU_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA:           return GPU_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return GPU_ONE_MINUS_DST_ALPHA;
        case GL_SRC_ALPHA_SATURATE:  return GPU_SRC_ALPHA_SATURATE;
        default:                     return GPU_ONE;
    }
}

static int gl_type_size(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:  return 1;

        case GL_SHORT:
        case GL_UNSIGNED_SHORT: return 2;

        case GL_FLOAT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FIXED:          return 4;

        default:                return 4;
    }
}

static int calc_stride(GLsizei stride, GLint size, GLenum type) {
    return stride ? stride : size * gl_type_size(type);
}

static void read_vertex_attrib_float(float *dst, const uint8_t *src, GLint size, GLenum type) {
    switch (type) {
        case GL_FLOAT:
            memcpy(dst, src, size * sizeof(float));
            break;
        case GL_FIXED:
            for (int j = 0; j < size; j++) {
                // GL_FIXED (16.16) конвертируем в float
                dst[j] = (float)((const int32_t*)src)[j] / 65536.0f;
            }
            break;
        case GL_SHORT:
            for (int j = 0; j < size; j++) dst[j] = (float)((const int16_t*)src)[j];
            break;
        case GL_BYTE:
            for (int j = 0; j < size; j++) dst[j] = (float)((const int8_t*)src)[j];
            break;
        default:
            memcpy(dst, src, size * sizeof(float));
            break;
    }
}

static GPU_Primitive_t gl_to_gpu_primitive(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES:      return GPU_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GPU_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN:   return GPU_TRIANGLE_FAN;
        default:                return GPU_TRIANGLES;
    }
}

static GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type) {
    if (format == GL_RGBA || format == GL_RGBA8_OES) {
        if (type == GL_UNSIGNED_BYTE)           return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_4_4_4_4)  return GPU_RGBA4;
        if (type == GL_UNSIGNED_SHORT_5_5_5_1)  return GPU_RGBA5551;
    }
    if (format == GL_RGB) {
        if (type == GL_UNSIGNED_BYTE)           return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_5_6_5)    return GPU_RGB565;
    }
    if (format == GL_LUMINANCE)                 return GPU_L8;
    if (format == GL_LUMINANCE_ALPHA)           return GPU_LA8;
    if (format == GL_ALPHA)                     return GPU_A8;
    return GPU_RGBA8;
}

static int gpu_texfmt_bpp(GPU_TEXCOLOR fmt) {
    switch (fmt) {
        case GPU_RGBA8:    return 4;
        case GPU_RGB8:     return 3;
        case GPU_RGBA5551: return 2;
        case GPU_RGB565:   return 2;
        case GPU_RGBA4:    return 2;
        case GPU_LA8:      return 2;
        case GPU_L8:       return 1;
        case GPU_A8:       return 1;
        case GPU_LA4:      return 1;
        default:           return 4;
    }
}

static C3D_Mtx* cur_mtx(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_stack[g.proj_sp];
        case GL_TEXTURE:    return &g.tex_stack[g.tex_sp];
        default:            return &g.mv_stack[g.mv_sp];
    }
}

static int* cur_sp(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_sp;
        case GL_TEXTURE:    return &g.tex_sp;
        default:            return &g.mv_sp;
    }
}

static C3D_Mtx* cur_stack(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return g.proj_stack;
        case GL_TEXTURE:    return g.tex_stack;
        default:            return g.mv_stack;
    }
}
static void* get_tex_staging(int size)
{
    if (g.tex_staging_size < size) {
        linearFree(g.tex_staging);
        g.tex_staging = linearAlloc(size);
        g.tex_staging_size = size;
    }
    return g.tex_staging;
}
static inline uint32_t morton_interleave(uint32_t x, uint32_t y) {
    static const uint32_t xlut[8] = {0x00,0x01,0x04,0x05,0x10,0x11,0x14,0x15};
    static const uint32_t ylut[8] = {0x00,0x02,0x08,0x0a,0x20,0x22,0x28,0x2a};
    return xlut[x & 7] | ylut[y & 7];
}
static void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            int flipped_y = pot_h - 1 - y;
            int tile_x = x >> 3;
            int tile_y = flipped_y >> 3;
            int lx = x & 7;
            int ly = flipped_y & 7;
            int tiles_per_row = pot_w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int pixel_offset = tile_offset + morton_interleave(lx, ly);
            dst[pixel_offset] = src[y * src_w + x];
        }
    }
    // Заполняем пустоты (padding) нулями (прозрачным цветом)
    for (int y = src_h; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            int flipped_y = pot_h - 1 - y;
            int tile_x = x >> 3;
            int tile_y = flipped_y >> 3;
            int lx = x & 7;
            int ly = flipped_y & 7;
            int tiles_per_row = pot_w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int pixel_offset = tile_offset + morton_interleave(lx, ly);
            dst[pixel_offset] = 0;
        }
    }
}
static void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            uint32_t pixel = src[y * src_w + x];
            uint8_t r = (pixel >>  0) & 0xFF;
            uint8_t g_c = (pixel >>  8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t a = (pixel >> 24) & 0xFF;
            uint32_t out_pixel = ((uint32_t)r << 24) | ((uint32_t)g_c << 16) | ((uint32_t)b << 8) | (uint32_t)a;

            int flipped_y = pot_h - 1 - y;
            int tile_x = x >> 3;
            int tile_y = flipped_y >> 3;
            int lx = x & 7;
            int ly = flipped_y & 7;
            int tiles_per_row = pot_w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int pixel_offset = tile_offset + morton_interleave(lx, ly);
            dst[pixel_offset] = out_pixel;
        }
    }
    for (int y = src_h; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            int flipped_y = pot_h - 1 - y;
            int tile_x = x >> 3;
            int tile_y = flipped_y >> 3;
            int lx = x & 7;
            int ly = flipped_y & 7;
            int tiles_per_row = pot_w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int pixel_offset = tile_offset + morton_interleave(lx, ly);
            dst[pixel_offset] = 0;
        }
    }
}

static uint32_t* rgb_to_rgba(const uint8_t *rgb, int w, int h) {
    uint32_t *out = (uint32_t*)malloc(w * h * 4);
    if (!out) return NULL;
    for (int i = 0; i < w * h; i++) {
        out[i] = (0xFF << 24) | (rgb[i*3+2] << 16) | (rgb[i*3+1] << 8) | rgb[i*3+0];
    }
    return out;
}

static void apply_depth_map(void) {
    float scale = g.depth_far - g.depth_near;
    float offset = g.depth_far;

    if (g.polygon_offset_fill_enabled) {
        offset += (g.polygon_offset_units * 0.0001f);
    }

    C3D_DepthMap(true, scale, offset);
}