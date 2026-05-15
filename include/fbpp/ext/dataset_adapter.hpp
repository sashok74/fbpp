#pragma once

#include <fbpp/core/firebird_compat.hpp>
#include <fbpp/core/message_metadata.hpp>
#include <fbpp/core/pack_utils.hpp>
#include <fbpp/core/transaction.hpp>

#ifdef FBPP_WITH_RAD_DATASET

#include <fbpp/core/extended_types.hpp>
#include <fbpp/core/timestamp_utils.hpp>
#include <fbpp/ext/rad_variant_decoder.hpp>

#include <Data.DB.hpp>
#include <System.Classes.hpp>
#include <System.DateUtils.hpp>
#include <System.SysUtils.hpp>
#include <System.Variants.hpp>
#include <System.hpp>

#if defined(FBPP_WITH_EHLIB)
#include <MemTableEh.hpp>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <iterator>
#include <memory>

namespace fbpp::ext {

struct DatasetRow {
    explicit DatasetRow(Data::Db::TDataSet* dataset = nullptr) : dataSet(dataset) {}
    Data::Db::TDataSet* dataSet = nullptr;
};

class DatasetScope {
public:
    explicit DatasetScope(Data::Db::TDataSet* dataset);
    ~DatasetScope();

	DatasetScope(const DatasetScope&) = delete;
	DatasetScope& operator=(const DatasetScope&) = delete;
	DatasetScope(DatasetScope&&) = delete;
    DatasetScope& operator=(DatasetScope&&) = delete;

private:
	Data::Db::TDataSet* dataset_;
};

namespace detail {

// Plan-based configureFieldDefs lives in fbpp::ext (rad_variant_decoder.hpp);
// this detail namespace keeps only dataset-specific helpers.
void ensureDatasetReady(Data::Db::TDataSet* dataset,
                        const fbpp::core::MessageMetadata& meta,
                        bool clearExisting);
void assignRow(Data::Db::TDataSet* dataset,
               const fbpp::core::MessageMetadata& meta,
               const unsigned char* buffer,
               fbpp::core::Transaction* txn);

void pushDataset(Data::Db::TDataSet* dataset);
void popDataset(Data::Db::TDataSet* dataset);
Data::Db::TDataSet* currentDataset();

} // namespace detail

} // namespace fbpp::ext

namespace fbpp::core {

template<>
struct UnpackerHelper<fbpp::ext::DatasetRow> {
    static fbpp::ext::DatasetRow unpack(const unsigned char* buffer,
                                        const MessageMetadata* metadata,
                                        Transaction* transaction);
};

} // namespace fbpp::core

// ---------------------------------------------------------------------------
// Inline implementation
// ---------------------------------------------------------------------------

namespace fbpp::ext {
namespace {

inline std::vector<Data::Db::TDataSet*>& datasetStack() {
    thread_local std::vector<Data::Db::TDataSet*> stack;
    return stack;
}

inline System::UnicodeString Utf8ToWide(const std::string& value) {
    return System::UnicodeString(System::UTF8String(value.c_str()));
}

inline std::string TrimTrailingSpaces(std::string value) {
    const auto it = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return ch != ' ';
    });
    value.erase(it.base(), value.end());
    return value;
}

inline double ApplyScale(int64_t value, int scale) {
    if (scale < 0) {
        const double factor = std::pow(10.0, static_cast<double>(-scale));
        return static_cast<double>(value) / factor;
    }
    return static_cast<double>(value);
}

inline double ApplyScale(double value, int scale) {
    if (scale < 0) {
        const double factor = std::pow(10.0, static_cast<double>(-scale));
        return value / factor;
    }
    return value;
}

inline double Int128ToDouble(const uint8_t* data, int scale) {
    uint64_t low = 0;
    int64_t high = 0;
    std::memcpy(&low, data, sizeof(low));
    std::memcpy(&high, data + sizeof(uint64_t), sizeof(high));

    long double value = static_cast<long double>(high);
    value *= 18446744073709551616.0L; // 2^64
    value += static_cast<long double>(low);
    return ApplyScale(static_cast<double>(value), scale);
}

inline System::TDateTime ChronoToDateTime(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;
    constexpr double microsPerDay = 86400000000.0;
    const auto micros = duration_cast<microseconds>(tp.time_since_epoch()).count();
    const auto seconds = micros / 1000000;
    const auto remainder = micros % 1000000;
    System::TDateTime dt = System::Dateutils::UnixToDateTime(static_cast<__int64>(seconds));
    dt += static_cast<double>(remainder) / microsPerDay;
    return dt;
}

class ControlGuard {
public:
    explicit ControlGuard(Data::Db::TDataSet* dataset) : dataset_(dataset) {
        if (dataset_) {
            dataset_->DisableControls();
        }
    }
    ~ControlGuard() {
        if (dataset_) {
            dataset_->EnableControls();
        }
    }

    ControlGuard(const ControlGuard&) = delete;
    ControlGuard& operator=(const ControlGuard&) = delete;

private:
    Data::Db::TDataSet* dataset_;
};

inline void AssignBlobField(Data::Db::TField* field,
                            const fbpp::core::FieldInfo& info,
                            const unsigned char* dataPtr,
                            fbpp::core::Transaction* txn) {
    if (!field || !field->IsBlob() || !txn) {
        return;
    }

    fbpp::core::Blob blobId(dataPtr);
    if (blobId.isNull()) {
        field->Clear();
        return;
    }

    ISC_QUAD blobRef{};
    std::memcpy(&blobRef, blobId.getId(), sizeof(blobRef));
    auto blobData = txn->loadBlob(&blobRef);
    auto blobField = dynamic_cast<Data::Db::TBlobField*>(field);
    if (!blobField) {
        field->Clear();
        return;
    }

    if (info.subType == 1) {
        std::string text(blobData.begin(), blobData.end());
        blobField->AsWideString = Utf8ToWide(text);
    } else {
        std::unique_ptr<System::Classes::TMemoryStream> stream(new System::Classes::TMemoryStream());
        if (!blobData.empty()) {
            stream->WriteBuffer(blobData.data(), static_cast<int>(blobData.size()));
            stream->Position = 0;
        }
        blobField->LoadFromStream(stream.get());
    }
}

inline void AssignField(Data::Db::TField* field,
                        const fbpp::core::FieldInfo& info,
                        const unsigned char* dataPtr,
                        fbpp::core::Transaction* txn) {
    if (!field) {
        return;
    }

    // BLOB → специальный path через TBlobField::LoadFromStream (varArray of byte
    // в Variant работает, но TBlobField::Value=arr ненадёжно).
    if (field->IsBlob()) {
        AssignBlobField(field, info, dataPtr, txn);
        return;
    }

    // Все остальные types → единый helper в lib (rad_variant_decoder.hpp).
    // Currency (NUMERIC scale ∈ [-4..-1]) → varCurrency (exact int64), не double.
    fbpp::ext::VariantDecodeOptions opts;
    opts.exactCurrency = true;
    opts.loadBlobs = false;  // BLOB уже отработан выше через AssignBlobField

    System::Variant value = fbpp::ext::decodeFieldToVariant(
        info, reinterpret_cast<const uint8_t*>(dataPtr), txn, opts);

    if (System::Variants::VarIsEmpty(value)) {
        field->Clear();
        return;
    }
    field->Value = value;
}

} // namespace

DatasetScope::DatasetScope(Data::Db::TDataSet* dataset)
    : dataset_(dataset) {
    if (!dataset_) {
        throw std::invalid_argument("DatasetScope requires non-null dataset");
    }
    detail::pushDataset(dataset_);
}

DatasetScope::~DatasetScope() {
    if (dataset_) {
        detail::popDataset(dataset_);
    }
}

namespace detail {

inline void pushDataset(Data::Db::TDataSet* dataset) {
    if (!dataset) {
        return;
    }
    datasetStack().push_back(dataset);
}

inline void popDataset(Data::Db::TDataSet* dataset) {
    auto& stack = datasetStack();
    if (stack.empty()) {
        return;
    }
    if (stack.back() == dataset) {
        stack.pop_back();
        return;
    }
    // If stack is misaligned, remove the first matching element from the back
    auto it = std::find(stack.rbegin(), stack.rend(), dataset);
    if (it != stack.rend()) {
        stack.erase(std::next(it).base());
    }
}

inline Data::Db::TDataSet* currentDataset() {
    const auto& stack = datasetStack();
    return stack.empty() ? nullptr : stack.back();
}

// mapFieldType + configureFieldDefs(meta) moved to fbpp::ext::makeColumnPlan()
// + fbpp::ext::configureFieldDefs(plans) in rad_variant_decoder.hpp —
// single source of truth for SQL_* → TFieldType + RadColumnKind.

inline bool callDatasetProc(Data::Db::TDataSet* dataset, const wchar_t* name) {
    if (!dataset) {
        return false;
    }
    System::UnicodeString methodName(name);
    System::TMethod method{};
    method.Code = dataset->MethodAddress(methodName);
    if (!method.Code) {
        return false;
    }
    method.Data = dataset;
    using Proc = void (__closure *)(void);
    Proc proc = reinterpret_cast<Proc&>(method);
    proc();
    return true;
}

inline void ensureDatasetReady(Data::Db::TDataSet* dataset,
                               const fbpp::core::MessageMetadata& meta,
                               bool clearExisting) {
    if (!dataset) {
        throw std::invalid_argument("Dataset pointer must not be null");
    }

	ControlGuard guard(dataset);

    const bool wasActive = dataset->Active;
    if (wasActive) {
        dataset->Close();
    }

    // Builds plans once and configures TFieldDefs through plan-based API.
    auto plans = fbpp::ext::buildColumnPlans(meta);
    configureFieldDefs(dataset->FieldDefs, plans);

#if defined(FBPP_WITH_EHLIB)
    if (auto* mem = dynamic_cast<Memtableeh::TMemTableEh*>(dataset)) {
        // CreateDataSet() сам делает Close+Open и оставляет таблицу пустой
        // (см. MemTableEh.pas:5757). Лишние Open()+EmptyTable() убраны.
        mem->Close();
        mem->CreateDataSet();
        return;
    }
#endif

    if (!callDatasetProc(dataset, L"CreateDataSet")) {
        if (!dataset->Active) {
            dataset->Open();
        }
    }

    if (!dataset->Active) {
        dataset->Open();
    }

    if (!dataset->Active) {
        throw std::runtime_error("Failed to open dataset; please create fields manually");
    }

    if (clearExisting) {
        if (!callDatasetProc(dataset, L"EmptyTable") &&
            !callDatasetProc(dataset, L"EmptyDataSet")) {
            dataset->First();
            while (!dataset->Eof) {
                dataset->Delete();
            }
        }
    }
}

inline void assignRow(Data::Db::TDataSet* dataset,
                      const fbpp::core::MessageMetadata& meta,
                      const unsigned char* buffer,
                      fbpp::core::Transaction* txn) {
    if (!dataset) {
        throw std::invalid_argument("Dataset pointer must not be null");
    }
    if (!buffer) {
        throw std::invalid_argument("Buffer pointer must not be null");
    }
    if (!dataset->Active) {
        throw std::runtime_error("Dataset must be active before loading data");
    }

    const auto fieldCount = meta.getCount();
    const auto fields = meta.getFields();

    dataset->Append();
    for (unsigned index = 0; index < fieldCount; ++index) {
        auto* field = dataset->Fields->Fields[index];
        const auto nullPtr = reinterpret_cast<const std::int16_t*>(
            buffer + meta.getNullOffset(index));
        if (nullPtr && *nullPtr == -1) {
            field->Clear();
            continue;
        }
        const auto* dataPtr = buffer + meta.getOffset(index);
        AssignField(field, fields[index], dataPtr, txn);
    }
    dataset->Post();
}

} // namespace detail
} // namespace fbpp::ext

namespace fbpp::core {

inline fbpp::ext::DatasetRow UnpackerHelper<fbpp::ext::DatasetRow>::unpack(
    const unsigned char* buffer,
    const MessageMetadata* metadata,
    Transaction* transaction) {
    if (!metadata) {
        throw FirebirdException("DatasetRow unpack requires message metadata");
    }

    auto* dataset = fbpp::ext::detail::currentDataset();
    if (!dataset) {
        throw FirebirdException("DatasetRow unpack requires active DatasetScope");
    }

    fbpp::ext::detail::assignRow(dataset, *metadata, buffer, transaction);
    return fbpp::ext::DatasetRow(dataset);
}

} // namespace fbpp::core

#endif  //FBPP_WITH_RAD_DATASET
#include <System.hpp>
