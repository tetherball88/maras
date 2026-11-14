#pragma once

#include "maras/domain/relationships/BonusesDomain.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS::Engine {

    /// Loads `bonuses.json` into the domain `BonusCatalog` singleton.
    class BonusesConfigLoader {
    public:
        static BonusesConfigLoader& GetSingleton();

        BonusesConfigLoader(const BonusesConfigLoader&) = delete;
        BonusesConfigLoader& operator=(const BonusesConfigLoader&) = delete;
        BonusesConfigLoader(BonusesConfigLoader&&) = delete;
        BonusesConfigLoader& operator=(BonusesConfigLoader&&) = delete;

        /// Loads the default `Data/SKSE/Plugins/MARAS/bonuses.json` configuration.
        bool Initialize();

        /// Loads bonus categories from an explicit JSON file path.
        bool LoadFromFile(const std::filesystem::path& filePath);

        /// Clears all loaded categories.
        void Clear() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

        [[nodiscard]] const MARAS::Domain::Relationships::BonusCatalog& GetCatalog() const noexcept {
            return catalog_;
        }

        [[nodiscard]] int GetBonusCount(std::string_view type) const { return catalog_.GetBonusCount(type); }

        [[nodiscard]] const MARAS::Domain::Relationships::BonusEntry* GetBonusEntry(std::string_view type,
                                                                                     int index = 0) const {
            return catalog_.GetBonusEntry(type, index);
        }

        [[nodiscard]] const std::unordered_map<std::string, std::vector<MARAS::Domain::Relationships::BonusEntry>>&
        AllCategories() const noexcept {
            return catalog_.AllCategories();
        }

    private:
        BonusesConfigLoader() = default;
        ~BonusesConfigLoader() = default;

        MARAS::Domain::Relationships::BonusCatalog catalog_;
        std::atomic<bool> loaded_{ false };
    };

}  // namespace MARAS::Engine
