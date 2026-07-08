//
// created by efimandreev0 on 05.04.2026.
//

#include <stdio.h>

#include "NovaGL.h"
#include "utils.h"

#define NOVA_VBO_STORAGE_RAW 0
#define NOVA_VBO_STORAGE_PACKED_PTC 1
#define NOVA_VBO_PTC_RAW_STRIDE 24
#define NOVA_VBO_PTC_PACKED_STRIDE 14
#define NOVA_VBO_PTC_PACK_THRESHOLD (NOVA_VBO_PTC_RAW_STRIDE * 128)

static float half_bits_to_float(uint16_t value) {
    uint32_t sign = ((uint32_t) value & 0x8000u) << 16;
    uint32_t exponent = ((uint32_t) value >> 10) & 0x1Fu;
    uint32_t mantissa = (uint32_t) value & 0x03FFu;
    uint32_t bits;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x03FFu;
            bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }

    union {
        uint32_t u;
        float f;
    } conv;
    conv.u = bits;
    return conv.f;
}

static void free_vbo_storage(VBOSlot *slot) {
    if (slot->allocated && slot->data) {
        /* Deferred: this frame's queued draws may still read the block
         * (glDeleteBuffers right after a draw is common in streaming engines).
         * Freed by nova_vbo_gc_collect K frames later, like textures/rings. */
        nova_vbo_defer_free(slot->data);
    }
    slot->data = NULL;
    slot->allocated = 0;
    slot->capacity = 0;
    slot->size = 0;
    slot->storage_kind = NOVA_VBO_STORAGE_RAW;
    slot->storage_stride = 0;
}

// half-float PTC packing never got turned on (this always return 0). pack code
// was dead so i droped it, unpack stay so old packed slots still readable.
// to turn on later: put a real heuristic here, glBufferData already check the flag
static int can_pack_ptc_vbo(GLsizeiptr size, const GLvoid *data, GLenum usage) {
    (void) size; (void) data; (void) usage;
    return 0;
}

int vbo_is_packed_ptc(const VBOSlot *slot) {
    return slot &&
           slot->allocated &&
           slot->data &&
           slot->storage_kind == NOVA_VBO_STORAGE_PACKED_PTC &&
           slot->storage_stride == NOVA_VBO_PTC_PACKED_STRIDE &&
           slot->size >= NOVA_VBO_PTC_RAW_STRIDE;
}

void vbo_decode_packed_ptc_vertex(const VBOSlot *slot, int vertex_index, uint8_t *out_vertex) {
    const uint8_t *src = (const uint8_t *) slot->data + vertex_index * NOVA_VBO_PTC_PACKED_STRIDE;
    uint16_t hx, hy, hz, hu, hv;
    float x, y, z, u, v;

    memcpy(&hx, src + 0, sizeof(uint16_t));
    memcpy(&hy, src + 2, sizeof(uint16_t));
    memcpy(&hz, src + 4, sizeof(uint16_t));
    memcpy(&hu, src + 6, sizeof(uint16_t));
    memcpy(&hv, src + 8, sizeof(uint16_t));

    x = half_bits_to_float(hx);
    y = half_bits_to_float(hy);
    z = half_bits_to_float(hz);
    u = half_bits_to_float(hu);
    v = half_bits_to_float(hv);

    memcpy(out_vertex + 0, &x, sizeof(float));
    memcpy(out_vertex + 4, &y, sizeof(float));
    memcpy(out_vertex + 8, &z, sizeof(float));
    memcpy(out_vertex + 12, &u, sizeof(float));
    memcpy(out_vertex + 16, &v, sizeof(float));
    memcpy(out_vertex + 20, src + 10, 4);
}

void vbo_decode_packed_ptc_span(const VBOSlot *slot, int first_vertex, int vertex_count, uint8_t *dst) {
    for (int i = 0; i < vertex_count; i++) {
        vbo_decode_packed_ptc_vertex(slot, first_vertex + i, dst + i * NOVA_VBO_PTC_RAW_STRIDE);
    }
}

void vbo_convert_slot_to_raw(VBOSlot *slot) {
    if (!vbo_is_packed_ptc(slot)) {
        return;
    }

    void *decoded = linearAlloc((size_t) slot->size);
    if (!decoded) {
        gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    vbo_decode_packed_ptc_span(slot, 0, slot->size / NOVA_VBO_PTC_RAW_STRIDE, (uint8_t *) decoded);
    nova_vbo_defer_free(slot->data); /* GPU may still read the packed block */
    slot->data = decoded;
    slot->capacity = slot->size;
    slot->storage_kind = NOVA_VBO_STORAGE_RAW;
    slot->storage_stride = 0;
    slot->allocated = 1;
    GSPGPU_FlushDataCache(slot->data, (u32) slot->size);
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    if (n < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!buffers) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_VBOS && g.vbos[id].in_use) {
            id++;
        }
        if (id == NOVA_MAX_VBOS) {
            gl_set_error(GL_OUT_OF_MEMORY);
            buffers[i] = 0;
            break;
        }

        memset(&g.vbos[id], 0, sizeof(g.vbos[id]));
        g.vbos[id].in_use = 1;
        buffers[i] = id;

        /* High-water-mark breadcrumb (change 4): NOVA_MAX_VBOS was shrunk to
         * reclaim .bss. Warn ONCE if the live-id high-water passes 75% of the
         * cap so an over-small table is noticed before allocations start
         * failing. `id` is the largest slot index handed out so far this run. */
        {
            static int s_vbo_hiwater = 0;
            static int s_warned_75 = 0;
            if ((int) id > s_vbo_hiwater) s_vbo_hiwater = (int) id;
            if (!s_warned_75 && s_vbo_hiwater > (NOVA_MAX_VBOS * 3) / 4) {
                printf("[NOVA] VBO high-water %d/%d (>75%% of NOVA_MAX_VBOS) — table may be too small.\n",
                       s_vbo_hiwater, NOVA_MAX_VBOS);
                s_warned_75 = 1;
            }
        }
    }
}

/* GLES 1.1: true only for a name returned by glGenBuffers and not since
 * deleted. NovaGL marks a slot in_use at gen time, so a reserved-but-unbound
 * name still counts as a buffer object. */
GLboolean glIsBuffer(GLuint buffer) {
    if (buffer > 0 && (int) buffer < NOVA_MAX_VBOS && g.vbos[buffer].in_use) return GL_TRUE;
    return GL_FALSE;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    if (n < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (!buffers) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id > 0 && (int) id < NOVA_MAX_VBOS && g.vbos[id].in_use) {
            VBOSlot *slot = &g.vbos[id];
            free_vbo_storage(slot);
            slot->in_use = 0;

            if (g.bound_array_buffer == id) g.bound_array_buffer = 0;
            if (g.bound_element_array_buffer == id) g.bound_element_array_buffer = 0;
        }
    }
}


void glBindBuffer(GLenum target, GLuint buffer) {
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        gl_set_error(GL_INVALID_ENUM); /* spec: binding unchanged */
        return;
    }
    /* GLES 1.1 / compat: binding an unused non-zero name CREATES the buffer
     * object (so glGenBuffers, glIsBuffer and glBufferData all agree, and
     * glGenBuffers can no longer hand out a name already in use here). */
    if (buffer != 0 && buffer < NOVA_MAX_VBOS && !g.vbos[buffer].in_use) {
        memset(&g.vbos[buffer], 0, sizeof(g.vbos[buffer]));
        g.vbos[buffer].in_use = 1;
    }
    if (target == GL_ARRAY_BUFFER) g.bound_array_buffer = buffer;
    else g.bound_element_array_buffer = buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    else {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (size < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    /* Spec: usage must be one of the nine STREAM/STATIC/DYNAMIC _DRAW/_READ/_COPY
     * tokens; an unknown value is GL_INVALID_ENUM. NovaGL only distinguishes
     * stream vs static, but it must still reject garbage. */
    switch (usage) {
        case GL_STREAM_DRAW: case GL_STATIC_DRAW: case GL_DYNAMIC_DRAW:
        case 0x88E1 /*STREAM_READ*/: case 0x88E2 /*STREAM_COPY*/:
        case 0x88E5 /*STATIC_READ*/: case 0x88E6 /*STATIC_COPY*/:
        case 0x88E9 /*DYNAMIC_READ*/: case 0x88EA /*DYNAMIC_COPY*/:
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    if (id == 0) {
        /* Spec: no buffer object bound to target. */
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (id >= NOVA_MAX_VBOS) {
        /* Id outside NovaGL's table capacity — closest spec error. */
        gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    VBOSlot *slot = &g.vbos[id];
    /* Spec: it is GL_INVALID_OPERATION to (re)specify data for a mapped buffer. */
    if (slot->mapped) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    int pack_ptc = (target == GL_ARRAY_BUFFER) && can_pack_ptc_vbo(size, data, usage);
    int desired_kind = pack_ptc ? NOVA_VBO_STORAGE_PACKED_PTC : NOVA_VBO_STORAGE_RAW;
    int desired_stride = pack_ptc ? NOVA_VBO_PTC_PACKED_STRIDE : 0;
    int desired_bytes = pack_ptc ? ((size / NOVA_VBO_PTC_RAW_STRIDE) * NOVA_VBO_PTC_PACKED_STRIDE) : (int) size;

    if (slot->allocated &&
        (slot->storage_kind != desired_kind || slot->storage_stride != desired_stride)) {
        free_vbo_storage(slot);
    }

    int is_stream = (usage == GL_STREAM_DRAW || usage == GL_DYNAMIC_DRAW);
    int reuse_existing = slot->allocated &&
                         slot->capacity >= desired_bytes &&
                         // for STREAM/DYNAMIC dont shrink, app grow back next
                         // frame anyway and we just thrash linearAlloc
                         (is_stream || slot->capacity <= desired_bytes * 4);

    if (!reuse_existing) {
        /* Round capacity up to the next power of two for ALL usages (not just
         * streaming). The infinite-world chunk renderer re-tessellates a sliding
         * window of static VBOs constantly, each time at a slightly different
         * exact size; allocating those exact sizes shreds the linear heap into
         * unusable fragments until linearAlloc fails with plenty of free bytes
         * left. Quantizing to powers of two makes nearby sizes land on the same
         * capacity, so the reuse path above takes over and we stop reallocating.
         * The non-stream reuse check still shrinks a buffer that becomes >4x
         * oversized, so this does not ratchet unbounded. */
        int new_capacity = desired_bytes;
        {
            int p = 256;
            while (p < new_capacity) p <<= 1;
            new_capacity = p;
        }

        // Alloc-before-free (safe-fallback rework): a mid-frame linear
        // exhaustion must NOT leave the slot empty — an empty VBO wedges the
        // draw path (all its geometry gets routed through the client-array
        // gather ring, which then can't grow under the same exhaustion and
        // hangs). So keep old_buf alive across the new linearAlloc:
        //   1. Try linearAlloc(new_capacity) while old_buf is still held.
        //   2. If that fails AND we have an old_buf, free old_buf and retry
        //      ONCE — this restores the original anti-fragmentation behaviour
        //      (releasing the old block can defragment enough linear space for
        //      the new one) as a fallback only.
        //   3. If it STILL fails: DO NOT zero the slot. Leave the previous
        //      valid data/capacity/allocated/size intact so the VBO keeps its
        //      last (stale but drawable) geometry, set GL_OUT_OF_MEMORY, return.
        // A fresh VBO (no old_buf) that fails stays data=NULL/allocated=0 — the
        // draw path skips it (see nova_draw_internal / ms_resolve_attr guards).
        void *old_buf = slot->data;

        void *new_buf = linearAlloc((size_t) new_capacity);
        if (!new_buf && old_buf) {
            // Retry once after releasing the old block (anti-fragmentation
            // fallback). From here old_buf is gone; a second failure leaves the
            // slot EMPTY (data=NULL/allocated=0) — but that only happens when we
            // just freed a block big enough that the retry should normally
            // succeed. The draw-path guards still cover the empty case.
            linearFree(old_buf);
            old_buf = NULL;
            slot->data = NULL;
            slot->allocated = 0;
            slot->capacity = 0;
            new_buf = linearAlloc((size_t) new_capacity);
        }
        if (!new_buf) {
            printf("[NOVA] glBufferData: linearAlloc(%d) FAILED (linearSpaceFree=%u). "
                   "VBO id=%u keeps %s.\n",
                   new_capacity, (unsigned) linearSpaceFree(), (unsigned) id,
                   old_buf ? "STALE geometry" : "empty (fresh VBO)");
            gl_set_error(GL_OUT_OF_MEMORY);
            // old_buf != NULL here means we never freed it: slot->data /
            // capacity / allocated / size are all still the previous valid
            // values, so the VBO stays drawable with stale contents. Return
            // WITHOUT touching slot->size below.
            return;
        }

        // New alloc succeeded — release the old block DEFERRED: draws issued
        // earlier this frame may still read it (respecify-after-draw), so it
        // parks in the frame-slot GC instead of going back to linearAlloc now.
        // (The retry path above keeps its immediate linearFree on purpose —
        // it exists to reclaim space RIGHT NOW under linear exhaustion.)
        if (old_buf)
            nova_vbo_defer_free(old_buf);

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    slot->size = size;
    slot->is_stream = is_stream;
    slot->usage = usage;
    slot->storage_kind = desired_kind;
    slot->storage_stride = (uint8_t) desired_stride;

    if (data && slot->data) {
        memcpy(slot->data, data, size);
        // flush only size, not the rounded-up capacity, else we pay double for
        // big stream VBO for no reason
        GSPGPU_FlushDataCache(slot->data, (u32) size);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    else {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    if (offset < 0 || size < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (id == 0) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (id >= NOVA_MAX_VBOS) return;
    if (size == 0 || !data) return;

    VBOSlot *slot = &g.vbos[id];
    /* Spec: GL_INVALID_OPERATION to update a currently-mapped buffer. */
    if (slot->mapped) {
        gl_set_error(GL_INVALID_OPERATION);
        return;
    }

#ifdef NOVAGL_STRICT_BUFFERSUBDATA
    /* Strict spec behaviour (opt-in): writing past the buffer's data store is
     * GL_INVALID_VALUE with no effect. The DEFAULT keeps the lenient auto-grow
     * below — several ports issue a glBufferSubData slightly past the current
     * size and rely on it extending the store rather than failing (a failed
     * sub-update would leave stale geometry). */
    if (!slot->allocated || (GLsizeiptr)(offset + size) > slot->size) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
#endif

    if (vbo_is_packed_ptc(slot)) {
        vbo_convert_slot_to_raw(slot);
        if (vbo_is_packed_ptc(slot)) {
            return;
        }
    }

    int required = offset + size;

    if (!slot->allocated || slot->capacity < required) {
        /* Growth must make progress from ANY starting point. The old seed of
         * 1 with a *3/2 integer step locked up forever: (1*3)/2 == 1, so a
         * sub-update against a VBO whose glBufferData had failed its
         * linearAlloc (capacity 0 — exactly the low-linear situation) spun
         * the render traversal for good. This WAS the floating in-game
         * renderTrav hang (RSP dump: main thread parked in this loop from
         * osg::GLBufferObject::compileBuffer). */
        int new_capacity = slot->capacity > 0 ? slot->capacity : required;

        while (new_capacity < required)
            new_capacity += new_capacity / 2 + 1;

        void *new_buf = linearAlloc((size_t) new_capacity);
        if (!new_buf) {
            gl_set_error(GL_OUT_OF_MEMORY);
            return;
        }

        if (slot->data && slot->capacity > 0) {
            memcpy(new_buf, slot->data, (size_t) slot->capacity);
            nova_vbo_defer_free(slot->data); /* may still be read by this frame's draws */
        }

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    memcpy((uint8_t *) slot->data + offset, data, size);

    if (required > slot->size)
        slot->size = required;

    // flush only the range we wrote. ARM11 cache line is 32 byte so round to it.
    // streaming app call this many times a frame, full flush kill cpu on big VBO
    uintptr_t start = (uintptr_t) slot->data + (uintptr_t) offset;
    uintptr_t end = start + (uintptr_t) size;
    start &= ~(uintptr_t) 31;
    end = (end + 31) & ~(uintptr_t) 31;
    GSPGPU_FlushDataCache((const void *) start, (u32) (end - start));
}

void glReadBuffer(GLenum mode) {
    // one color buffer per target on PICA, so nothing to pick. warn one time,
    // some engines spam this every frame
    (void) mode;
    static int warned = 0;
    if (!warned) {
        printf("[Nova]: glReadBuffer not supported\n");
        warned = 1;
    }
}

// ---- buffer mapping ----
// every VBO live in a linearAlloc block so "map" is just give back slot->data.
// GPU read VBO past the cpu cache, so on unmap we must flush the range or GPU
// see old garbage. packed-PTC cant be mapped (wrong layout) so convert to raw first.

/* Resolve target -> bound id, distinguishing "bad target" from "nothing bound"
 * so the map entry points can raise the correct GL error. Returns the slot or
 * NULL, writing *err with the error to raise (GL_NO_ERROR if the slot is fine). */
static VBOSlot *map_resolve_slot_err(GLenum target, GLenum *err) {
    *err = GL_NO_ERROR;
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    else { *err = GL_INVALID_ENUM; return NULL; }
    if (id == 0 || id >= NOVA_MAX_VBOS) { *err = GL_INVALID_OPERATION; return NULL; }
    VBOSlot *slot = &g.vbos[id];
    if (!slot->in_use || !slot->allocated || !slot->data) { *err = GL_INVALID_OPERATION; return NULL; }
    return slot;
}

void *glMapBuffer(GLenum target, GLenum access) {
    if (access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE) {
        gl_set_error(GL_INVALID_ENUM);
        return NULL;
    }
    GLenum err; VBOSlot *slot = map_resolve_slot_err(target, &err);
    if (!slot) { gl_set_error(err); return NULL; }
    if (slot->mapped) { gl_set_error(GL_INVALID_OPERATION); return NULL; } /* already mapped */
    if (slot->storage_kind != NOVA_VBO_STORAGE_RAW) {
        vbo_convert_slot_to_raw(slot);
    }
    slot->mapped = 1;
    return slot->data;
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    (void) access; // INVALIDATE_/UNSYNCHRONIZED_ we just ignore, no discard buffer.
    GLenum err; VBOSlot *slot = map_resolve_slot_err(target, &err);
    if (!slot) { gl_set_error(err); return NULL; }
    if (slot->mapped) { gl_set_error(GL_INVALID_OPERATION); return NULL; }
    /* offset>=0, length>=0, and the range must fit the allocated store. Check
     * against CAPACITY (not the logical size) so ports that map a region beyond
     * the last glBufferData size — common with streaming ring buffers — still
     * get a valid pointer rather than a spurious GL_INVALID_VALUE. */
    if (offset < 0 || length < 0 || offset + length > slot->capacity) {
        gl_set_error(GL_INVALID_VALUE);
        return NULL;
    }
    if (slot->storage_kind != NOVA_VBO_STORAGE_RAW) {
        vbo_convert_slot_to_raw(slot);
    }
    slot->mapped = 1;
    return (uint8_t *) slot->data + offset;
}

GLboolean glUnmapBuffer(GLenum target) {
    GLenum err; VBOSlot *slot = map_resolve_slot_err(target, &err);
    if (!slot) { gl_set_error(err); return GL_FALSE; }
    if (!slot->mapped) { gl_set_error(GL_INVALID_OPERATION); return GL_FALSE; } /* not mapped */
    slot->mapped = 0;
    // just flush the whole buffer, we dont track the mapped range. cheap enough.
    GSPGPU_FlushDataCache(slot->data, (u32) slot->size);
    return GL_TRUE;
}