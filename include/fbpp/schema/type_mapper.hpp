#pragma once

#include "fbpp/schema/adapter_config.hpp"
#include "fbpp/core/message_metadata.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace fbpp::schema {

struct CppTypeInfo {
    struct ScaledNumericInfo {
        int          intWords;
        std::int16_t scale;
    };

    std::string cppType;
    bool needsOptional = false;
    bool needsString = false;
    bool needsExtendedTypes = false;
    bool needsTTMath = false;
    bool needsChrono = false;
    bool needsCppDecimal = false;
    std::optional<ScaledNumericInfo> scaledInfo;
};

class TypeMapper {
public:
    static CppTypeInfo mapField(const fbpp::core::FieldInfo& field,
                                bool isOutput,
                                const AdapterConfig& config = {});
};

} // namespace fbpp::schema
