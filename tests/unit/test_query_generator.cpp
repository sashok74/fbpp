#include <gtest/gtest.h>
#include "../test_base.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/exception.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace fbpp::test;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) {
        throw std::runtime_error("Failed to open file: " + p.string());
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

class QueryGeneratorTest : public PersistentDatabaseTest {};

TEST_F(QueryGeneratorTest, GeneratesHeadersForSelect) {
    namespace fs = std::filesystem;

    fs::path tempDir = fs::temp_directory_path() / "fbpp_query_gen_test";
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    fs::path inputJson = tempDir / "queries.json";
    std::ofstream inputFile(inputJson);
    inputFile << R"({
        "SelectById": "SELECT ID, F_INTEGER, F_VARCHAR FROM TABLE_TEST_1 WHERE ID = :id"
    })";
    inputFile.close();

    fs::path outputHeader = tempDir / "queries.generated.hpp";
    fs::path supportHeader = tempDir / "queries.structs.generated.hpp";

    fs::path generatorExe = fs::path(BUILD_DIR) / "query_generator";
    ASSERT_TRUE(fs::exists(generatorExe)) << "query_generator executable not found";

    std::string command = "\"" + generatorExe.string() + "\" " +
        "--dsn firebird5:testdb "
        "--user SYSDBA "
        "--password planomer "
        "--charset UTF8 "
        "--input \"" + inputJson.string() + "\" "
        "--output \"" + outputHeader.string() + "\" "
        "--support \"" + supportHeader.string() + "\"";

    auto rc = std::system(command.c_str());
    ASSERT_EQ(rc, 0) << "Generator failed with exit code " << rc;

    ASSERT_TRUE(fs::exists(outputHeader));
    ASSERT_TRUE(fs::exists(supportHeader));

    auto mainContents = slurp(outputHeader);
    EXPECT_NE(mainContents.find("enum class QueryId"), std::string::npos);
    EXPECT_NE(mainContents.find("struct SelectByIdIn"), std::string::npos);
    EXPECT_NE(mainContents.find("struct SelectByIdOut"), std::string::npos);
    EXPECT_NE(mainContents.find("QueryDescriptor<QueryId::SelectById>"), std::string::npos);

    auto supportContents = slurp(supportHeader);
    EXPECT_NE(supportContents.find("StructDescriptor<generated::queries::SelectByIdIn>"), std::string::npos);
    EXPECT_NE(supportContents.find("StructDescriptor<generated::queries::SelectByIdOut>"), std::string::npos);

    fs::remove_all(tempDir);
}
