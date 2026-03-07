#pragma once

namespace fbpp::schema {

struct AdapterConfig {
    bool useTTMathNumeric = false;
    bool useTTMathInt128 = false;
    bool useChronoDatetime = false;
    bool useCppDecimalDecFloat = false;
    bool useStringForTextBlob = false;
    bool generateAliases = true;
};

} // namespace fbpp::schema
