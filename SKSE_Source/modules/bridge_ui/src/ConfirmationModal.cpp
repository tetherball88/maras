#include "maras/bridge_ui/ConfirmationModal.h"

#include "maras/app/AppContext.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/bridge_ui/ModalManager.h"
#include "maras/bridge_ui/UIManager.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SessionEpoch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MARAS::Bridge::Ui {

    namespace {

        // Papyrus ShowConfirmationModal return codes:
        //   0 = Accepted (proceed)
        //   1 = Cancelled (decline)
        //   2 = Declined with explain (decline + SkyrimNet TriggerTextInput + maras_decline)
        //   3 = Blocked (on cooldown, invalid actors, or relationship state changed)
        //   4 = Disabled by settings (auto-accept)
        //   5 = UI unavailable (auto-accept)
        constexpr std::int32_t kResultAccept = 0;
        constexpr std::int32_t kResultCancel = 1;
        constexpr std::int32_t kResultDeclineExplain = 2;
        constexpr std::int32_t kResultBlocked = 3;
        constexpr std::int32_t kResultDisabledAutoAccept = 4;
        constexpr std::int32_t kResultUiUnavailableAutoAccept = 5;

        // CheckAndSetConfirmationCooldown return codes (`ConfirmationModal.h`):
        constexpr std::int32_t kCooldownEligible = 0;
        constexpr std::int32_t kCooldownBlocked = 1;

        struct CooldownRecord {
            std::chrono::steady_clock::time_point timestamp;
            MARAS::SessionEpoch epoch{ 0 };
        };

        struct ConfirmationCooldownState {
            std::mutex mutex;
            std::unordered_map<std::string, CooldownRecord> actionCooldowns;
            std::unordered_map<std::string, CooldownRecord> declineCooldowns;
        };

        ConfirmationCooldownState& GetConfirmationCooldownState() {
            static ConfirmationCooldownState instance;
            return instance;
        }

        [[nodiscard]] std::string NormalizeAction(const char* actionType) {
            if (!actionType) {
                return {};
            }
            return Foundation::StringUtils::ToLowerStd(actionType);
        }

        [[nodiscard]] std::vector<RE::FormID> ExtractValidFormIds(const std::vector<RE::Actor*>& actors) {
            std::vector<RE::FormID> ids;
            ids.reserve(actors.size());
            for (const RE::Actor* actor : actors) {
                if (!actor) {
                    continue;
                }
                const RE::FormID id = actor->GetFormID();
                if (id == 0) {
                    continue;
                }
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                    ids.push_back(id);
                }
            }
            return ids;
        }

        [[nodiscard]] std::string BuildCooldownKey(
            std::string_view normalizedAction,
            std::vector<RE::FormID> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

            std::string key(normalizedAction);
            key.push_back('|');
            for (std::size_t i = 0; i < ids.size(); ++i) {
                if (i > 0) {
                    key.push_back(',');
                }
                key.append(std::format("{:08X}", ids[i]));
            }
            return key;
        }

        void PruneStaleEpochEntries(
            std::unordered_map<std::string, CooldownRecord>& table,
            MARAS::SessionEpoch currentEpoch) {
            for (auto it = table.begin(); it != table.end();) {
                if (it->second.epoch != currentEpoch) {
                    it = table.erase(it);
                } else {
                    ++it;
                }
            }
        }

        [[nodiscard]] bool IsWithinWindow(
            std::unordered_map<std::string, CooldownRecord>& table,
            const std::string& key,
            std::int32_t seconds,
            MARAS::SessionEpoch currentEpoch,
            std::chrono::steady_clock::time_point now) {
            if (seconds <= 0) {
                return false;
            }
            const auto it = table.find(key);
            if (it == table.end()) {
                return false;
            }
            if (it->second.epoch != currentEpoch) {
                table.erase(it);
                return false;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
            if (elapsed >= seconds) {
                table.erase(it);
                return false;
            }
            return true;
        }

        void RecordCooldownTimestamp(
            std::unordered_map<std::string, CooldownRecord> ConfirmationCooldownState::* field,
            const std::string& key) {
            const auto epoch = MARAS::GetSessionEpoch();
            const auto now = std::chrono::steady_clock::now();
            auto& state = GetConfirmationCooldownState();
            std::lock_guard lock(state.mutex);
            auto& table = state.*field;
            PruneStaleEpochEntries(table, epoch);
            table[key] = CooldownRecord{ now, epoch };
        }

        [[nodiscard]] bool IsConfirmationEnabledForAction(
            const State::SettingsData& s,
            std::string_view action,
            bool involvesPlayer) {
            if (!s.enableConfirmationModal) {
                return false;
            }
            if (involvesPlayer && !s.enableConfirmPlayerNpc) {
                return false;
            }
            if (!involvesPlayer && !s.enableConfirmNpcNpc) {
                return false;
            }

            if (action == "dating") {
                return s.confirmStatusChange_Dating;
            }
            if (action == "engaged" || action == "engagement") {
                return s.confirmStatusChange_Engagement;
            }
            if (action == "lover") {
                return s.confirmStatusChange_Lover;
            }
            if (action == "married" || action == "marry") {
                return s.confirmStatusChange_Marry;
            }
            if (action == "exdating" || action == "breakup_dating") {
                return s.confirmBreakup_Dating;
            }
            if (action == "jilted" || action == "breakup_engagement") {
                return s.confirmBreakup_Engagement;
            }
            if (action == "exlover" || action == "breakup_lover") {
                return s.confirmBreakup_Lover;
            }
            if (action == "divorced" || action == "divorce") {
                return s.confirmDivorce;
            }
            if (action == "low_affection_divorce") {
                return s.confirmDivorceDuringLowAffectionQuest;
            }
            if (action == "open_relationship") {
                return s.confirmOpenRelationship;
            }
            if (action == "close_relationship") {
                return s.confirmCloseRelationship;
            }
            return true;
        }

        struct ActionDisplayMeta {
            std::string title;
            std::string label;
            ModalTone tone{ ModalTone::Default };
            bool isOpenToggle{ false };
        };

        [[nodiscard]] ActionDisplayMeta DescribeAction(
            std::string_view action,
            [[maybe_unused]] const std::string& actorAName,
            [[maybe_unused]] const std::string& actorBName) {
            if (action == "dating") {
                return { MARAS::Tr("modal.confirm.title_dating"), MARAS::Tr("modal.confirm.label_dating"), ModalTone::Default, false };
            }
            if (action == "engaged" || action == "engagement") {
                return { MARAS::Tr("modal.confirm.title_engaged"), MARAS::Tr("modal.confirm.label_engaged"), ModalTone::Default, false };
            }
            if (action == "lover") {
                return { MARAS::Tr("modal.confirm.title_lover"), MARAS::Tr("modal.confirm.label_lover"), ModalTone::Warning, false };
            }
            if (action == "married" || action == "marry") {
                return { MARAS::Tr("modal.confirm.title_married"), MARAS::Tr("modal.confirm.label_married"), ModalTone::Success, false };
            }
            if (action == "exdating" || action == "breakup_dating") {
                return { MARAS::Tr("modal.confirm.title_exdating"), MARAS::Tr("modal.confirm.label_exdating"), ModalTone::Warning, false };
            }
            if (action == "jilted" || action == "breakup_engagement") {
                return { MARAS::Tr("modal.confirm.title_jilted"), MARAS::Tr("modal.confirm.label_jilted"), ModalTone::Warning, false };
            }
            if (action == "exlover" || action == "breakup_lover") {
                return { MARAS::Tr("modal.confirm.title_exlover"), MARAS::Tr("modal.confirm.label_exlover"), ModalTone::Warning, false };
            }
            if (action == "divorced" || action == "divorce") {
                return { MARAS::Tr("modal.confirm.title_divorced"), MARAS::Tr("modal.confirm.label_divorced"), ModalTone::Danger, false };
            }
            if (action == "low_affection_divorce") {
                return { MARAS::Tr("modal.confirm.title_low_affection_divorce"), MARAS::Tr("modal.confirm.label_low_affection_divorce"), ModalTone::Danger, false };
            }
            if (action == "open_relationship") {
                return { MARAS::Tr("modal.confirm.title_open_relationship"), MARAS::Tr("modal.confirm.label_open_relationship"), ModalTone::Warning, true };
            }
            if (action == "close_relationship") {
                return { MARAS::Tr("modal.confirm.title_close_relationship"), MARAS::Tr("modal.confirm.label_close_relationship"), ModalTone::Default, true };
            }
            return {
                MARAS::TrFmt("modal.confirm.title_fallback", { { "action", action } }),
                std::string(action),
                ModalTone::Default,
                false
            };
        }

        [[nodiscard]] std::string BuildDefaultMessage(
            std::string_view action,
            std::string_view actionLabel,
            const std::string& actorAName,
            const std::string& actorBName) {
            if (action == "low_affection_divorce") {
                return MARAS::TrFmt(
                    "modal.confirm.msg_low_affection_divorce",
                    { { "actorA", actorAName }, { "actorB", actorBName } });
            }
            if (action == "open_relationship") {
                return MARAS::TrFmt(
                    "modal.confirm.msg_open_relationship",
                    { { "actorA", actorAName }, { "actorB", actorBName } });
            }
            if (action == "close_relationship") {
                return MARAS::TrFmt(
                    "modal.confirm.msg_close_relationship",
                    { { "actorA", actorAName }, { "actorB", actorBName } });
            }
            if (action == "exdating" || action == "breakup_dating" ||
                action == "jilted" || action == "breakup_engagement" ||
                action == "exlover" || action == "breakup_lover" ||
                action == "divorced" || action == "divorce") {
                return MARAS::TrFmt(
                    "modal.confirm.msg_breakup",
                    { { "actorA", actorAName }, { "action", actionLabel }, { "actorB", actorBName } });
            }
            return MARAS::TrFmt(
                "modal.confirm.msg_enter",
                { { "actorA", actorAName }, { "actorB", actorBName }, { "action", actionLabel } });
        }

        void ResolveLatentOnGameThread(
            RE::BSScript::Internal::VirtualMachine* vm,
            RE::VMStackID stackID,
            MARAS::SessionEpoch expectedEpoch,
            std::int32_t result) {
            if (!vm) {
                return;
            }
            if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                MARAS_LOG_DEBUG(
                    "[ConfirmationModal] Dropped stale latent modal result (expectedEpoch={}, currentEpoch={})",
                    expectedEpoch,
                    MARAS::GetSessionEpoch());
                return;
            }
            MARAS::DispatchLatentResult(expectedEpoch, vm, stackID, result, "ConfirmationModal");
        }

    }  // namespace

    bool ConfirmationModal::IsOnCooldown(const char* actionType, const std::vector<RE::Actor*>& actors) {
        const std::string action = NormalizeAction(actionType);
        const std::vector<RE::FormID> ids = ExtractValidFormIds(actors);
        const std::string key = BuildCooldownKey(action, ids);

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        const std::int32_t actionSecs = settingsSnap ? settingsSnap->actionsCooldown : 10;
        const std::int32_t declineSecs = settingsSnap ? settingsSnap->actionsDeclineCooldown : 60;

        const auto epoch = MARAS::GetSessionEpoch();
        const auto now = std::chrono::steady_clock::now();

        auto& state = GetConfirmationCooldownState();
        std::lock_guard lock(state.mutex);
        PruneStaleEpochEntries(state.actionCooldowns, epoch);
        PruneStaleEpochEntries(state.declineCooldowns, epoch);
        return IsWithinWindow(state.actionCooldowns, key, actionSecs, epoch, now) ||
               IsWithinWindow(state.declineCooldowns, key, declineSecs, epoch, now);
    }

    std::int32_t ConfirmationModal::CheckAndSet(const char* actionType, const std::vector<RE::Actor*>& actors) {
        const std::string action = NormalizeAction(actionType);
        const std::vector<RE::FormID> ids = ExtractValidFormIds(actors);
        const std::string key = BuildCooldownKey(action, ids);

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        const std::int32_t actionSecs = settingsSnap ? settingsSnap->actionsCooldown : 10;
        const std::int32_t declineSecs = settingsSnap ? settingsSnap->actionsDeclineCooldown : 60;

        const auto epoch = MARAS::GetSessionEpoch();
        const auto now = std::chrono::steady_clock::now();

        auto& state = GetConfirmationCooldownState();
        std::lock_guard lock(state.mutex);
        PruneStaleEpochEntries(state.actionCooldowns, epoch);
        PruneStaleEpochEntries(state.declineCooldowns, epoch);
        if (IsWithinWindow(state.actionCooldowns, key, actionSecs, epoch, now) ||
            IsWithinWindow(state.declineCooldowns, key, declineSecs, epoch, now)) {
            return kCooldownBlocked;
        }
        state.actionCooldowns[key] = CooldownRecord{ now, epoch };
        return kCooldownEligible;
    }

    void ConfirmationModal::ShowWithCallback(
        const char* actionType,
        const std::vector<RE::Actor*>& mainActors,
        const std::vector<RE::Actor*>& secondaryActors,
        const char* paramsJson,
        std::function<void(std::int32_t)> onResult) {
        const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
        const std::string action = NormalizeAction(actionType);

        std::vector<RE::FormID> mainIds = ExtractValidFormIds(mainActors);
        std::vector<RE::FormID> secondaryIds = ExtractValidFormIds(secondaryActors);
        std::vector<RE::FormID> allIds = mainIds;
        for (const RE::FormID id : secondaryIds) {
            if (std::find(allIds.begin(), allIds.end(), id) == allIds.end()) {
                allIds.push_back(id);
            }
        }

        if (mainIds.empty()) {
            MARAS_LOG_WARN("[ConfirmationModal] Missing main actor(s) for action '{}'; returning blocked", action);
            if (onResult) {
                onResult(kResultBlocked);
            }
            return;
        }

        const RE::FormID actorAId = mainIds.front();
        const RE::FormID actorBId = !secondaryIds.empty()
                                        ? secondaryIds.front()
                                        : (mainIds.size() > 1 ? mainIds[1] : MARAS::kPlayerFormID);

        const bool involvesPlayer =
            std::any_of(allIds.begin(), allIds.end(), [](RE::FormID id) { return id == MARAS::kPlayerFormID; });

        nlohmann::json paramsObj = nlohmann::json::object();
        if (paramsJson && *paramsJson) {
            auto parsed = nlohmann::json::parse(paramsJson, nullptr, false);
            if (parsed.is_object()) {
                paramsObj = std::move(parsed);
            }
        }

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        const std::string cooldownKey = BuildCooldownKey(action, allIds);
        {
            const std::int32_t actionSecs = settingsSnap ? settingsSnap->actionsCooldown : 10;
            const std::int32_t declineSecs = settingsSnap ? settingsSnap->actionsDeclineCooldown : 60;
            const auto now = std::chrono::steady_clock::now();

            auto& state = GetConfirmationCooldownState();
            std::lock_guard lock(state.mutex);
            PruneStaleEpochEntries(state.actionCooldowns, expectedEpoch);
            PruneStaleEpochEntries(state.declineCooldowns, expectedEpoch);
            if (IsWithinWindow(state.actionCooldowns, cooldownKey, actionSecs, expectedEpoch, now) ||
                IsWithinWindow(state.declineCooldowns, cooldownKey, declineSecs, expectedEpoch, now)) {
                MARAS_LOG_DEBUG("[ConfirmationModal] Action '{}' blocked by active cooldown for key '{}'",
                                action,
                                cooldownKey);
                if (onResult) {
                    onResult(kResultBlocked);
                }
                return;
            }
        }

        const bool skipModalParam =
            paramsObj.contains("skipModal") && paramsObj["skipModal"].is_boolean() && paramsObj["skipModal"].get<bool>();
        if (skipModalParam || (settingsSnap && !IsConfirmationEnabledForAction(*settingsSnap, action, involvesPlayer))) {
            MARAS_LOG_DEBUG("[ConfirmationModal] Action '{}' disabled by settings/param; auto-accepting (4)", action);
            RecordCooldownTimestamp(&ConfirmationCooldownState::actionCooldowns, cooldownKey);
            if (onResult) {
                onResult(kResultDisabledAutoAccept);
            }
            return;
        }

        if (!UIManager::GetSingleton().IsInitialized()) {
            MARAS_LOG_WARN("[ConfirmationModal] UI unavailable for '{}'; auto-accepting (5)", action);
            RecordCooldownTimestamp(&ConfirmationCooldownState::actionCooldowns, cooldownKey);
            if (onResult) {
                onResult(kResultUiUnavailableAutoAccept);
            }
            return;
        }

        Foundation::RelationshipStatus initialStatus = Foundation::RelationshipStatus::Candidate;
        bool initialIsOpen = false;
        {
            std::lock_guard lock(MARAS::GetPapyrusMutex());
            const auto& relSvc = App::RelationshipUseCases::GetSingleton();
            initialStatus = relSvc.GetRelationshipStatus(actorAId, actorBId);
            initialIsOpen = relSvc.GetPairIsOpen(actorAId, actorBId);
        }

        const std::string actorAName = ActorSvc::GetName(actorAId);
        const std::string actorBName = ActorSvc::GetName(actorBId);
        const ActionDisplayMeta meta = DescribeAction(action, actorAName, actorBName);

        std::string activity;
        std::string details;
        if (auto it = paramsObj.find("activity"); it != paramsObj.end() && it->is_string()) {
            activity = it->get<std::string>();
        }
        if (auto it = paramsObj.find("details"); it != paramsObj.end() && it->is_string()) {
            details = it->get<std::string>();
        }

        ModalRequest req;
        req.modalType = "generic";
        req.title = meta.title;
        req.subtitle = MARAS::TrFmt("modal.common.pair_subtitle", { { "actorA", actorAName }, { "actorB", actorBName } });
        req.tone = meta.tone;
        req.size = ModalSize::Sm;
        req.message = BuildDefaultMessage(action, meta.label, actorAName, actorBName);

        if (auto it = paramsObj.find("title"); it != paramsObj.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
            req.title = it->get<std::string>();
        }
        if (auto it = paramsObj.find("subtitle"); it != paramsObj.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
            req.subtitle = it->get<std::string>();
        }
        if (auto it = paramsObj.find("message"); it != paramsObj.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
            req.message = it->get<std::string>();
        } else {
            if (!activity.empty()) {
                req.message.append("\nActivity: ");
                req.message.append(activity);
            }
            if (!details.empty()) {
                req.message.append("\nDetails: ");
                req.message.append(details);
            }
            if (auto itReason = paramsObj.find("reason");
                itReason != paramsObj.end() && itReason->is_string() && !itReason->get_ref<const std::string&>().empty()) {
                req.message.append(MARAS::TrFmt("modal.common.reason_suffix", { { "reason", itReason->get<std::string>() } }));
            }
        }

        const bool skyrimNetAvailable = App::GetSkyrimNetGateway().IsAvailable();
        req.buttons.push_back(ModalButton::Make("accept", MARAS::Tr("modal.common.accept"), ModalButtonVariant::Primary));
        if (skyrimNetAvailable) {
            req.buttons.push_back(ModalButton::Make("decline_explain", MARAS::Tr("modal.common.decline_explain"), ModalButtonVariant::Outline));
        }
        req.buttons.push_back(ModalButton::Make("cancel", MARAS::Tr("modal.common.cancel"), ModalButtonVariant::Secondary));
        req.defaultCancelButtonIndex = skyrimNetAvailable ? 2 : 1;

        MARAS_LOG_DEBUG(
            "[ConfirmationModal] Showing modal '{}' for {:08X} ({}) & {:08X} ({}) [SkyrimNet={}]",
            action,
            actorAId,
            actorAName,
            actorBId,
            actorBName,
            skyrimNetAvailable);

        auto dispatchOnMainThread = [expectedEpoch, sharedCb = std::make_shared<std::function<void(std::int32_t)>>(std::move(onResult))](std::int32_t code) mutable {
            if (!sharedCb || !*sharedCb) {
                return;
            }
            if (!MARAS::AddEpochTask(
                    expectedEpoch,
                    "ConfirmationModal::OnResult",
                    [sharedCb, code]() {
                        if (*sharedCb) {
                            (*sharedCb)(code);
                        }
                    })) {
                if (MARAS::IsSessionEpochCurrent(expectedEpoch) &&
                    MARAS::DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                    MARAS_LOG_WARN("[ConfirmationModal] AddEpochTask unavailable on main thread; invoking cancel fallback (origCode={})", code);
                    (*sharedCb)(kResultCancel);
                } else {
                    MARAS_LOG_ERROR("[ConfirmationModal] Failed to dispatch confirmation callback (code={})", code);
                }
            }
        };

        ModalManager::GetSingleton().ShowModal(
            std::move(req),
            [expectedEpoch,
             action,
             metaLabel = meta.label,
             activity = std::move(activity),
             isOpenToggle = meta.isOpenToggle,
             cooldownKey,
             actorAId,
             actorBId,
             actorAName,
             actorBName,
             initialStatus,
             initialIsOpen,
             dispatchOnMainThread = std::move(dispatchOnMainThread)](const ModalResponse& resp) mutable {
                if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                    MARAS_LOG_DEBUG("[ConfirmationModal] Dropped stale response for '{}' (epoch mismatch)", action);
                    return;
                }

                if (resp.aborted) {
                    RecordCooldownTimestamp(&ConfirmationCooldownState::declineCooldowns, cooldownKey);
                    dispatchOnMainThread(kResultCancel);
                    return;
                }

                if (resp.buttonId == "accept") {
                    bool stateChanged = false;
                    {
                        std::lock_guard lock(MARAS::GetPapyrusMutex());
                        const auto& relSvc = App::RelationshipUseCases::GetSingleton();
                        if (isOpenToggle) {
                            stateChanged = (relSvc.GetPairIsOpen(actorAId, actorBId) != initialIsOpen);
                        } else {
                            stateChanged = (relSvc.GetRelationshipStatus(actorAId, actorBId) != initialStatus);
                        }
                    }

                    if (stateChanged) {
                        MARAS_LOG_WARN(
                            "[ConfirmationModal] Pair {:08X}/{:08X} state changed while modal '{}' was open; blocking",
                            actorAId,
                            actorBId,
                            action);
                        dispatchOnMainThread(kResultBlocked);
                        return;
                    }

                    RecordCooldownTimestamp(&ConfirmationCooldownState::actionCooldowns, cooldownKey);
                    dispatchOnMainThread(kResultAccept);
                    return;
                }

                if (resp.buttonId == "decline_explain") {
                    RecordCooldownTimestamp(&ConfirmationCooldownState::declineCooldowns, cooldownKey);

                    if (App::GetSkyrimNetGateway().IsAvailable()) {
                        const RE::FormID speakerId = (actorAId != MARAS::kPlayerFormID) ? actorAId : actorBId;
                        const RE::FormID targetId = (actorAId != speakerId) ? actorAId : actorBId;
                        const std::string& declineLabel = !activity.empty() ? activity : metaLabel;
                        const std::string msg = std::format(
                            "The player declined {} involving {} and {} and wishes to explain.",
                            declineLabel,
                            actorAName,
                            actorBName);
                        App::GetSkyrimNetGateway().EmitActionDecline(
                            speakerId,
                            targetId,
                            actorAId,
                            actorBId,
                            action,
                            msg,
                            /*skipTrigger=*/true);

                        if (UIManager::GetSingleton().IsVisible()) {
                            UIManager::GetSingleton().Hide();
                        }
                        UIManager::GetSingleton().RunAfterFocusReleased([expectedEpoch]() {
                            MARAS::DispatchPostLock(
                                expectedEpoch,
                                []() {
                                    auto* innerVm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                                    if (!innerVm) {
                                        MARAS_LOG_WARN(
                                            "[ConfirmationModal] VirtualMachine unavailable for TriggerTextInput");
                                        return;
                                    }
                                    auto args = RE::MakeFunctionArguments();
                                    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
                                    if (!innerVm->DispatchStaticCall(
                                            "SkyrimNetApi", "TriggerTextInput", args, cb)) {
                                        MARAS_LOG_WARN(
                                            "[ConfirmationModal] Failed to dispatch SkyrimNetApi::TriggerTextInput");
                                    }
                                },
                                "ConfirmationModal::FireTriggerTextInput");
                        });
                    }

                    dispatchOnMainThread(kResultDeclineExplain);
                    return;
                }

                // Cancel or fallback button
                RecordCooldownTimestamp(&ConfirmationCooldownState::declineCooldowns, cooldownKey);
                dispatchOnMainThread(kResultCancel);
            });
    }

    RE::BSScript::LatentStatus ConfirmationModal::Show(
        RE::BSScript::Internal::VirtualMachine* vm,
        RE::VMStackID stackID,
        const char* actionType,
        const std::vector<RE::Actor*>& mainActors,
        const std::vector<RE::Actor*>& secondaryActors,
        const char* paramsJson) {
        if (!vm || !SKSE::GetTaskInterface()) {
            MARAS_LOG_WARN("[ConfirmationModal] VirtualMachine or SKSE TaskInterface unavailable for ConfirmationModal::Show");
            return RE::BSScript::LatentStatus::kFailed;
        }

        const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
        ShowWithCallback(
            actionType,
            mainActors,
            secondaryActors,
            paramsJson,
            [vm, stackID, expectedEpoch](std::int32_t code) {
                ResolveLatentOnGameThread(vm, stackID, expectedEpoch, code);
            });
        return RE::BSScript::LatentStatus::kStarted;
    }

}  // namespace MARAS::Bridge::Ui
