# The app's native library: the engine (every .cpp and .c under engine/src,
# as the Makefiles take them) and the Android glue in this folder.
cmake_minimum_required(VERSION 3.22.1)
project(oretsuba LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(ENGINE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../engine" ABSOLUTE)

file(GLOB_RECURSE ENGINE_CPP CONFIGURE_DEPENDS "${ENGINE}/src/*.cpp")
file(GLOB_RECURSE ENGINE_C CONFIGURE_DEPENDS "${ENGINE}/src/*.c")

# The engine's C interface is written once for every app. It is Objective-C++
# by its name only: here it is compiled as C++ and names the Android platform.
set(ENGINE_API "${ENGINE}/src/api/ore_api.mm")
set_source_files_properties("${ENGINE_API}" PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-x;c++")

# The decoders the engine brings along (stb, minimp3, pl_mpeg) are not ours to tidy.
set_source_files_properties(${ENGINE_C} PROPERTIES COMPILE_OPTIONS "-w")

add_library(oretsuba SHARED
    ${ENGINE_CPP}
    ${ENGINE_C}
    "${ENGINE_API}"
    android_platform.cpp
    audio_out.cpp
    cp932.cpp
    jni_bridge.cpp
    jni_main.cpp)

target_include_directories(oretsuba PRIVATE
    "${ENGINE}/src"
    "${ENGINE}/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}")

# The picture is composed in software: optimise in debug builds too, as the
# Xcode project does.
target_compile_options(oretsuba PRIVATE
    -O2 -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# The dictionary of the mining kit (engine/src/dict) needs SQLite and zlib, which
# this build does not link yet: the kit stays off here until it does.
target_compile_definitions(oretsuba PRIVATE ORE_MINING_KIT=0)

target_link_libraries(oretsuba PRIVATE android log aaudio m)
