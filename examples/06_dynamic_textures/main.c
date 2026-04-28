#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <math.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z, u, v;
    unsigned int color;
} Vertex;

#define TEX_SIZE 64

int main(void) {
    gfxInitDefault();
    nova_init();

    unsigned char *tex_pixels = (unsigned char *) malloc(TEX_SIZE * TEX_SIZE * 4);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    Vertex quad[6] = {
        {-2.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF}, {2.0f, -2.0f, 0.0f, 1.0f, 0.0f, 0xFFFFFFFF},
        {2.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF},
        {-2.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF}, {2.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF},
        {-2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF}
    };
    Vertex *vbo_data = (Vertex *) linearAlloc(sizeof(quad));
    memcpy(vbo_data, quad, sizeof(quad));
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), vbo_data, GL_STATIC_DRAW);

    float time = 0.0f;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        time += 0.1f;

        // --- ВЫЧИСЛЕНИЯ НА CPU И ОБНОВЛЕНИЕ GPU СТРОГО 1 РАЗ ЗА КАДР ---
        for (int y = 0; y < TEX_SIZE; y++)
            for (int x = 0; x < TEX_SIZE; x++) {
                float v = sinf(x / 8.0f + time) + sinf(y / 8.0f + time / 2.0f) + sinf(x / 8.0f + y / 8.0f + time);
                int c = (int) ((v + 3.0f) * 42.0f);
                if (c < 0) c = 0;
                if (c > 255) c = 255;
                int idx = (y * TEX_SIZE + x) * 4;
                tex_pixels[idx + 0] = c;
                tex_pixels[idx + 1] = c / 3;
                tex_pixels[idx + 2] = 0;
                tex_pixels[idx + 3] = 255;
            }
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEX_SIZE, TEX_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, tex_pixels);
        // -------------------------------------------------------------

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);

            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glTranslatef(0.0f, 0.0f, -5.0f);
            glRotatef(60.0f, 1.0f, 0.0f, 0.0f);
            glRotatef(time * 5.0f, 0.0f, 0.0f, 1.0f);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_TEXTURE_2D);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);

            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void *) 0);
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void *) (3 * 4));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void *) (5 * 4));

            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    free(tex_pixels);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    linearFree(vbo_data);
    nova_fini();
    gfxExit();
    return 0;
}
