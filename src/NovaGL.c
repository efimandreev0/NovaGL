/*
 * NovaGL.c - OpenGL ES 1.1 -> Citro3D Translation Layer Implementation
 */

#include "NovaGL.h"
#include "utils.h"

#include "NovaGL_shader_shbin.h"

void nova_init() {
    nova_init_ex(NOVA_CMD_BUF_SIZE, 512 * 1024, 256 * 1024, 512 * 1024);
}
void nova_init_ex(int cmd_buf_size, int client_array_buf_size, int index_buf_size, int tex_staging_size) {
    memset(&g, 0, sizeof(g));

    C3D_Init(cmd_buf_size);

    g.render_target_top = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                  GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    g.current_target = g.render_target_top;

    g.render_target_bot = C3D_RenderTargetCreate(NOVA_SCREEN_BOTTOM_W, NOVA_SCREEN_BOTTOM_H,
                                                  GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_bot, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    g.shader_dvlb = DVLB_ParseFile((u32*)NovaGL_shader_shbin, NovaGL_shader_shbin_size);

    if (g.shader_dvlb) {
        shaderProgramInit(&g.shader_program);
        shaderProgramSetVsh(&g.shader_program, &g.shader_dvlb->DVLE[0]);
        C3D_BindProgram(&g.shader_program);

        g.uLoc_projection = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "projection");
        g.uLoc_modelview  = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "modelview");
        g.uLoc_fogparams  = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "fogparams");
    }

    AttrInfo_Init(&g.attr_info);
    AttrInfo_AddLoader(&g.attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(&g.attr_info, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&g.attr_info, 2, GPU_UNSIGNED_BYTE, 4);
    C3D_SetAttrInfo(&g.attr_info);

    g.depth_near = 0.0f;
    g.depth_far = 1.0f;
    g.polygon_offset_fill_enabled = 0;
    apply_depth_map();

    g.client_array_buf_size = client_array_buf_size;
    //g.client_array_buf_size = 2 * 1024 * 1024;
    g.client_array_buf = linearAlloc(g.client_array_buf_size);
    g.index_buf_size = index_buf_size;
    g.index_buf = linearAlloc(g.index_buf_size);

    g.matrix_mode = GL_MODELVIEW;
    Mtx_Identity(&g.proj_stack[0]);
    Mtx_Identity(&g.mv_stack[0]);
    Mtx_Identity(&g.tex_stack[0]);
    g.proj_sp = 0;
    g.mv_sp = 0;
    g.tex_sp = 0;
    g.matrices_dirty = 1;

    g.cur_color[0] = 1.0f; g.cur_color[1] = 1.0f;
    g.cur_color[2] = 1.0f; g.cur_color[3] = 1.0f;

    g.depth_test_enabled = 1;
    g.depth_func = GL_LEQUAL;
    g.depth_mask = GL_TRUE;
    g.clear_depth = 1.0f;
    g.blend_src = GL_ONE;
    g.blend_dst = GL_ZERO;
    g.alpha_func = GL_ALWAYS;
    g.alpha_ref = 0.0f;
    g.cull_face_mode = GL_BACK;
    g.front_face = GL_CCW;

    g.fog_mode = GL_LINEAR;
    g.fog_start = 0.0f; g.fog_end = 1.0f; g.fog_density = 1.0f;
    g.fog_color[0] = g.fog_color[1] = g.fog_color[2] = 0.0f; g.fog_color[3] = 1.0f;

    g.vp_x = 0; g.vp_y = 0;
    g.vp_w = NOVA_SCREEN_W; g.vp_h = NOVA_SCREEN_H;

    g.color_mask_r = g.color_mask_g = g.color_mask_b = g.color_mask_a = GL_TRUE;

    g.tex_next_id = 1;
    g.vbo_next_id = 1;
    g.dl_recording = -1;
    g.dl_next_base = 1;
    g.texture_2d_enabled = 0;
    g.tev_dirty = 1;

    g.tex_staging_size = tex_staging_size;
    g.tex_staging = linearAlloc(g.tex_staging_size);

    g.initialized = 1;
}

void nova_fini(void) {
    if (!g.initialized) return;
    if (g.client_array_buf) linearFree(g.client_array_buf);
    if (g.index_buf) linearFree(g.index_buf);
    for (int i = 0; i < NOVA_MAX_TEXTURES; i++) {
        if (g.textures[i].allocated) C3D_TexDelete(&g.textures[i].tex);
    }
    for (int i = 0; i < NOVA_MAX_VBOS; i++) {
        if (g.vbos[i].allocated && g.vbos[i].data) linearFree(g.vbos[i].data);
    }
    if (g.shader_dvlb) {
        shaderProgramFree(&g.shader_program);
        DVLB_Free(g.shader_dvlb);
    }
    C3D_RenderTargetDelete(g.render_target_top);
    C3D_RenderTargetDelete(g.render_target_bot);
    C3D_Fini();
    g.initialized = 0;
}

void nova_frame_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;
}
void nova_frame_end(void) { C3D_FrameEnd(0); }
void nova_set_render_target(int is_right_eye) {
    C3D_FrameDrawOn(is_right_eye ? g.render_target_bot : g.render_target_top);
    g.current_target = is_right_eye ? g.render_target_bot : g.render_target_top;
}
static void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices) {
    if (count <= 0) return;
    if (is_elements && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }

    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS) {
        if (!g.vbos[g.va_vertex.vbo_id].allocated || g.vbos[g.va_vertex.vbo_id].data == NULL) return;
    }

    const uint8_t *idx_src = NULL;
    if (is_elements) {
        if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < NOVA_MAX_VBOS) {
            if (!g.vbos[g.bound_element_array_buffer].allocated || g.vbos[g.bound_element_array_buffer].data == NULL) return;
            idx_src = (const uint8_t*)g.vbos[g.bound_element_array_buffer].data + (uintptr_t)indices;
        } else {
            idx_src = (const uint8_t*)indices;
        }
        if (!idx_src) return;
    }

    apply_gpu_state();
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);

    GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
    if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;

    // --- FAST PATH ---
    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 && (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id && g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 && (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id && g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 && (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];

        if (is_elements) {
            GSPGPU_FlushDataCache(vbo->data, vbo->size);
            BufInfo_Add(bufInfo, vbo->data, 24, 3, 0x210);

            int idx_bytes = count * 2;
            if (idx_bytes > g.index_buf_size) return;

            g.index_buf_offset = (g.index_buf_offset + 7) & ~7;
            if (g.index_buf_offset + idx_bytes > g.index_buf_size) {
                C3D_FrameSplit(0);
                g.index_buf_offset = 0;
            }

            uint16_t *dst_idx = (uint16_t*)linear_alloc_ring(g.index_buf, &g.index_buf_offset, idx_bytes, g.index_buf_size);

            if (type == GL_UNSIGNED_SHORT) memcpy(dst_idx, idx_src, count * 2);
            else for (int i = 0; i < count; i++) dst_idx[i] = idx_src[i];

            GSPGPU_FlushDataCache(dst_idx, idx_bytes);
            C3D_DrawElements(prim, count, C3D_UNSIGNED_SHORT, dst_idx);
        } else {
            uint8_t *data = (uint8_t*)vbo->data + first * 24;
            GSPGPU_FlushDataCache(data, count * 24);
            BufInfo_Add(bufInfo, data, 24, 3, 0x210);

            if (mode == GL_QUADS) draw_emulated_quads(count);
            else C3D_DrawArrays(prim, 0, count);
        }
        cleanup_vbo_stream();
        return;
    }

    // --- SLOW PATH ---
    int req_size = count * 24;
    if (req_size > g.client_array_buf_size) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    uint8_t *dst = (uint8_t*)linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size, g.client_array_buf_size);
    uint8_t *dst_start = dst;

    int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
    int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
    int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

    const uint8_t *src_v = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated) ? (const uint8_t*)g.vbos[g.va_vertex.vbo_id].data + (uintptr_t)g.va_vertex.pointer : (const uint8_t*)g.va_vertex.pointer;
    const uint8_t *src_t = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_texcoord.vbo_id].allocated) ? (const uint8_t*)g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t)g.va_texcoord.pointer : (const uint8_t*)g.va_texcoord.pointer;
    const uint8_t *src_c = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_color.vbo_id].allocated) ? (const uint8_t*)g.vbos[g.va_color.vbo_id].data + (uintptr_t)g.va_color.pointer : (const uint8_t*)g.va_color.pointer;

    uint8_t def_col[4] = {
        (uint8_t)(g.cur_color[0] * 255.0f), (uint8_t)(g.cur_color[1] * 255.0f),
        (uint8_t)(g.cur_color[2] * 255.0f), (uint8_t)(g.cur_color[3] * 255.0f)
    };

    for (int i = 0; i < count; i++) {
        int src_index = is_elements ?
            (type == GL_UNSIGNED_SHORT ? ((const uint16_t*)idx_src)[i] : ((const uint8_t*)idx_src)[i]) :
            (first + i);

        // Vertex
        if (g.va_vertex.enabled && src_v && (uintptr_t)src_v > 0x1000) {
            float pos[3] = {0.0f, 0.0f, 0.0f};
            read_vertex_attrib_float(pos, src_v + src_index * p_str, g.va_vertex.size, g.va_vertex.type);
            memcpy(dst, pos, 12);
        } else memset(dst, 0, 12);

        // TexCoord
        if (g.va_texcoord.enabled && src_t && (uintptr_t)src_t > 0x1000) {
            float tc[2] = {0.0f, 0.0f};
            read_vertex_attrib_float(tc, src_t + src_index * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
            memcpy(dst + 12, tc, 8);
        } else memset(dst + 12, 0, 8);

        // Color
        if (g.va_color.enabled && src_c && (uintptr_t)src_c > 0x1000) {
            const uint8_t* c_ptr = src_c + src_index * c_str;
            if (g.va_color.type == GL_UNSIGNED_BYTE || (g.va_color.type != GL_FLOAT)) {
                memcpy(dst + 20, c_ptr, g.va_color.size == 3 ? 3 : 4);
                if (g.va_color.size == 3) dst[23] = 255;
            } else {
                const float *cf = (const float*)c_ptr;
                dst[20] = (uint8_t)(clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                dst[21] = (uint8_t)(clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                dst[22] = (uint8_t)(clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                dst[23] = g.va_color.size >= 4 ? (uint8_t)(clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
            }
        } else {
            memcpy(dst + 20, def_col, 4);
        }

        dst += 24;
    }

    GSPGPU_FlushDataCache(dst_start, req_size);
    BufInfo_Add(bufInfo, dst_start, 24, 3, 0x210);
    g.client_array_buf_offset += req_size;

    if (mode == GL_QUADS) {
        draw_emulated_quads(count);
    } else {
        C3D_DrawArrays(prim, 0, count);
    }

    cleanup_vbo_stream();
}
GLenum glGetError(void) { GLenum e = g.last_error; g.last_error = GL_NO_ERROR; return e; }




void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) { (void)nx; (void)ny; (void)nz; }

















GLuint glGenLists(GLsizei range) {
    GLuint base = g.dl_next_base;
    g.dl_next_base += range;
    if (g.dl_next_base >= NOVA_DISPLAY_LISTS) g.dl_next_base = 1;
    for (GLsizei i = 0; i < range && (base + i) < NOVA_DISPLAY_LISTS; i++) {
        g.dl_store[base + i].count = 0; g.dl_store[base + i].used = 1;
    }
    return base;
}

void glNewList(GLuint list, GLenum mode) {
    (void)mode;
    if (list < NOVA_DISPLAY_LISTS) { g.dl_recording = list; g.dl_store[list].count = 0; }
}

void glEndList(void) { g.dl_recording = -1; }

void glCallList(GLuint list) { dl_execute(list); }

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists) {
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 0;
        if (type == GL_UNSIGNED_INT) id = ((const GLuint*)lists)[i];
        else if (type == GL_UNSIGNED_BYTE) id = ((const GLubyte*)lists)[i];
        else if (type == GL_UNSIGNED_SHORT) id = ((const GLushort*)lists)[i];
        dl_execute(id);
    }
}

void glDeleteLists(GLuint list, GLsizei range) {
    for (GLsizei i = 0; i < range && (list + i) < NOVA_DISPLAY_LISTS; i++) {
        g.dl_store[list + i].used = 0; g.dl_store[list + i].count = 0;
    }
}


void glFlush(void) { }
void glFinish(void) { }

