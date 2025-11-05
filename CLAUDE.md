# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**fbpp** (Firebird Plus Plus) - Modern C++20 wrapper for Firebird 5 database OO API providing type-safe message packing/unpacking, RAII resource management, and comprehensive support for ALL Firebird extended types including INT128, DECFLOAT, and NUMERIC(38,x). Features include statement caching, named parameters, batch operations, and transaction templates.

## Critical Requirements
- Для сбора контекста  для классов, фунций, методов проекта исользуй mcp cpp-sitter
- **NO STUBS OR PLACEHOLDERS** - Every extended type must be fully implemented and tested against live Firebird 5 server
- **ALL extended types support** is mandatory: INT128, DECFLOAT(16/34), NUMERIC(38,x), TIMESTAMP/TIME WITH TIME ZONE
- **Live server testing** - Remote Firebird 5 at `firebird5:3050` with credentials `SYSDBA`/`planomer`
- **Test database**: `/mnt/test/fbpp_temp_test.fdb` (recreated per test)
- **Template-heavy design** - Minimize virtual functions, maximize compile-time optimizations
- **Multiple data formats**: JSON, tuple, and strongly-typed objects for parameters and results
- **Named parameters** - Full support for `:param_name` syntax in SQL queries

## Development Commands

### Building and Testing

**Primary build script** (recommended):
```bash
# Full clean build with Conan dependencies + tests
./build.sh RelWithDebInfo

# Other build types
./build.sh Debug
./build.sh Release
```

**Note**: `build.sh` performs:
1. Clean build directory
2. Install Conan dependencies
3. Configure CMake with Conan toolchain
4. Build entire project
5. **Run all tests automatically**

### Manual Building
```bash
# Quick rebuild after changes (without Conan reinstall)
cd build && cmake --build . -j$(nproc)

# Build specific target
cd build && cmake --build . --target test_statement
```

### Testing
```bash
# Run all tests (113 tests total)
cd build && ctest --output-on-failure

# Run specific test by name
cd build && ctest -R "CancelOperationTest" --output-on-failure

# Run single test directly with debug output
SPDLOG_LEVEL=debug ./build/tests/unit/test_cancel_operation

# Run adapter tests
./build/tests/adapters/test_ttmath_int128
./build/tests/adapters/test_cppdecimal_decfloat

# Debug a test with GDB
gdb ./build/tests/unit/test_statement
```

### Examples
```bash
# Basic operations and JSON
./build/examples/01_basic_operations
./build/examples/02_json_operations

# Batch operations
./build/examples/03_batch_simple
./build/examples/04_batch_advanced

# Named parameters
./build/examples/06_named_parameters

# Cancel operations
./build/examples/07_cancel_operation
./build/examples/07_cancel_test_variants
```

## Architecture

### Core Components (src/core/firebird/)

- **fb_connection.cpp** - Database attachment management with IAttachment interface, cancel operations
- **fb_transaction.cpp** - Transaction lifecycle with ITransaction interface
- **fb_statement.cpp** - Prepared statements with IStatement interface
- **fb_statement_cache.cpp** - Statement pooling and caching for performance
- **fb_result_set.cpp** - Cursor operations with IResultSet interface
- **fb_batch.cpp** - Batch operations with IBatch interface
- **fb_message_metadata.cpp** - Field metadata inspection and management
- **fb_message_builder.cpp** - Dynamic message buffer construction
- **fb_named_param_parser.cpp** - Named parameter parsing and substitution
- **fb_extended_types.cpp** - INT128, DECFLOAT utilities using IInt128/IDecFloat16/IDecFloat34
- **fb_exception.cpp** - Error handling wrapper for FbException

### Public Headers (include/fbpp/core/)

- **connection.hpp** - Database connection with statement caching
- **transaction.hpp** - Transaction management with templates
- **statement.hpp** - Prepared statement execution (tuple/JSON/object)
- **result_set.hpp** - Result iteration (tuple/JSON/object)
- **batch.hpp** - Batch operation support
- **statement_cache.hpp** - Statement caching interface
- **named_param_parser.hpp** - Named parameter handling
- **extended_types.hpp** - Int128, DecFloat16/34, TimestampTz, TimeTz classes
- **json_packer.hpp** - JSON → Firebird message buffer
- **json_unpacker.hpp** - Firebird message buffer → JSON
- **tuple_packer.hpp** - std::tuple → Firebird message buffer
- **tuple_unpacker.hpp** - Firebird message buffer → std::tuple
- **type_adapter.hpp** - Type adapter trait system
- **exception.hpp** - FirebirdException class

### Type System Mapping

| Firebird Type | SQL Type ID | C++ Type | Wire Size | Implementation |
|--------------|-------------|----------|-----------|----------------|
| SMALLINT | 500 | int16_t | 2 bytes | Native |
| INTEGER | 496 | int32_t | 4 bytes | Native |
| BIGINT | 580 | int64_t | 8 bytes | Native |
| INT128 | 32752 | Int128 (core) / ttmath::Int<4> (adapter) | 16 bytes | Core or TTMath |
| NUMERIC(18,x) | 496/580 | int64_t + scale | 4/8 bytes | Scaled integer |
| NUMERIC(38,x) | 32752 | Int128 + scale | 16 bytes | Core or TTMath |
| DECFLOAT(16) | 32760 | DecFloat16 (core) | 8 bytes | IBM decNumber |
| DECFLOAT(34) | 32762 | DecFloat34 (core) | 16 bytes | IBM decNumber |
| FLOAT | 482 | float | 4 bytes | Native |
| DOUBLE | 480 | double | 8 bytes | Native |
| CHAR(n) | 452 (SQL_TEXT) | std::string | n bytes | Fixed, space-padded |
| VARCHAR(n) | 448 (SQL_VARYING) | std::string | 2+n bytes | Variable length |
| DATE | 570 | uint32_t | 4 bytes | Days since 1858-11-17 |
| TIME | 560 | uint32_t | 4 bytes | 1/10000 sec units |
| TIMESTAMP | 510 | Timestamp{date,time} | 8 bytes | Composite |
| TIME_TZ | 32756 | TimeTz{time,zone,offset} | 8 bytes | With timezone |
| TIMESTAMP_TZ | 32754 | TimestampTz{ts,zone,offset} | 12 bytes | With timezone |
| BLOB | 520 | (not yet in main API) | varies | Binary/text LOB |

### Critical Type Handling Rules

#### NUMERIC Type Detection Priority
```cpp
case FieldType::Numeric:
    // 1. Check DECFLOAT types first (by wire_type)
    if (wire_type == SQL_DECFLOAT16) → use pack_decfloat16()
    if (wire_type == SQL_DECFLOAT34) → use pack_decfloat34()

    // 2. Check NUMERIC/DECIMAL with scale
    if (scale != 0) {
        if (precision > 18 || length == 16) → use pack_scaled_int128()
        else → use pack_scaled_int64()
    }

    // 3. Pure INT128 (no scale)
    if (wire_type == SQL_INT128 && scale == 0) → use pack_int128()
```

#### Scale Formula
```
Stored_value = Real_value × 10^(-scale)
Example: NUMERIC(38,2) value "123.45" → stored as INT128(12345), scale=-2
```

### Adapter System

Type adapters in `include/fbpp/adapters/`:
- **ttmath_int128.hpp** - INT128 support via TTMath
- **ttmath_numeric.hpp** - NUMERIC(38,x) with scale handling
- **cppdecimal_decfloat.hpp** - DECFLOAT types via IBM decNumber
- **chrono_datetime.hpp** - std::chrono integration for timestamps

### JSON Integration

JSON packing/unpacking in `include/fbpp/core/`:
- **json_packer.hpp** - Pack JSON values into Firebird message buffers
- **json_unpacker.hpp** - Unpack Firebird data to JSON
- Full support for all extended types including INT128, DECFLOAT, NUMERIC(38,x)

## Database Configuration

Configuration file: `config/test_config.json`

```json
{
  "db": {
    "server": "firebird5",
    "path": "/mnt/test/binding_test.fdb",
    "user": "SYSDBA",
    "password": "planomer",
    "charset": "UTF8"
  },
  "tests": {
    "temp_db": {
      "path": "/mnt/test/fbpp_temp_test.fdb",
      "server": "firebird5",
      "recreate_each_test": true
    }
  }
}
```

- **Server**: firebird5:3050 (remote)
- **Credentials**: SYSDBA / planomer
- **Character Set**: UTF8
- **Firebird API**: /opt/firebird/include/firebird/Interface.h
- **Client Library**: libfbclient.so

## Error Handling Pattern

```cpp
// At API boundaries - convert Firebird exceptions
try {
    // Firebird calls with ThrowStatusWrapper
} catch (const Firebird::FbException& e) {
    throw fbpp::core::FirebirdException(e);
}

// For precondition checks
if (!tra) {
    throw fbpp::core::FirebirdException("Null transaction");
}

// In error handlers
catch (const fbpp::core::FirebirdException& ex) {
    log.error("DB error: {}", ex.what());
    auto code = ex.getErrorCode();
    auto state = ex.getSQLState();
}
```

## Testing Requirements

**Total: 113 tests** covering all components

### Test Categories

1. **Adapter Tests** (tests/adapters/)
   - test_ttmath_int128.cpp - TTMath INT128 adapter
   - test_ttmath_scale.cpp - TTMath NUMERIC(38,x) with scale
   - test_cppdecimal_decfloat.cpp - CppDecimal DECFLOAT adapter

2. **Unit Tests** (tests/unit/)
   - test_statement.cpp - Statement execution
   - test_statement_cache.cpp - Statement caching
   - test_cancel_operation.cpp - Cancel operations (4 tests)
   - test_named_parameters.cpp - Named parameter parsing
   - test_config.cpp - Configuration loading
   - test_logging.cpp - Logging infrastructure
   - test_core_wrapper.cpp - Core wrappers
   - test_firebird_exception.cpp - Exception handling
   - test_fbclient_symbols.cpp - Firebird client library

### Extended Type Coverage Requirements

Every new feature test MUST cover ALL extended types:
- **INT128** - Pure 128-bit integer (no scale)
- **NUMERIC(38,2)** - INT128 with scale=-2
- **NUMERIC(38,38)** - Maximum scale
- **NUMERIC(18,4)** - INT64 with scale
- **DECFLOAT(16)** - IEEE 754-2008 decimal64
- **DECFLOAT(34)** - IEEE 754-2008 decimal128
- **TIMESTAMP WITH TIME ZONE** - TimestampTz
- **TIME WITH TIME ZONE** - TimeTz

## Include Path Convention

**ALWAYS use absolute paths from project roots:**
```cpp
// ✅ CORRECT
#include "fbpp/core/types.hpp"        // From include/
#include "core/firebird/fb_env.hpp"   // From src/

// ❌ WRONG - never use relative paths
#include "../types.hpp"
```

## Key Documentation

- **doc/FIREBIRD_TYPES_HANDLING.md** - Critical type handling rules (MUST READ)
- **doc/firebird_oo_api_doc.md** - Firebird OO API reference (121KB)
- **doc/FIREBIRD_BUFFER_AND_TYPES.md** - Buffer layouts and type mappings
- **doc/EXTENDED_TYPES_ADAPTERS.md** - Type adapter architecture (29KB)
- **doc/NAMED_PARAMETERS.md** - Named parameter implementation
- **doc/PREPARED_STATEMENT_CACHE_SPEC.md** - Statement caching specification
- **doc/TUPLE_PACKER_ARCHITECTURE.md** - Tuple packer design
- **doc/LIB_V1_ARCHITECTURE.md** - Library v1 architecture overview
- **doc/firebird_types_guide.md** - Type system guide
- **doc/examples/** - 13 Firebird OO API examples

## Dependencies

- **Firebird 5.x** client library (required)
- **C++20** compiler (GCC 11+, Clang 14+)
- **Conan 2** package manager
- **CMake 3.20+**
- **GoogleTest** (via Conan)
- **spdlog** (via Conan)
- **nlohmann_json** (via Conan)
- **TTMath** (vendored in third_party/)
- **CppDecimal** (vendored in third_party/)

## Project Structure

```
fbpp/
├── include/fbpp/              # Public headers
│   ├── core/                 # Core wrappers (25 headers)
│   │   ├── connection.hpp    # Connection management
│   │   ├── transaction.hpp   # Transaction wrapper
│   │   ├── statement.hpp     # Statement execution
│   │   ├── result_set.hpp    # Result iteration
│   │   ├── batch.hpp         # Batch operations
│   │   ├── extended_types.hpp # Int128, DecFloat, TimestampTz
│   │   ├── json_packer.hpp   # JSON → buffer
│   │   ├── json_unpacker.hpp # Buffer → JSON
│   │   ├── tuple_packer.hpp  # Tuple → buffer
│   │   ├── tuple_unpacker.hpp # Buffer → tuple
│   │   └── ...
│   ├── adapters/             # Type adapters (4 adapters)
│   │   ├── ttmath_int128.hpp     # TTMath INT128
│   │   ├── ttmath_numeric.hpp    # TTMath NUMERIC(38,x)
│   │   ├── cppdecimal_decfloat.hpp # CppDecimal DECFLOAT
│   │   └── chrono_datetime.hpp   # std::chrono datetime
│   ├── fbpp.hpp              # Main convenience header
│   ├── fbpp_extended.hpp     # Extended types header
│   └── fbpp_all.hpp          # All features header
├── src/
│   ├── core/firebird/        # Implementation (11 files)
│   └── util/                 # Utilities (3 files)
│       ├── logging.cpp       # spdlog integration
│       ├── config.cpp        # Configuration
│       └── config_loader.cpp # JSON config loading
├── tests/
│   ├── unit/                 # Unit tests (11 test suites)
│   ├── adapters/             # Adapter tests (3 test suites)
│   ├── test_base.hpp         # Test base class
│   └── CMakeLists.txt
├── examples/                 # Usage examples (10 examples)
├── doc/                      # Documentation (10 docs)
├── third_party/              # Vendored libraries
│   ├── ttmath/              # Big integer library
│   └── cppdecimal/          # IEEE decimal library
├── config/
│   └── test_config.json      # Test configuration
├── cmake/                    # CMake modules
├── build.sh                  # Build script
├── CMakeLists.txt            # Main CMake
└── conanfile.txt             # Conan dependencies
```

## Common Pitfalls

1. **NUMERIC type detection** - Always check in order: DECFLOAT → scaled NUMERIC → pure INT128
2. **Scale handling** - Firebird uses negative scale (scale=-2 means 2 decimal places)
3. **Epoch difference** - Firebird epoch is 1858-11-17, Unix is 1970-01-01 (40587 days difference)
4. **Wire types** - Check wire_type field for exact type identification
5. **Precision checks** - NUMERIC with precision > 18 requires INT128 storage (16 bytes)
6. **Statement caching** - Use Connection::prepare() or prepareReusable() for frequently executed queries
7. **Named parameters** - Use `:param_name` syntax, not `?` or `$1`
8. **CHAR vs VARCHAR** - CHAR (SQL_TEXT) is fixed-length, space-padded; VARCHAR (SQL_VARYING) has 2-byte length prefix
9. **NULL handling** - Always check null indicator byte in message buffers
10. **Cancel operations** - Enable with Connection::enableCancelOperations() before calling cancelOperation()

## Feature Highlights

### Statement Caching
```cpp
// Connection maintains a statement cache
auto stmt = conn.prepare("SELECT * FROM users WHERE id = ?");  // Cached
// Subsequent calls reuse cached statement
auto stmt2 = conn.prepare("SELECT * FROM users WHERE id = ?"); // Cache hit
```

### Named Parameters
```cpp
// Use :param_name syntax
auto stmt = conn.prepare("SELECT * FROM users WHERE name = :name AND age > :min_age");
stmt.executeQuery(txn, {{"name", "John"}, {"min_age", 18}});
```

### Multiple Data Formats
```cpp
// JSON format
nlohmann::json params = {{"id", 123}, {"name", "Alice"}};
stmt.execute(txn, params);

// Tuple format
auto result = stmt.executeQuery(txn).fetchOne<int, std::string, double>();

// Strongly-typed objects (via adapters)
User user = fetchObject<User>(result_set);
```

### Cancel Operations
```cpp
conn.enableCancelOperations();
// In another thread:
conn.cancelOperation(fb_cancel_raise);  // Interrupt long-running query
```

## Utilities for Code Analysis

- `rg` (ripgrep) - Fast code search
- `fd-find` + `fzf` - Interactive file search
- `clangd` - LSP server (compile_commands.json auto-generated)
- `bear` - Fallback for compile commands
- `valgrind` + `kcachegrind` - Memory and performance profiling
- `gdb` - Debugging with symbol information (RelWithDebInfo build)