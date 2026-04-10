//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

#define NOVA_VBO_STORAGE_RAW 0
#define NOVA_VBO_STORAGE_PACKED_PTC 1
#define NOVA_VBO_PTC_RAW_STRIDE 24
#define NOVA_VBO_PTC_PACKED_STRIDE 14
#define NOVA_VBO_PTC_PACK_THRESHOLD (NOVA_VBO_PTC_RAW_STRIDE * 128)

static uint16_t float_to_half_bits(float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;

    uint32_t sign = (conv.u >> 16) & 0x8000u;
    uint32_t exponent = (conv.u >> 23) & 0xFFu;
    uint32_t mantissa = conv.u & 0x7FFFFFu;

    if (exponent == 0xFFu) {
        if (mantissa) {
            return (uint16_t)(sign | 0x7E00u);
        }
        return (uint16_t)(sign | 0x7C00u);
    }

    int new_exp = (int)exponent - 127 + 15;
    if (new_exp >= 0x1F) {
        return (uint16_t)(sign | 0x7C00u);
    }

    if (new_exp <= 0) {
        if (new_exp < -10) {
            return (uint16_t)sign;
        }
        mantissa |= 0x800000u;
        uint32_t shifted = mantissa >> (uint32_t)(14 - new_exp);
        if ((mantissa >> (uint32_t)(13 - new_exp)) & 1u) {
            shifted++;
        }
        return (uint16_t)(sign | shifted);
    }

    uint16_t half = (uint16_t)(sign | ((uint32_t)new_exp << 10) | (mantissa >> 13));
    if (mantissa & 0x1000u) {
        half++;
    }
    return half;
}

static float half_bits_to_float(uint16_t value) {
    uint32_t sign = ((uint32_t)value & 0x8000u) << 16;
    uint32_t exponent = ((uint32_t)value >> 10) & 0x1Fu;
    uint32_t mantissa = (uint32_t)value & 0x03FFu;
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

static int can_pack_ptc_vbo(GLsizeiptr size, const GLvoid *data, GLenum usage) {
    (void)size;
    (void)data;
    (void)usage;
    return 0;
}

static void pack_ptc_vertex(uint8_t *dst, const uint8_t *src) {
    float x, y, z, u, v;
    memcpy(&x, src + 0, sizeof(float));
    memcpy(&y, src + 4, sizeof(float));
    memcpy(&z, src + 8, sizeof(float));
    memcpy(&u, src + 12, sizeof(float));
    memcpy(&v, src + 16, sizeof(float));

    uint16_t hx = float_to_half_bits(x);
    uint16_t hy = float_to_half_bits(y);
    uint16_t hz = float_to_half_bits(z);
    uint16_t hu = float_to_half_bits(u);
    uint16_t hv = float_to_half_bits(v);

    memcpy(dst + 0, &hx, sizeof(uint16_t));
    memcpy(dst + 2, &hy, sizeof(uint16_t));
    memcpy(dst + 4, &hz, sizeof(uint16_t));
    memcpy(dst + 6, &hu, sizeof(uint16_t));
    memcpy(dst + 8, &hv, sizeof(uint16_t));
    memcpy(dst + 10, src + 20, 4);
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
    const uint8_t *src = (const uint8_t*)slot->data + vertex_index * NOVA_VBO_PTC_PACKED_STRIDE;
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

    void *decoded = linearAlloc((size_t)slot->size);
    if (!decoded) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    vbo_decode_packed_ptc_span(slot, 0, slot->size / NOVA_VBO_PTC_RAW_STRIDE, (uint8_t*)decoded);
    linearFree(slot->data);
    slot->data = decoded;
    slot->capacity = slot->size;
    slot->storage_kind = NOVA_VBO_STORAGE_RAW;
    slot->storage_stride = 0;
    slot->allocated = 1;
    GSPGPU_FlushDataCache(slot->data, (u32)slot->size);
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 1;
        while (id < NOVA_MAX_VBOS && g.vbos[id].in_use) {
            id++;
        }
        if (id == NOVA_MAX_VBOS) { g.last_error = GL_OUT_OF_MEMORY; buffers[i] = 0; break; }

        memset(&g.vbos[id], 0, sizeof(g.vbos[id]));
        g.vbos[id].in_use = 1;
        buffers[i] = id;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id > 0 && (int)id < NOVA_MAX_VBOS && g.vbos[id].in_use) {
            VBOSlot *slot = &g.vbos[id];
            free_vbo_storage(slot);
            slot->in_use    = 0;

            if (g.bound_array_buffer == id)        g.bound_array_buffer = 0;
            if (g.bound_element_array_buffer == id) g.bound_element_array_buffer = 0;
        }
    }
}


void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) g.bound_array_buffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) g.bound_element_array_buffer = buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;

    if (id == 0 || id >= NOVA_MAX_VBOS) return;

    VBOSlot *slot = &g.vbos[id];
    int pack_ptc = (target == GL_ARRAY_BUFFER) && can_pack_ptc_vbo(size, data, usage);
    int desired_kind = pack_ptc ? NOVA_VBO_STORAGE_PACKED_PTC : NOVA_VBO_STORAGE_RAW;
    int desired_stride = pack_ptc ? NOVA_VBO_PTC_PACKED_STRIDE : 0;
    int desired_bytes = pack_ptc ? ((size / NOVA_VBO_PTC_RAW_STRIDE) * NOVA_VBO_PTC_PACKED_STRIDE) : (int)size;

    if (slot->allocated &&
        (slot->storage_kind != desired_kind || slot->storage_stride != desired_stride)) {
        free_vbo_storage(slot);
    }

    if (!slot->allocated ||
        slot->capacity < desired_bytes ||
        (desired_kind == NOVA_VBO_STORAGE_RAW && desired_bytes > 0 && slot->capacity > desired_bytes * 4))
    {
        int new_capacity = desired_bytes;

        void *new_buf = linearAlloc((size_t)new_capacity);
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
    slot->is_stream = (usage == GL_STREAM_DRAW);
    slot->storage_kind = desired_kind;
    slot->storage_stride = (uint8_t)desired_stride;

    if (data && slot->data)
    {
        if (pack_ptc) {
            const uint8_t *src = (const uint8_t*)data;
            uint8_t *dst = (uint8_t*)slot->data;
            int vertex_count = size / NOVA_VBO_PTC_RAW_STRIDE;
            for (int i = 0; i < vertex_count; i++) {
                pack_ptc_vertex(dst + i * NOVA_VBO_PTC_PACKED_STRIDE, src + i * NOVA_VBO_PTC_RAW_STRIDE);
            }
        } else {
            memcpy(slot->data, data, size);
        }
        GSPGPU_FlushDataCache(slot->data, (u32)desired_bytes);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;

    if (id == 0 || id >= NOVA_MAX_VBOS) return;

    VBOSlot *slot = &g.vbos[id];

    if (vbo_is_packed_ptc(slot)) {
        vbo_convert_slot_to_raw(slot);
        if (vbo_is_packed_ptc(slot)) {
            return;
        }
    }

    int required = offset + size;

    if (!slot->allocated || slot->capacity < required)
    {
        int new_capacity = slot->capacity ? slot->capacity : 1;

        while (new_capacity < required)
            new_capacity = (new_capacity * 3) / 2;

        void *new_buf = linearAlloc((size_t)new_capacity);
        if (!new_buf) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        if (slot->data && slot->capacity > 0) {
            memcpy(new_buf, slot->data, (size_t)slot->capacity);
            linearFree(slot->data);
        }

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    memcpy((uint8_t*)slot->data + offset, data, size);

    if (required > slot->size)
        slot->size = required;

    GSPGPU_FlushDataCache(slot->data, (u32)slot->size);
}
