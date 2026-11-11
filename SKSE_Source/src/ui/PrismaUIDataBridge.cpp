#include "ui/PrismaUIDataBridge.h"
#include "core/AdultChildProgression.h"
#include "core/AdultChildTravelService.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "core/AffectionService.h"
#include "core/SpouseHierarchyManager.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SpouseAssetsService.h"
#include "core/HomeCellService.h"
#include "core/LoggingService.h"
#include "core/NPCIndex.h"
#include "core/MatchmakingService.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/DeceasedRegistry.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIManager.h"
#include "utils/FormUtils.h"
#include "utils/EnumUtils.h"
#include "utils/Settings.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SpouseBuffService.h"
#include "core/BanterCoordinatorService.h"
#include "core/ChildrenFleeService.h"

namespace MARAS {

    namespace {

        std::string GetJsonString(const nlohmann::json& j, std::string_view key, std::string defaultVal = "") {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_string()) return it->get<std::string>();
            if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
            if (it->is_number_unsigned()) return std::to_string(it->get<uint64_t>());
            if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
            return defaultVal;
        }

        bool GetJsonBool(const nlohmann::json& j, std::string_view key, bool defaultVal = false) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_boolean()) return it->get<bool>();
            if (it->is_number()) return it->get<double>() != 0.0;
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                if (s == "true" || s == "1") return true;
                if (s == "false" || s == "0") return false;
            }
            return defaultVal;
        }

        int32_t GetJsonInt(const nlohmann::json& j, std::string_view key, int32_t defaultVal = 0) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_number_integer() || it->is_number_unsigned()) return static_cast<int32_t>(it->get<int64_t>());
            if (it->is_number_float()) return static_cast<int32_t>(it->get<double>());
            if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                int32_t parsed = defaultVal;
                const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                if (ec == std::errc{}) return parsed;
            }
            return defaultVal;
        }

        float GetJsonFloat(const nlohmann::json& j, std::string_view key, float defaultVal = 0.0f) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_number()) return static_cast<float>(it->get<double>());
            if (it->is_boolean()) return it->get<bool>() ? 1.0f : 0.0f;
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                float parsed = defaultVal;
                const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                if (ec == std::errc{}) return parsed;
            }
            return defaultVal;
        }

        RE::FormID ResolveToPlacedRefID(RE::FormID id) {
            if (id == 0 || id == kPlayerFormID) return id;
            if (auto* refr = ActorSvc::GetReference(id)) {
                return refr->GetFormID();
            }
            RE::FormID ref = NPCIndex::GetSingleton().GetRefID(id);
            return ref != 0 ? ref : id;
        }

        inline double RoundFloat(float val, double factor = 100.0) {
            return std::round(static_cast<double>(val) * factor) / factor;
        }

        bool ShouldRevealSecretsInUI() {
            const auto& s = Settings::GetSingleton();
            std::shared_lock lock(s.GetMutex());
            return s.revealSecretsInUI;
        }

        uint64_t MakeOrderedPairKey(RE::FormID a, RE::FormID b) {
            return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
        }

        bool IsSecretCheatingRecord(const CheatingRecord& c) {
            return c.isSecret && c.affairStatus != AffairStatus::Allowed;
        }

        struct SecretAffairPairIndex {
            std::unordered_map<uint64_t, uint32_t> secretCounts;
            std::unordered_map<uint64_t, uint32_t> publicCounts;

            static SecretAffairPairIndex Build(const NPCRelationshipManager& relMgr) {
                SecretAffairPairIndex idx;
                for (const auto& [pairKey, pairData] : relMgr.GetAllRelationshipPairs()) {
                    for (const auto& c : pairData.cheatingRecords) {
                        if (c.cheaterID == 0 || c.affairPartnerID == 0) continue;
                        const uint64_t key = MakeOrderedPairKey(c.cheaterID, c.affairPartnerID);
                        if (IsSecretCheatingRecord(c)) {
                            idx.secretCounts[key]++;
                        } else {
                            idx.publicCounts[key]++;
                        }
                    }
                }
                return idx;
            }

            bool IsSecretAffairPair(RE::FormID a, RE::FormID b) const {
                if (a == 0 || b == 0) return false;
                const uint64_t key = MakeOrderedPairKey(a, b);
                auto itSec = secretCounts.find(key);
                if (itSec == secretCounts.end() || itSec->second == 0) return false;
                auto itPub = publicCounts.find(key);
                return (itPub == publicCounts.end() || itPub->second == 0);
            }
        };

        struct MaskedRelationshipPair {
            RelationshipStatus status = RelationshipStatus::Candidate;
            bool isOpen = false;
            std::vector<StatusChangeRecord> statusHistory;
        };

        std::optional<MaskedRelationshipPair> MaskRelationshipPairForUI(
            RE::FormID actorA,
            RE::FormID actorB,
            RelationshipStatus rawStatus,
            bool rawIsOpen,
            const std::vector<StatusChangeRecord>* rawHistory,
            bool revealSecrets,
            const SecretAffairPairIndex& secretIndex)
        {
            MaskedRelationshipPair out;
            out.status = rawStatus;
            out.isOpen = rawIsOpen;
            if (rawHistory) {
                out.statusHistory = *rawHistory;
            }

            if (revealSecrets) {
                return out;
            }

            const bool isAffairStatus =
                rawStatus == RelationshipStatus::Lover ||
                rawStatus == RelationshipStatus::Dating ||
                rawStatus == RelationshipStatus::ExLover ||
                rawStatus == RelationshipStatus::ExDating ||
                rawStatus == RelationshipStatus::Candidate;

            if (!isAffairStatus || !secretIndex.IsSecretAffairPair(actorA, actorB)) {
                return out;
            }

            std::optional<RelationshipStatus> priorStatus;
            if (rawHistory) {
                for (auto it = rawHistory->rbegin(); it != rawHistory->rend(); ++it) {
                    if (it->toStatus == RelationshipStatus::Divorced || it->toStatus == RelationshipStatus::Jilted) {
                        priorStatus = it->toStatus;
                        break;
                    }
                }
            }

            if (priorStatus.has_value()) {
                out.status = *priorStatus;
                out.isOpen = false;
                out.statusHistory.clear();
                if (rawHistory) {
                    for (const auto& h : *rawHistory) {
                        if (h.toStatus != RelationshipStatus::Lover && h.toStatus != RelationshipStatus::Dating) {
                            out.statusHistory.push_back(h);
                        }
                    }
                }
                return out;
            }

            if (actorA == kPlayerFormID || actorB == kPlayerFormID) {
                out.status = RelationshipStatus::Candidate;
                out.isOpen = false;
                out.statusHistory.clear();
                return out;
            }

            return std::nullopt;
        }

        bool IsUnannouncedSecretPregnancyHiddenInUI(const ChildRegistryEntry& entry, bool revealSecrets) {
            if (revealSecrets) return false;
            if (entry.stage != ChildStage::Unborn) return false;
            if (entry.bioMotherID == kPlayerFormID) return false;
            const auto& sec = entry.paternitySecret;
            return !sec.discovered && !sec.discoveredByPartner && sec.isPregnancySecretForPartner;
        }

        RE::FormID ResolveEffectivePresumedFatherForUI(const NPCRelationshipManager& relMgr, const ChildRegistryEntry& entry) {
            RE::FormID explicitPresumed = relMgr.GetPresumedFather(entry.childID);
            if (explicitPresumed != 0) {
                return explicitPresumed;
            }
            const auto& sec = entry.paternitySecret;
            if (!sec.isSecretForPartner && !sec.isPregnancySecretForPartner && !sec.discoveredByPartner) {
                return 0;
            }
            RE::FormID partnerID = relMgr.GetMotherPartner(entry.childID);
            if (partnerID == 0 ||
                relMgr.MatchesActor(partnerID, entry.bioMotherID) ||
                (entry.bioFatherID != 0 && relMgr.MatchesActor(partnerID, entry.bioFatherID))) {
                return 0;
            }
            return partnerID;
        }

        ChildRegistryEntry MaskChildEntryForUI(const ChildRegistryEntry& entry, RE::FormID resolvedPresumedFatherID, bool revealSecrets) {
            if (revealSecrets) {
                return entry;
            }
            const auto& sec = entry.paternitySecret;
            const bool isSecretPartner = !sec.discovered &&
                                         (sec.isSecretForPartner || sec.isPregnancySecretForPartner) &&
                                         !sec.discoveredByPartner;
            const bool isSecretBio = !sec.discovered &&
                                     sec.isSecretForBioFather &&
                                     !sec.discoveredByBioFather;
            if (!isSecretPartner && !isSecretBio) {
                return entry;
            }

            ChildRegistryEntry masked = entry;
            if (isSecretPartner) {
                if (!entry.isAdopted) {
                    masked.bioFatherID = (resolvedPresumedFatherID != 0 && resolvedPresumedFatherID != kUnknownFatherFormID)
                                             ? resolvedPresumedFatherID
                                             : 0;
                }
                masked.presumedFatherID = 0;
            } else {
                masked.bioFatherID = 0;
                masked.presumedFatherID = 0;
            }
            if (masked.hasBeastblood) {
                const bool visibleParentHasBeastblood =
                    (masked.bioMotherID != 0 && NPCRelationshipManager::IsActorWerewolf(masked.bioMotherID)) ||
                    (masked.bioFatherID != 0 && masked.bioFatherID != kUnknownFatherFormID && NPCRelationshipManager::IsActorWerewolf(masked.bioFatherID));
                if (!visibleParentHasBeastblood) {
                    masked.hasBeastblood = false;
                }
            }
            masked.paternitySecret = PaternitySecret{};
            return masked;
        }

        void SanitizeRelationshipManagerDumpForUI(nlohmann::json& rmJson, const NPCRelationshipManager& relMgr) {
            if (!rmJson.is_object()) return;

            const auto secretIndex = SecretAffairPairIndex::Build(relMgr);

            if (auto itPairs = rmJson.find("relationshipPairs"); itPairs != rmJson.end() && itPairs->is_array()) {
                nlohmann::json sanitizedPairs = nlohmann::json::array();
                for (auto& pairObj : *itPairs) {
                    if (!pairObj.is_object()) continue;

                    RE::FormID actorA = 0;
                    RE::FormID actorB = 0;
                    if (auto itA = pairObj.find("actorA"); itA != pairObj.end() && itA->is_object()) {
                        actorA = PrismaUIDataBridge::StringToFormID(GetJsonString(*itA, "formId"));
                    }
                    if (auto itB = pairObj.find("actorB"); itB != pairObj.end() && itB->is_object()) {
                        actorB = PrismaUIDataBridge::StringToFormID(GetJsonString(*itB, "formId"));
                    }

                    const auto* rawData = (actorA != 0 && actorB != 0) ? relMgr.GetNPCData(actorA, actorB) : nullptr;
                    const RelationshipStatus rawStatus = rawData ? rawData->status : RelationshipStatus::Candidate;
                    const bool rawIsOpen = rawData ? rawData->isOpen : false;
                    const auto* rawHistory = rawData ? &rawData->statusHistory : nullptr;

                    auto maskedPairOpt = MaskRelationshipPairForUI(
                        actorA, actorB, rawStatus, rawIsOpen, rawHistory, /*revealSecrets=*/false, secretIndex);
                    if (!maskedPairOpt.has_value()) {
                        continue;
                    }

                    pairObj["status"] = {
                        {"id", static_cast<uint8_t>(maskedPairOpt->status)},
                        {"name", Utils::RelationshipStatusToString(maskedPairOpt->status)}
                    };
                    pairObj["isOpen"] = maskedPairOpt->isOpen;

                    if (auto itHist = pairObj.find("statusHistory"); itHist != pairObj.end() && itHist->is_array()) {
                        if (maskedPairOpt->statusHistory.empty()) {
                            pairObj["statusHistory"] = nlohmann::json::array();
                        } else if (rawHistory && maskedPairOpt->statusHistory.size() != rawHistory->size()) {
                            nlohmann::json filteredHist = nlohmann::json::array();
                            for (const auto& hObj : *itHist) {
                                if (!hObj.is_object()) continue;
                                int32_t toId = -1;
                                if (auto itTo = hObj.find("toStatus"); itTo != hObj.end() && itTo->is_object()) {
                                    toId = GetJsonInt(*itTo, "id", -1);
                                }
                                if (toId == static_cast<int32_t>(RelationshipStatus::Lover) ||
                                    toId == static_cast<int32_t>(RelationshipStatus::Dating)) {
                                    continue;
                                }
                                filteredHist.push_back(hObj);
                            }
                            pairObj["statusHistory"] = std::move(filteredHist);
                        }
                    }

                    if (auto itCheat = pairObj.find("cheatingRecords"); itCheat != pairObj.end() && itCheat->is_array()) {
                        nlohmann::json visibleCheat = nlohmann::json::array();
                        for (const auto& cObj : *itCheat) {
                            if (!cObj.is_object()) continue;
                            const bool isSec = GetJsonBool(cObj, "isSecret", false);
                            int32_t statusId = -1;
                            if (auto itSt = cObj.find("affairStatus"); itSt != cObj.end() && itSt->is_object()) {
                                statusId = GetJsonInt(*itSt, "id", -1);
                            }
                            if (isSec && statusId != static_cast<int32_t>(AffairStatus::Allowed)) {
                                continue;
                            }
                            visibleCheat.push_back(cObj);
                        }
                        pairObj["cheatingRecords"] = std::move(visibleCheat);
                    }

                    if (auto itChildren = pairObj.find("children"); itChildren != pairObj.end() && itChildren->is_array()) {
                        nlohmann::json visiblePairChildren = nlohmann::json::array();
                        for (const auto& cObj : *itChildren) {
                            if (!cObj.is_object()) continue;
                            const RE::FormID cID = PrismaUIDataBridge::StringToFormID(GetJsonString(cObj, "childId"));
                            if (auto cOpt = relMgr.GetChildEntry(cID); cOpt && IsUnannouncedSecretPregnancyHiddenInUI(*cOpt, false)) {
                                continue;
                            }
                            visiblePairChildren.push_back(cObj);
                        }
                        pairObj["children"] = std::move(visiblePairChildren);
                    }

                    sanitizedPairs.push_back(std::move(pairObj));
                }
                rmJson["relationshipPairs"] = std::move(sanitizedPairs);
            }

            if (auto itReg = rmJson.find("childrenRegistry"); itReg != rmJson.end() && itReg->is_array()) {
                nlohmann::json sanitizedChildren = nlohmann::json::array();
                for (auto& cObj : *itReg) {
                    if (!cObj.is_object()) continue;
                    RE::FormID cID = 0;
                    if (auto itChild = cObj.find("child"); itChild != cObj.end() && itChild->is_object()) {
                        cID = PrismaUIDataBridge::StringToFormID(GetJsonString(*itChild, "formId"));
                    }
                    auto entryOpt = relMgr.GetChildEntry(cID);
                    if (!entryOpt.has_value()) {
                        sanitizedChildren.push_back(std::move(cObj));
                        continue;
                    }
                    if (IsUnannouncedSecretPregnancyHiddenInUI(*entryOpt, /*revealSecrets=*/false)) {
                        continue;
                    }
                    const RE::FormID presFatherID = ResolveEffectivePresumedFatherForUI(relMgr, *entryOpt);
                    const ChildRegistryEntry masked = MaskChildEntryForUI(*entryOpt, presFatherID, /*revealSecrets=*/false);
                    cObj["bioFather"] = {
                        {"formId", std::format("{:08X}", masked.bioFatherID)},
                        {"name", masked.bioFatherID == 0 ? "" : (masked.bioFatherID == kUnknownFatherFormID ? "Unknown" : ActorSvc::GetName(masked.bioFatherID))}
                    };
                    cObj["presumedFather"] = {
                        {"formId", std::format("{:08X}", masked.presumedFatherID)},
                        {"name", masked.presumedFatherID == 0 ? "" : ActorSvc::GetName(masked.presumedFatherID)}
                    };
                    cObj["hasBeastblood"] = masked.hasBeastblood;
                    cObj["paternitySecret"] = {
                        {"isSecret", false},
                        {"isSecretForBioFather", false},
                        {"isSecretForPartner", false},
                        {"discovered", entryOpt->paternitySecret.discovered},
                        {"discoveredByPartner", entryOpt->paternitySecret.discovered ? entryOpt->paternitySecret.discoveredByPartner : false},
                        {"discoveredByBioFather", entryOpt->paternitySecret.discovered ? entryOpt->paternitySecret.discoveredByBioFather : false},
                        {"hasPhenotypeMismatch", entryOpt->paternitySecret.discovered ? entryOpt->paternitySecret.hasPhenotypeMismatch : false},
                        {"lastCheckTimestamp", 0.0f},
                        {"bioFatherName", ""},
                        {"bioFatherRaceFormId", ""}
                    };
                    sanitizedChildren.push_back(std::move(cObj));
                }
                rmJson["childrenRegistry"] = std::move(sanitizedChildren);
            }

            if (rmJson.contains("pregnancyState")) {
                nlohmann::json sanitizedPreg = nlohmann::json::array();
                for (const auto& [cID, entry] : relMgr.GetAllChildren()) {
                    if (entry.stage != ChildStage::Unborn) continue;
                    if (IsUnannouncedSecretPregnancyHiddenInUI(entry, /*revealSecrets=*/false)) continue;
                    const RE::FormID presumedFatherID = ResolveEffectivePresumedFatherForUI(relMgr, entry);
                    const ChildRegistryEntry masked = MaskChildEntryForUI(entry, presumedFatherID, /*revealSecrets=*/false);
                    nlohmann::json pObj;
                    pObj["mother"] = {
                        {"formId", std::format("{:08X}", entry.bioMotherID)},
                        {"name", ActorSvc::GetName(entry.bioMotherID)}
                    };
                    pObj["isPregnant"] = true;
                    pObj["conceptionTimestamp"] = entry.conceptionTimestamp;
                    const RE::FormID visiblePresumedID = (masked.presumedFatherID != 0) ? masked.presumedFatherID : 0;
                    pObj["presumedFather"] = {
                        {"formId", visiblePresumedID != 0 ? std::format("{:08X}", visiblePresumedID) : ""},
                        {"name", visiblePresumedID != 0 ? ActorSvc::GetName(visiblePresumedID) : ""}
                    };
                    pObj["bioFather"] = {
                        {"formId", std::format("{:08X}", masked.bioFatherID)},
                        {"name", (masked.bioFatherID != 0 && masked.bioFatherID != kUnknownFatherFormID) ? ActorSvc::GetName(masked.bioFatherID) : (masked.bioFatherID == kUnknownFatherFormID ? "Unknown" : "")}
                    };
                    sanitizedPreg.push_back(std::move(pObj));
                }
                rmJson["pregnancyState"] = std::move(sanitizedPreg);
            }

            if (auto itIntents = rmJson.find("pendingPartnerIntents"); itIntents != rmJson.end() && itIntents->is_array()) {
                nlohmann::json sanitizedIntents = nlohmann::json::array();
                for (const auto& iObj : *itIntents) {
                    if (!iObj.is_object()) continue;
                    int32_t intentId = -1;
                    if (auto itInt = iObj.find("intent"); itInt != iObj.end() && itInt->is_object()) {
                        intentId = GetJsonInt(*itInt, "id", -1);
                    }
                    if (intentId == static_cast<int32_t>(PartnerIntentType::ConfessTalking) ||
                        intentId == static_cast<int32_t>(PartnerIntentType::ConfessPhysical)) {
                        continue;
                    }
                    sanitizedIntents.push_back(iObj);
                }
                rmJson["pendingPartnerIntents"] = std::move(sanitizedIntents);
            }
        }

    }  // namespace

    std::string PrismaUIDataBridge::FormIDToString(RE::FormID formID) {
        return std::format("{:08X}", formID);
    }

    RE::FormID PrismaUIDataBridge::StringToFormID(const std::string& str) {
        if (str.empty()) return 0;
        std::string_view sv(str);
        if (sv.starts_with("0x") || sv.starts_with("0X")) {
            sv.remove_prefix(2);
        }
        RE::FormID parsed = 0;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed, 16);
        return (ec == std::errc{}) ? parsed : 0;
    }

    void PrismaUIDataBridge::Revert() {
        cachedChildRefIDs.clear();
        lastRequestedDetailNpcID_ = 0;
    }

    void PrismaUIDataBridge::HandleAction(const char* actionJsonStr) {
        if (!actionJsonStr) return;

        MARAS_LOG_INFO("PrismaUIDataBridge: Action payload received: {}", actionJsonStr);

        auto json = nlohmann::json::parse(actionJsonStr, nullptr, /*exceptions=*/false);
        if (!json.is_object()) {
            MARAS_LOG_ERROR("PrismaUIDataBridge: Invalid JSON object in action payload");
            return;
        }

        std::string action = GetJsonString(json, "action");
        nlohmann::json payload = (json.contains("payload") && json["payload"].is_object())
            ? json["payload"]
            : nlohmann::json::object();

        auto* taskIF = SKSE::GetTaskInterface();
        if (!taskIF) {
            MARAS_LOG_ERROR("PrismaUIDataBridge: SKSE TaskInterface unavailable for action '{}'", action);
            return;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        taskIF->AddTask([this, expectedEpoch, action = std::move(action), payload = std::move(payload)]() {
            try {
                std::lock_guard papyrusLock(PapyrusInterface::GetPapyrusMutex());
                if (action != "uiReady" && !IsSessionEpochCurrent(expectedEpoch)) {
                    MARAS_LOG_DEBUG("PrismaUIDataBridge: Dropping stale UI action '{}' across save boundary (expected={}, current={})",
                                    action, expectedEpoch, GetSessionEpoch());
                    return;
                }
                if (action == "uiReady") {
                    PrismaUIManager::GetSingleton().OnUIReady();
                } else if (action == "modalResponse") {
                    PrismaModalManager::GetSingleton().HandleModalResponse(payload);
                } else if (action == "fetchOverview") {
                    SendOverviewData();
                } else if (action == "fetchHouseholdBuffs") {
                    SendHouseholdBuffs();
                } else if (action == "fetchNPCList") {
                    SendNPCList();
                } else if (action == "fetchAllSkyrimNpcs") {
                    SendAllSkyrimNPCs();
                } else if (action == "fetchNPCDetail") {
                    std::string formStr = GetJsonString(payload, "formId");
                    SendNPCDetail(StringToFormID(formStr));
                } else if (action == "fetchMatchmakingData") {
                    SendMatchmakingData();
                } else if (action == "closeUi") {
                    PrismaUIManager::GetSingleton().Hide();
                } else if (action == "setGamePaused") {
                    bool paused = GetJsonBool(payload, "paused", false);
                    PrismaUIManager::GetSingleton().SetDashboardPaused(paused);
                } else if (action == "setAffection") {
                    ProcessSetAffection(payload);
                } else if (action == "setSocialClass") {
                    ProcessSetSocialClass(payload);
                } else if (action == "setSkillType") {
                    ProcessSetSkillType(payload);
                } else if (action == "setTemperament") {
                    ProcessSetTemperament(payload);
                } else if (action == "setPairIsOpen") {
                    ProcessSetPairIsOpen(payload);
                } else if (action == "setHierarchyRank") {
                    ProcessSetHierarchyRank(payload);
                } else if (action == "assignRelationship") {
                    ProcessAssignRelationship(payload);
                } else if (action == "removeRelationship") {
                    ProcessRemoveRelationship(payload);
                } else if (action == "updateSetting") {
                    ProcessUpdateSetting(payload);
                } else if (action == "searchNPCs") {
                    ProcessSearchNPCs(payload);
                } else if (action == "teleportPlayerToNpc") {
                    ProcessTeleportPlayerToNpc(payload);
                } else if (action == "teleportNpcToPlayer") {
                    ProcessTeleportNpcToPlayer(payload);
                } else if (action == "fleeChildrenFromCell") {
                    ChildrenFleeService::GetSingleton().FleeFromPlayerCell();
                } else if (action == "setChildCritterBanned") {
                    ProcessSetChildCritterBanned(payload);
                } else if (action == "setChildPetBanned") {
                    ProcessSetChildPetBanned(payload);
                } else if (action == "dismissChildCritter") {
                    ProcessDismissChildCritter(payload);
                } else if (action == "dismissChildPet") {
                    ProcessDismissChildPet(payload);
                } else if (action == "addChild") {
                    ProcessAddChild(payload);
                } else if (action == "removeChild") {
                    ProcessRemoveChild(payload);
                } else if (action == "updateChild") {
                    ProcessUpdateChild(payload);
                } else if (action == "spawnChild") {
                    ProcessSpawnChild(payload);
                } else if (action == "sendChildToTraining") {
                    ProcessSendChildToTraining(payload);
                } else if (action == "completeChildTraining") {
                    ProcessCompleteChildTraining(payload);
                } else if (action == "relocateFamilyUnit") {
                    ProcessRelocateFamilyUnit(payload);
                } else if (action == "setPaternitySecretEdges") {
                    ProcessSetPaternitySecretEdges(payload);
                } else if (action == "evaluateChildSecret") {
                    ProcessEvaluateChildSecret(payload);
                } else if (action == "setNPCCurrentHome") {
                    ProcessSetNPCCurrentHome(payload);
                } else if (action == "setNPCStayAtHome") {
                    ProcessSetNPCStayAtHome(payload);
                } else if (action == "dumpData") {
                    ProcessDumpData(payload);
                } else if (action == "dumpNPCIndex") {
                    ProcessDumpNPCIndex(payload);
                } else if (action == "skyrimNetMatchSuggest") {
                    ProcessSkyrimNetMatchSuggest(payload);
                } else if (action == "skyrimNetGlobalMatchSuggest") {
                    ProcessSkyrimNetGlobalMatchSuggest(payload);
                } else if (action == "generateMatchMemories") {
                    ProcessGenerateMatchMemories(payload);
                } else if (action == "updateMatchmakingSettings") {
                    ProcessUpdateMatchmakingSettings(payload);
                } else {
                    MARAS_LOG_WARN("PrismaUIDataBridge: Unknown action '{}'", action);
                }
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("PrismaUIDataBridge: Exception in task for action '{}': {}", action, e.what());
            }
        });
    }

    nlohmann::json PrismaUIDataBridge::BuildNPCProfileJson(RE::FormID formID) {
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        nlohmann::json profile;
        profile["formId"] = FormIDToString(formID);

        if (formID == kPlayerFormID) {
            auto player = RE::PlayerCharacter::GetSingleton();
            profile["name"] = ActorSvc::GetName(kPlayerFormID);
            std::string pRace = ActorSvc::GetRaceName(kPlayerFormID);
            profile["race"] = !pRace.empty() ? pRace : "Nord";
            profile["gender"] = (player && player->GetActorBase() && player->GetActorBase()->GetSex() == RE::SEX::kFemale) ? "female" : "male";
        } else {
            profile["name"] = ActorSvc::GetName(formID);
            profile["race"] = ActorSvc::GetRaceName(formID);
            profile["gender"] = (ActorSvc::GetSex(formID) == 1) ? "female" : "male";
        }

        profile["pluginName"] = ActorSvc::GetPluginName(formID);
        profile["socialClass"] = static_cast<uint8_t>(relMgr.GetSocialClass(formID));
        profile["skillType"] = static_cast<uint8_t>(relMgr.GetSkillType(formID));
        profile["temperament"] = static_cast<uint8_t>(relMgr.GetTemperament(formID));
        PartnerIntentType pendingIntentType = relMgr.GetPartnerPendingIntent(formID).type;
        if (!ShouldRevealSecretsInUI() &&
            (pendingIntentType == PartnerIntentType::ConfessTalking ||
             pendingIntentType == PartnerIntentType::ConfessPhysical)) {
            pendingIntentType = PartnerIntentType::None;
        }
        profile["pendingIntent"] = std::string(Utils::PartnerIntentTypeToString(pendingIntentType));

        std::string pkgName = "None";
        bool isChild = relMgr.IsChildNPC(formID);
        if (auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            if (auto currentPkg = actor->GetCurrentPackage()) {
                const char* edid = currentPkg->GetFormEditorID();
                const char* name = currentPkg->GetName();
                if (edid && edid[0] != '\0') {
                    pkgName = fmt::format("{}(0x{:08X})", edid, currentPkg->GetFormID());
                } else if (name && name[0] != '\0') {
                    pkgName = fmt::format("{}(0x{:08X})", name, currentPkg->GetFormID());
                } else {
                    pkgName = fmt::format("0x{:08X}", currentPkg->GetFormID());
                }
            }
        }
        profile["currentPackage"] = pkgName;
        profile["isChild"] = isChild;

        if (isChild) {
            auto& childSvc = PlayerChildrenService::GetSingleton();
            profile["critterBanned"] = childSvc.IsChildCritterBanned(formID);
            profile["petBanned"] = childSvc.IsChildPetBanned(formID);

            RE::FormID critterID = childSvc.GetCritter(formID);
            if (critterID != 0) {
                nlohmann::json critterObj;
                critterObj["formId"] = FormIDToString(critterID);
                critterObj["name"] = ActorSvc::GetName(critterID);
                profile["currentCritter"] = critterObj;
            } else {
                profile["currentCritter"] = nullptr;
            }

            RE::FormID petID = childSvc.GetPet(formID);
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["formId"] = FormIDToString(petID);
                petObj["name"] = ActorSvc::GetName(petID);
                profile["currentPet"] = petObj;
            } else {
                profile["currentPet"] = nullptr;
            }

            std::string blockReason;
            if (relMgr.IsChildBlockedFromAdoption(formID, &blockReason)) {
                profile["blockedFromAdoptionReason"] = blockReason;
            }
        }

        if (auto childOpt = relMgr.GetChildEntry(formID)) {
            const auto& cEntry = *childOpt;
            profile["isChild"] = (cEntry.stage != ChildStage::Adult);
            std::string curName = GetJsonString(profile, "name");
            if (!cEntry.name.empty() && (curName.empty() || curName.rfind("NPC_", 0) == 0)) {
                profile["name"] = cEntry.name;
            }
            std::string stageStr = "child";
            switch (cEntry.stage) {
                case ChildStage::Unborn:   stageStr = "unborn"; break;
                case ChildStage::Infant:   stageStr = "infant"; break;
                case ChildStage::Child:    stageStr = "child"; break;
                case ChildStage::Training: stageStr = "training"; break;
                case ChildStage::Adult:    stageStr = "adult"; break;
            }
            profile["stage"] = stageStr;

            auto* actor = HasPhysicalActor(formID) ? NPCIndex::GetSingleton().GetActorFromFormId(formID) : nullptr;
            bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
            bool isEphemeral = !hasRealActor || (cEntry.stage == ChildStage::Unborn) || (cEntry.stage == ChildStage::Infant) || (cEntry.stage == ChildStage::Training);
            profile["hasRefID"] = hasRealActor;
            profile["isEphemeral"] = isEphemeral;
            if (!cEntry.trainingLocation.empty()) {
                profile["trainingLocation"] = cEntry.trainingLocation;
            }
            int32_t resolvedProfileTrainIdx = cEntry.trainingClassIndex;
            if (!AdultChildProgression::IsValidTrainingClass(resolvedProfileTrainIdx) && !cEntry.trainingLocation.empty()) {
                resolvedProfileTrainIdx = AdultChildProgression::FindTrainingClassByLocation(cEntry.trainingLocation);
            }
            if (AdultChildProgression::IsValidTrainingClass(resolvedProfileTrainIdx)) {
                profile["trainingClassIndex"] = resolvedProfileTrainIdx;
                profile["trainingClassName"] = std::string(NPCRelationshipManager::GetTrainingClassName(resolvedProfileTrainIdx));
            }
            if (cEntry.trainingStartGameDay >= 0.0f) {
                profile["trainingStartGameDay"] = RoundFloat(cEntry.trainingStartGameDay);
            }
            const ChildRegistryEntry visibleProfileChild = MaskChildEntryForUI(
                cEntry, ResolveEffectivePresumedFatherForUI(relMgr, cEntry), ShouldRevealSecretsInUI());
            profile["hasBeastblood"] = visibleProfileChild.hasBeastblood;
            if (cEntry.stage == ChildStage::Unborn || cEntry.gender == 2) {
                profile["gender"] = "unknown";
            } else {
                profile["gender"] = (cEntry.gender == 1) ? "female" : "male";
            }
        }

        // Home resolution: check tenant registration in PlayerHouseService, profile currentHome, or adult child travel destination
        auto& houseSvc = PlayerHouseService::GetSingleton();
        RE::FormID tenantHouseID = houseSvc.GetTenantHouseFormID(formID);
        std::string currentHomeName = PlayerHouseService::GetHouseName(tenantHouseID);
        if (currentHomeName.empty()) {
            if (const auto* npcProf = relMgr.GetNPCProfile(formID)) {
                if (npcProf->currentHome.has_value() && npcProf->currentHome.value() != 0) {
                    currentHomeName = PlayerHouseService::GetHouseName(npcProf->currentHome.value());
                }
            }
        }
        if (currentHomeName.empty()) {
            if (auto childOpt = relMgr.GetChildEntry(formID); childOpt && childOpt->stage == ChildStage::Adult && !IsSyntheticChildID(formID)) {
                currentHomeName = AdultChildTravelService::GetSingleton().GetCurrentDestinationName(formID);
            }
        }
        if (!currentHomeName.empty()) {
            profile["currentHome"] = currentHomeName;
        }
        profile["stayAtHome"] = houseSvc.GetTenantStayAtHome(formID);

        std::string origHomeName = "";
        if (const auto* npcProf = relMgr.GetNPCProfile(formID)) {
            if (npcProf->originalHome.has_value() && npcProf->originalHome.value() != 0) {
                RE::FormID ohID = npcProf->originalHome.value();
                if (auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(ohID)) {
                    origHomeName = loc->GetName();
                } else if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(ohID)) {
                    origHomeName = cell->GetName();
                }
            }
        }
        if (origHomeName.empty()) {
            if (auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
                RE::FormID origCellID = HomeCellService::GetSingleton().GetNpcHome(actor);
                if (origCellID != 0) {
                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(origCellID)) {
                        if (cell->GetName() && cell->GetName()[0] != '\0') {
                            origHomeName = cell->GetName();
                        }
                    }
                }
            }
        }
        if (origHomeName.empty()) {
            if (auto childOpt = relMgr.GetChildEntry(formID)) {
                const ChildRegistryEntry cEntry = MaskChildEntryForUI(
                    *childOpt, ResolveEffectivePresumedFatherForUI(relMgr, *childOpt), ShouldRevealSecretsInUI());
                if (cEntry.guardianAID != kPlayerFormID && cEntry.guardianBID != kPlayerFormID) {
                    RE::FormID parentID = relMgr.ResolveNonPlayerChildTarget(cEntry);
                    if (parentID != 0) {
                        if (const auto* parentProf = relMgr.GetNPCProfile(parentID)) {
                            if (parentProf->originalHome.has_value() && parentProf->originalHome.value() != 0) {
                                RE::FormID ohID = parentProf->originalHome.value();
                                if (auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(ohID)) {
                                    origHomeName = loc->GetName();
                                } else if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(ohID)) {
                                    origHomeName = cell->GetName();
                                }
                            }
                        }
                        if (origHomeName.empty()) {
                            if (auto parentActor = RE::TESForm::LookupByID<RE::Actor>(parentID)) {
                                RE::FormID origCellID = HomeCellService::GetSingleton().GetNpcHome(parentActor);
                                if (origCellID != 0) {
                                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(origCellID)) {
                                        if (cell->GetName() && cell->GetName()[0] != '\0') {
                                            origHomeName = cell->GetName();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!origHomeName.empty()) {
            profile["originalHome"] = origHomeName;
        }
        return profile;
    }

    nlohmann::json PrismaUIDataBridge::BuildSettingsJson() {
        auto& settings = Settings::GetSingleton();
        const bool enablePolygamy = settings.GetEnablePolygamy();
        const bool hasSkyrimNet = IsSkyrimNetAvailable();
        const int32_t startDialGender = settings.GetStartDialGender();
        const int32_t minRelRankForDial = settings.GetMinRelRankForDial();

        nlohmann::json j;
        int32_t snappedTrainingCost = 0;
        {
            std::shared_lock lock(settings.GetMutex());
            j["skipWedding"] = settings.skipWedding;
            j["affectionDecayMult"] = RoundFloat(settings.affectionDecayMultiplier);
            j["lonelinessThresholdDays"] = settings.lonelinessThresholdDays;
            j["enablePolygamy"] = enablePolygamy;
            j["disablePolygamyQuest"] = settings.disablePolygamyQuest;
            j["hasSkyrimNet"] = hasSkyrimNet;
            j["simulateNoSkyrimNet"] = settings.simulateNoSkyrimNet;
            j["startDialGender"] = startDialGender;
            j["minRelRankForDial"] = minRelRankForDial;
            j["enableConfirmPlayerNpc"] = settings.enableConfirmPlayerNpc;
            j["enableConfirmNpcNpc"] = settings.enableConfirmNpcNpc;
            j["confirmStatusChange_Dating"] = settings.confirmStatusChange_Dating;
            j["confirmStatusChange_Engagement"] = settings.confirmStatusChange_Engagement;
            j["confirmStatusChange_Lover"] = settings.confirmStatusChange_Lover;
            j["confirmStatusChange_Marry"] = settings.confirmStatusChange_Marry;
            j["confirmBreakup_Dating"] = settings.confirmBreakup_Dating;
            j["confirmBreakup_Engagement"] = settings.confirmBreakup_Engagement;
            j["confirmBreakup_Lover"] = settings.confirmBreakup_Lover;
            j["confirmDivorce"] = settings.confirmDivorce;
            j["confirmDivorceDuringLowAffectionQuest"] = settings.confirmDivorceDuringLowAffectionQuest;
            j["confirmOpenRelationship"] = settings.confirmOpenRelationship;
            j["confirmCloseRelationship"] = settings.confirmCloseRelationship;
            j["enableConfirmationModal"] = settings.enableConfirmationModal;
            j["actionsCooldown"] = settings.actionsCooldown;
            j["actionsDeclineCooldown"] = settings.actionsDeclineCooldown;
            j["skyrimnetActionRadius"] = RoundFloat(settings.skyrimnetActionRadius);
            j["skyrimnetUseLLMMemories"] = settings.skyrimnetUseLLMMemories;
            j["skyrimnetUseDecisions"] = settings.skyrimnetUseDecisions;
            j["matchmakingConfirmResults"] = settings.matchmakingConfirmResults;
            j["playerPartnerCheatingAgencyEnabled"] = settings.playerPartnerCheatingAgencyEnabled;
            j["playerPartnerOpenRelAgencyEnabled"] = settings.playerPartnerOpenRelAgencyEnabled;
            j["playerPartnerCheatingChanceMultiplier"] = RoundFloat(settings.playerPartnerCheatingChanceMultiplier);
            j["playerPartnerCheatingMaxRollCap"] = settings.playerPartnerCheatingMaxRollCap;
            j["playerPartnerCheatingMaxSecretLovers"] = settings.playerPartnerCheatingMaxSecretLovers;
            j["playerPartnerCheatingAllowWhenHappy"] = settings.playerPartnerCheatingAllowWhenHappy;
            j["playerPartnerCheatingEnableNeglect"] = settings.playerPartnerCheatingEnableNeglect;
            j["playerPartnerCheatingEnableRevenge"] = settings.playerPartnerCheatingEnableRevenge;
            j["playerPartnerCheatingEnableHierarchy"] = settings.playerPartnerCheatingEnableHierarchy;
            j["playerPartnerCheatingEnableChildrenAnchor"] = settings.playerPartnerCheatingEnableChildrenAnchor;
            j["playerPartnerCheatingUseLLM"] = settings.playerPartnerCheatingUseLLM;
            j["playerPartnerCheatingConfirmResults"] = settings.playerPartnerCheatingConfirmResults;
            j["playerPartnerAgencyOpenLoverChanceMultiplier"] = RoundFloat(settings.playerPartnerAgencyOpenLoverChanceMultiplier);
            j["playerPartnerAgencyBreakupChanceMultiplier"] = RoundFloat(settings.playerPartnerAgencyBreakupChanceMultiplier);
            j["playerPartnerAgencyEnableForceGreets"] = settings.playerPartnerAgencyEnableForceGreets;
            j["playerPartnerAgencyEnableHomeArrivalEvents"] = settings.playerPartnerAgencyEnableHomeArrivalEvents;
            j["multLonelinessDecayStart"] = RoundFloat(settings.multLonelinessDecayStart);
            j["multPartnerAgencyScanInterval"] = RoundFloat(settings.multPartnerAgencyScanInterval);
            j["multRecentAttentionWindow"] = RoundFloat(settings.multRecentAttentionWindow);
            j["multVeryRecentVisitWindow"] = RoundFloat(settings.multVeryRecentVisitWindow);
            j["multNeglectGrowthStart"] = RoundFloat(settings.multNeglectGrowthStart);
            j["multContentNeglectStart"] = RoundFloat(settings.multContentNeglectStart);
            j["multProlongedAbsencePenalty"] = RoundFloat(settings.multProlongedAbsencePenalty);
            j["multAffairTalkingEscalation"] = RoundFloat(settings.multAffairTalkingEscalation);
            j["multMinSideFlingAgeForBreakup"] = RoundFloat(settings.multMinSideFlingAgeForBreakup);
            j["multPartnerIntentTTL"] = RoundFloat(settings.multPartnerIntentTTL);
            j["multHomeArrivalCooldown"] = RoundFloat(settings.multHomeArrivalCooldown);
            j["multAffairFatigueStart"] = RoundFloat(settings.multAffairFatigueStart);
            j["multRomanticAttendedBreakup"] = RoundFloat(settings.multRomanticAttendedBreakup);
            j["multSuggestOpenNeglectMin"] = RoundFloat(settings.multSuggestOpenNeglectMin);
            j["alwaysSuccess"] = settings.alwaysSuccessMarriage;
            j["logLevel"] = settings.logLevel;
            j["logDestination"] = settings.logDestination;
            j["toggleUIKey"] = settings.toggleUIKey;
            j["toggleUIModCtrl"] = settings.toggleUIModCtrl;
            j["toggleUIModShift"] = settings.toggleUIModShift;
            j["toggleUIModAlt"] = settings.toggleUIModAlt;
            j["childCustodyGenderPolicy"] = static_cast<uint8_t>(settings.childCustodyGenderPolicy);
            j["childCustodyPlayerPolicy"] = static_cast<uint8_t>(settings.childCustodyPlayerPolicy);
            j["childCustodyPrioritizeBioParent"] = settings.childCustodyPrioritizeBioParent;
            j["globalBanCritter"] = settings.bGlobalBanCritter;
            j["globalBanPet"] = settings.bGlobalBanPet;
            j["childTrainingDurationDays"] = settings.childTrainingDurationDays;
            j["childTrainingCost"] = settings.childTrainingCost;
            j["childTrainingRequireFaction"] = settings.childTrainingRequireFaction;
            snappedTrainingCost = std::max(0, settings.childTrainingCost);

            j["paternityBaseChance"] = settings.paternityBaseChance;
            j["paternityDailyGrowth"] = RoundFloat(settings.paternityDailyGrowth);
            j["paternityMaxChance"] = settings.paternityMaxChance;
            j["paternityPhenotypeBaseChance"] = settings.paternityPhenotypeBaseChance;
            j["paternityPhenotypeDailyGrowth"] = RoundFloat(settings.paternityPhenotypeDailyGrowth);
            j["paternityPhenotypeMaxChance"] = settings.paternityPhenotypeMaxChance;
            j["paternityPhenotypeBirthChance"] = settings.paternityPhenotypeBirthChance;
            j["paternityNonSkyrimNetRevealChance"] = settings.paternityNonSkyrimNetRevealChance;
            j["paternityBreakupChance"] = settings.paternityBreakupChance;
            j["paternityBioStepUpChance"] = settings.paternityBioStepUpChance;
            j["paternityForceDiceRolls"] = settings.paternityForceDiceRolls;
            j["revealSecretsInUI"] = settings.revealSecretsInUI;
            j["uiFullScreen"] = settings.uiFullScreen;
            j["uiScale"] = RoundFloat(settings.uiScale);
            j["autoPauseOnOpen"] = settings.autoPauseOnOpen;
            j["enableScenes"] = settings.enableScenes;
            j["scenesCooldownHours"] = RoundFloat(settings.scenesCooldownHours);
            j["enableScenes_Lover"] = settings.enableScenes_Lover;
            j["enableScenes_Dating"] = settings.enableScenes_Dating;
            j["enableScenes_Engaged"] = settings.enableScenes_Engaged;
            j["enableScenes_Married"] = settings.enableScenes_Married;
            j["enableScenes_OpenRelationship"] = settings.enableScenes_OpenRelationship;
            j["enableScenes_Cheating"] = settings.enableScenes_Cheating;
            j["enableScenes_CheatingDiscovered"] = settings.enableScenes_CheatingDiscovered;
            j["enableScenes_CheatingCaught"] = settings.enableScenes_CheatingCaught;
            j["enableScenes_LocTypeTemple"] = settings.enableScenes_LocTypeTemple;
            j["enableScenes_LocTypeStore"] = settings.enableScenes_LocTypeStore;
            j["enableScenes_LocTypeStewardsDwelling"] = settings.enableScenes_LocTypeStewardsDwelling;
            j["enableScenes_LocTypeInn"] = settings.enableScenes_LocTypeInn;
            j["enableScenes_LocTypeHouse"] = settings.enableScenes_LocTypeHouse;
            j["enableScenes_LocTypeBarracks"] = settings.enableScenes_LocTypeBarracks;
            j["enableScenes_LocTypeFarm"] = settings.enableScenes_LocTypeFarm;
            j["enableScenes_LocTypeGuild"] = settings.enableScenes_LocTypeGuild;
            j["enableScenes_LocTypePlayerHouse"] = settings.enableScenes_LocTypePlayerHouse;
            j["enableScenes_BYOH_LocTypeHomestead"] = settings.enableScenes_BYOH_LocTypeHomestead;
            j["enableScenes_LocTypeTown"] = settings.enableScenes_LocTypeTown;
            j["enableScenes_LocTypeSettlement"] = settings.enableScenes_LocTypeSettlement;
            j["enableScenes_LocTypeOrcStronghold"] = settings.enableScenes_LocTypeOrcStronghold;
            j["enableScenes_LocTypeCity"] = settings.enableScenes_LocTypeCity;
            j["enableScenes_LocTypeLumberMill"] = settings.enableScenes_LocTypeLumberMill;
        }

        j["playerTrainingFactions"] = AdultChildProgression::GetSingleton().BuildPlayerTrainingEligibilityJson();
        j["trainingCatalog"] = AdultChildProgression::GetSingleton().BuildTrainingCatalogJson(snappedTrainingCost);

        bool playerHasDebug = false;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            auto debugSpell = Utils::LookupForm<RE::SpellItem>(0xD, "TT_MARAS.esp", "PrismaUIDataBridge: debugSpell");
            if (debugSpell) {
                playerHasDebug = player->HasSpell(debugSpell);
            }
        }
        j["debugSpell"] = playerHasDebug;
        return j;
    }

    void PrismaUIDataBridge::SendSettingsData() {
        nlohmann::json data;
        data["settings"] = BuildSettingsJson();
        data["isGamePaused"] = PrismaUIManager::GetSingleton().IsDashboardPaused();
        std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveMARASOverview", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendOverviewData() {
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& affSvc = AffectionService::GetSingleton();
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();

        const bool revealSecrets = ShouldRevealSecretsInUI();
        const auto secretIndex = SecretAffairPairIndex::Build(relMgr);

        nlohmann::json data;
        auto calendar = RE::Calendar::GetSingleton();
        data["currentGameDay"] = calendar ? calendar->GetCurrentGameTime() : 0.0f;

        // Stat counts for Player (respecting secret-affair masking when Intrigue Mode is active)
        size_t marriedCount = 0;
        size_t datingCount = 0;
        size_t loverCount = 0;
        size_t engagedCount = 0;
        size_t divorcedCount = 0;
        size_t jiltedCount = 0;
        for (auto partnerID : relMgr.GetAllPartners(kPlayerFormID)) {
            const auto* relData = relMgr.GetNPCData(kPlayerFormID, partnerID);
            const RelationshipStatus rawStatus = relData ? relData->status : relMgr.GetRelationshipStatus(kPlayerFormID, partnerID);
            const bool rawIsOpen = relData ? relData->isOpen : false;
            const auto* rawHistory = relData ? &relData->statusHistory : nullptr;
            auto maskedOpt = MaskRelationshipPairForUI(
                kPlayerFormID, partnerID, rawStatus, rawIsOpen, rawHistory, revealSecrets, secretIndex);
            if (!maskedOpt.has_value()) continue;
            switch (maskedOpt->status) {
                case RelationshipStatus::Married:  ++marriedCount; break;
                case RelationshipStatus::Dating:   ++datingCount; break;
                case RelationshipStatus::Lover:    ++loverCount; break;
                case RelationshipStatus::Engaged:  ++engagedCount; break;
                case RelationshipStatus::Divorced: ++divorcedCount; break;
                case RelationshipStatus::Jilted:   ++jiltedCount; break;
                default: break;
            }
        }
        data["statCounts"]["married"] = marriedCount;
        data["statCounts"]["dating"] = datingCount;
        data["statCounts"]["lover"] = loverCount;
        data["statCounts"]["engaged"] = engagedCount;
        data["statCounts"]["divorced"] = divorcedCount;
        data["statCounts"]["jilted"] = jiltedCount;

        // Hierarchy top 3
        std::vector<std::string> rankFormIDs;
        nlohmann::json rankSpouses = nlohmann::json::array();
        for (int rank = 0; rank < 3; ++rank) {
            RE::FormID id = 0;
            for (auto formID : relMgr.GetAllMarried(kPlayerFormID)) {
                if (hierMgr.GetRank(formID) == rank) {
                    id = formID;
                    break;
                }
            }
            std::string formStr = id != 0 ? FormIDToString(id) : "";
            rankFormIDs.push_back(formStr);

            if (id != 0) {
                nlohmann::json spouseObj = BuildNPCProfileJson(id);
                spouseObj["affection"] = affSvc.GetPermanentAffection(id);
                rankSpouses.push_back(spouseObj);
            }
        }
        data["hierarchyRanks"] = rankFormIDs;
        data["hierarchySpouses"] = rankSpouses;

        // All NPC Profiles (including Player)
        nlohmann::json profilesArray = nlohmann::json::array();
        std::unordered_set<RE::FormID> addedProfiles;

        if (addedProfiles.insert(kPlayerFormID).second) {
            profilesArray.push_back(BuildNPCProfileJson(kPlayerFormID));
        }

        auto allRegistered = relMgr.GetAllRegisteredNPCs();
        for (auto formID : allRegistered) {
            if (addedProfiles.insert(formID).second) {
                profilesArray.push_back(BuildNPCProfileJson(formID));
            }
        }

        cachedChildRefIDs.clear();

        // Pull all indexed children directly from NPCIndex without scanning TESForm::GetAllForms()
        for (const auto& entry : NPCIndex::GetSingleton().GetAllEntries(NPCFilter::ChildOnly)) {
            RE::FormID childID = entry.refFormID;
            if (childID != 0) {
                cachedChildRefIDs.insert(childID);
                if (addedProfiles.insert(childID).second) {
                    nlohmann::json childObj = BuildNPCProfileJson(childID);
                    childObj["isChild"] = true;
                    uint8_t childGender = relMgr.GetChildGender(childID);
                    childObj["gender"] = (childGender == 2) ? "unknown" : ((childGender == 1) ? "female" : "male");
                    profilesArray.push_back(childObj);
                }
            }
        }

        // Also ensure any visible parent and child in childrenByID has an NPC profile
        for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
            if (IsUnannouncedSecretPregnancyHiddenInUI(cEntry, revealSecrets)) {
                continue;
            }
            const RE::FormID presFatherID = ResolveEffectivePresumedFatherForUI(relMgr, cEntry);
            const ChildRegistryEntry visibleChild = MaskChildEntryForUI(cEntry, presFatherID, revealSecrets);

            if (visibleChild.guardianAID != 0 && visibleChild.guardianAID != kPlayerFormID && addedProfiles.insert(visibleChild.guardianAID).second) {
                profilesArray.push_back(BuildNPCProfileJson(visibleChild.guardianAID));
            }
            if (visibleChild.guardianBID != 0 && visibleChild.guardianBID != kPlayerFormID && addedProfiles.insert(visibleChild.guardianBID).second) {
                profilesArray.push_back(BuildNPCProfileJson(visibleChild.guardianBID));
            }
            if (visibleChild.bioMotherID != 0 && visibleChild.bioMotherID != kPlayerFormID && addedProfiles.insert(visibleChild.bioMotherID).second) {
                profilesArray.push_back(BuildNPCProfileJson(visibleChild.bioMotherID));
            }
            if (visibleChild.bioFatherID != 0 && visibleChild.bioFatherID != kPlayerFormID && visibleChild.bioFatherID != kUnknownFatherFormID && addedProfiles.insert(visibleChild.bioFatherID).second) {
                profilesArray.push_back(BuildNPCProfileJson(visibleChild.bioFatherID));
            }
            if (visibleChild.presumedFatherID != 0 && visibleChild.presumedFatherID != kPlayerFormID && addedProfiles.insert(visibleChild.presumedFatherID).second) {
                profilesArray.push_back(BuildNPCProfileJson(visibleChild.presumedFatherID));
            }
            if (cID != 0) {
                cachedChildRefIDs.insert(cID);
            }
            if (cID != 0 && addedProfiles.insert(cID).second) {
                nlohmann::json childObj = BuildNPCProfileJson(cID);
                bool isChild = (cEntry.stage != ChildStage::Adult);
                childObj["isChild"] = isChild;
                std::string currentName = GetJsonString(childObj, "name");
                if ((currentName.empty() || currentName.rfind("NPC_", 0) == 0) && !cEntry.name.empty()) {
                    childObj["name"] = cEntry.name;
                }
                std::string currentRace = GetJsonString(childObj, "race");
                if (currentRace.empty() || currentRace == "Nord") {
                    RE::FormID motherID = relMgr.GetBioMother(cID);
                    RE::FormID parentFormID = motherID != 0 ? motherID :
                        (visibleChild.bioFatherID != 0 && visibleChild.bioFatherID != kUnknownFatherFormID ? visibleChild.bioFatherID : 0);
                    if (parentFormID != 0) {
                        childObj["race"] = ActorSvc::GetRaceName(parentFormID);
                    }
                }
                if (isChild) {
                    uint8_t childGender = relMgr.GetChildGender(cID);
                    if (cEntry.stage == ChildStage::Unborn || childGender == 2) {
                        childObj["gender"] = "unknown";
                    } else {
                        childObj["gender"] = (childGender == 1) ? "female" : "male";
                    }
                }
                profilesArray.push_back(childObj);
            }
        }

        // Also ensure any actor involved in a visible cheating record has an NPC profile
        for (const auto& [pairKey, pairData] : relMgr.GetAllRelationshipPairs()) {
            for (const auto& c : pairData.cheatingRecords) {
                if (!revealSecrets && IsSecretCheatingRecord(c)) {
                    continue;
                }
                if (c.cheaterID != 0 && c.cheaterID != kPlayerFormID && addedProfiles.insert(c.cheaterID).second) {
                    profilesArray.push_back(BuildNPCProfileJson(c.cheaterID));
                }
                if (c.victimID != 0 && c.victimID != kPlayerFormID && addedProfiles.insert(c.victimID).second) {
                    profilesArray.push_back(BuildNPCProfileJson(c.victimID));
                }
                if (c.affairPartnerID != 0 && c.affairPartnerID != kPlayerFormID && addedProfiles.insert(c.affairPartnerID).second) {
                    profilesArray.push_back(BuildNPCProfileJson(c.affairPartnerID));
                }
            }
        }

        data["npcProfiles"] = profilesArray;

        // All visible Children in child-centric registry
        nlohmann::json childrenArray = nlohmann::json::array();
        for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
            if (IsUnannouncedSecretPregnancyHiddenInUI(cEntry, revealSecrets)) {
                continue;
            }
            childrenArray.push_back(BuildChildJson(cEntry));
        }
        data["children"] = childrenArray;

        // All Relationship Pairs (Player + NPCxNPC, masked when Intrigue Mode is active)
        nlohmann::json relsArray = nlohmann::json::array();
        std::unordered_set<uint64_t> visitedPairs;

        std::vector<RE::FormID> searchActors = allRegistered;
        searchActors.push_back(kPlayerFormID);

        for (auto actorA : searchActors) {
            auto partners = relMgr.GetAllPartners(actorA);
            for (auto actorB : partners) {
                uint64_t pairKey = MakeOrderedPairKey(actorA, actorB);
                if (visitedPairs.insert(pairKey).second) {
                    const auto* relData = relMgr.GetNPCData(actorA, actorB);
                    const RelationshipStatus rawStatus = relData ? relData->status : relMgr.GetRelationshipStatus(actorA, actorB);
                    const bool rawIsOpen = relData ? relData->isOpen : false;
                    const auto* rawHistory = relData ? &relData->statusHistory : nullptr;

                    auto maskedOpt = MaskRelationshipPairForUI(
                        actorA, actorB, rawStatus, rawIsOpen, rawHistory, revealSecrets, secretIndex);
                    if (!maskedOpt.has_value()) {
                        continue;
                    }

                    nlohmann::json relObj;
                    relObj["actorA"] = FormIDToString(actorA);
                    relObj["actorB"] = FormIDToString(actorB);
                    relObj["status"] = static_cast<uint8_t>(maskedOpt->status);
                    relObj["isOpen"] = maskedOpt->isOpen;

                    nlohmann::json history = nlohmann::json::array();
                    for (const auto& h : maskedOpt->statusHistory) {
                        nlohmann::json hObj;
                        hObj["fromStatus"] = static_cast<uint8_t>(h.fromStatus);
                        hObj["toStatus"] = static_cast<uint8_t>(h.toStatus);
                        if (std::isnan(h.timestamp) || h.timestamp < 0.0f) {
                            hObj["timestamp"] = -1;
                        } else {
                            hObj["timestamp"] = h.timestamp;
                        }
                        hObj["initiatorFormId"] = FormIDToString(h.initiatorFormID);
                        history.push_back(hObj);
                    }
                    relObj["statusHistory"] = history;
                    relsArray.push_back(relObj);
                }
            }
        }

        data["relationships"] = relsArray;

        // Clean first-class cheating records export (filtered when Intrigue Mode is active)
        nlohmann::json cheatingArray = nlohmann::json::array();
        for (const auto& [pairKey, pairData] : relMgr.GetAllRelationshipPairs()) {
            for (const auto& c : pairData.cheatingRecords) {
                if (!revealSecrets && IsSecretCheatingRecord(c)) {
                    continue;
                }
                nlohmann::json cObj;
                cObj["cheaterId"] = FormIDToString(c.cheaterID);
                cObj["victimId"] = FormIDToString(c.victimID);
                cObj["affairPartnerId"] = FormIDToString(c.affairPartnerID);
                cObj["count"] = c.count;
                cObj["lastTimestamp"] = c.lastTimestamp;
                cObj["isSecret"] = c.isSecret;
                cObj["affairType"] = static_cast<uint8_t>(c.affairType);
                cObj["affairStatus"] = static_cast<uint8_t>(c.affairStatus);
                cObj["affairStage"] = static_cast<uint8_t>((c.victimID == kPlayerFormID) ? c.affairStage : AffairStage::Physical);
                cheatingArray.push_back(cObj);
            }
        }
        data["cheatingRecords"] = cheatingArray;

        // Affection Data
        nlohmann::json affArray = nlohmann::json::array();
        for (auto formID : allRegistered) {
            nlohmann::json affObj;
            affObj["formId"] = FormIDToString(formID);
            affObj["permanent"] = affSvc.GetPermanentAffection(formID);
            affObj["decayMultiplier"] = RoundFloat(affSvc.GetDecayMultiplier());
            affObj["daysSinceLastInteraction"] = std::round(affSvc.GetDaysSinceLastAffection(formID));
            affArray.push_back(affObj);
        }
        data["affectionData"] = affArray;

        // Settings snapshot
        data["settings"] = BuildSettingsJson();
        data["isGamePaused"] = PrismaUIManager::GetSingleton().IsDashboardPaused();

        // Player Homes list
        auto& houseSvc = PlayerHouseService::GetSingleton();
        nlohmann::json homesArray = nlohmann::json::array();
        for (auto houseID : houseSvc.GetAllPlayerHouses()) {
            std::string houseName = PlayerHouseService::GetHouseName(houseID);
            if (!houseName.empty()) {
                nlohmann::json hObj;
                hObj["formId"] = FormIDToString(houseID);
                hObj["name"] = houseName;
                hObj["locationName"] = "";
                homesArray.push_back(hObj);
            }
        }
        data["playerHomes"] = homesArray;

        std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveMARASOverview", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendHouseholdBuffs() {
        nlohmann::json buffsData = SpouseBuffService::GetHouseholdBuffsSnapshot();
        std::string jsonStr = buffsData.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveMARASBuffs", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendNPCList() {
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const bool revealSecrets = ShouldRevealSecretsInUI();
        nlohmann::json list = nlohmann::json::array();
        std::unordered_set<RE::FormID> added;

        if (added.insert(kPlayerFormID).second) {
            list.push_back(BuildNPCProfileJson(kPlayerFormID));
        }

        for (auto formID : relMgr.GetAllRegisteredNPCs()) {
            if (added.insert(formID).second) {
                list.push_back(BuildNPCProfileJson(formID));
            }
        }
        if (cachedChildRefIDs.empty()) {
            for (const auto& entry : NPCIndex::GetSingleton().GetAllEntries(NPCFilter::ChildOnly)) {
                if (entry.refFormID != 0) {
                    cachedChildRefIDs.insert(entry.refFormID);
                }
            }
        }
        for (const auto& [cID, cEntry] : relMgr.GetAllChildren()) {
            if (IsUnannouncedSecretPregnancyHiddenInUI(cEntry, revealSecrets)) {
                continue;
            }
            const RE::FormID presFatherID = ResolveEffectivePresumedFatherForUI(relMgr, cEntry);
            const ChildRegistryEntry visibleChild = MaskChildEntryForUI(cEntry, presFatherID, revealSecrets);

            if (visibleChild.guardianAID != 0 && visibleChild.guardianAID != kPlayerFormID && added.insert(visibleChild.guardianAID).second) {
                list.push_back(BuildNPCProfileJson(visibleChild.guardianAID));
            }
            if (visibleChild.guardianBID != 0 && visibleChild.guardianBID != kPlayerFormID && added.insert(visibleChild.guardianBID).second) {
                list.push_back(BuildNPCProfileJson(visibleChild.guardianBID));
            }
            if (visibleChild.bioMotherID != 0 && visibleChild.bioMotherID != kPlayerFormID && added.insert(visibleChild.bioMotherID).second) {
                list.push_back(BuildNPCProfileJson(visibleChild.bioMotherID));
            }
            if (visibleChild.bioFatherID != 0 && visibleChild.bioFatherID != kPlayerFormID && visibleChild.bioFatherID != kUnknownFatherFormID && added.insert(visibleChild.bioFatherID).second) {
                list.push_back(BuildNPCProfileJson(visibleChild.bioFatherID));
            }
            if (visibleChild.presumedFatherID != 0 && visibleChild.presumedFatherID != kPlayerFormID && added.insert(visibleChild.presumedFatherID).second) {
                list.push_back(BuildNPCProfileJson(visibleChild.presumedFatherID));
            }
            if (cID != 0) {
                cachedChildRefIDs.insert(cID);
                if (added.insert(cID).second) {
                    nlohmann::json childObj = BuildNPCProfileJson(cID);
                    bool isChild = (cEntry.stage != ChildStage::Adult);
                    childObj["isChild"] = isChild;
                    std::string currentName = GetJsonString(childObj, "name");
                    if ((currentName.empty() || currentName.rfind("NPC_", 0) == 0) && !cEntry.name.empty()) {
                        childObj["name"] = cEntry.name;
                    }
                    std::string currentRace = GetJsonString(childObj, "race");
                    if (currentRace.empty() || currentRace == "Nord") {
                        RE::FormID motherID = relMgr.GetBioMother(cID);
                        RE::FormID parentFormID = motherID != 0 ? motherID :
                            (visibleChild.bioFatherID != 0 && visibleChild.bioFatherID != kUnknownFatherFormID ? visibleChild.bioFatherID : 0);
                        if (parentFormID != 0) {
                            childObj["race"] = ActorSvc::GetRaceName(parentFormID);
                        }
                    }
                    if (isChild) {
                        uint8_t childGender = relMgr.GetChildGender(cID);
                        childObj["gender"] = (cEntry.stage == ChildStage::Unborn || childGender == 2) ? "unknown" : ((childGender == 1) ? "female" : "male");
                    }
                    list.push_back(childObj);
                }
            }
        }
        for (const auto& [pairKey, pairData] : relMgr.GetAllRelationshipPairs()) {
            for (const auto& c : pairData.cheatingRecords) {
                if (!revealSecrets && IsSecretCheatingRecord(c)) {
                    continue;
                }
                if (c.cheaterID != 0 && c.cheaterID != kPlayerFormID && added.insert(c.cheaterID).second) {
                    list.push_back(BuildNPCProfileJson(c.cheaterID));
                }
                if (c.victimID != 0 && c.victimID != kPlayerFormID && added.insert(c.victimID).second) {
                    list.push_back(BuildNPCProfileJson(c.victimID));
                }
                if (c.affairPartnerID != 0 && c.affairPartnerID != kPlayerFormID && added.insert(c.affairPartnerID).second) {
                    list.push_back(BuildNPCProfileJson(c.affairPartnerID));
                }
            }
        }

        for (auto childID : cachedChildRefIDs) {
            if (childID != 0 && added.insert(childID).second) {
                if (auto cOpt = relMgr.GetChildEntry(childID); cOpt && IsUnannouncedSecretPregnancyHiddenInUI(*cOpt, revealSecrets)) {
                    continue;
                }
                nlohmann::json childObj = BuildNPCProfileJson(childID);
                childObj["isChild"] = true;
                uint8_t childGender = relMgr.GetChildGender(childID);
                childObj["gender"] = (childGender == 2) ? "unknown" : ((childGender == 1) ? "female" : "male");
                list.push_back(childObj);
            }
        }

        std::string jsonStr = list.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveNPCList", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendNPCDetail(RE::FormID npcFormID) {
        if (npcFormID == 0) return;
        lastRequestedDetailNpcID_ = npcFormID;
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& affSvc = AffectionService::GetSingleton();
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();

        const bool revealSecrets = ShouldRevealSecretsInUI();

        nlohmann::json detail = BuildNPCProfileJson(npcFormID);
        detail["formId"] = FormIDToString(npcFormID);

        std::string actorName = ActorSvc::GetName(npcFormID);
        if (auto childOpt = relMgr.GetChildEntry(npcFormID); childOpt.has_value() && !childOpt->name.empty()) {
            actorName = childOpt->name;
        }
        if (!actorName.empty() && actorName.rfind("NPC_", 0) != 0) {
            detail["name"] = actorName;
        }

        detail["affection"]["permanent"] = affSvc.GetPermanentAffection(npcFormID);
        detail["affection"]["decayMultiplier"] = RoundFloat(affSvc.GetDecayMultiplier());
        detail["affection"]["daysSinceLastInteraction"] = std::round(affSvc.GetDaysSinceLastAffection(npcFormID));

        detail["hierarchyRank"] = hierMgr.GetRank(npcFormID);
        RelationshipStatus visibleStatus = relMgr.GetRelationshipStatus(npcFormID);
        if (!revealSecrets && npcFormID != kPlayerFormID) {
            const auto secretIndex = SecretAffairPairIndex::Build(relMgr);
            const auto* relData = relMgr.GetNPCData(npcFormID, kPlayerFormID);
            const bool rawIsOpen = relData ? relData->isOpen : false;
            const auto* rawHistory = relData ? &relData->statusHistory : nullptr;
            if (auto maskedOpt = MaskRelationshipPairForUI(
                    npcFormID, kPlayerFormID, visibleStatus, rawIsOpen, rawHistory, revealSecrets, secretIndex)) {
                visibleStatus = maskedOpt->status;
            }
        }
        detail["status"] = static_cast<uint8_t>(visibleStatus);
        detail["originalStatus"] = static_cast<uint8_t>(relMgr.GetOriginalRelationshipStatus(npcFormID, kPlayerFormID));

        std::string jsonStr = detail.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveNPCDetail", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendMatchmakingData() {
        SendNPCList();
        SendMatchmakingSettings();
    }

    void PrismaUIDataBridge::SendMatchmakingResults(const std::vector<MatchmakingResult>& results, bool isGlobal) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : results) {
            nlohmann::json item;
            item["actorAFormId"] = FormIDToString(r.actorAFormID);
            item["actorAName"] = r.actorAName;
            item["actorBFormId"] = FormIDToString(r.actorBFormID);
            item["actorBName"] = r.actorBName;
            item["formId"] = FormIDToString(r.actorBFormID);
            item["name"] = r.actorBName;
            item["action"] = r.action;
            item["targetStatus"] = r.targetStatus;
            item["suggestedStatus"] = static_cast<uint8_t>(Utils::StringToRelationshipStatus(r.targetStatus));
            item["memory"] = r.memory;
            item["reasoning"] = r.memory;
            item["isCheating"] = r.isCheating;
            item["selected"] = true;
            item["compatibilityScore"] = 85;
            arr.push_back(item);
        }
        nlohmann::json payload;
        payload["results"] = std::move(arr);
        payload["isGlobal"] = isGlobal;
        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveMatchmakingResults", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendGeneratedMemories(RE::FormID partnerFormId, const nlohmann::json& memoriesArray) {
        nlohmann::json data;
        data["partnerFormId"] = FormIDToString(partnerFormId);
        data["memories"] = memoriesArray;
        std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveGeneratedMemories", jsonStr.c_str());
    }

    void PrismaUIDataBridge::SendMatchmakingSettings() {
        const auto& s = Settings::GetSingleton();
        nlohmann::json j;
        {
            std::shared_lock lock(s.GetMutex());
            j["enabled"] = s.matchmakingEnabled;
            j["intervalDays"] = s.matchmakingIntervalDays;
            j["maxSuggestions"] = s.matchmakingMaxSuggestions;
            j["npcCooldownDays"] = s.matchmakingNpcCooldownDays;
            j["allowBreakups"] = s.matchmakingAllowBreakups;
            j["allowOpenClose"] = s.matchmakingAllowOpenClose;
            j["allowCheating"] = s.matchmakingAllowCheating;
            j["orientation"] = static_cast<uint8_t>(s.matchmakingOrientation);
            j["useLLM"] = s.matchmakingUseLLM;
            j["evaluationStep"] = s.matchmakingEvaluationStep;
            j["generateMemories"] = s.matchmakingGenerateMemories;
            j["confirmResults"] = s.matchmakingConfirmResults;
            j["diceRollChanceNewDating"] = s.diceRollChanceNewDating;
            j["diceRollChanceNewEngaged"] = s.diceRollChanceNewEngaged;
            j["diceRollChanceNewLoverAllowed"] = s.diceRollChanceNewLoverAllowed;
            j["diceRollChanceNewLoverCheating"] = s.diceRollChanceNewLoverCheating;
            j["diceRollChanceAdvance"] = s.diceRollChanceAdvance;
            j["diceRollChanceBreakup"] = s.diceRollChanceBreakup;
            j["diceRollChanceOpenClose"] = s.diceRollChanceOpenClose;
            j["playerPartnerCheatingAgencyEnabled"] = s.playerPartnerCheatingAgencyEnabled;
            j["playerPartnerOpenRelAgencyEnabled"] = s.playerPartnerOpenRelAgencyEnabled;
            j["playerPartnerCheatingChanceMultiplier"] = RoundFloat(s.playerPartnerCheatingChanceMultiplier);
            j["playerPartnerCheatingMaxRollCap"] = s.playerPartnerCheatingMaxRollCap;
            j["playerPartnerCheatingMaxSecretLovers"] = s.playerPartnerCheatingMaxSecretLovers;
            j["playerPartnerCheatingAllowWhenHappy"] = s.playerPartnerCheatingAllowWhenHappy;
            j["playerPartnerCheatingEnableNeglect"] = s.playerPartnerCheatingEnableNeglect;
            j["playerPartnerCheatingEnableRevenge"] = s.playerPartnerCheatingEnableRevenge;
            j["playerPartnerCheatingEnableHierarchy"] = s.playerPartnerCheatingEnableHierarchy;
            j["playerPartnerCheatingEnableChildrenAnchor"] = s.playerPartnerCheatingEnableChildrenAnchor;
            j["playerPartnerCheatingUseLLM"] = s.playerPartnerCheatingUseLLM;
            j["playerPartnerCheatingConfirmResults"] = s.playerPartnerCheatingConfirmResults;
            j["lonelinessThresholdDays"] = s.lonelinessThresholdDays;
            j["playerPartnerAgencyOpenLoverChanceMultiplier"] = RoundFloat(s.playerPartnerAgencyOpenLoverChanceMultiplier);
            j["playerPartnerAgencyBreakupChanceMultiplier"] = RoundFloat(s.playerPartnerAgencyBreakupChanceMultiplier);
            j["playerPartnerAgencyEnableForceGreets"] = s.playerPartnerAgencyEnableForceGreets;
            j["playerPartnerAgencyEnableHomeArrivalEvents"] = s.playerPartnerAgencyEnableHomeArrivalEvents;
            j["multLonelinessDecayStart"] = RoundFloat(s.multLonelinessDecayStart);
            j["multPartnerAgencyScanInterval"] = RoundFloat(s.multPartnerAgencyScanInterval);
            j["multRecentAttentionWindow"] = RoundFloat(s.multRecentAttentionWindow);
            j["multVeryRecentVisitWindow"] = RoundFloat(s.multVeryRecentVisitWindow);
            j["multNeglectGrowthStart"] = RoundFloat(s.multNeglectGrowthStart);
            j["multContentNeglectStart"] = RoundFloat(s.multContentNeglectStart);
            j["multProlongedAbsencePenalty"] = RoundFloat(s.multProlongedAbsencePenalty);
            j["multAffairTalkingEscalation"] = RoundFloat(s.multAffairTalkingEscalation);
            j["multMinSideFlingAgeForBreakup"] = RoundFloat(s.multMinSideFlingAgeForBreakup);
            j["multPartnerIntentTTL"] = RoundFloat(s.multPartnerIntentTTL);
            j["multHomeArrivalCooldown"] = RoundFloat(s.multHomeArrivalCooldown);
            j["multAffairFatigueStart"] = RoundFloat(s.multAffairFatigueStart);
            j["multRomanticAttendedBreakup"] = RoundFloat(s.multRomanticAttendedBreakup);
            j["multSuggestOpenNeglectMin"] = RoundFloat(s.multSuggestOpenNeglectMin);
        }
        std::string jsonStr = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveMatchmakingSettings", jsonStr.c_str());
    }

    void PrismaUIDataBridge::ProcessSkyrimNetMatchSuggest(const nlohmann::json& payload) {
        std::string formStr = GetJsonString(payload, "formId");
        if (formStr.empty() || formStr == "global" || formStr == "all" || formStr == "any") {
            MatchmakingService::GetSingleton().RunGlobalMatchmaking(false);
            return;
        }
        RE::FormID targetId = ResolveToPlacedRefID(StringToFormID(formStr));
        if (targetId != 0) {
            MatchmakingService::GetSingleton().RunManualMatchmaking(targetId);
        } else {
            MatchmakingService::GetSingleton().RunGlobalMatchmaking(false);
        }
    }

    void PrismaUIDataBridge::ProcessSkyrimNetGlobalMatchSuggest(const nlohmann::json& payload) {
        bool autoApply = GetJsonBool(payload, "autoApply", false);
        MatchmakingService::GetSingleton().RunGlobalMatchmaking(autoApply);
    }

    void PrismaUIDataBridge::ProcessGenerateMatchMemories(const nlohmann::json& payload) {
        RE::FormID targetId = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "targetFormId")));
        RE::FormID partnerId = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "partnerFormId")));
        std::string targetStatus = GetJsonString(payload, "targetStatus", "dating");
        std::string action = GetJsonString(payload, "action", "new");

        if (targetId != 0 && partnerId != 0) {
            SkyrimNetMemoryService::GetSingleton().PreviewRelationshipMemories(targetId, partnerId, targetStatus, action);
        }
    }

    void PrismaUIDataBridge::ProcessUpdateMatchmakingSettings(const nlohmann::json& payload) {
        auto& s = Settings::GetSingleton();
        {
            std::unique_lock lock(s.GetMutex());
            if (payload.contains("enabled")) s.matchmakingEnabled = GetJsonBool(payload, "enabled", s.matchmakingEnabled);
            if (payload.contains("intervalDays")) s.matchmakingIntervalDays = std::clamp(GetJsonInt(payload, "intervalDays", s.matchmakingIntervalDays), 1, 30);
            if (payload.contains("maxSuggestions")) s.matchmakingMaxSuggestions = std::clamp(GetJsonInt(payload, "maxSuggestions", s.matchmakingMaxSuggestions), 1, 15);
            if (payload.contains("npcCooldownDays")) s.matchmakingNpcCooldownDays = std::clamp(GetJsonInt(payload, "npcCooldownDays", s.matchmakingNpcCooldownDays), 1, 60);
            if (payload.contains("allowBreakups")) s.matchmakingAllowBreakups = GetJsonBool(payload, "allowBreakups", s.matchmakingAllowBreakups);
            if (payload.contains("allowOpenClose")) s.matchmakingAllowOpenClose = GetJsonBool(payload, "allowOpenClose", s.matchmakingAllowOpenClose);
            if (payload.contains("allowCheating")) s.matchmakingAllowCheating = GetJsonBool(payload, "allowCheating", s.matchmakingAllowCheating);
            if (payload.contains("orientation")) {
                int32_t o = GetJsonInt(payload, "orientation", static_cast<int32_t>(s.matchmakingOrientation));
                if (o >= 0 && o <= static_cast<int32_t>(Settings::MatchmakingOrientation::HeteroAndLesbian)) {
                    s.matchmakingOrientation = static_cast<Settings::MatchmakingOrientation>(o);
                }
            }
            if (payload.contains("useLLM")) s.matchmakingUseLLM = GetJsonBool(payload, "useLLM", s.matchmakingUseLLM);
            if (payload.contains("evaluationStep")) s.matchmakingEvaluationStep = GetJsonBool(payload, "evaluationStep", s.matchmakingEvaluationStep);
            if (payload.contains("generateMemories")) s.matchmakingGenerateMemories = GetJsonBool(payload, "generateMemories", s.matchmakingGenerateMemories);
            if (payload.contains("confirmResults")) s.matchmakingConfirmResults = GetJsonBool(payload, "confirmResults", s.matchmakingConfirmResults);
            if (payload.contains("diceRollChanceNewDating")) s.diceRollChanceNewDating = std::clamp(GetJsonInt(payload, "diceRollChanceNewDating", s.diceRollChanceNewDating), 0, 100);
            if (payload.contains("diceRollChanceNewEngaged")) s.diceRollChanceNewEngaged = std::clamp(GetJsonInt(payload, "diceRollChanceNewEngaged", s.diceRollChanceNewEngaged), 0, 100);
            if (payload.contains("diceRollChanceNewLoverAllowed")) s.diceRollChanceNewLoverAllowed = std::clamp(GetJsonInt(payload, "diceRollChanceNewLoverAllowed", s.diceRollChanceNewLoverAllowed), 0, 100);
            if (payload.contains("diceRollChanceNewLoverCheating")) s.diceRollChanceNewLoverCheating = std::clamp(GetJsonInt(payload, "diceRollChanceNewLoverCheating", s.diceRollChanceNewLoverCheating), 0, 100);
            if (payload.contains("diceRollChanceAdvance")) s.diceRollChanceAdvance = std::clamp(GetJsonInt(payload, "diceRollChanceAdvance", s.diceRollChanceAdvance), 0, 100);
            if (payload.contains("diceRollChanceBreakup")) s.diceRollChanceBreakup = std::clamp(GetJsonInt(payload, "diceRollChanceBreakup", s.diceRollChanceBreakup), 0, 100);
            if (payload.contains("diceRollChanceOpenClose")) s.diceRollChanceOpenClose = std::clamp(GetJsonInt(payload, "diceRollChanceOpenClose", s.diceRollChanceOpenClose), 0, 100);
            if (payload.contains("playerPartnerCheatingAgencyEnabled")) s.playerPartnerCheatingAgencyEnabled = GetJsonBool(payload, "playerPartnerCheatingAgencyEnabled", s.playerPartnerCheatingAgencyEnabled);
            if (payload.contains("playerPartnerOpenRelAgencyEnabled")) s.playerPartnerOpenRelAgencyEnabled = GetJsonBool(payload, "playerPartnerOpenRelAgencyEnabled", s.playerPartnerOpenRelAgencyEnabled);
            if (payload.contains("playerPartnerCheatingChanceMultiplier")) s.playerPartnerCheatingChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerCheatingChanceMultiplier", s.playerPartnerCheatingChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerCheatingMaxRollCap")) s.playerPartnerCheatingMaxRollCap = std::clamp(GetJsonInt(payload, "playerPartnerCheatingMaxRollCap", s.playerPartnerCheatingMaxRollCap), 5, 100);
            if (payload.contains("playerPartnerCheatingMaxSecretLovers")) s.playerPartnerCheatingMaxSecretLovers = std::clamp(GetJsonInt(payload, "playerPartnerCheatingMaxSecretLovers", s.playerPartnerCheatingMaxSecretLovers), 1, 5);
            if (payload.contains("playerPartnerCheatingAllowWhenHappy")) s.playerPartnerCheatingAllowWhenHappy = GetJsonBool(payload, "playerPartnerCheatingAllowWhenHappy", s.playerPartnerCheatingAllowWhenHappy);
            if (payload.contains("playerPartnerCheatingEnableNeglect")) s.playerPartnerCheatingEnableNeglect = GetJsonBool(payload, "playerPartnerCheatingEnableNeglect", s.playerPartnerCheatingEnableNeglect);
            if (payload.contains("playerPartnerCheatingEnableRevenge")) s.playerPartnerCheatingEnableRevenge = GetJsonBool(payload, "playerPartnerCheatingEnableRevenge", s.playerPartnerCheatingEnableRevenge);
            if (payload.contains("playerPartnerCheatingEnableHierarchy")) s.playerPartnerCheatingEnableHierarchy = GetJsonBool(payload, "playerPartnerCheatingEnableHierarchy", s.playerPartnerCheatingEnableHierarchy);
            if (payload.contains("playerPartnerCheatingEnableChildrenAnchor")) s.playerPartnerCheatingEnableChildrenAnchor = GetJsonBool(payload, "playerPartnerCheatingEnableChildrenAnchor", s.playerPartnerCheatingEnableChildrenAnchor);
            if (payload.contains("playerPartnerCheatingUseLLM")) s.playerPartnerCheatingUseLLM = GetJsonBool(payload, "playerPartnerCheatingUseLLM", s.playerPartnerCheatingUseLLM);
            if (payload.contains("playerPartnerCheatingConfirmResults")) s.playerPartnerCheatingConfirmResults = GetJsonBool(payload, "playerPartnerCheatingConfirmResults", s.playerPartnerCheatingConfirmResults);
            if (payload.contains("lonelinessThresholdDays")) s.lonelinessThresholdDays = std::clamp(GetJsonInt(payload, "lonelinessThresholdDays", s.lonelinessThresholdDays), 1, 14);
            if (payload.contains("playerPartnerAgencyOpenLoverChanceMultiplier")) s.playerPartnerAgencyOpenLoverChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerAgencyOpenLoverChanceMultiplier", s.playerPartnerAgencyOpenLoverChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerAgencyBreakupChanceMultiplier")) s.playerPartnerAgencyBreakupChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerAgencyBreakupChanceMultiplier", s.playerPartnerAgencyBreakupChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerAgencyEnableForceGreets")) s.playerPartnerAgencyEnableForceGreets = GetJsonBool(payload, "playerPartnerAgencyEnableForceGreets", s.playerPartnerAgencyEnableForceGreets);
            if (payload.contains("playerPartnerAgencyEnableHomeArrivalEvents")) s.playerPartnerAgencyEnableHomeArrivalEvents = GetJsonBool(payload, "playerPartnerAgencyEnableHomeArrivalEvents", s.playerPartnerAgencyEnableHomeArrivalEvents);
            if (payload.contains("multLonelinessDecayStart")) s.multLonelinessDecayStart = std::clamp(GetJsonFloat(payload, "multLonelinessDecayStart", s.multLonelinessDecayStart), 0.1f, 10.0f);
            if (payload.contains("multPartnerAgencyScanInterval")) s.multPartnerAgencyScanInterval = std::clamp(GetJsonFloat(payload, "multPartnerAgencyScanInterval", s.multPartnerAgencyScanInterval), 0.25f, 10.0f);
            if (payload.contains("multRecentAttentionWindow")) s.multRecentAttentionWindow = std::clamp(GetJsonFloat(payload, "multRecentAttentionWindow", s.multRecentAttentionWindow), 0.1f, 10.0f);
            if (payload.contains("multVeryRecentVisitWindow")) s.multVeryRecentVisitWindow = std::clamp(GetJsonFloat(payload, "multVeryRecentVisitWindow", s.multVeryRecentVisitWindow), 0.1f, 10.0f);
            if (payload.contains("multNeglectGrowthStart")) s.multNeglectGrowthStart = std::clamp(GetJsonFloat(payload, "multNeglectGrowthStart", s.multNeglectGrowthStart), 0.1f, 10.0f);
            if (payload.contains("multContentNeglectStart")) s.multContentNeglectStart = std::clamp(GetJsonFloat(payload, "multContentNeglectStart", s.multContentNeglectStart), 0.1f, 10.0f);
            if (payload.contains("multProlongedAbsencePenalty")) s.multProlongedAbsencePenalty = std::clamp(GetJsonFloat(payload, "multProlongedAbsencePenalty", s.multProlongedAbsencePenalty), 0.1f, 10.0f);
            if (payload.contains("multAffairTalkingEscalation")) s.multAffairTalkingEscalation = std::clamp(GetJsonFloat(payload, "multAffairTalkingEscalation", s.multAffairTalkingEscalation), 0.25f, 10.0f);
            if (payload.contains("multMinSideFlingAgeForBreakup")) s.multMinSideFlingAgeForBreakup = std::clamp(GetJsonFloat(payload, "multMinSideFlingAgeForBreakup", s.multMinSideFlingAgeForBreakup), 0.1f, 10.0f);
            if (payload.contains("multPartnerIntentTTL")) s.multPartnerIntentTTL = std::clamp(GetJsonFloat(payload, "multPartnerIntentTTL", s.multPartnerIntentTTL), 0.25f, 10.0f);
            if (payload.contains("multHomeArrivalCooldown")) s.multHomeArrivalCooldown = std::clamp(GetJsonFloat(payload, "multHomeArrivalCooldown", s.multHomeArrivalCooldown), 0.1f, 10.0f);
            if (payload.contains("multAffairFatigueStart")) s.multAffairFatigueStart = std::clamp(GetJsonFloat(payload, "multAffairFatigueStart", s.multAffairFatigueStart), 0.5f, 20.0f);
            if (payload.contains("multRomanticAttendedBreakup")) s.multRomanticAttendedBreakup = std::clamp(GetJsonFloat(payload, "multRomanticAttendedBreakup", s.multRomanticAttendedBreakup), 0.1f, 10.0f);
            if (payload.contains("multSuggestOpenNeglectMin")) s.multSuggestOpenNeglectMin = std::clamp(GetJsonFloat(payload, "multSuggestOpenNeglectMin", s.multSuggestOpenNeglectMin), 0.1f, 10.0f);
        }
        SendMatchmakingSettings();
    }

    void PrismaUIDataBridge::OnAffectionChanged(RE::FormID npcFormID, int value) {
        nlohmann::json data;
        data["formId"] = FormIDToString(npcFormID);
        data["value"] = value;
        std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onAffectionChanged", jsonStr.c_str());
    }

    void PrismaUIDataBridge::OnStatusChanged(RE::FormID actorA, RE::FormID actorB, uint8_t status) {
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const bool revealSecrets = ShouldRevealSecretsInUI();
        const auto* relData = relMgr.GetNPCData(actorA, actorB);
        uint8_t visibleStatus = status;
        bool visibleIsOpen = relData ? relData->isOpen : false;

        if (!revealSecrets) {
            const auto secretIndex = SecretAffairPairIndex::Build(relMgr);
            const auto* rawHistory = relData ? &relData->statusHistory : nullptr;
            auto maskedOpt = MaskRelationshipPairForUI(
                actorA,
                actorB,
                static_cast<RelationshipStatus>(status),
                visibleIsOpen,
                rawHistory,
                revealSecrets,
                secretIndex);
            if (!maskedOpt.has_value()) {
                return;
            }
            visibleStatus = static_cast<uint8_t>(maskedOpt->status);
            visibleIsOpen = maskedOpt->isOpen;
        }

        nlohmann::json data;
        data["actorA"] = FormIDToString(actorA);
        data["actorB"] = FormIDToString(actorB);
        data["status"] = visibleStatus;
        data["originalStatus"] = static_cast<uint8_t>(relMgr.GetOriginalRelationshipStatus(actorA, actorB));
        if (relData || !revealSecrets) {
            data["isOpen"] = visibleIsOpen;
        }
        if (actorA != 0) {
            data["actorAProfile"] = BuildNPCProfileJson(actorA);
        }
        if (actorB != 0) {
            data["actorBProfile"] = BuildNPCProfileJson(actorB);
        }
        std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onStatusChanged", jsonStr.c_str());
    }

    void PrismaUIDataBridge::OnHierarchyChanged() {
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();
        nlohmann::json ranks = nlohmann::json::array();
        ranks.push_back(FormIDToString(hierMgr.GetSpouseByRank(0)));
        ranks.push_back(FormIDToString(hierMgr.GetSpouseByRank(1)));
        ranks.push_back(FormIDToString(hierMgr.GetSpouseByRank(2)));
        nlohmann::json payload;
        payload["ranks"] = std::move(ranks);
        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onHierarchyChanged", jsonStr.c_str());
    }

    nlohmann::json PrismaUIDataBridge::BuildChildJson(const ChildRegistryEntry& entry) {
        const bool revealSecrets = ShouldRevealSecretsInUI();
        const auto& relMgr = NPCRelationshipManager::GetSingleton();
        const RE::FormID resolvedPresumedFatherID = ResolveEffectivePresumedFatherForUI(relMgr, entry);
        const ChildRegistryEntry visibleEntry = MaskChildEntryForUI(entry, resolvedPresumedFatherID, revealSecrets);
        const RE::FormID visiblePresumedFatherID = revealSecrets ? resolvedPresumedFatherID : visibleEntry.presumedFatherID;

        nlohmann::json cObj;
        cObj["childId"] = FormIDToString(visibleEntry.childID);
        std::string cName = visibleEntry.name;
        if (cName.empty() || cName.rfind("NPC_", 0) == 0) {
            cName = ActorSvc::GetName(visibleEntry.childID);
        }
        cObj["name"] = cName;
        cObj["timestamp"] = visibleEntry.timestamp;
        cObj["isAdopted"] = visibleEntry.isAdopted;
        cObj["guardianAId"] = visibleEntry.guardianAID != 0 ? FormIDToString(visibleEntry.guardianAID) : "";
        cObj["guardianBId"] = visibleEntry.guardianBID != 0 ? FormIDToString(visibleEntry.guardianBID) : "";
        cObj["guardianAName"] = visibleEntry.guardianAID != 0 ? ActorSvc::GetName(visibleEntry.guardianAID) : "";
        cObj["guardianBName"] = visibleEntry.guardianBID != 0 ? ActorSvc::GetName(visibleEntry.guardianBID) : "";
        cObj["bioMotherId"] = visibleEntry.bioMotherID != 0 ? FormIDToString(visibleEntry.bioMotherID) : "";
        cObj["bioFatherId"] = (visibleEntry.bioFatherID != 0 && visibleEntry.bioFatherID != kUnknownFatherFormID) ? FormIDToString(visibleEntry.bioFatherID) : "";
        std::string bioMotherName = visibleEntry.bioMotherID != 0 ? ActorSvc::GetName(visibleEntry.bioMotherID) : "";
        std::string bioFatherName = (visibleEntry.bioFatherID != 0 && visibleEntry.bioFatherID != kUnknownFatherFormID)
                                        ? ActorSvc::GetName(visibleEntry.bioFatherID)
                                        : "";
        if (bioFatherName.empty() && !visibleEntry.paternitySecret.bioFatherName.empty()) {
            bioFatherName = visibleEntry.paternitySecret.bioFatherName;
        }
        cObj["bioMotherName"] = bioMotherName;
        cObj["bioFatherName"] = bioFatherName;
        cObj["presumedFatherId"] = visiblePresumedFatherID != 0 ? FormIDToString(visiblePresumedFatherID) : "";
        cObj["presumedFatherName"] = visiblePresumedFatherID != 0 ? ActorSvc::GetName(visiblePresumedFatherID) : "";
        cObj["conceptionTimestamp"] = visibleEntry.conceptionTimestamp;
        cObj["birthTimestamp"] = visibleEntry.birthTimestamp;

        std::string stageStr = "child";
        switch (visibleEntry.stage) {
            case ChildStage::Unborn:   stageStr = "unborn"; break;
            case ChildStage::Infant:   stageStr = "infant"; break;
            case ChildStage::Child:    stageStr = "child"; break;
            case ChildStage::Training: stageStr = "training"; break;
            case ChildStage::Adult:    stageStr = "adult"; break;
        }
        cObj["stage"] = stageStr;

        auto* actor = HasPhysicalActor(visibleEntry.childID)
            ? NPCIndex::GetSingleton().GetActorFromFormId(visibleEntry.childID)
            : nullptr;
        bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
        bool isEphemeral = !hasRealActor || (visibleEntry.stage == ChildStage::Unborn) || (visibleEntry.stage == ChildStage::Infant) || (visibleEntry.stage == ChildStage::Training);

        cObj["hasRefID"] = hasRealActor;
        cObj["isEphemeral"] = isEphemeral;
        if (!visibleEntry.trainingLocation.empty()) {
            cObj["trainingLocation"] = visibleEntry.trainingLocation;
        }
        int32_t resolvedChildTrainIdx = visibleEntry.trainingClassIndex;
        if (!AdultChildProgression::IsValidTrainingClass(resolvedChildTrainIdx) && !visibleEntry.trainingLocation.empty()) {
            resolvedChildTrainIdx = AdultChildProgression::FindTrainingClassByLocation(visibleEntry.trainingLocation);
        }
        if (AdultChildProgression::IsValidTrainingClass(resolvedChildTrainIdx)) {
            cObj["trainingClassIndex"] = resolvedChildTrainIdx;
            cObj["trainingClassName"] = std::string(NPCRelationshipManager::GetTrainingClassName(resolvedChildTrainIdx));
        }
        if (visibleEntry.trainingStartGameDay >= 0.0f) {
            cObj["trainingStartGameDay"] = RoundFloat(visibleEntry.trainingStartGameDay);
        }
        cObj["isBiologicalChild"] = visibleEntry.isBiologicalChild;
        cObj["hasBeastblood"] = visibleEntry.hasBeastblood;
        cObj["trainingDiscounts"] = AdultChildProgression::GetSingleton().BuildChildTrainingDiscountsJson(visibleEntry);
        cObj["isSecret"] = visibleEntry.paternitySecret.HasActiveSecret();
        cObj["isSecretForBioFather"] = visibleEntry.paternitySecret.isSecretForBioFather;
        cObj["isSecretForPartner"] = visibleEntry.paternitySecret.isSecretForPartner;
        cObj["isPregnancySecretForPartner"] = visibleEntry.paternitySecret.isPregnancySecretForPartner;
        cObj["hasPhenotypeMismatch"] = visibleEntry.paternitySecret.hasPhenotypeMismatch;
        cObj["discoveredByPartner"] = visibleEntry.paternitySecret.discoveredByPartner;
        cObj["discoveredByBioFather"] = visibleEntry.paternitySecret.discoveredByBioFather;

        int childSex = 0;
        if (hasRealActor && actor->GetActorBase()) {
            childSex = (actor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
        } else {
            childSex = visibleEntry.gender;
        }
        if (visibleEntry.stage == ChildStage::Unborn || childSex == 2) {
            cObj["gender"] = "unknown";
        } else {
            cObj["gender"] = (childSex == 1) ? "female" : "male";
        }
        return cObj;
    }

    void PrismaUIDataBridge::OnChildUpdated(const ChildRegistryEntry& entry) {
        const bool revealSecrets = ShouldRevealSecretsInUI();
        if (IsUnannouncedSecretPregnancyHiddenInUI(entry, revealSecrets)) {
            return;
        }
        if (entry.childID != 0) {
            cachedChildRefIDs.insert(entry.childID);
        }
        nlohmann::json cObj = BuildChildJson(entry);
        std::string jsonStr = cObj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onChildUpdated", jsonStr.c_str());
    }

    void PrismaUIDataBridge::OnChildRemoved(RE::FormID childID) {
        cachedChildRefIDs.erase(childID);
        std::string formStr = FormIDToString(childID);
        nlohmann::json payload;
        payload["childId"] = formStr;
        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onChildRemoved", jsonStr.c_str());
    }

    void PrismaUIDataBridge::OnChildPromoted(RE::FormID oldID, RE::FormID newID) {
        cachedChildRefIDs.erase(oldID);
        if (newID != 0) {
            cachedChildRefIDs.insert(newID);
        }
        nlohmann::json payload;
        payload["oldId"] = FormIDToString(oldID);
        payload["newId"] = FormIDToString(newID);
        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("onChildPromoted", jsonStr.c_str());
    }

    void PrismaUIDataBridge::ProcessSetAffection(const nlohmann::json& payload) {
        RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        int value = GetJsonInt(payload, "value", 0);
        if (id != 0) {
            AffectionService::GetSingleton().SetPermanentAffection(id, value);
            MARAS_LOG_INFO("PrismaUIDataBridge: Set affection for {:08X} to {}", id, value);
        }
    }

    void PrismaUIDataBridge::ProcessSetSocialClass(const nlohmann::json& payload) {
        RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        int val = GetJsonInt(payload, "socialClass", 0);
        if (id != 0) {
            NPCRelationshipManager::GetSingleton().SetSocialClass(id, static_cast<int8_t>(val));
        }
    }

    void PrismaUIDataBridge::ProcessSetSkillType(const nlohmann::json& payload) {
        RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        int val = GetJsonInt(payload, "skillType", 0);
        if (id != 0) {
            NPCRelationshipManager::GetSingleton().SetSkillType(id, static_cast<int8_t>(val));
        }
    }

    void PrismaUIDataBridge::ProcessSetTemperament(const nlohmann::json& payload) {
        RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        int val = GetJsonInt(payload, "temperament", 0);
        if (id != 0) {
            NPCRelationshipManager::GetSingleton().SetTemperament(id, static_cast<int8_t>(val));
        }
    }

    void PrismaUIDataBridge::ProcessSetPairIsOpen(const nlohmann::json& payload) {
        RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));
        bool isOpen = GetJsonBool(payload, "isOpen", false);

        if (actorA != 0 && actorB != 0) {
            const bool fromMatchmaking = GetJsonBool(payload, "fromMatchmaking", false);
            const bool sendEvent = !(fromMatchmaking && actorA != kPlayerFormID && actorB != kPlayerFormID);
            const bool hasDualMemories = payload.contains("dualMemories") && payload["dualMemories"].is_array() && !payload["dualMemories"].empty();
            std::optional<SkyrimNetMemoryService::SuppressScope> suppressScope;
            std::optional<SkyrimNetMemoryService::MatchmakingApplyScope> mmScope;
            if (hasDualMemories) {
                suppressScope.emplace();
            } else if (fromMatchmaking && IsSkyrimNetAvailable()) {
                mmScope.emplace();
            }
            NPCRelationshipManager::GetSingleton().SetPairIsOpen(actorA, actorB, isOpen, sendEvent);
            if (hasDualMemories) {
                std::vector<GeneratedMemoryEntry> entries;
                for (const auto& item : payload["dualMemories"]) {
                    if (!item.is_object()) continue;
                    GeneratedMemoryEntry entry;
                    entry.actorName = GetJsonString(item, "actor");
                    entry.actorFormID = ResolveToPlacedRefID(StringToFormID(GetJsonString(item, "actorFormId")));
                    entry.content = GetJsonString(item, "content");
                    entry.location = GetJsonString(item, "location", "Skyrim");
                    entry.emotion = GetJsonString(item, "emotion", "content");
                    entry.importanceScore = GetJsonFloat(item, "importance_score", 0.7f);
                    entry.type = GetJsonString(item, "type", "RELATIONSHIP");
                    if (item.contains("tags") && item["tags"].is_array()) {
                        for (const auto& tag : item["tags"]) {
                            if (tag.is_string()) entry.tags.push_back(tag.get<std::string>());
                        }
                    }
                    entries.push_back(std::move(entry));
                }
                SkyrimNetMemoryService::GetSingleton().IngestMemories(entries, { actorA, actorB });
            }
            if (GetJsonBool(payload, "fromMatchmaking", false)) {
                float today = MatchmakingService::GetSingleton().GetCurrentGameDay();
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorA, today);
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorB, today);
            }
            SendOverviewData();
        }
    }

    void PrismaUIDataBridge::ProcessSetHierarchyRank(const nlohmann::json& payload) {
        RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        int rank = GetJsonInt(payload, "rank", -1);
        if (id != 0) {
            SpouseHierarchyManager::GetSingleton().SetRank(id, rank);
            OnHierarchyChanged();
        }
    }

    void PrismaUIDataBridge::ProcessAssignRelationship(const nlohmann::json& payload) {
        RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));
        uint8_t statusVal = static_cast<uint8_t>(GetJsonInt(payload, "status", 0));

        if (actorA == 0 || actorB == 0) {
            MARAS_LOG_ERROR("ProcessAssignRelationship: actorA ({:08X}) or actorB ({:08X}) is 0", actorA, actorB);
            return;
        }

        // Verify that both actors are valid placed actor references (or Player)
        auto isRef = [](RE::FormID id) -> bool {
            if (id == kPlayerFormID) return true;
            auto* form = RE::TESForm::LookupByID(id);
            if (!form) return false;
            if (form->GetFormType() == RE::FormType::ActorCharacter) return true;
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject()) {
                    return baseObj->GetFormType() == RE::FormType::NPC;
                }
            }
            return false;
        };

        if (!isRef(actorA)) {
            MARAS_LOG_ERROR("ProcessAssignRelationship: actorA FormID {:08X} is NOT a valid Actor Reference!", actorA);
            return;
        }
        if (!isRef(actorB)) {
            MARAS_LOG_ERROR("ProcessAssignRelationship: actorB FormID {:08X} is NOT a valid Actor Reference!", actorB);
            return;
        }

        auto status = static_cast<RelationshipStatus>(statusVal);
        const bool isMatchmakingApply = GetJsonBool(payload, "fromMatchmaking", false) || payload.contains("dualMemories");
        const bool sendEvent = !(isMatchmakingApply && actorA != kPlayerFormID && actorB != kPlayerFormID);
        const bool hasDualMemories = payload.contains("dualMemories") && payload["dualMemories"].is_array() && !payload["dualMemories"].empty();
        std::optional<SkyrimNetMemoryService::SuppressScope> suppressScope;
        std::optional<SkyrimNetMemoryService::MatchmakingApplyScope> mmScope;
        if (hasDualMemories) {
            suppressScope.emplace();
        } else if (isMatchmakingApply && IsSkyrimNetAvailable()) {
            mmScope.emplace();
        }
        bool success = NPCRelationshipManager::GetSingleton().PromoteToStatusWith(actorA, actorB, status, sendEvent);
        if (!success) {
            std::string reason;
            NPCRelationshipManager::GetSingleton().CanFormRelationship(actorA, actorB, status, &reason);
            if (reason.empty()) {
                reason = "Relationship assignment failed.";
            }
            nlohmann::json rej;
            rej["action"] = "assignRelationship";
            rej["actorA"] = FormIDToString(actorA);
            rej["actorB"] = FormIDToString(actorB);
            rej["status"] = statusVal;
            rej["reason"] = reason;
            std::string jsonStr = rej.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            PrismaUIManager::GetSingleton().InteropCall("onActionRejected", jsonStr.c_str());
        } else {
            // Ingest any dual memories attached to this assignment
            if (hasDualMemories) {
                std::vector<GeneratedMemoryEntry> entries;
                for (const auto& item : payload["dualMemories"]) {
                    if (!item.is_object()) continue;
                    GeneratedMemoryEntry entry;
                    entry.actorName = GetJsonString(item, "actor");
                    entry.actorFormID = ResolveToPlacedRefID(StringToFormID(GetJsonString(item, "actorFormId")));
                    entry.content = GetJsonString(item, "content");
                    entry.location = GetJsonString(item, "location", "Skyrim");
                    entry.emotion = GetJsonString(item, "emotion", "content");
                    entry.importanceScore = GetJsonFloat(item, "importance_score", 0.7f);
                    entry.type = GetJsonString(item, "type", "RELATIONSHIP");
                    if (item.contains("tags") && item["tags"].is_array()) {
                        for (const auto& tag : item["tags"]) {
                            if (tag.is_string()) entry.tags.push_back(tag.get<std::string>());
                        }
                    }
                    entries.push_back(entry);
                }
                SkyrimNetMemoryService::GetSingleton().IngestMemories(entries, { actorA, actorB });
            }

            if (GetJsonBool(payload, "fromMatchmaking", false) || payload.contains("dualMemories")) {
                float today = MatchmakingService::GetSingleton().GetCurrentGameDay();
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorA, today);
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorB, today);
            }
        }
        SendOverviewData();
        SendNPCDetail(actorA);
        SendNPCDetail(actorB);
    }

    void PrismaUIDataBridge::ProcessRemoveRelationship(const nlohmann::json& payload) {
        RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));

        if (actorA != 0 && actorB != 0) {
            NPCRelationshipManager::GetSingleton().RemoveRelationship(actorA, actorB);
            if (GetJsonBool(payload, "fromMatchmaking", false)) {
                float today = MatchmakingService::GetSingleton().GetCurrentGameDay();
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorA, today);
                MatchmakingService::GetSingleton().RecordNpcCooldown(actorB, today);
            }
            SendOverviewData();
            if (actorA != 0x14) SendNPCDetail(actorA);
            if (actorB != 0x14) SendNPCDetail(actorB);
        }
    }

    void PrismaUIDataBridge::ProcessUpdateSetting(const nlohmann::json& payload) {
        std::string key = GetJsonString(payload, "key");
        auto& s = Settings::GetSingleton();

        if (key == "debugSpell") {
            bool add = GetJsonBool(payload, "value", false);
            if (auto player = RE::PlayerCharacter::GetSingleton()) {
                auto debugSpell = Utils::LookupForm<RE::SpellItem>(0xD, "TT_MARAS.esp", "PrismaUIDataBridge: debugSpell");
                if (debugSpell) {
                    if (add) player->AddSpell(debugSpell);
                    else player->RemoveSpell(debugSpell);
                }
            }
            SendSettingsData();
            return;
        }

        if (key == "confirmAcceptProposal" || key == "confirmBreakupEngagement" || key == "confirmLowAffectionDivorce") {
            MARAS_LOG_WARN("PrismaUIDataBridge: Deprecated setting key '{}' ignored", key);
            SendSettingsData();
            return;
        }

        bool updated = false;
        auto itVal = payload.find("value");
        if (itVal != payload.end()) {
            if (itVal->is_boolean()) {
                updated = s.SetBoolByKey(key, itVal->get<bool>());
                if (updated && key == "autoPauseOnOpen" && PrismaUIManager::GetSingleton().IsDashboardOpen()) {
                    PrismaUIManager::GetSingleton().SetDashboardPaused(s.autoPauseOnOpen);
                }
            } else if (itVal->is_number()) {
                if (key == "affectionDecayMult" || key == "affectionDecayMultiplier" ||
                    key == "skyrimnetActionRadius" || key == "actionRadius" ||
                    key == "scenesCooldownHours" ||
                    key == "paternityDailyGrowth" || key == "paternityPhenotypeDailyGrowth" ||
                    key == "uiScale" ||
                    key == "playerPartnerCheatingChanceMultiplier" ||
                    key == "playerPartnerAgencyOpenLoverChanceMultiplier" ||
                    key == "playerPartnerAgencyBreakupChanceMultiplier" ||
                    key.starts_with("mult")) {
                    updated = s.SetFloatByKey(key, itVal->get<float>());
                } else {
                    updated = s.SetIntByKey(key, itVal->get<int32_t>());
                }
            }
        }

        if (!updated) {
            MARAS_LOG_WARN("PrismaUIDataBridge: Unknown or invalid setting key '{}'", key);
        }
        if (updated && key == "revealSecretsInUI") {
            SendOverviewData();
            if (lastRequestedDetailNpcID_ != 0) {
                SendNPCDetail(lastRequestedDetailNpcID_);
            }
        } else {
            SendSettingsData();
        }
    }

    void PrismaUIDataBridge::SendAllSkyrimNPCs() {
        nlohmann::json npcs = nlohmann::json::array();
        auto allEntries = NPCIndex::GetSingleton().GetAllEntries();

        for (const auto& entry : allEntries) {
            if (entry.refFormID == 0 || entry.refFormID == 0x07 || entry.refFormID == kPlayerFormID) continue;

            nlohmann::json item;
            item["formId"] = FormIDToString(entry.refFormID);
            item["name"] = entry.nameDisplay;
            item["race"] = entry.raceName;
            item["gender"] = (entry.sex == 1) ? "female" : "male";
            item["isChild"] = entry.isChild;
            item["pluginName"] = entry.pluginName.empty() ? ActorSvc::GetPluginName(entry.refFormID) : entry.pluginName;

            npcs.push_back(std::move(item));
        }

        MARAS_LOG_INFO("PrismaUIDataBridge: Collected {} placed Skyrim NPCs via NPCIndex", npcs.size());

        nlohmann::json response;
        response["npcs"] = std::move(npcs);
        std::string jsonStr = response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveAllSkyrimNPCs", jsonStr.c_str());
    }

    void PrismaUIDataBridge::ProcessSearchNPCs(const nlohmann::json& payload) {
        std::string query = GetJsonString(payload, "query");
        std::string side = GetJsonString(payload, "side", "A");

        nlohmann::json response;
        response["query"] = query;
        response["side"] = side;
        nlohmann::json results = nlohmann::json::array();

        if (query.length() < 3) {
            response["results"] = results;
            std::string jsonStr = response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            PrismaUIManager::GetSingleton().InteropCall("receiveNPCSearchResults", jsonStr.c_str());
            return;
        }

        auto matches = NPCIndex::GetSingleton().SearchBySubstring(query, 50);
        for (const auto& entry : matches) {
            if (entry.refFormID == 0x07 || entry.refFormID == kPlayerFormID) continue;
            results.push_back(BuildNPCProfileJson(entry.refFormID));
        }

        response["results"] = results;
        std::string jsonStr = response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PrismaUIManager::GetSingleton().InteropCall("receiveNPCSearchResults", jsonStr.c_str());
    }

    void PrismaUIDataBridge::ProcessTeleportPlayerToNpc(const nlohmann::json& payload) {
        MARAS_LOG_INFO("ProcessTeleportPlayerToNpc: payload={}", payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        if (npcFormID == 0) return;

        auto* target = ActorSvc::GetReference(npcFormID);
        if (!target) {
            target = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        }

        MARAS_LOG_INFO("ProcessTeleportPlayerToNpc: target={}", target != nullptr);

        if (target) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                player->MoveTo(target);
                MARAS_LOG_INFO("PrismaUIDataBridge: Teleported Player to NPC {:08X}", npcFormID);
            }
        } else {
            MARAS_LOG_WARN("PrismaUIDataBridge: Teleport Player to NPC {:08X} failed (reference not found)", npcFormID);
        }
    }

    void PrismaUIDataBridge::ProcessTeleportNpcToPlayer(const nlohmann::json& payload) {
        MARAS_LOG_INFO("ProcessTeleportNpcToPlayer: payload={}", payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        if (npcFormID == 0) return;

        auto* target = ActorSvc::GetReference(npcFormID);
        if (!target) {
            target = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        }

        MARAS_LOG_INFO("ProcessTeleportNpcToPlayer: target={}", target != nullptr);

        if (target) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                target->MoveTo(player);
                MARAS_LOG_INFO("PrismaUIDataBridge: Teleported NPC {:08X} to Player", npcFormID);
            }
        } else {
            MARAS_LOG_WARN("PrismaUIDataBridge: Teleport NPC {:08X} to Player failed (reference not found)", npcFormID);
        }
    }

    void PrismaUIDataBridge::ProcessSetChildCritterBanned(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        bool banned = GetJsonBool(payload, "banned", false);
        if (childID != 0) {
            PlayerChildrenService::GetSingleton().SetChildCritterBanned(childID, banned);
            MARAS_LOG_INFO("PrismaUIDataBridge: SetChildCritterBanned {:08X} -> {}", childID, banned);
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessSetChildPetBanned(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        bool banned = GetJsonBool(payload, "banned", false);
        if (childID != 0) {
            PlayerChildrenService::GetSingleton().SetChildPetBanned(childID, banned);
            MARAS_LOG_INFO("PrismaUIDataBridge: SetChildPetBanned {:08X} -> {}", childID, banned);
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessDismissChildCritter(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        if (childID != 0) {
            auto& childSvc = PlayerChildrenService::GetSingleton();
            RE::FormID critterID = childSvc.GetCritter(childID);
            if (critterID != 0) {
                if (auto critterActor = RE::TESForm::LookupByID<RE::Actor>(critterID)) {
                    critterActor->Disable();
                    critterActor->SetDelete(true);
                }
                childSvc.RemoveCritter(childID);
                MARAS_LOG_INFO("PrismaUIDataBridge: Dismissed critter {:08X} for child {:08X} (disabled & deleted)", critterID, childID);
            }
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessDismissChildPet(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        if (childID != 0) {
            auto& childSvc = PlayerChildrenService::GetSingleton();
            RE::FormID petID = childSvc.GetPet(childID);
            if (petID != 0) {
                childSvc.RemovePet(childID);
                MARAS_LOG_INFO("PrismaUIDataBridge: Dismissed pet {:08X} for child {:08X} (retained in world)", petID, childID);
            }
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessAddChild(const nlohmann::json& payload) {
        RE::FormID actorA = StringToFormID(GetJsonString(payload, "actorA"));
        RE::FormID actorB = StringToFormID(GetJsonString(payload, "actorB"));
        if (actorA == 0) {
            actorA = StringToFormID(GetJsonString(payload, "guardianAId"));
        }
        if (actorB == 0) {
            actorB = StringToFormID(GetJsonString(payload, "guardianBId"));
        }
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        std::string houseStr = GetJsonString(payload, "houseId");
        bool assignSpouseHome = GetJsonBool(payload, "assignSpouseHome", false);

        RE::FormID explicitHouseID = 0;
        if (!houseStr.empty()) {
            explicitHouseID = StringToFormID(houseStr);
            if (explicitHouseID == 0) {
                explicitHouseID = PlayerHouseService::GetSingleton().FindHouseByName(houseStr);
            }
        }

        if (childID != 0 && (actorA != 0 || actorB != 0)) {
            std::string blockReason;
            if (NPCRelationshipManager::GetSingleton().IsChildBlockedFromAdoption(childID, &blockReason)) {
                MARAS_LOG_WARN("PrismaUIDataBridge: ProcessAddChild blocked - child {:08X} cannot be adopted: {}", childID, blockReason);
                return;
            }

            // Player adoption: delegate directly to Papyrus AdoptChildGlobal on current SKSE task thread
            if (actorA == kPlayerFormID || actorB == kPlayerFormID) {
                RE::FormID spouseID = (actorA == kPlayerFormID) ? actorB : actorA;
                auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childID);
                if (!childActor) {
                    MARAS_LOG_ERROR("PrismaUIDataBridge: ProcessAddChild failed - child {:08X} not found as Actor", childID);
                    return;
                }
                auto* spouseActor = (spouseID != 0 && spouseID != kPlayerFormID) ? RE::TESForm::LookupByID<RE::Actor>(spouseID) : nullptr;
                auto* houseForm = explicitHouseID != 0 ? RE::TESForm::LookupByID(explicitHouseID) : nullptr;

                MARAS_LOG_INFO("PrismaUIDataBridge: Player adoption for child {:08X} (spouse: {:08X}, house: {:08X}) - calling Papyrus AdoptChildGlobal",
                               childID, spouseID, explicitHouseID);

                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                if (!vm) {
                    MARAS_LOG_ERROR("PrismaUIDataBridge: VirtualMachine unavailable for AdoptChildGlobal");
                    return;
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> nullCallback;
                auto childArg = childActor;
                auto spouseArg = spouseActor;
                auto houseArg = houseForm;
                auto args = RE::MakeFunctionArguments(std::move(childArg), std::move(spouseArg), std::move(houseArg));
                vm->DispatchStaticCall("TTM_ServiceChildrenLogic", "AdoptChildGlobal", args, nullCallback);
                return;
            }

            // NPC-to-NPC adoption: retain direct native C++ pipeline
            (void)NPCRelationshipManager::GetSingleton().RegisterChildActor(
                childID, actorA, actorB, explicitHouseID, 0, 0, 0,
                static_cast<uint8_t>(ChildStage::Child), true);
            MARAS_LOG_INFO("PrismaUIDataBridge: Registered child {:08X} with guardians {:08X} & {:08X}", childID, actorA, actorB);

            if (explicitHouseID != 0) {
                auto relocated = PlayerHouseService::GetSingleton().RelocateFamilyUnit(childID, explicitHouseID, assignSpouseHome);
                MARAS_LOG_INFO("PrismaUIDataBridge: Relocated family unit ({} members) for newly added child {:08X} to house {:08X}",
                               relocated.size(), childID, explicitHouseID);
                for (auto memberID : relocated) {
                    SendNPCDetail(memberID);
                }
            }

            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessRemoveChild(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID != 0) {
            NPCRelationshipManager::GetSingleton().RemoveChild(childID);
            MARAS_LOG_INFO("PrismaUIDataBridge: Removed child {:08X} from relationship pair", childID);
            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessUpdateChild(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) return;

        auto& mgr = NPCRelationshipManager::GetSingleton();
        bool changed = false;

        if (payload.contains("guardianAId")) {
            RE::FormID gAID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "guardianAId")));
            changed |= mgr.SetChildGuardian(childID, 0, gAID);
        }
        if (payload.contains("guardianBId")) {
            RE::FormID gBID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "guardianBId")));
            changed |= mgr.SetChildGuardian(childID, 1, gBID);
        }
        if (payload.contains("bioMotherId")) {
            RE::FormID bioMID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "bioMotherId")));
            changed |= mgr.SetBioMother(childID, bioMID);
        }
        const bool hasExplicitPresumedInPayload =
            payload.contains("presumedFatherId") && !GetJsonString(payload, "presumedFatherId").empty();
        if (payload.contains("bioFatherId")) {
            RE::FormID bioFID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "bioFatherId")));
            changed |= mgr.SetBioFather(childID, bioFID);
            if (!ShouldRevealSecretsInUI() && !hasExplicitPresumedInPayload) {
                changed |= mgr.SetPresumedFather(childID, 0);
                changed |= mgr.SetPaternitySecretEdges(childID, false, false);
            }
        }
        if (payload.contains("presumedFatherId")) {
            RE::FormID presFID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "presumedFatherId")));
            changed |= mgr.SetPresumedFather(childID, presFID);
        }
        if (payload.contains("hasBeastblood")) {
            changed |= mgr.SetChildBeastblood(childID, GetJsonBool(payload, "hasBeastblood", false));
        }

        if (changed) {
            MARAS_LOG_INFO("PrismaUIDataBridge: Updated child {:08X} parent/guardian assignments", childID);
            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessSpawnChild(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID != 0) {
            MARAS_LOG_INFO("PrismaUIDataBridge: Spawning synthetic child {:08X}", childID);
            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                std::string childIdStr = FormIDToString(childID);
                float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                SKSE::ModCallbackEvent modEvent("maras_spawn_synthetic_child", childIdStr, childIdFloat, nullptr);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("PrismaUIDataBridge: Dispatched maras_spawn_synthetic_child for {:08X}", childID);
            }
        }
    }

    void PrismaUIDataBridge::ProcessSendChildToTraining(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        int32_t classIdx = GetJsonInt(payload, "trainingClassIndex", 1);
        if (childID == 0 || !AdultChildProgression::IsValidTrainingClass(classIdx)) {
            MARAS_LOG_WARN("PrismaUIDataBridge: ProcessSendChildToTraining rejected invalid payload (childID={:08X}, classIdx={})",
                           childID, classIdx);
            return;
        }
        MARAS_LOG_INFO("PrismaUIDataBridge: ProcessSendChildToTraining child {:08X} -> classIdx {} ('{}')",
                       childID, classIdx, AdultChildProgression::GetTrainingClassName(classIdx));
        std::string failReason;
        if (!NPCRelationshipManager::GetSingleton().SendChildToTraining(childID, classIdx, &failReason)) {
            MARAS_LOG_WARN("PrismaUIDataBridge: SendChildToTraining failed for child {:08X} -> classIdx {}: {}",
                           childID, classIdx, failReason.empty() ? "unknown reason" : failReason);
        }
    }

    void PrismaUIDataBridge::ProcessCompleteChildTraining(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) {
            MARAS_LOG_WARN("PrismaUIDataBridge: ProcessCompleteChildTraining rejected payload with childID=0");
            return;
        }
        MARAS_LOG_INFO("PrismaUIDataBridge: ProcessCompleteChildTraining child {:08X}", childID);
        if (!NPCRelationshipManager::GetSingleton().CompleteChildTraining(childID)) {
            MARAS_LOG_WARN("PrismaUIDataBridge: CompleteChildTraining returned false for child {:08X}", childID);
        }
    }

    void PrismaUIDataBridge::ProcessRelocateFamilyUnit(const nlohmann::json& payload) {
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "npcId"));
        if (npcFormID == 0) {
            npcFormID = StringToFormID(GetJsonString(payload, "childId"));
        }
        if (npcFormID == 0) {
            npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        }
        std::string houseStr = GetJsonString(payload, "targetHouseId");
        if (houseStr.empty()) {
            houseStr = GetJsonString(payload, "home");
        }
        bool moveParents = GetJsonBool(payload, "moveParents", true);

        RE::FormID explicitHouseID = 0;
        if (!houseStr.empty()) {
            explicitHouseID = StringToFormID(houseStr);
            if (explicitHouseID == 0) {
                explicitHouseID = PlayerHouseService::GetSingleton().FindHouseByName(houseStr);
            }
        }

        if (npcFormID != 0 && explicitHouseID != 0) {
            auto relocated = PlayerHouseService::GetSingleton().RelocateFamilyUnit(npcFormID, explicitHouseID, moveParents);
            MARAS_LOG_INFO("PrismaUIDataBridge: Relocated family unit ({} members) for NPC {:08X} to house {:08X}",
                           relocated.size(), npcFormID, explicitHouseID);
            SendOverviewData();
            for (auto memberID : relocated) {
                SendNPCDetail(memberID);
            }
        }
    }

    void PrismaUIDataBridge::ProcessSetPaternitySecretEdges(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        bool secretBio = GetJsonBool(payload, "isSecretForBioFather", false);
        bool secretPartner = GetJsonBool(payload, "isSecretForPartner", false);
        std::optional<bool> pregSecretPartner = std::nullopt;
        if (payload.contains("isPregnancySecretForPartner") && payload["isPregnancySecretForPartner"].is_boolean()) {
            pregSecretPartner = payload["isPregnancySecretForPartner"].get<bool>();
        }

        if (childID != 0) {
            NPCRelationshipManager::GetSingleton().SetPaternitySecretEdges(childID, secretBio, secretPartner, pregSecretPartner);
            MARAS_LOG_INFO("PrismaUIDataBridge: SetPaternitySecretEdges child {:08X} -> secretBio: {}, secretPartner: {}",
                           childID, secretBio, secretPartner);
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessEvaluateChildSecret(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID != 0) {
            MARAS_LOG_INFO("PrismaUIDataBridge: ProcessEvaluateChildSecret for child {:08X}", childID);
            bool started = NPCRelationshipManager::GetSingleton().EvaluatePaternitySecret(childID);
            if (!started) {
                MARAS_LOG_WARN("PrismaUIDataBridge: EvaluatePaternitySecret failed to start for child {:08X}", childID);
                SendOverviewData();
                SendNPCDetail(childID);
            }
        }
    }

    void PrismaUIDataBridge::ProcessSetNPCCurrentHome(const nlohmann::json& payload) {
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        std::string homeStr = GetJsonString(payload, "home");
        if (npcFormID == 0) return;

        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto& relMgr = NPCRelationshipManager::GetSingleton();

        if (homeStr.empty() || homeStr == "Unset") {
            if (!houseSvc.CanRemoveTenant(npcFormID)) {
                MARAS_LOG_WARN("PrismaUIDataBridge: Cannot remove tenant {:08X} from player house due to family/children ties", npcFormID);
                SendOverviewData();
                SendNPCDetail(npcFormID);
                return;
            }
            if (houseSvc.RemoveTenantFromPlayerHouse(npcFormID)) {
                MARAS_LOG_INFO("PrismaUIDataBridge: Removed tenant {:08X} from player house", npcFormID);
                if (relMgr.IsChildNPC(npcFormID)) {
                    PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(npcFormID);
                }
            } else {
                MARAS_LOG_WARN("PrismaUIDataBridge: Failed to remove tenant {:08X} from player house", npcFormID);
            }
        } else {
            RE::FormID targetHouseID = 0;
            if (homeStr.length() == 8) {
                targetHouseID = StringToFormID(homeStr);
            }
            if (targetHouseID == 0) {
                targetHouseID = houseSvc.FindHouseByName(homeStr);
            }

            if (targetHouseID != 0) {
                auto relocated = houseSvc.RelocateFamilyUnit(npcFormID, targetHouseID, true);
                MARAS_LOG_INFO("PrismaUIDataBridge: ProcessSetNPCCurrentHome relocated {} family members for NPC {:08X} to house {:08X}",
                               relocated.size(), npcFormID, targetHouseID);
                SendOverviewData();
                SendNPCDetail(npcFormID);
                for (auto mID : relocated) {
                    SendNPCDetail(mID);
                }
                return;
            } else {
                MARAS_LOG_WARN("PrismaUIDataBridge: Player house '{}' not found for tenant {:08X}", homeStr, npcFormID);
            }
        }

        SendOverviewData();
        SendNPCDetail(npcFormID);
    }

    void PrismaUIDataBridge::ProcessSetNPCStayAtHome(const nlohmann::json& payload) {
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        bool stayAtHome = GetJsonBool(payload, "stayAtHome", true);
        if (npcFormID == 0) return;

        PlayerHouseService::GetSingleton().SetTenantStayAtHome(npcFormID, stayAtHome);
        MARAS_LOG_INFO("PrismaUIDataBridge: ProcessSetNPCStayAtHome tenant {:08X} stayAtHome = {}", npcFormID, stayAtHome);

        SendNPCDetail(npcFormID);
    }

    void PrismaUIDataBridge::ProcessDumpData(const nlohmann::json& /*payload*/) {
        try {
            nlohmann::json root;

            // 1. Meta
            nlohmann::json meta;
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &in_time_t);

            std::ostringstream ssTime, ssFile;
            ssTime << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            ssFile << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            std::string timeStr = ssTime.str();
            std::string fileTimeStr = ssFile.str();

            const bool revealSecrets = ShouldRevealSecretsInUI();
            meta["dumpTimestamp"] = timeStr;
            meta["serializationVersion"] = MARAS::Serialization::kDataVersion;
            meta["hasSkyrimNet"] = IsSkyrimNetAvailable();
            meta["simulateNoSkyrimNet"] = Settings::GetSingleton().GetSimulateNoSkyrimNet();
            meta["secretsMasked"] = !revealSecrets;
            if (auto calendar = RE::Calendar::GetSingleton()) {
                meta["gameDaysPassed"] = calendar->GetDaysPassed();
                meta["gameYear"] = calendar->GetYear();
                meta["gameMonth"] = calendar->GetMonth();
                meta["gameDay"] = calendar->GetDay();
                meta["gameHour"] = calendar->GetHour();
            }
            root["meta"] = meta;

            // 2. Settings
            root["settings"] = Settings::GetSingleton().DumpJson();

            // 3. Spouse Hierarchy
            root["spouseHierarchy"] = SpouseHierarchyManager::GetSingleton().DumpJson();

            // 4. NPC Relationship Manager (Profiles, Pairs, Children Registry, Pending)
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            nlohmann::json rmDump = relMgr.DumpJson();
            if (!revealSecrets) {
                SanitizeRelationshipManagerDumpForUI(rmDump, relMgr);
            }
            root["relationshipManager"] = std::move(rmDump);

            // 5. Affection Service
            root["affectionService"] = AffectionService::GetSingleton().DumpJson();

            // 6. Player House Service
            root["playerHouses"] = PlayerHouseService::GetSingleton().DumpJson();

            // 7. Spouse Assets Service
            root["spouseAssets"] = SpouseAssetsService::GetSingleton().DumpJson();

            // 8. Player Children Service
            root["playerChildrenService"] = PlayerChildrenService::GetSingleton().DumpJson();

            // 9. Matchmaking Service
            nlohmann::json mmDump = MatchmakingService::GetSingleton().DumpJson();
            if (!revealSecrets && mmDump.is_object() && mmDump.contains("lastPlayerPartnerCheatingDay")) {
                mmDump["lastPlayerPartnerCheatingDay"] = -1.0f;
            }
            root["matchmakingService"] = std::move(mmDump);

            // 10. Deceased Registry
            root["deceasedRegistry"] = DeceasedRegistry::GetSingleton().DumpJson();

            // 11. Banter Coordinator Service
            root["banterCoordinator"] = BanterCoordinatorService::GetSingleton().DumpJson();

            // 12. Adult Child Travel Service
            root["adultChildTravel"] = AdultChildTravelService::GetSingleton().DumpJson();

            std::filesystem::path dumpsDir = "Data/SKSE/Plugins/MARAS/dumps";
            std::error_code ec;
            std::filesystem::create_directories(dumpsDir, ec);

            std::string fileName = "maras_dump_" + fileTimeStr + ".json";
            std::filesystem::path fullPath = dumpsDir / fileName;
            std::ofstream outFile(fullPath);
            if (outFile.is_open()) {
                outFile << root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
                outFile.close();

                std::string absolutePathStr = std::filesystem::absolute(fullPath).string();
                MARAS_LOG_INFO("Successfully dumped MARAS stored data to '{}'", absolutePathStr);
                if (auto console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Stored data successfully dumped to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = true;
                res["fileName"] = fileName;
                res["filePath"] = fullPath.string();
                res["absolutePath"] = absolutePathStr;
                res["timestamp"] = timeStr;
                std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                PrismaUIManager::GetSingleton().InteropCall("onDataDumpCompleted", jsonStr.c_str());
            } else {
                MARAS_LOG_ERROR("Failed to create MARAS dump file at '{}'", fullPath.string());
                if (auto console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Failed to dump data to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = false;
                res["error"] = "Failed to create dump file at " + fullPath.string();
                std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                PrismaUIManager::GetSingleton().InteropCall("onDataDumpCompleted", jsonStr.c_str());
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("PrismaUIDataBridge: Exception while dumping data: {}", e.what());
            nlohmann::json res;
            res["success"] = false;
            res["error"] = std::string("Exception: ") + e.what();
            std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            PrismaUIManager::GetSingleton().InteropCall("onDataDumpCompleted", jsonStr.c_str());
        }
    }

    void PrismaUIDataBridge::ProcessDumpNPCIndex(const nlohmann::json& /*payload*/) {
        try {
            auto root = NPCIndex::GetSingleton().DumpIndexJson();
            const nlohmann::json metaObj = (root.is_object() && root.contains("meta") && root["meta"].is_object())
                ? root["meta"]
                : nlohmann::json::object();
            size_t totalNPCs = static_cast<size_t>(GetJsonInt(metaObj, "totalNPCs", 0));
            std::string timeStr = GetJsonString(metaObj, "dumpTimestamp");

            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &in_time_t);
            std::ostringstream ssFile;
            ssFile << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            std::string fileTimeStr = ssFile.str();

            std::filesystem::path dumpsDir = "Data/SKSE/Plugins/MARAS/dumps";
            std::error_code ec;
            std::filesystem::create_directories(dumpsDir, ec);

            std::string fileName = "maras_npc_index_" + fileTimeStr + ".json";
            std::filesystem::path fullPath = dumpsDir / fileName;
            std::ofstream outFile(fullPath);
            if (outFile.is_open()) {
                outFile << root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
                outFile.close();

                std::string absolutePathStr = std::filesystem::absolute(fullPath).string();
                MARAS_LOG_INFO("Successfully dumped NPC Index results ({} NPCs) to '{}'", totalNPCs, absolutePathStr);
                if (auto console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] NPC Index successfully dumped (%zu NPCs) to %s", totalNPCs, fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = true;
                res["fileName"] = fileName;
                res["filePath"] = fullPath.string();
                res["absolutePath"] = absolutePathStr;
                res["totalNPCs"] = totalNPCs;
                res["timestamp"] = timeStr;
                std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                PrismaUIManager::GetSingleton().InteropCall("onNPCIndexDumpCompleted", jsonStr.c_str());
            } else {
                MARAS_LOG_ERROR("Failed to create NPC Index dump file at '{}'", fullPath.string());
                if (auto console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Failed to dump NPC Index to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = false;
                res["error"] = "Failed to create dump file at " + fullPath.string();
                std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                PrismaUIManager::GetSingleton().InteropCall("onNPCIndexDumpCompleted", jsonStr.c_str());
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("PrismaUIDataBridge: Exception while dumping NPC Index: {}", e.what());
            nlohmann::json res;
            res["success"] = false;
            res["error"] = std::string("Exception: ") + e.what();
            std::string jsonStr = res.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            PrismaUIManager::GetSingleton().InteropCall("onNPCIndexDumpCompleted", jsonStr.c_str());
        }
    }

}  // namespace MARAS
