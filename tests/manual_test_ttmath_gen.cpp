// Manual test for generated TTMath adapter code
// Compile this to verify generated code works

#include "/tmp/generated_queries_ttmath.hpp"

#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"

#include <iostream>

int main() {
    std::cout << "Testing generated TTMath adapter code...\n\n";

    // Test 1: Type aliases
    std::cout << "[1] Testing type aliases...\n";
    Numeric18_2 n18_2("1234.56");
    Numeric38_2 n38_2("12345678901234567890.12");
    std::cout << "  ✓ Numeric18_2: " << n18_2.toString() << "\n";
    std::cout << "  ✓ Numeric38_2: " << n38_2.toString() << "\n";

    // Test 2: Struct instantiation with TTInt128
    std::cout << "\n[2] Testing struct with TTInt128...\n";
    generated::queries::InsertTTMathTypesIn input;
    input.param1 = 1;
    input.param2 = fbpp::adapters::TTInt128("123456789012345678901234567890");
    std::cout << "  ✓ TTInt128 param: " << input.param2.toString() << "\n";

    // Test 3: Struct with TTNumeric types
    std::cout << "\n[3] Testing struct with TTNumeric types...\n";
    input.param4 = fbpp::adapters::TTNumeric<2, -2>("99999.99");
    input.param6 = fbpp::adapters::TTNumeric<2, -8>("12345.12345678");
    input.param8 = fbpp::adapters::TTNumeric<1, -2>("9999.99");
    std::cout << "  ✓ TTNumeric<2,-2>: " << input.param4.toString() << "\n";
    std::cout << "  ✓ TTNumeric<2,-8>: " << input.param6.toString() << "\n";
    std::cout << "  ✓ TTNumeric<1,-2>: " << input.param8.toString() << "\n";

    // Test 4: Optional fields
    std::cout << "\n[4] Testing optional TTMath fields...\n";
    input.param3 = fbpp::adapters::TTInt128("999");
    input.param5 = fbpp::adapters::TTNumeric<2, -4>("123.4567");
    std::cout << "  ✓ Optional TTInt128: " << input.param3->toString() << "\n";
    std::cout << "  ✓ Optional TTNumeric: " << input.param5->toString() << "\n";

    // Test 5: Output struct
    std::cout << "\n[5] Testing output struct...\n";
    generated::queries::SelectAllTTMathOut output;
    output.fInt128Pure = fbpp::adapters::TTInt128("888888888888888888");
    output.fNumeric382 = fbpp::adapters::TTNumeric<2, -2>("77777.77");
    std::cout << "  ✓ Output INT128: " << output.fInt128Pure.toString() << "\n";
    std::cout << "  ✓ Output NUMERIC38_2: " << output.fNumeric382.toString() << "\n";

    // Test 6: QueryDescriptor metadata
    std::cout << "\n[6] Testing QueryDescriptor metadata...\n";
    using Desc = generated::queries::QueryDescriptor<generated::queries::QueryId::InsertTTMathTypes>;
    std::cout << "  ✓ Query name: " << Desc::name << "\n";
    std::cout << "  ✓ Has named params: " << (Desc::hasNamedParameters ? "yes" : "no") << "\n";

    // Test 7: StructDescriptor field count
    std::cout << "\n[7] Testing StructDescriptor...\n";
    using InsertDesc = fbpp::core::StructDescriptor<generated::queries::InsertTTMathTypesIn>;
    constexpr auto fieldCount = std::tuple_size_v<decltype(InsertDesc::fields)>;
    std::cout << "  ✓ InsertTTMathTypesIn field count: " << fieldCount << "\n";
    static_assert(fieldCount == 10);

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "✓✓✓ ALL TESTS PASSED ✓✓✓\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "\nGenerated code with TTMath adapters:\n";
    std::cout << "  • Compiles correctly\n";
    std::cout << "  • Type aliases work\n";
    std::cout << "  • Structs instantiate properly\n";
    std::cout << "  • TTInt128 and TTNumeric types integrate correctly\n";
    std::cout << "  • Optional adapter fields work\n";
    std::cout << "  • QueryDescriptor and StructDescriptor work\n";

    return 0;
}
