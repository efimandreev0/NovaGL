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
#define NOVA_SHADER_LIGHTING   4

// internal header, nobody outside src/ include this so we can pull 3ds.h here
#include <3ds.h>
#include <citro3d.h>
#include "NovaGL.h"

/* =========================================================================
 * NovaGL performance-hack resolution
 * =========================================================================
 * Drop-in, compile-time toggles inspired by vitaGL's speedhack flags, mapped
 * onto the PICA200/citro3d reality. None of them are on by default — every one
 * trades some OpenGL compliance / safety for raw CPU/GPU throughput, so you opt
 * in only once you know your port behaves. See README.md / api.md for the table.
 *
 * Master switch:
 *   -DNOVAGL_SPEEDHACKS=1   turns the safe "go fast" bundle on
 *                           (NO_DEBUG + DRAW_SPEEDHACK).
 *
 * Individual switches (also settable on their own):
 *   -DNOVAGL_NO_DEBUG=1       strip per-draw GL validation + per-vertex pointer
 *                             sanity guards. Big win on draw-call-bound scenes
 *                             on the 268 MHz ARM11; feed it bad enums/pointers
 *                             and it will happily march off a cliff.
 *   -DNOVAGL_DRAW_SPEEDHACK=1 tighter vertex-ring alignment (64B vs 128B → less
 *                             padding, fewer ring wraps/stalls) + trust caller
 *                             pointers in the interleave loop.
 *   -DNOVAGL_ASYNC_FRAME=1    non-blocking frame submission. Now a friendly
 *                             alias for double-buffering (NOVAGL_FRAME_BUFFERS=2,
 *                             resolved in NovaGL.c) — overlaps CPU frame N+1 with
 *                             GPU frame N (~+20–40% on GPU-bound scenes). This is
 *                             SAFE: the vertex/index rings and the texture/FBO
 *                             orphan GC are split into per-frame slots, so the GPU
 *                             never reads a buffer the CPU has overwritten/freed.
 *                             It is NOT in the SPEEDHACKS bundle only so frame
 *                             buffering stays an explicit, memory-cost choice (N×
 *                             the ring memory). Prefer the runtime
 *                             novaSetFrameBuffers(1/2/3) selector.
 * ====================================================================== */
#if defined(NOVAGL_SPEEDHACKS) && NOVAGL_SPEEDHACKS
#  ifndef NOVAGL_NO_DEBUG
#    define NOVAGL_NO_DEBUG 1
#  endif
#  ifndef NOVAGL_DRAW_SPEEDHACK
#    define NOVAGL_DRAW_SPEEDHACK 1
#  endif
/* NOVAGL_ASYNC_FRAME is intentionally NOT enabled by the bundle — frame
 * buffering depth is a separate, memory-cost choice (see novaSetFrameBuffers /
 * NOVAGL_FRAME_BUFFERS). */
#endif

/* DRAW_SPEEDHACK implies the 64-byte ring alignment that linear_alloc_ring
 * keys off of (utils.c). Defining it here keeps the one knob authoritative. */
#if defined(NOVAGL_DRAW_SPEEDHACK) && NOVAGL_DRAW_SPEEDHACK
#  ifndef NOVAGL_RING_ALIGN_64
#    define NOVAGL_RING_ALIGN_64 1
#  endif
#endif

/* Per-vertex "is this pointer plausibly real" guard used in the generic
 * interleave loop. The hacks assume the caller handed us valid client arrays,
 * so it collapses to a constant-true and the compiler drops the branch. */
#if (defined(NOVAGL_NO_DEBUG) && NOVAGL_NO_DEBUG) || \
    (defined(NOVAGL_DRAW_SPEEDHACK) && NOVAGL_DRAW_SPEEDHACK)
#  define NOVA_PTR_OK(p) (1)
#else
#  define NOVA_PTR_OK(p) ((uintptr_t)(p) > 0x1000)
#endif


typedef struct {
    C3D_Tex tex;
    int allocated;
    int bound_once; // set on first glBindTexture; glIsTexture tests this, not in_use

    int width, height;
    int pot_w, pot_h;

    int orig_width, orig_height;

    GPU_TEXCOLOR fmt;
    int min_filter;
    int mag_filter;
    int wrap_s;
    int wrap_t;
    int in_use;

    int is_solid_optimized; // texture with 1 color -> we make 1x1 to save vram

    // Set once the slot's storage has been re-homed in VRAM so it can serve as
    // a PICA render-target color buffer (see nova_texture_make_vram_target).
    // Stock glTexImage2D allocates linear RAM, which the GPU can't render into.
    int is_vram;

    // mipmap flags. if max_level unset PICA read garbage for missing mips and
    // you get ugly tiled repeat on screen, so we track it
    int generate_mipmap;   // GL_GENERATE_MIPMAP was set
    int max_level;         // GL_TEXTURE_MAX_LEVEL, -1 = not set
    int has_mipmap;        // real C3D_Tex have mips or no

    /* Cube-map support (GL_TEXTURE_CUBE_MAP). When is_cube is set, `tex` was
     * created with C3D_TexInitCube and its 6 face buffers live in `cube`
     * (embedded, not heap — so cube textures bypass the deferred orphan GC and
     * are deleted immediately). face_loaded tracks which of the 6 faces have
     * received data. */
    int is_cube;
    C3D_TexCube cube;
    int face_loaded[6];
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
    GLenum usage;
    uint8_t mapped;
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
    DL_OP_COLOR4F,
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


typedef struct {
    GLenum mode;          /* GL_OBJECT_LINEAR / GL_EYE_LINEAR / GL_SPHERE_MAP */
    GLfloat object_plane[4];
    GLfloat eye_plane[4];
} NovaTexGenCoord;

/* One fixed-function light (glLight*). GL minimum is 8 lights. */
#define NOVA_MAX_LIGHTS 8
typedef struct {
    int   enabled;
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float position[4];        /* GL_POSITION; w=0 directional, w!=0 positional */
    float spot_direction[3];  /* GL_SPOT_DIRECTION */
    float spot_exponent;      /* GL_SPOT_EXPONENT */
    float spot_cutoff;        /* GL_SPOT_CUTOFF (degrees, 180 = not a spot) */
    float atten_constant;     /* GL_CONSTANT_ATTENUATION */
    float atten_linear;       /* GL_LINEAR_ATTENUATION */
    float atten_quadratic;    /* GL_QUADRATIC_ATTENUATION */
} NovaLight;

// one client array (vertex/texcoord/color/normal). same fields for all 4 so
// VAO can just copy them around like a box
typedef struct {
    int enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *pointer;
    GLuint vbo_id;
} NovaVertArray;

// VAO is realy just a container that remember the 4 arrays + index buffer.
// bind = swap current live state in/out. slot 0 keep the default VAO.
#define NOVA_MAX_VAOS 64
typedef struct {
    int in_use;
    NovaVertArray vertex, texcoord, color, normal;
    GLuint element_buffer;
} VAOSlot;

extern struct NovaState {
    //C3D targets
    C3D_RenderTarget *render_target_top; // physical top LCD (left eye)
    C3D_RenderTarget *render_target_top_right; // physical top LCD (right eye)
    C3D_RenderTarget *render_target_bot;
    C3D_RenderTarget *current_target;

    C3D_Tex           app_tex;
    C3D_RenderTarget *app_target;
    /* Previous-frame snapshot of the app surface — emulates the N64/PC
     * "front buffer": while frame F is being built, holds frame F-1.
     * Updated at the top of every frame (novaSnapshotAppSurface), consumed
     * by novaBlitSnapshotToFBO for PD-style screen captures. Optional —
     * NULL target when allocation failed or the app surface is off. */
    C3D_Tex           app_prev_tex;
    C3D_RenderTarget *app_prev_target;
    int               app_pot_w, app_pot_h;     // texture storage (POT)
    int               app_logical_w, app_logical_h; // = LCD dims (240x400 native)
    /* A normal g.textures[] slot that ALIASES app_tex, so the screen can be
     * sampled as a texture (PD's framebuffer-0-as-texture effects). 0 = none. */
    GLuint            app_screen_tex_id;

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

    /* clipspace variant: takes already-clip-space input and multiplies it
     * by `projection` (= tilt * Zfix * caller_proj). Position is forwarded
     * with full 4-component w intact (unlike the other shaders, which force
     * w=1) so PICA's perspective divide produces correct 3D output.
     * Activated by novaBeginClipSpace2D() / Begin/End API. */
    DVLB_s *shader_clipspace_dvlb;
    shaderProgram_s shader_clipspace_program;
    int uLoc_projection_clipspace;
    int clipspace_mode_enabled;

    /* Lighting variant: outputs view + normalquat; PICA HW fragment lighting
     * (C3D_LightEnv) does the per-pixel maths. Selected when GL_LIGHTING is on
     * AND a normal array is bound. */
    DVLB_s *shader_lighting_dvlb;
    shaderProgram_s shader_lighting_program;
    int uLoc_projection_lighting;
    int uLoc_modelview_lighting;
    /* C3D lighting objects, (re)built from g.lights[]/g.mat_* by
     * nova_apply_light_env(). lighting_active mirrors the per-draw decision so
     * the TEV path knows to source GPU_FRAGMENT_PRIMARY_COLOR. light_dirty is
     * bumped on any glLight/glMaterial/enable change. */
    C3D_LightEnv  light_env;
    C3D_Light     c3d_lights[NOVA_MAX_LIGHTS];
    C3D_LightLut  light_lut_phong;
    /* Per-light hardware LUTs for GL spotlight cone (GL_SPOT_CUTOFF/EXPONENT)
     * and distance attenuation (GL_*_ATTENUATION). Rebuilt with the env in
     * nova_apply_light_env; kept per-light because each source has its own
     * cone angle / attenuation coefficients. */
    C3D_LightLut   light_lut_spot[NOVA_MAX_LIGHTS];
    C3D_LightLutDA light_lut_da[NOVA_MAX_LIGHTS];
    int           light_env_built;
    int           lighting_active;
    int           light_dirty;

    /* GL_KHR_debug: callback is stored for API completeness but never invoked
     * (NovaGL produces no async debug stream on PICA200). */
    GLDEBUGPROC   debug_callback;
    const void   *debug_user_param;

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
    float cur_normal[3]; // current normal from glNormal*. no lighting yet but
                         // we keep it so the state is real and ready if lighting come

    /* GL_LIGHTING enable + front-material state (glMaterial*). PICA fragment
     * lighting needs a normal-aware shader + C3D_LightEnv we don't build yet,
     * so these are stored as real, queryable state (like cur_normal) ready for
     * that path — the setter functions themselves are fully implemented. */
    int lighting_enabled;
    float mat_ambient[4];
    float mat_diffuse[4];
    float mat_specular[4];
    float mat_emission[4];
    float mat_shininess;

    /* Fixed-function light sources (glLight*) + light model. Full per-light
     * state stored; consumed by the future GPU light path. */
    NovaLight lights[NOVA_MAX_LIGHTS];
    float light_model_ambient[4];

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

    // live client array state. when a VAO is bound this is that VAO arrays.
    NovaVertArray va_vertex, va_texcoord, va_color, va_normal;

    // VAO pool. slot 0 = default VAO storage, bound_vao 0 means default.
    VAOSlot vaos[NOVA_MAX_VAOS];
    GLuint bound_vao;

    int depth_test_enabled;
    GLenum depth_func;
    GLboolean depth_mask;
    int blend_enabled;
    GLenum blend_src, blend_dst;
    /* Separate alpha-channel blend factors (glBlendFuncSeparate). glBlendFunc
     * keeps these equal to the colour factors. PICA's C3D_AlphaBlend takes
     * independent colour/alpha factors, so this is a real HW path. */
    GLenum blend_src_alpha, blend_dst_alpha;
    /* Blend equation, per-channel (glBlendEquationSeparate). PICA's GPU_BLENDEQUATION
     * is real HW — GL_FUNC_ADD / SUBTRACT / REVERSE_SUBTRACT / MIN / MAX all map. */
    GLenum blend_eq_rgb, blend_eq_alpha;
    /* glBlendColor — the constant fed to GL_CONSTANT_COLOR/ALPHA and their
     * inverse blend factors. Pushed via C3D_BlendingColor when blending is on
     * and a constant factor is in use. */
    float blend_color[4];
    /* glLogicOp / GL_COLOR_LOGIC_OP. On PICA the colour stage is EITHER blend
     * OR logic op (selected by the same fragOpMode bit), so when this is on we
     * emit C3D_ColorLogicOp instead of C3D_AlphaBlend. */
    int color_logic_op_enabled;
    GLenum logic_op;
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

    /* Heap-allocated on first glNewList — sizeof(DisplayList) * 512 ≈ 786KB
     * was sitting in .bss for every NovaGL app, even those that never call
     * glGenLists. Now we pay only when a recording actually happens. */
    DisplayList *dl_store;
    int dl_recording;
    GLuint dl_next_base;

    GLenum last_error;
    int initialized;
    /* Frame buffering: 1 = single (SYNCDRAW, CPU waits for GPU each frame),
     * 2 = double, 3 = triple (async — CPU runs ahead of the GPU). With K>1 the
     * vertex/index rings and the texture/FBO orphan GC are split into K slots
     * rotated per frame, so a slot is only reused/freed K frames later (when the
     * GPU has definitely finished it) — the use-after-free that broke async is
     * gone. See novaSetFrameBuffers(). */
    int frame_buffers;        /* K = 1/2/3 */
    int frame_slot;           /* current ring/GC slot, 0..K-1 */
    int c3d_frame_flag;       /* C3D_FrameBegin flag: SYNCDRAW (K==1) or 0 (async) */

    /* Active ring (points at the current slot's buffer). client_array_buf_size /
     * index_buf_size are the PER-SLOT capacities. */
    void *client_array_buf;
    int client_array_buf_size;
    int client_array_buf_offset;

    void *index_buf;
    int index_buf_size;
    int index_buf_offset;

    void *client_array_buf_slots[3];
    void *index_buf_slots[3];

    int tev_dirty;
    int last_tex_state;
    GLint tex_env_mode[3];
    int client_active_texture_unit;
    GLint pack_alignment;
    GLint unpack_alignment;

    int polygon_offset_fill_enabled;
    GLfloat depth_near;
    GLfloat depth_far;

    /* Stencil state — PICA200 has a real stencil buffer (24+8 depth/stencil)
     * and a real C3D_StencilTest path; previously NovaGL stored none of this
     * and stubbed glStencil*. State changes get pushed in apply_gpu_state. */
    int stencil_test_enabled;
    GLenum stencil_func;
    GLint  stencil_ref;
    GLuint stencil_mask;       /* read mask  (the "mask" arg to glStencilFunc) */
    GLuint stencil_write_mask; /* write mask (the arg to glStencilMask) */
    GLenum stencil_op_fail;
    GLenum stencil_op_zfail;
    GLenum stencil_op_zpass;
    GLint  clear_stencil;

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

    /* Per-unit TEV CONSTANT colour. Set via glTexEnvfv(GL_TEXTURE_ENV_COLOR).
     * Plumbed into C3D_TexEnvColor in apply_gpu_state whenever a stage's GL
     * source is GL_CONSTANT. Stored as 0..1 floats (GL convention); converted
     * to packed 0xAABBGGRR for citro3d on the way through. */
    GLfloat tex_env_color[3][4];

    /* GL_RGB_SCALE / GL_ALPHA_SCALE (glTexEnv): final per-stage multiplier
     * applied AFTER the combine function. GL allows 1.0/2.0/4.0; PICA has the
     * matching GPU_TEVSCALE_1/_2/_4. Pushed via C3D_TexEnvScale in the per-unit
     * TEV build. Default 1 (no scale). */
    GLint tex_env_rgb_scale[3];
    GLint tex_env_alpha_scale[3];

    /* Explicit TEV stage programming (overrides the per-unit GL_COMBINE
     * path when active). See NovaGL.h::novaSetExplicitTevStages for the
     * full contract. `explicit_tev_count > 0` means apply_gpu_state should
     * emit `explicit_tev_count` stages from this array instead of doing
     * its usual one-stage-per-active-unit loop. */
    int explicit_tev_count;
    NovaTevStageGL explicit_tev_stages[NOVA_TEV_MAX_STAGES];

    int line_smooth_enabled;

    int current_eye; // 0 = left, 1 = right
    float stereo_depth;

    /* Minimal FBO support: a pool of render targets that wrap a NovaGL texture.
     * glGenFramebuffers hands out IDs into this pool; glBindFramebuffer swaps
     * g.current_target between the screen targets and an FBO's C3D target. */
    FBOSlot fbos[NOVA_MAX_FBOS];
    GLuint bound_fbo; // 0 = screen (GL_FRAMEBUFFER / GL_DRAW_FRAMEBUFFER target)
    GLuint bound_read_fbo; // GL_READ_FRAMEBUFFER target (0 = screen); blit source

    /* novaDrawObjects fast-path: pre-validated pointers for a PTC layout
     * (position 3f@0, texcoord 2f@12, color 4ub@20, stride 24).
     * Skips the ~12 conditionals in nova_draw_internal's fast-path detector. */
    GLuint fast_vbo_id;
    GLuint fast_vbo_offset;
    GLuint fast_idx_vbo_id;
    GLenum fast_idx_type;
} g;

/* GL error semantics: glGetError must report the FIRST error generated since the
 * last query and hold it until queried (GL spec §2.5 / GLES 1.1 §2.5). Funnel every
 * error site through this so a later error never overwrites an earlier unread one.
 * The clear back to GL_NO_ERROR happens only in glGetError. */
static inline void gl_set_error(GLenum e) {
    if (g.last_error == GL_NO_ERROR) g.last_error = e;
}

void nova_fbo_gc_collect(void);
void nova_fbo_gc_collect_all(void);

/* Animated boot splashscreen (src/splashscreen.c). Runs synchronously at the
 * tail of nova_init_ex unless compiled out with -DNOVAGL_NO_SPLASHSCREEN=1
 * (vitaGL's -DNO_SPLASHSCREEN=1 is accepted as an alias). */
void nova_splash_run(void);

#endif //NOVAGL_CONTEXT_H
