# Papyrus scripts, compiled with the Creation Kit compiler under Wine
# (docs/stats/PLAN.md, decision D5 and spike S8). Included when
# TOM_BUILD_PAPYRUS is on.
#
# The compiler is a .NET program: the Wine prefix needs Wine Mono (or .NET).
# Paths are handed to it on Wine's Z: drive, which maps the Linux root in a
# default prefix.

set(TOM_SKYRIM_DIR "" CACHE PATH "Skyrim Special Edition install with the Creation Kit (Papyrus Compiler, Data/Source/Scripts)")
find_program(TOM_WINE wine DOC "Wine executable that runs the Papyrus compiler")
set(TOM_WINEPREFIX "" CACHE PATH "Wine prefix with Wine Mono for the Papyrus compiler; empty uses Wine's default (~/.wine)")

set(TOM_PAPYRUS_COMPILER "${TOM_SKYRIM_DIR}/Papyrus Compiler/PapyrusCompiler.exe")
set(TOM_PAPYRUS_VANILLA_SOURCES "${TOM_SKYRIM_DIR}/Data/Source/Scripts")
set(TOM_PAPYRUS_FLAGS "TESV_Papyrus_Flags.flg")

if(NOT TOM_WINE)
    message(FATAL_ERROR "TOM_BUILD_PAPYRUS needs Wine; install it or set TOM_WINE")
endif()
if(NOT EXISTS "${TOM_PAPYRUS_COMPILER}")
    message(FATAL_ERROR "TOM_BUILD_PAPYRUS: no Papyrus compiler at '${TOM_PAPYRUS_COMPILER}'; "
        "set TOM_SKYRIM_DIR to a Skyrim install with the Creation Kit")
endif()
if(NOT EXISTS "${TOM_PAPYRUS_VANILLA_SOURCES}/${TOM_PAPYRUS_FLAGS}")
    message(FATAL_ERROR "TOM_BUILD_PAPYRUS: no ${TOM_PAPYRUS_FLAGS} in '${TOM_PAPYRUS_VANILLA_SOURCES}'; "
        "unpack the Creation Kit's Scripts.zip there")
endif()

# tom_add_papyrus_scripts(<target> OUTPUT_DIR <dir> SOURCES <file.psc>...)
#
# Adds <target>, part of ALL, which compiles each source to <dir>/<name>.pex.
# Imports are the sources' own folders, then the vanilla sources. A script is
# rebuilt when its source changes; a change to a script it imports does not
# rebuild it.
function(tom_add_papyrus_scripts target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_DIR" "SOURCES")
    if(NOT arg_OUTPUT_DIR OR NOT arg_SOURCES)
        message(FATAL_ERROR "tom_add_papyrus_scripts: OUTPUT_DIR and SOURCES are required")
    endif()

    set(import_dirs "")
    foreach(source IN LISTS arg_SOURCES)
        cmake_path(ABSOLUTE_PATH source)
        cmake_path(GET source PARENT_PATH dir)
        list(APPEND import_dirs "${dir}")
    endforeach()
    list(REMOVE_DUPLICATES import_dirs)
    list(APPEND import_dirs "${TOM_PAPYRUS_VANILLA_SOURCES}")
    # The compiler separates imports with ';', which CMake would split.
    list(JOIN import_dirs "|" imports)

    set(outputs "")
    foreach(source IN LISTS arg_SOURCES)
        cmake_path(ABSOLUTE_PATH source)
        cmake_path(GET source STEM name)
        set(output "${arg_OUTPUT_DIR}/${name}.pex")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}"
                "-DWINE=${TOM_WINE}"
                "-DWINEPREFIX=${TOM_WINEPREFIX}"
                "-DCOMPILER=${TOM_PAPYRUS_COMPILER}"
                "-DFLAGS=${TOM_PAPYRUS_FLAGS}"
                "-DIMPORTS=${imports}"
                "-DSOURCE=${source}"
                "-DOUTPUT=${output}"
                -P "${CMAKE_SOURCE_DIR}/cmake/papyrus_compile.cmake"
            DEPENDS "${source}" "${CMAKE_SOURCE_DIR}/cmake/papyrus_compile.cmake"
            COMMENT "Compiling Papyrus script ${name}"
            VERBATIM
        )
        list(APPEND outputs "${output}")
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()
