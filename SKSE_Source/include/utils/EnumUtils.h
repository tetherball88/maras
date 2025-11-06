#pragma once

#include <string_view>

#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"

namespace MARAS::Utils {

    // Enum to string conversions for better logging and debugging

    constexpr std::string_view SocialClassToString(SocialClass socialClass) {
        switch (socialClass) {
            case SocialClass::Outcast:
                return "Outcast";
            case SocialClass::Poverty:
                return "Poverty";
            case SocialClass::Working:
                return "Working";
            case SocialClass::Middle:
                return "Middle";
            case SocialClass::Wealthy:
                return "Wealthy";
            case SocialClass::Religious:
                return "Religious";
            case SocialClass::Nobles:
                return "Nobles";
            case SocialClass::Rulers:
                return "Rulers";
        }
        return "Unknown";
    }

    constexpr std::string_view SkillTypeToString(SkillType skillType) {
        switch (skillType) {
            case SkillType::Warrior:
                return "Warrior";
            case SkillType::Mage:
                return "Mage";
            case SkillType::Rogue:
                return "Rogue";
            case SkillType::Craftsman:
                return "Craftsman";
            case SkillType::Ranger:
                return "Ranger";
            case SkillType::Orator:
                return "Orator";
        }
        return "Unknown";
    }

    constexpr std::string_view TemperamentToString(Temperament temperament) {
        switch (temperament) {
            case Temperament::Proud:
                return "Proud";
            case Temperament::Humble:
                return "Humble";
            case Temperament::Jealous:
                return "Jealous";
            case Temperament::Romantic:
                return "Romantic";
            case Temperament::Independent:
                return "Independent";
        }
        return "Unknown";
    }

    constexpr std::string_view RelationshipStatusToString(RelationshipStatus status) {
        switch (status) {
            case RelationshipStatus::Candidate:
                return "Candidate";
            case RelationshipStatus::Engaged:
                return "Engaged";
            case RelationshipStatus::Married:
                return "Married";
            case RelationshipStatus::Divorced:
                return "Divorced";
            case RelationshipStatus::Jilted:
                return "Jilted";
            case RelationshipStatus::Deceased:
                return "Deceased";
            default:
                return "Unknown";
        }
    }

    // String to enum conversions (for potential config file loading)

    SocialClass StringToSocialClass(std::string_view str);
    SkillType StringToSkillType(std::string_view str);
    Temperament StringToTemperament(std::string_view str);
    RelationshipStatus StringToRelationshipStatus(std::string_view str);

    // Utility functions

    // Get current game day from Skyrim
    GameDay GetCurrentGameDay();

    // Check if an NPC is valid and loaded
    bool IsValidNPC(FormID npcFormID);

    // Get NPC name for logging
    std::string GetNPCName(FormID npcFormID);

}  // namespace MARAS::Utils
