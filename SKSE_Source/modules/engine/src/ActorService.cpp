#include "maras/engine/ActorService.h"

#include "maras/engine/FormCache.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/state/WorldStore.h"

namespace MARAS::ActorSvc {

    namespace {
        constexpr bool IsPlayerFormID(RE::FormID formID) noexcept {
            return formID == kPlayerRefID || formID == kPlayerBaseID;
        }

        constexpr bool CanLookupEngineForm(RE::FormID formID) noexcept {
            return formID != 0 && formID != kUnknownFatherFormID && !IsSyntheticChildID(formID);
        }

        const State::ChildRecord* FindChildRecord(RE::FormID formID) {
            if (formID == 0 || formID == kUnknownFatherFormID) {
                return nullptr;
            }
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            if (!worldSnap || !worldSnap->lineage) {
                return nullptr;
            }
            return worldSnap->lineage->FindChild(State::ChildId{ formID });
        }

        std::string GetRaceNameImpl(RE::FormID formID, int depth) {
            if (formID == 0 || formID == kUnknownFatherFormID) {
                return "Nord";
            }

            if (IsPlayerFormID(formID)) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* race = player->GetRace()) {
                        if (const char* name = race->GetFullName(); name && *name) {
                            return std::string(name);
                        }
                        if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                            return std::string(edid);
                        }
                    }
                }
            }

            if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(formID)) {
                if (!entry->raceName.empty()) {
                    return entry->raceName;
                }
            }

            if (depth < 2) {
                if (const auto* child = FindChildRecord(formID)) {
                    const auto isValidParentID = [&](RE::FormID pid) noexcept {
                        return CanLookupEngineForm(pid) && pid != formID;
                    };

                    const RE::FormID candidates[] = {
                        child->bioMother.raw(),
                        child->bioFather.raw(),
                        child->guardians.slotA.raw(),
                        child->guardians.slotB.raw()
                    };

                    for (const RE::FormID candidateID : candidates) {
                        if (isValidParentID(candidateID)) {
                            return GetRaceNameImpl(candidateID, depth + 1);
                        }
                    }
                }
            }

            if (auto* npc = GetActorBase(formID)) {
                if (npc->race) {
                    if (const char* name = npc->race->GetFullName(); name && *name) {
                        return std::string(name);
                    }
                    if (const char* edid = npc->race->GetFormEditorID(); edid && *edid) {
                        return std::string(edid);
                    }
                }
            }
            return "Nord";
        }
    }  // namespace

    RE::TESNPC* GetActorBase(RE::FormID formID) {
        if (!CanLookupEngineForm(formID)) return nullptr;

        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(formID);
            entry && entry->baseFormID != 0) {
            if (auto* base = RE::TESForm::LookupByID<RE::TESNPC>(entry->baseFormID); base && !base->IsDeleted()) {
                return base;
            }
        }

        if (auto* form = RE::TESForm::LookupByID(formID); form && !form->IsDeleted()) {
            if (auto* npc = form->As<RE::TESNPC>()) {
                return npc;
            }
            if (auto* actor = form->As<RE::Actor>()) {
                return actor->GetActorBase();
            }
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject()) {
                    return baseObj->As<RE::TESNPC>();
                }
            }
        }

        if ((formID & 0xFF000000u) != 0xFF000000u) {
            const RE::FormID baseID = SpatialWorldIndex::GetSingleton().GetBaseID(formID);
            if (CanLookupEngineForm(baseID) && baseID != formID) {
                if (auto* baseForm = RE::TESForm::LookupByID<RE::TESNPC>(baseID); baseForm && !baseForm->IsDeleted()) {
                    return baseForm;
                }
            }
        }

        return nullptr;
    }

    RE::TESObjectREFR* GetReference(RE::FormID formID) {
        if (!CanLookupEngineForm(formID)) return nullptr;

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                return refr;
            }
        }

        const RE::FormID refID = SpatialWorldIndex::GetSingleton().GetRefID(formID);
        if (CanLookupEngineForm(refID) && refID != formID) {
            if (auto* form = RE::TESForm::LookupByID(refID)) {
                return form->As<RE::TESObjectREFR>();
            }
        }

        return nullptr;
    }

    std::string GetName(RE::FormID formID) {
        if (formID == 0) return "None";
        if (formID == kUnknownFatherFormID) return "Unknown";

        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(formID)) {
            if (!entry->nameDisplay.empty()) {
                return entry->nameDisplay;
            }
        }

        if (const auto* child = FindChildRecord(formID); child && !child->name.empty()) {
            return child->name;
        }

        if (IsSyntheticChildID(formID)) {
            return fmt::format("Child_{:08X}", formID);
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (auto* actor = form->As<RE::Actor>()) {
                if (const char* name = actor->GetDisplayFullName(); name && *name) {
                    return std::string(name);
                }
                if (auto* base = actor->GetActorBase()) {
                    if (const char* name = base->GetName(); name && *name) {
                        return std::string(name);
                    }
                }
            }

            if (auto* npc = form->As<RE::TESNPC>()) {
                if (const char* name = npc->GetName(); name && *name) {
                    return std::string(name);
                }
                if (const char* name = npc->GetFullName(); name && *name) {
                    return std::string(name);
                }
            }

            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (const char* name = refr->GetDisplayFullName(); name && *name) {
                    return std::string(name);
                }
                if (auto* baseObj = refr->GetBaseObject()) {
                    if (auto* npcBase = baseObj->As<RE::TESNPC>()) {
                        if (const char* name = npcBase->GetName(); name && *name) {
                            return std::string(name);
                        }
                    }
                }
            }
        }

        const RE::FormID refID = SpatialWorldIndex::GetSingleton().GetRefID(formID);
        if (CanLookupEngineForm(refID) && refID != formID) {
            if (auto* refForm = RE::TESForm::LookupByID(refID)) {
                if (auto* refr = refForm->As<RE::TESObjectREFR>()) {
                    if (const char* name = refr->GetDisplayFullName(); name && *name) {
                        return std::string(name);
                    }
                }
            }
        }

        return fmt::format("NPC_{:08X}", formID);
    }

    std::string GetRaceName(RE::FormID formID) {
        return GetRaceNameImpl(formID, 0);
    }

    RE::TESRace* GetRace(RE::FormID formID) {
        if (!CanLookupEngineForm(formID)) return nullptr;

        if (IsPlayerFormID(formID)) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                return player->GetRace();
            }
        }
        if (auto* refr = GetReference(formID)) {
            if (auto* actor = refr->As<RE::Actor>()) {
                return actor->GetRace();
            }
        }
        if (auto* npc = GetActorBase(formID)) {
            return npc->race;
        }
        return nullptr;
    }

    std::string GetRaceEditorID(RE::FormID formID) {
        if (auto* race = GetRace(formID)) {
            if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                return std::string(edid);
            }
        }
        return "";
    }

    std::string GetPluginName(RE::FormID formID) {
        if (formID == 0 || formID == kUnknownFatherFormID) return "";
        if (IsPlayerFormID(formID)) return "Skyrim.esm";

        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(formID)) {
            if (!entry->pluginName.empty()) {
                return entry->pluginName;
            }
        }

        if (const auto* child = FindChildRecord(formID)) {
            return child->isBiologicalChild ? "Fertility Mode" : "MARAS";
        }

        if (IsSyntheticChildID(formID)) {
            return "Fertility Mode";
        }

        if (auto* base = GetActorBase(formID)) {
            const auto* file = base->GetFile(0);
            if (file && file->fileName && file->fileName[0] != '\0') {
                return std::string(file->fileName);
            }
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            const auto* file = form->GetFile(0);
            if (file && file->fileName && file->fileName[0] != '\0') {
                return std::string(file->fileName);
            }
        }

        return "Unknown";
    }

    std::uint8_t GetSex(RE::FormID formID) {
        if (auto* npc = GetActorBase(formID)) {
            return static_cast<std::uint8_t>(npc->GetSex());
        }
        if (const auto* child = FindChildRecord(formID);
            child && (child->gender == 0 || child->gender == 1)) {
            return child->gender;
        }
        return 0;
    }

    bool IsValid(RE::FormID formID) {
        if (formID == 0 || formID == kUnknownFatherFormID) return false;

        if (IsSyntheticChildID(formID)) {
            return FindChildRecord(formID) != nullptr;
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (form->IsDeleted()) return false;

            if (form->GetFormType() == RE::FormType::ActorCharacter || form->GetFormType() == RE::FormType::NPC) {
                return true;
            }

            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject()) {
                    return !baseObj->IsDeleted() && baseObj->GetFormType() == RE::FormType::NPC;
                }
            }
            return false;
        }

        if ((formID & 0xFF000000u) != 0xFF000000u) {
            const RE::FormID baseID = SpatialWorldIndex::GetSingleton().GetBaseID(formID);
            if (CanLookupEngineForm(baseID) && baseID != formID) {
                if (auto* baseForm = RE::TESForm::LookupByID<RE::TESNPC>(baseID)) {
                    return !baseForm->IsDeleted();
                }
            }
        }

        return false;
    }

}  // namespace MARAS::ActorSvc

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

            if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id);
                entry && !entry->raceName.empty()) {
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
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(actor.raw())) {
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
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id);
            entry && !entry->nameDisplay.empty()) {
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
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id)) {
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
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id);
            entry && entry->baseFormID != 0) {
            return entry->baseFormID;
        }
        if (const auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(id)) {
            return npc->GetFormID();
        }
        return SpatialWorldIndex::GetSingleton().GetBaseID(id);
    }

}  // namespace MARAS
