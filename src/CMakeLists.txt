# =============================================================================
# NovaGL static library
# =============================================================================

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader.pica")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Compile .pica -> .shbin -> embedded .s + .h
ctr_add_shader_library(NovaGL_shader "${SHADER_SRC}")

dkp_add_embedded_binary_library(shaders NovaGL_shader)

option(NOVA_VBO_STREAM "Use VBO stream" ON)
if (NOVA_VBO_STREAM)
    add_definitions(-DNOVA_VBO_STREAM)
endif()
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
)

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

# Auto-install to portlibs after build
if(DEFINED ENV{DEVKITPRO})
    set(PORTLIBS_DIR "$ENV{DEVKITPRO}/portlibs/3ds")
    add_custom_command(TARGET NovaGL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTLIBS_DIR}/include"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTLIBS_DIR}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/../include/NovaGL.h"
                "${PORTLIBS_DIR}/include/NovaGL.h"
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:NovaGL>"
                "${PORTLIBS_DIR}/lib/libNovaGL.a"
            COMMENT "Installing NovaGL to ${PORTLIBS_DIR}"
    )
endif()