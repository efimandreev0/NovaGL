# =============================================================================
# Example 01: Colored Triangle
# =============================================================================

add_executable(01_triangle main.c
        ../09_racer/main.c)
target_link_libraries(01_triangle PRIVATE NovaGL)
target_include_directories(01_triangle PRIVATE "$ENV{DEVKITPRO}/portlibs/3ds/include")

ctr_create_3dsx(01_triangle)
