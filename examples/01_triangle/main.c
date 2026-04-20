#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <NovaGL.h>

typedef struct { float x, y, z, u, v; unsigned int color; } Vertex;
static unsigned int make_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

int main(void) {
    gfxInitDefault();
    nova_init();

    Vertex tri[3] = {
        { 0.0f,  0.8f, 0.0f, 0.0f, 0.0f, make_color(255, 0, 0, 255) },
        {-0.8f, -0.8f, 0.0f, 0.0f, 0.0f, make_color(0, 255, 0, 255) },
        { 0.8f, -0.8f, 0.0f, 0.0f, 0.0f, make_color(0, 0, 255, 255) },
    };

    Vertex *vbo_data = (Vertex*)linearAlloc(sizeof(tri));
    memcpy(vbo_data, tri, sizeof(tri));
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), vbo_data, GL_STATIC_DRAW);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);
            novaSet3DDepth(0.0f);

            glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();

            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);

            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void*)0);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)(5 * 4));

            glDrawArrays(GL_TRIANGLES, 0, 3);

            glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    glDeleteBuffers(1, &vbo); linearFree(vbo_data); nova_fini(); gfxExit(); return 0;
}