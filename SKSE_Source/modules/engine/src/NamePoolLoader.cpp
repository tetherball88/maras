#include "maras/engine/NamePoolLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 2> kChildNamePaths = {
            "Data/SKSE/Plugins/MARAS/childrenNames.json",
            "SKSE/Plugins/MARAS/childrenNames.json"
        };

        constexpr std::array<const char*, 2> kCritterNamePaths = {
            "Data/SKSE/Plugins/MARAS/critterNames.json",
            "SKSE/Plugins/MARAS/critterNames.json"
        };

        class MtRngAdapter final : public MARAS::Foundation::IRng {
        public:
            explicit MtRngAdapter(std::mt19937_64& engine) noexcept : engine_(engine) {}

            float NextFloat(float minVal = 0.0f, float maxVal = 1.0f) override {
                if (minVal > maxVal) {
                    std::swap(minVal, maxVal);
                }
                std::uniform_real_distribution<float> dist(minVal, maxVal);
                return dist(engine_);
            }

            std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) override {
                if (minVal > maxVal) {
                    std::swap(minVal, maxVal);
                }
                std::uniform_int_distribution<std::int32_t> dist(minVal, maxVal);
                return dist(engine_);
            }

        private:
            std::mt19937_64& engine_;
        };

        template <std::size_t N>
        [[nodiscard]] std::filesystem::path ResolveFirstExistingPath(
            const std::filesystem::path& customPath,
            const std::array<const char*, N>& defaults) {
            if (!customPath.empty()) {
                return customPath;
            }
            std::error_code ec;
            for (const char* candidate : defaults) {
                const std::filesystem::path path(candidate);
                if (std::filesystem::exists(path, ec)) {
                    return path;
                }
            }
            return {};
        }

        [[nodiscard]] const nlohmann::json& UnwrapNamesObject(const nlohmann::json& root) noexcept {
            if (root.contains("names") && root["names"].is_object()) {
                return root["names"];
            }
            return root;
        }

        [[nodiscard]] std::vector<std::string> ParseStringArray(const nlohmann::json& arr) {
            std::vector<std::string> out;
            if (!arr.is_array()) {
                return out;
            }
            out.reserve(arr.size());
            for (const auto& entry : arr) {
                if (!entry.is_string()) {
                    continue;
                }
                std::string name = MARAS::Foundation::StringUtils::TrimStd(entry.get<std::string>());
                if (!name.empty()) {
                    out.push_back(std::move(name));
                }
            }
            return out;
        }

        [[nodiscard]] std::string ExtractRaceLabel(RE::TESRace* race) {
            if (!race) {
                return {};
            }
            if (const char* editorId = race->GetFormEditorID(); editorId && *editorId != '\0') {
                return editorId;
            }
            if (const char* fullName = race->GetFullName(); fullName && *fullName != '\0') {
                return fullName;
            }
            return {};
        }

        [[nodiscard]] std::string ExtractActorRaceLabel(RE::Actor* actor) {
            if (!actor) {
                return {};
            }
            if (std::string label = ExtractRaceLabel(actor->GetRace()); !label.empty()) {
                return label;
            }
            if (auto* base = actor->GetActorBase()) {
                if (std::string label = ExtractRaceLabel(base->GetRace()); !label.empty()) {
                    return label;
                }
                if (const char* editorId = base->GetFormEditorID(); editorId && *editorId != '\0') {
                    return editorId;
                }
                if (const char* fullName = base->GetFullName(); fullName && *fullName != '\0') {
                    return fullName;
                }
            }
            return {};
        }

        [[nodiscard]] std::string ExtractFormRaceLabel(RE::TESForm* form) {
            if (!form) {
                return {};
            }
            if (auto* race = form->As<RE::TESRace>()) {
                return ExtractRaceLabel(race);
            }
            if (auto* actor = form->As<RE::Actor>()) {
                return ExtractActorRaceLabel(actor);
            }
            if (auto* npc = form->As<RE::TESNPC>()) {
                if (std::string label = ExtractRaceLabel(npc->GetRace()); !label.empty()) {
                    return label;
                }
                if (const char* editorId = npc->GetFormEditorID(); editorId && *editorId != '\0') {
                    return editorId;
                }
                if (const char* fullName = npc->GetFullName(); fullName && *fullName != '\0') {
                    return fullName;
                }
            }
            if (auto* ref = form->As<RE::TESObjectREFR>()) {
                return ExtractFormRaceLabel(ref->GetBaseObject());
            }
            if (const char* editorId = form->GetFormEditorID(); editorId && *editorId != '\0') {
                return editorId;
            }
            return {};
        }

    }  // namespace

    NamePoolLoader::NamePoolLoader() : rng_(std::random_device{}()) {}

    NamePoolLoader& NamePoolLoader::GetSingleton() {
        static NamePoolLoader instance;
        return instance;
    }

    bool NamePoolLoader::Initialize(const std::filesystem::path& childNamesPath,
                                    const std::filesystem::path& critterNamesPath) {
        const bool childOk = LoadChildNamesFromFile(childNamesPath);
        const bool critterOk = LoadCritterNamesFromFile(critterNamesPath);
        return childOk || critterOk;
    }

    bool NamePoolLoader::LoadChildNamesFromFile(const std::filesystem::path& customPath) {
        const std::filesystem::path path = ResolveFirstExistingPath(customPath, kChildNamePaths);
        if (path.empty()) {
            MARAS_LOG_WARN("[NamePoolLoader] childrenNames.json not found in default search paths");
            return false;
        }

        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(path, root, "NamePoolLoader") || !root.is_object()) {
            return false;
        }

        const auto& container = UnwrapNamesObject(root);
        std::unordered_map<std::string, std::vector<std::string>> maleMap;
        std::unordered_map<std::string, std::vector<std::string>> femaleMap;

        for (const auto& [raceKey, raceVal] : container.items()) {
            if (!raceVal.is_object()) {
                continue;
            }
            if (raceVal.contains("male")) {
                maleMap.emplace(raceKey, ParseStringArray(raceVal["male"]));
            }
            if (raceVal.contains("female")) {
                femaleMap.emplace(raceKey, ParseStringArray(raceVal["female"]));
            }
        }

        {
            std::unique_lock lock(poolMutex_);
            pool_.SetChildNames(std::move(maleMap), std::move(femaleMap));
        }

        MARAS_LOG_INFO("[NamePoolLoader] Loaded child name pool from '{}'", path.string());
        return true;
    }

    bool NamePoolLoader::LoadCritterNamesFromFile(const std::filesystem::path& customPath) {
        const std::filesystem::path path = ResolveFirstExistingPath(customPath, kCritterNamePaths);
        if (path.empty()) {
            MARAS_LOG_WARN("[NamePoolLoader] critterNames.json not found in default search paths");
            return false;
        }

        nlohmann::json root;
        if (!JsonOverrideLoader::LoadJsonFile(path, root, "NamePoolLoader") || !root.is_object()) {
            return false;
        }

        const auto& container = UnwrapNamesObject(root);
        std::unordered_map<std::string, std::vector<std::string>> critterMap;
        for (const auto& [categoryKey, listVal] : container.items()) {
            if (!listVal.is_array()) {
                continue;
            }
            critterMap.emplace(categoryKey, ParseStringArray(listVal));
        }

        {
            std::unique_lock lock(poolMutex_);
            pool_.SetCritterNames(std::move(critterMap));
        }

        MARAS_LOG_INFO("[NamePoolLoader] Loaded critter name pool from '{}'", path.string());
        return true;
    }

    void NamePoolLoader::Clear() noexcept {
        std::unique_lock lock(poolMutex_);
        pool_.Clear();
    }

    bool NamePoolLoader::IsLoaded() const noexcept {
        std::shared_lock lock(poolMutex_);
        return pool_.IsLoaded();
    }

    bool NamePoolLoader::IsChildLoaded() const noexcept {
        std::shared_lock lock(poolMutex_);
        return pool_.IsChildLoaded();
    }

    bool NamePoolLoader::IsCritterLoaded() const noexcept {
        std::shared_lock lock(poolMutex_);
        return pool_.IsCritterLoaded();
    }

    std::vector<std::string> NamePoolLoader::GetChildNames(std::string_view raceName, bool isFemale) const {
        std::shared_lock lock(poolMutex_);
        return pool_.GetChildNames(raceName, isFemale);
    }

    std::vector<std::string> NamePoolLoader::GetChildNames(RE::Actor* actor, bool isFemale) const {
        return GetChildNames(ExtractActorRaceLabel(actor), isFemale);
    }

    std::vector<std::string> NamePoolLoader::GetCritterNames(std::string_view raceName) const {
        std::shared_lock lock(poolMutex_);
        return pool_.GetCritterNames(raceName);
    }

    std::string NamePoolLoader::GetRandomChildName(std::string_view raceName, bool isFemale, bool isUnique) const {
        std::unordered_set<std::string> existingLower;
        if (isUnique) {
            if (const auto snapshot = MARAS::State::WorldStore::GetSingleton().Snapshot()) {
                if (snapshot && snapshot->lineage) {
                    existingLower =
                        MARAS::Domain::Lineage::NamePool::CollectExistingChildNamesLower(*snapshot->lineage);
                }
            }
        }

        std::shared_lock poolLock(poolMutex_);
        std::scoped_lock rngLock(rngMutex_);
        MtRngAdapter rngAdapter(rng_);
        return pool_.GetRandomChildName(raceName, isFemale, existingLower, isUnique, rngAdapter);
    }

    std::string NamePoolLoader::GetRandomChildName(RE::Actor* actor, bool isFemale, bool isUnique) const {
        return GetRandomChildName(ExtractActorRaceLabel(actor), isFemale, isUnique);
    }

    std::string NamePoolLoader::GetRandomCritterName(std::string_view raceName) const {
        std::shared_lock poolLock(poolMutex_);
        std::scoped_lock rngLock(rngMutex_);
        MtRngAdapter rngAdapter(rng_);
        return pool_.GetRandomCritterName(raceName, rngAdapter);
    }

    std::string NamePoolLoader::GetRandomCritterName(RE::TESForm* critterOrRace) const {
        return GetRandomCritterName(ExtractFormRaceLabel(critterOrRace));
    }

}  // namespace MARAS::Engine
