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
        lookup.c
        egl.c
        nova_stb_ds_impl.c
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

