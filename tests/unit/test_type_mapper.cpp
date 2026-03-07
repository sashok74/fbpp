#include <gtest/gtest.h>

#include "fbpp/schema/type_mapper.hpp"
#include "fbpp/core/firebird_compat.hpp"

using namespace fbpp::schema;
using fbpp::core::FieldInfo;

namespace {

FieldInfo makeField(unsigned type,
                    int scale = 0,
                    unsigned length = 4,
                    bool nullable = false,
                    unsigned subType = 0) {
    FieldInfo f{};
    f.type = type;
    f.scale = scale;
    f.length = length;
    f.nullable = nullable;
    f.subType = subType;
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// Integer types
// ---------------------------------------------------------------------------

TEST(TypeMapper, SmallInt) {
    auto r = TypeMapper::mapField(makeField(SQL_SHORT), false);
    EXPECT_EQ(r.cppType, "std::int16_t");
    EXPECT_FALSE(r.needsOptional);
    EXPECT_FALSE(r.needsTTMath);
}

TEST(TypeMapper, LongNoScale) {
    auto r = TypeMapper::mapField(makeField(SQL_LONG), false);
    EXPECT_EQ(r.cppType, "std::int32_t");
}

TEST(TypeMapper, LongWithScaleNoTTMath) {
    auto r = TypeMapper::mapField(makeField(SQL_LONG, -2), false, {});
    EXPECT_EQ(r.cppType, "std::int32_t");
    EXPECT_FALSE(r.needsTTMath);
}

TEST(TypeMapper, LongWithScaleWithTTMath) {
    AdapterConfig cfg;
    cfg.useTTMathNumeric = true;
    auto r = TypeMapper::mapField(makeField(SQL_LONG, -2), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::TTNumeric<1, -2>");
    EXPECT_TRUE(r.needsTTMath);
    ASSERT_TRUE(r.scaledInfo.has_value());
    EXPECT_EQ(r.scaledInfo->intWords, 1);
    EXPECT_EQ(r.scaledInfo->scale, -2);
}

TEST(TypeMapper, Int64NoScale) {
    auto r = TypeMapper::mapField(makeField(SQL_INT64), false);
    EXPECT_EQ(r.cppType, "std::int64_t");
}

TEST(TypeMapper, Int64WithScaleNoTTMath) {
    auto r = TypeMapper::mapField(makeField(SQL_INT64, -4), false, {});
    EXPECT_EQ(r.cppType, "std::int64_t");
    EXPECT_FALSE(r.needsTTMath);
}

TEST(TypeMapper, Int64WithScaleWithTTMath) {
    AdapterConfig cfg;
    cfg.useTTMathNumeric = true;
    auto r = TypeMapper::mapField(makeField(SQL_INT64, -4), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::TTNumeric<1, -4>");
    EXPECT_TRUE(r.needsTTMath);
    ASSERT_TRUE(r.scaledInfo.has_value());
    EXPECT_EQ(r.scaledInfo->intWords, 1);
    EXPECT_EQ(r.scaledInfo->scale, -4);
}

TEST(TypeMapper, Int128NoScaleCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_INT128), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Int128");
    EXPECT_TRUE(r.needsExtendedTypes);
    EXPECT_FALSE(r.needsTTMath);
}

TEST(TypeMapper, Int128NoScaleTTMathMode) {
    AdapterConfig cfg;
    cfg.useTTMathInt128 = true;
    auto r = TypeMapper::mapField(makeField(SQL_INT128), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::Int128");
    EXPECT_TRUE(r.needsTTMath);
}

TEST(TypeMapper, Int128WithScaleCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_INT128, -2), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Int128");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, Int128WithScaleTTMathMode) {
    AdapterConfig cfg;
    cfg.useTTMathNumeric = true;
    auto r = TypeMapper::mapField(makeField(SQL_INT128, -2), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::TTNumeric<2, -2>");
    EXPECT_TRUE(r.needsTTMath);
    ASSERT_TRUE(r.scaledInfo.has_value());
    EXPECT_EQ(r.scaledInfo->intWords, 2);
    EXPECT_EQ(r.scaledInfo->scale, -2);
}

// ---------------------------------------------------------------------------
// Float/double
// ---------------------------------------------------------------------------

TEST(TypeMapper, Float) {
    auto r = TypeMapper::mapField(makeField(SQL_FLOAT), false);
    EXPECT_EQ(r.cppType, "float");
}

TEST(TypeMapper, Double) {
    auto r = TypeMapper::mapField(makeField(SQL_DOUBLE), false);
    EXPECT_EQ(r.cppType, "double");
}

// ---------------------------------------------------------------------------
// String types
// ---------------------------------------------------------------------------

TEST(TypeMapper, Text) {
    auto r = TypeMapper::mapField(makeField(SQL_TEXT), false);
    EXPECT_EQ(r.cppType, "std::string");
    EXPECT_TRUE(r.needsString);
}

TEST(TypeMapper, Varying) {
    auto r = TypeMapper::mapField(makeField(SQL_VARYING), false);
    EXPECT_EQ(r.cppType, "std::string");
    EXPECT_TRUE(r.needsString);
}

TEST(TypeMapper, Boolean) {
    auto r = TypeMapper::mapField(makeField(SQL_BOOLEAN), false);
    EXPECT_EQ(r.cppType, "bool");
}

// ---------------------------------------------------------------------------
// DECFLOAT
// ---------------------------------------------------------------------------

TEST(TypeMapper, DecFloat16CoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_DEC16), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::DecFloat16");
    EXPECT_TRUE(r.needsExtendedTypes);
    EXPECT_FALSE(r.needsCppDecimal);
}

TEST(TypeMapper, DecFloat16CppDecimalMode) {
    AdapterConfig cfg;
    cfg.useCppDecimalDecFloat = true;
    auto r = TypeMapper::mapField(makeField(SQL_DEC16), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::DecFloat16");
    EXPECT_TRUE(r.needsCppDecimal);
}

TEST(TypeMapper, DecFloat34CoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_DEC34), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::DecFloat34");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, DecFloat34CppDecimalMode) {
    AdapterConfig cfg;
    cfg.useCppDecimalDecFloat = true;
    auto r = TypeMapper::mapField(makeField(SQL_DEC34), false, cfg);
    EXPECT_EQ(r.cppType, "fbpp::adapters::DecFloat34");
    EXPECT_TRUE(r.needsCppDecimal);
}

// ---------------------------------------------------------------------------
// Date/Time
// ---------------------------------------------------------------------------

TEST(TypeMapper, DateCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_TYPE_DATE), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Date");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, DateChronoMode) {
    AdapterConfig cfg;
    cfg.useChronoDatetime = true;
    auto r = TypeMapper::mapField(makeField(SQL_TYPE_DATE), false, cfg);
    EXPECT_EQ(r.cppType, "std::chrono::year_month_day");
    EXPECT_TRUE(r.needsChrono);
}

TEST(TypeMapper, TimeCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_TYPE_TIME), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Time");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, TimeChronoMode) {
    AdapterConfig cfg;
    cfg.useChronoDatetime = true;
    auto r = TypeMapper::mapField(makeField(SQL_TYPE_TIME), false, cfg);
    EXPECT_EQ(r.cppType, "std::chrono::hh_mm_ss<std::chrono::microseconds>");
    EXPECT_TRUE(r.needsChrono);
}

TEST(TypeMapper, TimestampCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_TIMESTAMP), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Timestamp");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, TimestampChronoMode) {
    AdapterConfig cfg;
    cfg.useChronoDatetime = true;
    auto r = TypeMapper::mapField(makeField(SQL_TIMESTAMP), false, cfg);
    EXPECT_EQ(r.cppType, "std::chrono::system_clock::time_point");
    EXPECT_TRUE(r.needsChrono);
}

TEST(TypeMapper, TimestampTz) {
    auto r = TypeMapper::mapField(makeField(SQL_TIMESTAMP_TZ), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::TimestampTz");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, TimeTz) {
    auto r = TypeMapper::mapField(makeField(SQL_TIME_TZ), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::TimeTz");
    EXPECT_TRUE(r.needsExtendedTypes);
}

// ---------------------------------------------------------------------------
// BLOB
// ---------------------------------------------------------------------------

TEST(TypeMapper, BlobBinary) {
    auto r = TypeMapper::mapField(makeField(SQL_BLOB, 0, 8, false, 0), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::Blob");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, BlobTextCoreMode) {
    auto r = TypeMapper::mapField(makeField(SQL_BLOB, 0, 8, false, 1), false, {});
    EXPECT_EQ(r.cppType, "fbpp::core::TextBlob");
    EXPECT_TRUE(r.needsExtendedTypes);
}

TEST(TypeMapper, BlobTextStringMode) {
    AdapterConfig cfg;
    cfg.useStringForTextBlob = true;
    auto r = TypeMapper::mapField(makeField(SQL_BLOB, 0, 8, false, 1), false, cfg);
    EXPECT_EQ(r.cppType, "std::string");
    EXPECT_TRUE(r.needsString);
}

// ---------------------------------------------------------------------------
// Nullable handling
// ---------------------------------------------------------------------------

TEST(TypeMapper, NullableViaFieldFlag) {
    auto r = TypeMapper::mapField(makeField(SQL_SHORT, 0, 2, true), false);
    EXPECT_TRUE(r.needsOptional);
    EXPECT_EQ(r.cppType, "std::optional<std::int16_t>");
}

TEST(TypeMapper, NullableViaLsbInTypeForOutputField) {
    // SQL_SHORT | 1 = 501 — LSB set means nullable
    auto r = TypeMapper::mapField(makeField(SQL_SHORT | 1U), true);
    EXPECT_TRUE(r.needsOptional);
    EXPECT_EQ(r.cppType, "std::optional<std::int16_t>");
}

TEST(TypeMapper, LsbNullableNotAppliedToInputField) {
    // For input fields (isOutput=false), LSB-nullable should NOT trigger needsOptional
    auto field = makeField(SQL_SHORT | 1U);
    field.nullable = false;
    auto r = TypeMapper::mapField(field, false);
    EXPECT_FALSE(r.needsOptional);
}
