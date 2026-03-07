#include "fbpp/schema/query_analyzer.hpp"

#include "fbpp/core/named_param_parser.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace fbpp::schema {

namespace {

std::string toCamelCase(const std::string& sqlName) {
    std::string out;
    out.reserve(sqlName.size());
    bool capitalize = false;
    for (char c : sqlName) {
        if (c == '_' || c == ' ') {
            capitalize = true;
            continue;
        }
        char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (capitalize) {
            cc = static_cast<char>(std::toupper(static_cast<unsigned char>(cc)));
            capitalize = false;
        }
        out.push_back(cc);
    }
    if (!out.empty()) {
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    }
    return out;
}

// Assign a deduplicated memberName given a desired base name and a usage tracker.
std::string deduplicatedName(const std::string& baseName,
                              std::unordered_map<std::string, int>& usedNames) {
    auto [it, inserted] = usedNames.emplace(baseName, 0);
    if (inserted) {
        return baseName;
    }
    ++(it->second);
    return baseName + std::to_string(it->second + 1);
}

} // namespace

QueryAnalyzer::QueryAnalyzer(fbpp::core::Connection& connection)
    : connection_(connection) {}

QueryKind QueryAnalyzer::classifyQuery(std::string_view sql) {
    // Skip leading whitespace.
    std::size_t start = 0;
    while (start < sql.size() && std::isspace(static_cast<unsigned char>(sql[start]))) {
        ++start;
    }
    // Extract first word.
    std::size_t end = start;
    while (end < sql.size() && !std::isspace(static_cast<unsigned char>(sql[end]))) {
        ++end;
    }
    std::string firstWord(sql.substr(start, end - start));
    std::transform(firstWord.begin(), firstWord.end(), firstWord.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (firstWord == "select") return QueryKind::select_query;
    if (firstWord == "insert") return QueryKind::insert_query;
    if (firstWord == "update") return QueryKind::update_query;
    if (firstWord == "delete") return QueryKind::delete_query;
    if (firstWord == "execute") {
        // Look at the second word to distinguish PROCEDURE from BLOCK.
        std::size_t s2 = end;
        while (s2 < sql.size() && std::isspace(static_cast<unsigned char>(sql[s2]))) ++s2;
        std::size_t e2 = s2;
        while (e2 < sql.size() && !std::isspace(static_cast<unsigned char>(sql[e2]))) ++e2;
        std::string secondWord(sql.substr(s2, e2 - s2));
        std::transform(secondWord.begin(), secondWord.end(), secondWord.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (secondWord == "block") return QueryKind::execute_block;
        return QueryKind::execute_procedure;
    }
    if (firstWord == "create" || firstWord == "alter" || firstWord == "drop") {
        return QueryKind::ddl;
    }
    return QueryKind::unknown;
}

QueryAnalysis QueryAnalyzer::analyze(std::string_view sql) const {
    QueryAnalysis analysis;
    analysis.originalSql = std::string(sql);
    analysis.kind = classifyQuery(sql);

    auto parseResult = fbpp::core::NamedParamParser::parse(analysis.originalSql);
    analysis.hasNamedParameters = parseResult.hasNamedParams;
    analysis.positionalSql = parseResult.hasNamedParams
                                 ? parseResult.convertedSql
                                 : analysis.originalSql;

    auto meta = connection_.describeQuery(analysis.positionalSql);

    // Build inputParams with camelCase deduplication.
    analysis.inputParams.reserve(meta.inputFields.size());
    std::unordered_map<std::string, int> usedInputNames;
    for (std::size_t i = 0; i < meta.inputFields.size(); ++i) {
        QueryParameterInfo info;
        info.field = meta.inputFields[i];
        info.ordinal = i;

        if (parseResult.hasNamedParams && i < parseResult.parameters.size()) {
            info.sqlName = parseResult.parameters[i].name;
            auto it = parseResult.nameToPositions.find(parseResult.parameters[i].name);
            if (it != parseResult.nameToPositions.end()) {
                info.repeated = (it->second.size() > 1);
            }
        } else {
            info.sqlName = info.field.name.empty()
                               ? ("PARAM_" + std::to_string(i + 1))
                               : info.field.name;
        }

        info.memberName = deduplicatedName(toCamelCase(info.sqlName), usedInputNames);
        analysis.inputParams.push_back(std::move(info));
    }

    // Build outputFields with camelCase deduplication.
    analysis.outputFields.reserve(meta.outputFields.size());
    std::unordered_map<std::string, int> usedOutputNames;
    for (std::size_t i = 0; i < meta.outputFields.size(); ++i) {
        QueryResultFieldInfo info;
        info.field = meta.outputFields[i];
        info.ordinal = i;
        info.sqlName = info.field.name.empty()
                           ? ("FIELD_" + std::to_string(i + 1))
                           : info.field.name;
        info.memberName = deduplicatedName(toCamelCase(info.sqlName), usedOutputNames);
        analysis.outputFields.push_back(std::move(info));
    }

    return analysis;
}

std::vector<QueryParameterInfo> QueryAnalyzer::analyzeInputParams(std::string_view sql) const {
    return analyze(sql).inputParams;
}

std::vector<QueryResultFieldInfo> QueryAnalyzer::analyzeOutputFields(std::string_view sql) const {
    return analyze(sql).outputFields;
}

} // namespace fbpp::schema
