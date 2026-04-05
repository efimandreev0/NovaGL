# NovaGL

OpenGL ES 1.1 → Citro3D translation layer for Nintendo 3DS (PICA200 GPU).

Drop-in replacement for `<GLES/gl.h>` that maps fixed-function GL calls to Citro3D's API. Designed for porting GL ES 1.1 applications (like Minecraft PE) to the 3DS.

## Features

- 85 GL ES 1.1 functions implemented
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
## Usage

### Integration

1. Add as a CMake subdirectory or build separately
2. Include the header: `#include <NovaGL.h>`
3. Link with CMake: `target_link_libraries(your_app PRIVATE NovaGL)`

### Initialization

```c
#include <3ds.h>
#include <citro3d.h>
#include <NovaGL.h>

int main(void) {
    gfxInitDefault();
    nova_init();       // Initialize translator + Citro3D

    while (aptMainLoop()) {
        
        // ... your GL ES 1.1 drawing code ...
        
        NovaSwapBuffers();
    }

    nova_fini();       // Cleanup
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
- `nova_set_render_target(0)` for top screen, `(1)` for bottom screen
- Max texture size limited by PICA200 (typically 1024x1024)

## License

MIT
