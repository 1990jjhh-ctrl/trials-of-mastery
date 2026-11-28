# Compiles one Papyrus script; run by tom_add_papyrus_scripts (papyrus.cmake)
# with cmake -P and the variables WINE, WINEPREFIX (may be empty), COMPILER,
# FLAGS, IMPORTS ('|'-separated), SOURCE and OUTPUT.
#
# The compiler exits with 0 even when compilation fails, so success is the
# output file existing after the old one was removed. Wine's output is shown
# only on failure; on success it is noise (rpcss and wow64 messages).

file(REMOVE "${OUTPUT}")
cmake_path(GET OUTPUT PARENT_PATH output_dir)
file(MAKE_DIRECTORY "${output_dir}")

if(WINEPREFIX)
    set(ENV{WINEPREFIX} "${WINEPREFIX}")
endif()

# Absolute Linux paths on Wine's Z: drive.
string(REPLACE "|" ";Z:" imports "Z:${IMPORTS}")

execute_process(
    COMMAND "${WINE}" "${COMPILER}" "Z:${SOURCE}" "-f=${FLAGS}" "-i=${imports}" "-o=Z:${output_dir}" -q
    RESULT_VARIABLE result
    OUTPUT_VARIABLE log
    ERROR_VARIABLE log
)
if(NOT result EQUAL 0 OR NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "Papyrus compile of ${SOURCE} failed (exit ${result}):\n${log}")
endif()
