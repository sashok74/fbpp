# fbpp - Modern C++20 Firebird Wrapper

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Firebird](https://img.shields.io/badge/Firebird-5.x-orange.svg)](https://firebirdsql.org/)

Modern, type-safe C++20 wrapper for Firebird 5 database OO API with full support for extended types.

## Features

- ( **Full Firebird 5 Extended Types** - INT128, DECFLOAT(16/34), NUMERIC(38,x), TIMESTAMP WITH TIME ZONE
- <¯ **Type-Safe** - Compile-time type checking with C++20 templates
- =æ **Multiple Data Formats** - JSON, tuples, and strongly-typed objects
- ¡ **High Performance** - Statement caching and batch operations
- =' **Developer Friendly** - Named parameters, RAII resource management
- >ê **Well Tested** - Comprehensive test suite with 100+ tests

## Quick Start

### Installation

#### Using Conan (Recommended)

```bash
# Add to conanfile.txt
[requires]
fbpp/1.0.0

# Or via command line
conan install --requires=fbpp/1.0.0
```

#### From Source

```bash
git clone https://github.com/sashok74/fbpp.git
cd fbpp
./build.sh Release
sudo cmake --install build
```

### Basic Example

```cpp
#include <fbpp/fbpp.hpp>
#include <iostream>

int main() {
    using namespace fbpp::core;

    // Connect to database
    Connection conn(ConnectionParams{
        .database = "localhost:employee.fdb",
        .user = "SYSDBA",
        .password = "masterkey"
    });

    // Start transaction
    auto txn = conn.StartTransaction();

    // Prepare and execute query
    auto stmt = conn.prepareStatement(
        "SELECT emp_no, first_name, salary FROM employee WHERE dept_no = ?"
    );

    auto rs = stmt->executeQuery(txn, std::make_tuple("Sales"));

    // Fetch results as tuples
    while (auto row = rs->fetchOne<int, std::string, double>()) {
        auto [emp_no, name, salary] = *row;
        std::cout << name << ": $" << salary << std::endl;
    }

    txn->Commit();
    return 0;
}
```

### JSON Example

```cpp
#include <fbpp/fbpp.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Insert with JSON
json employee = {
    {"emp_no", 1001},
    {"first_name", "John"},
    {"last_name", "Doe"},
    {"salary", 75000.00}
};

auto stmt = conn.prepareStatement(
    "INSERT INTO employee (emp_no, first_name, last_name, salary) "
    "VALUES (?, ?, ?, ?)"
);
stmt->execute(txn, employee);

// Fetch as JSON
auto rs = stmt->executeQuery(txn);
while (auto row = rs->fetchOneJSON()) {
    std::cout << row.dump(2) << std::endl;
}
```

### Extended Types Example

```cpp
#include <fbpp/fbpp_extended.hpp>
#include <fbpp/adapters/ttmath_int128.hpp>
#include <fbpp/adapters/cppdecimal_decfloat.hpp>

using namespace fbpp::core;
using namespace fbpp::adapters;

// INT128 support
Int128 big_number = make_int128("170141183460469231731687303715884105727");

// DECFLOAT(34) support
dec::DecQuad precise("123456789012345678901234.567890123456789");

// NUMERIC(38,2) with scale
TTNumeric<2, -2> money;  // 38 digits, 2 decimal places

auto stmt = conn.prepareStatement(
    "INSERT INTO financial_data (id, big_num, precise_num, money) "
    "VALUES (?, ?, ?, ?)"
);
stmt->execute(txn, std::make_tuple(1, big_number, precise, money));
```

## Requirements

- **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022+)
- **Firebird 5.x** client library
- **CMake 3.20+**

### Dependencies (managed by Conan)

- GoogleTest 1.14+ (testing only)
- spdlog 1.12+
- nlohmann_json 3.11+

## Documentation

- [API Reference](https://sashok74.github.io/fbpp/) (coming soon)
- [User Guide](doc/USER_GUIDE.md)
- [Type System Guide](doc/firebird_types_guide.md)
- [Extended Types](doc/EXTENDED_TYPES_ADAPTERS.md)
- [Named Parameters](doc/NAMED_PARAMETERS.md)
- [Statement Caching](doc/PREPARED_STATEMENT_CACHE_SPEC.md)
- [Project Analysis](doc/PROJECT_ANALYSIS.md) - Detailed architecture and roadmap

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
| Linux (x64) |  Tested | Ubuntu 20.04+, Debian 11+ |
| Windows (x64) | =á Should work | Not regularly tested |
| macOS (x64/ARM) | =á Should work | Not regularly tested |

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

## Project Status

**Current Version:** 1.0.0-alpha

Active development. API may change before 1.0.0 stable release.

### Roadmap

- [x] Core API (Connection, Transaction, Statement, ResultSet)
- [x] Extended types support (INT128, DECFLOAT, NUMERIC(38,x))
- [x] JSON integration
- [x] Named parameters
- [x] Statement caching
- [x] Batch operations
- [x] Cancel operations
- [ ] Connection pool
- [ ] Async API (C++20 coroutines)
- [ ] BLOB streaming API
- [ ] Events API
- [ ] Services API (backup/restore)

See [PROJECT_ANALYSIS.md](doc/PROJECT_ANALYSIS.md) for detailed project analysis and roadmap.

---

**Note:** This is an alpha release. While the core functionality is stable and tested, the API may change before the 1.0.0 stable release. Production use is not recommended until version 1.0.0.
