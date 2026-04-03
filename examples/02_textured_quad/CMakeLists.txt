# =============================================================================
# Example 02: Textured Quad
# =============================================================================

add_executable(02_textured_quad main.c)
target_link_libraries(02_textured_quad PRIVATE NovaGL)
target_include_directories(02_textured_quad PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(02_textured_quad)
