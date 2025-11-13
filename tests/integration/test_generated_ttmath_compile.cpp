// Simple compilation test for generated TTMath adapter code
// This test just verifies that the generated code compiles correctly

// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_ttmath.hpp"

#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"

#include <iostream>

int main() {
    std::cout << "Testing generated TTMath adapter code compilation...\n";

    // Test type aliases exist and work
    Numeric18_2 n18_2("1234.56");
    Numeric18_6 n18_6("1234.123456");
    Numeric38_2 n38_2("12345678901234567890.12");
    Numeric38_4 n38_4("1234567890123456.1234");
    Numeric38_8 n38_8("123456789012.12345678");
    Numeric38_18 n38_18("12.123456789012345678");

    std::cout << "Type aliases work correctly\n";

    // Test struct instantiation
    generated::queries::InsertTTMathTypesIn insertInput;
    insertInput.param1 = 1;
    insertInput.param2 = fbpp::adapters::TTInt128("12345678901234567890");
    insertInput.param3 = fbpp::adapters::TTInt128("98765432109876543210");
    insertInput.param4 = n38_2;
    insertInput.param5 = n38_4;
    insertInput.param6 = n38_8;
    insertInput.param7 = n38_18;
    insertInput.param8 = n18_2;
    insertInput.param9 = n18_6;
    insertInput.param10 = "Test";

    std::cout << "Struct instantiation works\n";

    // Test QueryDescriptor access
    static_assert(generated::queries::QueryDescriptor<generated::queries::QueryId::InsertTTMathTypes>::id
                  == generated::queries::QueryId::InsertTTMathTypes);

    auto queryName = generated::queries::QueryDescriptor<generated::queries::QueryId::InsertTTMathTypes>::name;
    std::cout << "Query name: " << queryName << "\n";

    // Test output struct
    generated::queries::SelectAllTTMathOut output;
    output.fId = 42;
    output.fInt128Pure = fbpp::adapters::TTInt128("123456789");
    output.fNumeric382 = n38_2;
    output.fNumeric388 = n38_8;
    output.fNumeric182 = n18_2;
    output.fName = "Output test";

    std::cout << "Output struct assignment works\n";

    // Test StructDescriptor exists (compile-time check)
    using InsertDesc = fbpp::core::StructDescriptor<generated::queries::InsertTTMathTypesIn>;
    constexpr auto fieldCount = std::tuple_size_v<decltype(InsertDesc::fields)>;
    std::cout << "InsertTTMathTypesIn has " << fieldCount << " fields\n";

    static_assert(fieldCount == 10, "InsertTTMathTypesIn should have 10 fields");

    std::cout << "\n✓ All compilation tests passed!\n";
    std::cout << "✓ Type aliases generated correctly\n";
    std::cout << "✓ Structs with TTMath adapters compile\n";
    std::cout << "✓ QueryDescriptor specializations work\n";
    std::cout << "✓ StructDescriptor specializations exist\n";

    return 0;
}
