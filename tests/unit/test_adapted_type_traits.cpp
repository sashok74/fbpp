#include <gtest/gtest.h>
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/type_adapter.hpp"
#include <string>

using namespace fbpp::core;

// Custom type with adapter
struct Price {
    int64_t cents;

    Price(double dollars) : cents(static_cast<int64_t>(dollars * 100 + 0.5)) {}
    explicit Price(int64_t c) : cents(c) {}

    bool operator==(const Price& other) const { return cents == other.cents; }
};

// Adapter for Price
template<>
struct fbpp::core::TypeAdapter<Price> {
    static constexpr bool is_specialized = true;
    using firebird_type = int64_t;
    using user_type = Price;

    static firebird_type to_firebird(const user_type& value) {
        return value.cents;
    }

    static user_type from_firebird(const firebird_type& value) {
        return Price(value);
    }
};

// Type without adapter
struct NoAdapter {
    int value;
};

class AdaptedTypeTraitsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(AdaptedTypeTraitsTest, AdaptedTypeHasTraits) {
    // Price type should have type traits through its adapter
    using traits = FirebirdTypeTraits<Price>;

    // Should inherit sql_type from int64_t
    EXPECT_EQ(traits::sql_type, SQL_INT64);
    EXPECT_EQ(traits::size, sizeof(int64_t));
    EXPECT_FALSE(traits::is_nullable);
    EXPECT_EQ(traits::scale, 0);

    // Should have adapted_type typedef
    EXPECT_TRUE((std::is_same_v<traits::adapted_type, Price>));

    // Base traits should point to int64_t traits
    EXPECT_TRUE((std::is_same_v<traits::base_traits, FirebirdTypeTraits<int64_t>>));
}

TEST_F(AdaptedTypeTraitsTest, EffectiveFirebirdType) {
    // For adapted types, effective type should be the Firebird equivalent
    EXPECT_TRUE((std::is_same_v<effective_firebird_type<Price>, int64_t>));

    // For non-adapted types, effective type should be the type itself
    EXPECT_TRUE((std::is_same_v<effective_firebird_type<int32_t>, int32_t>));
    EXPECT_TRUE((std::is_same_v<effective_firebird_type<std::string>, std::string>));
    EXPECT_TRUE((std::is_same_v<effective_firebird_type<NoAdapter>, NoAdapter>));
}

TEST_F(AdaptedTypeTraitsTest, FirebirdCompatibility) {
    // Adapted types should be Firebird compatible
    EXPECT_TRUE(is_firebird_compatible_v<Price>);

    // Basic types should remain compatible
    EXPECT_TRUE(is_firebird_compatible_v<int16_t>);
    EXPECT_TRUE(is_firebird_compatible_v<int32_t>);
    EXPECT_TRUE(is_firebird_compatible_v<int64_t>);
    EXPECT_TRUE(is_firebird_compatible_v<float>);
    EXPECT_TRUE(is_firebird_compatible_v<double>);
    EXPECT_TRUE(is_firebird_compatible_v<bool>);
    EXPECT_TRUE(is_firebird_compatible_v<std::string>);

    // Types without adapter and without direct support should not be compatible
    EXPECT_FALSE(is_firebird_compatible_v<NoAdapter>);
}

TEST_F(AdaptedTypeTraitsTest, OptionalAdaptedTypes) {
    // Optional of adapted type requires separate handling
    // The adapted type itself (Price) has traits through its adapter
    EXPECT_TRUE(has_type_adapter_v<Price>);
    EXPECT_TRUE(is_firebird_compatible_v<Price>);

    // But optional<Price> would need its own adapter
    // This test just verifies the basic adapter works
    Price price(49.99);
    auto fb_value = adapt_to_firebird(price);
    EXPECT_EQ(fb_value, 4999); // 49.99 * 100 = 4999 (integer truncation)
}

TEST_F(AdaptedTypeTraitsTest, AdapterIntegration) {
    Price price(29.99);

    // Get traits for adapted type
    using traits = FirebirdTypeTraits<Price>;

    // Convert using adapter
    auto fb_value = adapt_to_firebird(price);

    // FB value should match traits expectations
    EXPECT_TRUE((std::is_same_v<decltype(fb_value), int64_t>));
    EXPECT_EQ(fb_value, 2999);

    // sql_type should match int64_t
    EXPECT_EQ(traits::sql_type, FirebirdTypeTraits<int64_t>::sql_type);
}

// Test that we can chain adapters (adapter of adapter)
struct Discount {
    Price original_price;
    double percentage;

    Price discounted_price() const {
        return Price(original_price.cents * (1.0 - percentage / 100.0) / 100.0);
    }
};

template<>
struct fbpp::core::TypeAdapter<Discount> {
    static constexpr bool is_specialized = true;
    using firebird_type = int64_t;  // Store as discounted price in cents
    using user_type = Discount;

    static firebird_type to_firebird(const user_type& value) {
        return value.discounted_price().cents;
    }

    static user_type from_firebird(const firebird_type& value) {
        // Can't fully reconstruct, but this is for testing
        return Discount{Price(value), 0.0};
    }
};

TEST_F(AdaptedTypeTraitsTest, ChainedAdapters) {
    // Discount should also be Firebird compatible through its adapter
    EXPECT_TRUE(has_type_adapter_v<Discount>);
    EXPECT_TRUE(is_firebird_compatible_v<Discount>);

    using traits = FirebirdTypeTraits<Discount>;
    EXPECT_EQ(traits::sql_type, SQL_INT64);
}