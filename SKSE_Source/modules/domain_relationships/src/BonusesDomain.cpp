#include "maras/domain/relationships/BonusesDomain.h"

#include "maras/foundation/StringUtils.h"

namespace MARAS::Domain::Relationships {

    void BonusCatalog::SetCategories(std::unordered_map<std::string, std::vector<BonusEntry>> categories) {
        categories_.clear();
        categories_.reserve(categories.size());
        for (auto& [rawKey, entries] : categories) {
            categories_.emplace(Foundation::StringUtils::ToLowerStd(rawKey), std::move(entries));
        }
    }

    int BonusCatalog::GetBonusCount(std::string_view type) const {
        const auto it = categories_.find(Foundation::StringUtils::ToLowerStd(type));
        if (it == categories_.end()) {
            return 0;
        }
        return static_cast<int>(it->second.size());
    }

    const BonusEntry* BonusCatalog::GetBonusEntry(std::string_view type, int index) const {
        const auto it = categories_.find(Foundation::StringUtils::ToLowerStd(type));
        if (it == categories_.end()) {
            return nullptr;
        }
        if (index < 0 || static_cast<std::size_t>(index) >= it->second.size()) {
            return nullptr;
        }
        return &it->second[static_cast<std::size_t>(index)];
    }

}  // namespace MARAS::Domain::Relationships
