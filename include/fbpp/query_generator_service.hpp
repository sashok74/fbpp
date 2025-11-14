#pragma once

#include "fbpp/core/connection.hpp"
#include "fbpp/core/message_metadata.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fbpp::core {

struct AdapterConfig {
    bool useTTMathNumeric = false;
    bool useTTMathInt128 = false;
    bool useChronoDatetime = false;
    bool useCppDecimalDecFloat = false;
    bool generateAliases = true;
};

struct QueryDefinition {
    std::string name;
    std::string sql;
};

struct TypeMapping {
    struct ScaledNumericInfo {
        int intWords;  // For TTNumeric<IntWords, Scale>
        int16_t scale;
    };

    std::string cppType;
    bool needsOptional = false;
    bool needsString = false;
    bool needsExtendedTypes = false;

    // Adapter flags
    bool needsTTMath = false;
    bool needsChrono = false;
    bool needsCppDecimal = false;

    // For TTNumeric type aliases
    std::optional<ScaledNumericInfo> scaledInfo;
};

struct FieldSpec {
    std::string sqlName;
    std::string memberName;
    TypeMapping type;
    FieldInfo info;
};

struct QuerySpec {
    std::string name;
    std::string originalSql;
    std::vector<FieldSpec> inputs;
    std::vector<FieldSpec> outputs;
    bool hasNamedParameters = false;
    std::string positionalSql;
};

class QueryGeneratorService {
public:
    explicit QueryGeneratorService(Connection& connection);

    std::vector<QuerySpec> buildQuerySpecs(const std::vector<QueryDefinition>& definitions,
                                           const AdapterConfig& config = {}) const;

private:
    Connection& connection_;
};

std::string renderQueryGeneratorMainHeader(const std::vector<QuerySpec>& specs,
                                           std::string_view supportHeaderName,
                                           const AdapterConfig& config = {});
std::string renderQueryGeneratorSupportHeader(const std::vector<QuerySpec>& specs,
                                              const AdapterConfig& config = {});

} // namespace fbpp::core
