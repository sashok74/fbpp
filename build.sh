#!/usr/bin/env bash
# Build script for fbpp project with proper Conan and CMake configuration
# Usage: ./scripts/build.sh [Debug|Release|RelWithDebInfo|MinSizeRel]

set -euo pipefail

# Default to RelWithDebInfo if not specified
BTYPE="${1:-RelWithDebInfo}"

echo "================================"
echo "Building fbpp with build type: ${BTYPE}"
echo "================================"

# Clean old build artifacts
echo "→ Cleaning build directory..."
rm -rf build
rm -rf CMakeUserPresets.json

# Install Conan dependencies with matching build type
echo "→ Installing Conan dependencies..."
conan install . --output-folder=build --build=missing -s build_type="${BTYPE}"

# The toolchain file location depends on conan version
TOOLCHAIN_FILE="build/build/${BTYPE}/generators/conan_toolchain.cmake"
if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    # Try alternate location
    TOOLCHAIN_FILE="build/conan_toolchain.cmake"
    if [ ! -f "${TOOLCHAIN_FILE}" ]; then
        echo "Error: Could not find conan_toolchain.cmake"
        exit 1
    fi
fi

echo "→ Using toolchain: ${TOOLCHAIN_FILE}"

# Configure CMake with the Conan toolchain file and matching build type
echo "→ Configuring CMake..."
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="${PWD}/${TOOLCHAIN_FILE}" \
  -DCMAKE_BUILD_TYPE="${BTYPE}" \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=ON \
  -DFBPP_BUILD_LIBS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  
# Build the project
echo "→ Building project..."
cmake --build build -j$(nproc)

# Run tests
echo "→ Running tests..."
ctest --test-dir build --output-on-failure || true

echo "================================"
echo "Build complete!"
echo "================================"