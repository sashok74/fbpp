# CppDecimal library - IBM decNumber wrapper for IEEE 754-2008 decimal
add_library(cppdecimal STATIC
    # Base decNumber C sources (order matters!)
    src/decContext.c
    src/decimal32.c
    src/decimal64.c
    src/decimal128.c
    src/decNumber.c
    src/decPacked.c
    src/decSingle.c
    src/decDouble.c
    src/decQuad.c
    # CppDecimal C++ wrappers
    src/DecContext.cc
    src/DecNumber.cc
    src/DecSingle.cc
    src/DecDouble.cc
    src/DecQuad.cc
    src/DecPacked.cc
)

target_include_directories(cppdecimal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Required definitions for decNumber
target_compile_definitions(cppdecimal PRIVATE
    DECNUMDIGITS=34  # For decimal128
)

# C++17 minimum for CppDecimal
target_compile_features(cppdecimal PUBLIC cxx_std_17)

# Install headers
install(DIRECTORY include/
    DESTINATION include/cppdecimal
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hh"
)

# Install main decimal header
install(FILES include/decimal
    DESTINATION include/cppdecimal
)

# Install library
install(TARGETS cppdecimal
    EXPORT cppdecimalTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install license
install(FILES LICENSE
    DESTINATION share/licenses/cppdecimal
)