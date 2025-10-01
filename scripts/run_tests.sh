#!/bin/bash

set -e

echo "Building Firebird Binding Lab..."

# Install dependencies with Conan
if [ ! -d "build" ]; then
    echo "Installing dependencies with Conan..."
    conan install . --output-folder=build --build=missing
fi

# Configure with CMake
echo "Configuring with CMake..."
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake

# Build
echo "Building..."
cmake --build build -j$(nproc)

# Run tests
echo "Running tests..."
cd build
ctest -V

echo "All tests completed!"