//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_TEXTURES && g.textures[id].in_use) {
            id++;
        }
        if (id == NOVA_MAX_TEXTURES) { g.last_error = GL_OUT_OF_MEMORY; textures[i] = 0; break; }

        g.textures[id].in_use = 1;
        textures[i] = id;
    }
}
void glDeleteTextures(GLsizei n, const GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id > 0 && id < NOVA_MAX_TEXTURES && g.textures[id].in_use) {
            if (g.textures[id].allocated) {
                C3D_TexDelete(&g.textures[id].tex);
            }
            g.textures[id].allocated = 0;
            g.textures[id].in_use = 0;
            if (g.bound_texture == id) g.bound_texture = 0;
        }
    }
}

void glBindTexture(GLenum target, GLuint texture) { (void)target; g.bound_texture = texture; }

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)border; (void)internalformat;
    if (g.bound_texture == 0 || g.bound_texture >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[g.bound_texture];

    if (slot->allocated) { C3D_TexDelete(&slot->tex); slot->allocated = 0; }
    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    int pot_w = next_pow2(width); int pot_h = next_pow2(height);
    if (pot_w < 8) pot_w = 8; if (pot_h < 8) pot_h = 8;
    if (pot_w > 1024) pot_w = 1024; if (pot_h > 1024) pot_h = 1024;

    int need_init = 1;
    if (slot->allocated) {
        // Если текстура такого же размера уже была выделена в этом слоте - НЕ УДАЛЯЕМ ЕЁ!
        if (slot->pot_w == pot_w && slot->pot_h == pot_h && slot->fmt == gpu_fmt) {
            need_init = 0;
        } else {
            C3D_TexDelete(&slot->tex);
            slot->allocated = 0;
        }
    }

    if (need_init) {
        if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt)) { g.last_error = GL_OUT_OF_MEMORY; return; }
        slot->allocated = 1; slot->pot_w = pot_w; slot->pot_h = pot_h; slot->fmt = gpu_fmt;
    }
    slot->width = width; slot->height = height; slot->pot_w = pot_w; slot->pot_h = pot_h; slot->fmt = gpu_fmt;

    GPU_TEXTURE_FILTER_PARAM mag = (slot->mag_filter == GL_LINEAR) ? GPU_LINEAR : GPU_NEAREST;
    GPU_TEXTURE_FILTER_PARAM min_f = (slot->min_filter == GL_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_LINEAR || slot->min_filter == GL_LINEAR_MIPMAP_NEAREST) ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&slot->tex, mag, min_f);
    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(&slot->tex, ws, wt);

    if (pixels)
    {
        int bpp = gpu_texfmt_bpp(gpu_fmt);
        int needed = pot_w * pot_h * bpp;

        void *staging = get_tex_staging(needed);

        if (!staging) {
            printf("[NovaGL][ERROR] texture.c: glTexImage2D staging.");
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        if (gpu_fmt == GPU_RGBA8)
        {
            const uint32_t *src = (const uint32_t*)pixels;

            if (format == GL_RGB)
            {
                const uint8_t *src8 = (const uint8_t*)pixels;
                uint32_t *tmp = (uint32_t*)staging;
                for (int i = 0; i < width * height; i++) {
                    tmp[i] =
                        (0xFF << 24) |
                        (src8[i*3+2] << 16) |
                        (src8[i*3+1] << 8) |
                        (src8[i*3+0]);
                }
                src = tmp;
            }

            swizzle_rgba8((uint32_t*)slot->tex.data, src,
                          width, height, pot_w, pot_h);
        }
        else
        {
            memcpy(slot->tex.data, pixels, width * height * bpp);
        }

        C3D_TexFlush(&slot->tex);
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    if (g.bound_texture == 0 || g.bound_texture >= NOVA_MAX_TEXTURES) return;
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
        if (needs_free && temp_rgba) free(temp_rgba);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (g.bound_texture == 0 || g.bound_texture >= NOVA_MAX_TEXTURES) return;
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

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    glTexParameteri(target, pname, (GLint)param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (params) glTexParameteri(target, pname, (GLint)params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (params) glTexParameteri(target, pname, params[0]);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) {
    (void)data; (void)imageSize; (void)internalformat;
    glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void glActiveTexture(GLenum texture) {
    (void)texture; /* single texture unit on PICA200 — unit 0 only */
}

void glClientActiveTexture(GLenum texture) {
    (void)texture;
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

GLboolean glIsTexture(GLuint texture) {
    if (texture > 0 && texture < NOVA_MAX_TEXTURES && g.textures[texture].allocated) return GL_TRUE;
    return GL_FALSE;
}

/* 1D texture functions (emulated as 2D textures with height=1) */
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)internalformat; (void)border;
    /* Emulate 1D texture as 2D texture with height=1 */
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    /* Emulate 1D texture subimage as 2D with height=1 */
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

/* Texture coordinate generation (not fully implemented - PICA200 has limited support) */
void glTexGend(GLenum coord, GLenum pname, GLdouble param) {
    (void)coord; (void)pname; (void)param;
    /* Texture coordinate generation not implemented */
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params) {
    (void)coord; (void)pname; (void)params;
}

void glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    (void)coord; (void)pname; (void)param;
}

void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) {
    (void)coord; (void)pname; (void)params;
}

void glTexGeni(GLenum coord, GLenum pname, GLint param) {
    (void)coord; (void)pname; (void)param;
}

void glTexGeniv(GLenum coord, GLenum pname, const GLint *params) {
    (void)coord; (void)pname; (void)params;
}

/* ARB multitexture functions */
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) {
    (void)target;
    /* Only texture unit 0 is supported on PICA200 */
    if (target == GL_TEXTURE0_ARB || target == GL_TEXTURE0) {
        /* Store texcoord for immediate mode */
    }
}

void glActiveTextureARB(GLenum texture) {
    glActiveTexture(texture);
}

void glClientActiveTextureARB(GLenum texture) {
    glClientActiveTexture(texture);
}
