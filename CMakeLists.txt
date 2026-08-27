cmake_minimum_required(VERSION 3.16)
project(pacloader LANGUAGES C CXX)

option(PACLOADER_ENABLE_LTO
    "Enable link-time optimization for non-Debug pacloader builds"
    OFF)

option(PACLOADER_ENABLE_PORTABLE_TUNING
    "Enable non-native optimization tuning that preserves broad CPU compatibility"
    OFF)
option(PACLOADER_BUILD_TESTS
    "Build the standalone codec tests"
    OFF)

if(NOT (WIN32 OR MINGW OR CMAKE_C_COMPILER MATCHES "mingw"))
    message(FATAL_ERROR "pacloader currently targets 32-bit Windows/MinGW only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

file(GLOB_RECURSE MINHOOK_SOURCES CONFIGURE_DEPENDS src/minhook/src/*.c)
add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC src/minhook/include)
target_compile_options(minhook PRIVATE -fPIC -m32)

include(cmake/PacloaderSources.cmake)
add_executable(pacloader ${PACLOADER_SOURCES})
set_target_properties(pacloader PROPERTIES OUTPUT_NAME linuxloader)

target_include_directories(pacloader PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/log
    ${PROJECT_SOURCE_DIR}/src/elfio
    ${PROJECT_SOURCE_DIR}/src/glad/include
    ${PROJECT_SOURCE_DIR}/src/minhook/include
    ${PROJECT_SOURCE_DIR}/src/SDL3)

target_compile_definitions(pacloader PRIVATE
    PACLOADER_BUILD=1
    _WIN32_WINNT=0x0600
    WINVER=0x0600)

target_compile_options(pacloader PRIVATE
    -m32
    -Wall
    -Wextra
    -fpermissive
    -Wno-unused-parameter
    -Wimplicit-fallthrough=0
    -Wstrict-aliasing=0)

# Use -O3 for release-like builds; Debug keeps source-level diagnostics intact.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(pacloader PRIVATE -O3)
    target_compile_options(minhook PRIVATE -O3)

    if(PACLOADER_ENABLE_PORTABLE_TUNING)
        target_compile_options(pacloader PRIVATE
            -fno-semantic-interposition
            -fno-math-errno
            -fno-trapping-math
            -ftree-vectorize)
        target_compile_options(minhook PRIVATE
            -fno-semantic-interposition
            -fno-math-errno
            -fno-trapping-math
            -ftree-vectorize)
    endif()

    if(PACLOADER_ENABLE_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT PACLOADER_IPO_SUPPORTED
                            OUTPUT PACLOADER_IPO_ERROR
                            LANGUAGES C CXX)
        if(PACLOADER_IPO_SUPPORTED)
            set_property(TARGET minhook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_property(TARGET pacloader PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO is unavailable; using -O3 without LTO: ${PACLOADER_IPO_ERROR}")
        endif()
    endif()
endif()

target_link_options(pacloader PRIVATE
    -m32
    -static-libgcc
    -static-libstdc++
    "SHELL:-Xlinker --stack -Xlinker 4194304,4194304"
    -Wl,--large-address-aware)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(pacloader PRIVATE -s)
endif()

target_link_directories(pacloader PRIVATE ${PROJECT_SOURCE_DIR}/libs/win32)

get_filename_component(PACLOADER_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(PACLOADER_TOOLCHAIN_ROOT "${PACLOADER_TOOLCHAIN_BIN}" DIRECTORY)
find_library(PACLOADER_CRYPTO_LIBRARY NAMES crypto
    HINTS "${PACLOADER_TOOLCHAIN_ROOT}/lib" REQUIRED)

# Keep the pthread implementation internal to pacloader.  This leaves SDL3 as
# the only non-system DLL required by the native loader itself.
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=libwinpthread.a
    OUTPUT_VARIABLE PACLOADER_WINPTHREAD
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT IS_ABSOLUTE "${PACLOADER_WINPTHREAD}" OR NOT EXISTS "${PACLOADER_WINPTHREAD}")
    message(FATAL_ERROR "A static 32-bit libwinpthread.a is required")
endif()

target_link_libraries(pacloader PRIVATE
    minhook
    SDL3
    ${PACLOADER_WINPTHREAD}
    gdi32
    winmm
    ole32
    ws2_32
    opengl32
    psapi
    iphlpapi
    winhttp
    ${PACLOADER_CRYPTO_LIBRARY})

if(PACLOADER_BUILD_TESTS)
    enable_testing()
    add_executable(nbgiCodecTest
        tools/nbgiCodecTest.cpp
        src/loader/hardware/namco/banapassport/nbgiCodec.cpp)
    target_include_directories(nbgiCodecTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_options(nbgiCodecTest PRIVATE -m32 -Wall -Wextra)
    target_link_options(nbgiCodecTest PRIVATE -m32 -static-libgcc -static-libstdc++)
    target_link_libraries(nbgiCodecTest PRIVATE ${PACLOADER_CRYPTO_LIBRARY})
    add_test(NAME nbgiCodecTest COMMAND nbgiCodecTest)
endif()
