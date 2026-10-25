#include "services/ActorService.h"
#include "core/LoggingService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"

namespace MARAS::ActorSvc {

    namespace {
        constexpr bool IsPlayerFormID(RE::FormID formID) noexcept {
            return formID == kPlayerRefID || formID == kPlayerBaseID;
        }

        constexpr bool CanLookupEngineForm(RE::FormID formID) noexcept {
            return formID != 0 && formID != kUnknownFatherFormID && !IsSyntheticChildID(formID);
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

            if (auto entry = NPCIndex::GetSingleton().FindByFormID(formID)) {
                if (!entry->raceName.empty()) {
                    return entry->raceName;
                }
            }

            if (depth < 2) {
                auto& mgr = NPCRelationshipManager::GetSingleton();
                if (auto childOpt = mgr.GetChildEntry(formID); childOpt.has_value()) {
                    auto isValidParentID = [&](RE::FormID pid) noexcept {
                        return CanLookupEngineForm(pid) && pid != formID;
                    };

                    const RE::FormID candidates[] = {
                        mgr.GetBioMother(formID),
                        childOpt->bioFatherID,
                        childOpt->guardianAID,
                        childOpt->guardianBID
                    };

                    for (RE::FormID candidateID : candidates) {
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

        if (auto* form = RE::TESForm::LookupByID(formID)) {
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

        return nullptr;
    }

    RE::TESObjectREFR* GetReference(RE::FormID formID) {
        if (!CanLookupEngineForm(formID)) return nullptr;

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                return refr;
            }
        }

        RE::FormID refID = NPCIndex::GetSingleton().GetRefID(formID);
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

        // Fast path: check NPCIndex
        if (auto entry = NPCIndex::GetSingleton().FindByFormID(formID)) {
            if (!entry->nameDisplay.empty()) {
                return entry->nameDisplay;
            }
        }

        // Check child registry for synthetic/registered children
        if (auto childOpt = NPCRelationshipManager::GetSingleton().GetChildEntry(formID); childOpt.has_value() && !childOpt->name.empty()) {
            return childOpt->name;
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

        // Try getting ref ID name fallback if formID was base
        RE::FormID refID = NPCIndex::GetSingleton().GetRefID(formID);
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

        if (auto entry = NPCIndex::GetSingleton().FindByFormID(formID)) {
            if (!entry->pluginName.empty()) {
                return entry->pluginName;
            }
        }

        if (auto childOpt = NPCRelationshipManager::GetSingleton().GetChildEntry(formID); childOpt.has_value()) {
            return childOpt->isBiologicalChild ? "Fertility Mode" : "MARAS";
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

    uint8_t GetSex(RE::FormID formID) {
        if (auto* npc = GetActorBase(formID)) {
            return static_cast<uint8_t>(npc->GetSex());
        }
        if (auto childOpt = NPCRelationshipManager::GetSingleton().GetChildEntry(formID);
            childOpt.has_value() && (childOpt->gender == 0 || childOpt->gender == 1)) {
            return childOpt->gender;
        }
        return 0;
    }

    bool IsValid(RE::FormID formID) {
        if (formID == 0 || formID == kUnknownFatherFormID) return false;

        if (IsSyntheticChildID(formID)) {
            return NPCRelationshipManager::GetSingleton().GetChildEntry(formID).has_value();
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) return false;

        if (form->GetFormType() == RE::FormType::ActorCharacter || form->GetFormType() == RE::FormType::NPC) {
            return true;
        }

        if (auto* refr = form->As<RE::TESObjectREFR>()) {
            if (auto* baseObj = refr->GetBaseObject()) {
                return baseObj->GetFormType() == RE::FormType::NPC;
            }
        }

        return false;
    }

}  // namespace MARAS::ActorSvc
