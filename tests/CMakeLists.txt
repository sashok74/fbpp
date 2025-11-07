include(GoogleTest)

set(TABLE_TEST_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/table_test_1)
set(TABLE_TEST_MAIN_HEADER ${TABLE_TEST_GENERATED_DIR}/queries.generated.hpp)
set(TABLE_TEST_SUPPORT_HEADER ${TABLE_TEST_GENERATED_DIR}/queries.structs.generated.hpp)
set(TABLE_TEST_QUERIES_JSON ${CMAKE_CURRENT_SOURCE_DIR}/data/table_test_1_queries.json)

add_custom_command(
    OUTPUT ${TABLE_TEST_MAIN_HEADER} ${TABLE_TEST_SUPPORT_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TABLE_TEST_GENERATED_DIR}
    COMMAND $<TARGET_FILE:query_generator>
        --dsn firebird5:testdb
        --user SYSDBA
        --password planomer
        --charset UTF8
        --input ${TABLE_TEST_QUERIES_JSON}
        --output ${TABLE_TEST_MAIN_HEADER}
        --support ${TABLE_TEST_SUPPORT_HEADER}
    DEPENDS query_generator ${TABLE_TEST_QUERIES_JSON}
    VERBATIM
)

add_custom_target(generate_table_test_1_queries
    DEPENDS ${TABLE_TEST_MAIN_HEADER} ${TABLE_TEST_SUPPORT_HEADER}
)

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

# Struct pack test
add_executable(test_struct_pack
    unit/test_struct_pack.cpp
    test_base.cpp
)

target_link_libraries(test_struct_pack PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_struct_pack)

# Query generator test
add_executable(test_query_generator
    unit/test_query_generator.cpp
    test_base.cpp
)

target_link_libraries(test_query_generator PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

add_dependencies(test_query_generator query_generator)
target_compile_definitions(test_query_generator PRIVATE BUILD_DIR=\"${CMAKE_BINARY_DIR}\")

gtest_discover_tests(test_query_generator)

add_executable(test_query_generator_integration
    unit/test_query_generator_integration.cpp
    test_base.cpp
)

target_link_libraries(test_query_generator_integration PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

add_dependencies(test_query_generator_integration generate_table_test_1_queries)
target_include_directories(test_query_generator_integration PRIVATE ${TABLE_TEST_GENERATED_DIR})
target_sources(test_query_generator_integration PRIVATE ${TABLE_TEST_MAIN_HEADER} ${TABLE_TEST_SUPPORT_HEADER})

gtest_discover_tests(test_query_generator_integration)

gtest_discover_tests(test_struct_pack)

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
