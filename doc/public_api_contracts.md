# fbpp Public API Contracts

## Supported public entry points

Stable umbrella headers:

- `<fbpp/fbpp.hpp>`: minimal runtime API
- `<fbpp/fbpp_extended.hpp>`: minimal runtime API plus extended Firebird value wrappers
- `<fbpp/fbpp_all.hpp>`: convenience umbrella with adapters, batch helpers, and packing utilities

Stable advanced headers:

- `<fbpp/core/*.hpp>` for low-level runtime building blocks
- `<fbpp/adapters/*.hpp>` for opt-in adapter types
- `<fbpp/query_generator_service.hpp>` for the `fbpp_codegen` layer
- `<fbpp_util/trace.h>` for process-wide runtime tracing

Not stable as direct entry points:

- `<fbpp/core/detail/*.hpp>`
- implementation helpers such as `*_impl.hpp`
- generated headers produced by tests or examples

## Packaging contract

Installed package components:

- `core`: runtime library and supported public runtime headers
- `codegen`: query generator service layered on top of `core`

Expected consumer usage:

```cmake
find_package(fbpp CONFIG REQUIRED COMPONENTS core)
target_link_libraries(app PRIVATE fbpp::fbpp_core)
```

```cmake
find_package(fbpp CONFIG REQUIRED COMPONENTS codegen)
target_link_libraries(app PRIVATE fbpp::fbpp_codegen)
```

Compatibility target:

- `fbpp::fbpp` remains an alias to `fbpp::fbpp_core`

## Thread-safety contract

- `fbpp::core::Environment` is a process-wide immutable singleton. Initialization is thread-safe; access after initialization is read-only and may be concurrent.
- `fbpp::core::Connection` is thread-affine. Do not use one connection concurrently from multiple threads.
- `fbpp::core::Transaction`, `fbpp::core::Statement`, and `fbpp::core::ResultSet` inherit the same thread-affinity as their owning connection.
- `fbpp::core::StatementCache` protects cache bookkeeping internally, but cached statements are still bound to their owning connection and are not safe for concurrent execution.

## Trace contract

- `fbpp::util::setTraceSink()` installs a process-wide trace sink for fbpp diagnostics.
- Sink publication is atomic.
- Sink implementations must be safe for concurrent `log()` calls if the process uses fbpp objects from multiple threads.
- Per-connection tracing is not part of the current API contract.

## Error contract

`fbpp::core::FirebirdException` exposes the stable fields intended for integrators:

- `what()`: formatted human-readable message
- `getErrorCode()`: first Firebird GDS code when present
- `getSQLState()`: SQLSTATE or `HY000` fallback
- `getSQLCode()`: SQLCODE derived from the original Firebird status vector
- `getErrorMessages()`: parsed message chain
- `getStatusVector()`: copied snapshot of the original Firebird status vector in stable C++ form
