#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

using namespace fbpp::test;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) {
        throw std::runtime_error("Failed to open file: " + p.string());
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int runProcess(const std::filesystem::path& executable,
               const std::vector<std::string>& args) {
#ifdef _WIN32
    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    storage.push_back(executable.string());
    storage.insert(storage.end(), args.begin(), args.end());

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);

    return _spawnv(_P_WAIT, executable.string().c_str(), argv.data());
#else
    auto quoteArg = [](const std::string& value) {
        std::string quoted = "\"";
        for (char ch : value) {
            if (ch == '"' || ch == '\\') {
                quoted.push_back('\\');
            }
            quoted.push_back(ch);
        }
        quoted.push_back('"');
        return quoted;
    };

    std::string command = quoteArg(executable.string());
    for (const auto& arg : args) {
        command.push_back(' ');
        command += quoteArg(arg);
    }

    return std::system(command.c_str());
#endif
}

} // namespace

class QueryGeneratorTest : public SuiteDatabaseTest {
protected:
    std::vector<SchemaProfile> schemaProfiles() const override {
        return {SchemaProfile::TableTest1};
    }
};

TEST_F(QueryGeneratorTest, GeneratesHeadersForSelect) {
    namespace fs = std::filesystem;

    fs::path tempDir = fs::temp_directory_path() / "fbpp_query_gen_test";
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    fs::path inputJson = tempDir / "queries.json";
    std::ofstream inputFile(inputJson);
    inputFile << R"({
        "SelectAll": "SELECT ID, F_BIGINT, F_BOOLEAN, F_CHAR, F_DATE, F_DECFLOAT, F_DECIMAL, F_DOUBLE_PRECISION, F_FLOAT, F_INT128, F_INTEGER, F_NUMERIC, F_SMALINT, F_TIME, F_TIME_TZ, F_TIMESHTAMP, F_TIMESHTAMP_TZ, F_VARCHAR, F_BLOB_B, F_BLOB_T FROM TABLE_TEST_1 WHERE ID = :id"
    })";
    inputFile.close();

    fs::path outputHeader = tempDir / "queries.generated.hpp";
    fs::path supportHeader = tempDir / "queries.structs.generated.hpp";

    fs::path generatorExe = fs::path(QUERY_GENERATOR_EXE);
    ASSERT_TRUE(fs::exists(generatorExe)) << "query_generator executable not found";

    // Use the managed suite database prepared by the fixture.
    std::string dsn = db_params_.database;

    const auto rc = runProcess(generatorExe, {
        "--dsn", dsn,
        "--user", db_params_.user,
        "--password", db_params_.password,
        "--charset", db_params_.charset,
        "--input", inputJson.string(),
        "--output", outputHeader.string(),
        "--support", supportHeader.string()
    });
    ASSERT_EQ(rc, 0) << "Generator failed with exit code " << rc;

    ASSERT_TRUE(fs::exists(outputHeader));
    ASSERT_TRUE(fs::exists(supportHeader));

    auto mainContents = slurp(outputHeader);
    EXPECT_NE(mainContents.find("enum class QueryId"), std::string::npos);
    EXPECT_NE(mainContents.find("    None"), std::string::npos);
    EXPECT_NE(mainContents.find("#include \"queries.structs.generated.hpp\""), std::string::npos);
    EXPECT_NE(mainContents.find("struct SelectAllIn"), std::string::npos);
    EXPECT_NE(mainContents.find("struct SelectAllOut"), std::string::npos);
    EXPECT_NE(mainContents.find("QueryDescriptor<QueryId::SelectAll>"), std::string::npos);
    EXPECT_NE(mainContents.find("static constexpr std::string_view name = \"SelectAll\""), std::string::npos);
    EXPECT_NE(mainContents.find("static constexpr std::string_view sql = \"SELECT ID"), std::string::npos);
    EXPECT_NE(mainContents.find("static constexpr std::string_view positionalSql"), std::string::npos);
    EXPECT_NE(mainContents.find("static constexpr bool hasNamedParameters"), std::string::npos);
    EXPECT_NE(mainContents.find("std::optional<bool>"), std::string::npos);
    EXPECT_NE(mainContents.find("fbpp::core::Date"), std::string::npos);
    EXPECT_NE(mainContents.find("fbpp::core::DecFloat34"), std::string::npos);
    EXPECT_NE(mainContents.find("fbpp::core::TimeTz"), std::string::npos);
    EXPECT_NE(mainContents.find("fbpp::core::TimestampTz"), std::string::npos);
    EXPECT_NE(mainContents.find("fbpp::core::Blob"), std::string::npos);

    auto supportContents = slurp(supportHeader);
    EXPECT_NE(supportContents.find("StructDescriptor<generated::queries::SelectAllIn>"), std::string::npos);
    EXPECT_NE(supportContents.find("StructDescriptor<generated::queries::SelectAllOut>"), std::string::npos);

    fs::remove_all(tempDir);
}
