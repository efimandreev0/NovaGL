# =============================================================================
# gl2citro3d - OpenGL ES 1.1 -> Citro3D Translation Layer
# =============================================================================
# Build:
#   mkdir build && cd build
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/DevkitArm3DS.cmake ..
#   make
# =============================================================================
cmake_minimum_required(VERSION 3.13)
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if (NOT DEFINED ENV{DEVKITPRO})
		message(FATAL_ERROR "Please define DEVKITPRO to point to your SDK path!")
	else ()
		set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE PATH "toolchain file")
	endif ()
endif ()
project(NovaGL C ASM)

# Build library
add_subdirectory(src)

# Build examples
option(GL2CITRO3D_BUILD_EXAMPLES "Build examples" ON)
if(GL2CITRO3D_BUILD_EXAMPLES)
    add_subdirectory(examples/01_triangle)
    add_subdirectory(examples/02_textured_quad)
    add_subdirectory(examples/03_rotating_cube)
	add_subdirectory(examples/04_terrain_fog)
	add_subdirectory(examples/05_fire_particles)
	add_subdirectory(examples/06_dynamic_textures)
	add_subdirectory(examples/07)
	add_subdirectory(examples/08)
endif()
