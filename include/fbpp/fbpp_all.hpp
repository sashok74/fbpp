#pragma once

/**
 * @file fbpp_all.hpp
 * @brief Stable convenience umbrella header for the full core feature set.
 *
 * This header layers the minimal runtime, extended Firebird types, adapters,
 * batch helpers, and higher-level packing utilities on top of each other.
 */

// Include extended functionality
#include "fbpp/fbpp_extended.hpp"

// Type adapters
#include "fbpp/adapters/ttmath_int128.hpp"
#include "fbpp/adapters/ttmath_numeric.hpp"
#include "fbpp/adapters/cppdecimal_decfloat.hpp"
#include "fbpp/adapters/chrono_datetime.hpp"

// Batch operations
#include "fbpp/core/batch.hpp"
#include "fbpp/core/batch_impl.hpp"

// Data packers
#include "fbpp/core/json_packer.hpp"
#include "fbpp/core/json_unpacker.hpp"
#include "fbpp/core/tuple_packer.hpp"
#include "fbpp/core/tuple_unpacker.hpp"

// Additional utilities
#include "fbpp/core/message_builder.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/type_traits.hpp"
#include "fbpp/core/type_adapter.hpp"
