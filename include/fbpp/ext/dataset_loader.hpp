#pragma once

#include <fbpp/ext/dataset_adapter.hpp>

#ifdef FBPP_WITH_RAD_DATASET

namespace fbpp::ext {

class DatasetLoader {
public:
    explicit DatasetLoader(Data::Db::TDataSet* dataset,
                           const fbpp::core::MessageMetadata& metadata,
                           bool clearExisting = true)
        : dataSet_(dataset) {
        resetStructure(metadata, clearExisting);
    }

    void resetStructure(const fbpp::core::MessageMetadata& metadata,
                        bool clearExisting = true) {
        if (!dataSet_) {
            throw std::invalid_argument("DatasetLoader requires non-null dataset");
        }
        detail::ensureDatasetReady(dataSet_, metadata, clearExisting);
    }

    void loadRow(const unsigned char* buffer,
                 const fbpp::core::MessageMetadata& metadata,
                 fbpp::core::Transaction* txn) {
        if (!dataSet_) {
            throw std::runtime_error("DatasetLoader not initialized with dataset");
        }
        DatasetScope scope(dataSet_);
        detail::assignRow(dataSet_, metadata, buffer, txn);
    }

    Data::Db::TDataSet* dataset() const { return dataSet_; }

private:
    Data::Db::TDataSet* dataSet_ = nullptr;
};

} // namespace fbpp::ext

#endif // FBPP_WITH_RAD_DATASET

