#include <gtest/gtest.h>
#include <firebird/Interface.h>

TEST(FirebirdClientTest, MasterInterfaceAvailable) {
    Firebird::IMaster* master = Firebird::fb_get_master_interface();
    ASSERT_NE(master, nullptr);
}

TEST(FirebirdClientTest, GetDispatcher) {
    Firebird::IMaster* master = Firebird::fb_get_master_interface();
    ASSERT_NE(master, nullptr);
    
    Firebird::IProvider* provider = master->getDispatcher();
    ASSERT_NE(provider, nullptr);
}

TEST(FirebirdClientTest, GetUtilInterface) {
    Firebird::IMaster* master = Firebird::fb_get_master_interface();
    ASSERT_NE(master, nullptr);
    
    Firebird::IUtil* util = master->getUtilInterface();
    ASSERT_NE(util, nullptr);
}

TEST(FirebirdClientTest, GetStatus) {
    Firebird::IMaster* master = Firebird::fb_get_master_interface();
    ASSERT_NE(master, nullptr);
    
    Firebird::IStatus* status = master->getStatus();
    ASSERT_NE(status, nullptr);
    
    status->dispose();
}