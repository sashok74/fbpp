// Test program to verify IStatement memory management
#include <firebird/Interface.h>
#include <iostream>
#include <cstring>

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

        std::cout << "Testing IStatement->free() behavior..." << std::endl;

        // Test 1: Only free() without release() - as in official examples
        std::cout << "\nTest 1: free() only (as in official examples)" << std::endl;
        {
            ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

            // Prepare statement
            IStatement* stmt = attachment->prepare(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);

            std::cout << "Statement prepared, ptr: " << stmt << std::endl;

            // Free statement as in official examples
            stmt->free(&statusWrapper);
            std::cout << "Called stmt->free()" << std::endl;
            stmt = nullptr;
            std::cout << "Set stmt to nullptr" << std::endl;

            // Note: NO stmt->release() call here - as in official examples

            tra->commit(&statusWrapper);
            tra->release();
        }

        // Test 2: free() followed by release() - check if this causes problems
        std::cout << "\nTest 2: free() followed by release() (testing for double-free)" << std::endl;
        {
            ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

            // Prepare statement
            IStatement* stmt = attachment->prepare(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);

            std::cout << "Statement prepared, ptr: " << stmt << std::endl;

            // Free statement
            stmt->free(&statusWrapper);
            std::cout << "Called stmt->free()" << std::endl;

            // Try calling release() after free() - this should NOT crash if free() releases
            // the interface, but will crash if we're double-freeing
            try {
                stmt->release();
                std::cout << "Called stmt->release() - no crash, statement NOT released by free()" << std::endl;
            } catch (...) {
                std::cout << "Crash or exception on stmt->release() - statement WAS released by free()" << std::endl;
            }

            stmt = nullptr;

            tra->commit(&statusWrapper);
            tra->release();
        }

        // Test 3: Only release() without free()
        std::cout << "\nTest 3: release() only (without free())" << std::endl;
        {
            ITransaction* tra = attachment->startTransaction(&statusWrapper, 0, nullptr);

            // Prepare statement
            IStatement* stmt = attachment->prepare(&statusWrapper, tra, 0,
                "SELECT 1 FROM RDB$DATABASE",
                SQL_DIALECT_V6, IStatement::PREPARE_PREFETCH_METADATA);

            std::cout << "Statement prepared, ptr: " << stmt << std::endl;

            // Only release without free
            stmt->release();
            std::cout << "Called stmt->release() only" << std::endl;
            stmt = nullptr;

            tra->commit(&statusWrapper);
            tra->release();
        }

        std::cout << "\nAll tests completed successfully" << std::endl;

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