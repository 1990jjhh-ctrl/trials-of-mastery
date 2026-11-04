cmake_minimum_required(VERSION 3.25)

option(TOM_BUILD_TESTS "Build the unit tests of the core libraries" ON)

# Must be set before project(), which runs the vcpkg manifest install.
if(TOM_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(trials_of_mastery VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(TOM_BUILD_TESTS)
    enable_testing()
endif()

function(tom_enable_warnings target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endfunction()

add_subdirectory(modules/stats)
