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

NovaGL now reports errors properly (`glGetError` keeps the **first** error per spec, and the entry points raise `GL_INVALID_ENUM`/`GL_INVALID_VALUE`/`GL_INVALID_OPERATION` for bad arguments) and tightens a lot of state tracking and `glBindFramebuffer` read/draw separation. Where matching the spec *exactly* would change rendering, the **default keeps the historical port-friendly behaviour** and the strict behaviour is a compile flag (see *Strict-spec opt-ins* above) — so upgrading NovaGL shouldn't silently change how an existing port renders. Build with the strict flags for a more conformant layer once your port is known-good.

### Near-plane clipping (ON by default)

PICA's clip-space z range is `[-w, 0]`, not desktop GL's `[-w, w]`, and the vertex shaders only **clamp** z into it (a depth-range fix, not a clip). So a triangle that **straddles the near plane** — a vertex with `z < -w`, i.e. right under/through the camera — isn't actually clipped the way a desktop/PowerVR GPU clips it: its near vertex projects to a garbage screen position and the triangle drops out or distorts. The symptom is **"see-through walls/floor" when you stand close to or oblique to a surface** (seen in Wolfenstein-RPG walls and Perfect Dark's floor; absent on PS Vita because PowerVR near-clips in hardware).

NovaGL fixes this by CPU near-clipping the offending triangles (Sutherland–Hodgman against `z + w ≥ 0`) on both draw routes:

| Route | What it does |
|---|---|
| `novaDrawClipspaceTris` (fast3d / PD) | clips in clip space (verts already carry `xyzw`) |
| GPU-MVP path — `basic`/`texmtx`/`full` shaders (e.g. Wolfenstein-RPG via client arrays) | replays the shader's MVP **only** to find each vertex's near distance, then clips in **model space** and redraws through the *same* shader, so fog / texture-matrix / depth stay intact |

Both only run when a vertex actually crosses the near plane (a cheap one-dot-per-vertex test gates it), so geometry fully in front of the camera takes the normal path untouched. Disable with `-DNOVAGL_NEAR_CLIP=OFF` (GPU-MVP path) / `-DNOVAGL_CLIPSPACE_NEAR_CLIP=OFF` (clipspace lane), or the raw `-DNOVAGL_NO_NEAR_CLIP=1` / `-DNOVAGL_NO_CLIPSPACE_NEAR_CLIP=1`. Note: the VBO-based fast draw lanes (a port feeding an interleaved `[pos3|uv2|col4]` VBO that takes `C3D_DrawElements` directly) are **not** near-clipped — only client-array / de-indexed draws are.

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

### Strict-spec opt-ins (defaults favour port compatibility)

NovaGL adds proper `glGetError` reporting and a lot of spec-correct error handling, but where matching the spec *exactly* would change rendering and risk breaking an existing port, the **default keeps the historical, port-friendly behaviour** and the strict behaviour is opt-in. Turn these on for a cleaner, more conformant build once your port is known-good.

| Flag | Effect (default → strict) |
|---|---|
| `-DNOVAGL_STRICT_DEPTH_DEFAULT=1` | Initial depth state. Default: test **enabled**, `GL_LEQUAL` (port-friendly — some ports draw 3D without enabling depth). Strict: disabled, `GL_LESS` (GL spec). |
| `-DNOVAGL_CLEAR_RESPECTS_MASK=1` | `glClear`. Default: always clears the requested buffers. Strict: clear respects `glColorMask`/`glDepthMask`/stencil writemask (a stale `glDepthMask(FALSE)` then makes `glClear(DEPTH)` a no-op — which is spec, but breaks fast3d-style frame-start depth clears). |
| `-DNOVAGL_STRICT_TEX_FORMAT=1` | `glTexImage2D`/`glTexSubImage2D`. Default: unrecognized `(format,type)` falls back to RGBA8 (historical). Strict: reject with `GL_INVALID_ENUM`/`GL_INVALID_OPERATION` (catches the over-read on a wrong combo like `GL_RGBA + GL_UNSIGNED_SHORT_5_6_5`). |
| `-DNOVAGL_STRICT_MAX_TEXTURE_SIZE=1` | Textures > 1024. Default: auto-downscale (graceful on a constrained device). Strict: `GL_INVALID_VALUE`, no upload. |
| `-DNOVAGL_STRICT_BUFFERSUBDATA=1` | Out-of-range `glBufferSubData`. Default: auto-grow. Strict: `GL_INVALID_VALUE`. |
| `-DNOVAGL_GL_LIGHT_EYE_SPACE=1` | `glLight(GL_POSITION/GL_SPOT_DIRECTION)`. Default: stored raw (what the HW light env was tuned against). Strict: transformed by the current modelview into eye space (GL spec). |
| `-DNOVAGL_FBO_DEPTH24_STENCIL8=1` | FBO depth attachment. Default: `DEPTH16` (lower VRAM). Strict: `DEPTH24_STENCIL8` (matches the screen + enables stencil in FBOs, but doubles FBO depth VRAM). |
| `-DNOVAGL_NO_FBO_RESET_SCISSOR=1` | Inverse opt-out: binding an FBO resets the scissor to the full surface **by default** (fast3d menu-blur needs it); set this to leave the scissor untouched per spec. |
| `-DNOVAGL_NO_SOLID_TEX_OPT=1` | Inverse opt-out: single-colour textures collapse to an 8×8 VRAM stub **by default** (big VRAM saving, bit-exact for sampling); set this to upload them at real size. |

### Performance hacks

All **off by default** — each trades some OpenGL compliance/safety for raw throughput on the 268 MHz ARM11. Turn them on only once a port is known-good. Either `-D...=1` directly or flip the matching `cmake -DNOVAGL_...=ON` option.

| Flag | Effect | Risk |
|---|---|---|
| `-DNOVAGL_SPEEDHACKS=1` | **Master switch.** Enables the *safe* bundle: `NO_DEBUG` + `DRAW_SPEEDHACK`. Does **not** enable `ASYNC_FRAME`. | The union of the two safe ones below. |
| `-DNOVAGL_NO_DEBUG=1` | Strips per-draw GL spec validation (enum/value/type checks) and the per-vertex pointer sanity guards in the interleave loop. Biggest CPU win on draw-call-bound scenes. | Bad enums / negative counts / wild client pointers stop being caught and will crash or corrupt. |
| `-DNOVAGL_DRAW_SPEEDHACK=1` | Tightens the vertex ring buffer to 64-byte alignment (less padding → fewer ring wraps and GPU stalls) and trusts caller-supplied array pointers. Implies `NOVAGL_RING_ALIGN_64`. | Slightly looser GPU fetch alignment; assumes valid pointers. |
| `-DNOVAGL_ASYNC_FRAME=1` | Non-blocking frame submission — overlaps CPU frame N+1 with GPU frame N (~20–40% on GPU-bound scenes). Now an alias for **double-buffering** (`NOVAGL_FRAME_BUFFERS=2`), which is **safe** (see *Frame buffering* below). | None — the rings and orphan GC are multi-buffered so the GPU never reads freed/overwritten memory. |
| `-DNOVAGL_RING_ALIGN_64=1` | Just the 64-byte ring alignment, without the rest of `DRAW_SPEEDHACK`. | Low. |

### Frame buffering (single / double / triple)

How many frames the CPU may run ahead of the GPU. Pick at **runtime** with `novaSetFrameBuffers(n)` *before* `nova_init()`, or set the compile default `-DNOVAGL_FRAME_BUFFERS=n`:

| n | Mode | Behaviour |
|---|---|---|
| 1 | single (default) | `C3D_FRAME_SYNCDRAW` — the CPU waits for the GPU each frame. Lowest latency + memory; no overlap. |
| 2 | double | CPU builds frame N+1 while the GPU renders frame N. ~20–40% FPS on GPU-bound scenes. |
| 3 | triple | CPU may run up to two frames ahead — smoother under uneven frame times; highest latency + memory. |

```c
novaSetFrameBuffers(2);   // double-buffer
gfxInitDefault();
nova_init();
```

With 2/3, NovaGL keeps that many copies of the vertex/index ring buffers and defers texture/render-target deletion by the same number of frames, so a buffer is only reused/freed once the GPU has finished the frame that used it — async is **safe** (no black/swapping textures or corruption). Cost: `n ×` the ring memory (per-slot = the `nova_init_ex` sizes; ~2.5 MB each by default, so triple ≈ 7.5 MB of linear RAM). If a slot allocation fails, NovaGL transparently falls back to fewer buffers; `novaGetFrameBuffers()` reports the count actually in use.

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
