#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <NovaGL.h>

typedef struct { float x,y,z; unsigned int color; } Vertex;
unsigned int make_color(u8 r, u8 g, u8 b, u8 a) { return (a << 24) | (b << 16) | (g << 8) | r; }

void draw_limb(float w, float h, float d, u8 r, u8 g_c, u8 b) {
    Vertex limb[6] = {
        {-w, 0, 0, make_color(r,g_c,b,255)}, { w, 0, 0, make_color(r,g_c,b,255)}, { w, h, 0, make_color(r,g_c,b,255)},
        {-w, 0, 0, make_color(r,g_c,b,255)}, { w, h, 0, make_color(r,g_c,b,255)}, {-w, h, 0, make_color(r,g_c,b,255)}
    };
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &limb[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &limb[0].color);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main(void) {
    gfxInitDefault(); nova_init();
    float time = 0.0f;

    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown() & KEY_START) break;
        time += 0.05f; // Время обновляем ОДИН раз за кадр!

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);

            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glTranslatef(0.0f, -1.0f, -5.0f);

            glDisable(GL_TEXTURE_2D);
            glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);

            glPushMatrix();
                glTranslatef(0.0f, sinf(time*2.0f)*0.2f, 0.0f);
                draw_limb(0.5f, 1.5f, 0.5f, 0, 0, 255);

                glPushMatrix();
                    glTranslatef(0.5f, 1.3f, 0.0f);
                    glRotatef(sinf(time)*45.0f, 0, 0, 1);
                    draw_limb(0.2f, -1.0f, 0.2f, 255, 200, 150);
                glPopMatrix();

                glPushMatrix();
                    glTranslatef(-0.5f, 1.3f, 0.0f);
                    glRotatef(-sinf(time)*45.0f, 0, 0, 1);
                    draw_limb(0.2f, -1.0f, 0.2f, 255, 200, 150);
                glPopMatrix();
            glPopMatrix();

            glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    nova_fini(); gfxExit(); return 0;
}