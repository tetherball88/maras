#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace MARAS {

    struct BonusEntry {
        std::string description;
        int effectIndex = 0;
        std::string perk;  // store raw string from JSON; will be resolved to form at lookup
        std::string type;
        std::string unit;
        float value = 0.0f;
    };

    class BonusesService {
    public:
        static BonusesService& GetSingleton();

        // Load bonuses.json from Data/SKSE/Plugins/MARAS/bonuses.json
        // Returns true on success
        bool LoadFromFile();

        // Accessors used by Papyrus bindings
        int GetBonusCount(const std::string& type) const;
        const BonusEntry* GetBonusEntry(const std::string& type, int index = 0) const;

        // Clear loaded data (called on Revert or plugin unload if needed)
        void Clear();

    private:
        BonusesService() = default;

        bool ParseJson(const nlohmann::json& j);

        std::unordered_map<std::string, std::vector<BonusEntry>> data_;
    };

}  // namespace MARAS
