#include "maras/engine/BonusesConfigLoader.h"

#include "maras/engine/BuffReconciler.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"

#include <array>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 2> kDefaultBonusPaths = {
            "Data/SKSE/Plugins/MARAS/bonuses.json",
            "SKSE/Plugins/MARAS/bonuses.json"
        };

        [[nodiscard]] std::string ReadStringField(const nlohmann::json& obj, const char* key) {
            return (obj.contains(key) && obj[key].is_string()) ? obj[key].get<std::string>() : std::string{};
        }

        [[nodiscard]] MARAS::Domain::Relationships::BonusEntry ParseBonusEntry(const nlohmann::json& item) {
            MARAS::Domain::Relationships::BonusEntry entry;
            entry.name = ReadStringField(item, "name");
            entry.description = ReadStringField(item, "description");
            entry.effectIndex =
                (item.contains("effectIndex") && item["effectIndex"].is_number_integer()) ? item["effectIndex"].get<int>() : 0;
            entry.perk = ReadStringField(item, "perk");
            entry.type = ReadStringField(item, "type");
            entry.unit = ReadStringField(item, "unit");
            entry.value =
                (item.contains("value") && item["value"].is_number()) ? item["value"].get<float>() : 0.0f;
            return entry;
        }

    }  // namespace

    BonusesConfigLoader& BonusesConfigLoader::GetSingleton() {
        static BonusesConfigLoader instance;
        return instance;
    }

    bool BonusesConfigLoader::Initialize() {
        std::error_code ec;
        for (const char* candidate : kDefaultBonusPaths) {
            const std::filesystem::path path(candidate);
            if (std::filesystem::exists(path, ec)) {
                return LoadFromFile(path);
            }
        }
        MARAS_LOG_WARN("[BonusesConfigLoader] Could not find bonuses.json in default search paths");
        return false;
    }

    bool BonusesConfigLoader::LoadFromFile(const std::filesystem::path& filePath) {
        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(filePath, root, "BonusesConfigLoader")) {
            return false;
        }
        if (!root.is_object()) {
            MARAS_LOG_ERROR("[BonusesConfigLoader] Root is not a JSON object in '{}'", filePath.string());
            return false;
        }

        std::unordered_map<std::string, std::vector<MARAS::Domain::Relationships::BonusEntry>> parsedCategories;
        std::size_t totalEntries = 0;

        for (const auto& [category, items] : root.items()) {
            if (!items.is_array()) {
                MARAS_LOG_WARN("[BonusesConfigLoader] Category '{}' is not an array in '{}'",
                               category, filePath.string());
                continue;
            }

            std::vector<MARAS::Domain::Relationships::BonusEntry> list;
            list.reserve(items.size());
            for (const auto& item : items) {
                if (!item.is_object()) {
                    MARAS_LOG_WARN("[BonusesConfigLoader] Non-object entry in category '{}' of '{}'",
                                   category, filePath.string());
                    continue;
                }
                list.push_back(ParseBonusEntry(item));
                ++totalEntries;
            }
            parsedCategories.emplace(category, std::move(list));
        }

        catalog_.SetCategories(std::move(parsedCategories));
        loaded_.store(true, std::memory_order_release);
        MARAS::BuffReconciler::GetSingleton().InvalidateCache();

        MARAS_LOG_INFO("[BonusesConfigLoader] Loaded {} categories ({} entries) from '{}'",
                       catalog_.AllCategories().size(), totalEntries, filePath.string());
        return true;
    }

    void BonusesConfigLoader::Clear() noexcept {
        catalog_.Clear();
        loaded_.store(false, std::memory_order_release);
        MARAS::BuffReconciler::GetSingleton().InvalidateCache();
    }

}  // namespace MARAS::Engine
