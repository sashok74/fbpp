#include <fbpp/query_generator_service.hpp>

int main() {
    fbpp::core::AdapterConfig config;
    config.generateAliases = true;

    const auto mainHeader =
        fbpp::core::renderQueryGeneratorMainHeader({}, "queries.structs.generated.hpp", config);
    const auto supportHeader = fbpp::core::renderQueryGeneratorSupportHeader({}, config);

    return mainHeader.empty() || supportHeader.empty() ? 1 : 0;
}
