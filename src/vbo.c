//
// Created by Notebook on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glGenBuffers(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint start = g.vbo_next_id;
        while (g.vbos[g.vbo_next_id].allocated) {
            g.vbo_next_id++;
            if (g.vbo_next_id >= NOVA_MAX_VBOS) g.vbo_next_id = 1;
            if (g.vbo_next_id == start) { g.last_error = GL_OUT_OF_MEMORY; buffers[i] = 0; break; }
        }
        buffers[i] = g.vbo_next_id;
        g.vbo_next_id++;
        if (g.vbo_next_id >= NOVA_MAX_VBOS) g.vbo_next_id = 1;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id > 0 && (int)id < NOVA_MAX_VBOS && g.vbos[id].allocated) {
            if (g.vbos[id].data) linearFree(g.vbos[id].data);
            g.vbos[id].data = NULL; g.vbos[id].size = 0; g.vbos[id].allocated = 0;
            if (g.bound_array_buffer == id) g.bound_array_buffer = 0;
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
    (void)usage;

    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;

    if (id == 0 || id >= NOVA_MAX_VBOS) return;

    VBOSlot *slot = &g.vbos[id];

    // 🔥 если нужно больше памяти → расширяем
    if (!slot->allocated || slot->capacity < size)
    {
        int new_capacity = size;

        if (slot->capacity > 0)
        {
            new_capacity = slot->capacity;
            while (new_capacity < size)
                new_capacity = (new_capacity * 3) / 2; // growth
        }

        void *new_buf = linearAlloc(new_capacity);
        if (!new_buf) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        // копируем старые данные
        if (slot->data)
        {
            memcpy(new_buf, slot->data, slot->size);
            linearFree(slot->data);
        }

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    slot->size = size;

#ifdef NOVA_VBO_STREAM
    slot->is_stream = (usage == GL_STREAM_DRAW);
#endif
    if (data)
    {
        memcpy(slot->data, data, size);
        GSPGPU_FlushDataCache(slot->data, size);
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GLuint id = 0;
    if (target == GL_ARRAY_BUFFER) id = g.bound_array_buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) id = g.bound_element_array_buffer;

    if (id == 0 || id >= NOVA_MAX_VBOS) return;

    VBOSlot *slot = &g.vbos[id];

    int required = offset + size;

    if (!slot->allocated || slot->capacity < required)
    {
        int new_capacity = slot->capacity ? slot->capacity : 1;

        while (new_capacity < required)
            new_capacity = (new_capacity * 3) / 2;

        void *new_buf = linearAlloc(new_capacity);
        if (!new_buf) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        if (slot->data)
        {
            memcpy(new_buf, slot->data, slot->size);
            linearFree(slot->data);
        }

        slot->data = new_buf;
        slot->capacity = new_capacity;
        slot->allocated = 1;
    }

    memcpy((uint8_t*)slot->data + offset, data, size);

    if (required > slot->size)
        slot->size = required;

    GSPGPU_FlushDataCache((uint8_t*)slot->data + offset, size);
}