cmake_minimum_required(VERSION 3.20)

foreach(required_var
        FBPP_PARENT_BINARY_DIR
        FBPP_CONSUMER_SOURCE_DIR
        FBPP_CONSUMER_BINARY_DIR
        FBPP_INSTALL_PREFIX
        FBPP_CTEST_COMMAND)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "Missing required variable: ${required_var}")
    endif()
endforeach()

file(REMOVE_RECURSE "${FBPP_CONSUMER_BINARY_DIR}" "${FBPP_INSTALL_PREFIX}")

set(_install_command
    "${CMAKE_COMMAND}" "--install" "${FBPP_PARENT_BINARY_DIR}" "--prefix" "${FBPP_INSTALL_PREFIX}"
)
if(DEFINED FBPP_PARENT_MULTI_CONFIG AND FBPP_PARENT_MULTI_CONFIG)
    if(DEFINED FBPP_PARENT_BUILD_TYPE AND NOT FBPP_PARENT_BUILD_TYPE STREQUAL "")
        list(APPEND _install_command "--config" "${FBPP_PARENT_BUILD_TYPE}")
    endif()
endif()

execute_process(
    COMMAND ${_install_command}
    RESULT_VARIABLE _install_result
    COMMAND_ECHO STDOUT
)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "fbpp install-tree smoke test failed during install step")
endif()

set(_configure_command
    "${CMAKE_COMMAND}"
    "-S" "${FBPP_CONSUMER_SOURCE_DIR}"
    "-B" "${FBPP_CONSUMER_BINARY_DIR}"
    "-DCMAKE_PREFIX_PATH=${FBPP_INSTALL_PREFIX}"
    "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON"
)

if(DEFINED FBPP_PARENT_GENERATOR AND NOT FBPP_PARENT_GENERATOR STREQUAL "")
    list(APPEND _configure_command "-G" "${FBPP_PARENT_GENERATOR}")
endif()
if(DEFINED FBPP_PARENT_GENERATOR_PLATFORM AND NOT FBPP_PARENT_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _configure_command "-A" "${FBPP_PARENT_GENERATOR_PLATFORM}")
endif()
if(DEFINED FBPP_PARENT_GENERATOR_TOOLSET AND NOT FBPP_PARENT_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _configure_command "-T" "${FBPP_PARENT_GENERATOR_TOOLSET}")
endif()
if(DEFINED FBPP_PARENT_TOOLCHAIN_FILE AND NOT FBPP_PARENT_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _configure_command "-DCMAKE_TOOLCHAIN_FILE=${FBPP_PARENT_TOOLCHAIN_FILE}")
endif()
if((NOT DEFINED FBPP_PARENT_MULTI_CONFIG OR NOT FBPP_PARENT_MULTI_CONFIG)
   AND DEFINED FBPP_PARENT_BUILD_TYPE
   AND NOT FBPP_PARENT_BUILD_TYPE STREQUAL "")
    list(APPEND _configure_command "-DCMAKE_BUILD_TYPE=${FBPP_PARENT_BUILD_TYPE}")
endif()

execute_process(
    COMMAND ${_configure_command}
    RESULT_VARIABLE _configure_result
    COMMAND_ECHO STDOUT
)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "fbpp install-tree smoke test failed during consumer configure step")
endif()

set(_build_command
    "${CMAKE_COMMAND}" "--build" "${FBPP_CONSUMER_BINARY_DIR}" "--target" "smoke"
)
if(DEFINED FBPP_PARENT_MULTI_CONFIG AND FBPP_PARENT_MULTI_CONFIG)
    if(DEFINED FBPP_PARENT_BUILD_TYPE AND NOT FBPP_PARENT_BUILD_TYPE STREQUAL "")
        list(APPEND _build_command "--config" "${FBPP_PARENT_BUILD_TYPE}")
    endif()
endif()

execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _build_result
    COMMAND_ECHO STDOUT
)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "fbpp install-tree smoke test failed during consumer build step")
endif()

set(_ctest_command
    "${FBPP_CTEST_COMMAND}" "--test-dir" "${FBPP_CONSUMER_BINARY_DIR}" "--output-on-failure"
)
if(DEFINED FBPP_PARENT_MULTI_CONFIG AND FBPP_PARENT_MULTI_CONFIG)
    if(DEFINED FBPP_PARENT_BUILD_TYPE AND NOT FBPP_PARENT_BUILD_TYPE STREQUAL "")
        list(APPEND _ctest_command "--build-config" "${FBPP_PARENT_BUILD_TYPE}")
    endif()
endif()

execute_process(
    COMMAND ${_ctest_command}
    RESULT_VARIABLE _ctest_result
    COMMAND_ECHO STDOUT
)
if(NOT _ctest_result EQUAL 0)
    message(FATAL_ERROR "fbpp install-tree smoke test failed while running consumer tests")
endif()
