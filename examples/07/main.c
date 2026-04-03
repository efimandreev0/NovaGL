#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <NovaGL.h>

typedef struct { float x,y,z; unsigned int color; } Vertex;
unsigned int make_color(u8 r, u8 g, u8 b, u8 a) { return (a << 24) | (b << 16) | (g << 8) | r; }

/* Рисует простой вытянутый блок (как рука/нога Стива) */
void draw_limb(float w, float h, float d, u8 r, u8 g_c, u8 b) {
    Vertex limb[6] = { /* Используем квад для простоты */
        {-w, 0, 0, make_color(r,g_c,b,255)}, { w, 0, 0, make_color(r,g_c,b,255)}, { w, h, 0, make_color(r,g_c,b,255)},
        {-w, 0, 0, make_color(r,g_c,b,255)}, { w, h, 0, make_color(r,g_c,b,255)}, {-w, h, 0, make_color(r,g_c,b,255)}
    };
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &limb[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &limb[0].color);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main(void) {
    gfxInitDefault(); gl2c3d_init();
    float time = 0.0f;

    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown() & KEY_START) break;
        time += 0.05f;

        gl2c3d_frame_begin(); gl2c3d_set_render_target(0);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(0.0f, -1.0f, -5.0f);

        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        /* --- ТЕЛО (Иерархия матриц) --- */
        glPushMatrix();
            glTranslatef(0.0f, sinf(time*2.0f)*0.2f, 0.0f); /* Тело "дышит" (прыгает) */
            draw_limb(0.5f, 1.5f, 0.5f, 0, 0, 255); /* Синее тело */

            /* ПРАВАЯ РУКА (прикреплена к плечу) */
            glPushMatrix();
                glTranslatef(0.5f, 1.3f, 0.0f); /* Сдвиг к правому плечу */
                glRotatef(sinf(time)*45.0f, 0, 0, 1); /* Взмах рукой */
                draw_limb(0.2f, -1.0f, 0.2f, 255, 200, 150); /* Рука вниз */
            glPopMatrix();

            /* ЛЕВАЯ РУКА */
            glPushMatrix();
                glTranslatef(-0.5f, 1.3f, 0.0f); /* Сдвиг к левому плечу */
                glRotatef(-sinf(time)*45.0f, 0, 0, 1); /* Взмах в противофазе */
                draw_limb(0.2f, -1.0f, 0.2f, 255, 200, 150);
            glPopMatrix();

        glPopMatrix(); /* Возврат к миру */

        glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        gl2c3d_frame_end();
    }
    gl2c3d_fini(); gfxExit(); return 0;
}