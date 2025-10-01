# План интеграции CI/CD для проекта fbpp

## Оглавление
1. [Преимущества CI/CD для fbpp](#преимущества-cicd-для-fbpp)
2. [Элементы CI/CD по сложности внедрения](#элементы-cicd-по-сложности-внедрения)
3. [Этапы внедрения CI/CD](#этапы-внедрения-cicd)

---

## Преимущества CI/CD для fbpp

### Основные выгоды

**1. Кроссплатформенная надежность**
- Автоматическое тестирование на Windows и Linux при каждом коммите
- Гарантия совместимости с разными версиями компиляторов (GCC, Clang, MSVC)
- Выявление платформозависимых проблем до релиза

**2. Качество кода библиотеки**
- Запуск 113 unit-тестов при каждом изменении (все extended types)
- Проверка корректности работы с Firebird 5 extended types (INT128, DECFLOAT, NUMERIC(38,x))
- Предотвращение регрессий в критичных компонентах (statement cache, named parameters, batch ops)

**3. Упрощение распространения через Conan**
- Автоматическая сборка и публикация пакетов в Conan Center или приватный репозиторий
- Контроль версий и зависимостей
- Возможность для пользователей устанавливать fbpp одной командой: `conan install fbpp/1.0.0@`

**4. Профессиональный имидж проекта**
- Бейджи статуса сборки в README
- Автоматические release notes
- Документация API (doxygen) обновляется автоматически

**5. Интеграция с Claude Code**
- Автоматическое тестирование изменений, сделанных Claude Code агентами
- Feedback loop: Claude получает результаты тестов и может исправлять ошибки
- Интеграция с AGENTS.md workflow

### Максимальная польза извлекается из:

1. **Matrix builds** - одновременная проверка на всех целевых платформах (Linux/Windows × GCC/Clang/MSVC)
2. **Automated testing** - критично для template-heavy library с расширенными типами
3. **Conan packaging** - упрощает использование библиотеки в других проектах
4. **Code coverage** - выявляет непротестированные участки кода
5. **Security scanning** - важно для библиотеки работающей с БД

---

## Элементы CI/CD по сложности внедрения

### Уровень 1: Базовые элементы (1-2 дня)

#### 1.1. Basic Linux Build & Test
**Сложность**: ⭐ (простая)
**Польза**: ⭐⭐⭐⭐⭐ (критичная)

Базовая сборка и тестирование на Linux с GCC.

**Что дает:**
- Немедленное обнаружение ошибок сборки
- Проверка прохождения всех 113 тестов
- Основа для всех остальных CI/CD процессов

**Требует:**
- GitHub Actions workflow
- Docker контейнер с Firebird 5

#### 1.2. Code Formatting Check
**Сложность**: ⭐ (простая)
**Польза**: ⭐⭐⭐ (средняя)

Проверка стиля кода (clang-format).

**Что дает:**
- Единообразный стиль кода
- Уменьшение шума в diff при code review
- Автоматическое форматирование в PR

**Требует:**
- .clang-format конфигурация
- GitHub Action для проверки

#### 1.3. Static Analysis (clang-tidy)
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Статический анализ кода на потенциальные ошибки.

**Что дает:**
- Обнаружение memory leaks, undefined behavior
- Проверка современных C++20 best practices
- Предотвращение распространенных ошибок

**Требует:**
- .clang-tidy конфигурация
- Интеграция в workflow

### Уровень 2: Кроссплатформенность (3-5 дней)

#### 2.1. Windows Build & Test (MSVC)
**Сложность**: ⭐⭐⭐ (средняя-высокая)
**Польза**: ⭐⭐⭐⭐⭐ (критичная)

Сборка и тестирование на Windows с MSVC.

**Что дает:**
- Гарантия работы библиотеки на Windows
- Выявление platform-specific проблем
- Проверка работы с Firebird ODBC на Windows

**Требует:**
- Windows runner в GitHub Actions
- Firebird 5 установка на Windows
- Адаптация путей и тестов

#### 2.2. Multi-compiler Matrix
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Тестирование с разными компиляторами (GCC 11/12/13, Clang 14/15/16, MSVC 2022).

**Что дает:**
- Совместимость с различными компиляторами
- Выявление compiler-specific проблем
- Проверка соответствия C++20 стандарту

**Требует:**
- Matrix strategy в workflow
- Docker образы с разными компиляторами

#### 2.3. Firebird Version Matrix
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐ (средняя)

Тестирование с Firebird 5.0, 5.1 (и потенциально 4.0).

**Что дает:**
- Совместимость с разными версиями Firebird
- Обнаружение API changes
- Расширение поддерживаемых версий

**Требует:**
- Docker образы с разными версиями Firebird
- Matrix strategy

### Уровень 3: Качество и покрытие (2-3 дня)

#### 3.1. Code Coverage
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Измерение покрытия кода тестами.

**Что дает:**
- Визуализация непротестированных участков кода
- Метрики покрытия для отчетов
- Интеграция с Codecov/Coveralls

**Требует:**
- Компиляция с --coverage флагами
- Генерация lcov отчетов
- Интеграция с Codecov

#### 3.2. Memory Sanitizers
**Сложность**: ⭐⭐⭐ (средняя-высокая)
**Польза**: ⭐⭐⭐⭐⭐ (критичная)

Проверка на memory leaks, use-after-free, data races.

**Что дает:**
- Обнаружение скрытых memory проблем
- Проверка thread safety
- Повышение надежности библиотеки

**Требует:**
- AddressSanitizer (ASan)
- UndefinedBehaviorSanitizer (UBSan)
- ThreadSanitizer (TSan)
- Отдельные jobs в workflow

#### 3.3. Valgrind Integration
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐ (средняя)

Детальная проверка памяти с valgrind.

**Что дает:**
- Глубокий анализ работы с памятью
- Обнаружение leaks от Firebird client
- Профилирование производительности

**Требует:**
- Valgrind в Docker образе
- Suppression файлы для Firebird
- Увеличенное время выполнения

### Уровень 4: Распространение (3-4 дня)

#### 4.1. Conan Package Creation
**Сложность**: ⭐⭐⭐ (средняя-высокая)
**Польза**: ⭐⭐⭐⭐⭐ (критичная)

Создание conanfile.py и автоматическая сборка пакетов.

**Что дает:**
- Простая установка: `conan install fbpp/1.0.0@`
- Управление зависимостями
- Интеграция в другие проекты

**Требует:**
- conanfile.py с package info
- test_package для проверки пакета
- Conan profile configurations

#### 4.2. Conan Center Publication
**Сложность**: ⭐⭐⭐⭐ (высокая)
**Польза**: ⭐⭐⭐⭐⭐ (критичная)

Публикация в Conan Center Index.

**Что дает:**
- Доступность для всех пользователей Conan
- Официальный статус пакета
- Автоматическое обновление

**Требует:**
- Соответствие требованиям Conan Center
- PR в conan-center-index репозиторий
- Code review от maintainers

#### 4.3. GitHub Releases Automation
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Автоматическое создание релизов и changelog.

**Что дает:**
- Структурированные релизы с artifacts
- Автоматические release notes
- Бинарные пакеты для скачивания

**Требует:**
- semantic-release или similar
- Conventional commits
- Release artifacts (headers, binaries)

### Уровень 5: Документация (2-3 дня)

#### 5.1. Doxygen API Documentation
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Автоматическая генерация API документации.

**Что дает:**
- Актуальная документация API
- Автоматическое обновление при изменениях
- Публикация на GitHub Pages

**Требует:**
- Doxyfile конфигурация
- Doxygen комментарии в коде
- GitHub Pages deployment

#### 5.2. Usage Examples Testing
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐ (средняя)

Компиляция и запуск примеров из examples/.

**Что дает:**
- Гарантия работоспособности примеров
- Проверка актуальности документации
- Демонстрация возможностей библиотеки

**Требует:**
- Сборка examples в CI
- Проверка выхода примеров

#### 5.3. README Badges
**Сложность**: ⭐ (простая)
**Польза**: ⭐⭐⭐ (средняя)

Добавление бейджей статуса в README.

**Что дает:**
- Визуальный статус проекта
- Доверие пользователей
- Ссылки на coverage, docs

**Требует:**
- Shields.io интеграция
- Обновление README.md

### Уровень 6: Продвинутые возможности (5-7 дней)

#### 6.1. Performance Benchmarks
**Сложность**: ⭐⭐⭐⭐ (высокая)
**Польза**: ⭐⭐⭐ (средняя)

Автоматические бенчмарки производительности.

**Что дает:**
- Отслеживание производительности
- Обнаружение регрессий
- Сравнение с предыдущими версиями

**Требует:**
- Google Benchmark интеграция
- Стабильные runners
- Историческое хранение результатов

#### 6.2. Security Scanning (CodeQL)
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐⭐ (высокая)

Автоматическое сканирование на уязвимости.

**Что дает:**
- Обнаружение security проблем
- Соответствие security best practices
- GitHub Security Advisories

**Требует:**
- GitHub Advanced Security (бесплатно для public repos)
- CodeQL workflow
- Настройка правил

#### 6.3. Dependency Updates (Dependabot/Renovate)
**Сложность**: ⭐ (простая)
**Польза**: ⭐⭐⭐⭐ (высокая)

Автоматическое обновление зависимостей.

**Что дает:**
- Актуальные версии зависимостей
- Автоматические PR с обновлениями
- Security патчи

**Требует:**
- Dependabot конфигурация
- Или Renovate bot

#### 6.4. Nightly Builds
**Сложность**: ⭐⭐ (средняя)
**Польза**: ⭐⭐⭐ (средняя)

Ежедневные сборки из develop/main.

**Что дает:**
- Проверка стабильности
- Ранняя детекция проблем
- Bleeding-edge пакеты для тестирования

**Требует:**
- Scheduled workflow
- Отдельный канал публикации

### Уровень 7: Claude Code Integration (3-4 дня)

#### 7.1. Claude Code Testing Hook
**Сложность**: ⭐⭐⭐ (средняя-высокая)
**Польза**: ⭐⭐⭐⭐ (высокая для разработки с Claude)

Автоматический запуск тестов при изменениях от Claude Code.

**Что дает:**
- Немедленная обратная связь для Claude
- Автоматическое исправление ошибок
- Интеграция с AGENTS.md workflow

**Требует:**
- GitHub commit hooks
- Claude Code agent интеграция
- Webhook для уведомлений

#### 7.2. Auto-fix Failed Tests
**Сложность**: ⭐⭐⭐⭐ (высокая)
**Польза**: ⭐⭐⭐ (средняя)

Автоматическое создание PR с исправлениями от Claude.

**Что дает:**
- Автоматическое исправление простых ошибок
- Сокращение времени на рутинные исправления
- Примеры исправлений для обучения

**Требует:**
- Claude Code API интеграция
- PR creation bot
- Ограничения на типы исправлений

#### 7.3. AI Code Review Assistant
**Сложность**: ⭐⭐⭐⭐ (высокая)
**Польза**: ⭐⭐⭐ (средняя)

Claude Code проводит code review для PR.

**Что дает:**
- Автоматический code review
- Предложения по улучшению
- Проверка соответствия best practices

**Требует:**
- Claude API
- PR comment bot
- Контроль стоимости API calls

---

## Этапы внедрения CI/CD

### Этап 0: Подготовка (1 день)

#### Цель
Подготовить проект к внедрению CI/CD инфраструктуры.

#### Действия

**0.1. Проверка текущего состояния**
```bash
# Убедиться что проект собирается локально
./build.sh RelWithDebInfo

# Проверить что все тесты проходят
cd build && ctest --output-on-failure

# Проверить git remote
git remote -v
```

**0.2. Создание базовой документации**

Создать или обновить:
- `README.md` - базовое описание проекта, требования, quick start
- `LICENSE` - выбрать лицензию (MIT/Apache 2.0/BSD рекомендуются для библиотек)
- `.gitignore` - убедиться что build/, CMakeUserPresets.json исключены

**0.3. Структура веток**

Определить стратегию:
```
main (stable releases)
├── develop (integration branch)
├── feature/* (feature branches)
└── release/* (release preparation)
```

Или упрощенная:
```
main (все изменения)
└── feature/* (PR ветки)
```

**0.4. Conventional Commits**

Принять соглашение о формате коммитов:
```
feat: add statement caching support
fix: correct INT128 scale handling
docs: update API documentation
test: add DECFLOAT tests
chore: update dependencies
```

### Этап 1: Базовая CI (2-3 дня)

#### Цель
Настроить базовую сборку и тестирование на Linux.

#### 1.1. Создание GitHub Actions структуры

```bash
mkdir -p .github/workflows
```

**Файл: `.github/workflows/ci-linux.yml`**

```yaml
name: CI - Linux

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

env:
  CONAN_V2_MODE: 1

jobs:
  build-and-test:
    name: Linux GCC-11
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
        ports:
          - 3050:3050
        options: >-
          --health-cmd "echo 'SELECT 1 FROM RDB\$DATABASE;' | /usr/local/firebird/bin/isql -user SYSDBA -password planomer"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install Conan
        run: |
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Cache Conan packages
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: conan-linux-${{ hashFiles('conanfile.txt') }}
          restore-keys: conan-linux-

      - name: Install dependencies
        run: |
          # Firebird client library
          sudo apt-get update
          sudo apt-get install -y firebird-dev

          # Conan dependencies
          conan install . --output-folder=build --build=missing -s build_type=Release

      - name: Configure CMake
        run: |
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DBUILD_EXAMPLES=OFF \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Prepare test database directory
        run: |
          mkdir -p /tmp/test
          chmod 777 /tmp/test

      - name: Update test configuration
        run: |
          # Создать конфиг для CI окружения
          cat > config/test_config.json <<EOF
          {
            "db": {
              "server": "localhost",
              "path": "/tmp/test/binding_test.fdb",
              "user": "SYSDBA",
              "password": "planomer",
              "charset": "UTF8"
            },
            "tests": {
              "temp_db": {
                "path": "/tmp/test/fbpp_temp_test.fdb",
                "server": "localhost",
                "recreate_each_test": true
              }
            }
          }
          EOF

      - name: Run tests
        run: |
          cd build
          ctest --output-on-failure --timeout 300
        env:
          SPDLOG_LEVEL: info

      - name: Upload test logs on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: test-logs-linux
          path: |
            build/Testing/Temporary/LastTest.log
            /tmp/test/*.log
```

**Ключевые особенности:**
1. **Firebird service container** - запускает Firebird 5 в Docker для тестов
2. **Conan caching** - кэширует зависимости для ускорения сборки
3. **Test configuration** - создает конфиг для CI окружения
4. **Test logs upload** - сохраняет логи при падении тестов

#### 1.2. Проблема: Тесты требуют реального Firebird

**Анализ проблемы:**
Тесты используют `firebird5:3050` и `/mnt/test/` пути, что работает только в вашем окружении.

**Решение:**

**Файл: `tests/test_base.hpp`** (обновить)

```cpp
// Добавить метод для получения конфигурации из переменных окружения
static std::string getFirebirdServer() {
    const char* server = std::getenv("FIREBIRD_SERVER");
    return server ? server : "firebird5";
}

static std::string getTestDbPath() {
    const char* path = std::getenv("TEST_DB_PATH");
    return path ? path : "/mnt/test/fbpp_temp_test.fdb";
}
```

Или использовать `config/test_config.json` как источник истины (уже реализовано).

#### 1.3. Первый запуск

```bash
# Создать PR с изменениями
git checkout -b ci/basic-linux-build
git add .github/
git commit -m "ci: add basic Linux CI workflow"
git push -u origin ci/basic-linux-build

# Создать PR через GitHub UI или gh CLI:
gh pr create --title "CI: Basic Linux build and test" \
  --body "Adds basic CI pipeline for Linux with GCC-11"
```

**Ожидаемый результат:**
- Workflow запускается автоматически
- Сборка проходит успешно
- Все тесты проходят
- В PR появляется зеленая галочка

**Возможные проблемы и решения:**

| Проблема | Причина | Решение |
|----------|---------|---------|
| Firebird не стартует | Health check не работает | Увеличить timeout, проверить команду |
| Тесты не могут создать БД | Недостаточно прав | Добавить `chmod 777` для /tmp/test |
| Conan не находит пакеты | Неправильный profile | Явно указать `conan profile detect --force` |
| Firebird client не найден | Не установлен firebird-dev | Добавить `apt-get install firebird-dev` |

### Этап 2: Code Quality Checks (1-2 дня)

#### Цель
Добавить автоматические проверки качества кода.

#### 2.1. Clang-Format проверка

**Файл: `.clang-format`**

```yaml
---
Language: Cpp
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
PointerAlignment: Left
ReferenceAlignment: Left
NamespaceIndentation: All
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
BreakBeforeBraces: Attach
SpaceAfterTemplateKeyword: true
Standard: c++20
```

**Файл: `.github/workflows/code-quality.yml`**

```yaml
name: Code Quality

on:
  pull_request:
    branches: [main, develop]

jobs:
  format-check:
    name: Format Check
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install clang-format-16
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-format-16

      - name: Check formatting
        run: |
          find include src tests examples -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | \
            xargs clang-format-16 --dry-run --Werror

  clang-tidy:
    name: Static Analysis
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-tidy-16 firebird-dev
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Conan deps
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release

      - name: Configure CMake
        run: |
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

      - name: Run clang-tidy
        run: |
          # Анализировать только src/ и include/, исключить third_party/
          find src include -name "*.cpp" | \
            xargs clang-tidy-16 -p build --warnings-as-errors='*'
```

**Файл: `.clang-tidy`**

```yaml
---
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  cppcoreguidelines-*,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  modernize-*,
  -modernize-use-trailing-return-type,
  performance-*,
  readability-*,
  -readability-magic-numbers,
  -readability-identifier-length

WarningsAsErrors: '*'
HeaderFilterRegex: 'include/fbpp/.*'
FormatStyle: file
```

#### 2.2. Добавить pre-commit хук (опционально для разработчиков)

**Файл: `.pre-commit-config.yaml`**

```yaml
repos:
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v16.0.6
    hooks:
      - id: clang-format
        types_or: [c++, c]
        args: [--style=file]

  - repo: local
    hooks:
      - id: clang-tidy
        name: clang-tidy
        entry: clang-tidy-16
        language: system
        types: [c++]
        args: [-p=build, --warnings-as-errors='*']
```

Разработчики могут установить:
```bash
pip install pre-commit
pre-commit install
```

### Этап 3: Windows Support (3-4 дня)

#### Цель
Добавить сборку и тестирование на Windows.

#### 3.1. Windows CI workflow

**Файл: `.github/workflows/ci-windows.yml`**

```yaml
name: CI - Windows

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

jobs:
  build-and-test:
    name: Windows MSVC 2022
    runs-on: windows-2022

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install Conan
        run: |
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Download Firebird
        run: |
          # Скачать Firebird 5 installer
          $url = "https://github.com/FirebirdSQL/firebird/releases/download/v5.0.0/Firebird-5.0.0.1306-0-x64.exe"
          Invoke-WebRequest -Uri $url -OutFile firebird-installer.exe

      - name: Install Firebird
        run: |
          # Тихая установка Firebird
          Start-Process -Wait -FilePath "firebird-installer.exe" -ArgumentList "/VERYSILENT", "/SP-", "/NOICONS", "/TASKS=`"UseApplicationTask,UseServiceTask,AutoStartTask`""

          # Добавить в PATH
          echo "C:\Program Files\Firebird\Firebird_5_0" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append

      - name: Wait for Firebird service
        run: |
          Start-Sleep -Seconds 10
          # Проверить что служба запущена
          Get-Service -Name "FirebirdServerDefaultInstance"

      - name: Cache Conan packages
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: conan-windows-${{ hashFiles('conanfile.txt') }}
          restore-keys: conan-windows-

      - name: Install dependencies
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release

      - name: Configure CMake
        run: |
          cmake -S . -B build `
            -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake `
            -DBUILD_TESTING=ON `
            -DBUILD_EXAMPLES=OFF

      - name: Build
        run: cmake --build build --config Release -j

      - name: Prepare test environment
        run: |
          # Создать директорию для тестовых БД
          New-Item -ItemType Directory -Force -Path C:\temp\test

          # Обновить конфиг
          $config = @{
            db = @{
              server = "localhost"
              path = "C:/temp/test/binding_test.fdb"
              user = "SYSDBA"
              password = "masterkey"
              charset = "UTF8"
            }
            tests = @{
              temp_db = @{
                path = "C:/temp/test/fbpp_temp_test.fdb"
                server = "localhost"
                recreate_each_test = $true
              }
            }
          }
          $config | ConvertTo-Json | Set-Content -Path config/test_config.json

      - name: Run tests
        run: |
          cd build
          ctest -C Release --output-on-failure --timeout 300
        env:
          SPDLOG_LEVEL: info

      - name: Upload test logs on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: test-logs-windows
          path: |
            build/Testing/Temporary/LastTest.log
            C:/temp/test/*.log
```

#### 3.2. Адаптация кода для Windows

**Потенциальные проблемы:**

1. **Пути к файлам** - использовать `std::filesystem::path` для кроссплатформенности
2. **Firebird пути** - Windows использует другие дефолтные пути
3. **Разделители путей** - `/` vs `\`

**Файл: `cmake/FindFirebird.cmake`** (обновить для Windows)

```cmake
if(WIN32)
    # Windows specific paths
    find_path(FIREBIRD_INCLUDE_DIR
        NAMES firebird/Interface.h
        HINTS
            "C:/Program Files/Firebird/Firebird_5_0/include"
            "C:/Program Files/Firebird/Firebird_4_0/include"
            "$ENV{FIREBIRD}/include"
    )

    find_library(FIREBIRD_LIBRARY
        NAMES fbclient_ms fbclient
        HINTS
            "C:/Program Files/Firebird/Firebird_5_0/lib"
            "C:/Program Files/Firebird/Firebird_4_0/lib"
            "$ENV{FIREBIRD}/lib"
    )
else()
    # Linux/Unix paths (existing code)
    find_path(FIREBIRD_INCLUDE_DIR
        NAMES firebird/Interface.h
        HINTS
            /usr/include
            /usr/local/include
            /opt/firebird/include
            $ENV{FIREBIRD}/include
    )

    find_library(FIREBIRD_LIBRARY
        NAMES fbclient
        HINTS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
            /opt/firebird/lib
            $ENV{FIREBIRD}/lib
    )
endif()
```

### Этап 4: Matrix Builds (1-2 дня)

#### Цель
Тестировать на множестве комбинаций компиляторов и платформ.

#### 4.1. Объединенный matrix workflow

**Файл: `.github/workflows/ci-matrix.yml`**

```yaml
name: CI - Matrix

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

jobs:
  build-matrix:
    name: ${{ matrix.os }} - ${{ matrix.compiler }} - ${{ matrix.build_type }}
    runs-on: ${{ matrix.os }}

    strategy:
      fail-fast: false  # Продолжать даже если одна комбинация падает
      matrix:
        os: [ubuntu-22.04, ubuntu-20.04, windows-2022]
        compiler: [gcc-11, gcc-12, gcc-13, clang-14, clang-15, clang-16, msvc]
        build_type: [Debug, Release]

        # Ограничения
        exclude:
          # MSVC только на Windows
          - os: ubuntu-22.04
            compiler: msvc
          - os: ubuntu-20.04
            compiler: msvc
          # GCC/Clang только на Linux
          - os: windows-2022
            compiler: gcc-11
          - os: windows-2022
            compiler: gcc-12
          - os: windows-2022
            compiler: gcc-13
          - os: windows-2022
            compiler: clang-14
          - os: windows-2022
            compiler: clang-15
          - os: windows-2022
            compiler: clang-16
          # GCC 13 только на Ubuntu 22.04
          - os: ubuntu-20.04
            compiler: gcc-13
          - os: ubuntu-20.04
            compiler: clang-16

    services:
      firebird:
        image: ${{ matrix.os == 'ubuntu-22.04' && 'jacobalberty/firebird:v5.0' || matrix.os == 'ubuntu-20.04' && 'jacobalberty/firebird:v5.0' || '' }}
        # Windows не поддерживает service containers, используем установку

    steps:
      # ... шаги сборки с использованием matrix.compiler и matrix.build_type
```

**Оптимизация**: Запускать полный matrix только для main и release веток, для PR использовать сокращенный:

```yaml
on:
  push:
    branches: [main, develop, release/*]
  pull_request:
    branches: [main, develop]

jobs:
  build-matrix:
    strategy:
      matrix:
        # Для PR - только базовые конфигурации
        include:
          - os: ubuntu-22.04
            compiler: gcc-11
            build_type: Release
          - os: windows-2022
            compiler: msvc
            build_type: Release
        # Для main/release - полный matrix (через separate workflow)
```

### Этап 5: Code Coverage (2 дня)

#### Цель
Измерить покрытие кода тестами и интегрировать с Codecov.

#### 5.1. Coverage workflow

**Файл: `.github/workflows/coverage.yml`**

```yaml
name: Code Coverage

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

jobs:
  coverage:
    name: Generate Coverage Report
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev lcov
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Conan dependencies
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Debug

      - name: Configure CMake with coverage
        run: |
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
            -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
            -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests
        run: |
          cd build
          ctest --output-on-failure

      - name: Generate coverage report
        run: |
          # Захватить coverage данные
          lcov --directory build --capture --output-file coverage.info

          # Отфильтровать ненужные файлы
          lcov --remove coverage.info \
            '/usr/*' \
            '*/third_party/*' \
            '*/tests/*' \
            '*/examples/*' \
            '*/build/*' \
            --output-file coverage.info

          # Показать summary
          lcov --list coverage.info

      - name: Upload to Codecov
        uses: codecov/codecov-action@v4
        with:
          token: ${{ secrets.CODECOV_TOKEN }}
          files: ./coverage.info
          fail_ci_if_error: true
          verbose: true

      - name: Generate HTML report
        run: |
          genhtml coverage.info --output-directory coverage-html

      - name: Upload HTML report
        uses: actions/upload-artifact@v4
        with:
          name: coverage-report
          path: coverage-html/
```

#### 5.2. Настройка Codecov

1. Зарегистрировать проект на https://codecov.io
2. Получить CODECOV_TOKEN
3. Добавить в GitHub Secrets: Settings → Secrets → Actions → New repository secret

**Файл: `codecov.yml`** (в корне проекта)

```yaml
coverage:
  status:
    project:
      default:
        target: 80%  # Минимальное покрытие
        threshold: 1%  # Допустимое снижение
    patch:
      default:
        target: 70%  # Для новых изменений

ignore:
  - "tests/"
  - "examples/"
  - "third_party/"

comment:
  layout: "reach,diff,flags,tree"
  behavior: default
  require_changes: false
```

### Этап 6: Conan Package (3-4 дня)

#### Цель
Создать Conan пакет для распространения библиотеки.

#### 6.1. Создание conanfile.py

**Файл: `conanfile.py`**

```python
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.files import copy
import os

class FbppConan(ConanFile):
    name = "fbpp"
    version = "1.0.0"
    license = "MIT"  # или ваша лицензия
    author = "Your Name <your.email@example.com>"
    url = "https://github.com/sashok74/fbpp"
    description = "Modern C++20 wrapper for Firebird 5 database OO API"
    topics = ("firebird", "database", "c++20", "wrapper")

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_tests": [True, False],
        "with_examples": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_tests": False,
        "with_examples": False
    }

    exports_sources = (
        "CMakeLists.txt",
        "include/*",
        "src/*",
        "cmake/*",
        "third_party/*",
        "tests/*",
        "examples/*",
        "config/*",
        "LICENSE"
    )

    def requirements(self):
        self.requires("spdlog/1.12.0")
        self.requires("nlohmann_json/3.11.3")

        if self.options.with_tests:
            self.requires("gtest/1.14.0")

    def build_requirements(self):
        # Firebird клиент должен быть установлен в системе
        pass

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables["BUILD_TESTING"] = self.options.with_tests
        tc.variables["BUILD_EXAMPLES"] = self.options.with_examples
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

        if self.options.with_tests:
            cmake.test()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["fbpp", "cppdecimal"]
        self.cpp_info.includedirs = ["include"]

        # Требуемые системные библиотеки
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["fbclient"]
        elif self.settings.os == "Windows":
            self.cpp_info.system_libs = ["fbclient_ms"]

        # Определения компилятора
        self.cpp_info.defines = ["TTMATH_NOASM"]

        # Требования к стандарту
        self.cpp_info.cppstd = "20"
```

#### 6.2. Test package

**Директория: `test_package/`**

```
test_package/
├── CMakeLists.txt
├── conanfile.py
└── example.cpp
```

**Файл: `test_package/conanfile.py`**

```python
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.build import can_run

class FbppTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            cmd = os.path.join(self.cpp.build.bindirs[0], "example")
            self.run(cmd, env="conanrun")
```

**Файл: `test_package/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(PackageTest CXX)

find_package(fbpp REQUIRED CONFIG)

add_executable(example example.cpp)
target_link_libraries(example fbpp::fbpp)
target_compile_features(example PRIVATE cxx_std_20)
```

**Файл: `test_package/example.cpp`**

```cpp
#include <fbpp/fbpp.hpp>
#include <iostream>

int main() {
    std::cout << "fbpp library test - compilation successful!\n";

    // Базовая проверка что типы доступны
    using namespace fbpp::core;

    try {
        // Попытка создать connection (может упасть если нет Firebird, это ок для test_package)
        std::cout << "Extended types available: Int128, DecFloat16, DecFloat34\n";
    } catch (...) {
        std::cout << "Runtime test skipped (Firebird not available)\n";
    }

    return 0;
}
```

#### 6.3. Локальное тестирование пакета

```bash
# Создать пакет
conan create . --build=missing

# Тестировать с различными настройками
conan create . -s build_type=Debug --build=missing
conan create . -o shared=True --build=missing

# Экспортировать в локальный кэш
conan export . --version=1.0.0
```

#### 6.4. CI для Conan пакета

**Файл: `.github/workflows/conan-package.yml`**

```yaml
name: Conan Package

on:
  push:
    branches: [main, develop]
    tags: ['v*']
  pull_request:
    branches: [main]

jobs:
  create-package:
    name: Create Conan Package - ${{ matrix.os }}
    runs-on: ${{ matrix.os }}

    strategy:
      matrix:
        os: [ubuntu-22.04, windows-2022]
        build_type: [Debug, Release]

    steps:
      - uses: actions/checkout@v4

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install Conan
        run: |
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Firebird (Linux)
        if: matrix.os == 'ubuntu-22.04'
        run: sudo apt-get update && sudo apt-get install -y firebird-dev

      - name: Install Firebird (Windows)
        if: matrix.os == 'windows-2022'
        shell: pwsh
        run: |
          $url = "https://github.com/FirebirdSQL/firebird/releases/download/v5.0.0/Firebird-5.0.0.1306-0-x64.exe"
          Invoke-WebRequest -Uri $url -OutFile firebird-installer.exe
          Start-Process -Wait -FilePath "firebird-installer.exe" -ArgumentList "/VERYSILENT"

      - name: Create Conan package
        run: |
          conan create . -s build_type=${{ matrix.build_type }} --build=missing

      - name: Test package installation
        run: |
          conan install --requires=fbpp/1.0.0 --build=missing

      - name: Upload package to Artifactory (on tag)
        if: startsWith(github.ref, 'refs/tags/v')
        run: |
          # Загрузить в приватный Artifactory или другой репозиторий
          conan upload fbpp/1.0.0 -r=artifactory --all
        env:
          CONAN_PASSWORD: ${{ secrets.CONAN_PASSWORD }}
```

### Этап 7: Memory Sanitizers (2 дня)

#### Цель
Обнаружить memory leaks, use-after-free, data races.

#### 7.1. Sanitizers workflow

**Файл: `.github/workflows/sanitizers.yml`**

```yaml
name: Memory Sanitizers

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]
  schedule:
    # Запускать nightly для обнаружения flaky тестов
    - cron: '0 2 * * *'

jobs:
  address-sanitizer:
    name: AddressSanitizer
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev clang-16
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Conan dependencies
        run: conan install . --output-folder=build --build=missing -s build_type=Debug

      - name: Configure with ASan
        run: |
          export CC=clang-16
          export CXX=clang++-16
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
            -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests with ASan
        run: |
          cd build
          ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ctest --output-on-failure

  undefined-behavior-sanitizer:
    name: UndefinedBehaviorSanitizer
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev clang-16
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Conan dependencies
        run: conan install . --output-folder=build --build=missing -s build_type=Debug

      - name: Configure with UBSan
        run: |
          export CC=clang-16
          export CXX=clang++-16
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined" \
            -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests with UBSan
        run: |
          cd build
          UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 ctest --output-on-failure

  thread-sanitizer:
    name: ThreadSanitizer
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev clang-16
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Install Conan dependencies
        run: conan install . --output-folder=build --build=missing -s build_type=Debug

      - name: Configure with TSan
        run: |
          export CC=clang-16
          export CXX=clang++-16
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
            -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests with TSan
        run: |
          cd build
          TSAN_OPTIONS=second_deadlock_stack=1:halt_on_error=0 ctest --output-on-failure
```

**Важно**: TSan может давать false positives для Firebird клиентской библиотеки. Может потребоваться suppression file:

**Файл: `tsan.supp`**
```
# Suppress Firebird client library warnings
race:libfbclient.so
```

Использовать: `TSAN_OPTIONS="suppressions=tsan.supp"`

### Этап 8: Documentation (2-3 дня)

#### Цель
Автоматически генерировать и публиковать документацию.

#### 8.1. Doxygen конфигурация

**Файл: `Doxyfile`** (сокращенная версия, создать полную через `doxygen -g`)

```
PROJECT_NAME           = "fbpp - Firebird C++20 Wrapper"
PROJECT_NUMBER         = 1.0.0
PROJECT_BRIEF          = "Modern C++20 wrapper for Firebird 5 database"
OUTPUT_DIRECTORY       = docs/api
INPUT                  = include/fbpp README.md
RECURSIVE              = YES
EXTRACT_ALL            = YES
EXTRACT_PRIVATE        = NO
EXTRACT_STATIC         = YES
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
HTML_OUTPUT            = html
USE_MDFILE_AS_MAINPAGE = README.md

# Styling
HTML_COLORSTYLE_HUE    = 220
HTML_COLORSTYLE_SAT    = 100
HTML_COLORSTYLE_GAMMA  = 80

# Include paths
INCLUDE_PATH           = include
PREDEFINED             = __cplusplus=202002L

# Examples
EXAMPLE_PATH           = examples
```

#### 8.2. Documentation workflow

**Файл: `.github/workflows/documentation.yml`**

```yaml
name: Documentation

on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: write

jobs:
  build-docs:
    name: Build and Deploy Documentation
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v4

      - name: Install Doxygen
        run: |
          sudo apt-get update
          sudo apt-get install -y doxygen graphviz

      - name: Generate documentation
        run: doxygen Doxyfile

      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./docs/api/html
          cname: fbpp.yourdomain.com  # опционально
```

#### 8.3. README с бейджами

**Обновить: `README.md`**

```markdown
# fbpp - Firebird C++20 Wrapper

[![CI - Linux](https://github.com/sashok74/fbpp/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/sashok74/fbpp/actions/workflows/ci-linux.yml)
[![CI - Windows](https://github.com/sashok74/fbpp/actions/workflows/ci-windows.yml/badge.svg)](https://github.com/sashok74/fbpp/actions/workflows/ci-windows.yml)
[![Code Coverage](https://codecov.io/gh/sashok74/fbpp/branch/main/graph/badge.svg)](https://codecov.io/gh/sashok74/fbpp)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Conan Center](https://img.shields.io/conan/v/fbpp)](https://conan.io/center/fbpp)

Modern C++20 wrapper for Firebird 5 database OO API providing type-safe message packing/unpacking, RAII resource management, and comprehensive support for ALL Firebird extended types.

## Features

- 🔥 **Full Firebird 5 support** - INT128, DECFLOAT(16/34), NUMERIC(38,x), TIMESTAMP WITH TIME ZONE
- 🚀 **Modern C++20** - Templates, concepts, ranges, coroutines
- 🎯 **Type-safe** - Compile-time type checking
- 📦 **Multiple data formats** - JSON, tuples, strongly-typed objects
- ⚡ **High performance** - Statement caching, batch operations
- 🔧 **Easy to use** - RAII, named parameters, intuitive API

## Quick Start

### Installation with Conan

```bash
conan install --requires=fbpp/1.0.0
```

### Basic Example

```cpp
#include <fbpp/fbpp.hpp>

int main() {
    using namespace fbpp::core;

    // Connect to database
    auto conn = Connection::create("localhost:employees.fdb", "SYSDBA", "masterkey");
    auto txn = conn.startTransaction();

    // Execute query with named parameters
    auto stmt = conn.prepare("SELECT * FROM employees WHERE salary > :min_salary");
    auto rs = stmt.executeQuery(txn, {{"min_salary", 50000}});

    // Fetch results as JSON
    while (auto row = rs.fetchOneJSON()) {
        std::cout << row.dump(2) << std::endl;
    }

    txn.commit();
}
```

## Documentation

- [API Documentation](https://sashok74.github.io/fbpp/)
- [User Guide](docs/USER_GUIDE.md)
- [Examples](examples/)

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file.
```

### Этап 9: Releases & Publishing (2 дня)

#### Цель
Автоматизировать создание релизов и публикацию пакетов.

#### 9.1. Release workflow

**Файл: `.github/workflows/release.yml`**

```yaml
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

permissions:
  contents: write

jobs:
  create-release:
    name: Create GitHub Release
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0  # Для changelog

      - name: Generate changelog
        id: changelog
        run: |
          # Генерация changelog между тегами
          PREV_TAG=$(git describe --tags --abbrev=0 HEAD^ 2>/dev/null || echo "")
          if [ -z "$PREV_TAG" ]; then
            CHANGELOG=$(git log --pretty=format:"- %s (%h)" HEAD)
          else
            CHANGELOG=$(git log --pretty=format:"- %s (%h)" ${PREV_TAG}..HEAD)
          fi
          echo "changelog<<EOF" >> $GITHUB_OUTPUT
          echo "$CHANGELOG" >> $GITHUB_OUTPUT
          echo "EOF" >> $GITHUB_OUTPUT

      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          body: |
            ## Changes
            ${{ steps.changelog.outputs.changelog }}

            ## Installation

            ### Conan
            ```bash
            conan install --requires=fbpp/${{ github.ref_name }}
            ```

            ### From source
            ```bash
            git clone https://github.com/sashok74/fbpp.git
            cd fbpp
            git checkout ${{ github.ref_name }}
            ./build.sh Release
            ```
          draft: false
          prerelease: false
          files: |
            LICENSE
            README.md

  publish-conan:
    name: Publish to Conan Center
    needs: create-release
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v4

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install Conan
        run: pip install "conan>=2.0"

      - name: Create package
        run: |
          conan create . --version=${{ github.ref_name }} --build=missing

      - name: Upload to Artifactory
        run: |
          conan remote add artifactory ${{ secrets.CONAN_REMOTE_URL }}
          conan upload fbpp/${{ github.ref_name }} -r=artifactory --all --confirm
        env:
          CONAN_LOGIN_USERNAME: ${{ secrets.CONAN_USER }}
          CONAN_PASSWORD: ${{ secrets.CONAN_PASSWORD }}
```

#### 9.2. Semantic versioning

Использовать теги формата `v1.2.3`:
- `v1.0.0` - Major release (breaking changes)
- `v1.1.0` - Minor release (new features, backward compatible)
- `v1.1.1` - Patch release (bug fixes)

Создание релиза:
```bash
git tag -a v1.0.0 -m "Release v1.0.0 - Initial stable release"
git push origin v1.0.0
```

### Этап 10: Claude Code Integration (3-4 дня)

#### Цель
Интегрировать CI/CD с Claude Code агентами для автоматизации разработки.

#### 10.1. Claude Code workflow hook

**Файл: `.github/workflows/claude-code-integration.yml`**

```yaml
name: Claude Code Integration

on:
  push:
    branches:
      - 'claude/**'  # Все ветки созданные Claude Code
  pull_request:
    types: [opened, synchronize]
  workflow_dispatch:
    inputs:
      claude_task:
        description: 'Task description for Claude Code'
        required: false

jobs:
  run-tests-and-notify:
    name: Test and Notify Claude
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          ISC_PASSWORD: planomer
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4

      - name: Setup environment
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Build and test
        id: build_test
        continue-on-error: true
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release
          cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_TESTING=ON
          cmake --build build -j$(nproc)
          cd build && ctest --output-on-failure --rerun-failed --output-junit test-results.xml

      - name: Parse test results
        id: test_results
        if: always()
        run: |
          if [ -f build/test-results.xml ]; then
            # Парсинг JUnit XML для извлечения информации о падениях
            FAILURES=$(python3 -c "import xml.etree.ElementTree as ET; tree = ET.parse('build/test-results.xml'); print(len([tc for ts in tree.iter('testsuite') for tc in ts.iter('testcase') if tc.find('failure') is not None]))")
            echo "failures=$FAILURES" >> $GITHUB_OUTPUT

            # Извлечь детали падений
            python3 << 'EOF' > failure_details.txt
          import xml.etree.ElementTree as ET
          tree = ET.parse('build/test-results.xml')
          for ts in tree.iter('testsuite'):
              for tc in ts.iter('testcase'):
                  failure = tc.find('failure')
                  if failure is not None:
                      print(f"Test: {tc.get('name')}")
                      print(f"Message: {failure.get('message')}")
                      print(f"Details: {failure.text[:500]}")
                      print("---")
          EOF
          fi

      - name: Create Claude Code feedback comment
        if: always() && github.event_name == 'pull_request'
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const failureDetails = fs.existsSync('failure_details.txt')
              ? fs.readFileSync('failure_details.txt', 'utf8')
              : 'No failures';

            const body = `## 🤖 Claude Code CI Report

            **Build Status**: ${{ steps.build_test.outcome }}
            **Test Failures**: ${{ steps.test_results.outputs.failures || 0 }}

            ### Failed Tests
            \`\`\`
            ${failureDetails}
            \`\`\`

            ### Suggested Actions
            ${failureDetails !== 'No failures' ? `
            - Review test failures in the logs
            - Consider running locally: \`./build.sh && cd build && ctest\`
            - Check for type mismatches in extended types (INT128, DECFLOAT)
            ` : '✅ All tests passed!'}

            ### Next Steps for Claude Code
            ${failureDetails !== 'No failures' ? `
            To auto-fix these issues, comment:
            \`@claude-code fix test failures\`
            ` : ''}
            `;

            github.rest.issues.createComment({
              issue_number: context.issue.number,
              owner: context.repo.owner,
              repo: context.repo.repo,
              body: body
            });

      - name: Notify Claude Code API (if configured)
        if: always() && env.CLAUDE_WEBHOOK_URL != ''
        run: |
          curl -X POST ${{ secrets.CLAUDE_WEBHOOK_URL }} \
            -H "Content-Type: application/json" \
            -d '{
              "event": "ci_complete",
              "status": "${{ steps.build_test.outcome }}",
              "failures": ${{ steps.test_results.outputs.failures || 0 }},
              "branch": "${{ github.ref_name }}",
              "commit": "${{ github.sha }}"
            }'
        env:
          CLAUDE_WEBHOOK_URL: ${{ secrets.CLAUDE_WEBHOOK_URL }}

  auto-fix-on-failure:
    name: Auto-fix with Claude Code
    needs: run-tests-and-notify
    if: failure() && github.event_name == 'push' && startsWith(github.ref, 'refs/heads/claude/')
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v4
        with:
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Trigger Claude Code agent
        run: |
          # Вызвать Claude Code MCP agent для исправления
          echo "Triggering Claude Code auto-fix..."
          # Здесь можно интегрировать с mcp__claude-code-enhanced__claude_code
          # через API или webhook
```

#### 10.2. AGENTS.md интеграция

**Обновить: `AGENTS.md`**

```markdown
# Claude Code Agents Configuration

## CI/CD Integration

### Automatic Testing Agent

**Trigger**: On every push to `claude/*` branches

**Actions**:
1. Run full test suite (113 tests)
2. Generate detailed failure report
3. Post comment to PR with results
4. Notify Claude Code API (if configured)

### Auto-fix Agent

**Trigger**: When tests fail on `claude/*` branches OR manual comment `@claude-code fix test failures`

**Actions**:
1. Analyze test failure logs
2. Identify root cause (type mismatches, null handling, etc.)
3. Generate fix
4. Create commit with fix
5. Push to same branch
6. Re-trigger CI

### Code Review Agent

**Trigger**: PR opened or updated

**Actions**:
1. Review code changes
2. Check for:
   - Extended type handling correctness
   - Memory safety (RAII patterns)
   - Test coverage for new code
   - Documentation updates
3. Post review comments

## Manual Triggers

### Fix failing tests
```
@claude-code fix test failures
```

### Add missing tests
```
@claude-code add tests for [feature]
```

### Update documentation
```
@claude-code update docs for [feature]
```

## Configuration

Set these secrets in GitHub repository settings:

- `CLAUDE_API_KEY` - Claude API key for agent access
- `CLAUDE_WEBHOOK_URL` - Webhook URL for notifications (optional)
- `GITHUB_TOKEN` - Automatically provided by GitHub Actions

## Workflow Files

- `.github/workflows/claude-code-integration.yml` - Main integration workflow
- `.github/workflows/claude-auto-fix.yml` - Auto-fix triggered by comments
```

#### 10.3. Comment-triggered auto-fix

**Файл: `.github/workflows/claude-auto-fix.yml`**

```yaml
name: Claude Auto-Fix

on:
  issue_comment:
    types: [created]

jobs:
  trigger-fix:
    name: Trigger Claude Code Fix
    if: |
      github.event.issue.pull_request &&
      contains(github.event.comment.body, '@claude-code fix')
    runs-on: ubuntu-22.04

    steps:
      - name: React to comment
        uses: actions/github-script@v7
        with:
          script: |
            github.rest.reactions.createForIssueComment({
              owner: context.repo.owner,
              repo: context.repo.repo,
              comment_id: context.payload.comment.id,
              content: 'rocket'
            });

      - name: Get PR details
        id: pr
        uses: actions/github-script@v7
        with:
          script: |
            const pr = await github.rest.pulls.get({
              owner: context.repo.owner,
              repo: context.repo.repo,
              pull_number: context.issue.number
            });
            return {
              head_ref: pr.data.head.ref,
              head_sha: pr.data.head.sha
            };

      - uses: actions/checkout@v4
        with:
          ref: ${{ fromJSON(steps.pr.outputs.result).head_ref }}
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Call Claude Code MCP Agent
        run: |
          # Интеграция с Claude Code через MCP
          # Можно использовать mcp__claude-code-enhanced__claude_code

          # Пример: создать задачу для Claude через API
          curl -X POST ${{ secrets.CLAUDE_API_ENDPOINT }} \
            -H "Authorization: Bearer ${{ secrets.CLAUDE_API_KEY }}" \
            -H "Content-Type: application/json" \
            -d '{
              "task": "fix_test_failures",
              "context": {
                "repo": "${{ github.repository }}",
                "pr": ${{ github.event.issue.number }},
                "comment": "${{ github.event.comment.body }}"
              }
            }'

      - name: Post status update
        uses: actions/github-script@v7
        with:
          script: |
            github.rest.issues.createComment({
              issue_number: context.issue.number,
              owner: context.repo.owner,
              repo: context.repo.repo,
              body: '🤖 Claude Code agent has been triggered. Analyzing failures and preparing fix...'
            });
```

### Этап 11: Advanced Features (опционально, 3-5 дней)

#### 11.1. Performance benchmarks

**Файл: `.github/workflows/benchmarks.yml`**

```yaml
name: Performance Benchmarks

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 3 * * 0'  # Weekly on Sunday

jobs:
  benchmark:
    name: Run Benchmarks
    runs-on: ubuntu-22.04

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        ports:
          - 3050:3050

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev
          pip install "conan>=2.0"
          conan profile detect --force

      # Добавить Google Benchmark в conanfile.txt
      - name: Build benchmarks
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release
          cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_BENCHMARKS=ON
          cmake --build build -j$(nproc)

      - name: Run benchmarks
        run: |
          cd build
          ./benchmarks/fbpp_benchmarks --benchmark_format=json --benchmark_out=benchmark_results.json

      - name: Store benchmark result
        uses: benchmark-action/github-action-benchmark@v1
        with:
          tool: 'googlecpp'
          output-file-path: build/benchmark_results.json
          github-token: ${{ secrets.GITHUB_TOKEN }}
          auto-push: true
          alert-threshold: '150%'
          comment-on-alert: true
          fail-on-alert: false
```

#### 11.2. Dependency updates

**Файл: `.github/dependabot.yml`**

```yaml
version: 2
updates:
  # GitHub Actions updates
  - package-ecosystem: "github-actions"
    directory: "/"
    schedule:
      interval: "weekly"
    open-pull-requests-limit: 5

  # Conan dependencies (через custom script)
  - package-ecosystem: "pip"
    directory: "/"
    schedule:
      interval: "weekly"
```

Для Conan нужен custom workflow:

**Файл: `.github/workflows/dependency-check.yml`**

```yaml
name: Dependency Check

on:
  schedule:
    - cron: '0 4 * * 1'  # Weekly on Monday
  workflow_dispatch:

jobs:
  check-conan-updates:
    name: Check Conan Dependencies
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v4

      - name: Install Conan
        run: pip install "conan>=2.0"

      - name: Check for updates
        run: |
          conan search gtest --remote=conancenter
          conan search spdlog --remote=conancenter
          conan search nlohmann_json --remote=conancenter

          # Вывести текущие и доступные версии
          echo "Current versions:"
          cat conanfile.txt

      - name: Create issue if updates available
        if: # условие если есть обновления
        uses: actions/github-script@v7
        with:
          script: |
            github.rest.issues.create({
              owner: context.repo.owner,
              repo: context.repo.repo,
              title: 'Conan dependencies updates available',
              body: 'New versions of Conan dependencies are available...'
            });
```

#### 11.3. Security scanning

**Файл: `.github/workflows/codeql.yml`**

```yaml
name: "CodeQL"

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 5 * * 2'

jobs:
  analyze:
    name: Analyze
    runs-on: ubuntu-22.04
    permissions:
      actions: read
      contents: read
      security-events: write

    strategy:
      fail-fast: false
      matrix:
        language: ['cpp']

    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Initialize CodeQL
        uses: github/codeql-action/init@v2
        with:
          languages: ${{ matrix.language }}
          queries: security-and-quality

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y firebird-dev
          pip install "conan>=2.0"
          conan profile detect --force

      - name: Build
        run: |
          conan install . --output-folder=build --build=missing -s build_type=Release
          cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
          cmake --build build -j$(nproc)

      - name: Perform CodeQL Analysis
        uses: github/codeql-action/analyze@v2
```

---

## Приложение A: Контрольный список внедрения

### Минимальный набор (Обязательно)
- [ ] Этап 0: Подготовка (README, LICENSE, .gitignore)
- [ ] Этап 1: Базовая CI Linux (сборка + тесты)
- [ ] Этап 3: Windows Support (кроссплатформенность)
- [ ] Этап 6: Conan Package (распространение)

### Рекомендуемый набор (Высокий приоритет)
- [ ] Этап 2: Code Quality (clang-format, clang-tidy)
- [ ] Этап 4: Matrix Builds (множество компиляторов)
- [ ] Этап 5: Code Coverage (измерение покрытия)
- [ ] Этап 7: Memory Sanitizers (поиск memory issues)
- [ ] Этап 8: Documentation (Doxygen + GitHub Pages)

### Полный набор (Максимальная автоматизация)
- [ ] Этап 9: Releases & Publishing (автоматические релизы)
- [ ] Этап 10: Claude Code Integration (AI помощник)
- [ ] Этап 11: Advanced Features (benchmarks, security)

---

## Приложение B: Troubleshooting

### Частые проблемы

**1. Firebird не запускается в Docker**

Симптомы:
```
Error: Unable to complete network request to host "localhost"
```

Решение:
```yaml
services:
  firebird:
    options: >-
      --health-cmd "echo 'SELECT 1 FROM RDB\$DATABASE;' | /usr/local/firebird/bin/isql -user SYSDBA -password planomer"
      --health-interval 10s
      --health-timeout 5s
      --health-retries 5
```

**2. Conan не находит пакеты**

Симптомы:
```
ERROR: Package 'gtest/1.14.0' not found
```

Решение:
```bash
conan remote add conancenter https://center.conan.io
conan profile detect --force
```

**3. Windows пути не работают**

Симптомы:
```
Error: Cannot create database at /tmp/test/...
```

Решение: Использовать `C:/temp/test/` для Windows в test_config.json

**4. Тесты падают в CI но работают локально**

Возможные причины:
- Разные версии Firebird
- Разные права доступа к файлам
- Разные пути к БД

Решение: Использовать переменные окружения и конфигурационные файлы

**5. Sanitizers дают false positives**

Решение: Создать suppression файлы для Firebird клиентской библиотеки

---

## Приложение C: Оценка времени и ресурсов

### Временные затраты

| Этап | Время (дни) | Зависимости |
|------|-------------|-------------|
| 0. Подготовка | 1 | - |
| 1. Базовая CI Linux | 2-3 | 0 |
| 2. Code Quality | 1-2 | 0 |
| 3. Windows | 3-4 | 1 |
| 4. Matrix | 1-2 | 1, 3 |
| 5. Coverage | 2 | 1 |
| 6. Conan Package | 3-4 | 1, 3 |
| 7. Sanitizers | 2 | 1 |
| 8. Documentation | 2-3 | 0 |
| 9. Releases | 2 | 6 |
| 10. Claude Code | 3-4 | 1 |
| 11. Advanced | 3-5 | 1, 5 |

**Минимальный путь**: Этапы 0,1,3,6 = **9-12 дней**
**Рекомендуемый**: + Этапы 2,4,5,7,8 = **18-25 дней**
**Полный**: + Этапы 9,10,11 = **27-38 дней**

### Ресурсы GitHub Actions

Free tier для public репозиториев:
- Linux runners: безлимитно
- Windows runners: безлимитно
- macOS runners: безлимитно

Приватные репозитории:
- 2000 минут/месяц Linux
- 1000 минут/месяц Windows (2x множитель)
- 500 минут/месяц macOS (10x множитель)

Оценка использования (при полном CI/CD):
- 1 push в main: ~30 мин (Linux + Windows + sanitizers + coverage)
- 1 PR: ~15 мин (Linux + Windows, без sanitizers)
- Nightly builds: ~45 мин

**Итого**: ~500-1000 минут в месяц для активной разработки

---

## Приложение D: Best Practices

### Naming Conventions

**Branches**:
- `main` - production-ready code
- `develop` - integration branch
- `feature/feature-name` - новые фичи
- `fix/bug-description` - исправления
- `claude/task-description` - автоматические ветки от Claude Code

**Tags**:
- `v1.2.3` - релизы (semantic versioning)
- `v1.2.3-rc1` - release candidates
- `v1.2.3-beta1` - beta версии

**Workflows**:
- `ci-*.yml` - CI pipelines
- `cd-*.yml` - CD pipelines (deployment)
- `*-check.yml` - quality checks
- `*-integration.yml` - integration tests

### Commit Messages

Формат: `<type>(<scope>): <subject>`

Types:
- `feat` - новая функциональность
- `fix` - исправление бага
- `docs` - изменения документации
- `test` - добавление/изменение тестов
- `refactor` - рефакторинг кода
- `perf` - улучшение производительности
- `chore` - рутинные задачи (обновление зависимостей и т.д.)
- `ci` - изменения CI/CD

Examples:
```
feat(statement): add statement caching support
fix(int128): correct scale handling for NUMERIC(38,2)
docs(readme): add quick start guide
test(decfloat): add tests for DECFLOAT(34)
ci(windows): add Windows MSVC build
```

### PR Process

1. **Создание PR**:
   - Заполнить template с описанием изменений
   - Указать связанные issues
   - Добавить reviewers

2. **Автоматические проверки**:
   - CI должна пройти успешно
   - Code coverage не должен упасть
   - Code review от maintainers

3. **Merge стратегия**:
   - Squash and merge для feature веток
   - Merge commit для release веток
   - Rebase для мелких исправлений

### Security

- Не коммитить credentials в репозиторий
- Использовать GitHub Secrets для sensitive данных
- Регулярно обновлять зависимости
- Включить Dependabot alerts
- Запускать CodeQL security scanning

---

## Заключение

Этот план предоставляет пошаговый путь от базовой CI до полной автоматизации с интеграцией Claude Code. Рекомендуется начать с минимального набора (этапы 0-1-3-6) для быстрого получения базовой функциональности, а затем постепенно добавлять остальные компоненты по мере необходимости.

Ключевые преимущества после внедрения:
- ✅ Автоматическая проверка на Linux и Windows
- ✅ Простая установка через Conan
- ✅ Защита от регрессий
- ✅ Профессиональный имидж проекта
- ✅ Интеграция с Claude Code для ускорения разработки

**Estimated total time**: 2-6 недель в зависимости от выбранного уровня автоматизации.
