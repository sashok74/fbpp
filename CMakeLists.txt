cmake_minimum_required(VERSION 3.20)
project(firebird-binding-lab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build)

# Find Conan packages
find_package(GTest REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# Include GoogleTest module for gtest_discover_tests
include(GoogleTest)

include(cmake/FindFirebird.cmake)
find_package(Firebird REQUIRED)
message(STATUS "Firebird include dir: ${FIREBIRD_INCLUDE_DIRS}")
message(STATUS "Firebird library: ${FIREBIRD_LIBRARIES}")

# Add vendored libraries
add_subdirectory(third_party/ttmath)
add_subdirectory(third_party/cppdecimal)


# Main fbpp library
add_library(fbpp STATIC
    # Core components
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

    # Utilities
    src/util/logging.cpp
    src/util/config.cpp
    src/util/config_loader.cpp
)

# Configure fbpp library
target_include_directories(fbpp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Add Firebird include directories
# On Ubuntu/Debian, Firebird headers are in /usr/include/firebird/
message(STATUS "FIREBIRD_INCLUDE_DIRS: ${FIREBIRD_INCLUDE_DIRS}")

# CMake by default filters out /usr/include as it's a system directory
# We need to work around this by checking if it's really /usr/include and handle it
if(FIREBIRD_INCLUDE_DIRS STREQUAL "/usr/include")
    # If Firebird is in /usr/include, headers are in /usr/include/firebird/
    # We need to make sure this gets added. Since CMake filters /usr/include,
    # we'll use target_compile_options to add it explicitly
    target_compile_options(fbpp PUBLIC -I/usr/include)
    message(STATUS "Added /usr/include explicitly via compile options")
else()
    # For non-standard locations, use normal include directories
    target_include_directories(fbpp PUBLIC ${FIREBIRD_INCLUDE_DIRS})
    message(STATUS "Added Firebird include directories: ${FIREBIRD_INCLUDE_DIRS}")
endif()

target_link_libraries(fbpp
    PUBLIC
        ${FIREBIRD_LIBRARIES}
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ttmath
        cppdecimal
)

target_compile_features(fbpp PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(fbpp PRIVATE /wd4100)
else()
    target_compile_options(fbpp PRIVATE -Wno-unused-parameter)
endif()

# Create alias for modern CMake usage
add_library(fbpp::fbpp ALIAS fbpp)


# Исключаем примеры из сборки по умолчанию
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Build tests
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
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

# Install fbpp library and dependencies
install(TARGETS fbpp ttmath cppdecimal
    EXPORT fbppTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/fbpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# Install main convenience headers
install(FILES
    include/fbpp/fbpp.hpp
    include/fbpp/fbpp_extended.hpp
    include/fbpp/fbpp_all.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
)

# Install adapters headers
install(DIRECTORY include/fbpp/adapters
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fbpp
    FILES_MATCHING PATTERN "*.hpp"
)

# Export targets
install(EXPORT fbppTargets
    FILE fbppTargets.cmake
    NAMESPACE fbpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
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
)

# Install config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fbppConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fbpp
)
