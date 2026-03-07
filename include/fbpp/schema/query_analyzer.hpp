#pragma once

#include "fbpp/schema/query_analysis.hpp"
#include "fbpp/core/connection.hpp"

#include <string_view>
#include <vector>

namespace fbpp::schema {

class QueryAnalyzer {
public:
    explicit QueryAnalyzer(fbpp::core::Connection& connection);

    /// Full analysis: kind, named-param substitution, input params, output fields.
    QueryAnalysis analyze(std::string_view sql) const;

    /// Convenience: input params only (calls analyze internally).
    std::vector<QueryParameterInfo> analyzeInputParams(std::string_view sql) const;

    /// Convenience: output fields only (calls analyze internally).
    std::vector<QueryResultFieldInfo> analyzeOutputFields(std::string_view sql) const;

    /// Classify a SQL statement from its leading keyword.
    /// Public and static so callers can use it without a connection.
    static QueryKind classifyQuery(std::string_view sql);

private:
    fbpp::core::Connection& connection_;
};

} // namespace fbpp::schema
