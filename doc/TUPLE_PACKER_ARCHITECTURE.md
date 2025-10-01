# TuplePacker Architecture

## Overview
`TuplePacker` is a template class that provides compile-time type-safe packing of C++ tuples into Firebird database message buffers.

## Architecture Diagram

```mermaid
graph TB
    subgraph "TuplePacker - Основной класс"
        TP[TuplePacker<Args...>]
        TP --> |"pack()"|PACK["Упаковка tuple в буфер"]
        TP --> |"buildMetadata()"|BUILD["Создание метаданных"]
    end

    subgraph "Процесс упаковки tuple"
        TUPLE["std::tuple<int, string, double, ...>"]
        TUPLE --> PACK
        PACK --> |"1. Проверка"|VALIDATE["Валидация:<br/>- buffer != null<br/>- metadata != null<br/>- sizeof...(Args) == metadata->getCount()"]
        VALIDATE --> |"2. Очистка"|CLEAR["memset(buffer, 0, bufSize)"]
        CLEAR --> |"3. Рекурсия"|HELPER["TuplePackHelper::pack()"]
    end

    subgraph "TuplePackHelper - Рекурсивная упаковка"
        HELPER --> ITER["Index = sizeof...(Args)"]
        ITER --> GET_OFFSETS["Получить из metadata:<br/>- data_offset<br/>- null_offset<br/>- maxLength"]
        GET_OFFSETS --> PACK_ONE["packValue(std::get<idx>(tuple))"]
        PACK_ONE --> CHECK_MORE{Index > 1?}
        CHECK_MORE -->|Да| NEXT["Index - 1"]
        NEXT --> GET_OFFSETS
        CHECK_MORE -->|Нет| DONE["Завершение"]
    end

    subgraph "packValue() - Упаковка одного значения"
        PACK_ONE --> TYPE_CHECK{"Проверка типа T"}
        
        TYPE_CHECK -->|"std::string"| STR["VARCHAR:<br/>2 байта длина<br/>+ данные<br/>(с truncate)"]
        TYPE_CHECK -->|"bool"| BOOL["1 байт:<br/>0 или 1"]
        TYPE_CHECK -->|"Int128"| I128["16 байт<br/>raw данные"]
        TYPE_CHECK -->|"DecFloat16"| DF16["8 байт<br/>IEEE decimal"]
        TYPE_CHECK -->|"DecFloat34"| DF34["16 байт<br/>IEEE decimal"]
        TYPE_CHECK -->|"Timestamp"| TS["8 байт:<br/>4b date + 4b time"]
        TYPE_CHECK -->|"TimestampTz"| TSTZ["12 байт:<br/>8b timestamp<br/>+ 2b zone<br/>+ 2b offset"]
        TYPE_CHECK -->|"Time"| TIME["4 байта:<br/>время дня"]
        TYPE_CHECK -->|"TimeTz"| TIMETZ["8 байт:<br/>4b time<br/>+ 2b zone<br/>+ 2b offset"]
        TYPE_CHECK -->|"numeric"| NUM["sizeof(T) байт<br/>прямое копирование"]
        TYPE_CHECK -->|"std::optional<T>"| OPT["Если has_value():<br/>pack(*value)<br/>Иначе:<br/>null_ptr = -1"]
        
        STR --> SET_NULL["null_ptr = 0<br/>(не NULL)"]
        BOOL --> SET_NULL
        I128 --> SET_NULL
        DF16 --> SET_NULL
        DF34 --> SET_NULL
        TS --> SET_NULL
        TSTZ --> SET_NULL
        TIME --> SET_NULL
        TIMETZ --> SET_NULL
        NUM --> SET_NULL
        OPT --> |"или"| SET_NULL_FLAG["null_ptr = -1<br/>(NULL)"]
    end

    subgraph "Структура буфера в памяти"
        BUFFER["Буфер результата"]
        BUFFER --> LAYOUT["
        ┌─────────────────────┐
        │ Field 0 Data        │ ← data_offset[0]
        ├─────────────────────┤
        │ Field 1 Data        │ ← data_offset[1]
        ├─────────────────────┤
        │ Field 2 Data        │ ← data_offset[2]
        ├─────────────────────┤
        │ ...                 │
        ├─────────────────────┤
        │ NULL indicator 0    │ ← null_offset[0] (2 bytes)
        ├─────────────────────┤
        │ NULL indicator 1    │ ← null_offset[1] (2 bytes)
        ├─────────────────────┤
        │ NULL indicator 2    │ ← null_offset[2] (2 bytes)
        └─────────────────────┘
        "]
    end

    subgraph "buildMetadata() - Создание метаданных"
        BUILD --> MB["MessageBuilder(field_count)"]
        MB --> RECURSE["TuplePackHelper::buildMetadata()"]
        RECURSE --> ADD_FIELD["Для каждого типа в tuple:<br/>builder.addField<T>(name)"]
        ADD_FIELD --> |"string"| STR_FIELD["addFieldWithLength<br/>(name, 255)"]
        ADD_FIELD --> |"другие"| OTHER_FIELD["addField<T>(name)"]
        STR_FIELD --> NEXT_FIELD["index++"]
        OTHER_FIELD --> NEXT_FIELD
        NEXT_FIELD --> |"рекурсия"| ADD_FIELD
        NEXT_FIELD --> |"конец"| FINISH["builder.build()<br/>→ MessageMetadata"]
    end

    style TP fill:#e1f5fe
    style TUPLE fill:#fff3e0
    style BUFFER fill:#f3e5f5
    style TYPE_CHECK fill:#fff9c4
    style HELPER fill:#c8e6c9
```

## Key Components

### 1. **TuplePacker** - Main Template Class
- Takes variadic template parameters `Args...`
- Stores metadata and buffer size
- Provides `pack()` method to pack tuple into buffer
- Thread-safe for const operations

### 2. **TuplePackHelper** - Recursive Helper
- Uses compile-time recursion via template specialization
- Processes tuple elements from last to first
- Base case: `TuplePackHelper<0, Args...>` - empty implementation
- Index calculation: `idx = sizeof...(Args) - Index`

### 3. **packValue()** - Value Packing Function
- Overloaded for different types using `if constexpr`
- Special handling for `std::optional<T>` to support NULL values
- Handles string truncation with warnings
- Type-specific serialization:
  - **Strings**: 2-byte length prefix + UTF-8 data
  - **Bool**: Single byte (0 or 1)
  - **Int128**: 16 bytes raw data
  - **DecFloat16/34**: IEEE 754-2008 decimal format
  - **Timestamp**: Firebird format (date + time)
  - **Numeric types**: Direct memory copy

### 4. **Metadata Management**
- `MessageMetadata` provides field offsets and sizes
- NULL indicators stored separately (2 bytes per field)
- Support for maximum length constraints on VARCHAR fields
- Automatic field naming: `PARAM_0`, `PARAM_1`, etc.

## Memory Layout

The packed buffer follows Firebird's message format:

```
Buffer Layout:
┌──────────────────────────┐ Offset 0
│   Field 0 Data           │ 
├──────────────────────────┤ Offset N
│   Field 1 Data           │
├──────────────────────────┤ Offset M
│   Field 2 Data           │
├──────────────────────────┤
│   ...                    │
├──────────────────────────┤ NULL Offset Area
│   NULL Indicator 0 (2B)  │
├──────────────────────────┤
│   NULL Indicator 1 (2B)  │
├──────────────────────────┤
│   NULL Indicator 2 (2B)  │
└──────────────────────────┘
```

NULL Indicator values:
- `0`: Value is NOT NULL
- `-1`: Value is NULL

## Usage Example

```cpp
// Define data tuple
auto data = std::make_tuple(
    42,                        // INTEGER
    std::string("Hello"),      // VARCHAR
    3.14,                      // DOUBLE
    std::optional<int>(),      // NULL INTEGER
    Timestamp::now(),          // TIMESTAMP
    Int128(123456789),         // INT128
    DecFloat34("123.456")      // DECFLOAT(34)
);

// Create packer
TuplePacker<int, std::string, double, std::optional<int>, 
            Timestamp, Int128, DecFloat34> packer;

// Get metadata from prepared statement
auto stmt = connection->prepareStatement("INSERT INTO test VALUES (?, ?, ?, ?, ?, ?, ?)");
auto metadata = stmt->getInputMetadata();

// Pack into buffer
std::vector<uint8_t> buffer(metadata->getMessageLength());
packer.pack(data, buffer.data(), metadata);

// Execute with packed buffer
stmt->execute(transaction, metadata, buffer.data());
```

## Type Support

### Primitive Types
- `int16_t`, `int32_t`, `int64_t`
- `float`, `double`
- `bool`
- `const char*`, `std::string`

### Extended Types
- `Int128` - 128-bit integer
- `DecFloat16` - DECFLOAT(16) 
- `DecFloat34` - DECFLOAT(34)
- `Timestamp` - Date and time
- `TimestampTz` - Timestamp with timezone
- `Time` - Time of day
- `TimeTz` - Time with timezone
- `Blob` - Binary large object reference

### Optional Types
- `std::optional<T>` for any supported type T
- Properly handles NULL database values

## Implementation Details

### Compile-Time Recursion
The packer uses template metaprogramming to unroll the tuple at compile time:

```cpp
template<size_t Index, typename... Args>
struct TuplePackHelper {
    static void pack(const std::tuple<Args...>& tuple, 
                    uint8_t* buffer, 
                    const MessageMetadata* metadata) {
        // Pack current element
        packValue(std::get<idx>(tuple), ...);
        
        // Recursive call for remaining elements
        if constexpr (Index > 1) {
            TuplePackHelper<Index - 1, Args...>::pack(tuple, buffer, metadata);
        }
    }
};
```

### String Truncation
Strings longer than the database field length are automatically truncated with a warning:

```cpp
if (maxLength > 0 && actualLen > maxLength) {
    actualLen = maxLength;
    logger->warn("String truncated from {} to {} bytes", 
                 value.length(), maxLength);
}
```

### Error Handling
- Validates buffer and metadata pointers
- Checks tuple arity matches metadata field count
- Throws `FirebirdException` on errors
- Provides detailed error messages with field indices

## Performance Considerations

1. **Zero-copy for primitive types**: Direct memory copy for numeric types
2. **Compile-time optimization**: Template recursion unrolled at compile time
3. **Buffer pre-clearing**: Single memset for entire buffer
4. **Minimal allocations**: Works with pre-allocated buffers

## Thread Safety

- `TuplePacker` instances are thread-safe for const operations
- `pack()` method is const and can be called concurrently
- No internal state modification during packing
- Metadata building is lazy and protected

## Future Enhancements

1. Support for custom type serialization via traits
2. Batch packing optimization for multiple tuples
3. Compile-time validation of type compatibility
4. Support for nested tuples and structures