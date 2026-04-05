//
// Created by Notebook on 05.04.2026.
//

#include "NovaGL.h"
#include "utils.h"

void glColor4f(GLfloat r, GLfloat g_, GLfloat b, GLfloat a) {
    g.cur_color[0] = r; g.cur_color[1] = g_; g.cur_color[2] = b; g.cur_color[3] = a;
}

void glColor3f(GLfloat r, GLfloat g_, GLfloat b) {
    if (g.dl_recording >= 0) { dl_record_color3f(r, g_, b); return; }
    g.cur_color[0] = r; g.cur_color[1] = g_; g.cur_color[2] = b; g.cur_color[3] = 1.0f;
}

void glColor4ub(GLubyte r, GLubyte g_, GLubyte b, GLubyte a) {
    g.cur_color[0] = r / 255.0f; g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f; g.cur_color[3] = a / 255.0f;
}

void glColor3ub(GLubyte r, GLubyte g_, GLubyte b) {
    g.cur_color[0] = r / 255.0f; g.cur_color[1] = g_ / 255.0f;
    g.cur_color[2] = b / 255.0f; g.cur_color[3] = 1.0f;
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    (void)target; (void)s; (void)t; (void)r; (void)q;
}