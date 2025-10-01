# TTMath header-only library
add_library(ttmath INTERFACE)

target_include_directories(ttmath INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Define TTMATH_NOASM for pure header-only usage (no assembly optimizations)
target_compile_definitions(ttmath INTERFACE TTMATH_NOASM)

# Set minimum C++ standard
target_compile_features(ttmath INTERFACE cxx_std_17)

# Install headers
install(DIRECTORY ttmath/
    DESTINATION include/ttmath
    FILES_MATCHING PATTERN "*.h"
)

# Install license
install(FILES LICENSE
    DESTINATION share/licenses/ttmath
)