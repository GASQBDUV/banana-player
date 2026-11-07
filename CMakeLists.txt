cmake_minimum_required(VERSION 3.16)
project(banana-player VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The handheld has OpenGL ES 2 (Mesa Panfrost) through SDL2's KMSDRM; a desktop has desktop GL.
if(APPLE)
	set(BP_GLES_DEFAULT OFF)
else()
	set(BP_GLES_DEFAULT ON)
endif()
option(BP_GLES "Ask SDL for an OpenGL ES 2 context" ${BP_GLES_DEFAULT})

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

# what doesn't need a window: unit-tested on its own
add_library(bpcore STATIC src/aspect.cpp src/tracks.cpp src/format.cpp src/substyle.cpp src/settings.cpp src/resume.cpp src/playlist.cpp src/browser.cpp src/history.cpp src/png.cpp)
target_include_directories(bpcore PUBLIC src)
target_link_libraries(bpcore PUBLIC PkgConfig::MPV PkgConfig::ZLIB)

add_executable(banana-player src/main.cpp src/player.cpp src/ui.cpp src/input.cpp src/sleepwatch.cpp src/touch.cpp src/panel.cpp src/levelwatch.cpp)
target_link_libraries(banana-player PRIVATE bpcore PkgConfig::SDL2 PkgConfig::SDL2_TTF)
target_compile_definitions(banana-player PRIVATE
	BP_VERSION="${PROJECT_VERSION}"
	BP_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
if(BP_GLES)
	target_compile_definitions(banana-player PRIVATE BP_GLES=1)
endif()

enable_testing()
add_executable(core-test tests/core_test.cpp)
target_link_libraries(core-test PRIVATE bpcore)
add_test(NAME core COMMAND core-test)
