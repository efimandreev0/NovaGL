#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z;
    unsigned int color;
} Vertex;

unsigned int make_color(u8 r, u8 g, u8 b, u8 a) { return (a << 24) | (b << 16) | (g << 8) | r; }

#define MAX_PARTICLES 500

typedef struct {
    float x, y, z, vx, vy, vz, life, max_life;
} Particle;

float randf() { return (float) rand() / (float) RAND_MAX; }

int main(void) {
    gfxInitDefault();
    nova_init();
    Particle particles[MAX_PARTICLES];
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].life = -1.0f;
    Vertex *vertex_data = (Vertex *) linearAlloc(MAX_PARTICLES * 6 * sizeof(Vertex));

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        // --- ФИЗИКА ЧАСТИЦ 1 РАЗ ЗА КАДР ---
        int v_idx = 0;
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].life <= 0.0f) {
                particles[i].x = (randf() - 0.5f) * 1.5f;
                particles[i].y = -2.0f;
                particles[i].z = (randf() - 0.5f) * 1.5f;
                particles[i].vx = (randf() - 0.5f) * 0.05f;
                particles[i].vy = 0.05f + randf() * 0.05f;
                particles[i].vz = (randf() - 0.5f) * 0.05f;
                particles[i].max_life = 1.0f + randf();
                particles[i].life = particles[i].max_life;
            }
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].z += particles[i].vz;
            particles[i].life -= 0.016f;

            if (particles[i].life > 0.0f) {
                float fade = particles[i].life / particles[i].max_life;
                unsigned int col = make_color(255, (u8) (255 * fade), 0, (u8) (255 * fade));
                float s = 0.1f + (1.0f - fade) * 0.1f;
                float px = particles[i].x, py = particles[i].y, pz = particles[i].z;

                vertex_data[v_idx++] = (Vertex){px - s, py - s, pz, col};
                vertex_data[v_idx++] = (Vertex){px + s, py - s, pz, col};
                vertex_data[v_idx++] = (Vertex){px + s, py + s, pz, col};
                vertex_data[v_idx++] = (Vertex){px - s, py - s, pz, col};
                vertex_data[v_idx++] = (Vertex){px + s, py + s, pz, col};
                vertex_data[v_idx++] = (Vertex){px - s, py + s, pz, col};
            }
        }
        // ----------------------------------

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);
            glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glTranslatef(0.0f, 0.0f, -6.0f);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDisable(GL_TEXTURE_2D);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &vertex_data[0].x);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertex_data[0].color);

            glDrawArrays(GL_TRIANGLES, 0, v_idx);

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        novaSwapBuffers();
    }
    linearFree(vertex_data);
    nova_fini();
    gfxExit();
    return 0;
}
