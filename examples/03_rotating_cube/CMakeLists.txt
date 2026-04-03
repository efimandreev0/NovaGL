# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(03_rotating_cube main.c)
target_link_libraries(03_rotating_cube PRIVATE NovaGL)
target_include_directories(03_rotating_cube PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(03_rotating_cube)
