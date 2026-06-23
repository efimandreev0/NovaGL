//
// Created by efimandreev0 on 05.04.2026.
//

#ifndef NOVAGL_UTILS_H
#define NOVAGL_UTILS_H
#include <stdint.h>
#include "context.h"

unsigned int nova_next_pow2(unsigned int v);

void *linear_alloc_ring(void *base, int *offset, int size, int capacity);

float clampf(float x, float lo, float hi);

void dl_record_translate(float x, float y, float z);

void dl_record_color3f(float r, float g_, float b);

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
