# fbpp - Modern C++20 Firebird Wrapper

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Firebird](https://img.shields.io/badge/Firebird-5.x-orange.svg)](https://firebirdsql.org/)

Modern, type-safe C++20 wrapper for Firebird 5 database OO API with full support for extended types.

## Features

- **Full Firebird 5 Extended Types** - INT128, DECFLOAT(16/34), NUMERIC(38,x), TIME WITH TIME ZONE, TIMESTAMP WITH TIME ZONE
- **Type-Safe** - Compile-time type checking with C++20 templates
- **Multiple Data Formats** - JSON, tuples, and strongly-typed objects
- **High Performance** - Statement caching and batch operations
- **Developer Friendly** - Named parameters and RAII resource management
- **Well Tested** - Comprehensive automated test suite

## Quick Start

Use the canonical guide [doc/fbpp.md](doc/fbpp.md) for the current runtime model, type mapping rules, and query generator workflow.

For concrete up-to-date code, start with:

- `examples/01_basic_operations.cpp`
- `examples/02_json_operations.cpp`
- `examples/08_struct_fetch.cpp`
- `examples/09_extended_types_struct.cpp`

## Requirements

- **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022+)
- **Firebird 5.x** client library
- **CMake 3.20+**

### Dependencies (managed by Conan)

- GoogleTest 1.14+ (testing only)
- nlohmann_json 3.11+

## Documentation

- [Canonical Library Guide](doc/fbpp.md)
- [API Reference](https://sashok74.github.io/fbpp/)

## Examples

See [examples/](examples/) directory for more:

- `01_basic_operations.cpp` - CRUD operations with tuples
- `02_json_operations.cpp` - JSON parameter binding
- `03_batch_simple.cpp` - Batch operations
- `06_named_parameters.cpp` - Named parameter support
- `07_cancel_operation.cpp` - Cancel long-running queries

## Building from Source

```bash
# Install dependencies with Conan
conan install . --output-folder=build --build=missing -s build_type=Release

# Configure CMake
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=ON

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (x64) | Tested | Ubuntu 20.04+, Debian 11+ |
| Windows (x64) | Supported | Regularly validated in repository builds |
| macOS (x64/ARM) | Unverified | No regular validation in this repository |

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- Uses [TTMath](https://www.ttmath.org/) for INT128 support
- Uses [CppDecimal](https://github.com/vpiotr/decimal_for_cpp) for IEEE 754-2008 decimal arithmetic
- Inspired by modern C++ database libraries

## Support

- **Issues**: [GitHub Issues](https://github.com/sashok74/fbpp/issues)
- **Discussions**: [GitHub Discussions](https://github.com/sashok74/fbpp/discussions)
