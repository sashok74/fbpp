#pragma once

#include "fbpp/core/detail/sql_value_codec.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/transaction.hpp"
#include "fbpp/core/type_adapter.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <tuple>
#include <utility>
#include <optional>

namespace fbpp::core {

// Forward declaration
template<typename T>
struct StructDescriptor;

namespace detail {

// ============================================================================
// Type Traits
// ============================================================================

template<typename T>
inline constexpr bool dependent_false_v = false;

// is_optional trait
template<typename T>
struct is_optional : std::false_type {};

template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

// ============================================================================
// FieldDescriptor Implementation
// ============================================================================

/**
 * @brief Type-safe field descriptor for struct packing
 *
 * @tparam T Struct type
 * @tparam FieldType Type of the field (may be std::optional<U>)
 */
template<typename T, typename FieldType>
struct FieldDescriptor {
    using struct_type = T;
    using field_type = FieldType;
    using member_ptr_type = FieldType T::*;

    const char* sqlName;          // SQL column name
    unsigned sqlType;             // Firebird SQL type (SQL_INT64, SQL_INT128, etc.)
    int16_t scale;                // Scale for NUMERIC types (negative = decimal places)
    unsigned length;              // Length in bytes
    unsigned subType;             // Subtype (for BLOB: 0=binary, 1=text)
    bool nullable;                // Supports NULL values
    bool useAdapter;              // Requires TypeAdapter for conversion
    member_ptr_type memberPtr;    // Pointer to struct member

    // Type-safe field access
    constexpr auto& access(T& obj) const noexcept {
        return obj.*memberPtr;
    }

    constexpr const auto& access(const T& obj) const noexcept {
        return obj.*memberPtr;
    }

    // Check if field type is std::optional
    static constexpr bool is_optional_field = is_optional_v<FieldType>;
};

// ============================================================================
// Field Factory Functions
// ============================================================================

/**
 * @brief Create field descriptor with explicit parameters
 *
 * @tparam T Struct type
 * @tparam FieldType Field type
 * @param memberPtr Pointer to struct member
 * @param sqlName SQL column name
 * @param sqlType Firebird SQL type constant
 * @param scale Scale for NUMERIC (-2 = 2 decimal places)
 * @param length Length in bytes
 * @param subType Subtype (BLOB: 0=binary, 1=text)
 * @param nullable Supports NULL
 * @param useAdapter Requires TypeAdapter
 */
template<typename T, typename FieldType>
constexpr FieldDescriptor<T, FieldType> makeField(
    FieldType T::* memberPtr,
    const char* sqlName,
    unsigned sqlType,
    int16_t scale = 0,
    unsigned length = 0,
    unsigned subType = 0,
    bool nullable = false,
    bool useAdapter = false
) {
    return FieldDescriptor<T, FieldType>{
        sqlName, sqlType, scale, length, subType,
        nullable, useAdapter, memberPtr
    };
}

/**
 * @brief Create field descriptor with auto member pointer deduction
 *
 * Usage: makeField<&MyStruct::field>("FIELD", SQL_INT64)
 */
template<auto MemberPtr>
constexpr auto makeField(
    const char* sqlName,
    unsigned sqlType,
    int16_t scale = 0,
    unsigned length = 0,
    unsigned subType = 0,
    bool nullable = false,
    bool useAdapter = false
) {
    // Extract types from member pointer
    using PtrType = decltype(MemberPtr);

    // Member pointer type is: FieldType StructType::*
    // We need to extract both types
    using StructType = typename std::remove_cv_t<
        typename std::pointer_traits<std::add_pointer_t<PtrType>>::element_type
    >;

    // This is a bit tricky - we need to deduce FieldType from member pointer
    // Use helper to extract it
    using FieldType = std::remove_reference_t<decltype(
        std::declval<StructType&>().*MemberPtr
    )>;

    return FieldDescriptor<StructType, FieldType>{
        sqlName, sqlType, scale, length, subType,
        nullable, useAdapter, MemberPtr
    };
}

// ============================================================================
// Struct Descriptor Traits
// ============================================================================

/**
 * @brief Check if type has StructDescriptor specialization
 */
template<typename T, typename = void>
struct has_struct_descriptor : std::false_type {};

template<typename T>
struct has_struct_descriptor<
    T,
    std::void_t<
        decltype(StructDescriptor<T>::is_specialized),
        decltype(StructDescriptor<T>::fields)
    >
> : std::bool_constant<StructDescriptor<T>::is_specialized> {};

template<typename T>
inline constexpr bool has_struct_descriptor_v = has_struct_descriptor<T>::value;

// ============================================================================
// Pack/Unpack Field Helpers
// ============================================================================

/**
 * @brief Pack single field into Firebird buffer
 */
template<typename T, typename FieldType>
void packField(
    const T& value,
    uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction,
    const FieldDescriptor<T, FieldType>& descriptor,
    std::size_t index
) {
    const FieldInfo& fieldInfo = metadata->getField(static_cast<unsigned>(index));

    // Validate SQL type match
    if (descriptor.sqlType != fieldInfo.type) {
        throw FirebirdException(
            std::string("SQL type mismatch for field '") + descriptor.sqlName +
            "': expected " + std::to_string(descriptor.sqlType) +
            ", got " + std::to_string(fieldInfo.type)
        );
    }

    // Validate scale for NUMERIC types
    if (descriptor.scale != 0 && descriptor.scale != fieldInfo.scale) {
        throw FirebirdException(
            std::string("Scale mismatch for field '") + descriptor.sqlName +
            "': expected " + std::to_string(descriptor.scale) +
            ", got " + std::to_string(fieldInfo.scale)
        );
    }

    // Get pointers
    uint8_t* dataPtr = buffer + fieldInfo.offset;
    int16_t* nullPtr = reinterpret_cast<int16_t*>(buffer + fieldInfo.nullOffset);

    // Get field value
    const auto& fieldValue = descriptor.access(value);

    // Create context
    sql_value_codec::SqlWriteContext ctx{&fieldInfo, transaction, nullPtr};

    // Pack using sql_value_codec
    sql_value_codec::write_sql_value(ctx, fieldValue, dataPtr);
}

/**
 * @brief Unpack single field from Firebird buffer
 */
template<typename T, typename FieldType>
void unpackField(
    T& value,
    const uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction,
    const FieldDescriptor<T, FieldType>& descriptor,
    std::size_t index
) {
    const FieldInfo& fieldInfo = metadata->getField(static_cast<unsigned>(index));

    // Validate SQL type match
    if (descriptor.sqlType != fieldInfo.type) {
        throw FirebirdException(
            std::string("SQL type mismatch for field '") + descriptor.sqlName +
            "': expected " + std::to_string(descriptor.sqlType) +
            ", got " + std::to_string(fieldInfo.type)
        );
    }

    // Get pointers
    const uint8_t* dataPtr = buffer + fieldInfo.offset;
    const int16_t* nullPtr = reinterpret_cast<const int16_t*>(
        buffer + fieldInfo.nullOffset
    );

    // Get field reference
    auto& fieldRef = descriptor.access(value);

    // Create context
    sql_value_codec::SqlReadContext ctx{&fieldInfo, transaction, nullPtr};

    // Unpack using sql_value_codec
    sql_value_codec::read_sql_value(ctx, dataPtr, fieldRef);
}

// ============================================================================
// Pack/Unpack Implementation
// ============================================================================

/**
 * @brief Pack struct into Firebird buffer (implementation)
 */
template<typename T, typename Fields, std::size_t... Indexes>
void packStructImpl(
    const T& value,
    uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction,
    const Fields& fields,
    std::index_sequence<Indexes...>
) {
    // Fold expression - pack each field
    (packField(value, buffer, metadata, transaction,
               std::get<Indexes>(fields), Indexes), ...);
}

/**
 * @brief Unpack struct from Firebird buffer (implementation)
 */
template<typename T, typename Fields, std::size_t... Indexes>
void unpackStructImpl(
    T& value,
    const uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction,
    const Fields& fields,
    std::index_sequence<Indexes...>
) {
    // Fold expression - unpack each field
    (unpackField(value, buffer, metadata, transaction,
                 std::get<Indexes>(fields), Indexes), ...);
}

} // namespace detail

// ============================================================================
// StructDescriptor Base Template
// ============================================================================

/**
 * @brief Base template for struct descriptors (not specialized)
 *
 * Specialize this template for your struct types:
 *
 * @code
 * struct MyStruct {
 *     int64_t id;
 *     std::string name;
 * };
 *
 * template<>
 * struct StructDescriptor<MyStruct> {
 *     static constexpr bool is_specialized = true;
 *     static constexpr const char* name = "MY_STRUCT";
 *
 *     static constexpr auto fields = std::array{
 *         makeField(&MyStruct::id,   "ID",   SQL_INT64,   0, 8),
 *         makeField(&MyStruct::name, "NAME", SQL_VARYING, 0, 256)
 *     };
 *
 *     static constexpr size_t fieldCount = fields.size();
 * };
 * @endcode
 */
template<typename T>
struct StructDescriptor {
    static constexpr bool is_specialized = false;
    // No fields - user must specialize this template
};

// ============================================================================
// Concepts
// ============================================================================

/**
 * @brief Concept for struct-packable types
 *
 * A type is struct-packable if:
 * - It has a StructDescriptor specialization
 * - It's not a tuple or JSON
 */
template<typename T>
concept StructPackable =
    detail::has_struct_descriptor_v<T> &&
    !is_tuple_v<T> &&
    !is_json_v<T>;

/**
 * @brief Helper variable template for struct-packable check
 */
template<typename T>
inline constexpr bool is_struct_packable_v = StructPackable<T>;

// ============================================================================
// Public Pack/Unpack Functions
// ============================================================================

/**
 * @brief Pack struct into Firebird message buffer
 *
 * @tparam T Struct type with StructDescriptor specialization
 * @param value Struct instance to pack
 * @param buffer Output buffer (must be pre-allocated)
 * @param metadata Message metadata describing buffer layout
 * @param transaction Transaction for BLOB operations (optional)
 *
 * @throws FirebirdException on validation errors or type mismatches
 */
template<typename T>
    requires StructPackable<T>
void packStruct(
    const T& value,
    uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction = nullptr
) {
    if (!buffer) {
        throw FirebirdException("NULL buffer in packStruct");
    }
    if (!metadata) {
        throw FirebirdException("NULL metadata in packStruct");
    }

    // Get descriptor
    constexpr auto& fields = StructDescriptor<T>::fields;
    constexpr std::size_t fieldCount = fields.size();

    // Validate field count
    if (fieldCount != metadata->getCount()) {
        throw FirebirdException(
            std::string("Field count mismatch for ") +
            StructDescriptor<T>::name +
            ": expected " + std::to_string(fieldCount) +
            ", got " + std::to_string(metadata->getCount())
        );
    }

    // Zero buffer
    std::memset(buffer, 0, metadata->getMessageLength());

    // Pack all fields
    detail::packStructImpl(
        value, buffer, metadata, transaction,
        fields, std::make_index_sequence<fieldCount>{}
    );
}

/**
 * @brief Unpack struct from Firebird message buffer
 *
 * @tparam T Struct type with StructDescriptor specialization
 * @param buffer Input buffer containing packed data
 * @param metadata Message metadata describing buffer layout
 * @param transaction Transaction for BLOB operations (optional)
 * @return Unpacked struct instance
 *
 * @throws FirebirdException on validation errors or type mismatches
 */
template<typename T>
    requires StructPackable<T>
T unpackStruct(
    const uint8_t* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction = nullptr
) {
    if (!buffer) {
        throw FirebirdException("NULL buffer in unpackStruct");
    }
    if (!metadata) {
        throw FirebirdException("NULL metadata in unpackStruct");
    }

    // Get descriptor
    constexpr auto& fields = StructDescriptor<T>::fields;
    constexpr std::size_t fieldCount = fields.size();

    // Validate field count
    if (fieldCount != metadata->getCount()) {
        throw FirebirdException(
            std::string("Field count mismatch for ") +
            StructDescriptor<T>::name +
            ": expected " + std::to_string(fieldCount) +
            ", got " + std::to_string(metadata->getCount())
        );
    }

    // Create result
    T result{};

    // Unpack all fields
    detail::unpackStructImpl(
        result, buffer, metadata, transaction,
        fields, std::make_index_sequence<fieldCount>{}
    );

    return result;
}

} // namespace fbpp::core
