#include "maras/bridge_ui/ModalManager.h"

#include "maras/app/AppContext.h"
#include "maras/bridge_ui/ConfirmationModal.h"
#include "maras/bridge_ui/UIManager.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <utility>
#include <vector>

namespace MARAS::Bridge::Ui {

    ModalManager& ModalManager::GetSingleton() {
        static ModalManager instance;
        return instance;
    }

    std::string ModalManager::ShowModal(ModalRequest request, ModalCallback callback) {
        std::string modalId = request.modalId;
        if (modalId.empty()) {
            modalId = std::format("modal_{}", nextId_.fetch_add(1, std::memory_order_relaxed));
        }

        std::vector<std::string> buttonIds;
        buttonIds.reserve(request.buttons.size());
        for (const auto& btn : request.buttons) {
            buttonIds.push_back(btn.id);
        }

        const nlohmann::json spec = request.ToJson(modalId);
        std::string specJson = spec.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return EnqueueEntry(
            std::move(modalId),
            std::move(specJson),
            std::move(buttonIds),
            request.defaultCancelButtonIndex,
            std::move(callback));
    }

    std::string ModalManager::ShowRawJsonModal(
        const std::string& modalSpecJson,
        ModalCallback callback) {
        nlohmann::json parsed = nlohmann::json::parse(modalSpecJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            MARAS_LOG_ERROR("[ModalManager] Rejected malformed raw modal JSON payload");
            if (callback) {
                ModalResponse errResp;
                errResp.buttonIndex = 1;
                errResp.aborted = true;
                callback(errResp);
            }
            return {};
        }

        std::string modalId;
        if (auto it = parsed.find("modalId"); it != parsed.end() && it->is_string()) {
            modalId = it->get<std::string>();
        }
        if (modalId.empty()) {
            modalId = std::format("modal_{}", nextId_.fetch_add(1, std::memory_order_relaxed));
            parsed["modalId"] = modalId;
        }

        std::int32_t defaultCancelIndex = 1;
        if (auto it = parsed.find("defaultCancelButtonIndex");
            it != parsed.end() && it->is_number_integer()) {
            defaultCancelIndex = it->get<std::int32_t>();
        }

        std::vector<std::string> buttonIds;
        if (auto it = parsed.find("buttons"); it != parsed.end() && it->is_array()) {
            buttonIds.reserve(it->size());
            for (const auto& btn : *it) {
                if (btn.is_object()) {
                    if (auto idIt = btn.find("id"); idIt != btn.end() && idIt->is_string()) {
                        buttonIds.push_back(idIt->get<std::string>());
                    } else {
                        buttonIds.emplace_back();
                    }
                }
            }
        }

        std::string dumped = parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return EnqueueEntry(
            std::move(modalId),
            std::move(dumped),
            std::move(buttonIds),
            defaultCancelIndex,
            std::move(callback));
    }

    std::string ModalManager::EnqueueEntry(
        std::string modalId,
        std::string specJson,
        std::vector<std::string> buttonIds,
        std::int32_t defaultCancelButtonIndex,
        ModalCallback callback) {
        const std::uint64_t token = nextId_.fetch_add(1, std::memory_order_relaxed);
        MARAS::State::ModalQueueItem item;
        item.token = token;
        item.modalId = modalId;
        item.specJson = std::move(specJson);
        item.buttonIds = std::move(buttonIds);
        item.defaultCancelButtonIndex = defaultCancelButtonIndex;
        item.expectedEpoch = MARAS::State::GetSessionEpoch();

        MARAS::State::ModalEnqueueOutcome out{};
        {
            std::lock_guard lock(mutex_);
            if (callback) {
                callbacks_.emplace(token, std::move(callback));
            }
            out = queueState_.Enqueue(std::move(item));
        }

        if (out.activatedImmediately) {
            MARAS_LOG_DEBUG("[ModalManager] Activated modal '{}'", modalId);
        } else {
            MARAS_LOG_DEBUG("[ModalManager] Queued modal '{}' (pending={})", modalId, out.pendingQueueSize);
        }

        ScheduleProcessQueue();
        return modalId;
    }

    void ModalManager::ScheduleProcessQueue() {
        MARAS::AddCurrentEpochTask("ModalManager::ProcessQueue", []() {
            ModalManager::GetSingleton().ProcessQueueOnGameThread();
        });
    }

    bool ModalManager::HasActiveModal() const {
        std::lock_guard lock(mutex_);
        return queueState_.HasActiveOrQueued();
    }

    std::string ModalManager::GetActiveModalId() const {
        std::lock_guard lock(mutex_);
        return queueState_.GetActiveModalId();
    }

    void ModalManager::SendActiveModalToUI(bool forceResend) {
        auto& uiMgr = UIManager::GetSingleton();
        if (!uiMgr.IsInitialized() || !uiMgr.IsDomReady() || uiMgr.IsModalDisplayBlocked()) {
            return;
        }

        std::optional<MARAS::State::ModalSendPayload> payload;
        {
            std::lock_guard lock(mutex_);
            payload = queueState_.TakePayloadToSend(forceResend);
        }
        if (!payload.has_value()) {
            return;
        }

        MARAS_LOG_DEBUG("[ModalManager] Sending modal '{}' to UI (force={})", payload->modalId, forceResend);
        uiMgr.SendToUI("showMARASModal", payload->specJson);
    }

    void ModalManager::ProcessQueueOnGameThread() {
        auto& uiMgr = UIManager::GetSingleton();
        if (!uiMgr.IsInitialized()) {
            uiMgr.Initialize();
        }
        if (!uiMgr.IsInitialized()) {
            AbortAllModals("Magelight UI view unavailable");
            return;
        }

        MARAS::State::ModalPromoteOutcome outcome{};
        {
            std::lock_guard lock(mutex_);
            outcome = queueState_.PruneAndPromote(MARAS::State::GetSessionEpoch());
            for (const std::uint64_t staleToken : outcome.droppedStaleTokens) {
                callbacks_.erase(staleToken);
            }
        }

        if (!outcome.droppedStaleTokens.empty()) {
            MARAS_LOG_DEBUG("[ModalManager] Dropped {} stale modal(s) from prior epoch",
                            outcome.droppedStaleTokens.size());
        }
        if (!outcome.promotedModalId.empty()) {
            MARAS_LOG_DEBUG("[ModalManager] Promoted queued modal '{}' to active", outcome.promotedModalId);
        }

        if (outcome.hasActive && uiMgr.IsDomReady() && !uiMgr.IsModalDisplayBlocked()) {
            SendActiveModalToUI(false);
        }
        uiMgr.SyncViewVisibilityAndFocus();
    }

    bool ModalManager::HandleModalResponse(const nlohmann::json& payload) {
        if (!payload.is_object()) {
            MARAS_LOG_WARN("[ModalManager] Ignored non-object modalResponse payload");
            return false;
        }

        std::string incomingId;
        if (auto it = payload.find("modalId"); it != payload.end() && it->is_string()) {
            incomingId = it->get<std::string>();
        }
        if (incomingId.empty()) {
            MARAS_LOG_WARN("[ModalManager] Ignored modalResponse missing valid string modalId");
            return false;
        }

        std::string buttonId;
        if (auto it = payload.find("buttonId"); it != payload.end() && it->is_string()) {
            buttonId = it->get<std::string>();
        }

        std::int32_t buttonIndex = -1;
        if (auto it = payload.find("buttonIndex"); it != payload.end() && it->is_number_integer()) {
            buttonIndex = it->get<std::int32_t>();
        }

        bool aborted = false;
        if (auto it = payload.find("aborted"); it != payload.end() && it->is_boolean()) {
            aborted = it->get<bool>();
        }

        nlohmann::json values = nlohmann::json::object();
        if (auto it = payload.find("values"); it != payload.end() && it->is_object()) {
            values = *it;
        }

        MARAS::State::ModalResolveOutcome resolved{};
        ModalCallback resolvedCallback;
        {
            std::lock_guard lock(mutex_);
            resolved = queueState_.ResolveActive(
                incomingId, buttonId, buttonIndex, aborted, MARAS::State::GetSessionEpoch());
            if (!resolved.matched) {
                MARAS_LOG_WARN("[ModalManager] Ignored modalResponse for inactive/mismatched modal '{}'",
                               incomingId);
                return false;
            }
            if (const auto it = callbacks_.find(resolved.resolved.token); it != callbacks_.end()) {
                resolvedCallback = std::move(it->second);
                callbacks_.erase(it);
            }
            for (const std::uint64_t staleToken : resolved.droppedStaleTokens) {
                callbacks_.erase(staleToken);
            }
        }

        MARAS_LOG_DEBUG("[ModalManager] Resolved modal '{}' (button='{}', aborted={}, remaining={})",
                        resolved.resolved.modalId,
                        buttonId,
                        aborted,
                        resolved.hasMoreModals ? 1 : 0);

        auto& uiMgr = UIManager::GetSingleton();
        if (resolved.hasMoreModals) {
            MARAS_LOG_DEBUG("[ModalManager] Synchronously promoted queued modal '{}' to active",
                            resolved.promotedModalId);
            if (!uiMgr.IsInitialized()) {
                AbortAllModals("Magelight UI view unavailable");
            } else {
                if (uiMgr.IsDomReady() && !uiMgr.IsModalDisplayBlocked()) {
                    SendActiveModalToUI(false);
                }
                uiMgr.SyncViewVisibilityAndFocus();
            }
        } else {
            if (uiMgr.IsDomReady()) {
                uiMgr.SendToUI("clearMARASModal", "");
            }
            uiMgr.SyncViewVisibilityAndFocus();
        }

        if (resolvedCallback) {
            if (resolved.callbackEpochValid) {
                ModalResponse response;
                response.modalId = std::move(resolved.resolved.modalId);
                response.buttonId = std::move(buttonId);
                response.buttonIndex = resolved.resolvedButtonIndex;
                response.aborted = aborted;
                response.values = std::move(values);
                response.rawPayload = payload;
                try {
                    resolvedCallback(response);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[ModalManager] Exception in modal callback '{}': {}",
                                    response.modalId,
                                    e.what());
                } catch (...) {
                    MARAS_LOG_ERROR("[ModalManager] Unknown exception in modal callback '{}'",
                                    response.modalId);
                }
            } else {
                MARAS_LOG_DEBUG(
                    "[ModalManager] Dropped stale modal callback '{}' (expectedEpoch={}, currentEpoch={})",
                    resolved.resolved.modalId,
                    resolved.resolved.expectedEpoch,
                    MARAS::State::GetSessionEpoch());
            }
        }

        return true;
    }

    void ModalManager::AbortAllModals(const char* reason) {
        std::vector<std::pair<MARAS::State::ModalQueueItem, ModalCallback>> cancelled;
        {
            std::lock_guard lock(mutex_);
            auto items = queueState_.AbortAll();
            cancelled.reserve(items.size());
            for (auto& item : items) {
                ModalCallback cb;
                if (const auto it = callbacks_.find(item.token); it != callbacks_.end()) {
                    cb = std::move(it->second);
                    callbacks_.erase(it);
                }
                cancelled.emplace_back(std::move(item), std::move(cb));
            }
            callbacks_.clear();
        }

        if (cancelled.empty()) {
            return;
        }

        MARAS_LOG_WARN("[ModalManager] Aborting {} active/queued modal(s) ({})",
                       cancelled.size(),
                       reason ? reason : "unavailable");

        for (auto& [item, cb] : cancelled) {
            if (!cb) {
                continue;
            }
            ModalResponse abortedResp;
            abortedResp.modalId = std::move(item.modalId);
            abortedResp.buttonIndex = item.defaultCancelButtonIndex;
            abortedResp.aborted = true;
            try {
                cb(abortedResp);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[ModalManager] Exception aborting modal '{}': {}",
                                abortedResp.modalId,
                                e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[ModalManager] Unknown exception aborting modal '{}'",
                                abortedResp.modalId);
            }
        }
    }

    void ModalManager::Revert() {
        AbortAllModals("save revert");

        auto& uiMgr = UIManager::GetSingleton();
        if (uiMgr.IsDomReady()) {
            uiMgr.SendToUI("clearMARASModal", "");
        }
        uiMgr.SyncViewVisibilityAndFocus();
    }

    namespace {

        class ModalGatewayImpl final : public MARAS::App::IModalGateway {
        public:
            std::string ShowModal(
                MARAS::App::ModalRequest request,
                MARAS::App::ModalCallback callback) override {
                return ModalManager::GetSingleton().ShowModal(
                    std::move(request),
                    std::move(callback));
            }

            std::string ShowRawJsonModal(
                const std::string& modalSpecJson,
                MARAS::App::ModalCallback callback) override {
                return ModalManager::GetSingleton().ShowRawJsonModal(
                    modalSpecJson,
                    std::move(callback));
            }

            bool HasActiveModal() const override {
                return ModalManager::GetSingleton().HasActiveModal();
            }

            void SendActiveModalToUI(bool forceResend) override {
                auto& uiMgr = UIManager::GetSingleton();
                if (!uiMgr.IsModalDisplayBlocked()) {
                    ModalManager::GetSingleton().SendActiveModalToUI(forceResend);
                }
                uiMgr.SyncViewVisibilityAndFocus();
            }

            std::int32_t CheckAndSetConfirmationCooldown(
                const char* actionType,
                const std::vector<RE::Actor*>& actors) override {
                return ConfirmationModal::CheckAndSet(actionType, actors);
            }

            RE::BSScript::LatentStatus ShowConfirmationModal(
                RE::BSScript::Internal::VirtualMachine* vm,
                RE::VMStackID stackID,
                const char* actionType,
                const std::vector<RE::Actor*>& mainActors,
                const std::vector<RE::Actor*>& secondaryActors,
                const char* paramsJson) override {
                return ConfirmationModal::Show(
                    vm,
                    stackID,
                    actionType,
                    mainActors,
                    secondaryActors,
                    paramsJson);
            }

            void ShowConfirmationModalWithCallback(
                const char* actionType,
                const std::vector<RE::Actor*>& mainActors,
                const std::vector<RE::Actor*>& secondaryActors,
                const char* paramsJson,
                std::function<void(std::int32_t)> onResult) override {
                ConfirmationModal::ShowWithCallback(
                    actionType,
                    mainActors,
                    secondaryActors,
                    paramsJson,
                    std::move(onResult));
            }

            void Revert() override {
                ModalManager::GetSingleton().Revert();
            }
        };

    }  // namespace

    void ModalManager::RegisterAppGateway() noexcept {
        static ModalGatewayImpl instance;
        MARAS::App::SetModalGateway(&instance);
    }

}  // namespace MARAS::Bridge::Ui
