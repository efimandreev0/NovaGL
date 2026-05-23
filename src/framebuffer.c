//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

#define NOVA_FBO_GC_TARGETS 128
static C3D_RenderTarget *s_fbo_gc_targets[NOVA_FBO_GC_TARGETS];
static int s_fbo_gc_count = 0;

static void nova_queue_render_target_delete(C3D_RenderTarget *target) {
    if (!target) return;
    if (s_fbo_gc_count < NOVA_FBO_GC_TARGETS) {
        s_fbo_gc_targets[s_fbo_gc_count++] = target;
    } else {
        C3D_RenderTargetDelete(target);
    }
}

void nova_fbo_gc_collect(void) {
    for (int i = 0; i < s_fbo_gc_count; i++) {
        if (s_fbo_gc_targets[i]) {
            C3D_RenderTargetDelete(s_fbo_gc_targets[i]);
            s_fbo_gc_targets[i] = NULL;
        }
    }
    s_fbo_gc_count = 0;
}

static inline int fb_morton_offset(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int) morton_interleave((uint32_t) (x & 7), (uint32_t) (fy & 7));
}

void glGenFramebuffers(GLsizei n, GLuint *ids) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 0;
        for (int slot = 1; slot < NOVA_MAX_FBOS; slot++) {
            if (!g.fbos[slot].in_use) {
                g.fbos[slot].in_use = 1;
                g.fbos[slot].target = NULL;
                g.fbos[slot].color_tex_id = 0;
                id = (GLuint) slot;
                break;
            }
        }
        if (!id) {
            g.last_error = GL_OUT_OF_MEMORY;
        }
        ids[i] = id;
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint *ids) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = ids[i];
        if (id == 0 || id >= NOVA_MAX_FBOS) continue;
        if (!g.fbos[id].in_use) continue;
        if (g.bound_fbo == id) {
            g.bound_fbo = 0;
            C3D_FrameDrawOn(g.render_target_top);
            g.current_target = g.render_target_top;
        }
        if (g.fbos[id].target) {
            nova_queue_render_target_delete(g.fbos[id].target);
            g.fbos[id].target = NULL;
        }
        g.fbos[id].color_tex_id = 0;
        g.fbos[id].in_use = 0;
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    (void) target;
    C3D_RenderTarget *new_target;
    GLuint new_bound;
    if (framebuffer == 0) {
        new_target = g.render_target_top;
        new_bound = 0;
    } else {
        if (framebuffer >= NOVA_MAX_FBOS || !g.fbos[framebuffer].in_use) {
            g.last_error = GL_INVALID_OPERATION;
            return;
        }
        new_target = g.fbos[framebuffer].target;
        new_bound = framebuffer;
    }

    if (framebuffer != 0 && !new_target) {
        g.bound_fbo = new_bound;
        g.matrices_dirty = 1;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
        g.final_proj_cached_valid = 0;
        return;
    }

    // Splitting the GPU command buffer when switching render targets so the
    // previous target's writes commit to memory before any later sampling.
    if (g.current_target != new_target) {
        C3D_FrameSplit(0);
    }

    g.bound_fbo = new_bound;
    C3D_FrameDrawOn(new_target);
    g.current_target = new_target;

    // ФИКС: При смене цели - сбрасываем вьюпорт, так как правила вращения экрана меняются
    if (g.bound_fbo == 0) {
        C3D_SetViewport(g.vp_y, g.vp_x, g.vp_h, g.vp_w);
    } else {
        C3D_SetViewport(g.vp_x, g.vp_y, g.vp_w, g.vp_h);
    }

    // Форсируем обновление матриц, чтобы снялась/оделась матрица 'tilt'
    g.matrices_dirty = 1;
}

void glGenRenderbuffers(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = i + 1; }

void glDeleteRenderbuffers(GLsizei n, const GLuint *ids) {
    (void) n;
    (void) ids;
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    (void) target;
    (void) renderbuffer;
}

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    (void) target;
    (void) internalformat;
    (void) width;
    (void) height;
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    (void) target;
    (void) attachment;
    (void) renderbuffertarget;
    (void) renderbuffer;
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    (void) type;
    if (!pixels || width <= 0 || height <= 0) return;

    int bpp = (format == GL_RGB) ? 3 : 4;
    size_t total = (size_t) width * (size_t) height * (size_t) bpp;

    if (!g.current_target) {
        memset(pixels, 0, total);
        return;
    }
    C3D_FrameBuf *fb = &g.current_target->frameBuf;
    uint32_t *fb_data = (uint32_t *) fb->colorBuf;
    if (!fb_data) {
        memset(pixels, 0, total);
        return;
    }

    int fb_w = fb->width;
    int fb_h = fb->height;

    C3D_FrameSplit(0);
    gspWaitForP3D();

    uint8_t *dst = (uint8_t *) pixels;

    for (int row = 0; row < height; row++) {
        int logical_y = y + row;
        for (int col = 0; col < width; col++) {
            int logical_x = x + col;

            int phys_x, phys_y;

            // ФИКС ЧТЕНИЯ: Пиксели FBO не перевернуты
            if (g.bound_fbo == 0) {
                phys_x = logical_y;
                phys_y = logical_x;
            } else {
                phys_x = logical_x;
                phys_y = logical_y;
            }

            uint32_t pixel = 0;
            if (phys_x >= 0 && phys_x < fb_w && phys_y >= 0 && phys_y < fb_h) {
                pixel = fb_data[fb_morton_offset(phys_x, phys_y, fb_w, fb_h)];
            }

            uint8_t r = (uint8_t) ((pixel >> 24) & 0xFF);
            uint8_t g_ = (uint8_t) ((pixel >> 16) & 0xFF);
            uint8_t b = (uint8_t) ((pixel >> 8) & 0xFF);
            uint8_t a = (uint8_t) (pixel & 0xFF);

            uint8_t *out = dst + ((size_t) row * (size_t) width + (size_t) col) * (size_t) bpp;
            out[0] = r;
            out[1] = g_;
            out[2] = b;
            if (bpp == 4) out[3] = a;
        }
    }
}

void glPixelStorei(GLenum pname, GLint param) {
    if (param != 1 && param != 2 && param != 4 && param != 8) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (pname == GL_UNPACK_ALIGNMENT) g.unpack_alignment = param;
    else if (pname == GL_PACK_ALIGNMENT) g.pack_alignment = param;
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint) param); }
void glDrawBuffer(GLenum mode) { (void) mode; }

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    (void) target;
    (void) attachment;
    (void) textarget;
    (void) level;

    if (g.bound_fbo == 0 || g.bound_fbo >= NOVA_MAX_FBOS || !g.fbos[g.bound_fbo].in_use) {
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    FBOSlot *fbo = &g.fbos[g.bound_fbo];

    if (texture == 0) {
        if (fbo->target) {
            nova_queue_render_target_delete(fbo->target);
            fbo->target = NULL;
        }
        fbo->color_tex_id = 0;
        return;
    }

    if (texture >= NOVA_MAX_TEXTURES || !g.textures[texture].in_use || !g.textures[texture].allocated) {
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    TexSlot *slot = &g.textures[texture];

    if (fbo->target) {
        nova_queue_render_target_delete(fbo->target);
        fbo->target = NULL;
    }
    fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
    fbo->color_tex_id = texture;

    if (!fbo->target) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    if (g.bound_fbo != 0 && g.fbos[g.bound_fbo].target == fbo->target) {
        C3D_FrameDrawOn(fbo->target);
        g.current_target = fbo->target;
    }
}

GLenum glCheckFramebufferStatus(GLenum target) {
    (void) target;
    return GL_FRAMEBUFFER_COMPLETE;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1,
                       GLint dstY1, GLbitfield mask, GLenum filter) {
}

// ---------------------------------------------------------------------------
// novaCreateRenderTextureFBO — see NovaGL.h for full rationale.
//
// Why this needs to live here and not be expressible via existing GL calls:
// the stock glTexImage2D path goes through C3D_TexInit which allocates the
// color buffer in linear-mapped RAM. That is fine for sampling but PICA's
// render-target machinery wants VRAM-resident colorbufs (this is what every
// citro3d sample does, and what Butterscotch's surface allocator does — see
// src/3ds/render/surface.c::surface_alloc_storage). Attaching a linear-RAM
// texture as a render target appears to succeed but the GPU ends up writing
// to the wrong physical address window; sampling that texture later returns
// pre-clear garbage or transparent black. That symptom — "FBO drew fine to
// screen but reads back as transparent / random pixels" — is exactly what
// fast3d-on-NovaGL was hitting (menu blur backdrops, prev-frame motion blur).
//
// Doing this here means we can use C3D_TexInitVRAM directly (private to the
// citro3d allocator) without exposing VRAM-aware extensions on glTexImage2D.
// ---------------------------------------------------------------------------
int novaCreateRenderTextureFBO(int width, int height, int has_depth,
                               GLuint *out_tex_id, GLuint *out_fbo_id) {
    if (width <= 0 || height <= 0 || !out_tex_id || !out_fbo_id) {
        if (out_tex_id) *out_tex_id = 0;
        if (out_fbo_id) *out_fbo_id = 0;
        return 0;
    }

    int pot_w = (int) nova_next_pow2((unsigned int) width);
    int pot_h = (int) nova_next_pow2((unsigned int) height);
    if (pot_w < 8) pot_w = 8;
    if (pot_h < 8) pot_h = 8;

    /* --- Allocate texture slot ----------------------------------------- */
    GLuint tex_id = 1;
    while (tex_id < NOVA_MAX_TEXTURES && g.textures[tex_id].in_use) tex_id++;
    if (tex_id == NOVA_MAX_TEXTURES) {
        g.last_error = GL_OUT_OF_MEMORY;
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }
    TexSlot *slot = &g.textures[tex_id];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1;

    if (!C3D_TexInitVRAM(&slot->tex, (u16) pot_w, (u16) pot_h, GPU_RGBA8)) {
        slot->in_use = 0;
        g.last_error = GL_OUT_OF_MEMORY;
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }
    slot->allocated = 1;
    slot->width = width;
    slot->height = height;
    slot->orig_width = width;
    slot->orig_height = height;
    slot->pot_w = pot_w;
    slot->pot_h = pot_h;
    slot->fmt = GPU_RGBA8;
    slot->wrap_s = GL_CLAMP_TO_EDGE;
    slot->wrap_t = GL_CLAMP_TO_EDGE;
    slot->min_filter = GL_LINEAR;
    slot->mag_filter = GL_LINEAR;
    slot->max_level = 0;
    slot->generate_mipmap = 0;
    slot->has_mipmap = 0;

    C3D_TexSetFilter(&slot->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&slot->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    /* --- Allocate FBO slot --------------------------------------------- */
    GLuint fbo_id = 0;
    for (int slot_idx = 1; slot_idx < NOVA_MAX_FBOS; slot_idx++) {
        if (!g.fbos[slot_idx].in_use) {
            fbo_id = (GLuint) slot_idx;
            break;
        }
    }
    if (!fbo_id) {
        C3D_TexDelete(&slot->tex);
        memset(&slot->tex, 0, sizeof(slot->tex));
        slot->allocated = 0;
        slot->in_use = 0;
        g.last_error = GL_OUT_OF_MEMORY;
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }

    FBOSlot *fbo = &g.fbos[fbo_id];
    memset(fbo, 0, sizeof(*fbo));
    fbo->in_use = 1;

    /* C3D_DEPTHTYPE is a transparent union — passing GPU_RB_DEPTH16 picks
     * the enum branch, passing -1 (int) picks the "no depth" sentinel. */
    if (has_depth) {
        fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0,
                                                    GPU_RB_DEPTH16);
    } else {
        fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, -1);
    }
    if (!fbo->target) {
        fbo->in_use = 0;
        C3D_TexDelete(&slot->tex);
        memset(&slot->tex, 0, sizeof(slot->tex));
        slot->allocated = 0;
        slot->in_use = 0;
        g.last_error = GL_OUT_OF_MEMORY;
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }
    fbo->color_tex_id = tex_id;

    /* Zero out the freshly-allocated VRAM texture so the first sample of the
     * FBO (before anything's been rendered into it) returns transparent
     * black instead of whatever random VRAM contents happened to be there —
     * which would manifest as wrong textures showing through on geometry
     * that incidentally samples this FBO before its first render pass. */
    if (slot->tex.data && slot->tex.size > 0) {
        memset(slot->tex.data, 0, slot->tex.size);
        GSPGPU_FlushDataCache(slot->tex.data, slot->tex.size);
    }

    *out_tex_id = tex_id;
    *out_fbo_id = fbo_id;
    return 1;
}
