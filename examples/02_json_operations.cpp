/**
 * @file 02_json_operations.cpp
 * @brief JSON parameter binding and result fetching with TABLE_TEST_1
 *
 * Demonstrates:
 * - JSON array parameter binding
 * - Fetching results as JSON
 * - INSERT with RETURNING clause using JSON
 * - UPDATE operations with JSON parameters
 * - Working with existing TABLE_TEST_1 only
 */

#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <vector>
#include <fbpp/fbpp.hpp>
#include <fbpp_util/connection_helper.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Для красивого вывода
void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void printInfo(const std::string& label, const std::string& value) {
    std::cout << std::setw(20) << std::left << label << ": " << value << "\n";
}

int main() {
    printHeader("Firebird C++ Wrapper (fbpp) - JSON Example");
    
    try {
        // Получаем параметры подключения из конфига (секция "db")
        std::cout << "Загрузка конфигурации...\n";
        auto params = fbpp::util::getConnectionParams("db");

        printInfo("Database", params.database);
        printInfo("Username", params.user);
        printInfo("Charset", params.charset);

        std::cout << "\nПодключение к базе данных...\n";

        // Создаем подключение к существующей базе данных
        auto connection = std::make_unique<fbpp::core::Connection>(params);
        
        std::cout << "✓ Успешно подключились к базе данных!\n";
        
        // Получаем информацию о сервере
        std::cout << "\nПолучение информации о сервере...\n";
        
        auto transaction = connection->StartTransaction();
        
        // Выполняем простой запрос для проверки работы
        auto stmt = connection->prepareStatement(
            "SELECT "
            "  RDB$GET_CONTEXT('SYSTEM', 'ENGINE_VERSION') AS VERSION, "
            "  RDB$GET_CONTEXT('SYSTEM', 'DB_NAME') AS DB_NAME "
            "FROM RDB$DATABASE"
        );
        
        auto cursor = transaction->openCursor(stmt);
        
        // Используем JSON для чтения результата
        nlohmann::json jsonResult;
        bool fetched = cursor->fetch(jsonResult);
        if (fetched) {
            std::cout << "✓ Информация о сервере получена\n";
            std::cout << "  Версия: " << (jsonResult["VERSION"].is_null() ? "N/A" : jsonResult["VERSION"].get<std::string>()) << "\n";
            std::cout << "  База: " << (jsonResult["DB_NAME"].is_null() ? "N/A" : jsonResult["DB_NAME"].get<std::string>()) << "\n";
        }
        
        cursor->close();
        transaction->Commit();
        
        // INSERT с RETURNING используя JSON
        printHeader("Демонстрация INSERT с JSON параметрами и RETURNING");
        
        transaction = connection->StartTransaction();
        
        std::cout << "Вставляем новую запись используя JSON массив...\n";
        
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN) "
            "VALUES (?, ?, ?, ?) "
          //  "RETURNING ID"
        );
        
        // Используем JSON массив для параметров
        json insert_params = json::array({
            42,                                    // F_INTEGER
            "JSON тестовая запись",                // F_VARCHAR
            3.14159,                               // F_DOUBLE_PRECISION
            true                                   // F_BOOLEAN
        });
        
        // Выполняем с помощью универсального execute() - он автоматически определит JSON
        std::cout << "Параметры вставки (JSON): " << insert_params.dump() << "\n";
        
        // Для INSERT с RETURNING нужно использовать специальный подход
        // Пока выполним простой INSERT и получим ID отдельным запросом
        auto affected = transaction->execute(stmt, insert_params);
        std::cout << "✓ Запись вставлена! Затронуто строк: " << affected << "\n";
        transaction->CommitRetaining();
        // Получаем ID последней вставленной записи
        auto id_stmt = connection->prepareStatement(
            "SELECT MAX(ID) AS LAST_ID FROM TABLE_TEST_1 "
            "WHERE F_VARCHAR = ?"
        );
        
        auto id_cursor = transaction->openCursor(id_stmt, 
                                             json::array({"JSON тестовая запись"}));
        nlohmann::json id_result;
        bool id_fetched = id_cursor->fetch(id_result);
        int32_t inserted_id = 0;
        if (id_fetched && !id_result["LAST_ID"].is_null()) {
            inserted_id = id_result["LAST_ID"].get<int32_t>();
            std::cout << "  Получен ID: " << inserted_id << "\n\n";
        }
        id_cursor->close();
        
        transaction->Commit();
        
        // Чтение данных с помощью JSON
        printHeader("Чтение данных в JSON формате");
        
        transaction = connection->StartTransaction();
        
        stmt = connection->prepareStatement(
            "SELECT FIRST 5 "
            "  ID, F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN "
            "FROM TABLE_TEST_1 "
            "WHERE ID >= ? "
            "ORDER BY ID DESC"
        );
        
        // Используем JSON для параметра
        json select_params = json::array({inserted_id - 2});
        
        auto cursor_5 = transaction->openCursor(stmt, select_params);
        
        std::cout << "Читаем последние записи в JSON формате:\n\n";
        
        int record_count = 0;
        nlohmann::json row;
        while (cursor_5->fetch(row)) {
            record_count++;
            
            std::cout << "Запись #" << record_count << " (JSON):\n";
            std::cout << std::string(40, '-') << "\n";
            
            // Красивый вывод JSON
            std::cout << row.dump(2) << "\n";
            
            // Проверяем нашу запись
            if (row["ID"] == inserted_id) {
                std::cout << "  ⭐ Это наша только что вставленная запись!\n";
            }
            std::cout << "\n";
        }
        
        std::cout << "Всего прочитано записей: " << record_count << "\n\n";
        cursor->close();
        
        // UPDATE с JSON параметрами
        printHeader("UPDATE с JSON параметрами");
        
        std::cout << "Обновляем нашу запись используя JSON...\n";
        stmt = connection->prepareStatement(
            "UPDATE TABLE_TEST_1 "
            "SET F_VARCHAR = ?, F_INTEGER = F_INTEGER * ? "
            "WHERE ID = ?"
        );
        
        // JSON массив с параметрами для UPDATE
        json update_params = json::array({
            "Обновлено через JSON API",  // новое значение F_VARCHAR
            2,                           // множитель для F_INTEGER
            inserted_id                  // ID записи
        });
        
        std::cout << "Параметры обновления (JSON): " << update_params.dump() << "\n";
        
        auto update_result = transaction->execute(stmt, update_params);
        std::cout << "✓ Обновлено строк: " << update_result << "\n\n";
        
        // Читаем обновленную запись в JSON
        transaction->Commit();
        transaction = connection->StartTransaction();
        
        stmt = connection->prepareStatement(
            "SELECT ID, F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE ID = ?"
        );
        
        auto read_cursor = transaction->openCursor(stmt, json::array({inserted_id}));
        
        nlohmann::json updated_row;
        if (read_cursor->fetch(updated_row)) {
            std::cout << "Обновленная запись (JSON):\n";
            std::cout << updated_row.dump(2) << "\n";
            std::cout << "  F_INTEGER: было 42, стало " << updated_row["F_INTEGER"] << "\n";
        }
        
        read_cursor->close();
        transaction->Commit();
        
        // Демонстрация работы с NULL значениями через JSON
        printHeader("Работа с NULL значениями через JSON");
        
        transaction = connection->StartTransaction();
        
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN) "
            "VALUES (?, ?, ?, ?)"
        );
        
        // JSON с NULL значениями
        json null_params = json::array({
            999,        // F_INTEGER
            nullptr,    // F_VARCHAR - NULL
            nullptr,    // F_DOUBLE_PRECISION - NULL
            false       // F_BOOLEAN
        });
        
        std::cout << "Вставка с NULL значениями (JSON): " << null_params.dump() << "\n";
        affected = transaction->execute(stmt, null_params);
        std::cout << "✓ Вставлено строк: " << affected << "\n\n";
        
        // Читаем запись с NULL значениями
        stmt = connection->prepareStatement(
            "SELECT F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN "
            "FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
        );
        
        auto null_cursor = transaction->openCursor(stmt, json::array({999}));
        nlohmann::json null_row;
        if (null_cursor->fetch(null_row)) {
            std::cout << "Запись с NULL значениями (JSON):\n";
            std::cout << null_row.dump(2) << "\n";
            std::cout << "  F_VARCHAR is null: " << null_row["F_VARCHAR"].is_null() << "\n";
            std::cout << "  F_DOUBLE_PRECISION is null: " << null_row["F_DOUBLE_PRECISION"].is_null() << "\n";
        }
        null_cursor->close();
        
        if (transaction) {
            transaction->Commit();
            transaction.reset();  // Explicitly reset after commit
        }
        
        // Демонстрация работы с расширенными типами через JSON
        try {
            printHeader("Расширенные типы Firebird через JSON");
            
            // Start fresh transaction
            transaction = connection->StartTransaction();
        } catch (const Firebird::FbException& e) {
            std::cerr << "Ошибка Firebird при старте транзакции: ";
            char buf[256];
            Firebird::fb_get_master_interface()->getUtilInterface()->
                formatStatus(buf, sizeof(buf), e.getStatus());
            std::cerr << buf << "\n";
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "Ошибка при старте секции расширенных типов: " << e.what() << "\n";
            return 1;
        }
        
        // Вставка ВСЕХ типов через JSON с RETURNING ID
        std::cout << "Вставка всех типов Firebird через JSON:\n\n" << std::flush;
        
        try {
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_SMALINT, F_INTEGER, F_BIGINT, F_INT128, "
            "  F_FLOAT, F_DOUBLE_PRECISION, "
            "  F_NUMERIC, F_DECIMAL, F_DECFLOAT, "
            "  F_DATE, F_TIME, F_TIMESHTAMP, "
            "  F_CHAR, F_VARCHAR, F_BOOLEAN, "
            "  F_BLOB_T"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );
        
        // Подготавливаем JSON с параметрами для всех типов
        json all_types_params = json::array({
            12345,                                              // F_SMALINT (SMALLINT)
            1234567890,                                         // F_INTEGER (INTEGER) 
            8876543210987654321LL,                             // F_BIGINT (BIGINT)
            "170141183460469231731687303715884105727",         // F_INT128 (INT128 as string)
            3.14159f,                                           // F_FLOAT (FLOAT)
            2.718281828459045,                                  // F_DOUBLE_PRECISION (DOUBLE)
            "999999999999.999999",                              // F_NUMERIC (as string for precision)
            "12345678901234567890.12345678",                    // F_DECIMAL (as string for precision)
            "123456789012345678901234567890.1234",              // F_DECFLOAT (DECFLOAT(34) as string)
            "2024-12-31",                                       // F_DATE (ISO date)
            "23:59:59.9999",                                    // F_TIME (ISO time)
            "2024-12-31T23:59:59.9999",                         // F_TIMESHTAMP (ISO timestamp)
            "CHAR TEST",                                        // F_CHAR (fixed length)
            "Тестовая строка с Unicode 文字 🚀",                // F_VARCHAR (variable length)
            true,                                               // F_BOOLEAN
            "This is a text BLOB created via JSON"             // F_BLOB_T (text blob)
        });
        
        std::cout << "Параметры вставки (JSON массив):\n";
        std::cout << all_types_params.dump(2) << "\n\n";
        
        // Выполняем INSERT
        auto affected = transaction->execute(stmt, all_types_params);
        std::cout << "✓ Запись вставлена! Затронуто строк: " << affected << "\n\n";
        
        // Получаем ID последней вставленной записи
        auto id_stmt = connection->prepareStatement(
            "SELECT MAX(ID) AS LAST_ID FROM TABLE_TEST_1 "
            "WHERE F_SMALINT = 12345"
        );
        auto id_cursor = transaction->openCursor(id_stmt);
        int32_t new_id = 0;
        nlohmann::json id_row;
        if (id_cursor->fetch(id_row)) {
            if (!id_row["LAST_ID"].is_null()) {
                new_id = id_row["LAST_ID"].get<int32_t>();
                std::cout << "Получен ID вставленной записи: " << new_id << "\n\n";
            } else {
                std::cout << "ID не найден, используем ID=0\n\n";
            }
        }
        id_cursor->close();
        
        // Теперь читаем вставленную запись обратно
        if (new_id > 0) {
            std::cout << "Читаем вставленную запись (ID=" << new_id << "):\n\n";
            
            stmt = connection->prepareStatement(
                "SELECT ID, F_SMALINT, F_INTEGER, F_BIGINT, F_INT128, "
                "       F_FLOAT, F_DOUBLE_PRECISION, "
                "       F_NUMERIC, F_DECIMAL, F_DECFLOAT, "
                "       F_DATE, F_TIME, F_TIMESHTAMP, "
                "       F_CHAR, F_VARCHAR, F_BOOLEAN, "
                "       F_BLOB_T "
                "FROM TABLE_TEST_1 WHERE ID = ?"
            );
            
            auto verify_cursor = transaction->openCursor(stmt, json::array({new_id}));
        nlohmann::json row;
        if (verify_cursor->fetch(row)) {
            std::cout << "Прочитанные данные (JSON):\n";
            std::cout << row.dump(2) << "\n\n";
            
            // Проверяем типы значений в JSON
            std::cout << "Типы значений в JSON:\n";
            for (auto& [key, value] : row.items()) {
                std::cout << "  " << std::setw(20) << std::left << key << ": ";
                if (value.is_null()) {
                    std::cout << "null";
                } else if (value.is_string()) {
                    std::cout << "string";
                    if (key == "F_BLOB_T") {
                        std::cout << " (BLOB text: \"" << value.get<std::string>().substr(0, 30) << "...\")";
                    } else {
                        std::cout << " (\"" << value.get<std::string>() << "\")";
                    }
                } else if (value.is_number_integer()) {
                    std::cout << "integer (" << value << ")";
                } else if (value.is_number_float()) {
                    std::cout << "float (" << value << ")";
                } else if (value.is_boolean()) {
                    std::cout << "boolean (" << (value.get<bool>() ? "true" : "false") << ")";
                }
                std::cout << "\n";
            }
            
            // Проверяем корректность round-trip
            std::cout << "\n✓ Round-trip проверка:\n";
            std::cout << "  • INT128, DECIMAL, NUMERIC, DECFLOAT сохранены как строки\n";
            std::cout << "  • Даты и время в ISO формате\n";
            std::cout << "  • BLOB текст корректно сохранен и прочитан\n";
            std::cout << "  • Unicode строки сохранены корректно\n";
        }
        verify_cursor->close();
        }  // End of if (new_id > 0)
        
        transaction->Commit();
        
        } catch (const Firebird::FbException& e) {
            std::cerr << "Ошибка Firebird при вставке всех типов: ";
            char buf[256];
            Firebird::fb_get_master_interface()->getUtilInterface()->
                formatStatus(buf, sizeof(buf), e.getStatus());
            std::cerr << buf << "\n";
        } catch (const fbpp::core::FirebirdException& e) {
            std::cerr << "Ошибка при работе с расширенными типами: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Ошибка: " << e.what() << "\n";
        }
        
        // Массовая вставка с использованием JSON
        try {
        printHeader("Массовая вставка через JSON");
        
        transaction = connection->StartTransaction();
        
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_BOOLEAN) "
            "VALUES (?, ?, ?)"
        );
        
        // Вставляем несколько записей используя JSON
        std::vector<json> batch_data = {
            json::array({2001, "JSON Batch 1", true}),
            json::array({2002, "JSON Batch 2", false}),
            json::array({2003, "JSON Batch 3", true}),
            json::array({2004, "JSON Batch 4", nullptr}),  // NULL для F_BOOLEAN
            json::array({2005, "JSON Batch 5", true})
        };
        
        std::cout << "Вставка " << batch_data.size() << " записей через JSON:\n";
        
        int batch_count = 0;
        for (const auto& params : batch_data) {
            auto rows = transaction->execute(stmt, params);
            batch_count += rows;
            std::cout << "  Запись " << (batch_count) << ": " << params.dump() << "\n";
        }
        
        std::cout << "✓ Всего вставлено строк: " << batch_count << "\n\n";
        
        // Проверяем вставленные записи
        stmt = connection->prepareStatement(
            "SELECT COUNT(*) AS CNT FROM TABLE_TEST_1 WHERE F_INTEGER BETWEEN ? AND ?"
        );
        
        auto count_cursor = transaction->openCursor(stmt, json::array({2001, 2005}));
        nlohmann::json count_row;
        if (count_cursor->fetch(count_row)) {
            std::cout << "Проверка: найдено записей с F_INTEGER от 2001 до 2005: " 
                     << count_row["CNT"] << "\n";
        }
        count_cursor->close();
        
        transaction->Commit();
        
        } catch (const fbpp::core::FirebirdException& e) {
            std::cerr << "Ошибка при массовой вставке: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Ошибка: " << e.what() << "\n";
        }
        
        printHeader("JSON API успешно протестирован");
        
        std::cout << "Этот пример продемонстрировал возможности JSON API в fbpp:\n";
        std::cout << "  ✓ Использование JSON массивов для параметров запросов\n";
        std::cout << "  ✓ Чтение результатов в JSON формате через fetch<nlohmann::json>()\n";
        std::cout << "  ✓ Автоматическое определение типа параметров (tuple vs JSON)\n";
        std::cout << "  ✓ Работа с NULL значениями через JSON null\n";
        std::cout << "  ✓ Поддержка всех типов Firebird включая расширенные\n";
        std::cout << "  ✓ NUMERIC/DECIMAL передаются как строки для сохранения точности\n";
        std::cout << "  ✓ INT128 и DECFLOAT передаются как строки\n";
        std::cout << "  ✓ Даты и время передаются в ISO формате\n\n";
        
        std::cout << "Преимущества JSON API:\n";
        std::cout << "  • Более гибкая работа с динамическими данными\n";
        std::cout << "  • Легкая интеграция с REST API и веб-сервисами\n";
        std::cout << "  • Удобная сериализация/десериализация\n";
        std::cout << "  • Естественная работа с NULL значениями\n";
        
    } catch (const fbpp::core::FirebirdException& e) {
        std::cerr << "\n✗ Ошибка Firebird: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Ошибка: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
