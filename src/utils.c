//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

/* Value fixed by the GL spec; defined locally if NovaGL.h lacks it. */
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif


#include <stdio.h>
#include <stdlib.h>

/* ── File-scope state cache used by apply_gpu_state.
 *
 * These mirror the "last-applied" GPU state so we can skip redundant
 * C3D_DepthTest / AlphaBlend / TexBind / Scissor / ... register writes when
 * nothing changed between draws (the typical chunk-by-chunk render loop hits
 * the same depth/alpha/cull settings hundreds of times per frame).
 *
 * Reset via nova_invalidate_state_cache() — required on world reload because
 * texture/VBO ids get recycled, and a cached "tex unit 0 already bound to id
 * 7" would skip the necessary re-bind to a deleted slot. */
static GPU_WRITEMASK s_last_writemask          = (GPU_WRITEMASK)-1;
static int           s_last_depth_test_enabled = -1;
static GLenum        s_last_depth_func         = 0;
static int           s_last_alpha_test_enabled = -1;
static GLenum        s_last_alpha_func         = 0;
static u8            s_last_alpha_ref8         = 0xFF;
static int           s_last_blend_enabled      = -1;
static GLenum        s_last_blend_src          = 0;
static GLenum        s_last_blend_dst          = 0;
static GLenum        s_last_blend_src_alpha    = 0;
static GLenum        s_last_blend_dst_alpha    = 0;
static GLenum        s_last_blend_eq_rgb       = 0;
static GLenum        s_last_blend_eq_alpha     = 0;
static int           s_last_logic_op_enabled   = -1;
static GLenum        s_last_logic_op           = 0;
static u32           s_last_blend_color        = 0xDEADBEEFu;
static int           s_last_cull_enabled       = -1;
static GLenum        s_last_cull_mode          = 0;
static GLenum        s_last_front_face         = 0;
static int           s_last_scissor_enabled    = -1;
static int           s_last_scissor_x          = -1;
static int           s_last_scissor_y          = -1;
static int           s_last_scissor_w          = -1;
static int           s_last_scissor_h          = -1;
static int           s_last_scissor_fbo        = -1;
static C3D_RenderTarget *s_last_scissor_target = NULL;
static GLuint        s_last_tex_bound[3]       = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

/* attr/buf cache used by nova_setup_attr_info / nova_setup_buf_info */
static int   s_attr_pos_elements = -1;
static void *s_buf_base          = (void *) -1;
static int   s_buf_stride        = -1;

unsigned int nova_next_pow2(unsigned int v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

void *linear_alloc_ring(void *base, int *offset, int size, int capacity) {
    /* 128-byte alignment is the historical value; the optimized 64 (still
     * safe for cache-line 32 + GX-DMA 16) is opt-in. Some PICA200 dispatch
     * paths assume 128 — keep the conservative default unless explicitly
     * told otherwise. */
#ifdef NOVAGL_RING_ALIGN_64
    size = (size + 0x3F) & ~0x3F;
#else
    size = (size + 0x7F) & ~0x7F;
#endif

    if (*offset + size > capacity) {
        /* Ring is full. Splitting the command buffer + waiting for GPU is the
         * only way to safely wrap, because in-flight draws may still be
         * reading from the bytes we're about to overwrite. This is the
         * documented soft cap on how big client_array_buf / index_buf should
         * be relative to per-frame draw volume — if a caller hits this every
         * frame, grow the buffer via nova_init_ex.
         *
         * Future: split each ring into A/B halves with a GPU fence per half,
         * so a half can be re-used while the GPU still chews on the other.
         * Currently a single ring with one stall. */
        C3D_FrameSplit(0);
        gspWaitForP3D();
        *offset = 0; // wrap
    }

    void *ptr = (uint8_t *) base + *offset;
    *offset += size;

    return ptr;
}

float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static int clampi(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static void apply_scissor_box(void) {
    C3D_FrameBuf *fb = g.current_target ? &g.current_target->frameBuf : NULL;
    const int native_w = fb ? (int)fb->width : NOVA_SCREEN_H;
    const int native_h = fb ? (int)fb->height : NOVA_SCREEN_W;

    int x0 = g.scissor_x;
    int y0 = g.scissor_y;
    int x1 = g.scissor_x + g.scissor_w;
    int y1 = g.scissor_y + g.scissor_h;

    if (g.bound_fbo == 0) {
        /* Screen is rendered sideways (per-draw tilt); scissor rotated to
         * match. Logical screen is 400x240 (NOT fb-derived: the app surface fb
         * is POT-padded). */
        const int logical_w = NOVA_SCREEN_W; /* 400 */
        const int logical_h = NOVA_SCREEN_H; /* 240 */
        x0 = clampi(x0, 0, logical_w);
        x1 = clampi(x1, 0, logical_w);
        y0 = clampi(y0, 0, logical_h);
        y1 = clampi(y1, 0, logical_h);

        C3D_SetScissor(GPU_SCISSOR_NORMAL,
                       (u32)y0,
                       (u32)(logical_w - x1),
                       (u32)y1,
                       (u32)(logical_w - x0));
    } else {
        const int logical_w = native_w;
        const int logical_h = native_h;
        x0 = clampi(x0, 0, logical_w);
        x1 = clampi(x1, 0, logical_w);
        y0 = clampi(y0, 0, logical_h);
        y1 = clampi(y1, 0, logical_h);

        C3D_SetScissor(GPU_SCISSOR_NORMAL,
                       (u32)x0,
                       (u32)(logical_h - y1),
                       (u32)x1,
                       (u32)(logical_h - y0));
    }
}

void dl_record_translate(float x, float y, float z) {
    if (g.dl_store && g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_TRANSLATE;
            op->args[0] = x;
            op->args[1] = y;
            op->args[2] = z;
        }
    }
}

void dl_record_color3f(float r, float g_, float b) {
    if (g.dl_store && g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR3F;
            op->args[0] = r;
            op->args[1] = g_;
            op->args[2] = b;
        }
    }
}

void dl_execute(GLuint list) {
    if (!g.dl_store || list >= NOVA_DISPLAY_LISTS) return;
    DisplayList *dl = &g.dl_store[list];
    if (!dl->used) return;
    for (int i = 0; i < dl->count; i++) {
        DLOp *op = &dl->ops[i];
        if (op->type == DL_OP_TRANSLATE) glTranslatef(op->args[0], op->args[1], op->args[2]);
        else if (op->type == DL_OP_COLOR3F) glColor3f(op->args[0], op->args[1], op->args[2]);
    }
}

GPU_TESTFUNC gl_to_gpu_alpha_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_LESS;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_LEQUAL;
        case GL_GREATER: return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_GEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_GREATER;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_GEQUAL;
        case GL_GREATER: return GPU_LESS;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_LEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_LESS;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_LEQUAL;
        case GL_GREATER: return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_GEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor) {
    switch (factor) {
        case GL_ZERO: return GPU_ZERO;
        case GL_ONE: return GPU_ONE;
        case GL_SRC_COLOR: return GPU_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return GPU_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR: return GPU_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return GPU_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA: return GPU_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return GPU_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA: return GPU_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return GPU_ONE_MINUS_DST_ALPHA;
        case GL_SRC_ALPHA_SATURATE: return GPU_SRC_ALPHA_SATURATE;
        case GL_CONSTANT_COLOR: return GPU_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR: return GPU_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA: return GPU_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return GPU_ONE_MINUS_CONSTANT_ALPHA;
        default: return GPU_ONE;
    }
}

/* GL colour logic opcode -> PICA GPU_LOGICOP. Both enumerate the same 16 ops;
 * GL's values are 0x1500-0x150F in canonical order, so a table keyed off the
 * low nibble would also work, but the explicit switch is clearer and matches
 * the rest of the lookup file. */
GPU_LOGICOP gl_to_gpu_logicop(GLenum op) {
    switch (op) {
        case GL_CLEAR:         return GPU_LOGICOP_CLEAR;
        case GL_AND:           return GPU_LOGICOP_AND;
        case GL_AND_REVERSE:   return GPU_LOGICOP_AND_REVERSE;
        case GL_COPY:          return GPU_LOGICOP_COPY;
        case GL_AND_INVERTED:  return GPU_LOGICOP_AND_INVERTED;
        case GL_NOOP:          return GPU_LOGICOP_NOOP;
        case GL_XOR:           return GPU_LOGICOP_XOR;
        case GL_OR:            return GPU_LOGICOP_OR;
        case GL_NOR:           return GPU_LOGICOP_NOR;
        case GL_EQUIV:         return GPU_LOGICOP_EQUIV;
        case GL_INVERT:        return GPU_LOGICOP_INVERT;
        case GL_OR_REVERSE:    return GPU_LOGICOP_OR_REVERSE;
        case GL_COPY_INVERTED: return GPU_LOGICOP_COPY_INVERTED;
        case GL_OR_INVERTED:   return GPU_LOGICOP_OR_INVERTED;
        case GL_NAND:          return GPU_LOGICOP_NAND;
        case GL_SET:           return GPU_LOGICOP_SET;
        default:               return GPU_LOGICOP_COPY;
    }
}

GPU_BLENDEQUATION gl_to_gpu_blendeq(GLenum mode) {
    switch (mode) {
        case GL_FUNC_ADD: return GPU_BLEND_ADD;
        case GL_FUNC_SUBTRACT: return GPU_BLEND_SUBTRACT;
        case GL_FUNC_REVERSE_SUBTRACT: return GPU_BLEND_REVERSE_SUBTRACT;
        case GL_MIN: return GPU_BLEND_MIN;
        case GL_MAX: return GPU_BLEND_MAX;
        default: return GPU_BLEND_ADD;
    }
}

int gl_type_size(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE: return 1;

        case GL_SHORT:
        case GL_UNSIGNED_SHORT: return 2;

        case GL_FLOAT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FIXED:
        default: return 4;
    }
}

int calc_stride(GLsizei stride, GLint size, GLenum type) {
    return stride ? stride : size * gl_type_size(type);
}

void read_vertex_attrib_float(float *dst, const uint8_t *src, GLint size, GLenum type) {
    switch (type) {
        case GL_FLOAT:
            memcpy(dst, src, size * sizeof(float));
            break;
        case GL_FIXED:
            for (int j = 0; j < size; j++) {
                // GL_FIXED is 16.16 fixed point
                dst[j] = (float) ((const int32_t *) src)[j] / 65536.0f;
            }
            break;
        case GL_SHORT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int16_t *) src)[j];
            break;
        case GL_UNSIGNED_SHORT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const uint16_t *) src)[j];
            break;
        case GL_BYTE:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int8_t *) src)[j];
            break;
        case GL_UNSIGNED_BYTE:
            for (int j = 0; j < size; j++) dst[j] = (float) src[j];
            break;
        case GL_INT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int32_t *) src)[j];
            break;
        case GL_UNSIGNED_INT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const uint32_t *) src)[j];
            break;
        default:
            /* Unknown type: previous behaviour (raw float copy) kept as the
             * least-surprising fallback. */
            memcpy(dst, src, size * sizeof(float));
            break;
    }
}

GPU_Primitive_t gl_to_gpu_primitive(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES: return GPU_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GPU_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN: return GPU_TRIANGLE_FAN;
        default: return GPU_TRIANGLES;
    }
}

GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type) {
    if (format == GL_RGBA || format == GL_RGBA8_OES) {
        if (type == GL_UNSIGNED_BYTE) return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_4_4_4_4) return GPU_RGBA4;
        if (type == GL_UNSIGNED_SHORT_5_5_5_1) return GPU_RGBA5551;
    }
    if (format == GL_RGB) {
        if (type == GL_UNSIGNED_BYTE) return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_5_6_5) return GPU_RGB565;
    }
    if (format == GL_LUMINANCE) return GPU_L8;
    if (format == GL_LUMINANCE_ALPHA) return GPU_LA8;
    if (format == GL_LUMINANCE_ALPHA4_NOVA) return GPU_LA4;
    if (format == GL_ALPHA) return GPU_A8;
    return GPU_RGBA8;
}

int gpu_texfmt_bpp(GPU_TEXCOLOR fmt) {
    switch (fmt) {
        case GPU_RGBA8: return 4;
        case GPU_RGB8: return 3;
        case GPU_RGBA5551:
        case GPU_RGB565:
        case GPU_RGBA4:
        case GPU_LA8: return 2;
        case GPU_L8:
        case GPU_A8:
        case GPU_LA4: return 1;
        default: return 4;
    }
}

C3D_Mtx *cur_mtx(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_stack[g.proj_sp];
        case GL_TEXTURE: return &g.tex_stack[g.tex_sp];
        default: return &g.mv_stack[g.mv_sp];
    }
}

int *cur_sp(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_sp;
        case GL_TEXTURE: return &g.tex_sp;
        default: return &g.mv_sp;
    }
}

C3D_Mtx *cur_stack(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return g.proj_stack;
        case GL_TEXTURE: return g.tex_stack;
        default: return g.mv_stack;
    }
}

void *get_tex_staging(int size) {
    if (g.tex_staging_size < size) {
        void *new_buf = realloc(g.tex_staging, (size_t) size);
        if (!new_buf) {
            return NULL;
        }
        g.tex_staging = new_buf;
        g.tex_staging_size = size;
    }
    return g.tex_staging;
}

uint32_t morton_interleave(uint32_t x, uint32_t y) {
    static const uint32_t xlut[8] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    static const uint32_t ylut[8] = {0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a};
    return xlut[x & 7] | ylut[y & 7];
}

/* Per-pixel morton offset inside an 8x8 tile, precomputed at file scope.
 * Replaces 4 mul/shift ops with a single LUT read in the swizzle inner loop. */
static const uint8_t k_tile_morton[64] = {
     0,  1,  4,  5, 16, 17, 20, 21,
     2,  3,  6,  7, 18, 19, 22, 23,
     8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53,
    34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61,
    42, 43, 46, 47, 58, 59, 62, 63,
};

static inline int morton_offset(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int) morton_interleave(x & 7, fy & 7);
}

/* Tile-walk swizzlers: outer loops over 8x8 tiles in dest-space, inner loop
 * over 64 pixels in tile-space. ~2x faster than the naive per-pixel
 * morton_offset() call because the per-tile base pointer is hoisted out and
 * the inner loop becomes a flat 64-iteration LUT lookup. Used by
 * upload_page_*; the legacy swizzle_* names below delegate. */
void swizzle_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint8_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint8_t val = (sx < src_w && sy < src_h) ? src[sy * src_w + sx] : 0;
                    tile[k_tile_morton[(fy << 3) | px]] = val;
                }
            }
        }
    }
}

void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint16_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint16_t val = (sx < src_w && sy < src_h) ? src[sy * src_w + sx] : 0;
                    tile[k_tile_morton[(fy << 3) | px]] = val;
                }
            }
        }
    }
}

void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint32_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint32_t out_pixel = 0;
                    if (sx < src_w && sy < src_h) {
                        uint32_t pixel = src[sy * src_w + sx];
                        uint8_t r   = (pixel >>  0) & 0xFF;
                        uint8_t g_c = (pixel >>  8) & 0xFF;
                        uint8_t b   = (pixel >> 16) & 0xFF;
                        uint8_t a   = (pixel >> 24) & 0xFF;
                        out_pixel = ((uint32_t) r << 24) | ((uint32_t) g_c << 16) |
                                    ((uint32_t) b << 8)  |  (uint32_t) a;
                    }
                    tile[k_tile_morton[(fy << 3) | px]] = out_pixel;
                }
            }
        }
    }
}

uint32_t *rgb_to_rgba(const uint8_t *rgb, int w, int h) {
    uint32_t *out = (uint32_t *) malloc(w * h * 4);
    if (!out) return NULL;
    for (int i = 0; i < w * h; i++) {
        //this color channels in C3D is really fucking fuck. PICA200 is fucking puzzle.
        out[i] = (rgb[i * 3 + 0] << 24) | (rgb[i * 3 + 1] << 16) | (rgb[i * 3 + 2] << 8) | 0xFF;
    }
    return out;
}

void downscale_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter: average every source pixel covered by each destination cell.
    // Uses fixed-point spans so no source pixel is skipped even for large scale factors
    // (e.g. 4096x4096 -> 1024x1024, the typical GameMaker atlas case).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t r = 0, g = 0, b = 0, a = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint32_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint32_t p = row[xx];
                    r += (p >> 0) & 0xFF;
                    g += (p >> 8) & 0xFF;
                    b += (p >> 16) & 0xFF;
                    a += (p >> 24) & 0xFF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint8_t rr = (uint8_t) (r / count);
            uint8_t gg = (uint8_t) (g / count);
            uint8_t bb = (uint8_t) (b / count);
            uint8_t aa = (uint8_t) (a / count);
            dst[y * dst_w + x] = ((uint32_t) aa << 24) | ((uint32_t) bb << 16) | ((uint32_t) gg << 8) | rr;
        }
    }
}

void downscale_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter for RGBA4/RGB565-ish 16-bit. Unpack as 4x4-bit channels, average, repack.
    // This assumes the 16-bit format is a 4-channel 4-bit layout (works for RGBA4);
    // for RGB565 the per-channel averaging still produces a visually reasonable result
    // since we average within the same bit-layout (the bias from channel widths is negligible at dst scale).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint16_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint16_t p = row[xx];
                    c0 += (p >> 0) & 0xF;
                    c1 += (p >> 4) & 0xF;
                    c2 += (p >> 8) & 0xF;
                    c3 += (p >> 12) & 0xF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint16_t v = (uint16_t) (
                ((c0 / count) & 0xF) |
                (((c1 / count) & 0xF) << 4) |
                (((c2 / count) & 0xF) << 8) |
                (((c3 / count) & 0xF) << 12)
            );
            dst[y * dst_w + x] = v;
        }
    }
}

void downscale_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter for single-channel 8-bit (alpha/luminance).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t sum = 0, count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint8_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    sum += row[xx];
                    count++;
                }
            }
            if (count == 0) count = 1;
            dst[y * dst_w + x] = (uint8_t) (sum / count);
        }
    }
}

void apply_depth_map(void) {
    /* PICA200 stores the depth buffer with the "near = high, far = low"
     * convention (citro3d's base.c initializes with C3D_DepthMap(-1, 0)).
     * That's why gl_to_gpu_depth_testfunc inverts every comparison —
     * GL_LESS ("pass if nearer = smaller GL depth") becomes GPU_GREATER
     * ("pass if larger PICA buffer value").
     *
     * For glDepthRange(gl_near, gl_far) with NovaGL's projection fixup
     * producing z_ndc ∈ [-1, 0] (PICA near = -1, PICA far = 0), the
     * mapping to the inverted-buffer convention is:
     *   At z_ndc = -1 (GL near):  PICA depth = 1 - gl_near
     *   At z_ndc =  0 (GL far):   PICA depth = 1 - gl_far
     * Solving `depth = scale * z_ndc + offset`:
     *   scale  = -(gl_far - gl_near)
     *   offset =  1 - gl_far
     * Defaults (0, 1) → (-1, 0) — identical to citro3d's stock setup.
     *
     * The previous (+far-near, +far) passed scale=+1, offset=+1, giving
     * `depth = z_ndc + 1` (GL convention) while the comparator stayed
     * inverted. Result: near-camera geometry was treated as if at the
     * far plane → hand renders behind blocks in MC/PD, side-textures
     * disappear, block interiors don't render. */
    float scale  = -(g.depth_far - g.depth_near);
    float offset = 1.0f - g.depth_far;

    /* Polygon-offset: positive `units` in GL means "push polygons toward
     * the FAR plane". In PICA's inverted-depth buffer, far = small value,
     * so we SUBTRACT (was += before, pushing the wrong direction). */
    if (g.polygon_offset_fill_enabled) {
        offset -= (g.polygon_offset_units * 0.0001f);
    }

    C3D_DepthMap(true, scale, offset);
}

static GPU_TEVSRC get_tev_src(GLint gl_src, GPU_TEVSRC tex_src, GPU_TEVSRC prev_src) {
    if (gl_src == GL_TEXTURE) return tex_src;
    if (gl_src == GL_PREVIOUS) return prev_src;
    if (gl_src == GL_PRIMARY_COLOR) return GPU_PRIMARY_COLOR;
    if (gl_src == GL_CONSTANT) return GPU_CONSTANT;
    return GPU_PRIMARY_COLOR;
}

static int get_tev_op_rgb(GLint gl_op) {
    /* NB: the operand VALUES are GL_SRC_COLOR..GL_ONE_MINUS_SRC_ALPHA
     * (0x0300..0x0303, NovaGL.h). The old code compared against 0x8590/
     * 0x8598/0x859A — those are the OPERANDn_RGB *pname* constants, not
     * values — so ONE_MINUS_* silently decoded as plain SRC_COLOR. */
    if (gl_op == GL_SRC_COLOR) return GPU_TEVOP_RGB_SRC_COLOR;
    if (gl_op == GL_ONE_MINUS_SRC_COLOR) return GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
    if (gl_op == GL_SRC_ALPHA) return GPU_TEVOP_RGB_SRC_ALPHA;
    if (gl_op == GL_ONE_MINUS_SRC_ALPHA) return GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA;
    return GPU_TEVOP_RGB_SRC_COLOR;
}

/* Alpha-channel TEV operand. PICA's alpha unit is one-component only, so its
 * operand enum is smaller than the RGB one (SRC_ALPHA / ONE_MINUS_SRC_ALPHA /
 * SRC_R / SRC_G / SRC_B / their complements). For our use case we only need
 * the (1-)alpha forms — the R/G/B-broadcast variants are unused. */
static int get_tev_op_alpha(GLint gl_op) {
    if (gl_op == GL_ONE_MINUS_SRC_ALPHA) return GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA;
    return GPU_TEVOP_A_SRC_ALPHA;
}

/* Pack a 0..1 RGBA into PICA's 0xAABBGGRR layout (high byte = alpha) consumed
 * by C3D_TexEnvColor. */
static u32 pack_tev_color(const float c[4]) {
    u32 r = (u32) (clampf(c[0], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 g_ = (u32) (clampf(c[1], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 b = (u32) (clampf(c[2], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 a = (u32) (clampf(c[3], 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g_ << 8) | r;
}

/* GL_RGB_SCALE / GL_ALPHA_SCALE value (1, 2 or 4) -> PICA GPU_TEVSCALE. Anything
 * else falls back to 1x (GL only permits those three). */
static inline GPU_TEVSCALE gl_to_gpu_tevscale(GLint scale) {
    switch (scale) {
        case 2:  return GPU_TEVSCALE_2;
        case 4:  return GPU_TEVSCALE_4;
        default: return GPU_TEVSCALE_1;
    }
}

/* Translate a GL TEV source enum to PICA. Unlike get_tev_src() below (which
 * is for the per-unit GL_COMBINE path and resolves GL_TEXTURE to "this
 * unit's tex"), the explicit-stage path accepts named texture-unit sources
 * directly so a single stage can sample TEXTURE1 even when emitted in
 * "slot 0". Used by the novaSetExplicitTevStages handler in apply_gpu_state. */
static GPU_TEVSRC get_tev_src_explicit(GLenum gl_src) {
    switch (gl_src) {
        case GL_TEXTURE0_ARB:    return GPU_TEXTURE0;
        case GL_TEXTURE1_ARB:    return GPU_TEXTURE1;
        case GL_TEXTURE2_ARB:    return GPU_TEXTURE2;
        case GL_TEXTURE:         return GPU_TEXTURE0; // implicit unit-0 alias
        case GL_PREVIOUS:        return GPU_PREVIOUS;
        case GL_PRIMARY_COLOR:   return GPU_PRIMARY_COLOR;
        case GL_CONSTANT:        return GPU_CONSTANT;
        default:                 return GPU_PRIMARY_COLOR;
    }
}

static GPU_COMBINEFUNC gl_to_gpu_combinefunc(GLenum gl_func) {
    switch (gl_func) {
        case GL_REPLACE:     return GPU_REPLACE;
        case GL_MODULATE:    return GPU_MODULATE;
        case GL_ADD:         return GPU_ADD;
        case GL_INTERPOLATE: return GPU_INTERPOLATE;
        case GL_SUBTRACT:    return GPU_SUBTRACT;
        case GL_DOT3_RGBA_ARB: return GPU_DOT3_RGBA;
        case GL_MULT_ADD_NOVA: return GPU_MULTIPLY_ADD; /* (s0*s1)+s2 */
        default:             return GPU_REPLACE;
    }
}

void novaSetExplicitTevStages(int count, const NovaTevStageGL *stages) {
    if (count < 0) count = 0;
    if (count > NOVA_TEV_MAX_STAGES) count = NOVA_TEV_MAX_STAGES;
    /* The fast3d backend re-stages the TEV programme on EVERY draw (the
     * per-draw constants force it to). Most consecutive draws share the same
     * programme, so skip the dirty-flag when nothing changed — otherwise we'd
     * rewrite all 6 TEV stage registers per draw call. memcmp is safe: the
     * struct is all 4-byte members (no padding) and callers build it with
     * memset+assignments. */
    if (count == g.explicit_tev_count &&
        (count == 0 ||
         (stages && memcmp(g.explicit_tev_stages, stages,
                           (size_t) count * sizeof(NovaTevStageGL)) == 0))) {
        return;
    }
    g.explicit_tev_count = count;
    if (count > 0 && stages) {
        memcpy(g.explicit_tev_stages, stages, count * sizeof(NovaTevStageGL));
    }
    g.tev_dirty = 1;
}

/* Rebuild the PICA fragment-lighting environment from GL state and bind it.
 * Called from apply_gpu_state on lit draws. C3D stores colours in B,G,R order
 * (see C3D_LightAmbient: ambient[0]=b): the C3D_Light* setters take (r,g,b) and
 * swap internally, but C3D_Material is filled directly so we pre-swap to BGR. */
void nova_apply_light_env(void) {
    if (g.light_dirty || !g.light_env_built) {
        C3D_LightEnvInit(&g.light_env);

        C3D_Material mtl;
        /* BGR order to match C3D's internal convention. */
        mtl.ambient[0]   = g.mat_ambient[2];  mtl.ambient[1]   = g.mat_ambient[1];  mtl.ambient[2]   = g.mat_ambient[0];
        mtl.diffuse[0]   = g.mat_diffuse[2];  mtl.diffuse[1]   = g.mat_diffuse[1];  mtl.diffuse[2]   = g.mat_diffuse[0];
        mtl.specular0[0] = g.mat_specular[2]; mtl.specular0[1] = g.mat_specular[1]; mtl.specular0[2] = g.mat_specular[0];
        mtl.specular1[0] = 0.0f;              mtl.specular1[1] = 0.0f;              mtl.specular1[2] = 0.0f;
        mtl.emission[0]  = g.mat_emission[2]; mtl.emission[1]  = g.mat_emission[1]; mtl.emission[2]  = g.mat_emission[0];
        C3D_LightEnvMaterial(&g.light_env, &mtl);

        /* GL_LIGHT_MODEL_AMBIENT — global ambient term. */
        C3D_LightEnvAmbient(&g.light_env, g.light_model_ambient[0],
                            g.light_model_ambient[1], g.light_model_ambient[2]);

        /* Specular distribution LUT (Phong). shininess 0 would flatten the LUT
         * to a step; clamp to >=1 so unset-shininess materials still light. */
        float shininess = g.mat_shininess > 1.0f ? g.mat_shininess : 1.0f;
        LightLut_Phong(&g.light_lut_phong, shininess);
        C3D_LightEnvLut(&g.light_env, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &g.light_lut_phong);

        int n = 0;
        for (int i = 0; i < NOVA_MAX_LIGHTS && n < NOVA_MAX_LIGHTS; i++) {
            if (!g.lights[i].enabled) continue;
            NovaLight *L = &g.lights[i];
            C3D_Light *cl = &g.c3d_lights[n];
            C3D_LightInit(cl, &g.light_env);
            /* Diffuse and specular are separate in GL (GL_DIFFUSE / GL_SPECULAR);
             * the old code used C3D_LightColor which forces specular = diffuse.
             * Set them independently so a coloured/!=diffuse GL_SPECULAR is
             * honoured. specular1 (PICA's 2nd specular layer) has no GL analogue,
             * so pin it to 0 to avoid a phantom second highlight. (All three
             * setters take r,g,b and swap to BGR internally.) */
            C3D_LightDiffuse(cl, L->diffuse[0], L->diffuse[1], L->diffuse[2]);
            C3D_LightSpecular0(cl, L->specular[0], L->specular[1], L->specular[2]);
            C3D_LightSpecular1(cl, 0.0f, 0.0f, 0.0f);
            C3D_LightAmbient(cl, L->ambient[0], L->ambient[1], L->ambient[2]);
            /* GL_POSITION: w==0 -> directional, else positional. C3D_LightPosition
             * reads w to pick the mode. NOTE: GL transforms POSITION by the
             * modelview at glLight time into eye space; NovaGL passes it raw, so
             * the caller's space is whatever it set (the pddi re-sets lights each
             * frame). This is the main thing to tune if lights look off. */
            C3D_FVec pos = FVec4_New(L->position[0], L->position[1],
                                     L->position[2], L->position[3]);
            C3D_LightPosition(cl, &pos);

            /* Spotlight + distance attenuation only affect POSITIONAL lights
             * (GL ignores both for directional w==0 sources). */
            int positional = (L->position[3] != 0.0f);

            /* GL_SPOT_CUTOFF == 180 means "not a spotlight". A real cone is
             * 0..90 deg. C3D_LightSpotLut builds a LUT keyed on the angle to the
             * spot axis; we use citro3d's hard-edged spot_step helper, so
             * GL_SPOT_EXPONENT (soft falloff) is approximated as a hard cone.
             * C3D_LightSpotDir negates+normalises internally, so pass GL's
             * outward-pointing GL_SPOT_DIRECTION as-is. */
            if (positional && L->spot_cutoff >= 0.0f && L->spot_cutoff < 180.0f) {
                float cutoff_rad = L->spot_cutoff * 0.017453292519943295f; /* deg->rad */
                LightLut_Spotlight(&g.light_lut_spot[n], cutoff_rad);
                C3D_LightSpotLut(cl, &g.light_lut_spot[n]);
                C3D_LightSpotDir(cl, L->spot_direction[0], L->spot_direction[1], L->spot_direction[2]);
                C3D_LightSpotEnable(cl, true);
            }

            /* GL attenuation: 1/(kc + kl*d + kq*d^2). citro3d's quadratic helper
             * fixes kc at 1 (the GL default), so we feed kl/kq directly and
             * assume kc ~= 1 (documented limitation). Build a LUT over a distance
             * range out to where attenuation has decayed to ~1%. Only enable when
             * there's a real distance term. */
            if (positional && (L->atten_linear > 0.0f || L->atten_quadratic > 0.0f)) {
                float d_far;
                if (L->atten_quadratic > 0.0f)
                    d_far = sqrtf(99.0f / L->atten_quadratic);
                else
                    d_far = 99.0f / L->atten_linear;
                if (d_far < 16.0f)   d_far = 16.0f;
                if (d_far > 8192.0f) d_far = 8192.0f;
                LightLutDA_Quadratic(&g.light_lut_da[n], 0.0f, d_far,
                                     L->atten_linear, L->atten_quadratic);
                C3D_LightDistAttn(cl, &g.light_lut_da[n]);
                C3D_LightDistAttnEnable(cl, true);
            }
            n++;
        }
        g.light_env_built = 1;
        g.light_dirty = 0;
    }
    C3D_LightEnvBind(&g.light_env);
}

void novaClearExplicitTevStages(void) {
    if (g.explicit_tev_count != 0) {
        g.explicit_tev_count = 0;
        g.tev_dirty = 1;
    }
}


// ==== ИЗМЕНЕНИЯ APPLY GPU STATE ====
// Вынесена логика переворота матрицы и Scissor Box'а
// Теперь FBO (g.bound_fbo != 0) не крутит камеру!

void apply_gpu_state(void) {
    /* --- Shader selector --------------------------------------------------
     * Pick the cheapest shader that satisfies the current state:
     *   clipspace : explicit (set by novaBeginClipSpace2D)
     *   basic     : no VERTEX fog + identity tex matrix
     *   texmtx    : no VERTEX fog + non-identity tex matrix
     *   full      : vertex fog needed (GL_LINEAR fog enabled)
     * Vertex fog is only required for GL_LINEAR — GL_EXP/GL_EXP2 run on the
     * per-pixel hardware fog LUT (bound below regardless of shader), so EXP
     * fog games stay on the fast basic/texmtx path. The FULL shader's fog
     * uniforms carry a precomputed 1/(end-start); inv == 0 disables the blend.
     * Switching shaders invalidates the GPU's uniform state, so we force all
     * matrix stacks + fog dirty when the active program changes. */
    int vertex_fog_needed = g.fog_enabled && g.fog_mode == GL_LINEAR;
    /* Lit when GL_LIGHTING is on AND the caller bound a normal array (no normals
     * => nothing to light, stay on the normal fast path). */
    int lit = g.lighting_enabled && g.va_normal.enabled && g.shader_lighting_dvlb
              && !g.clipspace_mode_enabled;
    g.lighting_active = lit;
    int desired;
    if (g.clipspace_mode_enabled && g.shader_clipspace_dvlb) {
        desired = NOVA_SHADER_CLIPSPACE;
    } else if (lit) {
        desired = NOVA_SHADER_LIGHTING;
    } else if (!vertex_fog_needed && g.tex_mtx_is_identity && g.shader_basic_dvlb) {
        desired = NOVA_SHADER_BASIC;
    } else if (!vertex_fog_needed && g.shader_texmtx_dvlb) {
        desired = NOVA_SHADER_TEXMTX;
    } else {
        desired = NOVA_SHADER_FULL;
    }
    if (desired != g.active_shader) {
        switch (desired) {
            case NOVA_SHADER_CLIPSPACE: C3D_BindProgram(&g.shader_clipspace_program); break;
            case NOVA_SHADER_LIGHTING:  C3D_BindProgram(&g.shader_lighting_program);  break;
            case NOVA_SHADER_BASIC:     C3D_BindProgram(&g.shader_basic_program);     break;
            case NOVA_SHADER_TEXMTX:    C3D_BindProgram(&g.shader_texmtx_program);    break;
            default:                    C3D_BindProgram(&g.shader_program);           break;
        }
        g.active_shader = desired;
        g.matrices_dirty = 1;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
        g.fog_dirty = 1;
    }

    if (g.matrices_dirty) {
        /* NOTE: the old `if (g.fog_enabled) g.fog_dirty = 1;` here is gone.
         * Fog depends only on fog params (shader computes distance from the
         * per-vertex eye position, no fog uniform involves matrices), and the
         * forced dirty was rebuilding the 128-entry EXP fog LUT (128x expf on
         * a 268MHz ARM11) every frame the camera moved. */

        /* Stereo slider changes per frame; if 3D is active we must re-run the
         * projection rebuild even when only modelview changed (offset depends
         * on eye). Otherwise honour the per-stack flag. */
        int force_proj_upload = (osGet3DSliderState() > 0.0f && g.stereo_depth != 0.0f);
        int need_proj_rebuild = (g.proj_dirty || force_proj_upload || !g.final_proj_cached_valid);
        /* On the full shader the projection uniform is only re-uploaded when
         * rebuilt; on the basic shader we always need final_proj available to
         * combine with modelview, so we use the cached one when not rebuilt. */
        int uploaded_proj_this_call = 0;

        C3D_Mtx final_proj;
        int final_proj_built = 0;

        if (need_proj_rebuild) {
            C3D_Mtx adj_proj = g.proj_stack[g.proj_sp];

            float slider = osGet3DSliderState();
            if (slider > 0.0f && g.stereo_depth != 0.0f) {
                float shift = slider * g.stereo_depth;
                float offset = (g.current_eye == 0) ? shift : -shift;
                C3D_Mtx trans; Mtx_Identity(&trans); trans.r[0].w = offset;
                C3D_Mtx temp_proj; Mtx_Multiply(&temp_proj, &trans, &adj_proj);
                adj_proj = temp_proj;
            }
            // hack for PICA200 (3DS Z-range)
            adj_proj.r[2].x = adj_proj.r[2].x * 0.4999f - adj_proj.r[3].x * 0.5f;
            adj_proj.r[2].y = adj_proj.r[2].y * 0.4999f - adj_proj.r[3].y * 0.5f;
            adj_proj.r[2].z = adj_proj.r[2].z * 0.4999f - adj_proj.r[3].z * 0.5f;
            adj_proj.r[2].w = adj_proj.r[2].w * 0.4999f - adj_proj.r[3].w * 0.5f;

            C3D_Mtx tilt;
            Mtx_Identity(&tilt);

            // ФИКС ФРЕЙМБУФЕРОВ ЗДЕСЬ: ЭКРАН КРУТИТСЯ, А FBO - НЕТ!
            // The on-screen frame (app surface OR direct LCD) is rendered
            // sideways via this per-draw tilt; FBOs stay landscape.
            #ifndef NOVAGL_TILT_VARIANT
            #define NOVAGL_TILT_VARIANT 1
            #endif
            if (g.bound_fbo == 0) {
                #if NOVAGL_TILT_VARIANT == 1
                tilt.r[0].x = 0.0f; tilt.r[0].y =  1.0f;
                tilt.r[1].x = -1.0f; tilt.r[1].y = 0.0f;
                #elif NOVAGL_TILT_VARIANT == 2
                tilt.r[0].x = 0.0f; tilt.r[0].y = -1.0f;
                tilt.r[1].x = 1.0f;  tilt.r[1].y = 0.0f;
                #elif NOVAGL_TILT_VARIANT == 3
                tilt.r[0].x = -1.0f; tilt.r[0].y = 0.0f;
                tilt.r[1].x = 0.0f;  tilt.r[1].y = -1.0f;
                #endif
            }

            Mtx_Multiply(&final_proj, &tilt, &adj_proj);
            final_proj_built = 1;
            g.final_proj_cached = final_proj;
            g.final_proj_cached_valid = 1;
        } else {
            final_proj = g.final_proj_cached;
            final_proj_built = 1;
        }

        switch (g.active_shader) {
            case NOVA_SHADER_CLIPSPACE:
                /* The clipspace shader does NOT force w=1 (unlike full/basic/
                 * texmtx). Upload final_proj — same matrix the full shader
                 * uses — so post-clip rotation + Z-range fixup get applied
                 * while the perspective divide keeps a real w. This is what
                 * makes 3D geometry survive on top of fast3d, which already
                 * does the MVP transform on the CPU and hands us clip-space.
                 *
                 * The shader-switch path forces proj_dirty=1, so on the
                 * first call after novaBeginClipSpace2D the uniform gets
                 * (re-)bound at its new location. */
                if (g.uLoc_projection_clipspace >= 0 && need_proj_rebuild) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &final_proj);
                }
                break;
            case NOVA_SHADER_BASIC:
                if (need_proj_rebuild || g.mv_dirty) {
                    Mtx_Multiply(&g.mvp_combined, &final_proj, &g.mv_stack[g.mv_sp]);
                    if (g.uLoc_mvp_basic >= 0)
                        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_mvp_basic, &g.mvp_combined);
                }
                break;
            case NOVA_SHADER_TEXMTX:
                if (need_proj_rebuild || g.mv_dirty) {
                    Mtx_Multiply(&g.mvp_combined, &final_proj, &g.mv_stack[g.mv_sp]);
                    if (g.uLoc_mvp_texmtx >= 0)
                        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_mvp_texmtx, &g.mvp_combined);
                }
                if (g.uLoc_texmtx_texmtx >= 0 && g.tex_mtx_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_texmtx_texmtx, &g.tex_stack[g.tex_sp]);
                break;
            case NOVA_SHADER_LIGHTING:
                /* Like FULL: separate proj + modelview. The lighting unit needs
                 * the eye-space pos/normal the modelview produces, so we MUST
                 * upload modelview raw (not pre-combined into MVP). */
                if (need_proj_rebuild && g.uLoc_projection_lighting >= 0) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_lighting, &final_proj);
                    uploaded_proj_this_call = 1;
                }
                if (g.uLoc_modelview_lighting >= 0 && g.mv_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_modelview_lighting, &g.mv_stack[g.mv_sp]);
                break;
            default: /* NOVA_SHADER_FULL */
                if (need_proj_rebuild && g.uLoc_projection >= 0) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection, &final_proj);
                    uploaded_proj_this_call = 1;
                }
                if (g.uLoc_modelview >= 0 && g.mv_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_modelview, &g.mv_stack[g.mv_sp]);
                if (g.uLoc_texmtx >= 0 && g.tex_mtx_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_texmtx, &g.tex_stack[g.tex_sp]);
                break;
        }
        (void)uploaded_proj_this_call;
        g.matrices_dirty = 0;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 0;
    }

    /* --- Fragment lighting --------------------------------------------------
     * Bind the HW light env on lit draws (rebuilding it from GL state when
     * dirty); explicitly unbind on everything else so non-lit geometry isn't
     * tinted by a stale env. */
    if (g.lighting_active) {
        nova_apply_light_env();
    } else {
        C3D_LightEnvBind(NULL);
    }

    /* --- Fog --------------------------------------------------------------
     * Two independent mechanisms:
     *  (a) Vertex fog uniforms — only the FULL shader has them, and only
     *      GL_LINEAR uses them. fogparams[0] = (start, end, 1/(end-start));
     *      the shader computes f = 1 - (d - start) * inv. Uploading inv = 0
     *      forces f = 1 (no fog) — also used for EXP modes so the vertex
     *      blend never double-applies on top of the hardware LUT (the old
     *      code DID double-apply: vertex linear blend + per-pixel EXP LUT).
     *  (b) Hardware per-pixel fog LUT for GL_EXP/GL_EXP2 — shader-
     *      independent, so EXP fog now also works on basic/texmtx (the
     *      selector above no longer forces FULL for EXP modes). */
    if (g.fog_dirty) {
        if (g.active_shader == NOVA_SHADER_FULL && g.uLoc_fogparams >= 0) {
            int vertex_fog = g.fog_enabled && g.fog_mode == GL_LINEAR;
            if (vertex_fog) {
                float safe_end = g.fog_end;
                if (fabsf(safe_end - g.fog_start) < 0.001f) safe_end = g.fog_start + 0.001f;
                float inv_range = 1.0f / (safe_end - g.fog_start);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, g.fog_start, safe_end, inv_range, 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, g.fog_color[0], g.fog_color[1], g.fog_color[2], g.fog_color[3]);
            } else {
                /* inv = 0 -> f = 1 -> pure vertex color. */
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, 0.0f, 0.0f, 0.0f, 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        /* fog_color goes through the dedicated FogColor register on the LUT
         * path so nothing is multiplied on the vertex side. */
#ifdef NOVAGL_DISABLE_FOG_LUT
        if (0) {
#else
        if (g.fog_enabled && (g.fog_mode == GL_EXP || g.fog_mode == GL_EXP2)) {
#endif
            /* Rebuild LUT only when the parameters actually changed —
             * fog_dirty also fires on shader switches and glPopAttrib, and
             * FogLut_Exp is 128x expf on a 268MHz ARM11. */
            static GLenum s_lut_mode = 0;
            static float s_lut_density = -1.0f, s_lut_start = 0.0f, s_lut_end = 0.0f;
            if (s_lut_mode != g.fog_mode || s_lut_density != g.fog_density ||
                s_lut_start != g.fog_start || s_lut_end != g.fog_end) {
                if (g.fog_mode == GL_EXP) {
                    FogLut_Exp(&g.fog_lut, g.fog_density, 1.0f,
                               g.fog_start, g.fog_end);
                } else {
                    FogLut_Exp(&g.fog_lut, g.fog_density * g.fog_density, 2.0f,
                               g.fog_start, g.fog_end);
                }
                s_lut_mode = g.fog_mode;
                s_lut_density = g.fog_density;
                s_lut_start = g.fog_start;
                s_lut_end = g.fog_end;
            }
            C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
            u32 col = ((u32)(clampf(g.fog_color[0], 0, 1) * 255) << 0)
                    | ((u32)(clampf(g.fog_color[1], 0, 1) * 255) << 8)
                    | ((u32)(clampf(g.fog_color[2], 0, 1) * 255) << 16);
            C3D_FogColor(col);
            C3D_FogLutBind(&g.fog_lut);
        } else {
            C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
        }
        g.fog_dirty = 0;
    }

    /* The state cache vars live at file scope below — see s_last_*. The old
     * code re-declared them as `extern` inside this function (legal C but
     * misleading); since they're in the same TU, file-scope forward
     * declarations are enough and we just use them directly. */

    GPU_WRITEMASK writemask = 0;
    if (g.color_mask_r) writemask |= GPU_WRITE_RED;
    if (g.color_mask_g) writemask |= GPU_WRITE_GREEN;
    if (g.color_mask_b) writemask |= GPU_WRITE_BLUE;
    if (g.color_mask_a) writemask |= GPU_WRITE_ALPHA;
    if (g.depth_mask && g.depth_test_enabled) writemask |= GPU_WRITE_DEPTH;
    if (writemask != s_last_writemask ||
        s_last_depth_test_enabled != g.depth_test_enabled ||
        s_last_depth_func != g.depth_func) {
        /* MUST be the depth-specific (inverting) mapping: PICA's buffer is
         * near=high/far=low (see apply_depth_map), so GL_LESS runs as
         * GPU_GREATER. The plain gl_to_gpu_testfunc here z-rejected every
         * fragment against the far-plane clear (LESS vs cleared 0). */
        C3D_DepthTest(g.depth_test_enabled, gl_to_gpu_depth_testfunc(g.depth_func), writemask);
        s_last_writemask = writemask;
        s_last_depth_test_enabled = g.depth_test_enabled;
        s_last_depth_func = g.depth_func;
    }

    u8 alpha_ref8 = (u8)(clampf(g.alpha_ref, 0.0f, 1.0f) * 255.0f + 0.5f);
    if (s_last_alpha_test_enabled != g.alpha_test_enabled ||
        s_last_alpha_func != g.alpha_func ||
        s_last_alpha_ref8 != alpha_ref8) {
        C3D_AlphaTest(g.alpha_test_enabled, gl_to_gpu_testfunc(g.alpha_func), alpha_ref8);
        s_last_alpha_test_enabled = g.alpha_test_enabled;
        s_last_alpha_func = g.alpha_func;
        s_last_alpha_ref8 = alpha_ref8;
    }

    /* Colour-stage selection: logic op OR blend (mutually exclusive on PICA,
     * and per the GL spec GL_COLOR_LOGIC_OP overrides blending when enabled).
     * C3D_ColorLogicOp / C3D_AlphaBlend each flip the fragOpMode select bit, so
     * toggling between them re-pushes correctly. blend_color feeds the
     * GL_CONSTANT_COLOR/ALPHA factors via C3D_BlendingColor. */
    u32 blend_col_packed = pack_tev_color(g.blend_color);
    if (s_last_blend_enabled != g.blend_enabled ||
        s_last_blend_src != g.blend_src ||
        s_last_blend_dst != g.blend_dst ||
        s_last_blend_src_alpha != g.blend_src_alpha ||
        s_last_blend_dst_alpha != g.blend_dst_alpha ||
        s_last_blend_eq_rgb != g.blend_eq_rgb ||
        s_last_blend_eq_alpha != g.blend_eq_alpha ||
        s_last_logic_op_enabled != g.color_logic_op_enabled ||
        s_last_logic_op != g.logic_op ||
        s_last_blend_color != blend_col_packed) {
        if (g.color_logic_op_enabled) {
            C3D_ColorLogicOp(gl_to_gpu_logicop(g.logic_op));
        } else if (g.blend_enabled) {
            C3D_BlendingColor(blend_col_packed);
            C3D_AlphaBlend(gl_to_gpu_blendeq(g.blend_eq_rgb), gl_to_gpu_blendeq(g.blend_eq_alpha),
                           gl_to_gpu_blendfactor(g.blend_src), gl_to_gpu_blendfactor(g.blend_dst),
                           gl_to_gpu_blendfactor(g.blend_src_alpha), gl_to_gpu_blendfactor(g.blend_dst_alpha));
        } else {
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        }
        s_last_blend_enabled = g.blend_enabled;
        s_last_blend_src = g.blend_src;
        s_last_blend_dst = g.blend_dst;
        s_last_blend_src_alpha = g.blend_src_alpha;
        s_last_blend_dst_alpha = g.blend_dst_alpha;
        s_last_blend_eq_rgb = g.blend_eq_rgb;
        s_last_blend_eq_alpha = g.blend_eq_alpha;
        s_last_logic_op_enabled = g.color_logic_op_enabled;
        s_last_logic_op = g.logic_op;
        s_last_blend_color = blend_col_packed;
    }

    if (s_last_cull_enabled != g.cull_face_enabled ||
        s_last_cull_mode != g.cull_face_mode ||
        s_last_front_face != g.front_face) {
        if (g.cull_face_enabled) {
            GPU_CULLMODE cull;
            /* GL_FRONT_AND_BACK is emulated in nova_draw_internal (the draw
             * is skipped entirely, per spec). If a draw somehow reaches here
             * with that mode (custom nova* fast paths), the closest GPU state
             * is front-culling — but the draw-skip is the real mechanism. */
            if (g.cull_face_mode == GL_FRONT || g.cull_face_mode == GL_FRONT_AND_BACK)
                cull = (g.front_face == GL_CCW) ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
            else
                cull = (g.front_face == GL_CCW) ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
            C3D_CullFace(cull);
        } else {
            C3D_CullFace(GPU_CULL_NONE);
        }
        s_last_cull_enabled = g.cull_face_enabled;
        s_last_cull_mode = g.cull_face_mode;
        s_last_front_face = g.front_face;
    }

    // ВТОРОЙ ФИКС: SCISSOR
    if (g.scissor_test_enabled) {
        // Меняем при смене параметров ИЛИ при смене целевого FBO (там ориентация другая).
        if (s_last_scissor_enabled != 1 ||
            s_last_scissor_x != g.scissor_x || s_last_scissor_y != g.scissor_y ||
            s_last_scissor_w != g.scissor_w || s_last_scissor_h != g.scissor_h ||
            s_last_scissor_fbo != (g.bound_fbo == 0) ||
            s_last_scissor_target != g.current_target) {
            apply_scissor_box();
            s_last_scissor_enabled = 1;
            s_last_scissor_x = g.scissor_x;
            s_last_scissor_y = g.scissor_y;
            s_last_scissor_w = g.scissor_w;
            s_last_scissor_h = g.scissor_h;
            s_last_scissor_fbo = (g.bound_fbo == 0);
            s_last_scissor_target = g.current_target;
        }
    } else if (s_last_scissor_enabled != 0) {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        s_last_scissor_enabled = 0;
        s_last_scissor_target = NULL;
    }

    int current_tex_state = 0;
    for (int i = 0; i < 3; i++) {
        if (g.texture_2d_enabled_unit[i] && g.bound_texture[i] > 0) current_tex_state |= (1 << i);
    }

    /* The TEV base-colour source depends on lighting_active (vertex colour vs
     * HW fragment primary). Re-emit the chain when that toggles. */
    static int s_last_lighting_active = -1;
    if (s_last_lighting_active != g.lighting_active) {
        g.tev_dirty = 1;
        s_last_lighting_active = g.lighting_active;
    }

    if (g.tev_dirty || g.last_tex_state != current_tex_state) {
        // ── Explicit-stage path ────────────────────────────────────────
        // When the caller has installed an explicit TEV stage list via
        // novaSetExplicitTevStages, emit those stages verbatim and skip
        // the per-unit GL_COMBINE machinery entirely. This is the path
        // gfx_novagl uses for 2-cycle CCs / TRILERP / anything that
        // doesn't map 1:1 to "one stage per texture unit".
        if (g.explicit_tev_count > 0) {
            int n = g.explicit_tev_count;
            for (int i = 0; i < n; i++) {
                const NovaTevStageGL *s = &g.explicit_tev_stages[i];
                C3D_TexEnv *env = C3D_GetTexEnv(i);
                C3D_TexEnvInit(env);

                GPU_TEVSRC s0_rgb = get_tev_src_explicit(s->src_rgb[0]);
                GPU_TEVSRC s1_rgb = get_tev_src_explicit(s->src_rgb[1]);
                GPU_TEVSRC s2_rgb = get_tev_src_explicit(s->src_rgb[2]);
                GPU_TEVSRC s0_a   = get_tev_src_explicit(s->src_alpha[0]);
                GPU_TEVSRC s1_a   = get_tev_src_explicit(s->src_alpha[1]);
                GPU_TEVSRC s2_a   = get_tev_src_explicit(s->src_alpha[2]);

                C3D_TexEnvSrc(env, C3D_RGB,   s0_rgb, s1_rgb, s2_rgb);
                C3D_TexEnvSrc(env, C3D_Alpha, s0_a,   s1_a,   s2_a);

                C3D_TexEnvOpRgb(env,
                                get_tev_op_rgb(s->op_rgb[0]),
                                get_tev_op_rgb(s->op_rgb[1]),
                                get_tev_op_rgb(s->op_rgb[2]));
                C3D_TexEnvOpAlpha(env,
                                  get_tev_op_alpha(s->op_alpha[0]),
                                  get_tev_op_alpha(s->op_alpha[1]),
                                  get_tev_op_alpha(s->op_alpha[2]));

                C3D_TexEnvFunc(env, C3D_RGB,   gl_to_gpu_combinefunc(s->combine_rgb));
                C3D_TexEnvFunc(env, C3D_Alpha, gl_to_gpu_combinefunc(s->combine_alpha));

                // CONSTANT is per-stage on PICA; push regardless (cost is
                // a single register write, and skipping based on detection
                // adds branches without saving meaningful work).
                C3D_TexEnvColor(env, pack_tev_color(s->constant_color));
            }
            // Pad remaining slots with passthrough so leftover state from
            // a prior frame doesn't pollute the chain.
            for (int i = n; i < 6; i++) {
                C3D_TexEnv *env = C3D_GetTexEnv(i);
                C3D_TexEnvInit(env);
                C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            }
            g.last_tex_state = current_tex_state;
            g.tev_dirty = 0;
            goto tev_done;
        }

        int tev_stage = 0;
        for (int unit = 0; unit < 3; unit++) {
            if (!(current_tex_state & (1 << unit))) continue;

            C3D_TexEnv *env = C3D_GetTexEnv(tev_stage);
            C3D_TexEnvInit(env);

            GPU_TEVSRC tex_src = (unit == 0) ? GPU_TEXTURE0 : ((unit == 1) ? GPU_TEXTURE1 : GPU_TEXTURE2);
            /* On lit draws the "vertex colour" the chain modulates against is
             * the HW lighting output (GPU_FRAGMENT_PRIMARY_COLOR) instead of the
             * interpolated GPU_PRIMARY_COLOR — so texture * light works. */
            GPU_TEVSRC base_prim = g.lighting_active ? GPU_FRAGMENT_PRIMARY_COLOR : GPU_PRIMARY_COLOR;
            GPU_TEVSRC prev_src = (tev_stage == 0) ? base_prim : GPU_PREVIOUS;

            /* Track whether the configured stage references GPU_CONSTANT on
             * either the RGB or Alpha side — if it does, push the unit's
             * tex_env_color into the C3D stage. We do this for the per-unit
             * "const" colour, not the global TexEnvBufColor, which is a
             * different thing (used as a CC scratch register for fast3d-style
             * combiners not represented in legacy GL_COMBINE). */
            int uses_const = 0;

            if (g.tex_env_mode[unit] == GL_COMBINE) {
                /* RGB combiner. */
                GPU_TEVSRC s0_rgb = get_tev_src(g.tex_env_src0_rgb[unit], tex_src, prev_src);
                GPU_TEVSRC s1_rgb = get_tev_src(g.tex_env_src1_rgb[unit], tex_src, prev_src);
                GPU_TEVSRC s2_rgb = get_tev_src(g.tex_env_src2_rgb[unit], tex_src, prev_src);
                int op0_rgb = get_tev_op_rgb(g.tex_env_operand0_rgb[unit]);
                int op1_rgb = get_tev_op_rgb(g.tex_env_operand1_rgb[unit]);
                int op2_rgb = get_tev_op_rgb(g.tex_env_operand2_rgb[unit]);

                if (g.tex_env_src0_rgb[unit] == GL_CONSTANT ||
                    g.tex_env_src1_rgb[unit] == GL_CONSTANT ||
                    g.tex_env_src2_rgb[unit] == GL_CONSTANT) uses_const = 1;

                switch (g.tex_env_combine_rgb[unit]) {
                    case GL_DOT3_RGBA_ARB:
                        C3D_TexEnvSrc(env, C3D_RGB, s0_rgb, s1_rgb, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpRgb(env, op0_rgb, op1_rgb, 0);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_DOT3_RGBA); break;
                    case GL_REPLACE:
                        C3D_TexEnvSrc(env, C3D_RGB, s0_rgb, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpRgb(env, op0_rgb, 0, 0);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE); break;
                    case GL_ADD:
                        C3D_TexEnvSrc(env, C3D_RGB, s0_rgb, s1_rgb, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpRgb(env, op0_rgb, op1_rgb, 0);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD); break;
                    case GL_INTERPOLATE:
                        C3D_TexEnvSrc(env, C3D_RGB, s0_rgb, s1_rgb, s2_rgb);
                        C3D_TexEnvOpRgb(env, op0_rgb, op1_rgb, op2_rgb);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE); break;
                    case GL_MODULATE:
                    default:
                        C3D_TexEnvSrc(env, C3D_RGB, s0_rgb, s1_rgb, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpRgb(env, op0_rgb, op1_rgb, 0);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE); break;
                }

                /* Alpha combiner. If combine_alpha is unset (0) we mirror the
                 * RGB function with sources mapped to the alpha equivalents
                 * (SRC_ALPHA op), which matches the OpenGL ES 1.1 default
                 * before any explicit GL_COMBINE_ALPHA call. */
                GLint alpha_func = g.tex_env_combine_alpha[unit];
                if (alpha_func == 0) alpha_func = g.tex_env_combine_rgb[unit];

                GPU_TEVSRC s0_a = get_tev_src(
                    g.tex_env_src0_alpha[unit] ? g.tex_env_src0_alpha[unit] : g.tex_env_src0_rgb[unit],
                    tex_src, prev_src);
                GPU_TEVSRC s1_a = get_tev_src(
                    g.tex_env_src1_alpha[unit] ? g.tex_env_src1_alpha[unit] : g.tex_env_src1_rgb[unit],
                    tex_src, prev_src);
                GPU_TEVSRC s2_a = get_tev_src(
                    g.tex_env_src2_alpha[unit] ? g.tex_env_src2_alpha[unit] : g.tex_env_src2_rgb[unit],
                    tex_src, prev_src);
                int op0_a = get_tev_op_alpha(g.tex_env_operand0_alpha[unit]
                                                ? g.tex_env_operand0_alpha[unit] : GL_SRC_ALPHA);
                int op1_a = get_tev_op_alpha(g.tex_env_operand1_alpha[unit]
                                                ? g.tex_env_operand1_alpha[unit] : GL_SRC_ALPHA);
                int op2_a = get_tev_op_alpha(g.tex_env_operand2_alpha[unit]
                                                ? g.tex_env_operand2_alpha[unit] : GL_SRC_ALPHA);

                if ((g.tex_env_src0_alpha[unit] ? g.tex_env_src0_alpha[unit] : g.tex_env_src0_rgb[unit]) == GL_CONSTANT ||
                    (g.tex_env_src1_alpha[unit] ? g.tex_env_src1_alpha[unit] : g.tex_env_src1_rgb[unit]) == GL_CONSTANT ||
                    (g.tex_env_src2_alpha[unit] ? g.tex_env_src2_alpha[unit] : g.tex_env_src2_rgb[unit]) == GL_CONSTANT)
                    uses_const = 1;

                switch (alpha_func) {
                    case GL_REPLACE:
                        C3D_TexEnvSrc(env, C3D_Alpha, s0_a, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpAlpha(env, op0_a, 0, 0);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE); break;
                    case GL_ADD:
                        C3D_TexEnvSrc(env, C3D_Alpha, s0_a, s1_a, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpAlpha(env, op0_a, op1_a, 0);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_ADD); break;
                    case GL_INTERPOLATE:
                        C3D_TexEnvSrc(env, C3D_Alpha, s0_a, s1_a, s2_a);
                        C3D_TexEnvOpAlpha(env, op0_a, op1_a, op2_a);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_INTERPOLATE); break;
                    case GL_MODULATE:
                    default:
                        C3D_TexEnvSrc(env, C3D_Alpha, s0_a, s1_a, (GPU_TEVSRC) 0);
                        C3D_TexEnvOpAlpha(env, op0_a, op1_a, 0);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE); break;
                }
            } else {
                switch (g.tex_env_mode[unit]) {
                    case GL_REPLACE:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE); break;
                    case GL_ADD:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, prev_src, (GPU_TEVSRC) 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_ADD); break;
                    case GL_DECAL:
                        C3D_TexEnvSrc(env, C3D_RGB, prev_src, tex_src, tex_src);
                        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
                        C3D_TexEnvSrc(env, C3D_Alpha, prev_src, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE); break;
                    case GL_MODULATE:
                    default:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, prev_src, (GPU_TEVSRC) 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE); break;
                }
            }

            if (uses_const) {
                C3D_TexEnvColor(env, pack_tev_color(g.tex_env_color[unit]));
            }

            /* GL_RGB_SCALE / GL_ALPHA_SCALE: post-combine multiplier (1/2/4). */
            C3D_TexEnvScale(env, C3D_RGB,   gl_to_gpu_tevscale(g.tex_env_rgb_scale[unit]));
            C3D_TexEnvScale(env, C3D_Alpha, gl_to_gpu_tevscale(g.tex_env_alpha_scale[unit]));

            tev_stage++;
        }

        if (tev_stage == 0) {
            /* No texture units active: emit the base colour directly. Lit draws
             * show the HW lighting result; everything else the vertex colour. */
            C3D_TexEnv *env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            GPU_TEVSRC base_prim = g.lighting_active ? GPU_FRAGMENT_PRIMARY_COLOR : GPU_PRIMARY_COLOR;
            C3D_TexEnvSrc(env, C3D_Both, base_prim, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            tev_stage++;
        }

        for (int i = tev_stage; i < 6; i++) {
            C3D_TexEnv *env = C3D_GetTexEnv(i);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }

        g.last_tex_state = current_tex_state;
        g.tev_dirty = 0;
    }
tev_done:;

    // Бинд текстуры — самая частая команда (терен, частицы, hud).
    // Пропускаем повторный C3D_TexBind с тем же id. Сброс происходит когда
    // юнит отключается, чтобы при повторном включении бинд гарантированно
    // переустанавливался. Также сбрасывается из nova_invalidate_state_cache().
    for (int unit = 0; unit < 3; unit++) {
        if ((current_tex_state & (1 << unit)) && g.bound_texture[unit] < NOVA_MAX_TEXTURES) {
            if (s_last_tex_bound[unit] != g.bound_texture[unit]) {
                TexSlot *slot = &g.textures[g.bound_texture[unit]];
                if (slot->allocated) {
                    C3D_TexBind(unit, &slot->tex);
                    s_last_tex_bound[unit] = g.bound_texture[unit];
                }
            }
        } else {
            s_last_tex_bound[unit] = 0xFFFFFFFFu;
        }
    }
}

void nova_setup_attr_info(int pos_elements) {
    if (s_attr_pos_elements == pos_elements) return;
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, pos_elements);
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);
    C3D_SetAttrInfo(&g.attr_info);
    s_attr_pos_elements = pos_elements;
}

void nova_setup_buf_info(void *base, int stride) {
    if (s_buf_base == base && s_buf_stride == stride) return;
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, base, stride, 3, 0x210);
    s_buf_base = base;
    s_buf_stride = stride;
}

/* Force the next nova_setup_buf_info to re-apply. Used after the lit draw path
 * programs BufInfo directly (4 attrs / perm 0x3210), so the 3-attr cache can't
 * wrongly conclude "already set" on the following non-lit draw. */
void nova_invalidate_buf_cache(void) {
    s_buf_base = (void *) -1;
    s_buf_stride = -1;
}

/* Lit layout: pos(3f) + texcoord(2f) + color(4ub) + normal(3f), 4 attributes.
 * Kept on its own cache key so toggling lighting forces a re-setup. */
void nova_setup_attr_info_lit(void) {
    if (s_attr_pos_elements == -4) return; /* sentinel: lit layout already set */
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 3);          /* v0 position */
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);          /* v1 texcoord */
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);  /* v2 color    */
    AttrInfo_AddLoader(&g.attr_info, 3, GPU_FLOAT, 3);          /* v3 normal   */
    C3D_SetAttrInfo(&g.attr_info);
    s_attr_pos_elements = -4;
}

/* Invalidate the TexBind skip-cache for ONE texture id. Required whenever a
 * texture's storage is re-created at the same id (orphaning re-upload): the
 * C3D_Tex gets a NEW data pointer, but the per-unit skip-cache would say
 * "already bound" and the GPU would keep sampling the old block — which the
 * texture GC frees a frame later (manifested as textures turning black /
 * garbage moments after streaming-in). */
void nova_invalidate_tex_bind(GLuint tex_id) {
    for (int u = 0; u < 3; u++) {
        if (s_last_tex_bound[u] == tex_id) {
            s_last_tex_bound[u] = 0xFFFFFFFFu;
        }
    }
}

void nova_invalidate_state_cache(void) {
    s_last_writemask          = (GPU_WRITEMASK)-1;
    s_last_depth_test_enabled = -1;
    s_last_depth_func         = 0;
    s_last_alpha_test_enabled = -1;
    s_last_alpha_func         = 0;
    s_last_alpha_ref8         = 0xFF;
    s_last_blend_enabled      = -1;
    s_last_blend_src          = 0;
    s_last_blend_dst          = 0;
    s_last_blend_src_alpha    = 0;
    s_last_blend_dst_alpha    = 0;
    s_last_blend_eq_rgb       = 0;
    s_last_blend_eq_alpha     = 0;
    s_last_logic_op_enabled   = -1;
    s_last_logic_op           = 0;
    s_last_blend_color        = 0xDEADBEEFu;
    s_last_cull_enabled       = -1;
    s_last_cull_mode          = 0;
    s_last_front_face         = 0;
    s_last_scissor_enabled    = -1;
    s_last_scissor_x          = -1;
    s_last_scissor_y          = -1;
    s_last_scissor_w          = -1;
    s_last_scissor_h          = -1;
    s_last_scissor_fbo        = -1;
    s_last_scissor_target     = NULL;
    s_last_tex_bound[0]       = 0xFFFFFFFFu;
    s_last_tex_bound[1]       = 0xFFFFFFFFu;
    s_last_tex_bound[2]       = 0xFFFFFFFFu;
    s_attr_pos_elements       = -1;
    s_buf_base                = (void *) -1;
    s_buf_stride              = -1;
    // Также метим грязным основной кэш матриц/фога/TEV — на всякий случай.
    g.matrices_dirty = 1;
    g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
    g.fog_dirty = 1;
    g.tev_dirty = 1;
    g.last_tex_state = -1;
    /* Force shader selector to re-evaluate next apply_gpu_state. -1 mismatches
     * both 0 (full) and 1 (basic), so the next call will C3D_BindProgram. */
    g.active_shader = -1;
    g.final_proj_cached_valid = 0;
    /* Rebuild the HW light env on the next lit draw (a context reset can wipe
     * GPU lighting registers). */
    g.light_dirty = 1;
    g.light_env_built = 0;
}

void cleanup_vbo_stream(void) {
#if 0
#ifdef NOVA_VBO_STREAM
    if (g.bound_array_buffer) {
        VBOSlot *slot = &g.vbos[g.bound_array_buffer];
        if (slot->is_stream && slot->data) {
            free(slot->data);
            slot->data = NULL;
            slot->allocated = 0;
            slot->size = 0;
            slot->capacity = 0;
        }
    }

    // don't forget to clear index buffer
    if (g.bound_element_array_buffer) {
        VBOSlot *slot = &g.vbos[g.bound_element_array_buffer];
        if (slot->is_stream && slot->data) {
            free(slot->data);
            slot->data = NULL;
            slot->allocated = 0;
            slot->size = 0;
            slot->capacity = 0;
        }
    }
#endif
#endif
}

void draw_emulated_quads(int count) {
    int num_quads = count / 4;

    if (!g.static_quad_indices) return;

    int quads_drawn = 0;
    while (quads_drawn < num_quads) {
        int quads_to_draw = num_quads - quads_drawn;
        if (quads_to_draw > g.static_quad_count) quads_to_draw = g.static_quad_count;

        C3D_DrawElements(GPU_TRIANGLES, quads_to_draw * 6, C3D_UNSIGNED_SHORT, g.static_quad_indices);

        quads_drawn += quads_to_draw;
    }
}

void nova_hardware_swizzle(C3D_Tex *tex, const void *linear_pixels, int width, int height, GPU_TEXCOLOR format) {
    if (!linear_pixels || !tex->data) return;

    u32 transfer_flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    u32 in_fmt = 0, out_fmt = 0;
    switch (format) {
        case GPU_RGBA8: in_fmt = GX_TRANSFER_FMT_RGBA8;
            out_fmt = GX_TRANSFER_FMT_RGBA8;
            break;
        case GPU_RGB8: in_fmt = GX_TRANSFER_FMT_RGB8;
            out_fmt = GX_TRANSFER_FMT_RGB8;
            break;
        case GPU_RGB565: in_fmt = GX_TRANSFER_FMT_RGB565;
            out_fmt = GX_TRANSFER_FMT_RGB565;
            break;
        case GPU_RGBA5551: in_fmt = GX_TRANSFER_FMT_RGB5A1;
            out_fmt = GX_TRANSFER_FMT_RGB5A1;
            break;
        case GPU_RGBA4: in_fmt = GX_TRANSFER_FMT_RGBA4;
            out_fmt = GX_TRANSFER_FMT_RGBA4;
            break;
        default: printf("Unsupported format: %x", format);
            return;
    }

    transfer_flags |= GX_TRANSFER_IN_FORMAT(in_fmt) | GX_TRANSFER_OUT_FORMAT(out_fmt);

    GSPGPU_FlushDataCache(linear_pixels, width * height * gpu_texfmt_bpp(format));

    C3D_SyncDisplayTransfer((u32 *) linear_pixels, GX_BUFFER_DIM(width, height),
                            (u32 *) tex->data, GX_BUFFER_DIM(tex->width, tex->height), transfer_flags);
}