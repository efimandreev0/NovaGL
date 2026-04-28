//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

unsigned int nova_next_pow2(unsigned int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16;
    return v + 1;
}

void* linear_alloc_ring(void *base, int *offset, int size, int capacity) {
    size = (size + 0x7F) & ~0x7F;

    if (*offset + size > capacity) {
        C3D_FrameSplit(0);

        gspWaitForP3D();
        *offset = 0; // wrap
    }

    void *ptr = (uint8_t*)base + *offset;
    *offset += size;

    return ptr;
}

float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

void dl_record_translate(float x, float y, float z) {
    if (g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_TRANSLATE; op->args[0] = x; op->args[1] = y; op->args[2] = z;
        }
    }
}

void dl_record_color3f(float r, float g_, float b) {
    if (g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR3F; op->args[0] = r; op->args[1] = g_; op->args[2] = b;
        }
    }
}

void dl_execute(GLuint list) {
    if (list >= NOVA_DISPLAY_LISTS) return;
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
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_LESS;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_LEQUAL;
        case GL_GREATER:  return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_GEQUAL;
        case GL_ALWAYS:
        default:          return GPU_ALWAYS;
    }
}
GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_GREATER;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_GEQUAL;
        case GL_GREATER:  return GPU_LESS;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_LEQUAL;
        case GL_ALWAYS:
        default:          return GPU_ALWAYS;
    }
}
GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER:    return GPU_NEVER;
        case GL_LESS:     return GPU_LESS;
        case GL_EQUAL:    return GPU_EQUAL;
        case GL_LEQUAL:   return GPU_LEQUAL;
        case GL_GREATER:  return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL:   return GPU_GEQUAL;
        case GL_ALWAYS:
        default:          return GPU_ALWAYS;
    }
}

GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor) {
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

int gl_type_size(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:  return 1;

        case GL_SHORT:
        case GL_UNSIGNED_SHORT: return 2;

        case GL_FLOAT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FIXED:
        default:                return 4;
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

GPU_Primitive_t gl_to_gpu_primitive(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES:      return GPU_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GPU_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN:   return GPU_TRIANGLE_FAN;
        default:                return GPU_TRIANGLES;
    }
}

GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type) {
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
    if (format == GL_LUMINANCE_ALPHA4_NOVA)     return GPU_LA4;
    if (format == GL_ALPHA)                     return GPU_A8;
    return GPU_RGBA8;
}

int gpu_texfmt_bpp(GPU_TEXCOLOR fmt) {
    switch (fmt) {
        case GPU_RGBA8:    return 4;
        case GPU_RGB8:     return 3;
        case GPU_RGBA5551:
        case GPU_RGB565:
        case GPU_RGBA4:
        case GPU_LA8:      return 2;
        case GPU_L8:
        case GPU_A8:
        case GPU_LA4:      return 1;
        default:           return 4;
    }
}

C3D_Mtx* cur_mtx(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_stack[g.proj_sp];
        case GL_TEXTURE:    return &g.tex_stack[g.tex_sp];
        default:            return &g.mv_stack[g.mv_sp];
    }
}

int* cur_sp(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return &g.proj_sp;
        case GL_TEXTURE:    return &g.tex_sp;
        default:            return &g.mv_sp;
    }
}

C3D_Mtx* cur_stack(void) {
    switch (g.matrix_mode) {
        case GL_PROJECTION: return g.proj_stack;
        case GL_TEXTURE:    return g.tex_stack;
        default:            return g.mv_stack;
    }
}
void* get_tex_staging(int size)
{
    if (g.tex_staging_size < size) {
        void *new_buf = realloc(g.tex_staging, (size_t)size);
        if (!new_buf) {
            return NULL;
        }
        g.tex_staging = new_buf;
        g.tex_staging_size = size;
    }
    return g.tex_staging;
}
uint32_t morton_interleave(uint32_t x, uint32_t y) {
    static const uint32_t xlut[8] = {0x00,0x01,0x04,0x05,0x10,0x11,0x14,0x15};
    static const uint32_t ylut[8] = {0x00,0x02,0x08,0x0a,0x20,0x22,0x28,0x2a};
    return xlut[x & 7] | ylut[y & 7];
}
static inline int morton_offset(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int)morton_interleave(x & 7, fy & 7);
}

void swizzle_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint8_t val = (x < src_w && y < src_h) ? src[y * src_w + x] : 0;
            dst[morton_offset(x, y, pot_w, pot_h)] = val;
        }
    }
}

void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint16_t val = (x < src_w && y < src_h) ? src[y * src_w + x] : 0;
            dst[morton_offset(x, y, pot_w, pot_h)] = val;
        }
    }
}

void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint32_t out_pixel = 0;
            if (x < src_w && y < src_h) {
                uint32_t pixel = src[y * src_w + x];
                uint8_t r = (pixel >>  0) & 0xFF;
                uint8_t g_c = (pixel >>  8) & 0xFF;
                uint8_t b = (pixel >> 16) & 0xFF;
                uint8_t a = (pixel >> 24) & 0xFF;
                out_pixel = ((uint32_t)r << 24) | ((uint32_t)g_c << 16) | ((uint32_t)b << 8) | (uint32_t)a;
            }
            dst[morton_offset(x, y, pot_w, pot_h)] = out_pixel;
        }
    }
}

uint32_t* rgb_to_rgba(const uint8_t *rgb, int w, int h) {
    uint32_t *out = (uint32_t*)malloc(w * h * 4);
    if (!out) return nullptr;
    for (int i = 0; i < w * h; i++) {
        //this color channels in C3D is really fucking fuck. PICA200 is fucking puzzle.
        out[i] = (rgb[i*3+0] << 24) | (rgb[i*3+1] << 16) | (rgb[i*3+2] << 8) | 0xFF;
    }
    return out;
}

void downscale_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter: average every source pixel covered by each destination cell.
    // Uses fixed-point spans so no source pixel is skipped even for large scale factors
    // (e.g. 4096x4096 -> 1024x1024, the typical GameMaker atlas case).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int)((int64_t)y       * src_h / dst_h);
        int sy1 = (int)((int64_t)(y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int)((int64_t)x       * src_w / dst_w);
            int sx1 = (int)((int64_t)(x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t r = 0, g = 0, b = 0, a = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint32_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint32_t p = row[xx];
                    r += (p >>  0) & 0xFF;
                    g += (p >>  8) & 0xFF;
                    b += (p >> 16) & 0xFF;
                    a += (p >> 24) & 0xFF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint8_t rr = (uint8_t)(r / count);
            uint8_t gg = (uint8_t)(g / count);
            uint8_t bb = (uint8_t)(b / count);
            uint8_t aa = (uint8_t)(a / count);
            dst[y * dst_w + x] = ((uint32_t)aa << 24) | ((uint32_t)bb << 16) | ((uint32_t)gg << 8) | rr;
        }
    }
}

void downscale_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter for RGBA4/RGB565-ish 16-bit. Unpack as 4x4-bit channels, average, repack.
    // This assumes the 16-bit format is a 4-channel 4-bit layout (works for RGBA4);
    // for RGB565 the per-channel averaging still produces a visually reasonable result
    // since we average within the same bit-layout (the bias from channel widths is negligible at dst scale).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int)((int64_t)y       * src_h / dst_h);
        int sy1 = (int)((int64_t)(y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int)((int64_t)x       * src_w / dst_w);
            int sx1 = (int)((int64_t)(x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint16_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint16_t p = row[xx];
                    c0 += (p >>  0) & 0xF;
                    c1 += (p >>  4) & 0xF;
                    c2 += (p >>  8) & 0xF;
                    c3 += (p >> 12) & 0xF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint16_t v = (uint16_t)(
                ((c0 / count) & 0xF)        |
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
        int sy0 = (int)((int64_t)y       * src_h / dst_h);
        int sy1 = (int)((int64_t)(y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int)((int64_t)x       * src_w / dst_w);
            int sx1 = (int)((int64_t)(x + 1) * src_w / dst_w);
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
            dst[y * dst_w + x] = (uint8_t)(sum / count);
        }
    }
}

void apply_depth_map(void) {
    float scale = g.depth_far - g.depth_near;
    float offset = g.depth_far;

    if (g.polygon_offset_fill_enabled) {
        offset += (g.polygon_offset_units * 0.0001f);
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
    if (gl_op == 0x8590 /* GL_SRC_COLOR */) return GPU_TEVOP_RGB_SRC_COLOR;
    if (gl_op == 0x8598 /* GL_ONE_MINUS_SRC_COLOR */) return GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
    if (gl_op == GL_SRC_ALPHA) return GPU_TEVOP_RGB_SRC_ALPHA;
    if (gl_op == 0x859A /* GL_ONE_MINUS_SRC_ALPHA */) return GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA;
    return GPU_TEVOP_RGB_SRC_COLOR;
}
void apply_gpu_state(void) {
    if (g.matrices_dirty) {
        if (g.fog_enabled) g.fog_dirty = 1;

        if (g.uLoc_projection >= 0) {
            C3D_Mtx adj_proj = g.proj_stack[g.proj_sp];

            float slider = osGet3DSliderState();
            if (slider > 0.0f && g.stereo_depth != 0.0f) {
                // ВАЖНО: сдвиг применяется в CLIP-space (после проекции),
                // т.е. offset измеряется в NDC, где [-1, 1] = весь экран.
                // Раньше было adj_proj * trans, что давало сдвиг в pre-projection
                // координатах (пикселях view-space) — там 0.05 = 0.05 пикселя,
                // что глазом не видно. Теперь trans * adj_proj — сдвиг в NDC.
                float shift = slider * g.stereo_depth;
                float offset = (g.current_eye == 0) ? shift : -shift;

                C3D_Mtx trans;
                Mtx_Identity(&trans);
                trans.r[0].w = offset;

                C3D_Mtx temp_proj;
                Mtx_Multiply(&temp_proj, &trans, &adj_proj);
                adj_proj = temp_proj;
            }
            // hack for PICA200 (3DS Z-range)
            adj_proj.r[2].x = adj_proj.r[2].x * 0.4999f - adj_proj.r[3].x * 0.5f;
            adj_proj.r[2].y = adj_proj.r[2].y * 0.4999f - adj_proj.r[3].y * 0.5f;
            adj_proj.r[2].z = adj_proj.r[2].z * 0.4999f - adj_proj.r[3].z * 0.5f;
            adj_proj.r[2].w = adj_proj.r[2].w * 0.4999f - adj_proj.r[3].w * 0.5f;

            C3D_Mtx tilt;
            Mtx_Identity(&tilt);
            tilt.r[0].x =  0.0f; tilt.r[0].y = 1.0f;
            tilt.r[1].x = -1.0f; tilt.r[1].y = 0.0f;

            C3D_Mtx final_proj;
            Mtx_Multiply(&final_proj, &tilt, &adj_proj);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection, &final_proj);
        }
        if (g.uLoc_modelview >= 0)
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_modelview, &g.mv_stack[g.mv_sp]);
        g.matrices_dirty = 0;
    }

    if (g.fog_dirty) {
        if (g.uLoc_fogparams >= 0) {
            if (g.fog_enabled) {
                // protect from div by zero (if game send same start and end)
                float safe_end = g.fog_end;
                if (fabsf(safe_end - g.fog_start) < 0.001f) {
                    safe_end = g.fog_start + 0.001f;
                }
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, g.fog_start, safe_end, g.fog_density, 1.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, g.fog_color[0], g.fog_color[1], g.fog_color[2], g.fog_color[3]);
            } else {
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, 0.0f, 999999.0f, 0.0f, 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
        g.fog_dirty = 0;
    }

    GPU_WRITEMASK writemask = 0;
    if (g.color_mask_r) writemask |= GPU_WRITE_RED;
    if (g.color_mask_g) writemask |= GPU_WRITE_GREEN;
    if (g.color_mask_b) writemask |= GPU_WRITE_BLUE;
    if (g.color_mask_a) writemask |= GPU_WRITE_ALPHA;
    if (g.depth_mask && g.depth_test_enabled) writemask |= GPU_WRITE_DEPTH;
    C3D_DepthTest(g.depth_test_enabled, gl_to_gpu_testfunc(g.depth_func), writemask);

    C3D_AlphaTest(g.alpha_test_enabled, gl_to_gpu_testfunc(g.alpha_func), (u8)(g.alpha_ref * 255.0f));

    if (g.blend_enabled) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, gl_to_gpu_blendfactor(g.blend_src), gl_to_gpu_blendfactor(g.blend_dst), gl_to_gpu_blendfactor(g.blend_src), gl_to_gpu_blendfactor(g.blend_dst));
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }

    if (g.cull_face_enabled) {
        GPU_CULLMODE cull;
        if (g.cull_face_mode == GL_FRONT) cull = (g.front_face == GL_CCW) ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
        else cull = (g.front_face == GL_CCW) ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
        C3D_CullFace(cull);
    } else {
        C3D_CullFace(GPU_CULL_NONE);
    }

    if (g.scissor_test_enabled) {
        C3D_SetScissor(GPU_SCISSOR_NORMAL, g.scissor_y, g.scissor_x, g.scissor_y + g.scissor_h, g.scissor_x + g.scissor_w);
    } else {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    }

    int current_tex_state = 0;
    for (int i = 0; i < 3; i++) {
        if (g.texture_2d_enabled_unit[i] && g.bound_texture[i] > 0) {
            current_tex_state |= (1 << i);
        }
    }

    if (g.tev_dirty || g.last_tex_state != current_tex_state) {
        int tev_stage = 0;

        for (int unit = 0; unit < 3; unit++) {
            if (!(current_tex_state & (1 << unit))) continue;

            C3D_TexEnv *env = C3D_GetTexEnv(tev_stage);
            C3D_TexEnvInit(env);

            GPU_TEVSRC tex_src = (unit == 0) ? GPU_TEXTURE0 : ((unit == 1) ? GPU_TEXTURE1 : GPU_TEXTURE2);
            GPU_TEVSRC prev_src = (tev_stage == 0) ? GPU_PRIMARY_COLOR : GPU_PREVIOUS;

            if (g.tex_env_mode[unit] == GL_COMBINE) {
                // use our C helpers for translation
                GPU_TEVSRC s0 = get_tev_src(g.tex_env_src0_rgb[unit], tex_src, prev_src);
                GPU_TEVSRC s1 = get_tev_src(g.tex_env_src1_rgb[unit], tex_src, prev_src);
                GPU_TEVSRC s2 = get_tev_src(g.tex_env_src2_rgb[unit], tex_src, prev_src);

                int op0 = get_tev_op_rgb(g.tex_env_operand0_rgb[unit]);
                int op1 = get_tev_op_rgb(g.tex_env_operand1_rgb[unit]);
                int op2 = get_tev_op_rgb(g.tex_env_operand2_rgb[unit]);

                switch(g.tex_env_combine_rgb[unit]) {
                    case GL_DOT3_RGBA_ARB:
                        C3D_TexEnvSrc(env, C3D_Both, s0, s1, (GPU_TEVSRC)0);
                        C3D_TexEnvOpRgb(env, op0, op1, 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_DOT3_RGBA);
                        break;
                    case GL_REPLACE:
                        C3D_TexEnvSrc(env, C3D_Both, s0, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                        C3D_TexEnvOpRgb(env, op0, 0, 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
                        break;
                    case GL_ADD:
                        C3D_TexEnvSrc(env, C3D_Both, s0, s1, (GPU_TEVSRC)0);
                        C3D_TexEnvOpRgb(env, op0, op1, 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_ADD);
                        break;
                    case GL_INTERPOLATE:
                        C3D_TexEnvSrc(env, C3D_Both, s0, s1, s2);
                        C3D_TexEnvOpRgb(env, op0, op1, op2);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_INTERPOLATE);
                        break;
                    case GL_MODULATE:
                    default:
                        C3D_TexEnvSrc(env, C3D_Both, s0, s1, (GPU_TEVSRC)0);
                        C3D_TexEnvOpRgb(env, op0, op1, 0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
                        break;
                }
            } else {
                // standard FFP (GL_MODULATE / GL_REPLACE)
                switch (g.tex_env_mode[unit]) {
                    case GL_REPLACE:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
                        break;
                    case GL_ADD:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, prev_src, (GPU_TEVSRC)0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_ADD);
                        break;
                    case GL_DECAL:
                        C3D_TexEnvSrc(env, C3D_RGB, prev_src, tex_src, tex_src);
                        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
                        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
                        C3D_TexEnvSrc(env, C3D_Alpha, prev_src, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
                        break;
                    case GL_MODULATE:
                    default:
                        C3D_TexEnvSrc(env, C3D_Both, tex_src, prev_src, (GPU_TEVSRC)0);
                        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
                        break;
                }
            }
            tev_stage++;
        }

        // stub if textures disabled
        if (tev_stage == 0) {
            C3D_TexEnv *env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            tev_stage++;
        }

        // disable garbage stages (2-5), pass color through
        for (int i = tev_stage; i < 6; i++) {
            C3D_TexEnv *env = C3D_GetTexEnv(i);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }

        g.last_tex_state = current_tex_state;
        g.tev_dirty = 0;
    }

    for (int unit = 0; unit < 3; unit++) {
        if ((current_tex_state & (1 << unit)) && g.bound_texture[unit] < NOVA_MAX_TEXTURES) {
            TexSlot *slot = &g.textures[g.bound_texture[unit]];
            if (slot->allocated) {
                C3D_TexBind(unit, &slot->tex);
            }
        }
    }
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

void nova_hardware_swizzle(C3D_Tex* tex, const void* linear_pixels, int width, int height, GPU_TEXCOLOR format) {
    if (!linear_pixels || !tex->data) return;

    u32 transfer_flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    u32 in_fmt = 0, out_fmt = 0;
    switch (format) {
        case GPU_RGBA8:  in_fmt = GX_TRANSFER_FMT_RGBA8;  out_fmt = GX_TRANSFER_FMT_RGBA8; break;
        case GPU_RGB8:   in_fmt = GX_TRANSFER_FMT_RGB8;   out_fmt = GX_TRANSFER_FMT_RGB8; break;
        case GPU_RGB565: in_fmt = GX_TRANSFER_FMT_RGB565; out_fmt = GX_TRANSFER_FMT_RGB565; break;
        case GPU_RGBA5551:in_fmt= GX_TRANSFER_FMT_RGB5A1; out_fmt = GX_TRANSFER_FMT_RGB5A1; break;
        case GPU_RGBA4:  in_fmt = GX_TRANSFER_FMT_RGBA4;  out_fmt = GX_TRANSFER_FMT_RGBA4; break;
        default: printf("Unsupported format: %x", format); return;
    }

    transfer_flags |= GX_TRANSFER_IN_FORMAT(in_fmt) | GX_TRANSFER_OUT_FORMAT(out_fmt);

    GSPGPU_FlushDataCache(linear_pixels, width * height * gpu_texfmt_bpp(format));

    C3D_SyncDisplayTransfer((u32*)linear_pixels, GX_BUFFER_DIM(width, height),
                            (u32*)tex->data, GX_BUFFER_DIM(tex->width, tex->height), transfer_flags);
}
