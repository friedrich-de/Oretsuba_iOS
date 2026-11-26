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

# The dictionaries of the mining kit (engine/src/dict) are SQLite files. Android has no
# SQLite for native code, so the app carries its own: the amalgamation as sqlite.org
# publishes it, fetched once for each build folder and checked against its hash. A copy
# in engine/third_party/sqlite (sqlite3.c and sqlite3.h) is taken instead when there is one.
set(SQLITE_VERSION 3540100)
set(SQLITE_SHA3 92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30)
set(SQLITE_DIR "${ENGINE}/third_party/sqlite")
if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
    set(SQLITE_DIR "${CMAKE_CURRENT_BINARY_DIR}/sqlite-amalgamation-${SQLITE_VERSION}")
    if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
        set(SQLITE_ZIP "${CMAKE_CURRENT_BINARY_DIR}/sqlite-amalgamation-${SQLITE_VERSION}.zip")
        file(DOWNLOAD "https://www.sqlite.org/2026/sqlite-amalgamation-${SQLITE_VERSION}.zip" "${SQLITE_ZIP}"
            EXPECTED_HASH SHA3_256=${SQLITE_SHA3} TLS_VERIFY ON STATUS SQLITE_FETCHED)
        list(GET SQLITE_FETCHED 0 SQLITE_FETCH_CODE)
        if(NOT SQLITE_FETCH_CODE EQUAL 0)
            message(FATAL_ERROR "SQLite could not be fetched from sqlite.org (${SQLITE_FETCHED}). "
                "Put sqlite3.c and sqlite3.h of version ${SQLITE_VERSION} into engine/third_party/sqlite to build without it.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${SQLITE_ZIP}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()
# One connection is used by one thread at a time, which SQLite's default guards; what the
# kit does not use is left out.
set_source_files_properties("${SQLITE_DIR}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "-w" COMPILE_DEFINITIONS
    "SQLITE_DQS=0;SQLITE_DEFAULT_MEMSTATUS=0;SQLITE_OMIT_LOAD_EXTENSION;SQLITE_OMIT_DEPRECATED;SQLITE_OMIT_SHARED_CACHE;SQLITE_USE_ALLOCA;SQLITE_TEMP_STORE=2")

add_library(oretsuba SHARED
    ${ENGINE_CPP}
    ${ENGINE_C}
    "${ENGINE_API}"
    "${SQLITE_DIR}/sqlite3.c"
    android_kit_host.cpp
    android_platform.cpp
    audio_out.cpp
    cp932.cpp
    jni_bridge.cpp
    jni_main.cpp)

target_include_directories(oretsuba PRIVATE
    "${ENGINE}/src"
    "${ENGINE}/third_party"
    "${SQLITE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

# The picture is composed in software: optimise in debug builds too, as the
# Xcode project does.
target_compile_options(oretsuba PRIVATE
    -O2 -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# zlib, for the zip files dictionaries come in, is Android's own.
target_link_libraries(oretsuba PRIVATE android log aaudio m z)
