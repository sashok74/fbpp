# FBPP: Краткое руководство для LLM по работе с Firebird

## Суть

**fbpp** — C++20 обёртка для Firebird 5 с генерацией типобезопасного кода из SQL-запросов.

---

## Генерация кода (query_generator)

**Вход:** JSON с именованными SQL-запросами
```json
{
  "GetUserById": "SELECT id, name, salary FROM users WHERE id = :user_id",
  "InsertUser": "INSERT INTO users (name, salary) VALUES (:name, :salary)"
}
```

**Выход:** Два заголовочных файла с C++ структурами

```cpp
// queries.hpp - основной файл
enum class QueryId { GetUserById, InsertUser };

template<>
struct QueryDescriptor<QueryId::GetUserById> {
    static constexpr std::string_view sql = "SELECT id, name, salary FROM users WHERE id = :user_id";
    static constexpr std::string_view positionalSql = "SELECT id, name, salary FROM users WHERE id = ?";

    struct Input {
        int32_t user_id;
    };

    struct Output {
        int32_t id;
        std::string name;
        std::optional<double> salary;  // nullable поля -> std::optional
    };
};
```

**Команда генерации:**
```bash
query_generator --dsn firebird5:testdb --user SYSDBA --password planomer \
    --input queries.json --output queries.hpp --support queries.structs.hpp \
    --use-ttmath-numeric --use-chrono
```

---

## Маппинг типов SQL -> C++

| SQL тип | C++ тип | Примечание |
|---------|---------|------------|
| INTEGER | `int32_t` | |
| BIGINT | `int64_t` | |
| INT128 | `Int128` / `ttmath::Int<4>` | 16 байт |
| NUMERIC(38,x) | `Int128` + scale | С адаптером TTMath |
| DECFLOAT(16/34) | `DecFloat16/34` | IEEE 754-2008 |
| VARCHAR(n) | `std::string` | |
| DATE/TIME/TIMESTAMP | `Date/Time/Timestamp` или `std::chrono::*` | |
| TIME WITH TIME ZONE | `TimeTz` / `TimeWithTz` | raw Firebird wrapper или C++20 alias |
| TIMESTAMP WITH TIME ZONE | `TimestampTz` | 12 байт |
| BLOB (text) | `std::string` / `TextBlob` | |
| NULL-able поля | `std::optional<T>` | В Output структурах |

---

## Паттерн использования

```cpp
// 1. Подключение
auto conn = std::make_unique<Connection>(params);
auto txn = conn->StartTransaction();

// 2. Подготовка (с кешированием)
auto stmt = conn->prepareStatement(QueryDescriptor<QueryId::GetUserById>::sql);

// 3. Выполнение SELECT -> Cursor -> fetch
auto cursor = txn->openCursor(stmt, GetUserByIdInput{123});
GetUserByIdOutput row;
while (cursor->fetch(row)) {
    // row.id, row.name, row.salary.value_or(0)
}

// 4. Выполнение DML
unsigned affected = txn->execute(stmt, InsertUserInput{"Alice", 50000.0});

// 5. Batch операции
auto batch = txn->createBatch(stmt, true, false);
batch->appendMessage(InsertUserInput{"Bob", 60000.0});
batch->appendMessage(InsertUserInput{"Charlie", 70000.0});
batch->execute();

txn->Commit();
```

---

## Три формата данных

| Формат | Input | Output | Когда использовать |
|--------|-------|--------|-------------------|
| **Struct** | `MyInput{...}` | `MyOutput row` | Генерируемый код, типобезопасность |
| **Tuple** | `std::make_tuple(1, "x")` | `std::tuple<int, string>` | Простые запросы, ad-hoc |
| **JSON** | `{"id": 1, "name": "x"}` | `nlohmann::json` | REST API, динамические запросы |

---

## Ключевые компоненты API

| Класс | Назначение |
|-------|------------|
| `Connection` | Подключение + кеш statements |
| `Transaction` | ACID-граница, execute/openCursor |
| `Statement` | Подготовленный запрос + метаданные |
| `ResultSet` | Курсор + fetch(tuple/json/struct) |
| `QueryDescriptor<T>` | SQL + Input/Output типы (генерируется) |
| `StructDescriptor<T>` | Маппинг полей структуры (генерируется) |

---

## Для LLM: как генерировать новые запросы

1. **Добавить SQL в queries.json** с именованными параметрами (`:param_name`)
2. **Запустить query_generator** — получить структуры Input/Output
3. **Использовать** `executeQuery<QueryId::NewQuery>(conn, txn, input)` или `openCursor`

Генератор автоматически определяет типы из метаданных БД, включая nullable и extended types.
