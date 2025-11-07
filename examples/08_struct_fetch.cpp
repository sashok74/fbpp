#include "fbpp/core/connection.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/query_executor.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp/core/timestamp_utils.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace fbpp::core;

namespace {

constexpr int16_t kDecimalScale = -8;
constexpr int16_t kNumericScale = -6;

struct FetchInput {};

struct TableRow {
    std::int32_t id;
    std::optional<std::int64_t> fBigint;
    std::optional<bool> fBoolean;
    std::optional<std::string> fChar;
    std::optional<Date> fDate;
    std::optional<DecFloat34> fDecfloat;
    std::optional<Int128> fDecimal;
    std::optional<double> fDoublePrecision;
    std::optional<float> fFloat;
    std::optional<Int128> fInt128;
    std::optional<std::int32_t> fInteger;
    std::optional<double> fNumeric;
    std::optional<std::int16_t> fSmalint;
    std::optional<Time> fTime;
    std::optional<TimeTz> fTimeTz;
    std::optional<Timestamp> fTimestamp;
    std::optional<TimestampTz> fTimestampTz;
    std::optional<std::string> fVarchar;
    std::optional<Blob> fBlobBinary;
    std::optional<TextBlob> fBlobText;
    std::optional<std::int32_t> fNull;
};

enum class QueryId {
    FetchTop100
};

template<QueryId>
struct QueryDescriptor;

template<>
struct QueryDescriptor<QueryId::FetchTop100> {
    static constexpr std::string_view sql =
        "SELECT FIRST 100 "
        "ID, F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, F_DECIMAL, "
        "F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, F_NUMERIC, "
        "F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, F_TIMESHTAMP_TZ, "
        "F_VARCHAR, F_BLOB_B, F_BLOB_T, F_NULL "
        "FROM TABLE_TEST_1 ORDER BY ID";
    using Input = FetchInput;
    using Output = TableRow;
};

template<typename T>
std::string quoteString(const T& value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (char ch : value) {
        switch (ch) {
            case '\\':
            case '"':
                result.push_back('\\');
                result.push_back(ch);
                break;
            case '\n':
                result.append("\\n");
                break;
            case '\r':
                result.append("\\r");
                break;
            case '\t':
                result.append("\\t");
                break;
            default:
                result.push_back(ch);
                break;
        }
    }
    result.push_back('"');
    return result;
}

template<typename T>
std::string formatOptionalNumber(const std::optional<T>& value) {
    if (!value) {
        return "null";
    }

    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        return std::to_string(static_cast<long long>(*value));
    } else {
        std::ostringstream oss;
        oss << *value;
        return oss.str();
    }
}

std::string formatOptionalScaledDouble(const std::optional<double>& value, int scale) {
    if (!value) {
        return "null";
    }
    std::ostringstream oss;
    if (scale < 0) {
        oss << std::fixed << std::setprecision(-scale);
    }
    oss << *value;
    return oss.str();
}

std::string formatOptionalBool(const std::optional<bool>& value) {
    if (!value) {
        return "null";
    }
    return *value ? "true" : "false";
}

std::string formatOptionalString(const std::optional<std::string>& value) {
    if (!value) {
        return "null";
    }
    return quoteString(*value);
}

std::string formatOptionalChar(const std::optional<std::string>& value) {
    if (!value) {
        return "null";
    }
    std::string trimmed = *value;
    auto it = std::find_if(trimmed.rbegin(),
                           trimmed.rend(),
                           [](unsigned char ch) { return ch != ' '; });
    if (it == trimmed.rend()) {
        trimmed.clear();
    } else {
        trimmed.erase(it.base(), trimmed.end());
    }
    return quoteString(trimmed);
}

std::string formatDate(const Date& value) {
    auto ymd = timestamp_utils::from_firebird_date(value.getDate());
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << static_cast<int>(ymd.year())
        << '-' << std::setw(2) << static_cast<unsigned>(ymd.month())
        << '-' << std::setw(2) << static_cast<unsigned>(ymd.day());
    return oss.str();
}

std::string formatOptionalDate(const std::optional<Date>& value) {
    if (!value) {
        return "null";
    }
    return formatDate(*value);
}

std::string formatTimestamp(const Timestamp& value) {
    auto tp = timestamp_utils::from_firebird_timestamp(value.getDate(), value.getTime());
    return timestamp_utils::format_iso8601(tp);
}

std::string formatOptionalTimestamp(const std::optional<Timestamp>& value) {
    if (!value) {
        return "null";
    }
    return formatTimestamp(*value);
}

std::string formatTime(const Time& value) {
    auto duration = timestamp_utils::from_firebird_time(value.getTime());
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= std::chrono::duration_cast<std::chrono::microseconds>(hours);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= std::chrono::duration_cast<std::chrono::microseconds>(minutes);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    duration -= std::chrono::duration_cast<std::chrono::microseconds>(seconds);
    auto micros = duration.count();

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << static_cast<int>(hours.count())
        << ':' << std::setw(2) << static_cast<int>(minutes.count())
        << ':' << std::setw(2) << static_cast<int>(seconds.count());
    if (micros > 0) {
        oss << '.' << std::setw(6) << micros;
    }
    return oss.str();
}

std::string formatOptionalTime(const std::optional<Time>& value) {
    if (!value) {
        return "null";
    }
    return formatTime(*value);
}

std::string formatUtcOffset(int16_t offsetMinutes) {
    char sign = offsetMinutes >= 0 ? '+' : '-';
    int total = offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes;
    int hours = total / 60;
    int minutes = total % 60;
    std::ostringstream oss;
    oss << sign
        << std::setfill('0') << std::setw(2) << hours
        << ':' << std::setw(2) << minutes;
    return oss.str();
}

std::string formatTimeTz(const TimeTz& value) {
    std::ostringstream oss;
    oss << formatTime(Time(value.getTime()))
        << " (zone=" << value.getZoneId()
        << ", offset=" << formatUtcOffset(value.getOffset()) << ')';
    return oss.str();
}

std::string formatOptionalTimeTz(const std::optional<TimeTz>& value) {
    if (!value) {
        return "null";
    }
    return formatTimeTz(*value);
}

std::string formatTimestampTz(const TimestampTz& value) {
    std::ostringstream oss;
    oss << formatTimestamp(Timestamp(value.getDate(), value.getTime()))
        << " (zone=" << value.getZoneId()
        << ", offset=" << formatUtcOffset(value.getOffset()) << ')';
    return oss.str();
}

std::string formatOptionalTimestampTz(const std::optional<TimestampTz>& value) {
    if (!value) {
        return "null";
    }
    return formatTimestampTz(*value);
}

std::string formatOptionalDecFloat(const std::optional<DecFloat34>& value) {
    if (!value) {
        return "null";
    }
    return value->to_string();
}

std::string toUnsignedString(unsigned __int128 value) {
    if (value == 0) {
        return "0";
    }

    std::string digits;
    while (value > 0) {
        unsigned digit = static_cast<unsigned>(value % 10);
        digits.push_back(static_cast<char>('0' + digit));
        value /= 10;
    }
    std::reverse(digits.begin(), digits.end());
    return digits;
}

std::string formatInt128Value(const Int128& value, int scale) {
    static_assert(sizeof(unsigned __int128) == 16, "__int128 support required");

    __int128 signedValue{};
    std::memcpy(&signedValue, value.data(), sizeof(signedValue));

    bool negative = signedValue < 0;
    unsigned __int128 magnitude = negative
        ? static_cast<unsigned __int128>(-signedValue)
        : static_cast<unsigned __int128>(signedValue);

    int fractionalDigits = scale < 0 ? -scale : 0;
    unsigned __int128 scaleFactor = 1;
    for (int i = 0; i < fractionalDigits; ++i) {
        scaleFactor *= 10;
    }

    std::ostringstream oss;
    if (negative && magnitude != 0) {
        oss << '-';
    }

    if (fractionalDigits == 0) {
        oss << toUnsignedString(magnitude);
        return oss.str();
    }

    unsigned __int128 integerPart = magnitude / scaleFactor;
    unsigned __int128 fractionalPart = magnitude % scaleFactor;

    oss << toUnsignedString(integerPart) << '.';
    std::string fractional = toUnsignedString(fractionalPart);
    if (static_cast<int>(fractional.size()) < fractionalDigits) {
        oss << std::string(static_cast<std::size_t>(fractionalDigits - static_cast<int>(fractional.size())), '0');
    }
    oss << fractional;
    return oss.str();
}

std::string formatOptionalInt128(const std::optional<Int128>& value, int scale) {
    if (!value) {
        return "null";
    }
    return formatInt128Value(*value, scale);
}

std::string formatBlobIdString(const Blob& blob) {
    uint32_t idParts[2]{};
    std::memcpy(idParts, blob.getId(), sizeof(idParts));

    std::ostringstream oss;
    oss << "0x"
        << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << idParts[0]
        << ':' << std::setw(8) << idParts[1]
        << std::dec;
    return oss.str();
}

std::string formatOptionalBlob(const std::optional<Blob>& value) {
    if (!value) {
        return "null";
    }
    return "Blob{id=" + formatBlobIdString(*value) + "}";
}

std::string formatOptionalTextBlob(const std::optional<TextBlob>& value) {
    if (!value) {
        return "null";
    }
    std::ostringstream oss;
    oss << "TextBlob{id=" << formatBlobIdString(*value)
        << ", cached=" << (value->hasText() ? "true" : "false");
    if (value->hasText()) {
        std::string preview = value->getText();
        if (preview.size() > 32) {
            preview = preview.substr(0, 32) + "...";
        }
        oss << ", text=" << quoteString(preview);
    }
    oss << '}';
    return oss.str();
}

} // namespace

namespace fbpp::core {

template<>
struct StructDescriptor<::FetchInput> {
    static constexpr auto fields = std::make_tuple();
};

template<>
struct StructDescriptor<::TableRow> {
    static constexpr auto fields = std::make_tuple(
        makeField<&TableRow::id>("ID", SQL_LONG, 0, sizeof(std::int32_t), false),
        makeField<&TableRow::fBigint>("F_BIGINT", SQL_INT64, 0, sizeof(std::int64_t), true),
        makeField<&TableRow::fBoolean>("F_BOOLEAN", SQL_BOOLEAN, 0, 1, true),
        makeField<&TableRow::fChar>("F_CHAR", SQL_TEXT, 0, 10, true),
        makeField<&TableRow::fDate>("F_DATE", SQL_TYPE_DATE, 0, sizeof(uint32_t), true),
        makeField<&TableRow::fDecfloat>("F_DECFLOAT", SQL_DEC34, 0, 16, true),
        makeField<&TableRow::fDecimal>("F_DECIMAL", SQL_INT128, ::kDecimalScale, 16, true),
        makeField<&TableRow::fDoublePrecision>("F_DOUBLE_PRECISION", SQL_DOUBLE, 0, sizeof(double), true),
        makeField<&TableRow::fFloat>("F_FLOAT", SQL_FLOAT, 0, sizeof(float), true),
        makeField<&TableRow::fInt128>("F_INT128", SQL_INT128, 0, 16, true),
        makeField<&TableRow::fInteger>("F_INTEGER", SQL_LONG, 0, sizeof(std::int32_t), true),
        makeField<&TableRow::fNumeric>("F_NUMERIC", SQL_INT64, ::kNumericScale, sizeof(std::int64_t), true),
        makeField<&TableRow::fSmalint>("F_SMALINT", SQL_SHORT, 0, sizeof(std::int16_t), true),
        makeField<&TableRow::fTime>("F_TIME", SQL_TYPE_TIME, 0, sizeof(uint32_t), true),
        makeField<&TableRow::fTimeTz>("F_TIME_TZ", SQL_TIME_TZ, 0, 8, true),
        makeField<&TableRow::fTimestamp>("F_TIMESHTAMP", SQL_TIMESTAMP, 0, 8, true),
        makeField<&TableRow::fTimestampTz>("F_TIMESHTAMP_TZ", SQL_TIMESTAMP_TZ, 0, 12, true),
        makeField<&TableRow::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 66, true),
        makeField<&TableRow::fBlobBinary>("F_BLOB_B", SQL_BLOB, 0, 8, true),
        makeField<&TableRow::fBlobText>("F_BLOB_T", SQL_BLOB, 0, 8, true, 1),
        makeField<&TableRow::fNull>("F_NULL", SQL_LONG, 0, sizeof(std::int32_t), true));
};

} // namespace fbpp::core

int main() {
    try {
        std::vector<std::string> configPaths = {
            "../../config/test_config.json",
            "../config/test_config.json",
            "config/test_config.json",
            "./test_config.json"
        };

        std::ifstream configFile;
        for (const auto& path : configPaths) {
            configFile.open(path);
            if (configFile.is_open()) {
                std::cout << "Configuration loaded from: " << path << '\n';
                break;
            }
        }

        if (!configFile.is_open()) {
            std::cerr << "Could not find test_config.json\n";
            return 1;
        }

        auto config = nlohmann::json::parse(configFile);
        auto dbConfig = config["tests"]["persistent_db"];

        ConnectionParams params;
        params.database = dbConfig["server"].get<std::string>() + ":" + dbConfig["path"].get<std::string>();
        params.user = dbConfig["user"].get<std::string>();
        params.password = dbConfig["password"].get<std::string>();
        params.charset = dbConfig["charset"].get<std::string>();

        auto connection = std::make_unique<Connection>(params);
        auto transaction = connection->StartTransaction();

        auto rows = executeQuery<QueryDescriptor<QueryId::FetchTop100>>(
            *connection, *transaction, FetchInput{});
        transaction->Commit();

        std::cout << "Fetched " << rows.size() << " rows from TABLE_TEST_1\n";

        const std::size_t previewCount = std::min<std::size_t>(rows.size(), 5);
        for (std::size_t i = 0; i < previewCount; ++i) {
            const auto& row = rows[i];
            std::cout << "Row #" << i + 1 << '\n';
            std::cout << "  ID: " << row.id << '\n';
            std::cout << "  F_BIGINT: " << formatOptionalNumber(row.fBigint) << '\n';
            std::cout << "  F_BOOLEAN: " << formatOptionalBool(row.fBoolean) << '\n';
            std::cout << "  F_CHAR: " << formatOptionalChar(row.fChar) << '\n';
            std::cout << "  F_DATE: " << formatOptionalDate(row.fDate) << '\n';
            std::cout << "  F_DECFLOAT: " << formatOptionalDecFloat(row.fDecfloat) << '\n';
            std::cout << "  F_DECIMAL: " << formatOptionalInt128(row.fDecimal, ::kDecimalScale) << '\n';
            std::cout << "  F_DOUBLE_PRECISION: " << formatOptionalNumber(row.fDoublePrecision) << '\n';
            std::cout << "  F_FLOAT: " << formatOptionalNumber(row.fFloat) << '\n';
            std::cout << "  F_INT128: " << formatOptionalInt128(row.fInt128, 0) << '\n';
            std::cout << "  F_INTEGER: " << formatOptionalNumber(row.fInteger) << '\n';
            std::cout << "  F_NUMERIC: " << formatOptionalScaledDouble(row.fNumeric, ::kNumericScale) << '\n';
            std::cout << "  F_SMALINT: " << formatOptionalNumber(row.fSmalint) << '\n';
            std::cout << "  F_TIME: " << formatOptionalTime(row.fTime) << '\n';
            std::cout << "  F_TIME_TZ: " << formatOptionalTimeTz(row.fTimeTz) << '\n';
            std::cout << "  F_TIMESHTAMP: " << formatOptionalTimestamp(row.fTimestamp) << '\n';
            std::cout << "  F_TIMESHTAMP_TZ: " << formatOptionalTimestampTz(row.fTimestampTz) << '\n';
            std::cout << "  F_VARCHAR: " << formatOptionalString(row.fVarchar) << '\n';
            std::cout << "  F_BLOB_B: " << formatOptionalBlob(row.fBlobBinary) << '\n';
            std::cout << "  F_BLOB_T: " << formatOptionalTextBlob(row.fBlobText) << '\n';
            std::cout << "  F_NULL: " << formatOptionalNumber(row.fNull) << '\n';
            std::cout << '\n';
        }

        if (rows.empty()) {
            std::cout << "TABLE_TEST_1 contains no records.\n";
        }
        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "FirebirdException: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}

