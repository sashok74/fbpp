/**
 * @file 04_batch_advanced.cpp
 * @brief Advanced batch operations with extended types on TABLE_TEST_1
 *
 * Demonstrates:
 * - Batch INSERT/UPDATE/DELETE with 100+ records
 * - TTMath INT128 and NUMERIC(38,x) support
 * - CppDecimal DECFLOAT(16/34) support
 * - C++20 date/time types integration
 * - Performance comparison: regular vs batch operations
 * - Error handling in batch operations
 * - Using existing TABLE_TEST_1 only
 */

#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <tuple>
#include <vector>
#include <chrono>
//#include <algorithm>
#include <random>
#include <ttmath/ttmath.h>
#include <decimal>
#include <fbpp/fbpp_all.hpp>  // Включает все, включая batch и адаптеры
#include <fbpp_util/connection_helper.hpp>
#include <nlohmann/json.hpp>

// Типы из библиотек
using Int128 = ttmath::Int<2>;
using DecFloat34 = dec::DecQuad;
using Decimal34_8 = fbpp::adapters::TTNumeric<2, -8>;
using Numeric16_6 = fbpp::adapters::TTNumeric<1, -6>;

// C++20 стандартные типы для дат/времени
#if __cplusplus >= 202002L
using Date = std::chrono::year_month_day;
using Time = std::chrono::hh_mm_ss<std::chrono::microseconds>;
using Timestamp = std::chrono::system_clock::time_point;
using ZonedTimestamp = fbpp::core::ZonedTimestamp;
using TimeTz = fbpp::core::TimeTz;
#endif

// Структура записи для batch операций
using RecordTuple = std::tuple<
    int64_t,                        // F_BIGINT
    bool,                           // F_BOOLEAN
    std::string,                    // F_CHAR
#if __cplusplus >= 202002L
    Date,                          // F_DATE
#else
    fbpp::core::Date,
#endif
    DecFloat34,                     // F_DECFLOAT
    Decimal34_8,                    // F_DECIMAL
    double,                         // F_DOUBLE_PRECISION
    float,                          // F_FLOAT
    Int128,                         // F_INT128
    int32_t,                        // F_INTEGER (KEY!)
    Numeric16_6,                    // F_NUMERIC
    int16_t,                        // F_SMALINT
#if __cplusplus >= 202002L
    Time,                          // F_TIME
    TimeTz,                        // F_TIME_TZ
    Timestamp,                     // F_TIMESHTAMP
    ZonedTimestamp,                // F_TIMESHTAMP_TZ
#else
    fbpp::core::Time,
    fbpp::core::TimeTz,
    fbpp::core::Timestamp,
    fbpp::core::TimestampTz,
#endif
    std::string,                    // F_VARCHAR
    std::optional<int32_t>         // F_NULL
>;

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n\n";
}

class BatchOperationsDemo {
private:
    std::unique_ptr<fbpp::core::Connection> connection;
    int32_t base_key = 0;
    std::vector<RecordTuple> records;
    const size_t BATCH_SIZE = 100;

public:
    BatchOperationsDemo() {
        initConnection();
        getMaxKey();
        generateRecords();
    }

    void run() {
        printHeader("Batch операции с TABLE_TEST_1");

        // Сначала тест производительности вставки
        printHeader("ТЕСТ ПРОИЗВОДИТЕЛЬНОСТИ ВСТАВКИ");

        std::cout << "1. Обычная вставка в цикле (" << BATCH_SIZE << " записей)...\n";
        auto regularTime = regularInsert();

        // Удаляем эти записи
        deleteRecords();

        std::cout << "2. Batch INSERT (" << BATCH_SIZE << " записей)...\n";
        auto batchTime = batchInsertTest();

        // Сравнение
        compareResults(regularTime, batchTime);

        // Теперь выполняем остальные операции
        printHeader("ОСНОВНЫЕ BATCH ОПЕРАЦИИ");

        // Batch INSERT
        //printHeader("1. Batch INSERT " + std::to_string(BATCH_SIZE) + " записей");
        //batchInsert();
        verifyInsert();

        // Batch UPDATE
        printHeader("2. Batch UPDATE записей");
        batchUpdate();
        verifyUpdate();

        // Batch DELETE
        printHeader("3. Batch DELETE записей");
        batchDelete();
        verifyDelete();

        // Демонстрация обработки ошибок
        printHeader("4. Демонстрация обработки ошибок в Batch операциях");
        demonstrateErrorHandling();

        printHeader("✓ Все batch операции выполнены успешно!");
    }

private:
    void initConnection() {
        std::cout << "Подключение к базе данных...\n";

        auto params = fbpp::util::getConnectionParams("db");

        connection = std::make_unique<fbpp::core::Connection>(params);
        std::cout << "✓ Подключение установлено\n";
        std::cout << "  Database: " << params.database << "\n";
        std::cout << "  User: " << params.user << "\n\n";
    }

    void getMaxKey() {
        auto transaction = connection->StartTransaction();
        auto stmt = connection->prepareStatement(
            "SELECT COALESCE(MAX(F_INTEGER), 0) AS MAX_VAL FROM TABLE_TEST_1"
        );

        auto cursor = transaction->openCursor(stmt);
        std::tuple<int32_t> max_row;

        if (cursor->fetch(max_row)) {
            base_key = std::get<0>(max_row) + 1000; // Отступ для безопасности
        }

        transaction->Commit();
        std::cout << "Базовый ключ для новых записей: " << base_key << "\n";
    }

    void generateRecords() {
        std::cout << "Генерация " << BATCH_SIZE << " записей...\n";

        auto now = std::chrono::system_clock::now();
        std::mt19937 gen(std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_real_distribution<> dis(0.0, 100.0);

#if __cplusplus >= 202002L
        // C++20 типы
        auto today = std::chrono::floor<std::chrono::days>(now);
        Date test_date{std::chrono::year_month_day{today}};

        auto time_since_midnight = now.time_since_epoch() % std::chrono::days{1};
        Time test_time{std::chrono::duration_cast<std::chrono::microseconds>(time_since_midnight)};

        Timestamp test_timestamp = now;
        ZonedTimestamp test_timestamp_tz =
            fbpp::core::makeZonedTimestamp("Europe/Moscow", now);
        TimeTz test_time_tz{fbpp::core::Time{test_time.to_duration()}, 4, 180};
#else
        fbpp::core::Date test_date(now);
        fbpp::core::Time test_time(fbpp::core::timestamp_utils::current_time_of_day());
        fbpp::core::Timestamp test_timestamp(now);
        fbpp::core::TimestampTz test_timestamp_tz(now, 4, 180);
        fbpp::core::TimeTz test_time_tz(test_time, 4, 180);
#endif
        Int128 int128_val("999999999999999999999999999999999");
        Decimal34_8 decimal_val("10000123456789.12345678");
        Decimal34_8 decimal_inc("0.00000001");
        Numeric16_6 numeric_val("1234567.123456");
        Numeric16_6 numeric_inc("0.000001");
        DecFloat34 decfloat_val("123456789012345678901234.5678901234");
        DecFloat34 DecFloat34inc( 0.1111);
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            int32_t key = base_key + static_cast<int32_t>(i);

            // Создаем разнообразные данные
            int128_val = int128_val + Int128(i);
            decimal_val = decimal_val + decimal_inc;
            numeric_val = numeric_val + numeric_inc;
            decfloat_val = decfloat_val + DecFloat34inc;

            RecordTuple record = std::make_tuple(
                int64_t(9000000000000000000LL + i),            // F_BIGINT
                (i % 2 == 0),                                  // F_BOOLEAN
                "BATCH_" + std::to_string(i),                 // F_CHAR
                test_date,                                      // F_DATE
                decfloat_val,                                   // F_DECFLOAT
                decimal_val,                                    // F_DECIMAL
                1234.5678 + dis(gen),                         // F_DOUBLE_PRECISION
                float(567.89 + i),                             // F_FLOAT
                int128_val,                                     // F_INT128
                key,                                            // F_INTEGER (KEY!)
                numeric_val,                                    // F_NUMERIC
                int16_t(100 + i),                              // F_SMALINT
                test_time,                                      // F_TIME
                test_time_tz,                                  // F_TIME_TZ
                test_timestamp,                                // F_TIMESHTAMP
                test_timestamp_tz,                             // F_TIMESHTAMP_TZ
                "Batch record #" + std::to_string(i),         // F_VARCHAR
                (i % 10 == 0) ? std::optional<int32_t>{} : std::optional<int32_t>{static_cast<int32_t>(i)} // F_NULL
            );

            records.push_back(record);
        }

        std::cout << "✓ Сгенерировано " << records.size() << " записей\n\n";
    }

    // Обычная вставка в цикле
    std::chrono::milliseconds regularInsert() {
        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, "
            "  F_DECIMAL, F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, "
            "  F_NUMERIC, F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, "
            "  F_TIMESHTAMP_TZ, F_VARCHAR, F_NULL"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );

        auto start = std::chrono::high_resolution_clock::now();

        // Вставляем каждую запись отдельно
        for (const auto& record : records) {
            // Выполняем statement с параметрами
            transaction->execute(stmt, record);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        transaction->Commit();

        std::cout << "✓ Вставлено " << records.size() << " записей за "
                  << duration.count() << " мс\n";

        double records_per_sec = (BATCH_SIZE * 1000.0) / duration.count();
        std::cout << "  Производительность: " << std::fixed << std::setprecision(0)
                  << records_per_sec << " записей/сек\n\n";

        return duration;
    }

    // Batch вставка для теста производительности (возвращает время)
    std::chrono::milliseconds batchInsertTest() {
        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, "
            "  F_DECIMAL, F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, "
            "  F_NUMERIC, F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, "
            "  F_TIMESHTAMP_TZ, F_VARCHAR, F_NULL"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );

        auto batch = transaction->createBatch(stmt, true, true);

        auto start = std::chrono::high_resolution_clock::now();

        // Добавляем записи в batch
        //for (const auto& record : records) {        }
         batch->addMany(records);
             
        // Выполняем batch
        auto results = batch->execute(transaction.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        transaction->Commit();

        std::cout << "✓ Вставлено " << results.successCount << " записей за "
                  << duration.count() << " мс\n";

        if (results.failedCount > 0) {
            std::cout << "⚠ Ошибок: " << results.failedCount << "\n";
        }

        double records_per_sec = (BATCH_SIZE * 1000.0) / duration.count();
        std::cout << "  Производительность: " << std::fixed << std::setprecision(0)
                  << records_per_sec << " записей/сек\n\n";

        return duration;
    }

    // Batch вставка (старый метод для основных операций)
    void batchInsert() {
        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, "
            "  F_DECIMAL, F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, "
            "  F_NUMERIC, F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, "
            "  F_TIMESHTAMP_TZ, F_VARCHAR, F_NULL"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );

        auto batch = transaction->createBatch(stmt, true, true);

        auto start = std::chrono::high_resolution_clock::now();

        // Добавляем записи в batch
        for (const auto& record : records) {
            batch->add(record);
        }

        // Выполняем batch
        auto results = batch->execute(transaction.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        transaction->Commit();

        std::cout << "✓ Вставлено " << results.successCount << " записей за "
                  << duration.count() << " мс\n";

        if (results.failedCount > 0) {
            std::cout << "⚠ Ошибок: " << results.failedCount << "\n";
            for (const auto& err : results.errors) {
                std::cout << "  - " << err << "\n";
            }
        }

        // Показываем производительность
        double records_per_sec = (BATCH_SIZE * 1000.0) / duration.count();
        std::cout << "  Производительность: " << std::fixed << std::setprecision(0)
                  << records_per_sec << " записей/сек\n\n";
    }

    // Удаление всех тестовых записей
    void deleteRecords() {
        std::cout << "Удаление тестовых записей...\n";

        auto transaction = connection->StartTransaction();

        // Удаляем все записи с нашими ключами
        auto stmt = connection->prepareStatement(
            "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER >= ? AND F_INTEGER < ?"
        );

        std::tuple<int32_t, int32_t> params{base_key, base_key + static_cast<int32_t>(BATCH_SIZE)};
        transaction->execute(stmt, params);

        transaction->Commit();
        std::cout << "✓ Записи удалены\n\n";
    }

    // Сравнение результатов
    void compareResults(std::chrono::milliseconds regularTime, std::chrono::milliseconds batchTime) {
        double speedup = static_cast<double>(regularTime.count()) / batchTime.count();

        std::cout << "📊 Результаты сравнения:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  Обычная вставка:  " << std::setw(6) << regularTime.count() << " мс\n";
        std::cout << "  Batch вставка:    " << std::setw(6) << batchTime.count() << " мс\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  🚀 Ускорение:      " << std::fixed << std::setprecision(1)
                  << speedup << "x\n\n";

        if (speedup > 10) {
            std::cout << "  ⚡ Batch операции более чем в 10 раз быстрее!\n";
        } else if (speedup > 5) {
            std::cout << "  ✨ Batch операции значительно быстрее (>5x)\n";
        } else if (speedup > 2) {
            std::cout << "  ✓ Batch операции заметно быстрее (>2x)\n";
        } else {
            std::cout << "  📌 Batch операции немного быстрее\n";
        }

        // Дополнительная статистика
        int64_t saved_ms = regularTime.count() - batchTime.count();
        std::cout << "  ⏱️  Экономия времени: " << saved_ms << " мс\n";

        double regular_rps = (BATCH_SIZE * 1000.0) / regularTime.count();
        double batch_rps = (BATCH_SIZE * 1000.0) / batchTime.count();

        std::cout << "\n  Пропускная способность:\n";
        std::cout << "    • Обычная:  " << std::setw(8) << std::fixed << std::setprecision(0)
                  << regular_rps << " записей/сек\n";
        std::cout << "    • Batch:    " << std::setw(8) << batch_rps << " записей/сек\n\n";
    }

    void verifyInsert() {
        std::cout << "Проверка вставленных записей...\n";

        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "SELECT COUNT(*) FROM TABLE_TEST_1 WHERE F_INTEGER >= ? AND F_INTEGER < ?"
        );

        std::tuple<int32_t, int32_t> params{base_key, base_key + static_cast<int32_t>(BATCH_SIZE)};
        auto cursor = transaction->openCursor(stmt, params);

        std::tuple<int64_t> count_row;
        if (cursor->fetch(count_row)) {
            auto count = std::get<0>(count_row);
            if (count == static_cast<int64_t>(BATCH_SIZE)) {
                std::cout << "✓ Все " << BATCH_SIZE << " записей найдены\n";
            } else {
                std::cout << "⚠ Найдено только " << count << " записей из " << BATCH_SIZE << "\n";
            }
        }

        // Проверяем несколько случайных записей
        std::cout << "\nВыборочная проверка данных:\n";
        std::vector<int> indices = {0, 25, 50, 75, 99};

        for (int idx : indices) {
            auto check_stmt = connection->prepareStatement(
                "SELECT F_INTEGER, F_VARCHAR, F_BOOLEAN FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );

            std::tuple<int32_t> key_param{base_key + idx};
            auto check_cursor = transaction->openCursor(check_stmt, key_param);

            std::tuple<int32_t, std::string, bool> row;
            if (check_cursor->fetch(row)) {
                std::cout << "  [" << idx << "] F_INTEGER=" << std::get<0>(row)
                          << ", F_VARCHAR='" << std::get<1>(row) << "'"
                          << ", F_BOOLEAN=" << (std::get<2>(row) ? "true" : "false") << "\n";
            }
        }

        transaction->Commit();
        std::cout << "\n";
    }

    void batchUpdate() {
        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "UPDATE TABLE_TEST_1 SET F_VARCHAR = ?, F_DOUBLE_PRECISION = ? WHERE F_INTEGER = ?"
        );

        auto batch = transaction->createBatch(stmt, true, true);

        auto start = std::chrono::high_resolution_clock::now();

        // Обновляем записи - меняем VARCHAR и DOUBLE_PRECISION
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            int32_t key = base_key + static_cast<int32_t>(i);
            std::string new_varchar = "UPDATED_" + std::to_string(i);
            double new_double = 9999.99 - i;

            std::tuple<std::string, double, int32_t> update_params{new_varchar, new_double, key};
            batch->add(update_params);
        }

        auto results = batch->execute(transaction.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        transaction->Commit();

        std::cout << "✓ Обновлено " << results.successCount << " записей за "
                  << duration.count() << " мс\n";

        double updates_per_sec = (BATCH_SIZE * 1000.0) / duration.count();
        std::cout << "  Производительность: " << std::fixed << std::setprecision(0)
                  << updates_per_sec << " обновлений/сек\n\n";
    }

    void verifyUpdate() {
        std::cout << "Проверка обновленных записей...\n";

        auto transaction = connection->StartTransaction();

        // Проверяем, что все записи обновлены
        auto stmt = connection->prepareStatement(
            "SELECT COUNT(*) FROM TABLE_TEST_1 WHERE F_INTEGER >= ? AND F_INTEGER < ? "
            "AND F_VARCHAR LIKE 'UPDATED_%'"
        );

        std::tuple<int32_t, int32_t> params{base_key, base_key + static_cast<int32_t>(BATCH_SIZE)};
        auto cursor = transaction->openCursor(stmt, params);

        std::tuple<int64_t> count_row;
        if (cursor->fetch(count_row)) {
            auto count = std::get<0>(count_row);
            if (count == BATCH_SIZE) {
                std::cout << "✓ Все " << BATCH_SIZE << " записей обновлены\n";
            } else {
                std::cout << "⚠ Обновлено только " << count << " записей из " << BATCH_SIZE << "\n";
            }
        }

        // Проверяем несколько случайных записей
        std::cout << "\nВыборочная проверка обновлений:\n";
        std::vector<int> indices = {0, 49, 99};

        for (int idx : indices) {
            auto check_stmt = connection->prepareStatement(
                "SELECT F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );

            std::tuple<int32_t> key_param{base_key + idx};
            auto check_cursor = transaction->openCursor(check_stmt, key_param);

            std::tuple<int32_t, std::string, double> row;
            if (check_cursor->fetch(row)) {
                std::cout << "  [" << idx << "] F_INTEGER=" << std::get<0>(row)
                          << ", F_VARCHAR='" << std::get<1>(row) << "'"
                          << ", F_DOUBLE=" << std::fixed << std::setprecision(2)
                          << std::get<2>(row) << "\n";

                // Проверяем корректность обновления
                double expected = 9999.99 - idx;
                if (std::abs(std::get<2>(row) - expected) < 0.01) {
                    std::cout << "    ✓ Значение корректно\n";
                }
            }
        }

        transaction->Commit();
        std::cout << "\n";
    }

    void batchDelete() {
        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
        );

        auto batch = transaction->createBatch(stmt, true, true);

        auto start = std::chrono::high_resolution_clock::now();

        // Удаляем все тестовые записи кроме последней. для контроля
        for (size_t i = 0; i < BATCH_SIZE-3; ++i) {
            int32_t key = base_key + static_cast<int32_t>(i);
            std::tuple<int32_t> delete_params{key};
            batch->add(delete_params);
        }

        auto results = batch->execute(transaction.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        transaction->Commit();

        std::cout << "✓ Удалено " << results.successCount << " записей за "
                  << duration.count() << " мс\n";

        double deletes_per_sec = (BATCH_SIZE * 1000.0) / duration.count();
        std::cout << "  Производительность: " << std::fixed << std::setprecision(0)
                  << deletes_per_sec << " удалений/сек\n\n";
    }

    void verifyDelete() {
        std::cout << "Проверка удаления записей...\n";

        auto transaction = connection->StartTransaction();

        auto stmt = connection->prepareStatement(
            "SELECT COUNT(*) FROM TABLE_TEST_1 WHERE F_INTEGER >= ? AND F_INTEGER < ?"
        );

        std::tuple<int32_t, int32_t> params{base_key, base_key + static_cast<int32_t>(BATCH_SIZE)};
        auto cursor = transaction->openCursor(stmt, params);

        std::tuple<int64_t> count_row;
        if (cursor->fetch(count_row)) {
            auto count = std::get<0>(count_row);
            if (count == 0) {
                std::cout << "✓ Все " << BATCH_SIZE << " записей успешно удалены\n";
            } else {
                std::cout << "⚠ Осталось " << count << " записей, которые не были удалены\n";
            }
        }

        transaction->Commit();
        std::cout << "\n";
    }

    void demonstrateErrorHandling() {
        std::cout << "Создаем batch с намеренными ошибками (дублирующиеся ключи F_INTEGER)...\n\n";

        auto transaction = connection->StartTransaction();

        // Подготавливаем данные с ошибками
        // Некоторые ключи будут дублировать уже существующие записи
        std::vector<RecordTuple> errorRecords;

        // Генерируем текущие значения для полей
        auto now = std::chrono::system_clock::now();

#if __cplusplus >= 202002L
        auto today = std::chrono::floor<std::chrono::days>(now);
        Date test_date{std::chrono::year_month_day{today}};
        auto time_since_midnight = now.time_since_epoch() % std::chrono::days{1};
        Time test_time{std::chrono::duration_cast<std::chrono::microseconds>(time_since_midnight)};
        Timestamp test_timestamp = now;
        ZonedTimestamp test_timestamp_tz =
            fbpp::core::makeZonedTimestamp("Europe/Moscow", now);
        TimeTz test_time_tz{fbpp::core::Time{test_time.to_duration()}, 4, 180};
#else
        fbpp::core::Date test_date(now);
        fbpp::core::Time test_time(fbpp::core::timestamp_utils::current_time_of_day());
        fbpp::core::Timestamp test_timestamp(now);
        fbpp::core::TimestampTz test_timestamp_tz(now, 4, 180);
        fbpp::core::TimeTz test_time_tz(test_time, 4, 180);
#endif

        // Создаем 10 записей, некоторые с дублирующимися ключами
        int32_t test_keys[] = {
            base_key + 100000,  // Новый ключ - успешно
            base_key + 100001,  // Новый ключ - успешно
            base_key + static_cast<int32_t>(BATCH_SIZE - 3),  // Существующий ключ - ошибка!
            base_key + 100002,  // Новый ключ - успешно
            base_key + static_cast<int32_t>(BATCH_SIZE - 2),  // Существующий ключ - ошибка!
            base_key + 100003,  // Новый ключ - успешно
            base_key + 100003,  // Дублирующийся ключ в этом же batch - ошибка!
            base_key + 100004,  // Новый ключ - успешно
            base_key + static_cast<int32_t>(BATCH_SIZE - 1),  // Существующий ключ - ошибка!
            base_key + 100005   // Новый ключ - успешно
        };

        for (int i = 0; i < 10; ++i) {
            RecordTuple record = std::make_tuple(
                int64_t(8000000000000000000LL + i),           // F_BIGINT
                (i % 2 == 0),                                  // F_BOOLEAN
                "ERR_" + std::to_string(i),                   // F_CHAR (укороченная строка)
                test_date,                                      // F_DATE
                DecFloat34("99999.9999"),                      // F_DECFLOAT
                Decimal34_8("88888.88888888"),                 // F_DECIMAL
                777.777,                                        // F_DOUBLE_PRECISION
                float(666.66),                                  // F_FLOAT
                Int128("555555555555555555555555555"),          // F_INT128
                test_keys[i],                                   // F_INTEGER (KEY с уникальным ограничением!)
                Numeric16_6("4444.444444"),                    // F_NUMERIC
                int16_t(333),                                   // F_SMALINT
                test_time,                                      // F_TIME
                test_time_tz,                                  // F_TIME_TZ
                test_timestamp,                                // F_TIMESHTAMP
                test_timestamp_tz,                             // F_TIMESHTAMP_TZ
                "Error handling test #" + std::to_string(i),   // F_VARCHAR
                std::optional<int32_t>{i}                      // F_NULL
            );

            errorRecords.push_back(record);

            std::cout << "  Запись " << std::setw(2) << (i+1)
                      << ": F_INTEGER = " << test_keys[i];

            // Помечаем проблемные записи
            if (test_keys[i] == base_key + static_cast<int32_t>(BATCH_SIZE - 3) ||
                test_keys[i] == base_key + static_cast<int32_t>(BATCH_SIZE - 2) ||
                test_keys[i] == base_key + static_cast<int32_t>(BATCH_SIZE - 1)) {
                std::cout << " ⚠️ (существующий ключ)";
            } else if (i == 6) {
                std::cout << " ⚠️ (дубликат записи #6)";
            }
            std::cout << "\n";
        }

        std::cout << "\nВыполняем batch INSERT с continueOnError = true...\n\n";

        // Создаем statement для вставки
        auto stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, "
            "  F_DECIMAL, F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, "
            "  F_NUMERIC, F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, "
            "  F_TIMESHTAMP_TZ, F_VARCHAR, F_NULL"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );

        // Создаем batch с параметрами:
        // - recordCounts = true (получать статистику)
        // - continueOnError = true (продолжать при ошибках)
        auto batch = transaction->createBatch(stmt, true, true);

        // Добавляем все записи в batch
        batch->addMany(errorRecords);

        // Выполняем batch и получаем детальные результаты
        auto results = batch->execute(transaction.get());

        // Анализируем результаты
        std::cout << "📊 Результаты batch операции:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  Всего записей:    " << results.totalMessages << "\n";
        std::cout << "  ✅ Успешно:       " << results.successCount << "\n";
        std::cout << "  ❌ С ошибками:    " << results.failedCount << "\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

        // Детальный анализ каждой записи
        if (!results.perMessageStatus.empty()) {
            std::cout << "Детальный статус каждой записи:\n";
            for (size_t i = 0; i < results.perMessageStatus.size(); ++i) {
                std::cout << "  [" << std::setw(2) << (i+1) << "] F_INTEGER = "
                          << test_keys[i] << " -> ";

                if (results.perMessageStatus[i] >= 0) {
                    std::cout << "✅ УСПЕХ (затронуто записей: "
                              << results.perMessageStatus[i] << ")";
                } else {
                    std::cout << "❌ ОШИБКА";
                    // Показываем сообщение об ошибке, если оно есть
                    if (i < results.errors.size() && !results.errors[i].empty()) {
                        // Извлекаем ключевую информацию из сообщения об ошибке
                        std::string error = results.errors[i];
                        if (error.find("UNQ1_TABLE_TEST_F_INTEGER") != std::string::npos ||
                            error.find("unique") != std::string::npos ||
                            error.find("UNIQUE") != std::string::npos) {
                            std::cout << "\n      Причина: Нарушение уникальности ключа F_INTEGER";
                        } else {
                            std::cout << "\n      Причина: " << error;
                        }
                    }
                }
                std::cout << "\n";
            }
        }

        std::cout << "\n📝 Выводы:\n";
        std::cout << "• Batch операции с continueOnError=true продолжают работу при ошибках\n";
        std::cout << "• BatchResult.perMessageStatus показывает статус каждой записи\n";
        std::cout << "• BatchResult.errors содержит описания ошибок для неудачных записей\n";
        std::cout << "• Это позволяет обработать максимум данных и затем исправить ошибки\n";

        // Удаляем успешно вставленные тестовые записи
        std::cout << "\nУдаляем успешно вставленные тестовые записи...\n";
        auto delete_stmt = connection->prepareStatement(
            "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER >= ? AND F_VARCHAR LIKE 'Error handling test%'"
        );
        std::tuple<int32_t> delete_params{base_key + 100000};
        auto deleted = transaction->execute(delete_stmt, delete_params);
        std::cout << "✓ Удалено " << deleted << " тестовых записей\n";

        transaction->Commit();
        std::cout << "\n";
    }
};

int main() {
    try {
        BatchOperationsDemo demo;
        demo.run();

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "  Итоги:\n";
        std::cout << std::string(70, '=') << "\n";
        std::cout << "✓ Batch операции работают с высокой производительностью\n";
        std::cout << "✓ Стандартные C++20 типы используются прозрачно\n";
        std::cout << "✓ TTMath и CppDecimal обеспечивают точные вычисления\n";
        std::cout << "✓ Автоматическая конвертация через type adapters\n\n";

    } catch (const fbpp::core::FirebirdException& e) {
        std::cerr << "\n❌ Ошибка Firebird: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Ошибка: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
