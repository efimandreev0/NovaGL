#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z, u, v;
    u8 r, g, b, a;
} Vertex;

void set_vertex_color(Vertex *v, float y) {
    v->a = 255;
    if (y > 1.5f) {
        v->r = 255;
        v->g = 255;
        v->b = 255;
    } else if (y < -0.5f) {
        v->r = 40;
        v->g = 100;
        v->b = 40;
    } else {
        v->r = 80;
        v->g = 200;
        v->b = 80;
    }
}

#define GRID_SIZE 32
#define VERTICES_PER_QUAD 6
#define TOTAL_VERTICES ((GRID_SIZE - 1) * (GRID_SIZE - 1) * VERTICES_PER_QUAD)

int main(void) {
    gfxInitDefault();
    nova_init();

    Vertex *vbo_data = (Vertex *) linearAlloc(TOTAL_VERTICES * sizeof(Vertex));
    int v_idx = 0;
    for (int x = 0; x < GRID_SIZE - 1; x++)
        for (int z = 0; z < GRID_SIZE - 1; z++) {
            float x0 = x - GRID_SIZE / 2.0f, z0 = z - GRID_SIZE / 2.0f;
            float x1 = x0 + 1.0f, z1 = z0 + 1.0f;
            float y00 = sinf(x0 * 0.5f) * 1.5f + cosf(z0 * 0.5f) * 1.5f, y10 =
                    sinf(x1 * 0.5f) * 1.5f + cosf(z0 * 0.5f) * 1.5f;
            float y01 = sinf(x0 * 0.5f) * 1.5f + cosf(z1 * 0.5f) * 1.5f, y11 =
                    sinf(x1 * 0.5f) * 1.5f + cosf(z1 * 0.5f) * 1.5f;

            vbo_data[v_idx] = (Vertex){x0, y00, z0, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y00);
            vbo_data[v_idx] = (Vertex){x0, y01, z1, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y01);
            vbo_data[v_idx] = (Vertex){x1, y10, z0, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y10);
            vbo_data[v_idx] = (Vertex){x1, y10, z0, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y10);
            vbo_data[v_idx] = (Vertex){x0, y01, z1, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y01);
            vbo_data[v_idx] = (Vertex){x1, y11, z1, 0, 0, 0, 0, 0, 0};
            set_vertex_color(&vbo_data[v_idx++], y11);
        }
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v_idx * sizeof(Vertex), vbo_data, GL_STATIC_DRAW);

    float angle_y = 0.0f;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        angle_y += 0.5f; // Обновляем 1 раз за кадр!

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);

            glClearColor(0.6f, 0.8f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 50.0f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glTranslatef(0.0f, -4.0f, -20.0f);
            glRotatef(30.0f, 1.0f, 0.0f, 0.0f);
            glRotatef(angle_y, 0.0f, 1.0f, 0.0f);

            glEnable(GL_DEPTH_TEST);
            glDisable(GL_FOG);
            glDisable(GL_TEXTURE_2D);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);

            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void *) offsetof(Vertex, x));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void *) offsetof(Vertex, r));

            glDrawArrays(GL_TRIANGLES, 0, v_idx);

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    glDeleteBuffers(1, &vbo);
    linearFree(vbo_data);
    nova_fini();
    gfxExit();
    return 0;
}
