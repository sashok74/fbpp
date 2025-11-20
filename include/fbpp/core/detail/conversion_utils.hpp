#pragma once

#include "fbpp/core/exception.hpp"
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace fbpp::core::detail {

// Helper to convert decimal string to scaled integer
inline int64_t string_to_decimal_i64(const std::string& str, int scaleNeg /* <0 */) {
    if (scaleNeg >= 0) {
        throw FirebirdException("Scale must be negative for NUMERIC/DECIMAL types");
    }
    
    uint32_t requiredDecimals = static_cast<uint32_t>(-scaleNeg);
    
    // Find decimal point position
    size_t dotPos = str.find('.');
    std::string intPart;
    std::string fracPart;
    
    if (dotPos == std::string::npos) {
        // No decimal point - integer value
        intPart = str;
        fracPart = "";
    } else {
        intPart = str.substr(0, dotPos);
        fracPart = str.substr(dotPos + 1);
    }
    
    // Remove leading zeros from integer part (but keep at least one digit)
    size_t firstNonZero = intPart.find_first_not_of("0-+");
    if (firstNonZero == std::string::npos) {
        intPart = "0";
    } else if (firstNonZero > 0 && intPart[0] != '-' && intPart[0] != '+') {
        intPart = intPart.substr(firstNonZero);
    }
    
    // Pad or truncate fractional part to match required scale
    if (fracPart.length() > requiredDecimals) {
        // Truncate (with warning in real implementation)
        fracPart = fracPart.substr(0, requiredDecimals);
    } else if (fracPart.length() < requiredDecimals) {
        // Pad with zeros
        fracPart.append(requiredDecimals - fracPart.length(), '0');
    }
    
    // Combine and convert to integer
    std::string combined;
    bool negative = false;
    
    if (!intPart.empty() && intPart[0] == '-') {
        negative = true;
        intPart = intPart.substr(1);
    } else if (!intPart.empty() && intPart[0] == '+') {
        intPart = intPart.substr(1);
    }
    
    combined = intPart + fracPart;
    
    // Convert to int64_t
    int64_t result = 0;
    for (char c : combined) {
        if (!std::isdigit(c)) {
            throw FirebirdException("Invalid numeric string: " + str);
        }
        result = result * 10 + (c - '0');
    }
    
    return negative ? -result : result;
}

inline std::string decimal_to_string_i64(int64_t v, int scaleNeg /* <0 */) {
    const uint32_t s = static_cast<uint32_t>(-scaleNeg); // число знаков после точки
    bool neg = v < 0;
    uint64_t u = neg ? uint64_t(-(v + 1)) + 1 : uint64_t(v); // модуль без UB

    static const uint64_t POW10[19] = {1ull,10ull,100ull,1000ull,10000ull,100000ull,1000000ull,10000000ull,100000000ull,1000000000ull,
        10000000000ull,100000000000ull,1000000000000ull,10000000000000ull,100000000000000ull,1000000000000000ull,10000000000000000ull,
        100000000000000000ull,1000000000000000000ull};

    if (s >= 19) throw FirebirdException("Scale too large");
    uint64_t p = POW10[s];
    uint64_t hi = u / p, lo = u % p;

    char frac[32];
    std::snprintf(frac, sizeof frac, "%0*llu", int(s), (unsigned long long)lo);

    std::string out = std::to_string(hi);
    if (s) { out.push_back('.'); out += frac; }
    if (neg) out.insert(out.begin(), '-');
    return out;
}

// Helper to parse ISO date string "YYYY-MM-DD"
inline void parseIsoDate(const std::string& str, unsigned& year, unsigned& month, unsigned& day) {
    if (str.length() < 10 || str[4] != '-' || str[7] != '-') {
        throw FirebirdException("Invalid date format, expected YYYY-MM-DD: " + str);
    }
    
    year = std::stoul(str.substr(0, 4));
    month = std::stoul(str.substr(5, 2));
    day = std::stoul(str.substr(8, 2));
}

// Helper to parse ISO time string "HH:MM:SS[.fraction]"
inline void parseIsoTime(const std::string& str, unsigned& hours, unsigned& minutes, 
                         unsigned& seconds, unsigned& fractions) {
    if (str.length() < 8 || str[2] != ':' || str[5] != ':') {
        throw FirebirdException("Invalid time format, expected HH:MM:SS[.fraction]: " + str);
    }
    
    hours = std::stoul(str.substr(0, 2));
    minutes = std::stoul(str.substr(3, 2));
    seconds = std::stoul(str.substr(6, 2));
    
    fractions = 0;
    if (str.length() > 8 && str[8] == '.') {
        std::string fracStr = str.substr(9);
        // Firebird uses 1/10000 of second for fractions
        // Convert milliseconds or microseconds to this format
        if (fracStr.length() > 0) {
            // Pad or truncate to 4 digits
            while (fracStr.length() < 4) fracStr += '0';
            if (fracStr.length() > 4) fracStr = fracStr.substr(0, 4);
            fractions = std::stoul(fracStr);
        }
    }
}

// Helper to parse timezone offset "+HH:MM" or "-HH:MM"
inline int16_t parseTimezoneOffset(const std::string& tzStr) {
    if (tzStr.empty()) return 0;
    
    bool negative = false;
    size_t start = 0;
    
    if (tzStr[0] == '+') {
        start = 1;
    } else if (tzStr[0] == '-') {
        negative = true;
        start = 1;
    }
    
    size_t colonPos = tzStr.find(':', start);
    if (colonPos == std::string::npos) {
        // Try to parse as +HHMM format
        if (tzStr.length() >= start + 4) {
            int hours = std::stoi(tzStr.substr(start, 2));
            int minutes = std::stoi(tzStr.substr(start + 2, 2));
            int offset = hours * 60 + minutes;
            return negative ? -offset : offset;
        }
        throw FirebirdException("Invalid timezone offset format: " + tzStr);
    }
    
    int hours = std::stoi(tzStr.substr(start, colonPos - start));
    int minutes = std::stoi(tzStr.substr(colonPos + 1));
    int offset = hours * 60 + minutes;
    
    return negative ? -offset : offset;
}

} // namespace fbpp::core::detail
