#include "maras/engine/ChildSpawnService.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <random>

namespace MARAS {

    namespace {

        [[nodiscard]] bool IsCandidateActorAlive(const State::WorldStateSnapshot& snap, RE::FormID actorId) {
            if (actorId == 0 || actorId == kUnknownFatherFormID) {
                return false;
            }
            if (snap.deceased && snap.deceased->IsDeceased(Foundation::ActorId{ actorId })) {
                return false;
            }
            if (auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actorId)) {
                return !actor->IsDead();
            }
            if (auto* form = RE::TESForm::LookupByID(actorId)) {
                if (auto* actor = form->As<RE::Actor>()) {
                    return !actor->IsDead();
                }
            }
            return true;
        }

    }  // namespace

    ChildSpawnService& ChildSpawnService::GetSingleton() {
        static ChildSpawnService instance;
        return instance;
    }

    RE::FormID ChildSpawnService::ResolveNonPlayerChildTarget(const State::ChildRecord& child) const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const RE::FormID gA = child.guardians.slotA.raw();
        const RE::FormID gB = child.guardians.slotB.raw();
        const RE::FormID bioM = child.bioMother.raw();
        const RE::FormID bioF =
            (!child.bioFather.IsNull() && !child.bioFather.IsUnknownFather()) ? child.bioFather.raw() : 0;

        const bool gAAlive = IsCandidateActorAlive(*worldSnap, gA);
        const bool gBAlive = IsCandidateActorAlive(*worldSnap, gB);
        const bool bioMAlive = IsCandidateActorAlive(*worldSnap, bioM);
        const bool bioFAlive = IsCandidateActorAlive(*worldSnap, bioF);

        const Foundation::ActorId playerId = Foundation::ActorId::Player();

        // 0. If in Player household (unhoused), prefer the NPC co-guardian spouse first so the child stays with their NPC parent
        if (child.guardians.HasGuardian(playerId)) {
            const RE::FormID npcCoGuardian = child.guardians.OtherGuardian(playerId).raw();
            if (npcCoGuardian != 0 && !Utils::MatchesActor(npcCoGuardian, kPlayerFormID) &&
                IsCandidateActorAlive(*worldSnap, npcCoGuardian)) {
                return npcCoGuardian;
            }
            return kPlayerFormID;
        }

        // 1. Check if guardian A/B is bio-mother
        if (gA != 0 && Utils::MatchesActor(gA, bioM) && gAAlive) {
            return gA;
        }
        if (gB != 0 && Utils::MatchesActor(gB, bioM) && gBAlive) {
            return gB;
        }

        // 2. Check if guardian A/B is bio-father
        if (gA != 0 && Utils::MatchesActor(gA, bioF) && gAAlive) {
            return gA;
        }
        if (gB != 0 && Utils::MatchesActor(gB, bioF) && gBAlive) {
            return gB;
        }

        // 3. Pick randomly between guardian A or B if both present & alive
        if (gA != 0 && gB != 0 && gAAlive && gBAlive) {
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1);
            return (dist(rng) == 0) ? gA : gB;
        }

        // 4. If only one guardian present & alive
        if (gA != 0 && gAAlive) {
            return gA;
        }
        if (gB != 0 && gBAlive) {
            return gB;
        }

        // 5. Fallback to bio-mother
        if (bioM != 0 && bioMAlive) {
            return bioM;
        }

        // 6. Fallback to bio-father
        if (bioF != 0 && bioFAlive) {
            return bioF;
        }

        // 7. Fallback to Honorhall Orphanage center marker
        if (auto* marker = FormCache::GetSingleton().GetHonorhallOrphanageCenterMarker()) {
            return marker->GetFormID();
        }
        return 0;
    }

    RE::FormID ChildSpawnService::ResolveNonPlayerChildTargetById(RE::FormID childId) const {
        if (childId == 0) {
            return 0;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* childRec = worldSnap->lineage->FindChild(State::ChildId{ childId });
        return childRec ? ResolveNonPlayerChildTarget(*childRec) : 0;
    }

    void ChildSpawnService::SetChildSandboxAroundParentLinkedRef(RE::FormID childId, RE::FormID targetRefId) const {
        auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
        if (Utils::SetLinkedRef(childId, targetRefId, keyword)) {
            MARAS_LOG_INFO(
                "ChildSpawnService::SetChildSandboxAroundParentLinkedRef: child {:08X} -> target {:08X} (keyword {:08X})",
                childId, targetRefId, keyword ? keyword->GetFormID() : 0);
        } else {
            MARAS_LOG_WARN(
                "ChildSpawnService::SetChildSandboxAroundParentLinkedRef: failed for child {:08X}: target={:08X}, keywordPresent={}",
                childId, targetRefId, keyword != nullptr);
        }
    }

    void ChildSpawnService::RemoveChildSandboxAroundParentLinkedRef(RE::FormID childId) const {
        auto* keyword = FormCache::GetSingleton().GetChildSandboxAroundParentKeyword();
        if (Utils::RemoveLinkedRef(childId, keyword)) {
            MARAS_LOG_INFO("ChildSpawnService::RemoveChildSandboxAroundParentLinkedRef: removed 0x377 linked ref from child {:08X}",
                           childId);
        }
    }

    bool ChildSpawnService::PlaceChildAtMarker(RE::Actor* childActor, RE::TESObjectREFR* targetMarker) const {
        if (!childActor || !targetMarker) {
            return false;
        }
        childActor->MoveTo(targetMarker);
        childActor->EvaluatePackage();
        MARAS_LOG_DEBUG("ChildSpawnService::PlaceChildAtMarker: moved child {:08X} to marker {:08X}",
                        childActor->GetFormID(), targetMarker->GetFormID());
        return true;
    }

    bool ChildSpawnService::PlaceChildAtMarker(RE::FormID childId, RE::FormID targetMarkerId) const {
        if (!HasPhysicalActor(childId) || targetMarkerId == 0) {
            return false;
        }
        auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childId);
        auto* targetMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(targetMarkerId);
        return PlaceChildAtMarker(childActor, targetMarker);
    }

    bool ChildSpawnService::DisableAndDeleteActor(RE::FormID actorId) const {
        if (!HasPhysicalActor(actorId)) {
            return false;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor) {
            return false;
        }
        actor->Disable();
        actor->SetDelete(true);
        MARAS_LOG_INFO("ChildSpawnService::DisableAndDeleteActor: disabled and marked {:08X} for deletion", actorId);
        return true;
    }

    void ChildSpawnService::ClearAdoptableFaction(RE::FormID childId) const {
        if (!HasPhysicalActor(childId)) {
            return;
        }
        if (auto* adoptableFaction = FormCache::GetSingleton().GetBYOHRelationshipAdoptableFaction()) {
            Utils::RemoveFromFaction(childId, adoptableFaction);
        }
    }

    std::uint8_t ChildSpawnService::ResolveActorFMRRaceIndex(RE::FormID actorId) {
        if (!HasPhysicalActor(actorId)) {
            return 255;
        }

        std::string combined;
        if (auto* race = ActorSvc::GetRace(actorId)) {
            if (const char* edid = race->GetFormEditorID(); edid && edid[0] != '\0') {
                combined += edid;
                combined += ' ';
            }
            if (const char* rName = race->GetName(); rName && rName[0] != '\0') {
                combined += rName;
            }
        } else {
            combined = ActorSvc::GetRaceName(actorId);
        }

        if (combined.empty()) {
            return 255;
        }

        const std::string lower = Foundation::StringUtils::ToLowerStd(combined);
        // Must match Fertility Mode Reloaded (_JSW_BB_Storage.BirthMotherRace / GetRaceNameForStorage) 0..9 order:
        // 0=Argonian, 1=Breton, 2=Dark Elf, 3=High Elf, 4=Imperial, 5=Khajiit, 6=Nord, 7=Orc, 8=Redguard, 9=Wood Elf
        if (lower.find("argonian") != std::string::npos) return 0;
        if (lower.find("breton") != std::string::npos) return 1;
        if (lower.find("darkelf") != std::string::npos || lower.find("dark elf") != std::string::npos || lower.find("dunmer") != std::string::npos) return 2;
        if (lower.find("highelf") != std::string::npos || lower.find("high elf") != std::string::npos || lower.find("altmer") != std::string::npos) return 3;
        if (lower.find("imperial") != std::string::npos) return 4;
        if (lower.find("khajiit") != std::string::npos) return 5;
        if (lower.find("nord") != std::string::npos) return 6;
        if (lower.find("orc") != std::string::npos || lower.find("orsimer") != std::string::npos) return 7;
        if (lower.find("redguard") != std::string::npos) return 8;
        if (lower.find("woodelf") != std::string::npos || lower.find("wood elf") != std::string::npos || lower.find("bosmer") != std::string::npos) return 9;
        return 255;
    }

    bool ChildSpawnService::IsActorWerewolf(RE::FormID actorId) {
        if (actorId == 0 || actorId == kUnknownFatherFormID) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* childRec = snap->lineage->FindChild(State::ChildId{ actorId });
            childRec && childRec->hasBeastblood) {
            return true;
        }

        auto& cache = FormCache::GetSingleton();
        auto* wwChangeSpell = cache.GetWerewolfChangeSpell();
        auto* wwImmunitySpell = cache.GetWerewolfImmunitySpell();
        auto* wwBeastRace = cache.GetWerewolfBeastRace();

        if (Utils::MatchesActor(actorId, kPlayerFormID)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;
            if (wwBeastRace && player->GetRace() == wwBeastRace) return true;
            if (wwChangeSpell && player->HasSpell(wwChangeSpell)) return true;
            if (wwImmunitySpell && player->HasSpell(wwImmunitySpell)) return true;
            return false;
        }

        RE::Actor* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actorId);
        if (!actor) {
            if (auto* form = RE::TESForm::LookupByID(actorId)) {
                actor = form->As<RE::Actor>();
            }
        }
        RE::TESNPC* base = actor ? actor->GetActorBase() : ActorSvc::GetActorBase(actorId);
        const RE::FormID baseID = base ? base->GetFormID() : 0;
        const RE::FormID refID = actor ? actor->GetFormID() : actorId;

        // Farkas (0x01A692 / 0x01A693) and Vilkas (0x01A694 / 0x01A695) can be cured via Purity quests CR13 / CR14
        if (baseID == 0x0001A692 || refID == 0x0001A693) {
            if (auto* cr13 = cache.GetCR13Quest(); cr13 && cr13->IsCompleted()) {
                return false;
            }
        }
        if (baseID == 0x0001A694 || refID == 0x0001A695) {
            if (auto* cr14 = cache.GetCR14Quest(); cr14 && cr14->IsCompleted()) {
                return false;
            }
        }

        // Canon werewolf NPCs in Skyrim.esm whose base NPC records do not carry CompanionsCircle
        if (baseID == 0x0001BDB0 || refID == 0x0001BDE7 ||
            baseID == 0x000136AC || refID == 0x0002ABBD) {
            return true;
        }

        auto* playerWwFaction = cache.GetPlayerWerewolfFaction();
        auto* wwFaction = cache.GetWerewolfFaction();
        auto* circleFaction = cache.GetCompanionsCircleFaction();
        auto* frostmoonFaction = cache.GetFrostmoonWerewolvesFaction();

        if (actor) {
            if (wwBeastRace && actor->GetRace() == wwBeastRace) return true;
            if (wwChangeSpell && actor->HasSpell(wwChangeSpell)) return true;
            if (wwImmunitySpell && actor->HasSpell(wwImmunitySpell)) return true;
            if (playerWwFaction && actor->IsInFaction(playerWwFaction)) return true;
            if (wwFaction && actor->IsInFaction(wwFaction)) return true;
            if (circleFaction && actor->IsInFaction(circleFaction)) return true;
            if (frostmoonFaction && actor->IsInFaction(frostmoonFaction)) return true;
        }

        if (base) {
            if (wwBeastRace && base->race == wwBeastRace) return true;
            if (auto* spellData = base->GetSpellList()) {
                for (std::uint32_t i = 0; i < spellData->numSpells; ++i) {
                    auto* sp = spellData->spells ? spellData->spells[i] : nullptr;
                    if (sp && (sp == wwChangeSpell || sp == wwImmunitySpell)) {
                        return true;
                    }
                }
            }
            for (const auto& f : base->factions) {
                if (f.rank < 0 || !f.faction) continue;
                if (f.faction == playerWwFaction ||
                    f.faction == wwFaction ||
                    f.faction == circleFaction ||
                    f.faction == frostmoonFaction) {
                    return true;
                }
            }
        }

        return false;
    }

    bool ChildSpawnService::EvaluateChildBeastbloodInheritance(State::ChildRecord& record) {
        if (record.beastbloodEvaluated) {
            return false;
        }

        const RE::FormID motherId = record.bioMother.raw();
        const RE::FormID fatherId = record.bioFather.raw();
        const bool motherHas = (motherId != 0) && IsActorWerewolf(motherId);
        const bool fatherHas = (fatherId != 0 && fatherId != kUnknownFatherFormID) && IsActorWerewolf(fatherId);
        const int wwParentCount = (motherHas ? 1 : 0) + (fatherHas ? 1 : 0);

        if (wwParentCount >= 2) {
            record.hasBeastblood = true;
            record.beastbloodEvaluated = true;
            MARAS_LOG_INFO(
                "[Beastblood] Child {:08X} ('{}') inherited Beastblood (100% — both bio parents {:08X} & {:08X} are werewolves)",
                record.childId.raw(), record.name, motherId, fatherId);
            return true;
        }

        if (wwParentCount == 1) {
            std::uint64_t seed = (static_cast<std::uint64_t>(record.childId.raw()) * 0x9E3779B97F4A7C15ULL) ^
                                 (static_cast<std::uint64_t>(motherId) * 0xBF58476D1CE4E5B9ULL) ^
                                 (static_cast<std::uint64_t>(fatherId) * 0x94D049BB133111EBULL);
            seed ^= (seed >> 30);
            seed *= 0xBF58476D1CE4E5B9ULL;
            seed ^= (seed >> 27);
            seed *= 0x94D049BB133111EBULL;
            seed ^= (seed >> 31);
            record.hasBeastblood = ((seed & 1ULL) != 0);
            record.beastbloodEvaluated = true;
            MARAS_LOG_INFO(
                "[Beastblood] Child {:08X} ('{}') 50% Beastblood inheritance roll (mother={:08X}[{}], father={:08X}[{}]) -> {}",
                record.childId.raw(), record.name, motherId, motherHas, fatherId, fatherHas,
                record.hasBeastblood ? "INHERITED" : "MORTAL");
            return true;
        }

        record.hasBeastblood = false;
        if (motherId != 0 ||
            (fatherId != 0 && fatherId != kUnknownFatherFormID) ||
            record.isAdopted) {
            record.beastbloodEvaluated = true;
            return true;
        }
        return false;
    }

}  // namespace MARAS
