//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

/* Values fixed by the GL spec; defined locally if NovaGL.h lacks them. */
#ifndef GL_COMPILE
#define GL_COMPILE 0x1300
#endif
#ifndef GL_COMPILE_AND_EXECUTE
#define GL_COMPILE_AND_EXECUTE 0x1301
#endif

static int ensure_dl_store(void) {
    if (g.dl_store) return 1;
    g.dl_store = (DisplayList *) calloc(NOVA_DISPLAY_LISTS, sizeof(DisplayList));
    return g.dl_store != NULL;
}

GLuint glGenLists(GLsizei range) {
    /* Spec: range < 0 → GL_INVALID_VALUE, return 0. range == 0 → return 0,
     * no error. */
    if (range < 0) {
        g.last_error = GL_INVALID_VALUE;
        return 0;
    }
    if (range == 0) return 0;
    if (!ensure_dl_store()) {
        g.last_error = GL_OUT_OF_MEMORY;
        return 0;
    }

    /* Spec: the returned range must be CONTIGUOUS. The old wrap logic could
     * return a base near the end of the pool and mark fewer than `range`
     * slots — callers then recorded into unmarked ids. Now: if the range
     * doesn't fit at the cursor, restart from 1; if it still doesn't fit,
     * fail with 0 (no error per spec — "0 if no contiguous block"). */
    if (g.dl_next_base == 0) g.dl_next_base = 1;
    GLuint base = g.dl_next_base;
    if (base + (GLuint) range > NOVA_DISPLAY_LISTS) {
        base = 1;
        if (base + (GLuint) range > NOVA_DISPLAY_LISTS) {
            return 0;
        }
    }
    g.dl_next_base = base + (GLuint) range;
    if (g.dl_next_base >= NOVA_DISPLAY_LISTS) g.dl_next_base = 1;

    for (GLsizei i = 0; i < range; i++) {
        g.dl_store[base + i].count = 0;
        g.dl_store[base + i].used = 1;
    }
    return base;
}

void glNewList(GLuint list, GLenum mode) {
    /* Spec: list == 0 → GL_INVALID_VALUE; bad mode → GL_INVALID_ENUM;
     * nested glNewList → GL_INVALID_OPERATION. */
    if (list == 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) {
        g.last_error = GL_INVALID_ENUM;
        return;
    }
    if (g.dl_recording >= 0) {
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    if (!ensure_dl_store()) {
        g.last_error = GL_OUT_OF_MEMORY;
        return;
    }
    if (list < NOVA_DISPLAY_LISTS) {
        g.dl_recording = (int) list;
        g.dl_store[list].count = 0;
        g.dl_store[list].used = 1;
    }
}

void glEndList(void) {
    if (g.dl_recording < 0) {
        /* Spec: glEndList without a matching glNewList. */
        g.last_error = GL_INVALID_OPERATION;
        return;
    }
    g.dl_recording = -1;
}

void glCallList(GLuint list) { dl_execute(list); }

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists) {
    if (n < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (!lists) return;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = 0;
        if (type == GL_UNSIGNED_INT) id = ((const GLuint *) lists)[i];
        else if (type == GL_UNSIGNED_BYTE) id = ((const GLubyte *) lists)[i];
        else if (type == GL_UNSIGNED_SHORT) id = ((const GLushort *) lists)[i];
        else {
            g.last_error = GL_INVALID_ENUM;
            return;
        }
        dl_execute(id);
    }
}

void glDeleteLists(GLuint list, GLsizei range) {
    if (range < 0) {
        g.last_error = GL_INVALID_VALUE;
        return;
    }
    if (!g.dl_store) return;
    for (GLsizei i = 0; i < range && (list + i) < NOVA_DISPLAY_LISTS; i++) {
        g.dl_store[list + i].used = 0;
        g.dl_store[list + i].count = 0;
    }
}

GLboolean glIsList(GLuint list) {
    if (!g.dl_store || list == 0 || list >= NOVA_DISPLAY_LISTS) return GL_FALSE;
    return g.dl_store[list].used ? GL_TRUE : GL_FALSE;
}