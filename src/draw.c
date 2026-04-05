//
// created by efimandreev0 on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    nova_draw_internal(mode, first, count, 0, 0, NULL);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    nova_draw_internal(mode, 0, count, 1, type, indices);
}