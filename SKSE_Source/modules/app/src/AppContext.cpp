#include "maras/app/AppContext.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <atomic>
#include <mutex>

namespace MARAS::App {

    namespace {

        class NullUiBridgeGateway final : public IUiBridgeGateway {
        public:
            void InitializeDashboard() override {}
            void StartListeningInput() override {}
            void OnNewGameStarted(bool) override {}
            void ResetNewGameDeferral() override {}
            void DestroyDashboard() override {}
            void ShowDashboard() override {}
            void HideDashboard() override {}
            bool IsDashboardVisible() const override { return false; }
            bool IsDashboardOpen() const override { return false; }
            void Revert() override {}
            void SendOverviewData() override {}
            void SendSettingsData() override {}
            void SendNPCList() override {}
            void SendNPCDetail(std::uint32_t) override {}
            void SendHouseholdBuffs() override {}
            void SendMatchmakingResults(const std::vector<MatchmakingResult>&, bool) override {}
            void SendGeneratedMemories(std::uint32_t, const nlohmann::json&) override {}
            void OnAffectionChanged(std::uint32_t, int) override {}
            void OnStatusChanged(std::uint32_t, std::uint32_t, std::uint8_t) override {}
            void OnHierarchyChanged() override {}
            void OnChildUpdated(const State::ChildRecord&) override {}
            void OnChildRemoved(std::uint32_t) override {}
            void OnChildPromoted(std::uint32_t, std::uint32_t) override {}
        };

        class NullModalGateway final : public IModalGateway {
        public:
            std::string ShowModal(ModalRequest, ModalCallback callback) override {
                if (callback) {
                    ModalResponse resp;
                    resp.aborted = true;
                    callback(resp);
                }
                return {};
            }
            std::string ShowRawJsonModal(const std::string&, ModalCallback callback) override {
                if (callback) {
                    ModalResponse resp;
                    resp.aborted = true;
                    callback(resp);
                }
                return {};
            }
            bool HasActiveModal() const override { return false; }
            void SendActiveModalToUI(bool) override {}
            std::int32_t CheckAndSetConfirmationCooldown(const char*, const std::vector<RE::Actor*>&) override {
                return 0;
            }
            RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                             RE::VMStackID stackID,
                                                             const char*,
                                                             const std::vector<RE::Actor*>&,
                                                             const std::vector<RE::Actor*>&,
                                                             const char*) override {
                if (vm) {
                    DispatchLatentResult(GetSessionEpoch(), vm, stackID, 5, "NullModalConfirmation");
                }
                return RE::BSScript::LatentStatus::kStarted;
            }
            void Revert() override {}
        };

        class NullSkyrimNetGateway final : public ISkyrimNetGateway {
        public:
            bool IsAvailable() const override { return false; }
            bool ShouldUseDecisions() const override { return false; }
            bool ShouldTriggerNarration(RE::Actor*, RE::Actor*) const override { return false; }
            bool RegisterEvent(const std::string&, const std::string&, std::uint32_t, std::uint32_t,
                               bool, const std::vector<std::uint32_t>&) override { return false; }
            void RegisterDecorators() override {}
            bool EvaluatePaternitySecret(std::uint32_t) override { return false; }
            bool SendCustomPromptToLLM(const std::string&, const std::string&, const std::string&,
                                       std::function<void(const char*, int)> callback) override {
                if (callback) callback("", 0);
                return false;
            }
            bool EvaluateStepIn(const std::string&, std::function<void(const StepInEvalResult&)> callback) override {
                if (callback) callback(StepInEvalResult{});
                return false;
            }
            bool EvaluatePaternity(const std::string&, bool, std::function<void(const PaternityEvalResult&)> callback) override {
                if (callback) callback(PaternityEvalResult{});
                return false;
            }
            bool EvaluatePartnerAgency(const std::string&, const std::string&, bool, const std::string&,
                                       const std::string&, const std::string&,
                                       std::function<void(const PartnerAgencyEvalResult&)> callback) override {
                if (callback) callback(PartnerAgencyEvalResult{});
                return false;
            }
            bool EvaluateMatchmakingVerify(const std::string&, const std::string&, const std::string&,
                                           const std::string&, const std::string&,
                                           std::function<void(const MatchmakingVerifyEvalResult&)> callback) override {
                if (callback) callback(MatchmakingVerifyEvalResult{});
                return false;
            }
            std::string SanitizeSingleLine(std::string_view text) const override { return std::string(text); }
            std::string SanitizeKey(std::string_view rawKey) const override { return std::string(rawKey); }
            std::string GetLLMFailureGuidanceTitle(LLMFailureType) const override { return "LLM Request Failed"; }
            std::string GetLLMFailureGuidance(LLMFailureType, std::string_view) const override { return {}; }
            std::string GetLLMBatchFailureGuidance(bool, bool, std::string_view) const override { return {}; }
            void TickMemories() override {}
            void RevertMemories() override {}
            bool IsMemoriesSuppressed() const override { return false; }
            void PushSuppressMemories() override {}
            void PopSuppressMemories() override {}
            bool IsMatchmakingApplyActive() const override { return false; }
            void PushMatchmakingApply() override {}
            void PopMatchmakingApply() override {}
            void RecordRelationshipStatusChange(std::uint32_t, std::uint32_t,
                                                Foundation::RelationshipStatus, Foundation::RelationshipStatus) override {}
            void RecordOpenRelationshipToggle(std::uint32_t, std::uint32_t, bool) override {}
            void RecordInfidelityEvent(std::uint32_t, std::uint32_t, std::uint32_t,
                                       const std::string&, bool) override {}
            void RecordPregnancyDiscovered(std::uint32_t, std::uint32_t, std::uint32_t,
                                           const std::string&, std::int32_t) override {}
            void RecordPaternityReveal(std::uint32_t, const std::string&, const std::string&, bool) override {}
            void RecordChildBirth(std::uint32_t, const std::vector<std::uint32_t>&) override {}
            void RecordChildCustodyEvent(const std::string&, std::uint32_t, std::uint32_t,
                                         std::uint32_t, std::uint32_t, const std::string&) override {}
            void PreviewRelationshipMemories(std::uint32_t, std::uint32_t,
                                             const std::string&, const std::string&) override {}
            void IngestMemories(const std::vector<GeneratedMemoryEntry>&, const std::vector<std::uint32_t>&) override {}
            void RevertProfiles() override {}
            bool AreProfilesSuppressed() const override { return false; }
            void PushSuppressProfiles() override {}
            void PopSuppressProfiles() override {}
            void MarkActorStageUpdated(std::uint32_t, Foundation::ChildStage) override {}
            void RequestChildProfileUpdate(std::uint32_t, Foundation::ChildStage) override {}
            std::string GetActorBioTemplateName(std::uint32_t) const override { return {}; }
            std::uint64_t GetActorUUID(std::uint32_t) const override { return 0; }
            bool RequestActorProfileUpdate(std::uint32_t, const std::string&, const std::string&) override {
                return false;
            }
        };

        IUiBridgeGateway& GetDefaultUiBridgeGateway() noexcept {
            static NullUiBridgeGateway instance;
            return instance;
        }

        IModalGateway& GetDefaultModalGateway() noexcept {
            static NullModalGateway instance;
            return instance;
        }

        ISkyrimNetGateway& GetDefaultSkyrimNetGateway() noexcept {
            static NullSkyrimNetGateway instance;
            return instance;
        }

        constinit std::atomic<IUiBridgeGateway*> g_uiBridge{ nullptr };
        constinit std::atomic<IModalGateway*> g_modal{ nullptr };
        constinit std::atomic<ISkyrimNetGateway*> g_skyrimNet{ nullptr };

    }  // namespace

    void SetUiBridgeGateway(IUiBridgeGateway* gateway) noexcept {
        g_uiBridge.store(gateway, std::memory_order_release);
    }

    void SetModalGateway(IModalGateway* gateway) noexcept {
        g_modal.store(gateway, std::memory_order_release);
    }

    void SetSkyrimNetGateway(ISkyrimNetGateway* gateway) noexcept {
        g_skyrimNet.store(gateway, std::memory_order_release);
    }

    IUiBridgeGateway& GetUiBridgeGateway() noexcept {
        if (auto* gateway = g_uiBridge.load(std::memory_order_acquire)) {
            return *gateway;
        }
        return GetDefaultUiBridgeGateway();
    }

    IModalGateway& GetModalGateway() noexcept {
        if (auto* gateway = g_modal.load(std::memory_order_acquire)) {
            return *gateway;
        }
        return GetDefaultModalGateway();
    }

    ISkyrimNetGateway& GetSkyrimNetGateway() noexcept {
        if (auto* gateway = g_skyrimNet.load(std::memory_order_acquire)) {
            return *gateway;
        }
        return GetDefaultSkyrimNetGateway();
    }

    void ShowLLMRetryModal(const std::string& evaluationType,
                           const std::string& description,
                           LLMFailureType failureType,
                           std::function<void()> onRetry,
                           std::function<void()> onIgnore,
                           std::string_view llmVariant) {
        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("ShowLLMRetryModal: no task interface, falling back to ignore");
            if (onIgnore) onIgnore();
            return;
        }

        auto& skyrimNet = GetSkyrimNetGateway();
        ModalRequest req;
        req.title = "LLM Evaluation Failed";
        req.subtitle = evaluationType;
        req.message = "Choose Retry to send the request again, or Ignore to skip this evaluation.";
        req.tone = ModalTone::Warning;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = 1;

        if (!description.empty()) {
            req.controls.push_back(ModalControl::MakeInfoBox("error_details", "Failure Details", description, ModalTone::Warning));
        }
        req.controls.push_back(ModalControl::MakeInfoBox(
            "error_guidance",
            skyrimNet.GetLLMFailureGuidanceTitle(failureType),
            skyrimNet.GetLLMFailureGuidance(failureType, llmVariant),
            ModalTone::Default));

        req.buttons.push_back(ModalButton::Make("retry", "Retry Request", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("ignore", "Ignore", ModalButtonVariant::Secondary));

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        GetModalGateway().ShowModal(
            std::move(req),
            [expectedEpoch, onRetry = std::move(onRetry), onIgnore = std::move(onIgnore)](const ModalResponse& resp) mutable {
                if (resp.aborted || !IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(GetPapyrusMutex());
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.buttonIndex == 0 || resp.buttonId == "retry") {
                    if (onRetry) onRetry();
                } else {
                    if (onIgnore) onIgnore();
                }
            });
    }

}  // namespace MARAS::App
