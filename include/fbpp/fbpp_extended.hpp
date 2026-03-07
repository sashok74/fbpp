#pragma once

/**
 * @file fbpp_extended.hpp
 * @brief Stable umbrella header for core runtime plus extended Firebird types.
 *
 * This header intentionally does not pull adapter headers. Adapter selection is
 * opt-in through direct <fbpp/adapters/...> includes or the convenience header
 * <fbpp/fbpp_all.hpp>.
 */

// Include base functionality
#include "fbpp/fbpp.hpp"

// Extended types support
#include "fbpp/core/extended_types.hpp"
#include "fbpp/core/timestamp_utils.hpp"
