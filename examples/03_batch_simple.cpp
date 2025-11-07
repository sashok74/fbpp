/**
 * @file 03_batch_simple.cpp
 * @brief Simple batch operations demonstration with TABLE_TEST_1
 *
 * Demonstrates:
 * - Batch INSERT using tuples
 * - Working with basic Firebird types
 * - Efficient bulk data insertion
 * - Using existing TABLE_TEST_1 only
 * - Получение статистики выполнения batch операций
 */

#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <vector>
#include <chrono>
#include <ctime>
#include <tuple>
#include <optional>
#include <fbpp/fbpp_all.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace fbpp::core;

// Для красивого вывода
void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n\n";
}

void printInfo(const std::string& label, const std::string& value) {
    std::cout << std::setw(25) << std::left << label << ": " << value << "\n";
}

int main() {
    printHeader("Firebird Batch Operations with Tuple - Simple Version");

    try {
        // ====================================================================
        // ЭТАП 1: ПОДКЛЮЧЕНИЕ К БАЗЕ ДАННЫХ
        // ====================================================================
        printHeader("ЭТАП 1: Подключение к базе данных");

        std::cout << "Загрузка конфигурации...\n";

        // Поиск конфигурационного файла
        std::vector<std::string> paths = {
            "../../config/test_config.json",
            "../config/test_config.json",
            "config/test_config.json",
            "./test_config.json"
        };

        std::ifstream config_file;
        std::string config_path;
        for (const auto& path : paths) {
            config_file.open(path);
            if (config_file.is_open()) {
                config_path = path;
                break;
            }
        }

        if (!config_file.is_open()) {
            throw std::runtime_error("Не найден файл конфигурации test_config.json");
        }

        auto config = json::parse(config_file);
        auto db_config = config["tests"]["persistent_db"];

        std::string server = "firebird5";
        if (db_config.contains("server") && !db_config["server"].empty()) {
            server = db_config["server"];
        }
        std::string path = db_config["path"];
        std::string database = server + ":" + path;
        std::string username = db_config["user"];
        std::string password = db_config["password"];
        std::string charset = db_config["charset"];

        printInfo("Config file", config_path);
        printInfo("Database", database);
        printInfo("Username", username);
        printInfo("Charset", charset);

        std::cout << "\nПодключение к базе данных...\n";

        ConnectionParams params;
        params.database = database;
        params.user = username;
        params.password = password;
        params.charset = charset;

        auto connection = std::make_unique<Connection>(params);
        std::cout << "✓ Успешно подключились к базе данных!\n";

        // ====================================================================
        // ЭТАП 2: ЧТЕНИЕ МАКСИМАЛЬНОГО ЗНАЧЕНИЯ F_INTEGER
        // ====================================================================
        printHeader("ЭТАП 2: Получение максимального значения F_INTEGER");

        auto transaction = connection->StartTransaction();

        auto max_stmt = connection->prepareStatement(
            "SELECT COALESCE(MAX(F_INTEGER), 0) AS MAX_VAL FROM TABLE_TEST_1"
        );

        auto cursor = transaction->openCursor(max_stmt);

        int32_t max_f_integer = 0;
        std::tuple<int32_t> max_row;
        if (cursor->fetch(max_row)) {
            max_f_integer = std::get<0>(max_row);
            std::cout << "Текущее максимальное значение F_INTEGER: " << max_f_integer << "\n";
        } else {
            std::cout << "Таблица пуста, начинаем с F_INTEGER = 0\n";
        }

        cursor->close();
        transaction->Commit();

        // ====================================================================
        // ЭТАП 3: BATCH INSERT с использованием tuple
        // ====================================================================
        printHeader("ЭТАП 3: Batch вставка 10 записей через tuple");

        transaction = connection->StartTransaction();

        // Упрощенная вставка только основных полей
        auto insert_stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_INTEGER, F_VARCHAR, F_BIGINT, F_BOOLEAN, "
            "  F_DOUBLE_PRECISION, F_FLOAT, F_SMALINT, F_NULL"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        );

        // Создаем batch с поддержкой статистики
        auto batch = insert_stmt->createBatch(transaction.get(), true, false);

        std::cout << "Подготовка данных для вставки...\n\n";

        // Упрощенный тип для одной записи
        using RecordTuple = std::tuple<
            int32_t,                        // F_INTEGER (уникальный ключ)
            std::string,                    // F_VARCHAR
            int64_t,                        // F_BIGINT
            bool,                           // F_BOOLEAN
            double,                         // F_DOUBLE_PRECISION
            float,                          // F_FLOAT
            int16_t,                        // F_SMALINT
            std::optional<int32_t>         // F_NULL
        >;

        // Создаем 10 записей
        std::vector<RecordTuple> records;

        for (int i = 1; i <= 10; ++i) {
            int32_t new_f_integer = max_f_integer + 1000 + i; // Добавляем 1000 для уникальности

            RecordTuple record = std::make_tuple(
                new_f_integer,                                  // F_INTEGER (уникальный ключ)
                "Batch запись #" + std::to_string(i),          // F_VARCHAR
                int64_t(9000000000000000000LL + i),           // F_BIGINT
                (i % 2 == 0),                                  // F_BOOLEAN (четные - true)
                1234.5678 + i,                                 // F_DOUBLE_PRECISION
                float(567.89 + i),                             // F_FLOAT
                int16_t(100 + i),                              // F_SMALINT
                (i % 3 == 0) ? std::optional<int32_t>() :     // F_NULL (каждая 3-я NULL)
                               std::optional<int32_t>(i * 100)
            );

            records.push_back(record);

            // Выводим информацию о первых 3 записях
            if (i <= 3) {
                std::cout << "Запись " << i << ":\n";
                std::cout << "  F_INTEGER: " << new_f_integer << " (уникальный ключ)\n";
                std::cout << "  F_VARCHAR: " << "Batch запись #" + std::to_string(i) << "\n";
                std::cout << "  F_BOOLEAN: " << ((i % 2 == 0) ? "true" : "false") << "\n";
                std::cout << "  F_BIGINT: " << (9000000000000000000LL + i) << "\n\n";
            }
        }

        if (records.size() > 3) {
            std::cout << "  ... еще " << (records.size() - 3) << " записей\n\n";
        }

        // Добавляем все записи в batch используя addMany
        std::cout << "Добавление записей в batch...\n";
        batch->addMany(records);

        // Выполняем batch операцию
        std::cout << "Выполнение batch операции...\n";
        auto start_time = std::chrono::high_resolution_clock::now();

        auto result = batch->execute(transaction.get());

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n✓ Batch операция выполнена за " << duration.count() << " мс\n";
        std::cout << "Результаты:\n";
        std::cout << "  Всего сообщений: " << result.totalMessages << "\n";
        std::cout << "  Успешно вставлено: " << result.successCount << "\n";
        std::cout << "  С ошибками: " << result.failedCount << "\n";

        // Детали по каждому сообщению
        if (!result.perMessageStatus.empty()) {
            std::cout << "\nДетали по сообщениям:\n";
            for (size_t i = 0; i < std::min(size_t(3), result.perMessageStatus.size()); ++i) {
                std::cout << "  Сообщение " << (i + 1) << ": ";
                if (result.perMessageStatus[i] >= 0) {
                    std::cout << "✓ Успех (" << result.perMessageStatus[i] << " записей затронуто)\n";
                } else {
                    std::cout << "✗ Ошибка\n";
                    if (i < result.errors.size() && !result.errors[i].empty()) {
                        std::cout << "    Причина: " << result.errors[i] << "\n";
                    }
                }
            }
            if (result.perMessageStatus.size() > 3) {
                std::cout << "  ... еще " << (result.perMessageStatus.size() - 3) << " сообщений\n";
            }
        }

        transaction->Commit();

        // ====================================================================
        // ЭТАП 4: ПРОВЕРКА РЕЗУЛЬТАТОВ
        // ====================================================================
        printHeader("ЭТАП 4: Проверка результатов вставки");

        transaction = connection->StartTransaction();

        // Проверяем количество вставленных записей
        auto count_stmt = connection->prepareStatement(
            "SELECT COUNT(*) AS CNT FROM TABLE_TEST_1 WHERE F_INTEGER > ?"
        );

        auto count_cursor = transaction->openCursor(count_stmt, std::make_tuple(max_f_integer + 1000));
        std::tuple<int32_t> count_row;
        if (count_cursor->fetch(count_row)) {
            std::cout << "Количество новых записей в таблице: " << std::get<0>(count_row) << "\n\n";
        }
        count_cursor->close();

        // Читаем несколько вставленных записей для проверки
        auto check_stmt = connection->prepareStatement(
            "SELECT F_INTEGER, F_VARCHAR, F_BOOLEAN, F_BIGINT, F_DOUBLE_PRECISION "
            "FROM TABLE_TEST_1 "
            "WHERE F_INTEGER > ? "
            "ORDER BY F_INTEGER "
            "ROWS 3"
        );

        auto check_cursor = transaction->openCursor(check_stmt, std::make_tuple(max_f_integer + 1000));

        std::cout << "Первые 3 вставленные записи:\n";
        std::cout << std::string(60, '-') << "\n";

        using CheckTuple = std::tuple<
            int32_t,                    // F_INTEGER
            std::string,                // F_VARCHAR
            bool,                        // F_BOOLEAN
            int64_t,                     // F_BIGINT
            double                       // F_DOUBLE_PRECISION
        >;

        CheckTuple check_row;
        int row_num = 0;
        while (check_cursor->fetch(check_row) && row_num < 3) {
            row_num++;
            auto [f_int, f_varchar, f_bool, f_bigint, f_double] = check_row;

            std::cout << "Запись " << row_num << ":\n";
            std::cout << "  F_INTEGER: " << f_int << "\n";
            std::cout << "  F_VARCHAR: " << f_varchar << "\n";
            std::cout << "  F_BOOLEAN: " << (f_bool ? "true" : "false") << "\n";
            std::cout << "  F_BIGINT: " << f_bigint << "\n";
            std::cout << "  F_DOUBLE_PRECISION: " << std::fixed << std::setprecision(4) << f_double << "\n\n";
        }

        check_cursor->close();
        transaction->Commit();

        // ====================================================================
        // ЗАВЕРШЕНИЕ
        // ====================================================================
        printHeader("Batch операция успешно завершена");

        std::cout << "Демонстрация показала:\n";
        std::cout << "  ✓ Подключение к базе данных Firebird\n";
        std::cout << "  ✓ Чтение максимального значения ключевого поля\n";
        std::cout << "  ✓ Batch вставка с использованием tuple\n";
        std::cout << "  ✓ Работа с основными типами Firebird\n";
        std::cout << "  ✓ Получение детальной статистики выполнения\n";
        std::cout << "  ✓ Проверка результатов вставки\n\n";

        std::cout << "Данные остаются в таблице для последующего использования.\n";

    } catch (const FirebirdException& e) {
        std::cerr << "\n✗ Ошибка Firebird: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Ошибка: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
