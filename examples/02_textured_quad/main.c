#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>
#include <NovaGL.h>

typedef struct { float x, y, z, u, v; unsigned int color; } Vertex;
static unsigned int make_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static unsigned char* generate_checkerboard(int size, int tile_size) {
    unsigned char *pixels = (unsigned char*)malloc(size * size * 4);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int checker = ((x / tile_size) + (y / tile_size)) & 1;
        int idx = (y * size + x) * 4;
        pixels[idx+0] = checker?200:240; pixels[idx+1] = checker?80:240;
        pixels[idx+2] = checker?220:240; pixels[idx+3] = 255;
    }
    return pixels;
}

int main(void) {
    gfxInitDefault();
    nova_init();

    unsigned char *tex_data = generate_checkerboard(64, 8);
    GLuint texture; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
    free(tex_data);

    unsigned int white = make_color(255, 255, 255, 255);
    Vertex quad[6] = {
        {-0.7f,  0.7f, 0.0f,  0.0f, 0.0f, white}, {-0.7f, -0.7f, 0.0f,  0.0f, 2.0f, white}, { 0.7f, -0.7f, 0.0f,  2.0f, 2.0f, white},
        {-0.7f,  0.7f, 0.0f,  0.0f, 0.0f, white}, { 0.7f, -0.7f, 0.0f,  2.0f, 2.0f, white}, { 0.7f,  0.7f, 0.0f,  2.0f, 0.0f, white},
    };
    Vertex *vbo_data = (Vertex*)linearAlloc(sizeof(quad)); memcpy(vbo_data, quad, sizeof(quad));
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), vbo_data, GL_STATIC_DRAW);

    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown() & KEY_START) break;

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);
            novaSet3DDepth(0.0f);

            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();

            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture); glDisable(GL_DEPTH_TEST);

            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void*)0);
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void*)(3 * 4));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)(5 * 4));

            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    glDeleteTextures(1, &texture); glDeleteBuffers(1, &vbo); linearFree(vbo_data); nova_fini(); gfxExit(); return 0;
}