# NovaGL
<p align="center"><img width="50%" src="./NovaGL.png"></p>

OpenGL ES 1.1 → Citro3D translation layer for Nintendo 3DS (PICA200 GPU).

Drop-in replacement for `<GLES/gl.h>` that maps fixed-function GL calls to Citro3D. Built for porting GL ES 1.1 applications (like Minecraft PE, PD port, various homebrew engines) to the 3DS without rewriting the renderer from scratch.

**Full API reference with per-function implementation status → [api.md](api.md)**

## What actually works

- Matrix stacks (modelview, projection, texture) — 32 deep each
- VBO with linearAlloc-backed GPU memory, with map/unmap
- Textures: auto-PoT downscale, Morton swizzle, up to 2048 slots, persistent SD card cache
- 3 texture units, full GL_COMBINE / TEV compiler including PICA-specific extensions
- Alpha blend (all GL ES 1.1 factors + separate RGB/alpha equations), depth test, alpha test, scissor, stencil (real PICA path)
- Culling, polygon offset (approximate), fog (LINEAR/EXP/EXP2)
- Immediate mode emulation via internal ring buffer (`glBegin`/`glEnd`)
- Display list recording/playback (limited op set, good enough for font rendering)
- Hardware lighting via C3D_LightEnv (works on real HW, Citra's lighting emulation is patchy)
- Render-to-texture FBOs backed by VRAM (the stock `glFramebufferTexture2D` path, plus `novaCreateRenderTextureFBO` for the correct VRAM-backed path)
- Stereoscopic 3D (`novaBeginEye` / `novaSet3DDepth`)
- Clip-space 2D fast lane for UI/HUD (`novaBeginClipSpace2D` + `novaDrawClipspaceTris`)
- VAO (GL 3.0-style, stores client array state per object)
- `glReadPixels` (slow CPU path; optional DisplayTransfer HW path via compile flag)
- GL 2.0 shader pipeline stubs so ports that link against GL 2.0 headers compile

## What doesn't work

- GLSL shaders (PICA is fixed-function, period)
- `glClipPlane` (no user clip planes on PICA)
- `glPolygonMode` (no wireframe — enum-validated no-op)
- `glLineWidth` (always 1px)
- **`GL_LINES` / `GL_LINE_STRIP` / `GL_LINE_LOOP` / `GL_POINTS`** — PICA has no line/point primitive, so these currently rasterize as filled triangles. Accepted (and enum-validated) but visually wrong; treat like the other fixed-function gaps.
- Display lists record only `glTranslatef` / `glColor3f` / `glColor4f` (enough for bitmap-font rendering); other commands between `glNewList(GL_COMPILE)`/`glEndList` execute immediately rather than being deferred.
- `glBlitFramebuffer` ignores the src/dst rects, mask and filter (full-surface colour copy; use `novaBlitTargetToFBO`)
- Manual mip levels (`glTexImage2D` with `level > 0`) are accepted-and-ignored; use `GL_GENERATE_MIPMAP` / `glGenerateMipmap` for auto-mips (RGBA8/RGB8)
- TexGen
- EGL (stubs only — not needed on 3DS)

### Standards compliance

The default build aims to behave exactly like desktop/ES OpenGL — including error reporting (`glGetError` keeps the **first** error per spec, and the entry points raise `GL_INVALID_ENUM`/`GL_INVALID_VALUE`/`GL_INVALID_OPERATION` for bad arguments), default state, and `glBindFramebuffer` read/draw separation. The non-standard speed/VRAM optimizations are now opt-in flags (see *Compliance opt-outs* below). **One behaviour change to note:** the depth test now defaults to **disabled** with `glDepthFunc(GL_LESS)` (the GL spec default) instead of the old enabled/`GL_LEQUAL`. A correct port always sets its own depth state; if yours relied on the old default, add `glEnable(GL_DEPTH_TEST)`.

## GLFW + GLAD drop-in

A desktop game that uses **GLFW** for window/context/input and **GLAD** for GL loading can build and run on 3DS by pointing its include path at NovaGL's `include/` — `<GLFW/glfw3.h>`, `<glad/glad.h>` (and `<GL/gl.h>` / `<GLES/gl.h>`) resolve to the shims. The usual lifecycle works unchanged:

```c
#include <glad/glad.h>
#include <GLFW/glfw3.h>

int main(void) {
    glfwInit();                                   // -> gfxInitDefault()
    GLFWwindow *win = glfwCreateWindow(1280, 720, "Game", NULL, NULL); // -> nova_init()
    glfwMakeContextCurrent(win);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);   // succeeds (symbols are linked)

    while (!glfwWindowShouldClose(win)) {         // drives aptMainLoop()
        glfwPollEvents();                         // hidScanInput() + callback dispatch
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        /* ... your GL drawing ... */
        glfwSwapBuffers(win);                      // -> novaSwapBuffers()
    }
    glfwTerminate();                               // -> nova_fini() + gfxExit()
    return 0;
}
```

3DS caveats (documented, not bugs): single window/context; `glfwGetFramebufferSize` reports the top screen (400×240) regardless of the size you request (build your projection from it); `glfwGetKey` maps a useful subset of keys to 3DS buttons (Esc→START, arrows/WASD→D-pad, Enter/Space→A, …); the "mouse" is the bottom touchscreen (via `glfwGetCursorPos`/`glfwGetMouseButton`); `glfwGetTime` is `svcGetSystemTick`-based. Remove the generated `glad.c` from your build — NovaGL links the GL symbols directly. Joystick/gamepad (`glfwGetGamepadState`) maps to the pad + C-stick + ZL/ZR.

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro3d, and CMake 3.13+.

```bash
mkdir build && cd build
cmake ..
make

# Without examples
cmake -DGL2CITRO3D_BUILD_EXAMPLES=OFF ..
make
```

Useful compile flags:

| Flag | Effect |
|---|---|
| `-DNOVAGL_SMALL_INIT_DEFAULTS=1` | 512KB/128KB/256KB buffers instead of 2MB/512KB/512KB. For UI-only apps. |
| `-DNOVAGL_FRAME_MODE=0` | Async frame submission. ~20–40% faster on GPU-bound scenes, slightly riskier. |
| `-DNOVAGL_DISABLE_STENCIL=1` | Turn stencil back into a no-op if you hit citro3d asserts. |
| `-DNOVAGL_GL2_RETURN_DUMMY=1` | glCreateShader/glCreateProgram return fake non-zero ids. For ports that don't check and call glUseProgram regardless. |
| `-DNOVAGL_ENABLE_GLREADPIXELS_HW=1` | Use DisplayTransfer for full-screen glReadPixels. Faster but output is transposed relative to GL convention. |

### Compliance opt-outs (default build is spec-correct)

These restore older, non-standard-but-convenient behaviours. The default build now matches OpenGL; turn one on only if a port depended on the lenient behaviour.

| Flag | Effect |
|---|---|
| `-DNOVAGL_SOLID_TEX_OPT=1` | Collapse single-colour textures to an 8×8 VRAM stub (big VRAM saving; bit-exact for plain sampling, but `glTexSubImage2D`/`glCopyTexSubImage2D`/readback then see the stub). Off → real-size upload. |
| `-DNOVAGL_DOWNSCALE_OVERSIZE=1` | Auto-downscale textures larger than 1024 instead of raising `GL_INVALID_VALUE` (handy for oversized desktop atlases). |
| `-DNOVAGL_LENIENT_BUFFERSUBDATA=1` | Auto-grow the buffer on an out-of-range `glBufferSubData` instead of `GL_INVALID_VALUE`. |
| `-DNOVAGL_FBO_RESET_SCISSOR=1` | Reset the scissor box to the full surface when binding an FBO (fast3d menu-blur workaround). Spec leaves the scissor untouched on a framebuffer bind. |

### Performance hacks

All **off by default** — each trades some OpenGL compliance/safety for raw throughput on the 268 MHz ARM11. Turn them on only once a port is known-good. Either `-D...=1` directly or flip the matching `cmake -DNOVAGL_...=ON` option.

| Flag | Effect | Risk |
|---|---|---|
| `-DNOVAGL_SPEEDHACKS=1` | **Master switch.** Enables `NO_DEBUG` + `DRAW_SPEEDHACK` + `ASYNC_FRAME` in one go. The "just make it fast" bundle. | The union of the three below. |
| `-DNOVAGL_NO_DEBUG=1` | Strips per-draw GL spec validation (enum/value/type checks) and the per-vertex pointer sanity guards in the interleave loop. Biggest CPU win on draw-call-bound scenes. | Bad enums / negative counts / wild client pointers stop being caught and will crash or corrupt. |
| `-DNOVAGL_DRAW_SPEEDHACK=1` | Tightens the vertex ring buffer to 64-byte alignment (less padding → fewer ring wraps and GPU stalls) and trusts caller-supplied array pointers. Implies `NOVAGL_RING_ALIGN_64`. | Slightly looser GPU fetch alignment; assumes valid pointers. |
| `-DNOVAGL_ASYNC_FRAME=1` | Friendly alias for `-DNOVAGL_FRAME_MODE=0` — non-blocking frame submission, overlaps CPU frame N+1 with GPU frame N. ~20–40% on GPU-bound scenes. | None we know of (state cache is reset on frame boundaries), but harder to reason about timing. |
| `-DNOVAGL_RING_ALIGN_64=1` | Just the 64-byte ring alignment, without the rest of `DRAW_SPEEDHACK`. | Low. |

```bash
# fastest preset
cmake -DNOVAGL_SPEEDHACKS=ON ..
make
```

### Boot splashscreen

NovaGL shows a short animated **"Powered by NovaGL"** splash (gradient backdrop, embedded bitmap-font wordmark, breathing accent bar) the first time you call `nova_init()` / `nova_init_ex()`. It's self-contained (no assets, no PNG decoder), runs synchronously for ~2.2s, and hides early loading time. Disable it with:

```bash
cmake -DNOVAGL_SPLASHSCREEN=OFF ..        # CMake option
# or
-DNOVAGL_NO_SPLASHSCREEN=1                 # raw define (vitaGL's -DNO_SPLASHSCREEN=1 also works)
```

Tune timing with `-DNOVA_SPLASH_DURATION_MS=...` / `-DNOVA_SPLASH_FADE_MS=...`.

## Usage

### Integration

1. Add as a CMake subdirectory or build separately
2. `#include <NovaGL.h>` — intentionally does **not** include `<3ds.h>` so it won't collide with a `Thread` typedef in your engine
3. `target_link_libraries(your_app PRIVATE NovaGL)`

### Basic init

```c
#include <3ds.h>
#include <citro3d.h>
#include <NovaGL.h>

int main(void) {
    gfxInitDefault();
    nova_init();

    while (aptMainLoop()) {
        // your GL ES 1.1 drawing code
        novaSwapBuffers();
    }

    nova_fini();
    gfxExit();
    return 0;
}
```

### Stereoscopic 3D

```c
void RenderFrame(void) {
    int eyes = novaGetEyeCount();

    for (int i = 0; i < eyes; i++) {
        novaBeginEye(i);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        DrawWorld();

        novaSet3DDepth(0.0f);
        DrawUI();

        novaSet3DDepth(0.05f);
    }

    novaSwapBuffers();
}
```

### Vertex format

The default draw path expects this layout (matches MCPE's `VertexDeclPTC`):

```c
typedef struct {
    float x, y, z;       // Position  (12 bytes)
    float u, v;           // Texcoord  (8 bytes)
    unsigned int color;   // ABGR packed (4 bytes)
} Vertex;                 // 24 bytes total
```

Vertex data must live in linear memory (`linearAlloc` or a VBO backed by it) — the GPU reads it directly.

### Texture cache (optional)

Skips PNG decode + Morton swizzle on subsequent boots. ~300ms → ~30ms per texture on a 268MHz ARM11.

```c
nova_texture_cache_set_directory("sdmc:/Nova/cache/MyGame");

// ...

uint32_t hash = my_hash(blob, blobSize);
glBindTexture(GL_TEXTURE_2D, texId);
int origW, origH;
if (!nova_texture_cache_load(hash, &origW, &origH)) {
    uint8_t *pixels = stbi_load_from_memory(blob, blobSize, ...);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    nova_texture_cache_save(hash);
}
```

### Key differences from desktop GL

- Vertex data **must** be in linear memory (`linearAlloc`) for GPU access
- Textures are automatically Morton-swizzled and PoT-rounded during `glTexImage2D`
- `nova_set_render_target(0)` for top screen, `(1)` for right eye, `(2)` for bottom screen
- Max texture size: 1024×1024 (PICA200 limit)
- Depth buffer is D24S8 — clearing depth also clears stencil and vice versa (HW limitation)
- No GLSL, no compute, no geometry shaders, no transform feedback — it's a 2011 handheld

## License

MIT
