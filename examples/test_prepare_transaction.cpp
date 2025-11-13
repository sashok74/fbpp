/**
 * @file test_prepare_transaction.cpp
 * @brief Test to verify if prepareStatement needs an active transaction
 *
 * According to Firebird OO API documentation:
 * - IAttachment::prepare() can take transaction or nullptr
 * - Our wrapper now has unified method:
 *   - prepareStatement(sql) - uses cache with shared_ptr<Statement>
 *   - Internally uses nullptr for transaction when preparing
 */

#include <iostream>
#include <memory>
#include <fbpp/fbpp.hpp>
#include <fbpp_util/connection_helper.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace fbpp::core;

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n\n";
}

int main() {
    printHeader("Test: prepareStatement with and without transaction");

    try {
        // Загрузка конфигурации
        auto params = fbpp::util::getConnectionParams("tests.persistent_db");
        std::cout << "Configuration loaded successfully\n";
        std::cout << "  Database: " << params.database << "\n";
        std::cout << "  User: " << params.user << "\n\n";

        auto connection = std::make_unique<Connection>(params);
        std::cout << "Connected to: " << params.database << "\n\n";

        // ====================================================================
        // ТЕСТ 1: prepareStatement БЕЗ активной транзакции
        // ====================================================================
        printHeader("TEST 1: prepareStatement WITHOUT active transaction");

        try {
            // Убедимся, что нет активной транзакции
            std::cout << "Preparing statement without any transaction...\n";

            auto stmt1 = connection->prepareStatement(
                "SELECT COUNT(*) FROM TABLE_TEST_1 WHERE F_INTEGER > ?"
            );

            std::cout << "✓ Statement prepared successfully without transaction\n";

            // Попробуем выполнить этот statement с транзакцией
            auto trans1 = connection->StartTransaction();
            auto cursor1 = trans1->openCursor(stmt1, std::make_tuple(0));

            std::tuple<int32_t> result1;
            if (cursor1->fetch(result1)) {
                std::cout << "✓ Can execute statement prepared without transaction\n";
                std::cout << "  Count: " << std::get<0>(result1) << "\n";
            }
            cursor1->close();
            trans1->Commit();

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        // ====================================================================
        // ТЕСТ 2: prepareStatement С активной транзакцией
        // ====================================================================
        printHeader("TEST 2: prepareStatement WITH active transaction");

        try {
            auto trans2 = connection->StartTransaction();
            std::cout << "Transaction started\n";
            std::cout << "Preparing statement with active transaction...\n";

            auto stmt2 = connection->prepareStatement(
                "SELECT COUNT(*) FROM TABLE_TEST_1 WHERE F_INTEGER < ?"
            );

            std::cout << "✓ Statement prepared successfully with active transaction\n";

            // Выполним statement в той же транзакции
            auto cursor2 = trans2->openCursor(stmt2, std::make_tuple(1000000));

            std::tuple<int32_t> result2;
            if (cursor2->fetch(result2)) {
                std::cout << "✓ Can execute statement in same transaction\n";
                std::cout << "  Count: " << std::get<0>(result2) << "\n";
            }
            cursor2->close();
            trans2->Commit();

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        // ====================================================================
        // ТЕСТ 3: Повторное использование prepared statement
        // ====================================================================
        printHeader("TEST 3: Reusing prepared statement");

        try {
            // Подготовим statement без транзакции
            auto stmt3 = connection->prepareStatement(
                "SELECT F_VARCHAR FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );
            std::cout << "Statement prepared without transaction\n\n";

            // Используем его в первой транзакции
            auto trans3a = connection->StartTransaction();
            std::cout << "First transaction started\n";

            auto cursor3a = trans3a->openCursor(stmt3, std::make_tuple(777777));
            std::tuple<std::string> result3a;
            if (cursor3a->fetch(result3a)) {
                std::cout << "✓ First execution: " << std::get<0>(result3a) << "\n";
            } else {
                std::cout << "  No data found for 777777\n";
            }
            cursor3a->close();
            trans3a->Commit();
            std::cout << "First transaction committed\n\n";

            // Используем тот же statement во второй транзакции
            auto trans3b = connection->StartTransaction();
            std::cout << "Second transaction started\n";

            auto cursor3b = trans3b->openCursor(stmt3, std::make_tuple(888888));
            std::tuple<std::string> result3b;
            if (cursor3b->fetch(result3b)) {
                std::cout << "✓ Second execution: " << std::get<0>(result3b) << "\n";
            } else {
                std::cout << "  No data found for 888888\n";
            }
            cursor3b->close();
            trans3b->Commit();
            std::cout << "Second transaction committed\n";

            std::cout << "\n✓ Same prepared statement used in multiple transactions\n";

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        // ====================================================================
        // ТЕСТ 4: execute с prepared statement
        // ====================================================================
        printHeader("TEST 4: execute with prepared statement");

        try {
            // Подготовим INSERT без транзакции
            auto stmt4 = connection->prepareStatement(
                "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR) VALUES (?, ?)"
            );
            std::cout << "INSERT statement prepared without transaction\n";

            // Выполним в транзакции
            auto trans4 = connection->StartTransaction();

            // Сначала удалим старые тестовые данные
            auto cleanup4 = connection->prepareStatement(
                "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );
            trans4->execute(cleanup4, std::make_tuple(555555));

            // Теперь вставим новую запись
            unsigned affected = trans4->execute(
                stmt4,
                std::make_tuple(555555, std::string("Test prepared without transaction"))
            );

            std::cout << "✓ INSERT executed, affected rows: " << affected << "\n";
            trans4->Commit();

            // Очистим после себя
            auto trans4b = connection->StartTransaction();
            trans4b->execute(cleanup4, std::make_tuple(555555));
            trans4b->Commit();

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        // ====================================================================
        // ТЕСТ 5: Две параллельные транзакции с одним prepared statement
        // ====================================================================
        printHeader("TEST 5: Two parallel transactions with same prepared statement");

        try {
            // Подготовим statement без транзакции
            auto stmt5 = connection->prepareStatement(
                "SELECT F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );
            std::cout << "Statement prepared without transaction\n\n";

            // Открываем ПЕРВУЮ транзакцию
            auto trans5a = connection->StartTransaction();
            std::cout << "First transaction started\n";

            // Открываем ВТОРУЮ транзакцию (обе активны одновременно)
            auto trans5b = connection->StartTransaction();
            std::cout << "Second transaction started (both are active now)\n\n";

            // Выполняем запрос в ПЕРВОЙ транзакции
            std::cout << "Executing query in FIRST transaction...\n";
            auto cursor5a = trans5a->openCursor(stmt5, std::make_tuple(888888));
            std::tuple<int32_t, std::string> result5a;
            if (cursor5a->fetch(result5a)) {
                auto [id1, varchar1] = result5a;
                std::cout << "✓ First transaction result: F_INTEGER=" << id1
                          << ", F_VARCHAR='" << varchar1 << "'\n";
            } else {
                std::cout << "  No data found in first transaction\n";
            }
            cursor5a->close();

            // Выполняем запрос во ВТОРОЙ транзакции с другим параметром
            std::cout << "\nExecuting query in SECOND transaction...\n";
            auto cursor5b = trans5b->openCursor(stmt5, std::make_tuple(555555));
            std::tuple<int32_t, std::string> result5b;
            if (cursor5b->fetch(result5b)) {
                auto [id2, varchar2] = result5b;
                std::cout << "✓ Second transaction result: F_INTEGER=" << id2
                          << ", F_VARCHAR='" << varchar2 << "'\n";
            } else {
                std::cout << "  No data found in second transaction\n";
            }
            cursor5b->close();

            // Коммитим обе транзакции
            std::cout << "\nCommitting both transactions...\n";
            trans5a->Commit();
            std::cout << "✓ First transaction committed\n";

            trans5b->Commit();
            std::cout << "✓ Second transaction committed\n";

            std::cout << "\n✓ Same prepared statement successfully used in parallel transactions\n";

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        // ====================================================================
        // ТЕСТ 6: Параллельные транзакции с DML операциями
        // ====================================================================
        printHeader("TEST 6: Parallel transactions with DML operations");

        try {
            // Подготовим INSERT и UPDATE statements
            auto insert_stmt = connection->prepareStatement(
                "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR) VALUES (?, ?)"
            );
            auto update_stmt = connection->prepareStatement(
                "UPDATE TABLE_TEST_1 SET F_VARCHAR = ? WHERE F_INTEGER = ?"
            );
            auto select_stmt = connection->prepareStatement(
                "SELECT F_VARCHAR FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );
            std::cout << "Statements prepared without transaction\n\n";

            // Сначала удалим тестовые записи
            auto cleanup_trans = connection->StartTransaction();
            auto cleanup_stmt = connection->prepareStatement(
                "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER IN (?, ?)"
            );
            auto cleanup_cursor = cleanup_trans->openCursor(cleanup_stmt,
                std::make_tuple(444444, 333333));
            cleanup_cursor->close();
            cleanup_trans->Commit();
            std::cout << "Test data cleaned up\n\n";

            // Открываем две транзакции
            auto trans6a = connection->StartTransaction();
            std::cout << "Transaction A started\n";

            auto trans6b = connection->StartTransaction();
            std::cout << "Transaction B started\n\n";

            // В транзакции A вставляем запись
            std::cout << "Transaction A: Inserting record with F_INTEGER=444444\n";
            unsigned affected_a = trans6a->execute(
                insert_stmt,
                std::make_tuple(444444, std::string("From transaction A"))
            );
            std::cout << "✓ Inserted in transaction A, affected: " << affected_a << "\n\n";

            // В транзакции B вставляем другую запись
            std::cout << "Transaction B: Inserting record with F_INTEGER=333333\n";
            unsigned affected_b = trans6b->execute(
                insert_stmt,
                std::make_tuple(333333, std::string("From transaction B"))
            );
            std::cout << "✓ Inserted in transaction B, affected: " << affected_b << "\n\n";

            // Транзакция A пытается прочитать запись из транзакции B (не должна видеть)
            std::cout << "Transaction A trying to read record from transaction B (333333):\n";
            auto cursor_a_read_b = trans6a->openCursor(select_stmt, std::make_tuple(333333));
            std::tuple<std::string> result_a_read_b;
            if (cursor_a_read_b->fetch(result_a_read_b)) {
                std::cout << "  Found (unexpected): " << std::get<0>(result_a_read_b) << "\n";
            } else {
                std::cout << "✓ Not found (correct - isolation works)\n";
            }
            cursor_a_read_b->close();

            // Транзакция B пытается прочитать запись из транзакции A (не должна видеть)
            std::cout << "\nTransaction B trying to read record from transaction A (444444):\n";
            auto cursor_b_read_a = trans6b->openCursor(select_stmt, std::make_tuple(444444));
            std::tuple<std::string> result_b_read_a;
            if (cursor_b_read_a->fetch(result_b_read_a)) {
                std::cout << "  Found (unexpected): " << std::get<0>(result_b_read_a) << "\n";
            } else {
                std::cout << "✓ Not found (correct - isolation works)\n";
            }
            cursor_b_read_a->close();

            // Коммитим транзакцию A
            std::cout << "\nCommitting transaction A...\n";
            trans6a->Commit();
            std::cout << "✓ Transaction A committed\n";

            // Теперь транзакция B должна видеть запись из A (если read committed)
            std::cout << "\nTransaction B reading again after A committed:\n";
            auto cursor_b_read_a2 = trans6b->openCursor(select_stmt, std::make_tuple(444444));
            std::tuple<std::string> result_b_read_a2;
            if (cursor_b_read_a2->fetch(result_b_read_a2)) {
                std::cout << "✓ Now found: " << std::get<0>(result_b_read_a2) << "\n";
            } else {
                std::cout << "  Still not found (depends on isolation level)\n";
            }
            cursor_b_read_a2->close();

            // Коммитим транзакцию B
            std::cout << "\nCommitting transaction B...\n";
            trans6b->Commit();
            std::cout << "✓ Transaction B committed\n";

            // Очищаем тестовые данные
            auto final_cleanup = connection->StartTransaction();
            auto cleanup_cursor2 = final_cleanup->openCursor(cleanup_stmt,
                std::make_tuple(444444, 333333));
            cleanup_cursor2->close();
            final_cleanup->Commit();
            std::cout << "\n✓ Test data cleaned up\n";

            std::cout << "\n✓ Parallel transactions with DML operations completed successfully\n";

        } catch (const std::exception& e) {
            std::cout << "✗ Error: " << e.what() << "\n";
        }

        printHeader("Summary");

        std::cout << "Key findings:\n";
        std::cout << "1. prepareStatement() works WITHOUT active transaction\n";
        std::cout << "2. Prepared statements can be reused across transactions\n";
        std::cout << "3. This is by design - Firebird allows prepare with nullptr transaction\n";
        std::cout << "4. Our wrapper correctly implements this behavior\n\n";

        std::cout << "In our wrapper:\n";
        std::cout << "- connection->prepareStatement(sql) uses nullptr for transaction\n";
        std::cout << "- This calls attachment->prepare(&st, nullptr, ...)\n";
        std::cout << "- The prepared statement can then be executed in any transaction\n";

    } catch (const fbpp::core::FirebirdException& e) {
        std::cerr << "\n✗ FirebirdException: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
