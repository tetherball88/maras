#pragma once

#include "core/RelationshipTypes.h"
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
            default:
                break;
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
            default:
                break;
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
            default:
                break;
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
            case RelationshipStatus::Dating:
                return "Dating";
            case RelationshipStatus::Lover:
                return "Lover";
            case RelationshipStatus::ExDating:
                return "ExDating";
            case RelationshipStatus::ExLover:
                return "ExLover";
            default:
                return "Unknown";
        }
    }

    constexpr std::string_view AffairTypeToString(AffairType affairType) {
        switch (affairType) {
            case AffairType::Casual:
                return "Casual";
            case AffairType::Dating:
                return "Dating";
            case AffairType::Lover:
                return "Lover";
            default:
                return "Unknown";
        }
    }

    constexpr std::string_view AffairStatusToString(AffairStatus affairStatus) {
        switch (affairStatus) {
            case AffairStatus::Ongoing:
                return "Ongoing";
            case AffairStatus::EndedBreakup:
                return "EndedBreakup";
            case AffairStatus::EndedVictimSplit:
                return "EndedVictimSplit";
            case AffairStatus::Allowed:
                return "Allowed";
            case AffairStatus::EndedConfronted:
                return "EndedConfronted";
            default:
                return "Unknown";
        }
    }

    constexpr std::string_view AffairDiscoveryToString(AffairDiscovery disc) {
        switch (disc) {
            case AffairDiscovery::Confessed:
                return "confessed";
            case AffairDiscovery::Exposed:
                return "exposed";
            case AffairDiscovery::Caught:
                return "caught";
            default:
                return "";
        }
    }

    constexpr std::string_view AffairResolutionToString(AffairResolution res) {
        switch (res) {
            case AffairResolution::Confronted:
                return "confronted";
            case AffairResolution::Allowed:
                return "allowed";
            case AffairResolution::Breakup:
                return "breakup";
            default:
                return "";
        }
    }

    constexpr std::string_view AffairStageToString(AffairStage stage) {
        switch (stage) {
            case AffairStage::Talking:
                return "talking";
            case AffairStage::Physical:
            default:
                return "physical";
        }
    }

    constexpr std::string_view PartnerIntentTypeToString(PartnerIntentType type) {
        switch (type) {
            case PartnerIntentType::SuggestOpen:
                return "suggest_open";
            case PartnerIntentType::SuggestClose:
                return "suggest_close";
            case PartnerIntentType::ConfessTalking:
                return "confess_talking";
            case PartnerIntentType::ConfessPhysical:
                return "confess_physical";
            case PartnerIntentType::None:
            default:
                return "none";
        }
    }

    // String to enum conversions (case-insensitive, zero heap allocations)

    SocialClass StringToSocialClass(std::string_view str);
    SkillType StringToSkillType(std::string_view str);
    Temperament StringToTemperament(std::string_view str);
    RelationshipStatus StringToRelationshipStatus(std::string_view str);
    AffairType StringToAffairType(std::string_view str);
    AffairStatus StringToAffairStatus(std::string_view str);
    AffairDiscovery StringToAffairDiscovery(std::string_view str);
    AffairResolution StringToAffairResolution(std::string_view str);
    AffairStage StringToAffairStage(std::string_view str);
    PartnerIntentType StringToPartnerIntentType(std::string_view str);

}  // namespace MARAS::Utils
