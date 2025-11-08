#include "core/NPCTypeDeterminer.h"

#include <algorithm>

#include "core/FormCache.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"

namespace MARAS {

    // -------------------------- Public API --------------------------

    SocialClass NPCTypeDeterminer::DetermineSocialClass(
        RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getSocialClassOverride) {
        if (getSocialClassOverride) {
            if (auto ov = getSocialClassOverride(npcFormID); ov.has_value()) {
                auto result = Utils::StringToSocialClass(ov.value());
                MARAS_LOG_DEBUG("Using social class override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }
        return DetermineSocialClassByFaction(npcFormID);
    }

    SkillType NPCTypeDeterminer::DetermineSkillType(
        RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getSkillTypeOverride) {
        if (getSkillTypeOverride) {
            if (auto ov = getSkillTypeOverride(npcFormID); ov.has_value()) {
                auto result = Utils::StringToSkillType(ov.value());
                MARAS_LOG_DEBUG("Using skill type override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }

        // attempt DetermineSkillTypeByClass first (currently mirrors original commented code)
        if (auto byClass = DetermineSkillTypeByClass(npcFormID); byClass.has_value()) {
            return byClass.value();
        }
        return DetermineSkillTypeBySkills(npcFormID);
    }

    Temperament NPCTypeDeterminer::DetermineTemperament(
        RE::FormID npcFormID, const std::function<std::optional<std::string>(RE::FormID)>& getTemperamentOverride,
        const std::function<SocialClass(RE::FormID)>& getSocialClass,
        const std::function<SkillType(RE::FormID)>& getSkillType) {
        if (getTemperamentOverride) {
            if (auto ov = getTemperamentOverride(npcFormID); ov.has_value()) {
                auto result = Utils::StringToTemperament(ov.value());
                MARAS_LOG_DEBUG("Using temperament override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }

        SocialClass sc = getSocialClass ? getSocialClass(npcFormID) : SocialClass::Working;
        SkillType st = getSkillType ? getSkillType(npcFormID) : SkillType::Warrior;
        return ComputeTemperament(sc, st);
    }

    Temperament NPCTypeDeterminer::ComputeTemperament(SocialClass socialClass, SkillType skillType) {
        int socialClassIndex = static_cast<int>(socialClass);
        int skillTypeIndex = static_cast<int>(skillType);

        // Independent
        if ((socialClassIndex == 0 && (skillTypeIndex == 0 || skillTypeIndex == 4)) ||
            (socialClassIndex == 1 && skillTypeIndex == 3) ||
            (socialClassIndex == 2 && (skillTypeIndex == 3 || skillTypeIndex == 4)) ||
            (socialClassIndex == 3 && skillTypeIndex == 2) || (socialClassIndex == 4 && skillTypeIndex == 4) ||
            (socialClassIndex == 5 && skillTypeIndex == 0) || (socialClassIndex == 6 && skillTypeIndex == 2) ||
            (socialClassIndex == 7 && (skillTypeIndex == 1 || skillTypeIndex == 5))) {
            return Temperament::Independent;
        }
        // Jealous
        else if ((socialClassIndex == 0 && (skillTypeIndex == 1 || skillTypeIndex == 2)) ||
                 (socialClassIndex == 1 && skillTypeIndex == 2) || (socialClassIndex == 3 && skillTypeIndex == 5) ||
                 (socialClassIndex == 4 && (skillTypeIndex == 1 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 5 && skillTypeIndex == 3) || (socialClassIndex == 6 && skillTypeIndex == 0) ||
                 (socialClassIndex == 7 && skillTypeIndex == 4)) {
            return Temperament::Jealous;
        }
        // Humble
        else if ((socialClassIndex == 0 && skillTypeIndex == 3) || (socialClassIndex == 1 && skillTypeIndex == 0) ||
                 (socialClassIndex == 2 && skillTypeIndex == 1) || (socialClassIndex == 3 && skillTypeIndex == 4) ||
                 (socialClassIndex == 5 && (skillTypeIndex == 1 || skillTypeIndex == 4)) ||
                 (socialClassIndex == 6 && skillTypeIndex == 4) || (socialClassIndex == 7 && skillTypeIndex == 2)) {
            return Temperament::Humble;
        }
        // Proud
        else if ((socialClassIndex == 1 && skillTypeIndex == 4) ||
                 (socialClassIndex == 2 && (skillTypeIndex == 0 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 3 && skillTypeIndex == 0) || (socialClassIndex == 4 && skillTypeIndex == 0) ||
                 (socialClassIndex == 5 && skillTypeIndex == 5) ||
                 (socialClassIndex == 6 && (skillTypeIndex == 3 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 7 && skillTypeIndex == 0)) {
            return Temperament::Proud;
        }
        // Romantic
        else if ((socialClassIndex == 0 && skillTypeIndex == 5) ||
                 (socialClassIndex == 1 && (skillTypeIndex == 1 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 2 && skillTypeIndex == 2) ||
                 (socialClassIndex == 3 && (skillTypeIndex == 1 || skillTypeIndex == 3)) ||
                 (socialClassIndex == 4 && (skillTypeIndex == 2 || skillTypeIndex == 3)) ||
                 (socialClassIndex == 5 && skillTypeIndex == 2) || (socialClassIndex == 6 && skillTypeIndex == 1) ||
                 (socialClassIndex == 7 && skillTypeIndex == 3)) {
            return Temperament::Romantic;
        }
        // Fallback
        else {
            MARAS_LOG_DEBUG("Temperament matrix fallback (SC:{}, ST:{}), using Independent", socialClassIndex,
                            skillTypeIndex);
            return Temperament::Independent;
        }
    }

    // -------------------------- Internal Helpers --------------------------

    SocialClass NPCTypeDeterminer::DetermineSocialClassByFaction(RE::FormID npcFormID) {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_ERROR("Cannot find actor for FormID {:08X}", npcFormID);
            return SocialClass::Working;
        }

        auto rulerFactions = FormCache::GetSingleton().GetRulerFactions();
        auto nobleFactions = FormCache::GetSingleton().GetNobleFactions();
        auto religiousFactions = FormCache::GetSingleton().GetReligiousFactions();
        auto wealthyFactions = FormCache::GetSingleton().GetWealthyFactions();
        auto middleFactions = FormCache::GetSingleton().GetMiddleFactions();
        auto povertyFactions = FormCache::GetSingleton().GetPovertyFactions();
        auto outcastFactions = FormCache::GetSingleton().GetOutcastFactions();

        if (!actor->GetActorBase() || !actor->GetActorBase()->factions.size()) {
            MARAS_LOG_DEBUG("No factions found for actor {:08X}, defaulting to Working class", npcFormID);
            return SocialClass::Working;
        }

        auto& actorFactions = actor->GetActorBase()->factions;
        int maxClassIndex = -1;

        for (const auto& factionInfo : actorFactions) {
            if (!factionInfo.faction) continue;
            auto faction = factionInfo.faction;
            if (rulerFactions && rulerFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Rulers));
            } else if (nobleFactions && nobleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Nobles));
            } else if (religiousFactions && religiousFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Religious));
            } else if (wealthyFactions && wealthyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Wealthy));
            } else if (middleFactions && middleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Middle));
            } else if (povertyFactions && povertyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Poverty));
            } else if (outcastFactions && outcastFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Outcast));
            }
        }

        auto clothingKeyword = FormCache::GetSingleton().GetClothingRichKeyword();

        if (clothingKeyword && maxClassIndex < static_cast<int>(SocialClass::Wealthy)) {
            using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
            const Slot slotsToCheck[] = {Slot::kHead,
                                         Slot::kHair,
                                         Slot::kBody,
                                         Slot::kHands,
                                         Slot::kForearms,
                                         Slot::kAmulet,
                                         Slot::kRing,
                                         Slot::kFeet,
                                         Slot::kCalves,
                                         Slot::kShield,
                                         Slot::kTail,
                                         Slot::kLongHair,
                                         Slot::kCirclet,
                                         Slot::kEars,
                                         Slot::kModMouth,
                                         Slot::kModNeck,
                                         Slot::kModChestPrimary,
                                         Slot::kModBack,
                                         Slot::kModMisc1,
                                         Slot::kModPelvisPrimary,
                                         Slot::kModPelvisSecondary,
                                         Slot::kModLegRight,
                                         Slot::kModLegLeft,
                                         Slot::kModFaceJewelry,
                                         Slot::kModChestSecondary,
                                         Slot::kModShoulder,
                                         Slot::kModArmLeft,
                                         Slot::kModArmRight,
                                         Slot::kModMisc2};

            for (auto s : slotsToCheck) {
                if (auto armor = actor->GetWornArmor(s, /*a_noInit=*/true)) {
                    if (armor->HasKeyword(clothingKeyword)) {
                        MARAS_LOG_DEBUG("Actor {:08X} is wearing rich clothing keyword; promoting to Wealthy",
                                        npcFormID);
                        return SocialClass::Wealthy;
                    }
                }
            }
        }

        if (maxClassIndex == -1) {
            MARAS_LOG_DEBUG("No matching factions found for {:08X}, defaulting to Working class", npcFormID);
            return SocialClass::Working;
        }

        MARAS_LOG_DEBUG("Determined social class for {:08X}: {}", npcFormID,
                        Utils::SocialClassToString(static_cast<SocialClass>(maxClassIndex)));
        return static_cast<SocialClass>(maxClassIndex);
    }

    std::optional<SkillType> NPCTypeDeterminer::DetermineSkillTypeByClass(RE::FormID npcFormID) {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor || !actor->GetActorBase()) {
            MARAS_LOG_WARN("Cannot find actor or actor base for FormID {:08X}", npcFormID);
            return std::nullopt;
        }
        auto spouseClass = actor->GetActorBase()->npcClass;
        if (!spouseClass) {
            MARAS_LOG_DEBUG("No class found for actor {:08X}", npcFormID);
            return std::nullopt;
        }
        auto oratorClasses = FormCache::GetSingleton().GetOratorClasses();
        auto rangerClasses = FormCache::GetSingleton().GetRangerClasses();
        auto rogueClasses = FormCache::GetSingleton().GetRogueClasses();
        auto craftsmanClasses = FormCache::GetSingleton().GetCraftsmanClasses();
        auto mageClasses = FormCache::GetSingleton().GetMageClasses();
        auto warriorClasses = FormCache::GetSingleton().GetWarriorClasses();
        if (oratorClasses && oratorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Orator", npcFormID);
            return SkillType::Orator;
        }
        if (rangerClasses && rangerClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Ranger", npcFormID);
            return SkillType::Ranger;
        }
        if (rogueClasses && rogueClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Rogue", npcFormID);
            return SkillType::Rogue;
        }
        if (craftsmanClasses && craftsmanClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Craftsman", npcFormID);
            return SkillType::Craftsman;
        }
        if (mageClasses && mageClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Mage", npcFormID);
            return SkillType::Mage;
        }
        if (warriorClasses && warriorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Warrior", npcFormID);
            return SkillType::Warrior;
        }
        MARAS_LOG_DEBUG("No class match found for {:08X}, deferring to skill-based detection", npcFormID);
        return std::nullopt;
    }

    SkillType NPCTypeDeterminer::DetermineSkillTypeBySkills(RE::FormID npcFormID) {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_WARN("Cannot find actor for FormID {:08X} in skill determination", npcFormID);
            return SkillType::Warrior;
        }
        std::array<float, 18> skills{};
        auto actorValueOwner = actor->GetActorBase()->As<RE::ActorValueOwner>();
        if (!actorValueOwner) {
            MARAS_LOG_WARN("Actor {:08X} does not implement ActorValueOwner interface (actor or base)", npcFormID);
            return SkillType::Warrior;
        }
        skills[0] = actorValueOwner->GetActorValue(RE::ActorValue::kOneHanded);
        skills[1] = actorValueOwner->GetActorValue(RE::ActorValue::kTwoHanded);
        skills[2] = actorValueOwner->GetActorValue(RE::ActorValue::kArchery);
        skills[3] = actorValueOwner->GetActorValue(RE::ActorValue::kBlock);
        skills[4] = actorValueOwner->GetActorValue(RE::ActorValue::kSmithing);
        skills[5] = actorValueOwner->GetActorValue(RE::ActorValue::kHeavyArmor);
        skills[6] = actorValueOwner->GetActorValue(RE::ActorValue::kLightArmor);
        skills[7] = actorValueOwner->GetActorValue(RE::ActorValue::kPickpocket);
        skills[8] = actorValueOwner->GetActorValue(RE::ActorValue::kLockpicking);
        skills[9] = actorValueOwner->GetActorValue(RE::ActorValue::kSneak);
        skills[10] = actorValueOwner->GetActorValue(RE::ActorValue::kAlchemy);
        skills[11] = actorValueOwner->GetActorValue(RE::ActorValue::kSpeech);
        skills[12] = actorValueOwner->GetActorValue(RE::ActorValue::kAlteration);
        skills[13] = actorValueOwner->GetActorValue(RE::ActorValue::kConjuration);
        skills[14] = actorValueOwner->GetActorValue(RE::ActorValue::kDestruction);
        skills[15] = actorValueOwner->GetActorValue(RE::ActorValue::kIllusion);
        skills[16] = actorValueOwner->GetActorValue(RE::ActorValue::kRestoration);
        skills[17] = actorValueOwner->GetActorValue(RE::ActorValue::kEnchanting);
        int maxIndex = 0;
        float maxVal = skills[0];
        for (int i = 1; i < 18; ++i) {
            if (skills[i] > maxVal) {
                maxIndex = i;
                maxVal = skills[i];
            }
        }
        if (maxIndex == 0 || maxIndex == 1 || maxIndex == 3 || maxIndex == 5) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Warrior", npcFormID);
            return SkillType::Warrior;
        } else if (maxIndex == 2) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Ranger", npcFormID);
            return SkillType::Ranger;
        } else if (maxIndex == 7 || maxIndex == 8 || maxIndex == 9) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Rogue", npcFormID);
            return SkillType::Rogue;
        } else if (maxIndex == 12 || maxIndex == 13 || maxIndex == 14 || maxIndex == 15 || maxIndex == 16) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Mage", npcFormID);
            return SkillType::Mage;
        } else if (maxIndex == 11) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Orator", npcFormID);
            return SkillType::Orator;
        } else if (maxIndex == 4 || maxIndex == 10 || maxIndex == 17) {
            MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Craftsman", npcFormID);
            return SkillType::Craftsman;
        } else if (maxIndex == 6) {
            if (skills[2] > skills[7] && skills[2] > skills[8]) {
                MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Ranger (Light Armor case)", npcFormID);
                return SkillType::Ranger;
            } else {
                MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: Rogue (Light Armor case)", npcFormID);
                return SkillType::Rogue;
            }
        }
        return SkillType::Warrior;  // Fallback
    }

}  // namespace MARAS
