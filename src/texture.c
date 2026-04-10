//
// Created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

#define NOVA_TEXTURE_PAGE_SIZE 1024

/* Helper: get currently bound texture ID for the active unit */
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
    GPU_TEXTURE_FILTER_PARAM min_f =
        (slot->min_filter == GL_LINEAR ||
         slot->min_filter == GL_LINEAR_MIPMAP_LINEAR ||
         slot->min_filter == GL_LINEAR_MIPMAP_NEAREST) ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(tex, mag, min_f);

    GPU_TEXTURE_WRAP_PARAM ws =
        (slot->wrap_s == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE :
        ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt =
        (slot->wrap_t == GL_CLAMP_TO_EDGE) ? GPU_CLAMP_TO_EDGE :
        ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(tex, ws, wt);
}

static void free_texture_storage(TexSlot *slot) {
    if (slot->is_tiled && slot->pages) {
        int page_count = slot->tiles_x * slot->tiles_y;
        for (int i = 0; i < page_count; i++) {
            if (slot->pages[i].allocated) {
                C3D_TexDelete(&slot->pages[i].tex);
            }
        }
        free(slot->pages);
        slot->pages = NULL;
    } else if (slot->allocated) {
        C3D_TexDelete(&slot->tex);
    }

    slot->allocated = 0;
    slot->is_tiled = 0;
    slot->tiles_x = 0;
    slot->tiles_y = 0;
    slot->tile_w = 0;
    slot->tile_h = 0;
    slot->width = 0;
    slot->height = 0;
    slot->pot_w = 0;
    slot->pot_h = 0;
}

static void init_texture_defaults(TexSlot *slot) {
    slot->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    slot->mag_filter = GL_LINEAR;
    slot->wrap_s = GL_REPEAT;
    slot->wrap_t = GL_REPEAT;
}

static void upload_page_rgba8(C3D_Tex *tex, int pot_w, int pot_h,
                              const uint8_t *pixels, int row_stride,
                              int src_x0, int src_y0, int copy_w, int copy_h,
                              GLenum format) {
    uint32_t *dst = (uint32_t*)tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint32_t out_pixel = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                if (format == GL_RGB) {
                    const uint8_t *px = row + (src_x0 + x) * 3;
                    out_pixel = ((uint32_t)px[0] << 24) |
                                ((uint32_t)px[1] << 16) |
                                ((uint32_t)px[2] << 8)  |
                                0xFFu;
                } else {
                    const uint8_t *px = row + (src_x0 + x) * 4;
                    out_pixel = ((uint32_t)px[0] << 24) |
                                ((uint32_t)px[1] << 16) |
                                ((uint32_t)px[2] << 8)  |
                                (uint32_t)px[3];
                }
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = out_pixel;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_page_16bit(C3D_Tex *tex, int pot_w, int pot_h,
                              const uint8_t *pixels, int row_stride,
                              int src_x0, int src_y0, int copy_w, int copy_h) {
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

static void upload_page_8bit(C3D_Tex *tex, int pot_w, int pot_h,
                             const uint8_t *pixels, int row_stride,
                             int src_x0, int src_y0, int copy_w, int copy_h) {
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

/*static void upload_texture_pixels(C3D_Tex *tex, GPU_TEXCOLOR fmt, int pot_w, int pot_h,
                                  const GLvoid *pixels, int width, int height,
                                  int src_x0, int src_y0, int copy_w, int copy_h,
                                  GLenum format, GLenum type, GLint unpack_alignment) {
    if (!pixels) {
        memset(tex->data, 0, (size_t)pot_w * (size_t)pot_h * (size_t)gpu_texfmt_bpp(fmt));
        C3D_TexFlush(tex);
        return;
    }

    if (fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, unpack_alignment);
        upload_page_rgba8(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                          src_x0, src_y0, copy_w, copy_h, format);
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, unpack_alignment);
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                          src_x0, src_y0, copy_w, copy_h);
    } else {
        int row_stride = row_stride_bytes(width, 1, unpack_alignment);
        upload_page_8bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                         src_x0, src_y0, copy_w, copy_h);
    }
}*/

static void upload_texture_pixels(C3D_Tex *tex, GPU_TEXCOLOR fmt, int pot_w, int pot_h,
                                  const GLvoid *pixels, int width, int height,
                                  int src_x0, int src_y0, int copy_w, int copy_h,
                                  GLenum format, GLenum type, GLint unpack_alignment) {
    if (!pixels) {
        memset(tex->data, 0, (size_t)pot_w * (size_t)pot_h * (size_t)gpu_texfmt_bpp(fmt));
        C3D_TexFlush(tex);
        return;
    }

    if (fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, unpack_alignment);
        upload_page_rgba8(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                          src_x0, src_y0, copy_w, copy_h, format);
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, unpack_alignment);
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                          src_x0, src_y0, copy_w, copy_h);
    } else {
        int row_stride = row_stride_bytes(width, 1, unpack_alignment);
        upload_page_8bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride,
                         src_x0, src_y0, copy_w, copy_h);
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_TEXTURES && g.textures[id].in_use) {
            id++;
        }
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
            for (int u = 0; u < 3; u++)
                if (g.bound_texture[u] == id) g.bound_texture[u] = 0;
        }
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    g.bound_texture[g.active_texture_unit] = texture;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)border; (void)internalformat;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    if (width <= 0 || height <= 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }

    const int tiled = (width > NOVA_TEXTURE_PAGE_SIZE || height > NOVA_TEXTURE_PAGE_SIZE);
    const int tiles_x = (width + NOVA_TEXTURE_PAGE_SIZE - 1) / NOVA_TEXTURE_PAGE_SIZE;
    const int tiles_y = (height + NOVA_TEXTURE_PAGE_SIZE - 1) / NOVA_TEXTURE_PAGE_SIZE;

    if (slot->allocated) {
        int compatible = (slot->fmt == gpu_fmt &&
                          slot->width == width &&
                          slot->height == height &&
                          slot->is_tiled == tiled &&
                          (!tiled ? (slot->pot_w == next_pow2(width) && slot->pot_h == next_pow2(height))
                                  : (slot->tiles_x == tiles_x && slot->tiles_y == tiles_y)));
        if (!compatible) {
            free_texture_storage(slot);
        }
    }

    slot->fmt = gpu_fmt;
    slot->width = width;
    slot->height = height;

    if (!tiled) {
        int pot_w = next_pow2(width);
        int pot_h = next_pow2(height);
        if (pot_w < 8) pot_w = 8;
        if (pot_h < 8) pot_h = 8;

        if (!slot->allocated) {
            if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt)) {
                slot->allocated = 0;
                g.last_error = GL_OUT_OF_MEMORY;
                return;
            }
            slot->allocated = 1;
        }

        slot->is_tiled = 0;
        slot->pot_w = pot_w;
        slot->pot_h = pot_h;
        apply_slot_params_to_tex(&slot->tex, slot);
        upload_texture_pixels(&slot->tex, gpu_fmt, pot_w, pot_h, pixels,
                              width, height, 0, 0, width, height,
                              format, type, g.unpack_alignment);
        return;
    }

    if (!slot->allocated) {
        slot->pages = (TexPage*)calloc((size_t)(tiles_x * tiles_y), sizeof(TexPage));
        if (!slot->pages) {
            slot->allocated = 0;
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }
        slot->allocated = 1;
        slot->is_tiled = 1;
        slot->tiles_x = tiles_x;
        slot->tiles_y = tiles_y;
        slot->tile_w = NOVA_TEXTURE_PAGE_SIZE;
        slot->tile_h = NOVA_TEXTURE_PAGE_SIZE;
    }

    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int page_index = ty * tiles_x + tx;
            TexPage *page = &slot->pages[page_index];
            int page_w = width - tx * NOVA_TEXTURE_PAGE_SIZE;
            int page_h = height - ty * NOVA_TEXTURE_PAGE_SIZE;
            if (page_w > NOVA_TEXTURE_PAGE_SIZE) page_w = NOVA_TEXTURE_PAGE_SIZE;
            if (page_h > NOVA_TEXTURE_PAGE_SIZE) page_h = NOVA_TEXTURE_PAGE_SIZE;

            int page_pot_w = next_pow2(page_w);
            int page_pot_h = next_pow2(page_h);
            if (page_pot_w < 8) page_pot_w = 8;
            if (page_pot_h < 8) page_pot_h = 8;

            if (!page->allocated) {
                if (!C3D_TexInit(&page->tex, page_pot_w, page_pot_h, gpu_fmt)) {
                    free_texture_storage(slot);
                    g.last_error = GL_OUT_OF_MEMORY;
                    return;
                }
                page->allocated = 1;
            }

            page->width = page_w;
            page->height = page_h;
            page->pot_w = page_pot_w;
            page->pot_h = page_pot_h;
            apply_slot_params_to_tex(&page->tex, slot);
            upload_texture_pixels(&page->tex, gpu_fmt, page_pot_w, page_pot_h, pixels,
                                  width, height,
                                  tx * NOVA_TEXTURE_PAGE_SIZE,
                                  ty * NOVA_TEXTURE_PAGE_SIZE,
                                  page_w, page_h,
                                  format, type, g.unpack_alignment);
        }
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || !pixels) return;

    if (!slot->is_tiled) {
        if (slot->fmt == GPU_RGBA8) {
            int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, g.unpack_alignment);
            uint32_t *tex_data = (uint32_t*)slot->tex.data;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int dx = xoffset + x;
                    int dy = yoffset + y;
                    if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                    const uint8_t *row = (const uint8_t*)pixels + y * row_stride;
                    uint32_t out_pixel;
                    if (format == GL_RGB) {
                        const uint8_t *px = row + x * 3;
                        out_pixel = ((uint32_t)px[0] << 24) |
                                    ((uint32_t)px[1] << 16) |
                                    ((uint32_t)px[2] << 8)  |
                                    0xFFu;
                    } else {
                        const uint8_t *px = row + x * 4;
                        out_pixel = ((uint32_t)px[0] << 24) |
                                    ((uint32_t)px[1] << 16) |
                                    ((uint32_t)px[2] << 8)  |
                                    (uint32_t)px[3];
                    }
                    tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = out_pixel;
                }
            }
            C3D_TexFlush(&slot->tex);
        } else if (gpu_texfmt_bpp(slot->fmt) == 2) {
            int row_stride = row_stride_bytes(width, 2, g.unpack_alignment);
            uint16_t *tex_data = (uint16_t*)slot->tex.data;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int dx = xoffset + x;
                    int dy = yoffset + y;
                    if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                    uint16_t val;
                    const uint8_t *row = (const uint8_t*)pixels + y * row_stride;
                    memcpy(&val, row + x * 2, sizeof(uint16_t));
                    tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = val;
                }
            }
            C3D_TexFlush(&slot->tex);
        } else {
            int row_stride = row_stride_bytes(width, 1, g.unpack_alignment);
            uint8_t *tex_data = (uint8_t*)slot->tex.data;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int dx = xoffset + x;
                    int dy = yoffset + y;
                    if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                    const uint8_t *row = (const uint8_t*)pixels + y * row_stride;
                    tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = row[x];
                }
            }
            C3D_TexFlush(&slot->tex);
        }
        return;
    }

    for (int ty = 0; ty < slot->tiles_y; ty++) {
        for (int tx = 0; tx < slot->tiles_x; tx++) {
            int page_x0 = tx * NOVA_TEXTURE_PAGE_SIZE;
            int page_y0 = ty * NOVA_TEXTURE_PAGE_SIZE;
            int page_x1 = page_x0 + slot->pages[ty * slot->tiles_x + tx].width;
            int page_y1 = page_y0 + slot->pages[ty * slot->tiles_x + tx].height;

            int upd_x0 = xoffset;
            int upd_y0 = yoffset;
            int upd_x1 = xoffset + width;
            int upd_y1 = yoffset + height;

            int ix0 = upd_x0 > page_x0 ? upd_x0 : page_x0;
            int iy0 = upd_y0 > page_y0 ? upd_y0 : page_y0;
            int ix1 = upd_x1 < page_x1 ? upd_x1 : page_x1;
            int iy1 = upd_y1 < page_y1 ? upd_y1 : page_y1;

            if (ix0 >= ix1 || iy0 >= iy1) continue;

            int copy_w = ix1 - ix0;
            int copy_h = iy1 - iy0;
            int src_x0 = ix0 - xoffset;
            int src_y0 = iy0 - yoffset;
            int dst_x0 = ix0 - page_x0;
            int dst_y0 = iy0 - page_y0;

            TexPage *page = &slot->pages[ty * slot->tiles_x + tx];
            if (slot->fmt == GPU_RGBA8) {
                int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, g.unpack_alignment);
                uint32_t *tex_data = (uint32_t*)page->tex.data;
                for (int y = 0; y < copy_h; y++) {
                    for (int x = 0; x < copy_w; x++) {
                        const uint8_t *row = (const uint8_t*)pixels + (src_y0 + y) * row_stride;
                        uint32_t out_pixel;
                        if (format == GL_RGB) {
                            const uint8_t *px = row + (src_x0 + x) * 3;
                            out_pixel = ((uint32_t)px[0] << 24) |
                                        ((uint32_t)px[1] << 16) |
                                        ((uint32_t)px[2] << 8)  |
                                        0xFFu;
                        } else {
                            const uint8_t *px = row + (src_x0 + x) * 4;
                            out_pixel = ((uint32_t)px[0] << 24) |
                                        ((uint32_t)px[1] << 16) |
                                        ((uint32_t)px[2] << 8)  |
                                        (uint32_t)px[3];
                        }
                        tex_data[morton_offset_local(dst_x0 + x, dst_y0 + y, page->pot_w, page->pot_h)] = out_pixel;
                    }
                }
                C3D_TexFlush(&page->tex);
            } else if (gpu_texfmt_bpp(slot->fmt) == 2) {
                int row_stride = row_stride_bytes(width, 2, g.unpack_alignment);
                uint16_t *tex_data = (uint16_t*)page->tex.data;
                for (int y = 0; y < copy_h; y++) {
                    for (int x = 0; x < copy_w; x++) {
                        uint16_t val;
                        const uint8_t *row = (const uint8_t*)pixels + (src_y0 + y) * row_stride;
                        memcpy(&val, row + (src_x0 + x) * 2, sizeof(uint16_t));
                        tex_data[morton_offset_local(dst_x0 + x, dst_y0 + y, page->pot_w, page->pot_h)] = val;
                    }
                }
                C3D_TexFlush(&page->tex);
            } else {
                int row_stride = row_stride_bytes(width, 1, g.unpack_alignment);
                uint8_t *tex_data = (uint8_t*)page->tex.data;
                for (int y = 0; y < copy_h; y++) {
                    for (int x = 0; x < copy_w; x++) {
                        const uint8_t *row = (const uint8_t*)pixels + (src_y0 + y) * row_stride;
                        tex_data[morton_offset_local(dst_x0 + x, dst_y0 + y, page->pot_w, page->pot_h)] = row[src_x0 + x];
                    }
                }
                C3D_TexFlush(&page->tex);
            }
        }
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    /* Store params even before allocation — they'll be applied when the texture is created */
    if (pname == GL_TEXTURE_MIN_FILTER)      slot->min_filter = param;
    else if (pname == GL_TEXTURE_MAG_FILTER)  slot->mag_filter = param;
    else if (pname == GL_TEXTURE_WRAP_S)      slot->wrap_s = param;
    else if (pname == GL_TEXTURE_WRAP_T)      slot->wrap_t = param;

    if (!slot->allocated) return;

    /* Apply to the live C3D texture */
    if (slot->is_tiled && slot->pages) {
        int page_count = slot->tiles_x * slot->tiles_y;
        for (int i = 0; i < page_count; i++) {
            if (slot->pages[i].allocated) {
                apply_slot_params_to_tex(&slot->pages[i].tex, slot);
            }
        }
    } else {
        apply_slot_params_to_tex(&slot->tex, slot);
    }
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
    int unit = (int)(texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3)
        g.active_texture_unit = unit;
}

void glClientActiveTexture(GLenum texture) {
    int unit = (int)(texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3)
        g.client_active_texture_unit = unit;
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
    }
    g.tev_dirty = 1;
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
    if (texture > 0 && texture < NOVA_MAX_TEXTURES && g.textures[texture].in_use) return GL_TRUE;
    return GL_FALSE;
}

/* 1D texture functions (emulated as 2D textures with height=1) */
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)internalformat; (void)border;
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

/* Texture coordinate generation (not fully implemented - PICA200 has limited support) */
void glTexGend(GLenum coord, GLenum pname, GLdouble param) { (void)coord; (void)pname; (void)param; }
void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params) { (void)coord; (void)pname; (void)params; }
void glTexGenf(GLenum coord, GLenum pname, GLfloat param) { (void)coord; (void)pname; (void)param; }
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) { (void)coord; (void)pname; (void)params; }
void glTexGeni(GLenum coord, GLenum pname, GLint param) { (void)coord; (void)pname; (void)param; }
void glTexGeniv(GLenum coord, GLenum pname, const GLint *params) { (void)coord; (void)pname; (void)params; }

/* ARB multitexture functions */
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) {
    (void)target; (void)s; (void)t;
}

void glActiveTextureARB(GLenum texture) {
    glActiveTexture(texture);
}

void glClientActiveTextureARB(GLenum texture) {
    glClientActiveTexture(texture);
}
