# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(04_terrain_fog main.c)
target_link_libraries(04_terrain_fog PRIVATE NovaGL)
target_include_directories(04_terrain_fog PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")
ctr_create_3dsx(04_terrain_fog)
