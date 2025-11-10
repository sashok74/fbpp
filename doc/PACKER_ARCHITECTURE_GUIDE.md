# Архитектурная Схема Системы Упаковщиков fbpp

## 🎯 Философия Дизайна

**Zero-Boilerplate** — разработчик работает с чистыми C++ структурами, не касаясь бинарных буферов Firebird.

**Type-Safe** — все преобразования проверяются на этапе компиляции через concepts и SFINAE.

**Single Codec** — один модуль преобразований для всех форматов данных (tuple, JSON, struct).

**Adapter System** — расширенные типы (INT128, DECFLOAT, TIMESTAMP_TZ) интегрируются через trait-систему без изменения кодека.

---

## 📐 Концептуальная Архитектура

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         CLIENT APPLICATION                                  │
│                                                                             │
│  struct Order {                                                             │
│      int64_t id;                              // Native type                │
│      TTNumeric<4, -2> total;                  // Adapter: NUMERIC(38,2)     │
│      DecFloat16 discount;                     // Adapter: DECFLOAT(16)      │
│      std::chrono::system_clock::time_point    // Adapter: TIMESTAMP         │
│          created_at;                                                        │
│      std::optional<TimestampTz> closed_at;    // Extended: TIMESTAMP TZ     │
│  };                                                                         │
│                                                                             │
│  Order order{42, TTNumeric{12345}, DecFloat16{"0.15"}, now(), nullopt};    │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │
                                    │ pack(order)
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      STRUCT DESCRIPTOR (Generated)                          │
│                                                                             │
│  template<> struct StructDescriptor<Order> {                                │
│      static constexpr auto fields = std::array{                             │
│          Field{"id",         SqlType::BigInt,    scale=0,  nullable=false}, │
│          Field{"total",      SqlType::Int128,    scale=-2, useAdapter=true},│
│          Field{"discount",   SqlType::DecFloat16, scale=0, useAdapter=true},│
│          Field{"created_at", SqlType::Timestamp,  scale=0, useAdapter=true},│
│          Field{"closed_at",  SqlType::TStampTz,   scale=0, nullable=true}   │
│      };                                                                     │
│  };                                                                         │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │
                                    │ StructPacker::pack()
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        SQL VALUE CODEC (Central)                            │
│                                                                             │
│  write_sql_value(ctx, value, dataPtr):                                     │
│                                                                             │
│      ┌─────────────────────┐                                               │
│      │ Has TypeAdapter?    │───Yes──▶ Contextual Adapter?                  │
│      └─────────────────────┘              │                                │
│                │                           │                                │
│               No                          Yes─▶ adapt_to_firebird_ctx()    │
│                │                           │      (scale-aware)             │
│                │                          No──▶ adapt_to_firebird()         │
│                │                                 (basic)                    │
│                ▼                                                            │
│      ┌─────────────────────┐                                               │
│      │ Native SQL Type?    │───Yes──▶ memcpy(dataPtr, &value, size)        │
│      └─────────────────────┘                                               │
│                │                                                            │
│               No                                                            │
│                │                                                            │
│                ▼                                                            │
│      ┌─────────────────────┐                                               │
│      │ Extended Type?      │───Yes──▶ Int128/DecFloat/TimestampTz          │
│      │ (INT128/DECFLOAT/TZ)│            direct serialization               │
│      └─────────────────────┘                                               │
│                                                                             │
│  Null Handling: *nullPtr = value ? 0 : -1                                  │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │
                                    │ writes to
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     FIREBIRD MESSAGE BUFFER                                 │
│                                                                             │
│  ┌───────────┬────────┬───────────┬────────┬────────────┬────────┐        │
│  │ Field 0   │ NULL 0 │ Field 1   │ NULL 1 │ Field 2    │ NULL 2 │        │
│  ├───────────┼────────┼───────────┼────────┼────────────┼────────┤        │
│  │ int64_t   │   0    │ INT128    │   0    │ DECFLOAT16 │   0    │        │
│  │ (8 bytes) │ (2 B)  │ (16 bytes)│ (2 B)  │ (8 bytes)  │ (2 B)  │        │
│  └───────────┴────────┴───────────┴────────┴────────────┴────────┘        │
│                                                                             │
│  Layout determined by IMessageMetadata:                                    │
│    - field.offset: data position                                           │
│    - field.nullOffset: null indicator position                             │
│    - field.type: SQL type (SQL_INT128=32752, SQL_DECFLOAT16=32760)        │
│    - field.scale: decimal scale (negative = digits after decimal point)   │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │
                                    │ IStatement->execute()
                                    ▼
                              FIREBIRD 5 SERVER
```

---

## 🔄 Flow Упаковки с Адаптерами

### Пример: Упаковка `TTNumeric<4, -2> total = 123.45`

```
1. CLIENT LAYER
   ┌────────────────────────────────────┐
   │ TTNumeric<4, -2> total{12345}      │  // Stored as scaled int: 12345
   │ (type scale = -2 means 2 decimals) │  // Represents 123.45
   └────────────────┬───────────────────┘
                    │
                    │ struct member access
                    ▼
2. STRUCT PACKER
   ┌─────────────────────────────────────────────────────────────┐
   │ auto& member = order.total;                                 │
   │ const FieldDescriptor& fd = fields[1];                      │
   │ fd.useAdapter = true  →  needs TypeAdapter                  │
   └────────────────┬────────────────────────────────────────────┘
                    │
                    │ calls adapter
                    ▼
3. TYPE ADAPTER (ttmath_numeric.hpp)
   ┌─────────────────────────────────────────────────────────────┐
   │ TypeAdapter<TTNumeric<4, -2>>::to_firebird_ctx()            │
   │                                                             │
   │ Input:                                                      │
   │   - value.value_ = ttmath::Int<4>{12345}                    │
   │   - value.scale = -2 (C++ side)                             │
   │   - fb_scale = -2 (Firebird column NUMERIC(38,2))          │
   │   - fb_length = 16 bytes (INT128 storage)                   │
   │                                                             │
   │ Process:                                                    │
   │   1. Scale alignment: type_scale (-2) == db_scale (-2) ✓   │
   │   2. Convert to two's complement INT128                     │
   │   3. Sign extension for negative values                     │
   │                                                             │
   │ Output:                                                     │
   │   - 16-byte little-endian INT128: 0x39300000...            │
   └────────────────┬────────────────────────────────────────────┘
                    │
                    │ writes bytes
                    ▼
4. FIREBIRD BUFFER
   ┌────────────────────────────────────┐
   │ Offset 24: [39 30 00 00 00 ...]   │  // INT128 representation
   │ Offset 40: [00 00]                 │  // NULL indicator = 0 (NOT NULL)
   └────────────────────────────────────┘
```

### Пример: Упаковка `std::chrono::system_clock::time_point created_at`

```
1. CLIENT LAYER
   ┌─────────────────────────────────────────────────────┐
   │ auto created_at = std::chrono::system_clock::now(); │
   │ // 2025-11-10 14:30:00 UTC                          │
   └────────────────┬────────────────────────────────────┘
                    │
                    │ struct member access
                    ▼
2. STRUCT PACKER
   ┌──────────────────────────────────────────────────┐
   │ fd.useAdapter = true                             │
   │ fd.type = SqlType::Timestamp                     │
   └────────────────┬─────────────────────────────────┘
                    │
                    │ calls adapter
                    ▼
3. TYPE ADAPTER (chrono_datetime.hpp)
   ┌────────────────────────────────────────────────────────────┐
   │ TypeAdapter<system_clock::time_point>::to_firebird()       │
   │                                                            │
   │ Process:                                                   │
   │   1. Convert Unix epoch → Firebird epoch                   │
   │      (1970-01-01 → 1858-11-17, offset = 40587 days)       │
   │   2. Split into date + time components                     │
   │      date: days since 1858-11-17                           │
   │      time: 100 microseconds units (1/10000 sec)            │
   │   3. Create Timestamp{date, time}                          │
   │                                                            │
   │ Output:                                                    │
   │   Timestamp{date=61200, time=522000000}                    │
   └────────────────┬───────────────────────────────────────────┘
                    │
                    │ core extended type
                    ▼
4. SQL VALUE CODEC
   ┌──────────────────────────────────────────────────┐
   │ write_sql_value() for Timestamp:                 │
   │   memcpy(dataPtr,     &date, 4);  // uint32_t    │
   │   memcpy(dataPtr + 4, &time, 4);  // uint32_t    │
   └────────────────┬─────────────────────────────────┘
                    │
                    │ writes 8 bytes
                    ▼
5. FIREBIRD BUFFER
   ┌────────────────────────────────────┐
   │ Offset 42: [B0 EE 00 00]          │  // date (little-endian)
   │ Offset 46: [80 1C 1F 1F]          │  // time (little-endian)
   │ Offset 50: [00 00]                 │  // NULL indicator = 0
   └────────────────────────────────────┘
```

---

## 🧩 Компоненты Системы

### 1. TypeAdapter System (type_adapter.hpp)

```cpp
// Базовый интерфейс (INT128, DECFLOAT)
template<typename T>
struct TypeAdapter {
    using firebird_type = ...;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const T&);
    static T from_firebird(const firebird_type&);
};

// Контекстный интерфейс (NUMERIC с масштабом)
template<typename T>
struct TypeAdapter {
    static constexpr bool is_specialized = true;

    static void to_firebird(const T&, int16_t fb_scale,
                           unsigned fb_length, uint8_t* out);
    static T from_firebird(const uint8_t*, unsigned fb_length,
                          int16_t fb_scale);
};
```

**Детекторы**:
- `has_type_adapter_v<T>` — есть ли адаптер?
- `adapter_has_basic_to_from_v<T>` — базовый интерфейс?
- `adapter_has_ctx_to_from_v<T>` — контекстный интерфейс?

### 2. SQL Value Codec (sql_value_codec.hpp)

```cpp
namespace fbpp::core::detail::sql_value_codec {

struct SqlWriteContext {
    const FieldInfo* field;        // Metadata: type, scale, length
    Transaction* transaction;       // For BLOB operations
    int16_t* nullIndicator;        // Null flag pointer
};

// Universal write function
template<typename T>
void write_sql_value(const SqlWriteContext& ctx,
                    const T& value,
                    uint8_t* dataPtr) {

    // 1. Check for TypeAdapter
    if constexpr (has_type_adapter_v<T>) {
        if constexpr (adapter_has_ctx_to_from_v<T>) {
            // Context-aware: pass scale/length
            adapt_to_firebird_ctx(value, ctx.field->scale,
                                 ctx.field->length, dataPtr);
        } else {
            // Basic: convert to firebird_type
            auto fb = adapt_to_firebird(value);
            write_sql_value(ctx, fb, dataPtr);
        }
        setNotNull(ctx.nullIndicator);
        return;
    }

    // 2. Native types (int16_t, int32_t, int64_t, double)
    if constexpr (std::is_arithmetic_v<T>) {
        std::memcpy(dataPtr, &value, sizeof(T));
        setNotNull(ctx.nullIndicator);
        return;
    }

    // 3. Extended types (Int128, DecFloat, TimestampTz)
    if constexpr (std::is_same_v<T, Int128>) {
        std::memcpy(dataPtr, value.data(), 16);
        setNotNull(ctx.nullIndicator);
        return;
    }

    // ... other types
}

// Universal read function
template<typename T>
void read_sql_value(const SqlReadContext& ctx,
                   const uint8_t* dataPtr,
                   T& value) {
    // Check NULL first
    if (isNull(ctx.nullIndicator)) {
        throw FirebirdException("NULL value for non-nullable field");
    }

    // Mirror logic for unpacking
    // ...
}

} // namespace
```

**Ключевые особенности**:
- ✅ Один код для всех упаковщиков (tuple/JSON/struct)
- ✅ Автоматическая диспетчеризация: адаптер → нативный → расширенный
- ✅ Рекурсивная обработка `std::optional<T>`

### 3. Struct Packer (struct_packer.hpp)

```cpp
template<typename T>
class StructPacker {
public:
    static void pack(const T& value,
                    uint8_t* buffer,
                    MessageMetadata* metadata,
                    Transaction* transaction) {

        // 1. Get descriptor
        constexpr auto& fields = StructDescriptor<T>::fields;

        // 2. Validate field count
        if (fields.size() != metadata->getCount()) {
            throw FirebirdException("Field count mismatch");
        }

        // 3. Zero buffer
        std::memset(buffer, 0, metadata->getMessageLength());

        // 4. Pack each field
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            const FieldInfo& fi = metadata->getField(i);

            uint8_t* dataPtr = buffer + fi.offset;
            int16_t* nullPtr = reinterpret_cast<int16_t*>(
                buffer + fi.nullOffset);

            // Access struct member
            auto& member = value.*(field.memberPtr);

            // Create context
            SqlWriteContext ctx{&fi, transaction, nullPtr};

            // Delegate to codec
            write_sql_value(ctx, member, dataPtr);
        }
    }
};
```

**Упрощение для пользователя**:
```cpp
// High-level API
template<typename T>
void pack(const T& value, uint8_t* buffer,
         MessageMetadata* meta, Transaction* txn) {
    if constexpr (is_struct_packable_v<T>) {
        StructPacker<T>::pack(value, buffer, meta, txn);
    } else if constexpr (is_tuple_v<T>) {
        TuplePacker<T>::pack(value, buffer, meta, txn);
    } else if constexpr (is_json_v<T>) {
        JsonPacker::pack(value, buffer, meta, txn);
    }
}
```

### 4. Struct Descriptor (generated)

```cpp
// Generated by schema tool
template<>
struct StructDescriptor<Order> {
    static constexpr auto name = "ORDER";

    static constexpr auto fields = std::array{
        FieldDescriptor{
            .sqlName = "id",
            .type = SqlType::BigInt,
            .scale = 0,
            .length = 8,
            .nullable = false,
            .useAdapter = false,
            .memberPtr = &Order::id
        },
        FieldDescriptor{
            .sqlName = "total",
            .type = SqlType::Int128,
            .scale = -2,              // NUMERIC(38,2)
            .length = 16,
            .nullable = false,
            .useAdapter = true,       // Use TTNumeric adapter
            .memberPtr = &Order::total
        },
        FieldDescriptor{
            .sqlName = "discount",
            .type = SqlType::DecFloat16,
            .scale = 0,
            .length = 8,
            .nullable = false,
            .useAdapter = true,       // Use DecFloat16 adapter
            .memberPtr = &Order::discount
        },
        FieldDescriptor{
            .sqlName = "created_at",
            .type = SqlType::Timestamp,
            .scale = 0,
            .length = 8,
            .nullable = false,
            .useAdapter = true,       // Use chrono adapter
            .memberPtr = &Order::created_at
        },
        FieldDescriptor{
            .sqlName = "closed_at",
            .type = SqlType::TimestampTz,
            .scale = 0,
            .length = 12,
            .nullable = true,         // std::optional<TimestampTz>
            .useAdapter = false,      // Core extended type
            .memberPtr = &Order::closed_at
        }
    };
};
```

---

## 🔌 Интеграция Адаптеров

### TTMath для INT128 и NUMERIC(38,x)

```cpp
// INT128 (базовый интерфейс)
template<>
struct TypeAdapter<Int128> {
    using firebird_type = std::array<uint8_t, 16>;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const Int128& value) {
        std::array<uint8_t, 16> result;
        std::memcpy(result.data(), value.table, 16);  // Little-endian
        return result;
    }

    static Int128 from_firebird(const firebird_type& value) {
        Int128 result;
        std::memcpy(result.table, value.data(), 16);
        return result;
    }
};

// NUMERIC(38,x) (контекстный интерфейс)
template<int IntWords, int Scale>
struct TypeAdapter<TTNumeric<IntWords, Scale>> {
    static constexpr bool is_specialized = true;

    static void to_firebird(const TTNumeric<IntWords, Scale>& value,
                           int16_t fb_scale,
                           unsigned fb_length,
                           uint8_t* out) {
        // 1. Scale alignment
        int scale_diff = value.scale - fb_scale;
        auto adjusted = (scale_diff == 0) ? value.value_
                      : value.value_ * pow10(scale_diff);

        // 2. Two's complement with sign extension
        bool is_negative = adjusted.IsSign();
        std::array<uint8_t, 16> buffer;

        if (is_negative) {
            // Sign extension: fill with 0xFF
            std::memset(buffer.data(), 0xFF, 16);
        }

        // 3. Copy integer bytes (little-endian)
        size_t src_size = IntWords * sizeof(ttmath::uint);
        std::memcpy(buffer.data(), adjusted.table,
                   std::min(src_size, size_t(16)));

        // 4. Write to output
        std::memcpy(out, buffer.data(), fb_length);
    }

    static TTNumeric<IntWords, Scale> from_firebird(
        const uint8_t* in, unsigned fb_length, int16_t fb_scale) {
        // Reverse process...
    }
};
```

### CppDecimal для DECFLOAT

```cpp
// DECFLOAT(16) - dec::DecDouble
template<>
struct TypeAdapter<DecFloat16> {
    using firebird_type = std::array<uint8_t, 8>;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const DecFloat16& value) {
        std::array<uint8_t, 8> result;
        const void* raw_data = value.decimal_.data();
        std::memcpy(result.data(), raw_data, 8);
        return result;
    }

    static DecFloat16 from_firebird(const firebird_type& value) {
        return DecFloat16(value.data());
    }
};

// DECFLOAT(34) - dec::DecQuad (аналогично, 16 байт)
```

### std::chrono для дат/времени

```cpp
// TIMESTAMP - system_clock::time_point
template<>
struct TypeAdapter<std::chrono::system_clock::time_point> {
    using firebird_type = Timestamp;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const time_point& tp) {
        // 1. Convert Unix epoch → Firebird epoch
        constexpr int64_t EPOCH_DIFF = 40587;  // days
        auto unix_seconds = duration_cast<seconds>(tp.time_since_epoch()).count();

        // 2. Split into date + time
        int64_t total_days = unix_seconds / 86400 + EPOCH_DIFF;
        int64_t seconds_in_day = unix_seconds % 86400;

        uint32_t fb_date = static_cast<uint32_t>(total_days);
        uint32_t fb_time = static_cast<uint32_t>(seconds_in_day * 10000);

        return Timestamp(fb_date, fb_time);
    }

    static time_point from_firebird(const firebird_type& ts) {
        // Reverse conversion...
    }
};

// TIMESTAMP WITH TIME ZONE - zoned_time
template<>
struct TypeAdapter<std::chrono::zoned_time<microseconds>> {
    using firebird_type = TimestampTz;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const zoned_time<microseconds>& zt) {
        // 1. Get UTC time point
        auto utc_tp = zt.get_sys_time();

        // 2. Convert to Firebird timestamp
        auto [fb_date, fb_time] = to_firebird_timestamp(utc_tp);

        // 3. Get timezone info
        uint16_t zone_id = get_timezone_id(zt.get_time_zone()->name());
        int16_t offset_minutes = calculate_offset(zt.get_info());

        return TimestampTz(fb_date, fb_time, zone_id, offset_minutes);
    }

    // ...
};
```

---

## 💡 Пример Использования

### Определение Структуры

```cpp
#include <fbpp/fbpp.hpp>
#include <fbpp/adapters/ttmath_numeric.hpp>
#include <fbpp/adapters/cppdecimal_decfloat.hpp>
#include <fbpp/adapters/chrono_datetime.hpp>

using namespace fbpp::core;
using namespace std::chrono;

struct Order {
    int64_t id;
    std::string customer;
    TTNumeric<4, -2> total;                    // NUMERIC(38,2)
    DecFloat16 tax_rate;                        // DECFLOAT(16)
    system_clock::time_point created_at;        // TIMESTAMP
    std::optional<zoned_time<microseconds>>     // TIMESTAMP WITH TIME ZONE
        shipped_at;
};

// Generated descriptor (by schema tool)
template<>
struct StructDescriptor<Order> {
    static constexpr auto fields = std::array{
        Field{"id",         SqlType::BigInt,      &Order::id},
        Field{"customer",   SqlType::Varying,     &Order::customer},
        Field{"total",      SqlType::Int128,      &Order::total,      useAdapter=true},
        Field{"tax_rate",   SqlType::DecFloat16,  &Order::tax_rate,   useAdapter=true},
        Field{"created_at", SqlType::Timestamp,   &Order::created_at, useAdapter=true},
        Field{"shipped_at", SqlType::TimestampTz, &Order::shipped_at, nullable=true}
    };
};
```

### Вставка Данных

```cpp
// 1. Prepare statement
auto conn = Connection::create("firebird5", "/mnt/db/orders.fdb",
                              "SYSDBA", "password");
auto stmt = conn->prepare(
    "INSERT INTO orders (id, customer, total, tax_rate, created_at, shipped_at) "
    "VALUES (?, ?, ?, ?, ?, ?)"
);

// 2. Create transaction
auto txn = conn->startTransaction();

// 3. Create data
Order order{
    .id = 1001,
    .customer = "Alice Johnson",
    .total = TTNumeric<4, -2>{125099},  // $1250.99
    .tax_rate = DecFloat16{"0.08"},     // 8%
    .created_at = system_clock::now(),
    .shipped_at = std::nullopt          // Not shipped yet
};

// 4. Execute with pack()
stmt->execute(txn, order);  // ← автоматическая упаковка

// 5. Commit
txn->commit();
```

**Внутренний Flow**:
```cpp
// Statement::execute(Transaction*, const Order&)
template<typename T>
void Statement::execute(Transaction* txn, const T& params) {
    auto meta = getInputMetadata();
    std::vector<uint8_t> buffer(meta->getMessageLength());

    // Automatic dispatch to StructPacker
    fbpp::core::pack(params, buffer.data(), meta.get(), txn);

    // Execute with raw buffer
    stmt_->execute(txn->getHandle(), meta->getRawMetadata(),
                  buffer.data(), nullptr, nullptr);
}
```

### Чтение Данных

```cpp
// 1. Query
auto result = stmt->executeQuery(txn,
    "SELECT id, customer, total, tax_rate, created_at, shipped_at "
    "FROM orders WHERE id = ?",
    1001
);

// 2. Fetch as struct
Order fetched = result->fetchOne<Order>();  // ← автоматическая распаковка

// 3. Use data
std::cout << "Order #" << fetched.id << "\n"
          << "Customer: " << fetched.customer << "\n"
          << "Total: $" << fetched.total.to_double() << "\n"
          << "Tax Rate: " << fetched.tax_rate.to_string() << "\n";

if (fetched.shipped_at) {
    auto zt = *fetched.shipped_at;
    std::cout << "Shipped: "
              << std::format("{:%Y-%m-%d %H:%M:%S %Z}", zt) << "\n";
}
```

**Внутренний Flow**:
```cpp
// ResultSet::fetchOne<Order>()
template<typename T>
T ResultSet::fetchOne() {
    if (!fetch()) {
        throw FirebirdException("No rows available");
    }

    auto meta = getOutputMetadata();
    const uint8_t* buffer = getCurrentRowBuffer();

    // Automatic dispatch to StructUnpacker
    return fbpp::core::unpack<T>(buffer, meta.get(), transaction_);
}
```

---

## 🎨 Преимущества Архитектуры

### ✅ Для Разработчика

1. **Нулевой boilerplate** — работа с чистыми C++ структурами
2. **Type safety** — ошибки на этапе компиляции
3. **Современный C++20** — concepts, `std::chrono`, `std::optional`
4. **Расширяемость** — добавление новых типов через адаптеры

### ✅ Для Сопровождения

1. **Единый кодек** — одно место для всех преобразований
2. **Генерация дескрипторов** — sync с схемой БД
3. **Compile-time dispatch** — zero runtime overhead
4. **Тестируемость** — каждый уровень независим

### ✅ Для Производительности

1. **Zero-copy** — прямое копирование памяти где возможно
2. **Template-heavy** — максимум оптимизаций компилятора
3. **No virtual calls** — статическая диспетчеризация
4. **Cache-friendly** — линейный layout буфера

---

## 📊 Сравнение с Альтернативами

| Подход | Код | Type Safety | Расширенные Типы | Производительность |
|--------|-----|-------------|------------------|-------------------|
| **Raw Firebird API** | ⚠️ Много | ❌ Нет | ✅ Да | ⭐⭐⭐⭐⭐ |
| **libfbclient C++** | ⚠️ Средне | ⚠️ Частично | ⚠️ Ограничено | ⭐⭐⭐⭐ |
| **SOCI/ODBC** | ✅ Мало | ✅ Да | ❌ Нет | ⭐⭐⭐ |
| **fbpp (наш)** | ✅ Минимум | ✅ Полная | ✅ Все типы | ⭐⭐⭐⭐⭐ |

---

## 🔧 Расширение Системы

### Добавление Нового Адаптера

Пример: поддержка `boost::multiprecision::cpp_dec_float_50` для high-precision DECFLOAT.

```cpp
// 1. Специализация адаптера
namespace fbpp::core {
template<>
struct TypeAdapter<boost::multiprecision::cpp_dec_float_50> {
    using firebird_type = DecFloat34;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const cpp_dec_float_50& value) {
        // Конвертация в IEEE 754-2008 decimal128
        std::string str = value.str(34);  // 34 significant digits
        return DecFloat34(str.c_str());
    }

    static cpp_dec_float_50 from_firebird(const firebird_type& value) {
        std::string str = value.to_string();
        return cpp_dec_float_50(str);
    }
};
} // namespace

// 2. Использование
struct Scientific {
    cpp_dec_float_50 precise_value;  // DECFLOAT(34)
};

// 3. Дескриптор помечает useAdapter = true
template<>
struct StructDescriptor<Scientific> {
    static constexpr auto fields = std::array{
        Field{"precise_value", SqlType::DecFloat34,
              &Scientific::precise_value, useAdapter=true}
    };
};

// 4. Автоматическая работа через sql_value_codec
// Никакие другие файлы менять не нужно!
```

### Добавление Нового Firebird Типа

Пример: поддержка `INT256` (гипотетический будущий тип).

```cpp
// 1. Добавить в extended_types.hpp
class Int256 {
    std::array<uint8_t, 32> data_;
public:
    explicit Int256(const uint8_t* raw) {
        std::memcpy(data_.data(), raw, 32);
    }
    const uint8_t* data() const { return data_.data(); }
};

// 2. Добавить в sql_value_codec.hpp
template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const T& value, uint8_t* dataPtr) {
    // ... existing code ...

    else if constexpr (std::is_same_v<ValueType, Int256>) {
        std::memcpy(dataPtr, value.data(), 32);
        setNotNull(ctx.nullIndicator);
        return;
    }
}

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, T& value) {
    // ... existing code ...

    else if constexpr (std::is_same_v<ValueType, Int256>) {
        value = Int256(dataPtr);
        return;
    }
}

// 3. Готово! Все упаковщики автоматически поддерживают Int256
```

---

## 📚 Ссылки на Файлы

### Система Адаптеров
- **include/fbpp/core/type_adapter.hpp** — Trait-система, детекторы интерфейсов
- **include/fbpp/adapters/ttmath_int128.hpp** — INT128 через TTMath
- **include/fbpp/adapters/ttmath_numeric.hpp** — NUMERIC(38,x) с масштабом
- **include/fbpp/adapters/cppdecimal_decfloat.hpp** — DECFLOAT(16/34)
- **include/fbpp/adapters/chrono_datetime.hpp** — std::chrono типы

### Центральный Кодек
- **include/fbpp/core/detail/sql_value_codec.hpp** — write_sql_value/read_sql_value

### Упаковщики
- **include/fbpp/core/tuple_packer.hpp** — std::tuple упаковка
- **include/fbpp/core/json_packer.hpp** — nlohmann::json упаковка
- **include/fbpp/core/struct_packer.hpp** — struct упаковка (TODO)

### Расширенные Типы
- **include/fbpp/core/extended_types.hpp** — Int128, DecFloat, TimestampTz
- **include/fbpp/core/timestamp_utils.hpp** — Конверсия epoch, timezone

### Документация
- **doc/FIREBIRD_TYPES_HANDLING.md** — Правила обработки типов
- **doc/EXTENDED_TYPES_ADAPTERS.md** — Детали адаптеров (29KB)
- **doc/structure_pack.md** — Архитектура StructPacker
- **doc/TUPLE_PACKER_ARCHITECTURE.md** — Дизайн TuplePacker

### Тесты
- **tests/adapters/test_ttmath_int128.cpp** — Тесты TTMath INT128
- **tests/adapters/test_ttmath_scale.cpp** — Тесты NUMERIC с масштабом
- **tests/adapters/test_cppdecimal_decfloat.cpp** — Тесты DECFLOAT

---

## 🎓 Заключение

Архитектура fbpp упаковщиков предоставляет:

1. **Единообразный интерфейс** — `pack()/unpack()` для всех форматов
2. **Модульность** — каждый компонент независим и тестируем
3. **Расширяемость** — новые типы добавляются без изменения кодека
4. **Производительность** — compile-time dispatch, zero-copy, no virtuals
5. **Современность** — C++20 concepts, chrono, optional

**Ключевая идея**: разработчик описывает структуру данных на C++, система автоматически выполняет все преобразования благодаря trait-системе адаптеров и центральному кодеку типов.

---

**Дата создания**: 2025-11-10
**Автор**: Claude Code
**Версия**: 1.0
