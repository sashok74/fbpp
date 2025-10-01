// Test program to verify transaction memory leak
#include <firebird/Interface.h>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

using namespace Firebird;

int main() {
    IMaster* master = fb_get_master_interface();
    IStatus* status = master->getStatus();
    ThrowStatusWrapper statusWrapper(status);
    IProvider* provider = master->getDispatcher();
    IUtil* util = master->getUtilInterface();

    try {
        // Build DPB
        IXpbBuilder* dpb = util->getXpbBuilder(&statusWrapper, IXpbBuilder::DPB, nullptr, 0);
        dpb->insertString(&statusWrapper, isc_dpb_user_name, "SYSDBA");
        dpb->insertString(&statusWrapper, isc_dpb_password, "planomer");

        // Connect to database
        IAttachment* attachment = provider->attachDatabase(
            &statusWrapper,
            "/mnt/test/transaction_leak_test.fdb",
            dpb->getBufferLength(&statusWrapper),
            dpb->getBuffer(&statusWrapper)
        );

        dpb->dispose();

        if (!attachment) {
            std::cerr << "Failed to connect to database" << std::endl;
            return 1;
        }

        std::cout << "Starting transaction memory leak test..." << std::endl;
        std::cout << "Watch memory usage with: ps aux | grep 07_cancel" << std::endl;

        // Test 1: Without release() - current implementation
        std::cout << "\nTest 1: WITHOUT release() after commit (current implementation)" << std::endl;
        std::cout << "Creating 1000 transactions..." << std::endl;

        for (int i = 0; i < 1000; i++) {
            ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

            // Do some minimal work
            attachment->execute(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, nullptr, nullptr, nullptr, nullptr);

            // Commit WITHOUT release() - as in current implementation
            tra->commit(&statusWrapper);
            tra = nullptr;  // Just null the pointer

            if (i % 100 == 0) {
                std::cout << "Transactions completed: " << i << std::endl;
                // Give time to check memory
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Test 1 complete. Check memory usage now." << std::endl;
        std::cout << "Press Enter to continue to Test 2..." << std::endl;
        std::cin.get();

        // Test 2: With release() - proposed fix
        std::cout << "\nTest 2: WITH release() after commit (proposed fix)" << std::endl;
        std::cout << "Creating 1000 transactions..." << std::endl;

        for (int i = 0; i < 1000; i++) {
            ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

            // Do some minimal work
            attachment->execute(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, nullptr, nullptr, nullptr, nullptr);

            // Commit WITH release() - proposed fix
            tra->commit(&statusWrapper);
            tra->release();  // Explicitly release
            tra = nullptr;

            if (i % 100 == 0) {
                std::cout << "Transactions completed: " << i << std::endl;
                // Give time to check memory
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Test 2 complete. Check memory usage now." << std::endl;
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();

        // Cleanup
        attachment->detach(&statusWrapper);
        attachment->release();

    } catch (const FbException& e) {
        std::cerr << "Firebird error occurred" << std::endl;

        // Get error message
        char buf[256];
        util->formatStatus(buf, sizeof(buf), status);
        std::cerr << buf << std::endl;
        return 1;
    }

    status->dispose();
    return 0;
}