cmake_minimum_required(VERSION 3.25)

option(TOM_BUILD_TESTS "Build the unit tests of the core libraries" ON)
option(TOM_BUILD_PLUGIN "Build the SKSE plugin DLLs (Windows cross build only)" OFF)

# Must be set before project(), which runs the vcpkg manifest install.
if(TOM_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
if(TOM_BUILD_PLUGIN)
    list(APPEND VCPKG_MANIFEST_FEATURES "plugin")
endif()

project(trials_of_mastery VERSION 0.1.0 LANGUAGES CXX)

if(TOM_BUILD_PLUGIN AND NOT WIN32)
    message(FATAL_ERROR "TOM_BUILD_PLUGIN needs the Windows cross toolchain; use the 'plugin' preset")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(TOM_BUILD_TESTS)
    enable_testing()
endif()

# The plugin preset sets the clang-cl warning flags for everything it builds.
function(tom_enable_warnings target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endfunction()

if(TOM_BUILD_PLUGIN)
    # CommonLibSSE-NG (git submodule, GPL-3.0-or-later). AE only, 1.6.x and newer:
    # docs/PRD.md#4-shared-constraints.
    set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
    set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    include(lib/skyrim-mod-toolchain/cmake/commonlibsse.cmake)
endif()

add_subdirectory(modules/stats)
