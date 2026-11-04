find_package(nlohmann_json CONFIG REQUIRED)
find_package(SimpleIni CONFIG REQUIRED)

# Game-independent formula and configuration core.
add_library(tom_stats_core STATIC
    core/src/placeholder.cpp
)
target_link_libraries(tom_stats_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE SimpleIni::SimpleIni
)
tom_enable_warnings(tom_stats_core)

if(TOM_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    add_executable(tom_stats_core_tests
        tests/unit/placeholder_test.cpp
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
    target_commonlibsse_properties(tom_stats_plugin
        NAME "trials-of-mastery-stats"
        AUTHOR "Joel Hirsbrunner"
        USE_ADDRESS_LIBRARY
    )
    tom_enable_warnings(tom_stats_plugin)
endif()
