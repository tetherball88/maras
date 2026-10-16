#include "core/NameService.h"

#include "core/NPCRelationshipManager.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/StringUtils.h"

namespace MARAS {

    NameService& NameService::GetSingleton() {
        static NameService instance;
        return instance;
    }

    NameService::NameService()
        : rng_(std::random_device{}()) {
    }

    bool NameService::IsLoaded() const {
        return isChildLoaded_.load(std::memory_order_acquire) ||
               isCritterLoaded_.load(std::memory_order_acquire);
    }

    bool NameService::IsChildLoaded() const {
        return isChildLoaded_.load(std::memory_order_acquire);
    }

    bool NameService::IsCritterLoaded() const {
        return isCritterLoaded_.load(std::memory_order_acquire);
    }

    void NameService::Clear() {
        std::unique_lock lock(mutex_);
        maleNames_.clear();
        femaleNames_.clear();
        critterNames_.clear();
        isChildLoaded_.store(false, std::memory_order_release);
        isCritterLoaded_.store(false, std::memory_order_release);
    }

    bool NameService::LoadFromFile(const std::filesystem::path& customChildPath, const std::filesystem::path& customCritterPath) {
        bool okChild = LoadChildNamesFromFile(customChildPath);
        bool okCritter = LoadCritterNamesFromFile(customCritterPath);
        return okChild || okCritter;
    }

    bool NameService::LoadChildNamesFromFile(const std::filesystem::path& customPath) {
        std::vector<std::filesystem::path> searchPaths;
        if (!customPath.empty()) {
            searchPaths.push_back(customPath);
        }
        searchPaths.push_back("Data/SKSE/Plugins/MARAS/childrenNames.json");
        searchPaths.push_back("SKSE/Plugins/MARAS/childrenNames.json");

        std::filesystem::path targetPath;
        for (const auto& p : searchPaths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
                targetPath = p;
                break;
            }
        }

        if (targetPath.empty()) {
            MARAS_LOG_WARN("NameService: childrenNames.json not found in search paths (Data/SKSE/Plugins/MARAS/childrenNames.json or SKSE/Plugins/MARAS/childrenNames.json)");
            return false;
        }

        try {
            std::ifstream file(targetPath);
            if (!file.is_open()) {
                MARAS_LOG_ERROR("NameService: failed to open {}", targetPath.string());
                return false;
            }

            nlohmann::json rootJson;
            file >> rootJson;

            std::unique_lock lock(mutex_);
            maleNames_.clear();
            femaleNames_.clear();

            const auto& racesObject = (rootJson.contains("names") && rootJson["names"].is_object())
                                          ? rootJson["names"]
                                          : rootJson;

            size_t totalMale = 0;
            size_t totalFemale = 0;

            for (auto& [raceKeyRaw, genderEntries] : racesObject.items()) {
                if (!genderEntries.is_object()) continue;

                std::string raceKey = NormalizeRaceKeyLocked(raceKeyRaw);
                if (raceKey.empty()) continue;

                auto parseGender = [&](const char* key, auto& targetMap) -> size_t {
                    if (!genderEntries.contains(key) || !genderEntries[key].is_array()) return 0;
                    auto& vec = targetMap[raceKey];
                    size_t count = 0;
                    for (const auto& item : genderEntries[key]) {
                        if (item.is_string()) {
                            std::string val = StringUtils::TrimStd(item.get<std::string>());
                            if (!val.empty()) {
                                vec.push_back(std::move(val));
                                ++count;
                            }
                        }
                    }
                    return count;
                };

                totalMale += parseGender("male", maleNames_);
                totalFemale += parseGender("female", femaleNames_);
            }

            // Alias orc <-> orsimer
            if (maleNames_.contains("orsimer") && !maleNames_.contains("orc")) {
                maleNames_["orc"] = maleNames_["orsimer"];
            } else if (maleNames_.contains("orc") && !maleNames_.contains("orsimer")) {
                maleNames_["orsimer"] = maleNames_["orc"];
            }
            if (femaleNames_.contains("orsimer") && !femaleNames_.contains("orc")) {
                femaleNames_["orc"] = femaleNames_["orsimer"];
            } else if (femaleNames_.contains("orc") && !femaleNames_.contains("orsimer")) {
                femaleNames_["orsimer"] = femaleNames_["orc"];
            }

            const bool loaded = (!maleNames_.empty() || !femaleNames_.empty());
            isChildLoaded_.store(loaded, std::memory_order_release);

            MARAS_LOG_INFO("NameService: loaded {} male names and {} female names across {} races from {}",
                           totalMale, totalFemale, maleNames_.size(), targetPath.string());
            return loaded;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("NameService: exception while parsing {}: {}", targetPath.string(), e.what());
            return false;
        }
    }

    bool NameService::LoadCritterNamesFromFile(const std::filesystem::path& customPath) {
        std::vector<std::filesystem::path> searchPaths;
        if (!customPath.empty()) {
            searchPaths.push_back(customPath);
        }
        searchPaths.push_back("Data/SKSE/Plugins/MARAS/critterNames.json");
        searchPaths.push_back("SKSE/Plugins/MARAS/critterNames.json");

        std::filesystem::path targetPath;
        for (const auto& p : searchPaths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
                targetPath = p;
                break;
            }
        }

        if (targetPath.empty()) {
            MARAS_LOG_WARN("NameService: critterNames.json not found in search paths (Data/SKSE/Plugins/MARAS/critterNames.json or SKSE/Plugins/MARAS/critterNames.json)");
            return false;
        }

        try {
            std::ifstream file(targetPath);
            if (!file.is_open()) {
                MARAS_LOG_ERROR("NameService: failed to open {}", targetPath.string());
                return false;
            }

            nlohmann::json rootJson;
            file >> rootJson;

            std::unique_lock lock(mutex_);
            critterNames_.clear();

            const auto& categoriesObject = (rootJson.contains("names") && rootJson["names"].is_object())
                                               ? rootJson["names"]
                                               : rootJson;

            size_t totalNames = 0;

            for (auto& [keyRaw, nameArray] : categoriesObject.items()) {
                if (!nameArray.is_array()) continue;

                std::string key = NormalizeCritterRaceKeyLocked(keyRaw);
                if (key.empty()) {
                    key = StringUtils::ToLowerStd(StringUtils::TrimStd(keyRaw));
                }
                if (key.empty()) continue;

                auto& vec = critterNames_[key];
                for (const auto& item : nameArray) {
                    if (item.is_string()) {
                        std::string val = StringUtils::TrimStd(item.get<std::string>());
                        if (!val.empty()) {
                            vec.push_back(std::move(val));
                            ++totalNames;
                        }
                    }
                }
            }

            const bool loaded = !critterNames_.empty();
            isCritterLoaded_.store(loaded, std::memory_order_release);

            MARAS_LOG_INFO("NameService: loaded {} critter names across {} categories from {}",
                           totalNames, critterNames_.size(), targetPath.string());
            return loaded;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("NameService: exception while parsing {}: {}", targetPath.string(), e.what());
            return false;
        }
    }

    std::string NameService::NormalizeRaceKey(std::string_view rawRace) const {
        std::shared_lock lock(mutex_);
        return NormalizeRaceKeyLocked(rawRace);
    }

    std::string NameService::NormalizeRaceKeyLocked(std::string_view rawRace) const {
        std::string s = StringUtils::ToLowerStd(StringUtils::TrimStd(rawRace));
        if (s.empty() || s == "any" || s == "all" || s == "none") {
            return "";
        }

        // Strip common Skyrim race suffixes
        for (std::string_view suffix : { "racechild"sv, "racevampire"sv, "race"sv }) {
            if (s.length() >= suffix.length() &&
                s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0) {
                s = s.substr(0, s.length() - suffix.length());
                break;
            }
        }
        s = StringUtils::TrimStd(s);

        if (maleNames_.contains(s) || femaleNames_.contains(s)) {
            return s;
        }

        // Substrings / aliases
        if (s.find("argo") != std::string::npos || s.find("saxh") != std::string::npos) return "argonian";
        if (s.find("khaj") != std::string::npos) return "khajiit";
        if (s.find("orsi") != std::string::npos || s.find("orc") != std::string::npos) return "orsimer";
        if (s.find("altm") != std::string::npos || s.find("high") != std::string::npos) return "altmer";
        if (s.find("bosm") != std::string::npos || s.find("wood") != std::string::npos) return "bosmer";
        if (s.find("dunm") != std::string::npos || s.find("dark") != std::string::npos) return "dunmer";
        if (s.find("redg") != std::string::npos || s.find("yoku") != std::string::npos) return "redguard";
        if (s.find("bret") != std::string::npos) return "breton";
        if (s.find("imper") != std::string::npos || s.find("cyrod") != std::string::npos) return "imperial";
        if (s.find("nord") != std::string::npos || s.find("atmor") != std::string::npos) return "nord";

        return "";
    }

    std::unordered_set<std::string> NameService::GetExistingChildNames() const {
        std::unordered_set<std::string> names;
        for (const auto& [_, entry] : NPCRelationshipManager::GetSingleton().GetAllChildren()) {
            std::string trimmed = StringUtils::TrimStd(entry.name);
            if (!trimmed.empty() && !StringUtils::CaseInsensitiveEquals(trimmed, "Newborn")) {
                names.insert(StringUtils::ToLowerStd(trimmed));
            }
        }
        return names;
    }

    std::vector<std::string> NameService::GetChildNames(const std::string& raceName, bool isFemale) const {
        std::shared_lock lock(mutex_);

        const auto& map = isFemale ? femaleNames_ : maleNames_;
        std::string trimmedRace = StringUtils::TrimStd(raceName);

        if (trimmedRace.empty() || trimmedRace == "any" || trimmedRace == "all" || trimmedRace == "none") {
            std::vector<std::string> allNames;
            for (const auto& [_, list] : map) {
                allNames.insert(allNames.end(), list.begin(), list.end());
            }
            return allNames;
        }

        std::string normRace = NormalizeRaceKeyLocked(trimmedRace);
        if (normRace.empty()) {
            return {};
        }

        auto it = map.find(normRace);
        return (it != map.end()) ? it->second : std::vector<std::string>{};
    }

    std::string NameService::GetRandomChildName(const std::string& raceName, bool isFemale, bool isUnique) {
        if (!isChildLoaded_.load(std::memory_order_acquire)) {
            LoadChildNamesFromFile();
        }

        std::vector<std::string> pool = GetChildNames(raceName, isFemale);
        if (pool.empty()) {
            return "";
        }

        if (isUnique) {
            const auto usedNames = GetExistingChildNames();
            if (!usedNames.empty()) {
                std::erase_if(pool, [&](const std::string& name) {
                    return usedNames.contains(StringUtils::ToLowerStd(StringUtils::TrimStd(name)));
                });
                if (pool.empty()) {
                    return "";
                }
            }
        }

        std::unique_lock lock(mutex_);
        std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
        return pool[dist(rng_)];
    }

    std::string NameService::GetRandomChildName(RE::Actor* actor, bool isUnique) {
        if (!actor) {
            return "";
        }

        bool isFemale = false;
        if (auto* base = actor->GetActorBase()) {
            isFemale = (base->GetSex() == 1);
        }

        std::string raceName;
        if (auto* r = actor->GetRace()) {
            if (const char* edid = r->GetFormEditorID(); edid && *edid) {
                raceName = edid;
            } else if (const char* name = r->GetFullName(); name && *name) {
                raceName = name;
            }
        }
        if (raceName.empty()) {
            raceName = ActorSvc::GetRaceName(actor->GetFormID());
        }

        return GetRandomChildName(raceName, isFemale, isUnique);
    }

    std::string NameService::GetRandomChildName(RE::TESForm* raceForm, bool isFemale, bool isUnique) {
        if (!raceForm) {
            return GetRandomChildName("", isFemale, isUnique);
        }

        std::string raceName;
        if (auto* race = raceForm->As<RE::TESRace>()) {
            if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                raceName = edid;
            } else if (const char* name = race->GetFullName(); name && *name) {
                raceName = name;
            }
        } else if (auto* actor = raceForm->As<RE::Actor>()) {
            return GetRandomChildName(actor, isUnique);
        } else if (auto* npc = raceForm->As<RE::TESNPC>()) {
            if (auto* r = npc->GetRace()) {
                if (const char* edid = r->GetFormEditorID(); edid && *edid) {
                    raceName = edid;
                } else if (const char* name = r->GetFullName(); name && *name) {
                    raceName = name;
                }
            }
        }

        if (raceName.empty()) {
            raceName = ActorSvc::GetRaceName(raceForm->GetFormID());
        }

        return GetRandomChildName(raceName, isFemale, isUnique);
    }

    // ── Critter Name Generation ───────────────────────────────────────────────

    std::string NameService::NormalizeCritterRaceKey(std::string_view rawRace) const {
        std::shared_lock lock(mutex_);
        return NormalizeCritterRaceKeyLocked(rawRace);
    }

    std::string NameService::NormalizeCritterRaceKeyLocked(std::string_view rawRace) const {
        std::string_view trimmedRaw = StringUtils::TrimView(rawRace);
        std::string s = StringUtils::ToLowerStd(trimmedRaw);
        if (s.empty()) {
            return "";
        }

        if (critterNames_.contains(s)) {
            return s;
        }

        // Check for arctic fox before general fox
        if (s.find("foxarctic") != std::string::npos || s.find("arctic_fox") != std::string::npos ||
            s.find("arcticfox") != std::string::npos || s.find("arctic") != std::string::npos) {
            return "arctic_fox";
        }

        // Fox
        if (s.find("fox") != std::string::npos) {
            return "fox";
        }

        // Hare / Rabbit
        if (s.find("hare") != std::string::npos || s.find("rabbit") != std::string::npos ||
            s.find("bunny") != std::string::npos) {
            return "hare";
        }

        // Mudcrab
        if (s.find("mudcrab") != std::string::npos || s.find("crab") != std::string::npos) {
            return "mudcrab";
        }

        // Skeever / Rat / Mouse (avoid matching "rat" inside unrelated words like "crate", "pirate", "curate")
        auto hasStandaloneRat = [&]() -> bool {
            std::string stripped = s;
            if (stripped.ends_with("race")) {
                stripped.resize(stripped.size() - 4);
                stripped = StringUtils::TrimStd(stripped);
            }
            size_t pos = 0;
            while ((pos = stripped.find("rat", pos)) != std::string::npos) {
                const bool leftOk = (pos == 0) ||
                                    !std::isalpha(static_cast<unsigned char>(stripped[pos - 1])) ||
                                    (pos < trimmedRaw.size() && std::isupper(static_cast<unsigned char>(trimmedRaw[pos])));
                const size_t endPos = pos + 3;
                const bool rightOk = (endPos >= stripped.size()) ||
                                     !std::isalpha(static_cast<unsigned char>(stripped[endPos])) ||
                                     (endPos < trimmedRaw.size() && std::isupper(static_cast<unsigned char>(trimmedRaw[endPos])));
                if (leftOk && rightOk) {
                    return true;
                }
                pos += 3;
            }
            return false;
        };

        if (s.find("skeever") != std::string::npos || s.find("mouse") != std::string::npos || hasStandaloneRat()) {
            return "skeever";
        }

        return "";
    }

    std::vector<std::string> NameService::GetCritterNames(const std::string& raceName) const {
        std::shared_lock lock(mutex_);

        std::string trimmedRace = StringUtils::TrimStd(raceName);
        if (trimmedRace.empty() || trimmedRace == "any" || trimmedRace == "all" || trimmedRace == "none") {
            std::vector<std::string> allNames;
            for (const auto& [_, list] : critterNames_) {
                allNames.insert(allNames.end(), list.begin(), list.end());
            }
            return allNames;
        }

        std::string normRace = NormalizeCritterRaceKeyLocked(trimmedRace);
        if (!normRace.empty()) {
            auto it = critterNames_.find(normRace);
            if (it != critterNames_.end() && !it->second.empty()) {
                return it->second;
            }
        }

        // If unknown race, return all critter names
        std::vector<std::string> allNames;
        for (const auto& [_, list] : critterNames_) {
            allNames.insert(allNames.end(), list.begin(), list.end());
        }
        return allNames;
    }

    std::string NameService::GetRandomCritterName(const std::string& raceName) {
        if (!isCritterLoaded_.load(std::memory_order_acquire)) {
            LoadCritterNamesFromFile();
        }

        std::unique_lock lock(mutex_);
        std::vector<std::string> pool;
        if (!raceName.empty()) {
            std::string normRace = NormalizeCritterRaceKeyLocked(raceName);
            if (!normRace.empty()) {
                auto it = critterNames_.find(normRace);
                if (it != critterNames_.end() && !it->second.empty()) {
                    pool = it->second;
                }
            }
        }

        // If unknown race, empty raceName, or specific category was empty: choose random name across all critter categories
        if (pool.empty()) {
            for (const auto& [_, list] : critterNames_) {
                pool.insert(pool.end(), list.begin(), list.end());
            }
        }

        if (pool.empty()) {
            return "";
        }

        std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
        return pool[dist(rng_)];
    }

    std::string NameService::GetRandomCritterName(RE::Actor* actor) {
        if (!actor) {
            return GetRandomCritterName("");
        }

        std::string raceName;
        if (auto* r = actor->GetRace()) {
            if (const char* edid = r->GetFormEditorID(); edid && *edid) {
                raceName = edid;
            } else if (const char* name = r->GetFullName(); name && *name) {
                raceName = name;
            }
        }
        if (raceName.empty()) {
            if (auto* base = actor->GetActorBase()) {
                if (const char* edid = base->GetFormEditorID(); edid && *edid) {
                    raceName = edid;
                } else if (const char* name = base->GetFullName(); name && *name) {
                    raceName = name;
                }
            }
        }

        return GetRandomCritterName(raceName);
    }

    std::string NameService::GetRandomCritterName(RE::TESForm* raceForm) {
        if (!raceForm) {
            return GetRandomCritterName("");
        }

        if (auto* actor = raceForm->As<RE::Actor>()) {
            return GetRandomCritterName(actor);
        }

        std::string raceName;
        if (const char* edid = raceForm->GetFormEditorID(); edid && *edid) {
            raceName = edid;
        } else if (const char* name = raceForm->GetName(); name && *name) {
            raceName = name;
        }

        if (raceName.empty()) {
            if (auto* npc = raceForm->As<RE::TESNPC>()) {
                if (auto* r = npc->GetRace()) {
                    if (const char* redid = r->GetFormEditorID(); redid && *redid) {
                        raceName = redid;
                    } else if (const char* rname = r->GetFullName(); rname && *rname) {
                        raceName = rname;
                    }
                }
            }
        }

        return GetRandomCritterName(raceName);
    }

}  // namespace MARAS
