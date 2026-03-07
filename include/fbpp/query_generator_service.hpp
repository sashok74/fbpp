#pragma once

#include "fbpp/core/connection.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/schema/adapter_config.hpp"
#include "fbpp/schema/type_mapper.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fbpp::core {

// Backward-compatibility aliases — types now live in fbpp::schema
using AdapterConfig = fbpp::schema::AdapterConfig;
using TypeMapping = fbpp::schema::CppTypeInfo;

struct QueryDefinition {
    std::string name;
    std::string sql;
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
