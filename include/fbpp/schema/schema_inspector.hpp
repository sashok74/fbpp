#pragma once

#include "fbpp/schema/schema_types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace fbpp::core {
class Connection;
}

namespace fbpp::schema {

/// Read-only schema introspection via Firebird RDB$* system tables.
///
/// Each method opens its own transaction internally (StartTransaction / Commit).
/// The SchemaInspector holds only a reference to the Connection and does not
/// cache or hold any Firebird resources between calls.
///
/// **Identifier case handling (v1 limitation)**
/// All name lookups convert the supplied string to uppercase before querying,
/// which matches Firebird's default storage of unquoted identifiers.
/// Quoted identifiers that contain lowercase letters or special characters
/// (e.g. `CREATE TABLE "myTable"`) are stored as-is in RDB$* tables and are
/// NOT reachable via this API in v1.
class SchemaInspector {
public:
    explicit SchemaInspector(fbpp::core::Connection& connection);

    // ---- Tables & views --------------------------------------------------

    /// Returns names of all user tables, sorted alphabetically.
    std::vector<std::string> getTableNames() const;

    /// Returns names of all user views, sorted alphabetically.
    std::vector<std::string> getViewNames() const;

    /// Returns true when a user table with the given name exists.
    /// The comparison is case-insensitive; names are uppercased internally.
    bool tableExists(std::string_view name) const;

    /// Returns true when a user view with the given name exists.
    bool viewExists(std::string_view name) const;

    /// Returns full metadata for a table or view: columns, indexes, constraints.
    /// If the relation does not exist, returns a TableInfo with
    /// relationType == RelationType::unknown and empty collections.
    TableInfo getTableInfo(std::string_view name) const;

    // ---- Stored procedures -----------------------------------------------

    /// Returns names of all user stored procedures, sorted alphabetically.
    std::vector<std::string> getProcedureNames() const;

    /// Returns true when a user stored procedure with the given name exists.
    bool procedureExists(std::string_view name) const;

    /// Returns input/output parameter metadata for a stored procedure.
    ProcedureInfo getProcedureInfo(std::string_view name) const;

    // ---- Sequences (generators) ------------------------------------------

    /// Returns metadata for all user sequences: name, current value, increment.
    ///
    /// SequenceInfo::currentValue is the raw internal counter, identical to
    /// what GEN_ID(name, 0) returns.  For a freshly created sequence the value
    /// may be negative: Firebird stores (startWith - increment) so that the
    /// first NEXT VALUE FOR call returns startWith.  Calling this method does
    /// NOT advance any sequence.
    std::vector<SequenceInfo> getSequences() const;

    /// Returns true when a user sequence with the given name exists.
    bool sequenceExists(std::string_view name) const;

private:
    fbpp::core::Connection& connection_;
};

} // namespace fbpp::schema
