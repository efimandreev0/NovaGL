#ifndef __glad_h_
#define __glad_h_
#define __gl_h_

#include <NovaGL.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef GLADloadproc GLADloadfunc;

#define GLAD_GL_VERSION_1_0 1
#define GLAD_GL_VERSION_1_1 1
#define GLAD_GL_VERSION_1_2 1
#define GLAD_GL_VERSION_1_3 1
#define GLAD_GL_VERSION_1_4 1
#define GLAD_GL_VERSION_1_5 1
#define GLAD_GL_VERSION_2_0 1
#define GLAD_GL_VERSION_2_1 1
#define GLAD_GL_ES_VERSION_2_0 1
#define GLAD_GL_OES_vertex_buffer_object 1

#define gladLoadGL(load)      ((void)(load), 1)
#define gladLoadGLES2(load)   ((void)(load), 1)

#ifdef __cplusplus
}
#endif

#endif /* __glad_h_ */
