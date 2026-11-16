#include "maras/domain/lineage/NamePool.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <cctype>

namespace MARAS::Domain::Lineage {

    namespace {
        using Foundation::StringUtils::Contains;
        using Foundation::StringUtils::ToLowerStd;
        using Foundation::StringUtils::TrimStd;

        [[nodiscard]] bool ContainsStandaloneWord(std::string_view haystackLower, std::string_view wordLower) noexcept {
            std::size_t pos = 0;
            while ((pos = haystackLower.find(wordLower, pos)) != std::string_view::npos) {
                const bool leftBoundary =
                    (pos == 0) || !std::isalnum(static_cast<unsigned char>(haystackLower[pos - 1]));
                const std::size_t after = pos + wordLower.size();
                const bool rightBoundary =
                    (after >= haystackLower.size()) || !std::isalnum(static_cast<unsigned char>(haystackLower[after]));
                if (leftBoundary && rightBoundary) {
                    return true;
                }
                pos += wordLower.size();
            }
            return false;
        }
    }  // namespace

    std::string NormalizeRaceKey(std::string_view rawRace) {
        std::string key = ToLowerStd(TrimStd(rawRace));
        if (key.empty()) {
            return {};
        }

        for (std::string_view suffix : { std::string_view("racechild"), std::string_view("racevampire"), std::string_view("race") }) {
            if (key.size() > suffix.size() && key.ends_with(suffix)) {
                key.erase(key.size() - suffix.size());
                break;
            }
        }

        if (Contains(key, "argo") || Contains(key, "saxh")) return "argonian";
        if (Contains(key, "khaj")) return "khajiit";
        if (Contains(key, "orsi") || Contains(key, "orc")) return "orsimer";
        if (Contains(key, "altm") || Contains(key, "high")) return "altmer";
        if (Contains(key, "bosm") || Contains(key, "wood")) return "bosmer";
        if (Contains(key, "dunm") || Contains(key, "dark")) return "dunmer";
        if (Contains(key, "redg") || Contains(key, "yoku")) return "redguard";
        if (Contains(key, "bret")) return "breton";
        if (Contains(key, "imper") || Contains(key, "cyrod")) return "imperial";
        if (Contains(key, "nord") || Contains(key, "atmor")) return "nord";

        return key;
    }

    std::string NormalizeCritterRaceKey(std::string_view rawRace) {
        std::string lower = ToLowerStd(TrimStd(rawRace));
        if (lower.empty()) {
            return {};
        }
        if (Contains(lower, "arctic")) return "arctic_fox";
        if (Contains(lower, "fox")) return "fox";
        if (Contains(lower, "hare") || Contains(lower, "rabbit")) return "hare";
        if (Contains(lower, "mudcrab") || Contains(lower, "crab")) return "mudcrab";
        if (Contains(lower, "skeever") || ContainsStandaloneWord(lower, "rat")) return "skeever";
        return {};
    }

    void NamePool::SetChildNames(
        std::unordered_map<std::string, std::vector<std::string>> maleNamesByRace,
        std::unordered_map<std::string, std::vector<std::string>> femaleNamesByRace) {
        maleNames_.clear();
        femaleNames_.clear();

        auto ingest = [](const auto& src, auto& dst) {
            for (const auto& [rawKey, vec] : src) {
                const std::string key = NormalizeRaceKey(rawKey);
                if (key.empty()) {
                    continue;
                }
                auto& target = dst[key];
                for (const auto& name : vec) {
                    const std::string trimmed = TrimStd(name);
                    if (!trimmed.empty()) {
                        target.push_back(trimmed);
                    }
                }
            }
        };

        ingest(maleNamesByRace, maleNames_);
        ingest(femaleNamesByRace, femaleNames_);

        auto mirrorOrc = [](auto& map) {
            if (map.contains("orsimer") && !map.contains("orc")) {
                map["orc"] = map["orsimer"];
            } else if (map.contains("orc") && !map.contains("orsimer")) {
                map["orsimer"] = map["orc"];
            }
        };
        mirrorOrc(maleNames_);
        mirrorOrc(femaleNames_);

        childLoaded_ = (!maleNames_.empty() || !femaleNames_.empty());
    }

    void NamePool::OverlayChildNames(
        const std::unordered_map<std::string, std::vector<std::string>>& maleNamesByRace,
        const std::unordered_map<std::string, std::vector<std::string>>& femaleNamesByRace) {
        auto overlay = [](const auto& src, auto& dst) {
            for (const auto& [rawKey, vec] : src) {
                const std::string key = NormalizeRaceKey(rawKey);
                if (key.empty()) {
                    continue;
                }
                std::vector<std::string> cleaned;
                cleaned.reserve(vec.size());
                for (const auto& name : vec) {
                    const std::string trimmed = TrimStd(name);
                    if (!trimmed.empty()) {
                        cleaned.push_back(trimmed);
                    }
                }
                if (!cleaned.empty()) {
                    dst[key] = std::move(cleaned);
                }
            }
        };

        overlay(maleNamesByRace, maleNames_);
        overlay(femaleNamesByRace, femaleNames_);

        if (maleNamesByRace.contains("orsimer") || maleNamesByRace.contains("orc")) {
            if (maleNames_.contains("orsimer")) {
                maleNames_["orc"] = maleNames_["orsimer"];
            }
        }
        if (femaleNamesByRace.contains("orsimer") || femaleNamesByRace.contains("orc")) {
            if (femaleNames_.contains("orsimer")) {
                femaleNames_["orc"] = femaleNames_["orsimer"];
            }
        }

        childLoaded_ = (!maleNames_.empty() || !femaleNames_.empty());
    }

    void NamePool::SetCritterNames(
        std::unordered_map<std::string, std::vector<std::string>> critterNamesByRace) {
        critterNames_.clear();
        OverlayCritterNames(critterNamesByRace);
    }

    void NamePool::OverlayCritterNames(
        const std::unordered_map<std::string, std::vector<std::string>>& critterNamesByRace) {
        for (const auto& [rawKey, vec] : critterNamesByRace) {
            std::string key = NormalizeCritterRaceKey(rawKey);
            if (key.empty()) {
                key = ToLowerStd(TrimStd(rawKey));
            }
            if (key.empty()) {
                continue;
            }
            std::vector<std::string> cleaned;
            cleaned.reserve(vec.size());
            for (const auto& name : vec) {
                const std::string trimmed = TrimStd(name);
                if (!trimmed.empty()) {
                    cleaned.push_back(trimmed);
                }
            }
            if (!cleaned.empty()) {
                critterNames_[key] = std::move(cleaned);
            }
        }
        critterLoaded_ = !critterNames_.empty();
    }

    void NamePool::Clear() noexcept {
        maleNames_.clear();
        femaleNames_.clear();
        critterNames_.clear();
        childLoaded_ = false;
        critterLoaded_ = false;
    }

    std::vector<std::string> NamePool::GetChildNames(std::string_view raceName, bool isFemale) const {
        const auto& map = isFemale ? femaleNames_ : maleNames_;
        const std::string key = NormalizeRaceKey(raceName);
        if (!key.empty()) {
            if (const auto it = map.find(key); it != map.end()) {
                return it->second;
            }
            return {};
        }

        std::vector<std::string> all;
        for (const auto& [k, vec] : map) {
            if (k == "orc" && map.contains("orsimer")) {
                continue;
            }
            all.insert(all.end(), vec.begin(), vec.end());
        }
        return all;
    }

    std::string NamePool::GetRandomChildName(
        std::string_view raceName,
        bool isFemale,
        const std::unordered_set<std::string>& existingLowerNames,
        bool requireUnique,
        Foundation::IRng& rng) const {
        if (!childLoaded_) {
            return {};
        }

        std::vector<std::string> pool = GetChildNames(raceName, isFemale);
        if (pool.empty() && !raceName.empty()) {
            pool = GetChildNames({}, isFemale);
        }
        if (pool.empty()) {
            return {};
        }

        if (!requireUnique || existingLowerNames.empty()) {
            const auto idx = static_cast<std::size_t>(
                rng.NextInt(0, static_cast<std::int32_t>(pool.size() - 1)));
            return pool[idx];
        }

        std::vector<std::string> available;
        available.reserve(pool.size());
        for (const auto& n : pool) {
            if (!existingLowerNames.contains(ToLowerStd(n))) {
                available.push_back(n);
            }
        }
        if (available.empty()) {
            available = std::move(pool);
        }
        const auto idx = static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(available.size() - 1)));
        return available[idx];
    }

    std::vector<std::string> NamePool::GetCritterNames(std::string_view raceName) const {
        const std::string key = NormalizeCritterRaceKey(raceName);
        if (!key.empty()) {
            if (const auto it = critterNames_.find(key); it != critterNames_.end()) {
                return it->second;
            }
        }

        std::vector<std::string> all;
        for (const auto& [k, vec] : critterNames_) {
            (void)k;
            all.insert(all.end(), vec.begin(), vec.end());
        }
        return all;
    }

    std::string NamePool::GetRandomCritterName(
        std::string_view raceName,
        Foundation::IRng& rng) const {
        if (!critterLoaded_) {
            return {};
        }
        const std::vector<std::string> pool = GetCritterNames(raceName);
        if (pool.empty()) {
            return {};
        }
        const auto idx = static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(pool.size() - 1)));
        return pool[idx];
    }

    std::unordered_set<std::string> NamePool::CollectExistingChildNamesLower(
        const State::LineageTable& lineage) {
        std::unordered_set<std::string> names;
        for (const auto& [cid, rec] : lineage.AllChildren()) {
            (void)cid;
            if (!rec.name.empty()) {
                names.insert(ToLowerStd(rec.name));
            }
        }
        return names;
    }

}  // namespace MARAS::Domain::Lineage
