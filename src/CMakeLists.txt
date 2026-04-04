# =============================================================================
# NovaGL static library
# =============================================================================

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader.pica")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Compile .pica -> .shbin -> embedded .s + .h
ctr_add_shader_library(NovaGL_shader "${SHADER_SRC}")

dkp_add_embedded_binary_library(shaders NovaGL_shader)

# Library sources
add_library(NovaGL STATIC
        NovaGL.c
)

target_include_directories(NovaGL
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        PRIVATE
        "${GENERATED_DIR}"
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