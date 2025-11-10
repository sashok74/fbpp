#pragma once

#include "fbpp/core/connection.hpp"
#include "fbpp/core/message_metadata.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fbpp::core {

struct QueryDefinition {
    std::string name;
    std::string sql;
};

struct TypeMapping {
    std::string cppType;
    bool needsOptional = false;
    bool needsString = false;
    bool needsExtendedTypes = false;
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

    std::vector<QuerySpec> buildQuerySpecs(const std::vector<QueryDefinition>& definitions) const;

private:
    Connection& connection_;
};

std::string renderQueryGeneratorMainHeader(const std::vector<QuerySpec>& specs,
                                           std::string_view supportHeaderName);
std::string renderQueryGeneratorSupportHeader(const std::vector<QuerySpec>& specs);

} // namespace fbpp::core
