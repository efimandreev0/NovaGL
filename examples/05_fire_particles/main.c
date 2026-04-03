#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <math.h>
#include <NovaGL.h>

typedef struct { float x,y,z; unsigned int color; } Vertex;

unsigned int make_color(u8 r, u8 g, u8 b, u8 a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

#define MAX_PARTICLES 500

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;
    float max_life;
} Particle;

float randf() { return (float)rand() / (float)RAND_MAX; }

int main(void) {
    gfxInitDefault();
    gl2c3d_init();

    Particle particles[MAX_PARTICLES];
    for(int i=0; i<MAX_PARTICLES; i++) particles[i].life = -1.0f; /* Неактивны */

    /* В этом примере используем Client Arrays (без VBO) для проверки динамики */
    Vertex *vertex_data = (Vertex*)linearAlloc(MAX_PARTICLES * 6 * sizeof(Vertex));

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        /* Обновляем и спавним частицы */
        int v_idx = 0;
        for(int i=0; i<MAX_PARTICLES; i++) {
            if (particles[i].life <= 0.0f) {
                /* Спавн новой частицы (искра из костра) */
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
                /* Огонь: желтый внизу, красный в середине, пропадает вверху */
                u8 r = 255;
                u8 g = (u8)(255 * fade);
                u8 b = 0;
                u8 a = (u8)(255 * fade);
                unsigned int col = make_color(r, g, b, a);

                /* Биллборд квад (всегда смотрит в камеру Z+) */
                float s = 0.1f + (1.0f - fade) * 0.1f; /* Расширяется со временем */
                float px = particles[i].x, py = particles[i].y, pz = particles[i].z;

                vertex_data[v_idx++] = (Vertex){px-s, py-s, pz, col};
                vertex_data[v_idx++] = (Vertex){px+s, py-s, pz, col};
                vertex_data[v_idx++] = (Vertex){px+s, py+s, pz, col};
                
                vertex_data[v_idx++] = (Vertex){px-s, py-s, pz, col};
                vertex_data[v_idx++] = (Vertex){px+s, py+s, pz, col};
                vertex_data[v_idx++] = (Vertex){px-s, py+s, pz, col};
            }
        }

        gl2c3d_frame_begin();
        gl2c3d_set_render_target(0);

        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -6.0f);

        /* СЕКРЕТ КРАСИВЫХ ЧАСТИЦ: Отключаем запись в глубину, включаем Аддитивный Блендинг */
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE); /* Тестируем Z, но не пишем в него! */
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Аддитивный блендинг для свечения */
        
        glDisable(GL_TEXTURE_2D);

        /* Рисуем прямо из памяти, без VBO */
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &vertex_data[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertex_data[0].color);

        glDrawArrays(GL_TRIANGLES, 0, v_idx);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        /* Возвращаем стейт обратно */
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        gl2c3d_frame_end();
    }

    linearFree(vertex_data);
    gl2c3d_fini();
    gfxExit();
    return 0;
}