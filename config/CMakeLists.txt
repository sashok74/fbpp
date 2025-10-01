# Configuration files handling
# Copy test configuration to build directory for tests
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.json
    ${CMAKE_BINARY_DIR}/test_config.json
    COPYONLY
)

# Also copy to tests subdirectory where ctest runs
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.json
    ${CMAKE_BINARY_DIR}/tests/test_config.json
    COPYONLY
)

# Copy production config template (without sensitive data)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json.template
    ${CMAKE_BINARY_DIR}/config.json.template
    COPYONLY
)

# Install config template to installation directory
install(FILES config.json.template
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/fbpp
    RENAME config.json.example
)