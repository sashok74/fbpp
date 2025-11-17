#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "queries.generated.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/firebird_compat.hpp"

#include <chrono>
#include <optional>
#include <iostream>
#include <string>
#include <type_traits>

using namespace fbpp::test;

namespace {

template<typename T>
struct is_optional : std::false_type {};

template<typename U>
struct is_optional<std::optional<U>> : std::true_type {};

template<typename Field>
void assertHasValue(const Field& field) {
    if constexpr (is_optional<std::decay_t<Field>>::value) {
        ASSERT_TRUE(field.has_value());
    }
}

template<typename Field>
auto getValue(const Field& field) {
    if constexpr (is_optional<std::decay_t<Field>>::value) {
        return field.value();
    } else {
        return field;
    }
}

template<typename Field, typename Value>
void assignField(Field& field, Value&& value) {
    if constexpr (is_optional<std::decay_t<Field>>::value) {
        field = std::forward<Value>(value);
    } else {
        field = std::forward<Value>(value);
    }
}

template<typename Field, typename Value>
void expectFieldEquals(const Field& field, const Value& expected) {
    if constexpr (is_optional<std::decay_t<Field>>::value) {
        ASSERT_TRUE(field.has_value());
        EXPECT_EQ(field.value(), expected);
    } else {
        EXPECT_EQ(field, expected);
    }
}

template<typename Fn>
auto runOrFail(Fn&& fn) {
    using Result = decltype(fn());
    try {
        return fn();
    } catch (const fbpp::core::FirebirdException& e) {
        ADD_FAILURE() << "FirebirdException: " << e.what();
    } catch (const std::exception& e) {
        ADD_FAILURE() << "std::exception: " << e.what();
    }
    return Result{};
}

} // namespace

class QueryGeneratorIntegrationTest : public PersistentDatabaseTest {};

TEST_F(QueryGeneratorIntegrationTest, TableTest1CrudWorkflow) {
    using namespace generated::queries;
    constexpr std::int32_t testInteger = 424242;
    const auto suffix = static_cast<long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count() % 1000000);
    const std::string initialLabel = "integration_row_" + std::to_string(suffix);
    const std::string updatedLabel = initialLabel + "_updated";

    try {
        std::cout << "[integration] Start transaction" << std::endl;
        auto transaction = connection_->StartTransaction();

        // INSERT
        std::cout << "[integration] Insert step" << std::endl;
        TABLE_TEST_1_IIn insertParams{};
        assignField(insertParams.param1, testInteger);
        assignField(insertParams.param2, initialLabel);
        assignField(insertParams.param3, true);

        auto insertCount = runOrFail([&] {
            auto statement = connection_->prepareStatement(std::string(QueryDescriptor<QueryId::TABLE_TEST_1_I>::sql));
            return transaction->execute(statement, insertParams);
        });
        ASSERT_EQ(insertCount, 1u);

        // SELECT
        std::cout << "[integration] Select step" << std::endl;
        TABLE_TEST_1_SIn selectParams{};
        assignField(selectParams.param1, testInteger);
        assignField(selectParams.param2, initialLabel);

        auto selectedRows = runOrFail([&] {
            return transaction->executeQuery<QueryDescriptor<QueryId::TABLE_TEST_1_S>>(
                selectParams);
        });
        ASSERT_EQ(selectedRows.size(), 1);

        const auto& selected = selectedRows.front();
        assertHasValue(selected.id);
        const auto newId = getValue(selected.id);
        expectFieldEquals(selected.fInteger, testInteger);
        expectFieldEquals(selected.fVarchar, initialLabel);
        expectFieldEquals(selected.fBoolean, true);

        // UPDATE
        std::cout << "[integration] Update step" << std::endl;
        TABLE_TEST_1_UIn updateParams{};
        assignField(updateParams.param1, updatedLabel);
        assignField(updateParams.param2, newId);

        auto updateCount = runOrFail([&] {
            auto statement = connection_->prepareStatement(std::string(QueryDescriptor<QueryId::TABLE_TEST_1_U>::sql));
            return transaction->execute(statement, updateParams);
        });
        ASSERT_EQ(updateCount, 1u);

        // SELECT updated row
        assignField(selectParams.param1, testInteger);
        assignField(selectParams.param2, updatedLabel);
        auto updatedRows = runOrFail([&] {
            return transaction->executeQuery<QueryDescriptor<QueryId::TABLE_TEST_1_S>>(
                selectParams);
        });
        ASSERT_EQ(updatedRows.size(), 1);
        const auto& updatedRow = updatedRows.front();
        assertHasValue(updatedRow.id);
        EXPECT_EQ(getValue(updatedRow.id), newId);
        expectFieldEquals(updatedRow.fVarchar, updatedLabel);
        expectFieldEquals(updatedRow.fBoolean, true);

        // DELETE
        std::cout << "[integration] Delete step" << std::endl;
        TABLE_TEST_1_DIn deleteParams{};
        assignField(deleteParams.param1, newId);

        auto deleteCount = runOrFail([&] {
            auto statement = connection_->prepareStatement(std::string(QueryDescriptor<QueryId::TABLE_TEST_1_D>::sql));
            return transaction->execute(statement, deleteParams);
        });
        ASSERT_EQ(deleteCount, 1u);

        // Ensure row no longer present within transaction
        std::cout << "[integration] Verify absence" << std::endl;
        assignField(selectParams.param1, testInteger);
        assignField(selectParams.param2, updatedLabel);
        auto afterDeleteRows = runOrFail([&] {
            return transaction->executeQuery<QueryDescriptor<QueryId::TABLE_TEST_1_S>>(
                selectParams);
        });
        EXPECT_TRUE(afterDeleteRows.empty());

        std::cout << "[integration] Commit" << std::endl;
        transaction->Commit();
    } catch (const fbpp::core::FirebirdException& e) {
        FAIL() << "FirebirdException: " << e.what();
    } catch (const Firebird::FbException& e) {
        fbpp::core::FirebirdException wrapped(e);
        FAIL() << "FbException: " << wrapped.what();
    } catch (const std::exception& e) {
        FAIL() << "std::exception: " << e.what();
    } catch (...) {
        FAIL() << "Unknown exception";
    }
}
