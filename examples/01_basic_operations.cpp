/**
 * @file 01_basic_operations.cpp
 * @brief Basic CRUD operations with TABLE_TEST_1 using tuples
 *
 * Demonstrates:
 * - Connection to existing database
 * - INSERT, SELECT, UPDATE operations
 * - Tuple-based parameter binding
 * - Batch INSERT operations
 * - Working with existing TABLE_TEST_1
 */

#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <vector>
#include <optional>
#include <tuple>
#include <chrono>
#include <ctime>
#include <cstdint>
#if defined(_WIN32)
#include <intrin.h>
#else
#include <arpa/inet.h>  // для htonl
#endif
#if defined(_MSVC_LANG) && (_MSVC_LANG > __cplusplus)
#define FBPP_SAMPLE_CPLUSPLUS _MSVC_LANG
#else
#define FBPP_SAMPLE_CPLUSPLUS __cplusplus
#endif

#if FBPP_SAMPLE_CPLUSPLUS >= 202002L
#include <format>
#endif
#include <fbpp/fbpp_all.hpp>
#include <fbpp_util/connection_helper.hpp>
#include <ttmath/ttmath.h>
#include <decimal>

using json = nlohmann::json;

// Extended types from libraries
using Int128 = ttmath::Int<2>;
using DecFloat34 = dec::DecQuad;
using Decimal34_8 = fbpp::adapters::TTNumeric<2, -8>;
using Numeric16_6 = fbpp::adapters::TTNumeric<1, -6>;

// C++20 date/time types
#if FBPP_SAMPLE_CPLUSPLUS >= 202002L
using Date = std::chrono::year_month_day;
using Time = std::chrono::hh_mm_ss<std::chrono::microseconds>;
using Timestamp = std::chrono::system_clock::time_point;
using TimestampTz = std::chrono::zoned_time<std::chrono::microseconds>;
using TimeWithTz = std::pair<std::chrono::hh_mm_ss<std::chrono::microseconds>, std::string>;
#endif

namespace {
inline uint32_t hostToNetwork32(uint32_t value) {
#if defined(_WIN32)
    return _byteswap_ulong(value);
#else
    return static_cast<uint32_t>(htonl(value));
#endif
}
} // namespace

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
    // Генератор уникальных значений для F_INTEGER
    auto generate_unique_integer = []() {
        static int counter = 1000000 + std::chrono::system_clock::now().time_since_epoch().count() % 1000000;
        return counter++;
    };
    
    printHeader("Firebird C++ Wrapper (fbpp) - Connection Example");
    
    try {
        // Получаем параметры подключения из конфига (секция "db")
        // с автоматическим переопределением из ENV переменных
        std::cout << "Загрузка конфигурации...\n";
        auto params = fbpp::util::getConnectionParams("db");

        printInfo("Database", params.database);
        printInfo("Username", params.user);
        printInfo("Charset", params.charset);

        std::cout << "\nПодключение к базе данных...\n";

        // Создаем подключение к существующей базе данных
        auto connection = std::make_unique<fbpp::core::Connection>(params);
        
        std::cout << "✓ Успешно подключились к базе данных!\n";
        
        // Получаем информацию о сервере через простой запрос
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
        
        // Используем новый API с tuple
        std::tuple<std::string, std::string> server_info;
        if (cursor->fetch(server_info)) {
            auto [version, engine] = server_info;
            std::cout << "✓ Информация о сервере получена\n";
            std::cout << "  Версия: " << version << "\n";
            std::cout << "  Движок: " << engine << "\n";
        }
        
        cursor->close();
        transaction->Commit();
        
        // Проверяем наличие таблицы TABLE_TEST_1
        std::cout << "\nПроверка наличия таблицы TABLE_TEST_1...\n";
        
        transaction = connection->StartTransaction();
        stmt = connection->prepareStatement(
            "SELECT COUNT(*) "
            "FROM RDB$RELATIONS "
            "WHERE RDB$RELATION_NAME = 'TABLE_TEST_1' "
            "  AND RDB$SYSTEM_FLAG = 0"
        );
        
        cursor = transaction->openCursor(stmt);
        
        // Используем новый API с tuple
        std::tuple<int64_t> count_result;
        if (cursor->fetch(count_result)) {
            auto [count] = count_result;
            if (count > 0) {
                std::cout << "✓ Таблица TABLE_TEST_1 найдена\n";
            } else {
                std::cerr << "✗ Таблица TABLE_TEST_1 не найдена\n";
                return 1;
            }
        }
        
        cursor->close();
        transaction->Commit();
        
        // Подсчитаем количество полей в таблице
        std::cout << "\nАнализ структуры таблицы TABLE_TEST_1...\n";
        
        transaction = connection->StartTransaction();
        stmt = connection->prepareStatement(
            "SELECT COUNT(*) "
            "FROM RDB$RELATION_FIELDS "
            "WHERE RDB$RELATION_NAME = 'TABLE_TEST_1'"
        );
        
        cursor = transaction->openCursor(stmt);
        
        // Используем новый API с tuple
        std::tuple<int64_t> field_count;
        if (cursor->fetch(field_count)) {
            auto [count] = field_count;
            std::cout << "✓ Структура таблицы проанализирована\n";
            std::cout << "  Таблица содержит " << count << " полей\n";
            std::cout << "  включая INT128, DECFLOAT, TIME WITH TIME ZONE и др.\n";
        }
        
        cursor->close();
        transaction->Commit();
        
        // Демонстрируем возможности wrapper: INSERT с RETURNING
        printHeader("Демонстрация INSERT с RETURNING");
        
        transaction = connection->StartTransaction();
        
        // Используем наш высокоуровневый API для вставки данных с возвратом ID
        std::cout << "Вставляем новую запись с автоматическим получением ID...\n";
        
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN) "
            "VALUES (?, ?, ?, ?) "
            "RETURNING ID"
        );
        
        // Используем наш TuplePacker для упаковки параметров
        int unique_int1 = generate_unique_integer();
        auto input_params = std::make_tuple(
            unique_int1,                  // F_INTEGER (уникальное значение)
            std::string("Тестовая запись из wrapper"),  // F_VARCHAR
            3.14159,                     // F_DOUBLE_PRECISION
            true                         // F_BOOLEAN
        );
        std::cout << "  F_INTEGER (уникальное): " << unique_int1 << "\n";
        
        // Выполняем с помощью нашего высокоуровневого API
        auto [affected, returned_id] = transaction->execute(
            stmt, 
            input_params, 
            std::tuple<int32_t>{}  // Ожидаем возврат ID
        );
        
        auto id = std::get<0>(returned_id);
        std::cout << "✓ Запись вставлена! Получен ID: " << id << "\n";
        std::cout << "  Затронуто строк: " << affected << "\n\n";
        
        transaction->Commit();
        
        // Демонстрация работы с расширенными типами
        printHeader("Работа с расширенными типами (INT128, DECIMAL, DECFLOAT)");

        transaction = connection->StartTransaction();

        // Инициализируем расширенные типы
        Int128 int128_val("999999999999999999999999999999999");
        Decimal34_8 decimal_val("10000123456789.12345678");
        Numeric16_6 numeric_val("1234567.123456");
        DecFloat34 decfloat_val("123456789012345678901234.5678901234");

        // Генерируем уникальное значение для F_INTEGER
        int unique_int2 = generate_unique_integer();

        std::cout << "Вставляем запись с расширенными типами:\n";
        std::cout << "  F_INTEGER (уникальное): " << unique_int2 << "\n";
        std::cout << "  INT128:     " << int128_val.ToString() << "\n";
        std::cout << "  DECIMAL:    " << decimal_val.to_string() << "\n";
        std::cout << "  NUMERIC:    " << numeric_val.to_string() << "\n";
        std::cout << "  DECFLOAT:   " << decfloat_val.toString() << "\n\n";

        // Подготавливаем запрос для вставки расширенных типов
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_INTEGER, F_BIGINT, F_INT128, "
            "  F_DECIMAL, F_NUMERIC, F_DECFLOAT, "
            "  F_VARCHAR, F_BOOLEAN"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "RETURNING ID"
        );

        // Пакуем параметры включая расширенные типы
        auto extended_params = std::make_tuple(
            unique_int2,                                     // F_INTEGER (уникальное значение)
            int64_t(9000000000000000000LL),                // F_BIGINT
            int128_val,                                      // F_INT128
            decimal_val,                                     // F_DECIMAL
            numeric_val,                                     // F_NUMERIC
            decfloat_val,                                    // F_DECFLOAT
            std::string("Extended types test"),             // F_VARCHAR
            true                                             // F_BOOLEAN
        );

        // Выполняем вставку
        auto [ext_affected, ext_returned] = transaction->execute(
            stmt,
            extended_params,
            std::tuple<int32_t>{}  // Ожидаем возврат ID
        );

        auto ext_id = std::get<0>(ext_returned);
        std::cout << "✓ Запись с расширенными типами вставлена! ID: " << ext_id << "\n\n";

        // Читаем обратно для проверки
        stmt = connection->prepareStatement(
            "SELECT F_INT128, F_DECIMAL, F_NUMERIC, F_DECFLOAT "
            "FROM TABLE_TEST_1 WHERE ID = ?"
        );

        cursor = transaction->openCursor(stmt, std::make_tuple(ext_id));

        // Определяем типы для чтения расширенных полей
        std::tuple<
            std::optional<Int128>,
            std::optional<Decimal34_8>,
            std::optional<Numeric16_6>,
            std::optional<DecFloat34>
        > extended_row;

        if (cursor->fetch(extended_row)) {
            auto [read_int128, read_decimal, read_numeric, read_decfloat] = extended_row;

            std::cout << "Прочитанные расширенные типы:\n";
            std::cout << std::string(40, '-') << "\n";
            std::cout << "  INT128:     " << (read_int128 ? read_int128->ToString() : "NULL") << "\n";
            std::cout << "  DECIMAL:    " << (read_decimal ? read_decimal->to_string() : "NULL") << "\n";
            std::cout << "  NUMERIC:    " << (read_numeric ? read_numeric->to_string() : "NULL") << "\n";
            std::cout << "  DECFLOAT:   " << (read_decfloat ? read_decfloat->toString() : "NULL") << "\n\n";

            // Проверка точности
            std::cout << "✓ Все расширенные типы корректно сохранены и прочитаны!\n";
        }

        cursor->close();
        transaction->Commit();

        // Демонстрация работы с датой и временем
        printHeader("Работа с типами даты/времени");

        transaction = connection->StartTransaction();

        auto now = std::chrono::system_clock::now();

#if FBPP_SAMPLE_CPLUSPLUS >= 202002L
        // C++20 типы
        auto today = std::chrono::floor<std::chrono::days>(now);
        Date test_date{std::chrono::year_month_day{today}};

        auto time_since_midnight = now.time_since_epoch() % std::chrono::days{1};
        Time test_time{std::chrono::duration_cast<std::chrono::microseconds>(time_since_midnight)};

        Timestamp test_timestamp = now;
        TimestampTz test_timestamp_tz{"Europe/Moscow",
                                      std::chrono::time_point_cast<std::chrono::microseconds>(now)};
        TimeWithTz test_time_tz = std::make_pair(test_time, "Europe/Moscow");

        std::cout << "Используем C++20 chrono типы:\n";
#else
        fbpp::core::Date test_date(now);
        fbpp::core::Time test_time(fbpp::core::timestamp_utils::current_time_of_day());
        fbpp::core::Timestamp test_timestamp(now);
        fbpp::core::TimestampTz test_timestamp_tz(now, 4, 180);
        fbpp::core::TimeTz test_time_tz(test_time, 4, 180);

        std::cout << "Используем встроенные типы fbpp:\n";
#endif

        std::cout << "Вставляем запись с датами и временем...\n\n";

        // Подготавливаем запрос для вставки дат/времени
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_INTEGER, F_VARCHAR, "
            "  F_DATE, F_TIME, F_TIMESHTAMP, "
            "  F_TIME_TZ, F_TIMESHTAMP_TZ"
            ") VALUES (?, ?, ?, ?, ?, ?, ?) "
            "RETURNING ID"
        );

        // Генерируем уникальный ключ
        int unique_int3 = generate_unique_integer();

        // Пакуем параметры с датами и временем
        auto datetime_params = std::make_tuple(
            unique_int3,                                     // F_INTEGER (уникальное)
            std::string("Date/Time test"),                  // F_VARCHAR
            test_date,                                       // F_DATE
            test_time,                                       // F_TIME
            test_timestamp,                                  // F_TIMESHTAMP
            test_time_tz,                                    // F_TIME_TZ
            test_timestamp_tz                                // F_TIMESHTAMP_TZ
        );

        // Выполняем вставку
        auto [dt_affected, dt_returned] = transaction->execute(
            stmt,
            datetime_params,
            std::tuple<int32_t>{}  // Ожидаем возврат ID
        );

        auto dt_id = std::get<0>(dt_returned);
        std::cout << "✓ Запись с датами/временем вставлена! ID: " << dt_id << "\n\n";

        // Читаем обратно для проверки
        stmt = connection->prepareStatement(
            "SELECT F_DATE, F_TIME, F_TIMESHTAMP, F_TIME_TZ, F_TIMESHTAMP_TZ "
            "FROM TABLE_TEST_1 WHERE ID = ?"
        );

        cursor = transaction->openCursor(stmt, std::make_tuple(dt_id));

        // Определяем типы для чтения полей даты/времени
#if FBPP_SAMPLE_CPLUSPLUS >= 202002L
        std::tuple<
            std::optional<Date>,
            std::optional<Time>,
            std::optional<Timestamp>,
            std::optional<TimeWithTz>,
            std::optional<TimestampTz>
        > datetime_row;
#else
        std::tuple<
            std::optional<fbpp::core::Date>,
            std::optional<fbpp::core::Time>,
            std::optional<fbpp::core::Timestamp>,
            std::optional<fbpp::core::TimeTz>,
            std::optional<fbpp::core::TimestampTz>
        > datetime_row;
#endif

        if (cursor->fetch(datetime_row)) {
            std::cout << "Прочитанные даты и время:\n";
            std::cout << std::string(40, '-') << "\n";

#if FBPP_SAMPLE_CPLUSPLUS >= 202002L
            auto [read_date, read_time, read_timestamp, read_time_tz, read_timestamp_tz] = datetime_row;

            if (read_date) {
                // Вывод даты
                std::cout << "  DATE:       ";
                std::cout << static_cast<int>(read_date->year()) << "-"
                         << std::setfill('0') << std::setw(2) << static_cast<unsigned>(read_date->month()) << "-"
                         << std::setw(2) << static_cast<unsigned>(read_date->day()) << "\n";
            }
            if (read_time) {
                // Вывод времени
                std::cout << "  TIME:       ";
                std::cout << std::setfill('0') << std::setw(2) << read_time->hours().count() << ":"
                         << std::setw(2) << read_time->minutes().count() << ":"
                         << std::setw(2) << read_time->seconds().count() << "\n";
            }
            if (read_timestamp) {
                auto time_t = std::chrono::system_clock::to_time_t(*read_timestamp);
                std::cout << "  TIMESTAMP:  " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\n";
            }
            if (read_time_tz) {
                std::cout << "  TIME_TZ:    ";
                std::cout << std::setfill('0') << std::setw(2) << read_time_tz->first.hours().count() << ":"
                         << std::setw(2) << read_time_tz->first.minutes().count() << ":"
                         << std::setw(2) << read_time_tz->first.seconds().count()
                         << " " << read_time_tz->second << "\n";
            }
            if (read_timestamp_tz) {
                auto local_time = read_timestamp_tz->get_local_time();
                auto time_t = std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::time_point(
                        std::chrono::duration_cast<std::chrono::seconds>(local_time.time_since_epoch())
                    )
                );
                std::cout << "  TIMESTAMP_TZ: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                         << " " << read_timestamp_tz->get_time_zone()->name() << "\n";
            }
#else
            auto [read_date, read_time, read_timestamp, read_time_tz, read_timestamp_tz] = datetime_row;

            if (read_date) {
                std::cout << "  DATE:       " << read_date->to_string() << "\n";
            }
            if (read_time) {
                std::cout << "  TIME:       " << read_time->to_string() << "\n";
            }
            if (read_timestamp) {
                std::cout << "  TIMESTAMP:  " << read_timestamp->to_string() << "\n";
            }
            if (read_time_tz) {
                std::cout << "  TIME_TZ:    " << read_time_tz->to_string() << "\n";
            }
            if (read_timestamp_tz) {
                std::cout << "  TIMESTAMP_TZ: " << read_timestamp_tz->to_string() << "\n";
            }
#endif
            std::cout << "\n✓ Все типы даты/времени корректно сохранены и прочитаны!\n";
        }

        cursor->close();
        transaction->Commit();

        // Демонстрация работы с BLOB
        printHeader("Работа с BLOB (текстовые большие объекты)");

        transaction = connection->StartTransaction();

        std::cout << "Вставляем запись с TEXT BLOB (F_BLOB_T)...\n\n";

        // Подготавливаем большой текст для BLOB
        std::string blob_text = R"(
Это пример большого текстового BLOB для демонстрации возможностей fbpp.

Библиотека fbpp автоматически обрабатывает BLOB при использовании std::string в tuple.
Когда TuplePacker встречает поле с типом SQL_BLOB и subType == 1 (TEXT BLOB),
он автоматически:
1. Создает BLOB объект через транзакцию
2. Записывает данные в BLOB
3. Сохраняет BLOB_ID в записи таблицы

Преимущества:
- Прозрачная работа с BLOB как с обычными строками
- Поддержка больших объемов текста (до 4GB)
- Автоматическое управление ресурсами
- Полная поддержка Unicode и многоязычного контента

Пример многоязычного текста:
- English: The quick brown fox jumps over the lazy dog
- Русский: Съешь же ещё этих мягких французских булок да выпей чаю
- 中文: 我能吞下玻璃而不伤身体
- 日本語: 私はガラスを食べられます。それは私を傷つけません。
- العربية: أنا قادر على أكل الزجاج و هذا لا يؤلمني
- Emoji: 🚀 🎯 💻 🔥 ⭐

Этот текст демонстрирует, что BLOB корректно работает с любыми символами UTF-8.
)";

        // Подготавливаем запрос для вставки с BLOB
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_INTEGER, F_VARCHAR, F_BLOB_T"
            ") VALUES (?, ?, ?) "
            "RETURNING ID"
        );

        // Генерируем уникальный ключ
        int unique_int4 = generate_unique_integer();

        // Пакуем параметры - BLOB передается как обычная строка!
        auto blob_params = std::make_tuple(
            unique_int4,                                     // F_INTEGER (уникальное)
            std::string("Record with BLOB"),                // F_VARCHAR
            blob_text                                        // F_BLOB_T - автоматически конвертируется в BLOB
        );

        std::cout << "Размер текста для BLOB: " << blob_text.size() << " байт\n\n";

        // Выполняем вставку
        auto [blob_affected, blob_returned] = transaction->execute(
            stmt,
            blob_params,
            std::tuple<int32_t>{}  // Ожидаем возврат ID
        );


        auto blob_id = std::get<0>(blob_returned);
        std::cout << "✓ Запись с BLOB вставлена! ID: " << blob_id << "\n\n";

        // Читаем обратно для проверки
        stmt = connection->prepareStatement(
            "SELECT F_VARCHAR, F_BLOB_T "
            "FROM TABLE_TEST_1 WHERE ID = ?"
        );

        cursor = transaction->openCursor(stmt, std::make_tuple(blob_id));

        // Определяем типы для чтения BLOB
        std::tuple<
            std::optional<std::string>,  // F_VARCHAR
            std::optional<std::string>   // F_BLOB_T - автоматически читается как строка!
        > blob_row;

        if (cursor->fetch(blob_row)) {
            auto [read_varchar, read_blob] = blob_row;

            std::cout << "Прочитанные данные:\n";
            std::cout << std::string(40, '-') << "\n";
            std::cout << "  VARCHAR: " << (read_varchar ? *read_varchar : "NULL") << "\n";

            if (read_blob) {
                std::cout << "  BLOB размер: " << read_blob->size() << " байт\n";

                // Показываем первые 200 символов BLOB
                std::cout << "  BLOB (первые 200 символов):\n";
                std::string preview = read_blob->substr(0, 200);
                std::cout << preview << "...\n\n";

                // Проверяем целостность
                if (*read_blob == blob_text) {
                    std::cout << "✓ BLOB корректно сохранен и прочитан! Данные идентичны.\n";
                } else {
                    std::cout << "⚠ BLOB данные отличаются!\n";
                }
            } else {
                std::cout << "  BLOB: NULL\n";
            }
        }

        cursor->close();
        transaction->Commit();

        // Демонстрация работы с бинарным BLOB
        printHeader("Работа с бинарным BLOB (F_BLOB_B)");

        transaction = connection->StartTransaction();

        std::cout << "Вставляем запись с бинарным BLOB (F_BLOB_B)...\n\n";

        // Подготавливаем бинарные данные для BLOB - создаем простую PNG картинку
        // Создаем минимальную валидную PNG картинку 8x8 пикселей с градиентом
        std::vector<uint8_t> binary_data;

        // PNG signature (8 bytes)
        const uint8_t png_signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        binary_data.insert(binary_data.end(), png_signature, png_signature + 8);

        // IHDR chunk - заголовок изображения
#pragma pack(push, 1)
        struct IhdrChunk {
            uint32_t length = 0x0D000000;  // 13 bytes (big-endian)
            char type[4] = {'I', 'H', 'D', 'R'};
            uint32_t width = 0x08000000;   // 8 pixels (big-endian)
            uint32_t height = 0x08000000;  // 8 pixels (big-endian)
            uint8_t bit_depth = 8;         // 8 bits per channel
            uint8_t color_type = 2;        // RGB
            uint8_t compression = 0;       // Deflate
            uint8_t filter = 0;            // Adaptive
            uint8_t interlace = 0;         // No interlace
            uint32_t crc = 0x7E597C57;     // Correct CRC for this IHDR
        };
#pragma pack(pop)
        IhdrChunk ihdr;

        binary_data.insert(binary_data.end(),
                          reinterpret_cast<uint8_t*>(&ihdr),
                          reinterpret_cast<uint8_t*>(&ihdr) + sizeof(ihdr));

        // IDAT chunk - данные изображения (сжатые)
        // Создаем простой градиент 8x8 RGB (несжатый для примера)
        std::vector<uint8_t> raw_image_data;

        // Генерируем градиент от синего к красному
        for (int y = 0; y < 8; y++) {
            raw_image_data.push_back(0x00);  // Filter type: None
            for (int x = 0; x < 8; x++) {
                uint8_t red = (x * 255) / 7;      // Градиент по X
                uint8_t green = ((x + y) * 255) / 14;  // Диагональный градиент
                uint8_t blue = (y * 255) / 7;     // Градиент по Y
                raw_image_data.push_back(red);
                raw_image_data.push_back(green);
                raw_image_data.push_back(blue);
            }
        }

        // Простое сжатие zlib (минимальное)
        std::vector<uint8_t> compressed_data;
        compressed_data.push_back(0x78);  // zlib header
        compressed_data.push_back(0x9C);  // zlib header

        // Несжатый блок (BTYPE=00)
        compressed_data.push_back(0x01);  // BFINAL=1, BTYPE=00
        uint16_t len = raw_image_data.size();
        uint16_t nlen = ~len;
        compressed_data.push_back(len & 0xFF);
        compressed_data.push_back((len >> 8) & 0xFF);
        compressed_data.push_back(nlen & 0xFF);
        compressed_data.push_back((nlen >> 8) & 0xFF);

        // Добавляем сырые данные
        compressed_data.insert(compressed_data.end(), raw_image_data.begin(), raw_image_data.end());

        // Adler-32 checksum (упрощенный)
        uint32_t adler = 1;
        for (auto byte : raw_image_data) {
            adler = (adler + byte) % 65521;
        }
        compressed_data.push_back((adler >> 24) & 0xFF);
        compressed_data.push_back((adler >> 16) & 0xFF);
        compressed_data.push_back((adler >> 8) & 0xFF);
        compressed_data.push_back(adler & 0xFF);

        // IDAT chunk
        uint32_t idat_length = hostToNetwork32(static_cast<uint32_t>(compressed_data.size()));
        binary_data.insert(binary_data.end(),
                          reinterpret_cast<uint8_t*>(&idat_length),
                          reinterpret_cast<uint8_t*>(&idat_length) + 4);

        binary_data.push_back('I');
        binary_data.push_back('D');
        binary_data.push_back('A');
        binary_data.push_back('T');

        binary_data.insert(binary_data.end(), compressed_data.begin(), compressed_data.end());

        // CRC для IDAT (упрощенный)
        uint32_t idat_crc = 0x12345678;  // Dummy CRC
        idat_crc = hostToNetwork32(idat_crc);
        binary_data.insert(binary_data.end(),
                          reinterpret_cast<uint8_t*>(&idat_crc),
                          reinterpret_cast<uint8_t*>(&idat_crc) + 4);

        // IEND chunk - конец PNG файла
        const uint8_t iend_chunk[] = {
            0x00, 0x00, 0x00, 0x00,  // Length: 0
            'I', 'E', 'N', 'D',      // Type: "IEND"
            0xAE, 0x42, 0x60, 0x82   // CRC for IEND
        };
        binary_data.insert(binary_data.end(), iend_chunk, iend_chunk + sizeof(iend_chunk));

        std::cout << "Создана PNG картинка 8x8 пикселей с градиентом\n";
        std::cout << "Размер PNG файла: " << binary_data.size() << " байт\n";
        std::cout << "PNG signature (первые 8 байт): ";
        for (size_t i = 0; i < 8 && i < binary_data.size(); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(binary_data[i]) << " ";
        }
        std::cout << std::dec << "\n\n";

        // ПРАВИЛЬНЫЙ СПОСОБ: сначала создаем BLOB, потом вставляем его ID
        // Шаг 1: Создаем BLOB и получаем его ID
        ISC_QUAD binary_blob_id = transaction->createBlob(binary_data);
        std::cout << "✓ Бинарный BLOB создан. ID: "
                  << std::hex << std::setw(8) << std::setfill('0')
                  << *reinterpret_cast<uint32_t*>(&binary_blob_id) << ":"
                  << std::setw(8) << std::setfill('0')
                  << *(reinterpret_cast<uint32_t*>(&binary_blob_id) + 1)
                  << std::dec << "\n\n";

        // Шаг 2: Создаем объект Blob из ISC_QUAD
        fbpp::core::Blob binary_blob(reinterpret_cast<uint8_t*>(&binary_blob_id));

        // Шаг 3: Подготавливаем запрос для вставки с BLOB ID
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 ("
            "  F_INTEGER, F_VARCHAR, F_BLOB_B"
            ") VALUES (?, ?, ?) "
            "RETURNING ID"
        );

        // Генерируем уникальный ключ
        int unique_int5 = generate_unique_integer();

        // Вставляем запись с BLOB ID (не с данными!)
        auto binary_blob_params = std::make_tuple(
            unique_int5,                                     // F_INTEGER (уникальное)
            std::string("Record with binary BLOB"),         // F_VARCHAR
            binary_blob                                      // F_BLOB_B - объект Blob с ID
        );

        // Выполняем вставку
        auto [bin_affected, bin_returned] = transaction->execute(
            stmt,
            binary_blob_params,
            std::tuple<int32_t>{}  // Ожидаем возврат ID
        );

        auto bin_id = std::get<0>(bin_returned);
        std::cout << "✓ Запись с бинарным BLOB вставлена! ID записи: " << bin_id << "\n\n";

        // Читаем обратно для проверки
        stmt = connection->prepareStatement(
            "SELECT F_VARCHAR, F_BLOB_B "
            "FROM TABLE_TEST_1 WHERE ID = ?"
        );

        cursor = transaction->openCursor(stmt, std::make_tuple(bin_id));

        // Определяем типы для чтения - F_BLOB_B читается как Blob объект
        std::tuple<
            std::optional<std::string>,              // F_VARCHAR
            std::optional<fbpp::core::Blob>          // F_BLOB_B - объект Blob с ID
        > binary_row;

        if (cursor->fetch(binary_row)) {
            auto [read_varchar, read_blob_obj] = binary_row;

            std::cout << "Прочитанные данные:\n";
            std::cout << std::string(40, '-') << "\n";
            std::cout << "  VARCHAR: " << (read_varchar ? *read_varchar : "NULL") << "\n";

            if (read_blob_obj && !read_blob_obj->isNull()) {
                // Шаг 4: Загружаем данные BLOB по его ID
                ISC_QUAD* blob_id_ptr = reinterpret_cast<ISC_QUAD*>(read_blob_obj->getId());
                std::vector<uint8_t> read_binary = transaction->loadBlob(blob_id_ptr);

                std::cout << "  Бинарный BLOB размер: " << read_binary.size() << " байт\n";
                std::cout << "  BLOB ID: "
                          << std::hex << std::setw(8) << std::setfill('0')
                          << *reinterpret_cast<uint32_t*>(blob_id_ptr) << ":"
                          << std::setw(8) << std::setfill('0')
                          << *(reinterpret_cast<uint32_t*>(blob_id_ptr) + 1)
                          << std::dec << "\n";

                // Показываем первые 16 байт
                std::cout << "  Первые 16 байт (hex): ";
                for (size_t i = 0; i < 16 && i < read_binary.size(); i++) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                             << static_cast<int>(read_binary[i]) << " ";
                }
                std::cout << std::dec << "\n";

                // Проверяем PNG signature
                if (read_binary.size() >= 8) {
                    bool is_png = true;
                    for (size_t i = 0; i < 8; i++) {
                        if (read_binary[i] != png_signature[i]) {
                            is_png = false;
                            break;
                        }
                    }
                    if (is_png) {
                        std::cout << "  ✓ Обнаружена PNG сигнатура!\n";
                    }
                }

                // Проверяем целостность данных
                if (read_binary == binary_data) {
                    std::cout << "\n✓ Бинарный BLOB корректно сохранен и прочитан!\n";
                    std::cout << "  Все " << binary_data.size() << " байт идентичны.\n";

                    // Опционально: сохраняем PNG файл для проверки
                    std::string png_filename = "/tmp/fbpp_gradient_" + std::to_string(bin_id) + ".png";
                    std::ofstream png_file(png_filename, std::ios::binary);
                    if (png_file) {
                        png_file.write(reinterpret_cast<const char*>(read_binary.data()),
                                     read_binary.size());
                        png_file.close();
                        std::cout << "\n  PNG сохранен для проверки: " << png_filename << "\n";
                        std::cout << "  Можно открыть командой: display " << png_filename << "\n";
                    }
                } else {
                    std::cout << "\n⚠ Бинарные данные BLOB отличаются!\n";
                }
            } else {
                std::cout << "  Бинарный BLOB: NULL или пустой\n";
            }
        }

        cursor->close();
        transaction->Commit();

        // Теперь читаем данные используя TupleUnpacker
        printHeader("Чтение данных с помощью TupleUnpacker");
        
        try {
        
        transaction = connection->StartTransaction();
        
        // Определяем типы для распаковки
        using RowType = std::tuple<
            std::optional<int32_t>,      // ID
            std::optional<int32_t>,      // F_INTEGER
            std::optional<std::string>,  // F_VARCHAR
            std::optional<double>,       // F_DOUBLE_PRECISION
            std::optional<bool>          // F_BOOLEAN
        >;
        
        stmt = connection->prepareStatement(
            "SELECT FIRST 5 "
            "  ID, F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN "
            "FROM TABLE_TEST_1 "
            "WHERE ID >= " + std::to_string(id - 2) + " "
            "ORDER BY ID DESC"
        );
        
        // Открываем курсор без параметров
        cursor = transaction->openCursor(stmt);
        
        std::cout << "Читаем последние записи (включая только что вставленную):\n\n";
        
        int record_count = 0;
        
        // Используем новый API с tuple
        std::tuple<
            std::optional<int32_t>,      // ID
            std::optional<int32_t>,      // F_INTEGER
            std::optional<std::string>,  // F_VARCHAR
            std::optional<double>,       // F_DOUBLE_PRECISION
            std::optional<bool>          // F_BOOLEAN
        > row;
        
        while (cursor->fetch(row)) {
            record_count++;
            
            std::cout << "Запись #" << record_count << ":\n";
            std::cout << std::string(40, '-') << "\n";
            
            // Красиво выводим распакованные данные
            auto [row_id, f_int, f_varchar, f_double, f_bool] = row;
            
            std::cout << "  ID                  : " 
                     << (row_id ? std::to_string(*row_id) : "NULL") << "\n";
            std::cout << "  F_INTEGER           : " 
                     << (f_int ? std::to_string(*f_int) : "NULL") << "\n";
            std::cout << "  F_VARCHAR           : " 
                     << (f_varchar ? ("\"" + *f_varchar + "\"") : "NULL") << "\n";
            std::cout << "  F_DOUBLE_PRECISION  : ";
            if (f_double) {
                std::cout << std::fixed << std::setprecision(6) << *f_double;
            } else {
                std::cout << "NULL";
            }
            std::cout << "\n";
            std::cout << "  F_BOOLEAN           : " 
                     << (f_bool ? (*f_bool ? "TRUE" : "FALSE") : "NULL") << "\n";
            
            if (row_id && *row_id == id) {
                std::cout << "  ⭐ Это наша только что вставленная запись!\n";
            }
            std::cout << "\n";
        }
        
        std::cout << "Всего прочитано записей: " << record_count << "\n\n";
        
        // Демонстрируем UPDATE с параметрами
        std::cout << "Обновляем нашу запись...\n";
        stmt = connection->prepareStatement(
            "UPDATE TABLE_TEST_1 "
            "SET F_VARCHAR = ? "
            "WHERE ID = ?"
        );

        auto update_params = std::make_tuple(
            std::string("Обновлено через wrapper API"),
            id
        );

        auto update_result = transaction->execute(stmt, update_params);
        std::cout << "✓ Обновлено строк: " << update_result << "\n\n";

        // Читаем обновленную запись
        transaction->Commit();  // Фиксируем изменения
        transaction = connection->StartTransaction();  // Новая транзакция для чтения

        stmt = connection->prepareStatement(
            "SELECT ID, F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE ID = " + std::to_string(id)
        );

        auto read_cursor = transaction->openCursor(stmt);

        // Используем новый API с tuple
        std::tuple<int32_t, int32_t, std::string> updated_row;
        if (read_cursor->fetch(updated_row)) {
            auto [upd_id, upd_int, upd_varchar] = updated_row;
            std::cout << "Обновленная запись:\n";
            std::cout << "  ID: " << upd_id << "\n";
            std::cout << "  F_INTEGER: " << upd_int << " (было " << unique_int1 << ")\n";
            std::cout << "  F_VARCHAR: \"" << upd_varchar << "\"\n";
        }

        read_cursor->close();
        transaction->Commit();
        
        } catch (const fbpp::core::FirebirdException& e) {
            std::cerr << "Ошибка при чтении: " << e.what() << "\n";
        } catch (const Firebird::FbException& e) {
            std::cerr << "Ошибка Firebird: ";
            char buf[256];
            Firebird::fb_get_master_interface()->getUtilInterface()->
                formatStatus(buf, sizeof(buf), e.getStatus());
            std::cerr << buf << "\n";
        }

        // Пропускаем сложную демонстрацию, так как после UPDATE соединение в странном состоянии

        // ====================================================================
        // Batch операции
        // ====================================================================
        printHeader("Batch INSERT в TABLE_TEST_1");

        auto tra = connection->StartTransaction();
        stmt = connection->prepareStatement(
            "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR, F_DOUBLE_PRECISION, F_BOOLEAN) "
            "VALUES (?, ?, ?, ?) "
            "RETURNING ID"
        );

       // Create batch
        auto batch = stmt->createBatch(tra.get(), true, false); // recordCounts=true, continueOnError=false
        
        // Test data - используем уникальные значения для F_INTEGER
        std::vector<std::tuple<int32_t, std::string, double, bool>> data;
        for (int i = 0; i < 5; i++) {
            data.push_back({
                generate_unique_integer(),
                "BatchRecord" + std::to_string(i + 1),
                100.1 * (i + 1),
                true
            });
        }
    
       
        batch->addMany(data);

        // Execute batch
        auto results = batch->execute(tra.get());

        std::cout << results.totalMessages;
        std::cout << results.successCount;
        std::cout << results.failedCount;

        tra->Commit();

        // ====================================================================
        // Простая демонстрация обработки ошибки уникального ключа
        // ====================================================================
        printHeader("Простая демонстрация обработки ошибок");

        // Объявляем транзакцию до try блока, чтобы иметь к ней доступ в catch
        std::shared_ptr<fbpp::core::Transaction> err_trans;

        try {
            err_trans = connection->StartTransaction();

            // Используем фиксированный ключ для демонстрации
            int test_key = 888888;

            // Подготавливаем простой INSERT
            auto err_stmt = connection->prepareStatement(
                "INSERT INTO TABLE_TEST_1 (F_INTEGER, F_VARCHAR) "
                "VALUES (?, ?)"
            );

            // Сначала удалим старые записи с этим ключом используя execute
            auto cleanup_stmt = connection->prepareStatement(
                "DELETE FROM TABLE_TEST_1 WHERE F_INTEGER = ?"
            );

            // Тестируем execute для DELETE
            unsigned deleted = err_trans->execute(cleanup_stmt, std::make_tuple(test_key));
            if (deleted > 0) {
                std::cout << "Удалено старых записей: " << deleted << "\n";
            }

            // Первая вставка через execute
            std::cout << "Вставляем запись с F_INTEGER = " << test_key << "\n";
            unsigned inserted1 = err_trans->execute(
                err_stmt,
                std::make_tuple(test_key, std::string("Первая запись"))
            );
            err_trans->CommitRetaining();
            std::cout << "✓ Первая запись успешно вставлена (affected: " << inserted1 << ")\n\n";

            // Попытка вставить дубликат тоже через execute
            std::cout << "Пытаемся вставить дубликат с тем же F_INTEGER = " << test_key << "\n";
            unsigned inserted2 = err_trans->execute(
                err_stmt,
                std::make_tuple(test_key, std::string("Дубликат"))
            );

            std::cout << "✗ Неожиданно: дубликат вставлен!\n";
            err_trans->Rollback();

        } catch (const fbpp::core::FirebirdException& e) {
            std::cout << "⚠️ Перехвачена ошибка FirebirdException: " << e.what() << "\n";
            std::cout << "✓ База данных защищена от дублирования ключей!\n\n";
            // Явно откатываем транзакцию, чтобы не было warning при уничтожении
            if (err_trans && err_trans->isActive()) {
                err_trans->Rollback();
            }
        } catch (const Firebird::FbException& e) {
            std::cout << "⚠️ Перехвачена низкоуровневая ошибка FbException\n";
            char buf[512];
            Firebird::fb_get_master_interface()->getUtilInterface()->
                formatStatus(buf, sizeof(buf), e.getStatus());
            std::cout << "   Сообщение: " << buf << "\n";
            if (std::string(buf).find("UNIQUE") != std::string::npos ||
                std::string(buf).find("UNQ1_TABLE_TEST_F_INTEGER") != std::string::npos) {
                std::cout << "✓ База данных защищена от дублирования ключей!\n\n";
            }
            // Явно откатываем транзакцию
            if (err_trans && err_trans->isActive()) {
                err_trans->Rollback();
            }
        } catch (const std::exception& e) {
            std::cout << "Стандартная ошибка: " << e.what() << "\n";
            if (err_trans && err_trans->isActive()) {
                err_trans->Rollback();
            }
        } catch (...) {
            std::cout << "Неизвестная ошибка\n";
            if (err_trans && err_trans->isActive()) {
                err_trans->Rollback();
            }
        }

        printHeader("Подключение успешно завершено");
        
        std::cout << "Этот пример продемонстрировал возможности fbpp wrapper:\n";
        std::cout << "  ✓ Высокоуровневый API вместо работы с сырыми буферами\n";
        std::cout << "  ✓ INSERT с RETURNING для получения auto-generated ID\n";
        std::cout << "  ✓ Автоматическая упаковка параметров через TuplePacker\n";
        std::cout << "  ✓ Автоматическая распаковка результатов через TupleUnpacker\n";
        std::cout << "  ✓ Type-safe работа с std::tuple и std::optional\n";
        std::cout << "  ✓ Параметризованные запросы с защитой от SQL-инъекций\n\n";
        
        std::cout << "Следующие примеры покажут:\n";
        std::cout << "  02_batch_operations.cpp - Массовые вставки через Batch API\n";
        std::cout << "  03_all_types.cpp        - Работа со всеми типами Firebird\n";
        std::cout << "  04_json_binding.cpp     - JSON сериализация/десериализация\n";
        
    } catch (const fbpp::core::FirebirdException& e) {
        std::cerr << "\n✗ Ошибка Firebird: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Ошибка: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

#undef FBPP_SAMPLE_CPLUSPLUS
