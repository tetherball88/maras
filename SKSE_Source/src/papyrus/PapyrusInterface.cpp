#include "papyrus/PapyrusInterface.h"

#include "core/BanterCoordinatorService.h"
#include "core/ChildrenFleeService.h"
#include "core/DeferredActorResolver.h"
#include "core/IntimateThreadService.h"
#include "core/LoggingService.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PlayerHouseService.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetIntegration.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "ui/ConfirmationModal.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "ui/PrismaUIManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"

namespace MARAS::PapyrusInterface {

    // ========================================
    // Debug & logging functions
    // ========================================

    void Log(RE::StaticFunctionTag*, std::string msg, std::int32_t logLevel) {
        switch (logLevel) {
            case 0:
                MARAS_LOG_TRACE("Papyrus: {}", msg);
                break;
            case 1:
                MARAS_LOG_DEBUG("Papyrus: {}", msg);
                break;
            case 2:
                MARAS_LOG_WARN("Papyrus: {}", msg);
                break;
            case 3:
                MARAS_LOG_ERROR("Papyrus: {}", msg);
                break;
            default:
                break;
        }
    }

    void LogNPCStatistics(RE::StaticFunctionTag*) {
        NPCRelationshipManager::GetSingleton().LogStatistics();
    }

    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("LogNPCDetails called with null NPC");
            return;
        }
        NPCRelationshipManager::GetSingleton().LogNPCDetails(npc->GetFormID());
    }

    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS::LoggingService::GetSingleton().SetLogLevel(static_cast<int32_t>(logLevel));
    }

    std::int32_t GetLogLevel(RE::StaticFunctionTag*) {
        return MARAS::LoggingService::GetSingleton().GetLogLevel();
    }

    // ========================================
    // Keyword management bindings
    // ========================================

    bool HasNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("HasNpcKeyword: null argument(s) provided");
            return false;
        }
        return npc->HasKeyword(keyword);
    }

    bool AddNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("AddNpcKeyword: null argument(s) provided");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("AddNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        if (base->HasKeyword(keyword)) {
            return false;
        }
        const auto keywordID = keyword->GetFormID();
        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (!base->AddKeyword(keyword)) {
                return false;
            }
            NPCRelationshipManager::GetSingleton().AddTrackedKeyword(formID, keywordID);
            return true;
        }
        NPCRelationshipManager::GetSingleton().AddTrackedKeyword(formID, keywordID);
        AddCurrentEpochTask("PapyrusInterface::AddNpcKeyword", [formID, keywordID]() {
            std::lock_guard taskLock(GetPapyrusMutex());
            auto* mainBase = ActorSvc::GetActorBase(formID);
            auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(keywordID);
            if (mainBase && kw && !mainBase->HasKeyword(kw)) {
                mainBase->AddKeyword(kw);
            }
        });
        return true;
    }

    bool RemoveNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("RemoveNpcKeyword: null argument(s) provided");
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("RemoveNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        if (!base->HasKeyword(keyword)) {
            return false;
        }
        const auto keywordID = keyword->GetFormID();
        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (!base->RemoveKeyword(keyword)) {
                return false;
            }
            NPCRelationshipManager::GetSingleton().RemoveTrackedKeyword(formID, keywordID);
            return true;
        }
        NPCRelationshipManager::GetSingleton().RemoveTrackedKeyword(formID, keywordID);
        AddCurrentEpochTask("PapyrusInterface::RemoveNpcKeyword", [formID, keywordID]() {
            std::lock_guard taskLock(GetPapyrusMutex());
            auto* mainBase = ActorSvc::GetActorBase(formID);
            auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(keywordID);
            if (mainBase && kw && mainBase->HasKeyword(kw)) {
                mainBase->RemoveKeyword(kw);
            }
        });
        return true;
    }

    // ========================================
    // JSON string utility bindings
    // ========================================

    std::string GetJsonString(RE::StaticFunctionTag*, std::string json, std::string path, std::string defaultVal) {
        return MARAS::JsonService::GetString(json.c_str(), path.c_str(), defaultVal.c_str());
    }

    RE::Actor* GetJsonActor(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetActor(json.c_str(), path.c_str());
    }

    RE::TESForm* GetJsonForm(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetForm(json.c_str(), path.c_str());
    }

    int32_t GetJsonInt(RE::StaticFunctionTag*, std::string json, std::string path, int32_t defaultVal) {
        return MARAS::JsonService::GetInt(json.c_str(), path.c_str(), defaultVal);
    }

    float GetJsonFloat(RE::StaticFunctionTag*, std::string json, std::string path, float defaultVal) {
        return MARAS::JsonService::GetFloat(json.c_str(), path.c_str(), defaultVal);
    }

    bool GetJsonBool(RE::StaticFunctionTag*, std::string json, std::string path, bool defaultVal) {
        return MARAS::JsonService::GetBool(json.c_str(), path.c_str(), defaultVal);
    }

    int32_t GetJsonArrayLength(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetArrayLength(json.c_str(), path.c_str());
    }

    std::vector<RE::Actor*> GetJsonActorArray(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetActorArray(json.c_str(), path.c_str());
    }

    // ========================================
    // Prisma UI & Confirmation Modal bindings
    // ========================================

    RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                     RE::VMStackID stackID,
                                                     RE::StaticFunctionTag*,
                                                     RE::BSFixedString actionType,
                                                     std::vector<RE::Actor*> mainActors,
                                                     std::vector<RE::Actor*> secondaryActors,
                                                     RE::BSFixedString paramsJson) {
        MARAS_LOG_INFO("PapyrusInterface: ShowConfirmationModal actionType='{}' mainActors={} secondaryActors={} paramsJson='{}'",
                       actionType.c_str(), mainActors.size(), secondaryActors.size(), paramsJson.c_str());
        return ConfirmationModal::GetSingleton().Show(
            vm, stackID,
            actionType.c_str(), mainActors, secondaryActors,
            paramsJson.c_str());
    }

    RE::BSScript::LatentStatus ShowSkipWeddingModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                    RE::VMStackID stackID,
                                                    RE::StaticFunctionTag*,
                                                    RE::Actor* fiance) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowSkipWeddingModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        std::string fianceName = fiance ? ActorSvc::GetName(fiance->GetFormID()) : "your fiance";
        if (fianceName.empty()) {
            fianceName = "your fiance";
        }

        ModalRequest req;
        req.title = "Wedding Ceremony";
        req.subtitle = fianceName;
        req.message = "Would you like to skip the formal Temple of Mara wedding ceremony with " + fianceName +
                      " and marry immediately, or hold a traditional ceremony?";
        req.tone = ModalTone::Default;
        req.size = ModalSize::Sm;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeCheckbox(
            "dont_ask_again",
            "Remember my choice for future engagements",
            false));

        req.buttons.push_back(ModalButton::Make("skip_wedding", "Marry Immediately (Skip Ceremony)", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("hold_wedding", "Hold Wedding Ceremony", ModalButtonVariant::Secondary));

        MARAS_LOG_INFO("PapyrusInterface: ShowSkipWeddingModal queuing modal for '{}'", fianceName);

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted) {
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, 1, "ShowSkipWeddingModal");
                    return;
                }

                const bool dontAskAgain = resp.GetBool("dont_ask_again", false);
                if (resp.buttonIndex == 0 || resp.buttonId == "skip_wedding") {
                    if (dontAskAgain) {
                        std::lock_guard lock(GetPapyrusMutex());
                        Settings::GetSingleton().SetBoolByKey("skipWedding", true);
                        PrismaUIDataBridge::GetSingleton().SendSettingsData();
                        MARAS_LOG_INFO("PapyrusInterface: ShowSkipWeddingModal - persisted skipWedding=true");
                    }
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, 0, "ShowSkipWeddingModal");
                } else {
                    int32_t result = dontAskAgain ? 2 : 1;
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, result, "ShowSkipWeddingModal");
                }
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ShowDivorceSettlementModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                          RE::VMStackID stackID,
                                                          RE::StaticFunctionTag*,
                                                          RE::Actor* exSpouse,
                                                          int32_t totalFee,
                                                          int32_t goldShare,
                                                          int32_t assetsShare,
                                                          bool canPayDirectly) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowDivorceSettlementModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        std::string spouseName = exSpouse ? ActorSvc::GetName(exSpouse->GetFormID()) : "";

        ModalRequest req;
        req.title = "Divorce Settlement Notice";
        req.subtitle = !spouseName.empty() ? spouseName : "Hold Court";
        req.message = !spouseName.empty()
            ? ("A hold court has issued a divorce settlement ruling following your divorce from " + spouseName + ".")
            : "A hold court has issued a divorce settlement ruling.";
        req.tone = ModalTone::Danger;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = canPayDirectly ? 1 : 0;

        std::string breakdownText = fmt::format(
            "Court Filing Fee: 100 Gold\n"
            "Liquid Wealth Share: {} Gold\n"
            "Property & Renown Assessment: {} Gold\n"
            "Total Settlement Due: {} Gold",
            goldShare, assetsShare, totalFee);

        req.controls.push_back(ModalControl::MakeInfoBox(
            "fee_breakdown",
            "Settlement Breakdown",
            breakdownText,
            ModalTone::Warning));

        if (canPayDirectly) {
            req.buttons.push_back(ModalButton::Make(
                "pay_now",
                fmt::format("Pay Settlement Now ({} Gold)", totalFee),
                ModalButtonVariant::Primary));
            req.buttons.push_back(ModalButton::Make(
                "refuse_bounty",
                "Refuse / Add to Hold Bounty",
                ModalButtonVariant::Danger));
        } else {
            req.buttons.push_back(ModalButton::Make(
                "accept_bounty",
                fmt::format("Accept Hold Bounty ({} Gold)", totalFee),
                ModalButtonVariant::Danger));
        }

        MARAS_LOG_INFO("PapyrusInterface: ShowDivorceSettlementModal queuing modal for '{}' (fee={}, canPayDirectly={})",
                       req.subtitle, totalFee, canPayDirectly);

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID, canPayDirectly](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted) {
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, 1, "ShowDivorceSettlementModal");
                    return;
                }

                if (canPayDirectly && (resp.buttonIndex == 0 || resp.buttonId == "pay_now")) {
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, 0, "ShowDivorceSettlementModal");
                } else {
                    DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, 1, "ShowDivorceSettlementModal");
                }
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ShowChooseHomeModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                   RE::VMStackID stackID,
                                                   RE::StaticFunctionTag*,
                                                   RE::Actor* targetActor,
                                                   bool isChild) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowChooseHomeModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto houses = houseSvc.GetAllPlayerHouses();
        if (houses.empty()) {
            MARAS_LOG_WARN("PapyrusInterface: ShowChooseHomeModal called with no registered player houses");
            if (AddEpochTask(expectedEpoch, "PapyrusInterface::ShowChooseHomeModalEmpty", [vm, stackID]() {
                    vm->ReturnLatentResult<RE::BGSLocation*>(stackID, nullptr);
                })) {
                return RE::BSScript::LatentStatus::kStarted;
            }
            return RE::BSScript::LatentStatus::kFailed;
        }

        const RE::FormID targetFormID = targetActor ? targetActor->GetFormID() : 0;
        std::string targetName = targetActor ? ActorSvc::GetName(targetFormID) : "";
        if (targetName.empty()) {
            targetName = isChild ? "Child" : "Spouse";
        }

        const RE::FormID currentHouseID = (targetFormID != 0) ? houseSvc.GetTenantHouseFormID(targetFormID) : 0;
        std::vector<ModalSelectOption> houseOptions;
        houseOptions.reserve(houses.size());
        std::string defaultHouseVal;

        for (RE::FormID houseID : houses) {
            std::string houseName = PlayerHouseService::GetHouseName(houseID);
            if (houseName.empty()) {
                houseName = fmt::format("Home ({:08X})", houseID);
            }
            const auto tenants = houseSvc.GetPlayerHouseTenants(houseID);
            std::string label = fmt::format("{} ({} resident{})",
                                            houseName,
                                            tenants.size(),
                                            tenants.size() == 1 ? "" : "s");
            std::string valStr = std::to_string(houseID);
            houseOptions.push_back({valStr, std::move(label)});
            if (defaultHouseVal.empty() || houseID == currentHouseID) {
                defaultHouseVal = valStr;
            }
        }

        ModalRequest req;
        req.title = isChild ? "Choose Child Residence" : "Choose Spouse Residence";
        req.subtitle = targetName;
        req.message = isChild
            ? ("Select which player home " + targetName + " should live in.")
            : ("Select which player home " + targetName + " should live in and how they should spend their daily schedule.");
        req.tone = ModalTone::Default;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeSelect(
            "house_id",
            "Residence",
            std::move(houseOptions),
            defaultHouseVal));

        if (!isChild) {
            const bool currentStayAtHome = (targetFormID != 0) ? houseSvc.GetTenantStayAtHome(targetFormID) : true;
            std::vector<ModalSelectOption> schedOptions = {
                {"stay", "Stay at Home (Sandbox in Residence)"},
                {"original", "Keep Original AI Schedule"}};
            req.controls.push_back(ModalControl::MakeSelect(
                "schedule_mode",
                "Spouse Schedule",
                std::move(schedOptions),
                currentStayAtHome ? "stay" : "original"));
        }

        req.buttons.push_back(ModalButton::Make("confirm", "Move to Residence", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("cancel", "Cancel", ModalButtonVariant::Secondary));

        MARAS_LOG_INFO("PapyrusInterface: ShowChooseHomeModal queuing modal for '{}' (isChild={}, houses={})",
                       targetName, isChild, houses.size());

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID, targetFormID, isChild](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "confirm")) {
                    DispatchLatentFormResult<RE::BGSLocation>(expectedEpoch, vm, stackID, 0, "ShowChooseHomeModal");
                    return;
                }

                const std::string houseIdStr = resp.GetString("house_id", "");
                RE::FormID selectedHouseID = 0;
                if (!houseIdStr.empty()) {
                    try {
                        selectedHouseID = static_cast<RE::FormID>(std::stoul(houseIdStr));
                    } catch (...) {
                        selectedHouseID = 0;
                    }
                }

                auto* chosenLoc = selectedHouseID != 0
                    ? RE::TESForm::LookupByID<RE::BGSLocation>(selectedHouseID)
                    : nullptr;
                const RE::FormID resolvedHouseID = chosenLoc ? selectedHouseID : 0;

                if (chosenLoc && !isChild && targetFormID != 0) {
                    const bool stayAtHome = (resp.GetString("schedule_mode", "stay") != "original");
                    std::lock_guard lock(GetPapyrusMutex());
                    auto& svc = PlayerHouseService::GetSingleton();
                    svc.RelocateFamilyUnit(targetFormID, selectedHouseID, true);
                    svc.SetTenantStayAtHome(targetFormID, stayAtHome);
                }

                DispatchLatentFormResult<RE::BGSLocation>(expectedEpoch, vm, stackID, resolvedHouseID, "ShowChooseHomeModal");
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ShowChooseAdoptionSpouseModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                             RE::VMStackID stackID,
                                                             RE::StaticFunctionTag*,
                                                             RE::Actor* child) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowChooseAdoptionSpouseModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto spouses = relMgr.GetAllMarried();
        if (spouses.empty()) {
            if (AddEpochTask(expectedEpoch, "PapyrusInterface::ShowChooseAdoptionSpouseModalEmpty", [vm, stackID]() {
                    vm->ReturnLatentResult<RE::Actor*>(stackID, nullptr);
                })) {
                return RE::BSScript::LatentStatus::kStarted;
            }
            return RE::BSScript::LatentStatus::kFailed;
        }

        std::vector<ModalSelectOption> spouseOptions;
        spouseOptions.reserve(spouses.size() + 1);
        std::string defaultSpouseVal;

        for (RE::FormID spouseID : spouses) {
            auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(spouseID);
            if (!spouseActor) {
                continue;
            }
            std::string spouseName = ActorSvc::GetName(spouseID);
            if (spouseName.empty()) {
                spouseName = fmt::format("Spouse ({:08X})", spouseID);
            }

            RE::FormID houseID = houseSvc.GetTenantHouseFormID(spouseID);
            std::string label;
            if (houseID != 0) {
                std::string houseName = PlayerHouseService::GetHouseName(houseID);
                label = fmt::format("{} — {}", spouseName, !houseName.empty() ? houseName : "Player Home");
            } else {
                label = fmt::format("{} (Not Assigned to a Player Home)", spouseName);
            }

            std::string valStr = std::to_string(spouseID);
            spouseOptions.push_back({valStr, std::move(label)});
            if (defaultSpouseVal.empty()) {
                defaultSpouseVal = valStr;
            }
        }

        if (defaultSpouseVal.empty()) {
            if (AddEpochTask(expectedEpoch, "PapyrusInterface::ShowChooseAdoptionSpouseModalNoneLoaded", [vm, stackID]() {
                    vm->ReturnLatentResult<RE::Actor*>(stackID, nullptr);
                })) {
                return RE::BSScript::LatentStatus::kStarted;
            }
            return RE::BSScript::LatentStatus::kFailed;
        }

        spouseOptions.push_back({"0", "None (Adopt as Single Parent)"});

        std::string childName = child ? ActorSvc::GetName(child->GetFormID()) : "";

        ModalRequest req;
        req.title = "Adopt Child — Select Co-Parent";
        req.subtitle = !childName.empty() ? childName : "Child Adoption";
        req.message = !childName.empty()
            ? ("Choose which of your spouses will co-parent " + childName + " with you, or adopt as a single parent.")
            : "Choose which of your spouses will co-parent this child with you, or adopt as a single parent.";
        req.tone = ModalTone::Default;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeSelect(
            "spouse_id",
            "Co-Parent Spouse",
            std::move(spouseOptions),
            defaultSpouseVal));

        req.buttons.push_back(ModalButton::Make("confirm", "Confirm Co-Parent", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("single_parent", "Adopt as Single Parent", ModalButtonVariant::Secondary));

        MARAS_LOG_INFO("PapyrusInterface: ShowChooseAdoptionSpouseModal queuing modal (child='{}', spouses={})",
                       req.subtitle, spouses.size());

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "confirm")) {
                    DispatchLatentFormResult<RE::Actor>(expectedEpoch, vm, stackID, 0, "ShowChooseAdoptionSpouseModal");
                    return;
                }

                const std::string spouseIdStr = resp.GetString("spouse_id", "0");
                RE::FormID selectedSpouseID = 0;
                if (!spouseIdStr.empty() && spouseIdStr != "0") {
                    try {
                        selectedSpouseID = static_cast<RE::FormID>(std::stoul(spouseIdStr));
                    } catch (...) {
                        selectedSpouseID = 0;
                    }
                }

                DispatchLatentFormResult<RE::Actor>(expectedEpoch, vm, stackID, selectedSpouseID, "ShowChooseAdoptionSpouseModal");
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ShowDebugStatusModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                    RE::VMStackID stackID,
                                                    RE::StaticFunctionTag*,
                                                    RE::Actor* target) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowDebugStatusModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        if (!target) {
            if (AddEpochTask(expectedEpoch, "PapyrusInterface::ShowDebugStatusModalNullTarget", [vm, stackID]() {
                    vm->ReturnLatentResult<RE::BSFixedString>(stackID, RE::BSFixedString(""));
                })) {
                return RE::BSScript::LatentStatus::kStarted;
            }
            return RE::BSScript::LatentStatus::kFailed;
        }

        const RE::FormID targetID = target->GetFormID();
        std::string targetName = ActorSvc::GetName(targetID);
        if (targetName.empty()) {
            targetName = fmt::format("NPC ({:08X})", targetID);
        }

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        std::string currentStatus = "candidate";
        if (relMgr.IsRegistered(targetID)) {
            currentStatus = std::string(Utils::RelationshipStatusToString(relMgr.GetRelationshipStatus(kPlayerFormID, targetID)));
            for (char& ch : currentStatus) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }

        std::vector<ModalSelectOption> statusOptions = {
            {"candidate", "Candidate"},
            {"dating", "Dating"},
            {"lover", "Lover"},
            {"engaged", "Engaged"},
            {"married", "Married"},
            {"exdating", "Ex-Dating"},
            {"exlover", "Ex-Lover"},
            {"jilted", "Jilted"},
            {"divorced", "Divorced"},
            {"unregister", "Unregister from MARAS"}};

        ModalRequest req;
        req.title = "Debug Relationship Status";
        req.subtitle = targetName;
        req.message = fmt::format("Current MARAS status with Player: '{}'. Select a new status to apply:", currentStatus);
        req.tone = ModalTone::Default;
        req.size = ModalSize::Sm;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeSelect(
            "target_status",
            "Target Relationship Status",
            std::move(statusOptions),
            currentStatus));

        req.buttons.push_back(ModalButton::Make("apply", "Apply Status", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("cancel", "Cancel", ModalButtonVariant::Secondary));

        MARAS_LOG_INFO("PapyrusInterface: ShowDebugStatusModal queuing modal for '{}' (current='{}')",
                       targetName, currentStatus);

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "apply")) {
                    DispatchLatentResult<RE::BSFixedString>(expectedEpoch, vm, stackID, RE::BSFixedString(""), "ShowDebugStatusModal");
                    return;
                }

                const std::string chosen = resp.GetString("target_status", "");
                DispatchLatentResult<RE::BSFixedString>(expectedEpoch, vm, stackID, RE::BSFixedString(chosen), "ShowDebugStatusModal");
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ShowChildMigrationAuditModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                            RE::VMStackID stackID,
                                                            RE::StaticFunctionTag*) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("PapyrusInterface: ShowChildMigrationAuditModal - SKSE task interface unavailable");
            return RE::BSScript::LatentStatus::kFailed;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto playerView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId::Player(), &GetEngineWerewolfStateProvider());

        std::vector<RE::FormID> migratedChildIDs;
        std::vector<ModalChecklistItem> items;

        for (const auto& perceived : playerView.GetVisibleChildren()) {
            const RE::FormID cID = perceived.childId.raw();
            const bool isTempRef = ((cID >> 24) == 0xFF) && !IsSyntheticChildID(cID);
            const bool involvesPlayer =
                perceived.guardians.HasGuardian(State::ActorId::Player()) ||
                perceived.bioMother.IsPlayer() ||
                (!perceived.hideSecretFromViewer && perceived.perceivedBioFather.IsPlayer()) ||
                (!perceived.hideSecretFromViewer && perceived.presumedFather.IsPlayer());
            if (!perceived.isBiologicalChild && !isTempRef && !involvesPlayer) {
                continue;
            }

            migratedChildIDs.push_back(cID);

            ModalChecklistItem item;
            item.id = fmt::format("child_{:08X}", cID);
            item.label = !perceived.name.empty() ? perceived.name : ResolveActorNameSafe(cID, *worldSnap, *spatialSnap);
            if (item.label.empty()) {
                item.label = fmt::format("Child ({:08X})", cID);
            }

            switch (perceived.stage) {
                case State::ChildStage::Unborn:
                    item.badgeText = "Pregnancy";
                    item.badgeTone = "warning";
                    break;
                case State::ChildStage::Infant:
                    item.badgeText = "Infant";
                    item.badgeTone = "accent";
                    break;
                case State::ChildStage::Child:
                    item.badgeText = perceived.isAdopted ? "Adopted" : "Child";
                    item.badgeTone = perceived.isAdopted ? "accent" : "success";
                    break;
                case State::ChildStage::Training:
                    item.badgeText = "Training";
                    item.badgeTone = "accent";
                    break;
                case State::ChildStage::Adult:
                    item.badgeText = "Adult Child";
                    item.badgeTone = "neutral";
                    break;
            }

            auto resolveActorName = [&](State::ActorId actorId) -> std::string {
                if (actorId.IsNull() || actorId.raw() == kUnknownFatherFormID) {
                    return "";
                }
                return ResolveActorNameSafe(actorId.raw(), *worldSnap, *spatialSnap);
            };

            if (perceived.isAdopted) {
                std::string gAName = resolveActorName(perceived.guardians.slotA);
                std::string gBName = resolveActorName(perceived.guardians.slotB);
                if (!gAName.empty() && !gBName.empty()) {
                    item.subtitle = fmt::format("Adoptive Guardians: {} & {}", gAName, gBName);
                } else if (!gAName.empty()) {
                    item.subtitle = fmt::format("Adoptive Guardian: {}", gAName);
                } else if (!gBName.empty()) {
                    item.subtitle = fmt::format("Adoptive Guardian: {}", gBName);
                } else {
                    item.subtitle = "Adopted into Household";
                }
            } else {
                std::string motherName = resolveActorName(perceived.bioMother);
                std::string fatherName = resolveActorName(perceived.perceivedBioFather);
                if (fatherName.empty() && !perceived.perceivedBioFatherFallbackName.empty()) {
                    fatherName = perceived.perceivedBioFatherFallbackName;
                }

                if (!motherName.empty() && !fatherName.empty()) {
                    item.subtitle = fmt::format("Parents: {} & {}", motherName, fatherName);
                } else if (!motherName.empty()) {
                    item.subtitle = fmt::format("Mother: {}", motherName);
                } else if (!fatherName.empty()) {
                    item.subtitle = fmt::format("Father: {}", fatherName);
                } else {
                    item.subtitle = "Migrated Fertility Record";
                }
            }

            item.checked = true;
            items.push_back(std::move(item));
        }

        if (items.empty()) {
            if (AddEpochTask(expectedEpoch, "PapyrusInterface::ShowChildMigrationAuditModalEmpty", [vm, stackID]() {
                    vm->ReturnLatentResult<int32_t>(stackID, 0);
                })) {
                return RE::BSScript::LatentStatus::kStarted;
            }
            return RE::BSScript::LatentStatus::kFailed;
        }

        ModalRequest req;
        req.title = "Detected Pregnancies & Children";
        req.subtitle = "First-Time Fertility & Adoption Migration";
        req.message = "M.A.R.A.S migrated the following existing pregnancies, biological children, and adopted children into its household registry. "
                      "Uncheck any stale or unwanted records you wish to discard:";
        req.tone = ModalTone::Default;
        req.size = ModalSize::Lg;
        req.defaultCancelButtonIndex = 0;

        req.controls.push_back(ModalControl::MakeChecklist(
            "migrated_children",
            "Migrated Pregnancies & Children",
            std::move(items),
            true,
            "Unchecked entries will be removed from the M.A.R.A.S child and household registry."));

        req.buttons.push_back(ModalButton::Make("keep_selected", "Keep Selected", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("discard_all", "Discard All", ModalButtonVariant::Secondary));

        MARAS_LOG_INFO("PapyrusInterface: ShowChildMigrationAuditModal queuing modal with {} migrated entries",
                       migratedChildIDs.size());

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch, vm, stackID, childIDs = std::move(migratedChildIDs)](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(GetPapyrusMutex());
                const bool discardAll = (!resp.aborted && (resp.buttonIndex == 1 || resp.buttonId == "discard_all"));
                auto& mgr = NPCRelationshipManager::GetSingleton();
                int32_t keptCount = 0;
                int32_t removedCount = 0;

                std::unordered_set<RE::FormID> discardedParents;
                for (RE::FormID cID : childIDs) {
                    if (auto entryOpt = mgr.GetChildEntry(cID)) {
                        const std::string itemIdCheck = fmt::format("child_{:08X}", cID);
                        const bool keepCheck = !discardAll && (resp.aborted || resp.IsItemChecked("migrated_children", itemIdCheck, true));
                        if (!keepCheck) {
                            if (entryOpt->guardianAID != 0 && entryOpt->guardianAID != kPlayerFormID) discardedParents.insert(entryOpt->guardianAID);
                            if (entryOpt->guardianBID != 0 && entryOpt->guardianBID != kPlayerFormID) discardedParents.insert(entryOpt->guardianBID);
                            if (entryOpt->bioMotherID != 0 && entryOpt->bioMotherID != kPlayerFormID) discardedParents.insert(entryOpt->bioMotherID);
                            if (entryOpt->bioFatherID != 0 && entryOpt->bioFatherID != kPlayerFormID && entryOpt->bioFatherID != kUnknownFatherFormID) discardedParents.insert(entryOpt->bioFatherID);
                        }
                    }
                    const std::string itemId = fmt::format("child_{:08X}", cID);
                    const bool keep = !discardAll && (resp.aborted || resp.IsItemChecked("migrated_children", itemId, true));
                    if (keep) {
                        ++keptCount;
                    } else {
                        if (mgr.RemoveChild(cID)) {
                            ++removedCount;
                        }
                    }
                }

                for (RE::FormID parentID : discardedParents) {
                    if (mgr.GetAllPartners(parentID).empty() && mgr.GetChildrenOfActor(parentID).empty()) {
                        mgr.UnregisterNPC(parentID);
                    }
                }
                if (keptCount > 0) {
                    PrismaUIDataBridge::GetSingleton().SendOverviewData();
                }
                if (removedCount > 0) {
                    PrismaUIDataBridge::GetSingleton().SendOverviewData();
                }

                MARAS_LOG_INFO("PapyrusInterface: ShowChildMigrationAuditModal completed — kept={}, removed={}",
                               keptCount, removedCount);
                DispatchLatentResult<int32_t>(expectedEpoch, vm, stackID, keptCount, "ShowChildMigrationAuditModal");
            });

        return RE::BSScript::LatentStatus::kStarted;
    }

    int32_t CheckAndSetActionCooldown(RE::StaticFunctionTag*,
                                      RE::BSFixedString actionType,
                                      std::vector<RE::Actor*> actors) {
        MARAS_LOG_INFO("PapyrusInterface: CheckAndSetActionCooldown actionType='{}' actors={}",
                       actionType.c_str(), actors.size());
        return ConfirmationModal::GetSingleton().CheckAndSet(actionType.c_str(), actors);
    }

    void ShowUI(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS_LOG_INFO("Papyrus::ShowUI called");
        PrismaUIManager::GetSingleton().Show();
    }

    void HideUI(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        MARAS_LOG_INFO("Papyrus::HideUI called");
        PrismaUIManager::GetSingleton().Hide();
    }

    bool IsUIVisible(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        bool visible = PrismaUIManager::GetSingleton().IsVisible();
        MARAS_LOG_INFO("Papyrus::IsUIVisible called -> {}", visible);
        return visible;
    }

    // ========================================
    // Intimate scene & Banter Coordinator bindings
    // ========================================

    bool RegisterIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, std::vector<RE::Actor*> actors) {
        std::lock_guard lock(GetPapyrusMutex());
        return IntimateThreadService::GetSingleton().RegisterIntimateThread(framework.c_str(), threadID, actors);
    }

    void ReportIntimateInteraction(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB) {
        std::lock_guard lock(GetPapyrusMutex());
        IntimateThreadService::GetSingleton().ReportIntimateInteraction(framework.c_str(), threadID, actorA, actorB);
    }

    void ClearIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID) {
        std::lock_guard lock(GetPapyrusMutex());
        IntimateThreadService::GetSingleton().ClearIntimateThread(framework.c_str(), threadID);
    }

    void FleeChildrenFromPlayerCell(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        ChildrenFleeService::GetSingleton().FleeFromPlayerCell();
    }

    void StopChildrenFleeing(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        ChildrenFleeService::GetSingleton().StopFleeing();
    }

    std::vector<RE::Actor*> FindBanterCoupleCandidates(RE::StaticFunctionTag*, float afScanRadius) {
        std::lock_guard lock(GetPapyrusMutex());
        return BanterCoordinatorService::GetSingleton().FindBanterCoupleCandidates(afScanRadius);
    }

    bool IsBanterPartnerPresent(RE::StaticFunctionTag*, RE::Actor* akPartner, float afScanRadius) {
        std::lock_guard lock(GetPapyrusMutex());
        return BanterCoordinatorService::GetSingleton().IsBanterPartnerPresent(akPartner, afScanRadius);
    }

    void ResetBanterCoupleCooldown(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        if (!akActorA || !akActorB) return;
        std::lock_guard lock(GetPapyrusMutex());
        BanterCoordinatorService::GetSingleton().ResetCooldown(akActorA->GetFormID(), akActorB->GetFormID());
    }

    void ClearAllBanterCooldowns(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        BanterCoordinatorService::GetSingleton().ClearAllCooldowns();
    }

    float GetBanterCoupleCooldownHours(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return BanterCoordinatorService::GetSingleton().GetCooldownHours();
    }

    void SetBanterCoupleCooldownHours(RE::StaticFunctionTag*, float afHours) {
        std::lock_guard lock(GetPapyrusMutex());
        BanterCoordinatorService::GetSingleton().SetCooldownHours(afHours);
    }

    void RecordScenePlayed(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        std::lock_guard lock(GetPapyrusMutex());
        BanterCoordinatorService::GetSingleton().RecordScenePlayed(akActorA, akActorB);
    }

    bool IsLocationAllowedForScenes(RE::StaticFunctionTag*, RE::BGSLocation* akLocation) {
        std::lock_guard lock(GetPapyrusMutex());
        return BanterCoordinatorService::GetSingleton().IsLocationAllowedForScenes(akLocation);
    }

    bool ShouldTriggerSkyrimNetNarration(RE::StaticFunctionTag*, RE::Actor* speaker, RE::Actor* target) {
        std::lock_guard lock(GetPapyrusMutex());
        return MARAS::ShouldTriggerSkyrimNetNarration(speaker, target);
    }

    bool RegisterSkyrimNetEvent(RE::StaticFunctionTag*,
                                std::string eventType,
                                std::string content,
                                RE::Actor* originatorActor,
                                RE::Actor* targetActor,
                                bool exactAudience,
                                std::vector<RE::Actor*> audience) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID origID = originatorActor ? originatorActor->GetFormID() : 0;
        RE::FormID targID = targetActor ? targetActor->GetFormID() : 0;
        std::vector<RE::FormID> audienceIDs;
        audienceIDs.reserve(audience.size());
        for (RE::Actor* actor : audience) {
            if (actor) {
                audienceIDs.push_back(actor->GetFormID());
            }
        }
        return MARAS::RegisterSkyrimNetEvent(eventType, content, origID, targID, exactAudience, audienceIDs);
    }

    // ========================================
    // Registration function for SKSE
    // ========================================

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        // Core registration functions
        RegisterSynced<RegisterCandidate>(vm, "RegisterCandidate");
        RegisterSynced<UnregisterNPC>(vm, "UnregisterNPC");

        // Consolidated faction management functions
        RegisterSynced<SetNpcCharacteristics>(vm, "SetNpcCharacteristics");

        // Consolidated status check functions
        RegisterSynced<IsNPCStatus>(vm, "IsNPCStatus");
        RegisterSynced<IsNPCStatusByEnum>(vm, "IsNPCStatusByEnum");
        RegisterSynced<IsNPCStatusWith>(vm, "IsNPCStatusWith");
        RegisterSynced<IsNPCStatusWithByEnum>(vm, "IsNPCStatusWithByEnum");
        RegisterSynced<GetOriginalRelationshipStatus>(vm, "GetOriginalRelationshipStatus");
        RegisterSynced<GetOriginalRelationshipStatusString>(vm, "GetOriginalRelationshipStatusString");

        // Consolidated status transition functions
        RegisterSynced<PromoteNPCToStatus>(vm, "PromoteNPCToStatus");
        RegisterSynced<PromoteNPCToStatusByEnum>(vm, "PromoteNPCToStatusByEnum");
        RegisterSynced<PromoteNPCToStatusWith>(vm, "PromoteNPCToStatusWith");
        RegisterSynced<PromoteNPCToStatusWithByEnum>(vm, "PromoteNPCToStatusWithByEnum");

        // Consolidated bulk retrieval functions
        RegisterSynced<GetNPCsByStatus>(vm, "GetNPCsByStatus");
        RegisterSynced<GetNPCsByStatusEnum>(vm, "GetNPCsByStatusEnum");
        RegisterSynced<GetNPCsByStatusWith>(vm, "GetNPCsByStatusWith");
        RegisterSynced<GetRelationshipPartners>(vm, "GetRelationshipPartners");
        RegisterSynced<GetCurrentTeammates>(vm, "GetCurrentTeammates");
        RegisterSynced<IsPlayerFollower>(vm, "IsPlayerFollower");

        // Consolidated statistics functions
        RegisterSynced<GetStatusCount>(vm, "GetStatusCount");
        RegisterSynced<GetStatusCountByEnum>(vm, "GetStatusCountByEnum");

        // Status history functions
        RegisterSynced<GetStatusHistoryCount>(vm, "GetStatusHistoryCount");
        RegisterSynced<GetStatusHistoryEntry>(vm, "GetStatusHistoryEntry");

        // Debug functions
        RegisterSynced<LogNPCStatistics>(vm, "LogNPCStatistics");
        RegisterSynced<LogNPCDetails>(vm, "LogNPCDetails");
        RegisterSynced<Log>(vm, "Log");
        RegisterSynced<SetLogLevel>(vm, "SetLogLevel");
        RegisterSynced<GetLogLevel>(vm, "GetLogLevel");

        // Marriage difficulty calculation
        RegisterSynced<CalculateMarriageSuccessChance>(vm, "CalculateMarriageSuccessChance");

        // Spouse hierarchy
        RegisterSynced<SetHierarchyRank>(vm, "SetHierarchyRank");
        RegisterSynced<GetHierarchyRank>(vm, "GetHierarchyRank");

        // Player house functions
        RegisterSynced<RegisterPlayerHouseCell>(vm, "RegisterPlayerHouseCell");
        RegisterSynced<GetAllPlayerHouses>(vm, "GetAllPlayerHouses");
        RegisterSynced<GetAllPlayerHousesNames>(vm, "GetAllPlayerHousesNames");
        RegisterSynced<RegisterTenantInPlayerHouse>(vm, "RegisterTenantInPlayerHouse");
        RegisterSynced<RemoveTenantFromPlayerHouse>(vm, "RemoveTenantFromPlayerHouse");
        RegisterSynced<SetTenantStayAtHome>(vm, "SetTenantStayAtHome");
        RegisterSynced<GetTenantStayAtHome>(vm, "GetTenantStayAtHome");
        RegisterSynced<GetPlayerHouseTenants>(vm, "GetPlayerHouseTenants");
        RegisterSynced<GetHouseMarker>(vm, "GetHouseMarker");
        RegisterSynced<GetTenantHouse>(vm, "GetTenantHouse");
        RegisterSynced<CountPlayerHouses>(vm, "CountPlayerHouses");

        // Spouse assets functions
        RegisterSynced<GetCellDoors>(vm, "GetCellDoors");
        RegisterSynced<ShareHouseWithPlayer>(vm, "ShareHouseWithPlayer");
        RegisterSynced<StopShareHouseWithPlayer>(vm, "StopShareHouseWithPlayer");
        RegisterSynced<IsHouseSharedWithPlayer>(vm, "IsHouseSharedWithPlayer");
        RegisterSynced<HasSpouseSharedHouseWithPlayer>(vm, "HasSpouseSharedHouseWithPlayer");

        // Home cell helpers
        RegisterSynced<GetNpcOriginalHouse>(vm, "GetNpcOriginalHouse");
        RegisterSynced<GetNpcBeds>(vm, "GetNpcBeds");
        RegisterSynced<GetNpcOriginalHouseCenterMarker>(vm, "GetNpcOriginalHouseCenterMarker");

        // Home marker management
        RegisterSynced<GetTrackedNpcHomeMarker>(vm, "GetTrackedNpcHomeMarker");
        RegisterSynced<SetTrackedNpcHomeMarker>(vm, "SetTrackedNpcHomeMarker");
        RegisterSynced<GetChildHomeMarker>(vm, "GetChildHomeMarker");

        // Spouse buff/service bindings
        RegisterSynced<GetSpouseMultiplier>(vm, "GetSpouseMultiplier");
        RegisterSynced<GetFollowersMultipliers>(vm, "GetFollowersMultipliers");
        RegisterSynced<GetPermanentMultipliers>(vm, "GetPermanentMultipliers");

        // Affection system
        RegisterSynced<AddAffection>(vm, "AddAffection");
        RegisterSynced<GetAffection>(vm, "GetAffection");
        RegisterSynced<GetPermanentAffection>(vm, "GetPermanentAffection");
        RegisterSynced<SetPermanentAffection>(vm, "SetPermanentAffection");
        RegisterSynced<SetAffectionMinMax>(vm, "SetAffectionMinMax");
        RegisterSynced<ApplyDailyAffection>(vm, "ApplyDailyAffection");
        RegisterSynced<SetAffectionDecayMultiplier>(vm, "SetAffectionDecayMultiplier");
        RegisterSynced<GetAffectionDecayMultiplier>(vm, "GetAffectionDecayMultiplier");

        // NPC Type and Status Queries
        RegisterSynced<GetNpcStatusName>(vm, "GetNpcStatusName");
        RegisterSynced<GetNpcStatusEnum>(vm, "GetNpcStatusEnum");
        RegisterSynced<GetNpcStatusNameWith>(vm, "GetNpcStatusNameWith");
        RegisterSynced<GetNpcStatusEnumWith>(vm, "GetNpcStatusEnumWith");

        RegisterSynced<IsNPCInOpenRelationship>(vm, "IsNPCInOpenRelationship");
        RegisterSynced<GetPairIsOpen>(vm, "GetPairIsOpen");
        RegisterSynced<SetPairIsOpen>(vm, "SetPairIsOpen");
        RegisterSynced<RecordCheating>(vm, "RecordCheating");
        RegisterSynced<SetCheatingDiscovered>(vm, "SetCheatingDiscovered");
        RegisterSynced<RegisterIntimateThread>(vm, "RegisterIntimateThread");
        RegisterSynced<ReportIntimateInteraction>(vm, "ReportIntimateInteraction");
        RegisterSynced<ClearIntimateThread>(vm, "ClearIntimateThread");
        RegisterSynced<FleeChildrenFromPlayerCell>(vm, "FleeChildrenFromPlayerCell");
        RegisterSynced<StopChildrenFleeing>(vm, "StopChildrenFleeing");
        RegisterSynced<Breakup>(vm, "Breakup");
        RegisterSynced<BreakAllLoverConnections>(vm, "BreakAllLoverConnections");
        RegisterSynced<SetCheatingAffairStatus>(vm, "SetCheatingAffairStatus");
        RegisterSynced<GetCheatingAffairStatus>(vm, "GetCheatingAffairStatus");
        RegisterSynced<GetAffairStage>(vm, "GetAffairStage");
        RegisterSynced<SetAffairStage>(vm, "SetAffairStage");
        RegisterSynced<GetPartnerPendingIntent>(vm, "GetPartnerPendingIntent");
        RegisterSynced<ClearPartnerIntent>(vm, "ClearPartnerIntent");
        RegisterSynced<EvaluatePartnerHomeArrival>(vm, "EvaluatePartnerHomeArrival");
        RegisterSynced<HasCheatedOn>(vm, "HasCheatedOn");
        RegisterSynced<IsNPCCheating>(vm, "IsNPCCheating");
        RegisterSynced<IsRelationshipCheating>(vm, "IsRelationshipCheating");
        RegisterSynced<GetCheatingCount>(vm, "GetCheatingCount");
        RegisterSynced<GetCheatedOnPartners>(vm, "GetCheatedOnPartners");
        RegisterSynced<GetCheatersWhoCheatedOn>(vm, "GetCheatersWhoCheatedOn");
        RegisterSynced<RegisterChildActor>(vm, "RegisterChildActor");
        RegisterSynced<RemoveChild>(vm, "RemoveChild");
        RegisterSynced<GetChildren>(vm, "GetChildren");
        RegisterSynced<GetChildrenOfActor>(vm, "GetChildrenOfActor");
        RegisterSynced<UpdateSpouseChildrenFaction>(vm, "UpdateSpouseChildrenFaction");
        RegisterSynced<IsAvailableForStatus>(vm, "IsAvailableForStatus");
        RegisterSynced<IsAvailableForStatusWith>(vm, "IsAvailableForStatusWith");
        RegisterSynced<CanFormRelationship>(vm, "CanFormRelationship");
        RegisterSynced<GetJsonString>(vm, "GetJsonString");
        RegisterSynced<GetJsonActor>(vm, "GetJsonActor");
        RegisterSynced<GetJsonForm>(vm, "GetJsonForm");
        RegisterSynced<GetJsonInt>(vm, "GetJsonInt");
        RegisterSynced<GetJsonFloat>(vm, "GetJsonFloat");
        RegisterSynced<GetJsonBool>(vm, "GetJsonBool");
        RegisterSynced<GetJsonArrayLength>(vm, "GetJsonArrayLength");
        RegisterSynced<GetJsonActorArray>(vm, "GetJsonActorArray");
        RegisterSynced<GetNpcTypes>(vm, "GetNpcTypes");
        RegisterSynced<GetNpcTypeEnum>(vm, "GetNpcTypeEnum");
        RegisterSynced<GetNpcCurrentTypeEnum>(vm, "GetNpcCurrentTypeEnum");
        RegisterSynced<GetNpcCurrentTypeName>(vm, "GetNpcCurrentTypeName");

        // Determination helpers
        RegisterSynced<DetermineSocialClass>(vm, "DetermineSocialClass");
        RegisterSynced<DetermineSkillType>(vm, "DetermineSkillType");
        RegisterSynced<DetermineTemperament>(vm, "DetermineTemperament");

        // Bonuses JSON accessors
        RegisterSynced<GetBonusCount>(vm, "GetBonusCount");
        RegisterSynced<GetBonusPerk>(vm, "GetBonusPerk");
        RegisterSynced<GetBonusEffectIndex>(vm, "GetBonusEffectIndex");
        RegisterSynced<GetBonusPerkValue>(vm, "GetBonusPerkValue");
        RegisterSynced<GetBonusPerkType>(vm, "GetBonusPerkType");
        RegisterSynced<GetBonusPerkUnit>(vm, "GetBonusPerkUnit");
        RegisterSynced<GetBonusPerkDescription>(vm, "GetBonusPerkDescription");

        // Keyword management
        RegisterSynced<HasNpcKeyword>(vm, "HasNpcKeyword");
        RegisterSynced<AddNpcKeyword>(vm, "AddNpcKeyword");
        RegisterSynced<RemoveNpcKeyword>(vm, "RemoveNpcKeyword");

        // Prisma UI functions
        RegisterSynced<ShowUI>(vm, "ShowUI");
        RegisterSynced<HideUI>(vm, "HideUI");
        RegisterSynced<IsUIVisible>(vm, "IsUIVisible");

        // Confirmation modal functions
        RegisterLatentLocked<int32_t, ShowConfirmationModal>(vm, "ShowConfirmationModal");
        RegisterLatentLocked<int32_t, ShowSkipWeddingModal>(vm, "ShowSkipWeddingModal");
        RegisterLatentLocked<int32_t, ShowDivorceSettlementModal>(vm, "ShowDivorceSettlementModal");
        RegisterLatentLocked<RE::BGSLocation*, ShowChooseHomeModal>(vm, "ShowChooseHomeModal");
        RegisterLatentLocked<RE::Actor*, ShowChooseAdoptionSpouseModal>(vm, "ShowChooseAdoptionSpouseModal");
        RegisterLatentLocked<RE::BSFixedString, ShowDebugStatusModal>(vm, "ShowDebugStatusModal");
        RegisterLatentLocked<int32_t, ShowChildMigrationAuditModal>(vm, "ShowChildMigrationAuditModal");
        RegisterSynced<CheckAndSetActionCooldown>(vm, "CheckAndSetActionCooldown");

        // Settings & Migration functions
        RegisterSynced<SetSettingBool>(vm, "SetSettingBool");
        RegisterSynced<GetSettingBool>(vm, "GetSettingBool");
        RegisterSynced<SetSettingInt>(vm, "SetSettingInt");
        RegisterSynced<GetSettingInt>(vm, "GetSettingInt");
        RegisterSynced<SetSettingFloat>(vm, "SetSettingFloat");
        RegisterSynced<GetSettingFloat>(vm, "GetSettingFloat");
        RegisterSynced<IsSettingsMigrated>(vm, "IsSettingsMigrated");
        RegisterSynced<MarkSettingsMigrated>(vm, "MarkSettingsMigrated");

        RegisterSynced<GetStartDialGender>(vm, "GetStartDialGender");
        RegisterSynced<SetStartDialGender>(vm, "SetStartDialGender");
        RegisterSynced<GetEnablePolygamy>(vm, "GetEnablePolygamy");
        RegisterSynced<SetEnablePolygamy>(vm, "SetEnablePolygamy");
        RegisterSynced<GetDisablePolygamyQuest>(vm, "GetDisablePolygamyQuest");
        RegisterSynced<SetDisablePolygamyQuest>(vm, "SetDisablePolygamyQuest");
        RegisterSynced<GetMinRelRankForDial>(vm, "GetMinRelRankForDial");
        RegisterSynced<SetMinRelRankForDial>(vm, "SetMinRelRankForDial");

        // Player children / household adoption functions
        RegisterSynced<AssignChildToHouse>(vm, "AssignChildToHouse");
        RegisterSynced<IssueChildOrder>(vm, "IssueChildOrder");
        RegisterSynced<ResetChildForceGreeting>(vm, "ResetChildForceGreeting");
        RegisterSynced<AssignChildPet>(vm, "AssignChildPet");
        RegisterSynced<GetChildPet>(vm, "GetChildPet");
        RegisterSynced<GetChildForPet>(vm, "GetChildForPet");
        RegisterSynced<RemoveChildPet>(vm, "RemoveChildPet");
        RegisterSynced<SetChildPetBanned>(vm, "SetChildPetBanned");
        RegisterSynced<IsChildPetBanned>(vm, "IsChildPetBanned");
        RegisterSynced<IsPetBanned>(vm, "IsPetBanned");
        RegisterSynced<SetGlobalPetBanned>(vm, "SetGlobalPetBanned");
        RegisterSynced<IsGlobalPetBanned>(vm, "IsGlobalPetBanned");
        RegisterSynced<SetChildPetRetryTimestamp>(vm, "SetChildPetRetryTimestamp");
        RegisterSynced<RecordChildPetRetry>(vm, "RecordChildPetRetry");
        RegisterSynced<GetChildPetRetryTimestamp>(vm, "GetChildPetRetryTimestamp");
        RegisterSynced<IsPetRetryAvailable>(vm, "IsPetRetryAvailable");
        RegisterSynced<AssignChildCritter>(vm, "AssignChildCritter");
        RegisterSynced<GetChildCritter>(vm, "GetChildCritter");
        RegisterSynced<GetChildForCritter>(vm, "GetChildForCritter");
        RegisterSynced<GetChildForPetOrCritter>(vm, "GetChildForPetOrCritter");
        RegisterSynced<RemoveChildCritter>(vm, "RemoveChildCritter");
        RegisterSynced<SetChildCritterBanned>(vm, "SetChildCritterBanned");
        RegisterSynced<IsChildCritterBanned>(vm, "IsChildCritterBanned");
        RegisterSynced<IsCritterBanned>(vm, "IsCritterBanned");
        RegisterSynced<SetChildIsNewlyAdopted>(vm, "SetChildIsNewlyAdopted");
        RegisterSynced<IsChildNewlyAdopted>(vm, "IsChildNewlyAdopted");
        RegisterSynced<IsChildPlayerAdopted>(vm, "IsChildPlayerAdopted");
        RegisterSynced<SetHouseInitialMoveDone>(vm, "SetHouseInitialMoveDone");
        RegisterSynced<IsHouseInitialMoveDone>(vm, "IsHouseInitialMoveDone");
        RegisterSynced<SetChildChestIntroduced>(vm, "SetChildChestIntroduced");
        RegisterSynced<IsChildChestIntroduced>(vm, "IsChildChestIntroduced");
        RegisterSynced<SetGlobalCritterBanned>(vm, "SetGlobalCritterBanned");
        RegisterSynced<IsGlobalCritterBanned>(vm, "IsGlobalCritterBanned");
        RegisterSynced<SetChildCritterRetryTimestamp>(vm, "SetChildCritterRetryTimestamp");
        RegisterSynced<RecordChildCritterRetry>(vm, "RecordChildCritterRetry");
        RegisterSynced<GetChildCritterRetryTimestamp>(vm, "GetChildCritterRetryTimestamp");
        RegisterSynced<IsChildCritterRetryAvailable>(vm, "IsChildCritterRetryAvailable");
        RegisterSynced<GetAllPlayerChildren>(vm, "GetAllPlayerChildren");
        RegisterSynced<GetChildrenAssignedToHouse>(vm, "GetChildrenAssignedToHouse");
        RegisterSynced<GetNewlyAdoptedChild>(vm, "GetNewlyAdoptedChild");
        RegisterSynced<GetChildWithoutPet>(vm, "GetChildWithoutPet");
        RegisterSynced<GetChildWithoutCritter>(vm, "GetChildWithoutCritter");
        RegisterSynced<GetChildrenWithoutScene>(vm, "GetChildrenWithoutScene");
        RegisterSynced<GetWeightedRandomChild>(vm, "GetWeightedRandomChild");
        RegisterSynced<SetChildPlayedScene>(vm, "SetChildPlayedScene");
        RegisterSynced<HasChildPlayedScene>(vm, "HasChildPlayedScene");
        RegisterSynced<RecordChildInteraction>(vm, "RecordChildInteraction");
        RegisterSynced<GetChildLastInteractionTimestamp>(vm, "GetChildLastInteractionTimestamp");

        // Deceased Registry queries
        RegisterSynced<IsWidow>(vm, "IsWidow");
        RegisterSynced<IsNPCDeceased>(vm, "IsNPCDeceased");
        RegisterSynced<WasMarriedTo>(vm, "WasMarriedTo");
        RegisterSynced<WasEngagedTo>(vm, "WasEngagedTo");
        RegisterSynced<WasExOf>(vm, "WasExOf");
        RegisterSynced<GetDeceasedSpouseNames>(vm, "GetDeceasedSpouseNames");
        RegisterSynced<GetDeceasedChildrenNames>(vm, "GetDeceasedChildrenNames");
        RegisterSynced<HasDeceasedChildren>(vm, "HasDeceasedChildren");
        RegisterSynced<GetDeceasedCount>(vm, "GetDeceasedCount");

        // Fertility Mode Reloaded & Paternity Intrigue
        RegisterSynced<FindActorByName>(vm, "FindActorByName");
        RegisterSynced<SetNPCPregnant>(vm, "SetNPCPregnant");
        RegisterSynced<IsNPCPregnant>(vm, "IsNPCPregnant");
        RegisterSynced<RegisterUnbornChild>(vm, "RegisterUnbornChild");
        RegisterSynced<PromoteUnbornToInfant>(vm, "PromoteUnbornToInfant");
        RegisterSynced<RemoveUnbornChild>(vm, "RemoveUnbornChild");
        RegisterSynced<FindUnbornChild>(vm, "FindUnbornChild");
        RegisterSynced<GetUnbornChildren>(vm, "GetUnbornChildren");
        RegisterSynced<GetUnbornChildCount>(vm, "GetUnbornChildCount");
        RegisterSynced<RegisterFertilityChild>(vm, "RegisterFertilityChild");
        RegisterSynced<ResolvePaternitySecret>(vm, "ResolvePaternitySecret");
        RegisterSynced<ResolvePaternitySecretForPartner>(vm, "ResolvePaternitySecretForPartner");
        RegisterSynced<ResolvePaternitySecretForBioFather>(vm, "ResolvePaternitySecretForBioFather");
        RegisterSynced<RevealPregnancyToPartner>(vm, "RevealPregnancyToPartner");
        RegisterSynced<SetPaternitySecretEdges>(vm, "SetPaternitySecretEdges");
        RegisterSynced<IsSecretForBioFather>(vm, "IsSecretForBioFather");
        RegisterSynced<IsSecretForPartner>(vm, "IsSecretForPartner");
        RegisterSynced<IsPregnancySecretForPartner>(vm, "IsPregnancySecretForPartner");
        RegisterSynced<SetChildStage>(vm, "SetChildStage");
        RegisterSynced<GetChildStage>(vm, "GetChildStage");
        RegisterSynced<SetChildTrainingLocation>(vm, "SetChildTrainingLocation");
        RegisterSynced<GetChildTrainingLocation>(vm, "GetChildTrainingLocation");
        RegisterSynced<GetChildTrainingClass>(vm, "GetChildTrainingClass");
        RegisterSynced<GetChildSavedRaceIndex>(vm, "GetChildSavedRaceIndex");
        RegisterSynced<SetChildGender>(vm, "SetChildGender");
        RegisterSynced<GetChildGender>(vm, "GetChildGender");
        RegisterSynced<SetChildGuardian>(vm, "SetChildGuardian");
        RegisterSynced<FindSyntheticChild>(vm, "FindSyntheticChild");
        RegisterSynced<UpdateChildName>(vm, "UpdateChildName");
        RegisterSynced<GetPrimaryPartner>(vm, "GetPrimaryPartner");
        RegisterSynced<GetGuardianA>(vm, "GetGuardianA");
        RegisterSynced<GetGuardianB>(vm, "GetGuardianB");
        RegisterSynced<GetGuardians>(vm, "GetGuardians");
        RegisterSynced<GetCoGuardian>(vm, "GetCoGuardian");
        RegisterSynced<IsGuardian>(vm, "IsGuardian");
        RegisterSynced<GetBioMother>(vm, "GetBioMother");
        RegisterSynced<GetBioFather>(vm, "GetBioFather");
        RegisterSynced<GetMotherPartner>(vm, "GetMotherPartner");
        RegisterSynced<SetBioMother>(vm, "SetBioMother");
        RegisterSynced<SetBioFather>(vm, "SetBioFather");
        RegisterSynced<GetChildName>(vm, "GetChildName");
        RegisterSynced<FindChildPaternitySecret>(vm, "FindChildPaternitySecret");
        RegisterSynced<BuildPregnancyAnnouncedMessage>(vm, "BuildPregnancyAnnouncedMessage");
        RegisterSynced<BuildPregnancyAnnouncedJson>(vm, "BuildPregnancyAnnouncedJson");
        RegisterSynced<BuildChildBirthMessage>(vm, "BuildChildBirthMessage");
        RegisterSynced<BuildChildBirthJson>(vm, "BuildChildBirthJson");

        // Child name generation functions
        RegisterSynced<GenerateRandomChildName>(vm, "GenerateRandomChildName");
        RegisterSynced<GenerateRandomChildNameForActor>(vm, "GenerateRandomChildNameForActor");
        RegisterSynced<GenerateRandomChildNameForRace>(vm, "GenerateRandomChildNameForRace");
        RegisterSynced<GetChildNames>(vm, "GetChildNames");
        RegisterSynced<ReloadChildNames>(vm, "ReloadChildNames");

        // Critter name generation functions
        RegisterSynced<GenerateRandomCritterName>(vm, "GenerateRandomCritterName");
        RegisterSynced<GenerateRandomCritterNameForActor>(vm, "GenerateRandomCritterNameForActor");
        RegisterSynced<GenerateRandomCritterNameForRace>(vm, "GenerateRandomCritterNameForRace");
        RegisterSynced<GetCritterNames>(vm, "GetCritterNames");
        RegisterSynced<ReloadCritterNames>(vm, "ReloadCritterNames");

        // Banter Coordinator functions
        RegisterSynced<FindBanterCoupleCandidates>(vm, "FindBanterCoupleCandidates");
        RegisterSynced<IsBanterPartnerPresent>(vm, "IsBanterPartnerPresent");
        RegisterSynced<ResetBanterCoupleCooldown>(vm, "ResetBanterCoupleCooldown");
        RegisterSynced<ClearAllBanterCooldowns>(vm, "ClearAllBanterCooldowns");
        RegisterSynced<GetBanterCoupleCooldownHours>(vm, "GetBanterCoupleCooldownHours");
        RegisterSynced<SetBanterCoupleCooldownHours>(vm, "SetBanterCoupleCooldownHours");
        RegisterSynced<RecordScenePlayed>(vm, "RecordScenePlayed");
        RegisterSynced<IsLocationAllowedForScenes>(vm, "IsLocationAllowedForScenes");
        RegisterSynced<ShouldTriggerSkyrimNetNarration>(vm, "ShouldTriggerSkyrimNetNarration");
        RegisterSynced<RegisterSkyrimNetEvent>(vm, "RegisterSkyrimNetEvent");

        MARAS_LOG_INFO("Registered MARAS Papyrus functions including Confirmation Modal and Settings");
        return true;
    }

}  // namespace MARAS::PapyrusInterface
