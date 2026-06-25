# NovaGL static library

cmake_minimum_required(VERSION 3.9)

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader.pica")
set(SHADER_BASIC_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader_basic.pica")
set(SHADER_TEXMTX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader_texmtx.pica")
set(SHADER_CLIPSPACE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader_clipspace.pica")
set(SHADER_LIGHTING_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader_lighting.pica")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Compile .pica -> .shbin -> embedded .s + .h. Shader variants:
#   NovaGL_shader            — full FFP (fog + texmtx + separate proj/mv)
#   NovaGL_shader_basic      — no fog, MVP combined, identity texmtx
#   NovaGL_shader_texmtx     — no fog, MVP combined, texmtx applied
#   NovaGL_shader_clipspace  — raw clip-space passthrough (for UI/HUD)
#   NovaGL_shader_lighting   — eye-space view + normalquat for HW frag lighting
ctr_add_shader_library(NovaGL_shader "${SHADER_SRC}")
ctr_add_shader_library(NovaGL_shader_basic "${SHADER_BASIC_SRC}")
ctr_add_shader_library(NovaGL_shader_texmtx "${SHADER_TEXMTX_SRC}")
ctr_add_shader_library(NovaGL_shader_clipspace "${SHADER_CLIPSPACE_SRC}")
ctr_add_shader_library(NovaGL_shader_lighting "${SHADER_LIGHTING_SRC}")
dkp_add_embedded_binary_library(shaders
    NovaGL_shader NovaGL_shader_basic NovaGL_shader_texmtx NovaGL_shader_clipspace
    NovaGL_shader_lighting)

# Library sources
add_library(NovaGL STATIC
        matrix.c
        state.c
        raster.c
        clear.c
        texture.c
        vbo.c
        vertex_array.c
        current.c
        draw.c
        fog.c
        framebuffer.c
        display_list.c
        utils.c
        NovaGL.c
        immediate.c
        compat.c
        lookup.c
        egl.c
        splashscreen.c
        nova_stb_ds_impl.c
        nova_glfw.c
)

set_property(TARGET NovaGL PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

target_compile_options(NovaGL PRIVATE
        -O3
        -ffast-math
        -fno-math-errno
)

option(NOVA_VBO_STREAM "Use VBO stream" ON)
if (NOVA_VBO_STREAM)
    target_compile_definitions(NovaGL PRIVATE NOVA_VBO_STREAM)
endif ()

# ---- Performance hacks (off by default; trade compliance/safety for speed) ----
# Master switch: enables NO_DEBUG + DRAW_SPEEDHACK + ASYNC_FRAME together.
option(NOVAGL_SPEEDHACKS "Enable the full NovaGL speedhack bundle" ON)
if (NOVAGL_SPEEDHACKS)
    target_compile_definitions(NovaGL PRIVATE NOVAGL_SPEEDHACKS=1)
endif ()

# Individual knobs (also implied by NOVAGL_SPEEDHACKS via context.h).
option(NOVAGL_NO_DEBUG "Strip per-draw GL validation + pointer guards" OFF)
if (NOVAGL_NO_DEBUG)
    target_compile_definitions(NovaGL PRIVATE NOVAGL_NO_DEBUG=1)
endif ()

option(NOVAGL_DRAW_SPEEDHACK "Tighter vertex-ring alignment + trust caller pointers" OFF)
if (NOVAGL_DRAW_SPEEDHACK)
    target_compile_definitions(NovaGL PRIVATE NOVAGL_DRAW_SPEEDHACK=1)
endif ()

option(NOVAGL_ASYNC_FRAME "Non-blocking frame submission (NOVAGL_FRAME_MODE=0)" OFF)
if (NOVAGL_ASYNC_FRAME)
    target_compile_definitions(NovaGL PRIVATE NOVAGL_ASYNC_FRAME=1)
endif ()

# Boot splashscreen is ON by default; flip this OFF to compile it out.
option(NOVAGL_SPLASHSCREEN "Show the animated NovaGL boot splashscreen" ON)
if (NOT NOVAGL_SPLASHSCREEN)
    target_compile_definitions(NovaGL PRIVATE NOVAGL_NO_SPLASHSCREEN=1)
endif ()

target_include_directories(NovaGL
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        PRIVATE
        "${GENERATED_DIR}"
        "../.dkp-generated/shaders/"
)

target_link_libraries(NovaGL
        PUBLIC
        citro3d
        ctru
        m
        shaders
)

install(TARGETS NovaGL DESTINATION lib)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../include/NovaGL.h" DESTINATION include)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../include/nova_glfw.h" DESTINATION include)
# GLFW / GLAD / GL drop-in shim headers (point a port's includes at NovaGL's
# include dir and <GLFW/glfw3.h>, <glad/glad.h>, <GL/gl.h>, <GLES/gl.h> resolve).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/GLFW"  DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/glad"  DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/GL"    DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/GLES"  DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/GLES2" DESTINATION include)

