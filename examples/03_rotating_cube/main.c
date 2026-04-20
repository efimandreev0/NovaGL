/*
 * NovaGL Example: Rotating Cube in 3D (Stereoscopic)
 *
 * Demonstrates:
 *   - Automatic Stereoscopic 3D via NovaGL (novaGetEyeCount, novaBeginEye)
 *   - Perspective projection (glFrustumf)
 *   - Model transformations (glTranslatef, glRotatef)
 *   - Depth testing (GL_DEPTH_TEST)
 *   - Textured 3D geometry
 *   - Per-face vertex colors
 */

#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <NovaGL.h> // Меняем заголовок на NovaGL

typedef struct {
    float x, y, z;
    float u, v;
    unsigned int color;
} Vertex;

static unsigned int make_color(unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* Generate a simple 32x32 checkerboard */
static unsigned char* generate_texture(int size) {
    unsigned char *pixels = (unsigned char*)malloc(size * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int checker = ((x / 4) + (y / 4)) & 1;
            int idx = (y * size + x) * 4;
            pixels[idx + 0] = checker ? 255 : 60;
            pixels[idx + 1] = checker ? 255 : 60;
            pixels[idx + 2] = checker ? 255 : 60;
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

/* Build a cube: 6 faces * 2 triangles * 3 vertices = 36 vertices */
static void build_cube(Vertex *out, float s) {
    unsigned int colors[6] = {
        make_color(255, 100, 100, 255),  /* front  - red */
        make_color(100, 255, 100, 255),  /* back   - green */
        make_color(100, 100, 255, 255),  /* top    - blue */
        make_color(255, 255, 100, 255),  /* bottom - yellow */
        make_color(255, 100, 255, 255),  /* right  - magenta */
        make_color(100, 255, 255, 255),  /* left   - cyan */
    };

    /* Face positions: each face = 4 corners, we expand to 2 triangles */
    float faces[6][4][3] = {
        {{-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s}}, /* Front (+Z) */
        {{ s,-s,-s},{-s,-s,-s},{-s, s,-s},{ s, s,-s}}, /* Back (-Z) */
        {{-s, s, s},{ s, s, s},{ s, s,-s},{-s, s,-s}}, /* Top (+Y) */
        {{-s,-s,-s},{ s,-s,-s},{ s,-s, s},{-s,-s, s}}, /* Bottom (-Y) */
        {{ s,-s, s},{ s,-s,-s},{ s, s,-s},{ s, s, s}}, /* Right (+X) */
        {{-s,-s,-s},{-s,-s, s},{-s, s, s},{-s, s,-s}}, /* Left (-X) */
    };

    float uvs[4][2] = {{0,1},{1,1},{1,0},{0,0}};

    int v = 0;
    for (int f = 0; f < 6; f++) {
        int idx[6] = {0,1,2, 0,2,3};
        for (int i = 0; i < 6; i++) {
            int c = idx[i];
            out[v].x = faces[f][c][0];
            out[v].y = faces[f][c][1];
            out[v].z = faces[f][c][2];
            out[v].u = uvs[c][0];
            out[v].v = uvs[c][1];
            out[v].color = colors[f];
            v++;
        }
    }
}

int main(void) {
    // Инициализация сервисов и NovaGL
    gfxInitDefault();
    nova_init();

    // Задаем комфортную глубину 3D-эффекта (опционально, по дефолту уже 0.05f)
    novaSet3DDepth(0.06f);

    /* Create texture */
    const int TEX_SIZE = 32;
    unsigned char *tex_data = generate_texture(TEX_SIZE);

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

    /* Build cube geometry */
    Vertex cube[36];
    build_cube(cube, 0.5f);

    Vertex *vbo_data = (Vertex*)linearAlloc(sizeof(cube));
    memcpy(vbo_data, cube, sizeof(cube));

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), vbo_data, GL_STATIC_DRAW);

    float angle_x = 0.0f;
    float angle_y = 0.0f;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;

        angle_x += 1.0f;
        angle_y += 0.7f;

        // =================================================================
        // Вся магия 3D здесь:
        // Узнаем, сколько раз нам рисовать кадр. Если ползунок опущен = 1.
        // Если ползунок поднят = 2.
        int eyes = novaGetEyeCount();

        for (int i = 0; i < eyes; i++) {
            // NovaGL сама переключит буфер и незаметно "подвинет" матрицу!
            novaBeginEye(i);

            // Дальше идет абсолютно стандартный OpenGL код
            glClearColor(0.05f, 0.05f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            /* Perspective projection */
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            /* Simple perspective: fov ~60 deg, aspect ~5:3 (400x240) */
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);

            /* Modelview: pull back and rotate */
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glTranslatef(0.0f, 0.0f, -3.0f); // Отодвигаем куб назад
            glRotatef(angle_x, 1.0f, 0.0f, 0.0f);
            glRotatef(angle_y, 0.0f, 1.0f, 0.0f);

            /* Enable depth test and texturing */
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);

            /* Draw cube */
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);

            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void*)0);
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void*)(3 * 4));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)(5 * 4));

            glDrawArrays(GL_TRIANGLES, 0, 36);

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }
        // =================================================================

        // Завершаем кадр и передаем его на экран (вместо C3D_FrameBegin/End)
        novaSwapBuffers();
    }

    // Очистка
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    linearFree(vbo_data);

    nova_fini();
    gfxExit();
    return 0;
}