#pragma once

#include "maras/foundation/IRng.h"
#include "maras/state/WorldTables.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MARAS::Domain::Lineage {

    [[nodiscard]] std::string NormalizeRaceKey(std::string_view rawRace);
    [[nodiscard]] std::string NormalizeCritterRaceKey(std::string_view rawRace);

    class NamePool {
    public:
        void SetChildNames(
            std::unordered_map<std::string, std::vector<std::string>> maleNamesByRace,
            std::unordered_map<std::string, std::vector<std::string>> femaleNamesByRace);

        void SetCritterNames(
            std::unordered_map<std::string, std::vector<std::string>> critterNamesByRace);

        void Clear() noexcept;

        [[nodiscard]] bool IsChildLoaded() const noexcept { return childLoaded_; }
        [[nodiscard]] bool IsCritterLoaded() const noexcept { return critterLoaded_; }
        [[nodiscard]] bool IsLoaded() const noexcept { return childLoaded_ || critterLoaded_; }

        [[nodiscard]] std::vector<std::string> GetChildNames(
            std::string_view raceName,
            bool isFemale) const;

        [[nodiscard]] std::string GetRandomChildName(
            std::string_view raceName,
            bool isFemale,
            const std::unordered_set<std::string>& existingLowerNames,
            bool requireUnique,
            Foundation::IRng& rng) const;

        [[nodiscard]] std::vector<std::string> GetCritterNames(std::string_view raceName) const;

        [[nodiscard]] std::string GetRandomCritterName(
            std::string_view raceName,
            Foundation::IRng& rng) const;

        [[nodiscard]] static std::unordered_set<std::string> CollectExistingChildNamesLower(
            const State::LineageTable& lineage);

    private:
        std::unordered_map<std::string, std::vector<std::string>> maleNames_{};
        std::unordered_map<std::string, std::vector<std::string>> femaleNames_{};
        std::unordered_map<std::string, std::vector<std::string>> critterNames_{};
        bool childLoaded_{ false };
        bool critterLoaded_{ false };
    };

}  // namespace MARAS::Domain::Lineage
