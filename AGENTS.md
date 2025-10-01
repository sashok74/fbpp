# Repository Guidelines

## Project Structure & Module Organization
Core sources live under `src/core/firebird` and `src/util`; companion public headers are in `include/fbpp` and `include/fbpp_util` and must stay in sync. Tests reside in `tests/unit` for library behaviour and `tests/adapters` for type adapters, with shared fixtures in `tests/test_base.hpp`. Supporting assets include `config/` for sample JSON configs, `scripts/` for developer utilities, and `doc/` plus `examples/` for reference material. Keep generated build output in `build/` only; anything else should be tracked or ignored explicitly.

## Build, Test, and Development Commands
Run `./build.sh RelWithDebInfo` for a clean Conan + CMake build (Debug, Release, and MinSizeRel are also valid). For incremental work, call `conan install . --output-folder=build --build=missing -s build_type=Debug` followed by `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake` and `cmake --build build -j$(nproc)`. Execute `ctest --test-dir build --output-on-failure` or `scripts/run_tests.sh` for a full configure-build-test cycle. Examples can be enabled with `-DBUILD_EXAMPLES=ON`, while configs and docs remain opt-in via their respective CMake options.

## Coding Style & Naming Conventions
All code targets C++20 with warnings-as-errors handled per target; prefer standard library facilities before third-party helpers. Use four-space indentation, `PascalCase` for types, `camelCase` for methods/functions, and `snake_case` for file names (`test_statement_cache.cpp`). Headers should rely on `#pragma once`; prefer scoped namespaces (`fbpp::core`). Run `clang-tidy` with the provided `.clang-tidy` checks (`misc-unused-using-decls`, `readability-redundant-include`) before submitting.

## Testing Guidelines
Unit tests use GoogleTest; add new cases under `tests/unit/test_<feature>.cpp` and adapter tests under `tests/adapters`. Database-dependent tests should inherit from `fbpp::test::TempDatabaseTest` or `PersistentDatabaseTest` to avoid duplication. Ensure new behaviour is covered, and document any gaps in your pull request. When touching build logic, run the suite in both Debug and RelWithDebInfo to surface toolchain issues.

## Commit & Pull Request Guidelines
Follow the conventional commit prefixes seen in history (`feat`, `fix`, `refactor`, `chore`, etc.) with imperative summaries. Commits should remain focused: configuration updates, library changes, and tests belong in separate commits when possible. Pull requests need a clear description of the change, testing evidence (command output or reasoning), and links to tracking issues. Add screenshots or log excerpts when the change affects diagnostics or observable behaviour.

## Configuration & Environment
Copy `config/config.json.template` to `config/test_config.json` when wiring up a local Firebird instance; keep credentials out of version control. Runtime and test behaviour can also be tweaked via `FBLAB_*` environment variables (`FBLAB_DB_PATH`, `FBLAB_LOG_LEVEL`, cache toggles). Store large or transient outputs under `logs/`—the directory is ignored and safe for local experimentation.
