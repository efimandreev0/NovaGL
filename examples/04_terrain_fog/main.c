#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <NovaGL.h>

typedef struct {
    float x, y, z, u, v;
    unsigned int color;
} Vertex;

int main(void) {
    gfxInitDefault();
    nova_init();
    novaSet3DDepth(0.08f);

    unsigned char tex[32 * 32 * 4];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int idx = (y * 32 + x) * 4;
            int hole = ((x / 8) + (y / 8)) % 2 == 0;
            tex[idx] = 0;
            tex[idx + 1] = 255;
            tex[idx + 2] = 0;
            tex[idx + 3] = hole ? 0 : 255;
        }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);

    Vertex quad[6] = {
        {-1, -1, 0, 0, 0, 0xFFFFFFFF}, {1, -1, 0, 1, 0, 0xFFFFFFFF}, {1, 1, 0, 1, 1, 0xFFFFFFFF},
        {-1, -1, 0, 0, 0, 0xFFFFFFFF}, {1, 1, 0, 1, 1, 0xFFFFFFFF}, {-1, 1, 0, 0, 1, 0xFFFFFFFF}
    };

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        int eyes = novaGetEyeCount();
        for (int i = 0; i < eyes; i++) {
            novaBeginEye(i);

            glClearColor(0.8f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &quad[0].x);
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &quad[0].u);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &quad[0].color);

            glPushMatrix();
            glTranslatef(-1.5f, 0.0f, -4.0f);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_ALPHA_TEST);
            glPopMatrix();

            glPushMatrix();
            glTranslatef(1.5f, 0.0f, -4.0f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            for (int j = 0; j < 6; j++)
                quad[j].color = 0x80FF0000;
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_BLEND);
            for (int j = 0; j < 6; j++)
                quad[j].color = 0xFFFFFFFF;
            glPopMatrix();

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }
        novaSwapBuffers();
    }
    glDeleteTextures(1, &texture);
    nova_fini();
    gfxExit();
    return 0;
}
