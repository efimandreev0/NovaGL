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
        linearFree(slot->data);
    }
    slot->data = NULL;
    slot->allocated = 0;
    slot->capacity = 0;
    slot->size = 0;
    slot->storage_kind = NOVA_VBO_STORAGE_RAW;
    slot->storage_stride = 0;
}

/* The half-float PTC packing path was wired up but never enabled at the API
 * surface — can_pack_ptc_vbo() always returned 0, so pack_ptc_vertex() and
 * float_to_half_bits() were dead code. Removed to slim the binary; the
 * unpack path stays (vbo_decode_packed_ptc_vertex) so any pre-existing
 * packed slots from a prior build can still be read back when re-bound.
 *
 * To re-enable later: add a real heuristic here (e.g. "raw stride == 24 and
 * size > some threshold"), reintroduce float_to_half_bits + pack_ptc_vertex,
 * and the glBufferData path below already honours the pack_ptc flag. */
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
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    vbo_decode_packed_ptc_span(slot, 0, slot->size / NOVA_VBO_PTC_RAW_STRIDE, (uint8_t *) decoded);
    linearFree(slot->data);
    slot->data = decoded;
    slot->capacity = slot->size;
    slot->storage_kind = NOVA_VBO_STORAGE_RAW;
    slot->storage_stride = 0;
    slot->allocated = 1;
    GSPGPU_FlushDataCache(slot->data, (u32) slot->size);
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    if (n < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (!buffers) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_VBOS && g.vbos[id].in_use) {
            id++;
        }
        if (id == NOVA_MAX_VBOS) {
            g.last_error = GL_OUT_OF_MEMORY;
            buffers[i] = 0;
            break;
        }

        memset(&g.vbos[id], 0, sizeof(g.vbos[id]));
        g.vbos[id].in_use = 1;
        buffers[i] = id;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    if (n < 0) {
        g.last_error = GL_INVALID_VALUE;
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
    if (target == GL_ARRAY_BUFFER) g.bound_array_buffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) g.bound_element_array_buffer = buffer;
    else g.last_error = GL_INVALID_ENUM; /* spec: binding unchanged */
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    else {
        g.last_error = GL_INVALID_ENUM;
        return;
    }

    if (size < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (id == 0) {
        /* Spec: no buffer object bound to target. */
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    if (id >= NOVA_MAX_VBOS) return;

    VBOSlot *slot = &g.vbos[id];
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
                         /* For STREAM/DYNAMIC, never shrink the storage even
                          * if the new size is much smaller — we'd just be
                          * thrashing linearAlloc next time the app grows back
                          * up. Vita's orphan-trick same idea. */
                         (is_stream || slot->capacity <= desired_bytes * 4);

    if (!reuse_existing) {
        int new_capacity = desired_bytes;
        /* For streaming VBOs, round capacity up to next power of two so we
         * stop reallocating on each minor size jiggle. */
        if (is_stream) {
            int p = 256;
            while (p < new_capacity) p <<= 1;
            new_capacity = p;
        }

        void *new_buf = linearAlloc((size_t) new_capacity);
        if (!new_buf) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        if (slot->data) linearFree(slot->data);
        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    slot->size = size;
    slot->is_stream = is_stream;
    slot->storage_kind = desired_kind;
    slot->storage_stride = (uint8_t) desired_stride;

    if (data && slot->data) {
        /* pack_ptc path removed — see can_pack_ptc_vbo comment above. */
        memcpy(slot->data, data, size);
        /* Flush only the bytes we actually wrote (size, not the rounded-up
         * capacity). Big STREAM VBOs grew to capacity=2*size with the
         * power-of-two rule above; flushing the whole capacity would double
         * the cache-flush cost for nothing. */
        GSPGPU_FlushDataCache(slot->data, (u32) size);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    else {
        g.last_error = GL_INVALID_ENUM;
        return;
    }

    if (offset < 0 || size < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (id == 0) {
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    if (id >= NOVA_MAX_VBOS) return;
    if (size == 0 || !data) return;

    VBOSlot *slot = &g.vbos[id];

#ifdef NOVAGL_STRICT_BUFFERSUBDATA
    /* Spec behaviour: writing past the buffer's data store is
     * GL_INVALID_VALUE with no effect. Default build keeps the lenient
     * auto-grow below because several ports rely on it. */
    if (!slot->allocated || offset + size > slot->size) {
        g.last_error = GL_INVALID_VALUE;
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
        int new_capacity = slot->capacity ? slot->capacity : 1;

        while (new_capacity < required)
            new_capacity = (new_capacity * 3) / 2;

        void *new_buf = linearAlloc((size_t) new_capacity);
        if (!new_buf) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        if (slot->data && slot->capacity > 0) {
            memcpy(new_buf, slot->data, (size_t) slot->capacity);
            linearFree(slot->data);
        }

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    memcpy((uint8_t *) slot->data + offset, data, size);

    if (required > slot->size)
        slot->size = required;

    /* Range flush: streaming apps call glBufferSubData many times per frame
     * with small chunks. Flushing the whole buffer dominates CPU for big VBOs.
     * Cache lines on ARM11 are 32 bytes, so widen to that boundary. */
    uintptr_t start = (uintptr_t) slot->data + (uintptr_t) offset;
    uintptr_t end = start + (uintptr_t) size;
    start &= ~(uintptr_t) 31;
    end = (end + 31) & ~(uintptr_t) 31;
    GSPGPU_FlushDataCache((const void *) start, (u32) (end - start));
}

void glReadBuffer(int x) {
    /* Not supported (single color buffer per target on PICA). Print once,
     * not on every call — some engines call this per-frame.
     * TODO: change the signature to (GLenum mode) here AND in NovaGL.h. */
    (void) x;
    static int warned = 0;
    if (!warned) {
        printf("[Nova]: glReadBuffer not supported\n");
        warned = 1;
    }
}

/* ------------------------------------------------------------------------
 * Buffer mapping
 * ------------------------------------------------------------------------
 * NovaGL stores every VBO in a linearAlloc'd block, so "mapping" is just
 * handing out slot->data. The kicker is the cache: the GPU reads VBOs out of
 * physical memory bypassing the CPU's data cache, so if the caller writes
 * through the mapped pointer we have to flush the cache range on unmap.
 *
 * Packed-PTC VBOs can't be safely mapped (the bytes aren't in the layout the
 * caller expects). We convert them back to raw before handing out the pointer,
 * which costs a memcpy + decode but is a one-time hit per map. */

static VBOSlot *map_resolve_slot(GLenum target) {
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;
    if (id == 0 || id >= NOVA_MAX_VBOS) return NULL;
    VBOSlot *slot = &g.vbos[id];
    if (!slot->in_use || !slot->allocated || !slot->data) return NULL;
    return slot;
}

void *glMapBuffer(GLenum target, GLenum access) {
    (void) access; // we always expose a writable pointer regardless of mode
    VBOSlot *slot = map_resolve_slot(target);
    if (!slot) return NULL;
    if (slot->storage_kind != NOVA_VBO_STORAGE_RAW) {
        vbo_convert_slot_to_raw(slot);
    }
    return slot->data;
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    (void) length;
    (void) access; // INVALIDATE_* / UNSYNCHRONIZED_* are accepted but ignored —
                   // we don't synthesize a discard buffer; callers that pass
                   // GL_MAP_INVALIDATE_BUFFER_BIT get the same storage back
                   // because Arx writes the whole buffer before drawing anyway.
    VBOSlot *slot = map_resolve_slot(target);
    if (!slot) return NULL;
    if (offset < 0 || offset > slot->capacity) return NULL;
    if (slot->storage_kind != NOVA_VBO_STORAGE_RAW) {
        vbo_convert_slot_to_raw(slot);
    }
    return (uint8_t *) slot->data + offset;
}

GLboolean glUnmapBuffer(GLenum target) {
    VBOSlot *slot = map_resolve_slot(target);
    if (!slot) return GL_FALSE;
    // Conservatively flush the full buffer rather than tracking the mapped
    // range — Arx maps tiny chunks per frame, so flushing the whole VBO is
    // still cheap on the cache controller.
    GSPGPU_FlushDataCache(slot->data, (u32) slot->size);
    return GL_TRUE;
}