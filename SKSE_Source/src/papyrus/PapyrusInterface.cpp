#include "papyrus/PapyrusInterface.h"

#include "core/BanterCoordinatorService.h"
#include "core/ChildrenFleeService.h"
#include "core/IntimateThreadService.h"
#include "core/LoggingService.h"
#include "core/PlayerHouseService.h"
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
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().LogStatistics();
    }

    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("LogNPCDetails called with null NPC");
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().LogNPCDetails(npc->GetFormID());
    }

    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel) {
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
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("AddNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        if (!base->AddKeyword(keyword)) {
            return false;
        }
        NPCRelationshipManager::GetSingleton().AddTrackedKeyword(formID, keyword->GetFormID());
        return true;
    }

    bool RemoveNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("RemoveNpcKeyword: null argument(s) provided");
            return false;
        }
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("RemoveNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        if (!base->RemoveKeyword(keyword)) {
            return false;
        }
        NPCRelationshipManager::GetSingleton().RemoveTrackedKeyword(formID, keyword->GetFormID());
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

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [vm, stackID](const ModalResponse& resp) {
                if (resp.aborted) {
                    vm->ReturnLatentResult(stackID, 1);
                    return;
                }

                const bool dontAskAgain = resp.GetBool("dont_ask_again", false);
                if (resp.buttonIndex == 0 || resp.buttonId == "skip_wedding") {
                    if (dontAskAgain) {
                        Settings::GetSingleton().SetBoolByKey("skipWedding", true);
                        PrismaUIDataBridge::GetSingleton().SendSettingsData();
                        MARAS_LOG_INFO("PapyrusInterface: ShowSkipWeddingModal - persisted skipWedding=true");
                    }
                    vm->ReturnLatentResult(stackID, 0);
                } else {
                    int32_t result = dontAskAgain ? 2 : 1;
                    vm->ReturnLatentResult(stackID, result);
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

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [vm, stackID, canPayDirectly](const ModalResponse& resp) {
                if (resp.aborted) {
                    vm->ReturnLatentResult(stackID, 1);
                    return;
                }

                if (canPayDirectly && (resp.buttonIndex == 0 || resp.buttonId == "pay_now")) {
                    vm->ReturnLatentResult(stackID, 0);
                } else {
                    vm->ReturnLatentResult(stackID, 1);
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

        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto houses = houseSvc.GetAllPlayerHouses();
        if (houses.empty()) {
            MARAS_LOG_WARN("PapyrusInterface: ShowChooseHomeModal called with no registered player houses");
            SKSE::GetTaskInterface()->AddTask([vm, stackID]() {
                vm->ReturnLatentResult<RE::BGSLocation*>(stackID, nullptr);
            });
            return RE::BSScript::LatentStatus::kStarted;
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
            [vm, stackID, targetFormID, isChild](const ModalResponse& resp) {
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "confirm")) {
                    vm->ReturnLatentResult<RE::BGSLocation*>(stackID, nullptr);
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

                if (chosenLoc && !isChild && targetFormID != 0) {
                    const bool stayAtHome = (resp.GetString("schedule_mode", "stay") != "original");
                    std::lock_guard lock(GetPapyrusMutex());
                    auto& svc = PlayerHouseService::GetSingleton();
                    svc.RelocateFamilyUnit(targetFormID, selectedHouseID, true);
                    svc.SetTenantStayAtHome(targetFormID, stayAtHome);
                }

                vm->ReturnLatentResult<RE::BGSLocation*>(stackID, chosenLoc);
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

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto spouses = relMgr.GetAllMarried();
        if (spouses.empty()) {
            SKSE::GetTaskInterface()->AddTask([vm, stackID]() {
                vm->ReturnLatentResult<RE::Actor*>(stackID, nullptr);
            });
            return RE::BSScript::LatentStatus::kStarted;
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
            SKSE::GetTaskInterface()->AddTask([vm, stackID]() {
                vm->ReturnLatentResult<RE::Actor*>(stackID, nullptr);
            });
            return RE::BSScript::LatentStatus::kStarted;
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
            [vm, stackID](const ModalResponse& resp) {
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "confirm")) {
                    vm->ReturnLatentResult<RE::Actor*>(stackID, nullptr);
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

                auto* chosenSpouse = selectedSpouseID != 0
                    ? RE::TESForm::LookupByID<RE::Actor>(selectedSpouseID)
                    : nullptr;
                vm->ReturnLatentResult<RE::Actor*>(stackID, chosenSpouse);
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

        if (!target) {
            SKSE::GetTaskInterface()->AddTask([vm, stackID]() {
                vm->ReturnLatentResult<RE::BSFixedString>(stackID, RE::BSFixedString(""));
            });
            return RE::BSScript::LatentStatus::kStarted;
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
            [vm, stackID](const ModalResponse& resp) {
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "apply")) {
                    vm->ReturnLatentResult<RE::BSFixedString>(stackID, RE::BSFixedString(""));
                    return;
                }

                const std::string chosen = resp.GetString("target_status", "");
                vm->ReturnLatentResult<RE::BSFixedString>(stackID, RE::BSFixedString(chosen));
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

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        std::vector<RE::FormID> migratedChildIDs;
        std::vector<ModalChecklistItem> items;

        for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
            const bool isTempRef = ((cID >> 24) == 0xFF);
            const bool involvesPlayer = (cEntry.guardianAID == kPlayerFormID ||
                                         cEntry.guardianBID == kPlayerFormID ||
                                         cEntry.bioMotherID == kPlayerFormID ||
                                         cEntry.bioFatherID == kPlayerFormID);
            if (!cEntry.isBiologicalChild && !isTempRef && !involvesPlayer) {
                continue;
            }

            migratedChildIDs.push_back(cID);

            ModalChecklistItem item;
            item.id = fmt::format("child_{:08X}", cID);
            item.label = !cEntry.name.empty() ? cEntry.name : ActorSvc::GetName(cID);
            if (item.label.empty()) {
                item.label = fmt::format("Child ({:08X})", cID);
            }

            switch (cEntry.stage) {
                case ChildStage::Unborn:
                    item.badgeText = "Pregnancy";
                    item.badgeTone = "warning";
                    break;
                case ChildStage::Infant:
                    item.badgeText = "Infant";
                    item.badgeTone = "accent";
                    break;
                case ChildStage::Child:
                    item.badgeText = cEntry.isAdopted ? "Adopted" : "Child";
                    item.badgeTone = cEntry.isAdopted ? "accent" : "success";
                    break;
                case ChildStage::Training:
                    item.badgeText = "Training";
                    item.badgeTone = "accent";
                    break;
                case ChildStage::Adult:
                    item.badgeText = "Adult Child";
                    item.badgeTone = "neutral";
                    break;
            }

            auto resolveActorName = [](RE::FormID id) -> std::string {
                if (id == 0 || id == kUnknownFatherFormID) {
                    return "";
                }
                if (id == kPlayerFormID) {
                    std::string pName = ActorSvc::GetPlayerName(false);
                    return !pName.empty() ? pName : "Player";
                }
                return ActorSvc::GetName(id);
            };

            if (cEntry.isAdopted) {
                std::string gAName = resolveActorName(cEntry.guardianAID);
                std::string gBName = resolveActorName(cEntry.guardianBID);
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
                // Respect perspective-bound secret masking for the Player
                bool canPlayerKnowSecret = true;
                if (cEntry.paternitySecret.HasActiveSecret() && !cEntry.paternitySecret.discovered) {
                    if (cEntry.bioMotherID == kPlayerFormID) {
                        canPlayerKnowSecret = true;
                    } else if (cEntry.bioFatherID == kPlayerFormID) {
                        canPlayerKnowSecret = !cEntry.paternitySecret.isSecretForBioFather;
                    } else if (cEntry.guardianAID == kPlayerFormID || cEntry.guardianBID == kPlayerFormID) {
                        canPlayerKnowSecret = !cEntry.paternitySecret.isSecretForPartner;
                    } else {
                        canPlayerKnowSecret = false;
                    }
                }

                std::string motherName = resolveActorName(cEntry.bioMotherID);
                RE::FormID displayedFatherID = cEntry.bioFatherID;
                if (!canPlayerKnowSecret) {
                    if (cEntry.guardianAID != 0 && cEntry.guardianAID != cEntry.bioMotherID) {
                        displayedFatherID = cEntry.guardianAID;
                    } else if (cEntry.guardianBID != 0 && cEntry.guardianBID != cEntry.bioMotherID) {
                        displayedFatherID = cEntry.guardianBID;
                    } else {
                        displayedFatherID = 0;
                    }
                }
                std::string fatherName = resolveActorName(displayedFatherID);
                if (fatherName.empty() && canPlayerKnowSecret && !cEntry.paternitySecret.bioFatherName.empty()) {
                    fatherName = cEntry.paternitySecret.bioFatherName;
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
            SKSE::GetTaskInterface()->AddTask([vm, stackID]() {
                vm->ReturnLatentResult<int32_t>(stackID, 0);
            });
            return RE::BSScript::LatentStatus::kStarted;
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
            [vm, stackID, childIDs = std::move(migratedChildIDs)](const ModalResponse& resp) {
                const bool discardAll = (!resp.aborted && (resp.buttonIndex == 1 || resp.buttonId == "discard_all"));
                auto& mgr = NPCRelationshipManager::GetSingleton();
                int32_t keptCount = 0;
                int32_t removedCount = 0;

                for (RE::FormID cID : childIDs) {
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

                if (removedCount > 0) {
                    PrismaUIDataBridge::GetSingleton().SendOverviewData();
                }

                MARAS_LOG_INFO("PapyrusInterface: ShowChildMigrationAuditModal completed — kept={}, removed={}",
                               keptCount, removedCount);
                vm->ReturnLatentResult<int32_t>(stackID, keptCount);
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
        MARAS_LOG_INFO("Papyrus::ShowUI called");
        PrismaUIManager::GetSingleton().Show();
    }

    void HideUI(RE::StaticFunctionTag*) {
        MARAS_LOG_INFO("Papyrus::HideUI called");
        PrismaUIManager::GetSingleton().Hide();
    }

    bool IsUIVisible(RE::StaticFunctionTag*) {
        bool visible = PrismaUIManager::GetSingleton().IsVisible();
        MARAS_LOG_INFO("Papyrus::IsUIVisible called -> {}", visible);
        return visible;
    }

    // ========================================
    // Intimate scene & Banter Coordinator bindings
    // ========================================

    bool RegisterIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, std::vector<RE::Actor*> actors) {
        return IntimateThreadService::GetSingleton().RegisterIntimateThread(framework.c_str(), threadID, actors);
    }

    void ReportIntimateInteraction(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB) {
        IntimateThreadService::GetSingleton().ReportIntimateInteraction(framework.c_str(), threadID, actorA, actorB);
    }

    void ClearIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID) {
        IntimateThreadService::GetSingleton().ClearIntimateThread(framework.c_str(), threadID);
    }

    void FleeChildrenFromPlayerCell(RE::StaticFunctionTag*) {
        ChildrenFleeService::GetSingleton().FleeFromPlayerCell();
    }

    void StopChildrenFleeing(RE::StaticFunctionTag*) {
        ChildrenFleeService::GetSingleton().StopFleeing();
    }

    std::vector<RE::Actor*> FindBanterCoupleCandidates(RE::StaticFunctionTag*, float afScanRadius) {
        return BanterCoordinatorService::GetSingleton().FindBanterCoupleCandidates(afScanRadius);
    }

    bool IsBanterPartnerPresent(RE::StaticFunctionTag*, RE::Actor* akPartner, float afScanRadius) {
        return BanterCoordinatorService::GetSingleton().IsBanterPartnerPresent(akPartner, afScanRadius);
    }

    void ResetBanterCoupleCooldown(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        if (!akActorA || !akActorB) return;
        BanterCoordinatorService::GetSingleton().ResetCooldown(akActorA->GetFormID(), akActorB->GetFormID());
    }

    void ClearAllBanterCooldowns(RE::StaticFunctionTag*) {
        BanterCoordinatorService::GetSingleton().ClearAllCooldowns();
    }

    float GetBanterCoupleCooldownHours(RE::StaticFunctionTag*) {
        return BanterCoordinatorService::GetSingleton().GetCooldownHours();
    }

    void SetBanterCoupleCooldownHours(RE::StaticFunctionTag*, float afHours) {
        BanterCoordinatorService::GetSingleton().SetCooldownHours(afHours);
    }

    void RecordScenePlayed(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        BanterCoordinatorService::GetSingleton().RecordScenePlayed(akActorA, akActorB);
    }

    bool IsLocationAllowedForScenes(RE::StaticFunctionTag*, RE::BGSLocation* akLocation) {
        return BanterCoordinatorService::GetSingleton().IsLocationAllowedForScenes(akLocation);
    }

    // ========================================
    // Registration function for SKSE
    // ========================================

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        // Core registration functions
        vm->RegisterFunction("RegisterCandidate", "MARAS", RegisterCandidate);
        vm->RegisterFunction("UnregisterNPC", "MARAS", UnregisterNPC);

        // Consolidated faction management functions
        vm->RegisterFunction("SetNpcCharacteristics", "MARAS", SetNpcCharacteristics);

        // Consolidated status check functions
        vm->RegisterFunction("IsNPCStatus", "MARAS", IsNPCStatus);
        vm->RegisterFunction("IsNPCStatusByEnum", "MARAS", IsNPCStatusByEnum);
        vm->RegisterFunction("IsNPCStatusWith", "MARAS", IsNPCStatusWith);
        vm->RegisterFunction("IsNPCStatusWithByEnum", "MARAS", IsNPCStatusWithByEnum);
        vm->RegisterFunction("GetOriginalRelationshipStatus", "MARAS", GetOriginalRelationshipStatus);
        vm->RegisterFunction("GetOriginalRelationshipStatusString", "MARAS", GetOriginalRelationshipStatusString);

        // Consolidated status transition functions
        vm->RegisterFunction("PromoteNPCToStatus", "MARAS", PromoteNPCToStatus);
        vm->RegisterFunction("PromoteNPCToStatusByEnum", "MARAS", PromoteNPCToStatusByEnum);
        vm->RegisterFunction("PromoteNPCToStatusWith", "MARAS", PromoteNPCToStatusWith);
        vm->RegisterFunction("PromoteNPCToStatusWithByEnum", "MARAS", PromoteNPCToStatusWithByEnum);

        // Consolidated bulk retrieval functions
        vm->RegisterFunction("GetNPCsByStatus", "MARAS", GetNPCsByStatus);
        vm->RegisterFunction("GetNPCsByStatusEnum", "MARAS", GetNPCsByStatusEnum);
        vm->RegisterFunction("GetNPCsByStatusWith", "MARAS", GetNPCsByStatusWith);
        vm->RegisterFunction("GetRelationshipPartners", "MARAS", GetRelationshipPartners);
        vm->RegisterFunction("GetCurrentTeammates", "MARAS", GetCurrentTeammates);
        vm->RegisterFunction("IsPlayerFollower", "MARAS", IsPlayerFollower);

        // Consolidated statistics functions
        vm->RegisterFunction("GetStatusCount", "MARAS", GetStatusCount);
        vm->RegisterFunction("GetStatusCountByEnum", "MARAS", GetStatusCountByEnum);

        // Status history functions
        vm->RegisterFunction("GetStatusHistoryCount", "MARAS", GetStatusHistoryCount);
        vm->RegisterFunction("GetStatusHistoryEntry", "MARAS", GetStatusHistoryEntry);

        // Debug functions
        vm->RegisterFunction("LogNPCStatistics", "MARAS", LogNPCStatistics);
        vm->RegisterFunction("LogNPCDetails", "MARAS", LogNPCDetails);
        vm->RegisterFunction("Log", "MARAS", Log);
        vm->RegisterFunction("SetLogLevel", "MARAS", SetLogLevel);
        vm->RegisterFunction("GetLogLevel", "MARAS", GetLogLevel);

        // Marriage difficulty calculation
        vm->RegisterFunction("CalculateMarriageSuccessChance", "MARAS", CalculateMarriageSuccessChance);

        // Spouse hierarchy
        vm->RegisterFunction("SetHierarchyRank", "MARAS", SetHierarchyRank);
        vm->RegisterFunction("GetHierarchyRank", "MARAS", GetHierarchyRank);

        // Player house functions
        vm->RegisterFunction("RegisterPlayerHouseCell", "MARAS", RegisterPlayerHouseCell);
        vm->RegisterFunction("GetAllPlayerHouses", "MARAS", GetAllPlayerHouses);
        vm->RegisterFunction("GetAllPlayerHousesNames", "MARAS", GetAllPlayerHousesNames);
        vm->RegisterFunction("RegisterTenantInPlayerHouse", "MARAS", RegisterTenantInPlayerHouse);
        vm->RegisterFunction("RemoveTenantFromPlayerHouse", "MARAS", RemoveTenantFromPlayerHouse);
        vm->RegisterFunction("SetTenantStayAtHome", "MARAS", SetTenantStayAtHome);
        vm->RegisterFunction("GetTenantStayAtHome", "MARAS", GetTenantStayAtHome);
        vm->RegisterFunction("GetPlayerHouseTenants", "MARAS", GetPlayerHouseTenants);
        vm->RegisterFunction("GetHouseMarker", "MARAS", GetHouseMarker);
        vm->RegisterFunction("GetTenantHouse", "MARAS", GetTenantHouse);
        vm->RegisterFunction("CountPlayerHouses", "MARAS", CountPlayerHouses);

        // Spouse assets functions
        vm->RegisterFunction("GetCellDoors", "MARAS", GetCellDoors);
        vm->RegisterFunction("ShareHouseWithPlayer", "MARAS", ShareHouseWithPlayer);
        vm->RegisterFunction("StopShareHouseWithPlayer", "MARAS", StopShareHouseWithPlayer);
        vm->RegisterFunction("IsHouseSharedWithPlayer", "MARAS", IsHouseSharedWithPlayer);
        vm->RegisterFunction("HasSpouseSharedHouseWithPlayer", "MARAS", HasSpouseSharedHouseWithPlayer);

        // Home cell helpers
        vm->RegisterFunction("GetNpcOriginalHouse", "MARAS", GetNpcOriginalHouse);
        vm->RegisterFunction("GetNpcBeds", "MARAS", GetNpcBeds);
        vm->RegisterFunction("GetNpcOriginalHouseCenterMarker", "MARAS", GetNpcOriginalHouseCenterMarker);

        // Home marker management
        vm->RegisterFunction("GetTrackedNpcHomeMarker", "MARAS", GetTrackedNpcHomeMarker);
        vm->RegisterFunction("SetTrackedNpcHomeMarker", "MARAS", SetTrackedNpcHomeMarker);
        vm->RegisterFunction("GetChildHomeMarker", "MARAS", GetChildHomeMarker);

        // Spouse buff/service bindings
        vm->RegisterFunction("GetSpouseMultiplier", "MARAS", GetSpouseMultiplier);
        vm->RegisterFunction("GetFollowersMultipliers", "MARAS", GetFollowersMultipliers);
        vm->RegisterFunction("GetPermanentMultipliers", "MARAS", GetPermanentMultipliers);

        // Affection system
        vm->RegisterFunction("AddAffection", "MARAS", AddAffection);
        vm->RegisterFunction("GetAffection", "MARAS", GetAffection);
        vm->RegisterFunction("GetPermanentAffection", "MARAS", GetPermanentAffection);
        vm->RegisterFunction("SetPermanentAffection", "MARAS", SetPermanentAffection);
        vm->RegisterFunction("SetAffectionMinMax", "MARAS", SetAffectionMinMax);
        vm->RegisterFunction("ApplyDailyAffection", "MARAS", ApplyDailyAffection);
        vm->RegisterFunction("SetAffectionDecayMultiplier", "MARAS", SetAffectionDecayMultiplier);
        vm->RegisterFunction("GetAffectionDecayMultiplier", "MARAS", GetAffectionDecayMultiplier);

        // NPC Type and Status Queries
        vm->RegisterFunction("GetNpcStatusName", "MARAS", GetNpcStatusName);
        vm->RegisterFunction("GetNpcStatusEnum", "MARAS", GetNpcStatusEnum);
        vm->RegisterFunction("GetNpcStatusNameWith", "MARAS", GetNpcStatusNameWith);
        vm->RegisterFunction("GetNpcStatusEnumWith", "MARAS", GetNpcStatusEnumWith);
        vm->RegisterFunction("EvaluateRelationshipStatusChange", "MARAS", EvaluateRelationshipStatusChange);
        vm->RegisterFunction("IsNPCInOpenRelationship", "MARAS", IsNPCInOpenRelationship);
        vm->RegisterFunction("GetPairIsOpen", "MARAS", GetPairIsOpen);
        vm->RegisterFunction("SetPairIsOpen", "MARAS", SetPairIsOpen);
        vm->RegisterFunction("RecordCheating", "MARAS", RecordCheating);
        vm->RegisterFunction("SetCheatingDiscovered", "MARAS", SetCheatingDiscovered);
        vm->RegisterFunction("RegisterIntimateThread", "MARAS", RegisterIntimateThread);
        vm->RegisterFunction("ReportIntimateInteraction", "MARAS", ReportIntimateInteraction);
        vm->RegisterFunction("ClearIntimateThread", "MARAS", ClearIntimateThread);
        vm->RegisterFunction("FleeChildrenFromPlayerCell", "MARAS", FleeChildrenFromPlayerCell);
        vm->RegisterFunction("StopChildrenFleeing", "MARAS", StopChildrenFleeing);
        vm->RegisterFunction("Breakup", "MARAS", Breakup);
        vm->RegisterFunction("BreakAllLoverConnections", "MARAS", BreakAllLoverConnections);
        vm->RegisterFunction("SetCheatingAffairStatus", "MARAS", SetCheatingAffairStatus);
        vm->RegisterFunction("GetCheatingAffairStatus", "MARAS", GetCheatingAffairStatus);
        vm->RegisterFunction("HasCheatedOn", "MARAS", HasCheatedOn);
        vm->RegisterFunction("IsNPCCheating", "MARAS", IsNPCCheating);
        vm->RegisterFunction("IsRelationshipCheating", "MARAS", IsRelationshipCheating);
        vm->RegisterFunction("GetCheatingCount", "MARAS", GetCheatingCount);
        vm->RegisterFunction("GetCheatedOnPartners", "MARAS", GetCheatedOnPartners);
        vm->RegisterFunction("GetCheatersWhoCheatedOn", "MARAS", GetCheatersWhoCheatedOn);
        vm->RegisterFunction("RegisterChildActor", "MARAS", RegisterChildActor);
        vm->RegisterFunction("RemoveChild", "MARAS", RemoveChild);
        vm->RegisterFunction("GetChildren", "MARAS", GetChildren);
        vm->RegisterFunction("GetChildrenOfActor", "MARAS", GetChildrenOfActor);
        vm->RegisterFunction("UpdateSpouseChildrenFaction", "MARAS", UpdateSpouseChildrenFaction);
        vm->RegisterFunction("IsAvailableForStatus", "MARAS", IsAvailableForStatus);
        vm->RegisterFunction("IsAvailableForStatusWith", "MARAS", IsAvailableForStatusWith);
        vm->RegisterFunction("CanFormRelationship", "MARAS", CanFormRelationship);
        vm->RegisterFunction("GetJsonString", "MARAS", GetJsonString);
        vm->RegisterFunction("GetJsonActor", "MARAS", GetJsonActor);
        vm->RegisterFunction("GetJsonInt", "MARAS", GetJsonInt);
        vm->RegisterFunction("GetJsonFloat", "MARAS", GetJsonFloat);
        vm->RegisterFunction("GetJsonBool", "MARAS", GetJsonBool);
        vm->RegisterFunction("GetJsonArrayLength", "MARAS", GetJsonArrayLength);
        vm->RegisterFunction("GetJsonActorArray", "MARAS", GetJsonActorArray);
        vm->RegisterFunction("GetNpcTypes", "MARAS", GetNpcTypes);
        vm->RegisterFunction("GetNpcTypeEnum", "MARAS", GetNpcTypeEnum);
        vm->RegisterFunction("GetNpcCurrentTypeEnum", "MARAS", GetNpcCurrentTypeEnum);
        vm->RegisterFunction("GetNpcCurrentTypeName", "MARAS", GetNpcCurrentTypeName);

        // Determination helpers
        vm->RegisterFunction("DetermineSocialClass", "MARAS", DetermineSocialClass);
        vm->RegisterFunction("DetermineSkillType", "MARAS", DetermineSkillType);
        vm->RegisterFunction("DetermineTemperament", "MARAS", DetermineTemperament);

        // Bonuses JSON accessors
        vm->RegisterFunction("GetBonusCount", "MARAS", GetBonusCount);
        vm->RegisterFunction("GetBonusPerk", "MARAS", GetBonusPerk);
        vm->RegisterFunction("GetBonusEffectIndex", "MARAS", GetBonusEffectIndex);
        vm->RegisterFunction("GetBonusPerkValue", "MARAS", GetBonusPerkValue);
        vm->RegisterFunction("GetBonusPerkType", "MARAS", GetBonusPerkType);
        vm->RegisterFunction("GetBonusPerkUnit", "MARAS", GetBonusPerkUnit);
        vm->RegisterFunction("GetBonusPerkDescription", "MARAS", GetBonusPerkDescription);

        // Keyword management
        vm->RegisterFunction("HasNpcKeyword", "MARAS", HasNpcKeyword);
        vm->RegisterFunction("AddNpcKeyword", "MARAS", AddNpcKeyword);
        vm->RegisterFunction("RemoveNpcKeyword", "MARAS", RemoveNpcKeyword);

        // Prisma UI functions
        vm->RegisterFunction("ShowUI", "MARAS", ShowUI);
        vm->RegisterFunction("HideUI", "MARAS", HideUI);
        vm->RegisterFunction("IsUIVisible", "MARAS", IsUIVisible);

        // Confirmation modal functions
        RegisterLatentFixed<int32_t>(vm, "ShowConfirmationModal", "MARAS", ShowConfirmationModal);
        RegisterLatentFixed<int32_t>(vm, "ShowSkipWeddingModal", "MARAS", ShowSkipWeddingModal);
        RegisterLatentFixed<int32_t>(vm, "ShowDivorceSettlementModal", "MARAS", ShowDivorceSettlementModal);
        RegisterLatentFixed<RE::BGSLocation*>(vm, "ShowChooseHomeModal", "MARAS", ShowChooseHomeModal);
        RegisterLatentFixed<RE::Actor*>(vm, "ShowChooseAdoptionSpouseModal", "MARAS", ShowChooseAdoptionSpouseModal);
        RegisterLatentFixed<RE::BSFixedString>(vm, "ShowDebugStatusModal", "MARAS", ShowDebugStatusModal);
        RegisterLatentFixed<int32_t>(vm, "ShowChildMigrationAuditModal", "MARAS", ShowChildMigrationAuditModal);
        vm->RegisterFunction("CheckAndSetActionCooldown", "MARAS", CheckAndSetActionCooldown);

        // Settings & Migration functions
        vm->RegisterFunction("SetSettingBool", "MARAS", SetSettingBool);
        vm->RegisterFunction("GetSettingBool", "MARAS", GetSettingBool);
        vm->RegisterFunction("SetSettingInt", "MARAS", SetSettingInt);
        vm->RegisterFunction("GetSettingInt", "MARAS", GetSettingInt);
        vm->RegisterFunction("SetSettingFloat", "MARAS", SetSettingFloat);
        vm->RegisterFunction("GetSettingFloat", "MARAS", GetSettingFloat);
        vm->RegisterFunction("IsSettingsMigrated", "MARAS", IsSettingsMigrated);
        vm->RegisterFunction("MarkSettingsMigrated", "MARAS", MarkSettingsMigrated);

        vm->RegisterFunction("GetStartDialGender", "MARAS", GetStartDialGender);
        vm->RegisterFunction("SetStartDialGender", "MARAS", SetStartDialGender);
        vm->RegisterFunction("GetEnablePolygamy", "MARAS", GetEnablePolygamy);
        vm->RegisterFunction("SetEnablePolygamy", "MARAS", SetEnablePolygamy);
        vm->RegisterFunction("GetDisablePolygamyQuest", "MARAS", GetDisablePolygamyQuest);
        vm->RegisterFunction("SetDisablePolygamyQuest", "MARAS", SetDisablePolygamyQuest);
        vm->RegisterFunction("GetMinRelRankForDial", "MARAS", GetMinRelRankForDial);
        vm->RegisterFunction("SetMinRelRankForDial", "MARAS", SetMinRelRankForDial);

        // Player children / household adoption functions
        vm->RegisterFunction("AssignChildToHouse", "MARAS", AssignChildToHouse);
        vm->RegisterFunction("IssueChildOrder", "MARAS", IssueChildOrder);
        vm->RegisterFunction("ResetChildForceGreeting", "MARAS", ResetChildForceGreeting);
        vm->RegisterFunction("AssignChildPet", "MARAS", AssignChildPet);
        vm->RegisterFunction("GetChildPet", "MARAS", GetChildPet);
        vm->RegisterFunction("GetChildForPet", "MARAS", GetChildForPet);
        vm->RegisterFunction("RemoveChildPet", "MARAS", RemoveChildPet);
        vm->RegisterFunction("SetChildPetBanned", "MARAS", SetChildPetBanned);
        vm->RegisterFunction("IsChildPetBanned", "MARAS", IsChildPetBanned);
        vm->RegisterFunction("IsPetBanned", "MARAS", IsPetBanned);
        vm->RegisterFunction("SetGlobalPetBanned", "MARAS", SetGlobalPetBanned);
        vm->RegisterFunction("IsGlobalPetBanned", "MARAS", IsGlobalPetBanned);
        vm->RegisterFunction("SetChildPetRetryTimestamp", "MARAS", SetChildPetRetryTimestamp);
        vm->RegisterFunction("RecordChildPetRetry", "MARAS", RecordChildPetRetry);
        vm->RegisterFunction("GetChildPetRetryTimestamp", "MARAS", GetChildPetRetryTimestamp);
        vm->RegisterFunction("IsPetRetryAvailable", "MARAS", IsPetRetryAvailable);
        vm->RegisterFunction("AssignChildCritter", "MARAS", AssignChildCritter);
        vm->RegisterFunction("GetChildCritter", "MARAS", GetChildCritter);
        vm->RegisterFunction("GetChildForCritter", "MARAS", GetChildForCritter);
        vm->RegisterFunction("GetChildForPetOrCritter", "MARAS", GetChildForPetOrCritter);
        vm->RegisterFunction("RemoveChildCritter", "MARAS", RemoveChildCritter);
        vm->RegisterFunction("SetChildCritterBanned", "MARAS", SetChildCritterBanned);
        vm->RegisterFunction("IsChildCritterBanned", "MARAS", IsChildCritterBanned);
        vm->RegisterFunction("IsCritterBanned", "MARAS", IsCritterBanned);
        vm->RegisterFunction("SetChildIsNewlyAdopted", "MARAS", SetChildIsNewlyAdopted);
        vm->RegisterFunction("IsChildNewlyAdopted", "MARAS", IsChildNewlyAdopted);
        vm->RegisterFunction("IsChildPlayerAdopted", "MARAS", IsChildPlayerAdopted);
        vm->RegisterFunction("SetHouseInitialMoveDone", "MARAS", SetHouseInitialMoveDone);
        vm->RegisterFunction("IsHouseInitialMoveDone", "MARAS", IsHouseInitialMoveDone);
        vm->RegisterFunction("SetChildChestIntroduced", "MARAS", SetChildChestIntroduced);
        vm->RegisterFunction("IsChildChestIntroduced", "MARAS", IsChildChestIntroduced);
        vm->RegisterFunction("SetGlobalCritterBanned", "MARAS", SetGlobalCritterBanned);
        vm->RegisterFunction("IsGlobalCritterBanned", "MARAS", IsGlobalCritterBanned);
        vm->RegisterFunction("SetChildCritterRetryTimestamp", "MARAS", SetChildCritterRetryTimestamp);
        vm->RegisterFunction("RecordChildCritterRetry", "MARAS", RecordChildCritterRetry);
        vm->RegisterFunction("GetChildCritterRetryTimestamp", "MARAS", GetChildCritterRetryTimestamp);
        vm->RegisterFunction("IsChildCritterRetryAvailable", "MARAS", IsChildCritterRetryAvailable);
        vm->RegisterFunction("GetAllPlayerChildren", "MARAS", GetAllPlayerChildren);
        vm->RegisterFunction("GetChildrenAssignedToHouse", "MARAS", GetChildrenAssignedToHouse);
        vm->RegisterFunction("GetNewlyAdoptedChild", "MARAS", GetNewlyAdoptedChild);
        vm->RegisterFunction("GetChildWithoutPet", "MARAS", GetChildWithoutPet);
        vm->RegisterFunction("GetChildWithoutCritter", "MARAS", GetChildWithoutCritter);
        vm->RegisterFunction("GetChildrenWithoutScene", "MARAS", GetChildrenWithoutScene);
        vm->RegisterFunction("GetWeightedRandomChild", "MARAS", GetWeightedRandomChild);
        vm->RegisterFunction("SetChildPlayedScene", "MARAS", SetChildPlayedScene);
        vm->RegisterFunction("HasChildPlayedScene", "MARAS", HasChildPlayedScene);
        vm->RegisterFunction("RecordChildInteraction", "MARAS", RecordChildInteraction);
        vm->RegisterFunction("GetChildLastInteractionTimestamp", "MARAS", GetChildLastInteractionTimestamp);

        // Deceased Registry queries
        vm->RegisterFunction("IsWidow", "MARAS", IsWidow);
        vm->RegisterFunction("IsNPCDeceased", "MARAS", IsNPCDeceased);
        vm->RegisterFunction("WasMarriedTo", "MARAS", WasMarriedTo);
        vm->RegisterFunction("WasEngagedTo", "MARAS", WasEngagedTo);
        vm->RegisterFunction("WasExOf", "MARAS", WasExOf);
        vm->RegisterFunction("GetDeceasedSpouseNames", "MARAS", GetDeceasedSpouseNames);
        vm->RegisterFunction("GetDeceasedChildrenNames", "MARAS", GetDeceasedChildrenNames);
        vm->RegisterFunction("HasDeceasedChildren", "MARAS", HasDeceasedChildren);
        vm->RegisterFunction("GetDeceasedCount", "MARAS", GetDeceasedCount);

        // Fertility Mode Reloaded & Paternity Intrigue
        vm->RegisterFunction("FindActorByName", "MARAS", FindActorByName);
        vm->RegisterFunction("SetNPCPregnant", "MARAS", SetNPCPregnant);
        vm->RegisterFunction("IsNPCPregnant", "MARAS", IsNPCPregnant);
        vm->RegisterFunction("RegisterUnbornChild", "MARAS", RegisterUnbornChild);
        vm->RegisterFunction("PromoteUnbornToInfant", "MARAS", PromoteUnbornToInfant);
        vm->RegisterFunction("RemoveUnbornChild", "MARAS", RemoveUnbornChild);
        vm->RegisterFunction("FindUnbornChild", "MARAS", FindUnbornChild);
        vm->RegisterFunction("GetUnbornChildren", "MARAS", GetUnbornChildren);
        vm->RegisterFunction("GetUnbornChildCount", "MARAS", GetUnbornChildCount);
        vm->RegisterFunction("RegisterFertilityChild", "MARAS", RegisterFertilityChild);
        vm->RegisterFunction("ResolvePaternitySecret", "MARAS", ResolvePaternitySecret);
        vm->RegisterFunction("ResolvePaternitySecretForPartner", "MARAS", ResolvePaternitySecretForPartner);
        vm->RegisterFunction("ResolvePaternitySecretForBioFather", "MARAS", ResolvePaternitySecretForBioFather);
        vm->RegisterFunction("SetPaternitySecretEdges", "MARAS", SetPaternitySecretEdges);
        vm->RegisterFunction("IsSecretForBioFather", "MARAS", IsSecretForBioFather);
        vm->RegisterFunction("IsSecretForPartner", "MARAS", IsSecretForPartner);
        vm->RegisterFunction("SetChildStage", "MARAS", SetChildStage);
        vm->RegisterFunction("GetChildStage", "MARAS", GetChildStage);
        vm->RegisterFunction("SetChildTrainingLocation", "MARAS", SetChildTrainingLocation);
        vm->RegisterFunction("GetChildTrainingLocation", "MARAS", GetChildTrainingLocation);
        vm->RegisterFunction("SetChildGender", "MARAS", SetChildGender);
        vm->RegisterFunction("GetChildGender", "MARAS", GetChildGender);
        vm->RegisterFunction("SetChildGuardian", "MARAS", SetChildGuardian);
        vm->RegisterFunction("FindSyntheticChild", "MARAS", FindSyntheticChild);
        vm->RegisterFunction("UpdateChildName", "MARAS", UpdateChildName);
        vm->RegisterFunction("GetPrimaryPartner", "MARAS", GetPrimaryPartner);
        vm->RegisterFunction("GetGuardianA", "MARAS", GetGuardianA);
        vm->RegisterFunction("GetGuardianB", "MARAS", GetGuardianB);
        vm->RegisterFunction("GetGuardians", "MARAS", GetGuardians);
        vm->RegisterFunction("GetCoGuardian", "MARAS", GetCoGuardian);
        vm->RegisterFunction("IsGuardian", "MARAS", IsGuardian);
        vm->RegisterFunction("GetBioMother", "MARAS", GetBioMother);
        vm->RegisterFunction("GetBioFather", "MARAS", GetBioFather);
        vm->RegisterFunction("GetMotherPartner", "MARAS", GetMotherPartner);
        vm->RegisterFunction("SetBioMother", "MARAS", SetBioMother);
        vm->RegisterFunction("SetBioFather", "MARAS", SetBioFather);
        vm->RegisterFunction("GetChildName", "MARAS", GetChildName);
        vm->RegisterFunction("FindChildPaternitySecret", "MARAS", FindChildPaternitySecret);
        vm->RegisterFunction("BuildPregnancyAnnouncedMessage", "MARAS", BuildPregnancyAnnouncedMessage);
        vm->RegisterFunction("BuildPregnancyAnnouncedJson", "MARAS", BuildPregnancyAnnouncedJson);
        vm->RegisterFunction("BuildChildBirthMessage", "MARAS", BuildChildBirthMessage);
        vm->RegisterFunction("BuildChildBirthJson", "MARAS", BuildChildBirthJson);

        // Child name generation functions
        vm->RegisterFunction("GenerateRandomChildName", "MARAS", GenerateRandomChildName);
        vm->RegisterFunction("GenerateRandomChildNameForActor", "MARAS", GenerateRandomChildNameForActor);
        vm->RegisterFunction("GenerateRandomChildNameForRace", "MARAS", GenerateRandomChildNameForRace);
        vm->RegisterFunction("GetChildNames", "MARAS", GetChildNames);
        vm->RegisterFunction("ReloadChildNames", "MARAS", ReloadChildNames);

        // Critter name generation functions
        vm->RegisterFunction("GenerateRandomCritterName", "MARAS", GenerateRandomCritterName);
        vm->RegisterFunction("GenerateRandomCritterNameForActor", "MARAS", GenerateRandomCritterNameForActor);
        vm->RegisterFunction("GenerateRandomCritterNameForRace", "MARAS", GenerateRandomCritterNameForRace);
        vm->RegisterFunction("GetCritterNames", "MARAS", GetCritterNames);
        vm->RegisterFunction("ReloadCritterNames", "MARAS", ReloadCritterNames);

        // Banter Coordinator functions
        vm->RegisterFunction("FindBanterCoupleCandidates", "MARAS", FindBanterCoupleCandidates);
        vm->RegisterFunction("IsBanterPartnerPresent", "MARAS", IsBanterPartnerPresent);
        vm->RegisterFunction("ResetBanterCoupleCooldown", "MARAS", ResetBanterCoupleCooldown);
        vm->RegisterFunction("ClearAllBanterCooldowns", "MARAS", ClearAllBanterCooldowns);
        vm->RegisterFunction("GetBanterCoupleCooldownHours", "MARAS", GetBanterCoupleCooldownHours);
        vm->RegisterFunction("SetBanterCoupleCooldownHours", "MARAS", SetBanterCoupleCooldownHours);
        vm->RegisterFunction("RecordScenePlayed", "MARAS", RecordScenePlayed);
        vm->RegisterFunction("IsLocationAllowedForScenes", "MARAS", IsLocationAllowedForScenes);

        MARAS_LOG_INFO("Registered MARAS Papyrus functions including Confirmation Modal and Settings");
        return true;
    }

}  // namespace MARAS::PapyrusInterface
