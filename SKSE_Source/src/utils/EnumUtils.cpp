#include "utils/EnumUtils.h"

#include "utils/StringUtils.h"

namespace MARAS::Utils {

    SocialClass StringToSocialClass(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "outcast")) return SocialClass::Outcast;
        if (StringUtils::CaseInsensitiveEquals(str, "poverty")) return SocialClass::Poverty;
        if (StringUtils::CaseInsensitiveEquals(str, "working")) return SocialClass::Working;
        if (StringUtils::CaseInsensitiveEquals(str, "middle")) return SocialClass::Middle;
        if (StringUtils::CaseInsensitiveEquals(str, "wealthy")) return SocialClass::Wealthy;
        if (StringUtils::CaseInsensitiveEquals(str, "religious")) return SocialClass::Religious;
        if (StringUtils::CaseInsensitiveEquals(str, "nobles")) return SocialClass::Nobles;
        if (StringUtils::CaseInsensitiveEquals(str, "rulers")) return SocialClass::Rulers;

        if (!str.empty()) {
            MARAS_LOG_WARN("StringToSocialClass: Unrecognized social class '{}', defaulting to Working", str);
        }
        return SocialClass::Working;
    }

    SkillType StringToSkillType(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "warrior")) return SkillType::Warrior;
        if (StringUtils::CaseInsensitiveEquals(str, "mage")) return SkillType::Mage;
        if (StringUtils::CaseInsensitiveEquals(str, "rogue")) return SkillType::Rogue;
        if (StringUtils::CaseInsensitiveEquals(str, "craftsman")) return SkillType::Craftsman;
        if (StringUtils::CaseInsensitiveEquals(str, "ranger")) return SkillType::Ranger;
        if (StringUtils::CaseInsensitiveEquals(str, "orator")) return SkillType::Orator;

        if (!str.empty()) {
            MARAS_LOG_WARN("StringToSkillType: Unrecognized skill type '{}', defaulting to Warrior", str);
        }
        return SkillType::Warrior;
    }

    Temperament StringToTemperament(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "proud")) return Temperament::Proud;
        if (StringUtils::CaseInsensitiveEquals(str, "humble")) return Temperament::Humble;
        if (StringUtils::CaseInsensitiveEquals(str, "jealous")) return Temperament::Jealous;
        if (StringUtils::CaseInsensitiveEquals(str, "romantic")) return Temperament::Romantic;
        if (StringUtils::CaseInsensitiveEquals(str, "independent")) return Temperament::Independent;

        if (!str.empty()) {
            MARAS_LOG_WARN("StringToTemperament: Unrecognized temperament '{}', defaulting to Independent", str);
        }
        return Temperament::Independent;
    }

    RelationshipStatus StringToRelationshipStatus(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "candidate")) return RelationshipStatus::Candidate;
        if (StringUtils::CaseInsensitiveEquals(str, "engaged")) return RelationshipStatus::Engaged;
        if (StringUtils::CaseInsensitiveEquals(str, "married")) return RelationshipStatus::Married;
        if (StringUtils::CaseInsensitiveEquals(str, "divorced")) return RelationshipStatus::Divorced;
        if (StringUtils::CaseInsensitiveEquals(str, "jilted")) return RelationshipStatus::Jilted;
        if (StringUtils::CaseInsensitiveEquals(str, "dating")) return RelationshipStatus::Dating;
        if (StringUtils::CaseInsensitiveEquals(str, "lover")) return RelationshipStatus::Lover;
        if (StringUtils::CaseInsensitiveEquals(str, "exdating") || StringUtils::CaseInsensitiveEquals(str, "ex_dating")) {
            return RelationshipStatus::ExDating;
        }
        if (StringUtils::CaseInsensitiveEquals(str, "exlover") || StringUtils::CaseInsensitiveEquals(str, "ex_lover")) {
            return RelationshipStatus::ExLover;
        }

        if (!str.empty() && !StringUtils::CaseInsensitiveEquals(str, "unknown") &&
            !StringUtils::CaseInsensitiveEquals(str, "none")) {
            MARAS_LOG_WARN("StringToRelationshipStatus: Unrecognized status '{}', returning Unknown", str);
        }
        return RelationshipStatus::Unknown;
    }

    AffairType StringToAffairType(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "casual")) return AffairType::Casual;
        if (StringUtils::CaseInsensitiveEquals(str, "dating")) return AffairType::Dating;
        if (StringUtils::CaseInsensitiveEquals(str, "lover")) return AffairType::Lover;

        if (!str.empty()) {
            MARAS_LOG_WARN("StringToAffairType: Unrecognized affair type '{}', defaulting to Casual", str);
        }
        return AffairType::Casual;
    }

    AffairStatus StringToAffairStatus(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "ongoing")) return AffairStatus::Ongoing;
        if (StringUtils::CaseInsensitiveEquals(str, "endedbreakup") ||
            StringUtils::CaseInsensitiveEquals(str, "ended_breakup")) {
            return AffairStatus::EndedBreakup;
        }
        if (StringUtils::CaseInsensitiveEquals(str, "endedvictimsplit") ||
            StringUtils::CaseInsensitiveEquals(str, "ended_victim_split")) {
            return AffairStatus::EndedVictimSplit;
        }
        if (StringUtils::CaseInsensitiveEquals(str, "allowed")) return AffairStatus::Allowed;
        if (StringUtils::CaseInsensitiveEquals(str, "endedconfronted") ||
            StringUtils::CaseInsensitiveEquals(str, "ended_confronted")) {
            return AffairStatus::EndedConfronted;
        }

        if (!str.empty()) {
            MARAS_LOG_WARN("StringToAffairStatus: Unrecognized affair status '{}', defaulting to Ongoing", str);
        }
        return AffairStatus::Ongoing;
    }

    AffairDiscovery StringToAffairDiscovery(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "confessed")) return AffairDiscovery::Confessed;
        if (StringUtils::CaseInsensitiveEquals(str, "exposed")) return AffairDiscovery::Exposed;
        if (StringUtils::CaseInsensitiveEquals(str, "caught")) return AffairDiscovery::Caught;
        return AffairDiscovery::None;
    }

    AffairResolution StringToAffairResolution(std::string_view str) {
        if (StringUtils::CaseInsensitiveEquals(str, "confronted")) return AffairResolution::Confronted;
        if (StringUtils::CaseInsensitiveEquals(str, "allowed")) return AffairResolution::Allowed;
        if (StringUtils::CaseInsensitiveEquals(str, "breakup")) return AffairResolution::Breakup;
        return AffairResolution::None;
    }

}  // namespace MARAS::Utils
