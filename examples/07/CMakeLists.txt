# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(07_prj main.c)
target_link_libraries(07_prj PRIVATE NovaGL)
target_include_directories(07_prj PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(07_prj)
