# fbpp Integration Cheat-Sheet for LLM Prompts

Этот документ прикладывается к исходникам драйвера и передаётся LLM вместе с конкретной задачей. Он описывает ключевые факты и шаблоны, которыми модель должна руководствоваться при включении `fbpp` в новое С++‑приложение (консольное или с UI/C++Builder).  

## 1. Структура репозитория и ключевые файлы

| Компонент | Назначение | Файлы/каталоги |
|-----------|------------|----------------|
| Ядро драйвера | Реализация подключения, транзакций, типов, упаковщиков. | `src/core/firebird/*.cpp`, публичные заголовки `include/fbpp/**` |
| Генератор запросов | CLI, формирующий типобезопасные структуры. | `src/tools/query_generator.cpp`, бинари `build/query_generator` |
| Генерируемые хедеры | Пользовательские `QueryDescriptor` и `StructDescriptor`. | пример: `build/generated/table_test_1/queries.generated.hpp`, `queries.structs.generated.hpp` |
| Интеграционный тест | CRUD-пример с генератором. | `tests/unit/test_query_generator_integration.cpp` |
| Пример чтения таблицы | `examples/08_struct_fetch.cpp` – демонстрирует StructDescriptor, pack/unpack и вывод всех типов. |

## 2. Сборка и зависимости

1. **Conan + CMake** – стандартный сценарий:
   ```bash
   ./build.sh RelWithDebInfo        # либо вручную conan install + cmake configure
   cmake --build build
   ctest --test-dir build
   ```
2. Требования:
   - C++20 (`set(CMAKE_CXX_STANDARD 20)` в `CMakeLists.txt`).
   - Firebird клиентские заголовки/библиотеки (`/opt/firebird/include`, `libfbclient.so`).
   - `spdlog`, `nlohmann_json`, GoogleTest (для тестов).
3. Исполняемые примеры выключены по умолчанию (`BUILD_EXAMPLES=OFF`); включать через `-DBUILD_EXAMPLES=ON`.

## 3. Включение в сторонний проект (общие шаги для LLM)

1. **Подтянуть драйвер**:
   - Вариант *submodule*: `add_subdirectory(fbpp)` и `target_link_libraries(<app> PRIVATE fbpp)`.
   - Вариант *установленной библиотеки*: использовать `find_package(fbpp CONFIG REQUIRED)` (после `cmake --install` оригинального проекта).
2. **Настроить Firebird**:
   - Указать `ConnectionParams` (`hostname:path`, `SYSDBA`, пароль, `UTF8`).
   - Подготовить `config/test_config.json` либо напрямую инициализировать `ConnectionParams`.
3. **Генерация обёрток**:
   - Подготовить JSON `queries.json`:
     ```json
     {
       "SelectTop": "SELECT ID, NAME FROM MY_TABLE WHERE ID = :ID",
       "InsertRow": "INSERT INTO MY_TABLE (NAME) VALUES (:NAME)"
     }
     ```
   - Запустить:
     ```bash
     build/query_generator \
       --dsn firebird5:testdb \
       --user SYSDBA --password planomer --charset UTF8 \
       --input path/to/queries.json \
       --output generated/queries.generated.hpp \
       --support generated/queries.structs.generated.hpp
     ```
   - Включить авто‑генерацию через CMake (см. `tests/CMakeLists.txt`: цель `generate_table_test_1_queries`).
4. **Подключить заголовки**:
   ```cpp
   #include "generated/queries.generated.hpp"
   using namespace generated::queries;

   Connection conn(params);
   auto tra = conn.StartTransaction();
   auto rows = fbpp::core::executeQuery<QueryDescriptor<QueryId::SelectTop>>(conn, *tra, SelectTopIn{.param1 = 42});
   ```
   `queries.generated.hpp` уже `#include`-ит `queries.structs.generated.hpp`.
5. **Организовать двухуровневую поставку**:
   - `libfbpp` – базовый драйвер.
   - `libmy_queries` – статическая/динамическая библиотека, которая лишь `target_link_libraries(... fbpp)` и компилируется из сгенерированных заголовков (добавить обёртки/фасады при необходимости). При изменении SQL перекомпилируется только эта часть.

## 4. Использование генератора и QueryDescriptor

### Формат `QueryDescriptor`

```cpp
template<>
struct QueryDescriptor<QueryId::TABLE_TEST_1_U> {
    static constexpr QueryId id = QueryId::TABLE_TEST_1_U;
    static constexpr std::string_view sql =
        "UPDATE TABLE_TEST_1 SET F_VARCHAR = :F_VARCHAR WHERE ID = :ID";
    static constexpr std::string_view positionalSql =
        "UPDATE TABLE_TEST_1 SET F_VARCHAR = ? WHERE ID = ?";
    static constexpr bool hasNamedParameters = true;
    using Input = TABLE_TEST_1_UIn;
    using Output = TABLE_TEST_1_UOut;
};
```

* `sql` — исходный текст с именованными параметрами (для логирования, отладки, авто‑документации).
* `positionalSql` — версия с `?`, пригодна для движков без поддержки `:name`.
* `StructDescriptor<T>` хранится в support-файле и синхронизирован через `makeField<&Struct::member>("SQL_NAME", SQL_TYPE, scale, length, nullable, subtype)`.

### Пример CRUD (см. `tests/unit/test_query_generator_integration.cpp`)

```cpp
auto tra = conn.StartTransaction();
TABLE_TEST_1_IIn args{};
args.param1 = 42;
args.param2 = "Name";
args.param3 = true;

auto stmt = conn.prepareStatement(std::string(QueryDescriptor<QueryId::TABLE_TEST_1_I>::sql));
unsigned inserted = tra->execute(stmt, args);

TABLE_TEST_1_SIn selectArgs{};
selectArgs.param1 = 42;
selectArgs.param2 = "Name";
auto rows = executeQuery<QueryDescriptor<QueryId::TABLE_TEST_1_S>>(conn, *tra, selectArgs);
```

## 5. UI (C++Builder) vs. консоль

- **Консоль/сервисы**: используем синхронные вызовы (`executeQuery`, `executeNonQuery`). Регулируем логирование через `fbpp_util/logging.h`.
- **C++Builder/VCL/FireMonkey**:
  1. Добавить статическую библиотеку `fbpp.lib` в проект (Project → Add → Existing).
  2. Настроить пути к заголовкам: `include\fbpp`, `generated\...`.
  3. При работе в UI‑потоке оборачивать длительные операции в `TTask::Run` или `std::jthread`, чтобы не блокировать интерфейс.
  4. Использовать RAII‑классы (Connection/Transaction) в рамках отдельного класса‑сервиса и дергать его из формы.
  5. Для логов – направить вывод в `OutputDebugString` или файл (через `util::Logging::init("info", true, true, "logs/fbpp_ui.log")`).

## 6. Типовая инструкция для LLM (вкладывать в промпт)

```
1. Построй fbpp (см. CMakeLists.txt) и добавь как зависимость в текущий проект.
2. Создай JSON с запросами (пример: doc/fbpp_integration_prompt.md, раздел 3) и запусти build/query_generator.
3. Подключи сгенерированные заголовки (queries.generated.hpp) и добавь StructDescriptor support-файл в include path.
4. Обнови CMake (см. tests/CMakeLists.txt: цель generate_table_test_1_queries) – генерация должна выполняться перед сборкой приложения.
5. Покажи пример кода:
   - Создание Connection/Transaction.
   - Вызов executeQuery/executeNonQuery для QueryDescriptor (с аргументами Input/Output).
   - Для UI (C++Builder) – вызов в отдельном потоке или TTask, затем обновление визуальных компонентов.
6. Проверь, что новая библиотека линковается с fbpp (static или shared), и поясни, как перелинковать только обёртки при изменении SQL.
```

## 7. Отладка и тесты

- Прогон полной матрицы: `ctest --test-dir build --output-on-failure`.
- Интеграционный CRUD‑тест (`QueryGeneratorIntegrationTest.TableTest1CrudWorkflow`) иллюстрирует end-to-end сценарий.
- Пример форматирования всех типов: `examples/08_struct_fetch.cpp` (команды `cmake --build build --target 08_struct_fetch` и запуск `./build/examples/08_struct_fetch`).

Используй этот документ как «протокол» действий для LLM: при каждой новой интеграции достаточно включить его в промпт вместе с исходным кодом, чтобы модель понимала, что и как делать. ***!
