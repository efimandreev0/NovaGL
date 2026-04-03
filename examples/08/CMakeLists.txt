# =============================================================================
# Example 03: Rotating Cube
# =============================================================================

add_executable(08_prj main.c)
target_link_libraries(08_prj PRIVATE NovaGL)
target_include_directories(08_prj PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")
ctr_create_3dsx(08_prj)
