#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/DomainTypes.h"

#include <functional>
#include <optional>
#include <string>

namespace MARAS {

    /**
     * Stateless helper that determines NPC SocialClass, SkillType, and Temperament
     * from Skyrim forms, FormCache lists, and overrides, delegating pure calculations
     * to Domain::Relationships.
     */
    class NPCTypeDeterminer {
    public:
        static State::SocialClass DetermineSocialClass(
            RE::FormID npcFormID,
            const std::function<std::optional<std::string>(RE::FormID)>& getSocialClassOverride);

        static State::SkillType DetermineSkillType(
            RE::FormID npcFormID,
            const std::function<std::optional<std::string>(RE::FormID)>& getSkillTypeOverride);

        static State::Temperament DetermineTemperament(
            RE::FormID npcFormID,
            const std::function<std::optional<std::string>(RE::FormID)>& getTemperamentOverride,
            const std::function<State::SocialClass(RE::FormID)>& getSocialClass,
            const std::function<State::SkillType(RE::FormID)>& getSkillType);

        [[nodiscard]] static State::Temperament ComputeTemperament(
            State::SocialClass socialClass,
            State::SkillType skillType);

    private:
        static State::SocialClass DetermineSocialClassByFaction(RE::FormID npcFormID);
        static std::optional<State::SkillType> DetermineSkillTypeByClass(RE::FormID npcFormID);
        static State::SkillType DetermineSkillTypeBySkills(RE::FormID npcFormID);
    };

}  // namespace MARAS
