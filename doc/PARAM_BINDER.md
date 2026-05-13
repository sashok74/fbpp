# ParamBinder — typed name-based parameter binding

`fbpp::core::ParamBinder` lets you bind named parameters of a prepared
statement directly from native C++ values, **without** going through
`nlohmann::json`. It is the recommended path when the caller already has values
in their native type (`int64_t`, `std::string`, `Int128`, `chrono::time_point`,
…) — no JSON serialization round-trip, no precision loss for 128-bit integers
or DECFLOAT.

It is designed for callers that have a sequence of `(name, typed_value)` pairs
computed at runtime — for example legacy / VCL adapters whose runtime hands you
a typed `Variant` per column and which want lazy-transaction binding plus
non-throwing-on-typo semantics (classic IBO `Param.AsXxx := ...`).

- Public header: `include/fbpp/core/param_binder.hpp`
- Tests: `tests/unit/test_param_binder.cpp`, `tests/unit/test_param_binder_optional.cpp`
- Example: `examples/10_param_binder.cpp`

## When to use which path

| Style | Typical caller | Notes |
|---|---|---|
| `tx->execute(stmt, std::tuple{...})` | Generated code, internal C++ | Positional. Compile-time field count. |
| `tx->execute(stmt, json_object)` | Anything that already has JSON | One JSON conversion per param. Convenient. |
| `tx->execute(stmt, struct_value)` | Generated DTOs | Field-by-field, name-based via `StructDescriptor`. |
| **`tx->execute(stmt, ParamBinder)`** | Named binding from native types, dynamic / variant-driven code | No JSON, deduces `T` per `set` call, name-based. |

If you have a JSON object on hand → keep using JSON. If you have a struct DTO →
keep using struct path. **ParamBinder fills the gap** where you have neither: a
sequence of `(name, typed_value)` pairs being computed at runtime.

## Quick example

```cpp
#include "fbpp/core/connection.hpp"
#include "fbpp/core/param_binder.hpp"

auto stmt = conn.prepareStatement(R"SQL(
    INSERT INTO orders (id, customer, total, placed_at)
    VALUES (:id, :customer, :total, :placed_at)
)SQL");

auto tx = conn.StartTransaction();

ParamBinder b(stmt, tx.get());
b.set("id",        int64_t{42});
b.set("customer",  std::string("ACME"));
b.set("total",     1234.56);                       // double → NUMERIC if scaled
b.set("placed_at", std::chrono::system_clock::now());

tx->execute(stmt, b);
tx->Commit();
```

`T` is deduced from each value — you never write `set<int64_t>(...)`. If `T`
matches the column type (or a registered adapter, or can be parsed from a
string), the codec writes it. Otherwise you get a compile-time error.

## Lifecycle

```
Statement (long-lived, reused across tx) ──┐
                                            ├─► ParamBinder (one per tx exec)
Transaction (short-lived, optional)  ───────┘
```

A binder owns its own input message buffer (sized by the statement's input
metadata) and a private copy of `MessageMetadata`. It does **not** own the
statement (shared ownership via `std::shared_ptr<Statement>`).

A statement is typically prepared once and cached on the connection. Each
transaction execution creates (or reuses via `clear()`) a binder.

## API

```cpp
class ParamBinder {
public:
    explicit ParamBinder(std::shared_ptr<Statement> stmt,
                         Transaction* tx = nullptr);

    // Move-only, noexcept. Copy is deleted.
    ParamBinder(ParamBinder&&) noexcept = default;
    ParamBinder& operator=(ParamBinder&&) noexcept = default;

    template<typename T>
    bool set(std::string_view name, const T& value);

    bool set(std::string_view name, std::nullopt_t);  // explicit NULL

    bool setNull(std::string_view name);

    void setTransaction(Transaction* tx) noexcept;
    Transaction* transaction() const noexcept;

    void clear();

    bool empty() const noexcept;            // true if statement has no input params
    const MessageMetadata* metadata() const noexcept;
    const uint8_t* buffer() const noexcept;
};

// Transaction overloads (see <fbpp/core/transaction.hpp>)
unsigned Transaction::execute(<stmt>, const ParamBinder&);
std::unique_ptr<ResultSet>
        Transaction::openCursor(<stmt>, const ParamBinder&, unsigned flags = 0);
```

### `set(name, value)`

- Looks up `name` in the statement's named-parameter mapping (built by
  `NamedParamParser` at prepare time). If found, writes `value` at every
  position that `name` maps to (handles repeated `:name` in SQL automatically).
- Returns **`true` on success**, **`false` if the name is unknown**.
- The name is normalized internally: leading/trailing ASCII whitespace is
  trimmed and the result is lowercased before lookup. The mapping itself is
  stored in lowercase.
- The value is forwarded to `detail::packValueWithCodec` — the same per-field
  codec used by `TuplePacker` and `StructDescriptor`. Any type that works in
  one of those paths works here.

### `setNull(name)`

Marks the named parameter as SQL NULL (writes `-1` to every position's null
indicator). Same return contract as `set`.

### `clear()`

Re-zeroes the buffer and re-initializes every null indicator to NULL. Buffer
allocation and metadata are kept. Use this between iterations of a reuse loop;
do not allocate a new binder.

### `setTransaction(tx)`

Late-binds a transaction. Required before binding any value into a BLOB
column (the codec needs a transaction to call `createBlob`). Until then,
non-BLOB parameters bind fine without a transaction.

### `empty()`

True iff the statement has no input parameters. In that case every `set` call
returns false and `tx->execute(stmt, binder)` falls back to the no-args
overload.

## Behavior contract

These behaviors were chosen so the binder fits legacy IBO-style adapters but
they are general:

1. **Non-throwing unknown name.** `set("typo", 42)` returns `false`; nothing
   is written and no exception is raised. The caller can log and continue.
   Mirrors classic IBO `Param.AsXxx := ...` which silently no-ops on missing
   names.
2. **Name normalization.** `set("  Doc_Id  ", 1)` finds `:doc_id`. Leading and
   trailing ASCII whitespace are trimmed; case is folded.
3. **Repeated `:name` in SQL.** A single `set("v", 100)` writes the same value
   into every occurrence. No need to repeat.
4. **Missing parameter → NULL.** Constructor and `clear()` initialize every
   null indicator to `-1`. A position you never `set` is NULL in the database.
   This matches the JSON-path behavior.
5. **Lazy transaction.** Binder is constructible without a transaction; one
   can be attached later via `setTransaction`. The transaction is only
   required for BLOB writes — non-BLOB parameters bind without it.
6. **Move-only, noexcept.** Safe to store in `std::optional`, `std::map`, etc.
   Copy is deleted to prevent silent buffer duplication.

## Type support

Anything `sql_value_codec::write_sql_value` accepts. Concretely:

| Firebird type | C++ types that bind directly |
|---|---|
| SMALLINT / INTEGER / BIGINT | `int16_t` / `int32_t` / `int64_t` (and `int`) |
| FLOAT | `float` |
| DOUBLE PRECISION | `double` |
| BOOLEAN | `bool` |
| CHAR / VARCHAR | `std::string`, `const char*` |
| BLOB SUB_TYPE TEXT | `std::string` |
| BLOB SUB_TYPE BINARY | `std::string` (raw bytes; embedded NULs OK) |
| DATE | `std::chrono::year_month_day` (via `chrono_datetime` adapter) |
| TIME | `std::chrono::hh_mm_ss<…>` |
| TIMESTAMP | `std::chrono::system_clock::time_point` |
| TIMESTAMP WITH TIME ZONE | `std::chrono::zoned_time` |
| TIME WITH TIME ZONE | `fbpp::core::TimeTz` |
| NUMERIC(p≤18, s) | `double` (codec scales by `10^s`), `int64_t` (pre-scaled), `std::string` |
| NUMERIC(p>18, s) | `TTNumeric<words, scale>` (TTMath), `Int128` (pre-scaled), `std::string` |
| INT128 | `Int128`, `std::string` |
| DECFLOAT(16) | `DecFloat16`, `std::string` |
| DECFLOAT(34) | `DecFloat34`, `std::string` |

**`std::string` is a universal fallback** for every extended type. The codec
parses it via Firebird's own `IUtil::getInt128/getDecFloat16/getDecFloat34/
encodeDate/encodeTime` helpers (bit-exact with the server). Use this when you
do **not** want to depend on TTMath/CppDecimal — for example in a VCL adapter
where `Variant.AsString()` already gives you a string.

`std::optional<T>` works as expected: empty optional → SQL NULL; engaged
optional → recursive `set(*v)`. This goes through the codec's
`std::optional<T>` overload — no explicit `set()` overload is involved.

For literal NULL without spelling the inner type, pass `std::nullopt`:

```cpp
b.set("X", std::nullopt);   // equivalent to b.setNull("X")
```

This is an explicit overload of `set` because bare `std::nullopt` has type
`std::nullopt_t`, which the templated `set<T>` would otherwise route through
the generic codec memcpy path (silently writing junk into the buffer).

## Reuse pattern

The intended loop:

```cpp
auto stmt = conn.prepareStatement(sql);   // cached on the connection

ParamBinder b(stmt, tx.get());
for (const auto& row : rows) {
    b.clear();
    b.set("id",   row.id);
    b.set("name", row.name);
    b.set("amt",  row.amt);
    tx->execute(stmt, b);
}
```

Compared to `nlohmann::json` reuse, this avoids per-iteration JSON object
construction and reduces allocator churn — the buffer is allocated once.

## Move into `std::optional` / `std::map`

`ParamBinder` is move-only and `noexcept`-movable. It can be stored in
containers that move on rehash:

```cpp
struct DsqlEntry {
    std::shared_ptr<fbpp::core::Statement> stmt;
    std::unique_ptr<fbpp::core::MessageMetadata> outMd;
    std::optional<fbpp::core::ParamBinder> binder;   // lazily emplace()'d
    std::unique_ptr<fbpp::core::ResultSet> cursor;
};
std::map<std::string, DsqlEntry> cache;
```

`cache.emplace_or_assign` and rehash are safe because `ParamBinder`'s move
constructor is `noexcept`. Copy is intentionally deleted.

## BLOB binding

For BOTH `BLOB SUB_TYPE TEXT` and `BLOB SUB_TYPE BINARY`, you bind a
`std::string` containing the bytes (NUL bytes mid-string are preserved). The
codec creates a BLOB on the bound transaction and writes its ID into the
buffer.

Binary BLOB does not go through any charset interpretation — the bytes are
copied to BLOB storage as-is.

If `tx_` is `nullptr` at `set()` time and the column is BLOB, you get a
`FirebirdException` from the codec (the BLOB cannot be created without a
transaction). Set the transaction first via `setTransaction()`.

For text BLOB columns specifically, you can also use
`Transaction::createBlob(data, /*subType=*/1)` to tag the BLOB as text
explicitly when bypassing the codec.

## Codec changes that landed with ParamBinder

Adding ParamBinder uncovered two pre-existing gaps in
`include/fbpp/core/detail/sql_value_codec.hpp`. Both fixes apply equally to
the JSON, tuple, struct, and binder paths because they all share the codec.

1. **Plain `DOUBLE PRECISION` read silently produced zeros.**
   The read path's `if constexpr (T == double) { if (scale < 0) … }` branch
   handled scaled NUMERIC but had no body for `scale == 0`. The else-if
   chain skipped over `double`, so the generic else-memcpy at the bottom was
   unreachable. Added explicit `memcpy + return` for the unscaled case.
2. **`std::string` could not be bound to `BLOB SUB_TYPE BINARY`.**
   The string-write branch only treated TEXT BLOB (`subType == 1`) as a BLOB
   path. BINARY (`subType == 0`) fell through and threw "Unsupported type for
   string conversion: 521". Introduced helper `isAnyBlob` and updated the two
   call sites to use it. Semantics for both subtypes is the same: byte-exact
   copy.

## Companion: VCL `TDateTime` helper

For RAD Studio / C++Builder callers using VCL's `TDateTime` (which is a plain
`double` representing days since 1899-12-30), there is a header-only
conversion helper that has **no dependency on VCL**:

- Public header: `include/fbpp_util/tdatetime.hpp`
- Tests: `tests/unit/test_tdatetime.cpp`

```cpp
#include <fbpp_util/tdatetime.hpp>

// VCL-side
TDateTime placed = ...;
auto tp = fbpp::util::chrono_from_tdatetime(static_cast<double>(placed));
binder.set("placed_at", tp);

// Reverse: chrono → TDateTime for display in VCL
TDateTime back = fbpp::util::tdatetime_from_chrono(tp);
```

API:

```cpp
namespace fbpp::util {
    inline std::chrono::system_clock::time_point
        chrono_from_tdatetime(double tdt) noexcept;

    inline double
        tdatetime_from_chrono(std::chrono::system_clock::time_point tp) noexcept;
}
```

- Conversion goes through microseconds. Round-trip is microsecond-stable for
  any year within a few centuries of 1900 (well beyond Firebird's 100 µs
  TIMESTAMP granularity).
- Negative `TDateTime` values (dates before 1899-12-30) are not handled —
  VCL uses an unusual sign convention there. Convert on the Delphi side
  using `DateTimeToSystemTime` first if you need pre-1900 dates.
- The helper deliberately takes a plain `double`, not `System::TDateTime`, so
  fbpp does not pull in VCL.

## Pitfalls

1. **Don't reuse a binder across statements.** A binder is bound to one
   statement's input metadata. To bind a different statement, construct a
   new binder.
2. **Don't `set()` after `Commit()`.** The transaction is gone; BLOB writes
   will fail. The non-BLOB writes happen at `set()` time so they would have
   succeeded earlier — but executing the binder against a closed transaction
   throws.
3. **`set("id", 42)` deduces `int`, not `int64_t`.** Usually fine — the
   codec accepts any integer width that fits the column. If you need strict
   width, write `int64_t{42}` or use a typed variable.
4. **`set("name", "literal")` deduces `const char*`, not `std::string`.**
   The codec handles both; this is informational only.
5. **`std::vector<uint8_t>` is NOT yet a codec-supported type for BLOB
   BINARY** — bind via `std::string` instead (you can construct one from any
   byte range). Adding a vector overload is straightforward but currently not
   included.
6. **Statement with no input parameters.** `binder.empty()` is true, every
   `set` returns false, and `tx->execute(stmt, binder)` does the no-args
   execute. This is silent — no exception.

## Output naming convention (symmetric to ParamBinder's input lookup)

Input lookup (`set(name, value)`) trims and case-folds the name before
resolving against the named-parameter mapping — see "Behavior contract" #2.
Output metadata (`MessageMetadata::getField(name)`, `getIndex(name)`) is
**symmetric**: a two-pass lookup, exact first then ASCII case-fold + trim.
Both passes match against `name` and `alias` in the `FieldInfo`.

There are two name fields per output column and they have different semantics:

- `FieldInfo::name` — **provenance** (origin column from server). For
  literal expressions like `SELECT 0 AS flag` returns `"CONSTANT"`. NOT a
  user-facing identifier — using it directly for `SELECT 0 AS x, 0 AS y`
  collides on `"CONSTANT"`.
- `FieldInfo::alias` — **identity** (what the user wrote). For column ref
  without `AS` equals the column name; for `expr AS foo` equals `"FOO"`
  (Firebird uppercases unquoted identifiers); for `expr AS "Foo"` equals
  `"Foo"`. May be empty for unaliased expressions.

For user-facing output keys (JSON keys, VCL `TFieldDefs.Add`,
column→value maps) **always use the canonical helpers**:

```cpp
const std::string& name = displayName(fieldInfo);  // free function
std::string name        = meta.getDisplayName(i);  // by index
```

Both equal `alias.empty() ? name : alias`. They are the single source of
truth — `json_unpacker` and `StatementCache::extractMetadata` use them too.

## See also

- `doc/fbpp.md` — canonical library guide
- `examples/06_named_parameters.cpp` — equivalent flow via `nlohmann::json`
- `examples/10_param_binder.cpp` — equivalent flow via ParamBinder
