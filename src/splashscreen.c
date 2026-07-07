//
// splashscreen.c — NovaGL animated boot splashscreen ("Powered by NovaGL").
//
// Self-contained: no external assets, no PNG decoder, no extra services. Draws
// an animated gradient backdrop plus a tiny embedded bitmap-font wordmark using
// nothing but NovaGL's own public GL ES 1.1 entry points, so if the core draw
// path is alive this screen renders. Runs synchronously at the tail of
// nova_init_ex(); compile out with -DNOVAGL_NO_SPLASHSCREEN=1 (vitaGL's
// -DNO_SPLASHSCREEN=1 is honoured as an alias by NovaGL.c).
//

#include "NovaGL.h"
#include "utils.h"
#include "context.h"

#include <3ds.h>
#include <string.h>
#include <math.h>

#if !(defined(NOVAGL_NO_SPLASHSCREEN) && NOVAGL_NO_SPLASHSCREEN) && !defined(NO_SPLASHSCREEN)

/* ---- timing (milliseconds) ------------------------------------------------ */
#ifndef NOVA_SPLASH_DURATION_MS
#define NOVA_SPLASH_DURATION_MS 2200u
#endif
#ifndef NOVA_SPLASH_FADE_MS
#define NOVA_SPLASH_FADE_MS 500u
#endif
#define NOVA_SPLASH_MAX_FRAMES 600 /* safety cap if the clock misbehaves */

/* ---- 8x8 bitmap font ------------------------------------------------------
 * Hand-authored, MSB = leftmost pixel. Only the glyphs the two strings need
 * are present; everything else falls back to blank. The art is laid out with
 * 0b literals so each glyph is legible right here in the source. */
typedef struct { char ch; unsigned char rows[8]; } Glyph;

static const Glyph kFont[] = {
    {' ', {0,0,0,0,0,0,0,0}},
    {'N', {0b11000110,0b11100110,0b11110110,0b11111110,0b11011110,0b11001110,0b11000110,0b00000000}},
    {'G', {0b01111100,0b11000110,0b11000000,0b11001110,0b11000110,0b11000110,0b01111100,0b00000000}},
    {'L', {0b11000000,0b11000000,0b11000000,0b11000000,0b11000000,0b11000000,0b11111110,0b00000000}},
    {'P', {0b11111100,0b11000110,0b11000110,0b11111100,0b11000000,0b11000000,0b11000000,0b00000000}},
    {'o', {0b00000000,0b00000000,0b01111000,0b11001100,0b11001100,0b11001100,0b01111000,0b00000000}},
    {'v', {0b00000000,0b00000000,0b11001100,0b11001100,0b11001100,0b01111000,0b00110000,0b00000000}},
    {'a', {0b00000000,0b00000000,0b01111000,0b00001100,0b01111100,0b11001100,0b01111110,0b00000000}},
    {'w', {0b00000000,0b00000000,0b11000110,0b11000110,0b11010110,0b11111110,0b01101100,0b00000000}},
    {'e', {0b00000000,0b00000000,0b01111000,0b11001100,0b11111100,0b11000000,0b01111100,0b00000000}},
    {'r', {0b00000000,0b00000000,0b11011000,0b11101100,0b11000000,0b11000000,0b11000000,0b00000000}},
    {'d', {0b00001100,0b00001100,0b01111100,0b11001100,0b11001100,0b11001100,0b01111110,0b00000000}},
    {'b', {0b11000000,0b11000000,0b11111000,0b11001100,0b11001100,0b11001100,0b11111000,0b00000000}},
    {'y', {0b00000000,0b00000000,0b11001100,0b11001100,0b11001100,0b01111100,0b00001100,0b01111000}},
};

#define GLYPH_ADVANCE 7  /* font columns consumed per glyph (1 col of gap) */

/* PTC vertex: pos 3f @0, uv 2f @12, color 4ub @20 — 24 bytes, the layout the
 * draw fast-paths recognise. */
typedef struct { float x, y, z; float u, v; unsigned int color; } SplashVtx;

static const unsigned char *glyph_rows(char c) {
    for (unsigned i = 0; i < sizeof(kFont) / sizeof(kFont[0]); i++) {
        if (kFont[i].ch == c) return kFont[i].rows;
    }
    return kFont[0].rows; /* blank */
}

static unsigned int pack_rgba(float r, float g_, float b, float a) {
    if (r < 0) r = 0; if (r > 1) r = 1;
    if (g_ < 0) g_ = 0; if (g_ > 1) g_ = 1;
    if (b < 0) b = 0; if (b > 1) b = 1;
    if (a < 0) a = 0; if (a > 1) a = 1;
    unsigned int R = (unsigned int)(r * 255.0f + 0.5f);
    unsigned int G = (unsigned int)(g_ * 255.0f + 0.5f);
    unsigned int B = (unsigned int)(b * 255.0f + 0.5f);
    unsigned int A = (unsigned int)(a * 255.0f + 0.5f);
    return (A << 24) | (B << 16) | (G << 8) | R; /* ABGR, R in low byte */
}

static SplashVtx *push_quad(SplashVtx *w, const SplashVtx *end,
                            float x, float y, float ww, float hh, unsigned int col) {
    if (w + 6 > end) return w; /* never overrun the scratch buffer */
    const float x1 = x + ww, y1 = y + hh;
    const SplashVtx a = {x, y, 0.f, 0.f, 0.f, col};
    const SplashVtx b = {x1, y, 0.f, 0.f, 0.f, col};
    const SplashVtx c = {x1, y1, 0.f, 0.f, 0.f, col};
    const SplashVtx d = {x, y1, 0.f, 0.f, 0.f, col};
    w[0] = a; w[1] = b; w[2] = c;
    w[3] = a; w[4] = c; w[5] = d;
    return w + 6;
}

/* Pixel width of a string at font-pixel size `s` (no trailing gap). */
static float text_width(const char *str, float s) {
    int n = (int)strlen(str);
    if (n <= 0) return 0.f;
    return (float)(n * GLYPH_ADVANCE - 1) * s;
}

static SplashVtx *push_text(SplashVtx *w, const SplashVtx *end,
                            const char *str, float ox, float oy, float s, unsigned int col) {
    float pen = ox;
    for (const char *p = str; *p; ++p) {
        const unsigned char *rows = glyph_rows(*p);
        for (int ry = 0; ry < 8; ry++) {
            unsigned char bits = rows[ry];
            if (!bits) continue;
            for (int cx = 0; cx < 8; cx++) {
                if (bits & (0x80 >> cx)) {
                    w = push_quad(w, end, pen + (float)cx * s, oy + (float)ry * s, s, s, col);
                }
            }
        }
        pen += (float)GLYPH_ADVANCE * s;
    }
    return w;
}

void nova_splash_run(void) {
    /* Scratch geometry buffer. ~3k verts max for both strings + chrome; round
     * up generously. Must be linear memory so the GPU can read it directly. */
    const int kMaxVerts = 6144;
    SplashVtx *buf = (SplashVtx *)linearAlloc(sizeof(SplashVtx) * (size_t)kMaxVerts);
    if (!buf) return; /* no memory for the splash — skip it, never block boot */
    const SplashVtx *end = buf + kMaxVerts;

    const float W = (float)NOVA_SCREEN_W; /* 400 logical */
    const float H = (float)NOVA_SCREEN_H; /* 240 logical */

    const float title_s = 5.0f; /* "NovaGL"     font-pixel size */
    const float sub_s   = 2.0f; /* "Powered by" font-pixel size */
    const char *title = "NovaGL";
    const char *sub   = "Powered by";

    const float title_w = text_width(title, title_s);
    const float sub_w   = text_width(sub, sub_s);
    const float title_x = (W - title_w) * 0.5f;
    const float sub_x   = (W - sub_w) * 0.5f;
    const float title_y = H * 0.5f - 8.0f * title_s * 0.5f; /* vertically centred glyph cell */
    const float sub_y   = title_y - 8.0f * sub_s - 10.0f;

    const u64 t0 = osGetTime();

    for (int frame = 0; frame < NOVA_SPLASH_MAX_FRAMES; frame++) {
        if (!aptMainLoop()) break; /* user closed the app mid-splash */

        const u64 now = osGetTime();
        const unsigned elapsed = (now >= t0) ? (unsigned)(now - t0) : 0u;
        if (elapsed >= NOVA_SPLASH_DURATION_MS) break;

        /* fade: in over FADE, hold, out over FADE */
        float fade;
        if (elapsed < NOVA_SPLASH_FADE_MS) {
            fade = (float)elapsed / (float)NOVA_SPLASH_FADE_MS;
        } else if (elapsed > NOVA_SPLASH_DURATION_MS - NOVA_SPLASH_FADE_MS) {
            fade = (float)(NOVA_SPLASH_DURATION_MS - elapsed) / (float)NOVA_SPLASH_FADE_MS;
        } else {
            fade = 1.0f;
        }
        if (fade < 0.f) fade = 0.f; if (fade > 1.f) fade = 1.f;

        /* gentle breathing on the accent so it feels alive */
        const float phase = (float)elapsed * 0.0026f;
        const float pulse = 0.5f + 0.5f * nova_fast_sinf(phase);

        /* ---- build this frame's geometry ---- */
        SplashVtx *w = buf;

        /* Background vertical gradient (faded in/out from black). */
        const float topR = 0.043f * fade, topG = 0.063f * fade, topB = 0.125f * fade;   /* #0b1020 */
        const float botR = 0.102f * fade, botG = 0.043f * fade, botB = 0.180f * fade;   /* #1a0b2e */
        const unsigned int cTop = pack_rgba(topR, topG, topB, 1.0f);
        const unsigned int cBot = pack_rgba(botR, botG, botB, 1.0f);
        if (w + 6 <= end) {
            const SplashVtx tl = {0.f, 0.f, 0.f, 0.f, 0.f, cTop};
            const SplashVtx tr = {W,   0.f, 0.f, 0.f, 0.f, cTop};
            const SplashVtx br = {W,   H,   0.f, 0.f, 0.f, cBot};
            const SplashVtx bl = {0.f, H,   0.f, 0.f, 0.f, cBot};
            w[0] = tl; w[1] = tr; w[2] = br;
            w[3] = tl; w[4] = br; w[5] = bl;
            w += 6;
        }

        /* Subtitle "Powered by" — soft grey-blue. */
        const unsigned int subCol = pack_rgba(0.62f, 0.70f, 0.85f, fade);
        w = push_text(w, end, sub, sub_x, sub_y, sub_s, subCol);

        /* Title "NovaGL" — bright white. */
        const unsigned int titleCol = pack_rgba(0.96f, 0.97f, 1.00f, fade);
        w = push_text(w, end, title, title_x, title_y, title_s, titleCol);

        /* Accent underline beneath the wordmark, width breathes with `pulse`. */
        const float bar_full = title_w * 1.10f;
        const float bar_w = bar_full * (0.55f + 0.45f * pulse);
        const float bar_x = (W - bar_w) * 0.5f;
        const float bar_y = title_y + 8.0f * title_s + 4.0f;
        const unsigned int barCol = pack_rgba(0.20f + 0.40f * pulse, 0.85f, 1.00f, fade);
        w = push_quad(w, end, bar_x, bar_y, bar_w, 3.0f, barCol);

        const int nverts = (int)(w - buf);
        GSPGPU_FlushDataCache(buf, (u32)(nverts * (int)sizeof(SplashVtx)));

        /* ---- draw top screen ---- */
        nova_set_render_target(0);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0.f, W, H, 0.f, -1.f, 1.f); /* (0,0) top-left, logical 400x240 */
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBindBuffer(GL_ARRAY_BUFFER, 0); /* client arrays out of linear scratch */
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(SplashVtx), (void *)&buf->x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(SplashVtx), (void *)&buf->color);
        glDrawArrays(GL_TRIANGLES, 0, nverts);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        /* ---- bottom screen: black ---- */
        nova_set_render_target(2);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        novaSwapBuffers();
    }

    linearFree(buf);

    /* Restore the GL state the app expects right after nova_init: identity
     * matrices, depth test on, blend off, no client arrays, white current
     * colour. The cache is invalidated so the first real draw re-pushes
     * everything cleanly. */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    nova_invalidate_state_cache();
}

#else /* splash compiled out */

void nova_splash_run(void) { /* no-op */ }

#endif
