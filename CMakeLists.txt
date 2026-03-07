cmake_minimum_required(VERSION 3.20)
project(firebird-binding-lab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build)

# Find Conan packages
find_package(nlohmann_json REQUIRED)

include(cmake/FindFirebird.cmake)
find_package(Firebird REQUIRED)
message(STATUS "Firebird include dir: ${FIREBIRD_INCLUDE_DIRS}")
message(STATUS "Firebird library: ${FIREBIRD_LIBRARIES}")

# Add vendored libraries
add_subdirectory(third_party/ttmath)
add_subdirectory(third_party/cppdecimal)

function(fbpp_configure_cxx_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /wd4100)
    else()
        target_compile_options(${target_name} PRIVATE -Wno-unused-parameter)
    endif()
endfunction()

# Runtime wrapper
add_library(fbpp_core STATIC
    src/core/firebird/fb_connection.cpp
    src/core/firebird/fb_transaction.cpp
    src/core/firebird/fb_statement.cpp
    src/core/firebird/fb_statement_cache.cpp
    src/core/firebird/fb_named_param_parser.cpp
    src/core/firebird/fb_message_metadata.cpp
    src/core/firebird/fb_result_set.cpp
    src/core/firebird/fb_message_builder.cpp
    src/core/firebird/fb_exception.cpp
    src/core/firebird/fb_extended_types.cpp
    src/core/firebird/fb_batch.cpp

    src/util/trace.cpp
)

target_include_directories(fbpp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${FIREBIRD_INCLUDE_DIRS}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(fbpp_core
    PUBLIC
        $<BUILD_INTERFACE:Firebird::Firebird>
        nlohmann_json::nlohmann_json
        ttmath
        cppdecimal
)

fbpp_configure_cxx_target(fbpp_core)

# Schema introspection and query analysis layer
add_library(fbpp_schema STATIC
    src/schema/type_mapper.cpp
    src/schema/query_analyzer.cpp
)

target_include_directories(fbpp_schema PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${FIREBIRD_INCLUDE_DIRS}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(fbpp_schema PUBLIC fbpp_core)

fbpp_configure_cxx_target(fbpp_schema)

# Code generation helpers built on top of the runtime layer
add_library(fbpp_codegen STATIC
    src/core/firebird/query_generator_service.cpp
)

target_include_directories(fbpp_codegen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${FIREBIRD_INCLUDE_DIRS}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(fbpp_codegen
    PUBLIC
        fbpp_core
        fbpp_schema
)

fbpp_configure_cxx_target(fbpp_codegen)

# Internal helpers for examples and tests
add_library(fbpp_test_support STATIC
    src/util/connection_helper.cpp
    src/util/config.cpp
    src/util/config_loader.cpp
)

target_include_directories(fbpp_test_support PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${FIREBIRD_INCLUDE_DIRS}>
)

target_link_libraries(fbpp_test_support
    PUBLIC
        fbpp_core
        nlohmann_json::nlohmann_json
)

fbpp_configure_cxx_target(fbpp_test_support)

# Create aliases for modern CMake usage and local compatibility
add_library(fbpp ALIAS fbpp_core)
add_library(fbpp::fbpp ALIAS fbpp_core)
add_library(fbpp::fbpp_core ALIAS fbpp_core)
add_library(fbpp::fbpp_schema ALIAS fbpp_schema)
add_library(fbpp::fbpp_codegen ALIAS fbpp_codegen)
add_library(fbpp::fbpp_test_support ALIAS fbpp_test_support)

# Query generator tool
add_executable(query_generator
    src/tools/query_generator.cpp
)

target_link_libraries(query_generator PRIVATE
    fbpp_codegen
    nlohmann_json::nlohmann_json
)

fbpp_configure_cxx_target(query_generator)


# Исключаем примеры из сборки по умолчанию
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Build tests
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    enable_testing()
    
    # Basic infrastructure tests
    option(BUILD_BASIC_TESTS "Build basic infrastructure tests" ON)
    if(BUILD_BASIC_TESTS)
        add_subdirectory(tests)
    endif()
endif()

# Конфигурационные файлы (если нужны)
option(BUILD_CONFIG "Build config" OFF)
if(BUILD_CONFIG)
    add_subdirectory(config)
endif()

# Installation rules
include(GNUInstallDirs)

# Install exported libraries
install(TARGETS fbpp_core
    EXPORT fbppCoreTargets
    COMPONENT core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS fbpp_schema
    EXPORT fbppSchemaTargets
    COMPONENT schema
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS fbpp_codegen
    EXPORT fbppCodegenTargets
    COMPONENT codegen
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install public headers
install(DIRECTORY include/fbpp/core
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    COMPONENT core
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(FILES
    include/fbpp/fbpp.hpp
    include/fbpp/fbpp_extended.hpp
    include/fbpp/fbpp_all.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    COMPONENT core
)

install(DIRECTORY include/fbpp/adapters
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    COMPONENT core
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY include/fbpp/schema
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    COMPONENT schema
    FILES_MATCHING PATTERN "*.hpp"
)

install(FILES
    include/fbpp/query_generator_service.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    COMPONENT codegen
)

install(FILES
    include/fbpp_util/trace.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp_util
    COMPONENT core
)

# Export targets
install(EXPORT fbppCoreTargets
    FILE fbppCoreTargets.cmake
    NAMESPACE fbpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    COMPONENT core
)

install(EXPORT fbppSchemaTargets
    FILE fbppSchemaTargets.cmake
    NAMESPACE fbpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    COMPONENT schema
)

install(EXPORT fbppCodegenTargets
    FILE fbppCodegenTargets.cmake
    NAMESPACE fbpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    COMPONENT codegen
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindFirebird.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    COMPONENT core
)

# Create and install version file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Configure package config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fbppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR
)

# Install config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
    COMPONENT core
)
