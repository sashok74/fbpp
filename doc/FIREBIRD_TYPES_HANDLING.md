# Памятка по работе с типами Firebird в fbpp

## Критически важные правила определения типов

### 1. Иерархия проверок для FieldType::Numeric

**ВСЕГДА проверять в следующем порядке:**

```cpp
case FieldType::Numeric: {
    // 1️⃣ Сначала DECFLOAT типы (по wire_type)
    if (field.ext_meta.wire_type == SQL_DECFLOAT16) {
        // DECFLOAT(16) - 8 байт, IEEE 754-2008
        use: pack_decfloat16() / unpack_decfloat16()
    }
    else if (field.ext_meta.wire_type == SQL_DECFLOAT34) {
        // DECFLOAT(34) - 16 байт, IEEE 754-2008
        use: pack_decfloat34() / unpack_decfloat34()
    }
    // 2️⃣ Потом проверяем scale для NUMERIC/DECIMAL
    else if (field.scale != 0) {
        // NUMERIC/DECIMAL со scale (scale всегда <= 0)
        // Выбор INT64 vs INT128 по размеру или precision:
        if (field.length == 16 || field.ext_meta.precision > 18) {
            // NUMERIC(38,x) использует INT128
            use: pack_scaled_int128(value, scale) / unpack_scaled_int128(scale)
        } else {
            // NUMERIC(18,x) и меньше использует INT64
            use: pack_scaled_int64(value, scale) / unpack_scaled_int64(scale)
        }
    }
    // 3️⃣ Только потом проверяем чистый INT128
    else if (field.ext_meta.wire_type == SQL_INT128 && field.scale == 0) {
        // Чистый INT128 (170-битное целое без scale)
        use: pack_int128() / unpack_int128()
    }
}
```

### 2. Формула для NUMERIC/DECIMAL

```
Хранимое_значение = Реальное_значение × 10^(-scale)
```

**Примеры:**
- `NUMERIC(38,2)`: значение "123.45" → хранится как INT128(12345), scale=-2
- `NUMERIC(18,4)`: значение "99.9999" → хранится как INT64(999999), scale=-4
- `NUMERIC(38,0)`: значение "12345" → хранится как INT128(12345), scale=0

### 3. Ключевые константы типов

```cpp
// Базовые SQL типы (4xx-5xx)
SQL_SHORT    = 500  // SMALLINT (INT16)
SQL_LONG     = 496  // INTEGER (INT32)
SQL_INT64    = 580  // BIGINT (INT64)
SQL_FLOAT    = 482  // FLOAT (FLOAT32)
SQL_DOUBLE   = 480  // DOUBLE PRECISION (DOUBLE64)
SQL_DATE     = 510  // DATE
SQL_TIME     = 560  // TIME
SQL_TIMESTAMP = 510 // TIMESTAMP

// Расширенные типы (32xxx)
SQL_INT128      = 32752  // INT128 или основа для NUMERIC(38,x)
SQL_DECFLOAT16  = 32760  // DECFLOAT(16) - Decimal64
SQL_DECFLOAT34  = 32762  // DECFLOAT(34) - Decimal128
SQL_TIME_TZ     = 32756  // TIME WITH TIME ZONE
SQL_TIMESTAMP_TZ = 32754 // TIMESTAMP WITH TIME ZONE
```

### 4. Определение типа NUMERIC по метаданным

| SQL тип | field.type | field.scale | field.length | wire_type | Использовать |
|---------|------------|-------------|--------------|-----------|--------------|
| NUMERIC(9,2) | Numeric | -2 | 4 | SQL_LONG | pack_scaled_int64 |
| NUMERIC(18,4) | Numeric | -4 | 8 | SQL_INT64 | pack_scaled_int64 |
| NUMERIC(38,2) | Numeric | -2 | 16 | SQL_INT128 | pack_scaled_int128 |
| INT128 | Numeric | 0 | 16 | SQL_INT128 | pack_int128 |
| DECFLOAT(16) | Numeric | N/A | 8 | SQL_DECFLOAT16 | pack_decfloat16 |
| DECFLOAT(34) | Numeric | N/A | 16 | SQL_DECFLOAT34 | pack_decfloat34 |

### 5. Важные особенности

#### Scale в Firebird
- **Всегда <= 0** для NUMERIC/DECIMAL
- Отрицательный scale означает количество десятичных знаков
- `scale = -2` означает 2 знака после запятой
- `scale = 0` означает целое число

#### Проверка типа NUMERIC(38,x)
```cpp
bool is_numeric38_with_scale(const FieldDesc& field) {
    return field.type == FieldType::Numeric &&
           field.scale < 0 &&  // Есть десятичные знаки
           (field.length == 16 ||  // 16 байт = INT128
            field.ext_meta.precision > 18 ||  // Точность > 18
            field.ext_meta.wire_type == SQL_INT128);  // Явно INT128
}
```

#### Различие INT128 и NUMERIC(38,x)
- **INT128**: `wire_type == SQL_INT128 && scale == 0`
- **NUMERIC(38,x)**: `wire_type == SQL_INT128 && scale != 0`

### 6. Примеры правильной обработки

#### JsonPacker (правильный подход)
```cpp
case FieldType::Numeric:
    if (field.scale != 0) {
        // NUMERIC/DECIMAL - проверяем размер для выбора INT64/INT128
        if (field.ext_meta.precision > 18) {
            converter->pack_scaled_int128(value, field.scale, buffer, length);
        } else {
            converter->pack_scaled_int64(value, field.scale, buffer, length);
        }
    } else if (field.ext_meta.wire_type == SQL_INT128) {
        // Чистый INT128
        converter->pack_int128(value, buffer, length);
    }
```

#### MessageWriter (абстракция)
```cpp
void put_decimal(size_t field_idx, const std::string& value) {
    const auto& field = get_field(field_idx);
    // MessageWriter сам определяет нужную функцию по метаданным
    if (field.length == 16) {
        converter_->pack_scaled_int128(value, field.scale, ...);
    } else {
        converter_->pack_scaled_int64(value, field.scale, ...);
    }
}
```

### 7. Частые ошибки

❌ **Неправильно**: Проверять только wire_type для NUMERIC
```cpp
if (field.ext_meta.wire_type == SQL_INT128) {
    // Это может быть и INT128, и NUMERIC(38,x)!
}
```

✅ **Правильно**: Учитывать scale
```cpp
if (field.ext_meta.wire_type == SQL_INT128 && field.scale != 0) {
    // Это точно NUMERIC(38,x) со scale
}
```

❌ **Неправильно**: Игнорировать precision
```cpp
if (field.scale != 0) {
    pack_scaled_int64(...);  // А что если NUMERIC(38,2)?
}
```

✅ **Правильно**: Проверять precision или размер
```cpp
if (field.scale != 0) {
    if (field.ext_meta.precision > 18 || field.length == 16) {
        pack_scaled_int128(...);
    } else {
        pack_scaled_int64(...);
    }
}
```

### 8. Тестовые случаи для проверки

При реализации ВСЕГДА тестировать:
1. `NUMERIC(9,2)` - INT32 со scale
2. `NUMERIC(18,4)` - INT64 со scale  
3. `NUMERIC(38,2)` - INT128 со scale
4. `NUMERIC(38,38)` - INT128 с максимальным scale
5. `INT128` - чистый INT128 без scale
6. `DECFLOAT(16)` - IEEE decimal
7. `DECFLOAT(34)` - IEEE decimal

### 9. Диагностика типа в runtime

```cpp
void diagnose_numeric_type(const FieldDesc& field) {
    if (field.type != FieldType::Numeric) return;
    
    std::cout << "Numeric type analysis:\n";
    std::cout << "  scale: " << field.scale << "\n";
    std::cout << "  length: " << field.length << " bytes\n";
    std::cout << "  precision: " << field.ext_meta.precision << "\n";
    std::cout << "  wire_type: " << field.ext_meta.wire_type << "\n";
    
    if (field.ext_meta.wire_type == SQL_DECFLOAT16) {
        std::cout << "  => DECFLOAT(16)\n";
    } else if (field.ext_meta.wire_type == SQL_DECFLOAT34) {
        std::cout << "  => DECFLOAT(34)\n";
    } else if (field.scale != 0) {
        if (field.length == 16 || field.ext_meta.precision > 18) {
            std::cout << "  => NUMERIC(" << field.ext_meta.precision 
                     << "," << -field.scale << ") using INT128\n";
        } else {
            std::cout << "  => NUMERIC(" << field.ext_meta.precision 
                     << "," << -field.scale << ") using INT64\n";
        }
    } else if (field.ext_meta.wire_type == SQL_INT128) {
        std::cout << "  => INT128 (pure integer)\n";
    }
}
```

## Работа с типами даты и времени

### 1. Форматы хранения в Firebird

#### DATE (SQL_TYPE_DATE = 570)
- **Размер**: 4 байта
- **Формат**: количество дней с 17 ноября 1858 года (Modified Julian Day Number)
- **Диапазон**: от 1 января 100 года до 29 февраля 32768 года

#### TIME (SQL_TYPE_TIME = 560)
- **Размер**: 4 байта
- **Формат**: время дня в единицах 1/10000 секунды (0-863999999)
- **Точность**: до 0.1 миллисекунды

#### TIMESTAMP (SQL_TIMESTAMP = 510)
- **Размер**: 8 байт
- **Структура**:
  - Первые 4 байта: ISC_DATE (дни с 17.11.1858)
  - Следующие 4 байта: ISC_TIME (доли дня в 1/10000 сек)

#### TIME WITH TIME ZONE (SQL_TIME_TZ = 32756)
- **Размер**: 8 байт
- **Структура**:
  - Первые 4 байта: ISC_TIME
  - Следующие 2 байта: zone_id (IANA timezone ID)
  - Последние 2 байта: offset в минутах от UTC

#### TIMESTAMP WITH TIME ZONE (SQL_TIMESTAMP_TZ = 32754)
- **Размер**: 12 байт
- **Структура**:
  - Первые 8 байт: ISC_TIMESTAMP
  - Следующие 2 байта: zone_id (IANA timezone ID)
  - Последние 2 байта: offset в минутах от UTC

### 2. Константы и преобразования

```cpp
namespace fbpp::core::timestamp_utils {
    // Разница между Firebird epoch (17.11.1858) и Unix epoch (01.01.1970)
    constexpr int32_t FIREBIRD_EPOCH_DIFF = 40587;  // дней
    
    // Количество единиц времени в сутках (1/10000 секунды)
    constexpr uint32_t TIME_UNITS_PER_DAY = 864000000;
}
```

### 3. Функции конверсии

#### Преобразование std::chrono → Firebird

```cpp
// Unix timestamp → Firebird ISC_TIMESTAMP
std::pair<uint32_t, uint32_t> to_firebird_timestamp(
    std::chrono::system_clock::time_point tp) {
    
    // Получаем микросекунды с Unix эпохи
    auto since_epoch = tp.time_since_epoch();
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    auto days = total_seconds.count() / 86400;
    auto seconds_today = total_seconds.count() % 86400;
    
    // Конвертируем в формат Firebird
    uint32_t fb_date = days + FIREBIRD_EPOCH_DIFF;
    uint32_t fb_time = seconds_today * 10000;  // В единицах 1/10000 сек
    
    // Добавляем доли секунды
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch);
    auto micro_fraction = microseconds.count() % 1000000;
    fb_time += micro_fraction / 100;  // Конвертируем в 1/10000 сек
    
    return {fb_date, fb_time};
}
```

#### Преобразование Firebird → std::chrono

```cpp
// Firebird ISC_TIMESTAMP → Unix timestamp
std::chrono::system_clock::time_point from_firebird_timestamp(
    uint32_t fb_date, uint32_t fb_time) {
    
    // Дни с Unix эпохи
    int64_t days_since_unix = fb_date - FIREBIRD_EPOCH_DIFF;
    
    // Конвертируем время в микросекунды
    int64_t time_micros = (fb_time * 100LL);  // 1/10000 сек → микросекунды
    
    // Общее количество микросекунд
    int64_t total_micros = days_since_unix * 86400000000LL + time_micros;
    
    return std::chrono::system_clock::time_point(
        std::chrono::microseconds(total_micros)
    );
}
```

### 4. Использование в классах

#### Класс Timestamp

```cpp
class Timestamp {
private:
    uint32_t date_;  // ISC_DATE
    uint32_t time_;  // ISC_TIME
    
public:
    // Конструктор из std::chrono
    explicit Timestamp(std::chrono::system_clock::time_point tp) {
        auto [fb_date, fb_time] = timestamp_utils::to_firebird_timestamp(tp);
        date_ = fb_date;
        time_ = fb_time;
    }
    
    // Текущее время
    static Timestamp now() {
        return Timestamp(std::chrono::system_clock::now());
    }
    
    // Конверсия обратно в std::chrono
    std::chrono::system_clock::time_point to_time_point() const {
        return timestamp_utils::from_firebird_timestamp(date_, time_);
    }
    
    // Для pack_utils
    uint32_t getDate() const { return date_; }
    uint32_t getTime() const { return time_; }
};
```

#### Класс Time

```cpp
class Time {
private:
    uint32_t time_;  // ISC_TIME (доли дня)
    
public:
    // Конструктор из часов, минут, секунд
    Time(uint8_t hours, uint8_t minutes, uint8_t seconds, uint32_t fractions = 0) {
        time_ = hours * 36000000 +      // часы в 1/10000 сек
                minutes * 600000 +       // минуты в 1/10000 сек
                seconds * 10000 +        // секунды в 1/10000 сек
                fractions;               // уже в 1/10000 сек
    }
    
    // Из std::chrono::duration
    template<typename Rep, typename Period>
    explicit Time(std::chrono::duration<Rep, Period> d) {
        auto total_micros = std::chrono::duration_cast<std::chrono::microseconds>(d);
        time_ = (total_micros.count() / 100) % TIME_UNITS_PER_DAY;
    }
    
    // Текущее время дня
    static Time now() {
        auto now = std::chrono::system_clock::now();
        auto today = std::chrono::floor<std::chrono::days>(now);
        auto time_since_midnight = now - today;
        return Time(time_since_midnight);
    }
    
    uint32_t getTime() const { return time_; }
};
```

### 5. Упаковка в pack_utils.hpp

```cpp
// Timestamp
} else if constexpr (std::is_same_v<T, Timestamp>) {
    uint32_t date = value.getDate();
    uint32_t time = value.getTime();
    std::memcpy(dataPtr, &date, 4);
    std::memcpy(dataPtr + 4, &time, 4);
    
// TimestampTz
} else if constexpr (std::is_same_v<T, TimestampTz>) {
    uint32_t date = value.getDate();
    uint32_t time = value.getTime();
    uint16_t zone = value.getZoneId();
    int16_t offset = value.getOffset();
    std::memcpy(dataPtr, &date, 4);
    std::memcpy(dataPtr + 4, &time, 4);
    std::memcpy(dataPtr + 8, &zone, 2);
    std::memcpy(dataPtr + 10, &offset, 2);
    
// Time
} else if constexpr (std::is_same_v<T, Time>) {
    uint32_t time = value.getTime();
    std::memcpy(dataPtr, &time, 4);
    
// TimeTz
} else if constexpr (std::is_same_v<T, TimeTz>) {
    uint32_t time = value.getTime();
    uint16_t zone = value.getZoneId();
    int16_t offset = value.getOffset();
    std::memcpy(dataPtr, &time, 4);
    std::memcpy(dataPtr + 4, &zone, 2);
    std::memcpy(dataPtr + 6, &offset, 2);
}
```

### 6. Примеры использования

```cpp
// Текущее время
Timestamp ts1 = Timestamp::now();

// Конкретная дата/время через chrono
auto tp = std::chrono::system_clock::from_time_t(1705323045);  // Unix timestamp
Timestamp ts2(tp);

// Время дня
Time t1(14, 30, 45, 1234);  // 14:30:45.1234
Time t2 = Time::now();       // Текущее время дня

// С временной зоной (требует дополнительной реализации)
TimestampTz tstz(Timestamp::now(), 123, 120);  // zone_id=123, offset=+120 минут

// В тестах
auto tuple = std::make_tuple(
    smallint_val,
    integer_val,
    Timestamp::now(),     // Реальное текущее время
    Time::now(),          // Реальное время дня
    // ...
);
```

### 7. Важные особенности

1. **Firebird epoch**: 17 ноября 1858 года (Modified Julian Day Number)
2. **Unix epoch**: 1 января 1970 года
3. **Разница**: 40587 дней
4. **Точность времени**: 1/10000 секунды (0.1 миллисекунды)
5. **Timezone ID**: Соответствует IANA timezone database

### 8. Тестирование

При тестировании обязательно проверить:
1. Корректность преобразования epoch (1858 ↔ 1970)
2. Граничные значения (полночь, конец дня)
3. Високосные годы
4. Различные временные зоны
5. Точность до долей секунды

## Работа с BLOB полями

### TextBlob - автоматическая работа с текстовыми BLOB

**TextBlob** - специальный класс для упрощения работы с текстовыми BLOB полями (SUB_TYPE 1).

#### Определение класса

```cpp
class TextBlob : public Blob {
private:
    mutable std::optional<std::string> cached_text_;
    
public:
    // Конструкторы
    TextBlob();                                  // Пустой BLOB
    explicit TextBlob(const uint8_t* id);        // Из ID существующего BLOB
    explicit TextBlob(const std::string& text);  // Из текста
    
    // Методы работы с текстом
    bool hasText() const;                        // Есть ли кешированный текст
    const std::string& getText() const;          // Получить текст
    void setText(const std::string& text);       // Установить текст
    void clearText();                             // Очистить кеш
};
```

#### Текущие возможности

1. **Базовая поддержка в tuple_packer/unpacker**:
   - TextBlob упаковывается как обычный BLOB ID (8 байт)
   - При распаковке читается только ID BLOB

2. **Расширенная поддержка (packWithBlobs/unpackWithBlobs)**:
   - Методы в TuplePacker и TupleUnpacker для работы с IAttachment
   - Автоматическое создание BLOB из текста при упаковке
   - Автоматическое чтение текста из BLOB при распаковке

#### Ограничения текущей реализации

- Для автоматического создания/чтения BLOB требуется передача IAttachment и ITransaction
- В стандартных методах execute/openCursor пока используется обычная упаковка без создания BLOB

#### Пример использования (будущая реализация)

```cpp
// Создание текстового BLOB
TextBlob description("Длинный текст описания товара...");

// Вставка с автоматическим созданием BLOB
auto tuple = std::make_tuple(
    product_id,
    product_name, 
    description    // TextBlob будет автоматически создан в БД
);

// Для полной поддержки нужно использовать специальные методы:
statement->executeTupleWithBlobs(transaction, tuple);

// При чтении:
auto result = statement->openCursorTupleWithBlobs(transaction);
auto [id, name, desc] = result->fetchNextTuple<int, std::string, TextBlob>();
std::cout << desc.getText(); // Текст автоматически прочитан из BLOB
```

#### План развития

1. Интеграция packWithBlobs в Statement::executeTuple
2. Добавление executeTupleWithBlobs и openCursorTupleWithBlobs
3. Поддержка потокового чтения/записи больших BLOB
4. Автоматическое определение SUB_TYPE через метаданные

## Заключение

**Золотое правило**: При работе с FieldType::Numeric ВСЕГДА проверять в порядке:
1. DECFLOAT (по wire_type)
2. NUMERIC/DECIMAL (по scale != 0)
3. INT128 (по wire_type && scale == 0)

**Для даты/времени**: ВСЕГДА использовать правильное преобразование между эпохами Firebird (1858) и Unix (1970), учитывая разницу в 40587 дней.

**Для BLOB**: Использовать TextBlob для текстовых BLOB (SUB_TYPE 1) и обычный Blob для бинарных данных.

Это гарантирует правильную обработку всех типов Firebird.