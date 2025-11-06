# Workflow структурного доступа к запросам

## Общая схема
- Клиент формирует SQL-строку и передаёт её драйверу `fbpp`.
- Драйвер выполняет `prepareStatement`, получает входные и выходные `IMessageMetadata`.
- По метаданным клиент (или генератор в составе клиента) создаёт структуры `InParam<Query>` и `OutParam<Query>` плюс сопутствующие дескрипторы.
- Во время выполнения запроса клиентский код:
  1. Подготавливает запрос по заранее выбранному `QueryId`.
  2. Собирает структуру входных параметров и передаёт её драйверу.
  3. Драйвер упаковывает структуру во входной буфер Firebird и выполняет запрос.
  4. Драйвер распаковывает выходной буфер в структуру и возвращает её клиенту либо заполняет контейнер.

## Ответственность драйвера

### Получение метаданных
```cpp
MessageMetadataPtr prepareStatement(Connection& conn,
                                    Transaction* tra,
                                    std::wstring_view sql);

MessageMetadataPtr getInputMetadata(const PreparedStatement& stmt);
MessageMetadataPtr getOutputMetadata(const PreparedStatement& stmt);
```
*Схематически*: `prepareStatement` и `get*Metadata` оборачивают вызовы Firebird API и предоставляют `MessageMetadata` с полями `FieldInfo`.

### Упаковка структуры во входной буфер
```cpp
template<typename T>
void pack(const T& value,
          uint8_t* buffer,
          const MessageMetadata* metadata,
          Transaction* transaction);
```
*Схема реализации*:
1. Получить `StructDescriptor<T>` (генерируется на клиенте).
2. Согласовать количество полей с `metadata`.
3. Для каждого поля вызвать `sql_value_codec::write_sql_value(SqlWriteContext{field, transaction, nullPtr}, member, dataPtr)`.

### Распаковка буфера в структуру
```cpp
template<typename T>
T unpack(const uint8_t* buffer,
         const MessageMetadata* metadata,
         Transaction* transaction);
```
*Схема реализации*:
1. Создать `T result{}` и получить `StructDescriptor<T>`.
2. Для каждого поля вызвать `sql_value_codec::read_sql_value(SqlReadContext{field, transaction, nullPtr}, dataPtr, result.*memberPtr)`.

### Выполнение запроса
```cpp
template<typename QueryDescriptor>
auto execute(Connection& conn,
             Transaction& tra,
             const typename QueryDescriptor::Input& params)
    -> std::vector<typename QueryDescriptor::Output>;

template<typename QueryDescriptor>
std::optional<typename QueryDescriptor::Output>
fetchOne(Connection& conn,
         Transaction& tra,
         const typename QueryDescriptor::Input& params);

template<typename QueryDescriptor>
void executeNonQuery(Connection& conn,
                     Transaction& tra,
                     const typename QueryDescriptor::Input& params);
```
*Схема реализации*:
1. `prepareStatement(conn, &tra, QueryDescriptor::sql)`.
2. Упаковка `QueryDescriptor::Input` → буфер входных параметров.
3. Вызов Firebird API `execute`, `executeQuery`, `executeBatch`.
4. Для строк результата — распаковка в `QueryDescriptor::Output` и заполнение контейнера.

### Batch операции
```cpp
template<typename QueryDescriptor>
void executeBatch(Connection& conn,
                  Transaction& tra,
                  std::span<const typename QueryDescriptor::Input> batch);
```
*Схема*: итерация по входным структурам, упаковка каждой в сообщение и отправка в Firebird batch API.

## Контракт на стороне клиента

### Идентификация запросов
```cpp
enum class QueryId {
    None,
    catalogs_d,
    catalogs_i,
    catalogs_one_s,
    // ...
};

QueryId stringToQueryId(std::wstring_view name);
```

### Дескриптор запроса
```cpp
template<QueryId Q>
struct QueryDescriptor;

template<>
struct QueryDescriptor<QueryId::catalogs_i> {
    static constexpr std::wstring_view sql = LR"(
        INSERT INTO dbo.Catalogs (ParentID, CatalogType, CatalogName, Ord)
        VALUES (:ParentID, :CatalogType, :CatalogName,
                COALESCE(NULLIF(CAST(:Ord AS DECIMAL(19,10)), 0),
                         (SELECT ISNULL(MAX(c.Ord), 0) + 1
                          FROM dbo.Catalogs c WITH (UPDLOCK, HOLDLOCK)
                          WHERE c.ParentID = :ParentID)))
    )";

    struct Input {
        int parentId;
        int catalogType;
        std::wstring catalogName;
        double ord;
    };

    struct Output {
        // для INSERT без RETURNING структура может быть пустой
    };

    static constexpr auto descriptor = StructDescriptor<Input>{/* генерируется */};
    static constexpr auto outputDescriptor = StructDescriptor<Output>{/* генерируется */};
};
```
Генератор наполняет:
- `sql` — исходный текст запроса;
- `Input` и `Output` — структуры с полями, соответствующими метаданным;
- `StructDescriptor` — массив `FieldDescriptor`, содержащий имя столбца, тип, scale, длину, признак nullable и указатель на член структуры.

### Реестр запросов
```cpp
struct QueryRegistryEntry {
    QueryId id;
    std::wstring_view name;
    std::wstring_view sql;
};

inline constexpr QueryRegistryEntry queryRegistry[] = {
    {QueryId::catalogs_d, L"catalogs_d", QueryDescriptor<QueryId::catalogs_d>::sql},
    {QueryId::catalogs_i, L"catalogs_i", QueryDescriptor<QueryId::catalogs_i>::sql},
    {QueryId::catalogs_one_s, L"catalogs_one_s", QueryDescriptor<QueryId::catalogs_one_s>::sql},
    // ...
};
```

## Пример рабочего цикла

1. **Генерация** (запускается инструментом клиента):
   - Передаёт SQL `catalogs_i` в драйвер.
   - Получает `MessageMetadata` по входу и выходу.
   - Выписывает `QueryDescriptor<QueryId::catalogs_i>` и соответствующие `StructDescriptor` в заголовочный файл.

2. **Использование**:
   ```cpp
   using CatalogInsert = QueryDescriptor<QueryId::catalogs_i>;

   CatalogInsert::Input params{
       .parentId = 10,
       .catalogType = 3,
       .catalogName = L"New folder",
       .ord = 0.0,
   };

   auto inserted = fbpp::core::execute<CatalogInsert>(conn, tra, params);
   ```
   - `execute` вызывает `pack(params, buffer, inputMeta, &tra)`.
   - Выполняет SQL `CatalogInsert::sql`.
   - Поскольку запрос не возвращает строк, `execute` возвращает пустой `std::vector<CatalogInsert::Output>`.

3. **Чтение одного объекта**:
   ```cpp
   using CatalogSelect = QueryDescriptor<QueryId::catalogs_one_s>;
   CatalogSelect::Input filter{.catalogId = 10};

   if (auto row = fbpp::core::fetchOne<CatalogSelect>(conn, tra, filter)) {
       // row->parentId, row->catalogType, ...
   }
   ```
   - `fetchOne` упаковывает `filter`, выполняет `SELECT`, вызывает `unpack` для единственной записи и возвращает `std::optional<CatalogSelect::Output>`.

4. **Batch**:
   ```cpp
   using CatalogInsert = QueryDescriptor<QueryId::catalogs_i>;
   std::array<CatalogInsert::Input, 2> batch{...};
   fbpp::core::executeBatch<CatalogInsert>(conn, tra, batch);
   ```
   - Каждый элемент массива упаковывается в сообщение; драйвер выполняет batch-вставку.

Этот workflow обеспечивает совместное существование SQL, структур входных/выходных параметров и их дескрипторов, делегируя драйверу получение метаданных, преобразование значений и выполнение запросов всех типов (INSERT, UPDATE, SELECT, DELETE, batch). Клиент описывает только каталог запросов и их структуры, остальная работа автоматизирована.

## Текущий статус
- Реализован общий модуль `sql_value_codec` и интегрирован в tuple packer/unpacker, обеспечивая единый механизм преобразования Firebird ↔ C++.
- Добавлен метод `Connection::describeQuery`, возвращающий метаданные входных и выходных параметров для произвольного SQL.
- Написан интеграционный тест `test_query_metadata`, проверяющий запросы `SELECT` и `INSERT ... RETURNING`.
- Подготовлен документ `struct_workflow.md` с описанием ожидаемого контракта для генератора и клиентского кода.

## Следующие шаги
1. **StructDescriptor/StructPacker**: реализовать генерацию и поддержку `StructDescriptor<T>` для C++ структур, а также шаблонные функции `pack/unpack` поверх `sql_value_codec`.
2. **QueryDescriptor API**: создать шаблон `QueryDescriptor<QueryId>` с `Input`, `Output`, `StructDescriptor` и статической SQL-строкой.
3. **Вспомогательные функции выполнения**: реализовать `execute<QueryDescriptor>()`, `fetchOne<QueryDescriptor>()`, `executeNonQuery<QueryDescriptor>()` и `executeBatch<QueryDescriptor>()`, использующие `StructPacker/Unpacker`.
4. **Генератор договоров**: разработать утилиту (CLI) на клиентской стороне, которая использует `Connection::describeQuery` для построения `QueryDescriptor` и связанных структур, удостоверяясь, что SQL и типы синхронизированы.
