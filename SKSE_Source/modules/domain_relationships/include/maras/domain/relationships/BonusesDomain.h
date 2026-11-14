#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS::Domain::Relationships {

    struct BonusEntry {
        std::string name{};
        std::string description{};
        int effectIndex{ 0 };
        std::string perk{};
        std::string type{};
        std::string unit{};
        float value{ 0.0f };

        bool operator==(const BonusEntry&) const = default;
    };

    class BonusCatalog {
    public:
        void SetCategories(std::unordered_map<std::string, std::vector<BonusEntry>> categories);
        void Clear() noexcept { categories_.clear(); }

        [[nodiscard]] int GetBonusCount(std::string_view type) const;
        [[nodiscard]] const BonusEntry* GetBonusEntry(std::string_view type, int index = 0) const;
        [[nodiscard]] const std::unordered_map<std::string, std::vector<BonusEntry>>& AllCategories() const noexcept {
            return categories_;
        }

    private:
        std::unordered_map<std::string, std::vector<BonusEntry>> categories_;
    };

}  // namespace MARAS::Domain::Relationships
