#include "maras/bridge_ui/UiBridgeGatewayImpl.h"

#include "maras/bridge_ui/PrismaUIDataBridge.h"
#include "maras/bridge_ui/PrismaUIManager.h"

namespace MARAS::Bridge::Ui {

    namespace {

        class UiBridgeGatewayImpl final : public MARAS::App::IUiBridgeGateway {
        public:
            void InitializeDashboard() override {
                PrismaUIManager::GetSingleton().Initialize();
            }

            void StartListeningInput() override {
                PrismaUIManager::GetSingleton().StartListeningInput();
            }

            void OnNewGameStarted(bool deferInitialScan) override {
                PrismaUIManager::GetSingleton().OnNewGameStarted(deferInitialScan);
            }

            void ResetNewGameDeferral() override {
                PrismaUIManager::GetSingleton().ResetNewGameDeferral();
            }

            void DestroyDashboard() override {
                PrismaUIManager::GetSingleton().Destroy();
            }

            void ShowDashboard() override {
                PrismaUIManager::GetSingleton().Show();
            }

            void HideDashboard() override {
                PrismaUIManager::GetSingleton().Hide();
            }

            bool IsDashboardVisible() const override {
                return PrismaUIManager::GetSingleton().IsVisible();
            }

            bool IsDashboardOpen() const override {
                const auto& mgr = PrismaUIManager::GetSingleton();
                return mgr.IsInitialized() && mgr.IsVisible();
            }

            void Revert() override {
                PrismaUIManager::GetSingleton().Revert();
                PrismaUIDataBridge::GetSingleton().Revert();
            }

            void SendOverviewData() override {
                PrismaUIDataBridge::GetSingleton().SendOverviewData();
            }

            void SendSettingsData() override {
                PrismaUIDataBridge::GetSingleton().SendSettingsData();
            }

            void SendNPCList() override {
                PrismaUIDataBridge::GetSingleton().SendNPCList();
            }

            void SendNPCDetail(std::uint32_t npcFormID) override {
                PrismaUIDataBridge::GetSingleton().SendNPCDetail(npcFormID);
            }

            void SendHouseholdBuffs() override {
                PrismaUIDataBridge::GetSingleton().SendHouseholdBuffs();
            }

            void SendMatchmakingResults(
                const std::vector<MARAS::App::MatchmakingResult>& results,
                bool isGlobal) override {
                PrismaUIDataBridge::GetSingleton().SendMatchmakingResults(results, isGlobal);
            }

            void SendGeneratedMemories(
                std::uint32_t partnerFormId,
                const nlohmann::json& memoriesArray) override {
                PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(partnerFormId, memoriesArray);
            }

            void OnAffectionChanged(std::uint32_t npcFormID, int value) override {
                PrismaUIDataBridge::GetSingleton().OnAffectionChanged(npcFormID, value);
            }

            void OnStatusChanged(
                std::uint32_t actorA,
                std::uint32_t actorB,
                std::uint8_t status) override {
                PrismaUIDataBridge::GetSingleton().OnStatusChanged(
                    actorA,
                    actorB,
                    static_cast<MARAS::Foundation::RelationshipStatus>(status));
            }

            void OnHierarchyChanged() override {
                PrismaUIDataBridge::GetSingleton().OnHierarchyChanged();
            }

            void OnChildUpdated(const MARAS::State::ChildRecord& entry) override {
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }

            void OnChildRemoved(std::uint32_t childID) override {
                PrismaUIDataBridge::GetSingleton().OnChildRemoved(childID);
            }

            void OnChildPromoted(std::uint32_t oldID, std::uint32_t newID) override {
                PrismaUIDataBridge::GetSingleton().OnChildPromoted(oldID, newID);
            }
        };

    }  // namespace

    MARAS::App::IUiBridgeGateway& GetUiBridgeGatewayImpl() {
        static UiBridgeGatewayImpl instance;
        return instance;
    }

}  // namespace MARAS::Bridge::Ui
