# gl2citro3d

OpenGL ES 1.1 → Citro3D translation layer for Nintendo 3DS (PICA200 GPU).

Drop-in replacement for `<GLES/gl.h>` that maps fixed-function GL calls to Citro3D's API. Designed for porting GL ES 1.1 applications (like Minecraft PE) to the 3DS.

## Features

- 82 GL ES 1.1 functions implemented
- Matrix stacks (projection, modelview, texture)
- VBO support with linearAlloc-backed GPU memory
- Texture management with automatic Morton swizzle for PICA200
- TEV-based texture environment (GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND)
- Alpha blending, depth test, alpha test, scissor test, culling
- Fog support
- Display list recording/playback (for font rendering)
- Embedded PICA200 vertex shader (no romfs dependency)

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro3d, and CMake 3.13+ installed.

```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/DevkitArm3DS.cmake ..
make

# Build without examples
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/DevkitArm3DS.cmake -DGL2CITRO3D_BUILD_EXAMPLES=OFF ..
make
```

## Project Structure

```
gl2citro3d/
├── include/
│   └── gl2citro3d.h          # Public API header (replaces <GLES/gl.h>)
├── src/
│   ├── gl2citro3d.c           # Implementation (~1700 lines)
│   └── CMakeLists.txt         # Builds libgl2citro3d.a
├── shaders/
│   └── gl2citro3d_shader.pica # PICA200 vertex shader
├── cmake/
│   └── DevkitArm3DS.cmake     # Toolchain file for cross-compilation
├── examples/
│   ├── 01_triangle/           # Colored triangle (vertex colors, orthographic)
│   ├── 02_textured_quad/      # Textured quad (texture loading, UV repeat)
│   └── 03_rotating_cube/      # Rotating cube (perspective, depth test, transforms)
├── CMakeLists.txt             # Top-level build
└── README.md
```

## Usage

### Integration

1. Add as a CMake subdirectory or build separately
2. Include the header: `#include "gl2citro3d.h"`
3. Link with CMake: `target_link_libraries(your_app PRIVATE gl2citro3d)`

### Initialization

```c
#include <3ds.h>
#include <citro3d.h>
#include "gl2citro3d.h"

int main(void) {
    gfxInitDefault();
    gl2c3d_init();       // Initialize translator + Citro3D

    while (aptMainLoop()) {
        gl2c3d_frame_begin();
        gl2c3d_set_render_target(0);  // 0 = top screen

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ... your GL ES 1.1 drawing code ...

        gl2c3d_frame_end();
    }

    gl2c3d_fini();       // Cleanup
    gfxExit();
    return 0;
}
```

### Vertex Format

The translator uses a fixed vertex layout matching MCPE's `VertexDeclPTC`:

```c
typedef struct {
    float x, y, z;       // Position (12 bytes)
    float u, v;           // Texcoord (8 bytes)
    unsigned int color;   // ABGR packed color (4 bytes)
} Vertex;                 // Total: 24 bytes
```

### Key Differences from Desktop GL

- Vertex data **must** be in linear memory (`linearAlloc`) for GPU access
- Textures are automatically Morton-swizzled during `glTexImage2D`
- Use `gl2c3d_frame_begin()` / `gl2c3d_frame_end()` instead of swap buffers
- `gl2c3d_set_render_target(0)` for top screen, `(1)` for bottom screen
- Max texture size limited by PICA200 (typically 1024x1024)

## Examples

### 01 - Triangle
Colored triangle with vertex colors. Demonstrates basic initialization, orthographic projection, and vertex array drawing.

### 02 - Textured Quad
Procedurally generated checkerboard texture on a quad. Shows texture creation, filtering/wrapping parameters, and UV coordinates > 1.0 for texture repeat.

### 03 - Rotating Cube
Textured cube with per-face colors, perspective projection, depth testing, and real-time rotation via `glRotatef`.

## License

MIT
