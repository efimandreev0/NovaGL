//
// created by efimandreev0 on 05.04.2026.
//

#ifndef NOVAGL_CONTEXT_H
#define NOVAGL_CONTEXT_H

/* Shader IDs for g.active_shader. Order doesn't matter, just kept stable. */
#define NOVA_SHADER_FULL       0
#define NOVA_SHADER_BASIC      1
#define NOVA_SHADER_TEXMTX     2
#define NOVA_SHADER_CLIPSPACE  3

// Internal NovaGL header: safe to drag in <3ds.h> here because no NovaGL caller
// includes context.h (it lives under src/). All NovaGL .c files reach this via
// utils.h or by including context.h directly, so this is where the libctru/citro3d
// definitions get pulled in.
#include <3ds.h>
#include <citro3d.h>
#include "NovaGL.h"


typedef struct {
    C3D_Tex tex;
    int allocated;

    int width, height;
    int pot_w, pot_h;

    int orig_width, orig_height;

    GPU_TEXCOLOR fmt;
    int min_filter;
    int mag_filter;
    int wrap_s;
    int wrap_t;
    int in_use;

    int is_solid_optimized; //if texture have only 1 color -> creating 1x1 stub.

    // Mipmap state. Arx sets GL_GENERATE_MIPMAP before glTexImage2D when it
    // wants automatic mip generation, and GL_TEXTURE_MAX_LEVEL=0 for textures
    // that must never sample beyond level 0. We honour both — PICA200 with
    // unset MAX_LEVEL on a single-level C3D_Tex reads garbage memory for the
    // missing mips, which manifests as tiled-repeat corruption on screen.
    int generate_mipmap;   //!< glTexParameteri(GL_GENERATE_MIPMAP, GL_TRUE) was set
    int max_level;         //!< glTexParameteri(GL_TEXTURE_MAX_LEVEL, N); -1 = unset
    int has_mipmap;        //!< the underlying C3D_Tex actually has mip levels
} TexSlot;

typedef struct {
    //struct data
    void *data;
    int size;
    int capacity;
    int allocated;

    int in_use;
    int is_stream;
    uint8_t storage_kind;
    uint8_t storage_stride;
} VBOSlot;

#define NOVA_MAX_FBOS 64

typedef struct {
    int in_use;
    C3D_RenderTarget *target;
    GLuint color_tex_id;
} FBOSlot;

typedef enum {
    DL_OP_TRANSLATE,
    DL_OP_COLOR3F,
    DL_OP_NONE,
} DLOpType;

typedef struct {
    DLOpType type;
    float args[4];
} DLOp;

typedef struct {
    DLOp ops[NOVA_DL_MAX_OPS];
    int count;
    int used;
} DisplayList;

extern struct NovaState {
    //C3D targets
    C3D_RenderTarget *render_target_top; // Left eye
    C3D_RenderTarget *render_target_top_right; // Right eye
    C3D_RenderTarget *render_target_bot;
    C3D_RenderTarget *current_target;

    //Shader stuff
    /* Full FFP shader: separate proj+modelview dp4 chains, fog, texmtx. */
    DVLB_s *shader_dvlb;
    shaderProgram_s shader_program;
    int uLoc_projection;
    int uLoc_modelview;
    int uLoc_texmtx;       /* GL_TEXTURE matrix stack -> vertex shader */
    int uLoc_fogparams;

    /* Basic fast-path shader: single combined MVP dp4 chain, no fog math,
     * identity tex matrix. Selected when g.fog_enabled == 0 AND
     * g.tex_mtx_is_identity. Saves 4 dp4 + 2 dp4 + ~8 ALU ops per vertex. */
    DVLB_s *shader_basic_dvlb;
    shaderProgram_s shader_basic_program;
    int uLoc_mvp_basic;

    /* texmtx variant: MVP combined + tex matrix applied (no fog).
     * Selected when fog OFF and tex_mtx_is_identity == 0. */
    DVLB_s *shader_texmtx_dvlb;
    shaderProgram_s shader_texmtx_program;
    int uLoc_mvp_texmtx;
    int uLoc_texmtx_texmtx;

    /* clipspace variant: raw passthrough — position is already in clip
     * space. Activated by novaBeginClipSpace2D() / Begin/End API. */
    DVLB_s *shader_clipspace_dvlb;
    shaderProgram_s shader_clipspace_program;
    int clipspace_mode_enabled;

    /* Currently bound program selector. -1 forces re-bind on the next
     * apply_gpu_state (used on init / cache invalidate / clipspace toggle). */
    int active_shader; /* NOVA_SHADER_* constant */

    /* Combined MVP for the basic shader. Rebuilt on CPU when proj_dirty
     * or mv_dirty fires, so the shader only does one dp4 chain. */
    C3D_Mtx mvp_combined;
    /* Cached final projection (= tilt * adjusted_projection). Only rebuilt
     * when proj_dirty or stereo slider toggles. Avoids ~16 muls per draw
     * when only modelview changed under the basic shader. */
    C3D_Mtx final_proj_cached;
    int final_proj_cached_valid;

    /* tex matrix identity tracking: 1 means the current GL_TEXTURE stack
     * top is identity, so the basic shader (which skips texmtx) is safe.
     * The per-slot array tracks identity status across push/pop so popping
     * back to an identity slot restores the fast path. */
    int tex_mtx_is_identity;
    int tex_mtx_identity_stack[NOVA_MATRIX_STACK];

    C3D_AttrInfo attr_info;
    //Matrix stuff
    int matrix_mode;
    C3D_Mtx proj_stack[NOVA_MATRIX_STACK];
    int proj_sp;
    C3D_Mtx mv_stack[NOVA_MATRIX_STACK];
    int mv_sp;
    C3D_Mtx tex_stack[NOVA_MATRIX_STACK];
    int tex_sp;
    int matrices_dirty;
    /* Per-stack dirty bits. matrices_dirty stays as the OR for callers that
     * need a single dirty signal (e.g. fog distance recompute), but the
     * individual flags let apply_gpu_state skip the projection-mangle (tilt
     * multiply + Z-range fixup + uniform upload) when only modelview or
     * texture matrix changed. vitaGL has the same split via mvp_modified. */
    int proj_dirty;
    int mv_dirty;
    int tex_mtx_dirty;

    float cur_color[4];

    //Textures stuff
    TexSlot textures[NOVA_MAX_TEXTURES];
    int active_texture_unit;
    GLuint bound_texture[3];
    int texture_2d_enabled_unit[3];
    int tex_next_id;

    VBOSlot vbos[NOVA_MAX_VBOS];
    GLuint bound_array_buffer;
    GLuint bound_element_array_buffer;
    int vbo_next_id;

    struct {
        int enabled;
        GLint size;
        GLenum type;
        GLsizei stride;
        const void *pointer;
        GLuint vbo_id;
    } va_vertex, va_texcoord, va_color, va_normal;

    int depth_test_enabled;
    GLenum depth_func;
    GLboolean depth_mask;
    int blend_enabled;
    GLenum blend_src, blend_dst;
    int alpha_test_enabled;
    GLenum alpha_func;
    float alpha_ref;
    int cull_face_enabled;
    GLenum cull_face_mode;
    GLenum front_face;

    int scissor_test_enabled;
    GLint scissor_x, scissor_y;
    GLsizei scissor_w, scissor_h;

    GLenum shade_model;

    int fog_enabled;
    GLenum fog_mode;
    float fog_start, fog_end, fog_density;
    float fog_color[4];
    C3D_FogLut fog_lut;
    int fog_dirty;

    float clear_r, clear_g, clear_b, clear_a;
    float clear_depth;
    GLboolean color_mask_r, color_mask_g, color_mask_b, color_mask_a;
    float polygon_offset_factor, polygon_offset_units;

    GLint vp_x, vp_y;
    GLsizei vp_w, vp_h;

    DisplayList dl_store[NOVA_DISPLAY_LISTS];
    int dl_recording;
    GLuint dl_next_base;

    GLenum last_error;
    int initialized;
    void *client_array_buf;
    int client_array_buf_size;
    int client_array_buf_offset;

    void *index_buf;
    int index_buf_size;
    int index_buf_offset;

    int tev_dirty;
    int last_tex_state;
    GLint tex_env_mode[3];
    int client_active_texture_unit;
    GLint pack_alignment;
    GLint unpack_alignment;

    int polygon_offset_fill_enabled;
    GLfloat depth_near;
    GLfloat depth_far;

    void *tex_staging;
    int tex_staging_size;

    uint16_t *static_quad_indices;
    int static_quad_count;

    bool RenderTargetBottom;

    GLint tex_env_combine_rgb[3];
    GLint tex_env_src0_rgb[3];
    GLint tex_env_src1_rgb[3];
    GLint tex_env_src2_rgb[3];
    GLint tex_env_operand0_rgb[3];
    GLint tex_env_operand1_rgb[3];
    GLint tex_env_operand2_rgb[3];

    /* Alpha combine state (GL_COMBINE_ALPHA path) */
    GLint tex_env_combine_alpha[3];
    GLint tex_env_src0_alpha[3];
    GLint tex_env_src1_alpha[3];
    GLint tex_env_src2_alpha[3];
    GLint tex_env_operand0_alpha[3];
    GLint tex_env_operand1_alpha[3];
    GLint tex_env_operand2_alpha[3];

    int line_smooth_enabled;

    int current_eye; // 0 = left, 1 = right
    float stereo_depth;

    /* Minimal FBO support: a pool of render targets that wrap a NovaGL texture.
     * glGenFramebuffers hands out IDs into this pool; glBindFramebuffer swaps
     * g.current_target between the screen targets and an FBO's C3D target. */
    FBOSlot fbos[NOVA_MAX_FBOS];
    GLuint bound_fbo; // 0 = screen

    /* State revision: bumped on any state mutation that affects what
     * apply_gpu_state would push to the GPU. apply_gpu_state stores the
     * last-applied revision and early-outs when nothing's changed since.
     * vitaGL uses a similar dirty-tracking pattern. */
    uint32_t state_rev;
    uint32_t state_rev_applied;

    /* novaDrawObjects fast-path: pre-validated pointers for a PTC layout
     * (position 3f@0, texcoord 2f@12, color 4ub@20, stride 24).
     * Skips the ~12 conditionals in nova_draw_internal's fast-path detector. */
    GLuint fast_vbo_id;
    GLuint fast_vbo_offset;
    GLuint fast_idx_vbo_id;
    GLenum fast_idx_type;
} g;

void nova_fbo_gc_collect(void);

#endif //NOVAGL_CONTEXT_H
