# =============================================================================
# NovaGL static library
# =============================================================================

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/NovaGL_shader.pica")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Compile .pica -> .shbin -> embedded .s + .h
ctr_add_shader_library(NovaGL_shader "${SHADER_SRC}")

# Создаем таргет "shaders", в котором лежит скомпилированный бинарник
dkp_add_embedded_binary_library(shaders NovaGL_shader)

# Library sources (УБРАЛИ ОТСЮДА SHADER_OBJ)
add_library(NovaGL STATIC
        NovaGL.c
)

target_include_directories(NovaGL
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        PRIVATE
        "${GENERATED_DIR}"
)

# ЛИНКУЕМ ШЕЙДЕР СЮДА!
target_link_libraries(NovaGL
        PUBLIC
        citro3d
        ctru
        m
        shaders   # <--- Вот она, магия
)