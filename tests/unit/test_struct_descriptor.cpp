#include "fbpp/core/struct_descriptor.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace fbpp::core;

// ============================================================================
// Test Structures
// ============================================================================

struct SimpleStruct {
    int64_t id;
    std::string name;
    std::optional<double> value;
};

// Specialization for SimpleStruct
template<>
struct fbpp::core::StructDescriptor<SimpleStruct> {
    static constexpr bool is_specialized = true;
    static constexpr const char* name = "SIMPLE_STRUCT";

    static constexpr auto fields = std::tuple{
        detail::makeField(&SimpleStruct::id,    "ID",    SQL_INT64,   0, 8, 0, false, false),
        detail::makeField(&SimpleStruct::name,  "NAME",  SQL_VARYING, 0, 258, 0, false, false),
        detail::makeField(&SimpleStruct::value, "VALUE", SQL_DOUBLE,  0, 8, 0, true, false)
    };

    static constexpr size_t fieldCount = std::tuple_size_v<decltype(fields)>;
};

struct PersonStruct {
    int32_t id;
    std::optional<std::string> email;
};

template<>
struct fbpp::core::StructDescriptor<PersonStruct> {
    static constexpr bool is_specialized = true;
    static constexpr const char* name = "PERSON";

    static constexpr auto fields = std::tuple{
        detail::makeField(&PersonStruct::id,    "ID",    SQL_LONG,    0, 4, 0, false, false),
        detail::makeField(&PersonStruct::email, "EMAIL", SQL_VARYING, 0, 258, 0, true, false)
    };

    static constexpr size_t fieldCount = std::tuple_size_v<decltype(fields)>;
};

// Struct without descriptor
struct UnknownStruct {
    int value;
};

// ============================================================================
// Type Traits Tests
// ============================================================================

TEST(StructDescriptor, IsOptionalTrait) {
    using namespace detail;

    // Basic types are not optional
    EXPECT_FALSE(is_optional_v<int>);
    EXPECT_FALSE(is_optional_v<std::string>);
    EXPECT_FALSE(is_optional_v<double>);

    // std::optional types are optional
    EXPECT_TRUE(is_optional_v<std::optional<int>>);
    EXPECT_TRUE(is_optional_v<std::optional<std::string>>);
    EXPECT_TRUE(is_optional_v<std::optional<double>>);
}

TEST(StructDescriptor, HasDescriptorTrait) {
    // Types with descriptor
    EXPECT_TRUE(detail::has_struct_descriptor_v<SimpleStruct>);
    EXPECT_TRUE(detail::has_struct_descriptor_v<PersonStruct>);

    // Types without descriptor
    EXPECT_FALSE(detail::has_struct_descriptor_v<UnknownStruct>);
    EXPECT_FALSE(detail::has_struct_descriptor_v<int>);
    EXPECT_FALSE(detail::has_struct_descriptor_v<std::string>);
}

TEST(StructDescriptor, IsStructPackableTrait) {
    // Struct-packable types
    EXPECT_TRUE(is_struct_packable_v<SimpleStruct>);
    EXPECT_TRUE(is_struct_packable_v<PersonStruct>);

    // Not struct-packable
    EXPECT_FALSE(is_struct_packable_v<UnknownStruct>);
    EXPECT_FALSE(is_struct_packable_v<int>);
    EXPECT_FALSE((is_struct_packable_v<std::tuple<int, std::string>>));  // Parentheses for macro
    // Note: nlohmann::json check would require including json header
}

TEST(StructDescriptor, StructPackableConcept) {
    // Compile-time checks
    static_assert(StructPackable<SimpleStruct>);
    static_assert(StructPackable<PersonStruct>);

    static_assert(!StructPackable<UnknownStruct>);
    static_assert(!StructPackable<int>);
    static_assert(!StructPackable<std::tuple<int, std::string>>);
}

// ============================================================================
// FieldDescriptor Tests
// ============================================================================

TEST(FieldDescriptor, BasicProperties) {
    constexpr auto& fields = StructDescriptor<SimpleStruct>::fields;
    constexpr auto& field0 = std::get<0>(fields);

    // Check properties
    EXPECT_STREQ(field0.sqlName, "ID");
    EXPECT_EQ(field0.sqlType, SQL_INT64);
    EXPECT_EQ(field0.scale, 0);
    EXPECT_EQ(field0.length, 8);
    EXPECT_EQ(field0.subType, 0);
    EXPECT_FALSE(field0.nullable);
    EXPECT_FALSE(field0.useAdapter);

    // Check optional field
    constexpr auto& field2 = std::get<2>(fields);
    EXPECT_STREQ(field2.sqlName, "VALUE");
    EXPECT_TRUE(field2.nullable);
}

TEST(FieldDescriptor, FieldAccess) {
    SimpleStruct s{42, "test", 3.14};

    constexpr auto& fields = StructDescriptor<SimpleStruct>::fields;
    constexpr auto& field0 = std::get<0>(fields);
    constexpr auto& field1 = std::get<1>(fields);
    constexpr auto& field2 = std::get<2>(fields);

    // Access fields
    EXPECT_EQ(field0.access(s), 42);
    EXPECT_EQ(field1.access(s), "test");
    EXPECT_TRUE(field2.access(s).has_value());
    EXPECT_DOUBLE_EQ(*field2.access(s), 3.14);
}

TEST(FieldDescriptor, FieldAccessMutable) {
    SimpleStruct s{42, "test", std::nullopt};

    constexpr auto& fields = StructDescriptor<SimpleStruct>::fields;
    constexpr auto& field0 = std::get<0>(fields);
    constexpr auto& field1 = std::get<1>(fields);
    constexpr auto& field2 = std::get<2>(fields);

    // Modify through access
    field0.access(s) = 100;
    field1.access(s) = "modified";
    field2.access(s) = 5.0;

    EXPECT_EQ(s.id, 100);
    EXPECT_EQ(s.name, "modified");
    EXPECT_TRUE(s.value.has_value());
    EXPECT_DOUBLE_EQ(*s.value, 5.0);
}

TEST(FieldDescriptor, OptionalFieldDetection) {
    using namespace detail;

    constexpr auto& fields = StructDescriptor<SimpleStruct>::fields;

    // Non-optional fields
    using Field0Type = decltype(std::get<0>(fields));
    EXPECT_FALSE(Field0Type::is_optional_field);

    using Field1Type = decltype(std::get<1>(fields));
    EXPECT_FALSE(Field1Type::is_optional_field);

    // Optional field
    using Field2Type = decltype(std::get<2>(fields));
    EXPECT_TRUE(Field2Type::is_optional_field);
}

// ============================================================================
// StructDescriptor Tests
// ============================================================================

TEST(StructDescriptor, DescriptorProperties) {
    // SimpleStruct descriptor
    EXPECT_TRUE(StructDescriptor<SimpleStruct>::is_specialized);
    EXPECT_STREQ(StructDescriptor<SimpleStruct>::name, "SIMPLE_STRUCT");
    EXPECT_EQ(StructDescriptor<SimpleStruct>::fieldCount, 3);

    // PersonStruct descriptor
    EXPECT_TRUE(StructDescriptor<PersonStruct>::is_specialized);
    EXPECT_STREQ(StructDescriptor<PersonStruct>::name, "PERSON");
    EXPECT_EQ(StructDescriptor<PersonStruct>::fieldCount, 2);

    // UnknownStruct has no descriptor
    EXPECT_FALSE(StructDescriptor<UnknownStruct>::is_specialized);
}

TEST(StructDescriptor, FieldCount) {
    constexpr auto& simpleFields = StructDescriptor<SimpleStruct>::fields;
    EXPECT_EQ(std::tuple_size_v<decltype(simpleFields)>, 3);

    constexpr auto& personFields = StructDescriptor<PersonStruct>::fields;
    EXPECT_EQ(std::tuple_size_v<decltype(personFields)>, 2);
}

TEST(StructDescriptor, FieldIteration) {
    constexpr auto& fields = StructDescriptor<SimpleStruct>::fields;

    // Expected field names
    const char* expectedNames[] = {"ID", "NAME", "VALUE"};
    unsigned expectedTypes[] = {SQL_INT64, SQL_VARYING, SQL_DOUBLE};

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = std::get<0>(fields);  // Get by compile-time index
        // Note: This is simplified - real iteration would need template recursion
    }

    // Verify specific fields
    EXPECT_STREQ(std::get<0>(fields).sqlName, "ID");
    EXPECT_STREQ(std::get<1>(fields).sqlName, "NAME");
    EXPECT_STREQ(std::get<2>(fields).sqlName, "VALUE");

    EXPECT_EQ(std::get<0>(fields).sqlType, SQL_INT64);
    EXPECT_EQ(std::get<1>(fields).sqlType, SQL_VARYING);
    EXPECT_EQ(std::get<2>(fields).sqlType, SQL_DOUBLE);
}

// ============================================================================
// MakeField Factory Tests
// ============================================================================

TEST(MakeField, BasicFieldCreation) {
    auto field = detail::makeField(
        &SimpleStruct::id,
        "ID",
        SQL_INT64,
        0,    // scale
        8,    // length
        0,    // subType
        false, // nullable
        false  // useAdapter
    );

    EXPECT_STREQ(field.sqlName, "ID");
    EXPECT_EQ(field.sqlType, SQL_INT64);
    EXPECT_EQ(field.scale, 0);
    EXPECT_EQ(field.length, 8);
    EXPECT_FALSE(field.nullable);
    EXPECT_FALSE(field.useAdapter);
}

TEST(MakeField, OptionalFieldCreation) {
    auto field = detail::makeField(
        &SimpleStruct::value,
        "VALUE",
        SQL_DOUBLE,
        0,    // scale
        8,    // length
        0,    // subType
        true, // nullable
        false // useAdapter
    );

    EXPECT_STREQ(field.sqlName, "VALUE");
    EXPECT_TRUE(field.nullable);

    // Check is_optional_field
    using FieldType = decltype(field);
    EXPECT_TRUE(FieldType::is_optional_field);
}

TEST(MakeField, DefaultParameters) {
    // Test with default parameters (all zeros/false)
    auto field = detail::makeField(
        &SimpleStruct::id,
        "ID",
        SQL_INT64
        // scale=0, length=0, subType=0, nullable=false, useAdapter=false by default
    );

    EXPECT_STREQ(field.sqlName, "ID");
    EXPECT_EQ(field.sqlType, SQL_INT64);
    EXPECT_EQ(field.scale, 0);
    EXPECT_EQ(field.length, 0);
    EXPECT_EQ(field.subType, 0);
    EXPECT_FALSE(field.nullable);
    EXPECT_FALSE(field.useAdapter);
}

// ============================================================================
// Compilation Tests (Static Asserts)
// ============================================================================

TEST(StructDescriptor, CompilationChecks) {
    // These should compile
    static_assert(detail::is_optional_v<std::optional<int>>);
    static_assert(!detail::is_optional_v<int>);

    static_assert(detail::has_struct_descriptor_v<SimpleStruct>);
    static_assert(!detail::has_struct_descriptor_v<int>);

    static_assert(is_struct_packable_v<SimpleStruct>);
    static_assert(!is_struct_packable_v<int>);

    // Verify constexpr context works
    constexpr auto& desc = StructDescriptor<SimpleStruct>::fields;
    constexpr size_t count = std::tuple_size_v<decltype(desc)>;
    static_assert(count == 3);

    SUCCEED();  // If we get here, compilation succeeded
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
