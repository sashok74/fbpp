#include "fbpp/core/message_metadata.hpp"
#include "fbpp/core/environment.hpp"
#include "fbpp/core/exception.hpp"
#include <cctype>
#include <cstring>
#include <string_view>

namespace fbpp {
namespace core {

namespace {

// ASCII-only case fold + trim. Identifiers in Firebird metadata are normally
// ASCII; UTF-8 multi-byte case folding would require ICU and is out of scope.
std::string asciiUpperTrim(std::string_view s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out;
    out.reserve(e - b);
    for (size_t i = b; i < e; ++i) {
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

// Same as above but without trim — for stored names that come clean from FB.
std::string asciiUpper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

MessageMetadata::MessageMetadata(Firebird::IMessageMetadata* metadata)
    : env_(Environment::getInstance()),
      metadata_(metadata),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_) {
    if (!metadata_) {
        throw FirebirdException("Invalid metadata pointer");
    }
}

MessageMetadata::MessageMetadata(MessageMetadata&& other) noexcept
    : env_(Environment::getInstance()),
      metadata_(other.metadata_),
      status_(env_.getMaster()->getStatus()),
      statusWrapper_(status_),
      fields_(std::move(other.fields_)),
      name_upper_(std::move(other.name_upper_)),
      alias_upper_(std::move(other.alias_upper_)),
      fieldsLoaded_(other.fieldsLoaded_) {
    other.metadata_ = nullptr;
}

MessageMetadata& MessageMetadata::operator=(MessageMetadata&& other) noexcept {
    if (this != &other) {
        cleanup();
        metadata_ = other.metadata_;
        fields_ = std::move(other.fields_);
        name_upper_ = std::move(other.name_upper_);
        alias_upper_ = std::move(other.alias_upper_);
        fieldsLoaded_ = other.fieldsLoaded_;
        other.metadata_ = nullptr;
    }
    return *this;
}

MessageMetadata::~MessageMetadata() {
    cleanup();
    statusWrapper_.dispose();
}

void MessageMetadata::cleanup() {
    if (metadata_) {
        metadata_->release();
        metadata_ = nullptr;
    }
}

unsigned MessageMetadata::getCount() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getCount(&st);
}

void MessageMetadata::loadFields() const {
    if (fieldsLoaded_ || !metadata_) {
        return;
    }

    auto& st = status();

    unsigned count = getCount();
    fields_.clear();
    fields_.reserve(count);
    name_upper_.clear();
    name_upper_.reserve(count);
    alias_upper_.clear();
    alias_upper_.reserve(count);

    for (unsigned i = 0; i < count; ++i) {
        FieldInfo field;

        // Get field name
        const char* name = metadata_->getField(&st, i);
        field.name = name ? name : "";

        // Get relation (table) name
        const char* relation = metadata_->getRelation(&st, i);
        field.relation = relation ? relation : "";

        // Get owner name
        const char* owner = metadata_->getOwner(&st, i);
        field.owner = owner ? owner : "";

        // Get alias
        const char* alias = metadata_->getAlias(&st, i);
        field.alias = alias ? alias : "";

        // Get type info
        field.type = metadata_->getType(&st, i);
        field.nullable = metadata_->isNullable(&st, i);
        field.subType = metadata_->getSubType(&st, i);
        field.length = metadata_->getLength(&st, i);
        field.scale = metadata_->getScale(&st, i);
        field.charSet = metadata_->getCharSet(&st, i);
        field.offset = metadata_->getOffset(&st, i);
        field.nullOffset = metadata_->getNullOffset(&st, i);

        name_upper_.push_back(asciiUpper(field.name));
        alias_upper_.push_back(asciiUpper(field.alias));
        fields_.push_back(std::move(field));
    }

    fieldsLoaded_ = true;
}

FieldInfo MessageMetadata::getField(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    loadFields();
    
    if (index >= fields_.size()) {
        throw FirebirdException("Field index out of range");
    }
    
    return fields_[index];
}

std::optional<FieldInfo> MessageMetadata::getField(const std::string& name) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }

    loadFields();

    // Pass 1: exact match (preserves quoted-identifier semantics).
    for (const auto& field : fields_) {
        if (field.name == name || field.alias == name) {
            return field;
        }
    }

    // Pass 2: case-insensitive ASCII match with whitespace trim on lookup.
    const std::string lookup = asciiUpperTrim(name);
    if (lookup.empty()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (name_upper_[i] == lookup || alias_upper_[i] == lookup) {
            return fields_[i];
        }
    }

    return std::nullopt;
}

std::vector<FieldInfo> MessageMetadata::getFields() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    loadFields();
    return fields_;
}

std::optional<unsigned> MessageMetadata::getIndex(const std::string& name) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }

    // IMessageMetadata has no by-name lookup — we search manually using the
    // same two-pass semantics as getField(name).
    loadFields();

    // Pass 1: exact match.
    for (unsigned i = 0; i < fields_.size(); ++i) {
        if (fields_[i].name == name || fields_[i].alias == name) {
            return i;
        }
    }

    // Pass 2: case-insensitive ASCII match with whitespace trim on lookup.
    const std::string lookup = asciiUpperTrim(name);
    if (lookup.empty()) {
        return std::nullopt;
    }
    for (unsigned i = 0; i < fields_.size(); ++i) {
        if (name_upper_[i] == lookup || alias_upper_[i] == lookup) {
            return i;
        }
    }

    return std::nullopt;
}

std::string MessageMetadata::getFieldName(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    const char* name = metadata_->getField(&st, index);
    return name ? name : "";
}

unsigned MessageMetadata::getType(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getType(&st, index);
}

unsigned MessageMetadata::getSubType(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getSubType(&st, index);
}

unsigned MessageMetadata::getLength(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getLength(&st, index);
}

int MessageMetadata::getScale(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getScale(&st, index);
}

unsigned MessageMetadata::getCharSet(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getCharSet(&st, index);
}

unsigned MessageMetadata::getOffset(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getOffset(&st, index);
}

unsigned MessageMetadata::getNullOffset(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getNullOffset(&st, index);
}

bool MessageMetadata::isNullable(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->isNullable(&st, index);
}

std::string MessageMetadata::getRelation(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    const char* relation = metadata_->getRelation(&st, index);
    return relation ? relation : "";
}

std::string MessageMetadata::getOwner(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    const char* owner = metadata_->getOwner(&st, index);
    return owner ? owner : "";
}

std::string MessageMetadata::getAlias(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }

    auto& st = status();

    const char* alias = metadata_->getAlias(&st, index);
    return alias ? alias : "";
}

std::string MessageMetadata::getDisplayName(unsigned index) const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }

    loadFields();

    if (index >= fields_.size()) {
        throw FirebirdException("Field index out of range");
    }

    return displayName(fields_[index]);
}

unsigned MessageMetadata::getMessageLength() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getMessageLength(&st);
}

unsigned MessageMetadata::getAlignedLength() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getAlignedLength(&st);
}

unsigned MessageMetadata::getAlignment() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getAlignment(&st);
}

Firebird::IMetadataBuilder* MessageMetadata::getBuilder() const {
    if (!metadata_) {
        throw FirebirdException("Metadata is not initialized");
    }
    
    auto& st = status();
    
    return metadata_->getBuilder(&st);
}

Firebird::IMetadataBuilder* MessageMetadata::createBuilder(unsigned fieldCount) {
    auto& env = Environment::getInstance();
    auto master = env.getMaster();
    
    // For static method, create local status
    Firebird::IStatus* status = master->getStatus();
    Firebird::ThrowStatusWrapper statusWrapper(status);
    statusWrapper.init();
    
    auto result = master->getMetadataBuilder(&statusWrapper, fieldCount);
    
    statusWrapper.dispose();
    return result;
}

} // namespace core
} // namespace fbpp