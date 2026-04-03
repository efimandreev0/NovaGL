# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(06_dynamic_textures main.c)
target_link_libraries(06_dynamic_textures PRIVATE NovaGL)
target_include_directories(06_dynamic_textures PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(06_dynamic_textures)
