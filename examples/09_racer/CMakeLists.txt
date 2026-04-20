# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(09_racer main.c)
target_link_libraries(09_racer PRIVATE NovaGL)
target_include_directories(09_racer PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")
ctr_create_3dsx(09_racer)
