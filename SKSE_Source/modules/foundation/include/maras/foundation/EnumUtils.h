#pragma once

#include "maras/foundation/DomainEnums.h"

#include <string_view>

namespace MARAS::Foundation {

    [[nodiscard]] constexpr std::string_view SocialClassToString(SocialClass socialClass) noexcept {
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

    [[nodiscard]] constexpr std::string_view SkillTypeToString(SkillType skillType) noexcept {
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

    [[nodiscard]] constexpr std::string_view TemperamentToString(Temperament temperament) noexcept {
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

    [[nodiscard]] constexpr std::string_view RelationshipStatusToString(RelationshipStatus status) noexcept {
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

    [[nodiscard]] constexpr std::string_view AffairTypeToString(AffairType affairType) noexcept {
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

    [[nodiscard]] constexpr std::string_view AffairStatusToString(AffairStatus affairStatus) noexcept {
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

    [[nodiscard]] constexpr std::string_view AffairDiscoveryToString(AffairDiscovery disc) noexcept {
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

    [[nodiscard]] constexpr std::string_view AffairResolutionToString(AffairResolution res) noexcept {
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

    [[nodiscard]] constexpr std::string_view AffairStageToString(AffairStage stage) noexcept {
        switch (stage) {
            case AffairStage::Talking:
                return "talking";
            case AffairStage::Physical:
            default:
                return "physical";
        }
    }

    [[nodiscard]] constexpr std::string_view PartnerIntentTypeToString(PartnerIntentType type) noexcept {
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

    [[nodiscard]] SocialClass StringToSocialClass(std::string_view str);
    [[nodiscard]] SkillType StringToSkillType(std::string_view str);
    [[nodiscard]] Temperament StringToTemperament(std::string_view str);
    [[nodiscard]] RelationshipStatus StringToRelationshipStatus(std::string_view str);
    [[nodiscard]] AffairType StringToAffairType(std::string_view str);
    [[nodiscard]] AffairStatus StringToAffairStatus(std::string_view str);
    [[nodiscard]] AffairDiscovery StringToAffairDiscovery(std::string_view str);
    [[nodiscard]] AffairResolution StringToAffairResolution(std::string_view str);
    [[nodiscard]] AffairStage StringToAffairStage(std::string_view str);
    [[nodiscard]] PartnerIntentType StringToPartnerIntentType(std::string_view str);

}  // namespace MARAS::Foundation
