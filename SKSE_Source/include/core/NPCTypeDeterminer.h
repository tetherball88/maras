#pragma once

#include <functional>
#include <optional>

#include "RE/Skyrim.h"
#include "core/NPCRelationshipManager.h"  // for enum types

namespace MARAS {

    // A stateless helper that encapsulates NPC attribute determination logic.
    // It performs read-only queries against game forms and FormCache and returns
    // computed enum values. All persistence remains in NPCRelationshipManager.
    class NPCTypeDeterminer {
    public:
        // Determine SocialClass for an NPC. If an override provider returns a string,
        // that value is used; otherwise faction-based detection is used.
        static SocialClass DetermineSocialClass(
            RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getSocialClassOverride);

        // Determine SkillType for an NPC. If an override provider returns a string,
        // that value is used; otherwise skill-based detection is used.
        static SkillType DetermineSkillType(
            RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getSkillTypeOverride);

        // Determine Temperament for an NPC. If an override provider returns a string,
        // that value is used; otherwise the temperament matrix is applied using the
        // provided social class and skill type providers (typically from the manager).
        static Temperament DetermineTemperament(
            RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getTemperamentOverride,
            const std::function<SocialClass(RE::FormID)>& getSocialClass,
            const std::function<SkillType(RE::FormID)>& getSkillType);

        // Pure function to compute temperament from SocialClass and SkillType directly.
        static Temperament ComputeTemperament(SocialClass socialClass, SkillType skillType);

    private:
        // Helper methods used internally by the determiner
        static SocialClass DetermineSocialClassByFaction(RE::FormID npcFormID);
        static std::optional<SkillType> DetermineSkillTypeByClass(RE::FormID npcFormID);
        static SkillType DetermineSkillTypeBySkills(RE::FormID npcFormID);
    };

}  // namespace MARAS
