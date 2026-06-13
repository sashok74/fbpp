#include <gtest/gtest.h>
#include "fbpp/core/exception.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/connection.hpp"
#include "../test_base.hpp"
#include <firebird/Interface.h>
#ifdef _WIN32
#include <iberror.h>
#else
#include <firebird/iberror.h>
#endif

using namespace fbpp::core;

class FirebirdExceptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure Environment is initialized
        Environment::getInstance();
    }
};

// ============== Позитивные сценарии ==============

TEST_F(FirebirdExceptionTest, ConstructorFromString) {
    // Конструктор из строки
    const std::string msg = "Test error message";
    FirebirdException ex(msg);
    
    EXPECT_STREQ(ex.what(), msg.c_str());
    EXPECT_EQ(ex.getErrorCode(), 0);
    EXPECT_EQ(ex.getSQLState(), "HY000");  // Default SQL state
    EXPECT_EQ(ex.getSQLCode(), 0);
    EXPECT_TRUE(ex.getStatusVector().empty());
    
    // getErrorMessages should be empty or contain the message
    auto messages = ex.getErrorMessages();
    EXPECT_TRUE(messages.empty() || 
                (messages.size() == 1 && messages[0] == msg));
}

TEST_F(FirebirdExceptionTest, ConversionFromFbException) {
    // Создаем искусственный FbException с ошибкой
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();
    
    // Симулируем ошибку "table not found"
    intptr_t errorVector[] = {
        isc_arg_gds,
        335544580,  // isc_dsql_relation_err - Table unknown
        isc_arg_string,
        reinterpret_cast<intptr_t>("TEST_TABLE"),
        isc_arg_end
    };
    
    status->setErrors(errorVector);
    Firebird::FbException fbEx(status);
    
    // Конвертируем в наш FirebirdException
    FirebirdException ex(fbEx);
    
    // Проверяем что what() не пустое
    EXPECT_NE(std::string(ex.what()).length(), 0u);
    
    // Проверяем что error code установлен
    EXPECT_EQ(ex.getErrorCode(), 335544580);

    // SQLCODE должен быть вычислен из исходного status vector
    EXPECT_EQ(ex.getSQLCode(),
              isc_sqlcode(reinterpret_cast<const ISC_STATUS*>(status->getErrors())));
    
    // SQL State должен быть установлен
    EXPECT_FALSE(ex.getSQLState().empty());
    
    // Должен быть хотя бы один error message
    EXPECT_GE(ex.getErrorMessages().size(), 1u);

    ASSERT_GE(ex.getStatusVector().size(), 2u);
    EXPECT_EQ(ex.getStatusVector()[0].tag, isc_arg_gds);
    EXPECT_EQ(ex.getStatusVector()[0].payloadKind,
              FirebirdStatusEntry::PayloadKind::integer);
    EXPECT_EQ(ex.getStatusVector()[0].numericValue, 335544580);
    EXPECT_EQ(ex.getStatusVector()[1].tag, isc_arg_string);
    EXPECT_EQ(ex.getStatusVector()[1].payloadKind,
              FirebirdStatusEntry::PayloadKind::text);
    EXPECT_EQ(ex.getStatusVector()[1].textValue, "TEST_TABLE");
    
    // Cleanup
    status->dispose();
}

TEST_F(FirebirdExceptionTest, ErrorChain) {
    // Цепочка ошибок с несколькими GDS кодами
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();
    
    // Создаем цепочку ошибок
    intptr_t errorVector[] = {
        isc_arg_gds,
        335544321,  // isc_arith_except - Arithmetic exception
        isc_arg_gds,
        335544778,  // isc_division_by_zero
        isc_arg_string,
        reinterpret_cast<intptr_t>("Division by zero occurred"),
        isc_arg_number,
        42,  // Some context number
        isc_arg_warning,
        335544999,  // Some warning code
        isc_arg_end
    };
    
    status->setErrors(errorVector);
    Firebird::FbException fbEx(status);
    
    FirebirdException ex(fbEx);
    
    // Проверяем множественные сообщения
    auto messages = ex.getErrorMessages();
    EXPECT_GE(messages.size(), 2u);
    
    // what() должно содержать "Error chain:" при множественных ошибках
    std::string whatStr = ex.what();
    if (messages.size() > 1) {
        EXPECT_NE(whatStr.find("Error chain:"), std::string::npos);
    }
    
    // Первый error code должен быть сохранен
    EXPECT_EQ(ex.getErrorCode(), 335544321);
    EXPECT_EQ(ex.getSQLCode(),
              isc_sqlcode(reinterpret_cast<const ISC_STATUS*>(status->getErrors())));
    
    // Cleanup
    status->dispose();
}

// ============== Негативные сценарии ==============

TEST_F(FirebirdExceptionTest, NullStatus) {
    // Тест с nullptr статусом - не должно падать
    
    // Создаем минимальный FbException-подобный объект
    // Но так как конструктор FbException требует валидный IStatus,
    // проверим через extractErrorDetails напрямую (если это возможно)
    
    // Альтернатива: создаем пустой статус
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();
    status->init();  // Пустой статус
    
    Firebird::FbException fbEx(status);
    
    // Не должно упасть при пустом статусе
    EXPECT_NO_THROW({
        FirebirdException ex(fbEx);
        EXPECT_EQ(ex.getErrorCode(), 0);
        EXPECT_EQ(ex.getSQLState(), "HY000");
    });
    
    status->dispose();
}

TEST_F(FirebirdExceptionTest, LongErrorMessage) {
    // Тест с очень длинным сообщением
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();
    
    // Создаем очень длинную строку
    std::string longStr(2000, 'X');  // 2000 символов
    
    intptr_t errorVector[] = {
        isc_arg_gds,
        335544321,
        isc_arg_string,
        reinterpret_cast<intptr_t>(longStr.c_str()),
        isc_arg_end
    };
    
    status->setErrors(errorVector);
    Firebird::FbException fbEx(status);
    
    // Не должно упасть с длинным сообщением
    EXPECT_NO_THROW({
        FirebirdException ex(fbEx);
        // what() должно содержать хотя бы часть сообщения
        EXPECT_NE(std::string(ex.what()).length(), 0u);
        // Но не должно превышать разумные пределы (скажем, 5000 символов)
        EXPECT_LE(std::string(ex.what()).length(), 5000u);
    });
    
    status->dispose();
}

// ============== Интеграционные тесты ==============

class FirebirdExceptionIntegrationTest : public fbpp::test::TempDatabaseTest {
};

TEST_F(FirebirdExceptionIntegrationTest, InvalidSQLThrowsOurException) {
    // Проверяем что неверный SQL кидает именно наш FirebirdException
    EXPECT_THROW({
        auto tra = connection_->Execute("THIS IS NOT VALID SQL");
    }, FirebirdException);
    
    // Соединение должно остаться активным
    EXPECT_TRUE(connection_->isConnected());
    
    // Следующий валидный запрос должен работать
    EXPECT_NO_THROW({
        auto tra = connection_->Execute("SELECT 1 FROM RDB$DATABASE");
        tra->Commit();
    });
}

TEST_F(FirebirdExceptionIntegrationTest, DetailedErrorInfo) {
    // Проверяем детальную информацию об ошибке
    try {
        // Пытаемся выбрать из несуществующей таблицы
        auto tra = connection_->Execute("SELECT * FROM NON_EXISTENT_TABLE");
        FAIL() << "Should have thrown an exception";
    }
    catch (const FirebirdException& e) {
        // Проверяем что есть осмысленное сообщение
        EXPECT_NE(std::string(e.what()).length(), 0u);
        
        // Должен быть error code
        EXPECT_NE(e.getErrorCode(), 0);
        
        // SQL State должен быть установлен
        EXPECT_FALSE(e.getSQLState().empty());
        
        // Сообщения об ошибках должны быть
        EXPECT_FALSE(e.getErrorMessages().empty());
        
        // Сообщение должно содержать имя таблицы
        std::string whatStr = e.what();
        EXPECT_NE(whatStr.find("NON_EXISTENT_TABLE"), std::string::npos);

        // Канонический SQLSTATE движка (fb_sqlstate) для "Table unknown" —
        // 42S02, как и показывает isql. Раньше возвращался HY000 из карты.
        EXPECT_EQ(e.getSQLState(), "42S02");
    }
}

TEST_F(FirebirdExceptionIntegrationTest, EngineSQLStateNotMaskedByFallbackMap) {
    // Ошибка конверсии (isc_convert_error) отсутствует в fallback-карте:
    // раньше getSQLState() возвращал HY000, теперь — 22018 из status vector.
    try {
        auto tra = connection_->Execute(
            "EXECUTE BLOCK AS DECLARE i INTEGER; "
            "BEGIN i = CAST('abc' AS INTEGER); END");
        FAIL() << "Should have thrown an exception";
    }
    catch (const FirebirdException& e) {
        EXPECT_EQ(e.getSQLState(), "22018");
    }
}

TEST_F(FirebirdExceptionIntegrationTest, DivisionByZeroSQLState) {
    try {
        auto tra = connection_->Execute(
            "EXECUTE BLOCK AS DECLARE i INTEGER; BEGIN i = 1 / 0; END");
        FAIL() << "Should have thrown an exception";
    }
    catch (const FirebirdException& e) {
        EXPECT_EQ(e.getSQLState(), "22012");
    }
}

TEST_F(FirebirdExceptionIntegrationTest, TransactionErrors) {
    // Тестируем ошибки транзакций
    auto tra = connection_->StartTransaction();
    
    // Коммитим транзакцию
    tra->Commit();
    
    // Пытаемся использовать завершенную транзакцию
    EXPECT_THROW({
        connection_->ExecuteInTransaction(tra.get(), "SELECT 1 FROM RDB$DATABASE");
    }, FirebirdException);
}

// ============== Проверка утечек памяти ==============
// Эти тесты лучше запускать с AddressSanitizer или Valgrind

TEST_F(FirebirdExceptionTest, NoMemoryLeaks) {
    // Множественное создание и уничтожение исключений
    for (int i = 0; i < 100; ++i) {
        auto& env = Environment::getInstance();
        Firebird::IStatus* status = env.getMaster()->getStatus();
        
        intptr_t errorVector[] = {
            isc_arg_gds,
            335544321 + i,  // Разные коды ошибок
            isc_arg_string,
            reinterpret_cast<intptr_t>("Test error"),
            isc_arg_end
        };
        
        status->setErrors(errorVector);
        Firebird::FbException fbEx(status);
        
        // Создаем и уничтожаем исключение
        {
            FirebirdException ex(fbEx);
            // Используем исключение
            volatile auto msg = ex.what();
            volatile auto code = ex.getErrorCode();
            (void)msg;
            (void)code;
        }
        
        status->dispose();
    }
    
    // Если запущено с ASAN/Valgrind, утечки будут обнаружены
}

// ============== SQL State mapping ==============

TEST_F(FirebirdExceptionTest, SQLStateMapping) {
    struct TestCase {
        int error_code;
        const char* expected_state;
        const char* description;
    };
    
    // Ожидания — канонические значения fb_sqlstate() клиентской библиотеки.
    // Голый isc_arith_except (без уточняющего кода деления на ноль) — 22000;
    // реальный вектор деления на ноль содержит второй gds-код и даёт 22012
    // (см. интеграционный тест ниже).
    TestCase testCases[] = {
        {335544321, "22000", "Arithmetic exception (bare)"},
        {335544347, "23000", "Integrity constraint violation"},
        {335544665, "23000", "Unique key violation"},
        {335544336, "40001", "Deadlock"},
        {335544345, "40001", "Lock conflict"},
        {123456789, "HY000", "Unknown error - default state"}
    };
    
    auto& env = Environment::getInstance();
    
    for (const auto& tc : testCases) {
        Firebird::IStatus* status = env.getMaster()->getStatus();
        
        intptr_t errorVector[] = {
            isc_arg_gds,
            tc.error_code,
            isc_arg_end
        };
        
        status->setErrors(errorVector);
        Firebird::FbException fbEx(status);
        
        FirebirdException ex(fbEx);
        
        EXPECT_EQ(ex.getSQLState(), tc.expected_state) 
            << "Failed for " << tc.description 
            << " (code " << tc.error_code << ")";
        
        status->dispose();
    }
}

// Движок присылает точный SQLSTATE через isc_arg_sql_state (после isc_arg_gds);
// он должен иметь приоритет над захардкоженной картой кодов.
TEST_F(FirebirdExceptionTest, EngineSQLStateWinsOverMappedState) {
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();

    intptr_t errorVector[] = {
        isc_arg_gds,
        335544347,  // присутствует в карте (дал бы 23000)
        isc_arg_sql_state,
        reinterpret_cast<intptr_t>("42S02"),
        isc_arg_end
    };

    status->setErrors(errorVector);
    Firebird::FbException fbEx(status);
    FirebirdException ex(fbEx);

    EXPECT_EQ(ex.getSQLState(), "42S02");
    EXPECT_EQ(ex.getErrorCode(), 335544347);

    status->dispose();
}

TEST_F(FirebirdExceptionTest, StatusVectorSnapshotSurvivesStatusLifetime) {
    auto& env = Environment::getInstance();
    Firebird::IStatus* status = env.getMaster()->getStatus();

    intptr_t errorVector[] = {
        isc_arg_gds,
        335544580,
        isc_arg_string,
        reinterpret_cast<intptr_t>("TEST_TABLE"),
        isc_arg_end
    };

    status->setErrors(errorVector);
    Firebird::FbException fbEx(status);
    FirebirdException ex(fbEx);

    status->dispose();

    const auto& snapshot = ex.getStatusVector();
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].numericValue, 335544580);
    EXPECT_EQ(snapshot[1].textValue, "TEST_TABLE");
}
