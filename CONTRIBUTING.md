# Contributing to fbpp

Thank you for your interest in contributing to fbpp!

## How to Contribute

1. **Fork** the repository
2. **Create** a feature branch (`git checkout -b feature/amazing-feature`)
3. **Commit** your changes (`git commit -m 'feat: add amazing feature'`)
4. **Push** to the branch (`git push origin feature/amazing-feature`)
5. **Open** a Pull Request

## Commit Message Convention

We use [Conventional Commits](https://www.conventionalcommits.org/):

- `feat:` - New feature
- `fix:` - Bug fix
- `docs:` - Documentation changes
- `test:` - Adding or updating tests
- `refactor:` - Code refactoring
- `perf:` - Performance improvements
- `chore:` - Maintenance tasks
- `ci:` - CI/CD changes

### Examples

```
feat: add connection pool support
fix: correct INT128 scale handling in JSON packer
docs: update README with new examples
test: add DECFLOAT conversion tests
refactor: simplify statement cache eviction logic
```

## Code Style

- **C++20 standard** - Use modern C++ features
- **Format with clang-format** - Settings in `.clang-format`
- **Follow existing code style** - Consistency is key
- **Add tests for new features** - Maintain high test coverage
- **Update documentation** - Keep docs in sync with code

### Code Guidelines

- Use `snake_case` for variables and functions
- Use `PascalCase` for classes and types
- Use `UPPER_CASE` for constants
- Prefer `auto` when type is obvious
- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Avoid raw pointers for ownership
- Use RAII for resource management
- Document public API with Doxygen-style comments

### Example

```cpp
/// @brief Execute a parameterized query
/// @tparam Params Parameter types (tuple, json, etc.)
/// @param transaction Active transaction
/// @param params Query parameters
/// @return Number of affected rows
template<typename Params>
unsigned execute(Transaction* transaction, const Params& params);
```

## Testing

All tests must pass before merging:

```bash
cd build
ctest --output-on-failure
```

### Writing Tests

- Use GoogleTest framework
- Test both success and failure cases
- Test edge cases (NULL values, max values, etc.)
- Test all supported types for extended type features
- Use descriptive test names

```cpp
TEST_F(StatementTest, ExecuteWithNullParameters) {
    // Test implementation
}
```

## Documentation

- Update relevant documentation in `doc/` directory
- Add examples for new features
- Update `CLAUDE.md` if adding new patterns
- Keep `PROJECT_ANALYSIS.md` roadmap in sync

## Pull Request Process

1. **Update tests** - Add tests for new functionality
2. **Update docs** - Document new features
3. **Pass CI** - All checks must pass
4. **Code review** - Address reviewer feedback
5. **Squash commits** - Keep history clean

### PR Title Format

Follow conventional commits:

```
feat: add connection pool with configurable size
fix: resolve memory leak in statement cache
docs: add batch operations tutorial
```

### PR Description Template

```markdown
## Description
Brief description of changes

## Changes
- Change 1
- Change 2

## Testing
How was this tested?

## Breaking Changes
Any breaking changes? (yes/no)

## Related Issues
Closes #123
```

## Development Setup

### Prerequisites

- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- Conan 2.0+
- Firebird 5.x client library

### Build

```bash
# Clone
git clone https://github.com/sashok74/fbpp.git
cd fbpp

# Build
./build.sh Release

# Run tests
cd build && ctest --output-on-failure
```

### IDE Setup

**VS Code:**
- Install C++ extension
- Install clangd for code completion
- Use provided `.vscode/settings.json` (if available)

**CLion:**
- Open as CMake project
- Set C++20 in CMake settings
- Enable clang-tidy

## Reporting Bugs

Use the [issue tracker](https://github.com/sashok74/fbpp/issues) with:

- Clear title
- Steps to reproduce
- Expected vs actual behavior
- Environment details (OS, compiler, Firebird version)
- Minimal code example

## Feature Requests

Use [GitHub Discussions](https://github.com/sashok74/fbpp/discussions) for:

- New feature ideas
- API design discussions
- Architecture improvements

## Code of Conduct

- Be respectful and inclusive
- Welcome newcomers
- Focus on constructive feedback
- Follow GitHub's Community Guidelines

## Questions?

- **General questions**: [GitHub Discussions](https://github.com/sashok74/fbpp/discussions)
- **Bug reports**: [GitHub Issues](https://github.com/sashok74/fbpp/issues)
- **Security issues**: See [SECURITY.md](SECURITY.md)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
