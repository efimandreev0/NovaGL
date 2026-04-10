/*
 * NovaGL.c - OpenGL ES 1.1 -> Citro3D Translation Layer Implementation
 */

#include "NovaGL.h"
#include "utils.h"
#include "context.h"
#include <math.h>

#include "NovaGL_shader_shbin.h"

struct NovaState g;

void nova_init() {
    nova_init_ex(NOVA_CMD_BUF_SIZE, 8 * 1024 * 1024, 512 * 1024, 512 * 1024);
}

void nova_init_ex(int cmd_buf_size, int client_array_buf_size, int index_buf_size, int tex_staging_size) {
    memset(&g, 0, sizeof(g));

    if (client_array_buf_size < 8 * 1024 * 1024) {
        client_array_buf_size = 8 * 1024 * 1024;
    }
    if (index_buf_size < 512 * 1024) {
        index_buf_size = 512 * 1024;
    }

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

    g.static_quad_count = 16384;
    g.static_quad_indices = (uint16_t*)linearAlloc(g.static_quad_count * 6 * sizeof(uint16_t));
    if (g.static_quad_indices) {
        for (int q = 0; q < g.static_quad_count; q++) {
            uint16_t base = q * 4;
            g.static_quad_indices[q*6+0] = base+0; g.static_quad_indices[q*6+1] = base+1; g.static_quad_indices[q*6+2] = base+2;
            g.static_quad_indices[q*6+3] = base+0; g.static_quad_indices[q*6+4] = base+2; g.static_quad_indices[q*6+5] = base+3;
        }
        GSPGPU_FlushDataCache(g.static_quad_indices, g.static_quad_count * 6 * sizeof(uint16_t));
    }

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

    g.shade_model = GL_SMOOTH;
    g.fog_mode = GL_LINEAR;
    g.fog_start = 0.0f; g.fog_end = 1.0f; g.fog_density = 1.0f;
    g.fog_color[0] = g.fog_color[1] = g.fog_color[2] = 0.0f; g.fog_color[3] = 1.0f;
    g.fog_dirty = 1;

    g.vp_x = 0; g.vp_y = 0;
    g.vp_w = NOVA_SCREEN_W; g.vp_h = NOVA_SCREEN_H;

    g.color_mask_r = g.color_mask_g = g.color_mask_b = g.color_mask_a = GL_TRUE;

    g.tex_next_id = 1;
    g.vbo_next_id = 1;
    g.dl_recording = -1;
    g.dl_next_base = 1;
    g.active_texture_unit = 0;
    g.client_active_texture_unit = 0;
    g.texture_2d_enabled_unit[0] = 0;
    g.texture_2d_enabled_unit[1] = 0;
    g.texture_2d_enabled_unit[2] = 0;
    g.tex_env_mode[0] = GL_MODULATE;
    g.tex_env_mode[1] = GL_MODULATE;
    g.tex_env_mode[2] = GL_MODULATE;
    g.tev_dirty = 1;
    g.pack_alignment = 4;
    g.unpack_alignment = 4;

    (void)tex_staging_size;
    g.tex_staging_size = 0;
    g.tex_staging = NULL;

    g.initialized = 1;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;

    C3D_FrameDrawOn(g.render_target_top);
    g.current_target = g.render_target_top;
}
void novaSwapBuffers(void) {
    C3D_FrameEnd(0);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;

    C3D_FrameDrawOn(g.render_target_top);
    g.current_target = g.render_target_top;
}
void nova_fini(void) {
    if (!g.initialized) return;

    if (g.client_array_buf) linearFree(g.client_array_buf);
    if (g.index_buf)        linearFree(g.index_buf);

    if (g.tex_staging) {
        free(g.tex_staging);
        g.tex_staging = NULL;
        g.tex_staging_size = 0;
    }

    for (int i = 0; i < NOVA_MAX_TEXTURES; i++) {
        if (g.textures[i].is_tiled && g.textures[i].pages) {
            for (int p = 0; p < g.textures[i].tiles_x * g.textures[i].tiles_y; p++) {
                if (g.textures[i].pages[p].allocated) {
                    C3D_TexDelete(&g.textures[i].pages[p].tex);
                }
            }
            free(g.textures[i].pages);
            g.textures[i].pages = NULL;
        } else if (g.textures[i].allocated) {
            C3D_TexDelete(&g.textures[i].tex);
        }
    }
    for (int i = 0; i < NOVA_MAX_VBOS; i++) {
        if (g.vbos[i].allocated && g.vbos[i].data) linearFree(g.vbos[i].data);
    }

    if (g.static_quad_indices) {
        linearFree(g.static_quad_indices);
        g.static_quad_indices = NULL;
        g.static_quad_count   = 0;
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

void nova_set_render_target(int is_right_eye) {
    C3D_FrameDrawOn(is_right_eye ? g.render_target_bot : g.render_target_top);
    g.current_target = is_right_eye ? g.render_target_bot : g.render_target_top;
}

static TexSlot* get_active_tiled_texture_unit0(void) {
    if (!g.texture_2d_enabled_unit[0]) return NULL;
    GLuint tex_id = g.bound_texture[0];
    if (tex_id == 0 || tex_id >= NOVA_MAX_TEXTURES) return NULL;
    TexSlot *slot = &g.textures[tex_id];
    if (!slot->allocated || !slot->is_tiled || !slot->pages) return NULL;
    return slot;
}

static int primitive_vertex_count(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES: return 3;
        case GL_QUADS: return 4;
        case GL_LINES: return 2;
        default: return 0;
    }
}

static int tiled_page_from_uv(const TexSlot *slot, float u, float v) {
    int tx = (int)floorf((u * (float)slot->width) / (float)slot->tile_w);
    int ty = (int)floorf((v * (float)slot->height) / (float)slot->tile_h);

    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= slot->tiles_x) tx = slot->tiles_x - 1;
    if (ty >= slot->tiles_y) ty = slot->tiles_y - 1;
    return ty * slot->tiles_x + tx;
}

static int tiled_page_for_primitive(const TexSlot *slot, const uint8_t *base, int prim_start, int prim_verts) {
    const float *uv0 = (const float*)(base + prim_start * 24 + 12);
    int page = tiled_page_from_uv(slot, uv0[0], uv0[1]);

    for (int i = 1; i < prim_verts; i++) {
        const float *uv = (const float*)(base + (prim_start + i) * 24 + 12);
        if (tiled_page_from_uv(slot, uv[0], uv[1]) != page) {
            return page;
        }
    }
    return page;
}

static void remap_tiled_uvs_in_place(const TexSlot *slot, int page_index, uint8_t *base, int vert_count) {
    int tx = page_index % slot->tiles_x;
    int ty = page_index / slot->tiles_x;
    const TexPage *page = &slot->pages[page_index];
    const float page_x0 = (float)(tx * slot->tile_w);
    const float page_y0 = (float)(ty * slot->tile_h);

    for (int i = 0; i < vert_count; i++) {
        float *uv = (float*)(base + i * 24 + 12);
        float px = uv[0] * (float)slot->width;
        float py = uv[1] * (float)slot->height;
        uv[0] = (px - page_x0) / (float)page->pot_w;
        uv[1] = (py - page_y0) / (float)page->pot_h;
    }
}

static void draw_packed_run(GLenum mode, GPU_Primitive_t prim, uint8_t *base, int count) {
    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, base, 24, 3, 0x210);

    if (mode == GL_QUADS) draw_emulated_quads(count);
    else C3D_DrawArrays(prim, 0, count);
}

static void draw_tiled_batches(GLenum mode, GPU_Primitive_t prim, TexSlot *slot, uint8_t *base, int count) {
    int prim_verts = primitive_vertex_count(mode);

    if (prim_verts <= 0) {
        C3D_TexBind(0, &slot->pages[0].tex);
        GSPGPU_FlushDataCache(base, count * 24);
        draw_packed_run(mode, prim, base, count);
        return;
    }

    for (int prim_start = 0; prim_start + prim_verts <= count; ) {
        int page_index = tiled_page_for_primitive(slot, base, prim_start, prim_verts);
        int run_start = prim_start;
        int run_end = prim_start + prim_verts;

        while (run_end + prim_verts <= count &&
               tiled_page_for_primitive(slot, base, run_end, prim_verts) == page_index) {
            run_end += prim_verts;
        }

        uint8_t *run_base = base + run_start * 24;
        int run_count = run_end - run_start;
        remap_tiled_uvs_in_place(slot, page_index, run_base, run_count);
        GSPGPU_FlushDataCache(run_base, run_count * 24);
        C3D_TexBind(0, &slot->pages[page_index].tex);
        draw_packed_run(mode, prim, run_base, run_count);

        prim_start = run_end;
    }
}

static int packed_ptc_attr_compatible(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer, int expected_offset, int max_size) {
    int effective_stride = stride ? stride : 24;
    return type == (expected_offset == 20 ? GL_UNSIGNED_BYTE : GL_FLOAT) &&
           effective_stride == 24 &&
           (int)(uintptr_t)pointer == expected_offset &&
           size > 0 &&
           size <= max_size;
}

void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices) {
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
    GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
    if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;
    TexSlot *tiled_slot = get_active_tiled_texture_unit0();

    // --- FAST PATH ---
    if (!is_elements &&
        g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 && (uintptr_t)g.va_vertex.pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id && g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 && (uintptr_t)g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id && g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 && (uintptr_t)g.va_color.pointer == 20)
    {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        int available = vbo->size - first * 24;
        if (available < count * 24) {
            count = available / 24;
            if (count <= 0) return;
        }
        int req_size = count * 24;
        if (req_size > g.client_array_buf_size) {
            g.last_error = GL_OUT_OF_MEMORY;
            return;
        }

        uint8_t *dst_start = (uint8_t*)linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size, g.client_array_buf_size);
        if (vbo_is_packed_ptc(vbo)) {
            vbo_decode_packed_ptc_span(vbo, first, count, dst_start);
        } else {
            memcpy(dst_start, (const uint8_t*)vbo->data + first * 24, req_size);
        }

        if (tiled_slot) {
            draw_tiled_batches(mode, prim, tiled_slot, dst_start, count);
        } else {
            GSPGPU_FlushDataCache(dst_start, req_size);
            draw_packed_run(mode, prim, dst_start, count);
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

    VBOSlot *v_slot = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS) ? &g.vbos[g.va_vertex.vbo_id] : NULL;
    VBOSlot *t_slot = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS) ? &g.vbos[g.va_texcoord.vbo_id] : NULL;
    VBOSlot *c_slot = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS) ? &g.vbos[g.va_color.vbo_id] : NULL;

    if (v_slot && vbo_is_packed_ptc(v_slot) &&
        !packed_ptc_attr_compatible(g.va_vertex.size, g.va_vertex.type, g.va_vertex.stride, g.va_vertex.pointer, 0, 3)) {
        vbo_convert_slot_to_raw(v_slot);
    }
    if (t_slot && t_slot != v_slot && vbo_is_packed_ptc(t_slot) &&
        !packed_ptc_attr_compatible(g.va_texcoord.size, g.va_texcoord.type, g.va_texcoord.stride, g.va_texcoord.pointer, 12, 2)) {
        vbo_convert_slot_to_raw(t_slot);
    }
    if (c_slot && c_slot != v_slot && c_slot != t_slot && vbo_is_packed_ptc(c_slot) &&
        !packed_ptc_attr_compatible(g.va_color.size, g.va_color.type, g.va_color.stride, g.va_color.pointer, 20, 4)) {
        vbo_convert_slot_to_raw(c_slot);
    }

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
        uint8_t packed_vertex[24];
        const VBOSlot *packed_vertex_slot = NULL;

        // Vertex
        if (g.va_vertex.enabled && v_slot && vbo_is_packed_ptc(v_slot)) {
            vbo_decode_packed_ptc_vertex(v_slot, src_index, packed_vertex);
            packed_vertex_slot = v_slot;
            memcpy(dst, packed_vertex, 12);
        } else if (g.va_vertex.enabled && src_v && (uintptr_t)src_v > 0x1000) {
            float pos[3] = {0.0f, 0.0f, 0.0f};
            read_vertex_attrib_float(pos, src_v + src_index * p_str, g.va_vertex.size, g.va_vertex.type);
            memcpy(dst, pos, 12);
        } else memset(dst, 0, 12);

        // TexCoord
        if (g.va_texcoord.enabled && t_slot && vbo_is_packed_ptc(t_slot)) {
            if (packed_vertex_slot != t_slot) {
                vbo_decode_packed_ptc_vertex(t_slot, src_index, packed_vertex);
                packed_vertex_slot = t_slot;
            }
            memcpy(dst + 12, packed_vertex + 12, 8);
        } else if (g.va_texcoord.enabled && src_t && (uintptr_t)src_t > 0x1000) {
            float tc[2] = {0.0f, 0.0f};
            read_vertex_attrib_float(tc, src_t + src_index * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size, g.va_texcoord.type);
            memcpy(dst + 12, tc, 8);
        } else memset(dst + 12, 0, 8);

        // Color
        if (g.va_color.enabled && c_slot && vbo_is_packed_ptc(c_slot)) {
            if (packed_vertex_slot != c_slot) {
                vbo_decode_packed_ptc_vertex(c_slot, src_index, packed_vertex);
                packed_vertex_slot = c_slot;
            }
            memcpy(dst + 20, packed_vertex + 20, 4);
            if (g.va_color.size == 3) dst[23] = 255;
        } else if (g.va_color.enabled && src_c && (uintptr_t)src_c > 0x1000) {
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

    if (g.shade_model == GL_FLAT) {
        int verts_per_prim = (mode == GL_TRIANGLES || mode == GL_TRIANGLE_FAN || mode == GL_TRIANGLE_STRIP) ? 3
                           : (mode == GL_QUADS) ? 4 : 0;
        if (verts_per_prim > 0) {
            if (mode == GL_TRIANGLES || mode == GL_QUADS) {
                for (int i = 0; i + verts_per_prim <= count; i += verts_per_prim) {
                    uint8_t *provoking = dst_start + (i + verts_per_prim - 1) * 24 + 20;
                    for (int j = 0; j < verts_per_prim - 1; j++)
                        memcpy(dst_start + (i + j) * 24 + 20, provoking, 4);
                }
            } else if (mode == GL_TRIANGLE_STRIP) {
                for (int i = 2; i < count; i++) {
                    uint8_t *provoking = dst_start + i * 24 + 20;
                    memcpy(dst_start + (i - 2) * 24 + 20, provoking, 4);
                    memcpy(dst_start + (i - 1) * 24 + 20, provoking, 4);
                }
            } else if (mode == GL_TRIANGLE_FAN) {
                for (int i = 2; i < count; i++) {
                    uint8_t *provoking = dst_start + i * 24 + 20;
                    memcpy(dst_start + 20, provoking, 4);
                    memcpy(dst_start + (i - 1) * 24 + 20, provoking, 4);
                }
            }
        }
    }

    if (tiled_slot) {
        draw_tiled_batches(mode, prim, tiled_slot, dst_start, count);
    } else {
        GSPGPU_FlushDataCache(dst_start, req_size);
        draw_packed_run(mode, prim, dst_start, count);
    }

    cleanup_vbo_stream();
}

GLenum glGetError(void) { GLenum e = g.last_error; g.last_error = GL_NO_ERROR; return e; }

void glFlush(void) { }
void glFinish(void) { }
