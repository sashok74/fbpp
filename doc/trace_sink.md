# Trace Sink Integration

Библиотека больше не содержит встроенного логгера. Вместо этого она предоставляет
простое наблюдение за ключевыми событиями (подключение, транзакции, batch,
statement cache) через интерфейс `util::TraceSink`. Клиент сам решает, подключать
ли логирование и какой backend использовать.

## API

```cpp
#include <fbpp_util/trace.h>

namespace util {
    enum class TraceLevel { info, warn, error };

    class TraceSink {
    public:
        virtual ~TraceSink() = default;
        virtual void log(TraceLevel level,
                         std::string_view component,
                         std::string_view message) = 0;
    };

    void setTraceSink(TraceSink* sink);
    TraceSink* getTraceSink();

    // Утилиты для форматирования сообщений:
    void traceMessage(TraceLevel level,
                      std::string_view component,
                      std::string_view message);

    template<typename Formatter>
    void trace(TraceLevel level,
               std::string_view component,
               Formatter&& formatter);
}
```

По умолчанию sink отсутствует (`nullptr`), вызовы `trace` ничего не делают.

## Пример: вывод в spdlog

```cpp
#include <fbpp_util/trace.h>
#include <spdlog/spdlog.h>

class SpdlogSink : public util::TraceSink {
public:
    void log(util::TraceLevel level,
             std::string_view component,
             std::string_view message) override {
        auto logger = spdlog::default_logger();
        switch (level) {
            case util::TraceLevel::info:
                logger->info("[{}] {}", component, message);
                break;
            case util::TraceLevel::warn:
                logger->warn("[{}] {}", component, message);
                break;
            case util::TraceLevel::error:
                logger->error("[{}] {}", component, message);
                break;
        }
    }
};

int main() {
    auto sink = std::make_unique<SpdlogSink>();
    util::setTraceSink(sink.get());

    // ... работаем с fbpp ...

    util::setTraceSink(nullptr); // отключить по завершении
}
```

## Формат сообщений

Компоненты и события:

| Component      | События                                                                 |
|----------------|-------------------------------------------------------------------------|
| `Connection`   | Подключение/отключение, вызовы `cancelOperation`, попытка выполнить SQL |
| `Transaction`  | Commit/rollback (успех/ошибка), автосброс в деструкторе, BLOB операции  |
| `Batch`        | Начало выполнения, ошибки отдельных сообщений, финальная статистика     |
| `StatementCache` | Изменение конфигурации, ошибки подготовки SQL                         |
| `SqlValueCodec` | Предупреждения об усечении строк                                       |

Сообщения формируются через `std::ostringstream` внутри `util::trace`, поэтому
дополнительных зависимостей (fmt, spdlog) не требуется.
