/*
 * gl2citro3d Example 01: Colored Triangle
 *
 * Demonstrates:
 *   - gl2c3d_init / gl2c3d_fini
 *   - glClear, glClearColor
 *   - glMatrixMode, glLoadIdentity, glOrthof
 *   - Vertex arrays with position + color (no VBO)
 *   - glDrawArrays(GL_TRIANGLES)
 *   - glColor4f, glEnable/glDisable(GL_TEXTURE_2D)
 */

#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <NovaGL.h>

/* Vertex with position (3f) + texcoord (2f) + color (4ub) = 24 bytes
 * This matches the VertexDeclPTC layout the translator expects. */
typedef struct {
    float x, y, z;
    float u, v;
    unsigned int color;
} Vertex;

static unsigned int make_color(unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

int main(void) {
    gfxInitDefault();
    gl2c3d_init();

    /* Triangle vertices: position + dummy texcoord + ABGR color */
    Vertex tri[3] = {
        { 0.0f,    0.8f,  0.0f,  0.0f, 0.0f, make_color(255, 0, 0, 255)   },  /* top - red */
        {-0.8f,   -0.8f,  0.0f,  0.0f, 0.0f, make_color(0, 255, 0, 255)   },  /* bottom-left - green */
        { 0.8f,   -0.8f,  0.0f,  0.0f, 0.0f, make_color(0, 0, 255, 255)   },  /* bottom-right - blue */
    };

    /* Upload to linear memory (required by PICA200 GPU) */
    Vertex *vbo_data = (Vertex*)linearAlloc(sizeof(tri));
    memcpy(vbo_data, tri, sizeof(tri));

    /* Generate and fill a VBO */
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), vbo_data, GL_STATIC_DRAW);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        gl2c3d_frame_begin();
        gl2c3d_set_render_target(0);

        /* Clear to dark blue */
        glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Set up orthographic projection: simple [-1, 1] range */
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Disable texturing - we only want vertex colors */
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);

        /* Set up vertex arrays from VBO */
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), (void*)0);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void*)(5 * 4));

        glDrawArrays(GL_TRIANGLES, 0, 3);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        gl2c3d_frame_end();
    }

    glDeleteBuffers(1, &vbo);
    linearFree(vbo_data);
    gl2c3d_fini();
    gfxExit();
    return 0;
}
