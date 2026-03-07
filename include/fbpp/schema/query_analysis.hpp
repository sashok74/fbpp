#pragma once

#include "fbpp/core/message_metadata.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fbpp::schema {

enum class QueryKind {
    select_query,
    insert_query,
    update_query,
    delete_query,
    execute_procedure,
    execute_block,
    ddl,
    unknown
};

struct QueryParameterInfo {
    std::string sqlName;
    std::string memberName;
    fbpp::core::FieldInfo field;
    std::size_t ordinal = 0;
    bool repeated = false;
};

struct QueryResultFieldInfo {
    std::string sqlName;
    std::string memberName;
    fbpp::core::FieldInfo field;
    std::size_t ordinal = 0;
};

struct QueryAnalysis {
    std::string originalSql;
    std::string positionalSql;
    QueryKind kind = QueryKind::unknown;
    bool hasNamedParameters = false;
    std::vector<QueryParameterInfo> inputParams;
    std::vector<QueryResultFieldInfo> outputFields;
};

} // namespace fbpp::schema
