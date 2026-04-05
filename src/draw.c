//
// Created by Notebook on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count <= 0) return;

    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS) {
        if (!g.vbos[g.va_vertex.vbo_id].allocated || g.vbos[g.va_vertex.vbo_id].data == NULL) {
            return;
        }
    }

    apply_gpu_state();

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);

    // VBO fast path
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 &&
        (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id &&
        g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 &&
        (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id &&
        g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 &&
        (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        uint8_t *data = (uint8_t*)vbo->data + first * 24;
        GSPGPU_FlushDataCache(data, count * 24);
        BufInfo_Add(bufInfo, data, 24, 3, 0x210);

        if (mode == GL_QUADS) {
            int num_quads = count / 4;
            int idx_count = num_quads * 6;
            int idx_bytes = idx_count * 2;

            if (idx_bytes > g.index_buf_size) return;

            g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
            if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
                C3D_FrameSplit(0);
                g.index_buf_offset = 0;
            }

            uint16_t *idx = (uint16_t*)linear_alloc_ring(
    g.index_buf,
    &g.index_buf_offset,
    idx_bytes,
    g.index_buf_size
);
            for (int q = 0; q < num_quads; q++) {
                uint16_t base = q * 4;
                idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
                idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
            }
            GSPGPU_FlushDataCache(idx, idx_bytes);
            C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
#ifdef NOVA_VBO_STREAM
            if (g.bound_array_buffer)
            {
                VBOSlot *slot = &g.vbos[g.bound_array_buffer];

                if (slot->is_stream && slot->data)
                {
                    linearFree(slot->data);
                    slot->data = NULL;
                    slot->allocated = 0;
                    slot->size = 0;
                    slot->capacity = 0;
                }
            }
#endif
        } else {
            C3D_DrawArrays(gl_to_gpu_primitive(mode), 0, count);
#ifdef NOVA_VBO_STREAM
            if (g.bound_array_buffer)
            {
                VBOSlot *slot = &g.vbos[g.bound_array_buffer];

                if (slot->is_stream && slot->data)
                {
                    linearFree(slot->data);
                    slot->data = NULL;
                    slot->allocated = 0;
                    slot->size = 0;
                    slot->capacity = 0;
                }
            }
#endif
        }
        return;
    }

    // Slow path: assemble interleaved vertices from separate arrays
    int req_size = count * 24;

    // ЗАЩИТА 3: Переполнение буфера массивов клиента
    if (req_size > g.client_array_buf_size) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    uint8_t *dst = (uint8_t*)linear_alloc_ring(
        g.client_array_buf,
        &g.client_array_buf_offset,
        req_size,
        g.client_array_buf_size
    );

    uint8_t *dst_start = dst;
    int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
    int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
    int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

    for (int i = 0; i < count; i++) {
        if (g.va_vertex.enabled) {
            const uint8_t *src_ptr = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_vertex.vbo_id].data + (uintptr_t)g.va_vertex.pointer : (const uint8_t*)g.va_vertex.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float pos[3] = {0.0f, 0.0f, 0.0f};
                read_vertex_attrib_float(pos, src_ptr + (first + i) * p_str, g.va_vertex.size, g.va_vertex.type);
                memcpy(dst, pos, 12);
            } else { memset(dst, 0, 12); }
        } else { memset(dst, 0, 12); }

        if (g.va_texcoord.enabled) {
            const uint8_t *src_ptr = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_texcoord.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t)g.va_texcoord.pointer : (const uint8_t*)g.va_texcoord.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float tc[2] = {0.0f, 0.0f};
                read_vertex_attrib_float(tc, src_ptr + (first + i) * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
                memcpy(dst + 12, tc, 8);
            } else memset(dst + 12, 0, 8);
        } else { memset(dst + 12, 0, 8); }

        if (g.va_color.enabled) {
            const uint8_t *src_ptr = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_color.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_color.vbo_id].data + (uintptr_t)g.va_color.pointer : (const uint8_t*)g.va_color.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                if (g.va_color.type == GL_UNSIGNED_BYTE) {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + (first + i) * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + (first + i) * c_str, 4);
                } else if (g.va_color.type == GL_FLOAT) {
                    const float *cf = (const float*)(src_ptr + (first + i) * c_str);
                    dst[20] = (uint8_t)(clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                    dst[21] = (uint8_t)(clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                    dst[22] = (uint8_t)(clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                    dst[23] = g.va_color.size >= 4 ? (uint8_t)(clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
                } else {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + (first + i) * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + (first + i) * c_str, 4);
                }
            } else { dst[20] = dst[21] = dst[22] = dst[23] = 255; }
        } else {
            dst[20] = (uint8_t)(g.cur_color[0] * 255.0f); dst[21] = (uint8_t)(g.cur_color[1] * 255.0f);
            dst[22] = (uint8_t)(g.cur_color[2] * 255.0f); dst[23] = (uint8_t)(g.cur_color[3] * 255.0f);
        }
        dst += 24;
    }
    GSPGPU_FlushDataCache(dst_start, req_size);
    BufInfo_Add(bufInfo, dst_start, 24, 3, 0x210);
    g.client_array_buf_offset += req_size;

    if (mode == GL_QUADS) {
        int num_quads = count / 4;
        int idx_count = num_quads * 6;
        int idx_bytes = idx_count * 2;

        if (idx_bytes > g.index_buf_size) return;

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *idx = (uint16_t*)linear_alloc_ring(
    g.index_buf,
    &g.index_buf_offset,
    idx_bytes,
    g.index_buf_size
);
        for (int q = 0; q < num_quads; q++) {
            uint16_t base = q * 4;
            idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
            idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
        }
        GSPGPU_FlushDataCache(idx, idx_bytes);
        C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
#ifdef NOVA_VBO_STREAM
        if (g.bound_array_buffer)
        {
            VBOSlot *slot = &g.vbos[g.bound_array_buffer];

            if (slot->is_stream && slot->data)
            {
                linearFree(slot->data);
                slot->data = NULL;
                slot->allocated = 0;
                slot->size = 0;
                slot->capacity = 0;
            }
        }
#endif
    } else {
        GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
        if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;
        C3D_DrawArrays(prim, 0, count);
#ifdef NOVA_VBO_STREAM
        if (g.bound_array_buffer)
        {
            VBOSlot *slot = &g.vbos[g.bound_array_buffer];

            if (slot->is_stream && slot->data)
            {
                linearFree(slot->data);
                slot->data = NULL;
                slot->allocated = 0;
                slot->size = 0;
                slot->capacity = 0;
            }
        }
#endif
    }
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    if (count <= 0) return;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE) { g.last_error = GL_INVALID_ENUM; return; }

    // ЗАЩИТА: Проверка на пустые/выбитые из памяти VBO
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS) {
        if (!g.vbos[g.va_vertex.vbo_id].allocated || g.vbos[g.va_vertex.vbo_id].data == NULL) return;
    }

    const uint8_t *idx_src = NULL;

    if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < NOVA_MAX_VBOS) {
        if (!g.vbos[g.bound_element_array_buffer].allocated || g.vbos[g.bound_element_array_buffer].data == NULL) return;
        idx_src = (const uint8_t*)g.vbos[g.bound_element_array_buffer].data + (uintptr_t)indices;
    } else {
        idx_src = (const uint8_t*)indices;
    }
    if (!idx_src) return;

    apply_gpu_state();
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);

    GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
    if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;

    // VBO fast path
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 &&
        (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id &&
        g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 &&
        (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id &&
        g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 &&
        (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        GSPGPU_FlushDataCache(vbo->data, vbo->size);
        BufInfo_Add(bufInfo, vbo->data, 24, 3, 0x210);

        int idx_bytes = count * 2;
        if (idx_bytes > g.index_buf_size) return; // ЗАЩИТА

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *dst_idx = (uint16_t*)linear_alloc_ring(
            g.index_buf,
            &g.index_buf_offset,
            idx_bytes,
            g.index_buf_size
        );
        if (type == GL_UNSIGNED_SHORT) {
            memcpy(dst_idx, idx_src, count * 2);
        } else {
            for (int i = 0; i < count; i++) dst_idx[i] = idx_src[i];
        }
        GSPGPU_FlushDataCache(dst_idx, idx_bytes);
        C3D_DrawElements(prim, count, C3D_UNSIGNED_SHORT, dst_idx);
#ifdef NOVA_VBO_STREAM
        if (g.bound_array_buffer)
        {
            VBOSlot *slot = &g.vbos[g.bound_array_buffer];

            if (slot->is_stream && slot->data)
            {
                linearFree(slot->data);
                slot->data = NULL;
                slot->allocated = 0;
                slot->size = 0;
                slot->capacity = 0;
            }
        }
#endif
        return;
    }

    // Slow path
    int req_size = count * 24;

    if (req_size > g.client_array_buf_size) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    uint8_t *dst = (uint8_t*)linear_alloc_ring(
        g.client_array_buf,
        &g.client_array_buf_offset,
        req_size,
        g.client_array_buf_size
    );

    uint8_t *dst_start = dst;
    int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
    int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
    int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

    for (int i = 0; i < count; i++) {
        int src_index = (type == GL_UNSIGNED_SHORT) ? ((const uint16_t*)idx_src)[i] : ((const uint8_t*)idx_src)[i];

        if (g.va_vertex.enabled) {
            const uint8_t *src_ptr = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_vertex.vbo_id].data + (uintptr_t)g.va_vertex.pointer : (const uint8_t*)g.va_vertex.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float pos[3] = {0.0f, 0.0f, 0.0f};
                read_vertex_attrib_float(pos, src_ptr + src_index * p_str, g.va_vertex.size, g.va_vertex.type);
                memcpy(dst, pos, 12);
            } else memset(dst, 0, 12);
        } else memset(dst, 0, 12);

        if (g.va_texcoord.enabled) {
            const uint8_t *src_ptr = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_texcoord.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t)g.va_texcoord.pointer : (const uint8_t*)g.va_texcoord.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                float tc[2] = {0.0f, 0.0f};
                read_vertex_attrib_float(tc, src_ptr + src_index * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
                memcpy(dst + 12, tc, 8);
            } else memset(dst + 12, 0, 8);
        } else memset(dst + 12, 0, 8);

        if (g.va_color.enabled) {
            const uint8_t *src_ptr = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_color.vbo_id].allocated) ?
                (const uint8_t*)g.vbos[g.va_color.vbo_id].data + (uintptr_t)g.va_color.pointer : (const uint8_t*)g.va_color.pointer;
            if (src_ptr && (uintptr_t)src_ptr > 0x1000) {
                if (g.va_color.type == GL_UNSIGNED_BYTE) {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + src_index * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + src_index * c_str, 4);
                } else if (g.va_color.type == GL_FLOAT) {
                    const float *cf = (const float*)(src_ptr + src_index * c_str);
                    dst[20] = (uint8_t)(clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                    dst[21] = (uint8_t)(clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                    dst[22] = (uint8_t)(clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                    dst[23] = g.va_color.size >= 4 ? (uint8_t)(clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
                } else {
                    if (g.va_color.size == 3) { memcpy(dst + 20, src_ptr + src_index * c_str, 3); dst[23] = 255; }
                    else memcpy(dst + 20, src_ptr + src_index * c_str, 4);
                }
            } else { dst[20] = dst[21] = dst[22] = dst[23] = 255; }
        } else {
            dst[20] = (uint8_t)(g.cur_color[0] * 255.0f); dst[21] = (uint8_t)(g.cur_color[1] * 255.0f);
            dst[22] = (uint8_t)(g.cur_color[2] * 255.0f); dst[23] = (uint8_t)(g.cur_color[3] * 255.0f);
        }
        dst += 24;
    }

    GSPGPU_FlushDataCache(dst_start, req_size);
    BufInfo_Add(bufInfo, dst_start, 24, 3, 0x210);
    g.client_array_buf_offset += req_size;

    if (mode == GL_QUADS) {
        int num_quads = count / 4;
        int idx_count = num_quads * 6;
        int idx_bytes = idx_count * 2;

        if (idx_bytes > g.index_buf_size) return;

        g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
        if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
            C3D_FrameSplit(0);
            g.index_buf_offset = 0;
        }

        uint16_t *idx = (uint16_t*)linear_alloc_ring(
    g.index_buf,
    &g.index_buf_offset,
    idx_bytes,
    g.index_buf_size
);
        for (int q = 0; q < num_quads; q++) {
            uint16_t base = q * 4;
            idx[q*6+0] = base+0; idx[q*6+1] = base+1; idx[q*6+2] = base+2;
            idx[q*6+3] = base+0; idx[q*6+4] = base+2; idx[q*6+5] = base+3;
        }
        GSPGPU_FlushDataCache(idx, idx_bytes);
        C3D_DrawElements(GPU_TRIANGLES, idx_count, C3D_UNSIGNED_SHORT, idx);
#ifdef NOVA_VBO_STREAM
        if (g.bound_array_buffer)
        {
            VBOSlot *slot = &g.vbos[g.bound_array_buffer];

            if (slot->is_stream && slot->data)
            {
                linearFree(slot->data);
                slot->data = NULL;
                slot->allocated = 0;
                slot->size = 0;
                slot->capacity = 0;
            }
        }
#endif
    } else {
        C3D_DrawArrays(prim, 0, count);
#ifdef NOVA_VBO_STREAM
        if (g.bound_array_buffer)
        {
            VBOSlot *slot = &g.vbos[g.bound_array_buffer];

            if (slot->is_stream && slot->data)
            {
                linearFree(slot->data);
                slot->data = NULL;
                slot->allocated = 0;
                slot->size = 0;
                slot->capacity = 0;
            }
        }
#endif
    }
}