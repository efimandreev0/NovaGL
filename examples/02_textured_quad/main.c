/*
 * gl2citro3d Example 02: Textured Quad
 *
 * Demonstrates:
 *   - glGenTextures, glBindTexture, glTexImage2D
 *   - glTexParameteri (filtering, wrapping)
 *   - Vertex arrays with position + texcoord + color
 *   - glEnable(GL_TEXTURE_2D)
 *   - glEnable(GL_BLEND), glBlendFunc
 *   - Procedural checkerboard texture generation
 */

#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z;
    float u, v;
    unsigned int color;
} Vertex;

static unsigned int make_color(unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* Generate a 64x64 checkerboard RGBA texture */
static unsigned char* generate_checkerboard(int size, int tile_size) {
    unsigned char *pixels = (unsigned char*)malloc(size * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int checker = ((x / tile_size) + (y / tile_size)) & 1;
            int idx = (y * size + x) * 4;
            if (checker) {
                pixels[idx + 0] = 200;  /* R */
                pixels[idx + 1] = 80;   /* G */
                pixels[idx + 2] = 220;  /* B - purple */
                pixels[idx + 3] = 255;  /* A */
            } else {
                pixels[idx + 0] = 240;  /* R */
                pixels[idx + 1] = 240;  /* G */
                pixels[idx + 2] = 240;  /* B - white */
                pixels[idx + 3] = 255;  /* A */
            }
        }
    }
    return pixels;
}

int main(void) {
    gfxInitDefault();
    gl2c3d_init();

    /* Create checkerboard texture */
    const int TEX_SIZE = 64;
    unsigned char *tex_data = generate_checkerboard(TEX_SIZE, 8);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
    free(tex_data);

    /* Quad: two triangles, textured, white vertex color (= texture color only) */
    unsigned int white = make_color(255, 255, 255, 255);
    Vertex quad[6] = {
        /* Triangle 1 */
        {-0.7f,  0.7f, 0.0f,  0.0f, 0.0f, white},
        {-0.7f, -0.7f, 0.0f,  0.0f, 2.0f, white},  /* u,v > 1 to show repeat */
        { 0.7f, -0.7f, 0.0f,  2.0f, 2.0f, white},
        /* Triangle 2 */
        {-0.7f,  0.7f, 0.0f,  0.0f, 0.0f, white},
        { 0.7f, -0.7f, 0.0f,  2.0f, 2.0f, white},
        { 0.7f,  0.7f, 0.0f,  2.0f, 0.0f, white},
    };

    Vertex *vbo_data = (Vertex*)linearAlloc(sizeof(quad));
    memcpy(vbo_data, quad, sizeof(quad));

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), vbo_data, GL_STATIC_DRAW);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        gl2c3d_frame_begin();
        gl2c3d_set_render_target(0);

        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Enable texturing */
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDisable(GL_DEPTH_TEST);

        /* Draw the textured quad */
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void*)0);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void*)(3 * 4));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)(5 * 4));

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        gl2c3d_frame_end();
    }

    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    linearFree(vbo_data);
    gl2c3d_fini();
    gfxExit();
    return 0;
}
