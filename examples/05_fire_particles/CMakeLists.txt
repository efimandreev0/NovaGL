# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(05_fire_particles main.c)
target_link_libraries(05_fire_particles PRIVATE NovaGL)
target_include_directories(05_fire_particles PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(05_fire_particles)
