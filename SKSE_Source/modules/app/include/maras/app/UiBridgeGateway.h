#pragma once

#include "maras/app/ModalTypes.h"
#include "maras/state/WorldTables.h"

#include <nlohmann/json.hpp>

#include <cstdint>
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

}  // namespace MARAS::App
