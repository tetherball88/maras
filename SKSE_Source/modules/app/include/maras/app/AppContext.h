#pragma once

#include "PCH.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/SkyrimNetGateway.h"
#include "maras/state/WorldTables.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::App {

    class IUiBridgeGateway {
    public:
        virtual ~IUiBridgeGateway() = default;

        virtual void InitializeDashboard() = 0;
        virtual void StartListeningInput() = 0;
        virtual void OnNewGameStarted(bool deferInitialScan) = 0;
        virtual void ResetNewGameDeferral() = 0;
        virtual void DestroyDashboard() = 0;
        virtual void ShowDashboard() = 0;
        virtual void HideDashboard() = 0;
        virtual bool IsDashboardVisible() const = 0;
        virtual bool IsDashboardOpen() const = 0;
        virtual void Revert() = 0;

        virtual void SendOverviewData() = 0;
        virtual void SendSettingsData() = 0;
        virtual void SendNPCList() = 0;
        virtual void SendNPCDetail(std::uint32_t npcFormID) = 0;
        virtual void SendHouseholdBuffs() = 0;
        virtual void SendMatchmakingResults(const std::vector<MatchmakingResult>& results,
                                            bool isGlobal = false) = 0;
        virtual void SendGeneratedMemories(std::uint32_t partnerFormId,
                                           const nlohmann::json& memoriesArray) = 0;

        virtual void OnAffectionChanged(std::uint32_t npcFormID, int value) = 0;
        virtual void OnStatusChanged(std::uint32_t actorA, std::uint32_t actorB, std::uint8_t status) = 0;
        virtual void OnHierarchyChanged() = 0;
        virtual void OnChildUpdated(const State::ChildRecord& entry) = 0;
        virtual void OnChildRemoved(std::uint32_t childID) = 0;
        virtual void OnChildPromoted(std::uint32_t oldID, std::uint32_t newID) = 0;
    };

    class IModalGateway {
    public:
        virtual ~IModalGateway() = default;

        virtual std::string ShowModal(ModalRequest request, ModalCallback callback) = 0;
        virtual std::string ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback) = 0;
        virtual bool HasActiveModal() const = 0;
        virtual void SendActiveModalToUI(bool forceResend = false) = 0;

        virtual std::int32_t CheckAndSetConfirmationCooldown(const char* actionType,
                                                             const std::vector<RE::Actor*>& actors) = 0;
        virtual RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                                 RE::VMStackID stackID,
                                                                 const char* actionType,
                                                                 const std::vector<RE::Actor*>& mainActors,
                                                                 const std::vector<RE::Actor*>& secondaryActors,
                                                                 const char* paramsJson) = 0;

        virtual void Revert() = 0;
    };

    void SetUiBridgeGateway(IUiBridgeGateway* gateway) noexcept;
    void SetModalGateway(IModalGateway* gateway) noexcept;
    void SetSkyrimNetGateway(ISkyrimNetGateway* gateway) noexcept;

    IUiBridgeGateway& GetUiBridgeGateway() noexcept;
    IModalGateway& GetModalGateway() noexcept;
    ISkyrimNetGateway& GetSkyrimNetGateway() noexcept;

    class ScopedMemorySuppression {
    public:
        ScopedMemorySuppression() { GetSkyrimNetGateway().PushSuppressMemories(); }
        ~ScopedMemorySuppression() { GetSkyrimNetGateway().PopSuppressMemories(); }
        ScopedMemorySuppression(const ScopedMemorySuppression&) = delete;
        ScopedMemorySuppression& operator=(const ScopedMemorySuppression&) = delete;
    };

    class ScopedMatchmakingApply {
    public:
        ScopedMatchmakingApply() { GetSkyrimNetGateway().PushMatchmakingApply(); }
        ~ScopedMatchmakingApply() { GetSkyrimNetGateway().PopMatchmakingApply(); }
        ScopedMatchmakingApply(const ScopedMatchmakingApply&) = delete;
        ScopedMatchmakingApply& operator=(const ScopedMatchmakingApply&) = delete;
    };

    class ScopedProfileSuppression {
    public:
        ScopedProfileSuppression() { GetSkyrimNetGateway().PushSuppressProfiles(); }
        ~ScopedProfileSuppression() { GetSkyrimNetGateway().PopSuppressProfiles(); }
        ScopedProfileSuppression(const ScopedProfileSuppression&) = delete;
        ScopedProfileSuppression& operator=(const ScopedProfileSuppression&) = delete;
    };

    void ShowLLMRetryModal(const std::string& evaluationType,
                           const std::string& description,
                           LLMFailureType failureType,
                           std::function<void()> onRetry,
                           std::function<void()> onIgnore,
                           std::string_view llmVariant = "gamemaster_evaluation");

}  // namespace MARAS::App
