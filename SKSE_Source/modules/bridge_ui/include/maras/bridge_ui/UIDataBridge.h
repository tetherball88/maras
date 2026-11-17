#pragma once

#include "maras/app/ModalTypes.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/WorldTables.h"

#include <RE/Skyrim.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MARAS::Bridge::Ui {

    /// Bidirectional JSON data bridge between MARAS C++ coordinators and the React dashboard.
    class UIDataBridge {
    public:
        static UIDataBridge& GetSingleton();
        static void RegisterAppGateway() noexcept;

        UIDataBridge(const UIDataBridge&) = delete;
        UIDataBridge& operator=(const UIDataBridge&) = delete;
        UIDataBridge(UIDataBridge&&) = delete;
        UIDataBridge& operator=(UIDataBridge&&) = delete;

        void Initialize();
        void RegisterListeners();
        void Revert();

        // Outbound UI pushes
        void RefreshDashboardOnOpen();
        void SendOverviewData();
        void SendSettingsData();
        void SendLocaleData();
        void SendLocaleDataOnMainThreadSafe();
        void SendNPCList();
        void SendNPCDetail(RE::FormID npcFormID);
        void SendMatchmakingData();
        void SendAllSkyrimNPCs();
        void SendMatchmakingResults(
            const std::vector<MARAS::App::MatchmakingResult>& results,
            bool isGlobal = false);
        void SendGeneratedMemories(RE::FormID partnerFormId, const nlohmann::json& memoriesArray);
        void SendMatchmakingSettings();
        void SendHouseholdBuffs();
        std::string DumpStoredData(std::string_view tag = {});

        // Real-time event notifications to frontend
        void OnAffectionChanged(RE::FormID npcFormID, int newValue);
        void OnStatusChanged(
            RE::FormID actorA,
            RE::FormID actorB,
            MARAS::Foundation::RelationshipStatus newStatus);
        void OnHierarchyChanged();
        void OnChildUpdated(const MARAS::State::ChildRecord& entry);
        void OnChildRemoved(RE::FormID childID);
        void OnChildPromoted(RE::FormID oldID, RE::FormID newID);

    private:
        UIDataBridge() = default;
        ~UIDataBridge() = default;

        [[nodiscard]] static std::string FormIDToString(RE::FormID formID);
        [[nodiscard]] static RE::FormID StringToFormID(const std::string& str);

        static void OnActionFromUI(const char* argument);
        void HandleActionJson(const char* actionJsonStr);
        void HandleAction(const std::string& type, const nlohmann::json& payload);

        // Game-thread push implementations
        void RefreshDashboardOnOpenOnMainThread();
        void SendOverviewDataOnMainThread();
        void SendSettingsDataOnMainThread();
        void SendLocaleDataOnMainThread();
        void SendNPCListOnMainThread();
        void SendNPCDetailOnMainThread(RE::FormID npcFormID);
        void SendMatchmakingDataOnMainThread();
        void SendAllSkyrimNPCsOnMainThread();
        void SendMatchmakingResultsOnMainThread(
            const std::vector<MARAS::App::MatchmakingResult>& results,
            bool isGlobal);
        void SendGeneratedMemoriesOnMainThread(
            RE::FormID partnerFormId,
            const nlohmann::json& memoriesArray);
        void SendMatchmakingSettingsOnMainThread();
        void SendHouseholdBuffsOnMainThread();

        void OnAffectionChangedOnMainThread(RE::FormID npcFormID, int newValue);
        void OnStatusChangedOnMainThread(
            RE::FormID actorA,
            RE::FormID actorB,
            MARAS::Foundation::RelationshipStatus newStatus);
        void OnHierarchyChangedOnMainThread();
        void OnChildUpdatedOnMainThread(const MARAS::State::ChildRecord& entry);
        void OnChildRemovedOnMainThread(RE::FormID childID);
        void OnChildPromotedOnMainThread(RE::FormID oldID, RE::FormID newID);

        // Read-only JSON builders (strict CQS, uniform PerspectiveView intrigue masking)
        [[nodiscard]] nlohmann::json BuildOverviewJson() const;
        [[nodiscard]] nlohmann::json BuildSettingsJson() const;
        [[nodiscard]] nlohmann::json BuildMatchmakingSettingsJson() const;
        [[nodiscard]] nlohmann::json BuildNPCProfileJson(
            RE::FormID npcFormID,
            const MARAS::Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json BuildChildJson(
            const MARAS::Projection::PerceivedChildLineage& child,
            const MARAS::Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json BuildNPCDetailJson(
            RE::FormID npcFormID,
            const MARAS::Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json BuildAllSkyrimNPCsJson() const;
        [[nodiscard]] nlohmann::json BuildVisibleProfilesArray(
            const MARAS::Projection::PerspectiveView& view,
            const std::vector<MARAS::Projection::PerceivedChildLineage>& visibleChildren) const;
        [[nodiscard]] nlohmann::json BuildPlayerTrainingEligibilityJson() const;
        [[nodiscard]] nlohmann::json BuildTrainingCatalogJson(std::int32_t baseCost) const;
        [[nodiscard]] nlohmann::json BuildChildTrainingDiscountsJson(
            MARAS::State::ChildId childId,
            const MARAS::Projection::PerspectiveView& view) const;
        [[nodiscard]] std::string ResolveTrainingImageSrc(const std::string& imagePath) const;

        // Action handlers (always executed on game thread under current SessionEpoch + PapyrusMutex)
        void ProcessSetAffection(const nlohmann::json& payload);
        void ProcessSetSocialClass(const nlohmann::json& payload);
        void ProcessSetSkillType(const nlohmann::json& payload);
        void ProcessSetTemperament(const nlohmann::json& payload);
        void ProcessSetPairIsOpen(const nlohmann::json& payload);
        void ProcessSetHierarchyRank(const nlohmann::json& payload);
        void ProcessAssignRelationship(const nlohmann::json& payload);
        void ProcessRemoveRelationship(const nlohmann::json& payload);
        void ProcessUpdateSetting(const nlohmann::json& payload);
        void ProcessUpdateMatchmakingSettings(const nlohmann::json& payload);
        void ProcessSearchNPCs(const nlohmann::json& payload);
        void ProcessTeleportPlayerToNpc(const nlohmann::json& payload);
        void ProcessTeleportNpcToPlayer(const nlohmann::json& payload);
        void ProcessSetChildCritterBanned(const nlohmann::json& payload);
        void ProcessSetChildPetBanned(const nlohmann::json& payload);
        void ProcessDismissChildCritter(const nlohmann::json& payload);
        void ProcessDismissChildPet(const nlohmann::json& payload);
        void ProcessAddChild(const nlohmann::json& payload);
        void ProcessRemoveChild(const nlohmann::json& payload);
        void ProcessUpdateChild(const nlohmann::json& payload);
        void ProcessSpawnChild(const nlohmann::json& payload);
        void ProcessSendChildToTraining(const nlohmann::json& payload);
        void ProcessCompleteChildTraining(const nlohmann::json& payload);
        void ProcessRelocateFamilyUnit(const nlohmann::json& payload);
        void ProcessSetPaternitySecretEdges(const nlohmann::json& payload);
        void ProcessEvaluateChildSecret(const nlohmann::json& payload);
        void ProcessSetNPCCurrentHome(const nlohmann::json& payload);
        void ProcessSetNPCStayAtHome(const nlohmann::json& payload);
        void ProcessSkyrimNetMatchSuggest(const nlohmann::json& payload);
        void ProcessSkyrimNetGlobalMatchSuggest(const nlohmann::json& payload);
        void ProcessGenerateMatchMemories(const nlohmann::json& payload);
        std::string DumpStoredDataImpl(std::string_view tag, bool notifyUi);
        void ProcessDumpData(const nlohmann::json& payload);
        void ProcessDumpNPCIndex(const nlohmann::json& payload);

        mutable std::mutex mutex_;
        mutable std::unordered_map<std::string, std::string> cachedTrainingImages_;
        std::atomic<RE::FormID> lastRequestedDetailNpcID_{ 0 };
    };

}  // namespace MARAS::Bridge::Ui
