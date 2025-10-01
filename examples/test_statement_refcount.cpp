// Test program to verify IStatement reference counting
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
            "firebird5:/tmp/test_stmt.fdb",
            dpb->getBufferLength(&statusWrapper),
            dpb->getBuffer(&statusWrapper)
        );

        dpb->dispose();

        if (!attachment) {
            std::cerr << "Failed to connect to database" << std::endl;
            return 1;
        }

        std::cout << "Testing IStatement memory leak with free() but no release()..." << std::endl;
        std::cout << "Monitor memory with: ps aux | grep test_statement" << std::endl;

        // Test 1: Many statements with only free() - potential leak
        std::cout << "\nTest 1: Creating 10000 statements with only free() (no release())" << std::endl;
        std::cout << "This simulates the current fbpp implementation..." << std::endl;

        ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

        for (int i = 0; i < 10000; i++) {
            // Prepare statement
            IStatement* stmt = attachment->prepare(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);

            // Free statement WITHOUT release() - as in current fbpp implementation
            stmt->free(&statusWrapper);
            stmt = nullptr;  // Just null the pointer

            if (i % 1000 == 0) {
                std::cout << "Statements processed: " << i << std::endl;
                // Give time to check memory
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Test 1 complete. Check memory usage." << std::endl;
        std::cout << "Press Enter to continue to Test 2..." << std::endl;
        std::cin.get();

        // Test 2: Many statements with free() AND release() - no leak
        std::cout << "\nTest 2: Creating 10000 statements with free() AND release()" << std::endl;
        std::cout << "This is the proposed fix..." << std::endl;

        for (int i = 0; i < 10000; i++) {
            // Prepare statement
            IStatement* stmt = attachment->prepare(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);

            // Free statement WITH release() - proposed fix
            stmt->free(&statusWrapper);
            stmt->release();  // Explicitly release
            stmt = nullptr;

            if (i % 1000 == 0) {
                std::cout << "Statements processed: " << i << std::endl;
                // Give time to check memory
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Test 2 complete. Check memory usage." << std::endl;
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();

        tra->commit(&statusWrapper);
        tra->release();

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