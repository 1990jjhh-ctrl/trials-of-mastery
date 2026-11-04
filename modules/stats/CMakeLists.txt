find_package(nlohmann_json CONFIG REQUIRED)
find_package(SimpleIni CONFIG REQUIRED)

# Game-independent formula and configuration core.
add_library(tom_stats_core STATIC
    core/src/actor_stats.cpp
    core/src/curve.cpp
    core/src/curve_parse.cpp
    core/src/stat_registry.cpp
    core/src/stat_value.cpp
    core/src/table_merge.cpp
)
target_include_directories(tom_stats_core PUBLIC core/include)
target_link_libraries(tom_stats_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE SimpleIni::SimpleIni
)
tom_enable_warnings(tom_stats_core)

if(TOM_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    add_executable(tom_stats_core_tests
        tests/unit/actor_stats_test.cpp
        tests/unit/curve_parse_test.cpp
        tests/unit/curve_test.cpp
        tests/unit/stat_registry_test.cpp
        tests/unit/stat_value_test.cpp
        tests/unit/table_merge_test.cpp
    )
    target_link_libraries(tom_stats_core_tests PRIVATE tom_stats_core Catch2::Catch2WithMain)
    tom_enable_warnings(tom_stats_core_tests)
    catch_discover_tests(tom_stats_core_tests)
endif()

if(TOM_BUILD_PLUGIN)
    add_library(tom_stats_plugin SHARED
        plugin/src/main.cpp
        plugin/src/Messaging.cpp
    )
    set_target_properties(tom_stats_plugin PROPERTIES OUTPUT_NAME "trials-of-mastery-stats")
    target_precompile_headers(tom_stats_plugin PRIVATE plugin/src/PCH.h)
    target_link_libraries(tom_stats_plugin PRIVATE tom_stats_core CommonLibSSE::CommonLibSSE)
    # SKSE plugin version data. Addresses come from the Address Library, so one
    # DLL covers every AE runtime (docs/PRD.md#4-shared-constraints).
    #
    # The default StructCompatibility (Independent, SKSE's NoStructUse) is a
    # deliberate claim: the DLL loads on both sides of 1.6.629, where some game
    # struct layouts changed. It stays true only while every member whose
    # layout differs between runtimes is reached through CommonLibSSE-NG's
    # runtime accessors, never by a fixed offset.
    target_commonlibsse_properties(tom_stats_plugin
        NAME "trials-of-mastery-stats"
        AUTHOR "Joel Hirsbrunner"
        USE_ADDRESS_LIBRARY
    )
    tom_enable_warnings(tom_stats_plugin)
endif()
