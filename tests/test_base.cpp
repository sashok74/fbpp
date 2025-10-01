#include "test_base.hpp"

namespace fbpp {
namespace test {

// Static members initialization
int TempDatabaseTest::test_counter_ = 0;
ConnectionParams PersistentDatabaseTest::db_params_;

} // namespace test
} // namespace fbpp