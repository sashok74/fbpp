# Архитектура fbpp v1.0

## Что получит пользователь

### Установка (3 варианта)

#### 1. CMake FetchContent (рекомендуется)
```cmake
include(FetchContent)
FetchContent_Declare(fbpp
    GIT_REPOSITORY https://github.com/your-org/fbpp.git
    GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(fbpp)

target_link_libraries(myapp PRIVATE fbpp::fbpp)
```

#### 2. Системный пакет (будущее)
```bash
# Ubuntu/Debian
sudo apt install libfbpp-dev

# Fedora/RHEL
sudo dnf install fbpp-devel

# macOS
brew install fbpp
```

#### 3. Ручная сборка
```bash
git clone https://github.com/your-org/fbpp.git
cd fbpp
./build.sh RelWithDebInfo
sudo cmake --install build
```

### Что установится

```
/usr/local/
├── lib/
│   └── libfbpp.a              # ~15-20 MB статическая библиотека
├── include/fbpp/
│   ├── fbpp.hpp                # Основной заголовок (90% случаев)
│   ├── fbpp_extended.hpp       # + расширенные типы
│   ├── fbpp_all.hpp            # Всё включено
│   ├── core/                   # Отдельные компоненты
│   │   ├── connection.hpp
│   │   ├── transaction.hpp
│   │   ├── statement.hpp
│   │   └── ...
│   └── adapters/               # Адаптеры типов
│       ├── ttmath_int128.hpp
│       ├── ttmath_numeric.hpp
│       └── ...
└── lib/cmake/fbpp/
    ├── fbppConfig.cmake        # CMake конфигурация
    └── fbppTargets.cmake

```

## Структура библиотеки

### Единая библиотека libfbpp

**Содержит:**
- **Core** (5 MB) - Connection, Transaction, Statement, ResultSet
- **Extended Types** (10 MB) - INT128, DECFLOAT via TTMath/CppDecimal
- **Utilities** (1 MB) - JSON, Batch, Logging

**Зависимости (встроены как PRIVATE):**
- TTMath - для INT128 и NUMERIC(38,x)
- CppDecimal - для DECFLOAT(16/34)
- Пользователь их НЕ видит и НЕ линкует

**Внешние зависимости (нужно установить):**
- Firebird client library (libfbclient)
- C++20 компилятор
- spdlog, nlohmann_json (через Conan)

## Использование - 3 уровня

### Уровень 1: Минимальный (90% задач)

```cpp
#include <fbpp/fbpp.hpp>

int main() {
    auto conn = fbpp::Connection("firebird5:employee");
    auto trans = conn.startTransaction();
    auto result = trans.execute("SELECT * FROM COUNTRY");

    for (auto& row : result) {
        std::cout << row["NAME"] << "\n";
    }
}
```

**Включает:** connection, transaction, statement, result_set, exception

### Уровень 2: С расширенными типами

```cpp
#include <fbpp/fbpp_extended.hpp>

int main() {
    auto conn = fbpp::Connection("firebird5:testdb");

    // INT128
    fbpp::Int128 bignum("170141183460469231731687303715884105727");

    // NUMERIC(38,4)
    fbpp::Numeric<38,4> money("1234567890123456789012345678901234.5678");

    // DECFLOAT(34)
    fbpp::DecFloat34 precise("1.234567890123456789012345678901234E+100");

    conn.execute("INSERT INTO big_numbers VALUES (?, ?, ?)",
                 bignum, money, precise);
}
```

**Дополнительно:** extended_types, timestamp_utils, все адаптеры

### Уровень 3: Полный функционал

```cpp
#include <fbpp/fbpp_all.hpp>

int main() {
    auto conn = fbpp::Connection("firebird5:testdb");

    // Batch insert
    fbpp::Batch batch(conn, "INSERT INTO users VALUES (?, ?)");
    for (int i = 0; i < 10000; i++) {
        batch.add(i, "User" + std::to_string(i));
    }
    batch.execute();

    // JSON export
    auto json = conn.query("SELECT * FROM users").to_json();
    std::cout << json.dump(2) << std::endl;
}
```

**Дополнительно:** batch, json_packer, tuple_packer, все утилиты

## Изменения для пользователей существующего кода

### До рефакторинга:
```cpp
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/exception.hpp"

// Линковка с несколькими библиотеками
target_link_libraries(myapp
    firebird_binding
    mvp_test_utils
    ttmath
    cppdecimal)
```

### После рефакторинга:
```cpp
#include <fbpp/fbpp.hpp>  // Один заголовок

// Линковка с одной библиотекой
target_link_libraries(myapp fbpp::fbpp)
```

## Ключевые преимущества v1.0

1. **Простота** - один заголовок, одна библиотека
2. **Полнота** - все расширенные типы Firebird включены
3. **Изоляция** - TTMath/CppDecimal скрыты от пользователя
4. **Гибкость** - 3 уровня использования
5. **Совместимость** - старый код продолжит работать

## Размеры

- **Минимальная установка:** ~20 MB (libfbpp.a + headers)
- **Runtime зависимости:** libfbclient (~5 MB)
- **Полная установка с примерами:** ~25 MB

## Поддержка компиляторов

- GCC 11+ (полная поддержка)
- Clang 14+ (полная поддержка)
- MSVC 2022 (планируется)

## Лицензия

MIT License - свободное использование в коммерческих проектах