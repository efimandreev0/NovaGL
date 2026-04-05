//
// created by efimandreev0 on 05.04.2026.
//

#ifndef NOVAGL_CONTEXT_H
#define NOVAGL_CONTEXT_H
#include "NovaGL.h"

//Structures
typedef struct {
    C3D_Tex     tex;
    int         allocated;
    int         width, height;
    int         pot_w, pot_h;
    GPU_TEXCOLOR fmt;
    int         min_filter;
    int         mag_filter;
    int         wrap_s;
    int         wrap_t;
} TexSlot;

typedef struct {
    void   *data;
    int     size;
    int     capacity;
    int     allocated;
#ifdef NOVA_VBO_STREAM
    int     is_stream;
#endif
} VBOSlot;

typedef enum {
    DL_OP_TRANSLATE,
    DL_OP_COLOR3F,
    DL_OP_NONE,
} DLOpType;

typedef struct {
    DLOpType type;
    float    args[4];
} DLOp;

typedef struct {
    DLOp ops[NOVA_DL_MAX_OPS];
    int  count;
    int  used;
} DisplayList;

extern struct NovaState{
    C3D_RenderTarget *render_target_top;
    C3D_RenderTarget *render_target_bot;
    C3D_RenderTarget *current_target;
    DVLB_s           *shader_dvlb;
    shaderProgram_s   shader_program;
    int               uLoc_projection;
    int               uLoc_modelview;
    int               uLoc_fogparams;
    C3D_AttrInfo      attr_info;

    int        matrix_mode;
    C3D_Mtx    proj_stack[NOVA_MATRIX_STACK];
    int        proj_sp;
    C3D_Mtx    mv_stack[NOVA_MATRIX_STACK];
    int        mv_sp;
    C3D_Mtx    tex_stack[NOVA_MATRIX_STACK];
    int        tex_sp;
    int        matrices_dirty;

    float      cur_color[4];

    TexSlot    textures[NOVA_MAX_TEXTURES];
    GLuint     bound_texture;
    int        tex_next_id;
    int        texture_2d_enabled;

    VBOSlot    vbos[NOVA_MAX_VBOS];
    GLuint     bound_array_buffer;
    GLuint     bound_element_array_buffer;
    int        vbo_next_id;

    struct {
        int     enabled;
        GLint   size;
        GLenum  type;
        GLsizei stride;
        const void *pointer;
        GLuint  vbo_id;
    } va_vertex, va_texcoord, va_color, va_normal;

    int        depth_test_enabled;
    GLenum     depth_func;
    GLboolean  depth_mask;
    int        blend_enabled;
    GLenum     blend_src, blend_dst;
    int        alpha_test_enabled;
    GLenum     alpha_func;
    float      alpha_ref;
    int        cull_face_enabled;
    GLenum     cull_face_mode;
    GLenum     front_face;

    int        scissor_test_enabled;
    GLint      scissor_x, scissor_y;
    GLsizei    scissor_w, scissor_h;

    int        fog_enabled;
    GLenum     fog_mode;
    float      fog_start, fog_end, fog_density;
    float      fog_color[4];
    C3D_FogLut fog_lut;
    int        fog_dirty;

    float      clear_r, clear_g, clear_b, clear_a;
    float      clear_depth;
    GLboolean  color_mask_r, color_mask_g, color_mask_b, color_mask_a;
    float      polygon_offset_factor, polygon_offset_units;

    GLint      vp_x, vp_y;
    GLsizei    vp_w, vp_h;

    DisplayList dl_store[NOVA_DISPLAY_LISTS];
    int         dl_recording;
    GLuint      dl_next_base;

    GLenum     last_error;
    int        initialized;
    void      *client_array_buf;
    int        client_array_buf_size;
    int        client_array_buf_offset;

    void      *index_buf;
    int        index_buf_size;
    int        index_buf_offset;

    int        tev_dirty;
    int        last_tex_state;
    GLint      tex_env_mode;

    int polygon_offset_fill_enabled;
    GLfloat    depth_near;
    GLfloat    depth_far;

    void *tex_staging;
    int   tex_staging_size;

    uint16_t *static_quad_indices;
    int static_quad_count;

    bool RenderTargetBottom;
} g;

#endif //NOVAGL_CONTEXT_H