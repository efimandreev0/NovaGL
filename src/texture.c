#include "NovaGL.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define NOVA_TEXTURE_PAGE_SIZE 1024

// Forward declarations for helpers used by the cache block below (defined later in this TU).
static inline GLuint active_bound_texture(void);
static void apply_slot_params_to_tex(C3D_Tex *tex, const TexSlot *slot);

// ===[ Persistent texture cache (see NovaGL.h for API contract) ]===
//
// Cache file layout (little-endian):
//   magic        = 'NVSW' (0x5753564E)
//   version      = 1
//   fmt          = GPU_TEXCOLOR
//   pot_w, pot_h = hardware texture size (power of two, post-downscale)
//   tgt_w, tgt_h = logical texture size used for UV math in the caller
//   orig_w, orig_h = original image size before any downscale
//   data_size    = pot_w * pot_h * bpp
// body:
//   <data_size> bytes of morton-swizzled pixels (byte-for-byte what goes into C3D_Tex->data)

#define NOVA_TEXCACHE_MAGIC    0x5753564Eu   // 'NVSW' LE
#define NOVA_TEXCACHE_VERSION  1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t fmt;
    uint32_t pot_w;
    uint32_t pot_h;
    uint32_t tgt_w;
    uint32_t tgt_h;
    uint32_t orig_w;
    uint32_t orig_h;
    uint32_t data_size;
} NovaTexCacheHeader;

static char g_tex_cache_dir[256] = {0};
static int  g_tex_cache_dir_ready = 0;

static void nova_mkdir_p(const char* path) {
    // Walk the path and mkdir each "/"-terminated prefix. Any failure (including
    // EEXIST on already-existing segments, or mkdir("sdmc:") which is just the SD
    // drive name on 3DS) is silently ignored — if the *leaf* ends up unwritable
    // we'll find out at fopen time and degrade gracefully.
    char buf[256];
    size_t n = strnlen(path, sizeof(buf) - 1);
    memcpy(buf, path, n);
    buf[n] = 0;
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/') {
            buf[i] = 0;
            mkdir(buf, 0777);
            buf[i] = '/';
        }
    }
    mkdir(buf, 0777);
}

static void nova_tex_cache_ensure_dir(void) {
    if (g_tex_cache_dir_ready) return;
    if (g_tex_cache_dir[0] == 0) return;
    nova_mkdir_p(g_tex_cache_dir);
    g_tex_cache_dir_ready = 1;
}

static int nova_tex_cache_path(char* buf, size_t bufSize, uint32_t hash) {
    if (g_tex_cache_dir[0] == 0) return 0;
    int n = snprintf(buf, bufSize, "%s/%08x.nsw", g_tex_cache_dir, hash);
    return (n > 0 && (size_t)n < bufSize);
}

void nova_texture_cache_set_directory(const char* dir) {
    if (dir == NULL || dir[0] == 0) {
        g_tex_cache_dir[0] = 0;
        g_tex_cache_dir_ready = 0;
        return;
    }
    size_t n = strnlen(dir, sizeof(g_tex_cache_dir) - 1);
    memcpy(g_tex_cache_dir, dir, n);
    g_tex_cache_dir[n] = 0;
    // Strip trailing slash for consistent concatenation.
    if (n > 0 && g_tex_cache_dir[n - 1] == '/') g_tex_cache_dir[n - 1] = 0;
    g_tex_cache_dir_ready = 0;  // lazily mkdir on first save
}

int nova_texture_cache_has(uint32_t hash) {
    char path[320];
    if (!nova_tex_cache_path(path, sizeof(path), hash)) return 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    NovaTexCacheHeader hdr;
    int ok = (fread(&hdr, sizeof(hdr), 1, f) == 1)
          && hdr.magic == NOVA_TEXCACHE_MAGIC
          && hdr.version == NOVA_TEXCACHE_VERSION;
    fclose(f);
    return ok;
}

int nova_texture_cache_load(uint32_t hash, int* out_orig_w, int* out_orig_h) {
    char path[320];
    if (!nova_tex_cache_path(path, sizeof(path), hash)) return 0;

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return 0;
    TexSlot* slot = &g.textures[bound];

    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;

    NovaTexCacheHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return 0; }
    if (hdr.magic != NOVA_TEXCACHE_MAGIC || hdr.version != NOVA_TEXCACHE_VERSION) { fclose(f); return 0; }
    if (hdr.pot_w < 8 || hdr.pot_h < 8 || hdr.pot_w > 1024 || hdr.pot_h > 1024) { fclose(f); return 0; }

    GPU_TEXCOLOR fmt = (GPU_TEXCOLOR) hdr.fmt;
    size_t expected = (size_t) hdr.pot_w * (size_t) hdr.pot_h * (size_t) gpu_texfmt_bpp(fmt);
    if (hdr.data_size != expected) { fclose(f); return 0; }

    // (Re)allocate linear storage if dimensions/format changed.
    if (slot->allocated) {
        if (slot->fmt != fmt || slot->pot_w != (int) hdr.pot_w || slot->pot_h != (int) hdr.pot_h) {
            C3D_TexDelete(&slot->tex);
            slot->allocated = 0;
        }
    }
    if (!slot->allocated) {
        if (!C3D_TexInit(&slot->tex, hdr.pot_w, hdr.pot_h, fmt)) {
            fclose(f);
            g.last_error = GL_OUT_OF_MEMORY;
            return 0;
        }
        slot->allocated = 1;
    }

    slot->fmt = fmt;
    slot->pot_w = (int) hdr.pot_w;
    slot->pot_h = (int) hdr.pot_h;
    slot->width = (int) hdr.tgt_w;
    slot->height = (int) hdr.tgt_h;
    slot->orig_width = (int) hdr.orig_w;
    slot->orig_height = (int) hdr.orig_h;

    slot->is_solid_optimized = (slot->pot_w == 8 && slot->pot_h == 8 && (slot->orig_width > 8 || slot->orig_height > 8));
    // Slurp the pre-swizzled payload straight into the C3D linear buffer.
    size_t got = fread(slot->tex.data, 1, hdr.data_size, f);
    fclose(f);
    if (got != hdr.data_size) return 0;

    apply_slot_params_to_tex(&slot->tex, slot);
    C3D_TexFlush(&slot->tex);

    if (out_orig_w) *out_orig_w = slot->orig_width;
    if (out_orig_h) *out_orig_h = slot->orig_height;
    return 1;
}

void nova_texture_cache_save(uint32_t hash) {
    if (g_tex_cache_dir[0] == 0) return;

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot* slot = &g.textures[bound];
    if (!slot->allocated || slot->tex.data == NULL) return;

    nova_tex_cache_ensure_dir();

    char path[320];
    if (!nova_tex_cache_path(path, sizeof(path), hash)) return;

    size_t data_size = (size_t) slot->pot_w * (size_t) slot->pot_h * (size_t) gpu_texfmt_bpp(slot->fmt);

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        // First write failure: try to mkdir again in case the enable happened before
        // the fs was mounted, then retry once.
        g_tex_cache_dir_ready = 0;
        nova_tex_cache_ensure_dir();
        f = fopen(path, "wb");
        if (f == NULL) return;
    }

    NovaTexCacheHeader hdr = {
        .magic = NOVA_TEXCACHE_MAGIC,
        .version = NOVA_TEXCACHE_VERSION,
        .fmt = (uint32_t) slot->fmt,
        .pot_w = (uint32_t) slot->pot_w,
        .pot_h = (uint32_t) slot->pot_h,
        .tgt_w = (uint32_t) slot->width,
        .tgt_h = (uint32_t) slot->height,
        .orig_w = (uint32_t) slot->orig_width,
        .orig_h = (uint32_t) slot->orig_height,
        .data_size = (uint32_t) data_size,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(slot->tex.data, 1, data_size, f);
    fclose(f);
}

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

    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE || slot->wrap_s == GL_CLAMP) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE || slot->wrap_t == GL_CLAMP) ? GPU_CLAMP_TO_EDGE : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
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

static int is_texture_solid(const void* pixels, int width, int height, GLenum format, GLenum type, int alignment) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    int bpp = 4;
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_5_6_5) bpp = 2;
    else if (format == GL_LUMINANCE || format == GL_ALPHA) bpp = 1;
    else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) bpp = 3;

    int stride = row_stride_bytes(width, bpp, alignment);

    // БЫСТРЫЙ ПУТЬ: Если данные лежат плотно (без паддинга), читаем целыми массивами
    if (bpp == 4 && stride == width * 4) {
        const uint32_t* p32 = (const uint32_t*)pixels;
        uint32_t first = p32[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p32[i] != first) return 0; // Нашли другой пиксель - отмена
        }
        return 1; // Текстура сплошная!
    } else if (bpp == 2 && stride == width * 2) {
        const uint16_t* p16 = (const uint16_t*)pixels;
        uint16_t first = p16[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p16[i] != first) return 0;
        }
        return 1;
    } else if (bpp == 1 && stride == width) {
        const uint8_t* p8 = (const uint8_t*)pixels;
        uint8_t first = p8[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p8[i] != first) return 0;
        }
        return 1;
    }

    // МЕДЛЕННЫЙ ПУТЬ: Только для экзотических 24-битных RGB текстур или если есть паддинг
    const uint8_t* p8 = (const uint8_t*)pixels;
    uint8_t first_px[4] = {0};
    memcpy(first_px, p8, bpp);

    for (int y = 0; y < height; y++) {
        const uint8_t* row = p8 + y * stride;
        int start_x = (y == 0) ? 1 : 0;
        for (int x = start_x; x < width; x++) {
            const uint8_t* px = row + x * bpp;
            if (px[0] != first_px[0]) return 0;
            if (bpp > 1 && px[1] != first_px[1]) return 0;
            if (bpp > 2 && px[2] != first_px[2]) return 0;
            if (bpp > 3 && px[3] != first_px[3]) return 0;
        }
    }
    return 1;
}

static void upload_solid_texture(C3D_Tex *tex, GPU_TEXCOLOR fmt, const GLvoid *pixels, GLenum format, GLenum type) {
    const uint8_t *px = (const uint8_t*)pixels; // Only 1st px
    int num_pixels = tex->width * tex->height;

    if (fmt == GPU_RGBA8) {
        uint32_t out_pixel = 0;
        if (format == GL_RGB) {
            out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | 0xFFu;
        } else {
            out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | (uint32_t)px[3];
        }
        uint32_t *dst = (uint32_t*)tex->data;
        for (int i = 0; i < num_pixels; i++) dst[i] = out_pixel;
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        uint16_t val;
        memcpy(&val, px, sizeof(uint16_t));
        if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
            uint16_t r = (val >> 12) & 0xF; uint16_t g = (val >> 8) & 0xF; uint16_t b = (val >> 4) & 0xF; uint16_t a = val & 0xF;
            val = (a << 12) | (b << 8) | (g << 4) | r;
        } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
            uint16_t r = (val >> 11) & 0x1F; uint16_t g = (val >> 6) & 0x1F; uint16_t b = (val >> 1) & 0x1F; uint16_t a = val & 0x1;
            val = (a << 15) | (b << 10) | (g << 5) | r;
        } else if (type == GL_UNSIGNED_SHORT_5_6_5) {
            uint16_t r = (val >> 11) & 0x1F; uint16_t g = (val >> 5) & 0x3F; uint16_t b = val & 0x1F;
            val = (b << 11) | (g << 5) | r;
        }
        uint16_t *dst = (uint16_t*)tex->data;
        for (int i = 0; i < num_pixels; i++) dst[i] = val;
    } else {
        uint8_t val = px[0];
        memset(tex->data, val, num_pixels);
    }
    C3D_TexFlush(tex);
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
                    out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | 0xFFu;
                } else {
                    const uint8_t *px = row + (src_x0 + x) * 4;
                    out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | (uint32_t)px[3];
                }
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = out_pixel;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_page_16bit(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0, int src_y0, int copy_w, int copy_h, GLenum type) {
    uint16_t *dst = (uint16_t*)tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint16_t val = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                memcpy(&val, row + (src_x0 + x) * 2, sizeof(uint16_t));

                // Reverting channels for fucking PICA200
                if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    // OpenGL (R4 G4 B4 A4) -> PICA200 (A4 B4 G4 R4)
                    uint16_t r = (val >> 12) & 0xF;
                    uint16_t g = (val >> 8)  & 0xF;
                    uint16_t b = (val >> 4)  & 0xF;
                    uint16_t a = val & 0xF;
                    val = (a << 12) | (b << 8) | (g << 4) | r;
                } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
                    // OpenGL (R5 G5 B5 A1) -> PICA200 (A1 B5 G5 R5)
                    uint16_t r = (val >> 11) & 0x1F;
                    uint16_t g = (val >> 6)  & 0x1F;
                    uint16_t b = (val >> 1)  & 0x1F;
                    uint16_t a = val & 0x1;
                    val = (a << 15) | (b << 10) | (g << 5) | r;
                } else if (type == GL_UNSIGNED_SHORT_5_6_5) {
                    // OpenGL (R5 G6 B5) -> PICA200 (B5 G6 R5)
                    uint16_t r = (val >> 11) & 0x1F;
                    uint16_t g = (val >> 5)  & 0x3F;
                    uint16_t b = val & 0x1F;
                    val = (b << 11) | (g << 5) | r;
                }
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
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t*)pixels, row_stride, src_x0, src_y0, copy_w, copy_h, type);
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

    gspWaitForP3D();
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

    // ПРОВЕРЯЕМ: Сплошная ли текстура?
    int is_solid = is_texture_solid(pixels, width, height, format, type, g.unpack_alignment);

    int target_w = width;
    int target_h = height;

    if (is_solid) {
        target_w = 8; // Сжимаем до аппаратного минимума 3DS
        target_h = 8;
        slot->is_solid_optimized = 1;
    } else {
        slot->is_solid_optimized = 0;
        if (width > 1024 || height > 1024) {
            float scale_w = 1024.0f / width;
            float scale_h = 1024.0f / height;
            float scale = (scale_w < scale_h) ? scale_w : scale_h;
            target_w = (int)(width * scale);
            target_h = (int)(height * scale);
            if (target_w < 1) target_w = 1;
            if (target_h < 1) target_h = 1;
        }
    }

    int pot_w = nova_next_pow2(target_w);
    int pot_h = nova_next_pow2(target_h);
    if (pot_w < 8) pot_w = 8;
    if (pot_h < 8) pot_h = 8;

    if (slot->allocated) {
        if (slot->fmt != gpu_fmt || slot->pot_w != pot_w || slot->pot_h != pot_h) {
            free_texture_storage(slot);
        }
    }

    slot->fmt = gpu_fmt;
    // Оставляем логический размер оригинальным, чтобы glGet и UV-координаты работали корректно
    slot->width = is_solid ? width : target_w;
    slot->height = is_solid ? height : target_h;

    if (!slot->allocated) {
        if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt)) { g.last_error = GL_OUT_OF_MEMORY; return; }
        slot->allocated = 1;
    }

    slot->pot_w = pot_w;
    slot->pot_h = pot_h;
    apply_slot_params_to_tex(&slot->tex, slot);

    // УМНАЯ ЗАГРУЗКА
    if (is_solid) {
        upload_solid_texture(&slot->tex, gpu_fmt, pixels, format, type);
    } else {
        const GLvoid *upload_pixels = pixels;
        void *temp_pixels = NULL;

        if ((width > 1024 || height > 1024) && pixels) {
            int bpp = gpu_texfmt_bpp(gpu_fmt);
            temp_pixels = malloc(target_w * target_h * bpp);
            if (temp_pixels) {
                if (gpu_fmt == GPU_RGBA8) downscale_rgba8((uint32_t*)temp_pixels, (const uint32_t*)pixels, width, height, target_w, target_h);
                else if (bpp == 2)        downscale_16bit((uint16_t*)temp_pixels, (const uint16_t*)pixels, width, height, target_w, target_h);
                else                      downscale_8bit((uint8_t*)temp_pixels, (const uint8_t*)pixels, width, height, target_w, target_h);
                upload_pixels = temp_pixels;
            }
        }

        upload_texture_pixels(&slot->tex, gpu_fmt, pot_w, pot_h, upload_pixels, target_w, target_h, 0, 0, target_w, target_h, format, type, g.unpack_alignment);
        if (temp_pixels) free(temp_pixels);
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || !pixels) return;

    // If game wanna draw on full tex -> uncompressing it.
    if (slot->is_solid_optimized) {
        C3D_Tex old_tex = slot->tex;

        int full_target_w = slot->orig_width;
        int full_target_h = slot->orig_height;
        if (full_target_w > 1024 || full_target_h > 1024) {
            float sw = 1024.0f / full_target_w;
            float sh = 1024.0f / full_target_h;
            float s = (sw < sh) ? sw : sh;
            full_target_w = (int)(full_target_w * s);
            full_target_h = (int)(full_target_h * s);
        }

        int full_pot_w = nova_next_pow2(full_target_w);
        int full_pot_h = nova_next_pow2(full_target_h);
        if (full_pot_w < 8) full_pot_w = 8;
        if (full_pot_h < 8) full_pot_h = 8;

        C3D_Tex new_tex;
        if (C3D_TexInit(&new_tex, full_pot_w, full_pot_h, slot->fmt)) {
            // Заливаем новую текстуру старым цветом
            int bpp = gpu_texfmt_bpp(slot->fmt);
            if (bpp == 4) {
                uint32_t col = ((uint32_t*)old_tex.data)[0];
                for (int i = 0; i < full_pot_w * full_pot_h; i++) ((uint32_t*)new_tex.data)[i] = col;
            } else if (bpp == 2) {
                uint16_t col = ((uint16_t*)old_tex.data)[0];
                for (int i = 0; i < full_pot_w * full_pot_h; i++) ((uint16_t*)new_tex.data)[i] = col;
            } else {
                uint8_t col = ((uint8_t*)old_tex.data)[0];
                memset(new_tex.data, col, full_pot_w * full_pot_h);
            }

            // ВАЖНО: Ждем пока GPU закончит работу, чтобы не удалить память во время рендера
            gspWaitForP3D();
            C3D_TexDelete(&old_tex);

            slot->tex = new_tex;
            slot->pot_w = full_pot_w;
            slot->pot_h = full_pot_h;
            slot->width = full_target_w;
            slot->height = full_target_h;
            slot->is_solid_optimized = 0;
            apply_slot_params_to_tex(&slot->tex, slot);
        }
    }

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
                    out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | 0xFFu;
                } else {
                    const uint8_t *px = row + src_x * 4;
                    out_pixel = ((uint32_t)px[0] << 24) | ((uint32_t)px[1] << 16) | ((uint32_t)px[2] << 8) | (uint32_t)px[3];
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

                // Переворачиваем каналы под PICA200
                if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    uint16_t r = (val >> 12) & 0xF;
                    uint16_t g = (val >> 8)  & 0xF;
                    uint16_t b = (val >> 4)  & 0xF;
                    uint16_t a = val & 0xF;
                    val = (a << 12) | (b << 8) | (g << 4) | r;
                } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
                    uint16_t r = (val >> 11) & 0x1F;
                    uint16_t g = (val >> 6)  & 0x1F;
                    uint16_t b = (val >> 1)  & 0x1F;
                    uint16_t a = val & 0x1;
                    val = (a << 15) | (b << 10) | (g << 5) | r;
                } else if (type == GL_UNSIGNED_SHORT_5_6_5) {
                    uint16_t r = (val >> 11) & 0x1F;
                    uint16_t g = (val >> 5)  & 0x3F;
                    uint16_t b = val & 0x1F;
                    val = (b << 11) | (g << 5) | r;
                }

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
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    (void)level;
    if (target != GL_TEXTURE_2D) return;

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || !g.current_target) return;

    C3D_FrameBuf* fb = &g.current_target->frameBuf;
    uint32_t* fb_data = (uint32_t*)fb->colorBuf;
    if (!fb_data) return;

    int fb_w = fb->width;
    int fb_h = fb->height;

    uint32_t* tex_data = (uint32_t*)slot->tex.data;
    if (!tex_data) return;

    gspWaitForP3D();

    for (int cy = 0; cy < height; cy++) {
        for (int cx = 0; cx < width; cx++) {
            int logical_x = x + cx;
            int logical_y = y + cy;

            int phys_x = logical_y;
            int phys_y = logical_x;

            if (phys_x < 0 || phys_x >= fb_w || phys_y < 0 || phys_y >= fb_h) continue;

            int dst_x = xoffset + cx;
            int dst_y = yoffset + cy;

            if (dst_x < 0 || dst_x >= slot->pot_w || dst_y < 0 || dst_y >= slot->pot_h) continue;

            uint32_t pixel = fb_data[morton_offset_local(phys_x, phys_y, fb_w, fb_h)];

            if (slot->fmt == GPU_RGBA8) {
                tex_data[morton_offset_local(dst_x, dst_y, slot->pot_w, slot->pot_h)] = pixel;
            } else if (gpu_texfmt_bpp(slot->fmt) == 2) {
                uint16_t* tex16 = (uint16_t*)slot->tex.data;
                // Bruh, C3D-framebuffer have 0xRRGGBBAA format...
                uint8_t r   = (pixel >> 24) & 0xFF;
                uint8_t g_c = (pixel >> 16) & 0xFF;
                uint8_t b   = (pixel >> 8)  & 0xFF;
                tex16[morton_offset_local(dst_x, dst_y, slot->pot_w, slot->pot_h)] =
                    ((r >> 3) << 11) | ((g_c >> 2) << 5) | (b >> 3);
            }
        }
    }

    C3D_TexFlush(&slot->tex);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    glTexImage2D(target, level, internalformat, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glCopyTexSubImage2D(target, level, 0, 0, x, y, width, height);
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