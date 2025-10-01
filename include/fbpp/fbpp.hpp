#pragma once

// Version information
#define FBPP_VERSION_MAJOR 1
#define FBPP_VERSION_MINOR 0
#define FBPP_VERSION_PATCH 0

// Core functionality - always needed
#include "fbpp/core/environment.hpp"
#include "fbpp/core/connection.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/statement.hpp"
#include "fbpp/core/result_set.hpp"
#include "fbpp/core/exception.hpp"

// Template implementations (must be after statement.hpp)
#include "fbpp/core/transaction_impl.hpp"

// Convenience namespace
namespace fbpp {
    using namespace fbpp::core;
}