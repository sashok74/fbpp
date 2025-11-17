#include "fbpp/core/connection.hpp"
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/query_executor.hpp"
#include "fbpp/core/struct_pack.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"
#include "fbpp/adapters/chrono_datetime.hpp"
#include <fbpp_util/connection_helper.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace fbpp::core;

namespace {

using Decimal38_8 = fbpp::adapters::TTNumeric<2, -8>;
using Numeric64_6 = fbpp::adapters::TTNumeric<1, -6>;
using MicroTime = std::chrono::hh_mm_ss<std::chrono::microseconds>;
using ZonedMicroTime = std::chrono::zoned_time<std::chrono::microseconds>;
using TimeWithTz = fbpp::core::TimeWithTz;

struct ExtendedTypesInsertInput {
    int32_t id;
    Int128 fInt128;
    Decimal38_8 fDecimal;
    Numeric64_6 fNumeric;
    DecFloat34 fDecfloat;
    std::chrono::year_month_day fDate;
    MicroTime fTime;
    std::chrono::system_clock::time_point fTimestamp;
    ZonedMicroTime fTimestampTz;
    TimeWithTz fTimeTz;
    std::string fVarchar;
    std::string fBlobText;  // TEXT BLOB автоматически сохраняется из строки
};

struct ExtendedTypesFetchInput {
    int32_t id;
};

struct ExtendedTypesOutput {
    int32_t id;
    std::optional<Int128> fInt128;
    std::optional<Decimal38_8> fDecimal;
    std::optional<Numeric64_6> fNumeric;
    std::optional<DecFloat34> fDecfloat;
    std::optional<std::chrono::year_month_day> fDate;
    std::optional<MicroTime> fTime;
    std::optional<std::chrono::system_clock::time_point> fTimestamp;
    std::optional<ZonedMicroTime> fTimestampTz;
    std::optional<TimeWithTz> fTimeTz;
    std::optional<std::string> fVarchar;
    std::optional<std::string> fBlobText;  // TEXT BLOB автоматически читается в строку
};

ConnectionParams loadConnectionParams() {
    auto params = fbpp::util::getConnectionParams("tests.persistent_db");
    std::cout << "Configuration loaded successfully\n";
    std::cout << "  Database: " << params.database << '\n';
    std::cout << "  User: " << params.user << '\n';
    return params;
}

int32_t fetchNextId(Connection& connection) {
    const std::string sql = "SELECT COALESCE(MAX(ID), 0) + 1 FROM TABLE_TEST_1";
    auto statement = connection.prepareStatement(sql);
    auto transaction = connection.StartTransaction();
    auto cursor = transaction->openCursor(statement);
    std::tuple<int32_t> row{};
    if (!cursor->fetch(row)) {
        throw std::runtime_error("TABLE_TEST_1 must be accessible");
    }
    cursor->close();
    transaction->Commit();
    return std::get<0>(row);
}

ExtendedTypesInsertInput makeSampleRow(int32_t newId) {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const year_month_day testDate{year{2024}, month{6}, day{1}};
    const auto timeDuration = 12h + 34min + 56s + 987654us;
    MicroTime testTime{timeDuration};
    const sys_days baseDay{testDate};
    const auto timestampMicros = time_point_cast<microseconds>(baseDay + timeDuration);
    auto tz = std::chrono::locate_zone("Europe/Berlin");
    ZonedMicroTime timestampTz{tz, timestampMicros};
    TimeWithTz timeTz{testTime, std::string("Europe/Berlin")};

    return ExtendedTypesInsertInput{
        newId,
        Int128(987654321099LL),
        Decimal38_8("1234567.678901236789"),
        Numeric64_6("9876.543210"),
        DecFloat34("42.1951234567"),
        testDate,
        testTime,
        timestampMicros,
        timestampTz,
        timeTz,
        "extended_struct_demo",
        "This is TEXT BLOB content with UTF-8 support: Привет мир! 🚀"
    };
}

template<typename T>
std::string formatOptional(const std::optional<T>& value) {
    if (!value) {
        return "null";
    }
    std::ostringstream oss;
    oss << *value;
    return oss.str();
}

std::string formatDate(const std::chrono::year_month_day& value) {
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << static_cast<int>(value.year())
        << '-' << std::setw(2) << static_cast<unsigned>(value.month())
        << '-' << std::setw(2) << static_cast<unsigned>(value.day());
    return oss.str();
}

std::string formatOptionalDate(const std::optional<std::chrono::year_month_day>& value) {
    if (!value) {
        return "null";
    }
    return formatDate(*value);
}

std::string formatMicroTime(const MicroTime& time) {
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << time.hours().count() << ':'
        << std::setw(2) << time.minutes().count() << ':'
        << std::setw(2) << time.seconds().count() << '.'
        << std::setw(6) << time.subseconds().count();
    return oss.str();
}

std::string formatOptionalMicroTime(const std::optional<MicroTime>& value) {
    if (!value) {
        return "null";
    }
    return formatMicroTime(*value);
}

std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto days = std::chrono::floor<std::chrono::days>(tp);
    std::chrono::year_month_day ymd{days};
    auto time = std::chrono::hh_mm_ss{std::chrono::duration_cast<std::chrono::microseconds>(tp - days)};

    std::ostringstream oss;
    oss << formatDate(ymd) << ' ' << formatMicroTime(time);
    return oss.str();
}

std::string formatOptionalTimestamp(const std::optional<std::chrono::system_clock::time_point>& value) {
    if (!value) {
        return "null";
    }
    return formatTimestamp(*value);
}

std::string formatZonedTime(const ZonedMicroTime& value) {
    auto sysTime = value.get_sys_time();
    std::ostringstream oss;
    oss << formatTimestamp(sysTime) << " [" << value.get_time_zone()->name() << ']';
    return oss.str();
}

std::string formatOptionalZonedTime(const std::optional<ZonedMicroTime>& value) {
    if (!value) {
        return "null";
    }
    return formatZonedTime(*value);
}

std::string formatTimeWithTz(const TimeWithTz& value) {
    return formatMicroTime(value.first) + " @" + value.second;
}

std::string formatOptionalTimeWithTz(const std::optional<TimeWithTz>& value) {
    if (!value) {
        return "null";
    }
    return formatTimeWithTz(*value);
}

static std::pair<bool, std::string> decimalMagnitude(const Int128& value) {
    std::array<uint8_t, 16> raw{};
    std::memcpy(raw.data(), value.data(), raw.size());

    bool negative = (raw[15] & 0x80) != 0;
    if (negative) {
        uint8_t carry = 1;
        for (size_t i = 0; i < raw.size(); ++i) {
            raw[i] = static_cast<uint8_t>(~raw[i]);
            uint16_t sum = static_cast<uint16_t>(raw[i]) + carry;
            raw[i] = static_cast<uint8_t>(sum);
            carry = static_cast<uint8_t>(sum >> 8);
        }
    }

    std::array<uint32_t, 4> limbs{};
    for (int i = 0; i < 4; ++i) {
        size_t base = static_cast<size_t>(i) * 4;
        limbs[i] = static_cast<uint32_t>(raw[base]) |
                   (static_cast<uint32_t>(raw[base + 1]) << 8) |
                   (static_cast<uint32_t>(raw[base + 2]) << 16) |
                   (static_cast<uint32_t>(raw[base + 3]) << 24);
    }

    auto isZero = [&limbs]() {
        return std::all_of(limbs.begin(), limbs.end(), [](uint32_t v) { return v == 0; });
    };

    if (isZero()) {
        return {false, "0"};
    }

    std::string digits;
    digits.reserve(40);
    while (!isZero()) {
        uint64_t carry = 0;
        for (int idx = 3; idx >= 0; --idx) {
            uint64_t current = (carry << 32) | limbs[idx];
            limbs[idx] = static_cast<uint32_t>(current / 10);
            carry = current % 10;
        }
        digits.push_back(static_cast<char>('0' + carry));
    }

    std::reverse(digits.begin(), digits.end());
    if (digits == "0") {
        negative = false;
    }
    return {negative, digits};
}

std::string formatInt128Value(const Int128& value, int scale) {
    auto [negative, magnitude] = decimalMagnitude(value);
    int fractionalDigits = scale < 0 ? -scale : 0;
    std::string result;

    if (negative) {
        result.push_back('-');
    }

    if (fractionalDigits == 0) {
        result += magnitude;
        return result;
    }

    if (static_cast<int>(magnitude.size()) <= fractionalDigits) {
        result += "0.";
        result.append(static_cast<std::size_t>(fractionalDigits - static_cast<int>(magnitude.size())), '0');
        result += magnitude;
        return result;
    }

    const std::size_t splitPos = magnitude.size() - static_cast<std::size_t>(fractionalDigits);
    result.append(magnitude, 0, splitPos);
    result += '.';
    result.append(magnitude, splitPos, std::string::npos);
    return result;
}

std::string formatOptionalInt128(const std::optional<Int128>& value, int scale) {
    if (!value) {
        return "null";
    }
    return formatInt128Value(*value, scale);
}

std::string formatOptionalDecimal(const std::optional<Decimal38_8>& value) {
    if (!value) {
        return "null";
    }
    return value->to_string();
}

std::string formatOptionalNumeric(const std::optional<Numeric64_6>& value) {
    if (!value) {
        return "null";
    }
    return value->to_string();
}

std::string formatOptionalDecFloat(const std::optional<DecFloat34>& value) {
    if (!value) {
        return "null";
    }
    return value->to_string();
}

std::string formatOptionalTextBlob(const std::optional<std::string>& value) {
    if (!value) {
        return "null";
    }
    // TEXT BLOB уже загружен в строку, показываем длину и превью
    const auto& text = *value;
    std::ostringstream oss;
    oss << "\"" << text.size() << " bytes: ";
    if (text.size() > 50) {
        oss << text.substr(0, 50) << "...";
    } else {
        oss << text;
    }
    oss << "\"";
    return oss.str();
}

void printResult(const ExtendedTypesOutput& row) {
    std::cout << "\nFetched row id=" << row.id << '\n';
    std::cout << "  F_INT128: " << formatOptionalInt128(row.fInt128, 0) << '\n';
    std::cout << "  F_DECIMAL: " << formatOptionalDecimal(row.fDecimal) << '\n';
    std::cout << "  F_NUMERIC: " << formatOptionalNumeric(row.fNumeric) << '\n';
    std::cout << "  F_DECFLOAT: " << formatOptionalDecFloat(row.fDecfloat) << '\n';
    std::cout << "  F_DATE: " << formatOptionalDate(row.fDate) << '\n';
    std::cout << "  F_TIME: " << formatOptionalMicroTime(row.fTime) << '\n';
    std::cout << "  F_TIMESHTAMP: " << formatOptionalTimestamp(row.fTimestamp) << '\n';
    std::cout << "  F_TIMESHTAMP_TZ: " << formatOptionalZonedTime(row.fTimestampTz) << '\n';
    std::cout << "  F_TIME_TZ: " << formatOptionalTimeWithTz(row.fTimeTz) << '\n';
    std::cout << "  F_VARCHAR: " << (row.fVarchar ? *row.fVarchar : "null") << '\n';
    std::cout << "  F_BLOB_T: " << formatOptionalTextBlob(row.fBlobText) << '\n';
}

} // namespace

namespace fbpp::core {

template<>
struct StructDescriptor<::ExtendedTypesInsertInput> {
    static constexpr bool is_specialized = true;
    static constexpr const char* name = "EXTENDED_TYPES_INSERT_INPUT";

    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesInsertInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), 0, false),
        makeField<&ExtendedTypesInsertInput::fInt128>("F_INT128", SQL_INT128, 0, 16, 0, false),
        makeField<&ExtendedTypesInsertInput::fDecimal>("F_DECIMAL", SQL_INT128, -8, 16, 0, false),
        makeField<&ExtendedTypesInsertInput::fNumeric>("F_NUMERIC", SQL_INT64, -6, sizeof(int64_t), 0, false),
        makeField<&ExtendedTypesInsertInput::fDecfloat>("F_DECFLOAT", SQL_DEC34, 0, 16, 0, false),
        makeField<&ExtendedTypesInsertInput::fDate>("F_DATE", SQL_TYPE_DATE, 0, sizeof(uint32_t), 0, false),
        makeField<&ExtendedTypesInsertInput::fTime>("F_TIME", SQL_TYPE_TIME, 0, sizeof(uint32_t), 0, false),
        makeField<&ExtendedTypesInsertInput::fTimestamp>("F_TIMESHTAMP", SQL_TIMESTAMP, 0, 8, 0, false),
        makeField<&ExtendedTypesInsertInput::fTimestampTz>("F_TIMESHTAMP_TZ", SQL_TIMESTAMP_TZ, 0, 12, 0, false),
        makeField<&ExtendedTypesInsertInput::fTimeTz>("F_TIME_TZ", SQL_TIME_TZ, 0, 8, 0, false),
        makeField<&ExtendedTypesInsertInput::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 66, 0, false),
        makeField<&ExtendedTypesInsertInput::fBlobText>("F_BLOB_T", SQL_BLOB, 0, 8, 1, false)  // subType=1 для TEXT BLOB
    );

    static constexpr size_t fieldCount = std::tuple_size_v<decltype(fields)>;
};

template<>
struct StructDescriptor<::ExtendedTypesFetchInput> {
    static constexpr bool is_specialized = true;
    static constexpr const char* name = "EXTENDED_TYPES_FETCH_INPUT";

    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesFetchInput::id>("ID", SQL_LONG, 0, sizeof(int32_t), 0, false)
    );

    static constexpr size_t fieldCount = std::tuple_size_v<decltype(fields)>;
};

template<>
struct StructDescriptor<::ExtendedTypesOutput> {
    static constexpr bool is_specialized = true;
    static constexpr const char* name = "EXTENDED_TYPES_OUTPUT";

    static constexpr auto fields = std::make_tuple(
        makeField<&ExtendedTypesOutput::id>("ID", SQL_LONG, 0, sizeof(int32_t), 0, false),
        makeField<&ExtendedTypesOutput::fInt128>("F_INT128", SQL_INT128, 0, 16, 0, true),
        makeField<&ExtendedTypesOutput::fDecimal>("F_DECIMAL", SQL_INT128, -8, 16, 0, true),
        makeField<&ExtendedTypesOutput::fNumeric>("F_NUMERIC", SQL_INT64, -6, sizeof(int64_t), 0, true),
        makeField<&ExtendedTypesOutput::fDecfloat>("F_DECFLOAT", SQL_DEC34, 0, 16, 0, true),
        makeField<&ExtendedTypesOutput::fDate>("F_DATE", SQL_TYPE_DATE, 0, sizeof(uint32_t), 0, true),
        makeField<&ExtendedTypesOutput::fTime>("F_TIME", SQL_TYPE_TIME, 0, sizeof(uint32_t), 0, true),
        makeField<&ExtendedTypesOutput::fTimestamp>("F_TIMESHTAMP", SQL_TIMESTAMP, 0, 8, 0, true),
        makeField<&ExtendedTypesOutput::fTimestampTz>("F_TIMESHTAMP_TZ", SQL_TIMESTAMP_TZ, 0, 12, 0, true),
        makeField<&ExtendedTypesOutput::fTimeTz>("F_TIME_TZ", SQL_TIME_TZ, 0, 8, 0, true),
        makeField<&ExtendedTypesOutput::fVarchar>("F_VARCHAR", SQL_VARYING, 0, 66, 0, true),
        makeField<&ExtendedTypesOutput::fBlobText>("F_BLOB_T", SQL_BLOB, 0, 8, 1, true)  // subType=1 для TEXT BLOB
    );

    static constexpr size_t fieldCount = std::tuple_size_v<decltype(fields)>;
};

} // namespace fbpp::core

namespace local {

enum class QueryId { InsertExtended, FetchExtended, DeleteExtended };

template<QueryId>
struct QueryDescriptor;

template<>
struct QueryDescriptor<QueryId::InsertExtended> {
    static constexpr std::string_view sql =
        "INSERT INTO TABLE_TEST_1 ("
        "ID, F_INT128, F_DECIMAL, F_NUMERIC, F_DECFLOAT, "
        "F_DATE, F_TIME, F_TIMESHTAMP, F_TIMESHTAMP_TZ, F_TIME_TZ, F_VARCHAR, F_BLOB_T"
        ") VALUES (:id, :fInt128, :fDecimal, :fNumeric, :fDecfloat, "
        ":fDate, :fTime, :fTimestamp, :fTimestampTz, :fTimeTz, :fVarchar, :fBlobText)";
    using Input = ExtendedTypesInsertInput;
    using Output = fbpp::core::NoResult;
};

template<>
struct QueryDescriptor<QueryId::FetchExtended> {
    static constexpr std::string_view sql =
        "SELECT ID, F_INT128, F_DECIMAL, F_NUMERIC, F_DECFLOAT, "
        "F_DATE, F_TIME, F_TIMESHTAMP, F_TIMESHTAMP_TZ, F_TIME_TZ, F_VARCHAR, F_BLOB_T "
        "FROM TABLE_TEST_1 WHERE ID = :id";
    using Input = ExtendedTypesFetchInput;
    using Output = ExtendedTypesOutput;
};

template<>
struct QueryDescriptor<QueryId::DeleteExtended> {
    static constexpr std::string_view sql =
        "DELETE FROM TABLE_TEST_1 WHERE ID = :id";
    using Input = ExtendedTypesFetchInput;
    using Output = fbpp::core::NoResult;
};

} // namespace local

int main() {
    try {
        auto params = loadConnectionParams();
        auto connection = std::make_unique<Connection>(params);

        const int32_t newId = fetchNextId(*connection);
        auto insertRow = makeSampleRow(newId);

        std::cout << "Inserting row id=" << insertRow.id << " with label '" << insertRow.fVarchar << "'\n";

        auto insertTra = connection->StartTransaction();
        insertTra->executeNonQuery<local::QueryDescriptor<local::QueryId::InsertExtended>>(insertRow);
        insertTra->Commit();

        auto fetchTra = connection->StartTransaction();
        auto rows = fetchTra->executeQuery<local::QueryDescriptor<local::QueryId::FetchExtended>>(
            ExtendedTypesFetchInput{newId});
        fetchTra->Commit();

        if (rows.empty()) {
            std::cout << "Row was not found after insert!\n";
        } else {
            printResult(rows.front());
        }

      //  auto cleanupTra = connection->StartTransaction();
      //  executeNonQuery<local::QueryDescriptor<local::QueryId::DeleteExtended>>(
      //      *connection, *cleanupTra, ExtendedTypesFetchInput{newId});
      //  cleanupTra->Commit();

      // std::cout << "Cleanup completed, demo finished.\n";
        return 0;
    } catch (const FirebirdException& e) {
        std::cerr << "Firebird exception: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
