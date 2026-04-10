/*
 * NovaGL.h - OpenGL ES 1.1 -> Citro3D Translation Layer
 */

#ifndef NOVA_H
#define NOVA_H

#ifdef __cplusplus
extern "C" {
#endif
#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* GL Types */
typedef unsigned int    GLenum;
typedef unsigned char   GLboolean;
typedef unsigned int    GLbitfield;
typedef void            GLvoid;
typedef int8_t          GLbyte;
typedef uint8_t         GLubyte;
typedef int16_t         GLshort;
typedef uint16_t        GLushort;
typedef int             GLint;
typedef unsigned int    GLuint;
typedef int             GLsizei;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLclampd;
typedef int             GLfixed;
typedef ptrdiff_t       GLintptr;
typedef ptrdiff_t       GLsizeiptr;
typedef float           GLdouble;
//nova_constants
#define NOVA_MAX_TEXTURES     2048
#define NOVA_MAX_VBOS         32768
#define NOVA_MATRIX_STACK     32
#define NOVA_DISPLAY_LISTS    512
#define NOVA_DL_MAX_OPS       64
#define NOVA_CMD_BUF_SIZE     (512 * 1024)

#define NOVA_SCREEN_W         400
#define NOVA_SCREEN_H         240

#define NOVA_SCREEN_BOTTOM_W  240
#define NOVA_SCREEN_BOTTOM_H  320

#define DISPLAY_TRANSFER_FLAGS \
(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
GX_TRANSFER_RAW_COPY(0) | \
GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
//GL_constants
#define GL_FALSE                    0
#define GL_TRUE                     1
#define GL_NO_ERROR                 0
#define GL_INVALID_ENUM             0x0500
#define GL_INVALID_VALUE            0x0501
#define GL_INVALID_OPERATION        0x0502
#define GL_OUT_OF_MEMORY            0x0505

#define GL_BYTE                     0x1400
#define GL_UNSIGNED_BYTE            0x1401
#define GL_SHORT                    0x1402
#define GL_UNSIGNED_SHORT           0x1403
#define GL_INT                      0x1404
#define GL_UNSIGNED_INT             0x1405
#define GL_FLOAT                    0x1406
#define GL_FIXED                    0x140C

#define GL_POINTS                   0x0000
#define GL_LINES                    0x0001
#define GL_LINE_LOOP                0x0002
#define GL_LINE_STRIP               0x0003
#define GL_TRIANGLES                0x0004
#define GL_TRIANGLE_STRIP           0x0005
#define GL_TRIANGLE_FAN             0x0006
#define GL_QUADS                    0x0007

#define GL_MODELVIEW                0x1700
#define GL_PROJECTION               0x1701
#define GL_TEXTURE                  0x1702

#define GL_VERTEX_ARRAY             0x8074
#define GL_NORMAL_ARRAY             0x8075
#define GL_COLOR_ARRAY              0x8076
#define GL_TEXTURE_COORD_ARRAY      0x8078

#define GL_FOG                      0x0B60
#define GL_LIGHTING                 0x0B50
#define GL_TEXTURE_2D               0x0DE1
#define GL_CULL_FACE                0x0B44
#define GL_ALPHA_TEST               0x0BC0
#define GL_BLEND                    0x0BE2
#define GL_COLOR_LOGIC_OP           0x0BF2
#define GL_DITHER                   0x0BD0
#define GL_STENCIL_TEST             0x0B90
#define GL_DEPTH_TEST               0x0B71
#define GL_SCISSOR_TEST             0x0C11
#define GL_POLYGON_OFFSET_FILL      0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_COVERAGE          0x80A0
#define GL_MULTISAMPLE              0x809D
#define GL_RESCALE_NORMAL           0x803A
#define GL_NORMALIZE                0x0BA1
#define GL_COLOR_MATERIAL           0x0B57

#define GL_FOG_MODE                 0x0B65
#define GL_FOG_DENSITY              0x0B62
#define GL_FOG_START                0x0B63
#define GL_FOG_END                  0x0B64
#define GL_FOG_COLOR                0x0B66
#define GL_EXP                      0x0800
#define GL_EXP2                     0x0801
#define GL_LINEAR                   0x2601

#define GL_FLAT                     0x1D00
#define GL_SMOOTH                   0x1D01

#define GL_NEVER                    0x0200
#define GL_LESS                     0x0201
#define GL_EQUAL                    0x0202
#define GL_LEQUAL                   0x0203
#define GL_GREATER                  0x0204
#define GL_NOTEQUAL                 0x0205
#define GL_GEQUAL                   0x0206
#define GL_ALWAYS                   0x0207

#define GL_ZERO                     0
#define GL_ONE                      1
#define GL_SRC_COLOR                0x0300
#define GL_ONE_MINUS_SRC_COLOR      0x0301
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_DST_ALPHA                0x0304
#define GL_ONE_MINUS_DST_ALPHA      0x0305
#define GL_DST_COLOR                0x0306
#define GL_ONE_MINUS_DST_COLOR      0x0307
#define GL_SRC_ALPHA_SATURATE       0x0308

#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_NEAREST                  0x2600
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_LINEAR_MIPMAP_NEAREST    0x2701
#define GL_NEAREST_MIPMAP_LINEAR    0x2702
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
#define GL_REPEAT                   0x2901
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_MIRRORED_REPEAT          0x8370

#define GL_ALPHA                    0x1906
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_LUMINANCE                0x1909
#define GL_LUMINANCE_ALPHA          0x190A
#define GL_RGBA8_OES                0x8058
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG  0x8C02
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG   0x8C00

#define GL_UNSIGNED_SHORT_4_4_4_4   0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1   0x8034
#define GL_UNSIGNED_SHORT_5_6_5     0x8363

#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_STATIC_DRAW              0x88E4
#define GL_DYNAMIC_DRAW             0x88E8
#define GL_STREAM_DRAW              0x88E0

#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_STENCIL_BUFFER_BIT       0x00000400
#define GL_COLOR_BUFFER_BIT         0x00004000

#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_FRONT_AND_BACK           0x0408
#define GL_CW                       0x0900
#define GL_CCW                      0x0901

#define GL_MODELVIEW_MATRIX         0x0BA6
#define GL_PROJECTION_MATRIX        0x0BA7
#define GL_TEXTURE_MATRIX           0x0BA8
#define GL_VIEWPORT                 0x0BA2
#define GL_MAX_TEXTURE_SIZE         0x0D33
#define GL_PACK_ALIGNMENT           0x0D05
#define GL_UNPACK_ALIGNMENT         0x0CF5

#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_FASTEST                  0x1101
#define GL_NICEST                   0x1102
#define GL_DONT_CARE                0x1100

#define GL_FRAMEBUFFER              0x8D40
#define GL_RENDERBUFFER             0x8D41
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_ATTACHMENT         0x8D00
#define GL_DEPTH24_STENCIL8_OES     0x88F0

#define GL_FILL                     0x1B02
#define GL_LINE                     0x1B01
#define GL_POINT                    0x1B00

#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03

#define GL_COMPILE                  0x1300
#define GL_COMPILE_AND_EXECUTE      0x1301

#define GL_TEXTURE_ENV              0x2300
#define GL_TEXTURE_ENV_MODE         0x2200
#define GL_TEXTURE_ENV_COLOR        0x2201
#define GL_MODULATE                 0x2100
#define GL_DECAL                    0x2101
#define GL_ADD                      0x0104
#define GL_REPLACE                  0x1E01

#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE1                 0x84C1
#define GL_TEXTURE2                 0x84C2
#define GL_TEXTURE3                 0x84C3
#define GL_MAX_TEXTURE_UNITS        0x84E2

/* ARB_multitexture constants */
#define GL_TEXTURE0_ARB             0x84C0
#define GL_TEXTURE1_ARB             0x84C1
#define GL_TEXTURE2_ARB             0x84C2
#define GL_TEXTURE3_ARB             0x84C3
#define GL_TEXTURE4_ARB             0x84C4
#define GL_TEXTURE5_ARB             0x84C5
#define GL_TEXTURE6_ARB             0x84C6
#define GL_TEXTURE7_ARB             0x84C7
#define GL_TEXTURE8_ARB             0x84C8
#define GL_TEXTURE9_ARB             0x84C9
#define GL_TEXTURE10_ARB            0x84CA
#define GL_TEXTURE11_ARB            0x84CB
#define GL_TEXTURE12_ARB            0x84CC
#define GL_TEXTURE13_ARB            0x84CD
#define GL_TEXTURE14_ARB            0x84CE
#define GL_TEXTURE15_ARB            0x84CF
#define GL_TEXTURE16_ARB            0x84D0
#define GL_TEXTURE17_ARB            0x84D1
#define GL_TEXTURE18_ARB            0x84D2
#define GL_TEXTURE19_ARB            0x84D3
#define GL_TEXTURE20_ARB            0x84D4
#define GL_TEXTURE21_ARB            0x84D5
#define GL_TEXTURE22_ARB            0x84D6
#define GL_TEXTURE23_ARB            0x84D7
#define GL_TEXTURE24_ARB            0x84D8
#define GL_TEXTURE25_ARB            0x84D9
#define GL_TEXTURE26_ARB            0x84DA
#define GL_TEXTURE27_ARB            0x84DB
#define GL_TEXTURE28_ARB            0x84DC
#define GL_TEXTURE29_ARB            0x84DD
#define GL_TEXTURE30_ARB            0x84DE
#define GL_TEXTURE31_ARB            0x84DF

#define GL_COMBINE                        0x8570
#define GL_COMBINE_RGB                    0x8571
#define GL_COMBINE_ALPHA                  0x8572
#define GL_SRC0_RGB                       0x8580
#define GL_SRC1_RGB                       0x8581
#define GL_SRC2_RGB                       0x8582
#define GL_OPERAND0_RGB                   0x8590
#define GL_OPERAND1_RGB                   0x8591
#define GL_OPERAND2_RGB                   0x8592
#define GL_INTERPOLATE                    0x8575
#define GL_CONSTANT                       0x8576
#define GL_PRIMARY_COLOR                  0x8577
#define GL_PREVIOUS                       0x8578


#define GL_LINE_SMOOTH                    0x0B20
#define GL_LINE_WIDTH                     0x0B21
#define GL_SMOOTH_LINE_WIDTH_RANGE        0x0B22
#define GL_ALIASED_LINE_WIDTH_RANGE       0x846E
//API
void nova_init_ex(int cmd_buf_size, int client_array_buf_size, int index_buf_size, int tex_staging_size);
void nova_init(void);
void nova_fini(void);
void novaSwapBuffers(void);
void nova_set_render_target(int is_right_eye);
void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices);

GLenum glGetError(void);
void glClear(GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepthf(GLclampf depth);
void glClearDepth(GLclampd depth);
void glClearStencil(GLint s);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);

void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDepthRangef(GLclampf near_val, GLclampf far_val);
void glDepthRange(GLclampd near_val, GLclampd far_val);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glAlphaFunc(GLenum func, GLclampf ref);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);

/* Additional glColor variants */
void glColor3b(GLbyte r, GLbyte g, GLbyte b);
void glColor3bv(const GLbyte *v);
void glColor3d(GLdouble r, GLdouble g, GLdouble b);
void glColor3dv(const GLdouble *v);
void glColor3fv(const GLfloat *v);
void glColor3i(GLint r, GLint g, GLint b);
void glColor3iv(const GLint *v);
void glColor3s(GLshort r, GLshort g, GLshort b);
void glColor3sv(const GLshort *v);
void glColor3ubv(const GLubyte *v);
void glColor3ui(GLuint r, GLuint g, GLuint b);
void glColor3uiv(const GLuint *v);
void glColor3us(GLushort r, GLushort g, GLushort b);
void glColor3usv(const GLushort *v);
void glColor4b(GLbyte r, GLbyte g, GLbyte b, GLbyte a);
void glColor4bv(const GLbyte *v);
void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a);
void glColor4dv(const GLdouble *v);
void glColor4fv(const GLfloat *v);
void glColor4i(GLint r, GLint g, GLint b, GLint a);
void glColor4iv(const GLint *v);
void glColor4s(GLshort r, GLshort g, GLshort b, GLshort a);
void glColor4sv(const GLshort *v);
void glColor4ubv(const GLubyte *v);
void glColor4ui(GLuint r, GLuint g, GLuint b, GLuint a);
void glColor4uiv(const GLuint *v);
void glColor4us(GLushort r, GLushort g, GLushort b, GLushort a);
void glColor4usv(const GLushort *v);

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glColorMaterial(GLenum face, GLenum mode);
void glShadeModel(GLenum mode);

//matrix.c
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glMultMatrixf(const GLfloat *m);
void glLoadMatrixf(const GLfloat *m);
void glLoadMatrixd(const GLdouble *m);
void glMultMatrixd(const GLdouble *m);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glScaled(GLdouble x, GLdouble y, GLdouble z);
void glTranslated(GLdouble x, GLdouble y, GLdouble z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val);
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val);
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat near_val, GLfloat far_val);
void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val);
void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed near_val, GLfixed far_val);

void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void glTexGend(GLenum coord, GLenum pname, GLdouble param);
void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params);
void glTexGenf(GLenum coord, GLenum pname, GLfloat param);
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params);
void glTexGeni(GLenum coord, GLenum pname, GLint param);
void glTexGeniv(GLenum coord, GLenum pname, const GLint *params);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void glEnableClientState(GLenum cap);
void glDisableClientState(GLenum cap);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void glFogf(GLenum pname, GLfloat param);
void glFogfv(GLenum pname, const GLfloat *params);
void glFogi(GLenum pname, GLint param);
void glFogiv(GLenum pname, const GLint *params);
void glFogx(GLenum pname, GLfixed param);
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz);
void glNormal3bv(const GLbyte *v);
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz);
void glNormal3dv(const GLdouble *v);
void glNormal3fv(const GLfloat *v);
void glNormal3i(GLint nx, GLint ny, GLint nz);
void glNormal3iv(const GLint *v);
void glNormal3s(GLshort nx, GLshort ny, GLshort nz);
void glNormal3sv(const GLshort *v);
void glGetFloatv(GLenum pname, GLfloat *params);
void glGetIntegerv(GLenum pname, GLint *params);
const GLubyte* glGetString(GLenum name);
void glHint(GLenum target, GLenum mode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glLineWidth(GLfloat width);
void glPolygonMode(GLenum face, GLenum mode);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void glFlush(void);
void glFinish(void);
void glPixelStorei(GLenum pname, GLint param);
void glPixelStoref(GLenum pname, GLfloat param);
void glDrawBuffer(GLenum mode);
void glClipPlane(GLenum plane, const GLdouble *equation);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilMask(GLuint mask);
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void glGenFramebuffers(GLsizei n, GLuint *ids);
void glDeleteFramebuffers(GLsizei n, const GLuint *ids);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glGenRenderbuffers(GLsizei n, GLuint *ids);
void glDeleteRenderbuffers(GLsizei n, const GLuint *ids);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
GLuint glGenLists(GLsizei range);
void glNewList(GLuint list, GLenum mode);
void glEndList(void);
void glCallList(GLuint list);
void glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
void glDeleteLists(GLuint list, GLsizei range);

void glActiveTexture(GLenum texture);
void glClientActiveTexture(GLenum texture);
void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
void glActiveTextureARB(GLenum texture);
void glClientActiveTextureARB(GLenum texture);

/* glTexCoord variants */
void glTexCoord1d(GLdouble s);
void glTexCoord1dv(const GLdouble *v);
void glTexCoord1f(GLfloat s);
void glTexCoord1fv(const GLfloat *v);
void glTexCoord1i(GLint s);
void glTexCoord1iv(const GLint *v);
void glTexCoord1s(GLshort s);
void glTexCoord1sv(const GLshort *v);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord2dv(const GLdouble *v);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2fv(const GLfloat *v);
void glTexCoord2i(GLint s, GLint t);
void glTexCoord2iv(const GLint *v);
void glTexCoord2s(GLshort s, GLshort t);
void glTexCoord2sv(const GLshort *v);
void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r);
void glTexCoord3dv(const GLdouble *v);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord3fv(const GLfloat *v);
void glTexCoord3i(GLint s, GLint t, GLint r);
void glTexCoord3iv(const GLint *v);
void glTexCoord3s(GLshort s, GLshort t, GLshort r);
void glTexCoord3sv(const GLshort *v);
void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void glTexCoord4dv(const GLdouble *v);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glTexCoord4fv(const GLfloat *v);
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q);
void glTexCoord4iv(const GLint *v);
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q);
void glTexCoord4sv(const GLshort *v);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);

void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
GLboolean glIsTexture(GLuint texture);

/* Attribute stack */
void glPushAttrib(GLbitfield mask);
void glPopAttrib(void);
void glPushClientAttrib(GLbitfield mask);
void glPopClientAttrib(void);

/* Immediate mode (emulated via vertex arrays) */
void glBegin(GLenum mode);
void glEnd(void);
void glArrayElement(GLint i);

/* Vertex functions */
void glVertex2d(GLdouble x, GLdouble y);
void glVertex2dv(const GLdouble *v);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2fv(const GLfloat *v);
void glVertex2i(GLint x, GLint y);
void glVertex2iv(const GLint *v);
void glVertex2s(GLshort x, GLshort y);
void glVertex2sv(const GLshort *v);
void glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void glVertex3dv(const GLdouble *v);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3fv(const GLfloat *v);
void glVertex3i(GLint x, GLint y, GLint z);
void glVertex3iv(const GLint *v);
void glVertex3s(GLshort x, GLshort y, GLshort z);
void glVertex3sv(const GLshort *v);
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void glVertex4dv(const GLdouble *v);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glVertex4fv(const GLfloat *v);
void glVertex4i(GLint x, GLint y, GLint z, GLint w);
void glVertex4iv(const GLint *v);
void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w);
void glVertex4sv(const GLshort *v);

#ifdef __cplusplus
}
#endif

#endif /* NOVA_H */
