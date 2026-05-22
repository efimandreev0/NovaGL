/*
 * NovaGL.c - OpenGL ES 1.1 -> Citro3D Translation Layer Implementation
 */

#include "NovaGL.h"
#include "utils.h"
#include "context.h"
#include <math.h>

#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>

#include "NovaGL_shader_shbin.h"
#include "NovaGL_shader_basic_shbin.h"

/* Moved out of NovaGL.h: depends on GX_TRANSFER_* from <3ds.h>, which we now keep
 * confined to the .c side so the public header doesn't leak libctru's `Thread`
 * typedef into C++ callers. */
#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

struct NovaState g;

void nova_init() {
    nova_init_ex(NOVA_CMD_BUF_SIZE, 2 * 1024 * 1024, 512 * 1024, 512 * 1024);
}

void nova_init_ex(int cmd_buf_size, int client_array_buf_size, int index_buf_size, int tex_staging_size) {
    memset(&g, 0, sizeof(g));

    //if (client_array_buf_size < 8 * 1024 * 1024) {
    //    client_array_buf_size = 8 * 1024 * 1024;
    //}
    //if (index_buf_size < 512 * 1024) {
    //    index_buf_size = 512 * 1024;
    //}

    C3D_Init(cmd_buf_size);

    gfxSet3D(true);

    g.render_target_top = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                 GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    g.render_target_top_right = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                       GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_top_right, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

    g.current_target = g.render_target_top;

    g.render_target_bot = C3D_RenderTargetCreate(NOVA_SCREEN_BOTTOM_W, NOVA_SCREEN_BOTTOM_H,
                                                 GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_bot, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    g.shader_dvlb = DVLB_ParseFile((u32 *) NovaGL_shader_shbin, NovaGL_shader_shbin_size);

    if (g.shader_dvlb) {
        shaderProgramInit(&g.shader_program);
        shaderProgramSetVsh(&g.shader_program, &g.shader_dvlb->DVLE[0]);

        g.uLoc_projection = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "projection");
        g.uLoc_modelview = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "modelview");
        g.uLoc_texmtx    = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "texmtx");
        g.uLoc_fogparams = shaderInstanceGetUniformLocation(g.shader_program.vertexShader, "fogparams");
    }

    g.shader_basic_dvlb = DVLB_ParseFile((u32 *) NovaGL_shader_basic_shbin, NovaGL_shader_basic_shbin_size);
    if (g.shader_basic_dvlb) {
        shaderProgramInit(&g.shader_basic_program);
        shaderProgramSetVsh(&g.shader_basic_program, &g.shader_basic_dvlb->DVLE[0]);
        g.uLoc_mvp_basic = shaderInstanceGetUniformLocation(g.shader_basic_program.vertexShader, "mvp");
    }

    /* Start on the full shader; apply_gpu_state's selector will switch to
     * basic on the first draw if the state qualifies. */
    C3D_BindProgram(&g.shader_program);
    g.active_shader = 0;
    g.tex_mtx_is_identity = 1; /* tex stack starts identity */
    for (int i = 0; i < NOVA_MATRIX_STACK; i++) g.tex_mtx_identity_stack[i] = 1;

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
    g.static_quad_indices = (uint16_t *) linearAlloc(g.static_quad_count * 6 * sizeof(uint16_t));
    if (g.static_quad_indices) {
        for (int q = 0; q < g.static_quad_count; q++) {
            uint16_t base = q * 4;
            g.static_quad_indices[q * 6 + 0] = base + 0;
            g.static_quad_indices[q * 6 + 1] = base + 1;
            g.static_quad_indices[q * 6 + 2] = base + 2;
            g.static_quad_indices[q * 6 + 3] = base + 0;
            g.static_quad_indices[q * 6 + 4] = base + 2;
            g.static_quad_indices[q * 6 + 5] = base + 3;
        }
        GSPGPU_FlushDataCache(g.static_quad_indices, g.static_quad_count * 6 * sizeof(uint16_t));
    }

    for (int i = 0; i < 3; i++) {
        g.tex_env_combine_rgb[i] = GL_MODULATE;
        g.tex_env_src0_rgb[i] = GL_TEXTURE;
        g.tex_env_src1_rgb[i] = GL_PREVIOUS;
        g.tex_env_src2_rgb[i] = GL_CONSTANT;
        g.tex_env_operand0_rgb[i] = GL_SRC_COLOR;
        g.tex_env_operand1_rgb[i] = GL_SRC_COLOR;
        g.tex_env_operand2_rgb[i] = GL_SRC_ALPHA;
    }

    g.stereo_depth = 0.05f;

    g.matrix_mode = GL_MODELVIEW;
    Mtx_Identity(&g.proj_stack[0]);
    Mtx_Identity(&g.mv_stack[0]);
    Mtx_Identity(&g.tex_stack[0]);
    g.proj_sp = 0;
    g.mv_sp = 0;
    g.tex_sp = 0;
    g.matrices_dirty = 1;
    g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;

    g.cur_color[0] = 1.0f;
    g.cur_color[1] = 1.0f;
    g.cur_color[2] = 1.0f;
    g.cur_color[3] = 1.0f;

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
    g.fog_start = 0.0f;
    g.fog_end = 1.0f;
    g.fog_density = 1.0f;
    g.fog_color[0] = g.fog_color[1] = g.fog_color[2] = 0.0f;
    g.fog_color[3] = 1.0f;
    g.fog_dirty = 1;

    g.vp_x = 0;
    g.vp_y = 0;
    g.vp_w = NOVA_SCREEN_W;
    g.vp_h = NOVA_SCREEN_H;

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

    (void) tex_staging_size;
    g.tex_staging_size = 0;
    g.tex_staging = NULL;

    g.initialized = 1;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;

    C3D_FrameDrawOn(g.render_target_top);
    g.current_target = g.render_target_top;
}

int novaGetEyeCount(void) {
    return (osGet3DSliderState() > 0.0f) ? 2 : 1;
}

void novaSet3DDepth(float depth) {
    g.stereo_depth = depth;
}

void novaBeginEye(int eye) {
    g.current_eye = eye;

    nova_set_render_target(eye);

    g.matrices_dirty = 1;
    g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
    /* Stereo eye shift goes into final_proj — invalidate the cached value
     * so the next apply_gpu_state rebuilds with the new per-eye offset. */
    g.final_proj_cached_valid = 0;
}

void novaSwapBuffers(void) {
    C3D_FrameEnd(0);
    nova_fbo_gc_collect();

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;

    C3D_FrameDrawOn(g.render_target_top);
    g.current_target = g.render_target_top;
}

void nova_fini(void) {
    if (!g.initialized) return;

    // Когда nova_fini зовут из главного цикла после break, мы находимся
    // МЕЖДУ FrameBegin (последний swap) и невыполненным FrameEnd — то есть у
    // GPU ещё открыт кадр на наших render_target_top/_right/_bot. Если сразу
    // зовём C3D_RenderTargetDelete, оно лезет в renderqueue.c:364, видит
    // pending transfer на удаляемом таргете и валит процесс. Сначала закрываем
    // кадр и ждём GPU.
    C3D_FrameEnd(0);
    gspWaitForP3D();

    if (g.client_array_buf) linearFree(g.client_array_buf);
    if (g.index_buf) linearFree(g.index_buf);

    if (g.tex_staging) {
        free(g.tex_staging);
        g.tex_staging = NULL;
        g.tex_staging_size = 0;
    }
    for (int i = 0; i < NOVA_MAX_TEXTURES; i++) {
        if (g.textures[i].allocated) C3D_TexDelete(&g.textures[i].tex);
    }
    for (int i = 0; i < NOVA_MAX_VBOS; i++) {
        if (g.vbos[i].allocated && g.vbos[i].data) linearFree(g.vbos[i].data);
    }

    if (g.static_quad_indices) {
        linearFree(g.static_quad_indices);
        g.static_quad_indices = NULL;
        g.static_quad_count = 0;
    }

    if (g.shader_dvlb) {
        shaderProgramFree(&g.shader_program);
        DVLB_Free(g.shader_dvlb);
    }
    if (g.shader_basic_dvlb) {
        shaderProgramFree(&g.shader_basic_program);
        DVLB_Free(g.shader_basic_dvlb);
    }

    nova_fbo_gc_collect();

    C3D_RenderTargetDelete(g.render_target_top);
    C3D_RenderTargetDelete(g.render_target_top_right);
    C3D_RenderTargetDelete(g.render_target_bot);
    C3D_Fini();
    g.initialized = 0;
}

void nova_set_render_target(int target_mode) {
    if (target_mode == 1) {
        C3D_FrameDrawOn(g.render_target_top_right);
        g.current_target = g.render_target_top_right;
    } else if (target_mode == 2) {
        C3D_FrameDrawOn(g.render_target_bot);
        g.current_target = g.render_target_bot;
    } else {
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
    }
}

static int primitive_vertex_count(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES: return 3;
        case GL_QUADS: return 4;
        case GL_LINES: return 2;
        default: return 0;
    }
}

static void draw_packed_run(GLenum mode, GPU_Primitive_t prim, uint8_t *base, int count, int stride, int pos_elements) {
    nova_setup_attr_info(pos_elements);
    nova_setup_buf_info(base, stride);

    if (mode == GL_QUADS) {
        // === [DIAG] alternate quad path: draw as TRIANGLE_FAN ===
        // Original path uses C3D_DrawElements with persistent static_quad_indices.
        // If that index buffer is being cached/aliased by PICA200 we'd see ghosting.
        // For a single 4-vertex quad, TRIANGLE_FAN with the same vertex order
        // produces the exact same two triangles (0-1-2, 0-2-3) without any indices.
        #ifdef NOVAGL_QUAD_AS_FAN
        if (count == 4) {
            C3D_DrawArrays(GPU_TRIANGLE_FAN, 0, 4);
        } else {
            draw_emulated_quads(count);
        }
        #else
        draw_emulated_quads(count);
        #endif
    } else {
        C3D_DrawArrays(prim, 0, count);
    }
}

static int packed_ptc_attr_compatible(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer,
                                      int expected_offset, int max_size) {
    int effective_stride = stride ? stride : 24;
    return type == (expected_offset == 20 ? GL_UNSIGNED_BYTE : GL_FLOAT) &&
           effective_stride == 24 &&
           (int) (uintptr_t) pointer == expected_offset &&
           size > 0 &&
           size <= max_size;
}

void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices) {
    if (count <= 0) return;
    if (is_elements && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_INT) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }

    if (g.va_vertex.enabled && g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS) {
        if (!g.vbos[g.va_vertex.vbo_id].allocated || g.vbos[g.va_vertex.vbo_id].data == NULL) return;
    }

    const uint8_t *idx_src = NULL;
    if (is_elements) {
        if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < NOVA_MAX_VBOS) {
            if (!g.vbos[g.bound_element_array_buffer].allocated || g.vbos[g.bound_element_array_buffer].data == NULL)
                return;
            idx_src = (const uint8_t *) g.vbos[g.bound_element_array_buffer].data + (uintptr_t) indices;
        } else {
            idx_src = (const uint8_t *) indices;
        }
        if (!idx_src) return;
    }

    apply_gpu_state();
    GPU_Primitive_t prim = gl_to_gpu_primitive(mode);
    if (mode == GL_LINES || mode == GL_LINE_STRIP) prim = GPU_TRIANGLES;

    if (!is_elements &&
        g.va_vertex.enabled && g.va_vertex.vbo_id > 0 &&
        g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].allocated &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 && (uintptr_t) g.va_vertex.
        pointer == 0 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id && g.va_texcoord.size == 2 && g.va_texcoord.
        type == GL_FLOAT && g.va_texcoord.stride == 24 && (uintptr_t) g.va_texcoord.pointer == 12 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id && g.va_color.size == 4 && g.va_color.type ==
        GL_UNSIGNED_BYTE && g.va_color.stride == 24 && (uintptr_t) g.va_color.pointer == 20) {
        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        int available = vbo->size - first * 24;
        if (available < count * 24) {
            count = available / 24;
            if (count <= 0) return;
        }
        int req_size = count * 24;

        if (vbo_is_packed_ptc(vbo)) {
            if (req_size > g.client_array_buf_size) {
                g.last_error = GL_OUT_OF_MEMORY;
                return;
            }
            uint8_t *dst_start = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size,
                                                               g.client_array_buf_size);
            vbo_decode_packed_ptc_span(vbo, first, count, dst_start);
            GSPGPU_FlushDataCache(dst_start, req_size);
            draw_packed_run(mode, prim, dst_start, count, 24, 3);
        } else {

            uint8_t *base = (uint8_t *) vbo->data + first * 24;
            draw_packed_run(mode, prim, base, count, 24, 3);
        }
        cleanup_vbo_stream();
        return;
    }

    if (is_elements &&
        (type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_BYTE) &&
        (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN) &&
        g.shade_model != GL_FLAT &&
        g.va_vertex.enabled && g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS &&
        g.vbos[g.va_vertex.vbo_id].allocated && !vbo_is_packed_ptc(&g.vbos[g.va_vertex.vbo_id]) &&
        g.va_vertex.size == 3 && g.va_vertex.type == GL_FLOAT && g.va_vertex.stride == 24 &&
        g.va_texcoord.enabled && g.va_texcoord.vbo_id == g.va_vertex.vbo_id &&
        g.va_texcoord.size == 2 && g.va_texcoord.type == GL_FLOAT && g.va_texcoord.stride == 24 &&
        g.va_color.enabled && g.va_color.vbo_id == g.va_vertex.vbo_id &&
        g.va_color.size == 4 && g.va_color.type == GL_UNSIGNED_BYTE && g.va_color.stride == 24 &&
        (uintptr_t) g.va_texcoord.pointer == (uintptr_t) g.va_vertex.pointer + 12 &&
        (uintptr_t) g.va_color.pointer    == (uintptr_t) g.va_vertex.pointer + 20) {

        VBOSlot *vbo = &g.vbos[g.va_vertex.vbo_id];
        int elem_size = (type == GL_UNSIGNED_SHORT) ? 2 : 1;
        const void *gpu_indices = NULL;

        if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < NOVA_MAX_VBOS) {
            gpu_indices = idx_src;
        } else {
            int ibytes = count * elem_size;
            if (ibytes <= g.index_buf_size) {
                void *staged = linear_alloc_ring(g.index_buf, &g.index_buf_offset,
                                                 ibytes, g.index_buf_size);
                memcpy(staged, idx_src, ibytes);
                GSPGPU_FlushDataCache(staged, ibytes);
                gpu_indices = staged;
            }
        }

        if (gpu_indices) {
            uint8_t *base = (uint8_t *) vbo->data + (uintptr_t) g.va_vertex.pointer;
            nova_setup_attr_info(3);
            nova_setup_buf_info(base, 24);
            C3D_DrawElements(prim, count,
                             (type == GL_UNSIGNED_SHORT) ? C3D_UNSIGNED_SHORT : C3D_UNSIGNED_BYTE,
                             gpu_indices);
            cleanup_vbo_stream();
            return;
        }
    }

    // Force vec3 position even when Arx supplies a 4-component TexturedVertex.
    // The PICA200 shader (NovaGL_shader.pica) treats inPosition as a vec4 register
    // (dp4 with modelview/projection rows). When loader 0 advertises 4 floats,
    // the per-attribute offsets in BufInfo shift by 4 bytes vs. the layout the
    // shader/AttrInfo init time was set up for (immediate.c and the init path
    // both pin loader 0 to 3 floats). The mismatch reads UV/color from wrong
    // offsets and produces the tiled-repeat / horizontal-shear corruption seen
    // on splash and menu screens. Arx's TexturedVertex.w is RHW, which is 1.0
    // for the 2D ortho path used by the menu/splash (verified via matrix dump),
    // so dropping .w is safe here — pos.w gets implicit 1.0 in the vec4 register.
    int pos_elements = 3;
    int pos_bytes = pos_elements * 4;
    int internal_stride = pos_bytes + 12;
    int col_offset = pos_bytes + 8;

    int req_size = count * internal_stride;
    if (req_size > g.client_array_buf_size) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }

    uint8_t *dst = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size,
                                                 g.client_array_buf_size);
    uint8_t *dst_start = dst;

    VBOSlot *v_slot = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS)
                          ? &g.vbos[g.va_vertex.vbo_id]
                          : NULL;
    VBOSlot *t_slot = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS)
                          ? &g.vbos[g.va_texcoord.vbo_id]
                          : NULL;
    VBOSlot *c_slot = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS) ? &g.vbos[g.va_color.vbo_id] : NULL;

    if (v_slot && vbo_is_packed_ptc(v_slot) && !packed_ptc_attr_compatible(
            g.va_vertex.size, g.va_vertex.type, g.va_vertex.stride, g.va_vertex.pointer, 0,
            3)) vbo_convert_slot_to_raw(v_slot);
    if (t_slot && t_slot != v_slot && vbo_is_packed_ptc(t_slot) && !packed_ptc_attr_compatible(
            g.va_texcoord.size, g.va_texcoord.type, g.va_texcoord.stride, g.va_texcoord.pointer, 12,
            2)) vbo_convert_slot_to_raw(t_slot);
    if (c_slot && c_slot != v_slot && c_slot != t_slot && vbo_is_packed_ptc(c_slot) && !packed_ptc_attr_compatible(
            g.va_color.size, g.va_color.type, g.va_color.stride, g.va_color.pointer, 20,
            4)) vbo_convert_slot_to_raw(c_slot);

    int p_str = calc_stride(g.va_vertex.stride, g.va_vertex.size, g.va_vertex.type);
    int t_str = calc_stride(g.va_texcoord.stride, g.va_texcoord.size, g.va_texcoord.type);
    int c_str = calc_stride(g.va_color.stride, g.va_color.size, g.va_color.type);

    const uint8_t *src_v = (g.va_vertex.vbo_id > 0 && g.va_vertex.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_vertex.vbo_id].
                            allocated)
                               ? (const uint8_t *) g.vbos[g.va_vertex.vbo_id].data + (uintptr_t) g.va_vertex.pointer
                               : (const uint8_t *) g.va_vertex.pointer;
    const uint8_t *src_t = (g.va_texcoord.vbo_id > 0 && g.va_texcoord.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_texcoord.
                                vbo_id].allocated)
                               ? (const uint8_t *) g.vbos[g.va_texcoord.vbo_id].data + (uintptr_t) g.va_texcoord.pointer
                               : (const uint8_t *) g.va_texcoord.pointer;
    const uint8_t *src_c = (g.va_color.vbo_id > 0 && g.va_color.vbo_id < NOVA_MAX_VBOS && g.vbos[g.va_color.vbo_id].
                            allocated)
                               ? (const uint8_t *) g.vbos[g.va_color.vbo_id].data + (uintptr_t) g.va_color.pointer
                               : (const uint8_t *) g.va_color.pointer;

    uint8_t def_col[4] = {
        (uint8_t) (g.cur_color[0] * 255.0f), (uint8_t) (g.cur_color[1] * 255.0f),
        (uint8_t) (g.cur_color[2] * 255.0f), (uint8_t) (g.cur_color[3] * 255.0f)
    };

    for (int i = 0; i < count; i++) {
        int src_index = is_elements
                            ? (type == GL_UNSIGNED_INT
                                   ? (int) ((const uint32_t *) idx_src)[i]
                                   : type == GL_UNSIGNED_SHORT
                                         ? ((const uint16_t *) idx_src)[i]
                                         : ((const uint8_t *) idx_src)[i])
                            : (first + i);
        uint8_t packed_vertex[24];
        const VBOSlot *packed_vertex_slot = NULL;

        // Vertex
        if (g.va_vertex.enabled && v_slot && vbo_is_packed_ptc(v_slot)) {
            vbo_decode_packed_ptc_vertex(v_slot, src_index, packed_vertex);
            packed_vertex_slot = v_slot;
            memcpy(dst, packed_vertex, pos_bytes);
        } else if (g.va_vertex.enabled && src_v && (uintptr_t) src_v > 0x1000) {
            float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            read_vertex_attrib_float(pos, src_v + src_index * p_str, g.va_vertex.size, g.va_vertex.type);
            memcpy(dst, pos, pos_bytes);
        } else memset(dst, 0, pos_bytes);

        // TexCoord
        if (g.va_texcoord.enabled && t_slot && vbo_is_packed_ptc(t_slot)) {
            if (packed_vertex_slot != t_slot) {
                vbo_decode_packed_ptc_vertex(t_slot, src_index, packed_vertex);
                packed_vertex_slot = t_slot;
            }
            memcpy(dst + pos_bytes, packed_vertex + 12, 8);
        } else if (g.va_texcoord.enabled && src_t && (uintptr_t) src_t > 0x1000) {
            float tc[2] = {0.0f, 0.0f};
            read_vertex_attrib_float(tc, src_t + src_index * t_str, g.va_texcoord.size > 2 ? 2 : g.va_texcoord.size,
                                     g.va_texcoord.type);
            memcpy(dst + pos_bytes, tc, 8);
        } else memset(dst + pos_bytes, 0, 8);

        // Color
        if (g.va_color.enabled && c_slot && vbo_is_packed_ptc(c_slot)) {
            if (packed_vertex_slot != c_slot) {
                vbo_decode_packed_ptc_vertex(c_slot, src_index, packed_vertex);
                packed_vertex_slot = c_slot;
            }
            memcpy(dst + col_offset, packed_vertex + 20, 4);
            if (g.va_color.size == 3) dst[col_offset + 3] = 255;
        } else if (g.va_color.enabled && src_c && (uintptr_t) src_c > 0x1000) {
            const uint8_t *c_ptr = src_c + src_index * c_str;
            if (g.va_color.type == GL_UNSIGNED_BYTE || (g.va_color.type != GL_FLOAT)) {
                memcpy(dst + col_offset, c_ptr, g.va_color.size == 3 ? 3 : 4);
                if (g.va_color.size == 3) dst[col_offset + 3] = 255;
            } else {
                const float *cf = (const float *) c_ptr;
                dst[col_offset + 0] = (uint8_t) (clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 1] = (uint8_t) (clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 2] = (uint8_t) (clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 3] = g.va_color.size >= 4 ? (uint8_t) (clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
            }
        } else {
            memcpy(dst + col_offset, def_col, 4);
        }

        dst += internal_stride;
    }

    if (g.shade_model == GL_FLAT) {
        int verts_per_prim = (mode == GL_TRIANGLES || mode == GL_TRIANGLE_FAN || mode == GL_TRIANGLE_STRIP)
                                 ? 3
                                 : (mode == GL_QUADS)
                                       ? 4
                                       : 0;
        if (verts_per_prim > 0) {
            if (mode == GL_TRIANGLES || mode == GL_QUADS) {
                for (int i = 0; i + verts_per_prim <= count; i += verts_per_prim) {
                    uint8_t *provoking = dst_start + (i + verts_per_prim - 1) * internal_stride + col_offset;
                    for (int j = 0; j < verts_per_prim - 1; j++)
                        memcpy(dst_start + (i + j) * internal_stride + col_offset, provoking, 4);
                }
            } else if (mode == GL_TRIANGLE_STRIP) {
                for (int i = 2; i < count; i++) {
                    uint8_t *provoking = dst_start + i * internal_stride + col_offset;
                    memcpy(dst_start + (i - 2) * internal_stride + col_offset, provoking, 4);
                    memcpy(dst_start + (i - 1) * internal_stride + col_offset, provoking, 4);
                }
            } else if (mode == GL_TRIANGLE_FAN) {
                for (int i = 2; i < count; i++) {
                    uint8_t *provoking = dst_start + i * internal_stride + col_offset;
                    memcpy(dst_start + col_offset, provoking, 4);
                    memcpy(dst_start + (i - 1) * internal_stride + col_offset, provoking, 4);
                }
            }
        }
    }

    GSPGPU_FlushDataCache(dst_start, req_size);
    draw_packed_run(mode, prim, dst_start, count, internal_stride, pos_elements);
    cleanup_vbo_stream();
}

GLenum glGetError(void) {
    GLenum e = g.last_error;
    g.last_error = GL_NO_ERROR;
    return e;
}

void glFlush(void) {
    C3D_FrameSplit(0);
}

void glFinish(void) {
    C3D_FrameSplit(0);
    gspWaitForP3D();
}
