#pragma once

#include "maras/foundation/DomainEnums.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MARAS::Domain::Lineage {

    struct ChildProfileGuidanceInput {
        Foundation::ChildStage stage{ Foundation::ChildStage::Child };
        std::string childName{};
        std::string raceName{};
        bool isFemale{ false };
        bool isAdopted{ false };
        bool isBiologicalChild{ false };
        bool hasBeastblood{ false };
        bool trainedInWerewolfPack{ false };

        std::string motherName{};
        std::string motherRace{};
        bool motherIsGuardian{ false };

        std::string bioFatherName{};
        std::string bioFatherRace{};
        bool bioFatherIsGuardian{ false };

        std::string presumedFatherName{};
        std::string presumedFatherRace{};
        bool presumedFatherIsGuardian{ false };
        std::string motherPresumedRelStatus{};

        std::string guardianAName{};
        std::string guardianBName{};

        bool hasPresumedFather{ false };
        bool hasPaternitySecret{ false };
        bool isSingleMotherSecretFromBioFather{ false };
        bool partnerKnowsPaternity{ true };
        bool bioFatherKnowsPaternity{ true };
        bool hasPhenotypeMismatch{ false };
        std::string visualDiscrepancyDescription{};

        std::vector<std::string> siblingNames{};
        std::string homeLocationName{};
        std::string trainingLocation{};
        std::string trainingClassName{};
        std::string trainingDescription{};
        bool hasInheritedChildhoodIdentity{ false };
    };

    struct ChildSeedMemorySpec {
        bool shouldCreate{ false };
        std::string content{};
        float importance{ 0.75f };
        std::string memoryType{ "EXPERIENCE" };
        std::string emotion{ "content" };
        std::string location{};
        std::vector<std::string> tags{};
    };

    [[nodiscard]] std::string JoinNamesForProse(const std::vector<std::string>& names);

    [[nodiscard]] std::string ComputeBioFatherRoleLabel(
        bool bioFatherIsGuardian,
        bool hasPaternitySecret,
        bool isSingleMotherSecretFromBioFather,
        bool partnerKnowsPaternity,
        bool bioFatherKnowsPaternity);

    [[nodiscard]] std::string ComputePresumedFatherRoleLabel(
        bool presumedFatherIsGuardian,
        bool partnerKnowsPaternity,
        bool bioFatherKnowsPaternity);

    [[nodiscard]] std::int64_t ComputeChildRecentEventsMaxAgeSeconds(
        bool isBiologicalChild,
        bool isAdopted,
        float birthTimestampDays,
        std::int32_t registeredTimestampDays,
        float currentGameDays) noexcept;

    [[nodiscard]] std::string BuildChildProfileGenerationGuidance(const ChildProfileGuidanceInput& d);

    [[nodiscard]] ChildSeedMemorySpec BuildChildStageSeedMemory(const ChildProfileGuidanceInput& d);

}  // namespace MARAS::Domain::Lineage
