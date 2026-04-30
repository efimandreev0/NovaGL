//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

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
            C3D_RenderTargetDelete(g.fbos[id].target);
            g.fbos[id].target = NULL;
        }
        g.fbos[id].color_tex_id = 0;
        g.fbos[id].in_use = 0;
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    (void) target;
    if (framebuffer == 0) {
        g.bound_fbo = 0;
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
        return;
    }
    if (framebuffer >= NOVA_MAX_FBOS || !g.fbos[framebuffer].in_use || !g.fbos[framebuffer].target) {
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    g.bound_fbo = framebuffer;
    C3D_FrameDrawOn(g.fbos[framebuffer].target);
    g.current_target = g.fbos[framebuffer].target;
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

    /* Submit anything currently queued in the C3D command buffer and wait for
     * the GPU to drain. Without this we'd be reading the framebuffer before
     * pending draw calls in the same frame have actually executed. */
    C3D_FrameSplit(0);
    gspWaitForP3D();

    uint8_t *dst = (uint8_t *) pixels;

    /* GL convention: (x, y) is bottom-left of the source rect, Y goes up.
     * 3DS render target: rotated 90deg vs the visible screen, morton-tiled.
     * The same mapping that glCopyTexSubImage2D uses (phys_x = logical_y,
     * phys_y = logical_x, with morton's internal Y-flip) is what we need. */

    for (int row = 0; row < height; row++) {
        /* GL row 0 is the bottom of the requested rect; output bytes follow GL
         * convention too (row 0 first), so don't flip on output. */
        int logical_y = y + row;
        for (int col = 0; col < width; col++) {
            int logical_x = x + col;

            int phys_x = logical_y;
            int phys_y = logical_x;

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
    if (pname == GL_UNPACK_ALIGNMENT) {
        g.unpack_alignment = param;
    } else if (pname == GL_PACK_ALIGNMENT) {
        g.pack_alignment = param;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) {
    glPixelStorei(pname, (GLint) param);
}

void glDrawBuffer(GLenum mode) {
    (void) mode;
    /* Only single buffer supported on 3DS */
}

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

    /* Detach: drop the current render target. */
    if (texture == 0) {
        if (fbo->target) {
            C3D_RenderTargetDelete(fbo->target);
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

    /* (Re)create the C3D render target wrapping this texture's storage. */
    if (fbo->target) {
        C3D_RenderTargetDelete(fbo->target);
        fbo->target = NULL;
    }
    fbo->target = C3D_RenderTargetCreateFromTex(&slot->tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
    fbo->color_tex_id = texture;

    if (!fbo->target) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    /* If this FBO is currently bound, switch the GPU draw target now. */
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
    (void) srcX0;
    (void) srcY0;
    (void) srcX1;
    (void) srcY1;
    (void) dstX0;
    (void) dstY0;
    (void) dstX1;
    (void) dstY1;
    (void) mask;
    (void) filter;
}
