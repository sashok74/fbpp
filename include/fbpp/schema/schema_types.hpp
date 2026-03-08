#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fbpp::schema {

/// Firebird relation type — matches RDB$RELATIONS.RDB$RELATION_TYPE values.
enum class RelationType : int16_t {
    table                = 0,
    view                 = 1,
    external_table       = 2,
    monitoring_table     = 3,
    global_temp_delete   = 4,
    global_temp_preserve = 5,
    virtual_table        = 6,
    unknown              = -1
};

/// A single column as described by RDB$RELATION_FIELDS + RDB$FIELDS.
struct ColumnInfo {
    std::string name;
    int position;       ///< 0-based position in the relation
    int16_t sqlType;    ///< SQL wire-type code (SQL_SHORT=500, SQL_LONG=496, …)
    int scale;          ///< Negative = decimal places; 0 = exact integer
    int length;         ///< Field length in bytes
    int charsetId;      ///< Character set ID (0 for non-char fields)
    int subType;        ///< BLOB sub-type or numeric sub-type
    bool notNull;       ///< True when the column carries a NOT NULL constraint
    bool computed;      ///< True when the column is a computed/virtual column
};

enum class SortOrder { ascending, descending };

struct IndexSegment {
    std::string columnName;
    int position;  ///< 0-based position within the index key
};

struct IndexInfo {
    std::string name;
    bool unique;
    bool active;
    SortOrder sortOrder;
    std::vector<IndexSegment> segments;
};

enum class ConstraintType {
    primary_key,
    unique_key,
    foreign_key,
    check_constraint,
    not_null_constraint
};

struct ForeignKeyInfo {
    std::string referencedTable;  ///< Table that holds the referenced PK/UQ
    std::string referencedIndex;  ///< The PK/UQ constraint name on the referenced table
    std::string updateRule;       ///< RESTRICT / CASCADE / SET NULL / SET DEFAULT / NO ACTION
    std::string deleteRule;
};

struct ConstraintInfo {
    std::string name;
    ConstraintType type;
    std::string indexName;             ///< Backing index (empty for CHECK / NOT NULL)
    std::vector<std::string> columns;  ///< Columns covered by PK / UQ / FK
    std::optional<ForeignKeyInfo> foreignKey;
};

struct TableInfo {
    std::string name;
    RelationType relationType;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
    std::vector<ConstraintInfo> constraints;
};

enum class ParameterDirection { input, output };

struct ProcedureParamInfo {
    std::string name;
    int position;
    ParameterDirection direction;
    int16_t sqlType;
    int scale;
    int length;
    int charsetId;
    int subType;
    bool notNull;
};

struct ProcedureInfo {
    std::string name;
    std::vector<ProcedureParamInfo> inputParams;
    std::vector<ProcedureParamInfo> outputParams;
};

struct SequenceInfo {
    std::string name;
    std::int64_t currentValue;
    std::int64_t increment;
};

} // namespace fbpp::schema
