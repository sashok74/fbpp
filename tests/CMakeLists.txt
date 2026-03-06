include(GoogleTest)

add_compile_definitions(FBPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test_data")

add_executable(test_query_generator_runner
    test_query_generator_runner.cpp
    test_base.cpp
)

target_link_libraries(test_query_generator_runner PRIVATE
    fbpp
    GTest::gtest
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

set(TABLE_TEST_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/table_test_1)
set(TABLE_TEST_MAIN_HEADER ${TABLE_TEST_GENERATED_DIR}/queries.generated.hpp)
set(TABLE_TEST_SUPPORT_HEADER ${TABLE_TEST_GENERATED_DIR}/queries.structs.generated.hpp)
set(TABLE_TEST_QUERIES_JSON ${CMAKE_CURRENT_SOURCE_DIR}/data/table_test_1_queries.json)

add_custom_command(
    OUTPUT ${TABLE_TEST_MAIN_HEADER} ${TABLE_TEST_SUPPORT_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TABLE_TEST_GENERATED_DIR}
    COMMAND $<TARGET_FILE:test_query_generator_runner>
        --scope table_test_1
        --profile table_test_1
        --input ${TABLE_TEST_QUERIES_JSON}
        --output ${TABLE_TEST_MAIN_HEADER}
        --support ${TABLE_TEST_SUPPORT_HEADER}
    DEPENDS test_query_generator_runner ${TABLE_TEST_QUERIES_JSON}
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
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

add_dependencies(test_query_generator query_generator)
target_compile_definitions(test_query_generator PRIVATE
    QUERY_GENERATOR_EXE=\"$<TARGET_FILE:query_generator>\")

gtest_discover_tests(test_query_generator)

add_executable(test_query_generator_integration
    unit/test_query_generator_integration.cpp
    test_base.cpp
)

target_link_libraries(test_query_generator_integration PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

add_dependencies(test_query_generator_integration generate_table_test_1_queries)
target_include_directories(test_query_generator_integration PRIVATE ${TABLE_TEST_GENERATED_DIR})
target_sources(test_query_generator_integration PRIVATE ${TABLE_TEST_MAIN_HEADER} ${TABLE_TEST_SUPPORT_HEADER})

gtest_discover_tests(test_query_generator_integration)

# Cancel operation test
add_executable(test_cancel_operation
    unit/test_cancel_operation.cpp
    test_base.cpp
)

target_link_libraries(test_cancel_operation PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_cancel_operation)

# Struct descriptor test
add_executable(test_struct_descriptor
    unit/test_struct_descriptor.cpp
)

target_link_libraries(test_struct_descriptor PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_struct_descriptor)

# Basic infrastructure tests
set(BASIC_TEST_SOURCES
    unit/test_trace.cpp
    unit/test_config.cpp
    unit/test_fbclient_symbols.cpp
    unit/test_timestamp_utils.cpp
)

foreach(test_source ${BASIC_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})

    target_link_libraries(${test_name} PRIVATE
        fbpp
        GTest::gtest
        GTest::gtest_main
        nlohmann_json::nlohmann_json
        ${FIREBIRD_LIBRARIES}
    )

    gtest_discover_tests(${test_name})
endforeach()

# ============================================================================
# Integration tests
# ============================================================================

# TTMath integration tests with automatic header generation
set(GENERATED_TTMATH_HEADER "${CMAKE_BINARY_DIR}/generated_queries_ttmath.hpp")
set(GENERATED_TTMATH_SUPPORT "${CMAKE_BINARY_DIR}/generated_queries_ttmath_support.hpp")
set(TTMATH_QUERIES_JSON "${CMAKE_SOURCE_DIR}/tests/test_data/query_generator_ttmath_queries.json")

add_custom_command(
    OUTPUT ${GENERATED_TTMATH_HEADER} ${GENERATED_TTMATH_SUPPORT}
    COMMAND $<TARGET_FILE:test_query_generator_runner>
        --scope ttmath
        --profile ttmath
        --use-ttmath-numeric
        --use-ttmath-int128
        --input ${TTMATH_QUERIES_JSON}
        --output ${GENERATED_TTMATH_HEADER}
        --support ${GENERATED_TTMATH_SUPPORT}
    DEPENDS test_query_generator_runner ${TTMATH_QUERIES_JSON}
    COMMENT "Generating TTMath query header from ${TTMATH_QUERIES_JSON}"
    VERBATIM
)

# Custom target to ensure headers are generated
add_custom_target(generate_ttmath_queries
    DEPENDS ${GENERATED_TTMATH_HEADER} ${GENERATED_TTMATH_SUPPORT}
)

# Test: query_generator with TTMath adapters (full E2E integration test)
# This test verifies that:
#   1. query_generator can generate code with TTMath adapters
#   2. Generated code compiles correctly
#   3. Generated code works with actual database operations
add_executable(test_query_generator_ttmath
    integration/test_query_generator_ttmath.cpp
    test_base.cpp
)

# Make test depend on generated header
add_dependencies(test_query_generator_ttmath generate_ttmath_queries)

# Include directory for generated header
target_include_directories(test_query_generator_ttmath PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_query_generator_ttmath PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_query_generator_ttmath)

# Test: Standalone compilation test for generated TTMath code
# This is a quick smoke test that verifies the generated code compiles
add_executable(test_generated_ttmath_compile
    integration/test_generated_ttmath_compile.cpp
)

add_dependencies(test_generated_ttmath_compile generate_ttmath_queries)
target_include_directories(test_generated_ttmath_compile PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_generated_ttmath_compile PRIVATE
    fbpp
    ${FIREBIRD_LIBRARIES}
)

# ============================================================================
# CppDecimal adapter integration tests (DECFLOAT types)
# ============================================================================

# Generated header paths for CppDecimal test
set(GENERATED_DECFLOAT_HEADER "${CMAKE_BINARY_DIR}/generated_queries_decfloat.hpp")
set(GENERATED_DECFLOAT_SUPPORT "${CMAKE_BINARY_DIR}/generated_queries_decfloat_support.hpp")
set(DECFLOAT_QUERIES_JSON "${CMAKE_SOURCE_DIR}/tests/test_data/query_generator_decfloat_queries.json")

# Custom command to generate header using query_generator with --use-cppdecimal
add_custom_command(
    OUTPUT ${GENERATED_DECFLOAT_HEADER} ${GENERATED_DECFLOAT_SUPPORT}
    COMMAND $<TARGET_FILE:test_query_generator_runner>
        --scope decfloat
        --profile decfloat
        --use-cppdecimal
        --input ${DECFLOAT_QUERIES_JSON}
        --output ${GENERATED_DECFLOAT_HEADER}
        --support ${GENERATED_DECFLOAT_SUPPORT}
    DEPENDS test_query_generator_runner ${DECFLOAT_QUERIES_JSON}
    COMMENT "Generating CppDecimal query header from ${DECFLOAT_QUERIES_JSON}"
    VERBATIM
)

# Custom target to ensure headers are generated
add_custom_target(generate_decfloat_queries
    DEPENDS ${GENERATED_DECFLOAT_HEADER} ${GENERATED_DECFLOAT_SUPPORT}
)

# Test: query_generator with CppDecimal adapters (full E2E integration test)
# This test verifies that:
#   1. query_generator can generate code with CppDecimal adapters
#   2. Generated code compiles correctly
#   3. Generated code works with actual database operations
add_executable(test_query_generator_decfloat
    integration/test_query_generator_decfloat.cpp
    test_base.cpp
)

# Make test depend on generated header
add_dependencies(test_query_generator_decfloat generate_decfloat_queries)

# Include directory for generated header
target_include_directories(test_query_generator_decfloat PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_query_generator_decfloat PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_query_generator_decfloat)

# Test: Standalone compilation test for generated CppDecimal code
# This is a quick smoke test that verifies the generated code compiles
add_executable(test_generated_decfloat_compile
    integration/test_generated_decfloat_compile.cpp
)

add_dependencies(test_generated_decfloat_compile generate_decfloat_queries)
target_include_directories(test_generated_decfloat_compile PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_generated_decfloat_compile PRIVATE
    fbpp
    ${FIREBIRD_LIBRARIES}
)

# ============================================================================
# std::chrono adapter integration tests (DATE/TIME types)
# ============================================================================

# Generated header paths for std::chrono test
set(GENERATED_DATETIME_HEADER "${CMAKE_BINARY_DIR}/generated_queries_datetime.hpp")
set(GENERATED_DATETIME_SUPPORT "${CMAKE_BINARY_DIR}/generated_queries_datetime_support.hpp")
set(DATETIME_QUERIES_JSON "${CMAKE_SOURCE_DIR}/tests/test_data/query_generator_datetime_queries.json")

# Custom command to generate header using query_generator with --use-chrono
add_custom_command(
    OUTPUT ${GENERATED_DATETIME_HEADER} ${GENERATED_DATETIME_SUPPORT}
    COMMAND $<TARGET_FILE:test_query_generator_runner>
        --scope datetime
        --profile datetime
        --use-chrono
        --input ${DATETIME_QUERIES_JSON}
        --output ${GENERATED_DATETIME_HEADER}
        --support ${GENERATED_DATETIME_SUPPORT}
    DEPENDS test_query_generator_runner ${DATETIME_QUERIES_JSON}
    COMMENT "Generating std::chrono query header from ${DATETIME_QUERIES_JSON}"
    VERBATIM
)

# Custom target to ensure headers are generated
add_custom_target(generate_datetime_queries
    DEPENDS ${GENERATED_DATETIME_HEADER} ${GENERATED_DATETIME_SUPPORT}
)

# Test: query_generator with std::chrono adapters (full E2E integration test)
# This test verifies that:
#   1. query_generator can generate code with std::chrono adapters
#   2. Generated code compiles correctly
#   3. Generated code works with actual database operations
add_executable(test_query_generator_datetime
    integration/test_query_generator_datetime.cpp
    test_base.cpp
)

# Make test depend on generated header
add_dependencies(test_query_generator_datetime generate_datetime_queries)

# Include directory for generated header
target_include_directories(test_query_generator_datetime PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_query_generator_datetime PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_query_generator_datetime)

# Test: Standalone compilation test for generated std::chrono code
# This is a quick smoke test that verifies the generated code compiles
add_executable(test_generated_datetime_compile
    integration/test_generated_datetime_compile.cpp
)

add_dependencies(test_generated_datetime_compile generate_datetime_queries)
target_include_directories(test_generated_datetime_compile PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_generated_datetime_compile PRIVATE
    fbpp
    ${FIREBIRD_LIBRARIES}
)

# ============================================================================
# Combined adapter integration tests (ALL adapters: TTMath + CppDecimal + std::chrono)
# ============================================================================

# Generated header paths for combined test
set(GENERATED_COMBINED_HEADER "${CMAKE_BINARY_DIR}/generated_queries_combined.hpp")
set(GENERATED_COMBINED_SUPPORT "${CMAKE_BINARY_DIR}/generated_queries_combined_support.hpp")
set(COMBINED_QUERIES_JSON "${CMAKE_SOURCE_DIR}/tests/test_data/query_generator_combined_queries.json")

# Custom command to generate header using query_generator with ALL adapters enabled
add_custom_command(
    OUTPUT ${GENERATED_COMBINED_HEADER} ${GENERATED_COMBINED_SUPPORT}
    COMMAND $<TARGET_FILE:test_query_generator_runner>
        --scope combined
        --profile combined
        --use-ttmath-numeric
        --use-ttmath-int128
        --use-cppdecimal
        --use-chrono
        --input ${COMBINED_QUERIES_JSON}
        --output ${GENERATED_COMBINED_HEADER}
        --support ${GENERATED_COMBINED_SUPPORT}
    DEPENDS test_query_generator_runner ${COMBINED_QUERIES_JSON}
    COMMENT "Generating combined query header with ALL adapters from ${COMBINED_QUERIES_JSON}"
    VERBATIM
)

# Custom target to ensure headers are generated
add_custom_target(generate_combined_queries
    DEPENDS ${GENERATED_COMBINED_HEADER} ${GENERATED_COMBINED_SUPPORT}
)

# Test: query_generator with ALL adapters (full E2E integration test)
# This test verifies that:
#   1. query_generator can generate code with all adapters enabled
#   2. Generated code compiles correctly with mixed adapter types
#   3. All adapters work together in one database operation
add_executable(test_query_generator_combined
    integration/test_query_generator_combined.cpp
    test_base.cpp
)

# Make test depend on generated header
add_dependencies(test_query_generator_combined generate_combined_queries)

# Include directory for generated header
target_include_directories(test_query_generator_combined PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(test_query_generator_combined PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_query_generator_combined)

# Extended types conversion test
add_executable(test_extended_types_conversion
    unit/test_extended_types_conversion.cpp
    test_base.cpp
)

target_link_libraries(test_extended_types_conversion PRIVATE
    fbpp
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    ${FIREBIRD_LIBRARIES}
)

gtest_discover_tests(test_extended_types_conversion)
