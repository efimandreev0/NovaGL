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
            gl_set_error(GL_OUT_OF_MEMORY);
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

    // Cap LOD so PICA never samples a non-existent level. The hard ceiling is
    // the texture's PHYSICAL pyramid depth — tex->maxLevel, which
    // C3D_TexInitMipmap derived from the POT dimensions (0 for 8x8, 1 for
    // 16x16, ... 7 for 1024). Telling PICA a larger maxLevel makes it sample
    // uninitialised memory past the real pyramid => garbage at distance. On
    // top of that physical ceiling we honour a tighter caller-supplied
    // GL_TEXTURE_MAX_LEVEL, but never a looser one.
    int real_max = slot->has_mipmap ? (int) tex->maxLevel : 0;
    int max_lod = real_max;
    if (slot->has_mipmap && slot->max_level >= 0 && slot->max_level < max_lod)
        max_lod = slot->max_level;
    if (max_lod < 0) max_lod = 0;
    if (max_lod > 7) max_lod = 7;
    C3D_TexSetLodBias(tex, 0.0f);
    // tex->lodParam: bits  0..7 = bias, 16..19 = maxLevel, 24..27 = minLevel.
    // citro3d has no public setter for max/min level on a non-mipmap-init'd
    // texture, so patch the field directly.
    tex->lodParam = (tex->lodParam & ~(0xFu << 16)) | ((u32)(max_lod & 0xF) << 16);

    GPU_TEXTURE_WRAP_PARAM ws = (slot->wrap_s == GL_CLAMP_TO_EDGE || slot->wrap_s == GL_CLAMP || slot->wrap_s == GL_CLAMP_TO_BORDER)
                                    ? GPU_CLAMP_TO_EDGE
                                    : ((slot->wrap_s == GL_MIRRORED_REPEAT) ? GPU_MIRRORED_REPEAT : GPU_REPEAT);
    GPU_TEXTURE_WRAP_PARAM wt = (slot->wrap_t == GL_CLAMP_TO_EDGE || slot->wrap_t == GL_CLAMP || slot->wrap_t == GL_CLAMP_TO_BORDER)
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
#define NOVA_FRAME_SLOTS   3   /* max frame_buffers */
/* Orphans are bucketed by the frame slot that created them. A bucket is only
 * freed K (= frame_buffers) frames later, once the GPU has finished the frame
 * that referenced it — see novaSwapBuffers. (Single-buffer K=1 is the old
 * 1-frame deferral, safe because SYNCDRAW makes FrameBegin block.) */
static C3D_Tex s_tex_gc[NOVA_FRAME_SLOTS][NOVA_TEX_GC_SLOTS];
static int     s_tex_gc_count[NOVA_FRAME_SLOTS] = {0, 0, 0};

static void tex_gc_free_slot(int s) {
    for (int i = 0; i < s_tex_gc_count[s]; i++) C3D_TexDelete(&s_tex_gc[s][i]);
    s_tex_gc_count[s] = 0;
}

/* Free the orphans parked in the CURRENT slot. Called at swap AFTER rotating to
 * the next slot, so it reclaims the orphans from K frames ago (GPU done). */
void nova_tex_gc_collect(void) {
    tex_gc_free_slot(g.frame_slot);
}

/* Free every slot — only safe when the GPU is idle (nova_fini after FrameEnd +
 * gspWaitForP3D). */
void nova_tex_gc_collect_all(void) {
    for (int s = 0; s < NOVA_FRAME_SLOTS; s++) tex_gc_free_slot(s);
}

void nova_tex_gc_push(C3D_Tex *tex) {
    int s = g.frame_slot;
    if (s_tex_gc_count[s] >= NOVA_TEX_GC_SLOTS) {
        /* Slot full mid-frame (128 orphans in one frame — extreme). Flush the
         * pending draws and wait so this slot's entries become reclaimable. */
        C3D_FrameSplit(0);
        gspWaitForP3D();
        tex_gc_free_slot(s);
    }
    s_tex_gc[s][s_tex_gc_count[s]++] = *tex;
    memset(tex, 0, sizeof(*tex));
}

static void free_texture_storage(TexSlot *slot) {
    if (slot->allocated) {
        /* Any FBO render target wrapping this storage would render into the
         * freed buffer — orphan them first (they're re-wired from the new
         * storage on the next glBindFramebuffer / glFramebufferTexture2D). */
        nova_fbo_orphan_texture_targets((GLuint) (slot - g.textures));
        if (slot->is_cube) {
            /* Cube face buffers are referenced by slot->cube (embedded in the
             * slot), so they can't go through the deferred orphan GC, which
             * stores C3D_Tex by value and would dangle when the slot is reused.
             * Cube maps are typically static (loaded once), so deleting now is
             * fine. */
            C3D_TexDelete(&slot->tex);
            memset(&slot->tex, 0, sizeof(slot->tex));
            memset(&slot->cube, 0, sizeof(slot->cube));
        } else {
            nova_tex_gc_push(&slot->tex);
        }
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
    slot->is_vram = 0;
    slot->is_cube = 0;
    slot->face_loaded[0] = slot->face_loaded[1] = slot->face_loaded[2] = 0;
    slot->face_loaded[3] = slot->face_loaded[4] = slot->face_loaded[5] = 0;
    slot->fmt = (GPU_TEXCOLOR) 0;
    slot->pot_w = 0;
    slot->pot_h = 0;
    slot->width = 0;
    slot->height = 0;
    slot->orig_width = 0;
    slot->orig_height = 0;
}

// Re-home a texture slot's storage in VRAM so it can serve as a PICA render
// target color buffer. Stock glTexImage2D allocates linear RAM, which
// C3D_RenderTargetCreateFromTex accepts but the GPU then renders to the wrong
// physical window (sampling reads back garbage — see the rationale on
// novaCreateRenderTextureFBO in framebuffer.c). glFramebufferTexture2D calls
// this the first time a texture is attached as a color buffer. A freshly
// created render-target texture has no meaningful pixels yet (callers pass NULL
// to glTexImage2D), so discarding the old linear storage and zeroing the new
// VRAM buffer is safe. No-op once VRAM-backed.
int nova_texture_make_vram_target(GLuint texture) {
    if (texture == 0 || texture >= NOVA_MAX_TEXTURES) return 0;
    TexSlot *slot = &g.textures[texture];
    if (!slot->in_use || !slot->allocated) return 0;
    if (slot->is_vram) return 1;
    if (slot->pot_w <= 0 || slot->pot_h <= 0) return 0;

    C3D_Tex newtex;
    if (!C3D_TexInitVRAM(&newtex, (u16) slot->pot_w, (u16) slot->pot_h, slot->fmt)) {
        /* VRAM exhausted. Butterscotch-style fallback: a linear-RAM colour
         * buffer is a valid PICA render target too (citro3d resolves the
         * physical address either way — Butterscotch ships exactly this
         * C3D_TexInitVRAM→C3D_TexInit fallback for its surfaces). Reuse the
         * existing linear storage when it's a flat base-level allocation;
         * re-init flat when it carries mips (targets are single-level). */
        if (slot->has_mipmap || !slot->tex.data) {
            if (!C3D_TexInit(&newtex, (u16) slot->pot_w, (u16) slot->pot_h, slot->fmt)) {
                gl_set_error(GL_OUT_OF_MEMORY);
                return 0;
            }
            C3D_TexDelete(&slot->tex);
            slot->tex = newtex;
        }
        slot->has_mipmap = 0;
        /* is_vram doubles as "storage is render-target-capable — never re-home
         * it again"; set it for the linear fallback too so repeat attaches
         * don't re-zero the texture every call. */
        slot->is_vram = 1;
        apply_slot_params_to_tex(&slot->tex, slot);
        if (slot->tex.data && slot->tex.size > 0) {
            memset(slot->tex.data, 0, slot->tex.size);
            GSPGPU_FlushDataCache(slot->tex.data, slot->tex.size);
        }
        return 1;
    }
    C3D_TexDelete(&slot->tex);
    slot->tex = newtex;
    slot->is_vram = 1;
    slot->has_mipmap = 0; // VRAM render targets are single-level
    apply_slot_params_to_tex(&slot->tex, slot);

    if (slot->tex.data && slot->tex.size > 0) {
        memset(slot->tex.data, 0, slot->tex.size);
        GSPGPU_FlushDataCache(slot->tex.data, slot->tex.size);
    }
    return 1;
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

/* Used only by the opt-in NOVAGL_SOLID_TEX_OPT path; tagged unused so the
 * default (spec-correct) build doesn't warn. */
__attribute__((unused))
static int is_texture_solid(const void *pixels, int width, int height, GLenum format, GLenum type, int alignment) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    int bpp = 4;
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_5_6_5)
        bpp = 2;
    else if (format == GL_LUMINANCE_ALPHA && type == GL_UNSIGNED_BYTE) bpp = 2;
    else if (format == GL_LUMINANCE || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA4_NOVA) bpp = 1;
    else if ((format == GL_RGB || format == GL_BGR) && type == GL_UNSIGNED_BYTE) bpp = 3;

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
        } else if (format == GL_BGR) {
            out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | 0xFFu;
        } else if (format == GL_BGRA) {
            // BGRA byte order -> swap R/B into the GPU's RGBA8 packing
            out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | (uint32_t) px[3];
        } else {
            out_pixel = ((uint32_t) px[0] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[2] << 8) | (uint32_t) px[
                            3];
        }
        uint32_t *dst = (uint32_t *) tex->data;
        for (int i = 0; i < num_pixels; i++) dst[i] = out_pixel;
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        uint16_t val;
        if (fmt == GPU_LA8) {
            /* [L,A] source bytes -> PICA (L<<8)|A (see upload_page_16bit). */
            val = ((uint16_t) px[0] << 8) | px[1];
        } else {
            memcpy(&val, px, sizeof(uint16_t)); /* packed short, layouts match */
        }
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
                } else if (format == GL_BGR) {
                    /* 3 bytes B,G,R -> opaque RGBA8. */
                    const uint8_t *px = row + (src_x0 + x) * 3;
                    out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | 0xFFu;
                } else if (format == GL_BGRA) {
                    const uint8_t *px = row + (src_x0 + x) * 4;
                    out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | (
                                    uint32_t) px[3];
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
                              int src_y0, int copy_w, int copy_h, GPU_TEXCOLOR fmt) {
    uint16_t *dst = (uint16_t *) tex->data;
    /* Two distinct kinds of 2-byte source land here:
     *  - PACKED colour shorts (RGB565 / RGBA5551 / RGBA4): the GL packed-pixel
     *    layout already matches PICA bit-for-bit (R in the MSBs — see the
     *    glReadPixels packer in copyTexImage), so a straight 16-bit copy is
     *    correct.
     *  - GPU_LA8 from GL_LUMINANCE_ALPHA + UNSIGNED_BYTE: this is NOT a packed
     *    short — the source is two independent bytes laid out [L, A]. PICA LA8
     *    wants luminance in the high byte / alpha in the low byte (u16 =
     *    (L<<8)|A), the same "first component in the MSBs" convention RGBA8
     *    uses. A raw copy would swap L and A. Reorder explicitly. */
    int is_la8 = (fmt == GPU_LA8);
    for (int y = 0; y < pot_h; y++) {
        for (int x = 0; x < pot_w; x++) {
            uint16_t val = 0;
            if (x < copy_w && y < copy_h) {
                const uint8_t *row = pixels + (src_y0 + y) * row_stride;
                const uint8_t *p = row + (src_x0 + x) * 2;
                if (is_la8)
                    val = ((uint16_t) p[0] << 8) | p[1];   /* [L,A] -> (L<<8)|A */
                else
                    memcpy(&val, p, sizeof(uint16_t));      /* packed short, layouts match */
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
    (void) type; /* only the opt-in HW-swizzle path below consults it */
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
        int row_stride = row_stride_bytes(width, (format == GL_RGB || format == GL_BGR) ? 3 : 4, unpack_alignment);
        upload_page_rgba8(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h,
                          format);
    } else if (gpu_texfmt_bpp(fmt) == 2) {
        int row_stride = row_stride_bytes(width, 2, unpack_alignment);
        upload_page_16bit(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h,
                          fmt);
    } else {
        int row_stride = row_stride_bytes(width, 1, unpack_alignment);
        upload_page_8bit(tex, pot_w, pot_h, (const uint8_t *) pixels, row_stride, src_x0, src_y0, copy_w, copy_h);
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!textures) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_TEXTURES && g.textures[id].in_use) id++;
        if (id == NOVA_MAX_TEXTURES) {
            gl_set_error(GL_OUT_OF_MEMORY);
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
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!textures) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id > 0 && id < NOVA_MAX_TEXTURES && g.textures[id].in_use) {
            free_texture_storage(&g.textures[id]);
            g.textures[id].in_use = 0;

            /* Unlike a re-spec, DELETING the texture detaches it from any FBO
             * (GL semantics) — otherwise a recycled id would silently re-wire
             * an unrelated texture into an old FBO on the next bind. The
             * targets themselves were already orphaned by free_texture_storage. */
            for (int f = 1; f < NOVA_MAX_FBOS; f++) {
                if (g.fbos[f].in_use && g.fbos[f].color_tex_id == id)
                    g.fbos[f].color_tex_id = 0;
            }

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
    /* ES 1.1 has only GL_TEXTURE_2D; we also accept GL_TEXTURE_CUBE_MAP for
     * desktop-GL ports. NovaGL keeps a single binding slot per unit (a unit
     * samples one texture), so both targets share g.bound_texture[unit] — the
     * object's type is fixed by its first glTexImage2D. Anything else is
     * GL_INVALID_ENUM and must not change the binding. */
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (texture >= NOVA_MAX_TEXTURES) {
        /* Name outside NovaGL's fixed id space (NOVA_MAX_TEXTURES). Real GL would
         * create the object; we can't represent it — GL_INVALID_VALUE is the
         * closest spec error (a bad name value, not an allocation failure). */
        gl_set_error(GL_INVALID_VALUE);
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
    /* A name becomes a real texture OBJECT (glIsTexture == TRUE) only once bound. */
    if (texture != 0) g.textures[texture].bound_once = 1;

    g.bound_texture[g.active_texture_unit] = texture;
}

/* UV scale = logical / POT for a texture id. fast3d (and any GL client)
 * normalizes texcoords to the LOGICAL texture size, but PICA samples [0,1]
 * across the POT-padded storage, so NPOT textures (font glyphs!) need the
 * coords scaled down by this ratio or the image collapses into a fraction of
 * the rect — narrow glyphs degenerate to a vertical sliver ("t/i/l" → "'").
 * POT textures (most world tiles) return 1.0, so callers can multiply
 * unconditionally. Returns 1.0 for unknown/zero ids. */
void novaGetTexCoordScale(GLuint texture, float *su, float *sv) {
    float u = 1.0f, v = 1.0f;
    if (texture > 0 && texture < NOVA_MAX_TEXTURES && g.textures[texture].in_use &&
        g.textures[texture].allocated) {
        TexSlot *s = &g.textures[texture];
        /* `width`/`height` are the on-GPU logical extent (after any >1024
         * downscale); pot_w/pot_h the padded storage. Solid-optimized 8x8
         * stubs keep su=sv=1 (the whole stub is the colour). */
        if (s->pot_w > 0 && s->width > 0)  u = (float) s->width  / (float) s->pot_w;
        if (s->pot_h > 0 && s->height > 0) v = (float) s->height / (float) s->pot_h;
    }
    if (su) *su = u;
    if (sv) *sv = v;
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

/* ── Cube-map texture objects (GL_TEXTURE_CUBE_MAP) ───────────────────────
 * Backed by C3D_TexInitCube. Storage/upload/bind are complete; see api.md for
 * the one caveat: NovaGL's vertex path emits 2-component texcoords, so cube
 * sampling needs (s,t,r) — reflection/skybox texgen is a follow-up. */
static int cube_face_index(GLenum target) {
    switch (target) {
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X: return 0;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X: return 1;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y: return 2;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y: return 3;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z: return 4;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: return 5;
        default: return -1;
    }
}

static void tex_image_cube_face(GLenum target, GLint level, GLsizei width, GLsizei height,
                                GLenum format, GLenum type, const GLvoid *pixels) {
    int face = cube_face_index(target);
    if (face < 0) return;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];

    if (level != 0) return;                 /* cube mips not generated: level 0 only */
    if (width <= 0 || height <= 0) { gl_set_error(GL_INVALID_VALUE); return; }

    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);
    int pot_w = nova_next_pow2(width);
    int pot_h = nova_next_pow2(height);
    if (pot_w < 8) pot_w = 8;
    if (pot_h < 8) pot_h = 8;
    /* PICA cube faces are square and all six share size+format. */
    if (pot_w != pot_h) { int m = pot_w > pot_h ? pot_w : pot_h; pot_w = pot_h = m; }

    /* (Re)allocate the cube on first use or when geometry/format changed. */
    if (slot->allocated && (!slot->is_cube || slot->pot_w != pot_w ||
                            slot->pot_h != pot_h || slot->fmt != gpu_fmt)) {
        free_texture_storage(slot);
    }
    if (!slot->allocated) {
        memset(&slot->cube, 0, sizeof(slot->cube));
        if (!C3D_TexInitCube(&slot->tex, &slot->cube, (u16) pot_w, (u16) pot_h, gpu_fmt)) {
            gl_set_error(GL_OUT_OF_MEMORY);
            return;
        }
        slot->allocated = 1;
        slot->is_cube = 1;
        slot->fmt = gpu_fmt;
        slot->pot_w = pot_w; slot->pot_h = pot_h;
        slot->width = width; slot->height = height;
        slot->orig_width = width; slot->orig_height = height;
        slot->has_mipmap = 0;
        slot->is_solid_optimized = 0;
        for (int i = 0; i < 6; i++) slot->face_loaded[i] = 0;
    }
    apply_slot_params_to_tex(&slot->tex, slot);

    if (pixels) {
        /* Reuse the tested 2D Morton uploader against a throwaway view whose
         * data pointer is this face's buffer — each cube face is laid out
         * exactly like a same-size 2D tiled image. */
        C3D_Tex faceview = slot->tex;
        faceview.data = slot->cube.data[face];
        int cw = width  < pot_w ? width  : pot_w;
        int ch = height < pot_h ? height : pot_h;
        upload_texture_pixels(&faceview, gpu_fmt, pot_w, pot_h, pixels, width, height,
                              0, 0, cw, ch, format, type, g.unpack_alignment);
        C3D_TexFlush(&faceview);
        slot->face_loaded[face] = 1;
    }
}

/* Pixel transfer format/type validation shared by glTexImage2D / glTexSubImage2D
 * / the cube path. Returns 1 only for combinations NovaGL's upload path actually
 * handles correctly, so the GPU_RGBA8 reader is never fed a mismatched buffer. */
__attribute__((unused)) static int tex_format_known(GLenum f) {
    switch (f) {
        case GL_RGBA: case GL_RGBA8_OES: case GL_RGB: case GL_BGRA: case GL_BGR:
        case GL_LUMINANCE: case GL_ALPHA: case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE_ALPHA4_NOVA:
            return 1;
        default: return 0;
    }
}
__attribute__((unused)) static int tex_type_known(GLenum t) {
    switch (t) {
        case GL_UNSIGNED_BYTE: case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1: case GL_UNSIGNED_SHORT_5_6_5:
            return 1;
        default: return 0;
    }
}
__attribute__((unused)) static int tex_format_type_supported(GLenum format, GLenum type) {
    switch (format) {
        case GL_RGBA: case GL_RGBA8_OES:
            return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
                   type == GL_UNSIGNED_SHORT_5_5_5_1;
        case GL_RGB:
            return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5;
        case GL_BGRA: case GL_BGR:
            return type == GL_UNSIGNED_BYTE;             /* R/B swapped at upload */
        case GL_LUMINANCE: case GL_ALPHA:
        case GL_LUMINANCE_ALPHA: case GL_LUMINANCE_ALPHA4_NOVA:
            return type == GL_UNSIGNED_BYTE;
        default: return 0;
    }
}
/* (format,type) validation. LENIENT by default: returns 1 (allow) for any
 * combination, so an unrecognized pair falls through to gl_to_gpu_texfmt's
 * RGBA8 default exactly as the historical code did — this keeps ports that use
 * formats outside NovaGL's explicit list (e.g. BGRA + UNSIGNED_INT_8_8_8_8_REV,
 * which reads 4 bytes/texel just like RGBA8) working. Define
 * -DNOVAGL_STRICT_TEX_FORMAT=1 to instead reject unsupported/mismatched pairs
 * with GL_INVALID_ENUM/OPERATION (catches the over-read on a genuinely wrong
 * combo like RGBA + UNSIGNED_SHORT_5_6_5). */
static int tex_check_format_type(GLenum format, GLenum type) {
#ifdef NOVAGL_STRICT_TEX_FORMAT
    if (tex_format_type_supported(format, type)) return 1;
    gl_set_error((tex_format_known(format) && tex_type_known(type))
                 ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
    return 0;
#else
    (void) format; (void) type;
    return 1;
#endif
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels) {
    /* Cube-map face targets route to the dedicated cube path. */
    if (cube_face_index(target) >= 0) {
        (void) border; (void) internalformat;
        tex_image_cube_face(target, level, width, height, format, type, pixels);
        return;
    }
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
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (level < 0) {
        gl_set_error(GL_INVALID_VALUE);
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

    if (width <= 0 || height <= 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    /* Reject unsupported / mismatched (format,type) BEFORE touching pixels or
     * allocating — previously gl_to_gpu_texfmt silently aliased anything to
     * RGBA8, which over-read mismatched buffers and created a bogus texture. */
    if (!tex_check_format_type(format, type)) return;
    /* width/height > GL_MAX_TEXTURE_SIZE (1024) is auto-downscaled below by
     * default (a graceful fallback for oversized desktop atlases — better than a
     * missing texture on a constrained device). The spec-strict behaviour
     * (GL_INVALID_VALUE, no upload) is opt-in via -DNOVAGL_STRICT_MAX_TEXTURE_SIZE=1. */
#ifdef NOVAGL_STRICT_MAX_TEXTURE_SIZE
    if (width > 1024 || height > 1024) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
#endif

    GPU_TEXCOLOR gpu_fmt = gl_to_gpu_texfmt(format, type);

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

    /* Single-colour textures are collapsed to an 8x8 stub to save VRAM. This is
     * ON by default (it is bit-exact for plain sampling, and the glTexSubImage2D
     * / glCopyTexSubImage2D paths now un-optimize a solid dest before writing,
     * so the previously-observable cases are handled). Turning it OFF
     * (-DNOVAGL_NO_SOLID_TEX_OPT=1) uploads real-size solids — more VRAM, which
     * on VRAM-tight scenes can fail later texture allocations (= missing
     * textures / holes), so keep it on unless you have a reason. */
#ifndef NOVAGL_NO_SOLID_TEX_OPT
    int is_solid = is_texture_solid(pixels, width, height, format, type, g.unpack_alignment);
#else
    int is_solid = 0;
#endif

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
    // C3D_TexGenerateMipmap can only downscale GPU_RGBA8 / GPU_RGB8 — for every
    // other PICA format its inner switch hits `default: break`, so the mip
    // levels are left as uninitialised linearAlloc memory (garbage at distance)
    // while we still burn CPU walking the empty downscale loop. gl_to_gpu_texfmt
    // only ever yields RGBA8 of those two (GL_RGB+UB also maps to RGBA8), so in
    // practice this gates on RGBA8. Mip pyramids for 16-/8-bit formats would
    // need custom tiled (Morton) downscalers — TODO, see C3Di_DownscaleRGBA8.
    //
    // Also require min dimension >= 16: C3D_TexCalcMaxLevel returns 0 below
    // that (8x8 -> level 0 only), so a sub-16 "mipmapped" texture has no real
    // pyramid to generate and shouldn't claim has_mipmap.
    int fmt_mipmappable = (gpu_fmt == GPU_RGBA8 || gpu_fmt == GPU_RGB8);
    int want_mips = slot->generate_mipmap && fmt_mipmappable && !is_solid &&
                    pot_w >= 16 && pot_h >= 16;

    /* (The old "mip-state changed → re-init" check is gone: re-definition
     * orphans unconditionally above, so the slot is never allocated here.) */
    if (!slot->allocated) {
        bool ok = want_mips
            ? C3D_TexInitMipmap(&slot->tex, pot_w, pot_h, gpu_fmt)
            : C3D_TexInit(&slot->tex, pot_w, pot_h, gpu_fmt);
        if (!ok) {
            gl_set_error(GL_OUT_OF_MEMORY);
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
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (level > 0) return;
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated) {
        /* Spec: texture has no defined image to update. */
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    /* Cube faces aren't sub-updatable through this 2D path — slot->tex.data is
     * the C3D_TexCube pointer for a cube, so the 2D Morton writes below would
     * corrupt the cube struct. Cube faces are upload-once via glTexImage2D.
     * (A full cube sub-update path can be added later if a port needs it.) */
    if (slot->is_cube || cube_face_index(target) >= 0) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    /* Spec: the sub-rectangle must lie within the texture's defined dimensions
     * (the original glTexImage2D width/height); otherwise GL_INVALID_VALUE. */
    if ((GLint)(xoffset + width) > slot->orig_width ||
        (GLint)(yoffset + height) > slot->orig_height) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!tex_check_format_type(format, type)) return;
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
        int row_stride = row_stride_bytes(width, (format == GL_RGB || format == GL_BGR) ? 3 : 4, g.unpack_alignment);
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
                } else if (format == GL_BGR) {
                    const uint8_t *px = row + src_x * 3;
                    out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | 0xFFu;
                } else if (format == GL_BGRA) {
                    const uint8_t *px = row + src_x * 4;
                    out_pixel = ((uint32_t) px[2] << 24) | ((uint32_t) px[1] << 16) | ((uint32_t) px[0] << 8) | (
                                    uint32_t) px[3];
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

        if (xoffset == 0 && yoffset == 0 &&
            width == slot->orig_width && height == slot->orig_height &&
            slot->width == slot->orig_width && slot->height == slot->orig_height &&
            row_stride == width * 2 && slot->fmt != GPU_LA8) {
            swizzle_16bit((uint16_t *) slot->tex.data, (const uint16_t *) pixels,
                          width, height, slot->pot_w, slot->pot_h);
        } else {
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
                    const uint8_t *sp = row + src_x * 2;
                    if (slot->fmt == GPU_LA8)
                        val = ((uint16_t) sp[0] << 8) | sp[1];   /* [L,A] -> (L<<8)|A, see upload_page_16bit */
                    else
                        memcpy(&val, sp, sizeof(uint16_t));       /* packed short, layouts match */
                    // И здесь убран переворот
                    tex_data[morton_offset_local(dx, dy, slot->pot_w, slot->pot_h)] = val;
                }
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

/* GL3-style explicit mipmap generation. NovaGL builds mip levels at
 * glTexImage2D time when GL_GENERATE_MIPMAP is enabled, so here we only need to
 * validate the target. (Full on-demand regeneration could be added later.) */
void glGenerateMipmap(GLenum target) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated) return;
    /* If the texture already owns a mip pyramid (allocated via C3D_TexInitMipmap)
     * and the format is HW-downscalable, regenerate levels 1..N from the current
     * level 0 — this is the real on-demand path (e.g. after glTexSubImage2D
     * changed the base level). Without an existing pyramid there is nothing to
     * write into (and PICA texture memory isn't cheaply readable to re-init), so
     * just arm GL_GENERATE_MIPMAP for the next upload as a best effort. */
    if (slot->has_mipmap && (slot->fmt == GPU_RGBA8 || slot->fmt == GPU_RGB8) && !slot->is_cube) {
        C3D_TexGenerateMipmap(&slot->tex, GPU_TEXFACE_2D);
        apply_slot_params_to_tex(&slot->tex, slot);
    } else {
        slot->generate_mipmap = 1;
    }
}

/* Desktop-GL texture readback. PICA200 texture memory is Morton-tiled in VRAM
 * and not cheaply/portably readable here; these are no-op stubs so gl3 readback
 * paths (needToReadBackTextures == false on this profile) link and run. */
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels) {
    (void)target; (void)level; (void)format; (void)type; (void)pixels;
}

void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *pixels) {
    (void)target; (void)level; (void)pixels;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        gl_set_error(GL_INVALID_ENUM);
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
                default: gl_set_error(GL_INVALID_ENUM); return;
            }
            break;
        case GL_TEXTURE_MAG_FILTER:
            if (param == GL_NEAREST || param == GL_LINEAR) {
                if (slot->mag_filter == param) return;
                slot->mag_filter = param;
            }
            else { gl_set_error(GL_INVALID_ENUM); return; }
            break;
        case GL_TEXTURE_WRAP_S:
            if (param == GL_REPEAT || param == GL_CLAMP_TO_EDGE ||
                param == GL_CLAMP || param == GL_MIRRORED_REPEAT ||
                param == GL_CLAMP_TO_BORDER) {
                if (slot->wrap_s == param) return;
                slot->wrap_s = param;
            }
            else { gl_set_error(GL_INVALID_ENUM); return; }
            break;
        case GL_TEXTURE_WRAP_T:
            if (param == GL_REPEAT || param == GL_CLAMP_TO_EDGE ||
                param == GL_CLAMP || param == GL_MIRRORED_REPEAT ||
                param == GL_CLAMP_TO_BORDER) {
                if (slot->wrap_t == param) return;
                slot->wrap_t = param;
            }
            else { gl_set_error(GL_INVALID_ENUM); return; }
            break;
        case GL_GENERATE_MIPMAP:
            slot->generate_mipmap = (param != 0);
            break;
        case GL_TEXTURE_MAX_LEVEL:
            if (param < -1) { gl_set_error(GL_INVALID_VALUE); return; }
            slot->max_level = param;
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
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

/* ── Texture parameter / tex-env state queries (GLES 1.1) ─────────────────
 * Engines query sampler/tex-env state they previously set; returning the
 * tracked value keeps glGet round-trips honest. */
static GLint get_tex_param(const TexSlot *slot, GLenum pname) {
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: return slot->min_filter;
        case GL_TEXTURE_MAG_FILTER: return slot->mag_filter;
        case GL_TEXTURE_WRAP_S:     return slot->wrap_s;
        case GL_TEXTURE_WRAP_T:     return slot->wrap_t;
        case GL_GENERATE_MIPMAP:    return slot->generate_mipmap;
        case GL_TEXTURE_MAX_LEVEL:  return slot->max_level < 0 ? 0 : slot->max_level;
        default:                    return 0;
    }
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    if ((target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) || !params) {
        if (!params) return;
        gl_set_error(GL_INVALID_ENUM); return;
    }
    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    params[0] = get_tex_param(&g.textures[bound], pname);
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    if (!params) return;
    GLint v = 0;
    glGetTexParameteriv(target, pname, &v);
    params[0] = (GLfloat) v;
}

static GLint get_tex_env_param(int unit, GLenum pname) {
    switch (pname) {
        case GL_TEXTURE_ENV_MODE:  return g.tex_env_mode[unit];
        case GL_COMBINE_RGB:       return g.tex_env_combine_rgb[unit];
        case GL_COMBINE_ALPHA:     return g.tex_env_combine_alpha[unit];
        case GL_SRC0_RGB:          return g.tex_env_src0_rgb[unit];
        case GL_SRC1_RGB:          return g.tex_env_src1_rgb[unit];
        case GL_SRC2_RGB:          return g.tex_env_src2_rgb[unit];
        case GL_SRC0_ALPHA:        return g.tex_env_src0_alpha[unit];
        case GL_SRC1_ALPHA:        return g.tex_env_src1_alpha[unit];
        case GL_SRC2_ALPHA:        return g.tex_env_src2_alpha[unit];
        case GL_OPERAND0_RGB:      return g.tex_env_operand0_rgb[unit];
        case GL_OPERAND1_RGB:      return g.tex_env_operand1_rgb[unit];
        case GL_OPERAND2_RGB:      return g.tex_env_operand2_rgb[unit];
        case GL_OPERAND0_ALPHA:    return g.tex_env_operand0_alpha[unit];
        case GL_OPERAND1_ALPHA:    return g.tex_env_operand1_alpha[unit];
        case GL_OPERAND2_ALPHA:    return g.tex_env_operand2_alpha[unit];
        case GL_RGB_SCALE:         return g.tex_env_rgb_scale[unit];
        case GL_ALPHA_SCALE:       return g.tex_env_alpha_scale[unit];
        default:                   return 0;
    }
}

void glGetTexEnviv(GLenum target, GLenum pname, GLint *params) {
    if (target != GL_TEXTURE_ENV || !params) return;
    int unit = g.active_texture_unit;
    if (unit < 0 || unit >= 3) return;
    params[0] = get_tex_env_param(unit, pname);
}

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) {
    if (target != GL_TEXTURE_ENV || !params) return;
    int unit = g.active_texture_unit;
    if (unit < 0 || unit >= 3) return;
    if (pname == GL_TEXTURE_ENV_COLOR) {
        params[0] = g.tex_env_color[unit][0]; params[1] = g.tex_env_color[unit][1];
        params[2] = g.tex_env_color[unit][2]; params[3] = g.tex_env_color[unit][3];
        return;
    }
    params[0] = (GLfloat) get_tex_env_param(unit, pname);
}

/* Vector form of glTexEnvi. GL_TEXTURE_ENV_COLOR takes a 4-int colour
 * (0..255 → 0..1); everything else is scalar and forwards to glTexEnvi. */
void glTexEnviv(GLenum target, GLenum pname, const GLint *params) {
    if (!params) return;
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR) {
        int unit = g.active_texture_unit;
        if (unit < 0 || unit >= 3) return;
        for (int i = 0; i < 4; i++) g.tex_env_color[unit][i] = (GLfloat) params[i] / 255.0f;
        g.tev_dirty = 1;
        return;
    }
    glTexEnvi(target, pname, params[0]);
}

// ETC1 enum, not in our header but it constant in the spec so define inline
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

/* ── S3TC / DXT CPU decompression ─────────────────────────────────────────
 * PICA can only sample ETC1, so DXT1/3/5 (BC1/2/3) are decompressed to RGBA8
 * here and pushed through the normal glTexImage2D path (which Morton-swizzles,
 * pads to POT and can build mips). Output is row-major [R,G,B,A] bytes, exactly
 * what GL_RGBA + GL_UNSIGNED_BYTE expects. This is where DXT-shipping desktop
 * ports (re3 et al.) get their textures back instead of the old transparent
 * placeholder. */
static void dxt_color_palette(const uint8_t *cb, int allow_3color, uint8_t pal[4][4]) {
    uint16_t c0 = (uint16_t) (cb[0] | (cb[1] << 8));
    uint16_t c1 = (uint16_t) (cb[2] | (cb[3] << 8));
    uint8_t r0 = (uint8_t) ((c0 >> 11) & 0x1F), g0 = (uint8_t) ((c0 >> 5) & 0x3F), b0 = (uint8_t) (c0 & 0x1F);
    uint8_t r1 = (uint8_t) ((c1 >> 11) & 0x1F), g1 = (uint8_t) ((c1 >> 5) & 0x3F), b1 = (uint8_t) (c1 & 0x1F);
    pal[0][0] = (uint8_t) ((r0 << 3) | (r0 >> 2)); pal[0][1] = (uint8_t) ((g0 << 2) | (g0 >> 4));
    pal[0][2] = (uint8_t) ((b0 << 3) | (b0 >> 2)); pal[0][3] = 255;
    pal[1][0] = (uint8_t) ((r1 << 3) | (r1 >> 2)); pal[1][1] = (uint8_t) ((g1 << 2) | (g1 >> 4));
    pal[1][2] = (uint8_t) ((b1 << 3) | (b1 >> 2)); pal[1][3] = 255;
    /* c0 > c1 selects the 4-colour (opaque) interpolation; otherwise the
     * 3-colour + transparent-black mode. DXT3/DXT5 carry alpha separately and
     * are always 4-colour, so they pass allow_3color = 0. */
    if (c0 > c1 || !allow_3color) {
        for (int k = 0; k < 3; k++) {
            pal[2][k] = (uint8_t) ((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (uint8_t) ((pal[0][k] + 2 * pal[1][k]) / 3);
        }
        pal[2][3] = 255; pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; k++) pal[2][k] = (uint8_t) ((pal[0][k] + pal[1][k]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = 0; pal[3][3] = 0; /* transparent black */
    }
}

/* kind: 1 = DXT1, 3 = DXT3, 5 = DXT5. force_opaque pins alpha to 255 (the
 * GL_COMPRESSED_RGB_S3TC_DXT1 token, which ignores the 1-bit alpha). */
static void decode_s3tc_image(const uint8_t *src, int w, int h, int kind, int force_opaque, uint8_t *out) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    int block_bytes = (kind == 1) ? 8 : 16;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t *blk = src + (size_t) (by * bw + bx) * block_bytes;
            const uint8_t *cb = (kind == 1) ? blk : blk + 8;   /* colour half */
            uint8_t pal[4][4];
            dxt_color_palette(cb, kind == 1, pal);
            uint32_t cidx = (uint32_t) cb[4] | ((uint32_t) cb[5] << 8) | ((uint32_t) cb[6] << 16) | ((uint32_t) cb[7] << 24);

            uint8_t a8[16];
            if (kind == 3) {
                for (int i = 0; i < 16; i++) {
                    uint8_t nib = (uint8_t) ((blk[i >> 1] >> ((i & 1) * 4)) & 0xF);
                    a8[i] = (uint8_t) (nib * 17);              /* 0..15 -> 0..255 */
                }
            } else if (kind == 5) {
                uint8_t a0 = blk[0], a1 = blk[1], ap[8];
                ap[0] = a0; ap[1] = a1;
                if (a0 > a1) {
                    for (int k = 1; k <= 6; k++) ap[1 + k] = (uint8_t) (((7 - k) * a0 + k * a1) / 7);
                } else {
                    for (int k = 1; k <= 4; k++) ap[1 + k] = (uint8_t) (((5 - k) * a0 + k * a1) / 5);
                    ap[6] = 0; ap[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= (uint64_t) blk[2 + i] << (8 * i);
                for (int i = 0; i < 16; i++) a8[i] = ap[(bits >> (3 * i)) & 7];
            }

            for (int py = 0; py < 4; py++) {
                int y = by * 4 + py;
                if (y >= h) continue;
                for (int px = 0; px < 4; px++) {
                    int x = bx * 4 + px;
                    if (x >= w) continue;
                    int i = py * 4 + px;
                    int s = (cidx >> (2 * i)) & 3;
                    uint8_t *o = out + ((size_t) y * w + x) * 4;
                    o[0] = pal[s][0]; o[1] = pal[s][1]; o[2] = pal[s][2];
                    if (force_opaque)      o[3] = 255;
                    else if (kind == 1)    o[3] = pal[s][3];
                    else                   o[3] = a8[i];
                }
            }
        }
    }
}

/* Maps an S3TC token to (kind, force_opaque, block_bytes); returns 0 if not S3TC. */
static int s3tc_describe(GLenum internalformat, int *kind, int *force_opaque, int *block_bytes) {
    switch (internalformat) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:  *kind = 1; *force_opaque = 1; *block_bytes = 8;  return 1;
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: *kind = 1; *force_opaque = 0; *block_bytes = 8;  return 1;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: *kind = 3; *force_opaque = 0; *block_bytes = 16; return 1;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: *kind = 5; *force_opaque = 0; *block_bytes = 16; return 1;
        default: return 0;
    }
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const GLvoid *data) {
    (void) target; (void) level; (void) border;

    /* DXT1/3/5: decompress to RGBA8 and re-enter through the normal path. */
    int kind, force_opaque, block_bytes;
    if (s3tc_describe(internalformat, &kind, &force_opaque, &block_bytes)) {
        if (width <= 0 || height <= 0) { gl_set_error(GL_INVALID_VALUE); return; }
        int need = ((width + 3) / 4) * ((height + 3) / 4) * block_bytes;
        if (!data || imageSize < need) { gl_set_error(GL_INVALID_VALUE); return; }
        uint8_t *rgba = (uint8_t *) malloc((size_t) width * (size_t) height * 4);
        if (!rgba) { gl_set_error(GL_OUT_OF_MEMORY); return; }
        decode_s3tc_image((const uint8_t *) data, width, height, kind, force_opaque, rgba);
        glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        return;
    }

    // ETC1 (4 bpp, no alpha) and ETC1A4 (8 bpp, RGB + 4-bit alpha) the PICA can
    // sample by itself, just feed the tiled bytes. vs RGBA8 that's 8x / 4x less
    // vram — exactly what the repacked 3DS sprite atlases use.
    if (internalformat == GL_ETC1_RGB8_OES || internalformat == GL_ETC1_RGB8A4_NOVA) {
        int a4 = (internalformat == GL_ETC1_RGB8A4_NOVA);
        GPU_TEXCOLOR gpuFmt = a4 ? GPU_ETC1A4 : GPU_ETC1;

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
            if (!C3D_TexInit(&slot->tex, pot_w, pot_h, gpuFmt)) {
                gl_set_error(GL_OUT_OF_MEMORY);
                return;
            }
            slot->allocated = 1;
        }
        slot->fmt = gpuFmt;
        slot->pot_w = pot_w; slot->pot_h = pot_h;
        slot->width = width; slot->height = height;
        slot->orig_width = width; slot->orig_height = height;
        slot->is_solid_optimized = 0;
        slot->has_mipmap = 0;

        if (data && imageSize > 0) {
            int expected = a4 ? (pot_w * pot_h) : (pot_w * pot_h) / 2; /* 8 / 4 bpp */
            int copy = imageSize < expected ? imageSize : expected;
            memcpy(slot->tex.data, data, copy);
            C3D_TexFlush(&slot->tex);
        }
        apply_slot_params_to_tex(&slot->tex, slot);
        return;
    }

    // Remaining compressed formats (PVRTC, ETC2/EAC, BPTC, ...) have no PICA
    // path and no CPU decoder here. Per spec an unsupported compressed
    // internalformat is GL_INVALID_ENUM — surface that instead of silently
    // binding an empty placeholder (which read back as transparent garbage and
    // looked like a successful upload).
    (void) data; (void) imageSize; (void) width; (void) height; (void) border;
    static int warned = 0;
    if (!warned) {
        printf("[Nova]: compressed format 0x%04X not supported (no decoder)\n",
               (unsigned) internalformat);
        warned = 1;
    }
    gl_set_error(GL_INVALID_ENUM);
}

/* DXT sub-update: decompress the (4-aligned) sub-rect to RGBA8 and forward to
 * glTexSubImage2D. ETC1/other formats can't be partially updated this way. */
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                               const GLvoid *data) {
    int kind, force_opaque, block_bytes;
    if (!s3tc_describe(format, &kind, &force_opaque, &block_bytes)) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (width <= 0 || height <= 0) { gl_set_error(GL_INVALID_VALUE); return; }
    int need = ((width + 3) / 4) * ((height + 3) / 4) * block_bytes;
    if (!data || imageSize < need) { gl_set_error(GL_INVALID_VALUE); return; }
    uint8_t *rgba = (uint8_t *) malloc((size_t) width * (size_t) height * 4);
    if (!rgba) { gl_set_error(GL_OUT_OF_MEMORY); return; }
    decode_s3tc_image((const uint8_t *) data, width, height, kind, force_opaque, rgba);
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
}

void glActiveTexture(GLenum texture) {
    int unit = (int) (texture - GL_TEXTURE0);
    if (unit >= 0 && unit < NOVA_MAX_TEXTURE_UNITS) g.active_texture_unit = unit;
    else gl_set_error(GL_INVALID_ENUM); /* spec: unit out of range, unchanged */
}

void glClientActiveTexture(GLenum texture) {
    int unit = (int) (texture - GL_TEXTURE0);
    if (unit >= 0 && unit < NOVA_MAX_TEXTURE_UNITS) g.client_active_texture_unit = unit;
    else gl_set_error(GL_INVALID_ENUM);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    /* GL_POINT_SPRITE / GL_TEXTURE_FILTER_CONTROL are valid glTexEnv targets we
     * don't implement — accept silently; anything else is GL_INVALID_ENUM. */
    if (target != GL_TEXTURE_ENV) {
        if (target == 0x8861 /*GL_POINT_SPRITE*/ || target == GL_TEXTURE_FILTER_CONTROL) return;
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
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
        case GL_RGB_SCALE:
            /* GL permits only 1, 2 or 4. glTexEnvf passes 2.0f/4.0f -> 2/4. */
            if (param == 1 || param == 2 || param == 4) g.tex_env_rgb_scale[unit] = param;
            else { gl_set_error(GL_INVALID_VALUE); return; }
            break;
        case GL_ALPHA_SCALE:
            if (param == 1 || param == 2 || param == 4) g.tex_env_alpha_scale[unit] = param;
            else { gl_set_error(GL_INVALID_VALUE); return; }
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
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
    /* Spec: TRUE only for a name that has been BOUND at least once (a reserved-
     * but-never-bound glGenTextures name is not yet a texture object). */
    if (texture > 0 && texture < NOVA_MAX_TEXTURES &&
        g.textures[texture].in_use && g.textures[texture].bound_once) return GL_TRUE;
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
    if (target != GL_TEXTURE_2D) { gl_set_error(GL_INVALID_ENUM); return; }
    if (width < 0 || height < 0 || xoffset < 0 || yoffset < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    GLuint bound = active_bound_texture();
    if (bound == 0 || bound >= NOVA_MAX_TEXTURES) return;
    TexSlot *slot = &g.textures[bound];
    if (!slot->allocated) { gl_set_error(GL_INVALID_OPERATION); return; }
    if (!g.current_target) return;
    /* This reads the framebuffer back — commit pending draws so the copy
     * captures this frame's full geometry. */
    nova_batch_flush();
    /* A solid-optimized destination (opt-in NOVAGL_SOLID_TEX_OPT) is an 8x8 stub;
     * copying framebuffer pixels into it would clip to 8x8. Promote it to its
     * real size first so the copy lands correctly (matches glTexSubImage2D). */
    if (slot->is_solid_optimized && (width > slot->pot_w || height > slot->pot_h)) {
        /* NULL pixels => allocate real-size storage without a solid re-check or
         * any source read; the framebuffer copy below fills it. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, slot->orig_width, slot->orig_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    /* GPU fast path (Butterscotch's ctr_create_surf_ex pattern): full-texture
     * snapshots — the sprite_create_from_surface family, and everything coming
     * through glCopyTexImage2D — are drawn by the GPU into the destination
     * instead of the per-pixel morton loop + full GPU sync below. The GPU
     * handles the tiling, the format conversion and the sideways-screen
     * rotation; typical cost drops from ~8-10ms CPU + sync stall to one quad. */
    if (nova_copy_read_fb_to_texture(bound, xoffset, yoffset, x, y, width, height))
        return;

    /* --- CPU fallback ---------------------------------------------------- *
     * Source = the GL_READ_FRAMEBUFFER binding (same as glReadPixels and the
     * blit path). The screen is stored sideways (axis swap below); FBO content
     * is landscape and copies 1:1 — the old code applied the screen swap to
     * FBO sources too, transposing every FBO readback into garbage. */
    C3D_RenderTarget *src_tgt;
    int src_is_screen;
    if (g.bound_read_fbo != 0) {
        src_is_screen = 0;
        src_tgt = (g.bound_read_fbo < NOVA_MAX_FBOS && g.fbos[g.bound_read_fbo].in_use)
                      ? g.fbos[g.bound_read_fbo].target : NULL;
    } else {
        src_is_screen = 1;
        src_tgt = g.app_target ? g.app_target : g.current_target;
    }
    if (!src_tgt) return;

    C3D_FrameBuf *fb = &src_tgt->frameBuf;
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
    if (!src_is_screen && slot->fmt == GPU_RGBA8 &&
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

            int phys_x, phys_y;
            if (src_is_screen) {
                /* Screen storage is sideways: fb column = logical y, row = x. */
                phys_x = logical_y;
                phys_y = logical_x;
            } else {
                /* FBO storage is landscape — identity mapping (matches the
                 * raw-layout HW fast path above). */
                phys_x = logical_x;
                phys_y = logical_y;
            }

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
                } else if (slot->fmt == GPU_LA8) {
                    /* Luminance = Rec.601 weighting of RGB; PICA LA8 = (L<<8)|A. */
                    uint8_t l = (uint8_t) ((r * 77 + g_c * 150 + b * 29) >> 8);
                    val = ((uint16_t) l << 8) | a;
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


static NovaTexGenCoord g_texgen[NOVA_MAX_TEXTURE_UNITS][4] = {{{0}}};
static int g_texgen_init = 0;

static int texgen_coord_index(GLenum coord) {
    switch (coord) {
        case GL_S: return 0;
        case GL_T: return 1;
        case GL_R: return 2;
        case GL_Q: return 3;
        default:   return -1;
    }
}

static void texgen_ensure_defaults(void) {
    if (g_texgen_init) return;
    g_texgen_init = 1;
    for (int u = 0; u < NOVA_MAX_TEXTURE_UNITS; u++) {
        for (int c = 0; c < 4; c++) {
            g_texgen[u][c].mode = GL_EYE_LINEAR;
            /* Spec defaults: S plane = (1,0,0,0), T = (0,1,0,0), R/Q = 0. */
            g_texgen[u][c].object_plane[0] = (c == 0) ? 1.0f : 0.0f;
            g_texgen[u][c].object_plane[1] = (c == 1) ? 1.0f : 0.0f;
            g_texgen[u][c].object_plane[2] = 0.0f;
            g_texgen[u][c].object_plane[3] = 0.0f;
            memcpy(g_texgen[u][c].eye_plane, g_texgen[u][c].object_plane, sizeof(GLfloat) * 4);
        }
    }
}

void glTexGeni(GLenum coord, GLenum pname, GLint param) {
    texgen_ensure_defaults();
    int c = texgen_coord_index(coord);
    if (c < 0) { gl_set_error(GL_INVALID_ENUM); return; }
    if (pname != GL_TEXTURE_GEN_MODE) { gl_set_error(GL_INVALID_ENUM); return; }
    if (param != GL_OBJECT_LINEAR && param != GL_EYE_LINEAR && param != GL_SPHERE_MAP) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    int u = g.active_texture_unit;
    if (u < 0 || u >= NOVA_MAX_TEXTURE_UNITS) u = 0;
    g_texgen[u][c].mode = (GLenum) param;
}

void glTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    /* The only scalar pname is GL_TEXTURE_GEN_MODE; planes need the *v form. */
    glTexGeni(coord, pname, (GLint) param);
}

void glTexGend(GLenum coord, GLenum pname, GLdouble param) {
    glTexGeni(coord, pname, (GLint) param);
}

void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) {
    texgen_ensure_defaults();
    if (!params) return;
    int c = texgen_coord_index(coord);
    if (c < 0) { gl_set_error(GL_INVALID_ENUM); return; }
    int u = g.active_texture_unit;
    if (u < 0 || u >= NOVA_MAX_TEXTURE_UNITS) u = 0;
    NovaTexGenCoord *tg = &g_texgen[u][c];
    switch (pname) {
        case GL_TEXTURE_GEN_MODE:
            glTexGeni(coord, pname, (GLint) params[0]);
            break;
        case GL_OBJECT_PLANE:
            memcpy(tg->object_plane, params, sizeof(GLfloat) * 4);
            break;
        case GL_EYE_PLANE:
            /* Spec: the eye plane is stored transformed by the current
             * inverse-modelview at call time. NovaGL keeps the raw plane
             * (the modelview here is the GL fixed-function stack); a client
             * relying on the exact eye-linear transform would need that
             * multiply — recorded verbabumped on any glLighttim is correct for the common
             * identity-modelview setup. */
            memcpy(tg->eye_plane, params, sizeof(GLfloat) * 4);
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            break;
    }
}

void glTexGeniv(GLenum coord, GLenum pname, const GLint *params) {
    if (!params) return;
    if (pname == GL_TEXTURE_GEN_MODE) {
        glTexGeni(coord, pname, params[0]);
        return;
    }
    GLfloat f[4] = {
        (GLfloat) params[0], (GLfloat) params[1],
        (GLfloat) params[2], (GLfloat) params[3]
    };
    glTexGenfv(coord, pname, f);
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params) {
    if (!params) return;
    if (pname == GL_TEXTURE_GEN_MODE) {
        glTexGeni(coord, pname, (GLint) params[0]);
        return;
    }
    GLfloat f[4] = {
        (GLfloat) params[0], (GLfloat) params[1],
        (GLfloat) params[2], (GLfloat) params[3]
    };
    glTexGenfv(coord, pname, f);
}

/* GL_ARB_multitexture immediate texcoord. NovaGL's immediate path tracks a
 * single active texcoord set (PICA TEV samples both units with TEXCOORD0/1
 * but the FFP immediate emitter only carries one). For texture unit 0 this is
 * exactly glTexCoord2f; higher units have no immediate-mode storage, so we
 * apply unit 0 and ignore the rest (matches how the draw path consumes a
 * single texcoord stream). */
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) {
    if (target == GL_TEXTURE0 || target == GL_TEXTURE0_ARB) {
        glTexCoord2f(s, t);
    }
    /* else: unit >= 1 immediate texcoords are not represented; drop silently
     * (no error — valid multitexture call, just unsupported storage here). */
}

void glActiveTextureARB(GLenum texture) { glActiveTexture(texture); }
void glClientActiveTextureARB(GLenum texture) { glClientActiveTexture(texture); }