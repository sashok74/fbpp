# План реализации поддержки адаптеров в query_generator

## Дата: 2025-11-12
## Автор: Development Team
## Цель: Добавить поддержку специализированных адаптеров типов в query_generator

---

## Стратегия реализации

### Подход: Поэтапная реализация с инкрементальным тестированием

**Фаза 1:** Все параметры командной строки + только TTMath адаптеры (NUMERIC + INT128)
**Фаза 2:** Добавление CppDecimal адаптеров (DECFLOAT)
**Фаза 3:** Добавление std::chrono адаптеров (DATE/TIME/TIMESTAMP)

После каждой фазы: полная генерация, тесты, проверка работоспособности.

---

## ФАЗА 1: TTMath адаптеры + инфраструктура

### Цель фазы
- Добавить все параметры командной строки
- Реализовать поддержку TTMath для NUMERIC(38,x) и INT128
- Генерировать type aliases
- Создать полноценный тест

---

### Этап 1.1: Расширение структуры Options и парсинга аргументов

**Файл:** `src/tools/query_generator.cpp`

**Изменения в строках 23-31 (struct Options):**

```cpp
// БЫЛО:
struct Options {
    std::string dsn;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::filesystem::path inputPath;
    std::filesystem::path outputHeader;
    std::filesystem::path supportHeader;
};

// СТАНЕТ:
struct Options {
    std::string dsn;
    std::string user = "SYSDBA";
    std::string password = "planomer";
    std::string charset = "UTF8";
    std::filesystem::path inputPath;
    std::filesystem::path outputHeader;
    std::filesystem::path supportHeader;

    // Флаги адаптеров (добавляем ВСЕ сразу, реализуем поэтапно)
    bool useAdapters = false;           // --use-adapters (мастер-флаг)
    bool useTTMathNumeric = false;      // --use-ttmath-numeric
    bool useTTMathInt128 = false;       // --use-ttmath-int128
    bool useChrono = false;             // --use-chrono (пока не реализовано)
    bool useCppDecimal = false;         // --use-cppdecimal (пока не реализовано)
    bool generateAliases = true;        // --generate-aliases / --no-aliases
};
```

**Изменения в строках 39-77 (функция parseOptions):**

Добавить после строки 63 (`} else if (arg == "--support") {`):

```cpp
        } else if (arg == "--support") {
            opts.supportHeader = next();
        // === НОВЫЙ КОД: Парсинг флагов адаптеров ===
        } else if (arg == "--use-adapters") {
            opts.useAdapters = true;
            opts.useTTMathNumeric = true;
            opts.useTTMathInt128 = true;
            opts.useChrono = true;
            opts.useCppDecimal = true;
        } else if (arg == "--use-ttmath-numeric") {
            opts.useTTMathNumeric = true;
        } else if (arg == "--use-ttmath-int128") {
            opts.useTTMathInt128 = true;
        } else if (arg == "--use-chrono") {
            opts.useChrono = true;
        } else if (arg == "--use-cppdecimal") {
            opts.useCppDecimal = true;
        } else if (arg == "--no-aliases") {
            opts.generateAliases = false;
        // === КОНЕЦ НОВОГО КОДА ===
        } else if (arg == "--help" || arg == "-h") {
```

**Изменения в строках 33-37 (функция printUsage):**

```cpp
// БЫЛО:
void printUsage() {
    std::cout << "Usage: query_generator --dsn <dsn> [--user name] [--password pass] "
                 "[--charset utf8] --input <queries.json> --output <queries.hpp> "
                 "--support <queries.structs.hpp>\n";
}

// СТАНЕТ:
void printUsage() {
    std::cout <<
        "Usage: query_generator [options]\n\n"
        "Required:\n"
        "  --dsn <dsn>              Database DSN (e.g., firebird5:testdb)\n"
        "  --input <file>           Input queries JSON file\n"
        "  --output <file>          Output main header file\n"
        "  --support <file>         Output support header (StructDescriptor)\n\n"
        "Optional database:\n"
        "  --user <name>            Database user (default: SYSDBA)\n"
        "  --password <pass>        Database password (default: planomer)\n"
        "  --charset <charset>      Character set (default: UTF8)\n\n"
        "Type adapter options:\n"
        "  --use-adapters           Enable ALL adapters (TTMath, chrono, CppDecimal)\n"
        "  --use-ttmath-numeric     Use TTMath for NUMERIC types with scale\n"
        "  --use-ttmath-int128      Use TTMath for pure INT128 types (scale=0)\n"
        "  --use-chrono             Use std::chrono for DATE/TIME/TIMESTAMP types\n"
        "  --use-cppdecimal         Use CppDecimal adapters for DECFLOAT types\n\n"
        "Code generation:\n"
        "  --no-aliases             Disable type alias generation (use full names)\n"
        "  --help, -h               Show this help message\n\n"
        "Examples:\n"
        "  # Basic usage (core types only):\n"
        "  query_generator --dsn firebird5:testdb --input queries.json \\\n"
        "      --output generated.hpp --support generated_support.hpp\n\n"
        "  # With TTMath for numeric types:\n"
        "  query_generator --dsn firebird5:testdb --use-ttmath-numeric \\\n"
        "      --input queries.json --output generated.hpp --support generated_support.hpp\n\n"
        "  # Enable all adapters:\n"
        "  query_generator --dsn firebird5:testdb --use-adapters \\\n"
        "      --input queries.json --output generated.hpp --support generated_support.hpp\n";
}
```

---

### Этап 1.2: Создание структуры конфигурации адаптеров

**Файл:** `include/fbpp/query_generator_service.hpp`

**Изменения после строки 15 (после includes, перед namespace):**

```cpp
// ДОБАВИТЬ:
namespace fbpp::core {

/// Конфигурация для выбора адаптеров типов при генерации кода
struct AdapterConfig {
    bool useTTMathNumeric = false;   ///< TTMath для NUMERIC с scale != 0
    bool useTTMathInt128 = false;    ///< TTMath для чистого INT128 (scale = 0)
    bool useChrono = false;          ///< std::chrono для DATE/TIME/TIMESTAMP
    bool useCppDecimal = false;      ///< CppDecimal для DECFLOAT
    bool generateAliases = true;     ///< Генерировать type aliases для читаемости
};

} // namespace fbpp::core
```

**Изменения в строках 80-82 (объявления функций рендеринга):**

```cpp
// БЫЛО:
std::string renderQueryGeneratorMainHeader(
    const std::vector<QuerySpec>& specs,
    const std::string& supportHeaderName);

std::string renderQueryGeneratorSupportHeader(
    const std::vector<QuerySpec>& specs);

// СТАНЕТ:
std::string renderQueryGeneratorMainHeader(
    const std::vector<QuerySpec>& specs,
    const std::string& supportHeaderName,
    const AdapterConfig& config = {});  // Добавлен параметр config с default

std::string renderQueryGeneratorSupportHeader(
    const std::vector<QuerySpec>& specs);
```

**Изменения в строке 51 (класс QueryGeneratorService):**

```cpp
// БЫЛО:
std::vector<QuerySpec> buildQuerySpecs(const std::vector<QueryDefinition>& definitions) const;

// СТАНЕТ:
std::vector<QuerySpec> buildQuerySpecs(
    const std::vector<QueryDefinition>& definitions,
    const AdapterConfig& config = {}) const;  // Добавлен параметр config
```

---

### Этап 1.3: Расширение структуры TypeMapping

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в строках 38-43 (struct TypeMapping):**

```cpp
// БЫЛО:
struct TypeMapping {
    std::string cppType;
    bool needsExtendedTypes = false;
    bool needsOptional = false;
};

// СТАНЕТ:
struct TypeMapping {
    std::string cppType;
    bool needsExtendedTypes = false;
    bool needsOptional = false;

    // === НОВЫЕ ПОЛЯ для адаптеров ===
    bool needsTTMath = false;         // Требуется #include для TTMath
    bool needsChrono = false;         // Требуется #include <chrono>
    bool needsCppDecimal = false;     // Требуется #include для CppDecimal

    // Информация для генерации type aliases
    struct ScaledNumericInfo {
        int intWords;  // 1 для INT64, 2 для INT128
        int scale;     // Отрицательное значение (например, -8 для 8 дес. знаков)
    };
    std::optional<ScaledNumericInfo> scaledInfo;
};
```

---

### Этап 1.4: Обновление сигнатуры mapFieldType()

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в строке 45 (объявление функции):**

```cpp
// БЫЛО:
static TypeMapping mapFieldType(const FieldInfo& field, bool isOutput) {

// СТАНЕТ:
static TypeMapping mapFieldType(const FieldInfo& field, bool isOutput, const AdapterConfig& config) {
```

---

### Этап 1.5: Реализация логики выбора TTMath адаптеров в mapFieldType()

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в строках 72-160 (функция mapFieldType):**

Найти блок `case SQL_INT128:` (примерно строка 99):

```cpp
// БЫЛО:
case SQL_INT128:
    result.cppType = "fbpp::core::Int128";
    result.needsExtendedTypes = true;
    break;

// СТАНЕТ:
case SQL_INT128: {
    // Проверяем наличие scale для определения типа
    if (field.scale != 0) {
        // NUMERIC(38, x) - большое число с дробной частью
        if (config.useTTMathNumeric) {
            // Используем TTMath адаптер с compile-time scale
            result.cppType = std::format("fbpp::adapters::TTNumeric<2, {}>", field.scale);
            result.needsTTMath = true;
            result.scaledInfo = TypeMapping::ScaledNumericInfo{
                .intWords = 2,
                .scale = field.scale
            };
        } else {
            // Fallback на core тип без специального форматирования
            result.cppType = "fbpp::core::Int128";
            result.needsExtendedTypes = true;
        }
    } else {
        // Чистый INT128 без scale - целое 128-битное число
        if (config.useTTMathInt128) {
            result.cppType = "fbpp::adapters::TTInt128";
            result.needsTTMath = true;
        } else {
            result.cppType = "fbpp::core::Int128";
            result.needsExtendedTypes = true;
        }
    }
    break;
}
```

Найти блок `case SQL_INT64:` (примерно строка 82):

```cpp
// БЫЛО:
case SQL_INT64:
    if (field.scale < 0) {
        result.cppType = "double";
    } else {
        result.cppType = "std::int64_t";
    }
    break;

// СТАНЕТ:
case SQL_INT64: {
    if (field.scale != 0) {
        // NUMERIC(18, x) или NUMERIC(15, x) с дробной частью
        if (config.useTTMathNumeric) {
            // Используем 64-битный TTMath адаптер
            result.cppType = std::format("fbpp::adapters::TTNumeric<1, {}>", field.scale);
            result.needsTTMath = true;
            result.scaledInfo = TypeMapping::ScaledNumericInfo{
                .intWords = 1,
                .scale = field.scale
            };
        } else {
            // Fallback на double (потеря точности, но работает)
            result.cppType = "double";
        }
    } else {
        // Чистый INT64 - целое 64-битное число
        result.cppType = "std::int64_t";
    }
    break;
}
```

Найти блок `case SQL_LONG:` (примерно строка 74):

```cpp
// БЫЛО:
case SQL_LONG:
    if (field.scale < 0) {
        result.cppType = "double";
    } else {
        result.cppType = "std::int32_t";
    }
    break;

// СТАНЕТ:
case SQL_LONG: {
    if (field.scale != 0) {
        // NUMERIC(9, x) с дробной частью
        if (config.useTTMathNumeric) {
            // Для 32-битных NUMERIC используем double (TTMath избыточен)
            // Или можем использовать int32_t с ручной обработкой scale
            result.cppType = "double";
        } else {
            result.cppType = "double";
        }
    } else {
        result.cppType = "std::int32_t";
    }
    break;
}
```

---

### Этап 1.6: Обновление вызовов mapFieldType() с новым параметром

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в функции buildFieldSpecs() (строки 163-177):**

```cpp
// БЫЛО (строка 166):
static std::vector<FieldSpec> buildFieldSpecs(
    const std::vector<FieldInfo>& fields, bool isOutput) {
    std::vector<FieldSpec> result;
    result.reserve(fields.size());
    for (const auto& field : fields) {
        auto typeMapping = mapFieldType(field, isOutput);  // ← БЕЗ config
        // ...
    }
    return result;
}

// СТАНЕТ:
static std::vector<FieldSpec> buildFieldSpecs(
    const std::vector<FieldInfo>& fields,
    bool isOutput,
    const AdapterConfig& config) {  // Добавлен параметр config
    std::vector<FieldSpec> result;
    result.reserve(fields.size());
    for (const auto& field : fields) {
        auto typeMapping = mapFieldType(field, isOutput, config);  // Передаём config
        // ...
    }
    return result;
}
```

**Изменения в функции buildQuerySpecs() (строки 342-366):**

```cpp
// БЫЛО (строка 342):
std::vector<QuerySpec> QueryGeneratorService::buildQuerySpecs(
    const std::vector<QueryDefinition>& definitions) const {

// СТАНЕТ:
std::vector<QuerySpec> QueryGeneratorService::buildQuerySpecs(
    const std::vector<QueryDefinition>& definitions,
    const AdapterConfig& config) const {  // Добавлен параметр

// И внутри функции (строки ~356-357):
// БЫЛО:
spec.inputs = buildFieldSpecs(meta.inputFields, false);
spec.outputs = buildFieldSpecs(meta.outputFields, true);

// СТАНЕТ:
spec.inputs = buildFieldSpecs(meta.inputFields, false, config);   // Передаём config
spec.outputs = buildFieldSpecs(meta.outputFields, true, config);  // Передаём config
```

---

### Этап 1.7: Обновление функции renderMainHeader() - добавление параметра config

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в строке 180 (сигнатура функции):**

```cpp
// БЫЛО:
std::string renderQueryGeneratorMainHeader(
    const std::vector<QuerySpec>& specs,
    const std::string& supportHeaderName) {

// СТАНЕТ:
std::string renderQueryGeneratorMainHeader(
    const std::vector<QuerySpec>& specs,
    const std::string& supportHeaderName,
    const AdapterConfig& config) {  // Добавлен параметр
```

---

### Этап 1.8: Генерация дополнительных #include директив для TTMath

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в функции renderMainHeader() после строки 197 (блок includes):**

Найти код:

```cpp
// Строка 197:
bool needsOptional = false;
bool needsExtendedTypes = false;
for (const auto& spec : specs) {
    // ...
}

// Строки 205-207:
if (needsExtendedTypes) {
    out << "#include \"fbpp/core/extended_types.hpp\"\n";
}
```

Заменить на:

```cpp
// Сканируем все спецификации для определения необходимых includes
bool needsOptional = false;
bool needsExtendedTypes = false;
bool needsTTMath = false;       // НОВОЕ
bool needsChrono = false;       // НОВОЕ (для фазы 3)
bool needsCppDecimal = false;   // НОВОЕ (для фазы 2)

for (const auto& spec : specs) {
    for (const auto& field : spec.inputs) {
        needsOptional = needsOptional || field.nullable;
        needsExtendedTypes = needsExtendedTypes || field.needsExtendedTypes;
        needsTTMath = needsTTMath || field.needsTTMath;              // НОВОЕ
        needsChrono = needsChrono || field.needsChrono;              // НОВОЕ
        needsCppDecimal = needsCppDecimal || field.needsCppDecimal;  // НОВОЕ
    }
    for (const auto& field : spec.outputs) {
        needsOptional = needsOptional || field.nullable;
        needsExtendedTypes = needsExtendedTypes || field.needsExtendedTypes;
        needsTTMath = needsTTMath || field.needsTTMath;              // НОВОЕ
        needsChrono = needsChrono || field.needsChrono;              // НОВОЕ
        needsCppDecimal = needsCppDecimal || field.needsCppDecimal;  // НОВОЕ
    }
}

// Стандартные includes
out << "#pragma once\n\n";
out << "#include \"fbpp/core/connection.hpp\"\n";
out << "#include \"fbpp/core/transaction.hpp\"\n";
out << "#include \"fbpp/core/query_executor.hpp\"\n";
out << "#include \"" << supportHeaderName << "\"\n\n";

// Опциональные includes на основе используемых типов
if (needsOptional) {
    out << "#include <optional>\n";
}
if (needsExtendedTypes) {
    out << "#include \"fbpp/core/extended_types.hpp\"\n";
}

// === НОВЫЙ БЛОК: Includes для адаптеров ===
if (needsTTMath) {
    out << "#include \"fbpp/adapters/ttmath_numeric.hpp\"\n";
    out << "#include \"fbpp/adapters/ttmath_int128.hpp\"\n";
}
if (needsChrono) {
    out << "#include <chrono>\n";
    out << "#include \"fbpp/adapters/chrono_datetime.hpp\"\n";
}
if (needsCppDecimal) {
    out << "#include \"fbpp/adapters/cppdecimal_decfloat.hpp\"\n";
}
// === КОНЕЦ НОВОГО БЛОКА ===

out << "\n";
```

---

### Этап 1.9: Генерация type aliases для TTMath типов

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в функции renderMainHeader() после блока includes (после строки ~220):**

Добавить ПЕРЕД `namespace generated::queries {`:

```cpp
// === НОВЫЙ БЛОК: Генерация type aliases ===
if (config.generateAliases && needsTTMath) {
    // Собираем уникальные комбинации IntWords + Scale из всех полей
    std::set<std::pair<int, int>> int64Scales;   // pair<intWords=1, scale>
    std::set<std::pair<int, int>> int128Scales;  // pair<intWords=2, scale>

    auto collectScales = [&](const FieldSpec& field) {
        if (field.scaledInfo) {
            int scaleAbs = -field.scaledInfo->scale;  // Преобразуем -8 → 8
            if (field.scaledInfo->intWords == 1) {
                int64Scales.insert({1, scaleAbs});
            } else if (field.scaledInfo->intWords == 2) {
                int128Scales.insert({2, scaleAbs});
            }
        }
    };

    for (const auto& spec : specs) {
        for (const auto& field : spec.inputs) {
            collectScales(field);
        }
        for (const auto& field : spec.outputs) {
            collectScales(field);
        }
    }

    // Генерируем aliases ПЕРЕД namespace
    if (!int128Scales.empty() || !int64Scales.empty()) {
        out << "// ========================================\n";
        out << "// Type aliases for scaled numeric types\n";
        out << "// ========================================\n\n";

        for (const auto& [intWords, scaleAbs] : int128Scales) {
            out << std::format("using Decimal38_{} = fbpp::adapters::TTNumeric<2, -{}>;\n",
                              scaleAbs, scaleAbs);
        }

        for (const auto& [intWords, scaleAbs] : int64Scales) {
            out << std::format("using Numeric18_{} = fbpp::adapters::TTNumeric<1, -{}>;\n",
                              scaleAbs, scaleAbs);
        }

        out << "\n";
    }
}
// === КОНЕЦ НОВОГО БЛОКА ===

out << "namespace generated::queries {\n\n";
```

---

### Этап 1.10: Использование type aliases в генерируемых структурах

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в функции renderMainHeader() в блоке генерации полей структур (строки ~235-250):**

Найти код генерации полей:

```cpp
// Примерно строка 240:
for (const auto& field : spec.inputs) {
    out << "    " << field.type << " " << field.name << ";\n";
}
```

Заменить на:

```cpp
// Генерация полей input структуры
for (const auto& field : spec.inputs) {
    std::string fieldType;

    // === НОВАЯ ЛОГИКА: Используем alias если есть ===
    if (config.generateAliases && field.scaledInfo) {
        int scaleAbs = -field.scaledInfo->scale;
        if (field.scaledInfo->intWords == 2) {
            fieldType = std::format("Decimal38_{}", scaleAbs);
        } else if (field.scaledInfo->intWords == 1) {
            fieldType = std::format("Numeric18_{}", scaleAbs);
        } else {
            fieldType = field.type;  // Fallback
        }
    } else {
        fieldType = field.type;  // Используем полное имя типа
    }
    // === КОНЕЦ НОВОЙ ЛОГИКИ ===

    out << "    " << fieldType << " " << field.name << ";\n";
}

// То же самое для output структуры (примерно строка 250):
for (const auto& field : spec.outputs) {
    std::string fieldType;

    if (config.generateAliases && field.scaledInfo) {
        int scaleAbs = -field.scaledInfo->scale;
        if (field.scaledInfo->intWords == 2) {
            fieldType = std::format("Decimal38_{}", scaleAbs);
        } else if (field.scaledInfo->intWords == 1) {
            fieldType = std::format("Numeric18_{}", scaleAbs);
        } else {
            fieldType = field.type;
        }
    } else {
        fieldType = field.type;
    }

    out << "    " << fieldType << " " << field.name << ";\n";
}
```

---

### Этап 1.11: Обновление вызова в query_generator.cpp

**Файл:** `src/tools/query_generator.cpp`

**Изменения в функции main() (строки 137-141):**

```cpp
// БЫЛО:
const auto specs = service.buildQuerySpecs(definitions);
const auto supportHeaderName = opts.supportHeader.filename().string();

const auto mainHeader = renderQueryGeneratorMainHeader(specs, supportHeaderName);
const auto supportHeader = renderQueryGeneratorSupportHeader(specs);

// СТАНЕТ:
// Создаём конфигурацию адаптеров из опций командной строки
AdapterConfig adapterConfig;
adapterConfig.useTTMathNumeric = opts.useTTMathNumeric;
adapterConfig.useTTMathInt128 = opts.useTTMathInt128;
adapterConfig.useChrono = opts.useChrono;
adapterConfig.useCppDecimal = opts.useCppDecimal;
adapterConfig.generateAliases = opts.generateAliases;

const auto specs = service.buildQuerySpecs(definitions, adapterConfig);  // Передаём config
const auto supportHeaderName = opts.supportHeader.filename().string();

const auto mainHeader = renderQueryGeneratorMainHeader(specs, supportHeaderName, adapterConfig);  // Передаём config
const auto supportHeader = renderQueryGeneratorSupportHeader(specs);
```

Добавить include в начало файла (после строки 4):

```cpp
#include "fbpp/query_generator_service.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include <nlohmann/json.hpp>

// ДОБАВИТЬ:
using fbpp::core::AdapterConfig;  // Для удобства использования
```

---

### Этап 1.12: Обновление FieldSpec для хранения дополнительных флагов

**Файл:** `include/fbpp/query_generator_service.hpp`

**Изменения в строках 20-28 (struct FieldSpec):**

```cpp
// БЫЛО:
struct FieldSpec {
    std::string name;
    std::string type;
    bool nullable;
    bool needsExtendedTypes;
};

// СТАНЕТ:
struct FieldSpec {
    std::string name;
    std::string type;
    bool nullable;
    bool needsExtendedTypes;

    // === НОВЫЕ ПОЛЯ для адаптеров ===
    bool needsTTMath = false;
    bool needsChrono = false;
    bool needsCppDecimal = false;

    // Информация для генерации aliases
    struct ScaledNumericInfo {
        int intWords;  // 1 для INT64, 2 для INT128
        int scale;     // Firebird scale (отрицательное значение)
    };
    std::optional<ScaledNumericInfo> scaledInfo;
};
```

---

### Этап 1.13: Копирование флагов из TypeMapping в FieldSpec

**Файл:** `src/core/firebird/query_generator_service.cpp`

**Изменения в функции buildFieldSpecs() (строки ~169-176):**

```cpp
// БЫЛО:
for (const auto& field : fields) {
    auto typeMapping = mapFieldType(field, isOutput, config);

    FieldSpec spec;
    spec.name = field.name;
    spec.type = typeMapping.cppType;
    spec.nullable = field.nullable && isOutput;
    spec.needsExtendedTypes = typeMapping.needsExtendedTypes;

    result.push_back(std::move(spec));
}

// СТАНЕТ:
for (const auto& field : fields) {
    auto typeMapping = mapFieldType(field, isOutput, config);

    FieldSpec spec;
    spec.name = field.name;
    spec.type = typeMapping.cppType;
    spec.nullable = field.nullable && isOutput;
    spec.needsExtendedTypes = typeMapping.needsExtendedTypes;

    // === КОПИРУЕМ НОВЫЕ ФЛАГИ ===
    spec.needsTTMath = typeMapping.needsTTMath;
    spec.needsChrono = typeMapping.needsChrono;
    spec.needsCppDecimal = typeMapping.needsCppDecimal;
    spec.scaledInfo = typeMapping.scaledInfo;
    // === КОНЕЦ КОПИРОВАНИЯ ===

    result.push_back(std::move(spec));
}
```

---

### Этап 1.14: Создание тестовой базы данных с NUMERIC типами

**Новый файл:** `tests/test_data/query_generator_ttmath_schema.sql`

```sql
-- Тестовая схема для проверки генерации TTMath адаптеров
-- Покрывает все варианты NUMERIC типов

CREATE TABLE TEST_TTMATH_TYPES (
    ID INTEGER NOT NULL PRIMARY KEY,

    -- NUMERIC с разными precision и scale
    F_NUMERIC_18_2 NUMERIC(18, 2),   -- INT64 с scale=-2
    F_NUMERIC_18_6 NUMERIC(18, 6),   -- INT64 с scale=-6
    F_NUMERIC_38_2 NUMERIC(38, 2),   -- INT128 с scale=-2
    F_NUMERIC_38_8 NUMERIC(38, 8),   -- INT128 с scale=-8
    F_NUMERIC_38_18 NUMERIC(38, 18), -- INT128 с scale=-18

    -- Чистый INT128 без scale
    F_INT128 INT128,

    -- Для сравнения: обычные типы
    F_INTEGER INTEGER,
    F_BIGINT BIGINT,
    F_VARCHAR VARCHAR(100)
);
```

---

### Этап 1.15: Создание тестового queries.json

**Новый файл:** `tests/test_data/query_generator_ttmath_queries.json`

```json
{
  "InsertTTMathTypes": "INSERT INTO TEST_TTMATH_TYPES (ID, F_NUMERIC_18_2, F_NUMERIC_18_6, F_NUMERIC_38_2, F_NUMERIC_38_8, F_NUMERIC_38_18, F_INT128, F_INTEGER, F_BIGINT, F_VARCHAR) VALUES (:id, :num18_2, :num18_6, :num38_2, :num38_8, :num38_18, :int128, :integer, :bigint, :varchar)",

  "SelectById": "SELECT ID, F_NUMERIC_18_2, F_NUMERIC_18_6, F_NUMERIC_38_2, F_NUMERIC_38_8, F_NUMERIC_38_18, F_INT128, F_INTEGER, F_BIGINT, F_VARCHAR FROM TEST_TTMATH_TYPES WHERE ID = :id",

  "SelectAll": "SELECT ID, F_NUMERIC_18_2, F_NUMERIC_38_8, F_INT128 FROM TEST_TTMATH_TYPES ORDER BY ID"
}
```

---

### Этап 1.16: Создание интеграционного теста

**Новый файл:** `tests/unit/test_query_generator_ttmath.cpp`

```cpp
#include <gtest/gtest.h>
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/query_generator_service.hpp"
#include "tests/test_base.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using namespace fbpp::core;

class QueryGeneratorTTMathTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Создаём временную директорию для генерируемых файлов
        generatedDir_ = std::filesystem::temp_directory_path() / "fbpp_qgen_test";
        std::filesystem::create_directories(generatedDir_);

        // Пути к тестовым данным
        schemaFile_ = "tests/test_data/query_generator_ttmath_schema.sql";
        queriesFile_ = "tests/test_data/query_generator_ttmath_queries.json";

        outputHeader_ = generatedDir_ / "test_queries.hpp";
        supportHeader_ = generatedDir_ / "test_queries_support.hpp";
    }

    void TearDown() override {
        // Очистка временных файлов
        std::filesystem::remove_all(generatedDir_);
    }

    std::filesystem::path generatedDir_;
    std::filesystem::path schemaFile_;
    std::filesystem::path queriesFile_;
    std::filesystem::path outputHeader_;
    std::filesystem::path supportHeader_;
};

// Тест 1: Генерация без адаптеров (baseline)
TEST_F(QueryGeneratorTTMathTest, GenerateWithoutAdapters) {
    // TODO: Реализовать после завершения этапа 1.11
    GTEST_SKIP() << "Implement after phase 1 completion";
}

// Тест 2: Генерация с --use-ttmath-numeric
TEST_F(QueryGeneratorTTMathTest, GenerateWithTTMathNumeric) {
    // TODO: Реализовать
    GTEST_SKIP() << "Implement after phase 1 completion";
}

// Тест 3: Генерация с --use-ttmath-int128
TEST_F(QueryGeneratorTTMathTest, GenerateWithTTMathInt128) {
    // TODO: Реализовать
    GTEST_SKIP() << "Implement after phase 1 completion";
}

// Тест 4: Генерация type aliases
TEST_F(QueryGeneratorTTMathTest, GenerateWithAliases) {
    // TODO: Реализовать
    GTEST_SKIP() << "Implement after phase 1 completion";
}

// Тест 5: Использование сгенерированного кода (компиляция и выполнение)
TEST_F(QueryGeneratorTTMathTest, DISABLED_UseGeneratedCode) {
    // Этот тест будет реализован после успешной генерации
    // Он должен включать сгенерированный хедер и выполнять запросы
    GTEST_SKIP() << "Implement after successful code generation";
}
```

**Добавить тест в CMakeLists.txt:**

```cmake
# В tests/unit/CMakeLists.txt
add_executable(test_query_generator_ttmath
    test_query_generator_ttmath.cpp
)

target_link_libraries(test_query_generator_ttmath
    PRIVATE
        fbpp_core
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME QueryGeneratorTTMath COMMAND test_query_generator_ttmath)
```

---

### Контрольные точки для Фазы 1

После завершения всех этапов 1.1-1.16 проверить:

1. **Компиляция:**
   ```bash
   cd build && cmake --build . --target query_generator
   ```

2. **Help message:**
   ```bash
   ./build/src/tools/query_generator --help
   ```
   Должен показать все новые флаги.

3. **Генерация без адаптеров (baseline):**
   ```bash
   ./build/src/tools/query_generator \
       --dsn $FIREBIRD_HOST:$FIREBIRD_PERSISTENT_DB_PATH \
       --input tests/test_data/query_generator_ttmath_queries.json \
       --output /tmp/test_baseline.hpp \
       --support /tmp/test_baseline_support.hpp
   ```
   Проверить: должны быть `fbpp::core::Int128`, `double`.

4. **Генерация с TTMath:**
   ```bash
   ./build/src/tools/query_generator \
       --dsn $FIREBIRD_HOST:$FIREBIRD_PERSISTENT_DB_PATH \
       --use-ttmath-numeric \
       --input tests/test_data/query_generator_ttmath_queries.json \
       --output /tmp/test_ttmath.hpp \
       --support /tmp/test_ttmath_support.hpp
   ```
   Проверить:
   - Include: `#include "fbpp/adapters/ttmath_numeric.hpp"`
   - Aliases: `using Decimal38_8 = fbpp::adapters::TTNumeric<2, -8>;`
   - Fields: `Decimal38_8 f_numeric_38_8;`

5. **Запуск тестов:**
   ```bash
   cd build && ctest -R QueryGeneratorTTMath -V
   ```

---

## ФАЗА 2: CppDecimal адаптеры (DECFLOAT)

### Цель фазы
Добавить поддержку CppDecimal адаптеров для типов DECFLOAT(16) и DECFLOAT(34).

---

### Этап 2.1: Реализация логики для DECFLOAT в mapFieldType()

**Файл:** `src/core/firebird/query_generator_service.cpp`

Найти блоки `case SQL_DEC16:` и `case SQL_DEC34:` (примерно строки 122-130):

```cpp
// БЫЛО:
case SQL_DEC16:
    result.cppType = "fbpp::core::DecFloat16";
    result.needsExtendedTypes = true;
    break;

case SQL_DEC34:
    result.cppType = "fbpp::core::DecFloat34";
    result.needsExtendedTypes = true;
    break;

// СТАНЕТ:
case SQL_DEC16:
    if (config.useCppDecimal) {
        result.cppType = "fbpp::adapters::DecFloat16";
        result.needsCppDecimal = true;
    } else {
        result.cppType = "fbpp::core::DecFloat16";
        result.needsExtendedTypes = true;
    }
    break;

case SQL_DEC34:
    if (config.useCppDecimal) {
        result.cppType = "fbpp::adapters::DecFloat34";
        result.needsCppDecimal = true;
    } else {
        result.cppType = "fbpp::core::DecFloat34";
        result.needsExtendedTypes = true;
    }
    break;
```

---

### Этап 2.2: Создание тестовой схемы для DECFLOAT

**Новый файл:** `tests/test_data/query_generator_decfloat_schema.sql`

```sql
CREATE TABLE TEST_DECFLOAT_TYPES (
    ID INTEGER NOT NULL PRIMARY KEY,
    F_DECFLOAT16 DECFLOAT(16),
    F_DECFLOAT34 DECFLOAT(34),
    F_DESCRIPTION VARCHAR(200)
);
```

**Новый файл:** `tests/test_data/query_generator_decfloat_queries.json`

```json
{
  "InsertDecFloat": "INSERT INTO TEST_DECFLOAT_TYPES (ID, F_DECFLOAT16, F_DECFLOAT34, F_DESCRIPTION) VALUES (:id, :df16, :df34, :desc)",
  "SelectDecFloat": "SELECT ID, F_DECFLOAT16, F_DECFLOAT34, F_DESCRIPTION FROM TEST_DECFLOAT_TYPES WHERE ID = :id"
}
```

---

### Этап 2.3: Добавление тестов для CppDecimal

**Файл:** `tests/unit/test_query_generator_ttmath.cpp` (переименовать в `test_query_generator_adapters.cpp`)

Добавить тесты:

```cpp
// Тест 6: Генерация с --use-cppdecimal
TEST_F(QueryGeneratorAdaptersTest, GenerateWithCppDecimal) {
    // TODO: Реализовать
}

// Тест 7: Проверка include директив для CppDecimal
TEST_F(QueryGeneratorAdaptersTest, VerifyCppDecimalIncludes) {
    // TODO: Реализовать
}
```

---

### Контрольные точки для Фазы 2

1. **Генерация с CppDecimal:**
   ```bash
   ./build/src/tools/query_generator \
       --use-cppdecimal \
       --dsn ... \
       --input tests/test_data/query_generator_decfloat_queries.json \
       --output /tmp/test_cppdecimal.hpp \
       --support /tmp/test_cppdecimal_support.hpp
   ```

   Проверить:
   - Include: `#include "fbpp/adapters/cppdecimal_decfloat.hpp"`
   - Types: `fbpp::adapters::DecFloat16`, `fbpp::adapters::DecFloat34`

2. **Тесты:** `ctest -R QueryGeneratorAdapters`

---

## ФАЗА 3: std::chrono адаптеры (DATE/TIME/TIMESTAMP)

### Цель фазы
Добавить поддержку std::chrono типов для DATE, TIME, TIMESTAMP, TIME WITH TIME ZONE, TIMESTAMP WITH TIME ZONE.

---

### Этап 3.1: Реализация логики для datetime типов в mapFieldType()

**Файл:** `src/core/firebird/query_generator_service.cpp`

Найти блоки datetime типов (примерно строки 107-120):

```cpp
// БЫЛО:
case SQL_TYPE_DATE:
    result.cppType = "fbpp::core::Date";
    result.needsExtendedTypes = true;
    break;

case SQL_TYPE_TIME:
    result.cppType = "fbpp::core::Time";
    result.needsExtendedTypes = true;
    break;

case SQL_TIMESTAMP:
    result.cppType = "fbpp::core::Timestamp";
    result.needsExtendedTypes = true;
    break;

case SQL_TIMESTAMP_TZ:
    result.cppType = "fbpp::core::TimestampTz";
    result.needsExtendedTypes = true;
    break;

case SQL_TIME_TZ:
    result.cppType = "fbpp::core::TimeTz";
    result.needsExtendedTypes = true;
    break;

// СТАНЕТ:
case SQL_TYPE_DATE:
    if (config.useChrono) {
        result.cppType = "std::chrono::year_month_day";
        result.needsChrono = true;
    } else {
        result.cppType = "fbpp::core::Date";
        result.needsExtendedTypes = true;
    }
    break;

case SQL_TYPE_TIME:
    if (config.useChrono) {
        result.cppType = "std::chrono::hh_mm_ss<std::chrono::microseconds>";
        result.needsChrono = true;
    } else {
        result.cppType = "fbpp::core::Time";
        result.needsExtendedTypes = true;
    }
    break;

case SQL_TIMESTAMP:
    if (config.useChrono) {
        result.cppType = "std::chrono::system_clock::time_point";
        result.needsChrono = true;
    } else {
        result.cppType = "fbpp::core::Timestamp";
        result.needsExtendedTypes = true;
    }
    break;

case SQL_TIMESTAMP_TZ:
    if (config.useChrono) {
        result.cppType = "std::chrono::zoned_time<std::chrono::microseconds>";
        result.needsChrono = true;
    } else {
        result.cppType = "fbpp::core::TimestampTz";
        result.needsExtendedTypes = true;
    }
    break;

case SQL_TIME_TZ:
    if (config.useChrono) {
        // TIME WITH TIME ZONE - используем кастомный тип (нет прямого аналога в chrono)
        result.cppType = "fbpp::core::TimeWithTz";  // Пока оставляем core тип
        result.needsExtendedTypes = true;
    } else {
        result.cppType = "fbpp::core::TimeTz";
        result.needsExtendedTypes = true;
    }
    break;
```

---

### Этап 3.2: Генерация chrono type aliases

**Файл:** `src/core/firebird/query_generator_service.cpp`

В функции `renderMainHeader()` добавить после TTMath aliases:

```cpp
if (config.generateAliases && needsChrono) {
    out << "// ========================================\n";
    out << "// Type aliases for std::chrono types\n";
    out << "// ========================================\n\n";
    out << "using MicroTime = std::chrono::hh_mm_ss<std::chrono::microseconds>;\n";
    out << "using ZonedMicroTime = std::chrono::zoned_time<std::chrono::microseconds>;\n";
    out << "\n";
}
```

---

### Этап 3.3: Использование chrono aliases в структурах

В блоке генерации полей добавить логику для chrono типов:

```cpp
if (config.generateAliases && field.needsChrono) {
    if (field.type == "std::chrono::hh_mm_ss<std::chrono::microseconds>") {
        fieldType = "MicroTime";
    } else if (field.type == "std::chrono::zoned_time<std::chrono::microseconds>") {
        fieldType = "ZonedMicroTime";
    } else {
        fieldType = field.type;  // year_month_day, time_point - оставляем как есть
    }
} else {
    fieldType = field.type;
}
```

---

### Этап 3.4: Создание тестовых данных для datetime

**Новый файл:** `tests/test_data/query_generator_datetime_schema.sql`

```sql
CREATE TABLE TEST_DATETIME_TYPES (
    ID INTEGER NOT NULL PRIMARY KEY,
    F_DATE DATE,
    F_TIME TIME,
    F_TIMESTAMP TIMESTAMP,
    F_TIMESTAMP_TZ TIMESTAMP WITH TIME ZONE,
    F_TIME_TZ TIME WITH TIME ZONE
);
```

**Новый файл:** `tests/test_data/query_generator_datetime_queries.json`

```json
{
  "InsertDateTime": "INSERT INTO TEST_DATETIME_TYPES (ID, F_DATE, F_TIME, F_TIMESTAMP, F_TIMESTAMP_TZ, F_TIME_TZ) VALUES (:id, :date, :time, :timestamp, :timestamp_tz, :time_tz)",
  "SelectDateTime": "SELECT ID, F_DATE, F_TIME, F_TIMESTAMP, F_TIMESTAMP_TZ, F_TIME_TZ FROM TEST_DATETIME_TYPES WHERE ID = :id"
}
```

---

### Этап 3.5: Добавление тестов для chrono

```cpp
// Тест 8: Генерация с --use-chrono
TEST_F(QueryGeneratorAdaptersTest, GenerateWithChrono) {
    // TODO: Реализовать
}

// Тест 9: Проверка chrono type aliases
TEST_F(QueryGeneratorAdaptersTest, VerifyChronoAliases) {
    // TODO: Реализовать
}
```

---

### Контрольные точки для Фазы 3

1. **Генерация с chrono:**
   ```bash
   ./build/src/tools/query_generator \
       --use-chrono \
       --dsn ... \
       --input tests/test_data/query_generator_datetime_queries.json \
       --output /tmp/test_chrono.hpp \
       --support /tmp/test_chrono_support.hpp
   ```

   Проверить:
   - Include: `#include <chrono>`, `#include "fbpp/adapters/chrono_datetime.hpp"`
   - Aliases: `using MicroTime = ...`, `using ZonedMicroTime = ...`
   - Types: `std::chrono::year_month_day`, `MicroTime`, `ZonedMicroTime`

2. **Тесты:** `ctest -R QueryGeneratorAdapters`

---

## ФИНАЛЬНАЯ ФАЗА: Интеграция и комплексное тестирование

### Этап F.1: Тест с --use-adapters (все адаптеры вместе)

Создать комплексную схему с всеми типами:

**Файл:** `tests/test_data/query_generator_all_types_schema.sql`

```sql
CREATE TABLE TEST_ALL_ADAPTER_TYPES (
    ID INTEGER NOT NULL PRIMARY KEY,

    -- TTMath NUMERIC
    F_NUMERIC_38_8 NUMERIC(38, 8),
    F_INT128 INT128,

    -- CppDecimal DECFLOAT
    F_DECFLOAT34 DECFLOAT(34),

    -- std::chrono datetime
    F_DATE DATE,
    F_TIMESTAMP TIMESTAMP,
    F_TIMESTAMP_TZ TIMESTAMP WITH TIME ZONE,

    -- Обычные типы
    F_VARCHAR VARCHAR(100)
);
```

**Файл:** `tests/test_data/query_generator_all_types_queries.json`

```json
{
  "InsertAll": "INSERT INTO TEST_ALL_ADAPTER_TYPES (ID, F_NUMERIC_38_8, F_INT128, F_DECFLOAT34, F_DATE, F_TIMESTAMP, F_TIMESTAMP_TZ, F_VARCHAR) VALUES (:id, :num, :int128, :df, :date, :ts, :ts_tz, :text)",
  "SelectAll": "SELECT ID, F_NUMERIC_38_8, F_INT128, F_DECFLOAT34, F_DATE, F_TIMESTAMP, F_TIMESTAMP_TZ, F_VARCHAR FROM TEST_ALL_ADAPTER_TYPES WHERE ID = :id"
}
```

---

### Этап F.2: Создание полноценного интеграционного теста с выполнением запросов

**Файл:** `tests/integration/test_query_generator_integration.cpp`

```cpp
// Этот тест должен:
// 1. Сгенерировать код с --use-adapters
// 2. Включить сгенерированные заголовки
// 3. Создать тестовую таблицу
// 4. Выполнить INSERT с адаптерными типами
// 5. Выполнить SELECT и проверить корректность данных
// 6. Сравнить точность (round-trip accuracy)

#include "generated_all_types.hpp"  // Сгенерированный файл
#include <gtest/gtest.h>

TEST(QueryGeneratorIntegration, RoundTripWithAdapters) {
    using namespace generated::queries;

    // Создаём подключение
    auto connection = createTestConnection();

    // Подготавливаем данные с использованием адаптерных типов
    InsertAllIn input{
        .id = 1,
        .num = Decimal38_8("12345678.90123456"),  // TTMath
        .int128 = fbpp::adapters::TTInt128("170141183460469231731687303715884105727"),
        .df = fbpp::adapters::DecFloat34("1234567890.123456789012345678"),  // CppDecimal
        .date = std::chrono::year_month_day{std::chrono::year{2025}, std::chrono::January, std::chrono::day{12}},
        .ts = std::chrono::system_clock::now(),
        .ts_tz = ZonedMicroTime{std::chrono::current_zone(), std::chrono::system_clock::now()},
        .text = "Integration test"
    };

    // Вставляем данные
    auto tra = connection->StartTransaction();
    executeNonQuery<QueryDescriptor<QueryId::InsertAll>>(*connection, *tra, input);
    tra->Commit();

    // Читаем обратно
    tra = connection->StartTransaction();
    auto results = executeQuery<QueryDescriptor<QueryId::SelectAll>>(
        *connection, *tra, SelectAllIn{.id = 1});
    tra->Commit();

    ASSERT_EQ(results.size(), 1);
    const auto& row = results[0];

    // Проверяем точность (round-trip)
    EXPECT_EQ(row.num->to_string(), input.num.to_string());
    EXPECT_EQ(row.int128->to_string(), input.int128.to_string());
    EXPECT_EQ(row.df->to_string(), input.df.to_string());
    EXPECT_EQ(*row.date, input.date);
    // ... и т.д.
}
```

---

### Этап F.3: Обновление документации

**Новый файл:** `doc/QUERY_GENERATOR_USER_GUIDE.md`

```markdown
# Query Generator User Guide

## Введение
...

## Использование адаптеров типов

### TTMath для NUMERIC типов
...

### CppDecimal для DECFLOAT
...

### std::chrono для datetime типов
...

## Примеры использования
...

## Troubleshooting
...
```

**Обновить:** `CLAUDE.md`

Добавить секцию:

```markdown
## Query Generator с адаптерами

### Генерация с TTMath:
bash
query_generator --use-ttmath-numeric --dsn ... --input queries.json ...


### Генерация со всеми адаптерами:
bash
query_generator --use-adapters --dsn ... --input queries.json ...


### Сгенерированный код:
cpp
// Type aliases
using Decimal38_8 = fbpp::adapters::TTNumeric<2, -8>;

struct MyQueryIn {
    int32_t id;
    Decimal38_8 amount;  // Вместо fbpp::core::Int128
};

```

---

### Этап F.4: Создание примера использования

**Новый файл:** `examples/10_query_generator_with_adapters.cpp`

```cpp
// Демонстрация автоматически сгенерированных запросов с адаптерами
#include "fbpp/core/connection.hpp"
#include "generated/financial_queries.hpp"  // Сгенерированный файл

// Этот файл был сгенерирован командой:
// query_generator --use-adapters --dsn firebird5:financialdb \
//     --input financial_queries.json \
//     --output generated/financial_queries.hpp \
//     --support generated/financial_queries_support.hpp

int main() {
    using namespace generated::queries;

    auto connection = createConnection();

    // Используем сгенерированные типы с адаптерами
    InsertTransactionIn transaction{
        .id = 1,
        .amount = Decimal38_8("12345.67"),  // TTMath адаптер
        .fee = Decimal38_2("10.50"),
        .timestamp = std::chrono::system_clock::now(),  // chrono адаптер
        .rate = fbpp::adapters::DecFloat34("1.23456789")  // CppDecimal адаптер
    };

    auto tra = connection->StartTransaction();
    executeNonQuery<QueryDescriptor<QueryId::InsertTransaction>>(*connection, *tra, transaction);
    tra->Commit();

    return 0;
}
```

---

## Контрольный чеклист для завершения проекта

### Фаза 1 (TTMath):
- [ ] Все параметры командной строки добавлены
- [ ] AdapterConfig создан и передаётся через все функции
- [ ] mapFieldType() реализует логику для TTMath (NUMERIC + INT128)
- [ ] Генерируются корректные #include директивы
- [ ] Генерируются type aliases (Decimal38_X, Numeric18_X)
- [ ] Aliases используются в полях структур
- [ ] Тестовая схема и queries.json созданы
- [ ] Базовые unit-тесты написаны и проходят
- [ ] Сгенерированный код компилируется
- [ ] Ручная проверка сгенерированных файлов успешна

### Фаза 2 (CppDecimal):
- [ ] mapFieldType() обновлён для DECFLOAT типов
- [ ] Include директивы для CppDecimal генерируются
- [ ] Тесты для DECFLOAT созданы и проходят
- [ ] Проверка на реальной БД с DECFLOAT данными

### Фаза 3 (std::chrono):
- [ ] mapFieldType() обновлён для всех datetime типов
- [ ] Chrono include директивы генерируются
- [ ] Chrono type aliases (MicroTime, ZonedMicroTime) генерируются
- [ ] Тесты для datetime созданы и проходят

### Финал:
- [ ] Комплексный тест с --use-adapters создан
- [ ] Интеграционный тест с выполнением запросов проходит
- [ ] Round-trip accuracy подтверждена для всех типов
- [ ] Документация написана
- [ ] Пример использования создан и работает
- [ ] Все 113 существующих тестов по-прежнему проходят (обратная совместимость)
- [ ] CI/CD обновлён для тестирования query_generator

---

## Возможные проблемы и решения

### Проблема 1: Конфликт имён aliases
**Симптом:** Два запроса используют NUMERIC(38,8), alias генерируется дважды.
**Решение:** Использовать `std::set` для уникальности scale значений (уже реализовано в этапе 1.9).

### Проблема 2: Слишком длинные имена типов без aliases
**Симптом:** `std::chrono::hh_mm_ss<std::chrono::microseconds>` слишком длинный.
**Решение:** Генерировать aliases по умолчанию, добавить `--no-aliases` для отключения.

### Проблема 3: Firebird не предоставляет precision напрямую
**Симптом:** Не можем точно определить NUMERIC(18,2) vs NUMERIC(9,2).
**Решение:** Используем `length` поля: 4 байта → INT32, 8 байт → INT64, 16 байт → INT128.

### Проблема 4: TIME WITH TIME ZONE не имеет прямого аналога в std::chrono
**Симптом:** Нет готового типа для TIME_TZ.
**Решение:** Пока оставляем `fbpp::core::TimeWithTz`, можно создать custom chrono wrapper в будущем.

---

## Метрики успеха

1. **Функциональность:**
   - Генерация работает с 0 ошибками для всех комбинаций флагов
   - Сгенерированный код компилируется без предупреждений
   - Round-trip accuracy 100% для всех типов

2. **Производительность:**
   - Время генерации < 5 секунд для 100 запросов
   - Размер сгенерированных файлов разумный (< 10KB на 10 запросов)

3. **Качество кода:**
   - 100% существующих тестов проходят (обратная совместимость)
   - Минимум 80% покрытие новым кодом
   - 0 warnings от компилятора

4. **Документация:**
   - User guide покрывает все use cases
   - Примеры работают "из коробки"
   - CLAUDE.md обновлён с новыми инструкциями

---

## Конец документа

**Версия:** 1.0
**Дата последнего обновления:** 2025-11-12
**Статус:** Ready for implementation
