#include "core/PerspectiveEngineAdapters.h"

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "services/ActorService.h"

namespace MARAS {

    namespace {

        [[nodiscard]] constexpr bool CanLookupStaticEngineForm(RE::FormID id) noexcept {
            return id != 0 && id != kUnknownFatherFormID && !IsSyntheticChildID(id);
        }

        [[nodiscard]] const RE::TESNPC* LookupStaticNPCBase(
            RE::FormID id,
            const State::SpatialActorSnapshot& spatial) noexcept {
            if (!CanLookupStaticEngineForm(id)) {
                return nullptr;
            }
            if (id == kPlayerRefID || id == kPlayerBaseID) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    return player->GetActorBase();
                }
                return nullptr;
            }
            if (const auto* form = RE::TESForm::LookupByID(id)) {
                if (const auto* npc = form->As<RE::TESNPC>()) {
                    return npc;
                }
            }
            const RE::FormID baseID = ResolveBaseFormIDSafe(id, spatial);
            if (CanLookupStaticEngineForm(baseID) && baseID != id) {
                return RE::TESForm::LookupByID<RE::TESNPC>(baseID);
            }
            return nullptr;
        }

        [[nodiscard]] std::string ExtractRaceDisplayName(const RE::TESRace* race) {
            if (!race) {
                return {};
            }
            if (const char* name = race->GetFullName(); name && *name) {
                return std::string(name);
            }
            if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                return std::string(edid);
            }
            return {};
        }

        [[nodiscard]] std::string ResolveActorRaceSafeImpl(
            RE::FormID id,
            const State::WorldStateSnapshot& world,
            const State::SpatialActorSnapshot& spatial,
            int depth) {
            if (id == 0 || id == kUnknownFatherFormID) {
                return "Nord";
            }

            if (const auto* sample = spatial.Find(State::RefId{ id }); sample != nullptr && !sample->raceId.IsNull()) {
                if (const auto* race = RE::TESForm::LookupByID<RE::TESRace>(sample->raceId.raw())) {
                    if (auto name = ExtractRaceDisplayName(race); !name.empty()) {
                        return name;
                    }
                }
            }

            if (const auto entry = NPCIndex::GetSingleton().FindByFormID(id); entry && !entry->raceName.empty()) {
                return entry->raceName;
            }

            if (id == kPlayerRefID || id == kPlayerBaseID) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto name = ExtractRaceDisplayName(player->GetRace()); !name.empty()) {
                        return name;
                    }
                }
                return "Nord";
            }

            if (depth < 2 && world.lineage) {
                if (const auto* child = world.lineage->FindChild(State::ChildId{ id })) {
                    const RE::FormID candidates[] = {
                        child->bioMother.raw(),
                        child->bioFather.raw(),
                        child->guardians.slotA.raw(),
                        child->guardians.slotB.raw()
                    };
                    for (const RE::FormID candidateID : candidates) {
                        if (CanLookupStaticEngineForm(candidateID) && candidateID != id) {
                            return ResolveActorRaceSafeImpl(candidateID, world, spatial, depth + 1);
                        }
                    }
                }
            }

            if (const auto* base = LookupStaticNPCBase(id, spatial)) {
                if (auto name = ExtractRaceDisplayName(base->race); !name.empty()) {
                    return name;
                }
            }

            return "Nord";
        }

    }  // namespace

    std::optional<bool> EngineWerewolfStateProvider::IsAdultWerewolf(State::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather()) {
            return false;
        }

        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (const State::RefId ref = actor.AsRefId(); !ref.IsNull() && spatialSnap) {
            if (const auto* sample = spatialSnap->Find(ref); sample != nullptr) {
                return sample->IsWerewolf();
            }
        }

        auto& cache = FormCache::GetSingleton();
        auto* wwChangeSpell = cache.GetWerewolfChangeSpell();
        auto* wwImmunitySpell = cache.GetWerewolfImmunitySpell();
        auto* wwBeastRace = cache.GetWerewolfBeastRace();

        if (actor.IsPlayer()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (wwBeastRace && player->GetRace() == wwBeastRace) {
                    return true;
                }
                if (wwChangeSpell && player->HasSpell(wwChangeSpell)) {
                    return true;
                }
                if (wwImmunitySpell && player->HasSpell(wwImmunitySpell)) {
                    return true;
                }
            }
            return false;
        }

        if (IsSyntheticChildID(actor.raw())) {
            return false;
        }

        RE::FormID refID = actor.raw();
        RE::FormID baseID = 0;
        if (const auto entry = NPCIndex::GetSingleton().FindByFormID(actor.raw())) {
            refID = entry->refFormID != 0 ? entry->refFormID : refID;
            baseID = entry->baseFormID;
        }
        if (baseID == 0) {
            if (const auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(actor.raw())) {
                baseID = npc->GetFormID();
            }
        }

        // Farkas cured after Purity (CR13)
        if (baseID == 0x0001A692 || refID == 0x0001A693) {
            if (auto* cr13 = cache.GetCR13Quest(); cr13 && cr13->IsCompleted()) {
                return false;
            }
        }
        // Vilkas cured after Purity (CR14)
        if (baseID == 0x0001A694 || refID == 0x0001A695) {
            if (auto* cr14 = cache.GetCR14Quest(); cr14 && cr14->IsCompleted()) {
                return false;
            }
        }

        // Canon werewolves (Arnbjorn, Sinding)
        if (baseID == 0x0001BDB0 || refID == 0x0001BDE7 ||
            baseID == 0x000136AC || refID == 0x0002ABBD) {
            return true;
        }

        auto* base = (baseID != 0) ? RE::TESForm::LookupByID<RE::TESNPC>(baseID) : nullptr;
        if (!base) {
            return false;
        }

        auto* playerWwFaction = cache.GetPlayerWerewolfFaction();
        auto* wwFaction = cache.GetWerewolfFaction();
        auto* circleFaction = cache.GetCompanionsCircleFaction();
        auto* frostmoonFaction = cache.GetFrostmoonWerewolvesFaction();

        if (wwBeastRace && base->race == wwBeastRace) {
            return true;
        }
        if (auto* spellData = base->GetSpellList()) {
            for (std::uint32_t i = 0; i < spellData->numSpells; ++i) {
                auto* sp = spellData->spells ? spellData->spells[i] : nullptr;
                if (sp && (sp == wwChangeSpell || sp == wwImmunitySpell)) {
                    return true;
                }
            }
        }
        for (const auto& f : base->factions) {
            if (f.rank < 0 || !f.faction) {
                continue;
            }
            if (f.faction == playerWwFaction ||
                f.faction == wwFaction ||
                f.faction == circleFaction ||
                f.faction == frostmoonFaction) {
                return true;
            }
        }

        return false;
    }

    const EngineWerewolfStateProvider& GetEngineWerewolfStateProvider() noexcept {
        static const EngineWerewolfStateProvider kProvider{};
        return kProvider;
    }

    std::string ResolveActorNameSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial) {
        if (id == 0) {
            return {};
        }
        if (id == kUnknownFatherFormID) {
            return "Unknown";
        }
        if (const auto* sample = spatial.Find(State::RefId{ id }); sample != nullptr) {
            const auto nameView = sample->DisplayNameView();
            if (!nameView.empty()) {
                return std::string(nameView);
            }
        }
        if (world.lineage) {
            if (const auto* child = world.lineage->FindChild(State::ChildId{ id });
                child && !child->name.empty()) {
                return child->name;
            }
        }
        if (const auto entry = NPCIndex::GetSingleton().FindByFormID(id); entry && !entry->nameDisplay.empty()) {
            return entry->nameDisplay;
        }
        if (world.deceased) {
            if (const auto* dec = world.deceased->FindByActor(State::ActorId{ id });
                dec && !dec->name.empty()) {
                return dec->name;
            }
        }
        if (id == kPlayerRefID || id == kPlayerBaseID) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetDisplayFullName(); name && *name) {
                    return std::string(name);
                }
            }
        }
        return ActorSvc::GetName(id);
    }

    State::ActorSex ResolveActorSexSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial) noexcept {
        if (id == 0 || id == kUnknownFatherFormID) {
            return State::ActorSex::Unknown;
        }
        if (const auto* sample = spatial.Find(State::RefId{ id }); sample != nullptr) {
            if (sample->sex != State::ActorSex::Unknown) {
                return sample->sex;
            }
        }
        if (world.lineage) {
            if (const auto* child = world.lineage->FindChild(State::ChildId{ id })) {
                if (child->gender == 0) {
                    return State::ActorSex::Male;
                }
                if (child->gender == 1) {
                    return State::ActorSex::Female;
                }
            }
        }
        if (const auto entry = NPCIndex::GetSingleton().FindByFormID(id)) {
            if (entry->sex == 0) {
                return State::ActorSex::Male;
            }
            if (entry->sex == 1) {
                return State::ActorSex::Female;
            }
        }
        if (const auto* base = LookupStaticNPCBase(id, spatial)) {
            const auto rawSex = base->GetSex();
            if (rawSex == RE::SEXES::kMale) {
                return State::ActorSex::Male;
            }
            if (rawSex == RE::SEXES::kFemale) {
                return State::ActorSex::Female;
            }
        }
        return State::ActorSex::Unknown;
    }

    std::string ResolveActorRaceSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial) {
        return ResolveActorRaceSafeImpl(id, world, spatial, 0);
    }

    RE::FormID ResolveBaseFormIDSafe(
        RE::FormID id,
        const State::SpatialActorSnapshot& spatial) noexcept {
        if (id == 0 || id == kUnknownFatherFormID || IsSyntheticChildID(id)) {
            return 0;
        }
        if (id == kPlayerRefID || id == kPlayerBaseID) {
            return kPlayerBaseID;
        }
        if (const auto* sample = spatial.Find(State::RefId{ id }); sample != nullptr && !sample->baseId.IsNull()) {
            return sample->baseId.raw();
        }
        if (const auto entry = NPCIndex::GetSingleton().FindByFormID(id); entry && entry->baseFormID != 0) {
            return entry->baseFormID;
        }
        if (const auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(id)) {
            return npc->GetFormID();
        }
        return NPCIndex::GetSingleton().GetBaseID(id);
    }

}  // namespace MARAS
