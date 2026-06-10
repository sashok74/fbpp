# Дорожная карта: полная поддержка Firebird 5 API

Статус: утверждена (2026-06-10). Цель — довести `fbpp` до полного покрытия Firebird 5 OO API: прикладной DSQL-слой, события (IEvents), Services API и распределённые транзакции (IDtc). Фазы упорядочены по ценности; каждая — отдельная ветка `claude/**` → PR со своими тестами против живого Firebird 5 (фикстура `TempDatabaseTest`), полный прогон сюиты после каждого шага.

Текущее состояние покрытия — см. таблицу в [fbpp.md](fbpp.md#степень-покрытия-firebird-api). Дорожная карта закрывает все строки «частично» и «не покрыто», кроме сознательно исключённых (connection pool / async / coroutines — вне контракта runtime).

Сквозные паттерны (обязательны во всех фазах):

- RAII-гарды `detail::XpbBuilderGuard` (DPB/TPB/SPB) и `detail::BlobGuard` из `include/fbpp/core/detail/firebird_raii.hpp`;
- конвертация `Firebird::FbException` → `FirebirdException` на каждой публичной границе;
- семантика FB5: lifecycle-методы (`commit/rollback/free/close/cancel`) **не** освобождают интерфейс — явный `release()`;
- не бросающие деструкторы; новые `.cpp` — в список исходников корневого `CMakeLists.txt`; новые тесты — отдельный бинарь в `tests/CMakeLists.txt` (`gtest_discover_tests`);
- тесты новых фич включают расширенные типы (требование CLAUDE.md).

---

## Фаза 1. Опции транзакций (TPB) + savepoints — [M]

Сейчас `StartTransaction()` жёстко вызывает `startTransaction(&st, 0, nullptr)` (`fb_connection.cpp`) — ни уровней изоляции, ни read-only, ни lock timeout.

**API** (новый заголовок `include/fbpp/core/transaction_options.hpp`):

```cpp
enum class TxIsolation { Concurrency, Consistency, ReadCommittedRecordVersion,
                         ReadCommittedNoRecordVersion, ReadCommittedReadConsistency };
struct TransactionOptions {
    TxIsolation isolation = TxIsolation::Concurrency;
    bool readOnly = false;
    bool wait = true;
    std::optional<uint32_t> lockTimeoutSeconds;   // только при wait == true
    bool noAutoUndo = false;
};
std::shared_ptr<Transaction> Connection::StartTransaction(const TransactionOptions&);

// Savepoints — через SQL: в ITransaction OO API их нет
// (startSavepoint есть только у IReplicatedTransaction).
void Transaction::savepoint(const std::string& name);
void Transaction::releaseSavepoint(const std::string& name);
void Transaction::rollbackToSavepoint(const std::string& name);
```

TPB строится через `XpbBuilderGuard(IXpbBuilder::TPB)` зеркально DPB-коду в `connect()`: `isc_tpb_concurrency|consistency|read_committed(+rec_version|no_rec_version|read_consistency)`, `isc_tpb_read|write`, `isc_tpb_wait|nowait`, `isc_tpb_lock_timeout`.

Гочи: `lock_timeout` + `nowait` несовместимы — валидация до сервера; имена savepoint эмитятся только как delimited identifier с экранированием кавычек (анти-инъекция); существующий безпараметровый `StartTransaction()` не меняется.

Тесты: readOnly → INSERT кидает; nowait → мгновенный конфликт двух соединений; lockTimeout ≈ 1 c; видимость snapshot vs read committed; savepoint round-trip / повторный release / инъекция в имени.

## Фаза 2. BLOB-стриминг: BlobReader / BlobWriter в core — [L]

Сейчас только целиком в память (`Transaction::loadBlob/createBlob`). Новые файлы: `include/fbpp/core/blob.hpp`, `src/core/firebird/fb_blob.cpp`. Размещение в core (не ext): `ext/blob_stream.hpp` — VCL-специфичный адаптер, позже наслаивается на эти классы.

**API:**

```cpp
struct BlobInfo { uint64_t totalLength; uint32_t numSegments; uint32_t maxSegmentSize; bool isStream; };
enum class BlobSeekOrigin { Begin, Current, End };

class BlobReader {            // movable, держит shared_ptr<Transaction> (паттерн ResultSet)
    size_t read(void* buf, size_t maxBytes);   // 0 == EOF
    BlobInfo info() const;
    size_t seek(int offset, BlobSeekOrigin);   // только stream-blob
    void close();
};
class BlobWriter {
    void write(const void* data, size_t bytes); // чанкование <=32KB внутри
    ISC_QUAD finish();                          // close + id
    void cancel();                              // деструктор по умолчанию — cancel, не close
};
BlobReader  Transaction::openBlob(const ISC_QUAD& id);
BlobWriter  Transaction::createBlobStream(int subType = 0, bool streamType = true);
```

Гочи: `getSegment` возвращает `RESULT_SEGMENT` при малом буфере — это данные, не EOF; `seek` работает только при `isc_bpb_type_stream` в BPB (на segmented — `isc_bad_segstr_type` → понятная ошибка); перевод `loadBlob/createBlob` на новые классы — только при зелёных `test_blob_subtype`/`test_blob_error_paths`.

Тесты: round-trip 1 MB разными размерами чанков; продолжение по `RESULT_SEGMENT`; seek по stream / throw по segmented; `cancel()`; UTF-8 text subtype; reader переживает отпущенный вызывающим tx-handle.

## Фаза 3. Batch BLOB — [M]

`Batch` не оборачивает `registerBlob/addBlob/appendBlobData/getBlobAlignment`; `TAG_BLOB_POLICY` не выставляется.

**API:**

```cpp
enum class BatchBlobPolicy : unsigned { None, IdEngine, IdUser };  // BLOB_STREAM отложен
struct BatchOptions { bool recordCounts = true; bool continueOnError = false;
                      BatchBlobPolicy blobPolicy = BatchBlobPolicy::None; };
std::unique_ptr<Batch> createBatch(Transaction*, const BatchOptions&); // старые (bool,bool) — шимы

ISC_QUAD Batch::addBlob(const void* data, size_t len, int subType = 0); // чанкование >64KB внутри
void     Batch::appendBlobData(const void* data, size_t len);
ISC_QUAD Batch::registerBlob(const ISC_QUAD& existing);                 // стык с BlobWriter::finish()
unsigned Batch::getBlobAlignment() const;
```

Гочи: `addBlob` на батче с политикой `None` — пре-чек с внятным исключением (вместо engine error); id из `addBlob` валиден только внутри батча; `TAG_BLOB_POLICY` вставляется в pb при `IStatement::createBatch` (рядом с `TAG_RECORD_COUNTS` в `fb_statement.cpp`).

Тесты: батч-вставка строк с BLOB-колонкой через addBlob; >64 KB через внутреннее чанкование; `registerBlob` блоба из фазы 2; addBlob без политики → исключение.

## Фаза 4. Scrollable-курсоры — [S/M]

Флаг `CURSOR_TYPE_SCROLLABLE` уже пробрасывается, но методов позиционирования нет.

**API** (`ResultSet`): `fetchFirst/fetchLast/fetchPrior/fetchAbsolute(pos)/fetchRelative(off)` (шаблоны, зеркально `fetch()`), `bool isScrollable() const`. Внутри — единый `fetchInternal(FetchOp, arg, buffer)`; флаги курсора передаются в конструктор ResultSet хвостовым параметром по умолчанию (единственная точка конструирования — `fb_statement.cpp`).

Ключевая тонкость — защёлка `eof_`: каждый scroll сбрасывает её перед fetch (иначе «дойти до конца → fetchPrior» молча вернёт false). На forward-only курсоре scroll-методы кидают по пре-чеку `isScrollable()` (детерминированно, без round-trip). `generation_` бампается на каждом scroll (staleness-guard RowView). `fetchAbsolute(0)` → before-first → false; отрицательные позиции — с конца.

Тесты: Last→First→Absolute(5)→Relative(-2)→Prior по 10 строкам; fetchPrior после EOF; Absolute(-1); scroll на forward-only → исключение, обычный fetch после этого работает; регрессия `rows()`-итератора.

## Фаза 5. Connection::getDatabaseInfo + Statement::setCursorName — [M]

Новый `include/fbpp/core/detail/info_parser.hpp` — универсальный разбор инфо-буферов (tag / 2-byte LE len / payload до `isc_info_end`, обработка `isc_info_truncated`); переиспользуется `BlobReader::info()` из фазы 2.

**API:**

```cpp
struct DatabaseInfo { unsigned pageSize; uint64_t allocatedPages; unsigned odsVersion, odsMinorVersion;
                      bool forcedWrites, readOnly; unsigned sqlDialect, sweepInterval;
                      uint64_t currentMemory; unsigned attachmentId;
                      std::string serverVersion; std::vector<std::string> activeUserNames; };
DatabaseInfo Connection::getDatabaseInfo() const;

void Statement::setCursorName(const std::string& name);   // для UPDATE ... WHERE CURRENT OF
std::vector<unsigned char> Statement::getInfo(std::span<const unsigned char> items) const; // сырой escape hatch
```

Гочи: ретрай буфера ×2 до 64 KB при truncation (`isc_info_user_names` реально обрезается); счётчики — little-endian переменной длины, только через `readInfoInt`; positioned update требует `FOR UPDATE` и уже сфетченной строки — это и есть e2e-тест.

Тесты: pageSize/ODS 13/`serverVersion` содержит "Firebird"/SYSDBA в activeUserNames; юнит-тест парсера на рукотворном truncated-буфере; positioned update меняет ровно текущую строку; setCursorName на freed statement → исключение.

## Фаза 6. Хвосты качества — [S]

Один PR, четыре независимых коммита:

1. **Locale-safe кодек** (`detail/sql_value_codec.hpp`): `std::stof/stod` → `std::from_chars` (строже: сначала trim пробелов), `floating_to_string` → `std::to_chars`. Тест с глобальной локалью `de_DE`/`ru_RU`.
2. **Удалить мёртвый парсер** `StatementCache::parseNamedParameters` (~110 строк, ноль вызовов; рабочий путь — `NamedParamParser`).
3. **Подключить TTL**: `removeExpired()` реализован, но не вызывается — опортунистический sweep внутри `StatementCache::get()` под уже взятым локом (`lastSweep_`). Возврат checked-out инстанса в исчезнувшую запись уже деградирует корректно — закрепить тестом.
4. **trace.cpp**: публикация sink — `memory_order_release`/`acquire` (issue #16).

## Фаза 7. События БД (IEvents) — [M], в fbpp_core

Новые `include/fbpp/core/event_subscription.hpp` + `src/core/firebird/fb_events.cpp`.

**API:**

```cpp
class EventSubscription {   // non-copyable/non-movable; внутренне синхронизирован
    using Callback = std::function<void(const std::string& name, unsigned count)>;
    void cancel();          // идемпотентно: IEvents::cancel + release
    bool isActive() const noexcept;
};
std::unique_ptr<EventSubscription>
Connection::subscribeEvents(std::vector<std::string> names, EventSubscription::Callback);
```

Ключевое:

- EPB строится вручную (хелпера в IUtil нет): байт версии 1, затем на событие len(1)+имя+count(4 LE); ≤15 имён на блок;
- `CallbackImpl : Firebird::IEventCallbackImpl<...>` обязан реализовать собственный atomic `addRef()/release()`;
- **первый callback после `queEvents` — priming**: только baseline счётчиков, юзер-callback не вызывается;
- события одноразовые — re-queue внутри callback **до** вызова юзер-callback (иначе потеря событий между доставкой и перевзводом);
- callback приходит на потоке fbclient: контракт «только сигналить (condvar/queue/atomic), Connection не трогать» — в доке заголовка; деструктор ждёт in-flight callback (флаг + condvar);
- подписки не должны переживать Connection (предусловие, как у Statement).

Тесты: `POST_EVENT` из второго соединения через `EXECUTE BLOCK`; доставка по commit и отсутствие по rollback; несколько имён; re-queue на два коммита; cancel/деструктор под нагрузкой. Риск: remote events требуют RemoteAuxPort — явные таймауты с сообщением об этом.

## Фаза 8. Services API — [L], новый таргет fbpp_services

Новый компонент по образцу `fbpp_schema`: `include/fbpp/services/{service_manager.hpp,types.hpp}`, `src/services/{service_manager.cpp,backup_restore.cpp,user_management.cpp}`, `target_link_libraries(... PUBLIC fbpp_core)`, alias `fbpp::fbpp_services`, свой install component/export set, опционально package smoke. Обоснование: нулевая связность со statement-слоем, админская семантика — приложения-«читатели» за это не платят.

**API:**

```cpp
namespace fbpp::services {
struct ServiceConnectParams { std::string server, user = "SYSDBA", password, role; };
struct BackupOptions  { bool verbose; bool metadataOnly; bool noGarbageCollect; unsigned parallelWorkers; };
struct RestoreOptions { bool verbose; bool replace; unsigned pageSize; unsigned parallelWorkers; };
struct UserInfo { std::string name, firstName, middleName, lastName; std::optional<bool> admin; };

class ServiceManager {                    // RAII над IService
    std::string getServerVersion();
    void backup (const std::string& db, const std::string& bkp, const BackupOptions& = {}, const LineCallback& = {});
    void restore(const std::string& bkp, const std::string& db, const RestoreOptions& = {}, const LineCallback& = {});
    std::string getStatistics(const std::string& db, const StatisticsOptions& = {});
    void addUser(const UserInfo&, const std::string& password);
    void modifyUser(const UserInfo&, const std::optional<std::string>& password = {});
    void deleteUser(const std::string& name);
    std::vector<UserInfo> listUsers();
};
}
```

Ключевое: SPB через `XpbBuilderGuard` с kind `SPB_ATTACH`/`SPB_START` (kind сам пишет преамбулу версии — теги версии руками не вставлять); drain-цикл `query` с receive `{isc_info_svc_line}`, разбор ответа через `IXpbBuilder(SPB_RESPONSE, buf, len)`; пустая строка = конец действия; защита от зацикливания (cap итераций). `IService::cancel` — только при vtable ≥ 5 (version check).

Тесты: версия содержит "5."; backup→restore round-trip временной базы (server-side пути производить от каталога `tests.temp_db` из конфига — работает и локально `/mnt/test`, и в CI `/tmp`); статистика содержит "Database header"; user-lifecycle с pid-суффиксом и cleanup в TearDown.

## Фаза 9. Распределённые транзакции (IDtc) — [S/M], в fbpp_core

`IDtcStart::start()` возвращает **один** y-valve `ITransaction` на все attachments, а `Statement::execute/openCursor` используют только `getRawTransaction()` — существующая машинерия выполнения работает без изменений. Поэтому `DtcTransaction : Transaction` (с `connection_ == nullptr`) + `Prepare()`.

Новые `include/fbpp/core/dtc.hpp` + `src/core/firebird/fb_dtc.cpp`; подготовительный отдельный коммит — guard'ы `connection_ == nullptr` в `Transaction::loadBlob/createBlob` (бросать «operation requires a single-attachment transaction»).

**API:**

```cpp
class DtcTransaction : public Transaction {
    void Prepare();                 // фаза 1 2PC: ITransaction::prepare(0, nullptr)
    bool isPrepared() const noexcept;
};
class Dtc {
    class Builder {                 // IDtc::startBuilder; IDtcStart — IDisposable (dispose, не release!)
        Builder& add(Connection&);
        Builder& add(Connection&, const std::vector<uint8_t>& tpb);
        std::shared_ptr<DtcTransaction> start();
    };
    static std::shared_ptr<DtcTransaction> start(std::initializer_list<std::reference_wrapper<Connection>>);
};
```

Гочи: деструктор `Transaction` не виртуальный — `DtcTransaction` выдаётся только через `make_shared` (type-erased deleter), задокументировать; после `Prepare()` запретить retaining-варианты; limbo-восстановление (`gfix -two_phase`) — вне скоупа v1; оба Connection должны переживать транзакцию.

Тесты (две временные базы): атомарный commit и rollback по двум базам; видимость своих незакоммиченных строк через курсор на dtc-транзакции; `Prepare()`→`Commit()`; auto-rollback в деструкторе; disconnected connection в Builder → исключение.

---

## Порядок веток / PR

`claude/tx-options` → `claude/blob-streaming` → `claude/batch-blob` → `claude/scrollable-cursors` → `claude/db-info` → `claude/quality-tails` → `claude/events` → `claude/services` → `claude/dtc`.

Фазы 2 и 5 делят `detail/info_parser.hpp` — вводит фаза 2 (идёт первой).

## Верификация каждой фазы

1. Полная сборка (`cmake --build ... -j`) и полный прогон `ctest` против живого FB5 (~370 тестов).
2. PR → CI (GCC 13 + MSVC 2022); известный флак `CachedStatementsTest.ParallelConnectionsCanUseCachesIndependently` — см. issue #16, rerun.
3. Обновление таблицы покрытия в `doc/fbpp.md` и, для пользовательских фич, примера в `examples/`.
