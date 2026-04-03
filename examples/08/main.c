#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <NovaGL.h>

typedef struct { float x,y,z, u,v; unsigned int color; } Vertex;

int main(void) {
    gfxInitDefault(); gl2c3d_init();

    /* Генерируем текстуру в клеточку с ПРОЗРАЧНЫМИ дырками */
    unsigned char tex[32*32*4];
    for(int y=0;y<32;y++) for(int x=0;x<32;x++) {
        int idx = (y*32+x)*4;
        int hole = ((x/8)+(y/8)) % 2 == 0;
        tex[idx]=0; tex[idx+1]=255; tex[idx+2]=0; /* Зеленый */
        tex[idx+3]= hole ? 0 : 255; /* Дырка полностью прозрачна */
    }
    GLuint texture; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);

    Vertex quad[6] = {{-1,-1,0, 0,0, 0xFFFFFFFF},{1,-1,0, 1,0, 0xFFFFFFFF},{1,1,0, 1,1, 0xFFFFFFFF},
                      {-1,-1,0, 0,0, 0xFFFFFFFF},{1,1,0, 1,1, 0xFFFFFFFF},{-1,1,0, 0,1, 0xFFFFFFFF}};

    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown() & KEY_START) break;

        gl2c3d_frame_begin(); gl2c3d_set_render_target(0);
        glClearColor(0.8f, 0.3f, 0.3f, 1.0f); /* Красный фон (чтобы видеть прозрачность) */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glFrustumf(-0.83f, 0.83f, -0.5f, 0.5f, 1.0f, 100.0f);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &quad[0].x);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &quad[0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &quad[0].color);

        /* 1. Блок Листвы (Alpha Test). Дырки не закрашивают Z-буфер! */
        glPushMatrix();
            glTranslatef(-1.5f, 0.0f, -4.0f);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f); /* Отбросить пиксели с альфой < 128 */
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_ALPHA_TEST);
        glPopMatrix();

        /* 2. Блок Воды (Alpha Blend). Полупрозрачность. */
        glPushMatrix();
            glTranslatef(1.5f, 0.0f, -4.0f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            /* Делаем квад синим и полупрозрачным через Вертексный цвет (TEV тест!) */
            for(int i=0;i<6;i++) quad[i].color = 0x80FF0000; /* A=128, B=255, G=0, R=0 (Формат ABGR) */
            
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_BLEND);
            
            /* Возвращаем белый для следующих кадров */
            for(int i=0;i<6;i++) quad[i].color = 0xFFFFFFFF; 
        glPopMatrix();

        glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        gl2c3d_frame_end();
    }
    glDeleteTextures(1, &texture); gl2c3d_fini(); gfxExit(); return 0;
}