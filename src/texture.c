#include "NovaGL.h"
#include "utils.h"

#define NOVA_TEXTURE_PAGE_SIZE 1024

static inline GLuint active_bound_texture(void) {
    return g.bound_texture[g.active_texture_unit];
}

static int row_stride_bytes(int width, int bytes_per_pixel, int alignment) {
    int row = width * bytes_per_pixel;
    int align = alignment > 0 ? alignment : 1;
    int mask = align - 1;
    return (row + mask) & ~mask;
}

static inline int morton_offset_local(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int)morton_interleave((uint32_t)(x & 7), (uint32_t)(fy & 7));
}

static void apply_slot_params_to_tex(C3D_Tex *tex, const TexSlot *slot) {
    GPU_TEXTURE_FILTER_PARAM mag = (slot->mag_filter == GL_LINEAR) ? GPU_LINEAR : GPU_NEAREST;
    GPU_TEXTURE_FILTER_PARAM min_f = (slot->min_filter == GL_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_NEAREST) ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(tex, mag, min_f);

    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(tex, ws, wt);
}

static void free_texture_storage(TexSlot *slot) {
    if (slot->allocated) {
        C3D_TexDelete(&slot->tex);
    }
    memset(slot, 0, sizeof(TexSlot));
}

static void init_texture_defaults(TexSlot *slot) {
    slot->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    slot->mag_filter = GL_LINEAR;
    slot->wrap_s = GL_REPEAT;
    slot->wrap_t = GL_REPEAT;
}

static void upload_page_rgba8(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0, int src_y0, int copy_w, int copy_h, GLenum format) {
    uint32_t *dst = (uint32_t*)tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint32_t out_pixel = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                if (format == GL_RGB) {
                    const uint8_t *px = row + (src_x0 + x) * 3;
                    out_pixel = (0xFFu << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
                } else {
                    const uint8_t *px = row + (src_x0 + x) * 4;
                    out_pixel = ((uint32_t)px[3] << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
                }
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = out_pixel;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_page_16bit(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0, int src_y0, int copy_w, int copy_h) {
    uint16_t *dst = (uint16_t*)tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint16_t val = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                memcpy(&val, row + (src_x0 + x) * 2, sizeof(uint16_t));
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = val;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_page_8bit(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0, int src_y0, int copy_w, int copy_h) {
    uint8_t *dst = (uint8_t*)tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint8_t val = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                val = row[src_x0 + x];
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = val;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_texture_pixels(C3D_Tex *tex, GPU_TEXCOLOR fmt, int pot_w, int pot_h, const GLvoid *pixels, int width, int height, int src_x0, int src_y0, int copy_w, int copy_h, GLenum format, GLenum type, GLint unpack_alignment) {
    if (!pixels) { memset(tex->data, 0, (size_t)pot_w * (size_t)pot_h * (size_t)gpu_texfmt_bpp(fmt)); C3D_TexFlush(tex); return; }
    if (fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, unpack_alignment);
        upload_page_rgba8(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride, src_x0, src_y0, copy_w, copy_h, format);
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, unpack_alignment);
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride, src_x0, src_y0, copy_w, copy_h);
    } else {
        int row_stride = row_stride_bytes(width, 1, unpack_alignment);
        upload_page_8bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride, src_x0, src_y0, copy_w, copy_h);
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_TEXTURES && g.textures[id].in_use) id++;
        if (id == NOVA_MAX_TEXTURES) { g.last_error = GL_OUT_OF_MEMORY; textures[i] = 0; break; }
        memset(&g.textures[id], 0, sizeof(g.textures[id]));
        g.textures[id].in_use = 1;
        init_texture_defaults(&g.textures[id]);
        textures[i] = id;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id > 0 && id < NOVA_MAX_TEXTURES && g.textures[id].in_use) {
            free_texture_storage(&g.textures[id]);
            g.textures[id].in_use = 0;
            for (int u = 0; u < 3; u++) if (g.bound_texture[u] == id) g.bound_texture[u] = 0;
        }
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target; g.bound_texture[g.active_texture_unit] = texture;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)border; (void)internalformat;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    if (width <= 0 || height <= 0) { g.last_error = GL_INVALID_VALUE; return; }

    slot->orig_width = width;
    slot->orig_height = height;

    int target_w = width;
    int target_h = height;

    if (width > 1024 || height > 1024) {
        float scale_w = 1024.0f / width;
        float scale_h = 1024.0f / height;
        float scale = (scale_w < scale_h) ? scale_w : scale_h;
        target_w = (int)(width * scale);
        target_h = (int)(height * scale);
        if (target_w < 1) target_w = 1;
        if (target_h < 1) target_h = 1;
    }

    if (slot->allocated) {
        if (slot->fmt != gpu_fmt || slot->width != target_w || slot->height != target_h) {
            free_texture_storage(slot);
        }
    }

    slot->fmt = gpu_fmt;
    slot->width = target_w;
    slot->height = target_h;

    int pot_w = next_pow2(target_w);
    int pot_h = next_pow2(target_h);
    if (pot_w < 8) pot_w = 8;
    if (pot_h < 8) pot_h = 8;

    if (!slot->allocated) {
        if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt)) { g.last_error = GL_OUT_OF_MEMORY; return; }
        slot->allocated = 1;
    }

    slot->pot_w = pot_w;
    slot->pot_h = pot_h;
    apply_slot_params_to_tex(&slot->tex, slot);

    const GLvoid *upload_pixels = pixels;
    void *temp_pixels = NULL;

    if ((width > 1024 || height > 1024) && pixels) {
        int bpp = gpu_texfmt_bpp(gpu_fmt);
        temp_pixels = malloc(target_w * target_h * bpp);
        if (temp_pixels) {
            if (gpu_fmt == GPU_RGBA8) {
                downscale_rgba8((uint32_t*)temp_pixels, (const uint32_t*)pixels, width, height, target_w, target_h);
            } else if (bpp == 2) {
                downscale_16bit((uint16_t*)temp_pixels, (const uint16_t*)pixels, width, height, target_w, target_h);
            } else {
                downscale_8bit((uint8_t*)temp_pixels, (const uint8_t*)pixels, width, height, target_w, target_h);
            }
            upload_pixels = temp_pixels;
        }
    }

    upload_texture_pixels(&slot->tex, gpu_fmt, pot_w, pot_h, upload_pixels, target_w, target_h, 0, 0, target_w, target_h, format, type, g.unpack_alignment);
    if (temp_pixels) free(temp_pixels);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || !pixels) return;

    float scale_x = slot->orig_width > 0 ? ((float)slot->width / (float)slot->orig_width) : 1.0f;
    float scale_y = slot->orig_height > 0 ? ((float)slot->height / (float)slot->orig_height) : 1.0f;
    float step_x = slot->width > 0 ? ((float)slot->orig_width / (float)slot->width) : 1.0f;
    float step_y = slot->height > 0 ? ((float)slot->orig_height / (float)slot->height) : 1.0f;

    int real_xoffset = (int)(xoffset * scale_x);
    int real_yoffset = (int)(yoffset * scale_y);
    int real_width   = (int)(width * scale_x);
    int real_height  = (int)(height * scale_y);
    if (real_width < 1) real_width = 1;
    if (real_height < 1) real_height = 1;

    if (slot->fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, g.unpack_alignment);
        uint32_t *tex_data = (uint32_t*)slot->tex.data;

        for (int y = 0; y < real_height; y++) {
            int src_y = (int)(y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t*)pixels + src_y * row_stride;
            int dy = real_yoffset + y;

            for (int x = 0; x < real_width; x++) {
                int src_x = (int)(x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;

                uint32_t out_pixel;
                if (format == GL_RGB) {
                    const uint8_t *px = row + src_x * 3;
                    out_pixel = (0xFFu << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
                } else {
                    const uint8_t *px = row + src_x * 4;
                    out_pixel = ((uint32_t)px[3] << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
                }
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = out_pixel;
            }
        }
        C3D_TexFlush(&slot->tex);

    } else if (gpu_texfmt_bpp(slot->fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, g.unpack_alignment);
        uint16_t *tex_data = (uint16_t*)slot->tex.data;
        for (int y = 0; y < real_height; y++) {
            int src_y = (int)(y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t*)pixels + src_y * row_stride;
            int dy = real_yoffset + y;
            for (int x = 0; x < real_width; x++) {
                int src_x = (int)(x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                uint16_t val;
                memcpy(&val, row + src_x * 2, sizeof(uint16_t));
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = val;
            }
        }
        C3D_TexFlush(&slot->tex);

    } else {
        int row_stride = row_stride_bytes(width, 1, g.unpack_alignment);
        uint8_t *tex_data = (uint8_t*)slot->tex.data;
        for (int y = 0; y < real_height; y++) {
            int src_y = (int)(y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t*)pixels + src_y * row_stride;
            int dy = real_yoffset + y;
            for (int x = 0; x < real_width; x++) {
                int src_x = (int)(x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = row[src_x];
            }
        }
        C3D_TexFlush(&slot->tex);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    if (pname == GL_TEXTURE_MIN_FILTER)      slot->min_filter = param;
    else if (pname == GL_TEXTURE_MAG_FILTER)  slot->mag_filter = param;
    else if (pname == GL_TEXTURE_WRAP_S)      slot->wrap_s = param;
    else if (pname == GL_TEXTURE_WRAP_T)      slot->wrap_t = param;

    if (!slot->allocated) return;
    apply_slot_params_to_tex(&slot->tex, slot);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { glTexParameteri(target, pname, (GLint)param); }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { if (params) glTexParameteri(target, pname, (GLint)params[0]); }
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) { if (params) glTexParameteri(target, pname, params[0]); }
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) {
    (void)data; (void)imageSize; (void)internalformat;
    glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void glActiveTexture(GLenum texture) {
    int unit = (int)(texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3) g.active_texture_unit = unit;
}

void glClientActiveTexture(GLenum texture) {
    int unit = (int)(texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3) g.client_active_texture_unit = unit;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_ENV) return;
    int unit = g.active_texture_unit;

    switch (pname) {
        case GL_TEXTURE_ENV_MODE: g.tex_env_mode[unit] = param; break;
        case GL_COMBINE_RGB:      g.tex_env_combine_rgb[unit] = param; break;
        case GL_SRC0_RGB:         g.tex_env_src0_rgb[unit] = param; break;
        case GL_SRC1_RGB:         g.tex_env_src1_rgb[unit] = param; break;
        case GL_SRC2_RGB:         g.tex_env_src2_rgb[unit] = param; break;
        case GL_OPERAND0_RGB:     g.tex_env_operand0_rgb[unit] = param; break;
        case GL_OPERAND1_RGB:     g.tex_env_operand1_rgb[unit] = param; break;
        case GL_OPERAND2_RGB:     g.tex_env_operand2_rgb[unit] = param; break;
        case GL_COMBINE_ALPHA:    g.tex_env_combine_alpha[unit] = param; break;
        case GL_SRC0_ALPHA:       g.tex_env_src0_alpha[unit] = param; break;
        case GL_SRC1_ALPHA:       g.tex_env_src1_alpha[unit] = param; break;
        case GL_SRC2_ALPHA:       g.tex_env_src2_alpha[unit] = param; break;
        case GL_OPERAND0_ALPHA:   g.tex_env_operand0_alpha[unit] = param; break;
        case GL_OPERAND1_ALPHA:   g.tex_env_operand1_alpha[unit] = param; break;
        case GL_OPERAND2_ALPHA:   g.tex_env_operand2_alpha[unit] = param; break;
    }
    g.tev_dirty = 1;
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param) { glTexEnvi(target, pname, (GLint)param); }
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (pname == GL_TEXTURE_ENV_COLOR && params) { (void)target; }
    else if (params) { glTexEnvi(target, pname, (GLint)params[0]); }
}
GLboolean glIsTexture(GLuint texture) {
    if (texture > 0 && texture < NOVA_MAX_TEXTURES && g.textures[texture].in_use) return GL_TRUE;
    return GL_FALSE;
}
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)internalformat; (void)border;
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}
void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}
void glTexGend(GLenum coord, GLenum pname, GLdouble param) { (void)coord; (void)pname; (void)param; }
void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params) { (void)coord; (void)pname; (void)params; }
void glTexGenf(GLenum coord, GLenum pname, GLfloat param) { (void)coord; (void)pname; (void)param; }
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) { (void)coord; (void)pname; (void)params; }
void glTexGeni(GLenum coord, GLenum pname, GLint param) { (void)coord; (void)pname; (void)param; }
void glTexGeniv(GLenum coord, GLenum pname, const GLint *params) { (void)coord; (void)pname; (void)params; }
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) { (void)target; (void)s; (void)t; }
void glActiveTextureARB(GLenum texture) { glActiveTexture(texture); }
void glClientActiveTextureARB(GLenum texture) { glClientActiveTexture(texture); }