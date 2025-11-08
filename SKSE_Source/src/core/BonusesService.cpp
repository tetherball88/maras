#include "core/BonusesService.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "utils/Common.h"

namespace MARAS {

    BonusesService& BonusesService::GetSingleton() {
        static BonusesService s;
        return s;
    }

    bool BonusesService::ParseJson(const nlohmann::json& j) {
        try {
            data_.clear();

            if (!j.is_object()) return false;

            for (auto it = j.begin(); it != j.end(); ++it) {
                std::string key = it.key();
                // normalize keys to lowercase for case-insensitive lookup
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                const auto& arr = it.value();

                if (!arr.is_array()) continue;

                std::vector<BonusEntry> list;
                for (const auto& elem : arr) {
                    BonusEntry be;
                    if (elem.contains("description") && elem["description"].is_string())
                        be.description = elem["description"].get<std::string>();
                    if (elem.contains("effectIndex") && elem["effectIndex"].is_number_integer())
                        be.effectIndex = elem["effectIndex"].get<int>();
                    if (elem.contains("perk") && elem["perk"].is_string()) be.perk = elem["perk"].get<std::string>();
                    if (elem.contains("type") && elem["type"].is_string()) be.type = elem["type"].get<std::string>();
                    if (elem.contains("unit") && elem["unit"].is_string()) be.unit = elem["unit"].get<std::string>();
                    if (elem.contains("value") &&
                        (elem["value"].is_number_float() || elem["value"].is_number_integer()))
                        be.value = elem["value"].get<float>();

                    list.push_back(std::move(be));
                }

                data_.emplace(key, std::move(list));
            }

            MARAS_LOG_INFO("BonusesService: parsed {} bonus categories", data_.size());

            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("BonusesService::ParseJson exception: {}", e.what());
            return false;
        }
    }

    bool BonusesService::LoadFromFile() {
        // Typical runtime path where plugin files are placed
        std::filesystem::path path = "Data/SKSE/Plugins/MARAS/bonuses.json";

        try {
            if (!std::filesystem::exists(path)) {
                MARAS_LOG_WARN("BonusesService: bonuses.json not found at {}", path.string());
                return false;
            }

            std::ifstream in(path);
            if (!in.is_open()) {
                MARAS_LOG_ERROR("BonusesService: failed to open {}", path.string());
                return false;
            }

            nlohmann::json j;
            in >> j;

            if (!ParseJson(j)) {
                MARAS_LOG_ERROR("BonusesService: failed to parse JSON from {}", path.string());
                return false;
            }

            MARAS_LOG_INFO("BonusesService: loaded bonuses ({} categories)", data_.size());
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("BonusesService::LoadFromFile exception: {}", e.what());
            return false;
        }
    }

    void BonusesService::Clear() { data_.clear(); }

    int BonusesService::GetBonusCount(const std::string& type) const {
        auto it = data_.find(type);
        if (it == data_.end()) return 0;
        return static_cast<int>(it->second.size());
    }

    const BonusEntry* BonusesService::GetBonusEntry(const std::string& type, int index) const {
        auto it = data_.find(type);
        if (it == data_.end()) return nullptr;
        if (index < 0 || static_cast<size_t>(index) >= it->second.size()) return nullptr;
        return &it->second[static_cast<size_t>(index)];
    }

}  // namespace MARAS
