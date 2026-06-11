//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

/* Bumped from 128 because engines doing FBO-heavy post-processing (e.g. fast3d
 * for PD, anything with per-frame transient surfaces) can churn through dozens
 * of targets per frame. At 128 we were spilling into the synchronous-delete
 * fallback below, which stalls between draws and shows up as a stutter. */
#define NOVA_FBO_GC_TARGETS 512
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
        width == fb_h && height == fb_w && bpp == 4) {
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

    /* Soft fallback: untile + axis-swap pixel-by-pixel. */
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
    /* PICA200 has no native rect-to-rect blit with arbitrary scaling; we
     * approximate with the fullscreen quad path used by novaBlitTargetToFBO.
     * The src/dst rect args are ignored — we always copy the full source into
     * the full destination — which covers ~all engine use cases (resolve to
     * screen, frame snapshot, motion-blur capture). mask/filter ignored too:
     * always color, always linear. */
    (void) srcX0; (void) srcY0; (void) srcX1; (void) srcY1;
    (void) dstX0; (void) dstY0; (void) dstX1; (void) dstY1;
    (void) mask;  (void) filter;
    novaBlitTargetToFBO(g.bound_fbo, /*dst*/ 0); /* common case: blit FBO→screen */
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
int novaBlitTargetToFBO(GLuint src_fbo_id, GLuint dst_fbo_id)
{
    /* --- Resolve source render target ---------------------------------- */
    C3D_RenderTarget *src_tgt;
    if (src_fbo_id == 0) {
        src_tgt = g.render_target_top;
    } else {
        if (src_fbo_id >= NOVA_MAX_FBOS || !g.fbos[src_fbo_id].in_use ||
            !g.fbos[src_fbo_id].target) {
            return 0;
        }
        src_tgt = g.fbos[src_fbo_id].target;
    }
    if (!src_tgt || !src_tgt->frameBuf.colorBuf) return 0;

    /* --- Resolve destination render target ----------------------------- */
    C3D_RenderTarget *dst_tgt;
    if (dst_fbo_id == 0) {
        dst_tgt = g.render_target_top;
    } else {
        if (dst_fbo_id >= NOVA_MAX_FBOS || !g.fbos[dst_fbo_id].in_use ||
            !g.fbos[dst_fbo_id].target) {
            return 0;
        }
        dst_tgt = g.fbos[dst_fbo_id].target;
    }
    if (!dst_tgt || src_tgt == dst_tgt) return 0;

    /* --- Build transient C3D_Tex that aliases the source colorBuf ------
     * Same VRAM, same byte order (RGBA8 tiled). We can sample it as a
     * texture directly. C3D_Tex has more bookkeeping than we need so we
     * just zero-init and fill the fields that matter for sampling. */
    C3D_Tex src_tex;
    memset(&src_tex, 0, sizeof(src_tex));
    src_tex.data    = src_tgt->frameBuf.colorBuf;
    src_tex.width   = src_tgt->frameBuf.width;
    src_tex.height  = src_tgt->frameBuf.height;
    src_tex.fmt     = GPU_RGBA8;
    src_tex.size    = src_tex.width * src_tex.height * 4;
    src_tex.param   = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR)
                    | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
                    | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE)
                    | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
    src_tex.border  = 0;
    src_tex.lodParam = 0;

    /* --- Commit any pending draws so source is in VRAM ------------------ */
    C3D_FrameSplit(0);

    /* --- Save state we're about to clobber ------------------------------ */
    C3D_RenderTarget *saved_target = g.current_target;
    GLuint saved_bound_fbo = g.bound_fbo;

    /* --- Bind dst as the render target --------------------------------- */
    C3D_FrameDrawOn(dst_tgt);
    g.current_target = dst_tgt;
    g.bound_fbo = dst_fbo_id;

    /* --- Configure TEV stage 0: out = TEX0 ------------------------------ */
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    /* Passthrough stages 1..5 */
    for (int i = 1; i < 6; i++) {
        C3D_TexEnv *e = C3D_GetTexEnv(i);
        C3D_TexEnvInit(e);
        C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
        C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    }

    C3D_TexBind(0, &src_tex);

    /* --- Disable depth / blend / cull / scissor ------------------------- */
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

    /* --- Set viewport over the dst target's full extent ----------------- */
    C3D_SetViewport(0, 0, dst_tgt->frameBuf.width, dst_tgt->frameBuf.height);

    /* --- Quad geometry: clip-space fullscreen, UV covers source --------- *
     * The quad covers NDC [-1, 1] in xy, w=1 so the depth-clamp shader
     * leaves z alone. UVs are (0,0)-(1,1) — the linear sampler walks the
     * full source texture into the destination. */
    static const float quad_verts[4 * 8] = {
        /* x     y     z     w     u     v     padding (color slots — TEV
         *                                       in REPLACE mode ignores them) */
        -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f,  1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 1.0f,
    };

    /* Stage the verts into the linear ring buffer (PICA reads attribs
     * straight from VRAM-mapped memory; client_array_buf is linearAlloc'd
     * for exactly this purpose). */
    const int vbytes = sizeof(quad_verts);
    uint8_t *staged = (uint8_t *)linear_alloc_ring(g.client_array_buf,
                                                   &g.client_array_buf_offset,
                                                   vbytes,
                                                   g.client_array_buf_size);
    if (!staged) {
        /* Restore and bail */
        if (saved_target) {
            C3D_FrameDrawOn(saved_target);
            g.current_target = saved_target;
            g.bound_fbo = saved_bound_fbo;
        }
        nova_invalidate_state_cache();
        return 0;
    }
    memcpy(staged, quad_verts, vbytes);
    GSPGPU_FlushDataCache(staged, vbytes);

    /* Bind attributes: pos(4f), uv(2f), color(4f) at stride 32 */
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 4);  /* position */
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);  /* texcoord */
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_FLOAT, 4);  /* color (TEV ignores in REPLACE) */
    C3D_SetAttrInfo(&g.attr_info);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, staged, 8 * sizeof(float), 3, 0x210);

    /* Use clipspace shader so position passes through (with depth clamp). */
    int prev_shader = g.active_shader;
    int prev_clipspace = g.clipspace_mode_enabled;
    if (g.shader_clipspace_dvlb) {
        C3D_BindProgram(&g.shader_clipspace_program);
        g.active_shader = NOVA_SHADER_CLIPSPACE;
        /* Upload identity projection — the quad is already in clip space.
         * Skip the screen tilt: we're rendering to an FBO that the engine
         * will sample with its own UV conventions, no rotation needed. */
        C3D_Mtx identity;
        Mtx_Identity(&identity);
        if (g.uLoc_projection_clipspace >= 0) {
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &identity);
        }
    }

    /* Draw as 2 triangles (fan-equivalent: 0-1-2, 0-2-3). */
    static const uint16_t quad_indices[6] = { 0, 1, 2, 0, 2, 3 };
    uint8_t *idx_staged = (uint8_t *)linear_alloc_ring(g.index_buf,
                                                       &g.index_buf_offset,
                                                       sizeof(quad_indices),
                                                       g.index_buf_size);
    if (idx_staged) {
        memcpy(idx_staged, quad_indices, sizeof(quad_indices));
        GSPGPU_FlushDataCache(idx_staged, sizeof(quad_indices));
        C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT, idx_staged);
    }

    /* --- Restore --------------------------------------------------------- *
     * We trashed render target, viewport, depth/blend/alpha/cull/scissor,
     * TEV stages, AttrInfo, BufInfo, active shader, and bound texture. The
     * cleanest restore is to mark the entire state cache dirty so the next
     * apply_gpu_state re-pushes everything from g.* fields — much simpler
     * than threading save/restore through every register. */
    if (saved_target) {
        C3D_FrameDrawOn(saved_target);
        g.current_target = saved_target;
        g.bound_fbo = saved_bound_fbo;
    }
    g.active_shader = prev_shader;
    g.clipspace_mode_enabled = prev_clipspace;
    nova_invalidate_state_cache();

    return 1;
}
