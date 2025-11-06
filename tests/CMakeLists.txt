include(GoogleTest)

# Core wrapper test
add_executable(test_core_wrapper
    unit/test_core_wrapper.cpp
    test_base.cpp
)

target_link_libraries(test_core_wrapper PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_core_wrapper)

# Statement test
add_executable(test_statement
    unit/test_statement.cpp
    test_base.cpp
)

target_link_libraries(test_statement PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_statement)

# FirebirdException test
add_executable(test_firebird_exception
    unit/test_firebird_exception.cpp
    test_base.cpp
)

target_link_libraries(test_firebird_exception PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_firebird_exception)

# Statement cache test
add_executable(test_statement_cache
    unit/test_statement_cache.cpp
    test_base.cpp
)

target_link_libraries(test_statement_cache PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_statement_cache)

# Cached statements integration test
add_executable(test_cached_statements
    unit/test_cached_statements.cpp
    test_base.cpp
)

target_link_libraries(test_cached_statements PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_cached_statements)


# Adapted type traits test
add_executable(test_adapted_type_traits
    unit/test_adapted_type_traits.cpp
)

target_link_libraries(test_adapted_type_traits PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_adapted_type_traits)

# TTMath INT128 adapter test
add_executable(test_ttmath_int128
    adapters/test_ttmath_int128.cpp
)

target_link_libraries(test_ttmath_int128 PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_ttmath_int128)

# TTMath scale test (covers numeric functionality)
add_executable(test_ttmath_scale
    adapters/test_ttmath_scale.cpp
)

target_link_libraries(test_ttmath_scale PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_ttmath_scale)

# CppDecimal DECFLOAT adapter test
add_executable(test_cppdecimal_decfloat
    adapters/test_cppdecimal_decfloat.cpp
)

target_link_libraries(test_cppdecimal_decfloat PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_cppdecimal_decfloat)

# Named parameters test
add_executable(test_named_parameters
    unit/test_named_parameters.cpp
    test_base.cpp
)

target_link_libraries(test_named_parameters PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_named_parameters)

# Query metadata test
add_executable(test_query_metadata
    unit/test_query_metadata.cpp
    test_base.cpp
)

target_link_libraries(test_query_metadata PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_query_metadata)

# Cancel operation test
add_executable(test_cancel_operation
    unit/test_cancel_operation.cpp
    test_base.cpp
)

target_link_libraries(test_cancel_operation PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_cancel_operation)

# Basic infrastructure tests
set(BASIC_TEST_SOURCES
    unit/test_logging.cpp
    unit/test_config.cpp
    unit/test_fbclient_symbols.cpp
)

foreach(test_source ${BASIC_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})

    target_link_libraries(${test_name} PRIVATE
        fbpp
        GTest::gtest
        GTest::gtest_main
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ${FIREBIRD_LIBRARIES}
    )

    gtest_discover_tests(${test_name})
endforeach()
