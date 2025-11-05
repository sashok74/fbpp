
# Архитектура упаковщика строго типизированных структур

## Цели

- Автоматически сопоставлять бинарный буфер Firebird со строго типизированными C++-структурами без ручного кода.
- Использовать единый публичный API `fbpp::core::pack` / `fbpp::core::unpack`, как и для `std::tuple` и `nlohmann::json`.
- Корректно поддерживать nullable-поля через `std::optional<T>` и гарантировать раннюю диагностику нарушений схемы.
- Спрятать детали реализации и преобразований в `detail`, чтобы потребителю были видны только готовые структуры и вызовы.
- Переиспользовать существующие преобразования Firebird-типы ↔ C++ (числа со scale, даты/время, BLOB, UUID, DecFloat и др.).

## Общая схема

1. Генератор схемы БД создаёт пары «структура + статический дескриптор» для каждой сущности.
2. Дескриптор описывает поля (имя, Firebird-тип, scale, длина, nullable, указатель на поле и флаги адаптера).
3. `StructPacker<T>` читает дескриптор и заполняет Firebird-буфер, `StructUnpacker<T>` делает обратное.
4. `fbpp::core::pack/unpack` выбирают стратегию во время компиляции: tuple → TuplePacker, json → JsonPacker, struct → StructPacker, fallback → CustomPacker.

## Контракт генерируемых структур

```cpp
template<typename T>
struct StructDescriptor;

struct FieldDescriptor {
    const char* sqlName;
    SqlType type;
    int16_t scale;
    uint16_t length;
    uint16_t subType;
    bool nullable;
    bool useAdapter;
    auto memberPtr; // указатель на поле: T::*
};
```

Генератор создаёт специализации:

```cpp
template<>
struct StructDescriptor<Employee> {
    static constexpr auto name = "EMPLOYEE";
    static constexpr auto fields = std::array{
        makeField<&Employee::name>("NAME", SqlType::Varying, 0, 128, /*nullable*/false),
        makeField<&Employee::age>("AGE", SqlType::Integer, 0, sizeof(int32_t), false),
        makeField<&Employee::email>("EMAIL", SqlType::Varying, 0, 256, true)
    };
};
```

Дополнительно генерируются:
- `is_struct_packable_v<T>` — constexpr-флаг для `if constexpr` в публичном API.
- При необходимости — псевдонимы/traits для интеграции с системой адаптеров.

## Алгоритм StructPacker

1. Проверить `buffer`, `metadata`, транзакцию.
2. Получить `auto& fields = StructDescriptor<T>::fields;` и убедиться, что `fields.size() == metadata->getCount()`.
3. Обнулить буфер (`std::memset(buffer, 0, metadata->getMessageLength())`).
4. Для каждого `i`:
   - `FieldInfo fi = metadata->getField(i);`
   - `uint8_t* dataPtr = buffer + fi.offset;`
   - `auto* nullPtr = reinterpret_cast<int16_t*>(buffer + fi.nullOffset);`
   - `auto& member = value.*fields[i].memberPtr;`
   - Если `fields[i].nullable` и `member` — `std::optional` без значения → `*nullPtr = -1` и переход к следующему полю.
   - Иначе `*nullPtr = 0` и выбрать конверсию:
     - `fields[i].useAdapter == true` → `adapt_to_firebird_ctx`/`adapt_to_firebird` (аналогично TuplePacker).
     - `fields[i].useAdapter == false` → вызов `detail::write_sql_value(fields[i], member, dataPtr, transaction)`.
   - Для BLOB-полей `write_sql_value` вызывает `transaction->storeBlob` и записывает идентификатор.
5. На несоответствие типов/scale/длины — бросить `FirebirdException` с указанием структуры и поля.

## Алгоритм StructUnpacker

1. Создать `T result{};` и получить дескриптор.
2. Проверить счётчик полей в `metadata`.
3. Для каждого `i`:
   - Те же `fi`, `dataPtr`, `nullPtr`.
   - Если `*nullPtr == -1`:
     - Если поле nullable и тип — `std::optional`, присвоить `std::nullopt`.
     - Иначе — бросить исключение (несоответствие схеме).
   - Иначе считать значение:
     - При `useAdapter` — `adapt_from_firebird_ctx`/`adapt_from_firebird`.
     - Иначе — `detail::read_sql_value(fields[i], dataPtr, transaction)`.
   - Присвоить результат полю `result.*memberPtr`.
4. Вернуть `result`.

## Модуль преобразований `write_sql_value` и `read_sql_value`

- Располагается в `fbpp::core::detail::sql_value_codec`.
- Предоставляет перегруженные функции:
  ```cpp
  void write_sql_value(const FieldDescriptor& field,
                       const auto& cxxValue,
                       uint8_t* dataPtr,
                       Transaction* tra);

  auto read_sql_value(const FieldDescriptor& field,
                      const uint8_t* dataPtr,
                      Transaction* tra) -> decltype(auto);
  ```
- Внутри использует общие ветки для `SqlType` (скопированы и унифицированы с JsonPacker/JsonUnpacker):
  - целочисленные типы + обработка scale через `IUtil::formatInt64`/`string_to_decimal_i64`.
  - FLOAT/DOUBLE.
  - DATE/TIME/TIMESTAMP/TZ через `IUtil::encodeDate/decodeDate` и `encodeTime/decodeTime`.
  - BLOB: текстовые используют `transaction->storeBlob`/`loadBlob`, бинарные — прямое копирование `ISC_QUAD`.
  - BOOLEAN, CHAR/VARCHAR, GUID и т.д.
- Функции не привязаны к конкретному контейнеру и вызываются JSON/tuple/struct упаковщиками, исключая дублирование.

## Пример 1. Простая структура

```cpp
struct Employee {
    std::string name;
    int32_t age;
    std::optional<std::string> email;
};

Employee emp{"Alice", 29, std::nullopt};
auto stmt = conn.prepareStatement("INSERT INTO employees (name, age, email) VALUES (?, ?, ?)");
auto tra = conn.StartTransaction();
auto meta = stmt->getInputMetadata();
std::vector<uint8_t> buffer(meta->getMessageLength());

fbpp::core::pack(emp, buffer.data(), meta.get(), tra.get());
stmt->execute(tra.get(), meta->getRawMetadata(), buffer.data());
```

Для чтения:

```cpp
auto outMeta = stmt->getOutputMetadata();
Employee fetched = fbpp::core::unpack<Employee>(buffer.data(), outMeta.get(), tra.get());
```

Пользователь работает с `Employee` как с обычным C++-объектом, не касаясь бинарных буферов.

## Пример 2. Структура с адаптером

```cpp
struct Money {
    int64_t cents;
};

struct OrderSummary {
    int64_t id;
    Money total;
    std::optional<TimestampTz> closedAt;
};
```

Генератор также выпускает `TypeAdapter<Money>`:

```cpp
template<>
struct TypeAdapter<Money> {
    using firebird_type = int64_t;
    static constexpr bool is_specialized = true;

    static firebird_type to_firebird(const Money& m) { return m.cents; }
    static Money from_firebird(firebird_type cents) { return Money{cents}; }
};
```

`StructDescriptor<OrderSummary>` помечает поле `total` флагом `useAdapter = true`. Алгоритм упаковки/распаковки автоматически вызывает адаптер при работе с соответствующим столбцом.

## Расширяемость и сопровождение

- Добавление нового Firebird-типа = обновление `sql_value_codec`; все упаковщики сразу наследуют поддержку.
- Изменения схемы БД → повторная генерация структур и дескрипторов, ручных правок не требуется.
- Nullable-поля контролируются на этапе распаковки: несоответствие объявленной обязательности даёт исключение, защищая бизнес-логику.
- Подготовленные структуры остаются лёгковесными и пригодными для `std::optional`, сравнений, сериализации и т.п., потому что хранят чистые C++-типы.
