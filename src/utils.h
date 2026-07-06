//
// Created by efimandreev0 on 05.04.2026.
//

#ifndef NOVAGL_UTILS_H
#define NOVAGL_UTILS_H
#include <stdint.h>
#include "context.h"

unsigned int nova_next_pow2(unsigned int v);

/* Re-home an existing texture slot's storage in VRAM so it can be wrapped as a
 * PICA render target by glFramebufferTexture2D. No-op if already VRAM-backed.
 * Returns 1 on success, 0 on failure. Defined in texture.c. */
int nova_texture_make_vram_target(GLuint texture);

/* GPU path for glCopyTex(Sub)Image2D: draw the READ framebuffer's (x,y,w,h)
 * rect into texture `tex_id` via a render-target quad (Butterscotch's
 * ctr_create_surf_ex pattern). Full-destination copies only. Returns 1 when
 * copied, 0 when the caller must fall back to the CPU loop. framebuffer.c. */
int nova_copy_read_fb_to_texture(GLuint tex_id, GLint xoffset, GLint yoffset,
                                 GLint x, GLint y, GLsizei w, GLsizei h);

/* Orphan (GC-delete) every FBO render target wrapping texture `tex_id`'s
 * current storage. MUST be called before that storage is freed/re-created or
 * the target keeps rendering into freed memory. Attachments survive (GL
 * re-spec semantics) — targets are re-wired on the next bind. framebuffer.c. */
void nova_fbo_orphan_texture_targets(GLuint tex_id);

void *linear_alloc_ring(void *base, int *offset, int size, int capacity);
/* Wait-site tagger: breadcrumbs into the host log around every potentially
 * blocking GPU wait. Capped so steady-state frames don't spam. */
void nova_wait_tag(const char *tag);
void nova_wait_tag_arm(int budget);
void nova_invalidate_attr_cache(void);
/* Free ring buffers orphaned by adaptive growth K frames ago (call at swap,
 * after rotating g.frame_slot — mirrors the texture/FBO GC). */
void nova_ring_gc_collect(void);

float clampf(float x, float lo, float hi);

void dl_record_translate(float x, float y, float z);

void dl_record_color3f(float r, float g_, float b);

void dl_record_color4f(float r, float g_, float b, float a);

void dl_execute(GLuint list);

GPU_TESTFUNC gl_to_gpu_alpha_testfunc(GLenum func);

GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func);

GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func);

GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor);

GPU_BLENDEQUATION gl_to_gpu_blendeq(GLenum mode);

GPU_LOGICOP gl_to_gpu_logicop(GLenum op);

int gl_type_size(GLenum type);

int calc_stride(GLsizei stride, GLint size, GLenum type);

void read_vertex_attrib_float(float *dst, const uint8_t *src, GLint size, GLenum type);

GPU_Primitive_t gl_to_gpu_primitive(GLenum mode);

GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type);

int gpu_texfmt_bpp(GPU_TEXCOLOR fmt);

int vbo_is_packed_ptc(const VBOSlot *slot);

void vbo_decode_packed_ptc_vertex(const VBOSlot *slot, int vertex_index, uint8_t *out_vertex);

void vbo_decode_packed_ptc_span(const VBOSlot *slot, int first_vertex, int vertex_count, uint8_t *dst);

void vbo_convert_slot_to_raw(VBOSlot *slot);

C3D_Mtx *cur_mtx(void);

int *cur_sp(void);

C3D_Mtx *cur_stack(void);

void *get_tex_staging(int size);

/* Deferred texture deletion (orphaning GC in texture.c). Push retires a
 * C3D_Tex whose storage may still be referenced by THIS frame's queued
 * draws — it is deleted later instead of immediately (the struct is copied;
 * *tex is zeroed). Collect reclaims everything pushed during the previous
 * frame; call ONLY when the GPU is known to be done with it —
 * novaSwapBuffers does it right after C3D_FrameBegin's SYNCDRAW wait. */
void nova_tex_gc_push(C3D_Tex *tex);
void nova_tex_gc_collect(void);
void nova_tex_gc_collect_all(void);

/* Drop the per-unit TexBind skip-cache entries referring to `tex_id` so the
 * next draw re-emits the bind. MUST be called whenever a texture's storage
 * is re-created under the same id (the C3D_Tex data pointer changed). */
void nova_invalidate_tex_bind(GLuint tex_id);

uint32_t morton_interleave(uint32_t x, uint32_t y);

void swizzle_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int pot_w, int pot_h);

void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h);

void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h);

uint32_t *rgb_to_rgba(const uint8_t *rgb, int w, int h);

void downscale_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int dst_w, int dst_h);

void downscale_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int dst_w, int dst_h);

void downscale_8bit(uint8_t *dst, const uint8_t *src, int src_w, int src_h, int dst_w, int dst_h);

void apply_depth_map(void);

void apply_gpu_state(void);


/* ---- P3D-pending tracking -----------------------------------------------
 * Every draw submission must set g.p3d_pending so glFinish()/GC sync points
 * know whether a FrameSplit will actually produce a P3D interrupt (splitting
 * an empty command list raises none, and waiting on it deadlocks). The
 * function-like macros below shadow the citro3d names inside NovaGL only;
 * the parenthesised calls inside the shims bypass macro expansion. */
static inline void nova_c3d_draw_arrays_(GPU_Primitive_t primitive, int first, int size)
{
    g.p3d_pending = 1;
    (C3D_DrawArrays)(primitive, first, size);
}
static inline void nova_c3d_draw_elements_(GPU_Primitive_t primitive, int count, int type, const void* indices)
{
    g.p3d_pending = 1;
    (C3D_DrawElements)(primitive, count, type, indices);
}
#define C3D_DrawArrays(primitive, first, size) nova_c3d_draw_arrays_((primitive), (first), (size))
#define C3D_DrawElements(primitive, count, type, indices) nova_c3d_draw_elements_((primitive), (count), (type), (indices))

/* ---- Clipspace draw-call batcher (utils.c) -----------------------------
 * Coalesces consecutive novaDrawClipspaceTris() calls whose ENTIRE draw-
 * affecting GPU state is identical into a single C3D_DrawArrays — cuts the
 * per-draw-call cost that dominates scenes with many small same-material
 * draws (sparks, explosion parts, >2 characters). Default ON; opt out with
 * -DNOVAGL_BATCH_CLIPSPACE=0.
 *
 * Only novaDrawClipspaceTris (the Perfect Dark / fast3d clip-space path)
 * feeds it. Every other draw path and every render-target / frame / readback
 * site just calls nova_batch_flush(), which is a no-op unless a batch is open,
 * so other NovaGL consumers are unaffected. */
#ifndef NOVAGL_BATCH_CLIPSPACE
#define NOVAGL_BATCH_CLIPSPACE 1
#endif
/* Emit any pending batch NOW (no-op when empty). MUST be called before any
 * render-target switch, frame split/end, buffer clear, framebuffer readback,
 * or vertex-ring reset — see the barrier list in utils.c. */
void nova_batch_flush(void);
/* Append a clip-space triangle list to the current batch, merging into the
 * open run when state is unchanged or starting a new run (flush + one
 * apply_gpu_state) otherwise. Caller must have ruled out the near-clip case. */
void nova_batch_submit(const void *verts, int vertex_count);

void cleanup_vbo_stream(void);

void draw_emulated_quads(int count);

void nova_setup_attr_info(int pos_elements);

/* Lit-draw attribute/buffer layout: adds a normal loader (attr3, 3 floats)
 * after pos/texcoord/color. */
void nova_setup_attr_info_lit(void);

void nova_invalidate_buf_cache(void);

void nova_apply_light_env(void);

void nova_setup_buf_info(void *base, int stride);

static GPU_TEVSRC get_tev_src(GLint gl_src, GPU_TEVSRC tex_src, GPU_TEVSRC prev_src);

static int get_tev_op_rgb(GLint gl_op);

void nova_hardware_swizzle(C3D_Tex *tex, const void *linear_pixels, int width, int height, GPU_TEXCOLOR format);
#endif //NOVAGL_UTILS_H
