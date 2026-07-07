//
// Created by Pugemon on 07.07.2026.
//

#ifndef NOVAGL_VFP_MATH_H
#define NOVAGL_VFP_MATH_H

#include "NovaGL.h"
#include <3ds.h>

/* =========================================================================
 * VFPv2 Assembly Matrix Core
 * =========================================================================
 * PICA200/ARM11 uses a Vector Floating Point (VFPv2) coprocessor.
 * Standard C matrix multiplication generates excessive memory load/stores
 * due to register spilling. These inline assembly functions bypass the stack
 * completely, loading matrices directly into the 32 VFP registers (s0-s31),
 * computing the result in-place using fused multiply-accumulate (vmla),
 * and storing the result back via burst memory access (vstmia).
 *
 * NOTE: C3D_Mtx stores vectors BACKWARDS in memory: [W, Z, Y, X] (W is offset
 * 0).
 * ========================================================================= */

// Loads a standard OpenGL column-major matrix into a row-major C3D_Mtx (WZYX
// layout)
static inline void vfp_load_matrix(C3D_Mtx *dst, const GLfloat *m) {
  __asm__ __volatile__("vldmia %[b], {s16-s31} \n\t"

                       "vmov s0, s28 \n\t"
                       "vmov s1, s24 \n\t"
                       "vmov s2, s20 \n\t"
                       "vmov s3, s16 \n\t"
                       "vstmia %[a]!, {s0-s3} \n\t"

                       "vmov s0, s29 \n\t"
                       "vmov s1, s25 \n\t"
                       "vmov s2, s21 \n\t"
                       "vmov s3, s17 \n\t"
                       "vstmia %[a]!, {s0-s3} \n\t"

                       "vmov s0, s30 \n\t"
                       "vmov s1, s26 \n\t"
                       "vmov s2, s22 \n\t"
                       "vmov s3, s18 \n\t"
                       "vstmia %[a]!, {s0-s3} \n\t"

                       "vmov s0, s31 \n\t"
                       "vmov s1, s27 \n\t"
                       "vmov s2, s23 \n\t"
                       "vmov s3, s19 \n\t"
                       "vstmia %[a]!, {s0-s3} \n\t"

                       "sub %[a], %[a], #64 \n\t"
                       : [a] "+r"(dst)
                       : [b] "r"(m)
                       : "memory", "s0", "s1", "s2", "s3", "s16", "s17", "s18",
                         "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26",
                         "s27", "s28", "s29", "s30", "s31");
}

// Pipeline-interleaved multiplication: C3D_Mtx (WZYX) = C3D_Mtx (WZYX) *
// GLfloat (XYZW)
#define VFP_INTERLEAVED_MUL_GL                                                 \
  "vldmia %[dst], {s0-s3} \n\t"                                                \
  "vmul.f32 s4, s3, s28 \n\t"                                                  \
  "vmul.f32 s5, s3, s24 \n\t"                                                  \
  "vmul.f32 s6, s3, s20 \n\t"                                                  \
  "vmul.f32 s7, s3, s16 \n\t"                                                  \
  "vmla.f32 s4, s2, s29 \n\t"                                                  \
  "vmla.f32 s5, s2, s25 \n\t"                                                  \
  "vmla.f32 s6, s2, s21 \n\t"                                                  \
  "vmla.f32 s7, s2, s17 \n\t"                                                  \
  "vmla.f32 s4, s1, s30 \n\t"                                                  \
  "vmla.f32 s5, s1, s26 \n\t"                                                  \
  "vmla.f32 s6, s1, s22 \n\t"                                                  \
  "vmla.f32 s7, s1, s18 \n\t"                                                  \
  "vmla.f32 s4, s0, s31 \n\t"                                                  \
  "vmla.f32 s5, s0, s27 \n\t"                                                  \
  "vmla.f32 s6, s0, s23 \n\t"                                                  \
  "vmla.f32 s7, s0, s19 \n\t"                                                  \
  "vstmia %[dst]!, {s4-s7} \n\t"

static inline void vfp_mult_gl_matrix(C3D_Mtx *__restrict__ dst,
                                      const GLfloat *__restrict__ src_gl) {
  C3D_Mtx *ptr = dst;
  __asm__ __volatile__(
      "vldmia %[src], {s16-s31} \n\t" VFP_INTERLEAVED_MUL_GL
          VFP_INTERLEAVED_MUL_GL VFP_INTERLEAVED_MUL_GL VFP_INTERLEAVED_MUL_GL
      : [dst] "+r"(ptr), "+m"(*dst)
      : [src] "r"(src_gl)
      : "memory", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s16", "s17",
        "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26", "s27",
        "s28", "s29", "s30", "s31");
}

// Pipeline-interleaved multiplication: C3D_Mtx = C3D_Mtx * C3D_Mtx
#define VFP_INTERLEAVED_MUL_MTX                                                \
  "vldmia %[a], {s0-s3} \n\t"                                                  \
  "vmul.f32 s4, s3, s16 \n\t"                                                  \
  "vmul.f32 s5, s3, s17 \n\t"                                                  \
  "vmul.f32 s6, s3, s18 \n\t"                                                  \
  "vmul.f32 s7, s3, s19 \n\t"                                                  \
  "vmla.f32 s4, s2, s20 \n\t"                                                  \
  "vmla.f32 s5, s2, s21 \n\t"                                                  \
  "vmla.f32 s6, s2, s22 \n\t"                                                  \
  "vmla.f32 s7, s2, s23 \n\t"                                                  \
  "vmla.f32 s4, s1, s24 \n\t"                                                  \
  "vmla.f32 s5, s1, s25 \n\t"                                                  \
  "vmla.f32 s6, s1, s26 \n\t"                                                  \
  "vmla.f32 s7, s1, s27 \n\t"                                                  \
  "vmla.f32 s4, s0, s28 \n\t"                                                  \
  "vmla.f32 s5, s0, s29 \n\t"                                                  \
  "vmla.f32 s6, s0, s30 \n\t"                                                  \
  "vmla.f32 s7, s0, s31 \n\t"                                                  \
  "vstmia %[a]!, {s4-s7} \n\t"

static inline void vfp_mult_mtx_mtx(C3D_Mtx *__restrict__ dst,
                                    const C3D_Mtx *__restrict__ rhs) {
  C3D_Mtx *ptr = dst;
  __asm__ __volatile__("vldmia %[b], {s16-s31} \n\t" VFP_INTERLEAVED_MUL_MTX
                           VFP_INTERLEAVED_MUL_MTX VFP_INTERLEAVED_MUL_MTX
                               VFP_INTERLEAVED_MUL_MTX
                       : [a] "+r"(ptr), "+m"(*dst)
                       : [b] "r"(rhs)
                       : "memory", "s0", "s1", "s2", "s3", "s4", "s5", "s6",
                         "s7", "s16", "s17", "s18", "s19", "s20", "s21", "s22",
                         "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30",
                         "s31");
}

#undef VFP_INTERLEAVED_MUL_GL
#undef VFP_INTERLEAVED_MUL_MTX

static inline void vfp_translate(C3D_Mtx *__restrict__ m, GLfloat x, GLfloat y,
                                 GLfloat z) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmla.f32 s4, s7, %[x] \n\t"
                       "vmla.f32 s4, s6, %[y] \n\t"
                       "vmla.f32 s4, s5, %[z] \n\t"
                       "vmla.f32 s8, s11, %[x] \n\t"
                       "vmla.f32 s8, s10, %[y] \n\t"
                       "vmla.f32 s8, s9, %[z] \n\t"
                       "vmla.f32 s12, s15, %[x] \n\t"
                       "vmla.f32 s12, s14, %[y] \n\t"
                       "vmla.f32 s12, s13, %[z] \n\t"
                       "vmla.f32 s16, s19, %[x] \n\t"
                       "vmla.f32 s16, s18, %[y] \n\t"
                       "vmla.f32 s16, s17, %[z] \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [x] "t"(x), [y] "t"(y), [z] "t"(z)
                       : "memory", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
                         "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18",
                         "s19");
}

static inline void vfp_scale(C3D_Mtx *__restrict__ m, GLfloat x, GLfloat y,
                             GLfloat z) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmul.f32 s7, s7, %[x] \n\t"
                       "vmul.f32 s6, s6, %[y] \n\t"
                       "vmul.f32 s5, s5, %[z] \n\t"
                       "vmul.f32 s11, s11, %[x] \n\t"
                       "vmul.f32 s10, s10, %[y] \n\t"
                       "vmul.f32 s9, s9, %[z] \n\t"
                       "vmul.f32 s15, s15, %[x] \n\t"
                       "vmul.f32 s14, s14, %[y] \n\t"
                       "vmul.f32 s13, s13, %[z] \n\t"
                       "vmul.f32 s19, s19, %[x] \n\t"
                       "vmul.f32 s18, s18, %[y] \n\t"
                       "vmul.f32 s17, s17, %[z] \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [x] "t"(x), [y] "t"(y), [z] "t"(z)
                       : "memory", "s5", "s6", "s7", "s9", "s10", "s11", "s13",
                         "s14", "s15", "s17", "s18", "s19");
}

static inline void vfp_scale_translate(C3D_Mtx *__restrict__ m, GLfloat sx,
                                       GLfloat sy, GLfloat sz, GLfloat tx,
                                       GLfloat ty, GLfloat tz) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmla.f32 s4, s7, %[tx] \n\t"
                       "vmla.f32 s4, s6, %[ty] \n\t"
                       "vmla.f32 s4, s5, %[tz] \n\t"
                       "vmul.f32 s7, s7, %[sx] \n\t"
                       "vmul.f32 s6, s6, %[sy] \n\t"
                       "vmul.f32 s5, s5, %[sz] \n\t"
                       "vmla.f32 s8, s11, %[tx] \n\t"
                       "vmla.f32 s8, s10, %[ty] \n\t"
                       "vmla.f32 s8, s9, %[tz] \n\t"
                       "vmul.f32 s11, s11, %[sx] \n\t"
                       "vmul.f32 s10, s10, %[sy] \n\t"
                       "vmul.f32 s9, s9, %[sz] \n\t"
                       "vmla.f32 s12, s15, %[tx] \n\t"
                       "vmla.f32 s12, s14, %[ty] \n\t"
                       "vmla.f32 s12, s13, %[tz] \n\t"
                       "vmul.f32 s15, s15, %[sx] \n\t"
                       "vmul.f32 s14, s14, %[sy] \n\t"
                       "vmul.f32 s13, s13, %[sz] \n\t"
                       "vmla.f32 s16, s19, %[tx] \n\t"
                       "vmla.f32 s16, s18, %[ty] \n\t"
                       "vmla.f32 s16, s17, %[tz] \n\t"
                       "vmul.f32 s19, s19, %[sx] \n\t"
                       "vmul.f32 s18, s18, %[sy] \n\t"
                       "vmul.f32 s17, s17, %[sz] \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [sx] "t"(sx), [sy] "t"(sy), [sz] "t"(sz),
                         [tx] "t"(tx), [ty] "t"(ty), [tz] "t"(tz)
                       : "memory", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
                         "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18",
                         "s19");
}

static inline void vfp_apply_frustum(C3D_Mtx *__restrict__ m, GLfloat A,
                                     GLfloat B, GLfloat C, GLfloat D, GLfloat E,
                                     GLfloat F) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmul.f32 s0, s7, %[C] \n\t"
                       "vmla.f32 s0, s6, %[D] \n\t"
                       "vmla.f32 s0, s5, %[E] \n\t"
                       "vsub.f32 s0, s0, s4 \n\t"
                       "vmul.f32 s1, s5, %[F] \n\t"
                       "vmul.f32 s7, s7, %[A] \n\t"
                       "vmul.f32 s6, s6, %[B] \n\t"
                       "vmov s5, s0 \n\t"
                       "vmov s4, s1 \n\t"
                       "vmul.f32 s0, s11, %[C] \n\t"
                       "vmla.f32 s0, s10, %[D] \n\t"
                       "vmla.f32 s0, s9, %[E] \n\t"
                       "vsub.f32 s0, s0, s8 \n\t"
                       "vmul.f32 s1, s9, %[F] \n\t"
                       "vmul.f32 s11, s11, %[A] \n\t"
                       "vmul.f32 s10, s10, %[B] \n\t"
                       "vmov s9, s0 \n\t"
                       "vmov s8, s1 \n\t"
                       "vmul.f32 s0, s15, %[C] \n\t"
                       "vmla.f32 s0, s14, %[D] \n\t"
                       "vmla.f32 s0, s13, %[E] \n\t"
                       "vsub.f32 s0, s0, s12 \n\t"
                       "vmul.f32 s1, s13, %[F] \n\t"
                       "vmul.f32 s15, s15, %[A] \n\t"
                       "vmul.f32 s14, s14, %[B] \n\t"
                       "vmov s13, s0 \n\t"
                       "vmov s12, s1 \n\t"
                       "vmul.f32 s0, s19, %[C] \n\t"
                       "vmla.f32 s0, s18, %[D] \n\t"
                       "vmla.f32 s0, s17, %[E] \n\t"
                       "vsub.f32 s0, s0, s16 \n\t"
                       "vmul.f32 s1, s17, %[F] \n\t"
                       "vmul.f32 s19, s19, %[A] \n\t"
                       "vmul.f32 s18, s18, %[B] \n\t"
                       "vmov s17, s0 \n\t"
                       "vmov s16, s1 \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [A] "t"(A), [B] "t"(B), [C] "t"(C),
                         [D] "t"(D), [E] "t"(E), [F] "t"(F)
                       : "memory", "s0", "s1", "s4", "s5", "s6", "s7", "s8",
                         "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16",
                         "s17", "s18", "s19");
}

static inline void vfp_rotate_x(C3D_Mtx *__restrict__ m, GLfloat c, GLfloat s) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmul.f32 s0, s6, %[c] \n\t"
                       "vmla.f32 s0, s5, %[s] \n\t"
                       "vmul.f32 s1, s5, %[c] \n\t"
                       "vmls.f32 s1, s6, %[s] \n\t"
                       "vmov s6, s0 \n\t"
                       "vmov s5, s1 \n\t"
                       "vmul.f32 s0, s10, %[c] \n\t"
                       "vmla.f32 s0, s9, %[s] \n\t"
                       "vmul.f32 s1, s9, %[c] \n\t"
                       "vmls.f32 s1, s10, %[s] \n\t"
                       "vmov s10, s0 \n\t"
                       "vmov s9, s1 \n\t"
                       "vmul.f32 s0, s14, %[c] \n\t"
                       "vmla.f32 s0, s13, %[s] \n\t"
                       "vmul.f32 s1, s13, %[c] \n\t"
                       "vmls.f32 s1, s14, %[s] \n\t"
                       "vmov s14, s0 \n\t"
                       "vmov s13, s1 \n\t"
                       "vmul.f32 s0, s18, %[c] \n\t"
                       "vmla.f32 s0, s17, %[s] \n\t"
                       "vmul.f32 s1, s17, %[c] \n\t"
                       "vmls.f32 s1, s18, %[s] \n\t"
                       "vmov s18, s0 \n\t"
                       "vmov s17, s1 \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [c] "t"(c), [s] "t"(s)
                       : "memory", "s0", "s1", "s4", "s5", "s6", "s7", "s8",
                         "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16",
                         "s17", "s18", "s19");
}

static inline void vfp_rotate_y(C3D_Mtx *__restrict__ m, GLfloat c, GLfloat s) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmul.f32 s0, s7, %[c] \n\t"
                       "vmls.f32 s0, s5, %[s] \n\t"
                       "vmul.f32 s1, s7, %[s] \n\t"
                       "vmla.f32 s1, s5, %[c] \n\t"
                       "vmov s7, s0 \n\t"
                       "vmov s5, s1 \n\t"
                       "vmul.f32 s0, s11, %[c] \n\t"
                       "vmls.f32 s0, s9, %[s] \n\t"
                       "vmul.f32 s1, s11, %[s] \n\t"
                       "vmla.f32 s1, s9, %[c] \n\t"
                       "vmov s11, s0 \n\t"
                       "vmov s9, s1 \n\t"
                       "vmul.f32 s0, s15, %[c] \n\t"
                       "vmls.f32 s0, s13, %[s] \n\t"
                       "vmul.f32 s1, s15, %[s] \n\t"
                       "vmla.f32 s1, s13, %[c] \n\t"
                       "vmov s15, s0 \n\t"
                       "vmov s13, s1 \n\t"
                       "vmul.f32 s0, s19, %[c] \n\t"
                       "vmls.f32 s0, s17, %[s] \n\t"
                       "vmul.f32 s1, s19, %[s] \n\t"
                       "vmla.f32 s1, s17, %[c] \n\t"
                       "vmov s19, s0 \n\t"
                       "vmov s17, s1 \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [c] "t"(c), [s] "t"(s)
                       : "memory", "s0", "s1", "s4", "s5", "s6", "s7", "s8",
                         "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16",
                         "s17", "s18", "s19");
}

static inline void vfp_rotate_z(C3D_Mtx *__restrict__ m, GLfloat c, GLfloat s) {
  __asm__ __volatile__("vldmia %[m], {s4-s19} \n\t"
                       "vmul.f32 s0, s7, %[c] \n\t"
                       "vmla.f32 s0, s6, %[s] \n\t"
                       "vmul.f32 s1, s6, %[c] \n\t"
                       "vmls.f32 s1, s7, %[s] \n\t"
                       "vmov s7, s0 \n\t"
                       "vmov s6, s1 \n\t"
                       "vmul.f32 s0, s11, %[c] \n\t"
                       "vmla.f32 s0, s10, %[s] \n\t"
                       "vmul.f32 s1, s10, %[c] \n\t"
                       "vmls.f32 s1, s11, %[s] \n\t"
                       "vmov s11, s0 \n\t"
                       "vmov s10, s1 \n\t"
                       "vmul.f32 s0, s15, %[c] \n\t"
                       "vmla.f32 s0, s14, %[s] \n\t"
                       "vmul.f32 s1, s14, %[c] \n\t"
                       "vmls.f32 s1, s15, %[s] \n\t"
                       "vmov s15, s0 \n\t"
                       "vmov s14, s1 \n\t"
                       "vmul.f32 s0, s19, %[c] \n\t"
                       "vmla.f32 s0, s18, %[s] \n\t"
                       "vmul.f32 s1, s18, %[c] \n\t"
                       "vmls.f32 s1, s19, %[s] \n\t"
                       "vmov s19, s0 \n\t"
                       "vmov s18, s1 \n\t"
                       "vstmia %[m], {s4-s19} \n\t"
                       : "+m"(*m)
                       : [m] "r"(m), [c] "t"(c), [s] "t"(s)
                       : "memory", "s0", "s1", "s4", "s5", "s6", "s7", "s8",
                         "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16",
                         "s17", "s18", "s19");
}

#endif // NOVAGL_VFP_MATH_H
