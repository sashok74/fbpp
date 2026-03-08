#include <fbpp/schema/adapter_config.hpp>
#include <fbpp/schema/type_mapper.hpp>
#include <fbpp/schema/query_analysis.hpp>
#include <fbpp/schema/query_analyzer.hpp>
#include <fbpp/schema/schema_types.hpp>
#include <fbpp/schema/schema_inspector.hpp>

int main() {
    fbpp::schema::AdapterConfig config;
    fbpp::schema::QueryKind kind = fbpp::schema::QueryKind::unknown;
    fbpp::schema::QueryAnalysis analysis{};
    fbpp::schema::TableInfo table{};
    fbpp::schema::ProcedureInfo proc{};
    fbpp::schema::SequenceInfo seq{};
    (void)config; (void)kind; (void)analysis;
    (void)table; (void)proc; (void)seq;
    return 0;
}
