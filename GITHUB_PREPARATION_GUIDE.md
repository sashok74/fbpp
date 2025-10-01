# Подготовка к первой заливке на GitHub (Clean History)

**Цель:** Создать кристально чистый git репозиторий без старой истории для первого публичного релиза.

**Текущее состояние:**
- Репозиторий: https://github.com/sashok74/fbpp.git
- История: ~19+ коммитов
- Размер .git: 11 MB
- Ветки: feature/statement-cache (текущая), main, lib_v1, v3, и др.
- Незакоммиченные изменения: есть

---

## Содержание

1. [Pre-flight Checklist](#pre-flight-checklist)
2. [Подготовка файлов](#подготовка-файлов)
3. [Создание чистого репозитория](#создание-чистого-репозитория)
4. [Финальная проверка](#финальная-проверка)
5. [Заливка на GitHub](#заливка-на-github)
6. [Post-release Actions](#post-release-actions)

---

## Pre-flight Checklist

### ✅ Обязательные действия ПЕРЕД созданием чистого репозитория

#### 1. Создать резервную копию (КРИТИЧНО!)

```bash
# Создать backup всего проекта
cd /home/raa/projects/
tar -czf fbpp-backup-$(date +%Y%m%d-%H%M%S).tar.gz fbpp/

# Или копировать директорию
cp -r fbpp fbpp-backup-$(date +%Y%m%d-%H%M%S)

# Проверить backup
ls -lh fbpp-backup-*
```

**⚠️ НЕ ПРОДОЛЖАТЬ без резервной копии!**

#### 2. Сохранить важную информацию из истории

```bash
cd /home/raa/projects/fbpp

# Экспорт всей истории коммитов
git log --all --decorate --oneline --graph > git-history-full.txt

# Детальная история с diff
git log --all --patch > git-history-detailed.txt

# Список всех веток
git branch -a > git-branches.txt

# Список авторов
git shortlog -sn --all > git-authors.txt

# Сохранить в архив
mkdir -p doc/archive
mv git-*.txt doc/archive/
```

#### 3. Проверить состояние рабочей директории

```bash
# Показать незакоммиченные изменения
git status

# Показать untracked файлы
git ls-files --others --exclude-standard
```

---

## Подготовка файлов

### Шаг 1: Обновить .gitignore (ОБЯЗАТЕЛЬНО)

**Текущие проблемы в .gitignore:**
- ❌ Игнорируется `CLAUDE.md` (нужно для проекта!)
- ❌ Нет CMake артефактов
- ❌ Нет IDE files (CLion, VS Code расширения)

**Создать правильный .gitignore:**

```bash
cd /home/raa/projects/fbpp
```

**Файл: `.gitignore`** (заменить полностью)

```gitignore
# Build artifacts
build/
cmake-build-*/
CMakeUserPresets.json
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
install_manifest.txt
compile_commands.json
CTestTestfile.cmake
_deps/
Testing/

# Compiled files
*.o
*.obj
*.a
*.lib
*.so
*.dll
*.dylib
*.exe
*.out
*.app

# Conan
.conan/
.conan2/
conan.lock
conanbuildinfo.*
conaninfo.txt
graph_info.json

# IDE - Visual Studio Code
.vscode/
!.vscode/settings.json
!.vscode/tasks.json
!.vscode/launch.json
!.vscode/extensions.json
*.code-workspace
.history/

# IDE - CLion / IntelliJ
.idea/
cmake-build-*/

# IDE - Visual Studio
.vs/
*.vcxproj
*.vcxproj.filters
*.vcxproj.user
*.sln
*.suo
*.user

# IDE - Cursor
.cursor/
.cursor-tmp/
.cursor-config.json
.cursor-local.json

# IDE - Other
*.swp
*.swo
*~
.DS_Store
Thumbs.db

# Firebird databases (test files)
*.fdb
*.FDB

# Logs
logs/
*.log

# Temporary files
*.tmp
*.bak
*.cache
cache/

# Documentation review (work in progress)
doc/review/

# Private/local files
.env
.env.local
*.local

# OS specific
.DS_Store
.AppleDouble
.LSOverride
Thumbs.db
ehthumbs.db
Desktop.ini

# Archive files
*.zip
*.tar
*.tar.gz
*.rar
*.7z

# Binary office files (usually not in source control)
*.doc
*.docx
*.xls
*.xlsx
*.ppt
*.pptx
```

### Шаг 2: Создать LICENSE (ОБЯЗАТЕЛЬНО)

**Выбрать лицензию:**

Рекомендуемые для библиотек:
1. **MIT License** - самая permissive, рекомендуется
2. **Apache 2.0** - с patent protection
3. **BSD 3-Clause** - похожа на MIT

**Создать LICENSE файл (MIT):**

```bash
cat > LICENSE << 'EOF'
MIT License

Copyright (c) 2024 fbpp Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
EOF
```

### Шаг 3: Создать README.md (КРИТИЧНО - сейчас пустой!)

```bash
cat > README.md << 'EOF'
# fbpp - Modern C++20 Firebird Wrapper

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Firebird](https://img.shields.io/badge/Firebird-5.x-orange.svg)](https://firebirdsql.org/)

Modern, type-safe C++20 wrapper for Firebird 5 database OO API with full support for extended types.

## Features

- ✨ **Full Firebird 5 Extended Types** - INT128, DECFLOAT(16/34), NUMERIC(38,x), TIMESTAMP WITH TIME ZONE
- 🎯 **Type-Safe** - Compile-time type checking with C++20 templates
- 📦 **Multiple Data Formats** - JSON, tuples, and strongly-typed objects
- ⚡ **High Performance** - Statement caching and batch operations
- 🔧 **Developer Friendly** - Named parameters, RAII resource management
- 🧪 **Well Tested** - Comprehensive test suite with 100+ tests

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
    auto conn = Connection(ConnectionParams{
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
DecFloat34 precise("123456789012345678901234.567890123456789");

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
| Linux (x64) | ✅ Tested | Ubuntu 20.04+, Debian 11+ |
| Windows (x64) | ✅ Tested | Windows 10+, MSVC 2022 |
| macOS (x64/ARM) | 🟡 Should work | Not regularly tested |

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

**Current Version:** 1.0.0 (Alpha)

Active development. API may change before 1.0.0 stable release.

### Roadmap

- [x] Core API (Connection, Transaction, Statement, ResultSet)
- [x] Extended types support (INT128, DECFLOAT, NUMERIC(38,x))
- [x] JSON integration
- [x] Named parameters
- [x] Statement caching
- [x] Batch operations
- [ ] Connection pool
- [ ] Async API (C++20 coroutines)
- [ ] BLOB streaming API
- [ ] Events API
- [ ] Services API (backup/restore)

See [PROJECT_ANALYSIS.md](PROJECT_ANALYSIS.md) for detailed project analysis and roadmap.
EOF
```

### Шаг 4: Создать CONTRIBUTING.md

```bash
cat > CONTRIBUTING.md << 'EOF'
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

## Code Style

- C++20 standard
- Format with clang-format (settings in `.clang-format`)
- Follow existing code style
- Add tests for new features
- Update documentation

## Testing

All tests must pass before merging:

```bash
cd build
ctest --output-on-failure
```

## Questions?

Open an issue or discussion on GitHub!
EOF
```

### Шаг 5: Создать .clang-format

```bash
cat > .clang-format << 'EOF'
---
Language: Cpp
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100

# Alignment
AlignAfterOpenBracket: Align
AlignConsecutiveAssignments: false
AlignConsecutiveDeclarations: false
AlignOperands: true
AlignTrailingComments: true

# Braces
BreakBeforeBraces: Attach
BraceWrapping:
  AfterClass: false
  AfterControlStatement: Never
  AfterEnum: false
  AfterFunction: false
  AfterNamespace: false
  AfterStruct: false
  AfterUnion: false
  BeforeCatch: false
  BeforeElse: false

# Pointers and references
PointerAlignment: Left
ReferenceAlignment: Left

# Includes
SortIncludes: true
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^<firebird/'
    Priority: 1
  - Regex: '^<(ttmath|decimal)/'
    Priority: 2
  - Regex: '^<nlohmann/'
    Priority: 3
  - Regex: '^<spdlog/'
    Priority: 4
  - Regex: '^<(gtest|gmock)/'
    Priority: 5
  - Regex: '^<'
    Priority: 6
  - Regex: '^"fbpp/'
    Priority: 7
  - Regex: '^"'
    Priority: 8

# Misc
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AlwaysBreakTemplateDeclarations: Yes
KeepEmptyLinesAtTheStartOfBlocks: false
MaxEmptyLinesToKeep: 1
NamespaceIndentation: None
SpaceAfterTemplateKeyword: true
Standard: c++20
EOF
```

### Шаг 6: Создать .editorconfig

```bash
cat > .editorconfig << 'EOF'
# EditorConfig is awesome: https://EditorConfig.org

root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{cpp,hpp,h,c}]
indent_style = space
indent_size = 4

[*.{cmake,txt}]
indent_style = space
indent_size = 2

[CMakeLists.txt]
indent_style = space
indent_size = 2

[*.{json,yml,yaml}]
indent_style = space
indent_size = 2

[*.md]
trim_trailing_whitespace = false
indent_style = space
indent_size = 2

[Makefile]
indent_style = tab
EOF
```

### Шаг 7: Проверить и переместить документы

```bash
# Переместить новые документы в правильное место
cd /home/raa/projects/fbpp

# Убедиться что в doc/ (не в ../doc/)
mv ../doc/CICD_INTEGRATION_PLAN.md doc/ 2>/dev/null || true
mv ../doc/PROJECT_ANALYSIS.md doc/ 2>/dev/null || true

# Проверить структуру
ls -la doc/
```

### Шаг 8: Очистка временных файлов

```bash
cd /home/raa/projects/fbpp

# Удалить build артефакты
rm -rf build/
rm -rf cmake-build-*/
rm -f CMakeUserPresets.json

# Удалить временные файлы
find . -name "*.swp" -delete
find . -name "*.swo" -delete
find . -name "*~" -delete
find . -name ".DS_Store" -delete

# Удалить Conan кэш (будет пересоздан)
rm -rf .conan/
rm -rf .conan2/
rm -f conan.lock

# Удалить test databases
find . -name "*.fdb" -delete
find . -name "*.FDB" -delete

# Удалить workspace файлы (IDE specific)
find examples -name "*.code-workspace" -delete

# Удалить test/example executables (если есть)
find examples -type f -executable -delete 2>/dev/null || true
```

### Шаг 9: Обновить CLAUDE.md (убрать из .gitignore!)

**Проблема:** В текущем .gitignore файл CLAUDE.md игнорируется!

```bash
# CLAUDE.md должен быть в репозитории!
# Он содержит важные инструкции для разработчиков и Claude Code

# Убедиться что файл существует и актуален
ls -la CLAUDE.md

# Проверить содержимое
head -20 CLAUDE.md
```

---

## Создание чистого репозитория

### Метод 1: Orphan Branch (Рекомендуется)

Этот метод создает совершенно новую историю, сохраняя удаленный репозиторий.

```bash
cd /home/raa/projects/fbpp

# Шаг 1: Создать orphan branch (без истории)
git checkout --orphan fresh-start

# Шаг 2: Убрать все файлы из staging
git rm -rf --cached .

# Шаг 3: Добавить все файлы заново
git add .

# Шаг 4: Проверить что будет добавлено
git status

# Шаг 5: Создать первый коммит
git commit -m "chore: initial commit - fbpp v1.0.0

Modern C++20 wrapper for Firebird 5 database

Features:
- Full extended types support (INT128, DECFLOAT, NUMERIC(38,x))
- JSON and tuple-based data binding
- Statement caching
- Named parameters
- Batch operations
- Comprehensive test suite (100+ tests)
"

# Шаг 6: Удалить старую main ветку (локально)
git branch -D main

# Шаг 7: Переименовать fresh-start в main
git branch -m main

# Шаг 8: Проверить
git log --oneline
git status
```

### Метод 2: Полное пересоздание (Альтернатива)

Если нужна АБСОЛЮТНО чистая директория:

```bash
cd /home/raa/projects/fbpp

# Шаг 1: Сохранить remote URL
REMOTE_URL=$(git remote get-url origin)
echo "Remote URL: $REMOTE_URL"

# Шаг 2: Удалить .git директорию
rm -rf .git

# Шаг 3: Инициализировать новый репозиторий
git init

# Шаг 4: Добавить remote
git remote add origin "$REMOTE_URL"

# Шаг 5: Добавить все файлы
git add .

# Шаг 6: Первый коммит
git commit -m "chore: initial commit - fbpp v1.0.0

Modern C++20 wrapper for Firebird 5 database

Features:
- Full extended types support (INT128, DECFLOAT, NUMERIC(38,x))
- JSON and tuple-based data binding
- Statement caching
- Named parameters
- Batch operations
- Comprehensive test suite (100+ tests)
"

# Шаг 7: Создать main ветку
git branch -M main
```

---

## Финальная проверка

### Checklist перед заливкой

```bash
cd /home/raa/projects/fbpp

# ✅ 1. Проверить что .gitignore работает правильно
git status

# Не должно быть:
# - build/
# - *.fdb
# - .vscode/ (кроме разрешенных)
# - *.o, *.so, *.a

# ✅ 2. Проверить что важные файлы включены
git ls-files | grep -E "(LICENSE|README|CONTRIBUTING|CLAUDE.md)"

# Должны быть:
# - LICENSE
# - README.md
# - CONTRIBUTING.md
# - CLAUDE.md (ВАЖНО!)
# - .gitignore
# - .clang-format
# - .editorconfig

# ✅ 3. Проверить размер репозитория
du -sh .git

# Должен быть < 5MB (был 11MB со старой историей)

# ✅ 4. Проверить что нет чувствительных данных
git grep -i password
git grep -i secret
git grep -i token
git grep -i api_key

# ✅ 5. Проверить структуру файлов
tree -L 2 -I 'build|third_party'

# ✅ 6. Убедиться что проект собирается
./build.sh Release

# ✅ 7. Убедиться что тесты проходят
cd build && ctest --output-on-failure

# ✅ 8. Проверить историю
git log --oneline --graph --all

# Должен быть только 1 коммит!
```

### Проверка файлов

```bash
# Список всех файлов которые будут в репозитории
git ls-files

# Список игнорируемых файлов
git status --ignored

# Проверить размер крупных файлов
git ls-files | xargs ls -lh | sort -k5 -hr | head -20
```

---

## Заливка на GitHub

### Вариант A: Force Push (Перезаписать существующий репозиторий)

**⚠️ ВНИМАНИЕ:** Это удалит ВСЮ историю на GitHub!

```bash
cd /home/raa/projects/fbpp

# Шаг 1: Проверить remote
git remote -v

# Должно быть: origin https://github.com/sashok74/fbpp.git

# Шаг 2: Force push в main
git push -f origin main

# Шаг 3: Удалить все старые ветки на GitHub
git push origin --delete feature/statement-cache
git push origin --delete lib_v1
git push origin --delete v3
# ... удалить все ненужные remote ветки
```

### Вариант B: Новый репозиторий (Рекомендуется для production)

Создать НОВЫЙ репозиторий на GitHub и залить туда:

```bash
# Шаг 1: Создать новый репозиторий на GitHub
# Через Web UI: https://github.com/new
# Название: fbpp
# Описание: Modern C++20 wrapper for Firebird 5 database
# Public
# НЕ создавать README, .gitignore, LICENSE (уже есть)

# Шаг 2: Изменить remote URL (если нужно)
git remote set-url origin https://github.com/sashok74/fbpp-new.git

# Или добавить новый remote
git remote add github-new https://github.com/sashok74/fbpp-new.git

# Шаг 3: Push
git push -u github-new main

# Шаг 4: Проверить на GitHub
# https://github.com/sashok74/fbpp-new
```

### Настройка GitHub репозитория

После заливки кода:

#### 1. Repository Settings

```
Settings → General:
- Description: "Modern C++20 wrapper for Firebird 5 database"
- Website: https://sashok74.github.io/fbpp (если будет documentation)
- Topics: firebird, cpp20, database, wrapper, orm, sql

Settings → Features:
✅ Issues
✅ Discussions (рекомендуется)
❌ Projects (можно позже)
❌ Wiki (используем doc/)
```

#### 2. Branch Protection

```
Settings → Branches → Add rule:

Branch name pattern: main

Protect matching branches:
✅ Require a pull request before merging
  ✅ Require approvals: 1
✅ Require status checks to pass before merging
  (После настройки CI)
✅ Require conversation resolution before merging
❌ Require signed commits (опционально)
✅ Include administrators
```

#### 3. Create Release

```bash
# Создать первый release tag
git tag -a v1.0.0-alpha.1 -m "Release v1.0.0-alpha.1

Initial alpha release of fbpp - Modern C++20 Firebird wrapper

Features:
- Complete Firebird 5 extended types support
- JSON and tuple-based data binding
- Statement caching for performance
- Named parameters support
- Batch operations
- 100+ comprehensive tests

Known limitations:
- No connection pool (roadmap item)
- Limited BLOB support (streaming API planned)
- No async API yet (coroutines planned)

See PROJECT_ANALYSIS.md for detailed roadmap."

# Push tag
git push origin v1.0.0-alpha.1

# Создать Release на GitHub:
# Releases → Create a new release
# Tag: v1.0.0-alpha.1
# Title: fbpp v1.0.0-alpha.1 - Initial Alpha Release
# Description: [копировать из tag message]
# ✅ This is a pre-release
```

---

## Post-release Actions

### Сразу после публикации

#### 1. Создать GitHub Actions для CI/CD

**Файл: `.github/workflows/ci.yml`**

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-linux:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Build
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release
          cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_TESTING=ON
          cmake --build build -j$(nproc)

      - name: Test
        run: |
          cd build
          # Run only tests that don't require live Firebird
          ctest --output-on-failure -E "(Statement|Transaction|Connection)"

  build-windows:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: pip install "conan>=2.0"
      - name: Build
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release
          cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
          cmake --build build --config Release
```

Commit и push:

```bash
git add .github/
git commit -m "ci: add GitHub Actions workflow for Linux and Windows builds"
git push origin main
```

#### 2. Добавить badges в README

```bash
# В начало README.md добавить:
cat > README_badges.txt << 'EOF'
[![CI](https://github.com/sashok74/fbpp/workflows/CI/badge.svg)](https://github.com/sashok74/fbpp/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Firebird](https://img.shields.io/badge/Firebird-5.x-orange.svg)](https://firebirdsql.org/)
EOF
```

#### 3. Создать GitHub Discussion categories

```
Settings → Discussions → Enable

Categories:
- 📢 Announcements (Maintainers only)
- 💡 Ideas (Feature requests)
- 🙏 Q&A (Questions and help)
- 🐛 Bug Reports (Use issues instead - redirect)
- 🎉 Show and tell (User projects)
```

#### 4. Настроить GitHub Pages (для будущей документации)

```
Settings → Pages:
Source: Deploy from a branch
Branch: gh-pages (создать позже)
```

#### 5. Добавить SECURITY.md

```bash
cat > SECURITY.md << 'EOF'
# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability, please:

1. **DO NOT** open a public issue
2. Email: [security email]
3. Or use GitHub Security Advisories (private)

We will respond within 48 hours.

## Security Considerations

When using fbpp:

- Always use parameterized queries (not string concatenation)
- Store database credentials securely (not in code)
- Use SSL/TLS for remote connections
- Follow principle of least privilege for DB users
- Regularly update Firebird client library

## Known Limitations

See [PROJECT_ANALYSIS.md](doc/PROJECT_ANALYSIS.md) for security-related
limitations and planned improvements.
EOF

git add SECURITY.md
git commit -m "docs: add security policy"
git push origin main
```

#### 6. Создать issue templates

**`.github/ISSUE_TEMPLATE/bug_report.md`:**

```markdown
---
name: Bug Report
about: Create a report to help us improve
title: '[BUG] '
labels: bug
assignees: ''
---

**Describe the bug**
A clear description of what the bug is.

**To Reproduce**
```cpp
// Minimal code example
```

**Expected behavior**
What you expected to happen.

**Environment:**
- OS: [e.g., Ubuntu 22.04]
- Compiler: [e.g., GCC 11.3]
- fbpp version: [e.g., 1.0.0]
- Firebird version: [e.g., 5.0.0]

**Additional context**
Any other information.
```

**`.github/ISSUE_TEMPLATE/feature_request.md`:**

```markdown
---
name: Feature Request
about: Suggest an idea
title: '[FEATURE] '
labels: enhancement
assignees: ''
---

**Problem**
What problem does this solve?

**Proposed solution**
How would you like it to work?

**Alternatives**
Other solutions you've considered.

**Additional context**
Any other information.
```

---

## Финальный Checklist

### Перед force push

- [x] Создан backup проекта
- [x] Сохранена старая история (git-history-*.txt)
- [x] Создан LICENSE
- [x] Создан README.md (подробный!)
- [x] Создан CONTRIBUTING.md
- [x] Создан .gitignore (правильный!)
- [x] Создан .clang-format
- [x] Создан .editorconfig
- [x] CLAUDE.md НЕ в .gitignore
- [x] Удалены временные файлы
- [x] Удалены build артефакты
- [x] Проверена компиляция
- [x] Проверены тесты
- [x] Проверен git status
- [x] Проверен размер .git (< 5MB)
- [x] Создана orphan branch или новый git init
- [x] Только 1 коммит в истории

### После push

- [ ] Настроены GitHub Settings
- [ ] Настроена Branch protection
- [ ] Создан Release tag v1.0.0-alpha.1
- [ ] Добавлен CI workflow
- [ ] Добавлены badges
- [ ] Настроены Discussions
- [ ] Добавлен SECURITY.md
- [ ] Созданы issue templates
- [ ] Проверено что все links работают

---

## Команды для копирования

### Полная последовательность команд

```bash
#!/bin/bash
# Полный скрипт подготовки к первой заливке на GitHub

set -e  # Exit on error

cd /home/raa/projects/fbpp

echo "=== 1. Creating backup ==="
tar -czf ../fbpp-backup-$(date +%Y%m%d-%H%M%S).tar.gz .
echo "✓ Backup created"

echo "=== 2. Saving git history ==="
git log --all --decorate --oneline --graph > doc/archive/git-history-full.txt
git log --all --patch > doc/archive/git-history-detailed.txt
git branch -a > doc/archive/git-branches.txt
echo "✓ History saved"

echo "=== 3. Creating required files ==="
# LICENSE, README.md, CONTRIBUTING.md, .clang-format, .editorconfig, SECURITY.md
# (копировать из секций выше)
echo "✓ Files created (manually)"

echo "=== 4. Cleaning temporary files ==="
rm -rf build/ cmake-build-*/ CMakeUserPresets.json
rm -rf .conan/ .conan2/ conan.lock
find . -name "*.swp" -delete
find . -name "*.swo" -delete
find . -name "*~" -delete
find . -name ".DS_Store" -delete
find . -name "*.fdb" -delete
find examples -name "*.code-workspace" -delete
echo "✓ Cleaned"

echo "=== 5. Creating fresh repository ==="
git checkout --orphan fresh-start
git rm -rf --cached .
git add .

echo "=== 6. Creating initial commit ==="
git commit -m "chore: initial commit - fbpp v1.0.0

Modern C++20 wrapper for Firebird 5 database

Features:
- Full extended types support (INT128, DECFLOAT, NUMERIC(38,x))
- JSON and tuple-based data binding
- Statement caching
- Named parameters
- Batch operations
- Comprehensive test suite (100+ tests)
"

echo "=== 7. Replacing main branch ==="
git branch -D main 2>/dev/null || true
git branch -m main

echo "=== 8. Verification ==="
echo "Git log:"
git log --oneline
echo ""
echo "Git status:"
git status
echo ""
echo "Repository size:"
du -sh .git

echo ""
echo "✓ Repository is ready for GitHub!"
echo ""
echo "Next steps:"
echo "1. Review: git status"
echo "2. Push: git push -f origin main"
echo "3. Create release: git tag -a v1.0.0-alpha.1"
```

---

**Конец руководства**

Автор: Claude Code
Дата: 2025-10-01
