#include "utils/EnumUtils.h"

#include <algorithm>
#include <cctype>

namespace MARAS::Utils {

    // Helper function to convert string to lowercase
    std::string ToLower(std::string_view str) {
        std::string result(str);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    // String to enum conversions
    SocialClass StringToSocialClass(std::string_view str) {
        auto lower = ToLower(str);

        if (lower == "outcast") return SocialClass::Outcast;
        if (lower == "poverty") return SocialClass::Poverty;
        if (lower == "working") return SocialClass::Working;
        if (lower == "middle") return SocialClass::Middle;
        if (lower == "wealthy") return SocialClass::Wealthy;
        if (lower == "religious") return SocialClass::Religious;
        if (lower == "nobles") return SocialClass::Nobles;
        if (lower == "rulers") return SocialClass::Rulers;

        // Default to first value if string not recognized
        return SocialClass::Working;
    }

    SkillType StringToSkillType(std::string_view str) {
        auto lower = ToLower(str);

        if (lower == "warrior") return SkillType::Warrior;
        if (lower == "mage") return SkillType::Mage;
        if (lower == "rogue") return SkillType::Rogue;
        if (lower == "craftsman") return SkillType::Craftsman;
        if (lower == "ranger") return SkillType::Ranger;
        if (lower == "orator") return SkillType::Orator;

        // Default to first value if string not recognized
        return SkillType::Warrior;
    }

    Temperament StringToTemperament(std::string_view str) {
        auto lower = ToLower(str);

        if (lower == "proud") return Temperament::Proud;
        if (lower == "humble") return Temperament::Humble;
        if (lower == "jealous") return Temperament::Jealous;
        if (lower == "romantic") return Temperament::Romantic;
        if (lower == "independent") return Temperament::Independent;

        // Default to first value if string not recognized
        return Temperament::Independent;
    }

    RelationshipStatus StringToRelationshipStatus(std::string_view str) {
        auto lower = ToLower(str);

        if (lower == "candidate") return RelationshipStatus::Candidate;
        if (lower == "engaged") return RelationshipStatus::Engaged;
        if (lower == "married") return RelationshipStatus::Married;
        if (lower == "divorced") return RelationshipStatus::Divorced;
        if (lower == "jilted") return RelationshipStatus::Jilted;
        if (lower == "deceased") return RelationshipStatus::Deceased;

        return RelationshipStatus::Candidate;
    }

    // Utility functions
    GameDay GetCurrentGameDay() {
        auto calendar = RE::Calendar::GetSingleton();
        return calendar ? static_cast<GameDay>(calendar->GetDaysPassed()) : 0;
    }

    bool IsValidNPC(FormID npcFormID) {
        auto form = RE::TESForm::LookupByID(npcFormID);
        auto npc = form ? form->As<RE::TESNPC>() : nullptr;
        return npc != nullptr;
    }

    std::string GetNPCName(FormID npcFormID) {
        auto form = RE::TESForm::LookupByID(npcFormID);
        auto npc = form ? form->As<RE::TESNPC>() : nullptr;

        if (npc && npc->GetName()) {
            return std::string(npc->GetName());
        }

        return fmt::format("NPC_{:08X}", npcFormID);
    }

}  // namespace MARAS::Utils
