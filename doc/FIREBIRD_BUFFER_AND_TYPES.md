# Firebird Buffer Structure and Type Handling

## 1. Firebird Message Buffer Structure

### 1.1 Buffer Layout

Firebird uses a fixed-layout message buffer for data exchange. Each buffer contains:
- **Data area**: Fixed-offset fields for actual values
- **NULL indicators**: Separate area with 2-byte indicators per nullable field
- **Alignment**: Fields aligned to natural boundaries (2, 4, 8 bytes)

```cpp
// Example buffer layout for SELECT id INTEGER, name VARCHAR(10), price DECIMAL(10,2)
struct MessageBuffer {
    // Data area
    int32_t    id;           // offset 0, 4 bytes
    int16_t    name_length;  // offset 4, 2 bytes (VARCHAR length prefix)
    char       name_data[10]; // offset 6, 10 bytes
    int64_t    price_raw;    // offset 16, 8 bytes (scaled integer)
    
    // NULL indicators area (after data)
    int16_t    id_null;      // offset 24, ISC_NULL (-1) or ISC_NOTNULL (0)
    int16_t    name_null;    // offset 26
    int16_t    price_null;   // offset 28
    // Total: 30 bytes
};
```

### 1.2 Data Reading/Writing Patterns

#### Basic Types (INTEGER, BIGINT, FLOAT, DOUBLE)
```cpp
// Reading
int32_t value = *reinterpret_cast<const int32_t*>(buffer + field.dataOffset);

// Writing  
*reinterpret_cast<int32_t*>(buffer + field.dataOffset) = value;

// NULL handling
bool is_null = (*reinterpret_cast<const int16_t*>(buffer + field.nullOffset) == ISC_NULL);
```

#### VARCHAR/CHAR
```cpp
// VARCHAR reading
uint16_t length = *reinterpret_cast<const uint16_t*>(buffer + field.dataOffset);
const char* data = reinterpret_cast<const char*>(buffer + field.dataOffset + 2);
std::string str(data, length);

// CHAR reading (space-padded)
const char* data = reinterpret_cast<const char*>(buffer + field.dataOffset);
std::string str(data, field.length);
// Trim trailing spaces for CHAR
str.erase(str.find_last_not_of(' ') + 1);
```

#### NUMERIC/DECIMAL (Scaled Integers)
```cpp
// NUMERIC(18,2) stored as INT64 with scale -2
int64_t raw_value = *reinterpret_cast<const int64_t*>(buffer + field.dataOffset);
double actual = raw_value / std::pow(10, -field.scale); // scale is negative!

// NUMERIC(38,x) stored as INT128
unsigned char int128_bytes[16];
memcpy(int128_bytes, buffer + field.dataOffset, 16);
// Convert bytes to INT128 (see extended types section)
```

### 1.3 Important Nuances

1. **Endianness**: Firebird stores in little-endian, may need conversion on big-endian systems
2. **VARCHAR length**: Always 2-byte prefix, even for VARCHAR(1)
3. **CHAR padding**: Always space-padded to full length
4. **Scale sign**: NUMERIC/DECIMAL use negative scale (scale -2 = 2 decimal places)
5. **NULL indicators**: -1 for NULL, 0 for NOT NULL (2 bytes each)
6. **Alignment gaps**: Compiler may insert padding for alignment

## 2. Extended Type Functions (IUtil Interface)

### 2.1 INT128 Functions

| Function | Purpose | Signature |
|----------|---------|-----------|
| `getInt128` | Get IInt128 interface | `IInt128* getInt128(IStatus* status)` |
| `IInt128::set` | Set from string | `void set(IStatus* status, const char* str, int scale)` |
| `IInt128::get` | Get as string | `void get(IStatus* status, char* buffer, int bufferLength, int* scale)` |
| `IInt128::toInt64` | Convert to INT64 | `FB_I64 toInt64(IStatus* status, int* scale)` |
| `IInt128::fromInt64` | Set from INT64 | `void fromInt64(IStatus* status, FB_I64 value, int scale)` |
| `IInt128::compare` | Compare values | `int compare(IStatus* status, IInt128* other)` |
| `IInt128::sign` | Get sign | `int sign(IStatus* status)` |
| `IInt128::negate` | Negate value | `void negate(IStatus* status)` |
| `IInt128::precision` | Get precision | `int precision(IStatus* status)` |

### 2.2 DECFLOAT Functions

| Function | Purpose | Signature |
|----------|---------|-----------|
| `getDecFloat16` | Get IDecFloat16 interface | `IDecFloat16* getDecFloat16(IStatus* status)` |
| `getDecFloat34` | Get IDecFloat34 interface | `IDecFloat34* getDecFloat34(IStatus* status)` |
| `IDecFloat16::fromString` | Parse from string | `void fromString(IStatus* status, const char* str)` |
| `IDecFloat16::toString` | Convert to string | `void toString(IStatus* status, char* buffer, int bufferLength)` |
| `IDecFloat16::fromInt64` | Set from INT64 | `void fromInt64(IStatus* status, FB_I64 value)` |
| `IDecFloat16::toInt64` | Convert to INT64 | `FB_I64 toInt64(IStatus* status, int round)` |
| `IDecFloat16::fromDouble` | Set from double | `void fromDouble(IStatus* status, double value)` |
| `IDecFloat16::toDouble` | Convert to double | `double toDouble(IStatus* status)` |
| `IDecFloat16::compare` | Compare values | `int compare(IStatus* status, IDecFloat16* other)` |
| `IDecFloat16::isInfinity` | Check infinity | `FB_BOOLEAN isInfinity()` |
| `IDecFloat16::isNan` | Check NaN | `FB_BOOLEAN isNan()` |
| `IDecFloat16::sign` | Get sign | `int sign()` |

### 2.3 Date/Time with Timezone Functions

| Function | Purpose | Signature |
|----------|---------|-----------|
| `decodeTime` | Decode TIME to components | `void decodeTime(unsigned timeTz, unsigned* h, unsigned* m, unsigned* s, unsigned* f)` |
| `encodeTime` | Encode components to TIME | `unsigned encodeTime(unsigned h, unsigned m, unsigned s, unsigned f)` |
| `decodeTimeStamp` | Decode TIMESTAMP | `void decodeTimeStamp(const ISC_TIMESTAMP_TZ* ts, unsigned* y, unsigned* m, unsigned* d, unsigned* h, unsigned* min, unsigned* s, unsigned* f)` |
| `encodeTimeStamp` | Encode TIMESTAMP | `void encodeTimeStamp(ISC_TIMESTAMP_TZ* ts, unsigned y, unsigned m, unsigned d, unsigned h, unsigned min, unsigned s, unsigned f)` |
| `getTimeZoneName` | Get timezone name | `void getTimeZoneName(unsigned id, char* buffer, unsigned bufferLength)` |
| `decodeTimeTz` | Decode TIME WITH TZ | `void decodeTimeTz(IStatus* status, const ISC_TIME_TZ* timeTz, unsigned* h, unsigned* m, unsigned* s, unsigned* f, unsigned* tzId, const char** tzName)` |
| `decodeTimestampTz` | Decode TIMESTAMP WITH TZ | `void decodeTimestampTz(IStatus* status, const ISC_TIMESTAMP_TZ* tsTz, unsigned* y, unsigned* mo, unsigned* d, unsigned* h, unsigned* min, unsigned* s, unsigned* f, unsigned* tzId, const char** tzName)` |

## 3. Data Types for Containers

### 3.1 Standard Types Mapping

| Firebird Type | C++ Type for Containers | fbpp::types Structure |
|---------------|-------------------------|----------------------|
| SMALLINT | `int16_t` | - |
| INTEGER | `int32_t` | - |
| BIGINT | `int64_t` | - |
| FLOAT | `float` | - |
| DOUBLE | `double` | - |
| BOOLEAN | `bool` | - |
| DATE | `int32_t` | `fbpp::Date` |
| TIME | `uint32_t` | `fbpp::Time` |
| TIMESTAMP | `int64_t` | `fbpp::Timestamp` |
| VARCHAR/CHAR | `std::string` | - |
| BLOB | `std::vector<uint8_t>` | `fbpp::BlobId` |

### 3.2 Working with Extended Types

For extended Firebird types, the library works directly with native Firebird structures internally and provides conversion through IUtil interface:

| Firebird Type | Native Structure | Size | Conversion via IUtil |
|---------------|-----------------|------|---------------------|
| INT128 | `FB_I128` | 16 bytes | `IInt128::toString()`, `fromString()` |
| NUMERIC(38,x) | `FB_I128` + scale | 16 bytes | `IInt128` with scale parameter |
| DECFLOAT(16) | `FB_DEC16` | 8 bytes | `IDecFloat16::toString()`, `fromString()` |
| DECFLOAT(34) | `FB_DEC34` | 16 bytes | `IDecFloat34::toString()`, `fromString()` |
| TIME WITH TZ | `ISC_TIME_TZ` | 6 bytes | `IUtil::decodeTimeTz()` |
| TIMESTAMP WITH TZ | `ISC_TIMESTAMP_TZ` | 10 bytes | `IUtil::decodeTimestampTz()` |

The library internally uses direct pointer casting to these native structures for zero-copy access, then converts to target format (JSON, user types) using Firebird's IUtil interface.

## 4. FieldExtractor API

### 4.1 String Extraction (for JSON)

```cpp
class FieldExtractor {
public:
    // Extract field as string (for JSON serialization)
    static std::string extractAsString(
        const uint8_t* buffer,
        const FieldDesc& field,
        IUtil* util = nullptr  // Required for extended types
    );
    
    // Extract with NULL handling
    static std::optional<std::string> extractAsStringOpt(
        const uint8_t* buffer,
        const FieldDesc& field,
        IUtil* util = nullptr
    );
};
```

### 4.2 Internal Data Extraction

```cpp
// Internal extraction working directly with Firebird native types
class InternalExtractor {
public:
    // Get pointer to native Firebird type (zero-copy)
    static const FB_I128* getInt128Ptr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const FB_I128*>(buffer + offset);
    }
    
    static const FB_DEC16* getDecFloat16Ptr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const FB_DEC16*>(buffer + offset);
    }
    
    static const FB_DEC34* getDecFloat34Ptr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const FB_DEC34*>(buffer + offset);
    }
    
    static const ISC_TIME_TZ* getTimeTzPtr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const ISC_TIME_TZ*>(buffer + offset);
    }
    
    static const ISC_TIMESTAMP_TZ* getTimestampTzPtr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const ISC_TIMESTAMP_TZ*>(buffer + offset);
    }
    
    // NULL checking
    static bool isNull(const uint8_t* buffer, const FieldDesc& field) {
        if (field.nullOffset >= 0) {
            const int16_t* null_flag = reinterpret_cast<const int16_t*>(buffer + field.nullOffset);
            return (*null_flag == ISC_NULL);
        }
        return false;
    }
};
```

### 4.3 Usage Examples

```cpp
// JSON extraction using IUtil for conversion
void JsonPacker::extractField(const FieldDesc& field, const uint8_t* buffer, json& result) {
    if (InternalExtractor::isNull(buffer, field)) {
        result = nullptr;
        return;
    }
    
    switch (field.type) {
    case SQL_INT128: {
        const FB_I128* raw = InternalExtractor::getInt128Ptr(buffer, field.dataOffset);
        char str_buffer[64];
        util->getInt128(&status)->toString(&status, raw, field.scale, 
                                          sizeof(str_buffer), str_buffer);
        result = std::string(str_buffer);
        break;
    }
    case SQL_DEC16: {
        const FB_DEC16* raw = InternalExtractor::getDecFloat16Ptr(buffer, field.dataOffset);
        char str_buffer[64];
        util->getDecFloat16(&status)->toString(&status, raw, 
                                              sizeof(str_buffer), str_buffer);
        result = std::string(str_buffer);
        break;
    }
    case SQL_TIMESTAMP_TZ: {
        const ISC_TIMESTAMP_TZ* raw = InternalExtractor::getTimestampTzPtr(buffer, field.dataOffset);
        unsigned year, month, day, hour, min, sec, frac, tzId;
        const char* tzName;
        util->decodeTimestampTz(&status, raw, &year, &month, &day, 
                               &hour, &min, &sec, &frac, &tzId, &tzName);
        // Format as ISO 8601 string
        char iso_buffer[128];
        snprintf(iso_buffer, sizeof(iso_buffer), 
                "%04u-%02u-%02uT%02u:%02u:%02u.%03u %s",
                year, month, day, hour, min, sec, frac/1000, tzName);
        result = std::string(iso_buffer);
        break;
    }
    // ... other types
    }
}

// Direct buffer writing from JSON
void JsonPacker::packField(const FieldDesc& field, uint8_t* buffer, const json& value) {
    if (value.is_null()) {
        // Set NULL indicator
        if (field.nullOffset >= 0) {
            *reinterpret_cast<int16_t*>(buffer + field.nullOffset) = ISC_NULL;
        }
        return;
    }
    
    switch (field.type) {
    case SQL_INT128: {
        FB_I128* target = reinterpret_cast<FB_I128*>(buffer + field.dataOffset);
        util->getInt128(&status)->fromString(&status, field.scale, 
                                            value.get<std::string>().c_str(), target);
        break;
    }
    // ... other types
    }
}
```

## 5. Raw Data Layouts (Firebird Native Structures)

### 5.1 INT128 Raw Layout
```cpp
// Firebird definition (from firebird/impl/types_pub.h)
struct FB_I128_t {
    ISC_UINT64 fb_data[2];  // Little-endian: [0]=low 64 bits, [1]=high 64 bits
};
```
**Byte representation:**
- Bytes 0-7: Lower 64 bits (little-endian)
- Bytes 8-15: Upper 64 bits (little-endian)
- Example: `12345678901234567890` stored as `fb_data[0]=0xAB54A98CEB1F0AD2, fb_data[1]=0x0`

### 5.2 DECFLOAT Raw Layouts
```cpp
// DECFLOAT(16) - IEEE 754-2008 Decimal64 BID encoding
struct FB_DEC16_t {
    ISC_UINT64 fb_data[1];  // 8 bytes BID format
};

// DECFLOAT(34) - IEEE 754-2008 Decimal128 BID encoding  
struct FB_DEC34_t {
    ISC_UINT64 fb_data[2];  // 16 bytes BID format
};
```
**BID (Binary Integer Decimal) format:**
- Sign bit + combination field (5 bits) + exponent + significand
- NOT DPD (Densely Packed Decimal) encoding
- Direct hardware support on Intel/AMD with AVX-512

### 5.3 Time/Timestamp with Timezone Raw Layouts
```cpp
// TIME WITH TIME ZONE (6 bytes)
typedef struct {
    ISC_TIME utc_time;      // 4 bytes: time in 0.1ms units (0-863999999)
    ISC_USHORT time_zone;   // 2 bytes: IANA timezone ID
} ISC_TIME_TZ;

// TIME WITH TIME ZONE Extended (8 bytes)
typedef struct {
    ISC_TIME utc_time;      // 4 bytes
    ISC_USHORT time_zone;   // 2 bytes: IANA ID
    ISC_SHORT ext_offset;   // 2 bytes: UTC offset in minutes (-1439 to +1439)
} ISC_TIME_TZ_EX;

// TIMESTAMP WITH TIME ZONE (10 bytes)
typedef struct {
    ISC_TIMESTAMP utc_timestamp;  // 8 bytes (date + time)
    ISC_USHORT time_zone;         // 2 bytes
} ISC_TIMESTAMP_TZ;

// where ISC_TIMESTAMP is:
typedef struct {
    ISC_DATE timestamp_date;  // 4 bytes: Modified Julian Days since Nov 17, 1858
    ISC_TIME timestamp_time;  // 4 bytes: time in 0.1ms units
} ISC_TIMESTAMP;
```

### 5.4 BLOB ID Layout
```cpp
struct GDS_QUAD_t {
    ISC_LONG gds_quad_high;   // 4 bytes: transaction/page ID
    ISC_ULONG gds_quad_low;   // 4 bytes: sequence number
};
```

## 6. Zero-Copy Field Extraction

### 6.1 Direct Memory Mapping
```cpp
namespace fbpp::core::binding {

// Zero-copy extraction for Firebird native types
template<typename FbType>
class DirectExtractor {
public:
    // Extract without conversion - just cast
    static const FbType* extract_ptr(const uint8_t* buffer, size_t offset) {
        return reinterpret_cast<const FbType*>(buffer + offset);
    }
    
    // Extract with copy (for unaligned access)
    static FbType extract_copy(const uint8_t* buffer, size_t offset) {
        FbType result;
        std::memcpy(&result, buffer + offset, sizeof(FbType));
        return result;
    }
};

// Specializations for each Firebird type
template<> 
const FB_I128* DirectExtractor<FB_I128>::extract_ptr(const uint8_t* buffer, size_t offset) {
    // Ensure 16-byte alignment for INT128
    assert((reinterpret_cast<uintptr_t>(buffer + offset) & 15) == 0);
    return reinterpret_cast<const FB_I128*>(buffer + offset);
}

} // namespace fbpp::core::binding
```

### 6.2 Firebird Native Types Mapping

| SQL Type | Firebird Native Type | Size | Alignment | Direct Access |
|----------|---------------------|------|-----------|---------------|
| INT128 | `FB_I128` | 16 bytes | 16 | Pointer cast |
| NUMERIC(38,x) | `FB_I128` + scale | 16 bytes | 16 | Pointer cast |
| NUMERIC(18,x) | `int64_t` + scale | 8 bytes | 8 | Direct read |
| DECFLOAT(16) | `FB_DEC16` | 8 bytes | 8 | Pointer cast |
| DECFLOAT(34) | `FB_DEC34` | 16 bytes | 16 | Pointer cast |
| TIME WITH TZ | `ISC_TIME_TZ` | 6 bytes | 4 | Pointer cast |
| TIME WITH TZ EX | `ISC_TIME_TZ_EX` | 8 bytes | 4 | Pointer cast |
| TIMESTAMP WITH TZ | `ISC_TIMESTAMP_TZ` | 10 bytes | 4 | Pointer cast |
| TIMESTAMP WITH TZ EX | `ISC_TIMESTAMP_TZ_EX` | 12 bytes | 4 | Pointer cast |
| BLOB | `ISC_QUAD` | 8 bytes | 4 | Direct read |

## 7. Performance Considerations

1. **Zero-copy where possible**: Work directly with Firebird native types
2. **Lazy conversion**: Convert only when needed (e.g., to JSON strings)
3. **Direct pointer casting**: Avoid memcpy for aligned data
4. **IUtil for conversions**: Use Firebird's optimized conversion routines
5. **Batch processing**: Process entire buffer before any conversions

## 8. Implementation Priority

1. **Phase 1**: Basic types + INT128/DECIMAL support
2. **Phase 2**: DECFLOAT16/34 with IEEE 754-2008 compliance
3. **Phase 3**: Timezone types with ICU integration
4. **Phase 4**: Performance optimizations and SIMD