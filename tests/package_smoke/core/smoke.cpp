#include <fbpp/fbpp.hpp>
#include <fbpp/fbpp_extended.hpp>
#include <fbpp/fbpp_all.hpp>
#include <fbpp_util/trace.h>

int main() {
    fbpp::core::ConnectionParams params;
    params.options.statementCache.enabled = false;

    fbpp::core::StatementCacheConfig cache{};
    fbpp::core::TimeTz timeTz{};
    fbpp::core::FirebirdException exception("package smoke");
    fbpp::adapters::TTNumeric<1, -2> numeric{};

    (void)cache;
    (void)timeTz;
    (void)numeric;

    if (!exception.getStatusVector().empty()) {
        return 1;
    }

    return fbpp::util::getTraceSink() == nullptr && params.charset == "UTF8" ? 0 : 1;
}
