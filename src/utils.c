//
// Created by efimandreev0 on 05.04.2026.
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

static void apply_gpu_state(void) {
    if (g.matrices_dirty) {
        if (g.uLoc_projection >= 0) {
            C3D_Mtx adj_proj = g.proj_stack[g.proj_sp];
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

    if (g.uLoc_fogparams >= 0) {
        float range = g.fog_end - g.fog_start;
        if (range == 0.0f) range = 1.0f;
        C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, g.fog_start, g.fog_end, 1.0f / range, g.fog_enabled ? 1.0f : 0.0f);
    }

    GPU_WRITEMASK writemask = GPU_WRITE_COLOR;
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

    if (g.fog_enabled && g.fog_dirty) {
        u32 fc = ((u8)(g.fog_color[0]*255) << 24) | ((u8)(g.fog_color[1]*255) << 16) | ((u8)(g.fog_color[2]*255) << 8) | ((u8)(g.fog_color[3]*255));
        C3D_FogColor(fc);
        if (g.fog_mode == GL_LINEAR) {
            float range = g.fog_end - g.fog_start;
            if (range < 0.001f) range = 0.001f;
            for (int i = 0; i < 128; i++) {
                float depth = (float)i / 127.0f * g.fog_end;
                float f = (depth <= g.fog_start) ? 0.0f : (depth >= g.fog_end) ? 1.0f : (depth - g.fog_start) / range;
                g.fog_lut.data[i] = (u32)(clampf(f, 0.0f, 1.0f) * 0x7FF) & 0xFFF;
            }
        }
        else if (g.fog_mode == GL_EXP) FogLut_Exp(&g.fog_lut, g.fog_density, 0.0f, g.fog_end, 1.0f);
        else FogLut_Exp(&g.fog_lut, g.fog_density * g.fog_density, 0.0f, g.fog_end, 2.0f);
        C3D_FogGasMode(true, false, false);
        C3D_FogLutBind(&g.fog_lut);
        g.fog_dirty = 0;
    } else if (!g.fog_enabled) {
        C3D_FogGasMode(false, false, false);
    }

    int current_tex_state = (g.texture_2d_enabled && g.bound_texture > 0);
    if (g.tev_dirty || g.last_tex_state != current_tex_state) {
        C3D_TexEnv *env0 = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env0);
        if (current_tex_state) {
            switch (g.tex_env_mode) {
                case GL_REPLACE:
                    C3D_TexEnvSrc(env0, C3D_Both, GPU_TEXTURE0, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                    C3D_TexEnvFunc(env0, C3D_Both, GPU_REPLACE);
                    break;
                case GL_DECAL:
                    C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_TEXTURE0);
                    C3D_TexEnvFunc(env0, C3D_RGB, GPU_INTERPOLATE);
                    C3D_TexEnvSrc(env0, C3D_Alpha, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                    C3D_TexEnvFunc(env0, C3D_Alpha, GPU_REPLACE);
                    break;
                case GL_ADD:
                    C3D_TexEnvSrc(env0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0);
                    C3D_TexEnvFunc(env0, C3D_Both, GPU_ADD);
                    break;
                case GL_MODULATE:
                default:
                    C3D_TexEnvSrc(env0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0);
                    C3D_TexEnvFunc(env0, C3D_Both, GPU_MODULATE);
                    break;
            }
        } else {
            C3D_TexEnvSrc(env0, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env0, C3D_Both, GPU_REPLACE);
        }
        for (int i = 1; i < 6; i++) {
            C3D_TexEnv *env = C3D_GetTexEnv(i);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }
        g.last_tex_state = current_tex_state;
        g.tev_dirty = 0;
    }

    if (current_tex_state && g.bound_texture < NOVA_MAX_TEXTURES && g.textures[g.bound_texture].allocated) {
        C3D_TexBind(0, &g.textures[g.bound_texture].tex);
    }
}

static inline void cleanup_vbo_stream(void) {
#ifdef NOVA_VBO_STREAM
    if (g.bound_array_buffer) {
        VBOSlot *slot = &g.vbos[g.bound_array_buffer];
        if (slot->is_stream && slot->data) {
            linearFree(slot->data);
            slot->data = NULL;
            slot->allocated = 0;
            slot->size = 0;
            slot->capacity = 0;
        }
    }
#endif
}

static inline void draw_emulated_quads(int count) {
    int num_quads = count / 4;
    int idx_count = num_quads * 6;
    int idx_bytes = idx_count * 2;

    if (idx_bytes > g.index_buf_size) return;

    g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
    if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
        C3D_FrameSplit(0);
        g.index_buf_offset = 0;
    }

    uint16_t *idx = (uint16_t*)linear_alloc_ring(g.index_buf, &g.index_buf_offset, idx_bytes, g.index_buf_size);

    for (int q = 0; q < num_quads; q++) {
        uint16_t base = q * 4;
        idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
        idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
    }

    GSPGPU_FlushDataCache(idx, idx_bytes);
    C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
}