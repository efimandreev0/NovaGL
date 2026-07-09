//
// created by efimandreev0 on 05.04.2026.
//

#include <stdio.h>

#include "NovaGL.h"
#include "utils.h"

/* TEMP DIAG: включает константно-красный вывод блита (см. novaBlitTargetToFBO). */
/* #define NOVAGL_BLIT_DIAG_CONST_RED 1 */

/* Bumped from 128 because engines doing FBO-heavy post-processing (e.g. fast3d
 * for PD, anything with per-frame transient surfaces) can churn through dozens
 * of targets per frame. At 128 we were spilling into the synchronous-delete
 * fallback below, which stalls between draws and shows up as a stutter. */
#define NOVA_FBO_GC_TARGETS  512
#define NOVA_FBO_FRAME_SLOTS 3   /* max frame_buffers */
/* Like the texture GC, orphaned render targets are bucketed by the frame slot
 * that retired them and only deleted K (= frame_buffers) frames later, once the
 * GPU has finished the frame that referenced them (see novaSwapBuffers). */
static C3D_RenderTarget *s_fbo_gc_targets[NOVA_FBO_FRAME_SLOTS][NOVA_FBO_GC_TARGETS];
static int s_fbo_gc_count[NOVA_FBO_FRAME_SLOTS] = {0, 0, 0};

static void fbo_gc_free_slot(int s) {
    for (int i = 0; i < s_fbo_gc_count[s]; i++) {
        if (s_fbo_gc_targets[s][i]) {
            C3D_RenderTargetDelete(s_fbo_gc_targets[s][i]);
            s_fbo_gc_targets[s][i] = NULL;
        }
    }
    s_fbo_gc_count[s] = 0;
}

static void nova_queue_render_target_delete(C3D_RenderTarget *target) {
    if (!target) return;
    int s = g.frame_slot;
    if (s_fbo_gc_count[s] >= NOVA_FBO_GC_TARGETS) {
        /* Slot full (512 orphans in K frames — extreme). Retire the in-flight
         * work so this slot's targets are safe to delete now, then reuse it.
         * Raw split+gspWaitForP3D parks forever under citro3d's render queue
         * — use the sanctioned mid-frame drain instead. */
        nova_midframe_drain();
        fbo_gc_free_slot(s);
    }
    s_fbo_gc_targets[s][s_fbo_gc_count[s]++] = target;
}

/* Called from free_texture_storage (texture.c) whenever a texture's storage is
 * about to be orphaned/re-created: any FBO render target wrapping that storage
 * would keep rendering into the freed buffer. Queue those targets for GC and
 * clear the pointer; the ATTACHMENT itself survives (GL semantics — re-specifying
 * an attached texture keeps it attached), so color_tex_id stays and the target
 * is re-wired from the new storage on the next glBindFramebuffer /
 * glFramebufferTexture2D (see the lazy re-wire in glBindFramebuffer). */
void nova_fbo_orphan_texture_targets(GLuint tex_id) {
    if (tex_id == 0) return;
    for (int i = 1; i < NOVA_MAX_FBOS; i++) {
        FBOSlot *fbo = &g.fbos[i];
        if (!fbo->in_use || fbo->color_tex_id != tex_id || !fbo->target) continue;
        if (g.current_target == fbo->target) {
            /* The live draw target is dying — flush what's queued for it and
             * park drawing on the logical screen until the FBO is re-wired. */
            nova_batch_flush();
            C3D_FrameSplit(0);
            C3D_RenderTarget *scr = g.app_target ? g.app_target : g.render_target_top;
            if (scr) {
                C3D_FrameDrawOn(scr);
                g.current_target = scr;
            }
        }
        nova_queue_render_target_delete(fbo->target);
        fbo->target = NULL;
    }
}

/* Free the CURRENT slot's targets (called at swap after rotating to the next
 * slot — reclaims targets from K frames ago, GPU done). */
void nova_fbo_gc_collect(void) {
    fbo_gc_free_slot(g.frame_slot);
}

/* Free all slots — only when the GPU is idle (nova_fini). */
void nova_fbo_gc_collect_all(void) {
    for (int s = 0; s < NOVA_FBO_FRAME_SLOTS; s++) fbo_gc_free_slot(s);
}

static inline int fb_morton_offset(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int) morton_interleave((uint32_t) (x & 7), (uint32_t) (fy & 7));
}

void glGenFramebuffers(GLsizei n, GLuint *ids) {
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!ids) return;
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
            gl_set_error(GL_OUT_OF_MEMORY);
        }
        ids[i] = id;
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint *ids) {
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!ids) return;
    /* Deleting the bound FBO reverts the draw target to the screen and queues
     * the target for deletion — commit any pending batch first. */
    nova_batch_flush();
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = ids[i];
        if (id == 0 || id >= NOVA_MAX_FBOS) continue;
        if (!g.fbos[id].in_use) continue;
        /* A deleted name that was bound for reading reverts to fb 0 (spec). */
        if (g.bound_read_fbo == id) g.bound_read_fbo = 0;
        if (g.bound_fbo == id) {
            /* Deleting the bound FBO reverts to the logical screen = app
             * surface (or the LCD if no app surface). */
            g.bound_fbo = 0;
            C3D_RenderTarget *scr = g.app_target ? g.app_target : g.render_target_top;
            C3D_FrameDrawOn(scr);
            g.current_target = scr;
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
    /* Spec: target must be one of these three; otherwise GL_INVALID_ENUM and
     * the binding is unchanged. */
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    C3D_RenderTarget *new_target;
    GLuint new_bound;
    if (framebuffer == 0) {
        /* "fb 0" = the logical screen = the POT app surface (presented to the
         * physical LCD at swap). Falls back to the LCD if no app surface. */
        new_target = g.app_target ? g.app_target : g.render_target_top;
        new_bound = 0;
    } else {
        if (framebuffer >= NOVA_MAX_FBOS || !g.fbos[framebuffer].in_use) {
            gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        new_target = g.fbos[framebuffer].target;
        new_bound = framebuffer;
    }

    /* GL_READ_FRAMEBUFFER binds ONLY the read source (glReadPixels /
     * glBlitFramebuffer source). It must NOT touch the draw target, the screen
     * tilt, viewport, scissor or depth state. (The old code only ever updated
     * g.bound_read_fbo for an incomplete FBO, so read/draw separation was
     * effectively broken for any complete FBO.) */
    if (target == GL_READ_FRAMEBUFFER) {
        g.bound_read_fbo = new_bound;
        return;   /* read binding only — does NOT change the draw target */
    }
    /* About to (possibly) change the DRAW target — commit the pending batch into
     * the current target first, while the GPU still holds its state. */
    nova_batch_flush();
    /* GL_FRAMEBUFFER updates BOTH bindings; GL_DRAW_FRAMEBUFFER only the draw one. */
    if (target == GL_FRAMEBUFFER) g.bound_read_fbo = new_bound;

    /* Lazy re-wire: the attachment survives a glTexImage2D re-spec (GL
     * semantics), but the C3D target wrapping the OLD storage was orphaned by
     * nova_fbo_orphan_texture_targets. Recreate it from the new storage here,
     * the first time the FBO is bound again. This is the GL-shaped version of
     * Butterscotch's surface slot reuse: same slot, storage swapped under it. */
    if (framebuffer != 0 && !new_target) {
        FBOSlot *fbo = &g.fbos[framebuffer];
        GLuint ctex = fbo->color_tex_id;
        if (ctex > 0 && ctex < NOVA_MAX_TEXTURES && g.textures[ctex].in_use &&
            g.textures[ctex].allocated && !g.textures[ctex].is_cube &&
            nova_texture_make_vram_target(ctex)) {
#ifdef NOVAGL_FBO_DEPTH24_STENCIL8
            fbo->target = C3D_RenderTargetCreateFromTex(&g.textures[ctex].tex,
                                                        GPU_TEXFACE_2D, 0,
                                                        GPU_RB_DEPTH24_STENCIL8);
#else
            fbo->target = C3D_RenderTargetCreateFromTex(&g.textures[ctex].tex,
                                                        GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
#endif
            new_target = fbo->target;
        }
    }

    if (framebuffer != 0 && !new_target) {
        /* FBO with no colour attachment yet: record the draw binding so a later
         * glFramebufferTexture2D wires up the real C3D target, and mark matrices
         * dirty. Drawing into it before completion is GL_INVALID_FRAMEBUFFER_-
         * OPERATION, enforced at draw time (nova_draw_internal). */
        g.bound_fbo = new_bound;
        g.matrices_dirty = 1;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
        g.final_proj_cached_valid = 0;
        return;
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
    /* Keep the glViewport dedupe shadow in sync with what we just applied. */
    g.vp_applied_x = g.vp_x;
    g.vp_applied_y = g.vp_y;
    g.vp_applied_w = g.vp_w;
    g.vp_applied_h = g.vp_h;
    g.vp_applied_fbo0 = (g.bound_fbo == 0);
    g.vp_applied_valid = 1;

    /* The polygon-offset LSB depends on the bound target's depth format
     * (screen = D24S8, default FBOs = D16) — re-emit the depth map so the
     * bias stays one-LSB-exact on whichever buffer we now draw into. */
    apply_depth_map();

    /* OPT-IN WORKAROUND (-DNOVAGL_FBO_RESET_SCISSOR=1, default OFF): reset the
     * scissor to the FULL FBO when binding an offscreen target.
     *
     * ON by default (historical behaviour fast3d-style backends rely on): they
     * force a full VIEWPORT for their 2D blits but DON'T reset the scissor, so
     * an FBO render would otherwise inherit the previous screen pass's scissor
     * (the "dark menu blur" / clipped post-process symptom). Strictly the GL
     * scissor is global context state and shouldn't change on a bind — opt into
     * that spec behaviour with -DNOVAGL_NO_FBO_RESET_SCISSOR=1. */
#ifndef NOVAGL_NO_FBO_RESET_SCISSOR
    if (g.bound_fbo != 0) {
        GLuint ctex = g.fbos[framebuffer].color_tex_id;
        int lw = (ctex > 0 && ctex < NOVA_MAX_TEXTURES && g.textures[ctex].allocated)
                     ? g.textures[ctex].width : (int) new_target->frameBuf.width;
        int lh = (ctex > 0 && ctex < NOVA_MAX_TEXTURES && g.textures[ctex].allocated)
                     ? g.textures[ctex].height : (int) new_target->frameBuf.height;
        g.scissor_x = 0;
        g.scissor_y = 0;
        g.scissor_w = lw;
        g.scissor_h = lh;
    }
#endif

    // Форсируем обновление матриц, чтобы снялась/оделась матрица 'tilt'
    g.matrices_dirty = 1;
    g.state_dirty_bits |= NOVA_DIRTY_SCISSOR;
#ifdef NOVAGL_FBO_FLIP_CULL
    /* The winding sense depends on the bound target under this diagnostic. */
    g.state_dirty_bits |= NOVA_DIRTY_CULLING;
#endif
}

GLuint novaGetScreenTextureId(void) { return g.app_screen_tex_id; }

/* Renderbuffers carry no real state on NovaGL — FBO color attachments are
 * textures and the depth/stencil is created with the render target. But the
 * names must still be UNIQUE across calls (the old `ids[i] = i+1` handed out
 * 1..n every call, guaranteeing collisions). A tiny file-static in-use pool
 * gives spec-correct unique allocation without bloating the GL context. */
#define NOVA_MAX_RBOS 256
static uint8_t s_rbo_in_use[NOVA_MAX_RBOS];
static GLuint  s_bound_rbo = 0;

void glGenRenderbuffers(GLsizei n, GLuint *ids) {
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!ids) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 0;
        for (int slot = 1; slot < NOVA_MAX_RBOS; slot++) {
            if (!s_rbo_in_use[slot]) { s_rbo_in_use[slot] = 1; id = (GLuint) slot; break; }
        }
        if (!id) gl_set_error(GL_OUT_OF_MEMORY);
        ids[i] = id;
    }
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *ids) {
    if (n < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (!ids) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = ids[i];
        if (id > 0 && id < NOVA_MAX_RBOS && s_rbo_in_use[id]) {
            s_rbo_in_use[id] = 0;
            if (s_bound_rbo == id) s_bound_rbo = 0;
        }
    }
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    if (target != GL_RENDERBUFFER) { gl_set_error(GL_INVALID_ENUM); return; }
    /* Binding an unused non-zero name creates it (GL semantics). */
    if (renderbuffer != 0 && renderbuffer < NOVA_MAX_RBOS) s_rbo_in_use[renderbuffer] = 1;
    s_bound_rbo = renderbuffer;
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
    /* Spec: negative dims are GL_INVALID_VALUE; only GL_UNSIGNED_BYTE is the
     * mandated/implemented type here (PICA colour buffer is RGBA8). */
    if (width < 0 || height < 0) { gl_set_error(GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_BYTE) { gl_set_error(GL_INVALID_ENUM); return; }

    /* Commit pending draws so the read-back sees this frame's full geometry,
     * not a framebuffer missing the un-issued batch. */
    nova_batch_flush();

    /* Number of output channels by GL format. */
    int bpp;
    switch (format) {
        case GL_RGBA: case GL_BGRA: bpp = 4; break;
        case GL_RGB:  case GL_BGR:  bpp = 3; break;
        case GL_LUMINANCE_ALPHA:    bpp = 2; break;
        case GL_LUMINANCE: case GL_ALPHA:
        case GL_RED: case GL_GREEN: case GL_BLUE: bpp = 1; break;
        default: gl_set_error(GL_INVALID_ENUM); return;
    }
    if (!pixels || width == 0 || height == 0) return;

    /* Output rows are padded to GL_PACK_ALIGNMENT (default 4). */
    int pack = g.pack_alignment > 0 ? g.pack_alignment : 1;
    int row_bytes = width * bpp;
    int dst_stride = (row_bytes + pack - 1) & ~(pack - 1);
    size_t total = (size_t) dst_stride * (size_t) height;

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

    /* Fast path: full-target read for the screen (RGBA8 / RGB8). PICA's
     * DisplayTransfer engine de-swizzles and untiles in hardware in ~0.5ms
     * vs the per-pixel CPU loop's 8–10ms. We require:
     *   - top screen (bound_fbo == 0), so the rotation matches our axis swap
     *   - region == full framebuffer (x=y=0, width/height matching native)
     *   - linear destination buffer
     * In all other cases (sub-rect reads, FBO reads) fall back to the soft
     * de-swizzle loop below.
     *
     * NOTE: top screen native is 240×400 tiled; logical "GL" coords are
     * width=400 height=240, so the test matches against the swapped sizes.
     *
     * OPT-IN ONLY (-DNOVAGL_ENABLE_GLREADPIXELS_HW=1): DisplayTransfer
     * untiles but does NOT rotate, while the soft path below axis-swaps
     * 240×400 physical into the 400×240 logical layout callers expect.
     * Until the HW path gets a post-rotate pass its output is transposed
     * relative to the soft path, so it stays off by default. */
#ifdef NOVAGL_ENABLE_GLREADPIXELS_HW
    if (g.bound_fbo == 0 && x == 0 && y == 0 &&
        width == fb_h && height == fb_w && format == GL_RGBA &&
        dst_stride == width * 4) {
        u32 transfer_flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                             GX_TRANSFER_RAW_COPY(0) |
                             GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                             GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                             GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
        C3D_FrameSplit(0);
        C3D_SyncDisplayTransfer(fb_data, GX_BUFFER_DIM(fb_w, fb_h),
                                (u32 *)pixels, GX_BUFFER_DIM(fb_w, fb_h),
                                transfer_flags);
        return;
    }
#endif

    /* Soft fallback: untile + axis-swap pixel-by-pixel. CPU reads need the
     * queued draws retired — raw split+wait parks forever mid-frame. */
    nova_midframe_drain();

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

            uint8_t *out = dst + (size_t) row * (size_t) dst_stride + (size_t) col * (size_t) bpp;
            switch (format) {
                case GL_RGBA: out[0]=r; out[1]=g_; out[2]=b; out[3]=a; break;
                case GL_RGB:  out[0]=r; out[1]=g_; out[2]=b;          break;
                case GL_BGRA: out[0]=b; out[1]=g_; out[2]=r; out[3]=a; break;
                case GL_BGR:  out[0]=b; out[1]=g_; out[2]=r;          break;
                case GL_LUMINANCE_ALPHA:
                    out[0] = (uint8_t)((r*77 + g_*150 + b*29) >> 8); out[1] = a; break;
                case GL_LUMINANCE:
                    out[0] = (uint8_t)((r*77 + g_*150 + b*29) >> 8); break;
                case GL_ALPHA: out[0] = a;  break;
                case GL_RED:   out[0] = r;  break;
                case GL_GREEN: out[0] = g_; break;
                case GL_BLUE:  out[0] = b;  break;
                default: break;
            }
        }
    }
}

void glPixelStorei(GLenum pname, GLint param) {
    /* Validate pname first so an unknown pname is GL_INVALID_ENUM regardless of
     * the param value (spec ordering). */
    if (pname != GL_UNPACK_ALIGNMENT && pname != GL_PACK_ALIGNMENT) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (param != 1 && param != 2 && param != 4 && param != 8) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (pname == GL_UNPACK_ALIGNMENT) g.unpack_alignment = param;
    else g.pack_alignment = param;
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint) param); }
void glDrawBuffer(GLenum mode) { (void) mode; }

#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    /* NovaGL only wires a colour texture; depth/stencil come from the render
     * target itself, so DEPTH/STENCIL attachments are accepted as a no-op. Any
     * other attachment enum is invalid. */
    if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT ||
        attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        return;
    }
    if (attachment != GL_COLOR_ATTACHMENT0) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    /* textarget must be a 2D image (2D or a cube face); level 0 only (NovaGL
     * renders into the base level). */
    if (textarget != GL_TEXTURE_2D &&
        !(textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level != 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    /* Re-homing the texture to VRAM (immediate C3D_TexDelete of old storage) and
     * re-pointing the FBO target are about to happen — commit any pending batch
     * that might reference the current target / old storage first. */
    nova_batch_flush();

    if (g.bound_fbo == 0 || g.bound_fbo >= NOVA_MAX_FBOS || !g.fbos[g.bound_fbo].in_use) {
        gl_set_error(GL_INVALID_OPERATION);
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
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    TexSlot *slot = &g.textures[texture];

    // PICA render targets must be VRAM-resident. A texture created through the
    // stock glTexImage2D path lives in linear RAM; re-home it in VRAM before
    // wrapping it as a color attachment, otherwise the GPU renders to the wrong
    // physical window and later samples come back as garbage.
    if (!nova_texture_make_vram_target(texture)) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    /* Anti-churn (Butterscotch's surface-slot-reuse fix): re-attaching the SAME
     * texture whose storage hasn't moved keeps the existing target. Engines
     * that run their attach path every frame (GM-style per-frame surfaces)
     * otherwise orphan + recreate a render target per call — hundreds of GC
     * entries and creation stalls per second for a no-op state change. */
    if (fbo->color_tex_id == texture && fbo->target &&
        fbo->target->frameBuf.colorBuf == slot->tex.data) {
        return;
    }

    if (fbo->target) {
        nova_queue_render_target_delete(fbo->target);
        fbo->target = NULL;
    }
    /* DEPTH16 by default (historical, lower VRAM). FBO stencil + D24 precision
     * are opt-in via -DNOVAGL_FBO_DEPTH24_STENCIL8=1 — D24S8 doubles the FBO
     * depth-buffer VRAM, which can fail RenderTarget creation on VRAM-tight
     * scenes (PD's many per-frame effect FBOs). */
#ifdef NOVAGL_FBO_DEPTH24_STENCIL8
    fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
#else
    fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
#endif
    fbo->color_tex_id = texture;

    if (!fbo->target) {
        gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    if (g.bound_fbo != 0 && g.fbos[g.bound_fbo].target == fbo->target) {
        C3D_FrameDrawOn(fbo->target);
        g.current_target = fbo->target;
    }
}

GLenum glCheckFramebufferStatus(GLenum target) {
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD6
#endif
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        gl_set_error(GL_INVALID_ENUM);
        return 0;
    }
    /* The default framebuffer (fb 0) is always complete. An app FBO is complete
     * only once it has a colour attachment (a live C3D render target). */
    if (g.bound_fbo == 0) return GL_FRAMEBUFFER_COMPLETE;
    if (g.bound_fbo < NOVA_MAX_FBOS && g.fbos[g.bound_fbo].in_use && g.fbos[g.bound_fbo].target)
        return GL_FRAMEBUFFER_COMPLETE;
    return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1,
                       GLint dstY1, GLbitfield mask, GLenum filter) {
    // PICA has no rect->rect blit with scaling, so we cheat with the fullscreen
    // quad from novaBlitTargetToFBO. src/dst rects are ignored (always
    // full->full, color only, linear), but we DO honour the separate read/draw
    // bindings: the GL_READ_FRAMEBUFFER is the source and the GL_DRAW_FRAMEBUFFER
    // (g.bound_fbo) is the destination. Covers the engine cases that matter —
    // Butterscotch's letterbox present (surface->screen) and surface->surface
    // copies — plus the resolve/snapshot/motion-blur uses.
    (void) srcX0; (void) srcY0; (void) srcX1; (void) srcY1;
    (void) dstX0; (void) dstY0; (void) dstX1; (void) dstY1;
    (void) mask;  (void) filter;
    novaBlitTargetToFBO(g.bound_read_fbo, g.bound_fbo);
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
        gl_set_error(GL_OUT_OF_MEMORY);
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }
    TexSlot *slot = &g.textures[tex_id];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1;

    if (!C3D_TexInitVRAM(&slot->tex, (u16) pot_w, (u16) pot_h, GPU_RGBA8)) {
        slot->in_use = 0;
        gl_set_error(GL_OUT_OF_MEMORY);
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }
    slot->allocated = 1;
    slot->is_vram = 1;
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
        gl_set_error(GL_OUT_OF_MEMORY);
        *out_tex_id = 0;
        *out_fbo_id = 0;
        return 0;
    }

    FBOSlot *fbo = &g.fbos[fbo_id];
    memset(fbo, 0, sizeof(*fbo));
    fbo->in_use = 1;

    /* C3D_DEPTHTYPE is a transparent union — passing GPU_RB_DEPTH24_STENCIL8 picks
     * the enum branch, passing -1 (int) picks the "no depth" sentinel. */
    if (has_depth) {
        /* DEPTH16 default (lower VRAM); D24S8 opt-in (see glFramebufferTexture2D). */
#ifdef NOVAGL_FBO_DEPTH24_STENCIL8
        fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0,
                                                    GPU_RB_DEPTH24_STENCIL8);
#else
        fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0,
                                                    GPU_RB_DEPTH16);
#endif
    } else {
        fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, -1);
    }
    if (!fbo->target) {
        fbo->in_use = 0;
        C3D_TexDelete(&slot->tex);
        memset(&slot->tex, 0, sizeof(slot->tex));
        slot->allocated = 0;
        slot->in_use = 0;
        gl_set_error(GL_OUT_OF_MEMORY);
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

    /* CPU-memset нулей до VRAM-сэмплера в Citra может не доехать (hw-рендерер
     * не перечитывает emulated VRAM при создании host-поверхности) — чистим и
     * GPU-заливкой тоже: это идёт через GX memory fill, который Citra честно
     * исполняет. Иначе несэмплированный ещё FBO читается как цветной шум. */
    C3D_RenderTargetClear(fbo->target, C3D_CLEAR_COLOR, 0x00000000, 0);

    *out_tex_id = tex_id;
    *out_fbo_id = fbo_id;
    return 1;
}

// ---------------------------------------------------------------------------
// novaBlitTargetToFBO — see NovaGL.h for full rationale.
//
// PD's bondview uses `gDPCopyFramebufferEXT(g_PrevFrameFb, 0, ...)` to take
// a snapshot of the on-screen render target into a separate FBO, then
// samples that FBO as a texture for distortion / scope / cutscene overlays.
// Without an actual copy, g_PrevFrameFb holds stale data from earlier draws
// and the overlay shows last-frame leftovers — "ghosting" in cutscene starts.
//
// Implementation approach: render a fullscreen textured quad from the source
// render target into the destination. The PICA200 doesn't have a "blit
// framebuffer" API like OpenGL's glBlitFramebuffer, so this is the
// idiomatic GPU-side path. Both citro3d sample code and Butterscotch's
// surface allocator do it the same way.
//
// The source render target's colorBuf is wrapped as a transient C3D_Tex
// pointing at the same VRAM memory (tiled RGBA8 either way — same morton
// layout, same byte order). Bound to texture unit 0, the quad is drawn with
// TEV in REPLACE mode (out = TEX, no primary modulation, no depth test, no
// blending). After the draw we restore the previous render target / state.
// ---------------------------------------------------------------------------
/* One-shot source override: when set, a src_fbo_id==0 blit samples this
 * texture instead of the live app surface. Used by novaBlitSnapshotToFBO to
 * capture the PREVIOUS frame (front-buffer semantics). */
static C3D_Tex *s_blit_src_override;

/* ---------------------------------------------------------------------------
 * nova_gpu_blit_quad — the shared GPU copy core (Butterscotch's
 * ctr_create_surf_ex pattern, proven on hardware): draw src_tex through a
 * REPLACE TEV onto dst_tgt's (0,0,vp_w,vp_h) viewport as one fullscreen
 * clip-space quad with caller-supplied per-corner UVs. The GPU untiles,
 * retiles, converts formats and (via the UV field) rotates — no CPU morton
 * math, no gspWaitForP3D stall.
 *
 * Caller contract: pending batch flushed + C3D_FrameSplit issued BEFORE (so
 * the source's pixels are resolved in memory); AFTER the call the caller
 * restores its render target and calls nova_invalidate_state_cache() — we
 * clobber TEV, attr/buf info, viewport, depth/blend/cull/scissor, texture
 * bind and shader. UV corner order: (-1,-1) (1,-1) (1,1) (-1,1).
 * tilt_for_screen applies the 90° present tilt (landscape source onto the
 * sideways-stored screen). vp_x/vp_y place the write window inside the dst
 * framebuffer (the FBO/blit convention writes at (0,0); the glCopyTex* path
 * writes at (0, pot_h - h) to land in the same rows a CPU upload fills).
 * Returns 1 on success, 0 if the vertex/index ring had no room this frame.
 * ------------------------------------------------------------------------- */
static int nova_gpu_blit_quad(C3D_Tex *src_tex, const float uv[4][2],
                              C3D_RenderTarget *dst_tgt,
                              int vp_x, int vp_y, int vp_w, int vp_h,
                              int tilt_for_screen)
{
    C3D_FrameDrawOn(dst_tgt);
    g.current_target = dst_tgt;

    /* --- Configure TEV stage 0: out = TEX0 ------------------------------ */
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
#ifdef NOVAGL_BLIT_DIAG_CONST_RED
    /* TEMP DIAG: подменяем выборку текстуры константным красным — если
     * downstream-цепочка (запись в FBO → ImageRect → бэкдроп) исправна,
     * захваты станут красными; если нет — чёрный/магента укажут этап. */
    C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
    C3D_TexEnvColor(env, 0xFF0000FF); /* R=FF A=FF */
#else
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
#endif
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    /* Passthrough stages 1..5 */
    for (int i = 1; i < 6; i++) {
        C3D_TexEnv *e = C3D_GetTexEnv(i);
        C3D_TexEnvInit(e);
        C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
        C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    }

    C3D_TexBind(0, src_tex);

    /* --- Disable depth / blend / cull / scissor ------------------------- */
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

    /* --- Viewport over the dst write window ----------------------------- */
    C3D_SetViewport((u32) vp_x, (u32) vp_y, (u32) vp_w, (u32) vp_h);
    g.vp_applied_valid = 0; /* GPU viewport diverged from GL state — force re-apply */

    /* 10 float'ов на вершину: pos4 + uv2 + color4 (см. историю: лэйаут должен
     * совпадать с лоадерами, иначе цвет читается из соседней вершины). */
    float quad_verts[4 * 10] = {
        /* x     y     z     w     u         v         r     g     b     a */
        -1.0f, -1.0f, 0.0f, 1.0f, uv[0][0], uv[0][1], 1.0f, 1.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, uv[1][0], uv[1][1], 1.0f, 1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, uv[2][0], uv[2][1], 1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f, uv[3][0], uv[3][1], 1.0f, 1.0f, 1.0f, 1.0f,
    };

    /* Stage the verts into the linear ring buffer (PICA reads attribs
     * straight from VRAM-mapped memory). */
    const int vbytes = sizeof(quad_verts);
    uint8_t *staged = (uint8_t *)linear_alloc_ring(g.client_array_buf,
                                                   &g.client_array_buf_offset,
                                                   vbytes,
                                                   g.client_array_buf_size);
    if (!staged) return 0;
    memcpy(staged, quad_verts, vbytes);
    GSPGPU_FlushDataCache(staged, vbytes);

    /* Bind attributes: pos(4f), uv(2f), color(4f) at stride 40 */
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 4);  /* position */
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);  /* texcoord */
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_FLOAT, 4);  /* color (TEV ignores in REPLACE) */
    C3D_SetAttrInfo(&g.attr_info);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, staged, 10 * sizeof(float), 3, 0x210);

    /* Use clipspace shader so position passes through (with depth clamp). */
    int prev_shader = g.active_shader;
    int prev_clipspace = g.clipspace_mode_enabled;
    if (g.shader_clipspace_dvlb) {
        C3D_BindProgram(&g.shader_clipspace_program);
        g.active_shader = NOVA_SHADER_CLIPSPACE;
        /* The quad is already in clip space. For an FBO->FBO copy upload identity
         * (landscape->landscape, no rotation). For a present onto the logical
         * screen, upload the SAME 90° tilt the normal per-draw path uses (see
         * utils.c) so the landscape source lands upright on the sideways screen.
         * Keep NOVAGL_TILT_VARIANT in sync with utils.c. */
        C3D_Mtx clip_proj;
        Mtx_Identity(&clip_proj);
        if (tilt_for_screen) {
            #ifndef NOVAGL_TILT_VARIANT
            #define NOVAGL_TILT_VARIANT 1
            #endif
            #if NOVAGL_TILT_VARIANT == 1
            clip_proj.r[0].x = 0.0f; clip_proj.r[0].y =  1.0f;
            clip_proj.r[1].x = -1.0f; clip_proj.r[1].y = 0.0f;
            #elif NOVAGL_TILT_VARIANT == 2
            clip_proj.r[0].x = 0.0f; clip_proj.r[0].y = -1.0f;
            clip_proj.r[1].x = 1.0f;  clip_proj.r[1].y = 0.0f;
            #elif NOVAGL_TILT_VARIANT == 3
            clip_proj.r[0].x = -1.0f; clip_proj.r[0].y = 0.0f;
            clip_proj.r[1].x = 0.0f;  clip_proj.r[1].y = -1.0f;
            #endif
        }
        if (g.uLoc_projection_clipspace >= 0) {
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &clip_proj);
        }
    }

    /* Draw as 2 triangles (fan-equivalent: 0-1-2, 0-2-3). */
    static const uint16_t quad_indices[6] = { 0, 1, 2, 0, 2, 3 };
    uint8_t *idx_staged = (uint8_t *)linear_alloc_ring(g.index_buf,
                                                       &g.index_buf_offset,
                                                       sizeof(quad_indices),
                                                       g.index_buf_size);
    int drew = 0;
    if (idx_staged) {
        memcpy(idx_staged, quad_indices, sizeof(quad_indices));
        GSPGPU_FlushDataCache(idx_staged, sizeof(quad_indices));
        C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, idx_staged);
        drew = 1;
    }


    g.active_shader = prev_shader;
    g.clipspace_mode_enabled = prev_clipspace;
    return drew;
}

/* ---------------------------------------------------------------------------
 * nova_copy_read_fb_to_texture — GPU path for glCopyTex(Sub)Image2D.
 *
 * Butterscotch's ctr_create_surf_ex proved this on hardware: instead of
 * untangling the tiled/Morton colour buffer with CPU math (slow, a full
 * gspWaitForP3D stall, AND wrong for the FBO layout — the old loop applied
 * the sideways-screen axis swap to landscape FBO sources too), wrap the
 * destination texture in a render target and let the GPU draw the source
 * region as a textured quad. Only FULL-texture copies take this path (the
 * sprite_create_from_surface / snapshot family — glCopyTexImage2D always
 * lands here); partial updates keep the CPU fallback so untouched texels
 * survive the VRAM promotion. Returns 1 = copied, 0 = use the CPU path.
 * ------------------------------------------------------------------------- */
int nova_copy_read_fb_to_texture(GLuint tex_id, GLint xoffset, GLint yoffset,
                                 GLint x, GLint y, GLsizei w, GLsizei h) {
    if (tex_id == 0 || tex_id >= NOVA_MAX_TEXTURES) return 0;
    TexSlot *dst = &g.textures[tex_id];
    if (!dst->in_use || !dst->allocated || dst->is_cube) return 0;
    if (w <= 0 || h <= 0) return 0;
    /* Full-destination copies only (see above). */
    if (xoffset != 0 || yoffset != 0 || w != dst->width || h != dst->height) return 0;
    /* Destination must be a renderable colour format — GPU_TEXCOLOR values
     * 0..4 map 1:1 onto GPU_COLORBUF (RGBA8/RGB8/RGBA5551/RGB565/RGBA4).
     * LA8 and friends keep the CPU path. */
    if (dst->fmt != GPU_RGBA8 && dst->fmt != GPU_RGB8 && dst->fmt != GPU_RGBA5551 &&
        dst->fmt != GPU_RGB565 && dst->fmt != GPU_RGBA4) return 0;

    /* --- Resolve the READ framebuffer as a sampleable texture + the UV
     * window of the (x,y,w,h) rect. Same conventions as novaBlitTargetToFBO:
     * FBO sources are landscape (v runs sv at GL-bottom → 0 at top); the
     * screen is stored sideways, so its field is transposed (u follows the
     * source y axis, v follows source x) — the GPU does the rotation. */
    C3D_Tex *src_tex;
    float uv[4][2]; /* corner order (-1,-1) (1,-1) (1,1) (-1,1) */
    if (g.bound_read_fbo == 0) {
        /* Screen source needs the POT app surface; the raw LCD target is NPOT
         * and can't be sampled — direct-to-LCD builds use the CPU path. */
        if (!g.app_target || !g.app_tex.data) return 0;
        src_tex = &g.app_tex;
        /* Sideways storage: screen col X lives at memory row 400-X, screen
         * row Y at memory column Y (the tilt) — so the UV field is transposed:
         * u follows the source y axis across the quad's NDC y, v follows
         * source x across NDC x. u runs high-at-NDC-bottom because the
         * destination is written in upload row order (rows inverted). */
        float u_lo = (float) y / (float) g.app_pot_w;
        float u_hi = (float) (y + h) / (float) g.app_pot_w;
        float v_lo = (float) (g.app_logical_h - x) / (float) g.app_pot_h;
        float v_hi = (float) (g.app_logical_h - (x + w)) / (float) g.app_pot_h;
        uv[0][0] = u_hi; uv[0][1] = v_lo;
        uv[1][0] = u_hi; uv[1][1] = v_hi;
        uv[2][0] = u_lo; uv[2][1] = v_hi;
        uv[3][0] = u_lo; uv[3][1] = v_lo;
    } else {
        if (g.bound_read_fbo >= NOVA_MAX_FBOS || !g.fbos[g.bound_read_fbo].in_use ||
            !g.fbos[g.bound_read_fbo].target) return 0;
        GLuint stex = g.fbos[g.bound_read_fbo].color_tex_id;
        if (stex == 0 || stex >= NOVA_MAX_TEXTURES || !g.textures[stex].allocated) return 0;
        /* Feedback loop (copying an FBO into its own colour texture) — the
         * GPU can't sample its own render target; CPU path handles it. */
        if (stex == tex_id) return 0;
        TexSlot *ss = &g.textures[stex];
        src_tex = &ss->tex;
        /* FBO content occupies memory rows [0, height) (natural raster, the
         * window the blit path samples as [0,sv]) — GL window row y+j is
         * memory row y+j, so the rect's v window is [y, y+h)/pot_h. The v
         * DIRECTION across the quad is flipped (v high at NDC bottom): the
         * destination is written in upload row order (see the dst viewport
         * note below), which inverts rows relative to FBO storage. */
        float u_lo  = (float) x / (float) ss->pot_w;
        float u_hi  = (float) (x + w) / (float) ss->pot_w;
        float v_bot = (float) (y + h) / (float) ss->pot_h;
        float v_top = (float) y / (float) ss->pot_h;
        uv[0][0] = u_lo; uv[0][1] = v_bot;
        uv[1][0] = u_hi; uv[1][1] = v_bot;
        uv[2][0] = u_hi; uv[2][1] = v_top;
        uv[3][0] = u_lo; uv[3][1] = v_top;
    }

    /* Destination storage must be render-target-capable. The copy covers the
     * whole texture, so the promotion's zeroing loses nothing. */
    if (!nova_texture_make_vram_target(tex_id)) return 0;

    /* Reuse an FBO target already wrapping this texture — creating + GCing a
     * transient target per snapshot is exactly the churn Butterscotch's slot
     * reuse fixed. Only when none exists do we make a temp one. */
    C3D_RenderTarget *dst_tgt = NULL;
    int dst_tgt_is_temp = 0;
    for (int i = 1; i < NOVA_MAX_FBOS; i++) {
        if (g.fbos[i].in_use && g.fbos[i].color_tex_id == tex_id && g.fbos[i].target &&
            g.fbos[i].target->frameBuf.colorBuf == dst->tex.data) {
            dst_tgt = g.fbos[i].target;
            break;
        }
    }
    if (!dst_tgt) {
        dst_tgt = C3D_RenderTargetCreateFromTex(&dst->tex, GPU_TEXFACE_2D, 0, -1);
        if (!dst_tgt) return 0;
        dst_tgt_is_temp = 1;
    }

    /* Commit pending work so the source's pixels are resolved in memory. */
    nova_batch_flush();
    C3D_FrameSplit(0);

    C3D_RenderTarget *saved_target = g.current_target;
    /* Write window (0, pot_h - h): a CPU upload places texel row j at memory
     * row pot_h-1-j, i.e. the content occupies the TOP pot rows. The copy must
     * land in the same window (in the same row order — the quad's v direction
     * handles that) or regular texture sampling of an NPOT destination would
     * read POT padding instead of the copied pixels. */
    int ok = nova_gpu_blit_quad(src_tex, uv, dst_tgt,
                                0, dst->pot_h - h, w, h, /*tilt_for_screen=*/0);

    if (saved_target) {
        C3D_FrameDrawOn(saved_target);
        g.current_target = saved_target;
    }
    /* Temp target rides the orphan GC (Butterscotch's gc_add_target): the GPU
     * may still be executing the copy, so no synchronous delete. */
    if (dst_tgt_is_temp) nova_queue_render_target_delete(dst_tgt);
    nova_invalidate_state_cache();
    return ok;
}

/* Front-buffer capture: blit the previous-frame snapshot into dst. Falls
 * back to the live app surface when the snapshot isn't allocated. */
int novaBlitSnapshotToFBO(GLuint dst_fbo_id)
{
    int r;
    if (g.app_prev_target && g.app_prev_tex.data) {
        s_blit_src_override = &g.app_prev_tex;
        r = novaBlitTargetToFBO(0, dst_fbo_id);
        s_blit_src_override = NULL;
    } else {
        r = novaBlitTargetToFBO(0, dst_fbo_id);
    }
    return r;
}

int novaBlitTargetToFBO(GLuint src_fbo_id, GLuint dst_fbo_id)
{
    /* Commit any pending batch first: if the source is the current target its
     * batched pixels must be in VRAM before the blit samples them, and the blit
     * switches target + clobbers all GPU state regardless. */
    nova_batch_flush();

    /* --- Resolve SOURCE as a sampleable C3D_Tex ------------------------- *
     * PICA texture units only address power-of-two dims, so we must bind a
     * real POT texture, never alias the NPOT physical LCD. "fb 0" (the
     * screen) resolves to the POT app surface, whose logical content sits in
     * the [0,su]x[0,sv] sub-rect. FBO render-textures are POT too; sampling
     * their logical sub-rect keeps padding out of the copy. */
    C3D_Tex *bind_tex = NULL;
    float su = 1.0f, sv = 1.0f; /* source logical/POT UV extent */
    if (src_fbo_id == 0) {
        if (!g.app_target || !g.app_tex.data) return 0; /* no app surface */
        /* Snapshot override (front-buffer capture) — same layout as app_tex. */
        bind_tex = s_blit_src_override ? s_blit_src_override : &g.app_tex;
        su = (float) g.app_logical_w / (float) g.app_pot_w;
        sv = (float) g.app_logical_h / (float) g.app_pot_h;
    } else {
        if (src_fbo_id >= NOVA_MAX_FBOS || !g.fbos[src_fbo_id].in_use ||
            !g.fbos[src_fbo_id].target) {
            return 0;
        }
        GLuint stex = g.fbos[src_fbo_id].color_tex_id;
        if (stex == 0 || stex >= NOVA_MAX_TEXTURES || !g.textures[stex].allocated) return 0;
        TexSlot *ss = &g.textures[stex];
        bind_tex = &ss->tex;
        if (ss->pot_w > 0) su = (float) ss->width  / (float) ss->pot_w;
        if (ss->pot_h > 0) sv = (float) ss->height / (float) ss->pot_h;
    }

    /* --- Resolve DESTINATION render target ----------------------------- *
     * "fb 0" → the app surface (the real screen is only written by the
     * present blit). dst logical extent sets the viewport so we draw into the
     * logical region, leaving POT padding untouched. */
    C3D_RenderTarget *dst_tgt;
    int dst_logical_w, dst_logical_h;
    /* dst == "fb 0" is the logical screen. The source FBO is stored LANDSCAPE
     * (FBOs never get the per-draw tilt — see utils.c), but the screen is stored
     * SIDEWAYS, so a present into it must rotate 90°. We flag that here and apply
     * the tilt to the blit quad's projection below. */
    int dst_is_screen = (dst_fbo_id == 0);
    /* Capturing the screen (app surface) INTO a landscape FBO is the inverse
     * problem: the app surface is stored SIDEWAYS (it gets the per-draw tilt),
     * FBOs are stored landscape, so the blit must apply the INVERSE 90° tilt or
     * the captured image lands rotated. (PD's damage/scope blur + menu room
     * backdrop all snapshot the screen this way.) */
    int src_is_screen = (src_fbo_id == 0);
    if (dst_fbo_id == 0) {
        /* Prefer the POT app surface (presented later by novaSwapBuffers); if the
         * app surface is disabled (NOVAGL_APP_SURFACE=0, the default) blit
         * straight onto the physical top LCD render target instead. */
        if (g.app_target) {
            dst_tgt = g.app_target;
            dst_logical_w = g.app_logical_w;
            dst_logical_h = g.app_logical_h;
        } else if (g.render_target_top) {
            dst_tgt = g.render_target_top;
            dst_logical_w = (int) g.render_target_top->frameBuf.width;
            dst_logical_h = (int) g.render_target_top->frameBuf.height;
        } else {
            return 0;
        }
    } else {
        if (dst_fbo_id >= NOVA_MAX_FBOS || !g.fbos[dst_fbo_id].in_use ||
            !g.fbos[dst_fbo_id].target) {
            return 0;
        }
        dst_tgt = g.fbos[dst_fbo_id].target;
        GLuint dtex = g.fbos[dst_fbo_id].color_tex_id;
        if (dtex > 0 && dtex < NOVA_MAX_TEXTURES && g.textures[dtex].allocated) {
            dst_logical_w = g.textures[dtex].width;
            dst_logical_h = g.textures[dtex].height;
        } else {
            dst_logical_w = (int) dst_tgt->frameBuf.width;
            dst_logical_h = (int) dst_tgt->frameBuf.height;
        }
    }
    if (!dst_tgt || !bind_tex) return 0;
    /* Self-copy is a no-op — except with the snapshot override, where "src 0"
     * is really the previous-frame texture, not the live target. */
    if (src_fbo_id == dst_fbo_id && !s_blit_src_override) return 0;

    /* --- Resolve RAW Hazard for Source ------------------ */
    GLuint src_tex_id = (src_fbo_id == 0) ? g.app_screen_tex_id : g.fbos[src_fbo_id].color_tex_id;
    if (src_tex_id > 0 && src_tex_id < NOVA_MAX_TEXTURES && g.textures[src_tex_id].written_pending_split) {
        C3D_FrameSplit(0);
        g.textures[src_tex_id].written_pending_split = 0;
    }

    /* --- Save state we're about to clobber ------------------------------ */
    C3D_RenderTarget *saved_target = g.current_target;
    GLuint saved_bound_fbo = g.bound_fbo;
    g.bound_fbo = dst_fbo_id;

    /* --- Per-corner UVs, order (-1,-1) (1,-1) (1,1) (-1,1) --------------- *
     * Default landscape mapping covers the source logical sub-rect
     * [0,su]x[0,sv] (bottom-NDC→v-max flip, this blit's historical
     * convention).
     *
     * Screen→FBO capture: the app surface stores the frame SIDEWAYS (per-draw
     * tilt), the FBO is landscape. Instead of rotating the quad POSITIONS by
     * an "inverse tilt" matrix (the old approach — it left the captured image
     * rotated 90° inside the FBO, so consumers sampling the logical window saw
     * mostly padding = the "black menu blur"), keep the positions as a plain
     * fullscreen quad and TRANSPOSE THE UV FIELD: dst x walks the source's
     * v axis, dst y walks the source's u axis. Derived from the present path
     * (u ∝ fb column = logical y, v ∝ fb row = logical x). */
    float uv[4][2] = {
        { 0.0f, sv   },
        { su,   sv   },
        { su,   0.0f },
        { 0.0f, 0.0f },
    };
    if (src_is_screen && !dst_is_screen) {
        /* Transpose (sideways screen → landscape FBO), calibrated EMPIRICALLY
         * against PD's bondview/menu-blur overlays on Citra (2026-07-09).
         * Full calibration table — all four sign combinations were observed
         * or derived, they form a consistent flip group:
         *   u=(1+y)/2·su, v=(1−x)/2·sv  → vertical mirror   (original; was
         *                                  reported as "180°")
         *   u=(1−y)/2·su, v=(1+x)/2·sv  → horizontal mirror (attempt 1;
         *                                  "flipped about Y")
         *   u=(1+y)/2·su, v=(1+x)/2·sv  → true 180° rotation (attempt 2;
         *                                  upside-down face overlay)
         *   u=(1−y)/2·su, v=(1−x)/2·sv  → CORRECT (current)
         * Do not re-derive from theory — trust this table. The original
         * mapping stays available via -DNOVAGL_BLIT_CAPTURE_NO_ROT180=1. */
#ifdef NOVAGL_BLIT_CAPTURE_NO_ROT180
        uv[0][0] = 0.0f; uv[0][1] = sv;
        uv[1][0] = 0.0f; uv[1][1] = 0.0f;
        uv[2][0] = su;   uv[2][1] = 0.0f;
        uv[3][0] = su;   uv[3][1] = sv;
#else
        uv[0][0] = su;   uv[0][1] = sv;
        uv[1][0] = su;   uv[1][1] = 0.0f;
        uv[2][0] = 0.0f; uv[2][1] = 0.0f;
        uv[3][0] = 0.0f; uv[3][1] = sv;
#endif
    }

    int ok = nova_gpu_blit_quad(bind_tex, uv, dst_tgt,
                                0, 0, dst_logical_w, dst_logical_h, dst_is_screen);

    /* Mark destination texture as written so future samples know to flush */
    GLuint dst_tex_id = (dst_fbo_id == 0) ? g.app_screen_tex_id : g.fbos[dst_fbo_id].color_tex_id;
    if (dst_tex_id > 0 && dst_tex_id < NOVA_MAX_TEXTURES) {
        g.textures[dst_tex_id].written_pending_split = 1;
    }

    /* --- Restore --------------------------------------------------------- *
     * The blit trashed render target, viewport, depth/blend/alpha/cull/
     * scissor, TEV stages, AttrInfo, BufInfo, active shader, and bound
     * texture. The cleanest restore is to mark the entire state cache dirty
     * so the next apply_gpu_state re-pushes everything from g.* fields. */
    if (saved_target) {
        C3D_FrameDrawOn(saved_target);
        g.current_target = saved_target;
    }
    g.bound_fbo = saved_bound_fbo;
    nova_invalidate_state_cache();

    return ok;
}
