#pragma once

/**
 * @file fbpp.hpp
 * @brief Stable minimal umbrella header for the fbpp runtime layer.
 *
 * Supported public umbrella headers:
 * - <fbpp/fbpp.hpp> for the minimal runtime API
 * - <fbpp/fbpp_extended.hpp> for core + extended Firebird value types
 * - <fbpp/fbpp_all.hpp> for the convenience surface with adapters and helpers
 *
 * Advanced direct includes under <fbpp/core/...> and <fbpp/adapters/...> are
 * supported public headers. Headers under <fbpp/core/detail/...> and
 * implementation helpers such as *_impl.hpp are not stable entry points.
 */

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

// Typed named-parameter binder (must be after statement.hpp)
#include "fbpp/core/param_binder.hpp"
