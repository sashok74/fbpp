#!/usr/bin/env bash
# Environment setup script for local fbpp development
#
# Usage:
#   source scripts/setup_local_env.sh
#   ./build.sh
#
# This script sets environment variables that are used by:
# 1. CMake build (query_generator tool for code generation)
# 2. Test runtime (database connections)
#
# The same variables work for both build and test phases, ensuring consistency.
#
# NOTE: This script is OPTIONAL for local development.
# Without it, config/test_config.json defaults will be used.
# Use this script to override config values (e.g., for testing CI/CD locally).

# Firebird server configuration
export FIREBIRD_HOST=firebird5.home.lan
export FIREBIRD_PORT=3050
export FIREBIRD_USER=SYSDBA
export FIREBIRD_PASSWORD=planomer
export FIREBIRD_CHARSET=UTF8

# Database paths for tests
# Temp database: created and dropped for each test
export FIREBIRD_DB_PATH=/mnt/test/fbpp_temp_test.fdb

# Persistent database: used by query_generator and persistent tests
# This database contains TABLE_TEST_1 with full schema of all Firebird types
# For local development: short name "testdb" (resolves to /mnt/test/testdb on server)
export FIREBIRD_PERSISTENT_DB_PATH=testdb

echo "========================================="
echo "fbpp Local Development Environment"
echo "========================================="
echo "FIREBIRD_HOST:                 $FIREBIRD_HOST"
echo "FIREBIRD_PORT:                 $FIREBIRD_PORT"
echo "FIREBIRD_USER:                 $FIREBIRD_USER"
echo "FIREBIRD_PERSISTENT_DB_PATH:   $FIREBIRD_PERSISTENT_DB_PATH"
echo "FIREBIRD_DB_PATH:              $FIREBIRD_DB_PATH"
echo ""
echo "Environment configured successfully!"
echo "You can now run: ./build.sh"
echo "========================================="
