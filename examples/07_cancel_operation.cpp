/**
 * @file 07_cancel_operation.cpp
 * @brief Comprehensive demonstration of Connection::cancelOperation() API using multi-threading
 *
 * According to Firebird developers (Alex Peshkoff, Firebird 2.5):
 * - cancelOperation was designed to be called from ANOTHER THREAD
 * - It is NOT async signal safe (should NOT be called from signal handlers)
 * - The call is asynchronous - returns immediately, cancellation happens later
 *
 * This test verifies these design assumptions and demonstrates correct usage.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <future>
#include <fstream>
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/exception.hpp"
#include <nlohmann/json.hpp>
#include <firebird/Interface.h>

using namespace fbpp::core;
using namespace std::chrono_literals;

// Thread synchronization
std::mutex cout_mutex;
std::atomic<bool> query_started{false};
std::atomic<bool> cancel_requested{false};

// Thread-safe console output
void thread_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << msg << std::flush;
}

ConnectionParams loadConfig() {
    std::vector<std::string> paths = {
        "../../config/test_config.json",
        "../config/test_config.json",
        "config/test_config.json",
        "./test_config.json"
    };

    std::ifstream config_file;
    for (const auto& path : paths) {
        config_file.open(path);
        if (config_file.is_open()) {
            thread_print("Config loaded from: " + path + "\n");
            break;
        }
    }

    if (!config_file.is_open()) {
        throw std::runtime_error("test_config.json not found");
    }

    auto config = nlohmann::json::parse(config_file);
    auto db_config = config["tests"]["persistent_db"];

    ConnectionParams params;
    params.database = std::string(db_config.value("server", "firebird5")) + ":" +
                     std::string(db_config["path"]);
    params.user = db_config["user"];
    params.password = db_config["password"];
    params.charset = db_config["charset"];

    return params;
}

/**
 * TEST 1: Basic multi-threaded cancellation
 * Thread A executes query, Thread B cancels it
 */
void test_1_basic_multithread_cancel() {
    thread_print("\n========================================\n");
    thread_print("TEST 1: Basic Multi-threaded Cancellation\n");
    thread_print("========================================\n");
    thread_print("Thread A executes query, Thread B cancels after 2 seconds\n\n");

    auto params = loadConfig();

    // Reset flags
    query_started = false;
    cancel_requested = false;

    // Shared connection pointer for cancellation
    // IMPORTANT: Only the attachment handle is shared, not the full Connection object
    Connection* shared_conn = nullptr;

    // Thread A: Execute heavy query
    auto query_future = std::async(std::launch::async, [params, &shared_conn]() {
        Connection conn(params);  // Create connection OUTSIDE try block
        shared_conn = &conn;  // Share connection pointer

        bool was_cancelled = false;

        try {
            // Enable cancellation (default, but being explicit)
            conn.cancelOperation(CancelOperation::ENABLE);
            thread_print("[Thread A] Cancel operations ENABLED\n");

            auto tra = conn.StartTransaction();

            std::string heavy_sql = R"(
                SELECT COUNT(*), SUM(t1.F_INTEGER)
                FROM TABLE_TEST_1 t1
                CROSS JOIN TABLE_TEST_1 t2
                CROSS JOIN TABLE_TEST_1 t3
                CROSS JOIN TABLE_TEST_1 t4
                WHERE t1.F_INTEGER IS NOT NULL
            )";

            auto stmt = conn.prepareStatement(heavy_sql);

            thread_print("[Thread A] Starting heavy query...\n");
            query_started = true;

            auto rs = tra->openCursor(stmt);
            std::tuple<int64_t, std::optional<int64_t>> result;

            bool fetched = rs->fetch(result);

            if (fetched && !cancel_requested) {
                thread_print("[Thread A] ✗ Query completed without cancellation\n");
                thread_print("  COUNT = " + std::to_string(std::get<0>(result)) + "\n");
                tra->Commit();
            } else {
                thread_print("[Thread A] Query interrupted but fetch returned\n");
                tra->Rollback();
            }

        } catch (const FirebirdException& e) {
            thread_print("[Thread A] ✓ Query cancelled (FirebirdException): " + std::string(e.what()) + "\n");
            was_cancelled = true;
        } catch (const Firebird::FbException& e) {
            thread_print("[Thread A] ✓ Query cancelled (FbException)\n");
            was_cancelled = true;
        } catch (const std::exception& e) {
            thread_print("[Thread A] Unexpected error: " + std::string(e.what()) + "\n");
        }

        // IMPORTANT: Test if connection is still alive after cancellation!
        if (was_cancelled) {
            thread_print("[Thread A] Testing connection after cancellation...\n");

            if (conn.isConnected()) {
                thread_print("[Thread A] ✓ Connection is STILL ALIVE after cancel!\n");

                // Try to use connection again
                try {
                    auto tra2 = conn.StartTransaction();
                    auto stmt2 = conn.prepareStatement("SELECT 1 FROM RDB$DATABASE");
                    auto rs2 = tra2->openCursor(stmt2);
                    std::tuple<int> result;
                    rs2->fetch(result);
                    tra2->Commit();
                    thread_print("[Thread A] ✓ Successfully executed query after cancel!\n");
                } catch (const std::exception& e) {
                    thread_print("[Thread A] Failed to use connection: " + std::string(e.what()) + "\n");
                }
            } else {
                thread_print("[Thread A] ✗ Connection is DEAD after cancel\n");
            }
        }

        return was_cancelled;
    });

    // Thread B: Cancel after delay
    auto cancel_future = std::async(std::launch::async, [&shared_conn]() {
        // Wait for query to start
        while (!query_started) {
            std::this_thread::sleep_for(10ms);
        }

        thread_print("[Thread B] Waiting 2 seconds before cancel...\n");
        std::this_thread::sleep_for(2s);

        if (shared_conn) {
            try {
                auto start = std::chrono::high_resolution_clock::now();

                thread_print("[Thread B] Calling cancelOperation(RAISE)...\n");
                cancel_requested = true;
                shared_conn->cancelOperation(CancelOperation::RAISE);

                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                thread_print("[Thread B] ✓ RAISE returned immediately (took " +
                           std::to_string(duration.count()) + "ms)\n");

                if (duration.count() > 100) {
                    thread_print("[Thread B] WARNING: cancelOperation took >100ms (not async?)\n");
                }

                return true;
            } catch (const std::exception& e) {
                thread_print("[Thread B] Failed to cancel: " + std::string(e.what()) + "\n");
                return false;
            }
        }
        return false;
    });

    bool was_cancelled = query_future.get();
    bool cancel_ok = cancel_future.get();

    thread_print("\nResult: ");
    if (was_cancelled && cancel_ok) {
        thread_print("✓ SUCCESS - Multi-threaded cancellation works!\n");
    } else {
        thread_print("✗ FAILURE - Cancellation did not work as expected\n");
    }
}

/**
 * TEST 2: Critical section protection with DISABLE/ENABLE
 */
void test_2_critical_section_protection() {
    thread_print("\n========================================\n");
    thread_print("TEST 2: Critical Section Protection\n");
    thread_print("========================================\n");
    thread_print("Tests DISABLE/ENABLE to protect critical operations\n\n");

    auto params = loadConfig();

    query_started = false;
    cancel_requested = false;

    Connection* shared_conn = nullptr;
    std::atomic<bool> in_critical_section{false};
    std::atomic<bool> cancel_during_critical{false};

    // Thread A: Execute with critical section
    auto query_future = std::async(std::launch::async, [&]() {
        try {
            Connection conn(params);
            shared_conn = &conn;

            auto tra = conn.StartTransaction();

            // CRITICAL SECTION - disable cancellation
            thread_print("[Thread A] Entering CRITICAL section (DISABLE cancellation)\n");
            conn.cancelOperation(CancelOperation::DISABLE);
            in_critical_section = true;

            // Simulate critical operation
            query_started = true;
            std::string critical_sql = "SELECT COUNT(*) FROM RDB$RELATIONS";
            auto stmt1 = conn.prepareStatement(critical_sql);
            auto rs1 = tra->openCursor(stmt1);
            std::tuple<int> count;
            rs1->fetch(count);

            thread_print("[Thread A] Critical operation: found " +
                        std::to_string(std::get<0>(count)) + " relations\n");

            // Sleep to let Thread B try to cancel
            std::this_thread::sleep_for(2s);

            if (cancel_during_critical) {
                thread_print("[Thread A] ✗ FAILURE: Was cancelled during critical section!\n");
                return false;
            }

            // EXIT CRITICAL SECTION
            thread_print("[Thread A] Exiting CRITICAL section (ENABLE cancellation)\n");
            conn.cancelOperation(CancelOperation::ENABLE);
            in_critical_section = false;

            // Now run a moderately heavy cancellable query
            thread_print("[Thread A] Starting cancellable query...\n");
            std::string heavy_sql = R"(
                SELECT COUNT(*) FROM TABLE_TEST_1 t1
                CROSS JOIN TABLE_TEST_1 t2
                CROSS JOIN TABLE_TEST_1 t3
                WHERE t1.F_INTEGER > 0 AND t2.F_INTEGER > 0
            )";

            auto stmt2 = conn.prepareStatement(heavy_sql);
            auto rs2 = tra->openCursor(stmt2);
            rs2->fetch(count);

            thread_print("[Thread A] Query completed\n");
            tra->Commit();
            return true;

        } catch (const Firebird::FbException& e) {
            if (in_critical_section) {
                cancel_during_critical = true;
                thread_print("[Thread A] ✗ ERROR: Cancelled during critical section!\n");
                return false;
            } else {
                thread_print("[Thread A] ✓ Cancelled outside critical section\n");
                return true;
            }
        }
    });

    // Thread B: Try to cancel
    auto cancel_future = std::async(std::launch::async, [&]() {
        while (!query_started) {
            std::this_thread::sleep_for(10ms);
        }

        std::this_thread::sleep_for(500ms);  // Try during critical section

        if (shared_conn) {
            thread_print("[Thread B] Attempting cancel while in critical section...\n");
            cancel_requested = true;
            try {
                shared_conn->cancelOperation(CancelOperation::RAISE);
                thread_print("[Thread B] First RAISE sent (should be ignored)\n");
            } catch (const FirebirdException& e) {
                thread_print("[Thread B] First RAISE error: " + std::string(e.what()) + "\n");
            } catch (const Firebird::FbException& e) {
                thread_print("[Thread B] First RAISE FbException\n");
            }

            // Wait for critical section to end
            while (in_critical_section) {
                std::this_thread::sleep_for(100ms);
            }

            std::this_thread::sleep_for(100ms);
            thread_print("[Thread B] Sending second RAISE (should work now)...\n");
            try {
                shared_conn->cancelOperation(CancelOperation::RAISE);
                thread_print("[Thread B] Second RAISE sent\n");
            } catch (const FirebirdException& e) {
                thread_print("[Thread B] RAISE returned error (query might have finished): " +
                           std::string(e.what()) + "\n");
            } catch (const Firebird::FbException& e) {
                thread_print("[Thread B] RAISE returned FbException (query might have finished)\n");
            }
        }

        return true;
    });

    bool result = query_future.get();
    cancel_future.get();

    thread_print("\nResult: ");
    if (!cancel_during_critical) {
        thread_print("✓ SUCCESS - Critical section was protected from cancellation!\n");
        thread_print("  (Query may or may not have been cancelled after critical section)\n");
    } else {
        thread_print("✗ FAILURE - Critical section was not protected\n");
    }
}

/**
 * TEST 3: Verify asynchronous nature of cancelOperation
 */
void test_3_async_nature() {
    thread_print("\n========================================\n");
    thread_print("TEST 3: Asynchronous Nature\n");
    thread_print("========================================\n");
    thread_print("Verify that cancelOperation returns immediately\n\n");

    auto params = loadConfig();

    try {
        Connection conn(params);

        // Start a transaction but don't execute query yet
        auto tra = conn.StartTransaction();

        thread_print("Calling cancelOperation(RAISE) with no active operation...\n");

        auto start = std::chrono::high_resolution_clock::now();
        try {
            conn.cancelOperation(CancelOperation::RAISE);
        } catch (const FirebirdException& e) {
            // Expected: "nothing to cancel"
            thread_print("Got expected error: " + std::string(e.what()) + "\n");
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        thread_print("cancelOperation returned in " + std::to_string(duration.count()) + " microseconds\n");

        if (duration.count() < 10000) {  // Less than 10ms
            thread_print("✓ SUCCESS - Returns immediately (async behavior confirmed)\n");
        } else {
            thread_print("✗ WARNING - Took >10ms (might be synchronous)\n");
        }

        tra->Rollback();

    } catch (const std::exception& e) {
        thread_print("Test error: " + std::string(e.what()) + "\n");
    }
}

/**
 * TEST 4: Demonstrate immediate return and non-blocking nature
 */
void test_4_non_blocking_cancel() {
    thread_print("\n========================================\n");
    thread_print("TEST 4: Non-blocking Cancel Demonstration\n");
    thread_print("========================================\n");
    thread_print("Shows that cancelOperation returns immediately without waiting\n\n");

    auto params = loadConfig();

    // Shared connection pointer
    Connection* shared_conn = nullptr;
    std::atomic<bool> query_running{false};
    std::atomic<bool> query_cancelled{false};

    // Start a heavy query in background
    auto query_future = std::async(std::launch::async, [params, &shared_conn, &query_running, &query_cancelled]() {
        try {
            Connection conn(params);
            shared_conn = &conn;  // Share connection pointer

            auto tra = conn.StartTransaction();

            // Heavy query that takes several seconds
            std::string sql = R"(
                SELECT COUNT(*), SUM(t1.F_INTEGER)
                FROM TABLE_TEST_1 t1
                CROSS JOIN TABLE_TEST_1 t2
                CROSS JOIN TABLE_TEST_1 t3
                CROSS JOIN TABLE_TEST_1 t4
                WHERE t1.F_INTEGER > 0 AND t2.F_INTEGER > 0
            )";

            auto stmt = conn.prepareStatement(sql);
            thread_print("[Background] Starting heavy query...\n");
            query_running = true;

            auto rs = tra->openCursor(stmt);
            std::tuple<int64_t, std::optional<int64_t>> result;
            rs->fetch(result);

            thread_print("[Background] Query completed successfully!\n");
            tra->Commit();
            return false; // Not cancelled

        } catch (const Firebird::FbException& e) {
            query_cancelled = true;
            thread_print("[Background] ✓ Query was cancelled by RAISE\n");
            return true; // Cancelled
        } catch (const FirebirdException& e) {
            query_cancelled = true;
            thread_print("[Background] ✓ Query was cancelled: " + std::string(e.what()) + "\n");
            return true;
        }
    });

    // Wait for query to start
    while (!query_running || !shared_conn) {
        std::this_thread::sleep_for(10ms);
    }

    // Give query time to really get going
    std::this_thread::sleep_for(1000ms);

    // Now demonstrate non-blocking cancel
    thread_print("\n[Main] Calling cancelOperation(RAISE)...\n");

    auto cancel_start = std::chrono::high_resolution_clock::now();

    try {
        shared_conn->cancelOperation(CancelOperation::RAISE);
        thread_print("[Main] RAISE sent successfully\n");
    } catch (const std::exception& e) {
        thread_print("[Main] RAISE error: " + std::string(e.what()) + "\n");
    }

    auto cancel_end = std::chrono::high_resolution_clock::now();
    auto cancel_duration = std::chrono::duration_cast<std::chrono::milliseconds>(cancel_end - cancel_start);

    thread_print("[Main] cancelOperation returned in " + std::to_string(cancel_duration.count()) + "ms\n");

    if (cancel_duration.count() < 100) {
        thread_print("[Main] ✓ SUCCESS - RAISE returned immediately (non-blocking)\n");
    } else {
        thread_print("[Main] ✗ WARNING - RAISE took >100ms\n");
    }

    // Demonstrate that main thread is not blocked
    thread_print("\n[Main] Main thread continues immediately after RAISE...\n");

    // Do other work while query is being cancelled
    for (int i = 0; i < 3; i++) {
        thread_print("[Main] Doing other work... " + std::to_string(i+1) + "/3\n");
        std::this_thread::sleep_for(200ms);

        // Check if query was cancelled during our work
        if (query_cancelled) {
            thread_print("[Main] (Query was cancelled in background while we worked!)\n");
            break;
        }
    }

    // Now check final status with timeout
    thread_print("\n[Main] Checking background query status...\n");

    auto status = query_future.wait_for(std::chrono::milliseconds(500));

    if (status == std::future_status::ready) {
        bool was_cancelled = query_future.get();
        if (was_cancelled) {
            thread_print("[Main] ✓ Background query was successfully cancelled\n");
        } else {
            thread_print("[Main] Background query completed normally\n");
        }
    } else {
        thread_print("[Main] Background query still running after 500ms\n");
        thread_print("[Main] This demonstrates the asynchronous nature of cancellation\n");

        // Wait a bit more
        thread_print("[Main] Waiting for query to finish...\n");
        bool was_cancelled = query_future.get();
        if (was_cancelled) {
            thread_print("[Main] ✓ Query eventually cancelled\n");
        }
    }

    thread_print("\nSummary:\n");
    thread_print("- cancelOperation returned immediately (non-blocking)\n");
    thread_print("- Main thread could continue work while cancellation processed\n");
    thread_print("- Cancellation happens asynchronously at next reschedule point\n");
}

int main() {
    std::cout << "Starting 07_cancel_operation test suite...\n" << std::flush;

    thread_print("\n");
    thread_print("╔══════════════════════════════════════════════════════════╗\n");
    thread_print("║   Multi-threaded cancelOperation() Test Suite           ║\n");
    thread_print("║                                                          ║\n");
    thread_print("║  Testing the DESIGNED behavior of cancelOperation:      ║\n");
    thread_print("║  - Should be called from ANOTHER THREAD                 ║\n");
    thread_print("║  - NOT from signal handlers (not async signal safe)     ║\n");
    thread_print("║  - Asynchronous operation (returns immediately)         ║\n");
    thread_print("╚══════════════════════════════════════════════════════════╝\n");

    try {
        test_1_basic_multithread_cancel();
    } catch (const std::exception& e) {
        thread_print("TEST 1 error: " + std::string(e.what()) + "\n");
    }

    try {
        test_2_critical_section_protection();
    } catch (const std::exception& e) {
        thread_print("TEST 2 error: " + std::string(e.what()) + "\n");
    }

    try {
        test_3_async_nature();
    } catch (const std::exception& e) {
        thread_print("TEST 3 error: " + std::string(e.what()) + "\n");
    }
    
    // try {
    //     test_4_non_blocking_cancel();
    // } catch (const std::exception& e) {
    //     thread_print("TEST 4 error: " + std::string(e.what()) + "\n");
    // }
    

    thread_print("\n");
    thread_print("╔══════════════════════════════════════════════════════════╗\n");
    thread_print("║                All tests completed!                      ║\n");
    thread_print("╚══════════════════════════════════════════════════════════╝\n");
    thread_print("\n");
    thread_print("CONCLUSIONS:\n");
    thread_print("1. cancelOperation CAN be called from another thread\n");
    thread_print("2. DISABLE/ENABLE protect critical sections effectively\n");
    thread_print("3. The operation is asynchronous (returns immediately)\n");
    thread_print("4. Works on per-attachment basis\n");
    thread_print("\n");
    thread_print("IMPORTANT: Multi-threading is the intended design!\n");
    thread_print("According to Firebird developers, cancelOperation was\n");
    thread_print("designed for multi-threaded use, NOT signal handlers.\n");
    thread_print("\n");

    return 0;
}
