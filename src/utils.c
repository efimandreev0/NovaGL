//
// Created by efimandreev0 on 05.04.2026.
//
#include "NovaGL.h"
#include "utils.h"

/* Value fixed by the GL spec; defined locally if NovaGL.h lacks it. */
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif


#include <stdio.h>
#include <stdlib.h>

/* ── File-scope state cache used by apply_gpu_state.
 *
 * These mirror the "last-applied" GPU state so we can skip redundant
 * C3D_DepthTest / AlphaBlend / TexBind / Scissor / ... register writes when
 * nothing changed between draws (the typical chunk-by-chunk render loop hits
 * the same depth/alpha/cull settings hundreds of times per frame).
 *
 * Reset via nova_invalidate_state_cache() — required on world reload because
 * texture/VBO ids get recycled, and a cached "tex unit 0 already bound to id
 * 7" would skip the necessary re-bind to a deleted slot. */
static GLuint        s_last_tex_bound[3]       = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

/* attr/buf cache used by nova_setup_attr_info / nova_setup_buf_info */
static int   s_attr_pos_elements = -1;
static void *s_buf_base          = (void *) -1;
static int   s_buf_stride        = -1;

unsigned int nova_next_pow2(unsigned int v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/* ---- adaptive ring growth ------------------------------------------------
 * The old overflow path did C3D_FrameSplit + gspWaitForP3D and wrapped the
 * ring in place. That freezes hard under real 3D load (OpenMW first world
 * frame, main thread parked in gspWaitForEvent forever — a mid-frame split
 * does not reliably produce a wakeable P3D on Citra), and wrapping while
 * in-flight draws still read the bytes is corruption by design.
 *
 * New policy: on overflow the current slot's buffer is ORPHANED (parked on a
 * per-slot GC, freed K frames later when the GPU is provably done with it,
 * exactly like the texture/FBO GCs) and the slot gets a bigger allocation.
 * Rings thereby auto-size to the real per-frame draw volume after the first
 * heavy frame; no waits, no wraps. */
#define NOVA_RING_GC_MAX 8
static void *s_ring_gc[3][NOVA_RING_GC_MAX];
static int s_ring_gc_count[3];

/* ---- deferred VBO-storage free --------------------------------------------
 * Same lifetime rule as the ring GC: a linear block that backed a VBO drawn
 * from THIS frame must survive until the GPU has retired the frame. Reached
 * from glDeleteBuffers, glBufferData respecification and the glBufferSubData
 * grow path (all of which used to linearFree immediately — a use-after-free
 * whenever the app deletes/respecifies right after drawing, and a bigger
 * window under async frame_buffers>1). */
#define NOVA_VBO_GC_MAX 64
static void *s_vbo_gc[3][NOVA_VBO_GC_MAX];
static int s_vbo_gc_count[3];

void nova_vbo_defer_free(void *p) {
    if (!p) return;
    int s = g.frame_slot;
    if (s_vbo_gc_count[s] < NOVA_VBO_GC_MAX)
        s_vbo_gc[s][s_vbo_gc_count[s]++] = p;
    else
        linearFree(p); /* bucket full — old immediate-free behaviour */
}

void nova_vbo_gc_collect(void) {
    int s = g.frame_slot;
    for (int i = 0; i < s_vbo_gc_count[s]; i++)
        linearFree(s_vbo_gc[s][i]);
    s_vbo_gc_count[s] = 0;
}

void nova_vbo_gc_collect_all(void) {
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < s_vbo_gc_count[s]; i++)
            linearFree(s_vbo_gc[s][i]);
        s_vbo_gc_count[s] = 0;
    }
}

/* Diagnostic breadcrumbs into the host app's boot log. Weak: resolves to the
 * OpenMW CTR layer when linked into it, null elsewhere. */
extern void vitaBreadcrumb(const char *msg) __attribute__((weak));
static void ring_crumb(const char *fmt, int a, int b) {
    if (&vitaBreadcrumb) {
        char buf[96];
        snprintf(buf, sizeof(buf), fmt, a, b);
        vitaBreadcrumb(buf);
    }
}

static int s_wait_tag_budget = 0; /* silent until armed by the host app */

void nova_wait_tag_arm(int budget) {
    s_wait_tag_budget = budget;
}

void nova_wait_tag(const char *tag) {
    if (s_wait_tag_budget <= 0 || !&vitaBreadcrumb)
        return;
    s_wait_tag_budget--;
    vitaBreadcrumb(tag);
}

void nova_ring_gc_collect(void) {
    int s = g.frame_slot;
    for (int i = 0; i < s_ring_gc_count[s]; i++) {
        if (s_ring_gc[s][i]) {
            linearFree(s_ring_gc[s][i]);
            s_ring_gc[s][i] = NULL;
        }
    }
    s_ring_gc_count[s] = 0;
}

void nova_midframe_drain(void) {
    if (!g.p3d_pending)
        return;
    nova_wait_tag("[W] drain>");
    C3D_FrameEnd(0);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    if (g.current_target)
        C3D_FrameDrawOn(g.current_target);
    g.p3d_pending = 0;
    g.ring_synced_this_frame = 1;
    nova_wait_tag("[W] drain<");
}

static void *ring_grow(void **alias, void **slot_buf, int *slot_cap, int *shared_cap, int *offset, int size) {
    int newcap = *slot_cap > 0 ? *slot_cap : (256 * 1024);
    while (newcap < *slot_cap + size)
        newcap *= 2;
    void *nb = linearAlloc((size_t) newcap);
    if (!nb) {
        ring_crumb("[NovaRing] grow FAILED: want %d KB (linear exhausted)", newcap / 1024, 0);
        return NULL; /* caller falls back to the legacy stall path */
    }
    ring_crumb("[NovaRing] grew ring to %d KB (alloc %d KB)", newcap / 1024, 0);

    int s = g.frame_slot;
    if (s_ring_gc_count[s] < NOVA_RING_GC_MAX) {
        s_ring_gc[s][s_ring_gc_count[s]++] = *slot_buf;
    } else {
        /* GC bucket full (8 growths in one frame — pathological). Leak the
         * old buffer rather than freeing it under the GPU's feet. */
    }
    *slot_buf = nb;
    *slot_cap = newcap;
    *shared_cap = newcap;
    *alias = nb;
    *offset = 0;
    /* Kick the accumulated commands to the GPU asynchronously (NO wait — a
     * mid-frame wait is the exact freeze this rework removes). Safe: the
     * orphaned buffer stays alive in the GC until K frames from now, so
     * in-flight reads stay valid. Also keeps the citro3d command buffer
     * drained now that rings no longer force periodic splits. */
    if (g.p3d_pending)
        C3D_FrameSplit(0);
    return nb;
}

void *linear_alloc_ring(void *base, int *offset, int size, int capacity) {
    /* 128-byte alignment is the historical value; the optimized 64 (still
     * safe for cache-line 32 + GX-DMA 16) is opt-in. Some PICA200 dispatch
     * paths assume 128 — keep the conservative default unless explicitly
     * told otherwise. */
#ifdef NOVAGL_RING_ALIGN_64
    size = (size + 0x3F) & ~0x3F;
#else
    size = (size + 0x7F) & ~0x7F;
#endif

    if (*offset + size > capacity) {
        /* Identify which ring this is by its alias and grow it. */
        void *grown = NULL;
        int s = g.frame_slot;
        nova_wait_tag("[W] ring-grow>");
        if (base == g.client_array_buf)
            grown = ring_grow(&g.client_array_buf, &g.client_array_buf_slots[s], &g.client_array_buf_caps[s],
                              &g.client_array_buf_size, offset, size);
        else if (base == g.index_buf)
            grown = ring_grow(&g.index_buf, &g.index_buf_slots[s], &g.index_buf_caps[s], &g.index_buf_size,
                              offset, size);

        /* Grow failed (linear exhausted). Reclaim this slot's orphaned ring
         * buffers from earlier growths this frame, then retry the grow ONCE —
         * a fragmented heap sometimes has enough space once the GC frees the
         * old blocks. */
        if (!grown) {
            nova_ring_gc_collect();
            if (base == g.client_array_buf)
                grown = ring_grow(&g.client_array_buf, &g.client_array_buf_slots[s], &g.client_array_buf_caps[s],
                                  &g.client_array_buf_size, offset, size);
            else if (base == g.index_buf)
                grown = ring_grow(&g.index_buf, &g.index_buf_slots[s], &g.index_buf_caps[s], &g.index_buf_size,
                                  offset, size);
        }
        nova_wait_tag("[W] ring-grow<");

        if (grown) {
            base = grown;
        } else if (size > capacity) {
            /* UNRECOVERABLE: a single draw is larger than the ENTIRE ring
             * capacity, so wrapping *offset back to 0 cannot help — the draw
             * still would not fit. Return NULL and let the caller SKIP this
             * draw (drop a frame's geometry) instead of corrupting the ring or
             * hanging. This is the exact case that used to wedge the render
             * traversal. */
            ring_crumb("[NovaRing] overflow SKIP: size=%d KB > cap=%d KB", size / 1024, capacity / 1024);
            return NULL;
        } else {
            /* RECOVERABLE: the draw fits the ring, we just ran off the end of
             * the current fill. Drain the GPU the only way citro3d supports
             * mid-stream — end the frame and reopen it with SYNCDRAW — then
             * wrap. A raw gspWaitForP3D NEVER wakes here: citro3d's render
             * queue owns the P3D event callback, so the waitable event is never
             * signalled (observed as a permanent main-thread park on Citra).
             *
             * This drain may fire AT MOST ONCE per frame (g.ring_synced_this_frame):
             * a second mid-frame drain is wasteful and a hang risk. If we have
             * already drained this frame, fall through to a bare wrap (the
             * earlier drain already retired the in-flight reads for this slot,
             * so overwriting from offset 0 is safe). Costs a partial present;
             * correctness over beauty. */
            if (g.p3d_pending && !g.ring_synced_this_frame) {
                ring_crumb("[NovaRing] overflow fallback: size=%d KB cap=%d KB - sync", size / 1024, capacity / 1024);
                C3D_FrameEnd(0);
                C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
                if (g.current_target)
                    C3D_FrameDrawOn(g.current_target);
                g.p3d_pending = 0;
                g.ring_synced_this_frame = 1;
                ring_crumb("[NovaRing] sync done - wrapped", 0, 0);
            } else {
                ring_crumb("[NovaRing] overflow fallback: already synced - bare wrap", 0, 0);
            }
            *offset = 0; // wrap
        }
    }

    void *ptr = (uint8_t *) base + *offset;
    *offset += size;

    return ptr;
}

float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static int clampi(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static void apply_scissor_box(void) {
    C3D_FrameBuf *fb = g.current_target ? &g.current_target->frameBuf : NULL;
    const int native_w = fb ? (int)fb->width : NOVA_SCREEN_H;
    const int native_h = fb ? (int)fb->height : NOVA_SCREEN_W;

    int x0 = g.scissor_x;
    int y0 = g.scissor_y;
    int x1 = g.scissor_x + g.scissor_w;
    int y1 = g.scissor_y + g.scissor_h;

    if (g.bound_fbo == 0) {
        /* Screen is rendered sideways (per-draw tilt); scissor rotated to
         * match. Logical screen is 400x240 (NOT fb-derived: the app surface fb
         * is POT-padded). */
        const int logical_w = NOVA_SCREEN_W; /* 400 */
        const int logical_h = NOVA_SCREEN_H; /* 240 */
        x0 = clampi(x0, 0, logical_w);
        x1 = clampi(x1, 0, logical_w);
        y0 = clampi(y0, 0, logical_h);
        y1 = clampi(y1, 0, logical_h);

        C3D_SetScissor(GPU_SCISSOR_NORMAL,
                       (u32)y0,
                       (u32)(logical_w - x1),
                       (u32)y1,
                       (u32)(logical_w - x0));
    } else {
        const int logical_w = native_w;
        const int logical_h = native_h;
        x0 = clampi(x0, 0, logical_w);
        x1 = clampi(x1, 0, logical_w);
        y0 = clampi(y0, 0, logical_h);
        y1 = clampi(y1, 0, logical_h);

        C3D_SetScissor(GPU_SCISSOR_NORMAL,
                       (u32)x0,
                       (u32)(logical_h - y1),
                       (u32)x1,
                       (u32)(logical_h - y0));
    }
}

void dl_record_translate(float x, float y, float z) {
    if (g.dl_store && g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_TRANSLATE;
            op->args[0] = x;
            op->args[1] = y;
            op->args[2] = z;
        }
    }
}

void dl_record_color3f(float r, float g_, float b) {
    if (g.dl_store && g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR3F;
            op->args[0] = r;
            op->args[1] = g_;
            op->args[2] = b;
        }
    }
}

void dl_record_color4f(float r, float g_, float b, float a) {
    if (g.dl_store && g.dl_recording >= 0 && g.dl_recording < NOVA_DISPLAY_LISTS) {
        DisplayList *dl = &g.dl_store[g.dl_recording];
        if (dl->count < NOVA_DL_MAX_OPS) {
            DLOp *op = &dl->ops[dl->count++];
            op->type = DL_OP_COLOR4F;
            op->args[0] = r;
            op->args[1] = g_;
            op->args[2] = b;
            op->args[3] = a;
        }
    }
}

void dl_execute(GLuint list) {
    if (!g.dl_store || list >= NOVA_DISPLAY_LISTS) return;
    DisplayList *dl = &g.dl_store[list];
    if (!dl->used) return;
    for (int i = 0; i < dl->count; i++) {
        DLOp *op = &dl->ops[i];
        if (op->type == DL_OP_TRANSLATE) glTranslatef(op->args[0], op->args[1], op->args[2]);
        else if (op->type == DL_OP_COLOR3F) glColor3f(op->args[0], op->args[1], op->args[2]);
        else if (op->type == DL_OP_COLOR4F) glColor4f(op->args[0], op->args[1], op->args[2], op->args[3]);
    }
}

GPU_TESTFUNC gl_to_gpu_alpha_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_LESS;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_LEQUAL;
        case GL_GREATER: return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_GEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_EARLYDEPTHFUNC gl_to_gpu_earlydepthfunc(GLenum func) {
    switch (func) {
    case GL_LESS:    return GPU_EARLYDEPTH_GREATER;
    case GL_LEQUAL:  return GPU_EARLYDEPTH_GEQUAL;
    case GL_GREATER: return GPU_EARLYDEPTH_LESS;
    case GL_GEQUAL:  return GPU_EARLYDEPTH_LEQUAL;
    default:         return GPU_EARLYDEPTH_GEQUAL;
    }
}

GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_GREATER;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_GEQUAL;
        case GL_GREATER: return GPU_LESS;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_LEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func) {
    switch (func) {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_LESS;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_LEQUAL;
        case GL_GREATER: return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_GEQUAL;
        case GL_ALWAYS:
        default: return GPU_ALWAYS;
    }
}

GPU_TESTFUNC stencil_func_to_gpu(GLenum f) {
    switch (f) {
    case GL_NEVER:    return GPU_NEVER;
    case GL_LESS:     return GPU_LESS;
    case GL_LEQUAL:   return GPU_LEQUAL;
    case GL_GREATER:  return GPU_GREATER;
    case GL_GEQUAL:   return GPU_GEQUAL;
    case GL_EQUAL:    return GPU_EQUAL;
    case GL_NOTEQUAL: return GPU_NOTEQUAL;
    case GL_ALWAYS:
    default:          return GPU_ALWAYS;
    }
}

GPU_STENCILOP stencil_op_to_gpu(GLenum op) {
    switch (op) {
    case GL_ZERO:      return GPU_STENCIL_ZERO;
    case GL_REPLACE:   return GPU_STENCIL_REPLACE;
    case GL_INCR:      return GPU_STENCIL_INCR;      /* saturating */
    case GL_DECR:      return GPU_STENCIL_DECR;      /* saturating */
        /* PICA HAS distinct wrapping ops — use them so GL_*_WRAP semantics are
         * exact (wrap at 0/255 instead of saturating). */
    case GL_INCR_WRAP: return GPU_STENCIL_INCR_WRAP;
    case GL_DECR_WRAP: return GPU_STENCIL_DECR_WRAP;
    case GL_INVERT:    return GPU_STENCIL_INVERT;
    case GL_KEEP:
    default:           return GPU_STENCIL_KEEP;
    }
}

GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor) {
    switch (factor) {
        case GL_ZERO: return GPU_ZERO;
        case GL_ONE: return GPU_ONE;
        case GL_SRC_COLOR: return GPU_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return GPU_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR: return GPU_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return GPU_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA: return GPU_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return GPU_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA: return GPU_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return GPU_ONE_MINUS_DST_ALPHA;
        case GL_SRC_ALPHA_SATURATE: return GPU_SRC_ALPHA_SATURATE;
        case GL_CONSTANT_COLOR: return GPU_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR: return GPU_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA: return GPU_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return GPU_ONE_MINUS_CONSTANT_ALPHA;
        default: return GPU_ONE;
    }
}

/* GL colour logic opcode -> PICA GPU_LOGICOP. Both enumerate the same 16 ops;
 * GL's values are 0x1500-0x150F in canonical order, so a table keyed off the
 * low nibble would also work, but the explicit switch is clearer and matches
 * the rest of the lookup file. */
GPU_LOGICOP gl_to_gpu_logicop(GLenum op) {
    switch (op) {
        case GL_CLEAR:         return GPU_LOGICOP_CLEAR;
        case GL_AND:           return GPU_LOGICOP_AND;
        case GL_AND_REVERSE:   return GPU_LOGICOP_AND_REVERSE;
        case GL_COPY:          return GPU_LOGICOP_COPY;
        case GL_AND_INVERTED:  return GPU_LOGICOP_AND_INVERTED;
        case GL_NOOP:          return GPU_LOGICOP_NOOP;
        case GL_XOR:           return GPU_LOGICOP_XOR;
        case GL_OR:            return GPU_LOGICOP_OR;
        case GL_NOR:           return GPU_LOGICOP_NOR;
        case GL_EQUIV:         return GPU_LOGICOP_EQUIV;
        case GL_INVERT:        return GPU_LOGICOP_INVERT;
        case GL_OR_REVERSE:    return GPU_LOGICOP_OR_REVERSE;
        case GL_COPY_INVERTED: return GPU_LOGICOP_COPY_INVERTED;
        case GL_OR_INVERTED:   return GPU_LOGICOP_OR_INVERTED;
        case GL_NAND:          return GPU_LOGICOP_NAND;
        case GL_SET:           return GPU_LOGICOP_SET;
        default:               return GPU_LOGICOP_COPY;
    }
}

GPU_BLENDEQUATION gl_to_gpu_blendeq(GLenum mode) {
    switch (mode) {
        case GL_FUNC_ADD: return GPU_BLEND_ADD;
        case GL_FUNC_SUBTRACT: return GPU_BLEND_SUBTRACT;
        case GL_FUNC_REVERSE_SUBTRACT: return GPU_BLEND_REVERSE_SUBTRACT;
        case GL_MIN: return GPU_BLEND_MIN;
        case GL_MAX: return GPU_BLEND_MAX;
        default: return GPU_BLEND_ADD;
    }
}

int gl_type_size(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE: return 1;

        case GL_SHORT:
        case GL_UNSIGNED_SHORT: return 2;

        case GL_FLOAT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FIXED:
        default: return 4;
    }
}

int calc_stride(GLsizei stride, GLint size, GLenum type) {
    return stride ? stride : size * gl_type_size(type);
}

void read_vertex_attrib_float(float *dst, const uint8_t *src, GLint size, GLenum type) {
    switch (type) {
        case GL_FLOAT:
            memcpy(dst, src, size * sizeof(float));
            break;
        case GL_FIXED:
            for (int j = 0; j < size; j++) {
                // GL_FIXED is 16.16 fixed point
                dst[j] = (float) ((const int32_t *) src)[j] / 65536.0f;
            }
            break;
        case GL_SHORT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int16_t *) src)[j];
            break;
        case GL_UNSIGNED_SHORT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const uint16_t *) src)[j];
            break;
        case GL_BYTE:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int8_t *) src)[j];
            break;
        case GL_UNSIGNED_BYTE:
            for (int j = 0; j < size; j++) dst[j] = (float) src[j];
            break;
        case GL_INT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const int32_t *) src)[j];
            break;
        case GL_UNSIGNED_INT:
            for (int j = 0; j < size; j++) dst[j] = (float) ((const uint32_t *) src)[j];
            break;
        default:
            /* Unknown type: previous behaviour (raw float copy) kept as the
             * least-surprising fallback. */
            memcpy(dst, src, size * sizeof(float));
            break;
    }
}

GPU_Primitive_t gl_to_gpu_primitive(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES: return GPU_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GPU_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN: return GPU_TRIANGLE_FAN;
        default: return GPU_TRIANGLES;
    }
}

GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type) {
    if (format == GL_RGBA || format == GL_RGBA8_OES) {
        if (type == GL_UNSIGNED_BYTE) return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_4_4_4_4) return GPU_RGBA4;
        if (type == GL_UNSIGNED_SHORT_5_5_5_1) return GPU_RGBA5551;
    }
    if (format == GL_RGB) {
        if (type == GL_UNSIGNED_BYTE) return GPU_RGBA8;
        if (type == GL_UNSIGNED_SHORT_5_6_5) return GPU_RGB565;
    }
    if (format == GL_LUMINANCE) return GPU_L8;
    if (format == GL_LUMINANCE_ALPHA) return GPU_LA8;
    if (format == GL_LUMINANCE_ALPHA4_NOVA) return GPU_LA4;
    if (format == GL_ALPHA) return GPU_A8;
    return GPU_RGBA8;
}

int gpu_texfmt_bpp(GPU_TEXCOLOR fmt) {
    switch (fmt) {
        case GPU_RGBA8: return 4;
        case GPU_RGB8: return 3;
        case GPU_RGBA5551:
        case GPU_RGB565:
        case GPU_RGBA4:
        case GPU_LA8: return 2;
        case GPU_L8:
        case GPU_A8:
        case GPU_LA4: return 1;
        default: return 4;
    }
}

/* Persistent texture staging buffer. Capped at 1MB (change 5): the previous
 * unbounded growth let a single 1024x1024 RGBA8 downscale (4MB) permanently
 * inflate this buffer, holding 4MB of RAM for the rest of the run for what is a
 * rare over-cap upload. Requests over the cap return NULL — the caller (see
 * texture.c) then uses a transient malloc/free for that one upload. Matches the
 * 1MB HW-swizzle staging cap. After many frames with no large upload the buffer
 * is shrunk back to a small floor so a one-off big-ish texture doesn't pin RAM. */
#define NOVA_TEX_STAGING_CAP    (1024 * 1024)
#define NOVA_TEX_STAGING_FLOOR  (256 * 1024)
#define NOVA_TEX_STAGING_SHRINK_FRAMES 240
static int s_tex_staging_idle_frames = 0;

void *get_tex_staging(int size) {
    if (size > NOVA_TEX_STAGING_CAP) {
        /* Over cap: do NOT grow the persistent buffer. Caller falls back to a
         * transient allocation for this upload. */
        return NULL;
    }
    s_tex_staging_idle_frames = 0; /* a real staging use this frame */
    if (g.tex_staging_size < size) {
        void *new_buf = realloc(g.tex_staging, (size_t) size);
        if (!new_buf) {
            return NULL;
        }
        g.tex_staging = new_buf;
        g.tex_staging_size = size;
    }
    return g.tex_staging;
}

/* Called once per frame from novaSwapBuffers. After a long stretch with no
 * staging use, shrink the persistent buffer back to the floor so a single
 * mid-size upload doesn't pin ~1MB forever. Cheap: only reallocs on the exact
 * frame the idle counter trips, and only if the buffer is above the floor. */
void nova_tex_staging_tick(void) {
    if (g.tex_staging_size <= NOVA_TEX_STAGING_FLOOR)
        return;
    if (++s_tex_staging_idle_frames < NOVA_TEX_STAGING_SHRINK_FRAMES)
        return;
    void *shrunk = realloc(g.tex_staging, (size_t) NOVA_TEX_STAGING_FLOOR);
    if (shrunk) {
        g.tex_staging = shrunk;
        g.tex_staging_size = NOVA_TEX_STAGING_FLOOR;
    }
    s_tex_staging_idle_frames = 0;
}

uint32_t morton_interleave(uint32_t x, uint32_t y) {
    static const uint32_t xlut[8] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    static const uint32_t ylut[8] = {0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a};
    return xlut[x & 7] | ylut[y & 7];
}

/* Per-pixel morton offset inside an 8x8 tile, precomputed at file scope.
 * Replaces 4 mul/shift ops with a single LUT read in the swizzle inner loop. */
static const uint8_t k_tile_morton[64] = {
     0,  1,  4,  5, 16, 17, 20, 21,
     2,  3,  6,  7, 18, 19, 22, 23,
     8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53,
    34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61,
    42, 43, 46, 47, 58, 59, 62, 63,
};

static inline int morton_offset(int x, int y, int pot_w, int pot_h) {
    int fy = pot_h - 1 - y;
    int tile_offset = ((fy >> 3) * (pot_w >> 3) + (x >> 3)) * 64;
    return tile_offset + (int) morton_interleave(x & 7, fy & 7);
}

/* Tile-walk swizzlers: outer loops over 8x8 tiles in dest-space, inner loop
 * over 64 pixels in tile-space. ~2x faster than the naive per-pixel
 * morton_offset() call because the per-tile base pointer is hoisted out and
 * the inner loop becomes a flat 64-iteration LUT lookup. Used by
 * upload_page_*; the legacy swizzle_* names below delegate. */
void swizzle_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint8_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint8_t val = (sx < src_w && sy < src_h) ? src[sy * src_w + sx] : 0;
                    tile[k_tile_morton[(fy << 3) | px]] = val;
                }
            }
        }
    }
}

void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint16_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint16_t val = (sx < src_w && sy < src_h) ? src[sy * src_w + sx] : 0;
                    tile[k_tile_morton[(fy << 3) | px]] = val;
                }
            }
        }
    }
}

void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h) {
    for (int ty = 0; ty < pot_h; ty += 8) {
        for (int tx = 0; tx < pot_w; tx += 8) {
            uint32_t *tile = dst + ((pot_h - 8 - ty) >> 3) * (pot_w >> 3) * 64 + (tx >> 3) * 64;
            for (int py = 0; py < 8; py++) {
                int sy = ty + py;
                int fy = 7 - py;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    uint32_t out_pixel = 0;
                    if (sx < src_w && sy < src_h) {
                        uint32_t pixel = src[sy * src_w + sx];
                        uint8_t r   = (pixel >>  0) & 0xFF;
                        uint8_t g_c = (pixel >>  8) & 0xFF;
                        uint8_t b   = (pixel >> 16) & 0xFF;
                        uint8_t a   = (pixel >> 24) & 0xFF;
                        out_pixel = ((uint32_t) r << 24) | ((uint32_t) g_c << 16) |
                                    ((uint32_t) b << 8)  |  (uint32_t) a;
                    }
                    tile[k_tile_morton[(fy << 3) | px]] = out_pixel;
                }
            }
        }
    }
}

uint32_t *rgb_to_rgba(const uint8_t *rgb, int w, int h) {
    uint32_t *out = (uint32_t *) malloc(w * h * 4);
    if (!out) return NULL;
    for (int i = 0; i < w * h; i++) {
        //this color channels in C3D is really fucking fuck. PICA200 is fucking puzzle.
        out[i] = (rgb[i * 3 + 0] << 24) | (rgb[i * 3 + 1] << 16) | (rgb[i * 3 + 2] << 8) | 0xFF;
    }
    return out;
}

void downscale_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter: average every source pixel covered by each destination cell.
    // Uses fixed-point spans so no source pixel is skipped even for large scale factors
    // (e.g. 4096x4096 -> 1024x1024, the typical GameMaker atlas case).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t r = 0, g = 0, b = 0, a = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint32_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint32_t p = row[xx];
                    r += (p >> 0) & 0xFF;
                    g += (p >> 8) & 0xFF;
                    b += (p >> 16) & 0xFF;
                    a += (p >> 24) & 0xFF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint8_t rr = (uint8_t) (r / count);
            uint8_t gg = (uint8_t) (g / count);
            uint8_t bb = (uint8_t) (b / count);
            uint8_t aa = (uint8_t) (a / count);
            dst[y * dst_w + x] = ((uint32_t) aa << 24) | ((uint32_t) bb << 16) | ((uint32_t) gg << 8) | rr;
        }
    }
}

void downscale_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter for RGBA4/RGB565-ish 16-bit. Unpack as 4x4-bit channels, average, repack.
    // This assumes the 16-bit format is a 4-channel 4-bit layout (works for RGBA4);
    // for RGB565 the per-channel averaging still produces a visually reasonable result
    // since we average within the same bit-layout (the bias from channel widths is negligible at dst scale).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            uint32_t count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint16_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint16_t p = row[xx];
                    c0 += (p >> 0) & 0xF;
                    c1 += (p >> 4) & 0xF;
                    c2 += (p >> 8) & 0xF;
                    c3 += (p >> 12) & 0xF;
                    count++;
                }
            }
            if (count == 0) count = 1;
            uint16_t v = (uint16_t) (
                ((c0 / count) & 0xF) |
                (((c1 / count) & 0xF) << 4) |
                (((c2 / count) & 0xF) << 8) |
                (((c3 / count) & 0xF) << 12)
            );
            dst[y * dst_w + x] = v;
        }
    }
}

void downscale_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int dst_w, int dst_h) {
    // Box filter for single-channel 8-bit (alpha/luminance).
    for (int y = 0; y < dst_h; y++) {
        int sy0 = (int) ((int64_t) y * src_h / dst_h);
        int sy1 = (int) ((int64_t) (y + 1) * src_h / dst_h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src_h) sy1 = src_h;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = (int) ((int64_t) x * src_w / dst_w);
            int sx1 = (int) ((int64_t) (x + 1) * src_w / dst_w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src_w) sx1 = src_w;

            uint32_t sum = 0, count = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint8_t *row = src + yy * src_w;
                for (int xx = sx0; xx < sx1; xx++) {
                    sum += row[xx];
                    count++;
                }
            }
            if (count == 0) count = 1;
            dst[y * dst_w + x] = (uint8_t) (sum / count);
        }
    }
}

void apply_depth_map(void) {
    /* PICA200 stores the depth buffer with the "near = high, far = low"
     * convention (citro3d's base.c initializes with C3D_DepthMap(-1, 0)).
     * That's why gl_to_gpu_depth_testfunc inverts every comparison —
     * GL_LESS ("pass if nearer = smaller GL depth") becomes GPU_GREATER
     * ("pass if larger PICA buffer value").
     *
     * For glDepthRange(gl_near, gl_far) with NovaGL's projection fixup
     * producing z_ndc ∈ [-1, 0] (PICA near = -1, PICA far = 0), the
     * mapping to the inverted-buffer convention is:
     *   At z_ndc = -1 (GL near):  PICA depth = 1 - gl_near
     *   At z_ndc =  0 (GL far):   PICA depth = 1 - gl_far
     * Solving `depth = scale * z_ndc + offset`:
     *   scale  = -(gl_far - gl_near)
     *   offset =  1 - gl_far
     * Defaults (0, 1) → (-1, 0) — identical to citro3d's stock setup.
     *
     * The previous (+far-near, +far) passed scale=+1, offset=+1, giving
     * `depth = z_ndc + 1` (GL convention) while the comparator stayed
     * inverted. Result: near-camera geometry was treated as if at the
     * far plane → hand renders behind blocks in MC/PD, side-textures
     * disappear, block interiors don't render. */
    float scale  = -(g.depth_far - g.depth_near);
    float offset = 1.0f - g.depth_far;

    /* Polygon-offset: positive `units` in GL means "push polygons toward
     * the FAR plane". In PICA's inverted-depth buffer, far = small value,
     * so we SUBTRACT (was += before, pushing the wrong direction). */
    if (g.polygon_offset_fill_enabled) {
        offset -= (g.polygon_offset_units * 0.0001f);
    }

    C3D_DepthMap(true, scale, offset);
}

/* Clear the PICA on-chip early-depth buffer. Register map cross-checked
 * against the DMP GLES2 driver: TI_EARLYZ_CLEAR = 0x63 = libctru
 * GPUREG_EARLYDEPTH_CLEAR, EZ_Z_CLEAR_VALUE = 0x6A = GPUREG_EARLYDEPTH_DATA.
 * Two constraints drive the raw writes:
 *  - the clear only latches while early-Z is ENABLED (DMP: "to clear early
 *    depth buffer, need to enable early depth in advance"), and
 *  - C3D_EarlyDepthTest only mutates citro3d's shadow struct; the registers
 *    land at the NEXT draw's effect flush — after our clear, i.e. too late.
 * So enable + clear are written directly into the command list here, and
 * NOVA_DIRTY_EARLY_DEPTH makes the next draw re-emit the proper enable state
 * (citro3d re-emits its whole effect block from the shadow, which we never
 * touched). Clear value 0 = far plane in NovaGL's inverted-Z convention, so
 * the buffer starts "everything passes" — matching a fresh depth clear. */
void nova_clear_early_depth(void) {
    if (!g.early_z_allowed)
        return;
    GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 0x1, 1);
    GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 1);
    GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_DATA, 0x7, 0);
    GPUCMD_AddWrite(GPUREG_EARLYDEPTH_CLEAR, 1);
    g.state_dirty_bits |= NOVA_DIRTY_EARLY_DEPTH;
}

static GPU_TEVSRC get_tev_src(GLint gl_src, GPU_TEVSRC tex_src, GPU_TEVSRC prev_src) {
    if (gl_src == GL_TEXTURE) return tex_src;
    if (gl_src == GL_PREVIOUS) return prev_src;
    if (gl_src == GL_PRIMARY_COLOR) return GPU_PRIMARY_COLOR;
    if (gl_src == GL_CONSTANT) return GPU_CONSTANT;
    return GPU_PRIMARY_COLOR;
}

/* Pack a 0..1 RGBA into PICA's 0xAABBGGRR layout (high byte = alpha) consumed
 * by C3D_TexEnvColor. */
static u32 pack_tev_color(const float c[4]) {
    u32 r = (u32) (clampf(c[0], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 g_ = (u32) (clampf(c[1], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 b = (u32) (clampf(c[2], 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 a = (u32) (clampf(c[3], 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g_ << 8) | r;
}

/* GL_RGB_SCALE / GL_ALPHA_SCALE value (1, 2 or 4) -> PICA GPU_TEVSCALE. Anything
 * else falls back to 1x (GL only permits those three). */
static inline GPU_TEVSCALE gl_to_gpu_tevscale(GLint scale) {
    switch (scale) {
        case 2:  return GPU_TEVSCALE_2;
        case 4:  return GPU_TEVSCALE_4;
        default: return GPU_TEVSCALE_1;
    }
}

/* Translate a GL TEV source enum to PICA. Unlike get_tev_src() below (which
 * is for the per-unit GL_COMBINE path and resolves GL_TEXTURE to "this
 * unit's tex"), the explicit-stage path accepts named texture-unit sources
 * directly so a single stage can sample TEXTURE1 even when emitted in
 * "slot 0". Used by the novaSetExplicitTevStages handler in apply_gpu_state. */
static GPU_TEVSRC get_tev_src_explicit(GLenum gl_src) {
    switch (gl_src) {
        case GL_TEXTURE0_ARB:    return GPU_TEXTURE0;
        case GL_TEXTURE1_ARB:    return GPU_TEXTURE1;
        case GL_TEXTURE2_ARB:    return GPU_TEXTURE2;
        case GL_TEXTURE:         return GPU_TEXTURE0; // implicit unit-0 alias
        case GL_PREVIOUS:        return GPU_PREVIOUS;
        case GL_PRIMARY_COLOR:   return GPU_PRIMARY_COLOR;
        case GL_CONSTANT:        return GPU_CONSTANT;
        default:                 return GPU_PRIMARY_COLOR;
    }
}

GPU_COMBINEFUNC gl_to_gpu_combinefunc(GLenum gl_func) {
    switch (gl_func) {
        case GL_REPLACE:     return GPU_REPLACE;
        case GL_MODULATE:    return GPU_MODULATE;
        case GL_ADD:         return GPU_ADD;
        case GL_INTERPOLATE: return GPU_INTERPOLATE;
        case GL_SUBTRACT:    return GPU_SUBTRACT;
        case GL_DOT3_RGBA_ARB: return GPU_DOT3_RGBA;
        case GL_MULT_ADD_NOVA: return GPU_MULTIPLY_ADD; /* (s0*s1)+s2 */
        default:             return GPU_REPLACE;
    }
}

void novaSetExplicitTevStages(int count, const NovaTevStageGL *stages) {
    if (count < 0) count = 0;
    if (count > NOVA_TEV_MAX_STAGES) count = NOVA_TEV_MAX_STAGES;
    /* The fast3d backend re-stages the TEV programme on EVERY draw (the
     * per-draw constants force it to). Most consecutive draws share the same
     * programme, so skip the dirty-flag when nothing changed — otherwise we'd
     * rewrite all 6 TEV stage registers per draw call. memcmp is safe: the
     * struct is all 4-byte members (no padding) and callers build it with
     * memset+assignments. */
    if (count == g.explicit_tev_count &&
        (count == 0 ||
         (stages && memcmp(g.explicit_tev_stages, stages,
                           (size_t) count * sizeof(NovaTevStageGL)) == 0))) {
        return;
    }
    g.explicit_tev_count = count;
    if (count > 0 && stages) {
        memcpy(g.explicit_tev_stages, stages, count * sizeof(NovaTevStageGL));
    }
    g.tev_dirty = 1;
}

/* Rebuild the PICA fragment-lighting environment from GL state and bind it.
 * Called from apply_gpu_state on lit draws. C3D stores colours in B,G,R order
 * (see C3D_LightAmbient: ambient[0]=b): the C3D_Light* setters take (r,g,b) and
 * swap internally, but C3D_Material is filled directly so we pre-swap to BGR. */
void nova_apply_light_env(void) {
    if (g.light_dirty || !g.light_env_built) {
        C3D_LightEnvInit(&g.light_env);

        C3D_Material mtl;
        /* BGR order to match C3D's internal convention. */
        mtl.ambient[0]   = g.mat_ambient[2];  mtl.ambient[1]   = g.mat_ambient[1];  mtl.ambient[2]   = g.mat_ambient[0];
        mtl.diffuse[0]   = g.mat_diffuse[2];  mtl.diffuse[1]   = g.mat_diffuse[1];  mtl.diffuse[2]   = g.mat_diffuse[0];
        mtl.specular0[0] = g.mat_specular[2]; mtl.specular0[1] = g.mat_specular[1]; mtl.specular0[2] = g.mat_specular[0];
        mtl.specular1[0] = 0.0f;              mtl.specular1[1] = 0.0f;              mtl.specular1[2] = 0.0f;
        mtl.emission[0]  = g.mat_emission[2]; mtl.emission[1]  = g.mat_emission[1]; mtl.emission[2]  = g.mat_emission[0];
        C3D_LightEnvMaterial(&g.light_env, &mtl);

        /* GL_LIGHT_MODEL_AMBIENT — global ambient term. */
        C3D_LightEnvAmbient(&g.light_env, g.light_model_ambient[0],
                            g.light_model_ambient[1], g.light_model_ambient[2]);

        /* Specular distribution LUT (Phong). shininess 0 would flatten the LUT
         * to a step; clamp to >=1 so unset-shininess materials still light. */
        float shininess = g.mat_shininess > 1.0f ? g.mat_shininess : 1.0f;
        LightLut_Phong(&g.light_lut_phong, shininess);
        C3D_LightEnvLut(&g.light_env, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &g.light_lut_phong);

        int n = 0;
        for (int i = 0; i < NOVA_MAX_LIGHTS && n < NOVA_MAX_LIGHTS; i++) {
            if (!g.lights[i].enabled) continue;
            NovaLight *L = &g.lights[i];
            C3D_Light *cl = &g.c3d_lights[n];
            C3D_LightInit(cl, &g.light_env);
            /* Diffuse and specular are separate in GL (GL_DIFFUSE / GL_SPECULAR);
             * the old code used C3D_LightColor which forces specular = diffuse.
             * Set them independently so a coloured/!=diffuse GL_SPECULAR is
             * honoured. specular1 (PICA's 2nd specular layer) has no GL analogue,
             * so pin it to 0 to avoid a phantom second highlight. (All three
             * setters take r,g,b and swap to BGR internally.) */
            C3D_LightDiffuse(cl, L->diffuse[0], L->diffuse[1], L->diffuse[2]);
            C3D_LightSpecular0(cl, L->specular[0], L->specular[1], L->specular[2]);
            C3D_LightSpecular1(cl, 0.0f, 0.0f, 0.0f);
            C3D_LightAmbient(cl, L->ambient[0], L->ambient[1], L->ambient[2]);
            /* GL_POSITION: w==0 -> directional, else positional. C3D_LightPosition
             * reads w to pick the mode. NOTE: GL transforms POSITION by the
             * modelview at glLight time into eye space; NovaGL passes it raw, so
             * the caller's space is whatever it set (the pddi re-sets lights each
             * frame). This is the main thing to tune if lights look off. */
            C3D_FVec pos = FVec4_New(L->position[0], L->position[1],
                                     L->position[2], L->position[3]);
            C3D_LightPosition(cl, &pos);

            /* Spotlight + distance attenuation only affect POSITIONAL lights
             * (GL ignores both for directional w==0 sources). */
            int positional = (L->position[3] != 0.0f);

            /* GL_SPOT_CUTOFF == 180 means "not a spotlight". A real cone is
             * 0..90 deg. C3D_LightSpotLut builds a LUT keyed on the angle to the
             * spot axis; we use citro3d's hard-edged spot_step helper, so
             * GL_SPOT_EXPONENT (soft falloff) is approximated as a hard cone.
             * C3D_LightSpotDir negates+normalises internally, so pass GL's
             * outward-pointing GL_SPOT_DIRECTION as-is. */
            if (positional && L->spot_cutoff >= 0.0f && L->spot_cutoff < 180.0f) {
                float cutoff_rad = L->spot_cutoff * 0.017453292519943295f; /* deg->rad */
                LightLut_Spotlight(&g.light_lut_spot[n], cutoff_rad);
                C3D_LightSpotLut(cl, &g.light_lut_spot[n]);
                C3D_LightSpotDir(cl, L->spot_direction[0], L->spot_direction[1], L->spot_direction[2]);
                C3D_LightSpotEnable(cl, true);
            }

            /* GL attenuation: 1/(kc + kl*d + kq*d^2). citro3d's quadratic helper
             * fixes kc at 1 (the GL default), so we feed kl/kq directly and
             * assume kc ~= 1 (documented limitation). Build a LUT over a distance
             * range out to where attenuation has decayed to ~1%. Only enable when
             * there's a real distance term. */
            if (positional && (L->atten_linear > 0.0f || L->atten_quadratic > 0.0f)) {
                float d_far;
                if (L->atten_quadratic > 0.0f)
                    d_far = sqrtf(99.0f / L->atten_quadratic);
                else
                    d_far = 99.0f / L->atten_linear;
                if (d_far < 16.0f)   d_far = 16.0f;
                if (d_far > 8192.0f) d_far = 8192.0f;
                LightLutDA_Quadratic(&g.light_lut_da[n], 0.0f, d_far,
                                     L->atten_linear, L->atten_quadratic);
                C3D_LightDistAttn(cl, &g.light_lut_da[n]);
                C3D_LightDistAttnEnable(cl, true);
            }
            n++;
        }
        g.light_env_built = 1;
        g.light_dirty = 0;
    }
    C3D_LightEnvBind(&g.light_env);
}

void novaClearExplicitTevStages(void) {
    if (g.explicit_tev_count != 0) {
        g.explicit_tev_count = 0;
        g.tev_dirty = 1;
    }
}


/* =========================================================================
 * CORE RENDER STATE APPLICATION
 * =========================================================================
 * Translates the high-level OpenGL state machine into PICA200 hardware
 * registers. This function sits in the absolute hottest path of the engine.
 *
 * To maximize performance on the 268MHz ARM11, this function employs:
 *  - State Dirty Bitmasks (bypassing branch-heavy state checks).
 *  - Fast TexEnv Compiler (direct 32-bit register writes).
 *  - Deferred RAW Hazard Resolution (avoiding pipeline stalls).
 *  - Hardware Early-Z Culling (boosting fillrate).
 * ========================================================================= */
void apply_gpu_state(void) {
    /* --- 0. Incremental command-list kickoff (opt-in) ----------------------
     * DMP's driver hands the GPU finished command ranges mid-frame
     * (nngxSplitDrawCmdlist) so it executes the frame's head while the CPU
     * builds the tail. C3D_FrameSplit(0) is the citro3d equivalent — async,
     * no wait. Splitting BEFORE the next draw flushes everything queued so
     * far, which is equivalent to splitting after the previous one. */
    if (g.auto_split_draws > 0 && ++g.draws_since_split >= g.auto_split_draws) {
        g.draws_since_split = 0;
        if (g.p3d_pending)
            C3D_FrameSplit(0);
    }

    /* --- 1. Shader Selector -----------------------------------------------
     * Pick the cheapest shader that satisfies the current state:
     *   clipspace : explicit (set by novaBeginClipSpace2D)
     *   lighting  : HW fragment lighting enabled + normal array present
     *   basic     : no VERTEX fog + identity tex matrix
     *   texmtx    : no VERTEX fog + non-identity tex matrix
     *   full      : vertex fog needed (GL_LINEAR fog enabled)
     *
     * Switching shaders invalidates the GPU's uniform state, so we force all
     * matrix stacks + fog to be marked dirty upon change. */
    int vertex_fog_needed = g.fog_enabled && g.fog_mode == GL_LINEAR;
    int lit = g.lighting_enabled && g.va_normal.enabled && g.shader_lighting_dvlb
              && !g.clipspace_mode_enabled;
    g.lighting_active = lit;

    int desired;
    if (g.clipspace_mode_enabled && g.shader_clipspace_dvlb) {
        desired = NOVA_SHADER_CLIPSPACE;
    } else if (lit) {
        desired = NOVA_SHADER_LIGHTING;
    } else if (!vertex_fog_needed && g.tex_mtx_is_identity && g.shader_basic_dvlb) {
        desired = NOVA_SHADER_BASIC;
    } else if (!vertex_fog_needed && g.shader_texmtx_dvlb) {
        desired = NOVA_SHADER_TEXMTX;
    } else {
        desired = NOVA_SHADER_FULL;
    }

    if (desired != g.active_shader) {
        switch (desired) {
            case NOVA_SHADER_CLIPSPACE: C3D_BindProgram(&g.shader_clipspace_program); break;
            case NOVA_SHADER_LIGHTING:  C3D_BindProgram(&g.shader_lighting_program);  break;
            case NOVA_SHADER_BASIC:     C3D_BindProgram(&g.shader_basic_program);     break;
            case NOVA_SHADER_TEXMTX:    C3D_BindProgram(&g.shader_texmtx_program);    break;
            default:                    C3D_BindProgram(&g.shader_program);           break;
        }
        g.active_shader = desired;
        g.matrices_dirty = 1;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
        g.fog_dirty = 1;
    }

    /* --- 2. Matrices & Uniforms ------------------------------------------- */
    if (g.matrices_dirty) {
        int force_proj_upload = (osGet3DSliderState() > 0.0f && g.stereo_depth != 0.0f);
        int need_proj_rebuild = (g.proj_dirty || force_proj_upload || !g.final_proj_cached_valid);
        int uploaded_proj_this_call = 0;

        C3D_Mtx final_proj;

        if (need_proj_rebuild) {
            C3D_Mtx adj_proj = g.proj_stack[g.proj_sp];

            float slider = osGet3DSliderState();
            if (slider > 0.0f && g.stereo_depth != 0.0f) {
                float shift = slider * g.stereo_depth;
                float offset = (g.current_eye == 0) ? shift : -shift;
                C3D_Mtx trans; Mtx_Identity(&trans); trans.r[0].w = offset;
                C3D_Mtx temp_proj; Mtx_Multiply(&temp_proj, &trans, &adj_proj);
                adj_proj = temp_proj;
            }

            // PICA200 Clip-Space Z-range fixup (GL [-w, w] -> PICA [-w, 0])
            adj_proj.r[2].x = adj_proj.r[2].x * 0.4999f - adj_proj.r[3].x * 0.5f;
            adj_proj.r[2].y = adj_proj.r[2].y * 0.4999f - adj_proj.r[3].y * 0.5f;
            adj_proj.r[2].z = adj_proj.r[2].z * 0.4999f - adj_proj.r[3].z * 0.5f;
            adj_proj.r[2].w = adj_proj.r[2].w * 0.4999f - adj_proj.r[3].w * 0.5f;

            C3D_Mtx tilt;
            Mtx_Identity(&tilt);

            // Screen frame (app surface or direct LCD) is rendered sideways. FBOs stay landscape.
            #ifndef NOVAGL_TILT_VARIANT
            #define NOVAGL_TILT_VARIANT 1
            #endif
            if (g.bound_fbo == 0) {
                #if NOVAGL_TILT_VARIANT == 1
                tilt.r[0].x = 0.0f; tilt.r[0].y =  1.0f; tilt.r[1].x = -1.0f; tilt.r[1].y = 0.0f;
                #elif NOVAGL_TILT_VARIANT == 2
                tilt.r[0].x = 0.0f; tilt.r[0].y = -1.0f; tilt.r[1].x = 1.0f;  tilt.r[1].y = 0.0f;
                #elif NOVAGL_TILT_VARIANT == 3
                tilt.r[0].x = -1.0f; tilt.r[0].y = 0.0f; tilt.r[1].x = 0.0f;  tilt.r[1].y = -1.0f;
                #endif
            }

            Mtx_Multiply(&final_proj, &tilt, &adj_proj);
            g.final_proj_cached = final_proj;
            g.final_proj_cached_valid = 1;
        } else {
            final_proj = g.final_proj_cached;
        }

        switch (g.active_shader) {
            case NOVA_SHADER_CLIPSPACE:
                if (g.uLoc_projection_clipspace >= 0 && need_proj_rebuild) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &final_proj);
                }
                break;
            case NOVA_SHADER_BASIC:
                if (need_proj_rebuild || g.mv_dirty) {
                    Mtx_Multiply(&g.mvp_combined, &final_proj, &g.mv_stack[g.mv_sp]);
                    if (g.uLoc_mvp_basic >= 0)
                        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_mvp_basic, &g.mvp_combined);
                }
                break;
            case NOVA_SHADER_TEXMTX:
                if (need_proj_rebuild || g.mv_dirty) {
                    Mtx_Multiply(&g.mvp_combined, &final_proj, &g.mv_stack[g.mv_sp]);
                    if (g.uLoc_mvp_texmtx >= 0)
                        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_mvp_texmtx, &g.mvp_combined);
                }
                if (g.uLoc_texmtx_texmtx >= 0 && g.tex_mtx_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_texmtx_texmtx, &g.tex_stack[g.tex_sp]);
                break;
            case NOVA_SHADER_LIGHTING:
                if (need_proj_rebuild && g.uLoc_projection_lighting >= 0) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_lighting, &final_proj);
                    uploaded_proj_this_call = 1;
                }
                if (g.uLoc_modelview_lighting >= 0 && g.mv_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_modelview_lighting, &g.mv_stack[g.mv_sp]);
                break;
            default: /* NOVA_SHADER_FULL */
                if (need_proj_rebuild && g.uLoc_projection >= 0) {
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection, &final_proj);
                    uploaded_proj_this_call = 1;
                }
                if (g.uLoc_modelview >= 0 && g.mv_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_modelview, &g.mv_stack[g.mv_sp]);
                if (g.uLoc_texmtx >= 0 && g.tex_mtx_dirty)
                    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_texmtx, &g.tex_stack[g.tex_sp]);
                break;
        }
        (void)uploaded_proj_this_call;
        g.matrices_dirty = 0;
        g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 0;
    }

    /* --- 3. Render State Application (Dirty Bitmask) ----------------------
     * By tracking dirty state via a 32-bit bitmask, we bypass hundreds of
     * branch instructions. The variables used below (g.gpu_blend_src, etc.)
     * were pre-translated from GL enums to PICA enums during API calls. */
    if (g.state_dirty_bits & NOVA_DIRTY_DEPTH_TEST) {
        GPU_WRITEMASK writemask = 0;
        if (g.color_mask_r) writemask |= GPU_WRITE_RED;
        if (g.color_mask_g) writemask |= GPU_WRITE_GREEN;
        if (g.color_mask_b) writemask |= GPU_WRITE_BLUE;
        if (g.color_mask_a) writemask |= GPU_WRITE_ALPHA;
        if (g.depth_mask && g.depth_test_enabled) writemask |= GPU_WRITE_DEPTH;

        // C3D_DepthTest uses inverted semantics (GL_LESS -> PICA GREATER) internally mapped via gpu_depth_func
        C3D_DepthTest(g.depth_test_enabled, g.gpu_depth_func, writemask);
    }

    /* Hardware Early Z-Culling: Rejects occluded fragments before TMU sampling,
     * saving massive fillrate. Must be disabled if alpha test or blending modifies visibility.
     * novaSetEarlyZEnabled(0) kills it globally — diagnostic switch for the
     * "geometry shows through / black wedges when pitching the camera"
     * class of bugs (stale early-depth state is a prime suspect). */
    if (g.state_dirty_bits & NOVA_DIRTY_EARLY_DEPTH) {
        /* Early-Z can only express the four ORDERING comparisons — the DMP
         * driver's glEarlyDepthFuncDMP accepts exactly LESS/LEQUAL/GREATER/
         * GEQUAL and nothing else. For EQUAL/NOTEQUAL/ALWAYS/NEVER an early
         * pass both rejects fragments the real test would accept (GL_ALWAYS
         * skybox/fullscreen passes!) and poisons the on-chip early buffer
         * with depths the late test then discards. Keep early-Z off there. */
        int func_ok = (g.depth_func == GL_LESS || g.depth_func == GL_LEQUAL ||
                       g.depth_func == GL_GREATER || g.depth_func == GL_GEQUAL);
        int want_early_z = g.early_z_allowed && g.depth_test_enabled && func_ok &&
                           !g.alpha_test_enabled && !g.blend_enabled;
        C3D_EarlyDepthTest(want_early_z, g.gpu_early_depth_func, 0);
    }

    if (g.state_dirty_bits & NOVA_DIRTY_ALPHA_TEST) {
        C3D_AlphaTest(g.alpha_test_enabled, g.gpu_alpha_func, g.alpha_ref8); // alpha_ref8 pre-packed
    }

    if (g.state_dirty_bits & NOVA_DIRTY_BLEND_STATE) {
        if (g.color_logic_op_enabled) {
            C3D_ColorLogicOp(g.gpu_logic_op);
        } else if (g.blend_enabled) {
            C3D_BlendingColor(g.blend_color_packed); // Pre-packed 0xAABBGGRR
            C3D_AlphaBlend(g.gpu_blend_eq_rgb, g.gpu_blend_eq_alpha,
                           g.gpu_blend_src, g.gpu_blend_dst,
                           g.gpu_blend_src_alpha, g.gpu_blend_dst_alpha);
        } else {
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        }
    }

    if (g.state_dirty_bits & NOVA_DIRTY_CULLING) {
        if (g.cull_face_enabled) {
            GPU_CULLMODE cull;
            if (g.cull_face_mode == GL_FRONT || g.cull_face_mode == GL_FRONT_AND_BACK)
                cull = (g.front_face == GL_CCW) ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
            else
                cull = (g.front_face == GL_CCW) ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
            C3D_CullFace(cull);
        } else {
            C3D_CullFace(GPU_CULL_NONE);
        }
    }

    if (g.state_dirty_bits & NOVA_DIRTY_SCISSOR) {
        if (g.scissor_test_enabled) {
            apply_scissor_box();
        } else {
            C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        }
    }

    // Clear processed bits. Safe because all dependent state has been flushed to PICA.
    g.state_dirty_bits &= ~(NOVA_DIRTY_DEPTH_TEST | NOVA_DIRTY_EARLY_DEPTH |
                            NOVA_DIRTY_ALPHA_TEST | NOVA_DIRTY_BLEND_STATE |
                            NOVA_DIRTY_CULLING | NOVA_DIRTY_SCISSOR);


    /* --- 4. Fragment Lighting & Fog --------------------------------------- */
    if (g.lighting_active) {
        nova_apply_light_env();
    } else {
        C3D_LightEnvBind(NULL);
    }

    if (g.fog_dirty) {
        if (g.active_shader == NOVA_SHADER_FULL && g.uLoc_fogparams >= 0) {
            int vertex_fog = g.fog_enabled && g.fog_mode == GL_LINEAR;
            if (vertex_fog) {
                float safe_end = g.fog_end;
                if (fabsf(safe_end - g.fog_start) < 0.001f) safe_end = g.fog_start + 0.001f;
                float inv_range = 1.0f / (safe_end - g.fog_start);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, g.fog_start, safe_end, inv_range, 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, g.fog_color[0], g.fog_color[1], g.fog_color[2], g.fog_color[3]);
            } else {
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams, 0.0f, 0.0f, 0.0f, 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, g.uLoc_fogparams + 1, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

#ifdef NOVAGL_DISABLE_FOG_LUT
        if (0) {
#else
        if (g.fog_enabled && (g.fog_mode == GL_EXP || g.fog_mode == GL_EXP2)) {
#endif
            static GLenum s_lut_mode = 0;
            static float s_lut_density = -1.0f, s_lut_start = 0.0f, s_lut_end = 0.0f;
            if (s_lut_mode != g.fog_mode || s_lut_density != g.fog_density ||
                s_lut_start != g.fog_start || s_lut_end != g.fog_end) {
                if (g.fog_mode == GL_EXP) {
                    FogLut_Exp(&g.fog_lut, g.fog_density, 1.0f, g.fog_start, g.fog_end);
                } else {
                    FogLut_Exp(&g.fog_lut, g.fog_density * g.fog_density, 2.0f, g.fog_start, g.fog_end);
                }
                s_lut_mode = g.fog_mode;
                s_lut_density = g.fog_density;
                s_lut_start = g.fog_start;
                s_lut_end = g.fog_end;
            }
            C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
            u32 col = ((u32)(clampf(g.fog_color[0], 0, 1) * 255) << 0)
                    | ((u32)(clampf(g.fog_color[1], 0, 1) * 255) << 8)
                    | ((u32)(clampf(g.fog_color[2], 0, 1) * 255) << 16);
            C3D_FogColor(col);
            C3D_FogLutBind(&g.fog_lut);
        } else {
            C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
        }
        g.fog_dirty = 0;
    }


    /* --- 5. Texture Environment (TEV) Compiler ---------------------------- */
    int current_tex_state = 0;
    for (int i = 0; i < 3; i++) {
        if (g.texture_2d_enabled_unit[i] && g.bound_texture[i] > 0) current_tex_state |= (1 << i);
    }

    /* The TEV base-colour source depends on lighting_active (vertex colour vs
     * HW fragment primary). Re-emit the chain when that toggles. */
    static int s_last_lighting_active = -1;
    if (s_last_lighting_active != g.lighting_active) {
        g.tev_dirty = 1;
        s_last_lighting_active = g.lighting_active;
    }

    if (g.tev_dirty || g.last_tex_state != current_tex_state) {

        // Explicit-stage path (for multi-pass custom CCs like fast3d)
        if (g.explicit_tev_count > 0) {
            int n = g.explicit_tev_count;
            for (int i = 0; i < n; i++) {
                const NovaTevStageGL *s = &g.explicit_tev_stages[i];
                C3D_TexEnv *env = C3D_GetTexEnv(i);
                C3D_TexEnvInit(env);

                GPU_TEVSRC s0_rgb = get_tev_src_explicit(s->src_rgb[0]);
                GPU_TEVSRC s1_rgb = get_tev_src_explicit(s->src_rgb[1]);
                GPU_TEVSRC s2_rgb = get_tev_src_explicit(s->src_rgb[2]);
                GPU_TEVSRC s0_a   = get_tev_src_explicit(s->src_alpha[0]);
                GPU_TEVSRC s1_a   = get_tev_src_explicit(s->src_alpha[1]);
                GPU_TEVSRC s2_a   = get_tev_src_explicit(s->src_alpha[2]);

                C3D_TexEnvSrc(env, C3D_RGB,   s0_rgb, s1_rgb, s2_rgb);
                C3D_TexEnvSrc(env, C3D_Alpha, s0_a,   s1_a,   s2_a);

                C3D_TexEnvOpRgb(env,
                                get_tev_op_rgb(s->op_rgb[0]),
                                get_tev_op_rgb(s->op_rgb[1]),
                                get_tev_op_rgb(s->op_rgb[2]));
                C3D_TexEnvOpAlpha(env,
                                  get_tev_op_alpha(s->op_alpha[0]),
                                  get_tev_op_alpha(s->op_alpha[1]),
                                  get_tev_op_alpha(s->op_alpha[2]));

                C3D_TexEnvFunc(env, C3D_RGB,   gl_to_gpu_combinefunc(s->combine_rgb));
                C3D_TexEnvFunc(env, C3D_Alpha, gl_to_gpu_combinefunc(s->combine_alpha));

                // CONSTANT is per-stage on PICA; push regardless (cost is
                // a single register write, and skipping based on detection
                // adds branches without saving meaningful work).
                C3D_TexEnvColor(env, pack_tev_color(s->constant_color));
            }
            // Pad remaining
            for (int i = n; i < 6; i++) {
                C3D_TexEnv *env = C3D_GetTexEnv(i);
                C3D_TexEnvInit(env);
                C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            }
            g.last_tex_state = current_tex_state;
            g.tev_dirty = 0;
            goto tev_done;
        }

        int tev_stage = 0;
        GPU_TEVSRC base_prim = g.lighting_active ? GPU_FRAGMENT_PRIMARY_COLOR : GPU_PRIMARY_COLOR;

        for (int unit = 0; unit < 3; unit++) {
            if (!(current_tex_state & (1 << unit))) continue;

            FastTevConfig* tc = &g.fast_tev[unit];
            C3D_TexEnv* env = C3D_GetTexEnv(tev_stage);

            // Re-map 0xFF marker to runtime base/previous source
            GPU_TEVSRC prev_src = (tev_stage == 0) ? base_prim : GPU_PREVIOUS;

            uint32_t s0_r = (tc->src_rgb[0] == 0xFF) ? prev_src : tc->src_rgb[0];
            uint32_t s1_r = (tc->src_rgb[1] == 0xFF) ? prev_src : tc->src_rgb[1];
            uint32_t s2_r = (tc->src_rgb[2] == 0xFF) ? prev_src : tc->src_rgb[2];
            uint32_t s0_a = (tc->src_a[0]   == 0xFF) ? prev_src : tc->src_a[0];
            uint32_t s1_a = (tc->src_a[1]   == 0xFF) ? prev_src : tc->src_a[1];
            uint32_t s2_a = (tc->src_a[2]   == 0xFF) ? prev_src : tc->src_a[2];

            // Map into citro3d's C3D_TexEnv fields
            env->srcRgb     = s0_r | (s1_r << 4) | (s2_r << 8);
            env->srcAlpha   = s0_a | (s1_a << 4) | (s2_a << 8);
            env->opAll      = tc->op;
            env->funcRgb    = tc->func & 0xFFFF;
            env->funcAlpha  = tc->func >> 16;
            env->scaleRgb   = tc->scale & 0xF;
            env->scaleAlpha = tc->scale >> 4;

            if (tc->uses_const) {
                env->color = pack_tev_color(g.tex_env_color[unit]);
            }

            C3D_DirtyTexEnv(env); // Уведомляем citro3d, что стейт нужно отправить в GPU!
            tev_stage++;
        }

        if (tev_stage == 0) {
            /* No texture units active: emit the base colour directly. Lit draws
             * show the HW lighting result; everything else the vertex colour. */
            C3D_TexEnv *env = C3D_GetTexEnv(0);
            env->srcRgb     = base_prim | (GPU_PRIMARY_COLOR << 4) | (GPU_PRIMARY_COLOR << 8);
            env->srcAlpha   = base_prim | (GPU_PRIMARY_COLOR << 4) | (GPU_PRIMARY_COLOR << 8);
            env->opAll      = GPU_TEVOP_RGB_SRC_COLOR | (GPU_TEVOP_A_SRC_ALPHA << 12);
            env->funcRgb    = GPU_REPLACE;
            env->funcAlpha  = GPU_REPLACE;
            env->scaleRgb   = GPU_TEVSCALE_1;
            env->scaleAlpha = GPU_TEVSCALE_1;
            C3D_DirtyTexEnv(env);
            tev_stage++;
        }

        for (int i = tev_stage; i < 6; i++) {
            C3D_TexEnv *env = C3D_GetTexEnv(i);
            env->srcRgb     = GPU_PREVIOUS | (GPU_PRIMARY_COLOR << 4) | (GPU_PRIMARY_COLOR << 8);
            env->srcAlpha   = GPU_PREVIOUS | (GPU_PRIMARY_COLOR << 4) | (GPU_PRIMARY_COLOR << 8);
            env->opAll      = GPU_TEVOP_RGB_SRC_COLOR | (GPU_TEVOP_A_SRC_ALPHA << 12);
            env->funcRgb    = GPU_REPLACE;
            env->funcAlpha  = GPU_REPLACE;
            env->scaleRgb   = GPU_TEVSCALE_1;
            env->scaleAlpha = GPU_TEVSCALE_1;
            C3D_DirtyTexEnv(env);
        }

        g.last_tex_state = current_tex_state;
        g.tev_dirty = 0;
    }
tev_done:;

    /* --- 6. Texture Binding & Deferred RAW Hazard Resolution --------------
     * Detect if we are binding a texture that was rendered to earlier in
     * this exact frame (FBO). Emit C3D_FrameSplit(0) just-in-time to flush
     * the pipeline, avoiding unnecessary stalls. */
    int raw_hazard = 0;
    for (int unit = 0; unit < 3; unit++) {
        if ((current_tex_state & (1 << unit)) && g.bound_texture[unit] < NOVA_MAX_TEXTURES) {
            if (g.textures[g.bound_texture[unit]].written_pending_split) {
                raw_hazard = 1;
                g.textures[g.bound_texture[unit]].written_pending_split = 0;
            }
        }
    }
    if (raw_hazard) {
        C3D_FrameSplit(0);
    }

    // Бинд текстуры — самая частая команда (терен, частицы, hud).
    // Пропускаем повторный C3D_TexBind с тем же id. Сброс происходит когда
    // юнит отключается, чтобы при повторном включении бинд гарантированно
    // переустанавливался. Также сбрасывается из nova_invalidate_state_cache().
    for (int unit = 0; unit < 3; unit++) {
        if ((current_tex_state & (1 << unit)) && g.bound_texture[unit] < NOVA_MAX_TEXTURES) {
            if (s_last_tex_bound[unit] != g.bound_texture[unit]) {
                TexSlot *slot = &g.textures[g.bound_texture[unit]];
                if (slot->allocated) {
                    C3D_TexBind(unit, &slot->tex);
                    s_last_tex_bound[unit] = g.bound_texture[unit];
                }
            }
        } else {
            s_last_tex_bound[unit] = 0xFFFFFFFFu;
        }
    }
}

void nova_setup_attr_info(int pos_elements) {
    if (s_attr_pos_elements == pos_elements) return;
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, pos_elements);
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);
    C3D_SetAttrInfo(&g.attr_info);
    s_attr_pos_elements = pos_elements;
}

void nova_setup_buf_info(void *base, int stride) {
    if (s_buf_base == base && s_buf_stride == stride) return;
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, base, stride, 3, 0x210);
    s_buf_base = base;
    s_buf_stride = stride;
}

/* Force the next nova_setup_buf_info to re-apply. Used after the lit draw path
 * programs BufInfo directly (4 attrs / perm 0x3210), so the 3-attr cache can't
 * wrongly conclude "already set" on the following non-lit draw. */
void nova_invalidate_buf_cache(void) {
    s_buf_base = (void *) -1;
    s_buf_stride = -1;
}

/* Same for the AttrInfo cache: the multistream zero-copy draw path programs
 * per-draw loaders (formats vary per mesh), so the cached "layout already
 * set" keys must be dropped afterwards. */
void nova_invalidate_attr_cache(void) {
    s_attr_pos_elements = -999;
}

/* Lit layout: pos(3f) + texcoord(2f) + color(4ub) + normal(3f), 4 attributes.
 * Kept on its own cache key so toggling lighting forces a re-setup. */
void nova_setup_attr_info_lit(void) {
    if (s_attr_pos_elements == -4) return; /* sentinel: lit layout already set */
    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 3);          /* v0 position */
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);          /* v1 texcoord */
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);  /* v2 color    */
    AttrInfo_AddLoader(&g.attr_info, 3, GPU_FLOAT, 3);          /* v3 normal   */
    C3D_SetAttrInfo(&g.attr_info);
    s_attr_pos_elements = -4;
}

/* Invalidate the TexBind skip-cache for ONE texture id. Required whenever a
 * texture's storage is re-created at the same id (orphaning re-upload): the
 * C3D_Tex gets a NEW data pointer, but the per-unit skip-cache would say
 * "already bound" and the GPU would keep sampling the old block — which the
 * texture GC frees a frame later (manifested as textures turning black /
 * garbage moments after streaming-in). */
void nova_invalidate_tex_bind(GLuint tex_id) {
    for (int u = 0; u < 3; u++) {
        if (s_last_tex_bound[u] == tex_id) {
            s_last_tex_bound[u] = 0xFFFFFFFFu;
        }
    }
}

void nova_invalidate_state_cache(void) {
    g.state_dirty_bits = NOVA_DIRTY_ALL;
    g.vp_applied_valid = 0; /* force the next glViewport through the dedupe */
    s_last_tex_bound[0]       = 0xFFFFFFFFu;
    s_last_tex_bound[1]       = 0xFFFFFFFFu;
    s_last_tex_bound[2]       = 0xFFFFFFFFu;
    s_attr_pos_elements       = -1;
    s_buf_base                = (void *) -1;
    s_buf_stride              = -1;
    // Также метим грязным основной кэш матриц/фога/TEV — на всякий случай.
    g.matrices_dirty = 1;
    g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
    g.fog_dirty = 1;
    g.tev_dirty = 1;
    g.last_tex_state = -1;
    /* Force shader selector to re-evaluate next apply_gpu_state. -1 mismatches
     * both 0 (full) and 1 (basic), so the next call will C3D_BindProgram. */
    g.active_shader = -1;
    g.final_proj_cached_valid = 0;
    /* Rebuild the HW light env on the next lit draw (a context reset can wipe
     * GPU lighting registers). */
    g.light_dirty = 1;
    g.light_env_built = 0;
}

/* ========================================================================
 * Clipspace draw-call batcher
 * ========================================================================
 * Defers the C3D_DrawArrays in novaDrawClipspaceTris() and concatenates
 * consecutive draws whose entire draw-affecting GPU state is identical into a
 * single draw call. Correctness rule: two draws may merge ONLY if they would
 * produce pixel-identical output to drawing them separately in order. Merging
 * is order-preserving (verts appended in call order), so translucent/additive
 * blending stays correct.
 *
 * Design (see the spec this was built from):
 *  - The merge predicate is "apply_gpu_state() would push ZERO registers for
 *    the next draw": captured as a value snapshot (NovaBatchSig) of everything
 *    apply_gpu_state reads, PLUS guard dirty-flags for matrices/fog/tev (whose
 *    effect isn't cheap to snapshot — final_proj is 16 floats) and a stereo
 *    break (per-eye projection differs).
 *  - apply_gpu_state() runs ONCE per run (at run start), never per deferred
 *    draw, so the GPU holds exactly the run's state for its whole life. A new
 *    run flushes the old one FIRST (while the GPU still holds the old state)
 *    and then applies the new state.
 *  - Verts accumulate in a contiguous CPU scratch and are copied into the
 *    per-frame ring as ONE block at flush. (Appending straight into the ring
 *    would NOT work: linear_alloc_ring rounds every allocation up to 128 B, so
 *    consecutive appends leave alignment gaps that a single strided DrawArrays
 *    would read as garbage vertices.)
 */
#if NOVAGL_BATCH_CLIPSPACE

typedef struct {
    /* fog (per-pixel HW LUT is shader-independent, applies to clipspace) */
    int    fog_enabled; GLenum fog_mode;
    float  fog_density, fog_start, fog_end; float fog_color[4];
    /* writemask + depth */
    GLboolean cmask_r, cmask_g, cmask_b, cmask_a, depth_mask;
    int    depth_test; GLenum depth_func;
    /* alpha test (quantized ref, matches the apply_gpu_state cache) */
    int    alpha_test; GLenum alpha_func; u8 alpha_ref8;
    /* blend / logic op (packed colour, matches the cache) */
    int    blend_en; GLenum bsrc, bdst, bsrc_a, bdst_a, beq_rgb, beq_a;
    int    logic_en; GLenum logic_op; u32 blend_color_packed;
    /* cull */
    int    cull_en; GLenum cull_mode, front_face;
    /* scissor + render-target identity */
    int    scissor_en; GLint sx, sy; GLsizei sw, sh; int scissor_fbo_bit;
    void  *current_target; int bound_fbo;
    /* textures + per-bound-unit sampler params (eagerly baked into the C3D_Tex,
     * read by the GPU at draw time — THE main batching hazard) */
    GLuint bound_tex[3]; int tex_en[3];
    int    minf[3], magf[3], wraps[3], wrapt[3];
    /* explicit TEV programme (PD's primary path; CC constants live inside) */
    int    explicit_tev_count;
    NovaTevStageGL explicit_tev[NOVA_TEV_MAX_STAGES];
    /* legacy per-unit TEV — only meaningful when explicit_tev_count == 0 */
    GLint  te_mode[3];
    GLint  te_combine_rgb[3], te_src0_rgb[3], te_src1_rgb[3], te_src2_rgb[3];
    GLint  te_op0_rgb[3], te_op1_rgb[3], te_op2_rgb[3];
    GLint  te_combine_a[3], te_src0_a[3], te_src1_a[3], te_src2_a[3];
    GLint  te_op0_a[3], te_op1_a[3], te_op2_a[3];
    float  te_color[3][4];
    GLint  te_rgb_scale[3], te_alpha_scale[3];
} NovaBatchSig;

static struct {
    int          active;       /* a run is open */
    uint8_t     *scratch;      /* contiguous accumulated verts (28 B each) */
    int          scratch_cap;  /* bytes */
    int          total_verts;
    NovaBatchSig sig;          /* state shared by every vert in the run */
} s_batch;

static void batch_build_sig(NovaBatchSig *o) {
    memset(o, 0, sizeof(*o));   /* zero padding so memcmp is well-defined */

    o->fog_enabled = g.fog_enabled; o->fog_mode = g.fog_mode;
    o->fog_density = g.fog_density; o->fog_start = g.fog_start; o->fog_end = g.fog_end;
    o->fog_color[0] = g.fog_color[0]; o->fog_color[1] = g.fog_color[1];
    o->fog_color[2] = g.fog_color[2]; o->fog_color[3] = g.fog_color[3];

    o->cmask_r = g.color_mask_r; o->cmask_g = g.color_mask_g;
    o->cmask_b = g.color_mask_b; o->cmask_a = g.color_mask_a;
    o->depth_mask = g.depth_mask;
    o->depth_test = g.depth_test_enabled; o->depth_func = g.depth_func;

    o->alpha_test = g.alpha_test_enabled; o->alpha_func = g.alpha_func;
    o->alpha_ref8 = (u8)(clampf(g.alpha_ref, 0.0f, 1.0f) * 255.0f + 0.5f);

    o->blend_en = g.blend_enabled;
    o->bsrc = g.blend_src; o->bdst = g.blend_dst;
    o->bsrc_a = g.blend_src_alpha; o->bdst_a = g.blend_dst_alpha;
    o->beq_rgb = g.blend_eq_rgb; o->beq_a = g.blend_eq_alpha;
    o->logic_en = g.color_logic_op_enabled; o->logic_op = g.logic_op;
    o->blend_color_packed = pack_tev_color(g.blend_color);

    o->cull_en = g.cull_face_enabled; o->cull_mode = g.cull_face_mode;
    o->front_face = g.front_face;

    o->scissor_en = g.scissor_test_enabled;
    o->sx = g.scissor_x; o->sy = g.scissor_y; o->sw = g.scissor_w; o->sh = g.scissor_h;
    o->scissor_fbo_bit = (g.bound_fbo == 0);
    o->current_target = g.current_target; o->bound_fbo = (int) g.bound_fbo;

    for (int u = 0; u < 3; u++) {
        o->bound_tex[u] = g.bound_texture[u];
        o->tex_en[u] = g.texture_2d_enabled_unit[u];
        if (g.texture_2d_enabled_unit[u] && g.bound_texture[u] > 0 &&
            g.bound_texture[u] < NOVA_MAX_TEXTURES) {
            TexSlot *t = &g.textures[g.bound_texture[u]];
            o->minf[u] = t->min_filter; o->magf[u] = t->mag_filter;
            o->wraps[u] = t->wrap_s;    o->wrapt[u] = t->wrap_t;
        }
    }

    o->explicit_tev_count = g.explicit_tev_count;
    int n = g.explicit_tev_count;
    if (n < 0) n = 0; if (n > NOVA_TEV_MAX_STAGES) n = NOVA_TEV_MAX_STAGES;
    for (int i = 0; i < n; i++) o->explicit_tev[i] = g.explicit_tev_stages[i];

    if (g.explicit_tev_count == 0) {
        for (int u = 0; u < 3; u++) {
            o->te_mode[u]        = g.tex_env_mode[u];
            o->te_combine_rgb[u] = g.tex_env_combine_rgb[u];
            o->te_src0_rgb[u]    = g.tex_env_src0_rgb[u];
            o->te_src1_rgb[u]    = g.tex_env_src1_rgb[u];
            o->te_src2_rgb[u]    = g.tex_env_src2_rgb[u];
            o->te_op0_rgb[u]     = g.tex_env_operand0_rgb[u];
            o->te_op1_rgb[u]     = g.tex_env_operand1_rgb[u];
            o->te_op2_rgb[u]     = g.tex_env_operand2_rgb[u];
            o->te_combine_a[u]   = g.tex_env_combine_alpha[u];
            o->te_src0_a[u]      = g.tex_env_src0_alpha[u];
            o->te_src1_a[u]      = g.tex_env_src1_alpha[u];
            o->te_src2_a[u]      = g.tex_env_src2_alpha[u];
            o->te_op0_a[u]       = g.tex_env_operand0_alpha[u];
            o->te_op1_a[u]       = g.tex_env_operand1_alpha[u];
            o->te_op2_a[u]       = g.tex_env_operand2_alpha[u];
            o->te_color[u][0] = g.tex_env_color[u][0];
            o->te_color[u][1] = g.tex_env_color[u][1];
            o->te_color[u][2] = g.tex_env_color[u][2];
            o->te_color[u][3] = g.tex_env_color[u][3];
            o->te_rgb_scale[u]   = g.tex_env_rgb_scale[u];
            o->te_alpha_scale[u] = g.tex_env_alpha_scale[u];
        }
    }
}

void nova_batch_flush(void) {
    if (!s_batch.active || s_batch.total_verts <= 0) {
        s_batch.active = 0;
        s_batch.total_verts = 0;
        return;
    }
    int bytes = s_batch.total_verts * 28;
    /* Stage the whole run into the per-frame ring (aligned, handles wrap with
     * its own FrameSplit) and issue ONE draw with the run's current C3D state. */
    uint8_t *dst = (uint8_t *) linear_alloc_ring(g.client_array_buf,
                                                 &g.client_array_buf_offset,
                                                 bytes, g.client_array_buf_size);
    if (dst) {
        memcpy(dst, s_batch.scratch, (size_t) bytes);
        GSPGPU_FlushDataCache(dst, (u32) bytes);
        nova_setup_attr_info(4);
        nova_setup_buf_info(dst, 28);
        C3D_DrawArrays(GPU_TRIANGLES, 0, s_batch.total_verts);
    }
    s_batch.active = 0;
    s_batch.total_verts = 0;
}

void nova_batch_submit(const void *verts, int vertex_count) {
    const int bytes = vertex_count * 28;

    /* Capture the matrices/fog/tev dirty flags BEFORE apply_gpu_state clears
     * them — a pending real change to any of these breaks the run. */
    int break_dirty = g.matrices_dirty || g.proj_dirty || g.fog_dirty || g.tev_dirty;
    int stereo = (osGet3DSliderState() > 0.0f && g.stereo_depth != 0.0f);

    NovaBatchSig cand;
    batch_build_sig(&cand);

    int fits = s_batch.active &&
               ((s_batch.total_verts + vertex_count) * 28 <= g.client_array_buf_size);
    int can_merge = fits && !break_dirty && !stereo &&
                    cand.current_target == s_batch.sig.current_target &&
                    memcmp(&cand, &s_batch.sig, sizeof(NovaBatchSig)) == 0;

    if (!can_merge) {
        nova_batch_flush();   /* commit the old run (GPU still holds its state) */
        apply_gpu_state();    /* push THIS run's state, exactly once */
        s_batch.active = 1;
        s_batch.total_verts = 0;
        s_batch.sig = cand;
    }

    int need = (s_batch.total_verts + vertex_count) * 28;
    if (need > s_batch.scratch_cap) {
        int newcap = need + 64 * 1024;
        uint8_t *p = (uint8_t *) realloc(s_batch.scratch, (size_t) newcap);
        if (!p) {
            /* Couldn't grow scratch — draw what we have, then this chunk direct. The
             * run's state is already on the GPU (applied above, or unchanged on
             * the merge path), so a direct ring draw is correct. */
            nova_batch_flush();
            uint8_t *dst = (uint8_t *) linear_alloc_ring(g.client_array_buf,
                                                         &g.client_array_buf_offset,
                                                         bytes, g.client_array_buf_size);
            if (dst) {
                memcpy(dst, verts, (size_t) bytes);
                GSPGPU_FlushDataCache(dst, (u32) bytes);
                nova_setup_attr_info(4);
                nova_setup_buf_info(dst, 28);
                C3D_DrawArrays(GPU_TRIANGLES, 0, vertex_count);
            }
            s_batch.active = 0;
            s_batch.total_verts = 0;
            return;
        }
        s_batch.scratch = p;
        s_batch.scratch_cap = newcap;
    }

    memcpy(s_batch.scratch + s_batch.total_verts * 28, verts, (size_t) bytes);
    s_batch.total_verts += vertex_count;
}

#else  /* !NOVAGL_BATCH_CLIPSPACE — batcher compiled out, flush is a no-op */
void nova_batch_flush(void) { }
void nova_batch_submit(const void *verts, int vertex_count) { (void) verts; (void) vertex_count; }
#endif

void cleanup_vbo_stream(void) {
#if 0
#ifdef NOVA_VBO_STREAM
    if (g.bound_array_buffer) {
        VBOSlot *slot = &g.vbos[g.bound_array_buffer];
        if (slot->is_stream && slot->data) {
            free(slot->data);
            slot->data = NULL;
            slot->allocated = 0;
            slot->size = 0;
            slot->capacity = 0;
        }
    }

    // don't forget to clear index buffer
    if (g.bound_element_array_buffer) {
        VBOSlot *slot = &g.vbos[g.bound_element_array_buffer];
        if (slot->is_stream && slot->data) {
            free(slot->data);
            slot->data = NULL;
            slot->allocated = 0;
            slot->size = 0;
            slot->capacity = 0;
        }
    }
#endif
#endif
}

void draw_emulated_quads(int count) {
    int num_quads = count / 4;

    if (!g.static_quad_indices) return;

    /* The shared static_quad_indices buffer is 0-based (it indexes vertices
     * 0..static_quad_count*4-1). Re-running it for a second batch would draw the
     * FIRST N quads again, not the next N (there's no base-vertex offset on
     * PICA). So a single draw can cover at most static_quad_count quads; clamp
     * and warn once rather than silently emitting duplicated geometry. A caller
     * needing more should split the GL_QUADS draw into <= static_quad_count*4
     * vertex chunks. */
    if (num_quads > g.static_quad_count) {
        static int warned = 0;
        if (!warned) {
            printf("[Nova]: GL_QUADS batch of %d quads exceeds the %d-quad index buffer; "
                   "drawing the first %d (split the draw to render the rest).\n",
                   num_quads, g.static_quad_count, g.static_quad_count);
            warned = 1;
        }
        num_quads = g.static_quad_count;
    }
    C3D_DrawElements(GPU_TRIANGLES, num_quads * 6, C3D_UNSIGNED_SHORT, g.static_quad_indices);
}

void nova_hardware_swizzle(C3D_Tex *tex, const void *linear_pixels, int width, int height, GPU_TEXCOLOR format) {
    if (!linear_pixels || !tex->data) return;

    u32 transfer_flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    u32 in_fmt = 0, out_fmt = 0;
    switch (format) {
        case GPU_RGBA8: in_fmt = GX_TRANSFER_FMT_RGBA8;
            out_fmt = GX_TRANSFER_FMT_RGBA8;
            break;
        case GPU_RGB8: in_fmt = GX_TRANSFER_FMT_RGB8;
            out_fmt = GX_TRANSFER_FMT_RGB8;
            break;
        case GPU_RGB565: in_fmt = GX_TRANSFER_FMT_RGB565;
            out_fmt = GX_TRANSFER_FMT_RGB565;
            break;
        case GPU_RGBA5551: in_fmt = GX_TRANSFER_FMT_RGB5A1;
            out_fmt = GX_TRANSFER_FMT_RGB5A1;
            break;
        case GPU_RGBA4: in_fmt = GX_TRANSFER_FMT_RGBA4;
            out_fmt = GX_TRANSFER_FMT_RGBA4;
            break;
        default: printf("Unsupported format: %x", format);
            return;
    }

    transfer_flags |= GX_TRANSFER_IN_FORMAT(in_fmt) | GX_TRANSFER_OUT_FORMAT(out_fmt);

    GSPGPU_FlushDataCache(linear_pixels, width * height * gpu_texfmt_bpp(format));

    C3D_SyncDisplayTransfer((u32 *) linear_pixels, GX_BUFFER_DIM(width, height),
                            (u32 *) tex->data, GX_BUFFER_DIM(tex->width, tex->height), transfer_flags);
}

/* =========================================================================
 * PICA200 Hardware Profiler Implementation
 * ========================================================================= */
#define PICA_REG_PSC_BSYLOG_CTRL   0x400064
#define PICA_REG_PSC_BSYLOG_READ   0x40006C
#define PICA_REG_PSC_MEM_CNT_BASE  0x400078

void novaBeginProfiling(void) {
    uint32_t dummy[4];
    for (int i = 0; i < 4; i++) {
        GSPGPU_ReadHWRegs(PICA_REG_PSC_BSYLOG_READ, dummy, 4 * 4);
    }
    uint32_t ctrl = 0;
    GSPGPU_ReadHWRegs(PICA_REG_PSC_BSYLOG_CTRL, &ctrl, 4);
    ctrl = (ctrl & ~0xFF) | 1;
    GSPGPU_WriteHWRegs(PICA_REG_PSC_BSYLOG_CTRL, &ctrl, 4);
}

void novaEndProfiling(void) {
    uint32_t ctrl = 0;
    GSPGPU_ReadHWRegs(PICA_REG_PSC_BSYLOG_CTRL, &ctrl, 4);
    ctrl = (ctrl & ~0xFF) | 0;
    GSPGPU_WriteHWRegs(PICA_REG_PSC_BSYLOG_CTRL, &ctrl, 4);
}

void novaGetProfileStats(NovaProfileStats *out_stats) {
    if (!out_stats) return;

    uint32_t bsy[4] = {0};
    GSPGPU_ReadHWRegs(PICA_REG_PSC_BSYLOG_READ, bsy, 4 * 4);

    out_stats->vertex_processor   = bsy[0] & 0xFFFF;
    out_stats->command_interface  = (bsy[0] >> 16) & 0xFFFF;
    out_stats->triangle_interface = bsy[1] & 0xFFFF;
    out_stats->triangle_setup     = (bsy[1] >> 16) & 0xFFFF;
    out_stats->light_reflection   = bsy[2] & 0xFFFF;
    out_stats->texture_fetch      = (bsy[2] >> 16) & 0xFFFF;
    out_stats->color_updater      = bsy[3] & 0xFFFF;
    out_stats->texture_blender    = (bsy[3] >> 16) & 0xFFFF;

    uint32_t mem[8] = {0};
    for (int i = 0; i < 8; i++) {
        GSPGPU_ReadHWRegs(PICA_REG_PSC_MEM_CNT_BASE + (i * 4), &mem[i], 4);
    }

    out_stats->mem_vram_0_read    = mem[0];
    out_stats->mem_vram_0_write   = mem[1];
    out_stats->mem_vram_1_read    = mem[2];
    out_stats->mem_vram_1_write   = mem[3];
    out_stats->mem_p3d_geo_read   = mem[4];
    out_stats->mem_p3d_tex_read   = mem[5];
    out_stats->mem_p3d_cu_0_read  = mem[6];
    out_stats->mem_p3d_cu_0_write = mem[7];
}
