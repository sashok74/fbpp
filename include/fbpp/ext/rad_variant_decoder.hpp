#pragma once

// Single source of truth для декодирования FB row-buffer cell в VCL Variant.
// Используется И FbppAdapter::DsqlGetValue path, И dataset_adapter.hpp::AssignField.
// Раньше эти две точки расходились (адаптер для NUMERIC scale<0 возвращал
// Currency exact, AssignField — double через ApplyScale).
//
// API:
//   fbpp::ext::decodeFieldToVariant(field, dataPtr, txn, options) → Variant
//   fbpp::ext::makeScaledNumericVariant(raw, scale, options)      → Variant
//
// Покрывает все FB SQL types: TEXT/VARYING, BOOLEAN, SHORT/LONG/INT64
// (включая scaled NUMERIC/DECIMAL → Currency для exactCurrency=true),
// FLOAT/DOUBLE, INT128/DEC16/DEC34, DATE/TIME/TIMESTAMP, TIME_TZ/TIMESTAMP_TZ,
// BLOB (text/binary при наличии tx).

#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/timestamp_utils.hpp"
#include "fbpp_util/tdatetime.hpp"

#ifdef FBPP_WITH_RAD_DATASET

#include "fbpp/core/transaction.hpp"

// ВАЖНО: НЕ включаем здесь <Data.DB.hpp> — он конфликтует с Firebird's ibase.h
// (BDE legacy ISC_LONG/ISC_TIMESTAMP в Data.DB.hpp ≠ Firebird's modern types).
// Caller должен включить <Data.DB.hpp> ДО этого header — это делают
// dataset_adapter.hpp и FbppAdapter.cpp через VCL.h/PCH.
#include <System.hpp>
#include <System.Classes.hpp>
#include <System.SysUtils.hpp>
#include <System.Variants.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace fbpp::ext {

enum class UnsupportedPolicy {
    EmptyVariant,    // legacy: вернуть Variant() для unsupported types/blob без tx
    Throw,           // throw std::runtime_error с типом FB
    StringFallback   // (резерв) — пока эквивалент EmptyVariant; не реализован
};

struct VariantDecodeOptions {
    UnsupportedPolicy unsupported = UnsupportedPolicy::EmptyVariant;
    // Map NUMERIC SQL_SHORT/LONG/INT64 + scale ∈ [-4..-1] → varCurrency
    // (raw int64 × 10⁻⁴, exact). Если false — всегда double (legacy).
    bool exactCurrency = true;
    // Если true и txn != nullptr — load blob content. По умолчанию false:
    // mass-load в TMemTableEh не должен дёргать blob bytes per row.
    bool loadBlobs = false;
};

// Категория столбца для dispatch'а в writers/decoders. Соответствует таблице
// в makeColumnPlan() — единственное место правил SQL_* → kind.
enum class RadColumnKind {
    Int32,         // SQL_SHORT/LONG, scale 0
    Int64,         // SQL_INT64,  scale 0
    Bool,          // SQL_BOOLEAN
    Double,        // SQL_FLOAT/DOUBLE/D_FLOAT, или scaled NUMERIC при exactCurrency=false
    Currency,      // SQL_SHORT/LONG/INT64 с scale ∈ [-4..-1] (exactCurrency=true)
    Date,          // SQL_TYPE_DATE
    Time,          // SQL_TYPE_TIME, SQL_TIME_TZ
    DateTime,      // SQL_TIMESTAMP, SQL_TIMESTAMP_TZ
    WideString,    // SQL_TEXT/VARYING, SQL_INT128, SQL_DEC16/DEC34
    Blob,          // SQL_BLOB (sub_type 0/1)
    Unsupported
};

// План декодирования одного столбца. Строится через makeColumnPlan() из FieldInfo
// + offsets из MessageMetadata. Хранится cached в FbppAdapter::DsqlEntry::outputPlans
// и используется и для memtable, и для DsqlGetValue.
struct ColumnDecodePlan {
    fbpp::core::FieldInfo info{};
    unsigned offset = 0;
    unsigned nullOffset = 0;
    Data::Db::TFieldType fieldType = Data::Db::ftUnknown;
    RadColumnKind kind = RadColumnKind::Unsupported;
    bool needsTxn = false;   // true для Blob — без txn decoder вернёт unsupported
    bool lossy = false;      // INT128/DECFLOAT через double/string, TZ-types теряют offset
};

namespace detail {

inline std::string TrimTrailingSpaces(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

inline System::UnicodeString Utf8ToWide(const std::string& s) {
    return System::UnicodeString(System::UTF8String(s.c_str()));
}

// Same algorithm as AssignField: lossy but consistent для INT128 → double.
inline double Int128ToDouble(const uint8_t* data, int scale) {
    uint64_t low = 0;
    int64_t high = 0;
    std::memcpy(&low, data, sizeof(low));
    std::memcpy(&high, data + sizeof(uint64_t), sizeof(high));
    long double value = static_cast<long double>(high);
    value *= 18446744073709551616.0L;
    value += static_cast<long double>(low);
    if (scale < 0) {
        const long double f = std::pow(10.0L, static_cast<long double>(-scale));
        value /= f;
    }
    return static_cast<double>(value);
}

} // namespace detail

// Build varCurrency для NUMERIC scale ∈ [-4..-1] (exact int64 × 10⁻⁴);
// для scale=0 — varInt64; иначе — fallback double.
inline System::Variant
makeScaledNumericVariant(int64_t raw, int scale, const VariantDecodeOptions& opts = {}) {
    using System::Variant;
    if (scale == 0) return Variant((__int64)raw);
    if (opts.exactCurrency && scale >= -4 && scale < 0) {
        int64_t mult = 1;
        for (int i = 0; i < (4 + scale); ++i) mult *= 10;
        int64_t curRaw = raw * mult;
        // CurrencyBase::Val — public __int64 (см. syscurr.h). Не UB.
        Currency c;
        c.Val = curRaw;
        return Variant(c);
    }
    return Variant((double)raw / std::pow(10.0, -scale));
}

inline System::Variant
decodeFieldToVariant(const fbpp::core::FieldInfo& info,
                     const uint8_t* dataPtr,
                     fbpp::core::Transaction* txn = nullptr,
                     const VariantDecodeOptions& opts = {})
{
    using namespace fbpp::core;
    using System::Variant;

    auto unsupported = [&](const char* what) -> Variant {
        if (opts.unsupported == UnsupportedPolicy::Throw) {
            throw std::runtime_error(std::string("decodeFieldToVariant: unsupported FB type ") +
                                     what + " (sql_type=" + std::to_string(info.type) + ")");
        }
        return Variant();
    };

    switch (info.type) {
        case SQL_TEXT: {
            std::string text((const char*)dataPtr, info.length);
            return Variant(detail::Utf8ToWide(detail::TrimTrailingSpaces(std::move(text))));
        }
        case SQL_VARYING: {
            uint16_t len = 0;
            std::memcpy(&len, dataPtr, sizeof(uint16_t));
            std::string text((const char*)(dataPtr + 2), len);
            return Variant(detail::Utf8ToWide(text));
        }
        case SQL_BOOLEAN: {
            uint8_t b = 0; std::memcpy(&b, dataPtr, 1);
            return Variant((bool)(b != 0));
        }
        case SQL_SHORT: {
            int16_t raw; std::memcpy(&raw, dataPtr, 2);
            if (info.scale == 0) return Variant((int)raw);
            return makeScaledNumericVariant((int64_t)raw, info.scale, opts);
        }
        case SQL_LONG: {
            int32_t raw; std::memcpy(&raw, dataPtr, 4);
            if (info.scale == 0) return Variant((int)raw);
            return makeScaledNumericVariant((int64_t)raw, info.scale, opts);
        }
        case SQL_INT64: {
            int64_t raw; std::memcpy(&raw, dataPtr, 8);
            if (info.scale == 0) return Variant((__int64)raw);
            return makeScaledNumericVariant(raw, info.scale, opts);
        }
        case SQL_FLOAT: {
            float raw = 0.f; std::memcpy(&raw, dataPtr, 4);
            return Variant((double)raw);
        }
        case SQL_DOUBLE:
        case SQL_D_FLOAT: {
            double raw = 0.0; std::memcpy(&raw, dataPtr, 8);
            return Variant(raw);
        }
        case SQL_INT128: {
            // Lossy через double — INT128 не помещается в Currency/double точно
            // на больших значениях. Stage 5: можно через IUtil::int128->toString.
            return Variant(detail::Int128ToDouble(dataPtr, info.scale));
        }
        case SQL_DEC16: {
            DecFloat16 df(dataPtr);
            return Variant(detail::Utf8ToWide(df.to_string()));
        }
        case SQL_DEC34: {
            DecFloat34 df(dataPtr);
            return Variant(detail::Utf8ToWide(df.to_string()));
        }
        case SQL_TYPE_DATE: {
            uint32_t fbDate = 0; std::memcpy(&fbDate, dataPtr, 4);
            auto tp = timestamp_utils::from_firebird_timestamp(fbDate, 0);
            return Variant(System::TDateTime(fbpp::util::tdatetime_from_chrono(tp)));
        }
        case SQL_TYPE_TIME: {
            uint32_t fbTime = 0; std::memcpy(&fbTime, dataPtr, 4);
            auto micros = timestamp_utils::from_firebird_time(fbTime);
            const double fracOfDay = (double)micros.count() / 86400000000.0;
            return Variant(System::TDateTime(fracOfDay));
        }
        case SQL_TIMESTAMP: {
            uint32_t fbDate = 0, fbTime = 0;
            std::memcpy(&fbDate, dataPtr, 4);
            std::memcpy(&fbTime, dataPtr + 4, 4);
            auto tp = timestamp_utils::from_firebird_timestamp(fbDate, fbTime);
            return Variant(System::TDateTime(fbpp::util::tdatetime_from_chrono(tp)));
        }
        case SQL_TIME_TZ: {
            // TZ-метаданные в Variant не помещаются — возвращаем UTC time fraction.
            // Для full TZ support — отдельная задача (custom record type).
            ISC_TIME_TZ tz{};
            std::memcpy(&tz, dataPtr, sizeof(tz));
            auto micros = timestamp_utils::from_firebird_time(tz.utc_time);
            const double fracOfDay = (double)micros.count() / 86400000000.0;
            return Variant(System::TDateTime(fracOfDay));
        }
        case SQL_TIMESTAMP_TZ: {
            ISC_TIMESTAMP_TZ tz{};
            std::memcpy(&tz, dataPtr, sizeof(tz));
            auto tp = timestamp_utils::from_firebird_timestamp(
                tz.utc_timestamp.timestamp_date,
                tz.utc_timestamp.timestamp_time);
            return Variant(System::TDateTime(fbpp::util::tdatetime_from_chrono(tp)));
        }
        case SQL_BLOB: {
            if (!opts.loadBlobs || !txn) return unsupported("BLOB without tx");
            Blob blobId(dataPtr);
            if (blobId.isNull()) return Variant();
            ISC_QUAD blobRef{};
            std::memcpy(&blobRef, blobId.getId(), sizeof(blobRef));
            try {
                auto blobData = txn->loadBlob(&blobRef);
                if (info.subType == 1) {
                    // Text BLOB → UnicodeString
                    std::string text(blobData.begin(), blobData.end());
                    return Variant(detail::Utf8ToWide(text));
                }
                // Binary BLOB → varArray of varByte.
                int n = (int)blobData.size();
                Variant arr = System::Variants::VarArrayCreate(
                    OPENARRAY(int, (0, n - 1)), System::varByte);
                if (n > 0) {
                    void* p = System::Variants::VarArrayLock(arr);
                    std::memcpy(p, blobData.data(), n);
                    System::Variants::VarArrayUnlock(arr);
                }
                return arr;
            } catch (...) { return unsupported("BLOB load error"); }
        }
        default:
            return unsupported("unknown");
    }
}

// =============================================================================
// ColumnDecodePlan: единый слой type-mapping + decode
// =============================================================================
// makeColumnPlan() — единственное место правил SQL_* → TFieldType / RadColumnKind.
// После refactor'а никаких других mapping'ов в проекте быть не должно.

inline ColumnDecodePlan makeColumnPlan(const fbpp::core::FieldInfo& info) {
    using namespace fbpp::core;
    using Data::Db::TFieldType;

    ColumnDecodePlan p;
    p.info = info;
    p.offset = info.offset;
    p.nullOffset = info.nullOffset;
    p.fieldType = TFieldType::ftUnknown;
    p.kind = RadColumnKind::Unsupported;

    switch (info.type) {
        case SQL_TEXT:
        case SQL_VARYING:
            p.fieldType = TFieldType::ftWideString;
            p.kind = RadColumnKind::WideString;
            break;
        case SQL_BOOLEAN:
            p.fieldType = TFieldType::ftBoolean;
            p.kind = RadColumnKind::Bool;
            break;
        case SQL_SHORT:
            if (info.scale == 0) {
                p.fieldType = TFieldType::ftSmallint;
                p.kind = RadColumnKind::Int32;
            } else if (info.scale >= -4 && info.scale < 0) {
                p.fieldType = TFieldType::ftCurrency;
                p.kind = RadColumnKind::Currency;
            } else {
                p.fieldType = TFieldType::ftFloat;
                p.kind = RadColumnKind::Double;
                p.lossy = true;
            }
            break;
        case SQL_LONG:
            if (info.scale == 0) {
                p.fieldType = TFieldType::ftInteger;
                p.kind = RadColumnKind::Int32;
            } else if (info.scale >= -4 && info.scale < 0) {
                p.fieldType = TFieldType::ftCurrency;
                p.kind = RadColumnKind::Currency;
            } else {
                p.fieldType = TFieldType::ftFloat;
                p.kind = RadColumnKind::Double;
                p.lossy = true;
            }
            break;
        case SQL_INT64:
            if (info.scale == 0) {
                p.fieldType = TFieldType::ftLargeint;
                p.kind = RadColumnKind::Int64;
            } else if (info.scale >= -4 && info.scale < 0) {
                p.fieldType = TFieldType::ftCurrency;
                p.kind = RadColumnKind::Currency;
            } else {
                p.fieldType = TFieldType::ftFloat;
                p.kind = RadColumnKind::Double;
                p.lossy = true;
            }
            break;
        case SQL_FLOAT:
        case SQL_DOUBLE:
        case SQL_D_FLOAT:
            p.fieldType = TFieldType::ftFloat;
            p.kind = RadColumnKind::Double;
            break;
        case SQL_INT128:
        case SQL_DEC16:
        case SQL_DEC34:
            p.fieldType = TFieldType::ftWideString;
            p.kind = RadColumnKind::WideString;
            p.lossy = true;
            break;
        case SQL_TYPE_DATE:
            p.fieldType = TFieldType::ftDate;
            p.kind = RadColumnKind::Date;
            break;
        case SQL_TYPE_TIME:
            p.fieldType = TFieldType::ftTime;
            p.kind = RadColumnKind::Time;
            break;
        case SQL_TIME_TZ:
            p.fieldType = TFieldType::ftTime;
            p.kind = RadColumnKind::Time;
            p.lossy = true;
            break;
        case SQL_TIMESTAMP:
            p.fieldType = TFieldType::ftDateTime;
            p.kind = RadColumnKind::DateTime;
            break;
        case SQL_TIMESTAMP_TZ:
            p.fieldType = TFieldType::ftDateTime;
            p.kind = RadColumnKind::DateTime;
            p.lossy = true;
            break;
        case SQL_BLOB:
            p.fieldType = (info.subType == 1) ? TFieldType::ftMemo : TFieldType::ftBlob;
            p.kind = RadColumnKind::Blob;
            p.needsTxn = true;
            break;
        default:
            p.fieldType = TFieldType::ftUnknown;
            p.kind = RadColumnKind::Unsupported;
            p.lossy = true;
            break;
    }
    return p;
}

inline std::vector<ColumnDecodePlan>
buildColumnPlans(const fbpp::core::MessageMetadata& md) {
    std::vector<ColumnDecodePlan> plans;
    const unsigned n = md.getCount();
    plans.reserve(n);
    auto fields = md.getFields();
    for (unsigned i = 0; i < n; ++i) {
        ColumnDecodePlan p = makeColumnPlan(fields[i]);
        // info.offset/nullOffset уже выставлены в getFields(), но на всякий случай
        // подтянем из metadata API.
        p.offset = md.getOffset(i);
        p.nullOffset = md.getNullOffset(i);
        plans.push_back(std::move(p));
    }
    return plans;
}

inline void configureFieldDefs(Data::Db::TFieldDefs* defs,
                               const std::vector<ColumnDecodePlan>& plans)
{
    if (!defs) return;
    defs->Clear();
    defs->Capacity = (int)plans.size();
    for (const auto& p : plans) {
        int size = 0;
        if (p.fieldType == Data::Db::ftWideString ||
            p.fieldType == Data::Db::ftString) {
            size = (int)p.info.length;
            if (size <= 0) size = 1;
        }
        // User-facing identifier via shared identity helper (alias-then-name).
        // Для literal columns ('0 as flag, 0 as new_item_id') Firebird возвращает
        // name="CONSTANT" для обоих — displayName берёт alias и избегает
        // "Duplicate name" в TFieldDefs.
        defs->Add(detail::Utf8ToWide(fbpp::core::displayName(p.info)),
                  p.fieldType, size, !p.info.nullable);
    }
}

// Тонкий wrapper: проверка null + delegate в decodeFieldToVariant.
inline System::Variant
decodeColumnToVariant(const ColumnDecodePlan& plan,
                      const uint8_t* rowBase,
                      fbpp::core::Transaction* txn,
                      const VariantDecodeOptions& opts = {})
{
    int16_t nullFlag = 0;
    std::memcpy(&nullFlag, rowBase + plan.nullOffset, sizeof(int16_t));
    if (nullFlag == -1) return System::Variant();
    return decodeFieldToVariant(plan.info, rowBase + plan.offset, txn, opts);
}

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET
