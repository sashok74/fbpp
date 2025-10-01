# Техническое задание: Реализация кэша подготовленных запросов в FBPP

## 1. Обзор системы

Кэш подготовленных запросов позволит переиспользовать уже подготовленные в Firebird запросы, что существенно снизит накладные расходы на повторную подготовку идентичных SQL-запросов.

### 1.1. Текущая архитектура

- **Connection::prepareStatement()** - создает новый подготовленный запрос каждый раз
  - См. `src/core/firebird/fb_connection.cpp:420-436`
- **Statement** - обертка над `Firebird::IStatement*`
  - См. `include/fbpp/core/statement.hpp`
- **Statement::free()** - освобождает ресурсы подготовленного запроса
  - См. `include/fbpp/core/statement.hpp:149`

### 1.2. Конфигурация

Конфигурация хранится в классе `util::Config` (`include/fbpp_util/config.h`)
- Поддержка JSON файлов и переменных окружения
- Структуры: `DbConfig`, `LoggingConfig`, `TestsConfig`

## 2. Требования к реализации

### 2.1. Основные требования

1. **LRU кэш** без дополнительных наворотов
2. **Размер кэша** берется из конфигурации
3. **Хранение метаданных** о входных и выходных параметрах
4. **Именованные параметры** для входных значений
5. **Поддержка множественных параметров** с одним именем
6. **NULL как значение по умолчанию** для неустановленных параметров
7. **Минимализм** и сохранение работоспособности текущего кода
8. **Использование Statement::free()** для освобождения ресурсов

### 2.2. Архитектурные ограничения

- Минимально изменять существующие публичные интерфейсы
- Не нарушать текущую схему работы с параметрами
- Сохранить совместимость с текущими тестами

## 3. Детальная спецификация

### 3.1. Структура кэша

#### 3.1.1. Класс `StatementCache`

Местоположение: `include/fbpp/core/statement_cache.hpp`

```cpp
namespace fbpp {
namespace core {

class StatementCache {
public:
    struct CacheConfig {
        size_t maxSize = 100;  // Максимальный размер кэша
        bool enabled = true;   // Включен/выключен кэш
    };

    struct CachedStatement {
        std::unique_ptr<Statement> statement;
        std::string sql;
        unsigned flags;
        std::chrono::steady_clock::time_point lastUsed;
        size_t useCount = 0;

        // Метаданные о параметрах
        struct ParamInfo {
            std::string name;      // Имя параметра (может быть пустым)
            unsigned sqlType;      // SQL тип
            unsigned length;       // Размер данных
            short scale;          // Scale для NUMERIC/DECIMAL
            bool nullable;        // Может ли быть NULL
        };

        std::vector<ParamInfo> inputParams;
        std::vector<ParamInfo> outputParams;
    };

private:
    // LRU реализация через ordered map
    std::unordered_map<std::string, std::unique_ptr<CachedStatement>> cache_;
    std::list<std::string> lruList_;  // Для отслеживания порядка использования
    CacheConfig config_;
    mutable std::mutex mutex_;  // Thread safety
};

} // namespace core
} // namespace fbpp
```

### 3.2. Интеграция с Connection

#### 3.2.1. Модификация класса Connection

Добавить в `include/fbpp/core/connection.hpp`:

```cpp
class Connection {
    // ... существующий код ...

private:
    std::unique_ptr<StatementCache> statementCache_;

public:
    // Новый метод для получения кэшированного statement
    std::shared_ptr<Statement> getCachedStatement(const std::string& sql, unsigned flags = 0);

    // Метод для явной очистки кэша
    void clearStatementCache();

    // Получение статистики кэша
    StatementCache::Statistics getCacheStatistics() const;
};
```

### 3.3. Система именованных параметров

#### 3.3.1. Класс `NamedParameters`

Местоположение: `include/fbpp/core/named_parameters.hpp`

```cpp
namespace fbpp {
namespace core {

class NamedParameters {
public:
    // Установка значения параметра по имени
    template<typename T>
    void set(const std::string& name, const T& value);

    // Установка NULL для параметра
    void setNull(const std::string& name);

    // Очистка всех параметров
    void clear();

    // Проверка, установлен ли параметр
    bool has(const std::string& name) const;

    // Применение параметров к Statement
    void applyTo(Statement* stmt, Transaction* tra) const;

private:
    // Хранилище значений с поддержкой множественных параметров
    std::unordered_map<std::string, std::vector<std::variant<
        std::monostate,  // NULL
        int16_t, int32_t, int64_t,
        float, double,
        std::string,
        std::vector<uint8_t>,  // BLOB
        // ... другие типы
    >>> params_;
};

} // namespace core
} // namespace fbpp
```

### 3.4. Обновление конфигурации

#### 3.4.1. Расширение Config

В `include/fbpp_util/config.h`:

```cpp
struct CacheConfig {
    bool enabled = true;
    size_t maxStatements = 100;  // Максимальное количество кэшированных запросов
    size_t ttlMinutes = 60;      // Time-to-live для неиспользуемых запросов
};

class Config {
    // ... существующий код ...
    static CacheConfig& cache() { return instance().cache_; }
private:
    CacheConfig cache_;
};
```

### 3.5. Пример использования

```cpp
// Инициализация connection с кэшем
auto conn = std::make_unique<Connection>(params);

// Использование именованных параметров
NamedParameters params;
params.set("user_id", 123);
params.set("status", "active");
params.setNull("deleted_at");

// Получение кэшированного statement
auto stmt = conn->getCachedStatement(
    "SELECT * FROM users WHERE id = ? AND status = ? AND deleted_at IS ?"
);

// Выполнение с именованными параметрами
auto tra = conn->StartTransaction();
params.applyTo(stmt.get(), tra.get());
auto result = stmt->openCursor(tra);
```

## 4. Поэтапная реализация

### Этап 1: Базовая инфраструктура кэша
**Файлы для создания/изменения:**
- [x] `include/fbpp/core/statement_cache.hpp` - интерфейс кэша
- [x] `src/core/firebird/fb_statement_cache.cpp` - реализация LRU кэша
- [x] `include/fbpp_util/config.h` - добавление CacheConfig
- [x] `src/util/config.cpp` - загрузка настроек кэша

**Тесты:**
- [x] `tests/unit/test_statement_cache.cpp` - unit-тесты LRU логики

### Этап 2: Интеграция с Connection
**Файлы для изменения:**
- [x] `include/fbpp/core/connection.hpp` - добавление методов кэширования
- [x] `src/core/firebird/fb_connection.cpp` - реализация getCachedStatement()

**Тесты:**
- [x] `tests/unit/test_cached_statements.cpp` - интеграционные тесты

### Этап 2.5: Проверка кэша и рефакторинг API
**Цель:** Проверить работу кэша в реальных условиях и рассмотреть унификацию API для прозрачного кэширования.

**Файлы для создания/изменения:**
- [ ] `examples/05_cached_operations.cpp` - пример использования кэша (на основе `examples/01_basic_operations.cpp`)
- [ ] `include/fbpp/core/connection.hpp` - рефакторинг методов prepare (опционально)
- [ ] `src/core/firebird/fb_connection.cpp` - реализация прозрачного кэширования

**Задачи:**
1. Создать пример `05_cached_operations.cpp`:
   - Взять за основу `examples/01_basic_operations.cpp` (строки 1-1114)
   - Заменить все вызовы `prepareStatement()` на `getCachedStatement()`
   - Добавить вывод статистики кэша после каждой секции
   - Продемонстрировать эффективность кэша на повторяющихся запросах
   - Показать работу с разными флагами подготовки

2. Рассмотреть варианты унификации API:
   - **Вариант A**: Переименовать `getCachedStatement()` в `prepareStatement()`, старый убрать
     - Плюсы: полная прозрачность, автоматическое кэширование для всех
     - Минусы: изменение типа возврата с `unique_ptr` на `shared_ptr` может сломать код
   - **Вариант B**: Добавить параметр `bool useCache = true` в `prepareStatement()`
     - Плюсы: обратная совместимость, возможность отключить кэш
     - Минусы: нужно менять тип возврата или делать обертку
   - **Вариант C**: Сделать `prepareStatement()` прозрачной обертку над кэшем
     - При `cache.enabled = true` внутри использовать кэш
     - При `cache.enabled = false` работать как раньше
     - Возвращать `unique_ptr` с кастомным deleter для shared ownership
   - **Вариант D**: Оставить оба метода для разных сценариев
     - `prepareStatement()` - без кэша, для одноразовых запросов
     - `getCachedStatement()` - с кэшем, для часто используемых запросов

3. Добавить метрики производительности:
   - Измерить время подготовки с кэшем и без
   - Показать экономию ресурсов на реальных запросах
   - Вывести финальную статистику с hit rate

**Тесты:**
- [ ] Запустить `05_cached_operations` и убедиться в корректной работе
- [ ] Сравнить производительность с `01_basic_operations`
- [ ] Проверить, что все существующие примеры продолжают работать

### Этап 3: Именованные параметры ✅
**Файлы для создания/изменения:**
- [x] `include/fbpp/core/named_param_parser.hpp` - парсер именованных параметров
- [x] `include/fbpp/core/named_param_helper.hpp` - helper для конвертации JSON
- [x] `src/core/firebird/fb_named_param_parser.cpp` - реализация парсера
- [x] `include/fbpp/core/statement.hpp` - поддержка именованных параметров
- [x] `src/core/firebird/fb_statement_cache.cpp` - интеграция с кэшем

**Тесты:**
- [x] `tests/unit/test_named_parameters.cpp` - тесты парсера и интеграции
- [x] `examples/06_named_parameters.cpp` - пример использования
- [x] `doc/NAMED_PARAMETERS.md` - документация

### Этап 4: Метаданные и алиасы
**Файлы для изменения:**
- [ ] `include/fbpp/core/message_metadata.hpp` - расширение для хранения имен
- [ ] `src/core/firebird/fb_statement_cache.cpp` - заполнение метаданных

**Тесты:**
- [ ] `tests/unit/test_metadata_cache.cpp` - тесты метаданных

### Этап 5: Оптимизация и мониторинг
**Файлы для создания:**
- [ ] `include/fbpp/core/cache_statistics.hpp` - статистика кэша
- [ ] Документация и примеры использования

**Тесты:**
- [ ] Нагрузочные тесты производительности

## 5. Контрольный чек-лист

### 5.1. Этап 1 - Базовая инфраструктура
- [x] Создан класс StatementCache с LRU алгоритмом
- [x] Добавлена конфигурация кэша в Config
- [x] Реализовано чтение настроек из JSON и env
- [x] Unit-тесты для LRU логики проходят
- [x] Кэш корректно освобождает statement через free()

### 5.2. Этап 2 - Интеграция
- [x] Connection имеет экземпляр StatementCache
- [x] getCachedStatement() возвращает существующие запросы
- [x] Новые запросы добавляются в кэш
- [x] Старые запросы вытесняются при переполнении
- [x] Интеграционные тесты проходят

### 5.2.5. Этап 2.5 - Проверка кэша и рефакторинг API
- [ ] Добавлен вывод статистики кэша
- [ ] Измерена производительность с кэшем и без
- [ ] Выбран и реализован вариант унификации API
- [ ] Все существующие примеры продолжают работать

### 5.3. Этап 3 - Именованные параметры ✅
- [x] Парсер распознает :param и @param синтаксис
- [x] Поддержка множественных параметров с одним именем
- [x] NULL как значение по умолчанию
- [x] Регистронезависимые имена параметров
- [x] Интеграция с JSON пакером
- [x] Прозрачная работа с кэшем
- [x] Unit-тесты параметров проходят

### 5.4. Этап 4 - Метаданные
- [ ] Кэш хранит информацию о входных параметрах
- [ ] Кэш хранит информацию о выходных параметрах
- [ ] Поддержка алиасов для выходных параметров
- [ ] Тесты метаданных проходят

### 5.5. Этап 5 - Финализация
- [ ] Статистика кэша доступна
- [ ] Документация обновлена
- [ ] Примеры использования добавлены
- [ ] Все существующие тесты проходят
- [ ] Производительность улучшена

## 6. Важные замечания для реализации

### 6.1. Thread Safety
Кэш должен быть потокобезопасным, используя `std::mutex` для защиты критических секций.

### 6.2. Управление памятью
При вытеснении из кэша необходимо корректно вызывать `Statement::free()` для освобождения ресурсов Firebird.

### 6.3. Ключ кэширования
Ключ кэша должен включать не только SQL, но и флаги подготовки, так как один запрос может быть подготовлен с разными флагами.

### 6.4. Обработка ошибок
При ошибке получения statement из кэша должен автоматически создаваться новый.

### 6.5. Совместимость
Весь существующий код должен продолжать работать без изменений. Кэширование должно быть прозрачным для пользователя.

## 7. Тестовые сценарии

### 7.1. Базовые тесты
1. Кэширование простого SELECT запроса
2. Вытеснение старых запросов при переполнении
3. Повторное использование с разными параметрами
4. Работа с отключенным кэшем

### 7.2. Граничные случаи
1. Кэш размером 1
2. Одновременный доступ из нескольких потоков
3. Очень длинные SQL запросы
4. Запросы с большим количеством параметров

### 7.3. Производительность
1. Сравнение времени выполнения с/без кэша
2. Измерение overhead на поддержку кэша
3. Проверка утечек памяти

## 8. Детальные инструкции для этапа 2.5

### 8.1. Создание примера с кэшированием

**Исходный файл для копирования**: `examples/01_basic_operations.cpp` (1114 строк)

**Ключевые места с prepareStatement для замены**:
- Строка 142-147: Запрос информации о сервере
- Строка 167-172: Проверка наличия таблицы
- Строка 195-199: Анализ структуры таблицы
- Строка 223-227: INSERT с RETURNING
- Строка 274-281: Вставка расширенных типов
- Строка 306-309: SELECT для проверки расширенных типов
- Строка 372-379: Вставка дат/времени
- Строка 406-409: SELECT дат/времени
- Строка 535-540: Вставка с BLOB
- Строка 566-569: SELECT с BLOB
- Строка 736-741: Вставка бинарного BLOB
- Строка 764-767: SELECT бинарного BLOB
- Строка 861-867: SELECT для чтения данных
- Строка 920-924: UPDATE запрос
- Строка 937-940: SELECT после UPDATE
- Строка 975-979: Batch INSERT
- Строка 1022-1025: INSERT для демонстрации ошибок
- Строка 1028-1030: DELETE для очистки

**Добавить после каждой секции (примерно после строк 161, 213, 336, 497, 607, 843, 967)**:
```cpp
// Вывод статистики кэша
auto stats = connection->getCacheStatistics();
std::cout << "\n📊 Статистика кэша:\n";
std::cout << "  Размер кэша: " << stats.cacheSize << "\n";
std::cout << "  Попадания: " << stats.hitCount << "\n";
std::cout << "  Промахи: " << stats.missCount << "\n";
std::cout << "  Hit Rate: " << std::fixed << std::setprecision(1) << stats.hitRate << "%\n";
std::cout << "  Вытеснения: " << stats.evictionCount << "\n\n";
```

**Добавить измерение времени для ключевых запросов**:
```cpp
#include <chrono>

// При первом вызове (cache miss)
auto start = std::chrono::high_resolution_clock::now();
auto stmt = connection->getCachedStatement(sql);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
std::cout << "  ⏱️ Время подготовки (miss): " << duration.count() << " мкс\n";

// При повторном вызове того же SQL (cache hit)
start = std::chrono::high_resolution_clock::now();
stmt = connection->getCachedStatement(sql);  // Тот же SQL
end = std::chrono::high_resolution_clock::now();
duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
std::cout << "  ⏱️ Время подготовки (hit): " << duration.count() << " мкс\n";
```

### 8.2. Рекомендации по выбору варианта унификации API

**Текущая ситуация**:
- `prepareStatement()` возвращает `unique_ptr<Statement>` (см. `connection.hpp:61-66`)
- `getCachedStatement()` возвращает `shared_ptr<Statement>` (см. `connection.hpp:68-69`)
- Transaction методы используют `const unique_ptr<Statement>&` (см. `transaction.hpp:60-70`)

**Рекомендуемое решение: Вариант C** - прозрачная обертка с сохранением совместимости

Модифицировать `prepareStatement()` в `fb_connection.cpp:420-458`:
```cpp
std::unique_ptr<Statement> Connection::prepareStatement(const std::string& sql, unsigned flags) {
    // Если кэш включен в конфигурации, используем его прозрачно
    if (util::Config::cache().enabled) {
        // Lazy init кэша если нужно
        if (!statementCache_) {
            StatementCache::CacheConfig cacheConfig;
            cacheConfig.enabled = util::Config::cache().enabled;
            cacheConfig.maxSize = util::Config::cache().maxStatements;
            cacheConfig.ttlMinutes = util::Config::cache().ttlMinutes;
            statementCache_ = std::make_unique<StatementCache>(cacheConfig);
        }

        // Получаем shared_ptr из кэша
        auto cached = statementCache_->get(this, sql, flags);

        // Возвращаем unique_ptr с кастомным deleter, который держит shared_ptr
        return std::unique_ptr<Statement>(
            cached.get(),
            [cached](Statement*) mutable { /* держим shared_ptr живым */ }
        );
    }

    // Старая логика без кэша для обратной совместимости
    if (!attachment_) {
        throw FirebirdException("Not connected to database");
    }
    auto& st = status();
    Firebird::IStatement* stmt = attachment_->prepare(
        &st, nullptr, 0, sql.c_str(), 3, flags);
    if (!stmt) {
        throw FirebirdException("Failed to prepare statement");
    }
    return std::make_unique<Statement>(stmt, this);
}
```

**Преимущества**:
- Полная обратная совместимость
- Автоматическое кэширование при `cache.enabled = true`
- Не требует изменений в существующем коде
- Можно отключить через конфигурацию

### 8.3. Тестирование производительности

**Добавить в конец примера сводную статистику**:
```cpp
printHeader("Итоговая статистика производительности кэша");

auto final_stats = connection->getCacheStatistics();

std::cout << "📊 ФИНАЛЬНАЯ СТАТИСТИКА КЭША:\n";
std::cout << std::string(50, '=') << "\n\n";

std::cout << "Использование кэша:\n";
std::cout << "  • Всего запросов: " << (final_stats.hitCount + final_stats.missCount) << "\n";
std::cout << "  • Попадания (hits): " << final_stats.hitCount << "\n";
std::cout << "  • Промахи (misses): " << final_stats.missCount << "\n";
std::cout << "  • Hit Rate: " << std::fixed << std::setprecision(1) << final_stats.hitRate << "%\n\n";

std::cout << "Управление памятью:\n";
std::cout << "  • Размер кэша: " << final_stats.cacheSize << " / " << util::Config::cache().maxStatements << "\n";
std::cout << "  • Вытеснений (evictions): " << final_stats.evictionCount << "\n\n";

// Расчет экономии
if (total_prepare_time_cached > 0 && total_prepare_time_uncached > 0) {
    double speedup = (double)total_prepare_time_uncached / total_prepare_time_cached;
    double time_saved = total_prepare_time_uncached - total_prepare_time_cached;

    std::cout << "Производительность:\n";
    std::cout << "  • Время без кэша: " << total_prepare_time_uncached << " мкс\n";
    std::cout << "  • Время с кэшем: " << total_prepare_time_cached << " мкс\n";
    std::cout << "  • Ускорение: " << std::fixed << std::setprecision(1) << speedup << "x\n";
    std::cout << "  • Сэкономлено: " << time_saved << " мкс\n";
}
```

## 9. Ссылки и контекст

- **Statement класс**: `include/fbpp/core/statement.hpp`
- **Connection класс**: `include/fbpp/core/connection.hpp`
- **Текущая prepare реализация**: `src/core/firebird/fb_connection.cpp:420-458`
- **getCachedStatement реализация**: `src/core/firebird/fb_connection.cpp:462-487`
- **StatementCache класс**: `include/fbpp/core/statement_cache.hpp`
- **StatementCache реализация**: `src/core/firebird/fb_statement_cache.cpp`
- **Config структура**: `include/fbpp_util/config.h:30-35` (CacheConfig)
- **Config реализация**: `src/util/config.cpp:53-70` (загрузка cache конфига)
- **Firebird IStatement**: `/opt/firebird/include/firebird/IdlFbInterfaces.h`
- **Примеры использования**: `examples/` директория
- **Эталонный пример**: `examples/01_basic_operations.cpp`
- **Тесты кэша**: `tests/unit/test_statement_cache.cpp`, `tests/unit/test_cached_statements.cpp`

## 10. Чеклист выполнения

### Этап 1 - Базовая реализация кэша ✅
- [x] Создать класс StatementCache с LRU eviction
- [x] Добавить конфигурацию кэша (maxSize, ttlMinutes, enabled)
- [x] Реализовать SQL нормализацию (case-insensitive, whitespace)
- [x] Добавить thread-safe доступ (std::mutex)
- [x] Интегрировать с Connection::getCachedStatement()
- [x] Создать unit тесты для кэша

### Этап 2 - Интеграция с API ✅
- [x] Добавить методы в Connection класс
- [x] Реализовать shared_ptr владение для кэшированных statements
- [x] Добавить статистику кэша (hits, misses, evictions)
- [x] Создать интеграционные тесты

### Этап 2.5 - Валидация и упрощение API ✅
- [x] ~~Создать пример 05_cached_operations.cpp~~ (удален за ненадобностью)
- [x] ~~Заменить prepareStatement на getCachedStatement в примере~~
- [x] Добавить вывод статистики кэша
- [x] Добавить измерение производительности
- [x] Добавить поддержку shared_ptr в Transaction
- [x] **УПРОЩЕНИЕ API**: Переименовать getCachedStatement → prepareStatement
- [x] **BREAKING CHANGE**: Удалить старый prepareStatement с unique_ptr
- [x] Обновить все примеры и тесты для нового API

### Этап 3 - Именованные параметры ❌ (не реализовано)
- [ ] Реализовать парсинг именованных параметров `:param_name`
- [ ] Добавить поддержку множественных параметров с одним именем
- [ ] Расширить ParamInfo структуру
- [ ] Добавить тесты для именованных параметров

### Этап 4 - Расширенная поддержка метаданных ❌ (не реализовано)
- [ ] Добавить извлечение метаданных при кэшировании
- [ ] Хранить input/output параметры в кэше
- [ ] Добавить валидацию типов параметров
- [ ] Реализовать автоматическое приведение типов

## Статус проекта: **ОСНОВНАЯ ФУНКЦИОНАЛЬНОСТЬ ЗАВЕРШЕНА** ✅

**Готово к продакшену**:
- ✅ LRU кэш с thread safety
- ✅ Прозрачное кэширование через единый prepareStatement()
- ✅ Статистика и мониторинг
- ✅ Полное тестовое покрытие
- ✅ Упрощенный API с shared_ptr

**Следующие этапы** (опционально):
- Именованные параметры для улучшения DX
- Расширенные метаданные для валидации