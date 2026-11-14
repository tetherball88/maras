#include "maras/engine/NPCTypeDeterminer.h"

#include "maras/domain/relationships/NpcTypeRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/EnumUtils.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace MARAS {

    // -------------------------- Public API --------------------------

    State::SocialClass NPCTypeDeterminer::DetermineSocialClass(
        RE::FormID npcFormID,
        const std::function<std::optional<std::string>(RE::FormID)>& getSocialClassOverride) {
        if (getSocialClassOverride) {
            if (auto ov = getSocialClassOverride(npcFormID); ov.has_value()) {
                const auto result = Foundation::StringToSocialClass(ov.value());
                MARAS_LOG_DEBUG("Using social class override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }
        return DetermineSocialClassByFaction(npcFormID);
    }

    State::SkillType NPCTypeDeterminer::DetermineSkillType(
        RE::FormID npcFormID,
        const std::function<std::optional<std::string>(RE::FormID)>& getSkillTypeOverride) {
        if (getSkillTypeOverride) {
            if (auto ov = getSkillTypeOverride(npcFormID); ov.has_value()) {
                const auto result = Foundation::StringToSkillType(ov.value());
                MARAS_LOG_DEBUG("Using skill type override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }

        if (auto byClass = DetermineSkillTypeByClass(npcFormID); byClass.has_value()) {
            return byClass.value();
        }
        return DetermineSkillTypeBySkills(npcFormID);
    }

    State::Temperament NPCTypeDeterminer::DetermineTemperament(
        RE::FormID npcFormID,
        const std::function<std::optional<std::string>(RE::FormID)>& getTemperamentOverride,
        const std::function<State::SocialClass(RE::FormID)>& getSocialClass,
        const std::function<State::SkillType(RE::FormID)>& getSkillType) {
        if (getTemperamentOverride) {
            if (auto ov = getTemperamentOverride(npcFormID); ov.has_value()) {
                const auto result = Foundation::StringToTemperament(ov.value());
                MARAS_LOG_DEBUG("Using temperament override for {:08X}: {}", npcFormID, ov.value());
                return result;
            }
        }

        const State::SocialClass sc = getSocialClass ? getSocialClass(npcFormID) : State::SocialClass::Working;
        const State::SkillType st = getSkillType ? getSkillType(npcFormID) : State::SkillType::Warrior;
        return ComputeTemperament(sc, st);
    }

    State::Temperament NPCTypeDeterminer::ComputeTemperament(
        State::SocialClass socialClass,
        State::SkillType skillType) {
        return Domain::Relationships::ComputeTemperament(socialClass, skillType);
    }

    // -------------------------- Internal Helpers --------------------------

    State::SocialClass NPCTypeDeterminer::DetermineSocialClassByFaction(RE::FormID npcFormID) {
        auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID);
        auto* npcBase = ActorSvc::GetActorBase(npcFormID);
        if (!npcBase && actor) {
            npcBase = actor->GetActorBase();
        }
        if (!npcBase && !actor) {
            MARAS_LOG_WARN("Cannot find actor or base for FormID {:08X}", npcFormID);
            return State::SocialClass::Working;
        }

        auto& cache = FormCache::GetSingleton();
        auto* rulerFactions = cache.GetRulerFactions();
        auto* nobleFactions = cache.GetNobleFactions();
        auto* religiousFactions = cache.GetReligiousFactions();
        auto* wealthyFactions = cache.GetWealthyFactions();
        auto* middleFactions = cache.GetMiddleFactions();
        auto* povertyFactions = cache.GetPovertyFactions();
        auto* outcastFactions = cache.GetOutcastFactions();

        int maxClassIndex = -1;
        auto evaluateFaction = [&](RE::TESFaction* faction) {
            if (!faction) return;
            if (rulerFactions && rulerFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Rulers));
            } else if (nobleFactions && nobleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Nobles));
            } else if (religiousFactions && religiousFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Religious));
            } else if (wealthyFactions && wealthyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Wealthy));
            } else if (middleFactions && middleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Middle));
            } else if (povertyFactions && povertyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Poverty));
            } else if (outcastFactions && outcastFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(State::SocialClass::Outcast));
            }
        };

        std::unordered_set<RE::TESFaction*> removedFactions;
        if (actor) {
            if (auto* xFactions = actor->extraList.GetByType<RE::ExtraFactionChanges>()) {
                for (const auto& factionInfo : xFactions->factionChanges) {
                    if (!factionInfo.faction) continue;
                    if (factionInfo.rank < 0) {
                        removedFactions.insert(factionInfo.faction);
                    } else {
                        evaluateFaction(factionInfo.faction);
                    }
                }
            }
        }

        if (npcBase) {
            for (const auto& factionInfo : npcBase->factions) {
                if (!factionInfo.faction || factionInfo.rank < 0) continue;
                if (removedFactions.contains(factionInfo.faction)) continue;
                evaluateFaction(factionInfo.faction);
            }
        }

        auto* clothingKeyword = cache.GetClothingRichKeyword();

        if (actor && clothingKeyword && maxClassIndex < static_cast<int>(State::SocialClass::Wealthy)) {
            using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
            const Slot slotsToCheck[] = {
                Slot::kHead,
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
                Slot::kModMisc2
            };

            for (const auto slot : slotsToCheck) {
                if (auto* armor = actor->GetWornArmor(slot, /*a_noInit=*/true)) {
                    if (armor->HasKeyword(clothingKeyword)) {
                        MARAS_LOG_DEBUG("Actor {:08X} is wearing rich clothing keyword; promoting to Wealthy",
                                        npcFormID);
                        return State::SocialClass::Wealthy;
                    }
                }
            }
        }

        if (maxClassIndex == -1) {
            MARAS_LOG_DEBUG("No matching factions found for {:08X}, defaulting to Working class", npcFormID);
            return State::SocialClass::Working;
        }

        const auto determined = static_cast<State::SocialClass>(maxClassIndex);
        MARAS_LOG_DEBUG("Determined social class for {:08X}: {}",
                        npcFormID, Foundation::SocialClassToString(determined));
        return determined;
    }

    std::optional<State::SkillType> NPCTypeDeterminer::DetermineSkillTypeByClass(RE::FormID npcFormID) {
        auto* npcBase = ActorSvc::GetActorBase(npcFormID);
        if (!npcBase) {
            MARAS_LOG_WARN("Cannot find actor base for FormID {:08X}", npcFormID);
            return std::nullopt;
        }
        auto* spouseClass = npcBase->npcClass;
        if (!spouseClass) {
            MARAS_LOG_DEBUG("No class found for actor {:08X}", npcFormID);
            return std::nullopt;
        }

        auto& cache = FormCache::GetSingleton();
        auto* oratorClasses = cache.GetOratorClasses();
        auto* rangerClasses = cache.GetRangerClasses();
        auto* rogueClasses = cache.GetRogueClasses();
        auto* craftsmanClasses = cache.GetCraftsmanClasses();
        auto* mageClasses = cache.GetMageClasses();
        auto* warriorClasses = cache.GetWarriorClasses();

        if (oratorClasses && oratorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Orator", npcFormID);
            return State::SkillType::Orator;
        }
        if (rangerClasses && rangerClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Ranger", npcFormID);
            return State::SkillType::Ranger;
        }
        if (rogueClasses && rogueClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Rogue", npcFormID);
            return State::SkillType::Rogue;
        }
        if (craftsmanClasses && craftsmanClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Craftsman", npcFormID);
            return State::SkillType::Craftsman;
        }
        if (mageClasses && mageClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Mage", npcFormID);
            return State::SkillType::Mage;
        }
        if (warriorClasses && warriorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Warrior", npcFormID);
            return State::SkillType::Warrior;
        }

        MARAS_LOG_DEBUG("No class match found for {:08X}, deferring to skill-based detection", npcFormID);
        return std::nullopt;
    }

    State::SkillType NPCTypeDeterminer::DetermineSkillTypeBySkills(RE::FormID npcFormID) {
        auto* npcBase = ActorSvc::GetActorBase(npcFormID);
        if (!npcBase) {
            MARAS_LOG_WARN("Cannot find actor base for FormID {:08X} in skill determination", npcFormID);
            return State::SkillType::Warrior;
        }
        auto* actorValueOwner = npcBase->As<RE::ActorValueOwner>();
        if (!actorValueOwner) {
            MARAS_LOG_WARN("Actor {:08X} does not implement ActorValueOwner interface (actor or base)", npcFormID);
            return State::SkillType::Warrior;
        }

        const std::array<float, 18> skills{
            actorValueOwner->GetActorValue(RE::ActorValue::kOneHanded),
            actorValueOwner->GetActorValue(RE::ActorValue::kTwoHanded),
            actorValueOwner->GetActorValue(RE::ActorValue::kArchery),
            actorValueOwner->GetActorValue(RE::ActorValue::kBlock),
            actorValueOwner->GetActorValue(RE::ActorValue::kSmithing),
            actorValueOwner->GetActorValue(RE::ActorValue::kHeavyArmor),
            actorValueOwner->GetActorValue(RE::ActorValue::kLightArmor),
            actorValueOwner->GetActorValue(RE::ActorValue::kPickpocket),
            actorValueOwner->GetActorValue(RE::ActorValue::kLockpicking),
            actorValueOwner->GetActorValue(RE::ActorValue::kSneak),
            actorValueOwner->GetActorValue(RE::ActorValue::kAlchemy),
            actorValueOwner->GetActorValue(RE::ActorValue::kSpeech),
            actorValueOwner->GetActorValue(RE::ActorValue::kAlteration),
            actorValueOwner->GetActorValue(RE::ActorValue::kConjuration),
            actorValueOwner->GetActorValue(RE::ActorValue::kDestruction),
            actorValueOwner->GetActorValue(RE::ActorValue::kIllusion),
            actorValueOwner->GetActorValue(RE::ActorValue::kRestoration),
            actorValueOwner->GetActorValue(RE::ActorValue::kEnchanting)
        };

        const State::SkillType skillType = Domain::Relationships::ComputeSkillTypeFromSkills(skills);
        MARAS_LOG_DEBUG("Determined skill type by skills for {:08X}: {}",
                        npcFormID, Foundation::SkillTypeToString(skillType));
        return skillType;
    }

}  // namespace MARAS
