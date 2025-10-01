#include <gtest/gtest.h>
#include "fbpp/adapters/cppdecimal_decfloat.hpp"
#include <string>

// Use CppDecimal classes from dec namespace
using DecFloat16 = dec::DecDouble;
using DecFloat34 = dec::DecQuad;

class CppDecimalAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No special setup needed
    }
};

TEST_F(CppDecimalAdapterTest, DecFloat16BasicConversion) {
    // Create a DecDouble with a simple value
    DecFloat16 original(123.456);

    // Convert to Firebird type
    auto fb_value = fbpp::core::adapt_to_firebird(original);

    // Verify it's 8 bytes (DecFloat16 is 8 bytes)
    // fb_value is fbpp::core::DecFloat16 which stores 8 bytes

    // Convert back to CppDecimal type
    auto restored = fbpp::core::adapt_from_firebird<DecFloat16>(fb_value);

    // Check values are equal
    EXPECT_EQ(original.toString(), restored.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat16FromString) {
    // Test with string constructor
    DecFloat16 original("9999999999999999");  // 16 digits

    auto fb_value = fbpp::core::adapt_to_firebird(original);
    auto restored = fbpp::core::adapt_from_firebird<DecFloat16>(fb_value);

    EXPECT_EQ(original.toString(), restored.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat16NegativeNumbers) {
    DecFloat16 negative(-12345.6789);

    auto fb_value = fbpp::core::adapt_to_firebird(negative);
    auto restored = fbpp::core::adapt_from_firebird<DecFloat16>(fb_value);

    EXPECT_EQ(negative.toString(), restored.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat16SpecialValues) {
    // Test zero
    DecFloat16 zero(0);
    auto fb_zero = fbpp::core::adapt_to_firebird(zero);
    auto restored_zero = fbpp::core::adapt_from_firebird<DecFloat16>(fb_zero);
    EXPECT_EQ(zero.toString(), restored_zero.toString());

    // Test very small number
    DecFloat16 tiny("1E-300");
    auto fb_tiny = fbpp::core::adapt_to_firebird(tiny);
    auto restored_tiny = fbpp::core::adapt_from_firebird<DecFloat16>(fb_tiny);
    EXPECT_EQ(tiny.toString(), restored_tiny.toString());

    // Test very large number
    DecFloat16 huge("1E+300");
    auto fb_huge = fbpp::core::adapt_to_firebird(huge);
    auto restored_huge = fbpp::core::adapt_from_firebird<DecFloat16>(fb_huge);
    EXPECT_EQ(huge.toString(), restored_huge.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat34BasicConversion) {
    // Create a DecQuad with a simple value
    DecFloat34 original(987654321.123456789);

    // Convert to Firebird type
    auto fb_value = fbpp::core::adapt_to_firebird(original);

    // Verify it's 16 bytes (DecFloat34 is 16 bytes)
    // fb_value is fbpp::core::DecFloat34 which stores 16 bytes

    // Convert back to CppDecimal type
    auto restored = fbpp::core::adapt_from_firebird<DecFloat34>(fb_value);

    // Check values are equal
    EXPECT_EQ(original.toString(), restored.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat34VeryLargeNumbers) {
    // Test with maximum precision for DECFLOAT34 (34 digits)
    DecFloat34 huge("9999999999999999999999999999999999");  // 34 nines

    auto fb_value = fbpp::core::adapt_to_firebird(huge);
    auto restored = fbpp::core::adapt_from_firebird<DecFloat34>(fb_value);

    EXPECT_EQ(huge.toString(), restored.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat34HighPrecision) {
    // Test high precision decimal
    DecFloat34 precise("123456789012345678.901234567890123456");

    auto fb_value = fbpp::core::adapt_to_firebird(precise);
    auto restored = fbpp::core::adapt_from_firebird<DecFloat34>(fb_value);

    EXPECT_EQ(precise.toString(), restored.toString());

    // Test scientific notation
    DecFloat34 scientific("1.23456789012345678901234567890123E+20");
    auto fb_sci = fbpp::core::adapt_to_firebird(scientific);
    auto restored_sci = fbpp::core::adapt_from_firebird<DecFloat34>(fb_sci);

    EXPECT_EQ(scientific.toString(), restored_sci.toString());
}

TEST_F(CppDecimalAdapterTest, DecFloat34Arithmetic) {
    DecFloat34 a("999999999999999999999999999999999");
    DecFloat34 b("111111111111111111111111111111111");

    // Perform arithmetic
    DecFloat34 sum = a + b;
    DecFloat34 diff = a - b;
    DecFloat34 prod = a * DecFloat34(2);

    // Convert through Firebird types
    auto fb_sum = fbpp::core::adapt_to_firebird(sum);
    auto fb_diff = fbpp::core::adapt_to_firebird(diff);
    auto fb_prod = fbpp::core::adapt_to_firebird(prod);

    auto restored_sum = fbpp::core::adapt_from_firebird<DecFloat34>(fb_sum);
    auto restored_diff = fbpp::core::adapt_from_firebird<DecFloat34>(fb_diff);
    auto restored_prod = fbpp::core::adapt_from_firebird<DecFloat34>(fb_prod);

    EXPECT_EQ(sum.toString(), restored_sum.toString());
    EXPECT_EQ(diff.toString(), restored_diff.toString());
    EXPECT_EQ(prod.toString(), restored_prod.toString());
}

TEST_F(CppDecimalAdapterTest, RoundTripPrecision) {
    // Test that we don't lose precision in round-trip conversions
    std::vector<std::string> test_values = {
        "0.1",
        "0.01",
        "0.001",
        "123.456789012345678901234567890123",
        "-999.999999999999999999999999999999",
        "1E-30",
        "1E+30",
        "3.141592653589793238462643383279502884197"
    };

    for (const auto& str_val : test_values) {
        DecFloat34 original(str_val.c_str());
        auto fb_value = fbpp::core::adapt_to_firebird(original);
        auto restored = fbpp::core::adapt_from_firebird<DecFloat34>(fb_value);

        EXPECT_EQ(original.toString(), restored.toString())
            << "Failed for value: " << str_val;
    }
}