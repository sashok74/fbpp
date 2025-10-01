#pragma once

#include "fbpp/core/timestamp_utils.hpp"
#include <cstdint>
#include <string>
#include <cstring>
#include <array>
#include <chrono>

namespace fbpp::core {

/**
 * @brief 128-bit integer type for Firebird INT128
 *
 * Minimal wrapper for raw data storage and Firebird API integration.
 * For conversions use IUtil interface from Firebird API.
 */
class Int128 {
public:
    // Default constructor
    Int128() { std::memset(data_.data(), 0, 16); }

    // Construct from int64_t with sign extension
    explicit Int128(int64_t value) {
        std::memset(data_.data(), 0, 16);
        std::memcpy(data_.data(), &value, sizeof(int64_t));
        // Sign extend if negative
        if (value < 0) {
            std::memset(data_.data() + 8, 0xFF, 8);
        }
    }

    // Construct from raw bytes (16 bytes, little-endian)
    explicit Int128(const uint8_t* bytes) {
        std::memcpy(data_.data(), bytes, 16);
    }

    // Get raw bytes for Firebird API (little-endian)
    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }

    // Equality comparison for testing
    bool operator==(const Int128& other) const {
        return std::memcmp(data_.data(), other.data_.data(), 16) == 0;
    }

    bool operator!=(const Int128& other) const {
        return !(*this == other);
    }

private:
    std::array<uint8_t, 16> data_;  // Little-endian storage
};

/**
 * @brief DECFLOAT(16) - Decimal64 IEEE 754-2008
 *
 * Minimal wrapper for raw data storage. Use IDecFloat16 interface for conversions.
 */
class DecFloat16 {
public:
    // Default constructor
    DecFloat16() { std::memset(data_.data(), 0, 8); }

    // Construct from raw bytes (8 bytes)
    explicit DecFloat16(const uint8_t* bytes) {
        std::memcpy(data_.data(), bytes, 8);
    }

    // Construct from double value
    explicit DecFloat16(double value);

    // Construct from string
    explicit DecFloat16(const std::string& str);
    explicit DecFloat16(const char* str);

    // Get raw bytes for Firebird API
    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }

    // Convert to string representation
    std::string to_string() const;

    // Equality comparison for testing
    bool operator==(const DecFloat16& other) const {
        return std::memcmp(data_.data(), other.data_.data(), 8) == 0;
    }

    bool operator!=(const DecFloat16& other) const {
        return !(*this == other);
    }

private:
    std::array<uint8_t, 8> data_;
};

/**
 * @brief DECFLOAT(34) - Decimal128 IEEE 754-2008
 *
 * Minimal wrapper for raw data storage. Use IDecFloat34 interface for conversions.
 */
class DecFloat34 {
public:
    // Default constructor
    DecFloat34() { std::memset(data_.data(), 0, 16); }

    // Construct from raw bytes (16 bytes)
    explicit DecFloat34(const uint8_t* bytes) {
        std::memcpy(data_.data(), bytes, 16);
    }

    // Construct from double value
    explicit DecFloat34(double value);

    // Construct from string
    explicit DecFloat34(const std::string& str);
    explicit DecFloat34(const char* str);

    // Get raw bytes for Firebird API
    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }

    // Convert to string representation
    std::string to_string() const;

    // Equality comparison for testing
    bool operator==(const DecFloat34& other) const {
        return std::memcmp(data_.data(), other.data_.data(), 16) == 0;
    }

    bool operator!=(const DecFloat34& other) const {
        return !(*this == other);
    }

private:
    std::array<uint8_t, 16> data_;
};

/**
 * @brief Date type for Firebird DATE
 *
 * Minimal wrapper for ISC_DATE. Use timestamp_utils for conversions.
 */
class Date {
public:
    // Default constructor
    Date() : date_(0) {}

    // Construct from ISC_DATE
    explicit Date(uint32_t isc_date) : date_(isc_date) {}

    // Construct from year, month, day
    Date(int year, unsigned month, unsigned day) {
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = static_cast<int>(month) - 1;
        tm.tm_mday = static_cast<int>(day);
        tm.tm_isdst = -1;

        auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        auto [fb_date, _] = timestamp_utils::to_firebird_timestamp(tp);
        date_ = fb_date;
    }

    // Construct from time_point (truncates time)
    explicit Date(std::chrono::system_clock::time_point tp) {
        auto [fb_date, _] = timestamp_utils::to_firebird_timestamp(tp);
        date_ = fb_date;
    }

    // Get raw ISC_DATE value for Firebird API
    uint32_t getDate() const { return date_; }

    // Basic comparison for testing
    bool operator==(const Date& other) const { return date_ == other.date_; }
    bool operator!=(const Date& other) const { return date_ != other.date_; }

private:
    uint32_t date_;  // ISC_DATE - days since 17.11.1858
};

/**
 * @brief Timestamp type for Firebird TIMESTAMP
 *
 * Minimal wrapper for ISC_TIMESTAMP. Use timestamp_utils for conversions.
 */
class Timestamp {
public:
    // Default constructor
    Timestamp() : date_(0), time_(0) {}

    // Construct from date and time components
    Timestamp(uint32_t date, uint32_t time) : date_(date), time_(time) {}

    // Construct from std::chrono::system_clock::time_point
    explicit Timestamp(std::chrono::system_clock::time_point tp) {
        auto [fb_date, fb_time] = timestamp_utils::to_firebird_timestamp(tp);
        date_ = fb_date;
        time_ = fb_time;
    }

    // Get components for Firebird API
    uint32_t getDate() const { return date_; }
    uint32_t getTime() const { return time_; }

private:
    uint32_t date_;  // ISC_DATE - days since 17.11.1858
    uint32_t time_;  // ISC_TIME - fractions of day in 1/10000 sec
};

/**
 * @brief Timestamp with timezone for Firebird TIMESTAMP WITH TIME ZONE
 *
 * Minimal wrapper for ISC_TIMESTAMP_TZ. Use IUtil for conversions.
 */
class TimestampTz {
public:
    TimestampTz() : date_(0), time_(0), zone_id_(0), offset_(0) {}

    TimestampTz(uint32_t date, uint32_t time, uint16_t zone_id, int16_t offset)
        : date_(date), time_(time), zone_id_(zone_id), offset_(offset) {}

    // Construct from Timestamp and timezone info
    TimestampTz(const Timestamp& ts, uint16_t zone_id, int16_t offset)
        : date_(ts.getDate()), time_(ts.getTime()), zone_id_(zone_id), offset_(offset) {}

    // Construct from time_point and timezone info
    TimestampTz(std::chrono::system_clock::time_point tp, uint16_t zone_id, int16_t offset) {
        auto [fb_date, fb_time] = timestamp_utils::to_firebird_timestamp(tp);
        date_ = fb_date;
        time_ = fb_time;
        zone_id_ = zone_id;
        offset_ = offset;
    }

    // Get components for Firebird API
    uint32_t getDate() const { return date_; }
    uint32_t getTime() const { return time_; }
    uint16_t getZoneId() const { return zone_id_; }
    int16_t getOffset() const { return offset_; }

private:
    uint32_t date_;       // ISC_DATE
    uint32_t time_;       // ISC_TIME
    uint16_t zone_id_;    // IANA timezone ID
    int16_t offset_;      // Offset from UTC in minutes
};

/**
 * @brief Time type for Firebird TIME
 *
 * Minimal wrapper for ISC_TIME. Use timestamp_utils for conversions.
 */
class Time {
public:
    // Default constructor
    Time() : time_(0) {}

    // Construct from Firebird time (fractions of day)
    explicit Time(uint32_t time) : time_(time) {}

    // Construct from std::chrono::duration (time since midnight)
    template<typename Rep, typename Period>
    explicit Time(std::chrono::duration<Rep, Period> d) {
        time_ = timestamp_utils::to_firebird_time(d);
    }

    // Get raw time value for Firebird API
    uint32_t getTime() const { return time_; }

private:
    uint32_t time_;  // ISC_TIME - fractions of day in 1/10000 sec
};

/**
 * @brief Time with timezone for Firebird TIME WITH TIME ZONE
 *
 * Minimal wrapper for ISC_TIME_TZ. Use IUtil for conversions.
 */
class TimeTz {
public:
    TimeTz() : time_(0), zone_id_(0), offset_(0) {}

    TimeTz(uint32_t time, uint16_t zone_id, int16_t offset)
        : time_(time), zone_id_(zone_id), offset_(offset) {}

    // Construct from Time and timezone info
    TimeTz(const Time& time, uint16_t zone_id, int16_t offset)
        : time_(time.getTime()), zone_id_(zone_id), offset_(offset) {}

    // Get components for Firebird API
    uint32_t getTime() const { return time_; }
    uint16_t getZoneId() const { return zone_id_; }
    int16_t getOffset() const { return offset_; }

private:
    uint32_t time_;    // ISC_TIME
    uint16_t zone_id_; // IANA timezone ID
    int16_t offset_;   // Offset from UTC in minutes
};

/**
 * @brief Blob identifier for Firebird BLOB
 *
 * Minimal wrapper for ISC_QUAD blob ID.
 */
class Blob {
public:
    Blob() : blob_id_{0, 0} {}

    // Construct from blob ID (8 bytes)
    explicit Blob(const uint8_t* id) {
        std::memcpy(&blob_id_, id, 8);
    }

    // Get blob ID for Firebird API
    const uint8_t* getId() const {
        return reinterpret_cast<const uint8_t*>(&blob_id_);
    }

    uint8_t* getId() {
        return reinterpret_cast<uint8_t*>(&blob_id_);
    }

    // Check if blob is null
    bool isNull() const {
        return blob_id_[0] == 0 && blob_id_[1] == 0;
    }

protected:
    uint32_t blob_id_[2];  // 8-byte blob identifier
};

/**
 * @brief Text BLOB wrapper with caching
 *
 * Minimal text BLOB support with optional caching.
 */
class TextBlob : public Blob {
public:
    TextBlob() : Blob() {}

    // Construct from blob ID
    explicit TextBlob(const uint8_t* id) : Blob(id) {}

    // Construct from text (will create BLOB on pack)
    explicit TextBlob(const std::string& text) : Blob(), cached_text_(text) {}

    // Text access (for caching)
    bool hasText() const { return cached_text_.has_value(); }
    const std::string& getText() const {
        if (!cached_text_) {
            static const std::string empty;
            return empty;
        }
        return *cached_text_;
    }
    void setText(const std::string& text) { cached_text_ = text; }
    void clearText() { cached_text_.reset(); }

private:
    mutable std::optional<std::string> cached_text_;
};

} // namespace fbpp::core