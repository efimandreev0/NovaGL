/*
 * NovaGL.h - OpenGL ES 1.1 -> Citro3D Translation Layer
 */

#ifndef NOVA_H
#define NOVA_H

#ifdef __cplusplus
extern "C" {

#endif

// Intentionally NOT including <3ds.h> / <citro3d.h> from this public header:
// libctru's <3ds.h> defines a global `Thread` typedef which would collide with
// any C++ caller that has its own `class Thread` (e.g. the engine porting on top
// of NovaGL). The 3DS-specific includes are confined to NovaGL's .c files.
#include <stdint.h>
#include <stddef.h>

/* Calling convention macro from desktop GL headers. Real Windows GL needs
 * __stdcall, but ARM EABI on 3DS has only one calling convention, so this is
 * defined to nothing. Some engines (e.g. Arx Libertatis) declare GL function-
 * pointer typedefs like `GLenum (GLAPIENTRY *fn)()` and expect this macro to
 * exist regardless of platform. */
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

/* GL Types */
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int8_t GLbyte;
typedef uint8_t GLubyte;
typedef int16_t GLshort;
typedef uint16_t GLushort;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLclampd;
typedef int GLfixed;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
/* Real double, matching desktop GL. Implementations convert to float
 * element-wise internally (PICA is float-only), so the ABI is consistent
 * as long as NovaGL and the app are built against the same header.
 * (Was float historically; changed for OSG, which requires GLdouble==double.) */
typedef double GLdouble;
typedef char GLchar;

/* GL_KHR_debug callback signature + the glad loader proc type. NovaGL is a
 * static GL implementation (functions are linked directly, not fetched through
 * a loader), so these exist mainly so the GL-display/loader code that includes
 * NovaGL.h on 3DS compiles unchanged. */
typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                            GLsizei length, const GLchar *message, const void *userParam);
typedef GLDEBUGPROC GLDEBUGPROCKHR;
typedef void *(*GLADloadproc)(const char *name);
//nova_constants
/* These fixed-size tables live in .bss (real RAM on the 3DS target). The old
 * 2048/32768 caps reserved far more than any observed workload uses — the
 * prison-ship interior high-water marks sit well under a quarter of these — so
 * they were shrunk to reclaim ~1MB of zero-init .bss. The glGenBuffers/
 * glGenTextures overflow branches degrade gracefully (GL_OUT_OF_MEMORY, id 0),
 * and both emit a one-time breadcrumb when the live-id high-water mark passes
 * 75% of the cap, so if a heavier scene ever approaches the limit we notice
 * before it starts failing allocations. */
#define NOVA_MAX_TEXTURES     1024
#define NOVA_MAX_VBOS         8192
#define NOVA_MATRIX_STACK     32
#define NOVA_DISPLAY_LISTS    512
#define NOVA_DL_MAX_OPS       64
/* 3MB: a full FFP world frame (hundreds of draws, each with TEV/matrix
 * state) accumulates well over 512KB of GPU commands once the vertex rings
 * stopped force-splitting mid-frame (adaptive growth). Overflowing the
 * citro3d command buffer wedges the GPU silently. */
#define NOVA_CMD_BUF_SIZE     (3 * 1024 * 1024)

#define NOVA_SCREEN_W         400
#define NOVA_SCREEN_H         240

#define NOVA_SCREEN_BOTTOM_W  240
#define NOVA_SCREEN_BOTTOM_H  320

/* DISPLAY_TRANSFER_FLAGS lives in src/NovaGL.c — it uses GX_TRANSFER_* macros
 * from <3ds.h> which we no longer pull into the public header. */
//GL_constants
#define GL_FALSE                    0
#define GL_TRUE                     1
#define GL_NONE                     0
#define GL_NO_ERROR                 0
#define GL_INVALID_ENUM             0x0500
#define GL_INVALID_VALUE            0x0501
#define GL_INVALID_OPERATION        0x0502
#define GL_STACK_OVERFLOW           0x0503
#define GL_STACK_UNDERFLOW          0x0504
#define GL_OUT_OF_MEMORY            0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

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
/* GL_EXT_fog_coord: per-vertex fog coordinate array. PICA200 computes fog from
 * depth in our shader, so the array client-state is accepted but ignored. */
#define GL_FOG_COORDINATE_ARRAY     0x8457

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
#define GL_KEEP                     0x1E00
/* Stencil-op enums (real path now implemented on PICA200; see raster.c). */
#define GL_INCR                     0x1E02
#define GL_DECR                     0x1E03
#define GL_INVERT                   0x150A
#define GL_INCR_WRAP                0x8507
#define GL_DECR_WRAP                0x8508

/* Colour logic-op opcodes (glLogicOp + glEnable(GL_COLOR_LOGIC_OP)). All 16 map
 * 1:1 onto PICA's GPU_LOGICOP. GL_INVERT (0x150A) is already defined above for
 * the stencil-op path; it doubles as a logic op. */
#define GL_CLEAR                    0x1500
#define GL_AND                      0x1501
#define GL_AND_REVERSE              0x1502
#define GL_COPY                     0x1503
#define GL_AND_INVERTED             0x1504
#define GL_NOOP                     0x1505
#define GL_XOR                      0x1506
#define GL_OR                       0x1507
#define GL_NOR                      0x1508
#define GL_EQUIV                    0x1509
#define GL_OR_REVERSE               0x150B
#define GL_COPY_INVERTED            0x150C
#define GL_OR_INVERTED              0x150D
#define GL_NAND                     0x150E
#define GL_SET                      0x150F

/* Cube-map texture target + faces (glBindTexture / glTexImage2D). Storage is
 * backed by C3D_TexInitCube; see glTexImage2D notes on the texcoord caveat. */
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP     0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X  0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y  0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y  0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z  0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z  0x851A

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
/* Constant-colour blend factors (glBlendColor). Map to PICA's
 * GPU_CONSTANT_COLOR / GPU_CONSTANT_ALPHA and their inverses. */
#define GL_CONSTANT_COLOR           0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA           0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_BLEND_COLOR              0x8005

/* Blend equation (glBlendEquation / glBlendEquationSeparate[OES]). PICA200 has
 * a real per-channel blend op (GPU_BLENDEQUATION), so these map straight to HW
 * — not a stub. */
#define GL_FUNC_ADD                 0x8006
#define GL_MIN                      0x8007
#define GL_MAX                      0x8008
#define GL_BLEND_EQUATION           0x8009
#define GL_BLEND_EQUATION_RGB       0x8009
#define GL_FUNC_SUBTRACT            0x800A
#define GL_FUNC_REVERSE_SUBTRACT    0x800B
#define GL_BLEND_EQUATION_ALPHA     0x883D
/* OES aliases used by the GLES blend path. */
#define GL_FUNC_ADD_OES             0x8006
#define GL_BLEND_EQUATION_OES       0x8009
#define GL_FUNC_SUBTRACT_OES        0x800A
#define GL_FUNC_REVERSE_SUBTRACT_OES 0x800B

/* Material parameter names (glMaterial*). Lighting maths still lives in the
 * future GPU light path, but these record real, queryable material state. */
#define GL_AMBIENT                  0x1200
#define GL_DIFFUSE                  0x1201
#define GL_SPECULAR                 0x1202
#define GL_EMISSION                 0x1600
#define GL_SHININESS                0x1601
#define GL_AMBIENT_AND_DIFFUSE      0x1602
#define GL_COLOR_INDEXES            0x1603

/* Fixed-function lighting (glLight* / glLightModel*). Like the material state,
 * these record real, queryable lighting state. The per-vertex lighting maths is
 * the future GPU light path (needs a normal-aware shader + C3D_LightEnv). */
#define GL_MAX_LIGHTS               0x0D31
#define GL_LIGHT0                   0x4000
#define GL_LIGHT1                   0x4001
#define GL_LIGHT2                   0x4002
#define GL_LIGHT3                   0x4003
#define GL_LIGHT4                   0x4004
#define GL_LIGHT5                   0x4005
#define GL_LIGHT6                   0x4006
#define GL_LIGHT7                   0x4007
/* light parameter names (GL_AMBIENT/GL_DIFFUSE/GL_SPECULAR shared w/ material) */
#define GL_POSITION                 0x1203
#define GL_SPOT_DIRECTION           0x1204
#define GL_SPOT_EXPONENT            0x1205
#define GL_SPOT_CUTOFF              0x1206
#define GL_CONSTANT_ATTENUATION     0x1207
#define GL_LINEAR_ATTENUATION       0x1208
#define GL_QUADRATIC_ATTENUATION    0x1209
/* light model */
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE     0x0B52
#define GL_LIGHT_MODEL_AMBIENT      0x0B53

/* GL_KHR_debug — accepted so debug-build display code compiles. NovaGL has no
 * async debug message stream (PICA200), so the callback is stored but never
 * fired and the enables are no-ops. */
#define GL_DEBUG_OUTPUT_KHR              0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR  0x8242
#define GL_DEBUG_SEVERITY_HIGH_KHR       0x9146
#define GL_DEBUG_SEVERITY_MEDIUM_KHR     0x9147
#define GL_DEBUG_SEVERITY_LOW_KHR        0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION_KHR 0x826B
/* non-KHR aliases (same values) for code that uses the core spellings */
#define GL_DEBUG_OUTPUT                  0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS      0x8242
#define GL_DEBUG_SEVERITY_HIGH           0x9146
#define GL_DEBUG_SEVERITY_MEDIUM         0x9147
#define GL_DEBUG_SEVERITY_LOW            0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION   0x826B
#define GL_DEBUG_SOURCE_APPLICATION      0x824A
#define GL_DEBUG_SOURCE_THIRD_PARTY      0x8249

/* KHR_debug is not available on PICA200. Report it absent so callers that gate
 * on `if (GLAD_GL_KHR_debug)` skip the debug-group annotations; the push/pop
 * entry points below are inert stubs for the dead branch. */
#define GLAD_GL_KHR_debug 0

#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_NEAREST                  0x2600
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_LINEAR_MIPMAP_NEAREST    0x2701
#define GL_NEAREST_MIPMAP_LINEAR    0x2702
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
#define GL_CLAMP                    0x2900
#define GL_REPEAT                   0x2901
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_CLAMP_TO_BORDER          0x812D
#define GL_MIRRORED_REPEAT          0x8370

#define GL_RED                      0x1903
#define GL_GREEN                    0x1904
#define GL_BLUE                     0x1905
#define GL_ALPHA                    0x1906
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_LUMINANCE                0x1909
#define GL_LUMINANCE_ALPHA          0x190A
#define GL_BGR                      0x80E0
#define GL_BGRA                     0x80E1

/* Sized RGBA + packed depth-stencil internal formats used by librw's gl3
 * raster code. PICA200's on-screen depth buffer is D24S8; these tokens mostly
 * exist so the gl3 raster/FBO switch statements compile. */
#define GL_RGB5_A1                  0x8057
#define GL_DEPTH_STENCIL            0x84F9
#define GL_UNSIGNED_INT_24_8        0x84FA
#define GL_DEPTH24_STENCIL8         0x88F0
/* EXT spelling (GL_EXT_bgra / GL_EXT_texture_format_BGRA8888). Same value —
 * desktop & GameCube ports upload their "native" surfaces as BGRA. NovaGL
 * honours this by swapping R/B at upload time (see texture.c). */
#define GL_BGRA_EXT                 0x80E1
#define GL_BGR_EXT                  0x80E0

/* Sized internal formats — NovaGL stores everything as 8 bits/channel internally,
 * so these are accepted by glTexImage2D but mapped onto the equivalent unsized form. */
#define GL_ALPHA8                   0x803C
#define GL_LUMINANCE8               0x8040
#define GL_LUMINANCE8_ALPHA8        0x8045
#define GL_INTENSITY                0x8049
#define GL_INTENSITY8               0x804B
#define GL_RGB8                     0x8051
#define GL_RGBA8                    0x8058

/* Texture parameters / state queries not in OpenGL ES 1.1 */
#define GL_GENERATE_MIPMAP                 0x8191
#define GL_TEXTURE_MAX_LEVEL               0x813D
#define GL_TEXTURE_MAX_ANISOTROPY_EXT      0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT  0x84FF

/* Multisample query tokens — NovaGL has no MSAA, so glGetIntegerv reports 0 for both. */
#define GL_SAMPLE_BUFFERS                  0x80A8
#define GL_SAMPLES                         0x80A9
#define GL_SAMPLE_SHADING_ARB              0x8C36

/* NV_fog_distance — desktop GL extension; treated as a no-op hint by NovaGL. */
#define GL_FOG_DISTANCE_MODE_NV            0x855A

/* GL_EXT_fog_coord / fixed-function fog coordinate source — also a no-op. */
#define GL_FOG_COORDINATE_SOURCE           0x8450
#define GL_FOG_COORDINATE                  0x8451
#define GL_FRAGMENT_DEPTH                  0x8452
#define GL_FOG_COORD_SRC                   GL_FOG_COORDINATE_SOURCE
#define GL_FOG_COORD                       GL_FOG_COORDINATE

/* TexGen plane enums used by callers that compile against desktop GL but never get
 * called at runtime on NovaGL — provide the constant so the call site compiles. */
#define GL_OBJECT_PLANE                    0x2501
#define GL_EYE_PLANE                       0x2502
/* NovaGL extension: 4-bit luminance + 4-bit alpha packed in a single byte
 * (high nibble = alpha, low nibble = luminance) — maps directly to GPU_LA4. */
#define GL_LUMINANCE_ALPHA4_NOVA    0x6B34
#define GL_RGBA8_OES                0x8058
/* ETC1 (RGB, 4bpp) and ETC1A4 (RGB + 4-bit alpha, 8bpp) — both native PICA200
 * sampleable formats. ETC1A4 is a NovaGL extension token (no standard GL enum);
 * pass it to glCompressedTexImage2D with PICA-tiled data (alpha block then
 * colour block per 4x4). */
#define GL_ETC1_RGB8_OES            0x8D64
#define GL_ETC1_RGB8A4_NOVA         0x6E01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG  0x8C02
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG   0x8C00
/* S3TC / DXT format tokens. PICA200 cannot sample S3TC (only ETC1), so these
 * exist only so callers that switch on the format compile. The actual DXT data
 * is decompressed to RGBA on the CPU before upload (librw image.cpp does this
 * when gl3Caps.dxtSupported == false). */
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT      0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT     0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT     0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT     0x83F3

#define GL_UNSIGNED_SHORT_4_4_4_4   0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1   0x8034
#define GL_UNSIGNED_SHORT_5_6_5     0x8363

#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_STATIC_DRAW              0x88E4
#define GL_DYNAMIC_DRAW             0x88E8
#define GL_STREAM_DRAW              0x88E0
/* Buffer object query params (glGetBufferParameteriv). */
#define GL_BUFFER_SIZE              0x8764
#define GL_BUFFER_USAGE             0x8765
#define GL_BUFFER_ACCESS            0x88BB
#define GL_BUFFER_MAPPED            0x88BC

/* Buffer mapping access modes (glMapBuffer) */
#define GL_READ_ONLY                0x88B8
#define GL_WRITE_ONLY               0x88B9
#define GL_READ_WRITE               0x88BA

/* Buffer mapping access bits (glMapBufferRange, GL 3.0 / ARB_map_buffer_range) */
#define GL_MAP_READ_BIT                0x0001
#define GL_MAP_WRITE_BIT               0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT    0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT   0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT      0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT      0x0020

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
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
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
/* RGB / alpha post-combine scale factors — texture environment scale. NovaGL
 * doesn't currently apply these (PICA200 TEV has its own scale field), but the
 * tokens are needed so glTexEnv{i,f} calls compile. */
#define GL_RGB_SCALE                      0x8573
#define GL_ALPHA_SCALE                    0x0D1C
/* Texture LOD bias — desktop GL 1.4 feature; PICA200 has per-texture LOD bias
 * but the engine usually leaves this at 0, so these tokens are accepted as a
 * no-op in glTexParameter / glTexEnv calls. */
#define GL_TEXTURE_FILTER_CONTROL         0x8500
#define GL_TEXTURE_LOD_BIAS               0x8501
#define GL_SRC0_RGB                       0x8580
#define GL_SRC1_RGB                       0x8581
#define GL_SRC2_RGB                       0x8582
/* Aliases required by GL_ARB_texture_env_combine / OpenGL 1.3+ */
#define GL_SOURCE0_RGB                    GL_SRC0_RGB
#define GL_SOURCE1_RGB                    GL_SRC1_RGB
#define GL_SOURCE2_RGB                    GL_SRC2_RGB
#define GL_OPERAND0_RGB                   0x8590
#define GL_OPERAND1_RGB                   0x8591
#define GL_OPERAND2_RGB                   0x8592
/* Alpha combine sources / operands */
#define GL_SRC0_ALPHA                     0x8588
#define GL_SRC1_ALPHA                     0x8589
#define GL_SRC2_ALPHA                     0x858A
#define GL_SOURCE0_ALPHA                  GL_SRC0_ALPHA
#define GL_SOURCE1_ALPHA                  GL_SRC1_ALPHA
#define GL_SOURCE2_ALPHA                  GL_SRC2_ALPHA
#define GL_OPERAND0_ALPHA                 0x8598
#define GL_OPERAND1_ALPHA                 0x8599
#define GL_OPERAND2_ALPHA                 0x859A
/* Alpha combine function */
#define GL_COMBINE_ALPHA_ARB              GL_COMBINE_ALPHA
#define GL_INTERPOLATE                    0x8575
#define GL_SUBTRACT                       0x84E7
/* NovaGL extension: PICA's GPU_MULTIPLY_ADD combine func — out = (s0*s1)+s2.
 * Standard GL_COMBINE has no equivalent; valid in NovaTevStageGL.combine_*
 * (maps through gl_to_gpu_combinefunc). Value outside standard GL ranges. */
#define GL_MULT_ADD_NOVA                  0xA0F0
#define GL_CONSTANT                       0x8576
#define GL_PRIMARY_COLOR                  0x8577
#define GL_PREVIOUS                       0x8578

#define GL_LINE_SMOOTH                    0x0B20
#define GL_LINE_WIDTH                     0x0B21
#define GL_SMOOTH_LINE_WIDTH_RANGE        0x0B22
#define GL_ALIASED_LINE_WIDTH_RANGE       0x846E

/* GL 2.0+ shader pipeline constants (stubs) */
#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_ACTIVE_UNIFORMS                0x8B86
#define GL_ACTIVE_ATTRIBUTES              0x8B89

/* Framebuffer extension constants */
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
/* Currently-bound framebuffer queries (glGetIntegerv). Per the GL spec
 * GL_FRAMEBUFFER_BINDING and GL_DRAW_FRAMEBUFFER_BINDING are the same enum. */
#define GL_FRAMEBUFFER_BINDING            0x8CA6
#define GL_DRAW_FRAMEBUFFER_BINDING       0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING       0x8CAA
#define GL_TEXTURE_2D_TARGET              0x0DE1

#define GL_COMBINE_ARB                    GL_COMBINE_RGB
#define GL_COMBINE_RGB_ARB                GL_COMBINE_RGB
#define GL_SOURCE0_RGB_ARB                GL_SOURCE0_RGB
#define GL_SOURCE1_RGB_ARB                GL_SOURCE1_RGB
#define GL_PRIMARY_COLOR_ARB              GL_PRIMARY_COLOR
#define GL_DOT3_RGBA_ARB                  0x86AF

#define GL_S                    0x2000
#define GL_T                    0x2001
#define GL_R                    0x2002
#define GL_Q                    0x2003
#define GL_TEXTURE_GEN_MODE     0x2500
#define GL_EYE_LINEAR           0x2400
#define GL_OBJECT_LINEAR        0x2401
#define GL_SPHERE_MAP           0x2402

#define NOVA_MAX_TEXTURE_UNITS  3   /* matches g.bound_texture[3] */

#define EGLBoolean    int32_t
#define EGLDisplay    void*
#define EGLenum       uint32_t
#define EGLSurface    void*
#define EGLContext    void*
#define EGLConfig     void*
#define EGLint        int32_t

#define EGLint64      int64_t
#define EGLuint64     uint64_t

#define NativeDisplayType void*

#define EGL_FALSE                             0
#define EGL_TRUE                              1

#define EGL_SUCCESS                                  0x3000
#define EGL_BAD_PARAMETER                            0x300C
#define EGL_OPENGL_ES_API                            0x30A0
#define EGL_OPENGL_API                               0x30A2

#define EGL_DEFAULT_DISPLAY ((NativeDisplayType)0)
#define EGL_NO_CONTEXT      ((EGLContext)0)
#define EGL_NO_DISPLAY      ((EGLDisplay)0)
#define EGL_NO_SURFACE      ((EGLSurface)0)

//API
void nova_init_ex(int cmd_buf_size, int client_array_buf_size, int index_buf_size, int tex_staging_size);

void nova_init(void);
/* Diagnostics: arm the GPU-wait-site breadcrumb tagger for the next 
 * tag prints (breadcrumbs go to the host app's log via weak vitaBreadcrumb). */
void nova_wait_tag_arm(int budget);
/* 0 = free-run (no VBlank wait at swap), N>0 = wait N VBlanks per swap. */
void novaSetSwapInterval(int interval);

void nova_fini(void);

void nova_set_render_target(int target_mode); // 0 = Top Left, 1 = Top Right, 2 = Bottom
void nova_draw_internal(GLenum mode, GLint first, GLsizei count, int is_elements, GLenum type, const GLvoid *indices);

// Сбрасывает кэш «последнего применённого» GPU-стейта внутри apply_gpu_state.
// Вызывать когда контекст GL логически меняется (смена уровня, ресет графики)
// — иначе кэш может считать «привязано» то, что уже не привязано (например,
// текстуры старого мира удалены, ID переиспользован), и пропустить нужный
// C3D_TexBind / C3D_DepthTest. Безопасно вызывать в любой момент.
void nova_invalidate_state_cache(void);

void novaSwapBuffers(void);

/* ------------------------------------------------------------------------
 * Frame buffering depth (single / double / triple)
 * ------------------------------------------------------------------------
 * Selects how many frames the CPU may run ahead of the GPU:
 *   1 = single  — SYNCDRAW: the CPU waits for the GPU each frame. Lowest
 *                 latency + lowest memory; no CPU/GPU overlap. (Default.)
 *   2 = double  — the CPU builds frame N+1 while the GPU renders frame N
 *                 (~20–40% more FPS on GPU-bound scenes).
 *   3 = triple  — the CPU may run up to two frames ahead (smoother under
 *                 uneven frame times, highest memory + latency).
 *
 * With 2/3, NovaGL keeps that many copies of the vertex/index ring buffers
 * and defers texture/render-target deletion by the same number of frames, so
 * the GPU never reads a buffer the CPU has overwritten or freed — async is
 * SAFE (no black/swapping textures). The cost is N× the ring memory
 * (per-slot = the sizes passed to nova_init_ex; default ~2.5MB each).
 *
 * Call this BEFORE nova_init() / nova_init_ex(); the buffers are allocated at
 * init. After init it has no effect. Out-of-range values are clamped to 1..3.
 * If a slot allocation runs out of linear RAM, NovaGL falls back to fewer
 * buffers automatically. The compile-time default is NOVAGL_FRAME_BUFFERS
 * (1, or 2 if the legacy -DNOVAGL_ASYNC_FRAME is set). */
void novaSetFrameBuffers(int count);

/* Returns the active frame-buffer count (1/2/3) after init, or 0 if not yet
 * initialized (or if a fallback reduced it). */
int novaGetFrameBuffers(void);

int novaGetEyeCount(void);

void novaBeginEye(int eye);

void novaSet3DDepth(float depth);

// ===[ Persistent texture cache ]===
// When enabled, NovaGL can persist the final swizzled + downscaled texture payload
// (i.e. the bytes it just wrote into C3D_Tex->data after morton tiling) to a caller-
// provided directory on disk (typically an SD card). Subsequent boots can then read
// those bytes straight back into linear heap memory, skipping BOTH the caller's PNG
// decode AND NovaGL's CPU downscale + morton-interleave — a ~300ms → ~30ms saving
// on a 268MHz ARM11.
//
// The hash key is caller-chosen; the caller is expected to hash whatever source bytes
// uniquely identify this texture (e.g. the original PNG blob). NovaGL never decodes
// the PNG itself, so it cannot compute that hash.
//
// Typical lifecycle in a homebrew app:
//     nova_texture_cache_set_directory("sdmc:/Nova/cache/MyGame");
//     ...
//     uint32_t hash = my_hash_of_png_blob(blob, blobSize);
//     glBindTexture(GL_TEXTURE_2D, texId);
//     int origW, origH;
//     if (!nova_texture_cache_load(hash, &origW, &origH)) {
//         uint8_t* pixels = stbi_load_from_memory(blob, blobSize, ...);
//         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
//         stbi_image_free(pixels);
//         nova_texture_cache_save(hash);
//     }

// Configure the directory where cached texture blobs live. Pass NULL or "" to disable.
// The directory (and its parents up to and including the last "/"-terminated segment)
// will be created on the first save.
void nova_texture_cache_set_directory(const char *dir);

// Check whether a cache entry exists for `hash` without loading it.
// Returns 1 if present, 0 otherwise.
int nova_texture_cache_has(uint32_t hash);

// Try to populate the currently-bound texture from the cache. On success returns 1
// and writes the texture's *original* (pre-downscale) width/height to the out-params
// so the caller can compute UVs correctly. Returns 0 on miss or malformed entry.
int nova_texture_cache_load(uint32_t hash, int *out_orig_w, int *out_orig_h);

// Save the currently-bound texture (its swizzled C3D_Tex->data plus the metadata
// needed to restore it) under the given hash. No-op if caching isn't enabled.
void nova_texture_cache_save(uint32_t hash);

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

void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);

void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);

void glLogicOp(GLenum opcode);

void glBlendEquation(GLenum mode);

void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);

/* OES aliases (GLES1 extension names). Same implementation. */
void glBlendEquationOES(GLenum mode);

void glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha);

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

void glMaterialf(GLenum face, GLenum pname, GLfloat param);

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);

void glMateriali(GLenum face, GLenum pname, GLint param);

void glMaterialiv(GLenum face, GLenum pname, const GLint *params);

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params);

void glGetMaterialiv(GLenum face, GLenum pname, GLint *params);

void glLightf(GLenum light, GLenum pname, GLfloat param);

void glLightfv(GLenum light, GLenum pname, const GLfloat *params);

void glLighti(GLenum light, GLenum pname, GLint param);

void glLightiv(GLenum light, GLenum pname, const GLint *params);

void glGetLightfv(GLenum light, GLenum pname, GLfloat *params);

void glLightModelf(GLenum pname, GLfloat param);

void glLightModelfv(GLenum pname, const GLfloat *params);

void glLightModeli(GLenum pname, GLint param);

void glLightModeliv(GLenum pname, const GLint *params);

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

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels);

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels);

/* Mipmap generation. NovaGL builds mips when GL_GENERATE_MIPMAP is set on a
 * texture; this entry point exists for GL3-style callers. */
void glGenerateMipmap(GLenum target);

/* Desktop-GL texture readback. PICA200/citro3d can't read back tiled texture
 * memory cheaply; these are no-op stubs so gl3 readback code links. */
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void glGetCompressedTexImage(GLenum target, GLint level, GLvoid *pixels);

void glTexParameteri(GLenum target, GLenum pname, GLint param);

void glTexParameterf(GLenum target, GLenum pname, GLfloat param);

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const GLvoid *data);

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                               const GLvoid *data);

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format,
                  GLenum type, const GLvoid *pixels);

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type,
                     const GLvoid *pixels);

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

/* Buffer mapping: returns a CPU-side pointer to the bound VBO's storage.
 * On 3DS the linear-heap allocation backing each VBO is directly addressable,
 * so map/unmap are essentially "give me the pointer" / "flush the cache".
 *
 * - access is one of GL_READ_ONLY / GL_WRITE_ONLY / GL_READ_WRITE (ignored,
 *   we hand out a writable pointer either way — the engine already respects
 *   the bit it asked for).
 * - Returns NULL on invalid binding / packed-storage VBOs that can't be
 *   exposed without an internal conversion. */
void *glMapBuffer(GLenum target, GLenum access);

/* glMapBufferRange: GL 3.0 variant. `length` may extend past the current
 * buffer size only if GL_MAP_INVALIDATE_BUFFER_BIT is set; the access bits
 * other than INVALIDATE_* are accepted for compatibility but have no effect. */
void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);

/* Flushes the data cache for the buffer and returns GL_TRUE on success. */
GLboolean glUnmapBuffer(GLenum target);

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);

/* glFogCoordPointer: per-vertex fog coordinate array. NovaGL's fog is computed
 * in the vertex shader from depth, so this pointer is recorded but never read
 * — calls compile and don't crash, but they don't influence rendering. */
void glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid *pointer);

void glEnableClientState(GLenum cap);

void glDisableClientState(GLenum cap);

void glDrawArrays(GLenum mode, GLint first, GLsizei count);

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

/* glDrawRangeElements: same as glDrawElements but with an additional hint about the
 * range of indices the call will reference. NovaGL ignores the range — the underlying
 * draw path already walks the entire index buffer — so this just forwards to
 * glDrawElements. The signature matches GL_EXT_draw_range_elements / GL 1.2. */
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                         const GLvoid *indices);

/* glDrawElementsBaseVertex / glDrawRangeElementsBaseVertex: add `basevertex` to
 * every fetched index. NovaGL has no GPU-side base-vertex offset, so we have
 * to walk the index buffer and rebuild it with the offset applied. This
 * allocates a small scratch buffer when basevertex != 0 — keep the index
 * count modest to avoid stalls. With basevertex == 0 it forwards verbatim. */
void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const GLvoid *indices, GLint basevertex);

void glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count,
                                   GLenum type, const GLvoid *indices, GLint basevertex);

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

const GLubyte *glGetString(GLenum name);

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

void glTexEnviv(GLenum target, GLenum pname, const GLint *params);

/* GLES 1.1 state queries (texture sampler params + tex-env). */
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void glGetTexEnviv(GLenum target, GLenum pname, GLint *params);
void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params);

void glEnable(GLenum cap);

void glDisable(GLenum cap);

GLboolean glIsEnabled(GLenum cap);

GLboolean glIsTexture(GLuint texture);

GLboolean glIsBuffer(GLuint buffer);

GLboolean glIsFramebuffer(GLuint framebuffer);

GLboolean glIsList(GLuint list);

void glClearColorx(GLfixed r, GLfixed g, GLfixed b, GLfixed a);
void glClearDepthx(GLfixed depth);
void glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a);
void glDepthRangex(GLfixed n, GLfixed f);
void glAlphaFuncx(GLenum func, GLfixed ref);
void glLineWidthx(GLfixed width);
void glPolygonOffsetx(GLfixed factor, GLfixed units);
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz);
void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f);
void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void glScalex(GLfixed x, GLfixed y, GLfixed z);
void glTranslatex(GLfixed x, GLfixed y, GLfixed z);
void glLoadMatrixx(const GLfixed *m);
void glMultMatrixx(const GLfixed *m);
void glMaterialx(GLenum face, GLenum pname, GLfixed param);
void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params);
void glLightxv(GLenum light, GLenum pname, const GLfixed *params);
void glLightModelxv(GLenum pname, const GLfixed *params);
void glTexParameterx(GLenum target, GLenum pname, GLfixed param);
void glTexEnvx(GLenum target, GLenum pname, GLfixed param);
void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params);
void glFogxv(GLenum pname, const GLfixed *params);
/* Separate stencil faces (collapse to PICA's single stencil unit). */
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
void glStencilMaskSeparate(GLenum face, GLuint mask);
/* Multitexture coord convenience forms. */
void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
void glMultiTexCoord2fv(GLenum target, const GLfloat *v);
void glMultiTexCoord2i(GLenum target, GLint s, GLint t);
/* Misc. */
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
/* GLU helpers. */
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
               GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ);
GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const void *data);

/* Attribute stack */
void glPushAttrib(GLbitfield mask);

void glPopAttrib(void);

void glPushClientAttrib(GLbitfield mask);

void glPopClientAttrib(void);

/* Immediate mode (emulated via vertex arrays) */
void glBegin(GLenum mode);

void glEnd(void);

void glArrayElement(GLint i);

/* GL 2.0+ Shader pipeline (stubs for compat) */
GLuint glCreateShader(GLenum type);

void glShaderSource(GLuint shader, GLsizei count, const char **string, const GLint *length);

void glCompileShader(GLuint shader);

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);

void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, char *infoLog);

GLuint glCreateProgram(void);

void glAttachShader(GLuint program, GLuint shader);

void glLinkProgram(GLuint program);

void glGetProgramiv(GLuint program, GLenum pname, GLint *params);

void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, char *infoLog);

void glDeleteShader(GLuint shader);

void glDeleteProgram(GLuint program);

GLint glGetUniformLocation(GLuint program, const char *name);

GLint glGetAttribLocation(GLuint program, const char *name);

void glUseProgram(GLuint program);

void glUniform1i(GLint location, GLint v0);

void glUniform1f(GLint location, GLfloat v0);

void glUniform2f(GLint location, GLfloat v0, GLfloat v1);

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);

void glUniform4iv(GLint location, GLsizei count, const GLint *value);

void glBindAttribLocation(GLuint program, GLuint index, const char *name);

/* GL 3.1 uniform blocks (UBOs) — stubs; PICA has no uniform blocks. */
GLuint glGetUniformBlockIndex(GLuint program, const char *uniformBlockName);

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);

/* GL 3.0+ VAO (real, store the client arrays per object) */
void glGenVertexArrays(GLsizei n, GLuint *arrays);

void glBindVertexArray(GLuint array);

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);

/* GL 2.0+ Vertex attrib pointers (no-op, PICA is fixed-function) */
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                           const GLvoid *pointer);

void glEnableVertexAttribArray(GLuint index);

void glDisableVertexAttribArray(GLuint index);

/* Framebuffer extensions */
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);

GLenum glCheckFramebufferStatus(GLenum target);

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1,
                       GLint dstY1, GLbitfield mask, GLenum filter);

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

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height);

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width,
                      GLsizei height, GLint border);

void glReadBuffer(GLenum mode);

// egl*
EGLBoolean eglBindAPI(EGLenum api);

EGLDisplay eglGetDisplay(NativeDisplayType native_display);

EGLint eglGetError(void);

void (*eglGetProcAddress(char const *procname))(void);

EGLuint64 eglGetSystemTimeFrequencyNV(void);

EGLuint64 eglGetSystemTimeNV(void);

EGLenum eglQueryAPI(void);

EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval);

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface);

void *novaglGetProcAddress(const char *name);

char *novaglGetFuncName(uint32_t func);

/* GL_KHR_debug entry point. Stores the callback (real state) but never invokes
 * it — NovaGL emits no async debug messages. */
void glDebugMessageCallback(GLDEBUGPROC callback, const void *userParam);
void glDebugMessageCallbackKHR(GLDEBUGPROC callback, const void *userParam);

/* KHR_debug group annotations — inert stubs (PICA has no debug stream). */
void glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message);
void glPopDebugGroup(void);

/* glad-compatible loaders. NovaGL links its GL entry points directly, so there
 * is nothing to resolve through `load` — these just report success (non-zero)
 * so display init code that expects a glad loader proceeds. */
int gladLoadGLLoader(GLADloadproc load);
int gladLoadGLES1Loader(GLADloadproc load);
int gladLoadGLES2Loader(GLADloadproc load);

/* ------------------------------------------------------------------------
 * NovaGL fast-path extensions (vitaGL-inspired)
 * ------------------------------------------------------------------------
 * The standard glDrawArrays / glDrawElements paths walk a long chain of
 * conditional checks per call to detect whether the bound state matches
 * one of the optimized layouts (packed PTC, raw 24-byte interleaved, etc).
 * For engines that already know they conform to the layout, that overhead
 * is wasted. novaXxxPointer + novaDrawObjects let an application commit
 * its vertex layout once and then issue draws that skip the dispatch.
 *
 * Layout contract for novaDrawObjects:
 *   position : 3 floats at offset 0
 *   texcoord : 2 floats at offset 12
 *   color    : 4 unsigned bytes at offset 20 (ABGR / GL convention)
 *   stride   : 24 bytes (fixed PTC layout)
 *
 * Pointers passed to novaVertexPointerFast/etc must be linearAlloc'd memory
 * (use a VBO via glBufferData) — the GPU reads them directly bypassing CPU
 * cache, so the buffer is flushed on bind. */
void novaVertexPointerFast(GLuint vbo, GLuint offset);
void novaIndexPointerFast(GLuint vbo, GLenum type);
void novaDrawObjects(GLenum mode, GLsizei count);
void novaDrawObjectsIndexed(GLenum mode, GLsizei count, const GLvoid *indices);

/* Force-invalidate the NovaGL state cache (call after frame boundaries
 * or after foreign code touched the C3D state directly). Same as the
 * internal nova_invalidate_state_cache; exposed for engine integration. */
void novaInvalidateStateCache(void);

/* ------------------------------------------------------------------------
 * Render-to-texture (FBO) extension
 * ------------------------------------------------------------------------
 * NovaGL's stock glGenFramebuffers / glFramebufferTexture2D path attaches
 * a target to a texture allocated through C3D_TexInit (= linear RAM). PICA's
 * raster pipeline writes color through GX_MemoryFill / display-transfer
 * paths that prefer VRAM-resident color buffers; render-targets on linear
 * textures appear to "work" but produce garbage or silent no-ops on real
 * hardware (the symptom in fast3d-on-NovaGL is white/transparent blobs
 * wherever the engine renders to and then samples from an FBO — menu
 * blur backgrounds, previous-frame motion blur, character thumbnails).
 *
 * This helper does the citro3d-canonical sequence in one call:
 *   1. allocate a TexSlot
 *   2. C3D_TexInitVRAM for the color buffer
 *   3. set sane filter / wrap
 *   4. allocate an FBOSlot
 *   5. C3D_RenderTargetCreateFromTex with the requested depth format
 *
 * On success returns 1 and fills *out_tex_id with the GL texture id (use
 * with glBindTexture for sampling) and *out_fbo_id with the GL framebuffer
 * id (use with glBindFramebuffer to draw into the surface).
 *
 * has_depth: 0 → color-only target (matches Butterscotch surfaces);
 *            non-zero → 16-bit depth attached (matches fast3d's needs).
 *
 * On failure (out of slots / out of VRAM / target creation failed) returns
 * 0 and the out-params are left undefined. Caller frees with the standard
 * glDeleteTextures + glDeleteFramebuffers. */
int novaCreateRenderTextureFBO(int width, int height, int has_depth,
                               GLuint *out_tex_id, GLuint *out_fbo_id);

/* ------------------------------------------------------------------------
 * Render-target blit (glBlitFramebuffer equivalent on PICA)
 * ------------------------------------------------------------------------
 * Copies the colour content of one render target into another by drawing a
 * textured fullscreen quad — same approach as Butterscotch's surface_copy
 * and what the citro3d sample code uses for screen → texture transfers.
 * PICA has no analogue of glBlitFramebuffer; this is the cheapest GPU-side
 * path that works for arbitrary src/dst size pairs.
 *
 * Use cases:
 *   - fast3d's `gfx_rapi->copy_framebuffer(dst, src, ...)` for the N64
 *     `gDPCopyFramebufferEXT` opcode (PD's bondview snapshots the screen
 *     into a transient FBO for distortion / scope / cutscene overlays).
 *   - Generic "save the current frame for later sampling" patterns in
 *     engines that ported up from desktop GL.
 *
 * src_fbo_id / dst_fbo_id: 0 → on-screen target (g.render_target_top);
 * anything else → FBO id returned by novaCreateRenderTextureFBO /
 * glGenFramebuffers.
 *
 * Returns 1 on success, 0 if either id is invalid or the underlying GPU
 * draw call couldn't be staged (e.g. linear-heap exhaustion). Leaves the
 * NovaGL state cache invalidated so the next draw re-pushes the engine's
 * own GPU state — we trash render target, viewport, TEV, etc. internally
 * and don't bother saving them register-by-register. */
int novaBlitTargetToFBO(GLuint src_fbo_id, GLuint dst_fbo_id);

/* Copy the app surface into the previous-frame snapshot. Call once at the
 * top of every frame (before its clear/draws): while frame F is being built
 * the snapshot then holds the fully-presented frame F-1 — i.e. the N64/PC
 * "front buffer". No-op when the app surface or the snapshot is absent. */
void novaSnapshotAppSurface(void);

/* Blit the previous-frame snapshot (front buffer) into dst FBO. Falls back
 * to the live app surface when the snapshot isn't allocated. Use for
 * PD-style "capture what the player currently SEES" effects (menu blur,
 * damage blur); novaBlitTargetToFBO(0, dst) stays the mid-frame back-buffer
 * capture. */
int novaBlitSnapshotToFBO(GLuint dst_fbo_id);

/* ------------------------------------------------------------------------
 * Explicit TEV stage programming (multi-stage CC support)
 * ------------------------------------------------------------------------
 * NovaGL's default TEV path emits one PICA TEV stage per active texture
 * unit (driven by glEnable(GL_TEXTURE_2D) + GL_COMBINE state). That's
 * sufficient for the common "1 stage of texel * primary" case but breaks
 * down when the caller needs:
 *   - Two TEV stages from a single texture binding (e.g. fast3d's 2-cycle
 *     CC where cycle 1 does `previous * SHADE` after cycle 0 produced
 *     a colour from texel + primary).
 *   - A stage that samples GPU_TEXTURE1 from a stage other than unit 1's
 *     auto-allocated slot (e.g. TRILERP: `mix(TEX0, TEX1, factor)` needs
 *     both textures referenced from the SAME stage).
 *
 * novaSetExplicitTevStages() lets the caller program up to 6 PICA TEV
 * stages directly. While the explicit list is active (count > 0):
 *   - apply_gpu_state IGNORES the GL_COMBINE / TEXTURE_ENV_MODE machinery
 *     and emits exactly the stages provided.
 *   - GL_TEXTURE_2D enable/disable still controls which texture units are
 *     bound to the GPU, but does NOT influence stage count or programming.
 *   - The caller is responsible for matching TEV source enums to texture
 *     units that are actually bound (otherwise the sample reads stale or
 *     uninitialised VRAM).
 *
 * Call novaClearExplicitTevStages() (or pass count=0) to revert to the
 * GL_COMBINE-driven path. The explicit list is per-draw state — typically
 * the caller sets it at the start of a shader's setup and clears it after
 * the draw or on shader change.
 *
 * Source / op / func enums use GL constants (mirroring the GL_COMBINE
 * vocabulary) so callers don't need to learn citro3d's GPU_TEV* enums:
 *   src   : GL_TEXTURE0_ARB, GL_TEXTURE1_ARB, GL_TEXTURE2_ARB,
 *           GL_PREVIOUS, GL_PRIMARY_COLOR, GL_CONSTANT
 *   op    : GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
 *           GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
 *   func  : GL_REPLACE, GL_MODULATE, GL_ADD, GL_INTERPOLATE, GL_SUBTRACT
 *
 * `constant_color` is consumed only when one of the src fields is
 * GL_CONSTANT; otherwise ignored. */
#define NOVA_TEV_MAX_STAGES 6

typedef struct {
    GLenum  combine_rgb;
    GLenum  src_rgb[3];
    GLenum  op_rgb[3];
    GLenum  combine_alpha;
    GLenum  src_alpha[3];
    GLenum  op_alpha[3];
    float   constant_color[4];
} NovaTevStageGL;

void novaSetExplicitTevStages(int count, const NovaTevStageGL *stages);
void novaClearExplicitTevStages(void);

/* ------------------------------------------------------------------------
 * Pre-packed clip-space triangle fast lane
 * ------------------------------------------------------------------------
 * Draws GL_TRIANGLES from vertices ALREADY packed in NovaGL's native
 * interleaved layout for the clipspace shader:
 *     float   x, y, z, w;   // clip-space position (pre perspective divide)
 *     float   u, v;         // texcoord0 (texcoord1 mirrors it in-shader)
 *     uint8_t r, g, b, a;   // vertex colour
 * = 28 bytes per vertex. Skips the glVertexPointer/glDrawArrays conversion
 * path entirely: one memcpy into the GPU ring instead of a per-vertex
 * repack + float→byte colour conversion. Built for fast3d-style backends
 * that already assemble vertices per draw. All current GL state (TEV,
 * blending, depth, bound textures) applies as usual. Only meaningful while
 * novaBeginClipSpace2D mode is active. */
void novaDrawClipspaceTris(const void *verts, int vertex_count);

/* Texcoord scale (logical/POT) for a texture id — see texture.c. Multiply
 * client texcoords by this when feeding the clipspace fast lane so NPOT
 * textures (font glyphs) sample the right sub-region. POT textures → 1.0. */
void novaGetTexCoordScale(GLuint texture, float *su, float *sv);

/* Texture id that aliases the on-screen app surface, so callers can bind the
 * SCREEN as a sampleable texture (e.g. an engine sampling "framebuffer 0").
 * Returns 0 if the app surface isn't available. NOTE: sampling this while the
 * screen is also the active render target is a read-after-write hazard — the
 * caller should have snapshotted/finished the frame's screen writes first. */
GLuint novaGetScreenTextureId(void);

/* ------------------------------------------------------------------------
 * Raw clip-space passthrough (UI/HUD fast lane)
 * ------------------------------------------------------------------------
 * Between novaBeginClipSpace2D() and novaEndClipSpace2D() NovaGL switches
 * to a stripped vertex shader that just emits the position attribute as
 * clip-space output. No MVP transform, no fog, no texmtx — ≈10 dp4 saved
 * per vertex. Suitable for batched UI / sprite / font rendering when the
 * client has already done the projection on the CPU side.
 *
 * Caller contract:
 *   - Position attribute is (x, y, z, w) in clip space ([-1, 1] for x/y).
 *   - The screen-rotation tilt is NOT applied — your UI vertex producer
 *     must already account for the 3DS portrait-on-top orientation (swap
 *     x and y, or whatever your target screen requires).
 *   - Fog / tex matrix state is ignored while the mode is active.
 *
 * Calls nest as a flat begin/end — only one level deep. */
void novaBeginClipSpace2D(void);
void novaEndClipSpace2D(void);

/* =========================================================================
 * PICA200 Hardware Profiler
 * ========================================================================= */
typedef struct {
 unsigned int vertex_processor;    ///< VP: Vertex Shader workload
 unsigned int command_interface;   ///< CI: Command Buffer reads
 unsigned int triangle_interface;  ///< TI: Polygon interface
 unsigned int triangle_setup;      ///< TS: Rasterizer (Fillrate)
 unsigned int light_reflection;    ///< LR: Hardware lighting
 unsigned int texture_fetch;       ///< TX: Texture fetch (TMU)
 unsigned int color_updater;       ///< CU: Blending, Alpha test
 unsigned int texture_blender;     ///< TB: Texture blending (TexEnv)

 unsigned int mem_vram_0_read;
 unsigned int mem_vram_0_write;
 unsigned int mem_vram_1_read;
 unsigned int mem_vram_1_write;
 unsigned int mem_p3d_geo_read;    ///< Geometry DMA fetch
 unsigned int mem_p3d_tex_read;    ///< Texture sampling fetch
 unsigned int mem_p3d_cu_0_read;   ///< Framebuffer read (e.g., blending)
 unsigned int mem_p3d_cu_0_write;  ///< Framebuffer write
} NovaProfileStats;

void novaBeginProfiling(void);
void novaEndProfiling(void);
void novaGetProfileStats(NovaProfileStats *out_stats);
#ifdef __cplusplus
}
#endif

#endif /* NOVA_H */
