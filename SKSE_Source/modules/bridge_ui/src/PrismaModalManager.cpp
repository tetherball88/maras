#include "maras/bridge_ui/PrismaModalManager.h"

#include "maras/app/AppContext.h"
#include "maras/bridge_ui/ConfirmationModal.h"
#include "maras/bridge_ui/PrismaUIManager.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <utility>
#include <vector>

namespace MARAS::Bridge::Ui {

    PrismaModalManager& PrismaModalManager::GetSingleton() {
        static PrismaModalManager instance;
        return instance;
    }

    std::string PrismaModalManager::ShowModal(ModalRequest request, ModalCallback callback) {
        std::string modalId = request.modalId;
        if (modalId.empty()) {
            modalId = std::format("modal_{}", nextId_.fetch_add(1, std::memory_order_relaxed));
        }

        const nlohmann::json spec = request.ToJson(modalId);
        std::string specJson = spec.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return EnqueueEntry(std::move(modalId), std::move(specJson), std::move(callback));
    }

    std::string PrismaModalManager::ShowRawJsonModal(
        const std::string& modalSpecJson,
        ModalCallback callback) {
        nlohmann::json parsed = nlohmann::json::parse(modalSpecJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            MARAS_LOG_ERROR("[PrismaModalManager] Rejected malformed raw modal JSON payload");
            if (callback) {
                ModalResponse errResp;
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

        std::string dumped = parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return EnqueueEntry(std::move(modalId), std::move(dumped), std::move(callback));
    }

    std::string PrismaModalManager::EnqueueEntry(
        std::string modalId,
        std::string specJson,
        ModalCallback callback) {
        ModalEntry entry;
        entry.modalId = modalId;
        entry.specJson = std::move(specJson);
        entry.callback = std::move(callback);
        entry.expectedEpoch = MARAS::State::GetSessionEpoch();

        bool pushedActive = false;
        std::size_t queueSize = 0;
        {
            std::lock_guard lock(mutex_);
            if (!activeModal_.has_value()) {
                activeModal_ = std::move(entry);
                lastSentModalId_.clear();
                pushedActive = true;
            } else {
                queue_.push_back(std::move(entry));
                queueSize = queue_.size();
            }
        }

        if (pushedActive) {
            MARAS_LOG_DEBUG("[PrismaModalManager] Activated modal '{}'", modalId);
        } else {
            MARAS_LOG_DEBUG("[PrismaModalManager] Queued modal '{}' (pending={})", modalId, queueSize);
        }

        ScheduleProcessQueue();
        return modalId;
    }

    void PrismaModalManager::ScheduleProcessQueue() {
        MARAS::AddCurrentEpochTask("PrismaModalManager::ProcessQueue", []() {
            PrismaModalManager::GetSingleton().ProcessQueueOnGameThread();
        });
    }

    bool PrismaModalManager::HasActiveModal() const {
        std::lock_guard lock(mutex_);
        return activeModal_.has_value();
    }

    std::string PrismaModalManager::GetActiveModalId() const {
        std::lock_guard lock(mutex_);
        return activeModal_ ? activeModal_->modalId : std::string{};
    }

    void PrismaModalManager::SendActiveModalToUI(bool forceResend) {
        std::string specJson;
        std::string modalId;
        {
            std::lock_guard lock(mutex_);
            if (!activeModal_.has_value()) {
                return;
            }
            if (!forceResend && lastSentModalId_ == activeModal_->modalId) {
                return;
            }
            modalId = activeModal_->modalId;
            specJson = activeModal_->specJson;
            lastSentModalId_ = modalId;
        }

        MARAS_LOG_DEBUG("[PrismaModalManager] Sending modal '{}' to UI (force={})", modalId, forceResend);
        PrismaUIManager::GetSingleton().SendToUI("showMARASModal", specJson);
    }

    void PrismaModalManager::ProcessQueueOnGameThread() {
        bool hasActive = false;
        std::string promotedId;
        {
            std::lock_guard lock(mutex_);
            if (!activeModal_.has_value() && !queue_.empty()) {
                activeModal_ = std::move(queue_.front());
                queue_.pop_front();
                lastSentModalId_.clear();
                promotedId = activeModal_->modalId;
            }
            hasActive = activeModal_.has_value();
        }

        if (!promotedId.empty()) {
            MARAS_LOG_DEBUG("[PrismaModalManager] Promoted queued modal '{}' to active", promotedId);
        }

        auto& uiMgr = PrismaUIManager::GetSingleton();
        if (hasActive && !uiMgr.IsModalDisplayBlocked()) {
            SendActiveModalToUI(false);
        }
        uiMgr.SyncViewVisibilityAndFocus();
    }

    bool PrismaModalManager::HandleModalResponse(const nlohmann::json& payload) {
        if (!payload.is_object()) {
            MARAS_LOG_WARN("[PrismaModalManager] Ignored non-object modalResponse payload");
            return false;
        }

        std::string incomingId;
        if (auto it = payload.find("modalId"); it != payload.end() && it->is_string()) {
            incomingId = it->get<std::string>();
        }
        if (incomingId.empty()) {
            MARAS_LOG_WARN("[PrismaModalManager] Ignored modalResponse missing valid string modalId");
            return false;
        }

        std::string buttonId;
        if (auto it = payload.find("buttonId"); it != payload.end() && it->is_string()) {
            buttonId = it->get<std::string>();
        }

        bool aborted = false;
        if (auto it = payload.find("aborted"); it != payload.end() && it->is_boolean()) {
            aborted = it->get<bool>();
        }

        nlohmann::json values = nlohmann::json::object();
        if (auto it = payload.find("values"); it != payload.end() && it->is_object()) {
            values = *it;
        }

        ModalEntry resolved;
        {
            std::lock_guard lock(mutex_);
            if (!activeModal_.has_value() || activeModal_->modalId != incomingId) {
                MARAS_LOG_WARN("[PrismaModalManager] Ignored modalResponse for inactive/mismatched modal '{}'",
                               incomingId);
                return false;
            }
            resolved = std::move(*activeModal_);
            activeModal_.reset();
            lastSentModalId_.clear();
        }

        MARAS_LOG_DEBUG("[PrismaModalManager] Resolved modal '{}' (button='{}', aborted={})",
                        resolved.modalId,
                        buttonId,
                        aborted);

        PrismaUIManager::GetSingleton().SendToUI("clearMARASModal", "");
        ScheduleProcessQueue();

        if (resolved.callback) {
            if (MARAS::State::IsSessionEpochCurrent(resolved.expectedEpoch)) {
                ModalResponse response;
                response.modalId = std::move(resolved.modalId);
                response.buttonId = std::move(buttonId);
                response.aborted = aborted;
                response.values = std::move(values);
                response.rawPayload = payload;
                try {
                    resolved.callback(response);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[PrismaModalManager] Exception in modal callback '{}': {}",
                                    response.modalId,
                                    e.what());
                } catch (...) {
                    MARAS_LOG_ERROR("[PrismaModalManager] Unknown exception in modal callback '{}'",
                                    response.modalId);
                }
            } else {
                MARAS_LOG_DEBUG(
                    "[PrismaModalManager] Dropped stale modal callback '{}' (expectedEpoch={}, currentEpoch={})",
                    resolved.modalId,
                    resolved.expectedEpoch,
                    MARAS::State::GetSessionEpoch());
            }
        }

        return true;
    }

    void PrismaModalManager::Revert() {
        std::vector<ModalEntry> cancelled;
        {
            std::lock_guard lock(mutex_);
            if (activeModal_.has_value()) {
                cancelled.push_back(std::move(*activeModal_));
                activeModal_.reset();
            }
            while (!queue_.empty()) {
                cancelled.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            lastSentModalId_.clear();
        }

        if (!cancelled.empty()) {
            MARAS_LOG_INFO("[PrismaModalManager] Reverting {} active/queued modal(s)", cancelled.size());
        }

        for (auto& entry : cancelled) {
            if (!entry.callback) {
                continue;
            }
            ModalResponse abortedResp;
            abortedResp.modalId = std::move(entry.modalId);
            abortedResp.aborted = true;
            try {
                entry.callback(abortedResp);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[PrismaModalManager] Exception aborting modal '{}' on revert: {}",
                                abortedResp.modalId,
                                e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[PrismaModalManager] Unknown exception aborting modal '{}' on revert",
                                abortedResp.modalId);
            }
        }

        auto& uiMgr = PrismaUIManager::GetSingleton();
        uiMgr.SendToUI("clearMARASModal", "");
        uiMgr.SyncViewVisibilityAndFocus();
    }

    namespace {

        class ModalGatewayImpl final : public MARAS::App::IModalGateway {
        public:
            std::string ShowModal(
                MARAS::App::ModalRequest request,
                MARAS::App::ModalCallback callback) override {
                return PrismaModalManager::GetSingleton().ShowModal(
                    std::move(request),
                    std::move(callback));
            }

            std::string ShowRawJsonModal(
                const std::string& modalSpecJson,
                MARAS::App::ModalCallback callback) override {
                return PrismaModalManager::GetSingleton().ShowRawJsonModal(
                    modalSpecJson,
                    std::move(callback));
            }

            bool HasActiveModal() const override {
                return PrismaModalManager::GetSingleton().HasActiveModal();
            }

            void SendActiveModalToUI(bool forceResend) override {
                auto& uiMgr = PrismaUIManager::GetSingleton();
                if (!uiMgr.IsModalDisplayBlocked()) {
                    PrismaModalManager::GetSingleton().SendActiveModalToUI(forceResend);
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
                PrismaModalManager::GetSingleton().Revert();
            }
        };

    }  // namespace

    void PrismaModalManager::RegisterAppGateway() noexcept {
        static ModalGatewayImpl instance;
        MARAS::App::SetModalGateway(&instance);
    }

}  // namespace MARAS::Bridge::Ui
