#include "maras/engine/ActorService.h"

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
