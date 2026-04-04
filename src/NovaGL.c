/*
 * NovaGL.c - OpenGL ES 1.1 -> Citro3D Translation Layer Implementation
 */

#include "NovaGL.h"
#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */
#define GL2C3D_MAX_TEXTURES     2048
#define GL2C3D_MAX_VBOS         32768
#define GL2C3D_MATRIX_STACK     32
#define GL2C3D_DISPLAY_LISTS    512
#define GL2C3D_DL_MAX_OPS       64
#define GL2C3D_CMD_BUF_SIZE     (1024 * 1024)
#define GL2C3D_SCREEN_W         400
#define GL2C3D_SCREEN_H         240

static inline unsigned int next_pow2(unsigned int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

typedef struct {
    C3D_Tex     tex;
    int         allocated;
    int         width, height;
    int         pot_w, pot_h;
    GPU_TEXCOLOR fmt;
    int         min_filter;
    int         mag_filter;
    int         wrap_s;
    int         wrap_t;
} TexSlot;

typedef struct {
    void   *data;
    int     size;
    int     allocated;
} VBOSlot;

typedef enum {
    DL_OP_TRANSLATE,
    DL_OP_COLOR3F,
    DL_OP_NONE,
} DLOpType;

typedef struct {
    DLOpType type;
    float    args[4];
} DLOp;

typedef struct {
    DLOp ops[GL2C3D_DL_MAX_OPS];
    int  count;
    int  used;
} DisplayList;

static struct {
    C3D_RenderTarget *render_target_top;
    C3D_RenderTarget *render_target_bot;
    C3D_RenderTarget *current_target;
    DVLB_s           *shader_dvlb;
    shaderProgram_s   shader_program;
    int               uLoc_projection;
    int               uLoc_modelview;
    int               uLoc_fogparams;
    C3D_AttrInfo      attr_info;

    int        matrix_mode;
    C3D_Mtx    proj_stack[GL2C3D_MATRIX_STACK];
    int        proj_sp;
    C3D_Mtx    mv_stack[GL2C3D_MATRIX_STACK];
    int        mv_sp;
    C3D_Mtx    tex_stack[GL2C3D_MATRIX_STACK];
    int        tex_sp;
    int        matrices_dirty;

    float      cur_color[4];

    TexSlot    textures[GL2C3D_MAX_TEXTURES];
    GLuint     bound_texture;
    int        tex_next_id;
    int        texture_2d_enabled;

    VBOSlot    vbos[GL2C3D_MAX_VBOS];
    GLuint     bound_array_buffer;
    GLuint     bound_element_array_buffer;
    int        vbo_next_id;

    struct {
        int     enabled;
        GLint   size;
        GLenum  type;
        GLsizei stride;
        const void *pointer;
        GLuint  vbo_id;
    } va_vertex, va_texcoord, va_color, va_normal;

    int        depth_test_enabled;
    GLenum     depth_func;
    GLboolean  depth_mask;
    int        blend_enabled;
    GLenum     blend_src, blend_dst;
    int        alpha_test_enabled;
    GLenum     alpha_func;
    float      alpha_ref;
    int        cull_face_enabled;
    GLenum     cull_face_mode;
    GLenum     front_face;

    int        scissor_test_enabled;
    GLint      scissor_x, scissor_y;
    GLsizei    scissor_w, scissor_h;

    int        fog_enabled;
    GLenum     fog_mode;
    float      fog_start, fog_end, fog_density;
    float      fog_color[4];
    C3D_FogLut fog_lut;
    int        fog_dirty;

    float      clear_r, clear_g, clear_b, clear_a;
    float      clear_depth;
    GLboolean  color_mask_r, color_mask_g, color_mask_b, color_mask_a;
    float      polygon_offset_factor, polygon_offset_units;

    GLint      vp_x, vp_y;
    GLsizei    vp_w, vp_h;

    DisplayList dl_store[GL2C3D_DISPLAY_LISTS];
    int         dl_recording;
    GLuint      dl_next_base;

    GLenum     last_error;
    int        initialized;
    void      *client_array_buf;
    int        client_array_buf_size;
    int        client_array_buf_offset;

    void      *index_buf;
    int        index_buf_size;
    int        index_buf_offset;

    int        tev_dirty;
    int        last_tex_state;
    GLint      tex_env_mode;

    int polygon_offset_fill_enabled;
    GLfloat    depth_near;
    GLfloat    depth_far;
} g;

#include "../.dkp-generated/shaders/NovaGL_shader_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static void dl_record_translate(float x, float y, float z);
static void dl_record_color3f(float r, float g_, float b);

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
    uint32_t *out = (uint32_t*)linearAlloc(w * h * 4);
    if (!out) return NULL;
    for (int i = 0; i < w * h; i++) {
        out[i] = (0xFF << 24) | (rgb[i*3+2] << 16) | (rgb[i*3+1] << 8) | rgb[i*3+0];
    }
    return out;
}

extern const unsigned char NovaGL_shader_shbin[];
static const void* _force_shader_ref = NovaGL_shader_shbin;

static void apply_depth_map(void) {
    float scale = g.depth_far - g.depth_near;
    float offset = g.depth_far;

    if (g.polygon_offset_fill_enabled) {
        offset += (g.polygon_offset_units * 0.0001f);
    }

    C3D_DepthMap(true, scale, offset);
}

void nova_init(void) {
    memset(&g, 0, sizeof(g));

    C3D_Init(GL2C3D_CMD_BUF_SIZE);

    g.render_target_top = C3D_RenderTargetCreate(GL2C3D_SCREEN_H, GL2C3D_SCREEN_W,
                                                  GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    g.current_target = g.render_target_top;

    g.render_target_bot = C3D_RenderTargetCreate(240, 320,
                                                  GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_bot, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    g.shader_dvlb = DVLB_ParseFile((u32*)NovaGL_shader_shbin, NovaGL_shader_shbin_size);

    if (g.shader_dvlb) {
        shaderProgramInit(&g.shader_program);
        shaderProgramSetVsh(&g.shader_program, &g.shader_dvlb->DVLE[0]);
        C3D_BindProgram(&g.shader_program);

        g.uLoc_projection = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "projection");
        g.uLoc_modelview  = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "modelview");
        g.uLoc_fogparams  = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "fogparams");
    }

    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);
    C3D_SetAttrInfo(&g.attr_info);

    g.depth_near = 0.0f;
    g.depth_far = 1.0f;
    g.polygon_offset_fill_enabled = 0;
    apply_depth_map();

    g.client_array_buf_size = 2 * 1024 * 1024;
    g.client_array_buf = linearAlloc(g.client_array_buf_size);
    g.index_buf_size = 256 * 1024;
    g.index_buf = linearAlloc(g.index_buf_size);

    g.matrix_mode = GL_MODELVIEW;
    Mtx_Identity(&g.proj_stack[0]);
    Mtx_Identity(&g.mv_stack[0]);
    Mtx_Identity(&g.tex_stack[0]);
    g.proj_sp = 0;
    g.mv_sp = 0;
    g.tex_sp = 0;
    g.matrices_dirty = 1;

    g.cur_color[0] = 1.0f; g.cur_color[1] = 1.0f;
    g.cur_color[2] = 1.0f; g.cur_color[3] = 1.0f;

    g.depth_test_enabled = 1;
    g.depth_func = GL_LEQUAL;
    g.depth_mask = GL_TRUE;
    g.clear_depth = 1.0f;
    g.blend_src = GL_ONE;
    g.blend_dst = GL_ZERO;
    g.alpha_func = GL_ALWAYS;
    g.alpha_ref = 0.0f;
    g.cull_face_mode = GL_BACK;
    g.front_face = GL_CCW;

    g.fog_mode = GL_LINEAR;
    g.fog_start = 0.0f; g.fog_end = 1.0f; g.fog_density = 1.0f;
    g.fog_color[0] = g.fog_color[1] = g.fog_color[2] = 0.0f; g.fog_color[3] = 1.0f;

    g.vp_x = 0; g.vp_y = 0;
    g.vp_w = GL2C3D_SCREEN_W; g.vp_h = GL2C3D_SCREEN_H;

    g.color_mask_r = g.color_mask_g = g.color_mask_b = g.color_mask_a = GL_TRUE;

    g.tex_next_id = 1;
    g.vbo_next_id = 1;
    g.dl_recording = -1;
    g.dl_next_base = 1;
    g.texture_2d_enabled = 0;
    g.tev_dirty = 1;
    g.initialized = 1;
}

void nova_fini(void) {
    if (!g.initialized) return;
    if (g.client_array_buf) linearFree(g.client_array_buf);
    if (g.index_buf) linearFree(g.index_buf);
    for (int i = 0; i < GL2C3D_MAX_TEXTURES; i++) {
        if (g.textures[i].allocated) C3D_TexDelete(&g.textures[i].tex);
    }
    for (int i = 0; i < GL2C3D_MAX_VBOS; i++) {
        if (g.vbos[i].allocated && g.vbos[i].data) linearFree(g.vbos[i].data);
    }
    if (g.shader_dvlb) {
        shaderProgramFree(&g.shader_program);
        DVLB_Free(g.shader_dvlb);
    }
    C3D_RenderTargetDelete(g.render_target_top);
    C3D_RenderTargetDelete(g.render_target_bot);
    C3D_Fini();
    g.initialized = 0;
}

void nova_frame_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;
}
void nova_frame_end(void) { C3D_FrameEnd(0); }
void nova_set_render_target(int is_right_eye) {
    C3D_FrameDrawOn(is_right_eye ? g.render_target_bot : g.render_target_top);
    g.current_target = is_right_eye ? g.render_target_bot : g.render_target_top;
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
        float end_val = g.fog_end > 0.001f ? g.fog_end : 0.001f;
        C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, 1.0f / end_val, 0.0f, 0.0f, 0.0f);
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

    if (g.fog_dirty) {
        float data[256];
        float range = g.fog_end - g.fog_start;
        if (range < 0.001f) range = 0.001f;

        for (int i = 0; i <= 128; i++) {
            float x = (float)i / 128.0f; // Нормализованная координата (0.0 -> 1.0)
            float depth = x * g.fog_end; // Физическая дистанция
            float f = 1.0f;              // 1.0 = нет тумана, 0.0 = сплошной туман

            // Математика тумана по стандарту OpenGL
            if (g.fog_mode == GL_LINEAR) {
                f = (g.fog_end - depth) / range;
            } else if (g.fog_mode == GL_EXP) {
                f = expf(-g.fog_density * depth);
            } else { // GL_EXP2
                f = expf(-(g.fog_density * depth) * (g.fog_density * depth));
            }

            f = clampf(f, 0.0f, 1.0f);

            // Сохраняем значения и разницу (как того требует Citro3D)
            if (i < 128)
                data[i] = f;
            if (i > 0)
                data[i + 127] = f - data[i - 1];
        }

        // Запекаем массив через родную функцию Citro3D
        FogLut_FromArray(&g.fog_lut, data);
        g.fog_dirty = 0; // Сбрасываем флаг обновления
    }

    // 3. Аппаратно включаем или выключаем туман в текущем кадре
    if (g.fog_enabled) {
        u32 fc = ((u8)(g.fog_color[0]*255) << 24) |
                 ((u8)(g.fog_color[1]*255) << 16) |
                 ((u8)(g.fog_color[2]*255) << 8)  |
                 ((u8)(g.fog_color[3]*255));

        C3D_FogColor(fc);
        C3D_FogLutBind(&g.fog_lut);
        C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
    } else {
        C3D_FogLutBind(NULL);
        C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
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

    if (current_tex_state && g.bound_texture < GL2C3D_MAX_TEXTURES && g.textures[g.bound_texture].allocated) {
        C3D_TexBind(0, &g.textures[g.bound_texture].tex);
    }
}

GLenum glGetError(void) { GLenum e = g.last_error; g.last_error = GL_NO_ERROR; return e; }

void glClearColor(GLclampf r, GLclampf g_, GLclampf b, GLclampf a) {
    g.clear_r = clampf(r, 0.0f, 1.0f); g.clear_g = clampf(g_, 0.0f, 1.0f);
    g.clear_b = clampf(b, 0.0f, 1.0f); g.clear_a = clampf(a, 0.0f, 1.0f);
}

void glClearDepthf(GLclampf depth) { g.clear_depth = clampf(depth, 0.0f, 1.0f); }

void glClear(GLbitfield mask) {
    C3D_ClearBits bits = 0;
    u32 color = 0;
    u32 depth = 0;

    if (mask & GL_COLOR_BUFFER_BIT) {
        bits |= C3D_CLEAR_COLOR;
        color = ((u32)(g.clear_r * 255.0f) << 24) |
                ((u32)(g.clear_g * 255.0f) << 16) |
                ((u32)(g.clear_b * 255.0f) << 8)  |
                ((u32)(g.clear_a * 255.0f) << 0);
    }

    if (mask & GL_DEPTH_BUFFER_BIT) {
        bits |= C3D_CLEAR_DEPTH;
        depth = (u32)(g.clear_depth * 0xFFFFFF);
    }

    if (bits && g.current_target) {
        C3D_RenderTargetClear(g.current_target, bits, color, depth);
    }
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    g.vp_x = x; g.vp_y = y; g.vp_w = width; g.vp_h = height;
    C3D_SetViewport(y, x, height, width);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    g.scissor_x = x; g.scissor_y = y; g.scissor_w = width; g.scissor_h = height;
}
void glEnable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    g.depth_test_enabled = 1; break;
        case GL_BLEND:         g.blend_enabled = 1; break;
        case GL_ALPHA_TEST:    g.alpha_test_enabled = 1; break;
        case GL_CULL_FACE:     g.cull_face_enabled = 1; break;
        case GL_TEXTURE_2D:    g.tev_dirty = 1; g.texture_2d_enabled = 1; break;
        case GL_SCISSOR_TEST:  g.scissor_test_enabled = 1; break;

            //TODO: fix this fcking fog.
        case GL_FOG:           /*g.fog_enabled = 1; g.fog_dirty = 1;*/ break;

        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 1;
            apply_depth_map();
            break;
        default: break;
    }
}

void glDisable(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    g.depth_test_enabled = 0; break;
        case GL_BLEND:         g.blend_enabled = 0; break;
        case GL_ALPHA_TEST:    g.alpha_test_enabled = 0; break;
        case GL_CULL_FACE:     g.cull_face_enabled = 0; break;
        case GL_TEXTURE_2D:    g.tev_dirty = 1; g.texture_2d_enabled = 0; break;
        case GL_SCISSOR_TEST:  g.scissor_test_enabled = 0; break;

        case GL_FOG:           g.fog_enabled = 0; break;

        case GL_POLYGON_OFFSET_FILL:
            g.polygon_offset_fill_enabled = 0;
            apply_depth_map();
            break;
        default: break;
    }
}
void glDepthFunc(GLenum func) { g.depth_func = func; }
void glDepthMask(GLboolean flag) { g.depth_mask = flag; }

void glDepthRangef(GLclampf near_val, GLclampf far_val) {
    g.depth_near = clampf(near_val, 0.0f, 1.0f);
    g.depth_far  = clampf(far_val, 0.0f, 1.0f);
    apply_depth_map();
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) { g.blend_src = sfactor; g.blend_dst = dfactor; }
void glAlphaFunc(GLenum func, GLclampf ref) { g.alpha_func = func; g.alpha_ref = ref; }
void glCullFace(GLenum mode) { g.cull_face_mode = mode; }
void glFrontFace(GLenum mode) { g.front_face = mode; }
void glShadeModel(GLenum mode) { (void)mode; }
void glPolygonOffset(GLfloat factor, GLfloat units) {
    g.polygon_offset_factor = factor;
    g.polygon_offset_units = units;
    apply_depth_map();
}
void glLineWidth(GLfloat width) { (void)width; }
void glPolygonMode(GLenum face, GLenum mode) { (void)face; (void)mode; }

void glColorMask(GLboolean r, GLboolean g_, GLboolean b, GLboolean a) {
    g.color_mask_r = r; g.color_mask_g = g_; g.color_mask_b = b; g.color_mask_a = a;
}

void glColor4f(GLfloat r, GLfloat g_, GLfloat b, GLfloat a) {
    g.cur_color[0] = r; g.cur_color[1] = g_; g.cur_color[2] = b; g.cur_color[3] = a;
}

void glColor3f(GLfloat r, GLfloat g_, GLfloat b) {
    if (g.dl_recording >= 0) { dl_record_color3f(r, g_, b); return; }
    g.cur_color[0] = r; g.cur_color[1] = g_; g.cur_color[2] = b; g.cur_color[3] = 1.0f;
}

void glColor4ub(GLubyte r, GLubyte g_, GLubyte b, GLubyte a) {
    g.cur_color[0] = r / 255.0f; g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f; g.cur_color[3] = a / 255.0f;
}

void glColor3ub(GLubyte r, GLubyte g_, GLubyte b) {
    g.cur_color[0] = r / 255.0f; g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f; g.cur_color[3] = 1.0f;
}

void glFogf(GLenum pname, GLfloat param) {
    switch (pname) {
        case GL_FOG_MODE:    if (g.fog_mode != param) { g.fog_mode = param; g.fog_dirty = 1; } break;
        case GL_FOG_START:   if (g.fog_start != param) { g.fog_start = param; g.fog_dirty = 1; } break;
        case GL_FOG_END:     if (g.fog_end != param) { g.fog_end = param; g.fog_dirty = 1; } break;
        case GL_FOG_DENSITY: if (g.fog_density != param) { g.fog_density = param; g.fog_dirty = 1; } break;
        default: break;
    }
}
void glFogfv(GLenum pname, const GLfloat *params) {
    if (pname == GL_FOG_COLOR && params) {
        g.fog_color[0] = params[0]; g.fog_color[1] = params[1]; g.fog_color[2] = params[2]; g.fog_color[3] = params[3]; g.fog_dirty = 1;
    } else if (params) { glFogf(pname, params[0]); }
}
void glFogx(GLenum pname, GLfixed param) { glFogf(pname, (float)param / 65536.0f); }
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) { (void)nx; (void)ny; (void)nz; }

void glMatrixMode(GLenum mode) { g.matrix_mode = mode; }
void glLoadIdentity(void) { Mtx_Identity(cur_mtx()); g.matrices_dirty = 1; }

void glPushMatrix(void) {
    int *sp = cur_sp();
    C3D_Mtx *stack = cur_stack();
    if (*sp < GL2C3D_MATRIX_STACK - 1) {
        Mtx_Copy(&stack[*sp + 1], &stack[*sp]);
        (*sp)++;
    }
}

void glPopMatrix(void) {
    int *sp = cur_sp();
    if (*sp > 0) (*sp)--;
    g.matrices_dirty = 1;
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (g.dl_recording >= 0) { dl_record_translate(x, y, z); return; }
    C3D_Mtx tmp; Mtx_Identity(&tmp);
    tmp.r[0].w = x; tmp.r[1].w = y; tmp.r[2].w = z;
    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &tmp);
    Mtx_Copy(cur_mtx(), &result);
    g.matrices_dirty = 1;
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float rad = angle * M_PI / 180.0f;
    float len = sqrtf(x*x + y*y + z*z);
    if (len < 0.0001f) return;
    x /= len; y /= len; z /= len;
    C3D_Mtx rot;
    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        Mtx_Identity(&rot); float c = cosf(rad), s = sinf(rad);
        rot.r[1].y = c;  rot.r[1].z = -s; rot.r[2].y = s;  rot.r[2].z = c;
    } else if (x == 0.0f && y == 1.0f && z == 0.0f) {
        Mtx_Identity(&rot); float c = cosf(rad), s = sinf(rad);
        rot.r[0].x = c;  rot.r[0].z = s; rot.r[2].x = -s; rot.r[2].z = c;
    } else if (x == 0.0f && y == 0.0f && z == 1.0f) {
        Mtx_Identity(&rot); float c = cosf(rad), s = sinf(rad);
        rot.r[0].x = c;  rot.r[0].y = -s; rot.r[1].x = s;  rot.r[1].y = c;
    } else {
        float c = cosf(rad), s = sinf(rad), ic = 1.0f - c;
        Mtx_Identity(&rot);
        rot.r[0].x = x*x*ic + c;     rot.r[0].y = x*y*ic - z*s;   rot.r[0].z = x*z*ic + y*s;
        rot.r[1].x = y*x*ic + z*s;   rot.r[1].y = y*y*ic + c;     rot.r[1].z = y*z*ic - x*s;
        rot.r[2].x = z*x*ic - y*s;   rot.r[2].y = z*y*ic + x*s;   rot.r[2].z = z*z*ic + c;
    }
    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &rot);
    Mtx_Copy(cur_mtx(), &result); g.matrices_dirty = 1;
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    C3D_Mtx tmp; Mtx_Identity(&tmp);
    tmp.r[0].x = x; tmp.r[1].y = y; tmp.r[2].z = z;
    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &tmp);
    Mtx_Copy(cur_mtx(), &result); g.matrices_dirty = 1;
}

void glMultMatrixf(const GLfloat *m) {
    C3D_Mtx tmp;
    for (int r = 0; r < 4; r++) {
        tmp.r[r].x = m[0*4 + r]; tmp.r[r].y = m[1*4 + r];
        tmp.r[r].z = m[2*4 + r]; tmp.r[r].w = m[3*4 + r];
    }
    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &tmp);
    Mtx_Copy(cur_mtx(), &result); g.matrices_dirty = 1;
}

void glLoadMatrixf(const GLfloat *m) {
    C3D_Mtx *dst = cur_mtx();
    for (int r = 0; r < 4; r++) {
        dst->r[r].x = m[0*4 + r]; dst->r[r].y = m[1*4 + r];
        dst->r[r].z = m[2*4 + r]; dst->r[r].w = m[3*4 + r];
    }
    g.matrices_dirty = 1;
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    C3D_Mtx ortho; Mtx_Identity(&ortho);
    ortho.r[0].x = 2.0f / (right - left); ortho.r[0].w = -(right + left) / (right - left);
    ortho.r[1].y = 2.0f / (top - bottom); ortho.r[1].w = -(top + bottom) / (top - bottom);

    ortho.r[2].z = -2.0f / (far_val - near_val);
    ortho.r[2].w = -(far_val + near_val) / (far_val - near_val);

    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &ortho);
    Mtx_Copy(cur_mtx(), &result); g.matrices_dirty = 1;
}

void glGetFloatv(GLenum pname, GLfloat *params) {
    C3D_Mtx *src = NULL;
    if (pname == GL_MODELVIEW_MATRIX) src = &g.mv_stack[g.mv_sp];
    else if (pname == GL_PROJECTION_MATRIX) src = &g.proj_stack[g.proj_sp];
    else if (pname == GL_TEXTURE_MATRIX) src = &g.tex_stack[g.tex_sp];
    else { for (int i = 0; i < 16; i++) params[i] = 0.0f; return; }
    for (int r = 0; r < 4; r++) {
        params[0*4 + r] = src->r[r].x; params[1*4 + r] = src->r[r].y;
        params[2*4 + r] = src->r[r].z; params[3*4 + r] = src->r[r].w;
    }
}

void glGetIntegerv(GLenum pname, GLint *params) {
    if (pname == GL_VIEWPORT) {
        params[0] = g.vp_x; params[1] = g.vp_y; params[2] = g.vp_w; params[3] = g.vp_h;
    } else if (pname == GL_MAX_TEXTURE_SIZE) params[0] = 1024;
    else params[0] = 0;
}

const GLubyte* glGetString(GLenum name) {
    if (name == GL_VENDOR) return (const GLubyte*)"gl2citro3d";
    if (name == GL_RENDERER) return (const GLubyte*)"PICA200 (3DS)";
    if (name == GL_VERSION) return (const GLubyte*)"OpenGL ES-CM 1.1 gl2citro3d";
    if (name == GL_EXTENSIONS) return (const GLubyte*)"GL_OES_vertex_buffer_object GL_OES_matrix_palette";
    return (const GLubyte*)"";
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint start = g.tex_next_id;
        while (g.textures[g.tex_next_id].allocated) {
            g.tex_next_id++;
            if (g.tex_next_id >= GL2C3D_MAX_TEXTURES) g.tex_next_id = 1;
            if (g.tex_next_id == start) { g.last_error = GL_OUT_OF_MEMORY; textures[i] = 0; break; }
        }
        textures[i] = g.tex_next_id;
        g.tex_next_id++;
        if (g.tex_next_id >= GL2C3D_MAX_TEXTURES) g.tex_next_id = 1;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id > 0 && id < GL2C3D_MAX_TEXTURES && g.textures[id].allocated) {
            C3D_TexDelete(&g.textures[id].tex);
            g.textures[id].allocated = 0;
            if (g.bound_texture == id) g.bound_texture = 0;
        }
    }
}

void glBindTexture(GLenum target, GLuint texture) { (void)target; g.bound_texture = texture; }

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)border; (void)internalformat;
    if (g.bound_texture == 0 || g.bound_texture >= GL2C3D_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[g.bound_texture];

    if (slot->allocated) { C3D_TexDelete(&slot->tex); slot->allocated = 0; }
    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    int pot_w = next_pow2(width); int pot_h = next_pow2(height);
    if (pot_w < 8) pot_w = 8; if (pot_h < 8) pot_h = 8;
    if (pot_w > 1024) pot_w = 1024; if (pot_h > 1024) pot_h = 1024;

    if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt)) { g.last_error = GL_OUT_OF_MEMORY; return; }
    slot->allocated = 1; slot->width = width; slot->height = height; slot->pot_w = pot_w; slot->pot_h = pot_h; slot->fmt = gpu_fmt;

    /* Apply stored filter/wrap params (may have been set before allocation) */
    {
        GPU_TEXTURE_FILTER_PARAM mag = (slot->mag_filter == GL_LINEAR) ? GPU_LINEAR : GPU_NEAREST;
        GPU_TEXTURE_FILTER_PARAM min_f = (slot->min_filter == GL_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_NEAREST) ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(&slot->tex, mag, min_f);
        GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
        GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
        C3D_TexSetWrap(&slot->tex, ws, wt);
    }

    if (pixels) {
        if (gpu_fmt == GPU_RGBA8) {
            uint32_t *rgba_data = NULL;
            int needs_free = 0;
            if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                rgba_data = rgb_to_rgba((const uint8_t*)pixels, width, height); needs_free = 1;
            } else { rgba_data = (uint32_t*)pixels; }

            if (rgba_data) {
                uint32_t *swizzled = (uint32_t*)linearAlloc(pot_w * pot_h * 4);
                if (swizzled) {
                    memset(swizzled, 0, pot_w * pot_h * 4);
                    swizzle_rgba8(swizzled, rgba_data, width, height, pot_w, pot_h);
                    memcpy(slot->tex.data, swizzled, pot_w * pot_h * 4);
                    C3D_TexFlush(&slot->tex);
                    linearFree(swizzled);
                }
                if (needs_free) linearFree(rgba_data);
            }
        } else {
            memcpy(slot->tex.data, pixels, width * height * gpu_texfmt_bpp(gpu_fmt));
            C3D_TexFlush(&slot->tex);
        }
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    if (g.bound_texture == 0 || g.bound_texture >= GL2C3D_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[g.bound_texture];
    if (!slot->allocated || !pixels) return;

    if (slot->fmt == GPU_RGBA8) {
        const uint32_t *src = NULL;
        uint32_t *temp_rgba = NULL;
        int needs_free = 0;
        if ((format == GL_RGBA || format == GL_RGBA8_OES) && type == GL_UNSIGNED_BYTE) { src = (const uint32_t*)pixels; }
        else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) { temp_rgba = rgb_to_rgba((const uint8_t*)pixels, width, height); src = temp_rgba; needs_free = 1; }
        if (!src) return;

        uint32_t *tex_data = (uint32_t*)slot->tex.data;
        int pot_w = slot->pot_w; int pot_h = slot->pot_h;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int dx = xoffset + x; int dy = yoffset + y;
                if (dx >= pot_w || dy >= pot_h) continue;
                uint32_t pixel = src[y * width + x];
                uint8_t r = (pixel >> 0) & 0xFF, gc = (pixel >> 8) & 0xFF, b = (pixel >> 16) & 0xFF, a = (pixel >> 24) & 0xFF;
                uint32_t out_pixel = ((uint32_t)r << 24) | ((uint32_t)gc << 16) | ((uint32_t)b << 8) | (uint32_t)a;
                int fy = pot_h - 1 - dy;
                int tile_x = dx >> 3; int tile_y = fy >> 3; int lx = dx & 7; int ly = fy & 7;
                int pixel_offset = (tile_y * (pot_w >> 3) + tile_x) * 64 + morton_interleave(lx, ly);
                tex_data[pixel_offset] = out_pixel;
            }
        }
        C3D_TexFlush(&slot->tex);
        if (needs_free && temp_rgba) linearFree(temp_rgba);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (g.bound_texture == 0 || g.bound_texture >= GL2C3D_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[g.bound_texture];

    /* Store params even before allocation — they'll be applied when the texture is created */
    if (pname == GL_TEXTURE_MIN_FILTER)      slot->min_filter = param;
    else if (pname == GL_TEXTURE_MAG_FILTER)  slot->mag_filter = param;
    else if (pname == GL_TEXTURE_WRAP_S)      slot->wrap_s = param;
    else if (pname == GL_TEXTURE_WRAP_T)      slot->wrap_t = param;

    if (!slot->allocated) return;

    /* Apply to the live C3D texture */
    GPU_TEXTURE_FILTER_PARAM mag = (slot->mag_filter == GL_LINEAR) ? GPU_LINEAR : GPU_NEAREST;
    GPU_TEXTURE_FILTER_PARAM min_f = (slot->min_filter == GL_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_NEAREST) ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&slot->tex, mag, min_f);

    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(&slot->tex, ws, wt);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) {
    (void)data; (void)imageSize; (void)internalformat;
    glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint start = g.vbo_next_id;
        while (g.vbos[g.vbo_next_id].allocated) {
            g.vbo_next_id++;
            if (g.vbo_next_id >= GL2C3D_MAX_VBOS) g.vbo_next_id = 1;
            if (g.vbo_next_id == start) { g.last_error = GL_OUT_OF_MEMORY; buffers[i] = 0; break; }
        }
        buffers[i] = g.vbo_next_id;
        g.vbo_next_id++;
        if (g.vbo_next_id >= GL2C3D_MAX_VBOS) g.vbo_next_id = 1;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id > 0 && (int)id < GL2C3D_MAX_VBOS && g.vbos[id].allocated) {
            if (g.vbos[id].data) linearFree(g.vbos[id].data);
            g.vbos[id].data = NULL; g.vbos[id].size = 0; g.vbos[id].allocated = 0;
            if (g.bound_array_buffer == id) g.bound_array_buffer = 0;
            if (g.bound_element_array_buffer == id) g.bound_element_array_buffer = 0;
        }
    }
}

void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) g.bound_array_buffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) g.bound_element_array_buffer = buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    (void)usage;
    GLuint bound = 0;
    if (target == GL_ARRAY_BUFFER) bound = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) bound = g.bound_element_array_buffer;

    if (bound == 0 || bound >= GL2C3D_MAX_VBOS) return;
    VBOSlot *slot = &g.vbos[bound];

    if (slot->allocated && slot->size < (int)size) {
        if (slot->data) linearFree(slot->data);
        slot->data = NULL; slot->allocated = 0;
    }

    if (!slot->allocated) {
        slot->data = linearAlloc(size);
        if (!slot->data) { g.last_error = GL_OUT_OF_MEMORY; return; }
        slot->allocated = 1;
    }
    slot->size = (int)size;

    if (data) {
        memcpy(slot->data, data, size);
        GSPGPU_FlushDataCache(slot->data, size);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    GLuint bound = 0;
    if (target == GL_ARRAY_BUFFER) bound = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) bound = g.bound_element_array_buffer;
    if (bound == 0 || (int)bound >= GL2C3D_MAX_VBOS) return;
    VBOSlot *slot = &g.vbos[bound];
    if (!slot->allocated || !slot->data || !data) return;
    if (offset + (int)size > slot->size) { g.last_error = GL_INVALID_VALUE; return; }
    memcpy((uint8_t*)slot->data + offset, data, size);
    GSPGPU_FlushDataCache((uint8_t*)slot->data + offset, size);
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_vertex.size = size; g.va_vertex.type = type; g.va_vertex.stride = stride; g.va_vertex.pointer = pointer; g.va_vertex.vbo_id = g.bound_array_buffer;
}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_texcoord.size = size; g.va_texcoord.type = type; g.va_texcoord.stride = stride; g.va_texcoord.pointer = pointer; g.va_texcoord.vbo_id = g.bound_array_buffer;
}
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_color.size = size; g.va_color.type = type; g.va_color.stride = stride; g.va_color.pointer = pointer; g.va_color.vbo_id = g.bound_array_buffer;
}
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    g.va_normal.type = type; g.va_normal.stride = stride; g.va_normal.pointer = pointer; g.va_normal.vbo_id = g.bound_array_buffer;
}

void glEnableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 1;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 1;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 1;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 1;
}

void glDisableClientState(GLenum cap) {
    if (cap == GL_VERTEX_ARRAY) g.va_vertex.enabled = 0;
    else if (cap == GL_TEXTURE_COORD_ARRAY) g.va_texcoord.enabled = 0;
    else if (cap == GL_COLOR_ARRAY) g.va_color.enabled = 0;
    else if (cap == GL_NORMAL_ARRAY) g.va_normal.enabled = 0;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count <= 0) return;
    apply_gpu_state();

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);

    // VBO fast path
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 &&
        (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id &&
        g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 &&
        (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id &&
        g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 &&
        (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        uint8_t *data = (uint8_t*)vbo->data + first * 24;
        GSPGPU_FlushDataCache(data, count * 24);
        BufInfo_Add(bufInfo, data, 24, 3, 0x210);

        if (mode == GL_QUADS) {
            int num_quads = count / 4;
            int idx_count = num_quads * 6;
            int idx_bytes = idx_count * 2;

            g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
            if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
                C3D_FrameSplit(0);
                g.index_buf_offset = 0;
            }

            uint16_t *idx = (uint16_t*)((uint8_t*)g.index_buf + g.index_buf_offset);
            for (int q = 0; q < num_quads; q++) {
                uint16_t base = q * 4;
                idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
                idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
            }
            GSPGPU_FlushDataCache(idx, idx_bytes);
            C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
            g.index_buf_offset += idx_bytes;
        } else {
            C3D_DrawArrays(gl_to_gpu_primitive(mode), 0, count);
        }
        return;
    }

    // Slow path: assemble interleaved vertices from separate arrays
    {
        int req_size = count * 24;

        g.client_array_buf_offset = (g.client_array_buf_offset + 7) & ~7;
        if (g.client_array_buf_offset + req_size > g.client_array_buf_size) {
            C3D_FrameSplit(0);
            g.client_array_buf_offset = 0;
        }

        uint8_t *dst = (uint8_t*)g.client_array_buf + g.client_array_buf_offset;
        uint8_t *dst_start = dst;
        int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
        int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
        int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

        for (int i = 0; i < count; i++) {
            if (g.va_vertex.enabled) {
                const uint8_t *src_ptr = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated) ?
                    (const uint8_t*)g.vbos[g.va_vertex.vbo_id].data + (uintptr_t)g.va_vertex.pointer : (const uint8_t*)g.va_vertex.pointer;
                if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                    float pos[3] = {0.0f, 0.0f, 0.0f};
                    read_vertex_attrib_float(pos, src_ptr + (first + i) * p_str, g.va_vertex.size, g.va_vertex.type);
                    memcpy(dst, pos, 12);
                } else { memset(dst, 0, 12); }
            } else { memset(dst, 0, 12); }

            if (g.va_texcoord.enabled) {
                const uint8_t *src_ptr = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_texcoord.vbo_id].allocated) ?
                    (const uint8_t*)g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t)g.va_texcoord.pointer : (const uint8_t*)g.va_texcoord.pointer;
                if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                    float tc[2] = {0.0f, 0.0f};
                    read_vertex_attrib_float(tc, src_ptr + (first + i) * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
                    memcpy(dst + 12, tc, 8);
                } else memset(dst + 12, 0, 8);
            } else { memset(dst + 12, 0, 8); }

            if (g.va_color.enabled) {
                const uint8_t *src_ptr = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_color.vbo_id].allocated) ?
                    (const uint8_t*)g.vbos[g.va_color.vbo_id].data + (uintptr_t)g.va_color.pointer : (const uint8_t*)g.va_color.pointer;
                if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                    if (g.va_color.type == GL_UNSIGNED_BYTE) {
                        if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + (first + i) * c_str, 3); dst[23] = 255; }
                        else memcpy(dst + 20, src_ptr + (first + i) * c_str, 4);
                    } else if (g.va_color.type == GL_FLOAT) {
                        const float *cf = (const float*)(src_ptr + (first + i) * c_str);
                        dst[20] = (uint8_t)(clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                        dst[21] = (uint8_t)(clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                        dst[22] = (uint8_t)(clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                        dst[23] = g.va_color.size >= 4 ? (uint8_t)(clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
                    } else {
                        if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + (first + i) * c_str, 3); dst[23] = 255; }
                        else memcpy(dst + 20, src_ptr + (first + i) * c_str, 4);
                    }
                } else { dst[20] = dst[21] = dst[22] = dst[23] = 255; }
            } else {
                dst[20] = (uint8_t)(g.cur_color[0] * 255.0f); dst[21] = (uint8_t)(g.cur_color[1] * 255.0f);
                dst[22] = (uint8_t)(g.cur_color[2] * 255.0f); dst[23] = (uint8_t)(g.cur_color[3] * 255.0f);
            }
            dst += 24;
        }
        GSPGPU_FlushDataCache(dst_start, req_size);
        BufInfo_Add(bufInfo, dst_start, 24, 3, 0x210);
        g.client_array_buf_offset += req_size;
    }

    if (mode == GL_QUADS) {
        int num_quads = count / 4;
        int idx_count = num_quads * 6;
        int idx_bytes = idx_count * 2;

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *idx = (uint16_t*)((uint8_t*)g.index_buf + g.index_buf_offset);
        for (int q = 0; q < num_quads; q++) {
            uint16_t base = q * 4;
            idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
            idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
        }
        GSPGPU_FlushDataCache(idx, idx_bytes);
        C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
        g.index_buf_offset += idx_bytes;
    } else {
        GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
        if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;
        C3D_DrawArrays(prim, 0, count);
    }
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    if (count <= 0) return;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE) { g.last_error = GL_INVALID_ENUM; return; }

    const uint8_t *idx_src = NULL;

    if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < GL2C3D_MAX_VBOS &&
        g.vbos[g.bound_element_array_buffer].allocated && g.vbos[g.bound_element_array_buffer].data) {
        idx_src = (const uint8_t*)g.vbos[g.bound_element_array_buffer].data + (uintptr_t)indices;
    } else {
        idx_src = (const uint8_t*)indices;
    }
    if (!idx_src) return;

    apply_gpu_state();
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);

    GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
    if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;

    // VBO fast path
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 &&
        (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id &&
        g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 &&
        (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id &&
        g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 &&
        (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        GSPGPU_FlushDataCache(vbo->data, vbo->size);
        BufInfo_Add(bufInfo, vbo->data, 24, 3, 0x210);

        int idx_bytes = count * 2;

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *dst_idx = (uint16_t*)((uint8_t*)g.index_buf + g.index_buf_offset);
        if (type == GL_UNSIGNED_SHORT) {
            memcpy(dst_idx, idx_src, count * 2);
        } else {
            for (int i = 0; i < count; i++) dst_idx[i] = idx_src[i];
        }
        GSPGPU_FlushDataCache(dst_idx, idx_bytes);
        C3D_DrawElements(prim, count, C3D_UNSIGNED_SHORT, dst_idx);
        g.index_buf_offset += idx_bytes;
        return;
    }

    // Slow path
    int req_size = count * 24;

    g.client_array_buf_offset = (g.client_array_buf_offset + 7) & ~7;
    if (g.client_array_buf_offset + req_size > g.client_array_buf_size) {
        C3D_FrameSplit(0);
        g.client_array_buf_offset = 0;
    }

    uint8_t *dst = (uint8_t*)g.client_array_buf + g.client_array_buf_offset;
    uint8_t *dst_start = dst;
    int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
    int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
    int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

    for (int i = 0; i < count; i++) {
        int src_index = (type == GL_UNSIGNED_SHORT) ? ((const uint16_t*)idx_src)[i] : ((const uint8_t*)idx_src)[i];

        if (g.va_vertex.enabled) {
            const uint8_t *src_ptr = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_vertex.vbo_id].data + (uintptr_t)g.va_vertex.pointer : (const uint8_t*)g.va_vertex.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float pos[3] = {0.0f, 0.0f, 0.0f};
                read_vertex_attrib_float(pos, src_ptr + src_index * p_str, g.va_vertex.size, g.va_vertex.type);
                memcpy(dst, pos, 12);
            } else memset(dst, 0, 12);
        } else memset(dst, 0, 12);

        if (g.va_texcoord.enabled) {
            const uint8_t *src_ptr = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_texcoord.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t)g.va_texcoord.pointer : (const uint8_t*)g.va_texcoord.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float tc[2] = {0.0f, 0.0f};
                read_vertex_attrib_float(tc, src_ptr + src_index * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
                memcpy(dst + 12, tc, 8);
            } else memset(dst + 12, 0, 8);
        } else memset(dst + 12, 0, 8);

        if (g.va_color.enabled) {
            const uint8_t *src_ptr = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < GL2C3D_MAX_VBOS && g.vbos[g.va_color.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_color.vbo_id].data + (uintptr_t)g.va_color.pointer : (const uint8_t*)g.va_color.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                if (g.va_color.type == GL_UNSIGNED_BYTE) {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + src_index * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + src_index * c_str, 4);
                } else if (g.va_color.type == GL_FLOAT) {
                    const float *cf = (const float*)(src_ptr + src_index * c_str);
                    dst[20] = (uint8_t)(clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                    dst[21] = (uint8_t)(clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                    dst[22] = (uint8_t)(clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                    dst[23] = g.va_color.size >= 4 ? (uint8_t)(clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
                } else {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + src_index * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + src_index * c_str, 4);
                }
            } else { dst[20] = dst[21] = dst[22] = dst[23] = 255; }
        } else {
            dst[20] = (uint8_t)(g.cur_color[0] * 255.0f); dst[21] = (uint8_t)(g.cur_color[1] * 255.0f);
            dst[22] = (uint8_t)(g.cur_color[2] * 255.0f); dst[23] = (uint8_t)(g.cur_color[3] * 255.0f);
        }
        dst += 24;
    }

    GSPGPU_FlushDataCache(dst_start, req_size);
    BufInfo_Add(bufInfo, dst_start, 24, 3, 0x210);
    g.client_array_buf_offset += req_size;

    if (mode == GL_QUADS) {
        int num_quads = count / 4;
        int idx_count = num_quads * 6;
        int idx_bytes = idx_count * 2;

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *idx = (uint16_t*)((uint8_t*)g.index_buf + g.index_buf_offset);
        for (int q = 0; q < num_quads; q++) {
            uint16_t base = q * 4;
            idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
            idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
        }
        GSPGPU_FlushDataCache(idx, idx_bytes);
        C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
        g.index_buf_offset += idx_bytes;
    } else {
        C3D_DrawArrays(prim, 0, count);
    }
}
GLuint glGenLists(GLsizei range) {
    GLuint base = g.dl_next_base;
    g.dl_next_base += range;
    if (g.dl_next_base >= GL2C3D_DISPLAY_LISTS) g.dl_next_base = 1;
    for (GLsizei i = 0; i < range && (base + i) < GL2C3D_DISPLAY_LISTS; i++) {
        g.dl_store[base + i].count = 0; g.dl_store[base + i].used = 1;
    }
    return base;
}

void glNewList(GLuint list, GLenum mode) {
    (void)mode;
    if (list < GL2C3D_DISPLAY_LISTS) { g.dl_recording = list; g.dl_store[list].count = 0; }
}

void glEndList(void) { g.dl_recording = -1; }

static void dl_record_translate(float x, float y, float z) {
    if (g.dl_recording >= 0 && g.dl_recording < GL2C3D_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < GL2C3D_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_TRANSLATE; op->args[0] = x; op->args[1] = y; op->args[2] = z;
        }
    }
}

static void dl_record_color3f(float r, float g_, float b) {
    if (g.dl_recording >= 0 && g.dl_recording < GL2C3D_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < GL2C3D_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR3F; op->args[0] = r; op->args[1] = g_; op->args[2] = b;
        }
    }
}

static void dl_execute(GLuint list) {
    if (list >= GL2C3D_DISPLAY_LISTS) return;
    DisplayList *dl = &g.dl_store[list];
    if (!dl->used) return;
    for (int i = 0; i < dl->count; i++) {
        DLOp *op = &dl->ops[i];
        if (op->type == DL_OP_TRANSLATE) glTranslatef(op->args[0], op->args[1], op->args[2]);
        else if (op->type == DL_OP_COLOR3F) glColor3f(op->args[0], op->args[1], op->args[2]);
    }
}

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
    for (GLsizei i = 0; i < range && (list + i) < GL2C3D_DISPLAY_LISTS; i++) {
        g.dl_store[list + i].used = 0; g.dl_store[list + i].count = 0;
    }
}

void glGenFramebuffers(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = i + 1; }
void glDeleteFramebuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    (void)target;
    if (framebuffer == 0 || framebuffer == 1) {
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
    }
}
void glGenRenderbuffers(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = i + 1; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
void glBindRenderbuffer(GLenum target, GLuint renderbuffer) { (void)target; (void)renderbuffer; }
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { (void)target; (void)internalformat; (void)width; (void)height; }
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { (void)target; (void)attachment; (void)renderbuffertarget; (void)renderbuffer; }

void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void glFlush(void) { }
void glFinish(void) { }
void glPixelStorei(GLenum pname, GLint param) { (void)pname; (void)param; }

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    (void)x; (void)y; (void)width; (void)height; (void)format; (void)type;
    if (pixels) memset(pixels, 0, width * height * 4);
}
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val) {
    C3D_Mtx frustum; memset(&frustum, 0, sizeof(C3D_Mtx));
    float n = near_val; float f = far_val;
    frustum.r[0].x = (2.0f * n) / (right - left);
    frustum.r[0].z = (right + left) / (right - left);
    frustum.r[1].y = (2.0f * n) / (top - bottom);
    frustum.r[1].z = (top + bottom) / (top - bottom);

    // Standard OpenGL: z_ndc maps to [-1, +1]
    frustum.r[2].z = -(f + n) / (f - n);
    frustum.r[2].w = -(2.0f * f * n) / (f - n);
    frustum.r[3].z = -1.0f;

    C3D_Mtx result; Mtx_Multiply(&result, cur_mtx(), &frustum);
    Mtx_Copy(cur_mtx(), &result); g.matrices_dirty = 1;
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    glFrustumf((GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
}

void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed near_val, GLfixed far_val) {
    glFrustumf(left / 65536.0f, right / 65536.0f, bottom / 65536.0f, top / 65536.0f, near_val / 65536.0f, far_val / 65536.0f);
}

void glActiveTexture(GLenum texture) {
    (void)texture; /* single texture unit on PICA200 — unit 0 only */
}

void glClientActiveTexture(GLenum texture) {
    (void)texture;
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    (void)target; (void)s; (void)t; (void)r; (void)q;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (pname == GL_TEXTURE_ENV_MODE) {
        if (g.tex_env_mode != param) {
            g.tex_env_mode = param;
            g.tev_dirty = 1;
        }
    }
}


void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    glTexEnvi(target, pname, (GLint)param);
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (pname == GL_TEXTURE_ENV_COLOR && params) {
        /* TEV env color — store but PICA200 TEV constant color is limited */
        (void)target;
    } else if (params) {
        glTexEnvi(target, pname, (GLint)params[0]);
    }
}

GLboolean glIsEnabled(GLenum cap) {
    switch (cap) {
        case GL_DEPTH_TEST:    return g.depth_test_enabled;
        case GL_BLEND:         return g.blend_enabled;
        case GL_ALPHA_TEST:    return g.alpha_test_enabled;
        case GL_CULL_FACE:     return g.cull_face_enabled;
        case GL_TEXTURE_2D:    return g.texture_2d_enabled;
        case GL_SCISSOR_TEST:  return g.scissor_test_enabled;
        case GL_FOG:           return g.fog_enabled;
        default:               return GL_FALSE;
    }
}

GLboolean glIsTexture(GLuint texture) {
    if (texture > 0 && texture < GL2C3D_MAX_TEXTURES && g.textures[texture].allocated) return GL_TRUE;
    return GL_FALSE;
}