// Simple compilation test for generated CppDecimal adapter code
// This test just verifies that the generated code compiles correctly

// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_decfloat.hpp"

#include "fbpp/adapters/cppdecimal_decfloat.hpp"

#include <iostream>

int main() {
    std::cout << "Testing generated CppDecimal adapter code compilation...\n";

    // Test type aliases exist and work
    fbpp::adapters::DecFloat16 df16("123.456");
    fbpp::adapters::DecFloat34 df34("123456789012345678901234567890.1234");

    std::cout << "Type aliases work correctly\n";

    // Test struct instantiation
    generated::queries::InsertDecFloatTypesIn insertInput;
    insertInput.param1 = 1;
    insertInput.param2 = df16;
    insertInput.param3 = fbpp::adapters::DecFloat16("789.012");
    insertInput.param4 = df34;
    insertInput.param5 = fbpp::adapters::DecFloat34("987654321098765432109876543210.9876");
    insertInput.param6 = "Test";

    std::cout << "Struct instantiation works\n";

    // Test QueryDescriptor access
    static_assert(generated::queries::QueryDescriptor<generated::queries::QueryId::InsertDecFloatTypes>::id
                  == generated::queries::QueryId::InsertDecFloatTypes);

    auto queryName = generated::queries::QueryDescriptor<generated::queries::QueryId::InsertDecFloatTypes>::name;
    std::cout << "Query name: " << queryName << "\n";

    // Test output struct
    generated::queries::SelectAllDecFloatOut output;
    output.fId = 42;
    output.fDecfloat16 = df16;
    output.fDecfloat34 = df34;
    output.fName = "Output test";

    std::cout << "Output struct assignment works\n";

    // Test StructDescriptor exists (compile-time check)
    using InsertDesc = fbpp::core::StructDescriptor<generated::queries::InsertDecFloatTypesIn>;
    constexpr auto fieldCount = std::tuple_size_v<decltype(InsertDesc::fields)>;
    std::cout << "InsertDecFloatTypesIn has " << fieldCount << " fields\n";

    static_assert(fieldCount == 6, "InsertDecFloatTypesIn should have 6 fields");

    std::cout << "\n✓ All compilation tests passed!\n";
    std::cout << "✓ Type aliases generated correctly\n";
    std::cout << "✓ Structs with CppDecimal adapters compile\n";
    std::cout << "✓ QueryDescriptor specializations work\n";
    std::cout << "✓ StructDescriptor specializations exist\n";

    return 0;
}
