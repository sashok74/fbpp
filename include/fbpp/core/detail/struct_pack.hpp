#pragma once

#include "fbpp/core/detail/sql_value_codec.hpp"
#include "fbpp/core/exception.hpp"
#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/firebird_compat.hpp"
#include "fbpp/core/transaction.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <tuple>
#include <utility>

namespace fbpp::core {

template<typename T>
struct StructDescriptor; // Forward declaration to be specialized by clients

namespace detail {

template<typename T>
inline constexpr bool dependent_false_v = false;

template<auto MemberPtr>
struct MemberPointerTraits;

template<typename Struct, typename Member, Member Struct::*Ptr>
struct MemberPointerTraits<Ptr> {
    using StructType = Struct;
    using MemberType = Member;
};

template<typename Struct, typename Member>
struct StructFieldDescriptor {
    const char* sqlName;
    unsigned sqlType;
    int16_t scale;
    unsigned length;
    unsigned subType;
    bool nullable;
    Member Struct::* memberPtr;
};

template<typename Struct, typename Member>
constexpr StructFieldDescriptor<Struct, Member> makeFieldDescriptor(
    const char* sqlName,
    Member Struct::* memberPtr,
    unsigned sqlType,
    int16_t scale,
    unsigned length,
    bool nullable,
    unsigned subType) {
    return StructFieldDescriptor<Struct, Member>{
        sqlName, sqlType, scale, length, subType, nullable, memberPtr};
}

template<auto MemberPtr>
constexpr auto makeField(const char* sqlName,
                         unsigned sqlType,
                         int16_t scale,
                         unsigned length,
                         bool nullable,
                         unsigned subType = 0) {
    using Traits = MemberPointerTraits<MemberPtr>;
    using Struct = typename Traits::StructType;
    using Member = typename Traits::MemberType;
    return makeFieldDescriptor<Struct, Member>(
        sqlName, MemberPtr, sqlType, scale, length, nullable, subType);
}

template<typename T, typename = void>
struct has_struct_descriptor : std::false_type {};

template<typename T>
struct has_struct_descriptor<T, std::void_t<decltype(StructDescriptor<T>::fields)>> : std::true_type {};

template<typename T>
inline constexpr bool has_struct_descriptor_v = has_struct_descriptor<T>::value;

template<typename T, typename FieldDescriptor>
void packField(const T& value,
               uint8_t* buffer,
               const MessageMetadata* metadata,
               Transaction* transaction,
               const FieldDescriptor& descriptor,
               std::size_t index) {
    FieldInfo fieldInfo = metadata->getField(static_cast<unsigned>(index));
    uint8_t* dataPtr = buffer + metadata->getOffset(static_cast<unsigned>(index));
    auto* nullPtr = reinterpret_cast<int16_t*>(buffer + metadata->getNullOffset(static_cast<unsigned>(index)));

    const auto& memberValue = value.*(descriptor.memberPtr);
    sql_value_codec::SqlWriteContext ctx{&fieldInfo, transaction, nullPtr};
    sql_value_codec::write_sql_value(ctx, memberValue, dataPtr);
}

template<typename T, typename Fields, std::size_t... Indexes>
void packStructImpl(const T& value,
                    uint8_t* buffer,
                    const MessageMetadata* metadata,
                    Transaction* transaction,
                    const Fields& fields,
                    std::index_sequence<Indexes...>) {
    (packField(value, buffer, metadata, transaction, std::get<Indexes>(fields), Indexes), ...);
}

template<typename T, typename FieldDescriptor>
void unpackField(T& value,
                 const uint8_t* buffer,
                 const MessageMetadata* metadata,
                 Transaction* transaction,
                 const FieldDescriptor& descriptor,
                 std::size_t index) {
    FieldInfo fieldInfo = metadata->getField(static_cast<unsigned>(index));
    const uint8_t* dataPtr = buffer + metadata->getOffset(static_cast<unsigned>(index));
    const auto* nullPtr = reinterpret_cast<const int16_t*>(buffer + metadata->getNullOffset(static_cast<unsigned>(index)));

    auto& memberRef = value.*(descriptor.memberPtr);
    sql_value_codec::SqlReadContext ctx{&fieldInfo, transaction, nullPtr};
    sql_value_codec::read_sql_value(ctx, dataPtr, memberRef);
}

template<typename T, typename Fields, std::size_t... Indexes>
void unpackStructImpl(T& value,
                      const uint8_t* buffer,
                      const MessageMetadata* metadata,
                      Transaction* transaction,
                      const Fields& fields,
                      std::index_sequence<Indexes...>) {
    (unpackField(value, buffer, metadata, transaction, std::get<Indexes>(fields), Indexes), ...);
}

template<typename T>
void packStruct(const T& value,
                uint8_t* buffer,
                const MessageMetadata* metadata,
                Transaction* transaction) {
    static_assert(has_struct_descriptor_v<T>, "StructDescriptor specialization required for type");
    if (!buffer || !metadata) {
        throw FirebirdException("Invalid arguments for packStruct");
    }

    constexpr auto& fields = StructDescriptor<T>::fields;
    using FieldsType = std::decay_t<decltype(fields)>;
    constexpr std::size_t fieldCount = std::tuple_size_v<FieldsType>;

    if (fieldCount != metadata->getCount()) {
        throw FirebirdException("Struct field count mismatch with metadata");
    }

    std::memset(buffer, 0, metadata->getMessageLength());

    packStructImpl(value,
                   buffer,
                   metadata,
                   transaction,
                   fields,
                   std::make_index_sequence<fieldCount>{});
}

template<typename T>
T unpackStruct(const uint8_t* buffer,
               const MessageMetadata* metadata,
               Transaction* transaction) {
    static_assert(has_struct_descriptor_v<T>, "StructDescriptor specialization required for type");
    if (!buffer || !metadata) {
        throw FirebirdException("Invalid arguments for unpackStruct");
    }

    constexpr auto& fields = StructDescriptor<T>::fields;
    using FieldsType = std::decay_t<decltype(fields)>;
    constexpr std::size_t fieldCount = std::tuple_size_v<FieldsType>;

    if (fieldCount != metadata->getCount()) {
        throw FirebirdException("Struct field count mismatch with metadata");
    }

    T result{};

    unpackStructImpl(result,
                     buffer,
                     metadata,
                     transaction,
                     fields,
                     std::make_index_sequence<fieldCount>{});

    return result;
}

} // namespace detail

template<typename T>
inline constexpr bool is_struct_packable_v = detail::has_struct_descriptor_v<T>;

template<typename T>
inline void packStruct(const T& value,
                       uint8_t* buffer,
                       const MessageMetadata* metadata,
                       Transaction* transaction) {
    detail::packStruct(value, buffer, metadata, transaction);
}

template<typename T>
inline T unpackStruct(const uint8_t* buffer,
                      const MessageMetadata* metadata,
                      Transaction* transaction) {
    return detail::unpackStruct<T>(buffer, metadata, transaction);
}

template<auto MemberPtr>
constexpr auto makeField(const char* sqlName,
                         unsigned sqlType,
                         int16_t scale,
                         unsigned length,
                         bool nullable,
                         unsigned subType = 0) {
    return detail::makeField<MemberPtr>(sqlName, sqlType, scale, length, nullable, subType);
}

} // namespace fbpp::core
