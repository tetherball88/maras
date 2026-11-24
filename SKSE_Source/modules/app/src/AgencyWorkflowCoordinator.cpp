#include "maras/app/AgencyWorkflowCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/app/AsyncLlmBatchRunner.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/MatchmakingContextBuilder.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/agency/BanterPlanner.h"
#include "maras/domain/agency/EncounterTemplate.h"
#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/domain/agency/MatchmakingValidationRules.h"
#include "maras/domain/agency/NpcMatchmaker.h"
#include "maras/domain/agency/PartnerAgencyEvaluator.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BanterSceneStarter.h"
#include "maras/engine/EncounterConfigLoader.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/JsonService.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/SkseSerializationCallbacks.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace MARAS::App {

    namespace {

        constexpr float kBanterCoupleCooldownMultiplier = 2.0f;

        [[nodiscard]] float GetCurrentGameDaysSafe() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

        [[nodiscard]] int GetCurrentHourOfDaySafe() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return static_cast<int>(std::floor(calendar->GetHour()));
            }
            return 12;
        }

        [[nodiscard]] std::string FormatFormId8Hex(RE::FormID id) {
            return std::format("{:08X}", id);
        }

        [[nodiscard]] std::string GetPlayerNameSafe() {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetName(); name && name[0] != '\0') {
                    return name;
                }
            }
            return "Dragonborn";
        }

        [[nodiscard]] std::string GetJsonStringSafe(const nlohmann::json& obj,
                                                    std::string_view key,
                                                    std::string_view defaultVal = "") {
            if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
                return obj[key].get<std::string>();
            }
            return std::string(defaultVal);
        }

        [[nodiscard]] bool GetJsonBoolSafe(const nlohmann::json& obj,
                                           std::string_view key,
                                           bool defaultVal = false) {
            if (obj.is_object() && obj.contains(key) && obj[key].is_boolean()) {
                return obj[key].get<bool>();
            }
            return defaultVal;
        }

        [[nodiscard]] std::string NormalizeActionString(std::string_view raw,
                                                        std::string_view targetStatusHint = "",
                                                        std::optional<bool> openTargetHint = std::nullopt) {
            return Domain::Agency::NormalizeMatchmakingAction(raw, targetStatusHint, openTargetHint);
        }

        void ResolveResultFormIDs(MatchmakingResult& result) {
            auto& index = SpatialWorldIndex::GetSingleton();
            if (result.actorAFormID == 0 && !result.actorAName.empty()) {
                if (const auto entry = index.FindByName(result.actorAName, NPCFilter::AdultOnly)) {
                    result.actorAFormID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
                    if (!entry->nameDisplay.empty()) {
                        result.actorAName = entry->nameDisplay;
                    }
                }
            }
            if (result.actorBFormID == 0 && !result.actorBName.empty()) {
                if (const auto entry = index.FindByName(result.actorBName, NPCFilter::AdultOnly)) {
                    result.actorBFormID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
                    if (!entry->nameDisplay.empty()) {
                        result.actorBName = entry->nameDisplay;
                    }
                }
            }
        }

        [[nodiscard]] std::vector<MatchmakingResult> ParseMatchmakingJsonArray(const nlohmann::json& matchesArray) {
            std::vector<MatchmakingResult> out;
            if (!matchesArray.is_array()) {
                return out;
            }
            out.reserve(matchesArray.size());
            for (const auto& item : matchesArray) {
                if (!item.is_object()) {
                    continue;
                }
                MatchmakingResult r{};
                r.actorAName = GetJsonStringSafe(item, "actorA");
                if (r.actorAName.empty()) r.actorAName = GetJsonStringSafe(item, "actor_a");
                if (r.actorAName.empty()) r.actorAName = GetJsonStringSafe(item, "target");

                r.actorBName = GetJsonStringSafe(item, "actorB");
                if (r.actorBName.empty()) r.actorBName = GetJsonStringSafe(item, "actor_b");
                if (r.actorBName.empty()) r.actorBName = GetJsonStringSafe(item, "partner");
                if (r.actorBName.empty()) r.actorBName = GetJsonStringSafe(item, "candidate");
                if (r.actorBName.empty()) r.actorBName = GetJsonStringSafe(item, "targetMatch");

                const std::string rawTarget = Foundation::StringUtils::ToLowerStd(
                    Foundation::StringUtils::TrimView(GetJsonStringSafe(item, "targetStatus", "")));
                std::optional<bool> openTarget;
                if (item.contains("openTarget") && item["openTarget"].is_boolean()) {
                    openTarget = item["openTarget"].get<bool>();
                }
                r.action = NormalizeActionString(GetJsonStringSafe(item, "action", "new"), rawTarget, openTarget);
                r.targetStatus = rawTarget;
                if (r.targetStatus.empty()) {
                    if (r.action == "open") {
                        r.targetStatus = "open";
                    } else if (r.action == "close") {
                        r.targetStatus = "closed";
                    } else if (r.action == "new") {
                        r.targetStatus = "dating";
                    }
                }
                r.memory = GetJsonStringSafe(item, "memory");
                r.isCheating = GetJsonBoolSafe(item, "cheating", GetJsonBoolSafe(item, "isCheating", false));

                if (!r.actorAName.empty() && !r.actorBName.empty()) {
                    out.push_back(std::move(r));
                }
            }
            return out;
        }

        [[nodiscard]] ModalTone ParseModalTone(std::string_view tone) noexcept {
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "warning")) return ModalTone::Warning;
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "danger")) return ModalTone::Danger;
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "success")) return ModalTone::Success;
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "ai")) return ModalTone::Ai;
            return ModalTone::Default;
        }

        [[nodiscard]] ModalSize ParseModalSize(std::string_view size) noexcept {
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "sm")) return ModalSize::Sm;
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "lg")) return ModalSize::Lg;
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "xl")) return ModalSize::Xl;
            return ModalSize::Md;
        }

        [[nodiscard]] std::string FormatStatusDisplayLabel(Foundation::RelationshipStatus status) {
            switch (status) {
                case Foundation::RelationshipStatus::Dating: return "Dating";
                case Foundation::RelationshipStatus::Lover: return "Lover";
                case Foundation::RelationshipStatus::Engaged: return "Engaged";
                case Foundation::RelationshipStatus::Married: return "Married";
                case Foundation::RelationshipStatus::ExDating: return "Ex-Dating";
                case Foundation::RelationshipStatus::ExLover: return "Ex-Lover";
                case Foundation::RelationshipStatus::Jilted: return "Jilted";
                case Foundation::RelationshipStatus::Divorced: return "Divorced";
                case Foundation::RelationshipStatus::Candidate:
                case Foundation::RelationshipStatus::Unknown:
                default:
                    return "Single / Unrelated";
            }
        }

        [[nodiscard]] std::string FormatTargetStatusDisplayLabel(std::string_view targetStatus) {
            const std::string lower = Foundation::StringUtils::ToLowerStd(targetStatus);
            if (lower == "dating") return "Dating";
            if (lower == "lover") return "Lover";
            if (lower == "engaged") return "Engaged";
            if (lower == "married") return "Married";
            if (lower == "exdating") return "Ex-Dating";
            if (lower == "exlover") return "Ex-Lover";
            if (lower == "jilted") return "Jilted";
            if (lower == "divorced") return "Divorced";
            if (lower == "open") return "Open Relationship";
            if (lower == "closed") return "Exclusive / Closed";
            return std::string(targetStatus);
        }

        [[nodiscard]] std::string ResolveHouseDisplayName(RE::FormID houseId) {
            if (houseId == 0) {
                return "Home";
            }
            auto name = HouseholdEngineOps::GetHouseName(houseId);
            if (!name.empty()) {
                return name;
            }
            if (auto* form = RE::TESForm::LookupByID(houseId)) {
                if (const char* n = form->GetName(); n && n[0] != '\0') {
                    return n;
                }
            }
            return "Home";
        }

        [[nodiscard]] std::string ResolveHomeArrivalScenarioId(const Domain::Agency::HomeArrivalPlan& plan,
                                                               bool isLetterScenario) {
            using Domain::Agency::HomeArrivalKind;
            using Foundation::PartnerIntentType;
            switch (plan.kind) {
                case HomeArrivalKind::PendingIntentForcegreet:
                    switch (plan.intentType) {
                        case PartnerIntentType::SuggestOpen: return "intent_suggest_open";
                        case PartnerIntentType::SuggestClose: return "intent_suggest_close";
                        case PartnerIntentType::ConfessTalking: return "intent_confess_talking";
                        case PartnerIntentType::ConfessPhysical: return "intent_confess_physical";
                        default: return {};
                    }
                case HomeArrivalKind::OpenDateInHouse:
                case HomeArrivalKind::OpenDateLetter:
                    return isLetterScenario ? "open_date_away" : "open_in_house";
                case HomeArrivalKind::CheatingTalkingInHouse:
                case HomeArrivalKind::CheatingTalkingLetter:
                    return isLetterScenario ? "cheating_talking_letter" : "cheating_talking_in_house";
                case HomeArrivalKind::CheatingPhysicalInHouse:
                case HomeArrivalKind::CheatingPhysicalLetter:
                    return isLetterScenario ? "cheating_physical_letter" : "cheating_physical_in_house";
                default:
                    return {};
            }
        }

        [[nodiscard]] Domain::Agency::RenderedEncounterModal SynthesizeMinimalEncounterFallback(
            std::string_view templateId,
            std::string_view partnerName,
            std::string_view playerName,
            std::string_view locationName) {
            Domain::Agency::RenderedEncounterModal modal{};
            modal.modalTone = (templateId == "open_date_away") ? "ai" : "danger";
            modal.modalSize = "md";
            modal.defaultCancelButtonIndex = 0;
            modal.modalTitle = TrFmt("modal.encounter_fallback.title", { { "partner", partnerName } });
            modal.modalSubtitle = std::string(locationName);
            modal.envelopeMessage = TrFmt("modal.encounter_fallback.envelope",
                                          { { "player", playerName }, { "location", locationName } });
            modal.infoBoxId = "partner_note";
            modal.infoBoxTitle = Tr("modal.encounter_fallback.infobox_title");
            modal.letterBody = TrFmt("modal.encounter_fallback.body",
                                     { { "partner", partnerName }, { "location", locationName } });
            modal.buttonId = "close";
            modal.buttonLabel = Tr("modal.encounter_fallback.btn_close");
            return modal;
        }

        [[nodiscard]] std::size_t CountSharedChildrenWithPlayer(const State::WorldStateSnapshot& snap,
                                                                Foundation::ActorId partnerId) noexcept {
            if (!snap.lineage) {
                return 0;
            }
            std::size_t count = 0;
            for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                (void)cid;
                if (rec.stage == Foundation::ChildStage::Unborn) continue;
                const bool hasPlayer =
                    rec.guardians.HasGuardian(Foundation::ActorId::Player()) ||
                    rec.bioMother.IsPlayer() ||
                    rec.bioFather.IsPlayer();
                const bool hasPartner =
                    rec.guardians.HasGuardian(partnerId) ||
                    rec.bioMother == partnerId ||
                    rec.bioFather == partnerId;
                if (hasPlayer && hasPartner) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] float GetLatestNonBreakupStartDaySafe(const State::WorldStateSnapshot& snap,
                                                            Foundation::ActorId a,
                                                            Foundation::ActorId b) noexcept {
            if (!snap.relationships) return -1.0f;
            const auto* edge = snap.relationships->FindEdge(a, b);
            if (!edge) return -1.0f;
            for (auto it = edge->statusHistory.rbegin(); it != edge->statusHistory.rend(); ++it) {
                if (!Foundation::IsBreakupStatus(it->toStatus) && it->timestamp >= 0.0f) {
                    return it->timestamp;
                }
            }
            return -1.0f;
        }

        struct PartnerAgencyBatchItem {
            Domain::Agency::PartnerAgencyActionPlan plan{};
            PartnerAgencyContextInput ctxInput{};
            nlohmann::json contextJson{};
            PartnerAgencyEvalResult result{};
        };

        [[nodiscard]] bool IsSceneSettingEnabledForPair(const State::SettingsData& s,
                                                        const State::WorldStateSnapshot& snap,
                                                        RE::FormID idA,
                                                        RE::FormID idB) {
            if (!snap.relationships || !snap.affairs) {
                return false;
            }
            const Foundation::ActorId a{ idA };
            const Foundation::ActorId b{ idB };
            const auto status = snap.relationships->GetStatus(a, b);
            const bool isOpen = snap.relationships->IsPairOpen(a, b);
            // C5: Check cheater<->paramour cheating status directly, NOT victim HasCheatedOn(a, b).
            const bool isCheating = Domain::Relationships::IsRelationshipCheating(snap, a, b);

            if (isCheating) {
                return s.enableScenes_Cheating || s.enableScenes_CheatingDiscovered || s.enableScenes_CheatingCaught;
            }
            if (isOpen && s.enableScenes_OpenRelationship) {
                return true;
            }
            switch (status) {
                case Foundation::RelationshipStatus::Lover: return s.enableScenes_Lover;
                case Foundation::RelationshipStatus::Dating: return s.enableScenes_Dating;
                case Foundation::RelationshipStatus::Engaged: return s.enableScenes_Engaged;
                case Foundation::RelationshipStatus::Married: return s.enableScenes_Married;
                default: return false;
            }
        }

        [[nodiscard]] bool HasAnyInteriorInnInWorld() {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            auto* kwInn = FormCache::GetSingleton().GetLocTypeInnKeyword();
            if (!dataHandler || !kwInn) {
                return false;
            }
            for (auto* cell : dataHandler->interiorCells) {
                if (cell && cell->GetLocation() && cell->GetLocation()->HasKeyword(kwInn)) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    AgencyWorkflowCoordinator& AgencyWorkflowCoordinator::GetSingleton() {
        static AgencyWorkflowCoordinator instance;
        return instance;
    }

    bool AgencyWorkflowCoordinator::IsMatchmakingRunning() const noexcept {
        return runningMatchmaking_.load(std::memory_order_acquire);
    }

    bool AgencyWorkflowCoordinator::IsPartnerAgencyRunning() const noexcept {
        return runningPartnerAgency_.load(std::memory_order_acquire);
    }

    void AgencyWorkflowCoordinator::ClearMatchmakingRunning() noexcept {
        runningMatchmaking_.store(false, std::memory_order_release);
    }

    void AgencyWorkflowCoordinator::ClearPartnerAgencyRunning() noexcept {
        runningPartnerAgency_.store(false, std::memory_order_release);
    }

    void AgencyWorkflowCoordinator::ClearRunningBatchState() noexcept {
        ClearMatchmakingRunning();
        ClearPartnerAgencyRunning();
    }

    // =========================================================================
    // Cooldown & Persistent Day Accessors
    // =========================================================================

    void AgencyWorkflowCoordinator::RecordNpcCooldown(RE::FormID npcFormID, float currentGameDay) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return;
        }
        const RE::FormID ref = SpatialWorldIndex::GetSingleton().GetRefID(npcFormID);
        const RE::FormID canonicalId = (ref != 0) ? ref : npcFormID;
        State::WorldStore::GetSingleton().Modify([canonicalId, currentGameDay](State::WorldState& ws) {
            ws.cooldowns.Write().npcLastMatchmakingChangeDay[Foundation::ActorId{ canonicalId }] = currentGameDay;
        });
    }

    bool AgencyWorkflowCoordinator::IsNpcOnCooldown(RE::FormID npcFormID, float currentGameDay) const {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const RE::FormID ref = SpatialWorldIndex::GetSingleton().GetRefID(npcFormID);
        const RE::FormID canonicalId = (ref != 0) ? ref : npcFormID;
        if (Domain::Agency::IsNpcOnMatchmakingCooldown(*snap, Foundation::ActorId{ canonicalId }, currentGameDay)) {
            return true;
        }
        return canonicalId != npcFormID &&
               Domain::Agency::IsNpcOnMatchmakingCooldown(*snap, Foundation::ActorId{ npcFormID }, currentGameDay);
    }

    int AgencyWorkflowCoordinator::GetNpcCooldownRemaining(RE::FormID npcFormID, float currentGameDay) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->cooldowns || snap->settings->matchmakingNpcCooldownDays <= 0) {
            return 0;
        }
        const RE::FormID ref = SpatialWorldIndex::GetSingleton().GetRefID(npcFormID);
        const RE::FormID canonicalId = (ref != 0) ? ref : npcFormID;
        auto it = snap->cooldowns->npcLastMatchmakingChangeDay.find(Foundation::ActorId{ canonicalId });
        if (it == snap->cooldowns->npcLastMatchmakingChangeDay.end() && canonicalId != npcFormID) {
            it = snap->cooldowns->npcLastMatchmakingChangeDay.find(Foundation::ActorId{ npcFormID });
        }
        if (it == snap->cooldowns->npcLastMatchmakingChangeDay.end()) {
            return 0;
        }
        const float elapsed = currentGameDay - it->second;
        if (elapsed < 0.0f) {
            return 0;
        }
        const int remaining = static_cast<int>(
            std::ceil(static_cast<float>(snap->settings->matchmakingNpcCooldownDays) - elapsed));
        return std::max(0, remaining);
    }

    void AgencyWorkflowCoordinator::ClearNpcCooldowns() {
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.cooldowns.Write().npcLastMatchmakingChangeDay.clear();
        });
    }

    float AgencyWorkflowCoordinator::GetLastMatchmakingDay() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->cooldowns ? snap->cooldowns->lastMatchmakingDay : -1.0f;
    }

    void AgencyWorkflowCoordinator::SetLastMatchmakingDay(float day) {
        State::WorldStore::GetSingleton().Modify([day](State::WorldState& ws) {
            ws.cooldowns.Write().lastMatchmakingDay = day;
        });
    }

    float AgencyWorkflowCoordinator::GetLastPlayerPartnerCheatingDay() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->cooldowns ? snap->cooldowns->lastPlayerPartnerCheatingDay : -1.0f;
    }

    void AgencyWorkflowCoordinator::SetLastPlayerPartnerCheatingDay(float day) {
        State::WorldStore::GetSingleton().Modify([day](State::WorldState& ws) {
            ws.cooldowns.Write().lastPlayerPartnerCheatingDay = day;
        });
    }

    // =========================================================================
    // Matchmaking Workflows
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledMatchmaking(float currentGameDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->matchmakingEnabled) {
            return;
        }
        if (IsMatchmakingRunning()) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] TryScheduledMatchmaking skipped: batch already in progress");
            return;
        }

        const float lastDay = GetLastMatchmakingDay();
        const int intervalDays = std::max(1, snap->settings->matchmakingIntervalDays);
        if (lastDay >= 0.0f && currentGameDay >= lastDay &&
            (currentGameDay - lastDay) < static_cast<float>(intervalDays)) {
            return;
        }

        // Caller-level SkyrimNet runtime gate
        if (GetSkyrimNetGateway().IsAvailable() && snap->settings->matchmakingUseLLM) {
            RunLLMScheduledMatchmaking(currentGameDay);
        } else {
            RunDiceRollMatchmaking(currentGameDay);
        }
    }

    void AgencyWorkflowCoordinator::RunLLMScheduledMatchmaking(float currentGameDay) {
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            ClearMatchmakingRunning();
            return;
        }

        nlohmann::json ctx = BuildScheduledMatchmakingContextJson(*snap, currentGameDay);
        const std::size_t npcsCount = ctx.contains("npcsCount") && ctx["npcsCount"].is_number_unsigned()
                                          ? ctx["npcsCount"].get<std::size_t>()
                                          : 0;
        if (npcsCount < 2) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] RunLLMScheduledMatchmaking: fewer than 2 eligible NPCs ({})",
                            npcsCount);
            SetLastMatchmakingDay(currentGameDay);
            ClearMatchmakingRunning();
            return;
        }

        struct ScheduledPayload {
            nlohmann::json contextJson;
            nlohmann::json parsedResponse;
        };

        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = false,
            .isPartnerAgency = false,
            .skipConfirmationModal = false
        };

        LlmBatchCallbacks<ScheduledPayload> callbacks;
        callbacks.submitTask = [](ScheduledPayload& payload, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations/maras_matchmaking_scheduled",
                "gamemaster_evaluation",
                payload.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, "LLM request failed for scheduled matchmaking", nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, "Invalid JSON matches payload from scheduled matchmaking", nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](ScheduledPayload& p) {
                        p.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const ScheduledPayload&, const std::string& reason) {
            return std::format("Scheduled Matchmaking: {}", reason);
        };
        callbacks.onAllResolved = [this, opts](const std::shared_ptr<LlmBatchState<ScheduledPayload>>& state) {
            SetLastMatchmakingDay(opts.currentGameDay);

            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            auto& relUseCases = RelationshipUseCases::GetSingleton();
            std::set<RE::FormID> seenActorsInBatch;
            std::vector<MatchmakingResult> candidates;

            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                for (auto& cand : ParseMatchmakingJsonArray(item.payload.parsedResponse["matches"])) {
                    ResolveResultFormIDs(cand);
                    if (cand.actorAFormID == 0 || cand.actorBFormID == 0) continue;
                    if (seenActorsInBatch.contains(cand.actorAFormID) || seenActorsInBatch.contains(cand.actorBFormID)) {
                        continue;
                    }
                    if (relUseCases.IsPlayerActiveMainPartner(cand.actorAFormID) ||
                        relUseCases.IsPlayerActiveMainPartner(cand.actorBFormID)) {
                        continue;
                    }
                    cand.isCheating = IsPairCheatingTransition(
                        *snapNow, cand.actorAFormID, cand.actorBFormID, cand.targetStatus);
                    if (ValidateCandidate(cand, *snapNow, opts.currentGameDay, /*checkCooldown=*/true)) {
                        seenActorsInBatch.insert(cand.actorAFormID);
                        seenActorsInBatch.insert(cand.actorBFormID);
                        candidates.push_back(std::move(cand));
                    }
                }
            }

            if (candidates.empty()) {
                MARAS_LOG_INFO("[AgencyWorkflowCoordinator] LLM scheduled matchmaking returned 0 valid proposals");
                ClearMatchmakingRunning();
                return;
            }
            StartVerificationBatch(std::move(candidates), opts);
        };

        RunLlmBatch<ScheduledPayload>(
            Tr("modal.llm_retry.batch_scheduled_matchmaking"),
            "gamemaster_evaluation",
            { ScheduledPayload{ std::move(ctx), {} } },
            std::move(callbacks),
            GetSessionEpoch());
    }

    void AgencyWorkflowCoordinator::RunDiceRollMatchmaking(float currentGameDay) {
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings) {
            ClearMatchmakingRunning();
            return;
        }

        const auto eligibleNpcs = CollectWorldAdultMatchmakingProfiles(*snap, currentGameDay, true);
        SetLastMatchmakingDay(currentGameDay);

        if (eligibleNpcs.size() < 2) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] RunDiceRollMatchmaking: fewer than 2 eligible NPCs ({})",
                            eligibleNpcs.size());
            ClearMatchmakingRunning();
            return;
        }

        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));
        const auto proposals = Domain::Agency::GenerateDiceRollProposals(*snap, eligibleNpcs, currentGameDay, rng);

        std::unordered_set<RE::FormID> seenActors;
        std::vector<MatchmakingResult> candidates;
        candidates.reserve(proposals.size());
        for (const auto& p : proposals) {
            if (seenActors.contains(p.actorA.raw()) || seenActors.contains(p.actorB.raw())) {
                continue;
            }
            MatchmakingResult res{};
            res.actorAFormID = p.actorA.raw();
            res.actorBFormID = p.actorB.raw();
            res.actorAName = ResolveActorNameForAgency(p.actorA.raw());
            res.actorBName = ResolveActorNameForAgency(p.actorB.raw());
            if (p.slot.action == Domain::Agency::MatchmakingActionKind::OpenClose) {
                res.action = p.slot.openTarget ? "open" : "close";
                res.targetStatus = p.slot.openTarget ? "open" : "closed";
            } else {
                switch (p.slot.action) {
                    case Domain::Agency::MatchmakingActionKind::New: res.action = "new"; break;
                    case Domain::Agency::MatchmakingActionKind::Advance: res.action = "advance"; break;
                    case Domain::Agency::MatchmakingActionKind::Breakup: res.action = "breakup"; break;
                    default: res.action = "new"; break;
                }
                res.targetStatus = Foundation::StringUtils::ToLowerStd(
                    Foundation::RelationshipStatusToString(p.slot.targetStatus));
            }
            res.isCheating = p.slot.isCheating;
            if (ValidateCandidate(res, *snap, currentGameDay, /*checkCooldown=*/true)) {
                seenActors.insert(res.actorAFormID);
                seenActors.insert(res.actorBFormID);
                candidates.push_back(std::move(res));
            }
        }

        if (candidates.empty()) {
            MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Dice-roll scheduled matchmaking generated 0 valid matches");
            ClearMatchmakingRunning();
            return;
        }

        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = false,
            .isPartnerAgency = false,
            .skipConfirmationModal = false
        };
        if (GetSkyrimNetGateway().IsAvailable() && snap->settings->matchmakingEvaluationStep) {
            StartVerificationBatch(std::move(candidates), opts);
        } else {
            ProceedAfterVerification(std::move(candidates), opts);
        }
    }

    bool AgencyWorkflowCoordinator::RunManualMatchmaking(const std::vector<RE::FormID>& npcFormIDs) {
        if (npcFormIDs.empty()) {
            GetUiBridgeGateway().SendMatchmakingResults({}, false);
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunManualMatchmaking requires active SkyrimNet availability");
            GetUiBridgeGateway().SendMatchmakingResults({}, false);
            return false;
        }
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunManualMatchmaking skipped: batch already active");
            GetUiBridgeGateway().SendMatchmakingResults({}, false);
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();

        struct ManualTask {
            RE::FormID targetId{ 0 };
            std::string targetName{};
            nlohmann::json contextJson{};
            nlohmann::json parsedResponse{};
        };

        std::vector<ManualTask> payloads;
        payloads.reserve(npcFormIDs.size());

        for (const RE::FormID targetId : npcFormIDs) {
            if (!IsNpcEligibleWorldAdult(targetId, *snap)) {
                continue;
            }
            payloads.push_back(ManualTask{
                .targetId = targetId,
                .targetName = ResolveActorNameForAgency(targetId),
                .contextJson = BuildManualMatchmakingContextJson(*snap, targetId, currentGameDay),
                .parsedResponse = {}
            });
        }

        if (payloads.empty()) {
            ClearMatchmakingRunning();
            GetUiBridgeGateway().SendMatchmakingResults({}, false);
            return false;
        }

        LlmBatchCallbacks<ManualTask> callbacks;
        callbacks.submitTask = [](ManualTask& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations/maras_matchmaking_manual",
                "gamemaster_evaluation",
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [targetName = task.targetName, reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, std::format("Request failed for {}", targetName), nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, std::format("Invalid JSON response for {}", targetName), nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](ManualTask& t) {
                        t.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const ManualTask& t, const std::string& reason) {
            return std::format("{} ({:08X}): {}", t.targetName, t.targetId, reason);
        };
        callbacks.onAllResolved = [this, currentGameDay](const std::shared_ptr<LlmBatchState<ManualTask>>& state) {
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            std::vector<MatchmakingResult> dedupedResults;

            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                const RE::FormID targetId = item.payload.targetId;
                const std::string targetName = item.payload.targetName;
                std::set<RE::FormID> seenCandidateFormIDs;

                for (auto& r : ParseMatchmakingJsonArray(item.payload.parsedResponse["matches"])) {
                    ResolveResultFormIDs(r);

                    bool isA = (r.actorAFormID == targetId) ||
                               (r.actorAFormID == 0 && !targetName.empty() &&
                                Foundation::StringUtils::CaseInsensitiveEquals(r.actorAName, targetName));
                    const bool isB = (r.actorBFormID == targetId) ||
                                     (r.actorBFormID == 0 && !targetName.empty() &&
                                      Foundation::StringUtils::CaseInsensitiveEquals(r.actorBName, targetName));

                    if (isB && !isA) {
                        std::swap(r.actorAFormID, r.actorBFormID);
                        std::swap(r.actorAName, r.actorBName);
                        isA = true;
                    }
                    if (!isA) {
                        continue;
                    }

                    r.actorAFormID = targetId;
                    if (!targetName.empty() && targetName != "None") {
                        r.actorAName = targetName;
                    }
                    if (r.actorBFormID == 0 || r.actorBFormID == targetId) {
                        continue;
                    }
                    if (seenCandidateFormIDs.contains(r.actorBFormID)) {
                        continue;
                    }

                    // Manual preview: validate WITHOUT scheduled-cooldown rejection and do NOT auto-apply
                    if (!ValidateCandidate(r, *snapNow, currentGameDay, /*checkCooldown=*/false)) {
                        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Manual preview skipped {} <-> {}: {}",
                                       r.actorAName, r.actorBName, r.skipReason);
                        continue;
                    }

                    seenCandidateFormIDs.insert(r.actorBFormID);
                    r.isCheating = IsPairCheatingTransition(*snapNow, r.actorAFormID, r.actorBFormID, r.targetStatus);
                    dedupedResults.push_back(std::move(r));
                }
            }

            GetUiBridgeGateway().SendMatchmakingResults(dedupedResults, /*isGlobal=*/false);
            ClearMatchmakingRunning();
        };

        RunLlmBatch<ManualTask>(
            Tr("modal.llm_retry.batch_manual_matchmaking"),
            "gamemaster_evaluation",
            std::move(payloads),
            std::move(callbacks),
            GetSessionEpoch());
        return true;
    }

    bool AgencyWorkflowCoordinator::RunGlobalMatchmaking(bool autoApply,
                                                         std::optional<std::size_t> maxResultsOverride) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunGlobalMatchmaking requires active SkyrimNet availability");
            if (!autoApply) {
                GetUiBridgeGateway().SendMatchmakingResults({}, true);
            }
            return false;
        }
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunGlobalMatchmaking skipped: batch already active");
            if (!autoApply) {
                GetUiBridgeGateway().SendMatchmakingResults({}, true);
            }
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();
        nlohmann::json ctx = BuildScheduledMatchmakingContextJson(*snap, currentGameDay, maxResultsOverride);
        const std::size_t npcsCount = ctx.contains("npcsCount") && ctx["npcsCount"].is_number_unsigned()
                                          ? ctx["npcsCount"].get<std::size_t>()
                                          : 0;
        if (npcsCount < 2) {
            ClearMatchmakingRunning();
            if (!autoApply) {
                GetUiBridgeGateway().SendMatchmakingResults({}, true);
            }
            return false;
        }

        struct GlobalTask {
            nlohmann::json contextJson;
            nlohmann::json parsedResponse;
        };

        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = !autoApply,
            .isPartnerAgency = false,
            .skipConfirmationModal = false
        };

        LlmBatchCallbacks<GlobalTask> callbacks;
        callbacks.submitTask = [](GlobalTask& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations/maras_matchmaking_scheduled",
                "gamemaster_evaluation",
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, "LLM request failed for global matchmaking", nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, "Invalid JSON matches payload for global matchmaking", nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](GlobalTask& t) {
                        t.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const GlobalTask&, const std::string& reason) {
            return std::format("Global Matchmaking: {}", reason);
        };
        callbacks.onAllResolved = [this, opts, autoApply](const std::shared_ptr<LlmBatchState<GlobalTask>>& state) {
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            auto& relUseCases = RelationshipUseCases::GetSingleton();
            std::set<RE::FormID> seenActorsInBatch;
            std::vector<MatchmakingResult> validResults;

            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                for (auto& r : ParseMatchmakingJsonArray(item.payload.parsedResponse["matches"])) {
                    ResolveResultFormIDs(r);
                    if (r.actorAFormID == 0 || r.actorBFormID == 0) continue;
                    if (seenActorsInBatch.contains(r.actorAFormID) || seenActorsInBatch.contains(r.actorBFormID)) {
                        continue;
                    }
                    if (autoApply &&
                        (relUseCases.IsPlayerActiveMainPartner(r.actorAFormID) ||
                         relUseCases.IsPlayerActiveMainPartner(r.actorBFormID))) {
                        continue;
                    }
                    r.isCheating = IsPairCheatingTransition(*snapNow, r.actorAFormID, r.actorBFormID, r.targetStatus);
                    if (!ValidateCandidate(r, *snapNow, opts.currentGameDay, /*checkCooldown=*/autoApply)) {
                        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Global matchmaking skipped {} <-> {}: {}",
                                       r.actorAName, r.actorBName, r.skipReason);
                        continue;
                    }
                    seenActorsInBatch.insert(r.actorAFormID);
                    seenActorsInBatch.insert(r.actorBFormID);
                    validResults.push_back(std::move(r));
                }
            }

            if (!autoApply) {
                GetUiBridgeGateway().SendMatchmakingResults(validResults, /*isGlobal=*/true);
                ClearMatchmakingRunning();
                return;
            }

            if (validResults.empty()) {
                ClearMatchmakingRunning();
                return;
            }
            StartVerificationBatch(std::move(validResults), opts);
        };

        RunLlmBatch<GlobalTask>(
            Tr("modal.llm_retry.batch_global_matchmaking"),
            "gamemaster_evaluation",
            { GlobalTask{ std::move(ctx), {} } },
            std::move(callbacks),
            GetSessionEpoch());
        return true;
    }

    void AgencyWorkflowCoordinator::ProcessManualResults(RE::FormID targetNpcId, const nlohmann::json& matchesArray) {
        if (!matchesArray.is_array()) {
            return;
        }
        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const std::string targetName = ResolveActorNameForAgency(targetNpcId);

        std::set<RE::FormID> seenCandidateFormIDs;
        std::vector<MatchmakingResult> dedupedResults;
        for (auto& r : ParseMatchmakingJsonArray(matchesArray)) {
            ResolveResultFormIDs(r);
            bool isA = (r.actorAFormID == targetNpcId) ||
                       (r.actorAFormID == 0 && !targetName.empty() &&
                        Foundation::StringUtils::CaseInsensitiveEquals(r.actorAName, targetName));
            const bool isB = (r.actorBFormID == targetNpcId) ||
                             (r.actorBFormID == 0 && !targetName.empty() &&
                              Foundation::StringUtils::CaseInsensitiveEquals(r.actorBName, targetName));
            if (isB && !isA) {
                std::swap(r.actorAFormID, r.actorBFormID);
                std::swap(r.actorAName, r.actorBName);
                isA = true;
            }
            if (!isA) continue;
            r.actorAFormID = targetNpcId;
            if (!targetName.empty() && targetName != "None") {
                r.actorAName = targetName;
            }
            if (r.actorBFormID == 0 || r.actorBFormID == targetNpcId || seenCandidateFormIDs.contains(r.actorBFormID)) {
                continue;
            }
            if (!ValidateCandidate(r, *snap, currentGameDay, /*checkCooldown=*/false)) {
                continue;
            }
            seenCandidateFormIDs.insert(r.actorBFormID);
            r.isCheating = IsPairCheatingTransition(*snap, r.actorAFormID, r.actorBFormID, r.targetStatus);
            dedupedResults.push_back(std::move(r));
        }

        GetUiBridgeGateway().SendMatchmakingResults(dedupedResults, /*isGlobal=*/false);
    }

    void AgencyWorkflowCoordinator::ProcessGlobalManualResults(const nlohmann::json& matchesArray) {
        if (!matchesArray.is_array()) {
            return;
        }
        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();

        std::set<RE::FormID> seenActorsInBatch;
        std::vector<MatchmakingResult> dedupedResults;
        for (auto& r : ParseMatchmakingJsonArray(matchesArray)) {
            ResolveResultFormIDs(r);
            if (r.actorAFormID == 0 || r.actorBFormID == 0) continue;
            if (seenActorsInBatch.contains(r.actorAFormID) || seenActorsInBatch.contains(r.actorBFormID)) continue;
            if (!ValidateCandidate(r, *snap, currentGameDay, /*checkCooldown=*/false)) continue;
            seenActorsInBatch.insert(r.actorAFormID);
            seenActorsInBatch.insert(r.actorBFormID);
            r.isCheating = IsPairCheatingTransition(*snap, r.actorAFormID, r.actorBFormID, r.targetStatus);
            dedupedResults.push_back(std::move(r));
        }

        GetUiBridgeGateway().SendMatchmakingResults(dedupedResults, /*isGlobal=*/true);
    }

    bool AgencyWorkflowCoordinator::ValidateCandidate(MatchmakingResult& result,
                                                      const State::WorldStateSnapshot& snap,
                                                      float currentGameDay,
                                                      bool checkCooldown) const {
        ResolveResultFormIDs(result);

        Domain::Agency::CandidateValidationInput in{};
        in.actorA = Foundation::ActorId{ result.actorAFormID };
        in.actorB = Foundation::ActorId{ result.actorBFormID };
        in.actorAName = result.actorAName;
        in.actorBName = result.actorBName;
        in.action = result.action;
        in.targetStatus = result.targetStatus;
        in.currentGameDay = currentGameDay;
        in.checkCooldown = checkCooldown;

        if (result.actorAFormID != 0 && result.actorBFormID != 0 &&
            result.actorAFormID != result.actorBFormID &&
            result.actorAFormID != kPlayerFormID && result.actorBFormID != kPlayerFormID) {
            in.actorAEligibleAdult = IsNpcEligibleWorldAdult(result.actorAFormID, snap);
            in.actorBEligibleAdult = IsNpcEligibleWorldAdult(result.actorBFormID, snap);

            auto& relUseCases = RelationshipUseCases::GetSingleton();
            const std::string normAction = Domain::Agency::NormalizeMatchmakingAction(result.action, result.targetStatus);
            if (normAction == "new" || normAction == "advance") {
                in.areFamily = relUseCases.AreFamilyMembers(result.actorAFormID, result.actorBFormID, &in.familyReason);
                in.actorASex = ResolveActorSexForAgency(result.actorAFormID);
                in.actorBSex = ResolveActorSexForAgency(result.actorBFormID);

                Foundation::RelationshipStatus targetSt = Foundation::StringToRelationshipStatus(result.targetStatus);
                if (normAction == "new" && (targetSt == Foundation::RelationshipStatus::Candidate ||
                                            targetSt == Foundation::RelationshipStatus::Unknown)) {
                    targetSt = Foundation::RelationshipStatus::Dating;
                }
                if (targetSt != Foundation::RelationshipStatus::Unknown &&
                    targetSt != Foundation::RelationshipStatus::Candidate) {
                    in.canForm = relUseCases.CanFormRelationship(
                        result.actorAFormID, result.actorBFormID, targetSt, &in.canFormReason);
                }
            } else if (normAction == "open") {
                in.canOpen = relUseCases.CanOpenRelationship(result.actorAFormID, result.actorBFormID);
            } else if (normAction == "close") {
                in.canClose = relUseCases.CanCloseRelationship(result.actorAFormID, result.actorBFormID);
            }
        }

        const auto out = Domain::Agency::ValidateMatchmakingCandidate(snap, in);
        result.action = out.normalizedAction;
        result.targetStatus = out.normalizedTargetStatus;
        result.isCheating = out.isCheating;
        result.skipped = !out.valid;
        result.skipReason = out.valid ? std::string{} : out.skipReason;
        return out.valid;
    }

    void AgencyWorkflowCoordinator::StartVerificationBatch(std::vector<MatchmakingResult> candidates,
                                                           const MatchBatchOptions& opts) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable() || !snap->settings->matchmakingEvaluationStep) {
            ProceedAfterVerification(std::move(candidates), opts);
            return;
        }

        // Check before moving to prevent use-after-move when all items are skipped!
        const bool anyToVerify = std::any_of(candidates.begin(), candidates.end(), [](const MatchmakingResult& c) {
            return !c.skipped;
        });
        if (!anyToVerify) {
            ProceedAfterVerification(std::move(candidates), opts);
            return;
        }

        struct VerifyTask {
            MatchmakingResult candidate;
            nlohmann::json contextJson;
            bool skipVerify{ false };
            MatchmakingVerifyEvalResult evalResult;
        };

        std::vector<VerifyTask> tasks;
        tasks.reserve(candidates.size());
        for (auto& cand : candidates) {
            VerifyTask task{};
            task.skipVerify = cand.skipped;
            if (!cand.skipped) {
                task.contextJson = BuildVerifyContextJson(cand, *snap, opts.currentGameDay);
            }
            task.candidate = std::move(cand);
            tasks.push_back(std::move(task));
        }

        LlmBatchCallbacks<VerifyTask> callbacks;
        callbacks.submitTask = [](VerifyTask& task, auto reportDone) -> bool {
            if (task.skipVerify) {
                reportDone(true, false, {}, nullptr);
                return true;
            }
            return GetSkyrimNetGateway().EvaluateMatchmakingVerify(
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                task.candidate.action,
                task.candidate.targetStatus,
                task.candidate.actorAName,
                task.candidate.actorBName,
                [reportDone = std::move(reportDone)](const MatchmakingVerifyEvalResult& eval) {
                    if (!eval.success) {
                        const bool isInvalidJson = (eval.failureType == LLMFailureType::InvalidJson);
                        const std::string err = !eval.failureDetail.empty() ? eval.failureDetail : eval.reason;
                        reportDone(false, isInvalidJson, err, nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [eval](VerifyTask& t) {
                        t.evalResult = eval;
                    });
                });
        };
        callbacks.formatFailureLine = [](const VerifyTask& t, const std::string& reason) {
            return std::format("{} & {}: {}", t.candidate.actorAName, t.candidate.actorBName, reason);
        };
        callbacks.onAllResolved = [this, opts](const std::shared_ptr<LlmBatchState<VerifyTask>>& state) {
            std::vector<MatchmakingResult> verified;
            verified.reserve(state->tasks.size());
            for (auto& item : state->tasks) {
                MatchmakingResult res = item.payload.candidate;
                const auto decision = Domain::Agency::EvaluateVerificationBatchItem(
                    item.payload.skipVerify,
                    item.success,
                    item.payload.evalResult.suitable,
                    item.failureReason,
                    item.payload.evalResult.reason,
                    res.memory);
                if (!item.payload.skipVerify && decision.skipped) {
                    res.skipped = true;
                    res.skipReason = decision.skipReason;
                } else if (!decision.memoryToApply.empty()) {
                    res.memory = decision.memoryToApply;
                }
                verified.push_back(std::move(res));
            }
            ProceedAfterVerification(std::move(verified), opts);
        };

        const std::string llmVariant =
            GetSkyrimNetGateway().ShouldUseDecisions() ? "decisions" : "gamemaster_evaluation";
        RunLlmBatch<VerifyTask>(
            Tr("modal.llm_retry.batch_matchmaking_verify"),
            llmVariant,
            std::move(tasks),
            std::move(callbacks),
            GetSessionEpoch());
    }

    void AgencyWorkflowCoordinator::ProceedAfterVerification(std::vector<MatchmakingResult> results,
                                                             const MatchBatchOptions& opts) {
        std::vector<MatchmakingResult> eligible;
        eligible.reserve(results.size());
        for (auto& r : results) {
            if (!r.skipped) {
                eligible.push_back(std::move(r));
            }
        }
        if (eligible.empty()) {
            if (opts.isPartnerAgency) {
                ClearPartnerAgencyRunning();
            } else {
                ClearMatchmakingRunning();
            }
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (snap->settings &&
            Domain::Agency::ShouldShowMatchmakingReviewModal(
                opts.isPartnerAgency, opts.skipConfirmationModal, *snap->settings)) {
            ShowMatchmakingReviewModal(std::move(eligible), opts);
            return;
        }
        ApplyMatchmakingResults(std::move(eligible), opts);
    }

    void AgencyWorkflowCoordinator::ShowMatchmakingReviewModal(std::vector<MatchmakingResult> results,
                                                               const MatchBatchOptions& opts) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const bool revealSecrets = snap->settings ? snap->settings->revealSecretsInUI : false;
        const bool allowCheating = snap->settings ? snap->settings->matchmakingAllowCheating : true;
        auto& relUseCases = RelationshipUseCases::GetSingleton();

        std::vector<ModalChecklistItem> items;
        items.reserve(results.size());
        bool anyEligible = false;

        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            if (r.skipped) {
                continue;
            }
            anyEligible = true;

            ModalChecklistItem item{};
            item.id = std::format("pair_{}", i);
            const std::string nameA = !r.actorAName.empty() ? r.actorAName : ResolveActorNameForAgency(r.actorAFormID);
            const std::string nameB = !r.actorBName.empty() ? r.actorBName : ResolveActorNameForAgency(r.actorBFormID);
            item.label = TrFmt("modal.common.pair_subtitle", { { "actorA", nameA }, { "actorB", nameB } });

            const auto currentSt = relUseCases.GetRelationshipStatus(r.actorAFormID, r.actorBFormID);
            std::string currentLabel = FormatStatusDisplayLabel(currentSt);
            if (Foundation::IsActivePartnerStatus(currentSt) && snap->relationships) {
                const bool isOpen = snap->relationships->IsPairOpen(
                    Foundation::ActorId{ r.actorAFormID }, Foundation::ActorId{ r.actorBFormID });
                currentLabel += isOpen ? " (Open)" : " (Exclusive)";
            }
            item.subtitle = std::format("{} -> {}", currentLabel, FormatTargetStatusDisplayLabel(r.targetStatus));
            item.checked = true;
            item.checkboxDisabled = false;

            // H2 Intrigue Mode + L1 per-item targetStatus dropdown override
            if (r.action == "new" || r.action == "advance") {
                if (Domain::Agency::ShouldExposeMatchmakingCheatingBadge(r.isCheating, revealSecrets)) {
                    item.badgeText = Tr("modal.matchmaking_review.badge_cheating");
                    item.badgeTone = "warning";
                } else if (r.action == "advance") {
                    item.badgeText = "Advance";
                    item.badgeTone = "success";
                } else {
                    item.badgeText = "New";
                    item.badgeTone = "accent";
                }

                item.selectKey = "targetStatus";
                item.selectLabel = "Status";
                item.selectValue = Foundation::StringUtils::ToLowerStd(r.targetStatus);
                if (item.selectValue.empty()) {
                    item.selectValue = "dating";
                }

                struct StatusOptionSpec {
                    const char* value;
                    const char* label;
                    Foundation::RelationshipStatus statusEnum;
                };
                static constexpr StatusOptionSpec kPositiveOptions[] = {
                    { "dating", "Dating", Foundation::RelationshipStatus::Dating },
                    { "lover", "Lover", Foundation::RelationshipStatus::Lover },
                    { "engaged", "Engaged", Foundation::RelationshipStatus::Engaged },
                    { "married", "Married", Foundation::RelationshipStatus::Married }
                };
                for (const auto& spec : kPositiveOptions) {
                    ModalSelectOption opt{};
                    opt.value = spec.value;
                    opt.label = spec.label;
                    std::string reason;
                    bool canForm = relUseCases.CanFormRelationship(
                        r.actorAFormID, r.actorBFormID, spec.statusEnum, &reason);
                    if (canForm && spec.statusEnum == Foundation::RelationshipStatus::Lover && !allowCheating) {
                        if (IsPairCheatingTransition(*snap, r.actorAFormID, r.actorBFormID, "lover")) {
                            canForm = false;
                            reason = "Cheating is disabled in matchmaking settings";
                        }
                    }
                    if (!canForm && item.selectValue != spec.value) {
                        opt.disabled = true;
                        opt.tooltip = std::move(reason);
                    }
                    item.selectOptions.push_back(std::move(opt));
                }
            } else if (r.action == "breakup") {
                item.badgeText = "Breakup";
                item.badgeTone = "danger";
                item.selectKey = "targetStatus";
                item.selectLabel = "Status";
                item.selectValue = Foundation::StringUtils::ToLowerStd(r.targetStatus);
                if (item.selectValue.empty()) {
                    item.selectValue = "exdating";
                }
                item.selectOptions = {
                    { "exdating", "Ex-Dating", false, "" },
                    { "exlover", "Ex-Lover", false, "" },
                    { "jilted", "Jilted", false, "" },
                    { "divorced", "Divorced", false, "" }
                };
            } else if (r.action == "open") {
                item.badgeText = "Open";
                item.badgeTone = "accent";
            } else if (r.action == "close") {
                item.badgeText = "Close";
                item.badgeTone = "default";
            } else {
                item.badgeText = Tr("modal.matchmaking_review.badge_approved");
                item.badgeTone = "success";
            }

            if (!r.memory.empty()) {
                item.description = r.memory;
            } else {
                item.description = std::format("Proposed '{}' transition to {}.",
                                               r.action, FormatTargetStatusDisplayLabel(r.targetStatus));
            }
            items.push_back(std::move(item));
        }

        if (!anyEligible) {
            if (opts.isPartnerAgency) {
                ClearPartnerAgencyRunning();
            } else {
                ClearMatchmakingRunning();
            }
            return;
        }

        ModalRequest req{};
        req.modalType = "generic";
        req.title = Tr("modal.matchmaking_review.title");
        req.subtitle = opts.isGlobal
            ? Tr("modal.matchmaking_review.subtitle_global")
            : Tr("modal.matchmaking_review.subtitle_manual");
        req.message = Tr("modal.matchmaking_review.message");
        req.tone = ModalTone::Ai;
        req.size = items.size() > 4 ? ModalSize::Xl : ModalSize::Lg;
        req.defaultCancelButtonIndex = 1;
        req.controls.push_back(ModalControl::MakeChecklist(
            "selected_matches",
            Tr("modal.matchmaking_review.checklist_label"),
            std::move(items),
            /*showSelectAllToggle=*/true));

        ModalButton applyBtn = ModalButton::Make("apply", Tr("modal.matchmaking_review.btn_apply"), ModalButtonVariant::Primary);
        applyBtn.requireChecklistId = "selected_matches";
        applyBtn.minCheckedCount = 1;
        req.buttons.push_back(std::move(applyBtn));
        req.buttons.push_back(ModalButton::Make("discard", Tr("modal.matchmaking_review.btn_discard"), ModalButtonVariant::Secondary));

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        auto sharedResults = std::make_shared<std::vector<MatchmakingResult>>(std::move(results));

        GetModalGateway().ShowModal(
            std::move(req),
            [this, expectedEpoch, sharedResults, opts](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(GetPapyrusMutex());
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "apply")) {
                    if (opts.isPartnerAgency) {
                        ClearPartnerAgencyRunning();
                    } else {
                        ClearMatchmakingRunning();
                    }
                    return;
                }

                const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
                std::vector<MatchmakingResult> approved;
                approved.reserve(sharedResults->size());

                for (std::size_t i = 0; i < sharedResults->size(); ++i) {
                    auto& match = (*sharedResults)[i];
                    if (match.skipped) {
                        continue;
                    }
                    const std::string itemId = std::format("pair_{}", i);
                    if (!resp.IsItemChecked("selected_matches", itemId, false)) {
                        continue;
                    }
                    if (match.action == "new" || match.action == "advance" || match.action == "breakup") {
                        const std::string chosenStatus = Foundation::StringUtils::ToLowerStd(
                            resp.GetItemSelectValue("selected_matches", itemId, match.targetStatus));
                        if (!chosenStatus.empty() && chosenStatus != match.targetStatus) {
                            MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Review modal overrode {} <-> {}: '{}' -> '{}'",
                                           match.actorAName, match.actorBName, match.targetStatus, chosenStatus);
                            match.targetStatus = chosenStatus;
                            match.isCheating = IsPairCheatingTransition(
                                *snapNow, match.actorAFormID, match.actorBFormID, match.targetStatus);
                        }
                    }
                    approved.push_back(std::move(match));
                }

                ApplyMatchmakingResults(std::move(approved), opts);
            });
    }

    bool AgencyWorkflowCoordinator::ApplyMatch(MatchmakingResult& result, float currentGameDay) {
        if (result.skipped) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!IsNpcEligibleWorldAdult(result.actorAFormID, *snap) ||
            !IsNpcEligibleWorldAdult(result.actorBFormID, *snap)) {
            result.skipped = true;
            result.skipReason = "Actor is deceased or invalid";
            return false;
        }

        const RE::FormID idA = result.actorAFormID;
        const RE::FormID idB = result.actorBFormID;
        const std::string action = NormalizeActionString(result.action, result.targetStatus);
        result.action = action;

        auto& relUseCases = RelationshipUseCases::GetSingleton();
        bool applied = false;

        if (action == "new" || action == "advance") {
            Foundation::RelationshipStatus targetSt = Foundation::StringToRelationshipStatus(result.targetStatus);
            if (targetSt == Foundation::RelationshipStatus::Candidate ||
                targetSt == Foundation::RelationshipStatus::Unknown) {
                targetSt = Foundation::RelationshipStatus::Dating;
                result.targetStatus = "dating";
            }

            // M7 + H7: Talking-stage cheating affair for a player's closed active main partner
            const bool aIsClosedPlayerPartner =
                relUseCases.IsPlayerActiveMainPartner(idA) && !relUseCases.GetPairIsOpen(idA, kPlayerFormID);
            const bool bIsClosedPlayerPartner =
                relUseCases.IsPlayerActiveMainPartner(idB) && !relUseCases.GetPairIsOpen(idB, kPlayerFormID);
            const bool setTalkingStage =
                action == "new" &&
                targetSt == Foundation::RelationshipStatus::Lover &&
                result.isCheating &&
                (result.affairStage == Foundation::AffairStage::Talking ||
                 aIsClosedPlayerPartner || bIsClosedPlayerPartner);

            if (setTalkingStage) {
                const auto oldSt = relUseCases.GetRelationshipStatus(idA, idB);
                {
                    ScopedMemorySuppression suppress;
                    applied = relUseCases.PromoteToStatusWith(idA, idB, targetSt, /*sendEvent=*/false);
                }
                if (applied) {
                    if (aIsClosedPlayerPartner) {
                        relUseCases.SetCheatingAffairStage(idA, kPlayerFormID, idB, Foundation::AffairStage::Talking);
                    }
                    if (bIsClosedPlayerPartner) {
                        relUseCases.SetCheatingAffairStage(idB, kPlayerFormID, idA, Foundation::AffairStage::Talking);
                    }
                    if (GetSkyrimNetGateway().IsAvailable() && GetSkyrimNetGateway().IsMatchmakingApplyActive()) {
                        GetSkyrimNetGateway().RecordRelationshipStatusChange(idA, idB, oldSt, targetSt);
                    }
                }
            } else {
                applied = relUseCases.PromoteToStatusWith(idA, idB, targetSt, /*sendEvent=*/false);
            }
        } else if (action == "breakup") {
            const auto currentStatus = relUseCases.GetRelationshipStatus(idA, idB);
            const auto [defaultStr, defaultEnum] = Domain::Agency::GetBreakupTargetStatus(currentStatus);
            auto targetEnum = Foundation::StringToRelationshipStatus(result.targetStatus);
            std::string targetStr = Foundation::StringUtils::ToLowerStd(result.targetStatus);
            if (targetEnum != Foundation::RelationshipStatus::Divorced &&
                targetEnum != Foundation::RelationshipStatus::Jilted &&
                targetEnum != Foundation::RelationshipStatus::ExDating &&
                targetEnum != Foundation::RelationshipStatus::ExLover) {
                targetEnum = defaultEnum;
                targetStr = std::string(defaultStr);
            }
            switch (targetEnum) {
                case Foundation::RelationshipStatus::Divorced:
                    applied = relUseCases.PromoteToDivorced(idA, idB, /*sendEvent=*/false);
                    break;
                case Foundation::RelationshipStatus::Jilted:
                    applied = relUseCases.PromoteToJilted(idA, idB, /*sendEvent=*/false);
                    break;
                case Foundation::RelationshipStatus::ExDating:
                    applied = relUseCases.PromoteToExDating(idA, idB, /*sendEvent=*/false);
                    break;
                case Foundation::RelationshipStatus::ExLover:
                    applied = relUseCases.PromoteToExLover(idA, idB, /*sendEvent=*/false);
                    if (applied &&
                        result.postBreakupIntent != Foundation::PartnerIntentType::None &&
                        snap->settings && snap->settings->playerPartnerAgencyEnableForceGreets &&
                        relUseCases.GetPartnerPendingIntent(idA).type == Foundation::PartnerIntentType::None) {
                        relUseCases.SetPartnerPendingIntent(idA, result.postBreakupIntent, idB);
                        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Queued post-breakup confession intent '{}' for {:08X} regarding ex-lover {:08X}",
                                       Foundation::PartnerIntentTypeToString(result.postBreakupIntent), idA, idB);
                    }
                    break;
                default:
                    applied = false;
                    break;
            }
            if (applied) {
                result.targetStatus = targetStr;
            }
        } else if (action == "open") {
            applied = relUseCases.SetPairIsOpen(idA, idB, true, /*sendEvent=*/false);
            result.targetStatus = "open";
        } else if (action == "close") {
            applied = relUseCases.SetPairIsOpen(idA, idB, false, /*sendEvent=*/false);
            result.targetStatus = "closed";
        } else {
            result.skipped = true;
            result.skipReason = "Unknown action: " + action;
            return false;
        }

        if (!applied) {
            result.skipped = true;
            result.skipReason = "Relationship transition rejected";
            return false;
        }

        result.applied = true;
        RecordNpcCooldown(idA, currentGameDay);
        RecordNpcCooldown(idB, currentGameDay);

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Applied matchmaking: {:08X} ('{}') & {:08X} ('{}') action='{}' status='{}'",
                       idA, result.actorAName, idB, result.actorBName, result.action, result.targetStatus);
        return true;
    }

    void AgencyWorkflowCoordinator::ApplyMatchmakingResults(std::vector<MatchmakingResult> results,
                                                            const MatchBatchOptions& opts) {
        // H4: Wrap all ApplyMatch calls in ScopedMatchmakingApply so SkyrimNetMemoryService handles
        // batching, epistemic isolation, "memory" variant, and programmatic fallback cleanly.
        std::optional<ScopedMatchmakingApply> applyScope;
        if (GetSkyrimNetGateway().IsAvailable()) {
            applyScope.emplace();
        }

        std::size_t appliedCount = 0;
        for (auto& r : results) {
            if (!r.skipped && ApplyMatch(r, opts.currentGameDay)) {
                ++appliedCount;
            }
        }
        applyScope.reset();

        if (appliedCount > 0 && GetUiBridgeGateway().IsDashboardOpen()) {
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCList();
        }

        if (opts.isPartnerAgency) {
            ClearPartnerAgencyRunning();
        } else {
            ClearMatchmakingRunning();
        }
    }

    void AgencyWorkflowCoordinator::FinalizePartnerAgencyMatches(std::vector<MatchmakingResult> matches,
                                                                 float currentGameDay) {
        if (matches.empty()) {
            ClearPartnerAgencyRunning();
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = false,
            .isPartnerAgency = true,
            .skipConfirmationModal = !(snap->settings && snap->settings->playerPartnerCheatingConfirmResults)
        };
        if (!opts.skipConfirmationModal) {
            ShowMatchmakingReviewModal(std::move(matches), opts);
            return;
        }
        ApplyMatchmakingResults(std::move(matches), opts);
    }

    // =========================================================================
    // Player-Partner Agency (Infidelity / Open-Close Intents)
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledPlayerPartnerInfidelity(float currentGameDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->relationships || !snap->affairs) {
            return;
        }
        const auto& settings = *snap->settings;
        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            return;
        }

        const float lastDay = GetLastPlayerPartnerCheatingDay();
        const float scanInterval = settings.GetPartnerAgencyScanIntervalDays();
        if (lastDay >= 0.0f && currentGameDay >= lastDay && (currentGameDay - lastDay) < scanInterval) {
            return;
        }

        // L2: Atomic exchange prevents race between concurrent triggers
        if (runningPartnerAgency_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        auto* playerActor = RE::PlayerCharacter::GetSingleton();
        auto& spatialIdx = SpatialWorldIndex::GetSingleton();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        std::vector<Domain::Agency::PartnerAgencyInput> inputs;

        for (const auto& [partnerId, status] : snap->relationships->GetActivePartners(Foundation::ActorId::Player())) {
            if (!Foundation::IsMainTierStatus(status)) {
                continue;
            }
            if (!IsNpcEligibleWorldAdult(partnerId.raw(), *snap)) {
                continue;
            }

            // M4: Hard Immunity 3 — follower, same cell, or <= 3000.0f distance
            bool nearOrFollower = false;
            if (RE::Actor* livePartner = spatialIdx.GetActorFromFormId(partnerId.raw())) {
                if (livePartner->IsDead() || livePartner->IsDeleted() || livePartner->IsDisabled()) {
                    continue;
                }
                if (Utils::IsPlayerFollower(livePartner)) {
                    nearOrFollower = true;
                } else if (playerActor && livePartner->Is3DLoaded() &&
                           (livePartner->GetParentCell() == playerActor->GetParentCell() ||
                            livePartner->GetPosition().GetDistance(playerActor->GetPosition()) <= 3000.0f)) {
                    nearOrFollower = true;
                }
            } else if (spatialSnap) {
                if (const auto* sample = spatialSnap->Find(partnerId.AsRefId())) {
                    const auto* playerSample = spatialSnap->Find(Foundation::RefId::Player());
                    const bool sameCell = playerSample && !playerSample->parentCellId.IsNull() &&
                                          sample->parentCellId == playerSample->parentCellId;
                    nearOrFollower = sample->IsPlayerTeammate() || sameCell ||
                                     spatialSnap->IsWithinRadius(Foundation::RefId::Player(), partnerId.AsRefId(), 3000.0f);
                }
            }

            bool pregnantWithPlayer = false;
            if (snap->lineage) {
                for (const auto cid : snap->lineage->GetUnbornOfMother(partnerId)) {
                    if (const auto* child = snap->lineage->FindChild(cid)) {
                        if (child->bioFather.IsPlayer() || child->presumedFather.IsPlayer()) {
                            pregnantWithPlayer = true;
                            break;
                        }
                    }
                }
            }

            Domain::Agency::PartnerAgencyInput input{};
            input.partnerId = partnerId;
            input.partnerName = ResolveActorNameForAgency(partnerId.raw());
            input.partnerSex = ResolveActorSexForAgency(partnerId.raw());
            input.isFollowerOrNearPlayer = nearOrFollower;
            input.isPregnantWithPlayerChild = pregnantWithPlayer;
            inputs.push_back(std::move(input));
        }

        if (inputs.empty()) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            ClearPartnerAgencyRunning();
            return;
        }

        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));
        const auto plans = Domain::Agency::EvaluatePartnerAgencyActions(*snap, inputs, currentGameDay, rng);

        // C6: Clear any expired pending intents (including ConfessPhysical or SuggestOpen that expired without firing)
        {
            const float intentTTL = settings.GetPartnerIntentTTLDays();
            std::vector<Foundation::ActorId> expiredToClear;
            for (const auto& input : inputs) {
                const auto pending = snap->relationships->GetPendingIntent(input.partnerId);
                if (pending.type != Foundation::PartnerIntentType::None &&
                    pending.createdGameDay >= 0.0f &&
                    (currentGameDay - pending.createdGameDay) >= intentTTL) {
                    expiredToClear.push_back(input.partnerId);
                }
            }
            if (!expiredToClear.empty()) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    auto& rels = ws.relationships.Write();
                    for (const auto pid : expiredToClear) {
                        rels.ClearPendingIntent(pid);
                    }
                });
            }
        }

        const auto freshSnap = State::WorldStore::GetSingleton().Snapshot();
        const bool useLlm = GetSkyrimNetGateway().IsAvailable() && settings.playerPartnerCheatingUseLLM;
        auto& relUseCases = RelationshipUseCases::GetSingleton();

        std::vector<PartnerAgencyBatchItem> llmTasks;
        std::vector<MatchmakingResult> directMatches;
        std::unordered_set<RE::FormID> diceUsedCandidates;
        std::size_t newBondSearchCount = 0;

        auto makeContextInput = [&](const Domain::Agency::PartnerAgencyActionPlan& plan,
                                    const std::string& evalMode,
                                    bool canConfess,
                                    std::string driversSummary) {
            PartnerAgencyContextInput ci{};
            ci.partnerID = plan.partnerId.raw();
            ci.partnerName = ResolveActorNameForAgency(plan.partnerId.raw());
            ci.statusWithPlayer = freshSnap->relationships->GetStatus(Foundation::ActorId::Player(), plan.partnerId);
            ci.affection = freshSnap->affection ? freshSnap->affection->GetPermanent(plan.partnerId, 50) : 50;
            ci.daysSinceInteraction = std::max(0.0f, relUseCases.GetDaysSinceLastAffection(plan.partnerId.raw()));
            ci.hierarchyRank = (ci.statusWithPlayer == Foundation::RelationshipStatus::Married && freshSnap->hierarchy)
                                   ? freshSnap->hierarchy->GetRank(plan.partnerId)
                                   : -1;
            ci.sharedChildrenCount = CountSharedChildrenWithPlayer(*freshSnap, plan.partnerId);
            ci.isOpenRelationshipLover =
                freshSnap->relationships->IsPairOpen(Foundation::ActorId::Player(), plan.partnerId);
            ci.finalChance = plan.rolledChance;
            ci.driversSummary = std::move(driversSummary);
            ci.locCtx = ResolveActorLocationContext(plan.partnerId.raw(), *freshSnap);
            ci.evalMode = evalMode;
            ci.sideLoverID = plan.relatedLoverId.raw();
            ci.sideLoverName = plan.relatedLoverId.IsNull() ? std::string() : ResolveActorNameForAgency(plan.relatedLoverId.raw());
            ci.canConfess = canConfess;
            ci.sideAffairStage = Foundation::AffairStage::Physical;
            if (!plan.relatedLoverId.IsNull()) {
                if (const auto* aff = freshSnap->affairs->FindTriple(
                        plan.partnerId, Foundation::ActorId::Player(), plan.relatedLoverId)) {
                    ci.sideAffairStage = aff->affairStage;
                }
                const float startDay = GetLatestNonBreakupStartDaySafe(*freshSnap, plan.partnerId, plan.relatedLoverId);
                if (startDay >= 0.0f) {
                    ci.sideLoverAgeDays = std::max(0.0f, currentGameDay - startDay);
                }
            }
            return ci;
        };

        for (const auto& plan : plans) {
            using Domain::Agency::PartnerAgencyActionType;

            if (plan.type == PartnerAgencyActionType::EscalateTalkingToPhysical ||
                plan.type == PartnerAgencyActionType::ExpiredIntentConfessEscalatePhysical) {
                if (!plan.relatedLoverId.IsNull()) {
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.affairs.Write().SetStage(
                            plan.partnerId,
                            Foundation::ActorId::Player(),
                            plan.relatedLoverId,
                            Foundation::AffairStage::Physical,
                            currentGameDay);
                        ws.relationships.Write().ClearPendingIntent(plan.partnerId);
                    });
                }
                continue;
            }

            if (plan.type == PartnerAgencyActionType::ExpiredIntentSuggestClosePenalty) {
                const int curAff = freshSnap->affection ? freshSnap->affection->GetPermanent(plan.partnerId, 50) : 50;
                relUseCases.SetPermanentAffection(
                    plan.partnerId.raw(), std::clamp(curAff + plan.permanentAffectionDelta, 0, 100), true);
                continue;
            }

            const bool canSetPendingIntent =
                settings.playerPartnerAgencyEnableForceGreets &&
                relUseCases.GetPartnerPendingIntent(plan.partnerId.raw()).type == Foundation::PartnerIntentType::None;

            switch (plan.type) {
                case PartnerAgencyActionType::BreakupClosedAffairLover: {
                    auto ci = makeContextInput(plan, "breakup_affair", canSetPendingIntent,
                                               "guilt, renewed attention from the player, or affair fatigue");
                    if (!useLlm) {
                        MatchmakingResult br{};
                        br.actorAFormID = ci.partnerID;
                        br.actorBFormID = ci.sideLoverID;
                        br.actorAName = ci.partnerName;
                        br.actorBName = ci.sideLoverName;
                        br.action = "breakup";
                        br.targetStatus = "exlover";
                        br.isCheating = false;
                        if (plan.triggerConfessIntent && canSetPendingIntent) {
                            br.postBreakupIntent = (ci.sideAffairStage == Foundation::AffairStage::Talking)
                                                       ? Foundation::PartnerIntentType::ConfessTalking
                                                       : Foundation::PartnerIntentType::ConfessPhysical;
                        }
                        br.memory = std::format("{} ended their secret {} affair with {} out of guilt and renewed focus on their relationship with the player.",
                                                ci.partnerName, Foundation::AffairStageToString(ci.sideAffairStage), ci.sideLoverName);
                        if (ValidateCandidate(br, *freshSnap, currentGameDay, /*checkCooldown=*/false)) {
                            directMatches.push_back(std::move(br));
                        }
                    } else {
                        PartnerAgencyBatchItem task{};
                        task.plan = plan;
                        task.contextJson = BuildPartnerAgencyContextJson(*freshSnap, ci, currentGameDay);
                        task.ctxInput = std::move(ci);
                        llmTasks.push_back(std::move(task));
                    }
                    break;
                }
                case PartnerAgencyActionType::ConfessAffairIntent: {
                    // M2: Route standalone ConfessAffairIntent through "breakup_affair" LLM when useLlm is true
                    auto ci = makeContextInput(plan, "breakup_affair", true,
                                               "remorse and high affection after recent time with the player");
                    if (!useLlm) {
                        if (canSetPendingIntent) {
                            const auto confType = (ci.sideAffairStage == Foundation::AffairStage::Talking)
                                                      ? Foundation::PartnerIntentType::ConfessTalking
                                                      : Foundation::PartnerIntentType::ConfessPhysical;
                            relUseCases.SetPartnerPendingIntent(plan.partnerId.raw(), confType, plan.relatedLoverId.raw());
                        }
                    } else {
                        PartnerAgencyBatchItem task{};
                        task.plan = plan;
                        task.contextJson = BuildPartnerAgencyContextJson(*freshSnap, ci, currentGameDay);
                        task.ctxInput = std::move(ci);
                        llmTasks.push_back(std::move(task));
                    }
                    break;
                }
                case PartnerAgencyActionType::BreakupOpenLover: {
                    auto ci = makeContextInput(plan, "breakup_open_lover", false,
                                               "natural cooling of casual open-relationship fling");
                    if (!useLlm) {
                        MatchmakingResult br{};
                        br.actorAFormID = ci.partnerID;
                        br.actorBFormID = ci.sideLoverID;
                        br.actorAName = ci.partnerName;
                        br.actorBName = ci.sideLoverName;
                        br.action = "breakup";
                        br.targetStatus = "exlover";
                        br.isCheating = false;
                        br.memory = std::format("{} amicably ended their casual open-relationship fling with {} after {:.0f} days.",
                                                ci.partnerName, ci.sideLoverName, ci.sideLoverAgeDays);
                        if (ValidateCandidate(br, *freshSnap, currentGameDay, /*checkCooldown=*/false)) {
                            directMatches.push_back(std::move(br));
                        }
                    } else {
                        PartnerAgencyBatchItem task{};
                        task.plan = plan;
                        task.contextJson = BuildPartnerAgencyContextJson(*freshSnap, ci, currentGameDay);
                        task.ctxInput = std::move(ci);
                        llmTasks.push_back(std::move(task));
                    }
                    break;
                }
                case PartnerAgencyActionType::SuggestOpenIntent:
                case PartnerAgencyActionType::SuggestCloseIntent: {
                    const bool isSuggestOpen = (plan.type == PartnerAgencyActionType::SuggestOpenIntent);
                    auto ci = makeContextInput(plan,
                                               isSuggestOpen ? "suggest_open" : "suggest_close",
                                               false,
                                               isSuggestOpen ? "desire for open relationship freedom or polygamy parity"
                                                             : "desire for exclusive commitment with the player");
                    if (!useLlm) {
                        if (canSetPendingIntent) {
                            relUseCases.SetPartnerPendingIntent(
                                plan.partnerId.raw(),
                                isSuggestOpen ? Foundation::PartnerIntentType::SuggestOpen
                                              : Foundation::PartnerIntentType::SuggestClose,
                                0);
                        }
                    } else {
                        PartnerAgencyBatchItem task{};
                        task.plan = plan;
                        task.contextJson = BuildPartnerAgencyContextJson(*freshSnap, ci, currentGameDay);
                        task.ctxInput = std::move(ci);
                        llmTasks.push_back(std::move(task));
                    }
                    break;
                }
                case PartnerAgencyActionType::SeekOpenLover:
                case PartnerAgencyActionType::SeekSecretAffair:
                case PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair: {
                    if (newBondSearchCount >= 3) {
                        break;
                    }
                    const bool isOpenPartner = (plan.type == PartnerAgencyActionType::SeekOpenLover);
                    const std::string driverText =
                        (plan.type == PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair)
                            ? "unaddressed desire to open the relationship after being ignored by the player"
                            : (isOpenPartner ? "open relationship freedom and companionship"
                                             : "emotional distance from player");
                    auto ci = makeContextInput(plan, "candidate_select", false, driverText);
                    ci.isOpenRelationshipLover = isOpenPartner;
                    ci.shortlist = CollectShortlistedParamours(
                        *freshSnap, plan.partnerId, isOpenPartner, ci.locCtx, currentGameDay, rng);
                    if (ci.shortlist.empty()) {
                        break;
                    }
                    ++newBondSearchCount;

                    if (!useLlm) {
                        // Weighted dice-roll selection from scored shortlist with candidate dedup
                        std::vector<const ScoredAffairCandidate*> avail;
                        int totalWeight = 0;
                        for (const auto& sc : ci.shortlist) {
                            if (!diceUsedCandidates.contains(sc.formID)) {
                                avail.push_back(&sc);
                                totalWeight += std::max(1, sc.score);
                            }
                        }
                        if (avail.empty() || totalWeight <= 0) {
                            break;
                        }
                        const int wRoll = rng.NextInt(0, totalWeight - 1);
                        int accum = 0;
                        const ScoredAffairCandidate* chosen = avail.front();
                        for (const auto* sc : avail) {
                            accum += std::max(1, sc->score);
                            if (wRoll < accum) {
                                chosen = sc;
                                break;
                            }
                        }
                        MatchmakingResult r{};
                        r.actorAFormID = ci.partnerID;
                        r.actorBFormID = chosen->formID;
                        r.actorAName = ci.partnerName;
                        r.actorBName = chosen->name;
                        r.action = "new";
                        r.targetStatus = "lover";
                        r.isCheating = !isOpenPartner;
                        r.affairStage = isOpenPartner ? Foundation::AffairStage::Physical : Foundation::AffairStage::Talking;
                        r.memory = isOpenPartner
                                       ? std::format("{} formed an open-relationship lover bond with {} ({}) driven by {}.",
                                                     ci.partnerName, chosen->name, chosen->connectionTag, ci.driversSummary)
                                       : std::format("{} began a secret talking-stage affair with {} ({}) driven by {}.",
                                                     ci.partnerName, chosen->name, chosen->connectionTag, ci.driversSummary);
                        if (ValidateCandidate(r, *freshSnap, currentGameDay, /*checkCooldown=*/false)) {
                            diceUsedCandidates.insert(chosen->formID);
                            directMatches.push_back(std::move(r));
                        }
                    } else {
                        PartnerAgencyBatchItem task{};
                        task.plan = plan;
                        task.contextJson = BuildPartnerAgencyContextJson(*freshSnap, ci, currentGameDay);
                        task.ctxInput = std::move(ci);
                        llmTasks.push_back(std::move(task));
                    }
                    break;
                }
                default:
                    break;
            }
        }

        if (llmTasks.empty()) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            FinalizePartnerAgencyMatches(std::move(directMatches), currentGameDay);
            return;
        }

        auto sharedDirectMatches = std::make_shared<std::vector<MatchmakingResult>>(std::move(directMatches));

        LlmBatchCallbacks<PartnerAgencyBatchItem> callbacks;
        callbacks.submitTask = [](PartnerAgencyBatchItem& task, auto reportDone) -> bool {
            const std::string affairStageStr =
                task.contextJson.contains("affairStage") && task.contextJson["affairStage"].is_string()
                    ? task.contextJson["affairStage"].get<std::string>()
                    : std::string("talking");
            return GetSkyrimNetGateway().EvaluatePartnerAgency(
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                task.ctxInput.evalMode,
                task.ctxInput.canConfess,
                task.ctxInput.partnerName,
                task.ctxInput.sideLoverName,
                affairStageStr,
                [reportDone = std::move(reportDone)](const PartnerAgencyEvalResult& eval) {
                    if (!eval.success) {
                        const bool isInvalidJson = (eval.failureType == LLMFailureType::InvalidJson);
                        const std::string err = !eval.failureDetail.empty() ? eval.failureDetail : eval.reasoning;
                        reportDone(false, isInvalidJson, err, nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [eval](PartnerAgencyBatchItem& item) {
                        item.result = eval;
                    });
                });
        };
        callbacks.formatFailureLine = [](const PartnerAgencyBatchItem& t, const std::string& reason) {
            return std::format("{} ({}): {}",
                               t.ctxInput.partnerName,
                               t.ctxInput.evalMode.empty() ? "agency" : t.ctxInput.evalMode,
                               reason);
        };
        callbacks.onAllResolved = [this, currentGameDay, sharedDirectMatches](
                                      const std::shared_ptr<LlmBatchState<PartnerAgencyBatchItem>>& state) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            auto& relMgr = RelationshipUseCases::GetSingleton();
            const bool forceGreetsEnabled =
                snapNow->settings && snapNow->settings->playerPartnerAgencyEnableForceGreets;

            std::unordered_set<RE::FormID> usedCandidates;
            std::vector<MatchmakingResult> matches = std::move(*sharedDirectMatches);

            for (const auto& item : state->tasks) {
                if (!item.success) {
                    continue;
                }
                const auto& ci = item.payload.ctxInput;
                const auto& eval = item.payload.result;
                const auto& mode = ci.evalMode;

                if (!IsNpcEligibleWorldAdult(ci.partnerID, *snapNow)) {
                    continue;
                }
                if ((mode == "breakup_affair" || mode == "breakup_open_lover") &&
                    ci.sideLoverID != 0 && !IsNpcEligibleWorldAdult(ci.sideLoverID, *snapNow)) {
                    continue;
                }

                if (mode == "suggest_open" || mode == "suggest_close") {
                    if (eval.willSuggest && forceGreetsEnabled &&
                        relMgr.GetPartnerPendingIntent(ci.partnerID).type == Foundation::PartnerIntentType::None) {
                        const auto sType = (mode == "suggest_open")
                                               ? Foundation::PartnerIntentType::SuggestOpen
                                               : Foundation::PartnerIntentType::SuggestClose;
                        relMgr.SetPartnerPendingIntent(ci.partnerID, sType, 0);
                        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Partner agency queued intent '{}' for {:08X}",
                                       Foundation::PartnerIntentTypeToString(sType), ci.partnerID);
                    }
                    continue;
                }

                if (mode == "breakup_affair") {
                    const auto confType = (ci.sideAffairStage == Foundation::AffairStage::Talking)
                                              ? Foundation::PartnerIntentType::ConfessTalking
                                              : Foundation::PartnerIntentType::ConfessPhysical;
                    if (eval.willBreakup && ci.sideLoverID != 0) {
                        MatchmakingResult br{};
                        br.actorAFormID = ci.partnerID;
                        br.actorBFormID = ci.sideLoverID;
                        br.actorAName = ci.partnerName;
                        br.actorBName = ci.sideLoverName;
                        br.action = "breakup";
                        br.targetStatus = "exlover";
                        br.isCheating = false;
                        br.memory = !eval.reasoning.empty()
                                        ? eval.reasoning
                                        : std::format("{} ended their secret {} affair with {}.",
                                                      ci.partnerName,
                                                      Foundation::AffairStageToString(ci.sideAffairStage),
                                                      ci.sideLoverName);
                        if (eval.willConfess && ci.canConfess) {
                            br.postBreakupIntent = confType;
                        }
                        if (ValidateCandidate(br, *snapNow, currentGameDay, /*checkCooldown=*/false)) {
                            matches.push_back(std::move(br));
                        }
                    } else if (eval.willConfess && ci.canConfess && forceGreetsEnabled &&
                               relMgr.GetPartnerPendingIntent(ci.partnerID).type == Foundation::PartnerIntentType::None) {
                        // M2: Do NOT drop standalone willConfess when !willBreakup!
                        relMgr.SetPartnerPendingIntent(ci.partnerID, confType, ci.sideLoverID);
                        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Partner agency queued guilt confession '{}' for {:08X} (lover={:08X})",
                                       Foundation::PartnerIntentTypeToString(confType), ci.partnerID, ci.sideLoverID);
                    }
                    continue;
                }

                if (mode == "breakup_open_lover") {
                    if (eval.willBreakup && ci.sideLoverID != 0) {
                        MatchmakingResult br{};
                        br.actorAFormID = ci.partnerID;
                        br.actorBFormID = ci.sideLoverID;
                        br.actorAName = ci.partnerName;
                        br.actorBName = ci.sideLoverName;
                        br.action = "breakup";
                        br.targetStatus = "exlover";
                        br.isCheating = false;
                        br.memory = !eval.reasoning.empty()
                                        ? eval.reasoning
                                        : std::format("{} amicably ended their open-relationship fling with {}.",
                                                      ci.partnerName, ci.sideLoverName);
                        if (ValidateCandidate(br, *snapNow, currentGameDay, /*checkCooldown=*/false)) {
                            matches.push_back(std::move(br));
                        }
                    }
                    continue;
                }

                // candidate_select: SeekOpenLover / SeekSecretAffair / ExpiredIntentForceSuggestOpenAffair
                if (!eval.willCheat) {
                    continue;
                }

                // C2: Resolve chosenCandidateIndex FIRST before falling back to chosenCandidateName!
                std::vector<Domain::Agency::ShortlistCandidateRef> shortlistRefs;
                shortlistRefs.reserve(ci.shortlist.size());
                for (const auto& c : ci.shortlist) {
                    shortlistRefs.push_back({ Foundation::ActorId{ c.formID }, c.name, c.connectionTag });
                }
                std::unordered_set<Foundation::ActorId> usedActorIds;
                usedActorIds.reserve(usedCandidates.size());
                for (const RE::FormID uid : usedCandidates) {
                    usedActorIds.insert(Foundation::ActorId{ uid });
                }

                const auto choice = Domain::Agency::ResolvePartnerAgencyCandidate(
                    eval.chosenCandidateIndex, eval.chosenCandidateName, shortlistRefs, usedActorIds);
                RE::FormID chosenId = choice.actorId.raw();
                std::string chosenName = choice.name;
                std::string reasoning = eval.reasoning;
                if (choice.matchedByIndex && chosenId != 0) {
                    reasoning = ci.isOpenRelationshipLover
                                    ? std::format("{} formed an open-relationship lover bond with {} ({}) driven by {} ({}).",
                                                  ci.partnerName, choice.name, choice.connectionTag, ci.driversSummary, eval.reasoning)
                                    : std::format("{} began a secret talking-stage affair with {} ({}) driven by {} ({}).",
                                                  ci.partnerName, choice.name, choice.connectionTag, ci.driversSummary, eval.reasoning);
                } else if (choice.needsExternalNameLookup) {
                    const std::string_view trimmed = Foundation::StringUtils::TrimView(eval.chosenCandidateName);
                    if (const auto entry = SpatialWorldIndex::GetSingleton().FindByName(trimmed, NPCFilter::AdultOnly)) {
                        chosenId = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
                        chosenName = entry->nameDisplay;
                    }
                }

                if (chosenId == 0 || usedCandidates.contains(chosenId)) {
                    continue;
                }

                // C3 + H5: Route through MatchmakingResult -> ValidateCandidate -> FinalizePartnerAgencyMatches -> ApplyMatch
                MatchmakingResult r{};
                r.actorAFormID = ci.partnerID;
                r.actorBFormID = chosenId;
                r.actorAName = ci.partnerName;
                r.actorBName = chosenName.empty() ? ResolveActorNameForAgency(chosenId) : chosenName;
                r.action = "new";
                r.targetStatus = "lover";
                r.isCheating = !ci.isOpenRelationshipLover;
                r.affairStage = ci.isOpenRelationshipLover
                                    ? Foundation::AffairStage::Physical
                                    : Foundation::AffairStage::Talking;
                r.memory = std::move(reasoning);

                if (ValidateCandidate(r, *snapNow, currentGameDay, /*checkCooldown=*/false)) {
                    usedCandidates.insert(chosenId);
                    matches.push_back(std::move(r));
                } else {
                    MARAS_LOG_WARN("[AgencyWorkflowCoordinator] Partner agency ValidateCandidate rejected {:08X} <-> {:08X}: {}",
                                   r.actorAFormID, r.actorBFormID, r.skipReason);
                }
            }

            FinalizePartnerAgencyMatches(std::move(matches), currentGameDay);
        };

        const std::string llmVariant =
            GetSkyrimNetGateway().ShouldUseDecisions() ? "decisions" : "gamemaster_evaluation";
        RunLlmBatch<PartnerAgencyBatchItem>(
            Tr("modal.llm_retry.batch_partner_agency"),
            llmVariant,
            std::move(llmTasks),
            std::move(callbacks),
            GetSessionEpoch());
    }

    // =========================================================================
    // Partner Home Arrival Event + Encounter Template Modal
    // =========================================================================

    bool AgencyWorkflowCoordinator::EvaluatePartnerHomeArrival(RE::BGSLocation* newLocation) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->relationships) {
            return false;
        }
        const auto& settings = *snap->settings;
        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            return false;
        }
        if (!settings.playerPartnerAgencyEnableHomeArrivalEvents) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat() || player->IsDead()) {
            return false;
        }

        RE::TESObjectCELL* currentCell = player->GetParentCell();
        RE::BGSLocation* cellLoc = newLocation ? newLocation : (currentCell ? currentCell->GetLocation() : nullptr);
        if (!currentCell && !cellLoc) {
            return false;
        }

        const RE::FormID cellID = currentCell ? currentCell->GetFormID() : 0;
        const RE::FormID cellLocID = cellLoc ? cellLoc->GetFormID() : 0;
        const RE::FormID locationKey = (cellLocID != 0) ? cellLocID : cellID;
        if (locationKey == 0) {
            return false;
        }

        const std::string currentCellName =
            (currentCell && currentCell->GetName() && currentCell->GetName()[0] != '\0')
                ? currentCell->GetName()
                : "UnnamedCell";
        const std::string currentLocName =
            (cellLoc && cellLoc->GetName() && cellLoc->GetName()[0] != '\0')
                ? cellLoc->GetName()
                : (currentCellName != "UnnamedCell" ? currentCellName : "Home");

        auto& idx = SpatialWorldIndex::GetSingleton();
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        const auto playerHouses = HouseholdUseCases::GetSingleton().GetAllPlayerHouses();

        auto isRegisteredPlayerHouse = [&](RE::FormID candidateCellID, RE::BGSLocation* candidateLoc) {
            for (const RE::FormID phID : playerHouses) {
                if (phID != 0 && (phID == candidateCellID || (candidateLoc && IsLocationOrParentMatch(candidateLoc, phID)))) {
                    return true;
                }
            }
            return false;
        };

        auto matchesOriginalHomeForm = [&](RE::FormID homeFormID) -> bool {
            if (homeFormID == 0) return false;
            if (homeFormID == cellID || IsLocationOrParentMatch(cellLoc, homeFormID)) {
                return true;
            }
            if (auto* hCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(homeFormID)) {
                if (auto* hLoc = hCell->GetLocation(); hLoc && !IsSettlementLevelKeyword(hLoc)) {
                    if (IsLocationOrParentMatch(cellLoc, hLoc->GetFormID())) {
                        return true;
                    }
                }
            }
            return false;
        };

        // Resolve resident active main partners of the Player at this arrival location
        std::vector<Domain::Agency::HomeArrivalPartnerCandidate> residentCandidates;
        for (const auto& [partnerId, st] : snap->relationships->GetActivePartners(Foundation::ActorId::Player())) {
            if (!Foundation::IsMainTierStatus(st)) {
                continue;
            }
            if (snap->deceased && snap->deceased->IsDeceased(partnerId)) {
                continue;
            }

            RE::Actor* livePartner = idx.GetActorFromFormId(partnerId.raw());
            if (livePartner) {
                if (livePartner->IsDead() || livePartner->IsDeleted() || livePartner->IsDisabled()) continue;
                if (Utils::IsPlayerFollower(livePartner)) continue;
                if (livePartner->IsInCombat()) continue;
            }

            bool residesHere = false;
            const RE::FormID tenantHouseID =
                snap->household ? snap->household->GetTenantHouse(partnerId.raw()).raw() : 0;
            if (tenantHouseID != 0) {
                residesHere = (tenantHouseID == cellID || IsLocationOrParentMatch(cellLoc, tenantHouseID));
            } else {
                // 1. SpatialWorldIndex original interior home cell
                const RE::FormID homeCellID = idx.GetNpcHome(partnerId.raw());
                if (matchesOriginalHomeForm(homeCellID)) {
                    residesHere = true;
                }
                // 2. NPCProfile currentHome / originalHome / homeMarker
                if (!residesHere) {
                    if (const auto prof = relUseCases.GetNPCProfile(partnerId.raw())) {
                        if (prof->currentHome.has_value() && matchesOriginalHomeForm(*prof->currentHome)) {
                            residesHere = true;
                        } else if (prof->originalHome.has_value() && matchesOriginalHomeForm(*prof->originalHome)) {
                            residesHere = true;
                        } else if (prof->homeMarker.has_value() && *prof->homeMarker != 0) {
                            if (auto* markerRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(*prof->homeMarker)) {
                                auto* mCell = markerRef->GetParentCell();
                                auto* mLoc = markerRef->GetCurrentLocation();
                                if (!isRegisteredPlayerHouse(mCell ? mCell->GetFormID() : 0, mLoc)) {
                                    if (mCell && matchesOriginalHomeForm(mCell->GetFormID())) {
                                        residesHere = true;
                                    } else if (mLoc && !IsSettlementLevelKeyword(mLoc) &&
                                               IsLocationOrParentMatch(cellLoc, mLoc->GetFormID())) {
                                        residesHere = true;
                                    }
                                }
                            }
                        }
                    }
                }
                // 3. Static SpatialWorldIndex editorLocationID / placementCellID
                if (!residesHere) {
                    if (const auto entryOpt = idx.FindByFormID(partnerId.raw())) {
                        if (entryOpt->editorLocationID != 0) {
                            if (auto* edLoc = RE::TESForm::LookupByID<RE::BGSLocation>(entryOpt->editorLocationID);
                                edLoc && !IsSettlementLevelKeyword(edLoc) &&
                                IsLocationOrParentMatch(cellLoc, entryOpt->editorLocationID)) {
                                residesHere = true;
                            }
                        }
                        if (!residesHere && entryOpt->placementCellID != 0) {
                            if (auto* pCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(entryOpt->placementCellID);
                                pCell && pCell->IsInteriorCell() && matchesOriginalHomeForm(entryOpt->placementCellID)) {
                                residesHere = true;
                            }
                        }
                    }
                }
            }

            if (!residesHere) {
                continue;
            }

            Domain::Agency::HomeArrivalPartnerCandidate cand{};
            cand.partnerId = partnerId;
            cand.residesAtPlayerLocation = true;
            cand.isPartnerInCurrentCell = (livePartner && currentCell && livePartner->GetParentCell() == currentCell);
            cand.isBusyInScene = (livePartner && livePartner->GetCurrentScene() != nullptr);

            // Flag any live side lovers that are dead/disabled/deleted/follower/in-combat/in-scene
            auto checkLoverEligibility = [&](Foundation::ActorId lid) {
                if (lid.IsNull() || lid.IsPlayer()) return;
                if (RE::Actor* loverActor = idx.GetActorFromFormId(lid.raw())) {
                    if (loverActor->IsDead() || loverActor->IsDeleted() || loverActor->IsDisabled() ||
                        Utils::IsPlayerFollower(loverActor) || loverActor->IsInCombat() ||
                        loverActor->GetCurrentScene() != nullptr) {
                        cand.ineligibleSideLovers.push_back(lid);
                    }
                }
            };
            for (const auto& [otherId, otherSt] : snap->relationships->GetActivePartners(partnerId)) {
                if (otherSt == Foundation::RelationshipStatus::Lover) {
                    checkLoverEligibility(otherId);
                }
            }
            if (snap->affairs) {
                for (const auto& rec : snap->affairs->GetByCheater(partnerId)) {
                    if (rec.deceivedPartner.IsPlayer() && rec.affairStatus == Foundation::AffairStatus::Ongoing) {
                        checkLoverEligibility(rec.paramour);
                    }
                }
            }

            residentCandidates.push_back(std::move(cand));
        }

        if (residentCandidates.empty()) {
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();
        const float intentTTLDays = settings.GetPartnerIntentTTLDays();

        // Step 0: Lazy TTL expiry for RESIDENT partners only, WITH full consequences (0cebd663:4873-4906)
        for (const auto& cand : residentCandidates) {
            const auto intent = snap->relationships->GetPendingIntent(cand.partnerId);
            if (intent.type != Foundation::PartnerIntentType::None &&
                intent.createdGameDay >= 0.0f &&
                (currentGameDay - intent.createdGameDay) >= intentTTLDays) {
                if (intent.type == Foundation::PartnerIntentType::ConfessTalking) {
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.relationships.Write().ClearPendingIntent(cand.partnerId);
                        if (!intent.relatedLover.IsNull()) {
                            ws.affairs.Write().SetStage(
                                cand.partnerId,
                                Foundation::ActorId::Player(),
                                intent.relatedLover,
                                Foundation::AffairStage::Physical,
                                currentGameDay);
                        }
                    });
                } else if (intent.type == Foundation::PartnerIntentType::SuggestClose) {
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.relationships.Write().ClearPendingIntent(cand.partnerId);
                    });
                    const int curAff = relUseCases.GetPermanentAffection(cand.partnerId.raw());
                    relUseCases.SetPermanentAffection(cand.partnerId.raw(), std::clamp(curAff - 12, 0, 100), true);
                } else {
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.relationships.Write().ClearPendingIntent(cand.partnerId);
                    });
                }
            }
        }

        const auto refreshedSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!refreshedSnap || !spatialSnap) {
            return false;
        }

        const bool hasAnyTavern = HasAnyInteriorInnInWorld();
        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));

        const auto planOpt = Domain::Agency::EvaluatePartnerHomeArrival(
            *refreshedSnap,
            *spatialSnap,
            Foundation::LocationId{ locationKey },
            hasAnyTavern,
            GetCurrentHourOfDaySafe(),
            currentGameDay,
            rng,
            residentCandidates);
        if (!planOpt.has_value()) {
            return false;
        }

        auto plan = *planOpt;
        const RE::FormID partnerId = plan.partnerId.raw();
        const RE::FormID loverId = plan.relatedLoverId.raw();
        const std::string partnerName = ResolveActorNameForAgency(partnerId);
        const std::string loverName = loverId != 0 ? ResolveActorNameForAgency(loverId) : std::string();
        const bool isOpen = refreshedSnap->relationships->IsPairOpen(Foundation::ActorId::Player(), plan.partnerId);
        const bool isPriorityIntent = (plan.kind == Domain::Agency::HomeArrivalKind::PendingIntentForcegreet);

        // Priority 1: Pending forcegreet intent — do NOT burn home-arrival location cooldown!
        if (isPriorityIntent) {
            const std::string scenario = ResolveHomeArrivalScenarioId(plan, false);
            if (scenario.empty()) {
                return false;
            }
            const std::string stageStr =
                (plan.intentType == Foundation::PartnerIntentType::ConfessTalking)
                    ? "talking"
                    : ((plan.intentType == Foundation::PartnerIntentType::ConfessPhysical) ? "physical" : "none");

            nlohmann::json payload;
            payload["scenario"] = scenario;
            payload["partnerFormId"] = FormatFormId8Hex(partnerId);
            payload["partnerName"] = partnerName;
            payload["loverFormId"] = loverId != 0 ? FormatFormId8Hex(loverId) : std::string();
            payload["loverName"] = loverName;
            payload["affairStage"] = stageStr;
            payload["isOpenRelationship"] = isOpen;
            payload["intentType"] = std::string(Foundation::PartnerIntentTypeToString(plan.intentType));
            payload["targetLocationFormId"] = FormatFormId8Hex(locationKey);
            payload["targetLocationName"] = currentLocName;

            EmitModCallbackEvent(
                "maras_partner_home_arrival_event",
                payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                1.0f,
                partnerId);

            MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Dispatched Priority1 home-arrival intent '{}': partner={:08X}, lover={:08X}, loc={:08X}",
                           scenario, partnerId, loverId, locationKey);
            return true;
        }

        // Priority 2: Side-lover home-arrival encounter (in-house or letter/away)
        bool isLetterScenario =
            (plan.kind == Domain::Agency::HomeArrivalKind::OpenDateLetter ||
             plan.kind == Domain::Agency::HomeArrivalKind::CheatingTalkingLetter ||
             plan.kind == Domain::Agency::HomeArrivalKind::CheatingPhysicalLetter);

        const auto loverLocCtx = ResolveActorLocationContext(loverId, *refreshedSnap);
        TavernRendezvousTarget tavern{};
        if (isLetterScenario) {
            tavern = FindNearbyTavernForRendezvous(cellLoc, loverLocCtx);
            if (!tavern.found) {
                RE::Actor* partnerActor = idx.GetActorFromFormId(partnerId);
                const bool partnerInCell = (partnerActor && currentCell && partnerActor->GetParentCell() == currentCell);
                if (!partnerInCell) {
                    return false;
                }
                isLetterScenario = false;
            }
        }

        const std::string scenario = ResolveHomeArrivalScenarioId(plan, isLetterScenario);
        if (scenario.empty()) {
            return false;
        }

        // Burn per-location cooldown ONLY on Priority 2
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.cooldowns.Write().lastHomeArrivalEventDayByLoc[Foundation::LocationId{ locationKey }] = currentGameDay;
        });

        const std::string targetLocName =
            isLetterScenario
                ? tavern.displayName
                : (!loverLocCtx.subLocationName.empty()
                       ? loverLocCtx.subLocationName
                       : (!loverLocCtx.locationName.empty()
                              ? loverLocCtx.locationName
                              : ResolveHouseDisplayName(refreshedSnap->household
                                                            ? refreshedSnap->household->GetTenantHouse(partnerId).raw()
                                                            : 0)));
        const RE::FormID targetLocId =
            isLetterScenario
                ? tavern.innLocationID
                : (loverLocCtx.subLocationID != 0
                       ? loverLocCtx.subLocationID
                       : (loverLocCtx.settlementLocationID != 0 ? loverLocCtx.settlementLocationID : locationKey));
        const RE::FormID targetMarkerId = isLetterScenario ? tavern.centerMarkerID : 0;

        if (isLetterScenario) {
            const auto catalog = Engine::EncounterConfigLoader::GetSingleton().GetCatalog();
            const Domain::Agency::EncounterTemplateSpec* tmpl = catalog ? catalog->FindTemplate(scenario) : nullptr;
            const std::string playerName = GetPlayerNameSafe();

            Domain::Agency::RenderedEncounterModal rendered;
            if (tmpl) {
                rendered = Domain::Agency::RenderEncounterTemplate(
                    *tmpl, plan.partnerTemperament, partnerName, playerName, tavern.displayName, loverName);
            } else {
                MARAS_LOG_WARN("[AgencyWorkflowCoordinator] Encounter template '{}' missing from catalog; synthesizing minimal fallback modal",
                               scenario);
                rendered = SynthesizeMinimalEncounterFallback(scenario, partnerName, playerName, tavern.displayName);
            }

            ModalRequest req{};
            req.modalType = "generic";
            req.title = rendered.modalTitle;
            req.subtitle = rendered.modalSubtitle;
            req.message = rendered.envelopeMessage;
            req.tone = ParseModalTone(rendered.modalTone);
            req.size = ParseModalSize(rendered.modalSize);
            req.defaultCancelButtonIndex = rendered.defaultCancelButtonIndex;
            req.controls.push_back(ModalControl::MakeInfoBox(
                rendered.infoBoxId, rendered.infoBoxTitle, rendered.letterBody, req.tone));
            req.buttons.push_back(ModalButton::Make(
                rendered.buttonId.empty() ? std::string("close") : rendered.buttonId,
                rendered.buttonLabel.empty() ? Tr("modal.encounter_fallback.btn_close") : rendered.buttonLabel,
                ModalButtonVariant::Primary));
            GetModalGateway().ShowModal(std::move(req), nullptr);
        }

        const std::string stageStr =
            (plan.kind == Domain::Agency::HomeArrivalKind::CheatingTalkingLetter ||
             plan.kind == Domain::Agency::HomeArrivalKind::CheatingTalkingInHouse)
                ? "talking"
                : ((plan.kind == Domain::Agency::HomeArrivalKind::CheatingPhysicalLetter ||
                    plan.kind == Domain::Agency::HomeArrivalKind::CheatingPhysicalInHouse)
                       ? "physical"
                       : "none");

        nlohmann::json payload;
        payload["scenario"] = scenario;
        payload["partnerFormId"] = FormatFormId8Hex(partnerId);
        payload["partnerName"] = partnerName;
        payload["loverFormId"] = loverId != 0 ? FormatFormId8Hex(loverId) : std::string();
        payload["loverName"] = loverName;
        payload["affairStage"] = stageStr;
        payload["isOpenRelationship"] = isOpen;
        payload["intentType"] = "none";
        payload["targetLocationFormId"] = targetLocId != 0 ? FormatFormId8Hex(targetLocId) : std::string();
        payload["targetMarkerFormId"] = targetMarkerId != 0 ? FormatFormId8Hex(targetMarkerId) : std::string();
        payload["targetLocationName"] = targetLocName;

        EmitModCallbackEvent(
            "maras_partner_home_arrival_event",
            payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
            0.0f,
            partnerId);

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Dispatched Priority2 home-arrival event: scenario='{}', partner={:08X}, lover={:08X}, loc={:08X}",
                       scenario, partnerId, loverId, locationKey);
        return true;
    }

    // =========================================================================
    // NPC-NPC Banter Coordination
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledBanter(float currentGameDay) {
        if (!GetSkyrimNetGateway().IsAvailable()) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->enableScenes || IsBanterSceneOnCooldown(currentGameDay)) {
            return;
        }
        (void)TryTriggerBanter();
    }

    bool AgencyWorkflowCoordinator::TryTriggerBanter() {
        if (!GetSkyrimNetGateway().IsAvailable()) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->enableScenes) {
            return false;
        }
        const float currentDay = GetCurrentGameDaysSafe();
        if (IsBanterSceneOnCooldown(currentDay)) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerLoc = player ? player->GetCurrentLocation() : nullptr;
        if (!BanterSceneStarter::GetSingleton().IsLocationAllowedForScenes(playerLoc)) {
            return false;
        }

        const auto couple = FindBanterCoupleCandidates(snap->settings->skyrimnetActionRadius);
        if (couple.size() < 2 || !couple[0] || !couple[1]) {
            return false;
        }

        const RE::FormID idA = couple[0]->GetFormID();
        const RE::FormID idB = couple[1]->GetFormID();
        if (!IsSceneSettingEnabledForPair(*snap->settings, *snap, idA, idB)) {
            return false;
        }

        const bool partnerPresent = BanterSceneStarter::GetSingleton().IsBanterPartnerPresent(
            couple[1], snap->settings->skyrimnetActionRadius);
        nlohmann::json payload;
        payload["actorA"] = FormatFormId8Hex(idA);
        payload["actorB"] = FormatFormId8Hex(idB);
        payload["partnerPresent"] = partnerPresent;

        EmitModCallbackEvent(
            "maras_banter_scene_trigger",
            payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
            partnerPresent ? 1.0f : 0.0f,
            idA);

        RecordScenePlayed(couple[0], couple[1]);
        return true;
    }

    std::vector<RE::Actor*> AgencyWorkflowCoordinator::FindBanterCoupleCandidates(float scanRadius) const {
        return BanterSceneStarter::GetSingleton().FindBanterCoupleCandidates(scanRadius);
    }

    void AgencyWorkflowCoordinator::RecordScenePlayed(RE::Actor* actorA, RE::Actor* actorB) {
        const float currentDay = GetCurrentGameDaysSafe();
        const RE::FormID idA = actorA ? actorA->GetFormID() : 0;
        const RE::FormID idB = actorB ? actorB->GetFormID() : 0;

        State::WorldStore::GetSingleton().Modify([currentDay, idA, idB](State::WorldState& ws) {
            auto& cd = ws.cooldowns.Write();
            cd.lastBanterSceneDay = currentDay;
            if (idA != 0 && idB != 0) {
                cd.banterCoupleCooldowns[State::CanonicalPairKey(
                    Foundation::ActorId{ idA }, Foundation::ActorId{ idB })] = currentDay;
            }
        });

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Recorded banter scene played ({:08X}, {:08X}) on day {:.3f}",
                       idA, idB, currentDay);
    }

    bool AgencyWorkflowCoordinator::IsBanterSceneOnCooldown(float currentDay) const {
        const float cooldownHours = GetBanterCooldownHours();
        if (cooldownHours <= 0.0f) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float lastSceneDay = snap->cooldowns ? snap->cooldowns->lastBanterSceneDay : -1.0f;
        if (lastSceneDay < 0.0f || currentDay < lastSceneDay) {
            return false;
        }
        return (currentDay - lastSceneDay) < (cooldownHours / 24.0f);
    }

    bool AgencyWorkflowCoordinator::IsCoupleOnBanterCooldown(RE::FormID actorA,
                                                             RE::FormID actorB,
                                                             float currentDay) const {
        const float cooldownHours = GetBanterCooldownHours() * kBanterCoupleCooldownMultiplier;
        if (cooldownHours <= 0.0f) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->cooldowns) {
            return false;
        }
        const State::CanonicalPairKey key(Foundation::ActorId{ actorA }, Foundation::ActorId{ actorB });
        const auto it = snap->cooldowns->banterCoupleCooldowns.find(key);
        if (it == snap->cooldowns->banterCoupleCooldowns.end()) {
            return false;
        }
        if (currentDay < it->second) {
            return false;
        }
        return (currentDay - it->second) < (cooldownHours / 24.0f);
    }

    void AgencyWorkflowCoordinator::SetBanterCooldownHours(float hours) {
        const float clamped = std::max(0.0f, hours);
        State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
            ws.settings.Write().scenesCooldownHours = clamped;
            ws.cooldowns.Write().savedBanterCooldownHours = clamped;
        });
    }

    float AgencyWorkflowCoordinator::GetBanterCooldownHours() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->settings ? std::max(0.0f, snap->settings->scenesCooldownHours) : 24.0f;
    }

    void AgencyWorkflowCoordinator::Revert() {
        ClearRunningBatchState();
        MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] Reverted transient agency workflow state");
    }

}  // namespace MARAS::App
