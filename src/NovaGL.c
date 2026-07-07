/*
 * NovaGL.c - OpenGL ES 1.1 -> Citro3D Translation Layer Implementation
 */

#include "NovaGL.h"
#include "utils.h"

/* Value fixed by the GL spec; defined locally if NovaGL.h lacks it. */
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif

#include "context.h"
#include <math.h>

#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>

#include "NovaGL_shader_shbin.h"
#include "NovaGL_shader_basic_shbin.h"
#include "NovaGL_shader_texmtx_shbin.h"
#include "NovaGL_shader_clipspace_shbin.h"
#include "NovaGL_shader_lighting_shbin.h"

/* VBlank pacing default for novaSwapBuffers (runtime-overridable through
 * novaSetSwapInterval). Compile with -DNOVAGL_VSYNC=0 to free-run. Must be
 * defined before nova_init_ex seeds g.swap_interval from it. */
#ifndef NOVAGL_VSYNC
#define NOVAGL_VSYNC 1
#endif

/* Primitive enums used by the draw-mode validator; values fixed by spec. */
#ifndef GL_POINTS
#define GL_POINTS 0x0000
#endif
#ifndef GL_LINE_LOOP
#define GL_LINE_LOOP 0x0002
#endif

/* Moved out of NovaGL.h: depends on GX_TRANSFER_* from <3ds.h>, which we now keep
 * confined to the .c side so the public header doesn't leak libctru's `Thread`
 * typedef into C++ callers. */
#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

/* Frame buffering depth (1 = single/SYNCDRAW, 2 = double, 3 = triple). With K>1
 * the CPU runs ahead of the GPU (async); NovaGL splits the vertex/index rings
 * and the texture/FBO orphan GC into K rotating slots so a slot is reused only
 * K frames later (GPU finished) — making async SAFE. Choose at runtime with
 * novaSetFrameBuffers() before nova_init(), or set the compile default here.
 * Legacy -DNOVAGL_ASYNC_FRAME maps to double-buffering (now safe). */
#ifndef NOVAGL_FRAME_BUFFERS
#  if defined(NOVAGL_ASYNC_FRAME) && NOVAGL_ASYNC_FRAME
#    define NOVAGL_FRAME_BUFFERS 2
#  else
#    define NOVAGL_FRAME_BUFFERS 1
#  endif
#endif

struct NovaState g;

/* Pending frame-buffer count set by novaSetFrameBuffers() before init (0 = use
 * the compile-time NOVAGL_FRAME_BUFFERS default). Lives outside `g` because
 * nova_init memsets `g`. */
static int s_pending_frame_buffers = 0;

void novaSetFrameBuffers(int count) {
    s_pending_frame_buffers = count; /* clamped to [1,3] in nova_init_ex */
}

int novaGetFrameBuffers(void) {
    return g.initialized ? g.frame_buffers : 0;
}

// stuff we skip on purpose, so nobody ask "why this not fixed" later:
//
//  - lighting (glLight*): not done. need C3D_LightEnv per light + normals
//    trough draw.c. no real GL ES 1.1 game we run use FF lights anyway, they
//    all do their own thing with TEV / vertex colors. maybe later.
//  - eglGetProcAddress is O(N) table scan, but it only called on init (<100
//    times ever) so who care. gperf not worth a build dep.
//  - TEV stage mask single-reg trick: citro3d dont expose the reg and raw
//    GPUCMD_AddWrite break between libctru versions. we pad unused stages, fine.
//  - no CI / regresion tests yet. would need headless Citra + golden crc. todo
//    someday when i have time and more vodka balalaika.

void nova_init() {
    /* Defaults reverted to the historical values (2MB array / 512KB index /
     * 512KB tex_staging). Smaller defaults caused PD-class workloads to wrap
     * the ring multiple times per frame, hammering linear_alloc_ring's
     * synchronous wait path and freezing the GPU.
     *
     * Compile with -DNOVAGL_SMALL_INIT_DEFAULTS=1 to opt into the lean
     * variant (good for UI-only apps that never push much geometry). */
#ifdef NOVAGL_SMALL_INIT_DEFAULTS
    nova_init_ex(NOVA_CMD_BUF_SIZE, 512 * 1024, 128 * 1024, 256 * 1024);
#else
    nova_init_ex(NOVA_CMD_BUF_SIZE, 2 * 1024 * 1024, 512 * 1024, 512 * 1024);
#endif
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

    /* Top LCD target. With the app surface DISABLED (the default, see below)
     * the game renders straight into this target, so it MUST carry a depth/
     * stencil buffer — without it depth testing is a no-op and 3D geometry
     * renders with back faces showing through (the rotating-cube demo bug).
     * The "-1 / no depth" optimisation only made sense when every draw went to
     * the POT app surface (which has its own D24S8) and this target received
     * just the depth-test-off present quad; that path is off by default now. */
    g.render_target_top = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                 GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g.render_target_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    /* Right-eye target is created lazily on the first novaBeginEye(1) call —
     * if the user never enables stereo we save ~768KB of VRAM (color + depth)
     * for textures. Originally created up-front "just in case".
     *
     * Compile with -DNOVAGL_DISABLE_LAZY_RIGHT_EYE=1 to restore the eager
     * allocation (debug aid: rules out lazy-init as the cause if stereo
     * paths behave oddly). */
#ifdef NOVAGL_DISABLE_LAZY_RIGHT_EYE
    g.render_target_top_right = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                       GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (g.render_target_top_right)
        C3D_RenderTargetSetOutput(g.render_target_top_right, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
#else
    g.render_target_top_right = NULL;
#endif

    /* App surface: POT VRAM render-texture in the SAME orientation as the
     * physical top target (240x400 native → padded to 256x512). The whole
     * top-screen frame renders here; novaSwapBuffers presents it 1:1 to
     * render_target_top. Makes "fb 0" sampleable (see context.h). Carries a
     * D24S8 depth buffer so 3D rendering works exactly as on the LCD.
     *
     * DISABLED BY DEFAULT (NOVAGL_APP_SURFACE=0): the POT surface is 256x512
     * while the logical screen is 240x400, and a draw that doesn't call
     * glViewport inherits C3D's default viewport = the FULL target (256x512),
     * so its content lands in the wrong sub-rect and the 1:1 present then
     * shows it zoomed into a corner with a wrong aspect ratio (the cube demo
     * looked warped/spinning-wrong for the same reason). Rendering straight to
     * render_target_top — as before commit 6500431 — sizes the default
     * viewport to the real screen and Just Works. Flip this to 1 only for the
     * "framebuffer 0 as a texture" effects (PD scope/cutscene distortion);
     * those paths also need a viewport that's clamped to the logical region. */
    g.app_logical_w = NOVA_SCREEN_H; /* native top fb width  = 240 */
    g.app_logical_h = NOVA_SCREEN_W; /* native top fb height = 400 */
    g.app_pot_w = (int) nova_next_pow2((unsigned) g.app_logical_w);
    g.app_pot_h = (int) nova_next_pow2((unsigned) g.app_logical_h);
#ifndef NOVAGL_APP_SURFACE
#define NOVAGL_APP_SURFACE 0
#endif
#if NOVAGL_APP_SURFACE
    if (C3D_TexInitVRAM(&g.app_tex, (u16) g.app_pot_w, (u16) g.app_pot_h, GPU_RGBA8)) {
        C3D_TexSetFilter(&g.app_tex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&g.app_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        g.app_target = C3D_RenderTargetCreateFromTex(&g.app_tex, GPU_TEXFACE_2D, 0,
                                                     GPU_RB_DEPTH24_STENCIL8);
        /* Zero the whole POT buffer so LINEAR sampling across the logical↔pad
         * boundary never pulls undefined VRAM (Butterscotch does the same). */
        if (g.app_target && g.app_tex.data) {
            memset(g.app_tex.data, 0, g.app_tex.size);
            GSPGPU_FlushDataCache(g.app_tex.data, g.app_tex.size);
        }
    }
#endif
    if (!g.app_target) {
        /* Allocation failed — fall back to rendering straight to the LCD.
         * Framebuffer-from-screen effects won't work, but the game renders. */
        if (g.app_tex.data) { C3D_TexDelete(&g.app_tex); memset(&g.app_tex, 0, sizeof(g.app_tex)); }
        g.current_target = g.render_target_top;
        g.app_screen_tex_id = 0;
    } else {
        g.current_target = g.app_target;

        /* Register a g.textures[] slot that ALIASES app_tex so the screen can
         * be bound as a sampleable texture (PD samples framebuffer 0 directly
         * for cutscene/scope distortion — "fb 0 as texture"). The slot shares
         * app_tex's VRAM (struct copy; app_tex's fields are fixed after init).
         * Marked in_use so glGenTextures skips it. */
        GLuint sid = 1;
        while (sid < NOVA_MAX_TEXTURES && g.textures[sid].in_use) sid++;
        if (sid < NOVA_MAX_TEXTURES) {
            TexSlot *s = &g.textures[sid];
            memset(s, 0, sizeof(*s));
            s->tex = g.app_tex;          /* alias: same VRAM .data */
            s->allocated = 1;
            s->is_vram = 1;              /* already VRAM-resident; never re-home it */
            s->in_use = 1;
            s->fmt = GPU_RGBA8;
            s->width = g.app_logical_w;  s->height = g.app_logical_h;
            s->orig_width = g.app_logical_w; s->orig_height = g.app_logical_h;
            s->pot_w = g.app_pot_w;      s->pot_h = g.app_pot_h;
            s->min_filter = GL_LINEAR;   s->mag_filter = GL_LINEAR;
            s->wrap_s = GL_CLAMP_TO_EDGE; s->wrap_t = GL_CLAMP_TO_EDGE;
            s->max_level = 0;
            g.app_screen_tex_id = sid;
        } else {
            g.app_screen_tex_id = 0;
        }
    }

    /* Previous-frame snapshot ("front buffer" emulation). Same POT layout as
     * the app surface, colour only (never depth-tested). If VRAM is too tight
     * the snapshot quietly stays NULL and captures fall back to the live app
     * surface (loses one frame of history but still renders something). */
    if (g.app_target) {
        if (C3D_TexInitVRAM(&g.app_prev_tex, (u16) g.app_pot_w, (u16) g.app_pot_h, GPU_RGBA8)) {
            C3D_TexSetFilter(&g.app_prev_tex, GPU_LINEAR, GPU_LINEAR);
            C3D_TexSetWrap(&g.app_prev_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            g.app_prev_target = C3D_RenderTargetCreateFromTex(&g.app_prev_tex, GPU_TEXFACE_2D, 0, -1);
            if (!g.app_prev_target) {
                C3D_TexDelete(&g.app_prev_tex);
                memset(&g.app_prev_tex, 0, sizeof(g.app_prev_tex));
            } else {
                /* GPU-clear: CPU-memset нулей до Citra-сэмплера может не доехать. */
                C3D_RenderTargetClear(g.app_prev_target, C3D_CLEAR_COLOR, 0x00000000, 0);
            }
        }
    }

#if NOVAGL_APP_SURFACE
    /* Diagnostic: confirm the app surface (the sampleable "fb 0") actually
     * allocated. If app_target is NULL the screen fell back to the NPOT LCD and
     * every "copy screen into FBO" effect (PD damage blur / menu backdrop) will
     * silently no-op. Lands in sdmc:/3ds/pd/stdout.txt. */
    printf("[NovaGL] APP_SURFACE: app_target=%p app_tex.data=%p prev_target=%p screen_tex_id=%u pot=%dx%d logical=%dx%d\n",
           (void *)g.app_target, (void *)g.app_tex.data, (void *)g.app_prev_target,
           (unsigned)g.app_screen_tex_id,
           g.app_pot_w, g.app_pot_h, g.app_logical_w, g.app_logical_h);
#endif

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

    g.shader_texmtx_dvlb = DVLB_ParseFile((u32 *) NovaGL_shader_texmtx_shbin, NovaGL_shader_texmtx_shbin_size);
    if (g.shader_texmtx_dvlb) {
        shaderProgramInit(&g.shader_texmtx_program);
        shaderProgramSetVsh(&g.shader_texmtx_program, &g.shader_texmtx_dvlb->DVLE[0]);
        g.uLoc_mvp_texmtx    = shaderInstanceGetUniformLocation(g.shader_texmtx_program.vertexShader, "mvp");
        g.uLoc_texmtx_texmtx = shaderInstanceGetUniformLocation(g.shader_texmtx_program.vertexShader, "texmtx");
    }

    g.shader_clipspace_dvlb = DVLB_ParseFile((u32 *) NovaGL_shader_clipspace_shbin, NovaGL_shader_clipspace_shbin_size);
    if (g.shader_clipspace_dvlb) {
        shaderProgramInit(&g.shader_clipspace_program);
        shaderProgramSetVsh(&g.shader_clipspace_program, &g.shader_clipspace_dvlb->DVLE[0]);
        g.uLoc_projection_clipspace = shaderInstanceGetUniformLocation(
            g.shader_clipspace_program.vertexShader, "projection");
    }

    g.shader_lighting_dvlb = DVLB_ParseFile((u32 *) NovaGL_shader_lighting_shbin,
                                            NovaGL_shader_lighting_shbin_size);
    if (g.shader_lighting_dvlb) {
        shaderProgramInit(&g.shader_lighting_program);
        shaderProgramSetVsh(&g.shader_lighting_program, &g.shader_lighting_dvlb->DVLE[0]);
        g.uLoc_projection_lighting = shaderInstanceGetUniformLocation(
            g.shader_lighting_program.vertexShader, "projection");
        g.uLoc_modelview_lighting = shaderInstanceGetUniformLocation(
            g.shader_lighting_program.vertexShader, "modelview");
    }
    g.light_env_built = 0;
    g.lighting_active = 0;
    g.light_dirty = 1;

    /* Start on the full shader; apply_gpu_state's selector will switch to
     * a faster variant on the first draw if the state qualifies. */
    C3D_BindProgram(&g.shader_program);
    g.active_shader = NOVA_SHADER_FULL;
    g.clipspace_mode_enabled = 0;
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

    /* Resolve frame buffering depth K (runtime override > compile default),
     * clamp to [1,3]. K==1 -> SYNCDRAW (CPU waits); K>1 -> async (flag 0). */
    int K = (s_pending_frame_buffers > 0) ? s_pending_frame_buffers : NOVAGL_FRAME_BUFFERS;
    if (K < 1) K = 1;
    if (K > 3) K = 3;
    g.frame_buffers = K;
    g.frame_slot = 0;
    g.swap_interval = NOVAGL_VSYNC;
    g.c3d_frame_flag = (K > 1) ? 0 : C3D_FRAME_SYNCDRAW;

    /* One vertex + index ring PER frame slot (full per-slot capacity), so the
     * GPU reading slot s's data is never overwritten by the CPU until that slot
     * comes round again K frames later. Falls back to fewer/smaller buffers if
     * an allocation fails (K is reduced so we never index a NULL slot). */
    g.client_array_buf_size = client_array_buf_size;
    g.index_buf_size = index_buf_size;
    for (int i = 0; i < K; i++) {
        g.client_array_buf_caps[i] = client_array_buf_size;
        g.index_buf_caps[i] = index_buf_size;
        g.client_array_buf_slots[i] = linearAlloc(client_array_buf_size);
        g.index_buf_slots[i]        = linearAlloc(index_buf_size);
        if (!g.client_array_buf_slots[i] || !g.index_buf_slots[i]) {
            /* Out of linear RAM for this slot — drop back to the slots we have. */
            if (g.client_array_buf_slots[i]) { linearFree(g.client_array_buf_slots[i]); g.client_array_buf_slots[i] = NULL; }
            if (g.index_buf_slots[i])        { linearFree(g.index_buf_slots[i]);        g.index_buf_slots[i] = NULL; }
            if (i == 0) { K = 1; } else { K = i; }
            g.frame_buffers = K;
            g.c3d_frame_flag = (K > 1) ? 0 : C3D_FRAME_SYNCDRAW;
            break;
        }
    }
    g.client_array_buf = g.client_array_buf_slots[0];
    g.index_buf = g.index_buf_slots[0];

    /* Bump from 16384 -> max possible under 16-bit index range. With 4 verts
     * per quad and uint16 indices, the highest packed base is 65532 ⇒ 16383
     * quads is the per-batch hard limit if we want indices fit in u16. Keep
     * at 16384 (192KB static buffer); going higher would force 32-bit indices
     * which C3D_DrawElements can take but it doubles the index bandwidth. */
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
        /* Alpha-combine defaults mirror RGB (the GL ES 1.1 baseline). Leaving
         * these zero would make the new separate-alpha path interpret missing
         * fields as "fall back to RGB", which is also correct, but pre-seeding
         * matches what an app that explicitly enables GL_COMBINE would see. */
        g.tex_env_combine_alpha[i] = GL_MODULATE;
        g.tex_env_src0_alpha[i] = GL_TEXTURE;
        g.tex_env_src1_alpha[i] = GL_PREVIOUS;
        g.tex_env_src2_alpha[i] = GL_CONSTANT;
        g.tex_env_operand0_alpha[i] = GL_SRC_ALPHA;
        g.tex_env_operand1_alpha[i] = GL_SRC_ALPHA;
        g.tex_env_operand2_alpha[i] = GL_SRC_ALPHA;
        /* TEV CONSTANT defaults to white — same as C3D_TexEnvInit which sets
         * env->color = 0xFFFFFFFF. Stays out of the way unless the engine
         * explicitly calls glTexEnvfv(GL_TEXTURE_ENV_COLOR, ...). */
        g.tex_env_color[i][0] = 1.0f;
        g.tex_env_color[i][1] = 1.0f;
        g.tex_env_color[i][2] = 1.0f;
        g.tex_env_color[i][3] = 1.0f;
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
    g.cur_color_packed = 0xFFFFFFFF;

    // GL default current normal is (0,0,1), not zero
    g.cur_normal[0] = 0.0f;
    g.cur_normal[1] = 0.0f;
    g.cur_normal[2] = 1.0f;

    /* NovaGL ships depth test ENABLED + GL_LEQUAL by default (NOT the GL spec
     * default of disabled+GL_LESS). This is an intentional port-compatibility
     * choice: several 3DS ports render 3D geometry without explicitly enabling
     * depth, and the spec-strict "off" default silently dropped their world.
     * A conformant app sets its own depth state regardless. Opt into the strict
     * spec default with -DNOVAGL_STRICT_DEPTH_DEFAULT=1. */
#ifdef NOVAGL_STRICT_DEPTH_DEFAULT
    g.depth_test_enabled = 0;
    g.depth_func = GL_LESS;
#else
    g.depth_test_enabled = 1;
    g.depth_func = GL_LEQUAL;
#endif
    g.gpu_depth_func = gl_to_gpu_depth_testfunc(g.depth_func);
    g.gpu_early_depth_func = gl_to_gpu_earlydepthfunc(g.depth_func);

    g.depth_mask = GL_TRUE;
    g.clear_depth = 1.0f;

    /* Stencil defaults — disabled, but parameters set so the first
     * glEnable(GL_STENCIL_TEST) pushes sane values to PICA. */
    g.stencil_test_enabled = 0;
    g.stencil_func = GL_ALWAYS;
    g.gpu_stencil_func = GPU_ALWAYS;
    g.stencil_ref = 0;
    g.stencil_mask = 0xFF;
    g.stencil_write_mask = 0xFF;

    g.stencil_op_fail  = GL_KEEP;
    g.stencil_op_zfail = GL_KEEP;
    g.stencil_op_zpass = GL_KEEP;
    g.gpu_stencil_op_fail = GPU_STENCIL_KEEP;
    g.gpu_stencil_op_zfail = GPU_STENCIL_KEEP;
    g.gpu_stencil_op_zpass = GPU_STENCIL_KEEP;

    g.clear_stencil = 0;

    g.blend_src = GL_ONE;
    g.blend_dst = GL_ZERO;
    g.blend_src_alpha = GL_ONE;
    g.blend_dst_alpha = GL_ZERO;
    g.gpu_blend_src = GPU_ONE;
    g.gpu_blend_dst = GPU_ZERO;
    g.gpu_blend_src_alpha = GPU_ONE;
    g.gpu_blend_dst_alpha = GPU_ZERO;

    g.blend_eq_rgb = GL_FUNC_ADD;
    g.blend_eq_alpha = GL_FUNC_ADD;
    g.gpu_blend_eq_rgb = GPU_BLEND_ADD;
    g.gpu_blend_eq_alpha = GPU_BLEND_ADD;

    /* glBlendColor default is transparent black; logic op default GL_COPY. */
    g.blend_color[0] = g.blend_color[1] = g.blend_color[2] = g.blend_color[3] = 0.0f;
    g.blend_color_packed = 0x0;
    g.color_logic_op_enabled = 0;
    g.logic_op = GL_COPY;
    g.gpu_logic_op = GPU_LOGICOP_COPY;
    /* GL_RGB_SCALE / GL_ALPHA_SCALE default to 1 on every texture unit. */
    for (int u = 0; u < 3; u++) {
        g.tex_env_rgb_scale[u] = 1;
        g.tex_env_alpha_scale[u] = 1;
        nova_update_fast_tev(u);
    }
    /* GL default material: ambient (0.2,0.2,0.2,1), diffuse (0.8,0.8,0.8,1),
     * specular/emission (0,0,0,1), shininess 0. */
    g.lighting_enabled = 0;
    g.mat_ambient[0] = g.mat_ambient[1] = g.mat_ambient[2] = 0.2f; g.mat_ambient[3] = 1.0f;
    g.mat_diffuse[0] = g.mat_diffuse[1] = g.mat_diffuse[2] = 0.8f; g.mat_diffuse[3] = 1.0f;
    g.mat_specular[0] = g.mat_specular[1] = g.mat_specular[2] = 0.0f; g.mat_specular[3] = 1.0f;
    g.mat_emission[0] = g.mat_emission[1] = g.mat_emission[2] = 0.0f; g.mat_emission[3] = 1.0f;
    g.mat_shininess = 0.0f;
    /* GL light defaults: all ambient black, dir (0,0,1,0), spot off, atten
     * (1,0,0). diffuse/specular white only on LIGHT0, black on the rest. */
    for (int li = 0; li < NOVA_MAX_LIGHTS; li++) {
        NovaLight *L = &g.lights[li];
        L->enabled = 0;
        L->ambient[0] = L->ambient[1] = L->ambient[2] = 0.0f; L->ambient[3] = 1.0f;
        float c = (li == 0) ? 1.0f : 0.0f;
        L->diffuse[0]  = L->diffuse[1]  = L->diffuse[2]  = c; L->diffuse[3]  = 1.0f;
        L->specular[0] = L->specular[1] = L->specular[2] = c; L->specular[3] = 1.0f;
        L->position[0] = 0.0f; L->position[1] = 0.0f; L->position[2] = 1.0f; L->position[3] = 0.0f;
        L->spot_direction[0] = 0.0f; L->spot_direction[1] = 0.0f; L->spot_direction[2] = -1.0f;
        L->spot_exponent = 0.0f;
        L->spot_cutoff = 180.0f;
        L->atten_constant = 1.0f;
        L->atten_linear = 0.0f;
        L->atten_quadratic = 0.0f;
    }
    g.light_model_ambient[0] = g.light_model_ambient[1] = g.light_model_ambient[2] = 0.2f;
    g.light_model_ambient[3] = 1.0f;
    g.alpha_func = GL_ALWAYS;
    g.gpu_alpha_func = GPU_ALWAYS;
    g.alpha_ref = 0.0f;
    g.alpha_ref8 = 0x0;
    g.cull_face_mode = GL_BACK;
    g.front_face = GL_CCW;

    g.state_dirty_bits = NOVA_DIRTY_ALL;

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

    /* Honour the caller's tex_staging hint instead of dropping it. get_tex_staging
     * will grow lazily, but pre-sizing here lets a heavy texture loader skip
     * the first realloc spike. */
    g.tex_staging_size = 0;
    g.tex_staging = NULL;
    if (tex_staging_size > 0) {
        get_tex_staging(tex_staging_size);
    }

    g.initialized = 1;

    C3D_FrameBegin(g.c3d_frame_flag);
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;

    if (g.app_target) {
        C3D_FrameDrawOn(g.app_target);
        g.current_target = g.app_target;
    } else {
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
    }

    /* Animated "Powered by NovaGL" boot splash. Runs synchronously here, inside
     * the freshly-opened frame, and leaves the exact post-init invariant behind
     * (open frame on the app surface, ring offsets at 0). Disable with
     * -DNOVAGL_NO_SPLASHSCREEN=1 (or vitaGL's -DNO_SPLASHSCREEN=1). */
#if !(defined(NOVAGL_NO_SPLASHSCREEN) && NOVAGL_NO_SPLASHSCREEN) && !defined(NO_SPLASHSCREEN)
    nova_splash_run();
#endif
}

int novaGetEyeCount(void) {
    return (osGet3DSliderState() > 0.0f) ? 2 : 1;
}

void novaSet3DDepth(float depth) {
    g.stereo_depth = depth;
}

void novaBeginEye(int eye) {
    /* Commit the pending batch under the CURRENT eye's projection before we flip
     * eye / target (never merge geometry across an eye boundary). */
    nova_batch_flush();

    g.current_eye = eye;

    nova_set_render_target(eye);

    g.matrices_dirty = 1;
    g.proj_dirty = g.mv_dirty = g.tex_mtx_dirty = 1;
    /* Stereo eye shift goes into final_proj — invalidate the cached value
     * so the next apply_gpu_state rebuilds with the new per-eye offset. */
    g.final_proj_cached_valid = 0;
}

/* Present the app surface (POT VRAM render-tex holding this frame's top-screen
 * content) onto the physical LCD target with a 1:1 quad. The app content was
 * rendered in the SAME sideways orientation as the LCD (identical per-draw
 * tilt + viewport), so this is a straight copy of the logical sub-rect — no
 * rotation. Must run inside an open C3D frame, before C3D_FrameEnd. */
static void nova_present_app(C3D_RenderTarget *lcd) {
    if (!g.app_target || !lcd || !g.app_tex.data) return;

    /* Commit any pending batch into the app surface before we switch target to
     * the LCD and clobber state (novaSwapBuffers already flushes; belt-and-
     * suspenders for any direct caller). */
    nova_batch_flush();

    /* Make sure the app-surface render is finished in VRAM before we sample
     * it (render-to-texture → sample-in-same-frame hazard). */
    C3D_FrameSplit(0);

    C3D_FrameDrawOn(lcd);

    /* TEV stage 0 = REPLACE(TEX0); pad the rest. */
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    for (int i = 1; i < 6; i++) {
        C3D_TexEnv *e = C3D_GetTexEnv(i);
        C3D_TexEnvInit(e);
        C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
        C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    }

    C3D_TexBind(0, &g.app_tex);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_SetViewport(0, 0, (u32) lcd->frameBuf.width, (u32) lcd->frameBuf.height);

    /* App is stored in the SAME sideways layout as the LCD, so the present is
     * a straight 1:1 copy of the logical sub-rect (no rotation). V maps
     * straight — flipping it mirrors the screen left-right (the texture's V
     * axis is the screen's horizontal axis under the 90° storage). */
    const float u1 = (float) g.app_logical_w / (float) g.app_pot_w;
    const float v1 = (float) g.app_logical_h / (float) g.app_pot_h;

    float v[6 * 7];
    const float corners[6][4] = {
        {-1.f, -1.f, 0.f, 0.f}, /* x,y,u,v */
        { 1.f, -1.f, u1,  0.f},
        { 1.f,  1.f, u1,  v1 },
        {-1.f, -1.f, 0.f, 0.f},
        { 1.f,  1.f, u1,  v1 },
        {-1.f,  1.f, 0.f, v1 },
    };
    for (int i = 0; i < 6; i++) {
        float *o = &v[i * 7];
        o[0] = corners[i][0]; o[1] = corners[i][1]; o[2] = 0.f; o[3] = 1.f;
        o[4] = corners[i][2]; o[5] = corners[i][3];
        ((uint8_t *) (o + 6))[0] = 255;
        ((uint8_t *) (o + 6))[1] = 255;
        ((uint8_t *) (o + 6))[2] = 255;
        ((uint8_t *) (o + 6))[3] = 255;
    }

    int prev_clipspace = g.clipspace_mode_enabled;
    int prev_shader = g.active_shader;
    /* Identity post-clip transform: the quad is already in final LCD space. */
    if (g.shader_clipspace_dvlb) {
        C3D_BindProgram(&g.shader_clipspace_program);
        g.active_shader = NOVA_SHADER_CLIPSPACE;
        C3D_Mtx id; Mtx_Identity(&id);
        if (g.uLoc_projection_clipspace >= 0)
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &id);
    }

    const int bytes = 6 * 28;
    uint8_t *staged = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset,
                                                    bytes, g.client_array_buf_size);
    /* linear_alloc_ring can now return NULL (ring can't fit this draw). Skip
     * the present quad rather than dereference NULL. */
    if (staged) {
        memcpy(staged, v, (size_t) bytes);
        GSPGPU_FlushDataCache(staged, (u32) bytes);
        nova_setup_attr_info(4);
        nova_setup_buf_info(staged, 28);
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
    }

    g.active_shader = prev_shader;
    g.clipspace_mode_enabled = prev_clipspace;
    nova_invalidate_state_cache();
}

/* Copy the app surface's logical content into the previous-frame snapshot.
 * Called at the TOP of each frame (before the frame's clear/draws) so that
 * while frame F is being built the snapshot holds the fully-presented frame
 * F-1 — the exact "front buffer" a PD-style screen capture expects. Same
 * sideways layout both sides, so a straight 1:1 quad. Must run inside an
 * open C3D frame. */
void novaSnapshotAppSurface(void) {
    if (!g.app_target || !g.app_tex.data || !g.app_prev_target || !g.app_prev_tex.data) return;

    nova_batch_flush();
    /* Commit the app surface's writes before sampling it. */
    C3D_FrameSplit(0);

    C3D_FrameDrawOn(g.app_prev_target);

    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    for (int i = 1; i < 6; i++) {
        C3D_TexEnv *e = C3D_GetTexEnv(i);
        C3D_TexEnvInit(e);
        C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, (GPU_TEVSRC) 0, (GPU_TEVSRC) 0);
        C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    }

    C3D_TexBind(0, &g.app_tex);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    /* Logical sub-rect → logical sub-rect, 1:1. */
    C3D_SetViewport(0, 0, (u32) g.app_logical_w, (u32) g.app_logical_h);

    const float u1 = (float) g.app_logical_w / (float) g.app_pot_w;
    const float v1 = (float) g.app_logical_h / (float) g.app_pot_h;

    float v[6 * 7];
    const float corners[6][4] = {
        {-1.f, -1.f, 0.f, 0.f},
        { 1.f, -1.f, u1,  0.f},
        { 1.f,  1.f, u1,  v1 },
        {-1.f, -1.f, 0.f, 0.f},
        { 1.f,  1.f, u1,  v1 },
        {-1.f,  1.f, 0.f, v1 },
    };
    for (int i = 0; i < 6; i++) {
        float *o = &v[i * 7];
        o[0] = corners[i][0]; o[1] = corners[i][1]; o[2] = 0.f; o[3] = 1.f;
        o[4] = corners[i][2]; o[5] = corners[i][3];
        ((uint8_t *) (o + 6))[0] = 255;
        ((uint8_t *) (o + 6))[1] = 255;
        ((uint8_t *) (o + 6))[2] = 255;
        ((uint8_t *) (o + 6))[3] = 255;
    }

    int prev_clipspace = g.clipspace_mode_enabled;
    int prev_shader = g.active_shader;
    if (g.shader_clipspace_dvlb) {
        C3D_BindProgram(&g.shader_clipspace_program);
        g.active_shader = NOVA_SHADER_CLIPSPACE;
        C3D_Mtx id; Mtx_Identity(&id);
        if (g.uLoc_projection_clipspace >= 0)
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uLoc_projection_clipspace, &id);
    }

    const int bytes = 6 * 28;
    uint8_t *staged = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset,
                                                    bytes, g.client_array_buf_size);
    if (staged) {
        memcpy(staged, v, (size_t) bytes);
        GSPGPU_FlushDataCache(staged, (u32) bytes);
        nova_setup_attr_info(4);
        nova_setup_buf_info(staged, 28);
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
        /* Resolve the snapshot before anything samples it. */
        C3D_FrameSplit(0);
    }

    g.active_shader = prev_shader;
    g.clipspace_mode_enabled = prev_clipspace;

    /* Back to the app surface for the frame's real draws. */
    C3D_FrameDrawOn(g.app_target);
    g.current_target = g.app_target;
    nova_invalidate_state_cache();
}

/* Frame-rate cap. citro3d's C3D_FRAME_SYNCDRAW only waits for the GPU to
 * finish drawing — it does NOT pace the CPU loop to the display. On real
 * hardware the slow ARM11 hides this, but under Citra the GPU "finishes"
 * instantly, so the main loop free-runs at hundreds of fps. Games that advance
 * animation per-frame (e.g. the rotating-cube demo does `angle += 1.0` each
 * iteration) then spin absurdly fast. Waiting one VBlank per swap caps the
 * loop at 60 fps — the standard 3DS main-loop idiom — so per-frame animation
 * runs at a sane, hardware-consistent rate. Compile with -DNOVAGL_VSYNC=0 to
 * free-run (e.g. for benchmarking); see the guard near the top of the file. */

void novaSetSwapInterval(int interval) {
    g.swap_interval = interval < 0 ? 0 : interval;
}

void novaSwapBuffers(void) {
    /* Commit any pending batch before the frame ends + the ring slot rotates —
     * an un-issued batch's verts would be lost when its slot is reused. */
    nova_batch_flush();
    /* Present the app surface to the physical top LCD before ending the frame. */
    if (g.app_target) nova_present_app(g.render_target_top);

    C3D_FrameEnd(0);
    /* VBlank pacing quantizes the frame rate to 60/N: a 45ms frame stalls
     * until the 4th impulse and reads as a hard 15 FPS. Engines that pace
     * themselves (OpenMW advances simulation by measured dt) disable this
     * via novaSetSwapInterval(0). */
    for (int vb = 0; vb < g.swap_interval; vb++)
        gspWaitForVBlank();

    /* Rotate to the next frame slot. The vertex/index rings and the orphan GC
     * buckets for THIS slot were last used `frame_buffers` frames ago, so the
     * GPU has finished them — they're safe to reuse/free now. (K==1 is the old
     * 1-frame deferral, safe because C3D_FrameBegin below runs SYNCDRAW.) */
    g.frame_slot = (g.frame_slot + 1) % g.frame_buffers;

    nova_fbo_gc_collect();              /* free this slot's orphaned FBO targets */

    nova_wait_tag("[W] frameBegin>");
    C3D_FrameBegin(g.c3d_frame_flag);   /* SYNCDRAW (K==1) waits; async (K>1) doesn't */
    nova_wait_tag("[W] frameBegin<");

    nova_tex_gc_collect();              /* free this slot's orphaned textures   */
    nova_ring_gc_collect();             /* free ring buffers orphaned by growth */
    nova_tex_staging_tick();            /* shrink idle texture staging buffer   */

    /* Point the active rings at this slot's buffers and reset their offsets —
     * the GPU is done with whatever was here K frames ago. Capacities are
     * per-slot (adaptive growth). */
    g.client_array_buf = g.client_array_buf_slots[g.frame_slot];
    g.index_buf        = g.index_buf_slots[g.frame_slot];
    g.client_array_buf_size = g.client_array_buf_caps[g.frame_slot];
    g.index_buf_size = g.index_buf_caps[g.frame_slot];
    g.client_array_buf_offset = 0;
    g.index_buf_offset = 0;
    /* New frame: re-arm the once-per-frame linear_alloc_ring mid-frame drain. */
    g.ring_synced_this_frame = 0;

    /* Resume drawing into the app surface (the logical "screen"). */
    if (g.app_target) {
        C3D_FrameDrawOn(g.app_target);
        g.current_target = g.app_target;
    } else {
        C3D_FrameDrawOn(g.render_target_top);
        g.current_target = g.render_target_top;
    }
}

void nova_fini(void) {
    if (!g.initialized) return;

    /* Commit any pending batch before the final C3D_FrameEnd. */
    nova_batch_flush();

    // Когда nova_fini зовут из главного цикла после break, мы находимся
    // МЕЖДУ FrameBegin (последний swap) и невыполненным FrameEnd — то есть у
    // GPU ещё открыт кадр на наших render_target_top/_right/_bot. Если сразу
    // зовём C3D_RenderTargetDelete, оно лезет в renderqueue.c:364, видит
    // pending transfer на удаляемом таргете и валит процесс. Сначала закрываем
    // кадр и ждём GPU.
    nova_wait_tag("[W] swap-end>");
    C3D_FrameEnd(0);
    nova_wait_tag("[W] swap-p3d>");
    gspWaitForP3D();
    nova_wait_tag("[W] swap-p3d<");
    g.p3d_pending = 0; /* frame fully retired; next wait needs new work */

    /* GPU is idle now — flush ALL frame slots of orphaned texture/FBO storage. */
    nova_tex_gc_collect_all();
    nova_fbo_gc_collect_all();

    for (int i = 0; i < g.frame_buffers; i++) {
        if (g.client_array_buf_slots[i]) linearFree(g.client_array_buf_slots[i]);
        if (g.index_buf_slots[i])        linearFree(g.index_buf_slots[i]);
    }
    g.client_array_buf = NULL;
    g.index_buf = NULL;

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

    if (g.dl_store) {
        free(g.dl_store);
        g.dl_store = NULL;
    }

    if (g.shader_dvlb) {
        shaderProgramFree(&g.shader_program);
        DVLB_Free(g.shader_dvlb);
    }
    if (g.shader_basic_dvlb) {
        shaderProgramFree(&g.shader_basic_program);
        DVLB_Free(g.shader_basic_dvlb);
    }
    if (g.shader_texmtx_dvlb) {
        shaderProgramFree(&g.shader_texmtx_program);
        DVLB_Free(g.shader_texmtx_dvlb);
    }
    if (g.shader_clipspace_dvlb) {
        shaderProgramFree(&g.shader_clipspace_program);
        DVLB_Free(g.shader_clipspace_dvlb);
    }

    /* FBO orphans already flushed (all slots) above via nova_fbo_gc_collect_all. */

    if (g.app_target) {
        C3D_RenderTargetDelete(g.app_target);
        g.app_target = NULL;
    }
    if (g.app_tex.data) {
        C3D_TexDelete(&g.app_tex);
        memset(&g.app_tex, 0, sizeof(g.app_tex));
    }
    C3D_RenderTargetDelete(g.render_target_top);
    if (g.render_target_top_right) C3D_RenderTargetDelete(g.render_target_top_right);
    C3D_RenderTargetDelete(g.render_target_bot);
    C3D_Fini();
    g.initialized = 0;
}

void nova_set_render_target(int target_mode) {
    /* Switching the draw target with an open batch would draw the old run into
     * the new target — commit it first (every branch below does C3D_FrameDrawOn
     * with no preceding split). */
    nova_batch_flush();
    if (target_mode == 1) {
        /* Lazy-init right-eye target on first use — see comment in nova_init_ex. */
        if (!g.render_target_top_right) {
            g.render_target_top_right = C3D_RenderTargetCreate(NOVA_SCREEN_H, NOVA_SCREEN_W,
                                                               GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
            if (g.render_target_top_right) {
                C3D_RenderTargetSetOutput(g.render_target_top_right, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
            } else {
                /* Allocation failed — fall back to left-eye, no stereo. */
                C3D_FrameDrawOn(g.render_target_top);
                g.current_target = g.render_target_top;
                return;
            }
        }
        C3D_FrameDrawOn(g.render_target_top_right);
        g.current_target = g.render_target_top_right;
    } else if (target_mode == 2) {
        C3D_FrameDrawOn(g.render_target_bot);
        g.current_target = g.render_target_bot;
    } else {
        /* Eye 0 / main "screen" → the POT app surface (presented to the LCD at
         * swap). Falls back to the LCD directly if the app surface failed to
         * allocate. */
        if (g.app_target) {
            C3D_FrameDrawOn(g.app_target);
            g.current_target = g.app_target;
        } else {
            C3D_FrameDrawOn(g.render_target_top);
            g.current_target = g.render_target_top;
        }
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

/* ── Near-plane triangle clipping ───────────────────────────────────────────
 * PICA's clip-z range is [-w, 0] (not GL's [-w, w]); the vertex shaders only
 * CLAMP z into it (depth-only). So a triangle that straddles the near plane
 * (a vertex with z < -w, i.e. z+w < 0) is never properly clipped — its near
 * vertex projects to a garbage screen position and the triangle drops/distorts
 * → "see through walls" when close/oblique. PowerVR/vitaGL near-clip in HW so
 * they don't show it. We clip such triangles on the CPU (Sutherland-Hodgman
 * against z+w>=0). Two entry points, each with its own vertex layout:
 *   • novaDrawClipspaceTris (below) — fast3d/PD already hands us clip-space
 *     xyzw, so we clip in clip space (NovaClipV).
 *   • nova_basic_near_clip — the GPU-MVP path (e.g. Wolfenstein-RPG walls): we
 *     replay the shader's MVP only to get each vertex's near distance, then clip
 *     in MODEL space (NovaModelV) and redraw through the SAME shader, so fog /
 *     texmtx / depth stay intact.
 * Both only kick in when a vertex actually crosses near; otherwise the normal
 * draw path runs. Disable: -DNOVAGL_NO_CLIPSPACE_NEAR_CLIP=1 / -DNOVAGL_NO_NEAR_CLIP=1. */
#ifndef NOVAGL_NO_CLIPSPACE_NEAR_CLIP
typedef struct { float x, y, z, w, u, v; uint8_t r, g, b, a; } NovaClipV; /* 28 B */

static inline NovaClipV nova_clipv_lerp(const NovaClipV *a, const NovaClipV *b, float t) {
    NovaClipV o;
    o.x = a->x + t * (b->x - a->x); o.y = a->y + t * (b->y - a->y);
    o.z = a->z + t * (b->z - a->z); o.w = a->w + t * (b->w - a->w);
    o.u = a->u + t * (b->u - a->u); o.v = a->v + t * (b->v - a->v);
    o.r = (uint8_t) (a->r + t * ((float) b->r - a->r));
    o.g = (uint8_t) (a->g + t * ((float) b->g - a->g));
    o.b = (uint8_t) (a->b + t * ((float) b->b - a->b));
    o.a = (uint8_t) (a->a + t * ((float) b->a - a->a));
    return o;
}

/* Clip one triangle against z + w >= 0 (Sutherland-Hodgman), fan-triangulate the
 * <=4-vertex result into `out` (<=6 verts). Returns vertices written. */
static int nova_clip_tri_near(const NovaClipV tri[3], NovaClipV out[6]) {
    NovaClipV poly[4];
    int n = 0;
    for (int i = 0; i < 3 && n < 4; i++) {
        const NovaClipV *cur = &tri[i];
        const NovaClipV *nxt = &tri[(i + 1) % 3];
        float dc = cur->z + cur->w;
        float dn = nxt->z + nxt->w;
        int ci = dc >= 0.0f, ni = dn >= 0.0f;
        if (ci) poly[n++] = *cur;
        if (ci != ni && n < 4) {
            float denom = dc - dn;
            float t = (denom != 0.0f) ? (dc / denom) : 0.0f;
            poly[n++] = nova_clipv_lerp(cur, nxt, t);
        }
    }
    if (n < 3) return 0;
    int vc = 0;
    for (int i = 1; i + 1 < n; i++) {
        out[vc++] = poly[0]; out[vc++] = poly[i]; out[vc++] = poly[i + 1];
    }
    return vc;
}
#endif

#ifndef NOVAGL_NO_NEAR_CLIP
typedef struct { float x, y, z, u, v; uint8_t r, g, b, a; } NovaModelV; /* 24 B = [pos3|uv2|col4] */

static inline NovaModelV nova_modelv_lerp(const NovaModelV *a, const NovaModelV *b, float t) {
    NovaModelV o;
    o.x = a->x + t * (b->x - a->x); o.y = a->y + t * (b->y - a->y); o.z = a->z + t * (b->z - a->z);
    o.u = a->u + t * (b->u - a->u); o.v = a->v + t * (b->v - a->v);
    o.r = (uint8_t) (a->r + t * ((float) b->r - a->r));
    o.g = (uint8_t) (a->g + t * ((float) b->g - a->g));
    o.b = (uint8_t) (a->b + t * ((float) b->b - a->b));
    o.a = (uint8_t) (a->a + t * ((float) b->a - a->a));
    return o;
}

/* Clip a triangle (model-space verts + each vertex's clip-space near distance
 * d = z+w) against the near plane, fan-triangulate into out[<=6]. Interpolating
 * the MODEL verts by the clip-space t is exact (MVP is linear, so the model
 * point at parameter t maps to the clip point at t). Returns vertices written. */
static int nova_clip_tri_near_model(const NovaModelV tri[3], const float d[3], NovaModelV out[6]) {
    NovaModelV poly[4];
    int n = 0;
    for (int i = 0; i < 3 && n < 4; i++) {
        int j = (i + 1) % 3;
        float dc = d[i], dn = d[j];
        int ci = dc >= 0.0f, ni = dn >= 0.0f;
        if (ci) poly[n++] = tri[i];
        if (ci != ni && n < 4) {
            float denom = dc - dn;
            float t = (denom != 0.0f) ? (dc / denom) : 0.0f;
            poly[n++] = nova_modelv_lerp(&tri[i], &tri[j], t);
        }
    }
    if (n < 3) return 0;
    int vc = 0;
    for (int i = 1; i + 1 < n; i++) {
        out[vc++] = poly[0]; out[vc++] = poly[i]; out[vc++] = poly[i + 1];
    }
    return vc;
}

/* Near-clip the GPU-MVP triangle path (basic/texmtx/full shaders). `flat` is the
 * de-indexed [pos3|uv2|col4]=24B buffer nova_draw_internal builds. We replay the
 * shader's MVP (final_proj·modelview) on the CPU only to get each vertex's
 * near-plane distance z+w, then clip in MODEL space and redraw via draw_packed_run
 * — i.e. through the SAME bound shader, so fog/texmtx/depth are untouched (unlike
 * a clipspace passthrough, which would drop GL_LINEAR fog and the texture matrix).
 * Returns 1 if it drew the batch (caller must skip its own draw), else 0. */
static int nova_basic_near_clip(GLenum mode, GPU_Primitive_t prim, const uint8_t *flat,
                                int count, int stride, int pos_elements) {
    if (mode != GL_TRIANGLES || count < 3) return 0;
    if (stride != 24 || pos_elements != 3) return 0; /* only the [pos3|uv2|col4] layout */
    if (g.active_shader != NOVA_SHADER_BASIC &&
        g.active_shader != NOVA_SHADER_TEXMTX &&
        g.active_shader != NOVA_SHADER_FULL) return 0;

    /* MVP the active shader applies (valid after apply_gpu_state). Pre-fold the z
     * and w rows into one row 'a' so the near distance is a single dot/vertex:
     * d = z_clip + w_clip = a·(x,y,z,1). */
    C3D_Mtx mvp;
    Mtx_Multiply(&mvp, &g.final_proj_cached, &g.mv_stack[g.mv_sp]);
    const C3D_FVec rz = mvp.r[2], rw = mvp.r[3];
    float ax = rz.x + rw.x, ay = rz.y + rw.y, az = rz.z + rw.z, aw = rz.w + rw.w;

    const NovaModelV *vin = (const NovaModelV *) flat;

    /* Pass 1 — does any vertex cross the near plane (z + w < 0)? */
    int crosses = 0;
    for (int i = 0; i < count; i++) {
        const NovaModelV *p = &vin[i];
        if (ax * p->x + ay * p->y + az * p->z + aw < 0.0f) { crosses = 1; break; }
    }
    if (!crosses) return 0;

    /* Pass 2 — clip the crossing triangles (worst case 2 out tris per input). */
    int in_tris = count / 3;
    int out_bytes = in_tris * 6 * 24;
    if (out_bytes > g.client_array_buf_size) return 0;
    NovaModelV *out = (NovaModelV *) linear_alloc_ring(
        g.client_array_buf, &g.client_array_buf_offset, out_bytes, g.client_array_buf_size);
    /* Ring couldn't fit the clipped output. Returning 0 makes the caller fall
     * through to its own (unclipped) draw path — which will also hit the ring
     * and skip if it can't fit. Either way, no NULL deref. */
    if (!out) return 0;
    int out_count = 0;
    for (int t = 0; t < in_tris; t++) {
        const NovaModelV *tri = &vin[t * 3];
        float d[3];
        for (int k = 0; k < 3; k++) {
            const NovaModelV *p = &tri[k];
            d[k] = ax * p->x + ay * p->y + az * p->z + aw;
        }
        out_count += nova_clip_tri_near_model(tri, d, &out[out_count]);
    }
    if (out_count == 0) return 1; /* whole batch behind near — nothing to draw */

    GSPGPU_FlushDataCache(out, (u32) (out_count * 24));
    draw_packed_run(mode, prim, (uint8_t *) out, out_count, 24, pos_elements);
    return 1;
}
#else
static inline int nova_basic_near_clip(GLenum mode, GPU_Primitive_t prim, const uint8_t *flat,
                                       int count, int stride, int pos_elements) {
    (void) mode; (void) prim; (void) flat; (void) count; (void) stride; (void) pos_elements;
    return 0;
}
#endif

/* -------------------------------------------------------------------------
 * Zero-copy multistream draw.
 *
 * PICA200 reads up to 12 independent vertex streams, each with its own base
 * address and stride, as long as the data lives in linear memory. Every VBO
 * already does (vbo.c allocates with linearAlloc and flushes on upload), so
 * a draw whose enabled arrays are all VBO-backed needs NO per-draw CPU
 * gathering at all -- the exact 13MB/frame of memcpy that capped OpenMW at
 * ~6 FPS. Attributes map to the shader inputs the staged path uses:
 * v0=pos(3f), v1=uv(2f), v2=color, v3=normal (lit shader only). Arrays that
 * are disabled become PICA fixed attributes carrying the current GL state.
 *
 * Falls back (return 0) to the staged path for: client-side arrays, packed
 * PTC slots, flat shading (needs per-face color replication), non-triangle
 * primitives (need CPU expansion), 32-bit indices, the clipspace pipe, and
 * any format PICA loaders cannot fetch.
 * ------------------------------------------------------------------------- */
typedef struct {
    const uint8_t *ptr;
    int stride;
    GPU_FORMATS fmt;
    int comps;
} MsAttr;

/* 1 = streamable, 0 = disabled (use fixed), -1 = enabled but not streamable */
static int ms_resolve_attr(const NovaVertArray *va, int minComps, int maxComps, int firstVertex, MsAttr *out) {
    if (!va->enabled)
        return 0;
    if (va->vbo_id <= 0 || va->vbo_id >= NOVA_MAX_VBOS)
        return -1;
    VBOSlot *slot = &g.vbos[va->vbo_id];
    if (!slot->allocated || !slot->data || vbo_is_packed_ptc(slot))
        return -1;

    GPU_FORMATS fmt;
    int esize;
    switch (va->type) {
        case GL_FLOAT:          fmt = GPU_FLOAT;          esize = 4; break;
        case GL_UNSIGNED_BYTE:  fmt = GPU_UNSIGNED_BYTE;  esize = 1; break;
        case GL_SHORT:          fmt = GPU_SHORT;          esize = 2; break;
        case GL_BYTE:           fmt = GPU_BYTE;           esize = 1; break;
        default:                return -1;
    }
    if (va->size < minComps || va->size > maxComps)
        return -1;
    int stride = va->stride ? va->stride : va->size * esize;
    if (stride <= 0 || stride > 255)
        return -1;
    const uint8_t *p = (const uint8_t *) slot->data + (uintptr_t) va->pointer + (size_t) firstVertex * (size_t) stride;
    /* PICA fetch wants element-aligned bases. */
    if ((uintptr_t) p & (uintptr_t) (esize - 1))
        return -1;
    out->ptr = p;
    out->stride = stride;
    out->fmt = fmt;
    out->comps = va->size;
    return 1;
}

static int try_draw_multistream(GLenum mode, GPU_Primitive_t prim, GLint first, GLsizei count,
                                int is_elements, GLenum type, const uint8_t *idx_src) {
    if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN)
        return 0;
    if (g.shade_model == GL_FLAT)
        return 0;
    if (g.active_shader == NOVA_SHADER_CLIPSPACE)
        return 0;
    if (is_elements && type == GL_UNSIGNED_INT)
        return 0;

    const int firstVertex = is_elements ? 0 : first;
    MsAttr pos, uv, col, nrm;
    if (ms_resolve_attr(&g.va_vertex, 3, 3, firstVertex, &pos) != 1)
        return 0;
    const int lit = g.lighting_active;
    const int uvr = ms_resolve_attr(&g.va_texcoord, 2, 2, firstVertex, &uv);
    const int colr = ms_resolve_attr(&g.va_color, 4, 4, firstVertex, &col);
    const int nrmr = lit ? ms_resolve_attr(&g.va_normal, 3, 3, firstVertex, &nrm) : 0;
    if (uvr < 0 || colr < 0 || nrmr < 0)
        return 0;

    const void *gpuIdx = NULL;
    int c3dType = C3D_UNSIGNED_SHORT;
    if (is_elements) {
        const int esz = (type == GL_UNSIGNED_SHORT) ? 2 : 1;
        c3dType = (type == GL_UNSIGNED_SHORT) ? C3D_UNSIGNED_SHORT : C3D_UNSIGNED_BYTE;
        if (g.bound_element_array_buffer > 0 && g.bound_element_array_buffer < NOVA_MAX_VBOS) {
            gpuIdx = idx_src; /* EBO storage: linear + flushed on upload */
        } else {
            const int ibytes = count * esz;
            if (ibytes > g.index_buf_size)
                return 0;
            void *staged = linear_alloc_ring(g.index_buf, &g.index_buf_offset, ibytes, g.index_buf_size);
            if (!staged)
                return 0; /* index ring can't fit — bail to the staged path */
            memcpy(staged, idx_src, ibytes);
            GSPGPU_FlushDataCache(staged, (u32) ibytes);
            gpuIdx = staged;
        }
    }

    C3D_AttrInfo *ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, pos.fmt, pos.comps);
    if (uvr == 1)
        AttrInfo_AddLoader(ai, 1, uv.fmt, uv.comps);
    else
        AttrInfo_AddFixed(ai, 1);
    if (colr == 1)
        AttrInfo_AddLoader(ai, 2, col.fmt, col.comps);
    else
        AttrInfo_AddFixed(ai, 2);
    if (lit) {
        if (nrmr == 1)
            AttrInfo_AddLoader(ai, 3, nrm.fmt, nrm.comps);
        else
            AttrInfo_AddFixed(ai, 3);
    }

    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    int ok = BufInfo_Add(bi, pos.ptr, pos.stride, 1, 0x0) >= 0;
    if (ok && uvr == 1)
        ok = BufInfo_Add(bi, uv.ptr, uv.stride, 1, 0x1) >= 0;
    if (ok && colr == 1)
        ok = BufInfo_Add(bi, col.ptr, col.stride, 1, 0x2) >= 0;
    if (ok && lit && nrmr == 1)
        ok = BufInfo_Add(bi, nrm.ptr, nrm.stride, 1, 0x3) >= 0;

    /* Either way the shared AttrInfo/BufInfo no longer match the staged
     * path's cached single-buffer layout. */
    nova_invalidate_attr_cache();
    nova_invalidate_buf_cache();
    if (!ok)
        return 0; /* staged fallback reprograms everything */

    if (uvr != 1)
        C3D_FixedAttribSet(1, 0.0f, 0.0f, 0.0f, 1.0f);
    if (colr != 1)
        C3D_FixedAttribSet(2, g.cur_color[0], g.cur_color[1], g.cur_color[2], g.cur_color[3]);
    if (lit && nrmr != 1)
        C3D_FixedAttribSet(3, g.cur_normal[0], g.cur_normal[1], g.cur_normal[2], 0.0f);

    if (is_elements)
        C3D_DrawElements(prim, count, c3dType, gpuIdx);
    else
        C3D_DrawArrays(prim, 0, count);

    nova_wait_tag("[Draw] ms");
    return 1;
}


void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices) {
    /* If this app mixes the clipspace fast-lane with the generic draw path,
     * commit any pending clipspace batch first so draw order is preserved.
     * No-op for pure-clipspace (PD) and pure-generic (other ports) callers. */
    nova_batch_flush();
    /* Spec validation order matters: GL_INVALID_ENUM for bad mode/type,
     * GL_INVALID_VALUE for negative count/first — all before any drawing.
     * Compiled out under NOVAGL_NO_DEBUG: the enum/value checks become pure
     * CPU overhead once a port is known-good, and on the ARM11 they add up
     * across thousands of draws per frame. We keep the count<=0 early-out
     * (a negative count would blow the interleave loop) but drop the error
     * bookkeeping and the per-call enum scans. */
#if defined(NOVAGL_NO_DEBUG) && NOVAGL_NO_DEBUG
    if (count <= 0) return;
#else
    switch (mode) {
        case GL_POINTS: case GL_LINES: case GL_LINE_LOOP: case GL_LINE_STRIP:
        case GL_TRIANGLES: case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN:
        case GL_QUADS: /* desktop-GL leniency for ports */
            break;
        default:
            gl_set_error(GL_INVALID_ENUM);
            return;
    }
    if (count < 0 || first < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (count == 0) return;
#endif
    /* GL_FRONT_AND_BACK culling: spec says ALL triangles are discarded
     * (points/lines unaffected — but NovaGL rasterizes everything as
     * triangles anyway). PICA has no such cull mode; emulate by skipping
     * the draw entirely. Previously this silently behaved like GL_BACK. */
    if (g.cull_face_enabled && g.cull_face_mode == GL_FRONT_AND_BACK) {
        switch (mode) {
            case GL_TRIANGLES: case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN:
            case GL_QUADS:
                return;
            default:
                break;
        }
    }
#if !(defined(NOVAGL_NO_DEBUG) && NOVAGL_NO_DEBUG)
    if (is_elements && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_INT) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
#endif

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

    if (!is_elements && !g.lighting_active &&
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
                gl_set_error(GL_OUT_OF_MEMORY);
                return;
            }
            uint8_t *dst_start = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size,
                                                               g.client_array_buf_size);
            /* Ring can't fit this decoded run — skip the draw (drop geometry)
             * rather than decode into a NULL pointer. */
            if (dst_start) {
                vbo_decode_packed_ptc_span(vbo, first, count, dst_start);
                GSPGPU_FlushDataCache(dst_start, req_size);
                draw_packed_run(mode, prim, dst_start, count, 24, 3);
            }
        } else {

            uint8_t *base = (uint8_t *) vbo->data + first * 24;
            draw_packed_run(mode, prim, base, count, 24, 3);
        }
        /*
        * Deferred FBO Synchronization (Deferred RAW Hazard Resolution)
        * If we have just rendered geometry into the FBO, we mark its
        * texture as "dirty". We do NOT flush the pipeline immediately,
        * so that the GPU can process the command buffer uninterrupted.
        */
        if (g.bound_fbo != 0 && g.bound_fbo < NOVA_MAX_FBOS) {
            GLuint ctex = g.fbos[g.bound_fbo].color_tex_id;
            if (ctex > 0 && ctex < NOVA_MAX_TEXTURES) {
                g.textures[ctex].written_pending_split = 1;
            }
        }

        cleanup_vbo_stream();
        return;
    }

    if (is_elements && !g.lighting_active &&
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
                /* NULL => index ring can't fit; gpu_indices stays NULL and the
                 * `if (gpu_indices)` guard below skips this fast path so the
                 * generic staged path (or a dropped draw) handles it. */
                if (staged) {
                    memcpy(staged, idx_src, ibytes);
                    GSPGPU_FlushDataCache(staged, ibytes);
                    gpu_indices = staged;
                }
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

    if (try_draw_multistream(mode, prim, first, count, is_elements, type, idx_src)) {
        cleanup_vbo_stream();
        return;
    }
    nova_wait_tag("[Draw] staged");

    /* Safe-fallback (change 3): if ANY enabled, VBO-bound attribute array
     * points at a VBO slot that failed to allocate (allocated==0 || data==NULL
     * — e.g. a fresh glBufferData that hit linear exhaustion), SKIP this draw
     * entirely instead of routing its geometry through the gather ring (which
     * under the same exhaustion is exactly what wedges the render traversal).
     * A stale-but-valid VBO (kept by change 1) still has allocated!=0, so it
     * draws normally. The vertex/element VBOs were already guarded above; this
     * also covers texcoord/color/normal so we never gather from an empty slot. */
    {
        const NovaVertArray *arrs[4] = { &g.va_vertex, &g.va_texcoord, &g.va_color, &g.va_normal };
        for (int a = 0; a < 4; a++) {
            const NovaVertArray *va = arrs[a];
            if (va->enabled && va->vbo_id > 0 && va->vbo_id < NOVA_MAX_VBOS) {
                const VBOSlot *s = &g.vbos[va->vbo_id];
                if (!s->allocated || s->data == NULL) {
                    cleanup_vbo_stream();
                    return;
                }
            }
        }
    }

    // Position-element selection.
    //
    // Default: 3. For the full/basic/texmtx shaders we MUST stay on 3 even
    // if the caller bound vec4 — those .pica programs force w=1 explicitly,
    // and a 4-float loader would shift the BufInfo attribute offsets so
    // UV/color get read from the wrong bytes (this is the original
    // tiled-repeat / shear corruption that pinned this to 3 in the first
    // place; Arx's TexturedVertex.w is RHW which is always 1 for the 2D
    // ortho path it used, so dropping w was safe there).
    //
    // Clipspace shader path: when novaBeginClipSpace2D is active and the
    // caller bound vec4 position, take all 4 components. fast3d already
    // did MVP on the CPU and hands us real clip-space xyzw — dropping w
    // would force PICA's perspective divide to a no-op and collapse all
    // 3D geometry. nova_setup_attr_info(4) below configures the loader
    // to match, and the packing loop walks pos_bytes so UV/color land in
    // the right place.
    int pos_elements = (g.active_shader == NOVA_SHADER_CLIPSPACE && g.va_vertex.size == 4) ? 4 : 3;
    int pos_bytes = pos_elements * 4;
    /* Lit draws append a 3-float normal (attr3) after the colour, matching the
     * lighting shader's loader (nova_setup_attr_info_lit). */
    int lit = g.lighting_active;
    int normal_bytes = lit ? 12 : 0;
    int internal_stride = pos_bytes + 12 + normal_bytes;
    int col_offset = pos_bytes + 8;
    int normal_offset = pos_bytes + 12;

    int req_size = count * internal_stride;
    if (req_size > g.client_array_buf_size) {
        gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    uint8_t *dst = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset, req_size,
                                                 g.client_array_buf_size);
    /* Ring exhausted and this draw can't fit even after a wrap (a single draw
     * bigger than the whole ring). SKIP the entire gather+draw rather than
     * write the interleave loop into a NULL pointer — this is the wedge the
     * safe-fallback rework fixes: drop this batch's geometry for one frame
     * instead of hanging the render traversal. */
    if (!dst) {
        gl_set_error(GL_OUT_OF_MEMORY);
        cleanup_vbo_stream();
        return;
    }
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
    int n_str = calc_stride(g.va_normal.stride, g.va_normal.size, g.va_normal.type);

    const uint8_t *src_n = (g.va_normal.vbo_id > 0 && g.va_normal.vbo_id < NOVA_MAX_VBOS &&
                            g.vbos[g.va_normal.vbo_id].allocated)
                               ? (const uint8_t *) g.vbos[g.va_normal.vbo_id].data + (uintptr_t) g.va_normal.pointer
                               : (const uint8_t *) g.va_normal.pointer;

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
        } else if (g.va_vertex.enabled && src_v && NOVA_PTR_OK(src_v)) {
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
        } else if (g.va_texcoord.enabled && src_t && NOVA_PTR_OK(src_t)) {
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
        } else if (g.va_color.enabled && src_c && NOVA_PTR_OK(src_c)) {
            const uint8_t *c_ptr = src_c + src_index * c_str;
            if (g.va_color.type == GL_UNSIGNED_BYTE) {
                memcpy(dst + col_offset, c_ptr, g.va_color.size == 3 ? 3 : 4);
                if (g.va_color.size == 3) dst[col_offset + 3] = 255;
            } else if (g.va_color.type == GL_FLOAT) {
                const float *cf = (const float *) c_ptr;
                dst[col_offset + 0] = (uint8_t) (clampf(cf[0], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 1] = (uint8_t) (clampf(cf[1], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 2] = (uint8_t) (clampf(cf[2], 0.0f, 1.0f) * 255.0f);
                dst[col_offset + 3] = g.va_color.size >= 4 ? (uint8_t) (clampf(cf[3], 0.0f, 1.0f) * 255.0f) : 255;
            } else if (g.va_color.type == GL_FIXED) {
                /* ES 1.1 allows GL_FIXED colors (16.16). Previously these fell
                 * into the raw-memcpy branch and produced garbage colors. */
                const int32_t *cx = (const int32_t *) c_ptr;
                for (int ch = 0; ch < 4; ch++) {
                    float v = (ch < g.va_color.size) ? ((float) cx[ch] / 65536.0f) : 1.0f;
                    dst[col_offset + ch] = (uint8_t) (clampf(v, 0.0f, 1.0f) * 255.0f);
                }
            } else {
                /* BYTE/SHORT/INT (signed-normalized) and UNSIGNED_SHORT/INT
                 * (unsigned-normalized) colour arrays — normalize per the GL
                 * fixed->float rules, then pack to 0..255. Rare types, kept off
                 * the UB/FLOAT fast paths above. */
                for (int ch = 0; ch < 4; ch++) {
                    float v;
                    if (ch < g.va_color.size) {
                        switch (g.va_color.type) {
                            case GL_BYTE:  v = (2.0f*(float)((const int8_t  *)c_ptr)[ch] + 1.0f) / 255.0f; break;
                            case GL_SHORT: v = (2.0f*(float)((const int16_t *)c_ptr)[ch] + 1.0f) / 65535.0f; break;
                            case GL_INT:   v = (2.0f*(float)((const int32_t *)c_ptr)[ch] + 1.0f) / 4294967295.0f; break;
                            case GL_UNSIGNED_SHORT: v = (float)((const uint16_t *)c_ptr)[ch] / 65535.0f; break;
                            case GL_UNSIGNED_INT:   v = (float)((const uint32_t *)c_ptr)[ch] / 4294967295.0f; break;
                            default: v = 0.0f; break;
                        }
                    } else {
                        v = 1.0f; /* missing alpha defaults to 1 */
                    }
                    dst[col_offset + ch] = (uint8_t) (clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        } else {
            *(uint32_t*)(dst + col_offset) = g.cur_color_packed;
        }

        // Normal (lit draws only). Read as float3; falls back to the current
        // glNormal3f when no array supplies it, default +Z otherwise.
        if (lit) {
            float nrm[3] = {g.cur_normal[0], g.cur_normal[1], g.cur_normal[2]};
            if (g.va_normal.enabled && src_n && NOVA_PTR_OK(src_n)) {
                read_vertex_attrib_float(nrm, src_n + src_index * n_str,
                                         g.va_normal.size >= 3 ? 3 : g.va_normal.size, g.va_normal.type);
            }
            memcpy(dst + normal_offset, nrm, 12);
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
    if (lit) {
        /* 4-attribute layout (pos, texcoord, color, normal). buf_info needs the
         * matching permutation 0x3210, so set it directly rather than via the
         * 3-attr nova_setup_buf_info. */
        nova_setup_attr_info_lit();
        C3D_BufInfo *bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo, dst_start, internal_stride, 4, 0x3210);
        nova_invalidate_buf_cache(); /* bypassed the buf cache; keep it honest */
        if (mode == GL_QUADS) {
            draw_emulated_quads(count);
        } else {
            C3D_DrawArrays(prim, 0, count);
        }
    } else {
        /* GPU-MVP path: near-clip triangles that straddle the near plane (e.g.
         * Wolfenstein-RPG walls under the real projection) before they reach
         * PICA, which can't near-clip them. Falls through to the normal draw
         * when nothing crosses near (the common case). */
        if (!nova_basic_near_clip(mode, prim, dst_start, count, internal_stride, pos_elements)) {
            draw_packed_run(mode, prim, dst_start, count, internal_stride, pos_elements);
        }
    }
    cleanup_vbo_stream();
}

GLenum glGetError(void) {
    GLenum e = g.last_error;
    g.last_error = GL_NO_ERROR;
    return e;
}

/* NovaClipV + nova_clip_tri_near + nova_basic_near_clip are defined above
 * nova_draw_internal (both draw paths share them). */

void novaDrawClipspaceTris(const void *verts, int vertex_count) {
    if (!verts || vertex_count <= 0) return;
    if (g.cull_face_enabled && g.cull_face_mode == GL_FRONT_AND_BACK) return;

#if !NOVAGL_BATCH_CLIPSPACE
    /* Batcher runs apply_gpu_state once per run (see nova_batch_submit); the
     * immediate path applies it per draw. */
    apply_gpu_state();
#endif

#ifndef NOVAGL_NO_CLIPSPACE_NEAR_CLIP
    /* Does ANY vertex cross the near plane (z + w < 0)? If not, skip clipping. */
    int any_near = 0;
    const NovaClipV *in_v = (const NovaClipV *) verts;
    for (int i = 0; i < vertex_count; i++) {
        if (in_v[i].z + in_v[i].w < 0.0f) { any_near = 1; break; }
    }
    if (any_near) {
        int in_tris = vertex_count / 3;
        int max_out = in_tris * 6;                 /* <=2 output tris per input */
        int out_bytes = max_out * 28;
        if (out_bytes <= g.client_array_buf_size) {  /* else fall through to no-clip */
#if NOVAGL_BATCH_CLIPSPACE
            /* The clipped stream is variable-length and never batched: commit
             * the pending run first (the GPU still holds its state), then apply
             * state for this standalone clipped draw. */
            nova_batch_flush();
            apply_gpu_state();
#endif
            NovaClipV *cdst = (NovaClipV *) linear_alloc_ring(
                g.client_array_buf, &g.client_array_buf_offset, out_bytes, g.client_array_buf_size);
            /* Ring can't fit the clipped output — drop this draw rather than
             * clip into NULL. */
            if (!cdst) return;
            int out_count = 0;
            for (int t = 0; t < in_tris; t++) {
                out_count += nova_clip_tri_near(&in_v[t * 3], &cdst[out_count]);
            }
            if (out_count == 0) return;            /* whole batch behind near */
            GSPGPU_FlushDataCache(cdst, (u32) (out_count * 28));
            nova_setup_attr_info(4);
            nova_setup_buf_info(cdst, 28);
            C3D_DrawArrays(GPU_TRIANGLES, 0, out_count);
            return;
        }
    }
#endif

    const int bytes = vertex_count * 28;
    if (bytes > g.client_array_buf_size) {
        gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }
#if NOVAGL_BATCH_CLIPSPACE
    /* Defer + coalesce: appended to the open run, or flushes the old run and
     * starts a new one. The actual C3D_DrawArrays happens in nova_batch_flush
     * (at the next state change or a barrier). */
    nova_batch_submit(verts, vertex_count);
#else
    uint8_t *dst = (uint8_t *) linear_alloc_ring(g.client_array_buf, &g.client_array_buf_offset,
                                                 bytes, g.client_array_buf_size);
    /* Ring can't fit this passthrough batch — drop it rather than memcpy into
     * NULL. */
    if (dst) {
        memcpy(dst, verts, (size_t) bytes);
        GSPGPU_FlushDataCache(dst, (u32) bytes);

        /* pos 4×float + uv 2×float + colour 4×u8 — exactly the loader layout
         * nova_setup_attr_info(4) configures, 28-byte stride. */
        nova_setup_attr_info(4);
        nova_setup_buf_info(dst, 28);
        C3D_DrawArrays(GPU_TRIANGLES, 0, vertex_count);
    }
#endif
}

void glFlush(void) {
    nova_batch_flush();   /* commit deferred draws before the split */
    /* Empty split = no P3D interrupt; also nothing to flush. */
    if (g.p3d_pending)
        C3D_FrameSplit(0);
}

void glFinish(void) {
    nova_batch_flush();   /* commit deferred draws before the split */
    /* Wait ONLY when work was actually submitted: splitting an empty command
     * list raises no P3D interrupt and gspWaitForP3D() then never returns
     * (the previous frame's event was already consumed by novaSwapBuffers).
     * OSG calls glFinish on RTT paths before any draw touched the frame. */
    if (g.p3d_pending) {
        nova_wait_tag("[W] glFinish>");
        /* See linear_alloc_ring: raw gspWaitForP3D never wakes while the
         * citro3d render queue owns the P3D callback. FrameEnd+SYNCDRAW
         * begin is the sanctioned mid-stream drain. */
        C3D_FrameEnd(0);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        if (g.current_target)
            C3D_FrameDrawOn(g.current_target);
        nova_wait_tag("[W] glFinish<");
        g.p3d_pending = 0;
    }
}