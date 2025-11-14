// Simple compilation test for generated std::chrono adapter code
// This test just verifies that the generated code compiles correctly

// CMake adds CMAKE_BINARY_DIR to include path, so we can include directly
#include "generated_queries_datetime.hpp"

#include "fbpp/adapters/chrono_datetime.hpp"

#include <iostream>
#include <chrono>

int main() {
    std::cout << "Testing generated std::chrono adapter code compilation...\n";

    // Test std::chrono types work
    auto date = std::chrono::year_month_day{
        std::chrono::year{2024}, std::chrono::January, std::chrono::day{15}
    };

    auto time = std::chrono::hh_mm_ss<std::chrono::microseconds>{
        std::chrono::hours{14} + std::chrono::minutes{30} + std::chrono::seconds{45}
    };

    auto timestamp = std::chrono::system_clock::now();

    std::cout << "std::chrono types work correctly\n";

    // Test struct instantiation
    generated::queries::InsertDateTimeTypesIn insertInput;
    insertInput.param1 = 1;
    insertInput.param2 = date;
    insertInput.param3 = std::chrono::year_month_day{
        std::chrono::year{2025}, std::chrono::March, std::chrono::day{20}
    };
    insertInput.param4 = time;
    insertInput.param5 = timestamp;
    insertInput.param6 = std::chrono::system_clock::now();
    insertInput.param7 = "Test";

    std::cout << "Struct instantiation works\n";

    // Test QueryDescriptor access
    static_assert(generated::queries::QueryDescriptor<generated::queries::QueryId::InsertDateTimeTypes>::id
                  == generated::queries::QueryId::InsertDateTimeTypes);

    auto queryName = generated::queries::QueryDescriptor<generated::queries::QueryId::InsertDateTimeTypes>::name;
    std::cout << "Query name: " << queryName << "\n";

    // Test output struct
    generated::queries::SelectAllDateTimeOut output;
    output.fId = 42;
    output.fDate = date;
    output.fTime = time;
    output.fTimestamp = timestamp;
    output.fName = "Output test";

    std::cout << "Output struct assignment works\n";

    // Test StructDescriptor exists (compile-time check)
    using InsertDesc = fbpp::core::StructDescriptor<generated::queries::InsertDateTimeTypesIn>;
    constexpr auto fieldCount = std::tuple_size_v<decltype(InsertDesc::fields)>;
    std::cout << "InsertDateTimeTypesIn has " << fieldCount << " fields\n";

    static_assert(fieldCount == 7, "InsertDateTimeTypesIn should have 7 fields");

    std::cout << "\n✓ All compilation tests passed!\n";
    std::cout << "✓ std::chrono types work correctly\n";
    std::cout << "✓ Structs with std::chrono adapters compile\n";
    std::cout << "✓ QueryDescriptor specializations work\n";
    std::cout << "✓ StructDescriptor specializations exist\n";

    return 0;
}
