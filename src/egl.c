//
// Created by Notebook on 15.04.2026.
//
#include <stdio.h>
#include <3ds/os.h>

#include "NovaGL.h"
#include "context.h"

//#define EGL_PEDANTIC // This flag makes eGL error be properly set when a function success

char *get_egl_error_literal(uint32_t code) {
    switch (code) {
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        default:
            return "Unknown Error";
    }
}

// Error set funcs
#define SET_EGL_ERROR(x) \
	printf("%s:%d: %s set %s\n", __FILE__, __LINE__, __func__, get_egl_error_literal(x)); \
	egl_error = x; \
	return;
#define SET_EGL_ERROR_WITH_RET(x, y) \
	printf("%s:%d: %s set %s\n", __FILE__, __LINE__, __func__, get_egl_error_literal(x)); \
	egl_error = x; \
	return y;

EGLint egl_error = EGL_SUCCESS;
EGLenum rend_api = EGL_OPENGL_ES_API;

// EGL implementation

EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval) {
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    novaSwapBuffers();
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api) {
    switch (api) {
        case EGL_OPENGL_API:
        case EGL_OPENGL_ES_API:
            rend_api = api;
#ifdef EGL_PEDANTIC
            egl_error = EGL_SUCCESS;
#endif
            return EGL_TRUE;
        default:
            SET_EGL_ERROR_WITH_RET(EGL_BAD_PARAMETER, EGL_FALSE);
    }
}

EGLenum eglQueryAPI(void) {
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    return rend_api;
}

EGLint eglGetError(void) {
    EGLint ret = egl_error;
    egl_error = EGL_SUCCESS;
    return ret;
}

void (*eglGetProcAddress(char const *procname))(void) {
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    return novaglGetProcAddress(procname);
}

EGLDisplay eglGetDisplay(NativeDisplayType native_display) {
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    if (native_display == EGL_DEFAULT_DISPLAY)
        return ((EGLDisplay) 1);
    else
        return EGL_NO_DISPLAY;
}

EGLuint64 eglGetSystemTimeFrequencyNV(void) {
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    return (EGLuint64) SYSCLOCK_ARM11;
}

EGLuint64 eglGetSystemTimeNV(void) {
#ifdef EGL_PEDANTIC
    egl_error = EGL_SUCCESS;
#endif
    return svcGetSystemTick();
}

// === GL_KHR_debug ===========================================================
// NovaGL has no asynchronous debug message stream on PICA200, so we record the
// callback (so glGetPointerv(GL_DEBUG_CALLBACK_*) etc. would be honest) but
// never call it. GL_DEBUG_OUTPUT[_SYNCHRONOUS] go through glEnable's default
// no-op case.
void glDebugMessageCallback(GLDEBUGPROC callback, const void *userParam) {
    g.debug_callback = callback;
    g.debug_user_param = userParam;
}

void glDebugMessageCallbackKHR(GLDEBUGPROC callback, const void *userParam) {
    glDebugMessageCallback(callback, userParam);
}

void glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar *message) {
    (void) source; (void) id; (void) length; (void) message;
}

void glPopDebugGroup(void) {
}

// === glad loader shims ======================================================
// NovaGL's GL entry points are linked directly into the binary, so there is no
// function-pointer table to populate from `load`. Report success so display
// init code written against glad keeps going. The loader proc is intentionally
// unused.
int gladLoadGLLoader(GLADloadproc load) {
    (void) load;
    return 1;
}

int gladLoadGLES1Loader(GLADloadproc load) {
    (void) load;
    return 1;
}

int gladLoadGLES2Loader(GLADloadproc load) {
    (void) load;
    return 1;
}
