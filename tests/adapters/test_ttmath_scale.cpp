#include <gtest/gtest.h>
#include "fbpp/adapters/ttmath_numeric.hpp"
#include "fbpp/core/type_adapter.hpp"
#include <vector>
#include <tuple>

using namespace fbpp::adapters;
using namespace fbpp::core;

class TTMathScaleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
};

// Test different scale types
using Scale0 = TTNumeric<2, 0>;      // No decimal places (integer)
using Scale1 = TTNumeric<2, -1>;     // 1 decimal place
using Scale2 = TTNumeric<2, -2>;     // 2 decimal places (Money)
using Scale4 = TTNumeric<2, -4>;     // 4 decimal places (Percent)
using Scale6 = TTNumeric<2, -6>;     // 6 decimal places (HighPrecision)
using Scale8 = TTNumeric<2, -8>;     // 8 decimal places
using Scale18 = TTNumeric<2, -18>;   // 18 decimal places (max for NUMERIC)

TEST_F(TTMathScaleTest, ScaleConversionSameScale) {
    // Test conversion when source and target have the same scale
    Scale2 money("123.45");

    uint8_t buffer[16];
    adapt_to_firebird_ctx(money, -2, 16, buffer);

    auto restored = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored.to_string(), "123.45");
}

TEST_F(TTMathScaleTest, ScaleConversionUpscaling) {
    // Test when database has MORE decimal places than type
    Scale2 money("123.45");  // Scale -2

    uint8_t buffer[16];
    // Store as scale -4 (4 decimal places)
    adapt_to_firebird_ctx(money, -4, 16, buffer);

    // Read back as scale -4
    auto high_precision = adapt_from_firebird_ctx<Scale4>(buffer, 16, -4);
    EXPECT_EQ(high_precision.to_string(), "123.45");  // Should be exact
}

TEST_F(TTMathScaleTest, ScaleConversionDownscaling) {
    // Test when database has FEWER decimal places than type
    Scale4 precise("123.4567");  // Scale -4

    uint8_t buffer[16];
    // Store as scale -2 (2 decimal places) - will truncate
    adapt_to_firebird_ctx(precise, -2, 16, buffer);

    // Read back as scale -2
    auto money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(money.to_string(), "123.45");  // Truncated to 2 decimal places
}

TEST_F(TTMathScaleTest, CrossScaleConversion) {
    // Test converting between different scale types through Firebird format
    struct TestCase {
        std::string input;
        int from_scale;
        int db_scale;
        int to_scale;
        std::string expected;
    };

    std::vector<TestCase> test_cases = {
        // Same scale conversions
        {"123.45", -2, -2, -2, "123.45"},
        {"0.1234", -4, -4, -4, "0.1234"},

        // Upscaling (adding zeros)
        {"123.45", -2, -4, -4, "123.45"},      // -2 -> -4
        {"123.45", -2, -6, -6, "123.45"},      // -2 -> -6

        // Downscaling (truncation)
        {"123.456789", -6, -2, -2, "123.45"},  // -6 -> -2 (truncate)
        {"123.456789", -6, -4, -4, "123.4567"}, // -6 -> -4 (truncate)

        // Integer to decimal
        {"123", 0, -2, -2, "123"},             // 0 -> -2

        // Decimal to integer (truncation)
        {"123.45", -2, 0, 0, "123"},           // -2 -> 0 (truncate decimal part)
    };

    for (const auto& tc : test_cases) {
        uint8_t buffer[16];

        if (tc.from_scale == 0) {
            Scale0 value(tc.input);
            adapt_to_firebird_ctx(value, tc.db_scale, 16, buffer);
        } else if (tc.from_scale == -2) {
            Scale2 value(tc.input);
            adapt_to_firebird_ctx(value, tc.db_scale, 16, buffer);
        } else if (tc.from_scale == -4) {
            Scale4 value(tc.input);
            adapt_to_firebird_ctx(value, tc.db_scale, 16, buffer);
        } else if (tc.from_scale == -6) {
            Scale6 value(tc.input);
            adapt_to_firebird_ctx(value, tc.db_scale, 16, buffer);
        }

        std::string result;
        if (tc.to_scale == 0) {
            auto restored = adapt_from_firebird_ctx<Scale0>(buffer, 16, tc.db_scale);
            result = restored.to_string();
        } else if (tc.to_scale == -2) {
            auto restored = adapt_from_firebird_ctx<Scale2>(buffer, 16, tc.db_scale);
            result = restored.to_string();
        } else if (tc.to_scale == -4) {
            auto restored = adapt_from_firebird_ctx<Scale4>(buffer, 16, tc.db_scale);
            result = restored.to_string();
        } else if (tc.to_scale == -6) {
            auto restored = adapt_from_firebird_ctx<Scale6>(buffer, 16, tc.db_scale);
            result = restored.to_string();
        }

        EXPECT_EQ(result, tc.expected)
            << "Failed for input: " << tc.input
            << " with scales: " << tc.from_scale
            << " -> DB(" << tc.db_scale << ")"
            << " -> " << tc.to_scale;
    }
}

TEST_F(TTMathScaleTest, NegativeNumbersWithScale) {
    // Test negative numbers with different scales
    Scale2 neg_money("-999.99");
    Scale4 neg_percent("-12.3456");
    Scale6 neg_precise("-0.123456");

    uint8_t buffer1[16], buffer2[16], buffer3[16];

    // Convert with matching scales
    adapt_to_firebird_ctx(neg_money, -2, 16, buffer1);
    adapt_to_firebird_ctx(neg_percent, -4, 16, buffer2);
    adapt_to_firebird_ctx(neg_precise, -6, 16, buffer3);

    // Restore and verify
    auto restored1 = adapt_from_firebird_ctx<Scale2>(buffer1, 16, -2);
    auto restored2 = adapt_from_firebird_ctx<Scale4>(buffer2, 16, -4);
    auto restored3 = adapt_from_firebird_ctx<Scale6>(buffer3, 16, -6);

    EXPECT_EQ(restored1.to_string(), "-999.99");
    EXPECT_EQ(restored2.to_string(), "-12.3456");
    EXPECT_EQ(restored3.to_string(), "-0.123456");

    // Test cross-scale with negatives
    adapt_to_firebird_ctx(neg_precise, -2, 16, buffer1);  // -6 -> -2
    auto truncated = adapt_from_firebird_ctx<Scale2>(buffer1, 16, -2);
    EXPECT_EQ(truncated.to_string(), "-0.12");  // Truncated to 2 decimal places
}

TEST_F(TTMathScaleTest, ExtremePrecisionValues) {
    // Test with maximum precision values for different scales

    // Maximum value for NUMERIC(38,2) - 36 integer digits, 2 decimal
    Scale2 max_money("999999999999999999999999999999999999.99");
    uint8_t buffer[16];
    adapt_to_firebird_ctx(max_money, -2, 16, buffer);
    auto restored_money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored_money.to_string(), "999999999999999999999999999999999999.99");

    // Minimum (most negative) value
    Scale2 min_money("-999999999999999999999999999999999999.99");
    adapt_to_firebird_ctx(min_money, -2, 16, buffer);
    auto restored_min = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored_min.to_string(), "-999999999999999999999999999999999999.99");

    // Very small positive value with high precision
    Scale18 tiny("0.000000000000000001");  // 18 decimal places
    adapt_to_firebird_ctx(tiny, -18, 16, buffer);
    auto restored_tiny = adapt_from_firebird_ctx<Scale18>(buffer, 16, -18);
    EXPECT_EQ(restored_tiny.to_string(), "0.000000000000000001");
}

TEST_F(TTMathScaleTest, MixedScaleArithmetic) {
    // Test arithmetic operations between different scale types
    Scale2 price("19.99");        // Price with 2 decimal places
    Scale4 tax_rate("0.0825");    // 8.25% tax rate with 4 decimal places

    // Calculate tax amount (need to handle scale properly)
    // tax = price * tax_rate = 19.99 * 0.0825 = 1.649175
    auto price_raw = price.raw_value();      // 1999 (scale -2)
    auto rate_raw = tax_rate.raw_value();    // 825 (scale -4)

    // Multiply raw values: 1999 * 825 = 1649175
    auto tax_raw = price_raw * rate_raw;

    // Result has scale -6 (sum of scales: -2 + -4)
    Scale6 tax_amount(tax_raw);
    EXPECT_EQ(tax_amount.to_string(), "1.649175");

    // Store as scale -2 (money format)
    uint8_t buffer[16];
    adapt_to_firebird_ctx(tax_amount, -2, 16, buffer);
    auto tax_money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(tax_money.to_string(), "1.64");  // Truncated to cents
}

TEST_F(TTMathScaleTest, ScaleWithDifferentStorageSizes) {
    // Test with different storage sizes (length parameter)
    Scale2 small_value("99.99");
    Scale2 medium_value("9999.99");
    Scale2 large_value("999999999999.99");

    // Test with 4-byte storage (INT32 range)
    uint8_t buffer4[4];
    adapt_to_firebird_ctx(small_value, -2, 4, buffer4);
    auto restored4 = adapt_from_firebird_ctx<Scale2>(buffer4, 4, -2);
    EXPECT_EQ(restored4.to_string(), "99.99");

    // Test with 8-byte storage (INT64 range)
    uint8_t buffer8[8];
    adapt_to_firebird_ctx(medium_value, -2, 8, buffer8);
    auto restored8 = adapt_from_firebird_ctx<Scale2>(buffer8, 8, -2);
    EXPECT_EQ(restored8.to_string(), "9999.99");

    // Test with 16-byte storage (INT128 range)
    uint8_t buffer16[16];
    adapt_to_firebird_ctx(large_value, -2, 16, buffer16);
    auto restored16 = adapt_from_firebird_ctx<Scale2>(buffer16, 16, -2);
    EXPECT_EQ(restored16.to_string(), "999999999999.99");
}

TEST_F(TTMathScaleTest, ZeroAndBoundaryValues) {
    // Test zero values with different scales
    Scale0 zero_int("0");
    Scale2 zero_money("0.00");
    Scale6 zero_precise("0.000000");

    uint8_t buffer[16];

    adapt_to_firebird_ctx(zero_int, 0, 16, buffer);
    auto restored_int = adapt_from_firebird_ctx<Scale0>(buffer, 16, 0);
    EXPECT_EQ(restored_int.to_string(), "0");

    adapt_to_firebird_ctx(zero_money, -2, 16, buffer);
    auto restored_money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored_money.to_string(), "0");

    adapt_to_firebird_ctx(zero_precise, -6, 16, buffer);
    auto restored_precise = adapt_from_firebird_ctx<Scale6>(buffer, 16, -6);
    EXPECT_EQ(restored_precise.to_string(), "0");

    // Test boundary values
    Scale2 one_cent("0.01");
    adapt_to_firebird_ctx(one_cent, -2, 16, buffer);
    auto restored_cent = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored_cent.to_string(), "0.01");

    Scale2 minus_one_cent("-0.01");
    adapt_to_firebird_ctx(minus_one_cent, -2, 16, buffer);
    auto restored_minus = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(restored_minus.to_string(), "-0.01");
}

TEST_F(TTMathScaleTest, RealWorldFinancialCalculations) {
    // Simulate real-world financial calculations with proper scale handling

    // Invoice calculation
    Scale2 unit_price("29.99");
    Scale0 quantity("5");
    Scale4 discount_rate("0.15");  // 15% discount
    Scale4 tax_rate("0.0875");     // 8.75% sales tax

    // Calculate subtotal: price * quantity
    // unit_price.raw_value() = 2999 (29.99 * 100 for scale -2)
    // quantity.raw_value() = 5 (scale 0)
    // Result should be 2999 * 5 = 14995 (149.95 with scale -2)
    auto subtotal_raw = unit_price.raw_value() * quantity.raw_value();
    Scale2 subtotal(subtotal_raw);  // Already has correct scale
    EXPECT_EQ(subtotal.to_string(), "149.95");

    // Apply discount
    auto discount_amount_raw = subtotal.raw_value() * discount_rate.raw_value();
    Scale6 discount_amount(discount_amount_raw);  // Scale -6 from multiplication

    uint8_t buffer[16];
    adapt_to_firebird_ctx(discount_amount, -2, 16, buffer);
    auto discount_money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(discount_money.to_string(), "22.49");  // Truncated to cents

    // Calculate after discount
    Scale2 after_discount = subtotal - discount_money;
    EXPECT_EQ(after_discount.to_string(), "127.46");

    // Calculate tax
    auto tax_raw = after_discount.raw_value() * tax_rate.raw_value();
    Scale6 tax_amount(tax_raw);

    adapt_to_firebird_ctx(tax_amount, -2, 16, buffer);
    auto tax_money = adapt_from_firebird_ctx<Scale2>(buffer, 16, -2);
    EXPECT_EQ(tax_money.to_string(), "11.15");  // Truncated to cents

    // Final total
    Scale2 total = after_discount + tax_money;
    EXPECT_EQ(total.to_string(), "138.61");
}

TEST_F(TTMathScaleTest, OptionalWithDifferentScales) {
    // Test optional support with different scales
    std::optional<Scale2> opt_money = Scale2("123.45");
    std::optional<Scale4> opt_percent = Scale4("12.3456");
    std::optional<Scale6> opt_precise = std::nullopt;

    // Test conversion with values
    auto fb_money = adapt_to_firebird_opt(opt_money);
    ASSERT_TRUE(fb_money.has_value());
    auto restored_money = adapt_from_firebird_opt<Scale2>(fb_money);
    ASSERT_TRUE(restored_money.has_value());
    EXPECT_EQ(restored_money->to_string(), "123.45");

    auto fb_percent = adapt_to_firebird_opt(opt_percent);
    ASSERT_TRUE(fb_percent.has_value());
    auto restored_percent = adapt_from_firebird_opt<Scale4>(fb_percent);
    ASSERT_TRUE(restored_percent.has_value());
    EXPECT_EQ(restored_percent->to_string(), "12.3456");

    // Test null values
    auto fb_null = adapt_to_firebird_opt(opt_precise);
    ASSERT_FALSE(fb_null.has_value());
    auto restored_null = adapt_from_firebird_opt<Scale6>(fb_null);
    ASSERT_FALSE(restored_null.has_value());
}

TEST_F(TTMathScaleTest, NewMathOperators) {
    // Test assignment operators
    Scale2 money("100.00");
    Scale2 amount("50.25");

    // Test +=
    money += amount;
    EXPECT_EQ(money.to_string(), "150.25");

    // Test -=
    money -= Scale2("25.25");
    EXPECT_EQ(money.to_string(), "125");

    // Test *=
    money *= Scale2("2.00");
    EXPECT_EQ(money.to_string(), "250");

    // Test /=
    money /= Scale2("5.00");
    EXPECT_EQ(money.to_string(), "50");

    // Test unary minus
    Scale2 negative = -money;
    EXPECT_EQ(negative.to_string(), "-50");

    // Test unary plus
    Scale2 positive = +money;
    EXPECT_EQ(positive.to_string(), "50");

    // Test prefix increment
    ++money;
    EXPECT_EQ(money.to_string(), "51");

    // Test prefix decrement
    --money;
    EXPECT_EQ(money.to_string(), "50");

    // Test postfix increment
    Scale2 before_inc = money++;
    EXPECT_EQ(before_inc.to_string(), "50");
    EXPECT_EQ(money.to_string(), "51");

    // Test postfix decrement
    Scale2 before_dec = money--;
    EXPECT_EQ(before_dec.to_string(), "51");
    EXPECT_EQ(money.to_string(), "50");

    // Test modulo
    Scale2 dividend("17.50");
    Scale2 divisor("5.00");
    Scale2 remainder = dividend % divisor;
    EXPECT_EQ(remainder.to_string(), "2.5");

    // Test utility functions
    Scale2 zero("0.00");
    EXPECT_TRUE(zero.is_zero());
    EXPECT_FALSE(zero.is_negative());
    EXPECT_FALSE(zero.is_positive());

    Scale2 neg_value("-10.50");
    EXPECT_FALSE(neg_value.is_zero());
    EXPECT_TRUE(neg_value.is_negative());
    EXPECT_FALSE(neg_value.is_positive());

    Scale2 pos_value("10.50");
    EXPECT_FALSE(pos_value.is_zero());
    EXPECT_FALSE(pos_value.is_negative());
    EXPECT_TRUE(pos_value.is_positive());

    // Test abs
    Scale2 abs_neg = neg_value.abs();
    EXPECT_EQ(abs_neg.to_string(), "10.5");

    Scale2 abs_pos = pos_value.abs();
    EXPECT_EQ(abs_pos.to_string(), "10.5");
}

TEST_F(TTMathScaleTest, IncrementDecrementWithDifferentScales) {
    // Test increment/decrement with different scales
    Scale0 integer("100");
    ++integer;
    EXPECT_EQ(integer.to_string(), "101");
    --integer;
    EXPECT_EQ(integer.to_string(), "100");

    Scale2 money("99.99");
    ++money;
    EXPECT_EQ(money.to_string(), "100.99");
    --money;
    EXPECT_EQ(money.to_string(), "99.99");

    Scale4 precise("0.9999");
    ++precise;
    EXPECT_EQ(precise.to_string(), "1.9999");
    --precise;
    EXPECT_EQ(precise.to_string(), "0.9999");

    Scale6 very_precise("0.999999");
    ++very_precise;
    EXPECT_EQ(very_precise.to_string(), "1.999999");
    --very_precise;
    EXPECT_EQ(very_precise.to_string(), "0.999999");
}