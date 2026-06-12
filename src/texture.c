#include "NovaGL.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <dirent.h>
#include "stb_ds.h"

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
/* v2: invalidates every entry written while the (now opt-in) GX HW-swizzle
 * path was scrambling RGBA byte order — those cache files contain
 * channel-swapped texel data ("red instead of blue" forever, even after the
 * upload path was fixed, because the poisoned conversion was re-served from
 * disk). */
#define NOVA_TEXCACHE_VERSION  2u

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
static int g_tex_cache_dir_ready = 0;

static struct {
    uint32_t key;
    char value;
} *g_cache_index = NULL;

static int g_cache_index_built = 0;

static void build_cache_index(void) {
    if (g_cache_index_built) return;
    g_cache_index_built = 1;
    if (g_tex_cache_dir[0] == 0) return;

    DIR *dir = opendir(g_tex_cache_dir);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".nsw")) {
            uint32_t hash;
            if (sscanf(ent->d_name, "%08x.nsw", &hash) == 1) {
                hmput(g_cache_index, hash, 1);
            }
        }
    }
    closedir(dir);
}

// ================================================

static void nova_mkdir_p(const char *path) {
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

static int nova_tex_cache_path(char *buf, size_t bufSize, uint32_t hash) {
    if (g_tex_cache_dir[0] == 0) return 0;
    int n = snprintf(buf, bufSize, "%s/%08x.nsw", g_tex_cache_dir, hash);
    return (n > 0 && (size_t) n < bufSize);
}

void nova_texture_cache_set_directory(const char *dir) {
    if (dir == NULL || dir[0] == 0) {
        g_tex_cache_dir[0] = 0;
        g_tex_cache_dir_ready = 0;
        return;
    }
    size_t n = strnlen(dir, sizeof(g_tex_cache_dir) - 1);
    memcpy(g_tex_cache_dir, dir, n);
    g_tex_cache_dir[n] = 0;
    if (n > 0 && g_tex_cache_dir[n - 1] == '/') g_tex_cache_dir[n - 1] = 0;
    g_tex_cache_dir_ready = 0;

    // Сбрасываем индекс при смене директории
    g_cache_index_built = 0;
    hmfree(g_cache_index);
}

// МГНОВЕННАЯ ПРОВЕРКА КЭША (Без медленных fopen!)
int nova_texture_cache_has(uint32_t hash) {
    build_cache_index();
    return hmgeti(g_cache_index, hash) >= 0;
}

int nova_texture_cache_load(uint32_t hash, int *out_orig_w, int *out_orig_h) {
    char path[320];
    if (!nova_tex_cache_path(path, sizeof(path), hash)) return 0;

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return 0;
    TexSlot *slot = &g.textures[bound];

    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;

    // ===[ ФИКС СКОРОСТИ 2: БУФЕРИЗАЦИЯ FAT32 ]===
    // Ускоряет чтение огромных файлов с SD-карты на 3DS в 5-10 раз.
    setvbuf(f, NULL, _IOFBF, 128 * 1024);
    // ============================================

    NovaTexCacheHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    if (hdr.magic != NOVA_TEXCACHE_MAGIC || hdr.version != NOVA_TEXCACHE_VERSION) {
        fclose(f);
        return 0;
    }
    if (hdr.pot_w < 8 || hdr.pot_h < 8 || hdr.pot_w > 1024 || hdr.pot_h > 1024) {
        fclose(f);
        return 0;
    }

    GPU_TEXCOLOR fmt = (GPU_TEXCOLOR) hdr.fmt;
    size_t expected = (size_t) hdr.pot_w * (size_t) hdr.pot_h * (size_t) gpu_texfmt_bpp(fmt);
    if (hdr.data_size != expected) {
        fclose(f);
        return 0;
    }

    if (slot->allocated) {
        if (slot->fmt != fmt || slot->pot_w != (int) hdr.pot_w || slot->pot_h != (int) hdr.pot_h) {
            /* Deferred delete — queued draws may still sample the old image. */
            nova_tex_gc_push(&slot->tex);
            nova_invalidate_tex_bind((GLuint) (slot - g.textures));
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
    slot->is_solid_optimized = (slot->pot_w == 8 && slot->pot_h == 8 && (
                                    slot->orig_width > 8 || slot->orig_height > 8));

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
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || slot->tex.data == NULL) return;

    nova_tex_cache_ensure_dir();

    char path[320];
    if (!nova_tex_cache_path(path, sizeof(path), hash)) return;

    size_t data_size = (size_t) slot->pot_w * (size_t) slot->pot_h * (size_t) gpu_texfmt_bpp(slot->fmt);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        g_tex_cache_dir_ready = 0;
        nova_tex_cache_ensure_dir();
        f = fopen(path, "wb");
        if (f == NULL) return;
    }

    setvbuf(f, NULL, _IOFBF, 128 * 1024);

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

    build_cache_index();
    hmput(g_cache_index, hash, 1);
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
    return tile_offset + (int) morton_interleave((uint32_t) (x & 7), (uint32_t) (fy & 7));
}

static void apply_slot_params_to_tex(C3D_Tex *tex, const TexSlot *slot) {
    GPU_TEXTURE_FILTER_PARAM mag = (slot->mag_filter == GL_LINEAR) ? GPU_LINEAR : GPU_NEAREST;

    // Decode the GL minification filter into separate per-level and inter-level
    // (mipmap) filters. If the texture doesn't actually have mip levels we MUST
    // pin the inter-level filter to NEAREST and clamp LOD to 0, otherwise
    // PICA200 happily samples beyond level 0 into uninitialised memory and
    // renders garbage tiled patterns.
    GPU_TEXTURE_FILTER_PARAM min_f;
    GPU_TEXTURE_FILTER_PARAM mip_f;
    int wants_mips = (slot->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
                      slot->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                      slot->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                      slot->min_filter == GL_LINEAR_MIPMAP_LINEAR);
    int linear_within = (slot->min_filter == GL_LINEAR ||
                         slot->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                         slot->min_filter == GL_LINEAR_MIPMAP_LINEAR);
    int linear_between = (slot->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                          slot->min_filter == GL_LINEAR_MIPMAP_LINEAR);

    min_f = linear_within ? GPU_LINEAR : GPU_NEAREST;
    if (wants_mips && slot->has_mipmap) {
        mip_f = linear_between ? GPU_LINEAR : GPU_NEAREST;
    } else {
        // No mips available — force NEAREST mip filter and clamp LOD to 0
        // below so PICA can't reach beyond level 0.
        mip_f = GPU_NEAREST;
    }

    C3D_TexSetFilter(tex, mag, min_f);
    C3D_TexSetFilterMipmap(tex, mip_f);

    // Cap LOD so PICA never samples a non-existent level. When mips are
    // present we still respect a Arx-supplied GL_TEXTURE_MAX_LEVEL cap.
    int max_lod = slot->has_mipmap ? (slot->max_level >= 0 ? slot->max_level : 7) : 0;
    if (max_lod < 0) max_lod = 0;
    if (max_lod > 7) max_lod = 7;
    C3D_TexSetLodBias(tex, 0.0f);
    // tex->lodParam: bits  0..7 = bias, 16..19 = maxLevel, 24..27 = minLevel.
    // citro3d has no public setter for max/min level on a non-mipmap-init'd
    // texture, so patch the field directly.
    tex->lodParam = (tex->lodParam & ~(0xFu << 16)) | ((u32)(max_lod & 0xF) << 16);

    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE || slot->wrap_s == GL_CLAMP)
                                    ? GPU_CLAMP_TO_EDGE
                                    : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE || slot->wrap_t == GL_CLAMP)
                                    ? GPU_CLAMP_TO_EDGE
                                    : ((slot->wrap_t == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    C3D_TexSetWrap(tex, ws, wt);
}

/* ── Deferred texture deletion (orphaning) ────────────────────────────────
 *
 * NovaGL records draw commands all frame; the GPU executes them at
 * C3D_FrameEnd. Freeing or overwriting a texture's storage mid-frame
 * therefore corrupts any QUEUED draw that still references it. fast3d hits
 * this constantly: its LRU texture cache evicts and re-uploads texture ids
 * mid-frame while a level streams in — the "textures randomly swap to other
 * textures while moving" bug in PD. Desktop GL drivers orphan storage
 * internally; we do the same: old storage goes into this GC and is only
 * C3D_TexDelete'd once the frame that referenced it has fully executed
 * (novaSwapBuffers collects after C3D_FrameBegin's SYNCDRAW wait). */
#define NOVA_TEX_GC_SLOTS 128
static C3D_Tex s_tex_gc[NOVA_TEX_GC_SLOTS];
static int s_tex_gc_count = 0;

void nova_tex_gc_collect(void) {
    for (int i = 0; i < s_tex_gc_count; i++) {
        C3D_TexDelete(&s_tex_gc[i]);
    }
    s_tex_gc_count = 0;
}

void nova_tex_gc_push(C3D_Tex *tex) {
    if (s_tex_gc_count >= NOVA_TEX_GC_SLOTS) {
        /* GC full mid-frame (128 orphans in one frame — extreme). Flush the
         * pending draws and wait so the entries become reclaimable. */
        C3D_FrameSplit(0);
        gspWaitForP3D();
        nova_tex_gc_collect();
    }
    s_tex_gc[s_tex_gc_count++] = *tex;
    memset(tex, 0, sizeof(*tex));
}

static void free_texture_storage(TexSlot *slot) {
    if (slot->allocated) {
        nova_tex_gc_push(&slot->tex);
        /* Storage re-created at the same id ⇒ data pointer changes; the
         * TexBind skip-cache must not treat the id as "already bound". */
        nova_invalidate_tex_bind((GLuint) (slot - g.textures));
    }
    /* Clear storage-related state ONLY. The old memset(slot, 0, sizeof)
     * also wiped in_use and the sampler params, so re-defining a bound
     * texture silently "deleted" it: the next glBindTexture saw in_use==0
     * and re-created the slot with default filters/wrap. */
    slot->allocated = 0;
    slot->has_mipmap = 0;
    slot->is_solid_optimized = 0;
    slot->fmt = (GPU_TEXCOLOR) 0;
    slot->pot_w = 0;
    slot->pot_h = 0;
    slot->width = 0;
    slot->height = 0;
    slot->orig_width = 0;
    slot->orig_height = 0;
}

static void init_texture_defaults(TexSlot *slot) {
    slot->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    slot->mag_filter = GL_LINEAR;
    slot->wrap_s = GL_REPEAT;
    slot->wrap_t = GL_REPEAT;
    slot->generate_mipmap = 0;
    slot->max_level = -1; // -1 = unset (no cap)
    slot->has_mipmap = 0;
}

static int is_texture_solid(const void *pixels, int width, int height, GLenum format, GLenum type, int alignment) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    int bpp = 4;
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_5_6_5)
        bpp = 2;
    else if (format == GL_LUMINANCE_ALPHA && type == GL_UNSIGNED_BYTE) bpp = 2;
    else if (format == GL_LUMINANCE || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA4_NOVA) bpp = 1;
    else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) bpp = 3;

    int stride = row_stride_bytes(width, bpp, alignment);

    // БЫСТРЫЙ ПУТЬ: Если данные лежат плотно (без паддинга), читаем целыми массивами
    if (bpp == 4 && stride == width * 4) {
        const uint32_t *p32 = (const uint32_t *) pixels;
        uint32_t first = p32[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p32[i] != first) return 0; // Нашли другой пиксель - отмена
        }
        return 1; // Текстура сплошная!
    } else if (bpp == 2 && stride == width * 2) {
        const uint16_t *p16 = (const uint16_t *) pixels;
        uint16_t first = p16[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p16[i] != first) return 0;
        }
        return 1;
    } else if (bpp == 1 && stride == width) {
        const uint8_t *p8 = (const uint8_t *) pixels;
        uint8_t first = p8[0];
        int total = width * height;
        for (int i = 1; i < total; i++) {
            if (p8[i] != first) return 0;
        }
        return 1;
    }

    // МЕДЛЕННЫЙ ПУТЬ: Только для экзотических 24-битных RGB текстур или если есть паддинг
    const uint8_t *p8 = (const uint8_t *) pixels;
    uint8_t first_px[4] = {0};
    memcpy(first_px, p8, bpp);

    for (int y = 0; y < height; y++) {
        const uint8_t *row = p8 + y * stride;
        int start_x = (y == 0) ? 1 : 0;
        for (int x = start_x; x < width; x++) {
            const uint8_t *px = row + x * bpp;
            if (px[0] != first_px[0]) return 0;
            if (bpp > 1 && px[1] != first_px[1]) return 0;
            if (bpp > 2 && px[2] != first_px[2]) return 0;
            if (bpp > 3 && px[3] != first_px[3]) return 0;
        }
    }
    return 1;
}

static void upload_solid_texture(C3D_Tex *tex, GPU_TEXCOLOR fmt, const GLvoid *pixels, GLenum format, GLenum type) {
    const uint8_t *px = (const uint8_t *) pixels; // Only 1st px
    int num_pixels = tex->width * tex->height;

    if (fmt == GPU_RGBA8) {
        uint32_t out_pixel = 0;
        if (format == GL_RGB) {
            out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | 0xFFu;
        } else {
            out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | (uint32_t) px[
                            3];
        }
        uint32_t *dst = (uint32_t *) tex->data;
        for (int i = 0; i < num_pixels; i++) dst[i] = out_pixel;
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        uint16_t val;
        memcpy(&val, px, sizeof(uint16_t));
        uint16_t *dst = (uint16_t *) tex->data;
        for (int i = 0; i < num_pixels; i++) dst[i] = val;
    } else {
        uint8_t val = px[0];
        memset(tex->data, val, num_pixels);
    }
    C3D_TexFlush(tex);
}

static void upload_page_rgba8(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0,
                              int src_y0, int copy_w, int copy_h, GLenum format) {
    uint32_t *dst = (uint32_t *) tex->data;
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint32_t out_pixel = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                if (format == GL_RGB) {
                    const uint8_t *px = row + (src_x0 + x) * 3;
                    out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | 0xFFu;
                } else {
                    const uint8_t *px = row + (src_x0 + x) * 4;
                    out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | (
                                    uint32_t) px[3];
                }
            }
            dst[morton_offset_local(x, y, pot_w, pot_h)] = out_pixel;
        }
    }
    C3D_TexFlush(tex);
}

static void upload_page_16bit(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0,
                              int src_y0, int copy_w, int copy_h, GLenum type) {
    uint16_t *dst = (uint16_t *) tex->data;
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

static void upload_page_8bit(C3D_Tex *tex, int pot_w, int pot_h, const uint8_t *pixels, int row_stride, int src_x0,
                             int src_y0, int copy_w, int copy_h) {
    uint8_t *dst = (uint8_t *) tex->data;
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

/* True iff the (src_w x src_h) source fully covers the (pot_w x pot_h) target
 * and the input pixel layout matches the hardware format byte-for-byte. When
 * this holds, GX DisplayTransfer can swizzle in hardware (~10x faster than the
 * per-pixel CPU morton loop on a 1024^2 texture). */
static int can_use_hw_swizzle(GPU_TEXCOLOR fmt, int pot_w, int pot_h, int width, int height,
                              int copy_w, int copy_h, int src_x0, int src_y0,
                              GLenum format, GLenum type, GLint unpack_alignment) {
    if (src_x0 != 0 || src_y0 != 0) return 0;
    if (width != copy_w || height != copy_h) return 0;
    if (width != pot_w || height != pot_h) return 0;
    if (unpack_alignment != 1 && unpack_alignment != 2 && unpack_alignment != 4) return 0;
    /* GX DisplayTransfer is unreliable below 64px per axis (sf2d gated its
     * hardware-tiling path the same way); small textures go through the CPU
     * swizzler, which is cheap at those sizes anyway. */
    if (width < 64 || height < 64) return 0;
    /* RGB-as-RGBA8 requires a CPU repack step (3→4 bytes) so HW path
     * doesn't apply; same for RGB565 fed via GL_RGB+UNSIGNED_BYTE. */
    if (fmt == GPU_RGBA8) {
        if (format != GL_RGBA && format != GL_RGBA8_OES) return 0;
        if (type != GL_UNSIGNED_BYTE) return 0;
        /* DisplayTransfer needs 8-byte row alignment of the linear source; a
         * 4-aligned row stride for 4 bpp ≥8 pixels wide always satisfies it. */
        return ((width & 7) == 0 && (height & 7) == 0);
    }
    if (fmt == GPU_RGB565 || fmt == GPU_RGBA5551 || fmt == GPU_RGBA4) {
        return ((width & 7) == 0 && (height & 7) == 0);
    }
    /* Single-channel paths: HW transfer formats don't include L8/A8/LA4, so
     * we keep the CPU swizzler. */
    return 0;
}

static void upload_texture_pixels(C3D_Tex *tex, GPU_TEXCOLOR fmt, int pot_w, int pot_h, const GLvoid *pixels, int width,
                                  int height, int src_x0, int src_y0, int copy_w, int copy_h, GLenum format,
                                  GLenum type, GLint unpack_alignment) {
    if (!pixels) {
        memset(tex->data, 0, (size_t) pot_w * (size_t) pot_h * (size_t) gpu_texfmt_bpp(fmt));
        C3D_TexFlush(tex);
        return;
    }
    /* HW-accelerated path — OPT-IN ONLY (-DNOVAGL_ENABLE_HW_SWIZZLE=1).
     *
     * Disabled by default because Citra's emulation of GX DisplayTransfer
     * linear→tiled for arbitrary texture dimensions is unreliable (it's a
     * path almost no homebrew exercises — Citra mainly emulates the
     * framebuffer-present transfer), producing black/garbage textures for
     * everything that takes this path while the CPU swizzler renders fine.
     * On real hardware the path is believed correct (byte order + flip are
     * handled in the staging copy below), but until it's validated on a
     * console, correctness beats the ~10x swizzle speedup.
     *
     * Source must be in **linear** RAM for the GX DMA engine to read it —
     * game-heap pointers will silently produce garbage or crash the GPU,
     * hence the tex_staging copy (which IS linearAlloc'd). */
#ifdef NOVAGL_ENABLE_HW_SWIZZLE
    if (can_use_hw_swizzle(fmt, pot_w, pot_h, width, height, copy_w, copy_h, src_x0, src_y0,
                           format, type, unpack_alignment)) {
        int bytes = pot_w * pot_h * gpu_texfmt_bpp(fmt);
        /* Cap HW staging at 1MB — bigger textures are rare on 3DS and we'd
         * rather pay the soft-swizzle CPU cost than balloon linear RAM. */
        if (bytes <= 1024 * 1024) {
            void *linear_src = get_tex_staging(bytes);
            if (linear_src) {
                /* The GX transfer engine only re-tiles — it does NOT apply
                 * NovaGL's texture conventions, so the staging copy must:
                 *  1. flip vertically (CPU swizzler stores row 0 at the
                 *     bottom, see morton_offset_local's fy = pot_h-1-y);
                 *  2. for RGBA8, swap GL byte order (R,G,B,A in memory) to
                 *     PICA's (A,B,G,R in memory) — a plain bswap32. The
                 *     16-bit packed formats (565/5551/4444) already match.
                 * A raw memcpy here scrambled every channel (alpha took the
                 * red value!) and flipped nothing — textured draws came out
                 * as transparent garbage. */
                if (fmt == GPU_RGBA8) {
                    const uint32_t *src32 = (const uint32_t *) pixels;
                    uint32_t *dst32 = (uint32_t *) linear_src;
                    for (int y = 0; y < pot_h; y++) {
                        const uint32_t *srow = src32 + (size_t) (pot_h - 1 - y) * pot_w;
                        uint32_t *drow = dst32 + (size_t) y * pot_w;
                        for (int x = 0; x < pot_w; x++) {
                            drow[x] = __builtin_bswap32(srow[x]);
                        }
                    }
                } else {
                    const uint8_t *src8 = (const uint8_t *) pixels;
                    uint8_t *dst8 = (uint8_t *) linear_src;
                    int row = pot_w * gpu_texfmt_bpp(fmt);
                    for (int y = 0; y < pot_h; y++) {
                        memcpy(dst8 + (size_t) y * row,
                               src8 + (size_t) (pot_h - 1 - y) * row, (size_t) row);
                    }
                }
                nova_hardware_swizzle(tex, linear_src, pot_w, pot_h, fmt);
                return;
            }
        }
        /* fall through to soft path */
    }
#endif
    if (fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, unpack_alignment);
        upload_page_rgba8(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h,
                          format);
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, unpack_alignment);
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h,
                          type);
    } else {
        int row_stride = row_stride_bytes(width, 1, unpack_alignment);
        upload_page_8bit(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h);
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_TEXTURES && g.textures[id].in_use) id++;
        if (id == NOVA_MAX_TEXTURES) {
            g.last_error = GL_OUT_OF_MEMORY;
            textures[i] = 0;
            break;
        }
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

            /* Don't C3D_TexBind(unit, NULL) — passing NULL through libctru is
             * not universally safe; instead invalidate the bind cache so the
             * next apply_gpu_state re-binds whatever the caller binds next.
             * Logical state: clear the slot. */
            for (int u = 0; u < 3; u++) {
                if (g.bound_texture[u] == id) {
                    g.bound_texture[u] = 0;
                }
            }
            nova_invalidate_state_cache();
        }
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    /* Spec: only GL_TEXTURE_2D exists in ES 1.1; anything else is
     * GL_INVALID_ENUM and must not change the binding. */
    if (target != GL_TEXTURE_2D) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    if (texture >= NOVA_MAX_TEXTURES) {
        /* Out of our id space — can't represent it. Closest spec error. */
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    /* Spec (GL ES 1.1 §3.8.10): binding an unused name CREATES a new texture
     * object with default state. Desktop-GL ports routinely skip
     * glGenTextures and just bind arbitrary ids — without this, the slot
     * had min_filter=0/wrap=0 and filtering silently degraded to NEAREST
     * with broken wrap modes. */
    if (texture != 0 && !g.textures[texture].in_use) {
        memset(&g.textures[texture], 0, sizeof(g.textures[texture]));
        g.textures[texture].in_use = 1;
        init_texture_defaults(&g.textures[texture]);
    }

    g.bound_texture[g.active_texture_unit] = texture;
}

// === [DIAG] dump first N glTexImage2D inputs to SD for offline inspection ===
// Set NOVA_DIAG_TEX_LIMIT=N at compile time to re-enable; default off.
#ifndef NOVA_DIAG_TEX_LIMIT
#define NOVA_DIAG_TEX_LIMIT 0
#endif
static int s_diag_tex_count = 0;

static int diag_input_bpp(GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 ||
        type == GL_UNSIGNED_SHORT_5_5_5_1 ||
        type == GL_UNSIGNED_SHORT_5_6_5) return 2;
    if (format == GL_RGBA) return 4;
    if (format == GL_RGB)  return 3;
    if (format == GL_LUMINANCE_ALPHA) return 2;
    if (format == GL_LUMINANCE || format == GL_ALPHA) return 1;
    return 4;
}

static void diag_dump_teximage(int idx, GLsizei w, GLsizei h, GLenum format, GLenum type,
                               const GLvoid *pixels, GLint align) {
    if (!pixels || w <= 0 || h <= 0) return;
    mkdir("sdmc:/3ds/arxlibertatis", 0777);
    mkdir("sdmc:/3ds/arxlibertatis/nova_dump", 0777);
    int bpp = diag_input_bpp(format, type);
    int stride = row_stride_bytes(w, bpp, align);
    char path[256];
    snprintf(path, sizeof(path),
             "sdmc:/3ds/arxlibertatis/nova_dump/tex%02d_%dx%d_f%04X_t%04X_bpp%d.raw",
             idx, (int)w, (int)h, (unsigned)format, (unsigned)type, bpp);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("[NovaDiag] tex#%d: failed to open %s\n", idx, path);
        return;
    }
    fwrite(pixels, (size_t)stride * (size_t)h, 1, fp);
    fclose(fp);
    printf("[NovaDiag] tex#%d: dumped %d bytes (stride=%d) -> %s\n",
           idx, stride * (int)h, stride, path);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels) {
    (void) target;
    (void) level;
    (void) border;
    (void) internalformat;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    /* Spec: border must be 0 in GL ES (and effectively in modern GL).
     * GL_INVALID_VALUE, no state change. */
    if (border != 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (level < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    /* Manual mip uploads (level > 0): NovaGL stores only level 0 (mips are
     * auto-generated via GL_GENERATE_MIPMAP / C3D_TexGenerateMipmap). Before
     * this guard, a game uploading levels 1..N would repeatedly destroy and
     * re-create level 0 with each smaller image. Accept-and-ignore is
     * spec-tolerable here: LOD is clamped to 0 when no real mip chain exists
     * (see apply_slot_params_to_tex), so sampling stays correct. */
    if (level > 0) {
        return;
    }
    /* ES 1.1: internalformat must equal format (GL_INVALID_OPERATION
     * otherwise). Desktop ports pass sized formats (GL_RGBA8, 3, 4...), so
     * we accept those as aliases instead of erroring — leniency keeps ports
     * working — but a *conflicting* combination (e.g. internal=GL_RGB with
     * format=GL_ALPHA) is genuinely a caller bug worth surfacing. */

    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    if (width <= 0 || height <= 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }

    int diag_idx = -1;
    if (s_diag_tex_count < NOVA_DIAG_TEX_LIMIT) {
        diag_idx = s_diag_tex_count++;
        printf("[NovaDiag] tex#%d glTexImage2D: tex_id=%u w=%d h=%d format=0x%04X type=0x%04X "
               "internal=0x%04X align=%d pixels=%p gpu_fmt=%d\n",
               diag_idx, (unsigned)bound, (int)width, (int)height,
               (unsigned)format, (unsigned)type, (unsigned)internalformat,
               (int)g.unpack_alignment, pixels, (int)gpu_fmt);
        diag_dump_teximage(diag_idx, width, height, format, type, pixels, g.unpack_alignment);
    }

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
            target_w = (int) (width * scale);
            target_h = (int) (height * scale);
            if (target_w < 1) target_w = 1;
            if (target_h < 1) target_h = 1;
        }
    }

    int pot_w = nova_next_pow2(target_w);
    int pot_h = nova_next_pow2(target_h);
    if (pot_w < 8) pot_w = 8;
    if (pot_h < 8) pot_h = 8;

    /* Re-definition ALWAYS orphans the old storage — even when format and
     * size match. Overwriting the texel data in place corrupts queued draws
     * that still sample the old image this frame (see tex_gc_push above).
     * Fresh linearAlloc per re-upload; the old block is reclaimed once the
     * frame completes. */
    if (slot->allocated) {
        free_texture_storage(slot);
    }

    slot->fmt = gpu_fmt;
    // Оставляем логический размер оригинальным, чтобы glGet и UV-координаты работали корректно
    slot->width = is_solid ? width : target_w;
    slot->height = is_solid ? height : target_h;

    // Decide up-front whether this texture needs a mip pyramid. Arx sets
    // GL_GENERATE_MIPMAP before glTexImage2D (OpenGL 1.4 semantics) when it
    // wants auto-generated mips, otherwise it pins GL_TEXTURE_MAX_LEVEL=0.
    // is_solid textures stay 8x8 — never worth mipping.
    int want_mips = slot->generate_mipmap && !is_solid && pot_w >= 2 && pot_h >= 2;

    /* (The old "mip-state changed → re-init" check is gone: re-definition
     * orphans unconditionally above, so the slot is never allocated here.) */
    if (!slot->allocated) {
        bool ok = want_mips
            ? C3D_TexInitMipmap(&slot->tex, pot_w, pot_h, gpu_fmt)
            : C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt);
        if (!ok) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }
        slot->allocated = 1;
        slot->has_mipmap = want_mips;
    }

    slot->pot_w = pot_w;
    slot->pot_h = pot_h;
    apply_slot_params_to_tex(&slot->tex, slot);

    // УМНАЯ ЗАГРУЗКА
    if (is_solid) {
        upload_solid_texture(&slot->tex, gpu_fmt, pixels, format, type);
    } else {
        const GLvoid *upload_pixels = pixels;

        if ((width > 1024 || height > 1024) && pixels) {
            int bpp = gpu_texfmt_bpp(gpu_fmt);
            /* Reuse tex_staging instead of per-call malloc/free — same buffer
             * grows as needed and stays around for subsequent uploads, no
             * heap fragmentation. */
            void *temp_pixels = get_tex_staging(target_w * target_h * bpp);
            if (temp_pixels) {
                if (gpu_fmt == GPU_RGBA8) downscale_rgba8((uint32_t *) temp_pixels, (const uint32_t *) pixels, width,
                                                          height, target_w, target_h);
                else if (bpp == 2) downscale_16bit((uint16_t *) temp_pixels, (const uint16_t *) pixels, width, height,
                                                   target_w, target_h);
                else downscale_8bit((uint8_t *) temp_pixels, (const uint8_t *) pixels, width, height, target_w,
                                    target_h);
                upload_pixels = temp_pixels;
            }
        }

        upload_texture_pixels(&slot->tex, gpu_fmt, pot_w, pot_h, upload_pixels, target_w, target_h, 0, 0, target_w,
                              target_h, format, type, g.unpack_alignment);
        /* tex_staging is owned by the global state; nothing to free here. */
    }

    // Auto-generate mip levels 1..N for the freshly-uploaded level 0.
    if (slot->has_mipmap && !is_solid) {
        C3D_TexGenerateMipmap(&slot->tex, GPU_TEXFACE_2D);
        // Re-apply filter/LOD: max_level may need to reflect the actual pyramid depth now.
        apply_slot_params_to_tex(&slot->tex, slot);
    }

    if (diag_idx >= 0) {
        printf("[NovaDiag] tex#%d settled: slot->{width=%d height=%d orig=%dx%d pot=%dx%d fmt=%d "
               "solid=%d} wrap=(s=0x%X t=0x%X) filter=(min=0x%X mag=0x%X)\n",
               diag_idx, slot->width, slot->height, slot->orig_width, slot->orig_height,
               slot->pot_w, slot->pot_h, (int)slot->fmt, slot->is_solid_optimized,
               (unsigned)slot->wrap_s, (unsigned)slot->wrap_t,
               (unsigned)slot->min_filter, (unsigned)slot->mag_filter);

        // Dump SWIZZLED contents of the C3D_Tex so we can compare against the raw input.
        // If raw is readable in GIMP but this swizzled dump (de-swizzled offline) is garbled,
        // the bug is inside upload_page_*. If both look correct, the bug is at draw/UV time.
        if (slot->allocated && slot->tex.data) {
            int bpp = gpu_texfmt_bpp(slot->fmt);
            size_t bytes = (size_t)slot->pot_w * (size_t)slot->pot_h * (size_t)bpp;
            char spath[256];
            snprintf(spath, sizeof(spath),
                     "sdmc:/3ds/arxlibertatis/nova_dump/tex%02d_SWIZZLED_%dx%d_bpp%d.bin",
                     diag_idx, slot->pot_w, slot->pot_h, bpp);
            FILE *sf = fopen(spath, "wb");
            if (sf) {
                fwrite(slot->tex.data, bytes, 1, sf);
                fclose(sf);
                printf("[NovaDiag] tex#%d swizzled dump: %zu bytes -> %s\n",
                       diag_idx, bytes, spath);
            }
        }
    }
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels) {
    (void) target;
    /* See glTexImage2D: only level 0 is stored; manual mip uploads are
     * accepted-and-ignored so they can't corrupt the base level. */
    if (level < 0 || width < 0 || height < 0 || xoffset < 0 || yoffset < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (level > 0) return;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated) {
        /* Spec: texture has no defined image to update. */
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    if (!pixels) return;

    // If game wanna draw on full tex -> uncompressing it.
    if (slot->is_solid_optimized) {
        C3D_Tex old_tex = slot->tex;

        int full_target_w = slot->orig_width;
        int full_target_h = slot->orig_height;
        if (full_target_w > 1024 || full_target_h > 1024) {
            float sw = 1024.0f / full_target_w;
            float sh = 1024.0f / full_target_h;
            float s = (sw < sh) ? sw : sh;
            full_target_w = (int) (full_target_w * s);
            full_target_h = (int) (full_target_h * s);
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
                uint32_t col = ((uint32_t *) old_tex.data)[0];
                for (int i = 0; i < full_pot_w * full_pot_h; i++) ((uint32_t *) new_tex.data)[i] = col;
            } else if (bpp == 2) {
                uint16_t col = ((uint16_t *) old_tex.data)[0];
                for (int i = 0; i < full_pot_w * full_pot_h; i++) ((uint16_t *) new_tex.data)[i] = col;
            } else {
                uint8_t col = ((uint8_t *) old_tex.data)[0];
                memset(new_tex.data, col, full_pot_w * full_pot_h);
            }

            /* Deferred delete via the texture GC — the old gspWaitForP3D
             * here didn't even flush the still-CPU-side command buffer
             * (no FrameSplit), so it stalled without protecting anything.
             * The GC keeps the old storage alive until the frame's queued
             * draws have executed. */
            nova_tex_gc_push(&old_tex);
            nova_invalidate_tex_bind((GLuint) (slot - g.textures));

            slot->tex = new_tex;
            slot->pot_w = full_pot_w;
            slot->pot_h = full_pot_h;
            slot->width = full_target_w;
            slot->height = full_target_h;
            slot->is_solid_optimized = 0;
            apply_slot_params_to_tex(&slot->tex, slot);
        }
    }

    float scale_x = slot->orig_width > 0 ? ((float) slot->width / (float) slot->orig_width) : 1.0f;
    float scale_y = slot->orig_height > 0 ? ((float) slot->height / (float) slot->orig_height) : 1.0f;
    float step_x = slot->width > 0 ? ((float) slot->orig_width / (float) slot->width) : 1.0f;
    float step_y = slot->height > 0 ? ((float) slot->orig_height / (float) slot->height) : 1.0f;

    int real_xoffset = (int) (xoffset * scale_x);
    int real_yoffset = (int) (yoffset * scale_y);
    int real_width = (int) (width * scale_x);
    int real_height = (int) (height * scale_y);
    if (real_width < 1) real_width = 1;
    if (real_height < 1) real_height = 1;

    if (slot->fmt == GPU_RGBA8) {
        int row_stride = row_stride_bytes(width, format == GL_RGB ? 3 : 4, g.unpack_alignment);
        uint32_t *tex_data = (uint32_t *) slot->tex.data;

        for (int y = 0; y < real_height; y++) {
            int src_y = (int) (y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t *) pixels + src_y * row_stride;
            int dy = real_yoffset + y;

            for (int x = 0; x < real_width; x++) {
                int src_x = (int) (x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;

                uint32_t out_pixel;
                if (format == GL_RGB) {
                    const uint8_t *px = row + src_x * 3;
                    out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | 0xFFu;
                } else {
                    const uint8_t *px = row + src_x * 4;
                    out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | (
                                    uint32_t) px[3];
                }
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = out_pixel;
            }
        }
        C3D_TexFlush(&slot->tex);
    } else if (gpu_texfmt_bpp(slot->fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, g.unpack_alignment);
        uint16_t *tex_data = (uint16_t *) slot->tex.data;
        for (int y = 0; y < real_height; y++) {
            int src_y = (int) (y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t *) pixels + src_y * row_stride;
            int dy = real_yoffset + y;
            for (int x = 0; x < real_width; x++) {
                int src_x = (int) (x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;

                uint16_t val;
                memcpy(&val, row + src_x * 2, sizeof(uint16_t));
                // И здесь убран переворот
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = val;
            }
        }
        C3D_TexFlush(&slot->tex);
    } else {
        int row_stride = row_stride_bytes(width, 1, g.unpack_alignment);
        uint8_t *tex_data = (uint8_t *) slot->tex.data;
        for (int y = 0; y < real_height; y++) {
            int src_y = (int) (y * step_y);
            if (src_y >= height) src_y = height - 1;
            const uint8_t *row = (const uint8_t *) pixels + src_y * row_stride;
            int dy = real_yoffset + y;
            for (int x = 0; x < real_width; x++) {
                int src_x = (int) (x * step_x);
                if (src_x >= width) src_x = width - 1;
                int dx = real_xoffset + x;
                if (dx < 0 || dy < 0 || dx >= slot->pot_w || dy >= slot->pot_h) continue;
                tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = row[src_x];
            }
        }
        C3D_TexFlush(&slot->tex);
    }

    /* GL 1.4 / ES 1.1 GL_GENERATE_MIPMAP semantics: mip levels regenerate on
     * ANY modification of level 0, including subimage updates. Without this,
     * a texture-atlas update leaves stale mips that bleed old pixels at
     * distance. */
    if (slot->has_mipmap && slot->generate_mipmap) {
        C3D_TexGenerateMipmap(&slot->tex, GPU_TEXFACE_2D);
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_2D) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    /* Spec: invalid enum values are GL_INVALID_ENUM and must NOT modify
     * state. Previously garbage params were stored verbatim and silently
     * decoded as NEAREST / REPEAT downstream.
     *
     * Unchanged-value early-outs: the fast3d backend re-applies sampler
     * params unconditionally per draw flush (per-texture state has no
     * reliable cross-draw cache on its side), so the common case here is
     * "same value again" — skip the apply_slot_params_to_tex recompute. */
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            switch (param) {
                case GL_NEAREST: case GL_LINEAR:
                case GL_NEAREST_MIPMAP_NEAREST: case GL_NEAREST_MIPMAP_LINEAR:
                case GL_LINEAR_MIPMAP_NEAREST: case GL_LINEAR_MIPMAP_LINEAR:
                    if (slot->min_filter == param) return;
                    slot->min_filter = param; break;
                default: g.last_error = GL_INVALID_ENUM; return;
            }
            break;
        case GL_TEXTURE_MAG_FILTER:
            if (param == GL_NEAREST || param == GL_LINEAR) {
                if (slot->mag_filter == param) return;
                slot->mag_filter = param;
            }
            else { g.last_error = GL_INVALID_ENUM; return; }
            break;
        case GL_TEXTURE_WRAP_S:
            if (param == GL_REPEAT || param == GL_CLAMP_TO_EDGE ||
                param == GL_CLAMP || param == GL_MIRRORED_REPEAT) {
                if (slot->wrap_s == param) return;
                slot->wrap_s = param;
            }
            else { g.last_error = GL_INVALID_ENUM; return; }
            break;
        case GL_TEXTURE_WRAP_T:
            if (param == GL_REPEAT || param == GL_CLAMP_TO_EDGE ||
                param == GL_CLAMP || param == GL_MIRRORED_REPEAT) {
                if (slot->wrap_t == param) return;
                slot->wrap_t = param;
            }
            else { g.last_error = GL_INVALID_ENUM; return; }
            break;
        case GL_GENERATE_MIPMAP:
            slot->generate_mipmap = (param != 0);
            break;
        case GL_TEXTURE_MAX_LEVEL:
            if (param < -1) { g.last_error = GL_INVALID_VALUE; return; }
            slot->max_level = param;
            break;
        default:
            g.last_error = GL_INVALID_ENUM;
            return;
    }

    if (!slot->allocated) return;
    apply_slot_params_to_tex(&slot->tex, slot);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { glTexParameteri(target, pname, (GLint) param); }

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (params) glTexParameteri(target, pname, (GLint) params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (params) glTexParameteri(target, pname, params[0]);
}

/* GL ETC1 / ETC1_RGB8 enum (KHR_compressed_texture_etc1). Not in NovaGL.h but
 * the format is constant in the GL spec, so accept it inline here. */
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const GLvoid *data) {
    (void) target; (void) level; (void) border;

    /* ETC1: PICA200 has native sampler support — just hand the pre-compressed
     * bytes to C3D_TexInit(.., GPU_ETC1) and copy them in. 4 bits/pixel — vs
     * 32 for RGBA8 — so 8x VRAM win for textures the engine already shipped
     * compressed (typical GameCube/Wii port atlas). */
    if (internalformat == GL_ETC1_RGB8_OES) {
        GLuint bound = active_bound_texture();
        if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
        TexSlot *slot = &g.textures[bound];

        int pot_w = nova_next_pow2(width);
        int pot_h = nova_next_pow2(height);
        if (pot_w < 8) pot_w = 8;
        if (pot_h < 8) pot_h = 8;

        /* Always orphan on re-definition — in-place overwrite corrupts
         * queued draws (see tex_gc_push). */
        if (slot->allocated) {
            free_texture_storage(slot);
        }
        if (!slot->allocated) {
            if (!C3D_TexInit(&slot->tex, pot_w, pot_h, GPU_ETC1)) {
                g.last_error = GL_OUT_OF_MEMORY;
                return;
            }
            slot->allocated = 1;
        }
        slot->fmt = GPU_ETC1;
        slot->pot_w = pot_w; slot->pot_h = pot_h;
        slot->width = width; slot->height = height;
        slot->orig_width = width; slot->orig_height = height;
        slot->is_solid_optimized = 0;
        slot->has_mipmap = 0;

        if (data && imageSize > 0) {
            int expected = (pot_w * pot_h) / 2; /* ETC1 = 4 bpp */
            int copy = imageSize < expected ? imageSize : expected;
            memcpy(slot->tex.data, data, copy);
            C3D_TexFlush(&slot->tex);
        }
        apply_slot_params_to_tex(&slot->tex, slot);
        return;
    }

    /* Other compressed formats (PVRTC, DXT, …) — PICA200 can't sample them,
     * allocate an empty RGBA8 placeholder so the binding stays valid. */
    (void) data; (void) imageSize;
    glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void glActiveTexture(GLenum texture) {
    int unit = (int) (texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3) g.active_texture_unit = unit;
}

void glClientActiveTexture(GLenum texture) {
    int unit = (int) (texture - GL_TEXTURE0);
    if (unit >= 0 && unit < 3) g.client_active_texture_unit = unit;
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_ENV) return;
    int unit = g.active_texture_unit;

    switch (pname) {
        case GL_TEXTURE_ENV_MODE: g.tex_env_mode[unit] = param;
            break;
        case GL_COMBINE_RGB: g.tex_env_combine_rgb[unit] = param;
            break;
        case GL_SRC0_RGB: g.tex_env_src0_rgb[unit] = param;
            break;
        case GL_SRC1_RGB: g.tex_env_src1_rgb[unit] = param;
            break;
        case GL_SRC2_RGB: g.tex_env_src2_rgb[unit] = param;
            break;
        case GL_OPERAND0_RGB: g.tex_env_operand0_rgb[unit] = param;
            break;
        case GL_OPERAND1_RGB: g.tex_env_operand1_rgb[unit] = param;
            break;
        case GL_OPERAND2_RGB: g.tex_env_operand2_rgb[unit] = param;
            break;
        case GL_COMBINE_ALPHA: g.tex_env_combine_alpha[unit] = param;
            break;
        case GL_SRC0_ALPHA: g.tex_env_src0_alpha[unit] = param;
            break;
        case GL_SRC1_ALPHA: g.tex_env_src1_alpha[unit] = param;
            break;
        case GL_SRC2_ALPHA: g.tex_env_src2_alpha[unit] = param;
            break;
        case GL_OPERAND0_ALPHA: g.tex_env_operand0_alpha[unit] = param;
            break;
        case GL_OPERAND1_ALPHA: g.tex_env_operand1_alpha[unit] = param;
            break;
        case GL_OPERAND2_ALPHA: g.tex_env_operand2_alpha[unit] = param;
            break;
    }
    g.tev_dirty = 1;
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param) { glTexEnvi(target, pname, (GLint) param); }

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (!params) return;
    if (target != GL_TEXTURE_ENV) return;
    if (pname == GL_TEXTURE_ENV_COLOR) {
        /* TEV CONSTANT for the active unit. Stored verbatim — apply_gpu_state
         * packs to 0xAABBGGRR for C3D_TexEnvColor when a stage consumes it. */
        int unit = g.active_texture_unit;
        if (unit >= 0 && unit < 3) {
            g.tex_env_color[unit][0] = params[0];
            g.tex_env_color[unit][1] = params[1];
            g.tex_env_color[unit][2] = params[2];
            g.tex_env_color[unit][3] = params[3];
            g.tev_dirty = 1;
        }
        return;
    }
    glTexEnvi(target, pname, (GLint) params[0]);
}

GLboolean glIsTexture(GLuint texture) {
    if (texture > 0 && texture < NOVA_MAX_TEXTURES && g.textures[texture].in_use) return GL_TRUE;
    return GL_FALSE;
}

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format,
                  GLenum type, const GLvoid *pixels) {
    (void) target;
    (void) level;
    (void) internalformat;
    (void) border;
    glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type,
                     const GLvoid *pixels) {
    (void) target;
    (void) level;
    glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, 0, width, 1, format, type, pixels);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
    (void) level;
    if (target != GL_TEXTURE_2D) return;

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated || !g.current_target) return;

    C3D_FrameBuf *fb = &g.current_target->frameBuf;
    uint32_t *fb_data = (uint32_t *) fb->colorBuf;
    if (!fb_data) return;

    int fb_w = fb->width;
    int fb_h = fb->height;

    uint32_t *tex_data = (uint32_t *) slot->tex.data;
    if (!tex_data) return;

    /* HW fast path: full-area FBO→tex copy (same tiled layout, same RGBA8
     * format). When source is the screen, the axis-swap rule still applies
     * and we have to keep the per-pixel path. */
#ifndef NOVAGL_DISABLE_GLCOPYTEXSUB_HW
    if (g.bound_fbo != 0 && slot->fmt == GPU_RGBA8 &&
        x == 0 && y == 0 && xoffset == 0 && yoffset == 0 &&
        width == fb_w && height == fb_h &&
        slot->pot_w == fb_w && slot->pot_h == fb_h) {
        C3D_FrameSplit(0);
        C3D_SyncTextureCopy(fb_data, 0, tex_data, 0,
                            (u32)(fb_w * fb_h * 4), 8);
        return;
    }
#endif

    C3D_FrameSplit(0);
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
                uint16_t *tex16 = (uint16_t *) slot->tex.data;
                uint8_t r = (pixel >> 24) & 0xFF;
                uint8_t g_c = (pixel >> 16) & 0xFF;
                uint8_t b = (pixel >> 8) & 0xFF;
                uint8_t a = pixel & 0xFF;

                uint16_t val = 0;
                if (slot->fmt == GPU_RGBA4) {
                    val = ((r >> 4) << 12) | ((g_c >> 4) << 8) | ((b >> 4) << 4) | (a >> 4);
                } else if (slot->fmt == GPU_RGB565) {
                    val = ((r >> 3) << 11) | ((g_c >> 2) << 5) | (b >> 3);
                } else if (slot->fmt == GPU_RGBA5551) {
                    val = ((r >> 3) << 11) | ((g_c >> 3) << 6) | ((b >> 3) << 1) | (a >> 7);
                } else {
                    val = ((r >> 3) << 11) | ((g_c >> 2) << 5) | (b >> 3); // Fallback
                }

                tex16[morton_offset_local(dst_x, dst_y, slot->pot_w, slot->pot_h)] = val;
            }
        }
    }

    C3D_TexFlush(&slot->tex);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width,
                      GLsizei height, GLint border) {
    glTexImage2D(target, level, internalformat, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glCopyTexSubImage2D(target, level, 0, 0, x, y, width, height);
}

void glTexGend(GLenum coord, GLenum pname, GLdouble param) {
    (void) coord;
    (void) pname;
    (void) param;
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params) {
    (void) coord;
    (void) pname;
    (void) params;
}

void glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    (void) coord;
    (void) pname;
    (void) param;
}

void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) {
    (void) coord;
    (void) pname;
    (void) params;
}

void glTexGeni(GLenum coord, GLenum pname, GLint param) {
    (void) coord;
    (void) pname;
    (void) param;
}

void glTexGeniv(GLenum coord, GLenum pname, const GLint *params) {
    (void) coord;
    (void) pname;
    (void) params;
}

void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) {
    (void) target;
    (void) s;
    (void) t;
}

void glActiveTextureARB(GLenum texture) { glActiveTexture(texture); }
void glClientActiveTextureARB(GLenum texture) { glClientActiveTexture(texture); }