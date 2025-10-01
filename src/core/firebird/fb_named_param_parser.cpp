#include "fbpp/core/named_param_parser.hpp"
#include <algorithm>
#include <cctype>

namespace fbpp {
namespace core {

NamedParamParser::ParseResult NamedParamParser::parse(const std::string& sql) {
    ParseResult result;
    result.convertedSql.reserve(sql.size());

    size_t paramPosition = 0;
    char currentQuote = '\0';
    bool inSingleLineComment = false;
    bool inMultiLineComment = false;

    for (size_t i = 0; i < sql.size(); ++i) {
        char ch = sql[i];
        char nextCh = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        // Handle string literals
        if (currentQuote != '\0') {
            result.convertedSql += ch;
            // Check for quote escape (doubled quote)
            if (ch == currentQuote) {
                if (nextCh == currentQuote) {
                    result.convertedSql += nextCh;
                    ++i; // Skip next quote
                } else {
                    currentQuote = '\0'; // End of string
                }
            }
            continue;
        }

        // Start of string literal
        if (ch == '\'' || ch == '"') {
            currentQuote = ch;
            result.convertedSql += ch;
            continue;
        }

        // Handle comments
        if (!inSingleLineComment && !inMultiLineComment) {
            // Check for single-line comment
            if (ch == '-' && nextCh == '-') {
                inSingleLineComment = true;
                result.convertedSql += ch;
                continue;
            }
            // Check for multi-line comment start
            if (ch == '/' && nextCh == '*') {
                inMultiLineComment = true;
                result.convertedSql += ch;
                continue;
            }
        }

        // End of single-line comment
        if (inSingleLineComment && (ch == '\n' || ch == '\r')) {
            inSingleLineComment = false;
            result.convertedSql += ch;
            continue;
        }

        // End of multi-line comment
        if (inMultiLineComment && ch == '*' && nextCh == '/') {
            inMultiLineComment = false;
            result.convertedSql += ch;
            result.convertedSql += nextCh;
            ++i; // Skip next '/'
            continue;
        }

        // If we're in a comment, just copy the character
        if (inSingleLineComment || inMultiLineComment) {
            result.convertedSql += ch;
            continue;
        }

        // Check for named parameter (: or @)
        if ((ch == ':' || ch == '@') && i + 1 < sql.size()) {
            // Check if next character starts an identifier
            char next = sql[i + 1];
            if (std::isalpha(next) || next == '_') {
                // Extract parameter name
                size_t nameStart = i + 1;
                size_t nameEnd = nameStart;

                while (nameEnd < sql.size()) {
                    char c = sql[nameEnd];
                    if (std::isalnum(c) || c == '_') {
                        nameEnd++;
                    } else {
                        break;
                    }
                }

                std::string paramName = sql.substr(nameStart, nameEnd - nameStart);
                std::string paramNameLower = toLower(paramName);

                // Add parameter info
                NamedParamInfo info;
                info.name = paramNameLower;
                info.position = paramPosition;
                info.sqlOffset = i;
                result.parameters.push_back(info);

                // Update name-to-positions map
                result.nameToPositions[paramNameLower].push_back(paramPosition);

                // Replace with ?
                result.convertedSql += '?';
                i = nameEnd - 1; // Skip parameter name
                paramPosition++;
                result.hasNamedParams = true;
                continue;
            }
        }

        // Check for existing positional parameter
        if (ch == '?') {
            paramPosition++;
        }

        // Copy character as-is
        result.convertedSql += ch;
    }

    return result;
}

bool NamedParamParser::isInString(const std::string& sql, size_t pos, char& quoteChar) {
    quoteChar = '\0';
    bool inString = false;
    char currentQuote = '\0';

    for (size_t i = 0; i < pos && i < sql.size(); ++i) {
        char ch = sql[i];
        char nextCh = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        if (currentQuote != '\0') {
            // We're in a string
            if (ch == currentQuote) {
                if (nextCh == currentQuote) {
                    // Escaped quote, skip it
                    ++i;
                } else {
                    // End of string
                    currentQuote = '\0';
                    inString = false;
                }
            }
        } else {
            // Not in a string
            if (ch == '\'' || ch == '"') {
                currentQuote = ch;
                inString = true;
            }
        }
    }

    quoteChar = currentQuote;
    return inString;
}

bool NamedParamParser::isInComment(const std::string& sql, size_t pos) {
    bool inSingleLine = false;
    bool inMultiLine = false;

    for (size_t i = 0; i < pos && i < sql.size(); ++i) {
        char ch = sql[i];
        char nextCh = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        // Skip strings
        char quoteChar;
        if (isInString(sql, i, quoteChar)) {
            continue;
        }

        if (!inSingleLine && !inMultiLine) {
            if (ch == '-' && nextCh == '-') {
                inSingleLine = true;
                ++i;
            } else if (ch == '/' && nextCh == '*') {
                inMultiLine = true;
                ++i;
            }
        }

        if (inSingleLine && (ch == '\n' || ch == '\r')) {
            inSingleLine = false;
        }

        if (inMultiLine && ch == '*' && nextCh == '/') {
            inMultiLine = false;
            ++i;
        }
    }

    return inSingleLine || inMultiLine;
}

std::string NamedParamParser::extractParamName(const std::string& sql, size_t& pos) {
    if (pos >= sql.size() || (sql[pos] != ':' && sql[pos] != '@')) {
        return "";
    }

    size_t start = pos + 1;
    size_t end = start;

    // Parameter name must start with letter or underscore
    if (start >= sql.size() || (!std::isalpha(sql[start]) && sql[start] != '_')) {
        return "";
    }

    // Continue while we have alphanumeric or underscore
    while (end < sql.size() && (std::isalnum(sql[end]) || sql[end] == '_')) {
        ++end;
    }

    pos = end;
    return sql.substr(start, end - start);
}

std::string NamedParamParser::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace core
} // namespace fbpp