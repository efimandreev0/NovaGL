#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <math.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z, u, v;
    u8 r, g, b, a;
} Vertex;

/* Помощник для отрисовки куба/блока */
void draw_box(float x, float y, float z, float w, float h, float d, u8 r, u8 g_c, u8 b) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(w, h, d);
    Vertex box[36];
    int idx = 0;
    // Очень ленивая генерация куба (передняя, задняя, верх, низ, лево, право)
    float f[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}
    };
    for (int i = 0; i < 6; i++) {
        int id[6] = {0, 1, 2, 0, 2, 3};
        for (int j = 0; j < 6; j++) {
            box[idx++] = (Vertex){f[i][id[j]][0], f[i][id[j]][1], f[i][id[j]][2], 0, 0, r, g_c, b, 255};
        }
    }
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &box[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &box[0].r);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glPopMatrix();
}

#define OBSTACLES 20

typedef struct {
    float x, y, z;
} Obstacle;

int main(void) {
    gfxInitDefault();
    nova_init();
    novaSet3DDepth(0.08f); // Чуть усиливаем чувство скорости и глубины

    float player_x = 0.0f, player_y = 0.0f;
    float speed = 0.5f;
    int score = 0;
    bool game_over = false;

    Obstacle obs[OBSTACLES];
    for (int i = 0; i < OBSTACLES; i++) {
        obs[i].x = ((rand() % 200) - 100) / 10.0f;
        obs[i].y = ((rand() % 200) - 100) / 10.0f;
        obs[i].z = -20.0f - (rand() % 100);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kHeld = hidKeysHeld();
        if (hidKeysDown() & KEY_START) break;

        // --- ЛОГИКА ---
        if (!game_over) {
            circlePosition circle;
            hidCircleRead(&circle);
            player_x += (circle.dx / 160.0f) * 0.15f;
            player_y += (circle.dy / 160.0f) * 0.15f;

            // Ограничитель экрана
            if (player_x < -8.0f) player_x = -8.0f;
            if (player_x > 8.0f) player_x = 8.0f;
            if (player_y < -6.0f) player_y = -6.0f;
            if (player_y > 6.0f) player_y = 6.0f;

            speed += 0.0001f; // Игра постепенно ускоряется

            for (int i = 0; i < OBSTACLES; i++) {
                obs[i].z += speed;

                // Коллизия (игрок находится на Z = -2.0f)
                if (obs[i].z > -2.5f && obs[i].z < -1.5f) {
                    if (fabs(obs[i].x - player_x) < 1.0f && fabs(obs[i].y - player_y) < 1.0f) {
                        game_over = true;
                    }
                }

                // Респавн препятствия
                if (obs[i].z > 0.0f) {
                    obs[i].x = ((rand() % 160) - 80) / 10.0f;
                    obs[i].y = ((rand() % 120) - 60) / 10.0f;
                    obs[i].z = -60.0f;
                    score++;
                }
            }
        }

        // --- РЕНДЕР ---
        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);

            // Если проиграли - экран становится красным
            if (game_over) glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
            else glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 150.0f);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_TEXTURE_2D);

            // Рисуем игрока (Синенький кораблик)
            draw_box(player_x, player_y, -2.0f, 0.5f, 0.3f, 0.8f, 0, 150, 255);

            // Рисуем препятствия
            for (int o = 0; o < OBSTACLES; o++) {
                u8 c = (u8) (255 - (-obs[o].z * 3));
                if (obs[o].z < -80.0f) c = 0;
                draw_box(obs[o].x, obs[o].y, obs[o].z, 0.8f, 0.8f, 0.8f, c, 0, 0); // Красные блоки
            }

            float wall_z = fmodf(score * speed * -10.0f, 10.0f);
            for (int w = 0; w < 10; w++) {
                draw_box(-9.0f, 0.0f, wall_z - (w * 10.0f), 0.2f, 8.0f, 0.2f, 0, 255, 255); // Левая стена
                draw_box(9.0f, 0.0f, wall_z - (w * 10.0f), 0.2f, 8.0f, 0.2f, 0, 255, 255); // Правая стена
            }
        }
        novaSwapBuffers();
    }
    nova_fini();
    gfxExit();
    return 0;
}
