//
// Created by efimandreev0 on 05.04.2026.
//

#ifndef NOVAGL_UTILS_H
#define NOVAGL_UTILS_H
#include <stdint.h>

static inline unsigned int next_pow2(unsigned int v);

static void* linear_alloc_ring(void *base, int *offset, int size, int capacity);

static inline float clampf(float x, float lo, float hi);

static void dl_record_translate(float x, float y, float z);

static void dl_record_color3f(float r, float g_, float b);

static void dl_execute(GLuint list);

static GPU_TESTFUNC gl_to_gpu_alpha_testfunc(GLenum func);

static GPU_TESTFUNC gl_to_gpu_depth_testfunc(GLenum func);

static GPU_TESTFUNC gl_to_gpu_testfunc(GLenum func);

static GPU_BLENDFACTOR gl_to_gpu_blendfactor(GLenum factor);

static int gl_type_size(GLenum type);

static int calc_stride(GLsizei stride, GLint size, GLenum type);

static void read_vertex_attrib_float(float *dst, const uint8_t *src, GLint size, GLenum type);

static GPU_Primitive_t gl_to_gpu_primitive(GLenum mode);

static GPU_TEXCOLOR gl_to_gpu_texfmt(GLenum format, GLenum type);

static int gpu_texfmt_bpp(GPU_TEXCOLOR fmt);

static C3D_Mtx* cur_mtx(void);

static int* cur_sp(void);

static C3D_Mtx* cur_stack(void);

static void* get_tex_staging(int size);

static inline uint32_t morton_interleave(uint32_t x, uint32_t y);

static void swizzle_16bit(uint16_t *dst, const uint16_t *src, int src_w, int src_h, int pot_w, int pot_h);

static void swizzle_rgba8(uint32_t *dst, const uint32_t *src, int src_w, int src_h, int pot_w, int pot_h);

static uint32_t* rgb_to_rgba(const uint8_t *rgb, int w, int h);

static void apply_depth_map(void);

static void apply_gpu_state(void);

static inline void cleanup_vbo_stream(void);

static inline void draw_emulated_quads(int count);
#endif //NOVAGL_UTILS_H