# fbpp Library Examples
# All examples use existing TABLE_TEST_1 database table

# Common settings for all examples
set(EXAMPLE_SOURCES
    01_basic_operations.cpp      # Basic CRUD operations with tuples (now with auto-cache)
    02_json_operations.cpp       # JSON parameter binding and result fetching
    03_batch_simple.cpp         # Simple batch operations demonstration
    04_batch_advanced.cpp       # Advanced batch with TTMath/CppDecimal support
    08_struct_fetch.cpp         # Struct-based fetch demonstration
    06_named_parameters.cpp     # Named parameters support demonstration
    07_cancel_operation.cpp     # Multi-threaded cancelOperation() API demonstration
    07_cancel_test_variants.cpp  # Test transaction memory leak
    test_prepare_transaction.cpp   # Test if prepare needs transaction
    test_statement_free.cpp      # Test statement free/release behavior
    test_statement_refcount.cpp  # Test statement reference counting leak
)

# Build each example
foreach(example_src ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_src} NAME_WE)
    add_executable(${example_name} ${example_src})

    target_include_directories(${example_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${FIREBIRD_INCLUDE_DIR}
    )

    target_link_libraries(${example_name} PRIVATE
        fbpp
        ${FIREBIRD_LIBRARIES}
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ttmath
        cppdecimal
        pthread
    )

    target_compile_features(${example_name} PRIVATE cxx_std_20)
endforeach()
