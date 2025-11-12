# ТЕХНИЧЕСКОЕ ЗАДАНИЕ: Рефакторинг и исправление CI/CD для проекта fbpp

## 1. СУТЬ ПРОЕКТА

**fbpp** (Firebird Plus Plus) - современная C++20 библиотека-обертка для Firebird 5 database OO API.

### Ключевые характеристики проекта:
- **Язык**: C++20 (требуется GCC 11+ или Clang 14+)
- **Система сборки**: CMake 3.20+
- **Менеджер пакетов**: Conan 2.x
- **СУБД**: Firebird 5.x (критически важна именно версия 5.0+)
- **Фреймворк тестирования**: GoogleTest
- **Всего тестов**: 113 unit и integration тестов

### Основные возможности библиотеки:
- Type-safe упаковка/распаковка сообщений Firebird
- RAII управление ресурсами (connection, transaction, statement, result set)
- Полная поддержка расширенных типов Firebird 5: INT128, DECFLOAT(16/34), NUMERIC(38,x), TIMESTAMP/TIME WITH TIME ZONE
- Кэширование prepared statements
- Именованные параметры в SQL запросах (`:param_name`)
- Batch операции
- Работа с данными в форматах: JSON, tuple, strongly-typed objects
- Операции отмены (cancel operations)

### Архитектурные особенности:
- Template-heavy дизайн для compile-time оптимизаций
- Минимизация виртуальных функций
- Адаптеры типов для работы с расширенными типами
- Vendored библиотеки: TTMath (INT128), CppDecimal (DECFLOAT)

---

## 2. ТЕКУЩЕЕ СОСТОЯНИЕ CI/CD

### 2.1 Существующий workflow файл

**Файл**: `.github/workflows/ci-linux.yml`

**Текущие проблемы**:
1. ❌ Использует матрицу компиляторов (gcc-11, clang-14) - нужен только GCC 11
2. ❌ Матрица build_type (Release, Debug) - избыточно на первом этапе
3. ❌ Проблемы с установкой Firebird 5 из GitHub releases (сложная распаковка buildroot.tar.gz)
4. ❌ Использует GitHub Actions services для Firebird (образ `jacobalberty/firebird:v5.0`) с паролем `masterkey`, но тесты используют пароль `planomer`
5. ❌ Несоответствие параметров подключения между CI и локальной конфигурацией
6. ❌ Использует CMake presets (conan-release/conan-debug), что усложняет конфигурацию
7. ❌ Clang-format проверка выполняется только для одной комбинации матрицы

### 2.2 Триггеры workflow

```yaml
on:
  push:
    branches: [main, develop, 'claude/fix-cicd-pipeline-*']
  pull_request:
    branches: [main, develop]
  workflow_dispatch:
```

**Хорошо**: есть возможность ручного запуска через `workflow_dispatch`

---

## 3. СКРИПТЫ СБОРКИ И КОМПИЛЯЦИИ

### 3.1 Основной скрипт сборки: `build.sh`

**Расположение**: `/home/user/fbpp/build.sh`

**Что делает скрипт**:
```bash
#!/usr/bin/env bash
# Использование: ./build.sh [Debug|Release|RelWithDebInfo|MinSizeRel]

# 1. Принимает build type (по умолчанию RelWithDebInfo)
BTYPE="${1:-RelWithDebInfo}"

# 2. Очистка старых артефактов
rm -rf build
rm -rf CMakeUserPresets.json

# 3. Установка зависимостей через Conan
conan install . --output-folder=build --build=missing -s build_type="${BTYPE}"

# 4. Поиск conan_toolchain.cmake
# Может быть в двух местах:
#   - build/build/${BTYPE}/generators/conan_toolchain.cmake
#   - build/conan_toolchain.cmake

# 5. Конфигурация CMake с параметрами:
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="${PWD}/${TOOLCHAIN_FILE}" \
  -DCMAKE_BUILD_TYPE="${BTYPE}" \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=ON \
  -DFBPP_BUILD_LIBS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 6. Сборка проекта
cmake --build build -j$(nproc)

# 7. Запуск тестов
ctest --test-dir build --output-on-failure || true
```

**Ключевые особенности**:
- Скрипт выполняет полный цикл: очистка → Conan → CMake → build → test
- Использует флаг `--build=missing` для Conan (компилирует отсутствующие пакеты)
- Автоматически запускает тесты в конце
- Игнорирует ошибки тестов (`|| true`)

### 3.2 CMake конфигурация: `CMakeLists.txt`

**Основные параметры сборки**:
```cmake
cmake_minimum_required(VERSION 3.20)
project(firebird-binding-lab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

**Опции сборки**:
- `BUILD_TESTING=ON` - включает тесты
- `BUILD_EXAMPLES=ON` - включает примеры (по умолчанию OFF)
- `BUILD_CONFIG=ON` - конфигурационные файлы
- `BUILD_BASIC_TESTS=ON` - базовые инфраструктурные тесты
- `FBPP_BUILD_LIBS=ON` - сборка библиотек

**Поиск зависимостей**:
```cmake
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Firebird REQUIRED)  # через cmake/FindFirebird.cmake
```

**Структура сборки**:
1. Vendored библиотеки: `ttmath` (header-only), `cppdecimal` (static)
2. Основная библиотека: `fbpp` (static) - 11 .cpp файлов в src/core/firebird/ + 3 в src/util/
3. Утилита: `query_generator` - генератор кода из SQL запросов
4. Тесты: 16 исполняемых файлов тестов (unit + adapters)
5. Примеры: 13 примеров использования (опционально)

---

## 4. ЗАВИСИМОСТИ ПРОЕКТА

### 4.1 Зависимости через Conan

**Файл**: `conanfile.txt`

```ini
[requires]
gtest/1.14.0
spdlog/1.12.0
nlohmann_json/3.11.3

[generators]
CMakeDeps
CMakeToolchain

[layout]
cmake_layout
```

**Пояснение**:
- **gtest** - фреймворк тестирования GoogleTest
- **spdlog** - быстрая библиотека логирования
- **nlohmann_json** - популярная JSON библиотека для C++
- **Генераторы**: CMakeDeps создает find_package() файлы, CMakeToolchain - toolchain для CMake
- **Layout**: cmake_layout настраивает Conan для работы с CMake проектами

### 4.2 Vendored библиотеки (скачиваются из интернета или уже в репозитории)

**1. TTMath** (`third_party/ttmath/`)
- **Назначение**: Поддержка INT128 и NUMERIC(38,x) типов
- **Тип**: Header-only библиотека
- **CMake**: Interface library
- **Особенности**:
  - Определяет `TTMATH_NOASM` для чистого header-only использования
  - Требует C++17 минимум
  - Лицензия: BSD

**2. CppDecimal** (`third_party/cppdecimal/`)
- **Назначение**: Поддержка DECFLOAT(16) и DECFLOAT(34) типов через IBM decNumber
- **Тип**: Static библиотека
- **Состав**:
  - C файлы IBM decNumber: decContext.c, decimal32.c, decimal64.c, decimal128.c, etc.
  - C++ обертки: DecContext.cc, DecNumber.cc, DecSingle.cc, DecDouble.cc, DecQuad.cc
- **Особенности**:
  - Определяет `DECNUMDIGITS=34` для decimal128
  - Требует C++17 минимум
  - Лицензия: ICU License

**ВАЖНО**: Эти библиотеки уже находятся в репозитории в `third_party/`, их не нужно скачивать отдельно.

### 4.3 Системные зависимости

**Ubuntu 22.04 пакеты** (устанавливаются через apt):
```bash
cmake               # Система сборки
ninja-build         # Быстрый build system (опционально)
gcc-11              # Компилятор GCC 11
g++-11              # Компилятор C++ GCC 11
libstdc++-11-dev    # Стандартная библиотека C++
python3-pip         # Для установки Conan
wget                # Для скачивания Firebird
libncurses5         # Зависимость Firebird
libtommath1         # Зависимость Firebird
```

### 4.4 Firebird 5.x

**КРИТИЧЕСКИ ВАЖНО**: Требуется именно Firebird 5.0+, т.к. проект использует расширенные типы, которых нет в Firebird 3.x/4.x:
- INT128 (SQL type 32752)
- DECFLOAT(16) (SQL type 32760)
- DECFLOAT(34) (SQL type 32762)
- TIME WITH TIME ZONE (SQL type 32756)
- TIMESTAMP WITH TIME ZONE (SQL type 32754)

**Что нужно установить**:
1. **Заголовочные файлы**: `/usr/include/firebird/Interface.h` и другие headers
2. **Клиентская библиотека**: `libfbclient.so` в `/usr/lib/x86_64-linux-gnu/`

**Поиск через FindFirebird.cmake**:
```cmake
find_path(FIREBIRD_INCLUDE_DIR
    NAMES firebird/Interface.h
    PATHS /usr/include /usr/local/include /opt/firebird/include
)

find_library(FIREBIRD_LIBRARY
    NAMES fbclient
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib /opt/firebird/lib
)
```

---

## 5. FIREBIRD 5 СЕРВЕР: ТРЕБОВАНИЯ И НАСТРОЙКА

### 5.1 Версия сервера

**Требование**: Firebird 5.0.x (минимум 5.0.0)

**Docker образ**: `jacobalberty/firebird:v5.0`

### 5.2 Параметры подключения для CI

**Для Docker сервиса в GitHub Actions**:
```yaml
services:
  firebird:
    image: jacobalberty/firebird:v5.0
    env:
      FIREBIRD_USER: SYSDBA
      FIREBIRD_PASSWORD: planomer  # ВАЖНО: не masterkey!
      ISC_PASSWORD: planomer
    ports:
      - 3050:3050
    options: >-
      --health-cmd "/usr/local/firebird/bin/isql -z"
      --health-interval 10s
      --health-timeout 5s
      --health-retries 5
```

**Health check**: Используется команда `isql -z` для проверки готовности сервера.

### 5.3 Параметры подключения

**Формат connection string**: `<host>:<path>`

Примеры:
- `localhost:/tmp/fbpp_temp_test.fdb` - локальная БД
- `firebird5:/mnt/test/fbpp_temp_test.fdb` - удаленный сервер
- `192.168.7.248:testdb` - IP адрес с относительным путем

---

## 6. СТРАТЕГИИ ТЕСТИРОВАНИЯ БАЗЫ ДАННЫХ

### 6.1 Два режима работы с БД

Проект поддерживает два паттерна работы с тестовыми базами данных:

#### 6.1.1 Persistent Database (постоянная БД)

**Класс**: `PersistentDatabaseTest` (наследуется от `FbppTestBase`)

**Поведение**:
- БД создается один раз для всего test suite (`SetUpTestSuite`)
- Переиспользуется между всеми тестами в suite
- НЕ удаляется после завершения тестов
- Схема создается при первом запуске

**Конфигурация** (`config/test_config.json`):
```json
{
  "tests": {
    "persistent_db": {
      "path": "testdb",
      "server": "192.168.7.248",
      "user": "SYSDBA",
      "password": "planomer",
      "charset": "UTF8",
      "create_once": true
    }
  }
}
```

**Схема persistent БД**:
```sql
CREATE TABLE test_data (
    id INTEGER NOT NULL PRIMARY KEY,
    name VARCHAR(100),
    amount DOUBLE PRECISION,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE test_log (
    id INTEGER GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    message VARCHAR(500),
    logged_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**Использование**:
```cpp
class MyPersistentTest : public PersistentDatabaseTest {
protected:
    void SetUp() override {
        connection_ = std::make_unique<Connection>(db_params_);
    }
};
```

#### 6.1.2 Temporary Database (временная БД)

**Класс**: `TempDatabaseTest` (наследуется от `FbppTestBase`)

**Поведение**:
- БД создается заново для КАЖДОГО теста (`SetUp`)
- Удаляется после завершения теста (`TearDown`)
- К имени БД добавляется уникальный суффикс: `_<PID>_<counter>`
- Схема создается в `createTestSchema()` для каждого теста

**Конфигурация** (`config/test_config.json`):
```json
{
  "tests": {
    "temp_db": {
      "path": "/mnt/test/fbpp_temp_test.fdb",
      "server": "192.168.7.248",
      "user": "SYSDBA",
      "password": "planomer",
      "charset": "UTF8",
      "recreate_each_test": true
    }
  }
}
```

**Схема temp БД** (базовая):
```sql
CREATE TABLE test_table (
    id INTEGER NOT NULL PRIMARY KEY,
    name VARCHAR(100),
    amount DOUBLE PRECISION
);
```

**Использование**:
```cpp
class MyTempTest : public TempDatabaseTest {
protected:
    void createTestSchema() override {
        connection_->ExecuteDDL(
            "CREATE TABLE my_test_table (...)"
        );
    }
};
```

### 6.2 Переопределение параметров через environment variables

**Приоритет**: ENV vars > config file

**Переменные окружения**:
```bash
FIREBIRD_HOST=localhost              # Хост сервера
FIREBIRD_PORT=3050                   # Порт (добавляется к host)
FIREBIRD_USER=SYSDBA                 # Пользователь
FIREBIRD_PASSWORD=planomer           # Пароль
FIREBIRD_CHARSET=UTF8                # Кодировка

# Пути к БД:
FIREBIRD_DB_PATH=/tmp/fbpp_temp_test.fdb              # Для temp БД
FIREBIRD_PERSISTENT_DB_PATH=/tmp/fbpp_persistent_test.fdb  # Для persistent БД
```

**Логика определения пути БД** (из `tests/test_base.hpp:76-86`):
```cpp
bool is_relative_path = (path.find('/') == std::string::npos);
if (is_relative_path) {
    // Для относительных путей (типа "testdb") используем FIREBIRD_PERSISTENT_DB_PATH
    if (const char* env_persistent_path = std::getenv("FIREBIRD_PERSISTENT_DB_PATH")) {
        path = env_persistent_path;
    }
} else {
    // Для абсолютных путей используем FIREBIRD_DB_PATH
    if (const char* env_path = std::getenv("FIREBIRD_DB_PATH")) {
        path = env_path;
    }
}
```

### 6.3 Какие тесты используют какую стратегию

**Анализ тестов показывает**:
- Большинство тестов наследуются от `TempDatabaseTest`
- Некоторые могут использовать `PersistentDatabaseTest` для ускорения

**Для CI/CD**:
- **Рекомендация**: использовать `TempDatabaseTest` стратегию для изоляции тестов
- Настроить `FIREBIRD_DB_PATH=/tmp/fbpp_temp_test.fdb`
- Создать директорию `/tmp` перед запуском тестов (уже есть в системе)

---

## 7. ЦЕЛЕВАЯ КОНФИГУРАЦИЯ CI/CD

### 7.1 Требования к новому CI/CD

**Первоочередная цель**: Работающий CI/CD для **ОДНОЙ** конфигурации:
- **ОС**: Ubuntu 22.04
- **Компилятор**: GCC 11 (gcc-11, g++-11)
- **Build type**: RelWithDebInfo (оптимизации + отладочная информация)
- **База данных**: Firebird 5.0.x в Docker контейнере

### 7.2 Этапы CI/CD pipeline

**1. Checkout кода**
```yaml
- uses: actions/checkout@v4
```

**2. Установка системных зависимостей**
```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  gcc-11 \
  g++-11 \
  libstdc++-11-dev \
  python3-pip \
  wget \
  libncurses5 \
  libtommath1
```

**3. Установка Firebird 5 клиента**
- Скачать официальный пакет Firebird 5.0.0 для Linux x64
- Распаковать архив
- Извлечь `buildroot.tar.gz`
- Скопировать headers в `/usr/include/`
- Скопировать `libfbclient.so*` в `/usr/lib/x86_64-linux-gnu/`
- Выполнить `ldconfig`

**Альтернатива** (проще): использовать PPA или готовый Docker образ с установленным клиентом.

**4. Установка Conan**
```bash
pip3 install conan
conan --version
```

**5. Настройка Conan profile**
```bash
export CC=gcc-11
export CXX=g++-11
conan profile detect --force
```

**6. Установка зависимостей через Conan**
```bash
conan install . --output-folder=build --build=missing \
  -s build_type=RelWithDebInfo \
  -s compiler.cppstd=20
```

**7. Конфигурация CMake**

Два варианта:

**Вариант A**: Через CMake preset (conan-relwithdebinfo)
```bash
cmake --preset conan-relwithdebinfo \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=OFF
```

**Вариант B**: Прямая конфигурация
```bash
cmake -S . -B build/RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=OFF
```

**8. Сборка**
```bash
cmake --build build/RelWithDebInfo -j$(nproc)
```

**9. Запуск тестов**
```bash
cd build/RelWithDebInfo
ctest --output-on-failure --verbose
```

### 7.3 Настройка Firebird сервиса

**GitHub Actions service container**:
```yaml
services:
  firebird:
    image: jacobalberty/firebird:v5.0
    env:
      FIREBIRD_USER: SYSDBA
      FIREBIRD_PASSWORD: planomer
      ISC_PASSWORD: planomer
    ports:
      - 3050:3050
    options: >-
      --health-cmd "/usr/local/firebird/bin/isql -z"
      --health-interval 10s
      --health-timeout 5s
      --health-retries 5
```

**Environment variables для тестов**:
```yaml
env:
  FIREBIRD_HOST: localhost
  FIREBIRD_PORT: 3050
  FIREBIRD_USER: SYSDBA
  FIREBIRD_PASSWORD: planomer
  FIREBIRD_DB_PATH: /tmp/fbpp_temp_test.fdb
  FIREBIRD_PERSISTENT_DB_PATH: /tmp/fbpp_persistent_test.fdb
  FIREBIRD_CHARSET: UTF8
```

**Проверка подключения перед тестами**:
```bash
timeout 10 bash -c 'until echo > /dev/tcp/localhost/3050; do sleep 1; done' \
  && echo "✓ Firebird port 3050 is open"
```

### 7.4 Структура директорий после сборки

С опцией `cmake_layout` в Conan:
```
build/
├── RelWithDebInfo/          # Build directory для RelWithDebInfo
│   ├── tests/
│   │   ├── unit/
│   │   │   ├── test_statement
│   │   │   ├── test_core_wrapper
│   │   │   └── ...
│   │   └── adapters/
│   │       ├── test_ttmath_int128
│   │       └── ...
│   └── CTestTestfile.cmake
├── conan_toolchain.cmake
└── CMakePresets.json
```

**Запуск тестов**:
```bash
cd build/RelWithDebInfo
ctest --output-on-failure
```

---

## 8. ДЕТАЛЬНЫЙ ПЛАН РЕФАКТОРИНГА

### 8.1 Проблемы текущего CI/CD и решения

| № | Проблема | Решение |
|---|----------|---------|
| 1 | Матрица компиляторов и build types | Убрать матрицу, оставить только gcc-11 + RelWithDebInfo |
| 2 | Пароль в Firebird service (`masterkey` vs `planomer`) | Изменить пароль на `planomer` в service env |
| 3 | Сложная установка Firebird из GitHub release | Упростить: использовать Docker image с pre-installed client или проверить наличие в Ubuntu PPA |
| 4 | Несоответствие путей БД | Унифицировать ENV vars: использовать `/tmp/` для обоих типов БД |
| 5 | CMake presets с lowercase build type | Исправить: использовать `conan-relwithdebinfo` или прямую конфигурацию |
| 6 | Не создается директория для БД | Добавить `mkdir -p /tmp` перед тестами (избыточно, но безопасно) |
| 7 | Clang-format проверка только для одной комбинации | Вынести в отдельный job или убрать на первом этапе |
| 8 | Тесты могут падать из-за конфликта БД | Использовать уникальные имена через PID + counter (уже реализовано) |

### 8.2 Новая структура workflow

**Файл**: `.github/workflows/ci-linux-gcc.yml`

**Основные секции**:

```yaml
name: CI - Linux GCC 11

on:
  push:
    branches: [main, develop, 'claude/**']
  pull_request:
    branches: [main, develop]
  workflow_dispatch:

jobs:
  build-and-test-gcc:
    name: Build and Test (GCC 11, RelWithDebInfo)
    runs-on: ubuntu-22.04
    timeout-minutes: 30

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
          ISC_PASSWORD: planomer
        ports:
          - 3050:3050
        options: >-
          --health-cmd "/usr/local/firebird/bin/isql -z"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      - name: Checkout
      - name: Install system dependencies
      - name: Install Firebird 5 client
      - name: Install Conan
      - name: Configure Conan
      - name: Install Conan dependencies
      - name: Configure CMake
      - name: Build
      - name: Run tests
```

### 8.3 Упрощение установки Firebird клиента

**Проблема**: Текущий метод со скачиванием tar.gz и распаковкой buildroot сложен и хрупок.

**Решение 1** (рекомендуемое): Использовать Docker multi-stage build
```dockerfile
# Stage 1: Extract Firebird client from official image
FROM jacobalberty/firebird:v5.0 as firebird-client
# Client library already installed

# Stage 2: Copy client to runner
# Copy libfbclient.so and headers to GitHub Actions runner
```

**Решение 2**: Pre-built пакет
- Создать собственный GitHub release с pre-extracted Firebird client
- Скачивать его одной командой `wget + tar xzf`

**Решение 3**: Использовать готовый пакет
```bash
# Проверить доступность в Ubuntu PPA
sudo add-apt-repository ppa:mapopa/firebird5.0
sudo apt-get update
sudo apt-get install -y firebird5.0-dev libfbclient2
```

### 8.4 Проверки перед запуском тестов

**1. Проверка установки Firebird клиента**:
```bash
echo "=== Firebird client verification ==="
ls -la /usr/include/firebird/Interface.h || echo "ERROR: Headers not found"
ls -la /usr/lib/x86_64-linux-gnu/libfbclient.so* || echo "ERROR: Library not found"
ldconfig -p | grep fbclient || echo "ERROR: Library not registered"
```

**2. Проверка подключения к Firebird service**:
```bash
echo "=== Testing Firebird connectivity ==="
timeout 10 bash -c 'until echo > /dev/tcp/localhost/3050; do sleep 1; done' \
  && echo "✓ Firebird is ready" \
  || echo "✗ Cannot connect to Firebird"
```

**3. Вывод environment variables**:
```bash
echo "=== Firebird connection settings ==="
echo "FIREBIRD_HOST=$FIREBIRD_HOST"
echo "FIREBIRD_PORT=$FIREBIRD_PORT"
echo "FIREBIRD_USER=$FIREBIRD_USER"
echo "FIREBIRD_DB_PATH=$FIREBIRD_DB_PATH"
```

### 8.5 Обработка ошибок

**Fail-fast поведение**:
- Если установка зависимостей fails → останавливаем workflow
- Если сборка fails → останавливаем workflow
- Если тесты fails → workflow должен пометиться как failed

**Убрать `|| true`** из команды запуска тестов:
```bash
# ❌ Плохо:
ctest --output-on-failure || true

# ✅ Хорошо:
ctest --output-on-failure
```

---

## 9. СПЕЦИФИКА РАБОТЫ С ИИ

### 9.1 Автоматизация для ИИ агента

**Контекст**: CI/CD будет создаваться и запускаться ИИ агентом (например, Claude Code или GitHub Copilot).

**Требования**:

1. **Самодостаточность**: Workflow должен содержать всё необходимое для сборки и тестирования
2. **Детальный вывод**: Использовать `--verbose` флаги для диагностики
3. **Проверки на каждом этапе**: Выводить результаты проверок (`ls -la`, `--version`, etc.)
4. **Комментарии в YAML**: Пояснять, что делает каждый step
5. **Имена steps**: Понятные и описательные названия

### 9.2 Триггеры для тестирования

**Для тестирования ИИ должен иметь возможность**:

1. **Push триггер**: Автоматический запуск при push в ветки `claude/**`
   ```yaml
   on:
     push:
       branches: ['claude/**']
   ```

2. **Manual dispatch**: Ручной запуск через GitHub UI или API
   ```yaml
   workflow_dispatch:
     inputs:
       debug_mode:
         description: 'Enable debug output'
         required: false
         default: 'false'
   ```

3. **API запуск**: Через GitHub REST API
   ```bash
   curl -X POST \
     -H "Authorization: token $GITHUB_TOKEN" \
     -H "Accept: application/vnd.github.v3+json" \
     https://api.github.com/repos/sashok74/fbpp/actions/workflows/ci-linux-gcc.yml/dispatches \
     -d '{"ref":"claude/fix-cicd-pipeline-XXX"}'
   ```

### 9.3 Проверка результатов ИИ

**ИИ должен уметь**:

1. **Читать статус workflow**:
   ```bash
   gh run list --workflow=ci-linux-gcc.yml --limit=1
   ```

2. **Получать логи**:
   ```bash
   gh run view <run-id> --log
   ```

3. **Анализировать ошибки**:
   - Парсить вывод ctest для failed тестов
   - Искать ошибки компиляции в build логах
   - Проверять connection errors к Firebird

4. **Итерировать**:
   - Если тесты падают → анализ → фикс → коммит → push → повторный запуск

### 9.4 Рекомендуемый workflow для ИИ

**Шаг 1**: Создать новую ветку
```bash
git checkout -b claude/cicd-refactor-<session-id>
```

**Шаг 2**: Создать новый workflow файл
```bash
# Создать .github/workflows/ci-linux-gcc.yml
# (см. детальную спецификацию в разделе 10)
```

**Шаг 3**: Закоммитить и запушить
```bash
git add .github/workflows/ci-linux-gcc.yml
git commit -m "Add refactored CI/CD for Linux GCC 11"
git push -u origin claude/cicd-refactor-<session-id>
```

**Шаг 4**: Запустить workflow
```bash
gh workflow run ci-linux-gcc.yml --ref claude/cicd-refactor-<session-id>
```

**Шаг 5**: Мониторить выполнение
```bash
gh run watch
```

**Шаг 6**: Анализ и итерации
```bash
# Если failed:
gh run view <run-id> --log > ci-log.txt
# Анализировать логи, исправить проблему, повторить шаги 3-5
```

---

## 10. ДЕТАЛЬНАЯ СПЕЦИФИКАЦИЯ НОВОГО WORKFLOW

### 10.1 Полный workflow файл

**Файл**: `.github/workflows/ci-linux-gcc.yml`

```yaml
name: CI - Linux GCC 11

on:
  push:
    branches:
      - main
      - develop
      - 'claude/**'
  pull_request:
    branches:
      - main
      - develop
  workflow_dispatch:
    inputs:
      debug_mode:
        description: 'Enable verbose debug output'
        required: false
        default: 'false'
        type: boolean

env:
  BUILD_TYPE: RelWithDebInfo
  CC: gcc-11
  CXX: g++-11

jobs:
  build-and-test:
    name: Build and Test (GCC 11, RelWithDebInfo)
    runs-on: ubuntu-22.04
    timeout-minutes: 30

    services:
      firebird:
        image: jacobalberty/firebird:v5.0
        env:
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
          ISC_PASSWORD: planomer
        ports:
          - 3050:3050
        options: >-
          --health-cmd "/usr/local/firebird/bin/isql -z"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

    steps:
      # ============================================
      # Step 1: Checkout repository
      # ============================================
      - name: Checkout code
        uses: actions/checkout@v4

      # ============================================
      # Step 2: Display system information
      # ============================================
      - name: Display system information
        run: |
          echo "========================================="
          echo "System Information"
          echo "========================================="
          uname -a
          echo ""
          echo "CPU info:"
          nproc
          echo ""
          echo "Memory info:"
          free -h
          echo ""
          echo "Disk space:"
          df -h
          echo ""

      # ============================================
      # Step 3: Install system dependencies
      # ============================================
      - name: Install system dependencies
        run: |
          echo "========================================="
          echo "Installing system dependencies"
          echo "========================================="
          sudo apt-get update

          sudo apt-get install -y \
            cmake \
            gcc-11 \
            g++-11 \
            libstdc++-11-dev \
            python3-pip \
            wget \
            libncurses5 \
            libtommath1

          echo ""
          echo "✓ System dependencies installed"

          # Verify compiler installation
          echo ""
          echo "Compiler versions:"
          gcc-11 --version | head -n1
          g++-11 --version | head -n1
          cmake --version | head -n1

      # ============================================
      # Step 4: Install Firebird 5 client library
      # ============================================
      - name: Install Firebird 5.0 client
        run: |
          echo "========================================="
          echo "Installing Firebird 5.0 client library"
          echo "========================================="

          # Download Firebird 5.0.0 for Linux x64
          wget -q https://github.com/FirebirdSQL/firebird/releases/download/v5.0.0/Firebird-5.0.0.1306-0-linux-x64.tar.gz

          echo "✓ Downloaded Firebird archive"

          # Extract main archive
          tar xzf Firebird-5.0.0.1306-0-linux-x64.tar.gz
          cd Firebird-5.0.0.1306-0-linux-x64

          # Extract buildroot which contains actual files
          echo "→ Extracting buildroot..."
          tar xzf buildroot.tar.gz

          # Verify extraction
          echo ""
          echo "Checking extracted structure:"
          ls -la opt/firebird/include/ 2>/dev/null || echo "✗ Headers not found"
          ls -la opt/firebird/lib/ 2>/dev/null || echo "✗ Library not found"

          # Copy headers to system directory
          echo ""
          echo "→ Installing Firebird headers..."
          sudo cp -rv opt/firebird/include/* /usr/include/

          # Copy library to system directory
          echo ""
          echo "→ Installing Firebird library..."
          sudo cp -v opt/firebird/lib/libfbclient.so* /usr/lib/x86_64-linux-gnu/

          # Update library cache
          sudo ldconfig

          cd ..

          # Verify installation
          echo ""
          echo "========================================="
          echo "Firebird client verification"
          echo "========================================="
          echo "Headers:"
          ls -la /usr/include/firebird/Interface.h
          echo ""
          echo "Library:"
          ls -la /usr/lib/x86_64-linux-gnu/libfbclient.so*
          echo ""
          echo "Library cache:"
          ldconfig -p | grep fbclient
          echo ""
          echo "✓ Firebird 5.0 client installed successfully"

      # ============================================
      # Step 5: Install Conan package manager
      # ============================================
      - name: Install Conan
        run: |
          echo "========================================="
          echo "Installing Conan package manager"
          echo "========================================="
          pip3 install conan
          conan --version
          echo ""
          echo "✓ Conan installed"

      # ============================================
      # Step 6: Configure Conan profile
      # ============================================
      - name: Setup Conan profile
        run: |
          echo "========================================="
          echo "Configuring Conan profile"
          echo "========================================="

          # Detect default profile
          conan profile detect --force

          # Display detected profile
          echo ""
          echo "Conan profile:"
          conan profile show
          echo ""
          echo "✓ Conan profile configured"

      # ============================================
      # Step 7: Install Conan dependencies
      # ============================================
      - name: Install Conan dependencies
        run: |
          echo "========================================="
          echo "Installing Conan dependencies"
          echo "========================================="

          conan install . \
            --output-folder=build \
            --build=missing \
            -s build_type=${{ env.BUILD_TYPE }} \
            -s compiler.cppstd=20

          echo ""
          echo "✓ Conan dependencies installed"

          # Display generated files
          echo ""
          echo "Generated Conan files:"
          ls -la build/

      # ============================================
      # Step 8: Configure CMake
      # ============================================
      - name: Configure CMake
        run: |
          echo "========================================="
          echo "Configuring CMake"
          echo "========================================="

          # Check for CMake presets
          if [ -f "CMakePresets.json" ]; then
            echo "CMakePresets.json found:"
            cat CMakePresets.json
            echo ""
            echo "Available presets:"
            cmake --list-presets
            echo ""
          fi

          # Find Conan toolchain file
          TOOLCHAIN_FILE=""
          if [ -f "build/conan_toolchain.cmake" ]; then
            TOOLCHAIN_FILE="build/conan_toolchain.cmake"
          elif [ -f "build/build/${{ env.BUILD_TYPE }}/generators/conan_toolchain.cmake" ]; then
            TOOLCHAIN_FILE="build/build/${{ env.BUILD_TYPE }}/generators/conan_toolchain.cmake"
          else
            echo "✗ ERROR: conan_toolchain.cmake not found"
            exit 1
          fi

          echo "Using toolchain: $TOOLCHAIN_FILE"
          echo ""

          # Configure with CMake
          cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE="${PWD}/${TOOLCHAIN_FILE}" \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DCMAKE_C_COMPILER=${{ env.CC }} \
            -DCMAKE_CXX_COMPILER=${{ env.CXX }} \
            -DBUILD_TESTING=ON \
            -DBUILD_EXAMPLES=OFF \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_VERBOSE_MAKEFILE=${{ github.event.inputs.debug_mode == 'true' && 'ON' || 'OFF' }}

          echo ""
          echo "✓ CMake configured successfully"

      # ============================================
      # Step 9: Build project
      # ============================================
      - name: Build
        run: |
          echo "========================================="
          echo "Building project"
          echo "========================================="

          cmake --build build -j$(nproc)

          echo ""
          echo "✓ Build completed successfully"

          # Display built test executables
          echo ""
          echo "Built test executables:"
          find build -type f -executable -name "test_*" | head -n 20

      # ============================================
      # Step 10: Verify Firebird service connectivity
      # ============================================
      - name: Verify Firebird connectivity
        run: |
          echo "========================================="
          echo "Verifying Firebird service connectivity"
          echo "========================================="

          # Wait for Firebird to be ready
          timeout 30 bash -c 'until echo > /dev/tcp/localhost/3050; do sleep 1; done' \
            && echo "✓ Firebird service is accessible on port 3050" \
            || (echo "✗ ERROR: Cannot connect to Firebird on port 3050" && exit 1)

          echo ""
          echo "Firebird connection parameters:"
          echo "  Host: localhost"
          echo "  Port: 3050"
          echo "  User: SYSDBA"
          echo "  Password: planomer"

      # ============================================
      # Step 11: Run tests
      # ============================================
      - name: Run tests
        env:
          FIREBIRD_HOST: localhost
          FIREBIRD_PORT: 3050
          FIREBIRD_USER: SYSDBA
          FIREBIRD_PASSWORD: planomer
          FIREBIRD_DB_PATH: /tmp/fbpp_temp_test.fdb
          FIREBIRD_PERSISTENT_DB_PATH: /tmp/fbpp_persistent_test.fdb
          FIREBIRD_CHARSET: UTF8
        run: |
          echo "========================================="
          echo "Running tests"
          echo "========================================="

          echo "Environment variables:"
          echo "  FIREBIRD_HOST=$FIREBIRD_HOST"
          echo "  FIREBIRD_PORT=$FIREBIRD_PORT"
          echo "  FIREBIRD_USER=$FIREBIRD_USER"
          echo "  FIREBIRD_DB_PATH=$FIREBIRD_DB_PATH"
          echo "  FIREBIRD_PERSISTENT_DB_PATH=$FIREBIRD_PERSISTENT_DB_PATH"
          echo ""

          # Create temp directory (should exist, but ensure)
          mkdir -p /tmp

          # Run CTest
          cd build
          ctest --output-on-failure --verbose

          echo ""
          echo "========================================="
          echo "✓ All tests passed"
          echo "========================================="

      # ============================================
      # Step 12: Upload build artifacts on failure
      # ============================================
      - name: Upload build logs on failure
        if: failure()
        uses: actions/upload-artifact@v3
        with:
          name: build-logs
          path: |
            build/CMakeFiles/CMakeOutput.log
            build/CMakeFiles/CMakeError.log
            build/Testing/Temporary/LastTest.log
          if-no-files-found: ignore

      # ============================================
      # Step 13: Display summary
      # ============================================
      - name: Summary
        if: always()
        run: |
          echo "========================================="
          echo "CI/CD Pipeline Summary"
          echo "========================================="
          echo "Build type: ${{ env.BUILD_TYPE }}"
          echo "Compiler: ${{ env.CC }} / ${{ env.CXX }}"
          echo "Status: ${{ job.status }}"
          echo "========================================="
```

### 10.2 Альтернативный вариант с CMake preset

Если использовать CMake preset вместо прямой конфигурации:

```yaml
# Step 8: Configure CMake (alternative with preset)
- name: Configure CMake (with preset)
  run: |
    echo "========================================="
    echo "Configuring CMake with preset"
    echo "========================================="

    # Conan generates CMakePresets.json with cmake_layout
    cmake --preset conan-$(echo "${{ env.BUILD_TYPE }}" | tr '[:upper:]' '[:lower:]') \
      -DBUILD_TESTING=ON \
      -DBUILD_EXAMPLES=OFF

    echo ""
    echo "✓ CMake configured with preset"

# Step 9: Build (alternative with preset)
- name: Build (with preset)
  run: |
    echo "========================================="
    echo "Building project with preset"
    echo "========================================="

    cmake --build --preset conan-$(echo "${{ env.BUILD_TYPE }}" | tr '[:upper:]' '[:lower:]')

    echo ""
    echo "✓ Build completed"
```

**Проблема**: Preset name должен быть `conan-relwithdebinfo` (lowercase).

---

## 11. КРИТЕРИИ УСПЕХА

### 11.1 Минимальные требования (MVP)

**CI/CD считается работающим, если**:

✅ **Сборка проходит успешно**
- Все зависимости устанавливаются
- CMake конфигурируется без ошибок
- Проект компилируется без ошибок (0 errors)
- Могут быть warnings, но не errors

✅ **Все тесты проходят**
- 113/113 тестов проходят (PASSED)
- Нет failed, skipped или timeout тестов
- Подключение к Firebird работает
- Обе стратегии БД (persistent + temp) работают

✅ **Процесс автоматизирован**
- Workflow запускается автоматически при push
- Можно запустить вручную через workflow_dispatch
- Результаты видны в GitHub Actions UI
- Логи доступны для анализа

### 11.2 Дополнительные критерии качества

🎯 **Производительность**:
- Полный цикл CI/CD выполняется за < 15 минут
- Установка зависимостей: < 5 минут
- Сборка проекта: < 3 минуты
- Запуск тестов: < 5 минут

🎯 **Надежность**:
- 95%+ success rate при повторных запусках
- Нет flaky тестов (нестабильных)
- Health check Firebird работает корректно

🎯 **Информативность**:
- Логи детальные и понятные
- При падении теста понятна причина
- Артефакты загружаются при failures

### 11.3 Checklist для приемки

**Перед закрытием задачи проверить**:

- [ ] Новый workflow файл создан: `.github/workflows/ci-linux-gcc.yml`
- [ ] Старый файл переименован или удален: `.github/workflows/ci-linux.yml`
- [ ] Workflow успешно выполнился минимум 3 раза подряд
- [ ] Все 113 теста проходят
- [ ] Firebird 5 client корректно устанавливается
- [ ] Параметры подключения к БД соответствуют конфигурации
- [ ] Environment variables корректно передаются тестам
- [ ] Обе стратегии БД (persistent + temp) работают
- [ ] Логи детальные и информативные
- [ ] Время выполнения приемлемое (< 15 минут)
- [ ] Нет hard-coded значений (все через ENV или параметры)
- [ ] Документация обновлена (если необходимо)

---

## 12. ВОЗМОЖНЫЕ ПРОБЛЕМЫ И РЕШЕНИЯ

### 12.1 Частые проблемы

| Проблема | Симптомы | Решение |
|----------|----------|---------|
| Firebird client не найден | `fatal error: firebird/Interface.h: No such file or directory` | Проверить установку headers в `/usr/include/firebird/` |
| Библиотека libfbclient не найдена | `cannot find -lfbclient` | Проверить наличие библиотеки и выполнить `ldconfig` |
| Не подключается к Firebird сервису | `Unable to complete network request` | Проверить health check, порт 3050, пароль `planomer` |
| Тесты падают с ошибкой БД | `database file already exists` или `lock conflict` | Проверить уникальность имен БД (PID + counter) |
| Conan не находит пакеты | `ERROR: Package 'gtest/1.14.0' not found` | Добавить `--build=missing` или проверить Conan remote |
| CMake toolchain не найден | `ERROR: conan_toolchain.cmake not found` | Проверить output-folder Conan и путь к toolchain |
| Wrong build type в preset | `Unknown CMake preset: conan-RelWithDebInfo` | Использовать lowercase: `conan-relwithdebinfo` |
| Timeout при сборке | Job killed после 30 минут | Увеличить timeout или оптимизировать сборку |

### 12.2 Диагностические команды

**Проверка установки Firebird**:
```bash
# Headers
find /usr -name Interface.h 2>/dev/null

# Library
find /usr -name "libfbclient.so*" 2>/dev/null
ldconfig -p | grep fbclient

# Test connection
echo > /dev/tcp/localhost/3050 && echo "Port open" || echo "Port closed"
```

**Проверка Conan**:
```bash
# Profile
conan profile show

# Cache
conan cache path gtest/1.14.0

# Installed packages
conan list "*"
```

**Проверка CMake**:
```bash
# Presets
cmake --list-presets

# Generated files
ls -la build/CMakeFiles/

# Compile commands
cat build/compile_commands.json | jq '.[0]'
```

**Проверка тестов**:
```bash
# List tests
ctest --test-dir build -N

# Run specific test
ctest --test-dir build -R test_statement -V

# Show test output
cat build/Testing/Temporary/LastTest.log
```

---

## 13. ПЛАН ВЫПОЛНЕНИЯ ДЛЯ ИИ

### 13.1 Последовательность действий

**Этап 1: Подготовка** (5 минут)
1. Создать новую ветку `claude/cicd-refactor-<session-id>`
2. Изучить текущий workflow `.github/workflows/ci-linux.yml`
3. Прочитать документацию: `CLAUDE.md`, конфигурацию `config/test_config.json`

**Этап 2: Создание нового workflow** (10 минут)
1. Создать файл `.github/workflows/ci-linux-gcc.yml`
2. Скопировать спецификацию из раздела 10.1 этого ТЗ
3. Адаптировать под специфику проекта (если нужно)
4. Добавить комментарии для понятности

**Этап 3: Тестирование** (30-60 минут)
1. Закоммитить и запушить новый workflow
2. Запустить workflow через `workflow_dispatch` или push
3. Мониторить выполнение через `gh run watch`
4. Анализировать логи при ошибках
5. Итеративно исправлять проблемы
6. Повторять до успешного прохождения всех тестов

**Этап 4: Валидация** (15 минут)
1. Запустить workflow 3 раза подряд для проверки стабильности
2. Убедиться, что все 113 теста проходят
3. Проверить время выполнения (должно быть < 15 минут)
4. Проверить информативность логов

**Этап 5: Финализация** (10 минут)
1. Переименовать или удалить старый workflow
2. Обновить документацию (если нужно)
3. Создать PR с описанием изменений
4. Отметить выполненные пункты в checklist

**Общее время**: ~1.5-2 часа

### 13.2 Команды для ИИ

**Создание ветки**:
```bash
git checkout -b claude/cicd-refactor-011CV3MJmzRegzhwwpBxz6bM
```

**Создание workflow файла**:
```bash
mkdir -p .github/workflows
# Создать файл .github/workflows/ci-linux-gcc.yml
# с содержимым из раздела 10.1
```

**Коммит и push**:
```bash
git add .github/workflows/ci-linux-gcc.yml
git commit -m "Refactor CI/CD: simplify to GCC 11 only, fix Firebird connection

- Remove compiler/build type matrix
- Use only GCC 11 + RelWithDebInfo
- Fix Firebird password to 'planomer'
- Simplify Firebird client installation
- Add detailed logging and checks
- Ensure all 113 tests pass"

git push -u origin claude/cicd-refactor-011CV3MJmzRegzhwwpBxz6bM
```

**Запуск workflow** (автоматически после push или вручную):
```bash
gh workflow run ci-linux-gcc.yml --ref claude/cicd-refactor-011CV3MJmzRegzhwwpBxz6bM
```

**Мониторинг**:
```bash
gh run watch
# или
gh run list --workflow=ci-linux-gcc.yml --limit=5
```

**Просмотр логов**:
```bash
gh run view <run-id> --log
# или для конкретного job
gh run view <run-id> --log --job=<job-id>
```

**При ошибках**:
```bash
# Сохранить лог для анализа
gh run view <run-id> --log > ci-failure.log

# Исправить проблему
# Закоммитить
git add -A
git commit -m "Fix CI: <описание исправления>"
git push

# Workflow запустится автоматически
```

---

## 14. ДОПОЛНИТЕЛЬНЫЕ РЕКОМЕНДАЦИИ

### 14.1 Оптимизация производительности

**Кэширование Conan**:
```yaml
- name: Cache Conan packages
  uses: actions/cache@v3
  with:
    path: ~/.conan2
    key: conan-${{ runner.os }}-${{ hashFiles('conanfile.txt') }}
    restore-keys: |
      conan-${{ runner.os }}-
```

**Кэширование APT пакетов**:
```yaml
- name: Cache APT packages
  uses: actions/cache@v3
  with:
    path: /var/cache/apt/archives
    key: apt-${{ runner.os }}-${{ hashFiles('.github/workflows/ci-linux-gcc.yml') }}
```

**Кэширование Firebird client**:
Создать отдельный release с pre-extracted Firebird client для быстрой установки.

### 14.2 Улучшения после MVP

**После того как базовый CI/CD заработает**, можно добавить:

1. **Матрицу компиляторов** (GCC 11, GCC 12, GCC 13, Clang 14, Clang 15)
2. **Матрицу build types** (Debug, Release, RelWithDebInfo)
3. **Code coverage** (gcov + codecov.io)
4. **Static analysis** (clang-tidy, cppcheck)
5. **Sanitizers** (AddressSanitizer, UndefinedBehaviorSanitizer)
6. **Clang-format** проверку форматирования
7. **Documentation generation** (Doxygen)
8. **Benchmark tests** (Google Benchmark)
9. **Windows CI** (MSVC компилятор)
10. **MacOS CI** (Clang на macOS)

### 14.3 Безопасность

**Не хранить секреты в plain text**:
- Использовать GitHub Secrets для паролей
- Пароль `planomer` можно оставить для тестовой БД, т.к. это не production

**Пример использования secrets**:
```yaml
env:
  FIREBIRD_PASSWORD: ${{ secrets.FIREBIRD_TEST_PASSWORD }}
```

### 14.4 Мониторинг

**GitHub Actions Badges**:
Добавить badge в README.md:
```markdown
![CI Status](https://github.com/sashok74/fbpp/workflows/CI%20-%20Linux%20GCC%2011/badge.svg)
```

**Notifications**:
Настроить уведомления в Slack/Discord при failures (опционально).

---

## 15. ЗАКЛЮЧЕНИЕ

### 15.1 Резюме ТЗ

Данное техническое задание описывает полный процесс рефакторинга CI/CD для проекта **fbpp** с фокусом на:

1. **Упрощение**: одна конфигурация (GCC 11, RelWithDebInfo) вместо матрицы
2. **Стабильность**: корректная установка Firebird 5, правильные параметры подключения
3. **Автоматизация**: полностью автоматический pipeline от checkout до тестов
4. **Информативность**: детальные логи для диагностики проблем
5. **ИИ-friendly**: структура и команды для работы ИИ агента

### 15.2 Ожидаемый результат

После выполнения этого ТЗ:
- ✅ CI/CD запускается автоматически при push в ветки `main`, `develop`, `claude/**`
- ✅ Все 113 теста проходят успешно
- ✅ Время выполнения < 15 минут
- ✅ Логи детальные и понятные
- ✅ ИИ может запускать и анализировать результаты самостоятельно

### 15.3 Контактная информация

**Репозиторий**: `sashok74/fbpp`
**Ветка для разработки**: `claude/cicd-refactor-011CV3MJmzRegzhwwpBxz6bM`
**Исполнитель**: ИИ агент (Claude Code)
**Целевая платформа**: GitHub Actions

---

**ВЕРСИЯ ДОКУМЕНТА**: 1.0
**ДАТА**: 2025-11-12
**СТАТУС**: Готов к выполнению
