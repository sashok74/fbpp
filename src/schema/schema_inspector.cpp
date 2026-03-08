#include "fbpp/schema/schema_inspector.hpp"

#include "fbpp/core/connection.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/transaction.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>

namespace fbpp::schema {

namespace {

std::string toUpper(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

/// Map RDB$FIELD_TYPE (BLR type code stored in system tables) to the SQL wire
/// type used in Firebird's client API (SQL_SHORT, SQL_LONG, etc. from sqlda_pub.h).
/// These are the same values reported by IMessageMetadata::getType().
int16_t rdbFieldTypeToSqlType(int32_t rdbType) {
    switch (rdbType) {
        case 7:   return SQL_SHORT;        // SMALLINT
        case 8:   return SQL_LONG;         // INTEGER
        case 10:  return SQL_FLOAT;        // FLOAT
        case 12:  return SQL_TYPE_DATE;    // DATE
        case 13:  return SQL_TYPE_TIME;    // TIME
        case 14:  return SQL_TEXT;         // CHAR
        case 16:  return SQL_INT64;        // BIGINT / NUMERIC / DECIMAL
        case 23:  return SQL_BOOLEAN;      // BOOLEAN
        case 24:  return SQL_DEC16;        // DECFLOAT(16)
        case 25:  return SQL_DEC34;        // DECFLOAT(34)
        case 26:  return SQL_INT128;       // INT128
        case 27:  return SQL_DOUBLE;       // DOUBLE PRECISION
        case 28:  return SQL_TIME_TZ;      // TIME WITH TIME ZONE
        case 29:  return SQL_TIMESTAMP_TZ; // TIMESTAMP WITH TIME ZONE
        case 35:  return SQL_TIMESTAMP;    // TIMESTAMP
        case 37:  return SQL_VARYING;      // VARCHAR
        case 261: return SQL_BLOB;         // BLOB
        default:  return static_cast<int16_t>(rdbType);
    }
}

} // namespace

SchemaInspector::SchemaInspector(fbpp::core::Connection& connection)
    : connection_(connection) {}

// ---- Tables & views ------------------------------------------------------

std::vector<std::string> SchemaInspector::getTableNames() const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT TRIM(RDB$RELATION_NAME) FROM RDB$RELATIONS"
        " WHERE RDB$SYSTEM_FLAG = 0 AND RDB$RELATION_TYPE = 0"
        " ORDER BY RDB$RELATION_NAME");
    auto rs = transaction->openCursor(stmt);

    std::vector<std::string> names;
    std::tuple<std::string> row;
    while (rs->fetch(row)) {
        names.push_back(std::get<0>(row));
    }
    transaction->Commit();
    return names;
}

std::vector<std::string> SchemaInspector::getViewNames() const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT TRIM(RDB$RELATION_NAME) FROM RDB$RELATIONS"
        " WHERE RDB$SYSTEM_FLAG = 0 AND RDB$RELATION_TYPE = 1"
        " ORDER BY RDB$RELATION_NAME");
    auto rs = transaction->openCursor(stmt);

    std::vector<std::string> names;
    std::tuple<std::string> row;
    while (rs->fetch(row)) {
        names.push_back(std::get<0>(row));
    }
    transaction->Commit();
    return names;
}

bool SchemaInspector::tableExists(std::string_view name) const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT COUNT(*) FROM RDB$RELATIONS"
        " WHERE RDB$SYSTEM_FLAG = 0 AND RDB$RELATION_TYPE = 0"
        " AND TRIM(RDB$RELATION_NAME) = ?");
    auto rs = transaction->openCursor(stmt, std::make_tuple(toUpper(name)));
    std::tuple<int32_t> row;
    bool found = rs->fetch(row) && std::get<0>(row) > 0;
    transaction->Commit();
    return found;
}

bool SchemaInspector::viewExists(std::string_view name) const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT COUNT(*) FROM RDB$RELATIONS"
        " WHERE RDB$SYSTEM_FLAG = 0 AND RDB$RELATION_TYPE = 1"
        " AND TRIM(RDB$RELATION_NAME) = ?");
    auto rs = transaction->openCursor(stmt, std::make_tuple(toUpper(name)));
    std::tuple<int32_t> row;
    bool found = rs->fetch(row) && std::get<0>(row) > 0;
    transaction->Commit();
    return found;
}

TableInfo SchemaInspector::getTableInfo(std::string_view name) const {
    const std::string upperName = toUpper(name);
    TableInfo info;
    info.name = upperName;
    info.relationType = RelationType::unknown;

    auto transaction = connection_.StartTransaction();

    // 1. Determine relation type
    {
        auto stmt = connection_.prepareStatement(
            "SELECT CAST(COALESCE(RDB$RELATION_TYPE, 0) AS INTEGER)"
            " FROM RDB$RELATIONS WHERE TRIM(RDB$RELATION_NAME) = ?");
        auto rs = transaction->openCursor(stmt, std::make_tuple(upperName));
        std::tuple<int32_t> row;
        if (rs->fetch(row)) {
            switch (std::get<0>(row)) {
                case 0: info.relationType = RelationType::table; break;
                case 1: info.relationType = RelationType::view; break;
                case 2: info.relationType = RelationType::external_table; break;
                case 3: info.relationType = RelationType::monitoring_table; break;
                case 4: info.relationType = RelationType::global_temp_delete; break;
                case 5: info.relationType = RelationType::global_temp_preserve; break;
                case 6: info.relationType = RelationType::virtual_table; break;
                default: info.relationType = RelationType::unknown; break;
            }
        }
    }

    if (info.relationType == RelationType::unknown) {
        transaction->Commit();
        return info;
    }

    // 2. Columns
    {
        auto stmt = connection_.prepareStatement(
            "SELECT TRIM(rf.RDB$FIELD_NAME),"
            "       CAST(rf.RDB$FIELD_POSITION AS INTEGER),"
            "       CAST(f.RDB$FIELD_TYPE AS INTEGER),"
            "       CAST(f.RDB$FIELD_SCALE AS INTEGER),"
            "       CAST(f.RDB$FIELD_LENGTH AS INTEGER),"
            "       CAST(COALESCE(f.RDB$CHARACTER_SET_ID, 0) AS INTEGER),"
            "       CAST(COALESCE(f.RDB$FIELD_SUB_TYPE, 0) AS INTEGER),"
            "       CASE WHEN rf.RDB$NULL_FLAG = 1 THEN 1 ELSE 0 END,"
            "       CASE WHEN f.RDB$COMPUTED_BLR IS NOT NULL THEN 1 ELSE 0 END"
            " FROM RDB$RELATION_FIELDS rf"
            " JOIN RDB$FIELDS f ON f.RDB$FIELD_NAME = rf.RDB$FIELD_SOURCE"
            " WHERE TRIM(rf.RDB$RELATION_NAME) = ?"
            " ORDER BY rf.RDB$FIELD_POSITION");
        auto rs = transaction->openCursor(stmt, std::make_tuple(upperName));

        using ColRow = std::tuple<std::string, int32_t, int32_t, int32_t,
                                  int32_t, int32_t, int32_t, int32_t, int32_t>;
        ColRow row;
        while (rs->fetch(row)) {
            ColumnInfo col;
            col.name      = std::get<0>(row);
            col.position  = std::get<1>(row);
            col.sqlType   = rdbFieldTypeToSqlType(std::get<2>(row));
            col.scale     = std::get<3>(row);
            col.length    = std::get<4>(row);
            col.charsetId = std::get<5>(row);
            col.subType   = std::get<6>(row);
            col.notNull   = (std::get<7>(row) != 0);
            col.computed  = (std::get<8>(row) != 0);
            info.columns.push_back(std::move(col));
        }
    }

    // 3. Indexes — collect metadata first, then fetch segments per index
    {
        struct IdxMeta {
            std::string name;
            bool unique;
            bool active;
            SortOrder sortOrder;
        };
        std::vector<IdxMeta> idxMeta;
        {
            auto stmt = connection_.prepareStatement(
                "SELECT TRIM(i.RDB$INDEX_NAME),"
                "       CAST(COALESCE(i.RDB$UNIQUE_FLAG, 0) AS INTEGER),"
                "       CAST(COALESCE(i.RDB$INDEX_INACTIVE, 0) AS INTEGER),"
                "       CAST(COALESCE(i.RDB$INDEX_TYPE, 0) AS INTEGER)"
                " FROM RDB$INDICES i"
                " WHERE TRIM(i.RDB$RELATION_NAME) = ?"
                " ORDER BY i.RDB$INDEX_NAME");
            auto rs = transaction->openCursor(stmt, std::make_tuple(upperName));
            using IdxRow = std::tuple<std::string, int32_t, int32_t, int32_t>;
            IdxRow row;
            while (rs->fetch(row)) {
                idxMeta.push_back({
                    std::get<0>(row),
                    std::get<1>(row) != 0,
                    std::get<2>(row) == 0,  // RDB$INDEX_INACTIVE: 0 = active
                    std::get<3>(row) == 1 ? SortOrder::descending : SortOrder::ascending
                });
            }
        }

        for (auto& m : idxMeta) {
            IndexInfo idx;
            idx.name      = m.name;
            idx.unique    = m.unique;
            idx.active    = m.active;
            idx.sortOrder = m.sortOrder;

            auto segStmt = connection_.prepareStatement(
                "SELECT TRIM(RDB$FIELD_NAME), CAST(RDB$FIELD_POSITION AS INTEGER)"
                " FROM RDB$INDEX_SEGMENTS WHERE TRIM(RDB$INDEX_NAME) = ?"
                " ORDER BY RDB$FIELD_POSITION");
            auto segRs = transaction->openCursor(segStmt, std::make_tuple(m.name));
            using SegRow = std::tuple<std::string, int32_t>;
            SegRow segRow;
            while (segRs->fetch(segRow)) {
                idx.segments.push_back({std::get<0>(segRow), std::get<1>(segRow)});
            }
            info.indexes.push_back(std::move(idx));
        }
    }

    // 4. Constraints — collect metadata first, then fetch columns per constraint
    {
        struct ConMeta {
            std::string name, typeStr, indexName;
            std::string refConstraintName, refTableName, updateRule, deleteRule;
        };
        std::vector<ConMeta> conMeta;
        {
            auto stmt = connection_.prepareStatement(
                "SELECT TRIM(c.RDB$CONSTRAINT_NAME),"
                "       TRIM(c.RDB$CONSTRAINT_TYPE),"
                "       COALESCE(TRIM(c.RDB$INDEX_NAME), ''),"
                "       COALESCE(TRIM(rc.RDB$CONST_NAME_UQ), ''),"
                "       COALESCE(TRIM(pk.RDB$RELATION_NAME), ''),"
                "       COALESCE(TRIM(rc.RDB$UPDATE_RULE), ''),"
                "       COALESCE(TRIM(rc.RDB$DELETE_RULE), '')"
                " FROM RDB$RELATION_CONSTRAINTS c"
                " LEFT JOIN RDB$REF_CONSTRAINTS rc"
                "   ON rc.RDB$CONSTRAINT_NAME = c.RDB$CONSTRAINT_NAME"
                " LEFT JOIN RDB$RELATION_CONSTRAINTS pk"
                "   ON pk.RDB$CONSTRAINT_NAME = rc.RDB$CONST_NAME_UQ"
                " WHERE TRIM(c.RDB$RELATION_NAME) = ?"
                " ORDER BY c.RDB$CONSTRAINT_NAME");
            auto rs = transaction->openCursor(stmt, std::make_tuple(upperName));
            using ConRow = std::tuple<std::string, std::string, std::string,
                                      std::string, std::string, std::string, std::string>;
            ConRow row;
            while (rs->fetch(row)) {
                conMeta.push_back({
                    std::get<0>(row), std::get<1>(row), std::get<2>(row),
                    std::get<3>(row), std::get<4>(row), std::get<5>(row), std::get<6>(row)
                });
            }
        }

        for (auto& m : conMeta) {
            ConstraintInfo con;
            con.name      = m.name;
            con.indexName = m.indexName;

            if      (m.typeStr == "PRIMARY KEY") con.type = ConstraintType::primary_key;
            else if (m.typeStr == "UNIQUE")      con.type = ConstraintType::unique_key;
            else if (m.typeStr == "FOREIGN KEY") con.type = ConstraintType::foreign_key;
            else if (m.typeStr == "CHECK")       con.type = ConstraintType::check_constraint;
            else                                 con.type = ConstraintType::not_null_constraint;

            if (con.type == ConstraintType::foreign_key) {
                ForeignKeyInfo fk;
                fk.referencedTable = m.refTableName;
                fk.referencedIndex = m.refConstraintName;
                fk.updateRule      = m.updateRule;
                fk.deleteRule      = m.deleteRule;
                con.foreignKey     = std::move(fk);
            }

            if (!con.indexName.empty()) {
                auto colStmt = connection_.prepareStatement(
                    "SELECT TRIM(RDB$FIELD_NAME)"
                    " FROM RDB$INDEX_SEGMENTS WHERE TRIM(RDB$INDEX_NAME) = ?"
                    " ORDER BY RDB$FIELD_POSITION");
                auto colRs = transaction->openCursor(colStmt, std::make_tuple(con.indexName));
                std::tuple<std::string> colRow;
                while (colRs->fetch(colRow)) {
                    con.columns.push_back(std::get<0>(colRow));
                }
            }
            info.constraints.push_back(std::move(con));
        }
    }

    transaction->Commit();
    return info;
}

// ---- Stored procedures ---------------------------------------------------

std::vector<std::string> SchemaInspector::getProcedureNames() const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT TRIM(RDB$PROCEDURE_NAME) FROM RDB$PROCEDURES"
        " WHERE RDB$SYSTEM_FLAG = 0 ORDER BY RDB$PROCEDURE_NAME");
    auto rs = transaction->openCursor(stmt);

    std::vector<std::string> names;
    std::tuple<std::string> row;
    while (rs->fetch(row)) {
        names.push_back(std::get<0>(row));
    }
    transaction->Commit();
    return names;
}

bool SchemaInspector::procedureExists(std::string_view name) const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT COUNT(*) FROM RDB$PROCEDURES"
        " WHERE RDB$SYSTEM_FLAG = 0 AND TRIM(RDB$PROCEDURE_NAME) = ?");
    auto rs = transaction->openCursor(stmt, std::make_tuple(toUpper(name)));
    std::tuple<int32_t> row;
    bool found = rs->fetch(row) && std::get<0>(row) > 0;
    transaction->Commit();
    return found;
}

ProcedureInfo SchemaInspector::getProcedureInfo(std::string_view name) const {
    const std::string upperName = toUpper(name);
    ProcedureInfo info;
    info.name = upperName;

    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT TRIM(p.RDB$PARAMETER_NAME),"
        "       CAST(p.RDB$PARAMETER_NUMBER AS INTEGER),"
        "       CAST(p.RDB$PARAMETER_TYPE AS INTEGER),"
        "       CAST(f.RDB$FIELD_TYPE AS INTEGER),"
        "       CAST(f.RDB$FIELD_SCALE AS INTEGER),"
        "       CAST(f.RDB$FIELD_LENGTH AS INTEGER),"
        "       CAST(COALESCE(f.RDB$CHARACTER_SET_ID, 0) AS INTEGER),"
        "       CAST(COALESCE(f.RDB$FIELD_SUB_TYPE, 0) AS INTEGER),"
        "       CAST(COALESCE(p.RDB$NULL_FLAG, 0) AS INTEGER)"
        " FROM RDB$PROCEDURE_PARAMETERS p"
        " JOIN RDB$FIELDS f ON f.RDB$FIELD_NAME = p.RDB$FIELD_SOURCE"
        " WHERE TRIM(p.RDB$PROCEDURE_NAME) = ?"
        " ORDER BY p.RDB$PARAMETER_TYPE, p.RDB$PARAMETER_NUMBER");
    auto rs = transaction->openCursor(stmt, std::make_tuple(upperName));

    using ParamRow = std::tuple<std::string, int32_t, int32_t, int32_t,
                                int32_t, int32_t, int32_t, int32_t, int32_t>;
    ParamRow row;
    while (rs->fetch(row)) {
        ProcedureParamInfo param;
        param.name      = std::get<0>(row);
        param.position  = std::get<1>(row);
        param.direction = (std::get<2>(row) == 0)
                              ? ParameterDirection::input
                              : ParameterDirection::output;
        param.sqlType   = rdbFieldTypeToSqlType(std::get<3>(row));
        param.scale     = std::get<4>(row);
        param.length    = std::get<5>(row);
        param.charsetId = std::get<6>(row);
        param.subType   = std::get<7>(row);
        param.notNull   = (std::get<8>(row) != 0);

        if (param.direction == ParameterDirection::input) {
            info.inputParams.push_back(std::move(param));
        } else {
            info.outputParams.push_back(std::move(param));
        }
    }
    transaction->Commit();
    return info;
}

// ---- Sequences (generators) ----------------------------------------------

std::vector<SequenceInfo> SchemaInspector::getSequences() const {
    auto transaction = connection_.StartTransaction();

    // Phase 1: collect names and increments
    std::vector<SequenceInfo> result;
    {
        auto stmt = connection_.prepareStatement(
            "SELECT TRIM(RDB$GENERATOR_NAME),"
            "       CAST(COALESCE(RDB$GENERATOR_INCREMENT, 1) AS BIGINT)"
            " FROM RDB$GENERATORS WHERE RDB$SYSTEM_FLAG = 0"
            " ORDER BY RDB$GENERATOR_NAME");
        auto rs = transaction->openCursor(stmt);
        using GenRow = std::tuple<std::string, int64_t>;
        GenRow row;
        while (rs->fetch(row)) {
            SequenceInfo info;
            info.name        = std::get<0>(row);
            info.increment   = std::get<1>(row);
            info.currentValue = 0;
            result.push_back(std::move(info));
        }
    }

    // Phase 2: fetch current value per generator via GEN_ID(name, 0).
    // The generator name is embedded directly into the SQL string because
    // GEN_ID() requires a literal identifier, not a parameter placeholder.
    // v1 limitation: quoted identifiers (containing lowercase or special
    // characters) will produce invalid SQL here.  See class-level doc.
    for (auto& info : result) {
        auto valStmt = connection_.prepareStatement(
            "SELECT GEN_ID(" + info.name + ", 0) FROM RDB$DATABASE");
        auto valRs = transaction->openCursor(valStmt);
        std::tuple<int64_t> valRow;
        if (valRs->fetch(valRow)) {
            info.currentValue = std::get<0>(valRow);
        }
    }

    transaction->Commit();
    return result;
}

bool SchemaInspector::sequenceExists(std::string_view name) const {
    auto transaction = connection_.StartTransaction();
    auto stmt = connection_.prepareStatement(
        "SELECT COUNT(*) FROM RDB$GENERATORS"
        " WHERE RDB$SYSTEM_FLAG = 0 AND TRIM(RDB$GENERATOR_NAME) = ?");
    auto rs = transaction->openCursor(stmt, std::make_tuple(toUpper(name)));
    std::tuple<int32_t> row;
    bool found = rs->fetch(row) && std::get<0>(row) > 0;
    transaction->Commit();
    return found;
}

} // namespace fbpp::schema
