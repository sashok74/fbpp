#pragma once

#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/type_adapter.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp_util/logging.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fbpp::core::detail::sql_value_codec {

struct SqlWriteContext {
    const FieldInfo* field;
    Transaction* transaction;
    int16_t* nullIndicator;
};

struct SqlReadContext {
    const FieldInfo* field;
    Transaction* transaction;
    const int16_t* nullIndicator;
};

inline void setNull(int16_t* ptr) {
    if (ptr) {
        *ptr = -1;
    }
}

inline void setNotNull(int16_t* ptr) {
    if (ptr) {
        *ptr = 0;
    }
}

inline bool isNull(const int16_t* ptr) {
    return ptr && *ptr == -1;
}

inline bool isTextBlob(const FieldInfo* field) {
    return field && field->type == SQL_BLOB && field->subType == 1;
}

inline bool isNumericAsString(const FieldInfo* field) {
    if (!field) {
        return false;
    }

    switch (field->type) {
        case SQL_INT128:
        case SQL_DEC16:
        case SQL_DEC34:
        case SQL_TYPE_DATE:
        case SQL_TYPE_TIME:
        case SQL_TIMESTAMP:
        case SQL_TIME_TZ:
        case SQL_TIMESTAMP_TZ:
            return true;
        case SQL_INT64:
            return field->scale != 0;
        default:
            return false;
    }
}

inline ISC_QUAD createBlob(Transaction* transaction, const std::vector<uint8_t>& data) {
    if (!transaction) {
        ISC_QUAD nullId{};
        nullId.gds_quad_high = 0;
        nullId.gds_quad_low = 0;
        return nullId;
    }
    return transaction->createBlob(data);
}

inline int64_t pow10_int(int scale) {
    int64_t result = 1;
    for (int i = 0; i < scale; ++i) {
        result *= 10;
    }
    return result;
}

inline int64_t round_scaled(double value) {
    if (value >= 0.0) {
        return static_cast<int64_t>(value + 0.5);
    }
    return static_cast<int64_t>(value - 0.5);
}

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const T& value, uint8_t* dataPtr);

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const std::optional<T>& value, uint8_t* dataPtr) {
    if (!value.has_value()) {
        setNull(ctx.nullIndicator);
        return;
    }
    write_sql_value(SqlWriteContext{ctx.field, ctx.transaction, ctx.nullIndicator}, *value, dataPtr);
}

template<typename T>
void write_sql_value(const SqlWriteContext& ctx, const T& value, uint8_t* dataPtr) {
    using ValueType = std::decay_t<T>;

    if constexpr (has_type_adapter_v<ValueType>) {
        if constexpr (adapter_has_ctx_to_from_v<ValueType>) {
            int16_t fbScale = ctx.field ? ctx.field->scale : 0;
            unsigned fbLength = ctx.field ? ctx.field->length : sizeof(ValueType);
            adapt_to_firebird_ctx(value, fbScale, fbLength, dataPtr);
            setNotNull(ctx.nullIndicator);
            return;
        } else if constexpr (adapter_has_basic_to_from_v<ValueType>) {
            auto fbValue = adapt_to_firebird(value);
            write_sql_value(SqlWriteContext{ctx.field, ctx.transaction, ctx.nullIndicator}, fbValue, dataPtr);
            return;
        }
    }

    if constexpr (std::is_same_v<ValueType, double>) {
        if (ctx.field && ctx.field->scale < 0) {
            const int scale = -ctx.field->scale;
            const int64_t factor = pow10_int(scale);
            int64_t scaled = round_scaled(value * static_cast<double>(factor));
            if (ctx.field->type == SQL_SHORT || ctx.field->type == (SQL_SHORT | 1)) {
                int16_t v = static_cast<int16_t>(scaled);
                std::memcpy(dataPtr, &v, sizeof(int16_t));
            } else if (ctx.field->type == SQL_LONG || ctx.field->type == (SQL_LONG | 1)) {
                int32_t v = static_cast<int32_t>(scaled);
                std::memcpy(dataPtr, &v, sizeof(int32_t));
            } else {
                std::memcpy(dataPtr, &scaled, sizeof(int64_t));
            }
            setNotNull(ctx.nullIndicator);
            return;
        }
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
        if (isTextBlob(ctx.field)) {
            std::vector<uint8_t> textData(value.begin(), value.end());
            ISC_QUAD blobId = createBlob(ctx.transaction, textData);
            std::memcpy(dataPtr, &blobId, sizeof(ISC_QUAD));
        } else if (ctx.field && ctx.field->type == SQL_TEXT) {
            size_t copyLen = std::min(value.size(), static_cast<size_t>(ctx.field->length));
            std::memcpy(dataPtr, value.data(), copyLen);
            if (copyLen < ctx.field->length) {
                std::memset(dataPtr + copyLen, ' ', ctx.field->length - copyLen);
            }
        } else {
            bool numericString = isNumericAsString(ctx.field);
            if (numericString) {
                uint16_t len = static_cast<uint16_t>(value.length());
                std::memcpy(dataPtr, &len, sizeof(uint16_t));
                std::memcpy(dataPtr + sizeof(uint16_t), value.data(), value.length());
            } else {
                unsigned maxDataLength = ctx.field ? ctx.field->length - sizeof(uint16_t) : 0;
                size_t actualLen = value.length();

                if (maxDataLength > 0 && actualLen > maxDataLength) {
                    auto logger = util::Logging::get();
                    if (logger) {
                        logger->warn("String truncated from {} to {} bytes", actualLen, maxDataLength);
                    }
                    actualLen = maxDataLength;
                }

                uint16_t len = static_cast<uint16_t>(actualLen);
                std::memcpy(dataPtr, &len, sizeof(uint16_t));
                std::memcpy(dataPtr + sizeof(uint16_t), value.data(), actualLen);
            }
        }
    } else if constexpr (std::is_same_v<ValueType, const char*>) {
        if (isTextBlob(ctx.field)) {
            size_t len = std::strlen(value);
            std::vector<uint8_t> textData(value, value + len);
            ISC_QUAD blobId = createBlob(ctx.transaction, textData);
            std::memcpy(dataPtr, &blobId, sizeof(ISC_QUAD));
        } else if (ctx.field && ctx.field->type == SQL_TEXT) {
            size_t strLen = std::strlen(value);
            size_t copyLen = std::min(strLen, static_cast<size_t>(ctx.field->length));
            std::memcpy(dataPtr, value, copyLen);
            if (copyLen < ctx.field->length) {
                std::memset(dataPtr + copyLen, ' ', ctx.field->length - copyLen);
            }
        } else {
            unsigned maxDataLength = ctx.field ? ctx.field->length - sizeof(uint16_t) : 0;
            size_t actualLen = std::strlen(value);

            if (maxDataLength > 0 && actualLen > maxDataLength) {
                auto logger = util::Logging::get();
                if (logger) {
                    logger->warn("C-string truncated from {} to {} bytes", actualLen, maxDataLength);
                }
                actualLen = maxDataLength;
            }

            uint16_t len = static_cast<uint16_t>(actualLen);
            std::memcpy(dataPtr, &len, sizeof(uint16_t));
            std::memcpy(dataPtr + sizeof(uint16_t), value, actualLen);
        }
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        uint8_t boolVal = value ? 1 : 0;
        std::memcpy(dataPtr, &boolVal, 1);
    } else if constexpr (std::is_same_v<ValueType, Int128>) {
        std::memcpy(dataPtr, value.data(), 16);
    } else if constexpr (std::is_same_v<ValueType, DecFloat16>) {
        std::memcpy(dataPtr, value.data(), 8);
    } else if constexpr (std::is_same_v<ValueType, DecFloat34>) {
        std::memcpy(dataPtr, value.data(), 16);
    } else if constexpr (std::is_same_v<ValueType, Timestamp>) {
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        std::memcpy(dataPtr, &date, 4);
        std::memcpy(dataPtr + 4, &time, 4);
    } else if constexpr (std::is_same_v<ValueType, TimestampTz>) {
        uint32_t date = value.getDate();
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(dataPtr, &date, 4);
        std::memcpy(dataPtr + 4, &time, 4);
        std::memcpy(dataPtr + 8, &zone, 2);
        std::memcpy(dataPtr + 10, &offset, 2);
    } else if constexpr (std::is_same_v<ValueType, Time>) {
        uint32_t time = value.getTime();
        std::memcpy(dataPtr, &time, 4);
    } else if constexpr (std::is_same_v<ValueType, TimeTz>) {
        uint32_t time = value.getTime();
        uint16_t zone = value.getZoneId();
        int16_t offset = value.getOffset();
        std::memcpy(dataPtr, &time, 4);
        std::memcpy(dataPtr + 4, &zone, 2);
        std::memcpy(dataPtr + 6, &offset, 2);
    } else if constexpr (std::is_same_v<ValueType, Blob>) {
        std::memcpy(dataPtr, value.getId(), 8);
    } else if constexpr (std::is_same_v<ValueType, TextBlob>) {
        std::memcpy(dataPtr, value.getId(), 8);
    } else {
        std::memcpy(dataPtr, &value, sizeof(ValueType));
    }

    setNotNull(ctx.nullIndicator);
}

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, T& value);

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, std::optional<T>& value) {
    if (isNull(ctx.nullIndicator)) {
        value.reset();
        return;
    }

    T tmp{};
    read_sql_value(SqlReadContext{ctx.field, ctx.transaction, ctx.nullIndicator}, dataPtr, tmp);
    value = std::move(tmp);
}

template<typename T>
void read_sql_value(const SqlReadContext& ctx, const uint8_t* dataPtr, T& value) {
    using ValueType = std::decay_t<T>;

    if (isNull(ctx.nullIndicator)) {
        std::string fieldName = ctx.field ? ctx.field->name : "<unknown>";
        throw FirebirdException("NULL value for non-nullable field: " + fieldName);
    }

    if constexpr (has_type_adapter_v<ValueType>) {
        if constexpr (adapter_has_ctx_to_from_v<ValueType>) {
            int16_t fbScale = ctx.field ? ctx.field->scale : 0;
            unsigned fbLength = ctx.field ? ctx.field->length : sizeof(ValueType);
            value = adapt_from_firebird_ctx<ValueType>(dataPtr, fbLength, fbScale);
            return;
        } else if constexpr (adapter_has_basic_to_from_v<ValueType>) {
            using firebird_type = firebird_equivalent_t<ValueType>;
            firebird_type fbValue{};
            read_sql_value(SqlReadContext{ctx.field, ctx.transaction, ctx.nullIndicator}, dataPtr, fbValue);
            value = adapt_from_firebird<ValueType>(fbValue);
            return;
        }
    }

    if constexpr (std::is_same_v<ValueType, double>) {
        if (ctx.field && ctx.field->scale < 0) {
            const int scale = -ctx.field->scale;
            int64_t raw = 0;
            if (ctx.field->type == SQL_SHORT || ctx.field->type == (SQL_SHORT | 1)) {
                int16_t v{};
                std::memcpy(&v, dataPtr, sizeof(int16_t));
                raw = v;
            } else if (ctx.field->type == SQL_LONG || ctx.field->type == (SQL_LONG | 1)) {
                int32_t v{};
                std::memcpy(&v, dataPtr, sizeof(int32_t));
                raw = v;
            } else {
                std::memcpy(&raw, dataPtr, sizeof(int64_t));
            }
            const int64_t factor = pow10_int(scale);
            value = static_cast<double>(raw) / static_cast<double>(factor);
            return;
        }
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
        if (isTextBlob(ctx.field) && ctx.transaction) {
            ISC_QUAD blobId{};
            std::memcpy(&blobId, dataPtr, sizeof(ISC_QUAD));
            auto blobData = ctx.transaction->loadBlob(&blobId);
            if (!blobData.empty()) {
                value.assign(reinterpret_cast<const char*>(blobData.data()), blobData.size());
            } else {
                value.clear();
            }
        } else {
            uint16_t len{};
            std::memcpy(&len, dataPtr, sizeof(uint16_t));
            value.assign(reinterpret_cast<const char*>(dataPtr + sizeof(uint16_t)), len);
        }
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        uint8_t boolVal{};
        std::memcpy(&boolVal, dataPtr, 1);
        value = boolVal != 0;
    } else if constexpr (std::is_same_v<ValueType, Int128>) {
        value = Int128(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, DecFloat16>) {
        value = DecFloat16(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, DecFloat34>) {
        value = DecFloat34(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, Timestamp>) {
        uint32_t date{};
        uint32_t time{};
        std::memcpy(&date, dataPtr, 4);
        std::memcpy(&time, dataPtr + 4, 4);
        value = Timestamp(date, time);
    } else if constexpr (std::is_same_v<ValueType, TimestampTz>) {
        uint32_t date{};
        uint32_t time{};
        uint16_t zone{};
        int16_t offset{};
        std::memcpy(&date, dataPtr, 4);
        std::memcpy(&time, dataPtr + 4, 4);
        std::memcpy(&zone, dataPtr + 8, 2);
        std::memcpy(&offset, dataPtr + 10, 2);
        value = TimestampTz(date, time, zone, offset);
    } else if constexpr (std::is_same_v<ValueType, Time>) {
        uint32_t time{};
        std::memcpy(&time, dataPtr, 4);
        value = Time(time);
    } else if constexpr (std::is_same_v<ValueType, TimeTz>) {
        uint32_t time{};
        uint16_t zone{};
        int16_t offset{};
        std::memcpy(&time, dataPtr, 4);
        std::memcpy(&zone, dataPtr + 4, 2);
        std::memcpy(&offset, dataPtr + 6, 2);
        value = TimeTz(time, zone, offset);
    } else if constexpr (std::is_same_v<ValueType, Blob>) {
        value = Blob(dataPtr);
    } else if constexpr (std::is_same_v<ValueType, TextBlob>) {
        value = TextBlob(dataPtr);
    } else {
        std::memcpy(&value, dataPtr, sizeof(ValueType));
    }
}

} // namespace fbpp::core::detail::sql_value_codec
