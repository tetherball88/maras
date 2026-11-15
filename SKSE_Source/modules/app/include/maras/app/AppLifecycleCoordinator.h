#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "maras/app/AppContext.h"
#include "maras/engine/EngineEventHooks.h"

#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace MARAS::App {

    /// Layer 4 lifecycle coordinator bridging Layer 3 engine hooks (co-save load/revert,
    /// polling timers, actor load batches, death, quest commands, external marriage factions,
    /// and intimate cheating detection) to Layer 4 use cases and gateways.
    class AppLifecycleCoordinator final : public Engine::IEngineEventHooks {
    public:
        static AppLifecycleCoordinator& GetSingleton();

        AppLifecycleCoordinator(const AppLifecycleCoordinator&) = delete;
        AppLifecycleCoordinator& operator=(const AppLifecycleCoordinator&) = delete;
        AppLifecycleCoordinator(AppLifecycleCoordinator&&) = delete;
        AppLifecycleCoordinator& operator=(AppLifecycleCoordinator&&) = delete;

        /// Registers this coordinator as the active `MARAS::Engine::IEngineEventHooks` receiver.
        void RegisterWithEngine() noexcept;

        // SKSE Co-Save Lifecycle
        void BeginCoSaveLoad() override;
        void FinalizeCoSaveLoad(bool loadedAny, std::span<const Foundation::RefId> retiredSceneMarkers = {}) override;
        void OnCoSaveRevert() override;

        // PollingService Timers (Main Thread)
        void OnPollingFastTick(float deltaSeconds) override;
        void OnPollingUpdate(float currentGameDay) override;
        void OnPollingAutoMarriageTick(float currentGameDay) override;
        void OnPollingDayChanged(float currentGameDay) override;
        void OnPollingRefreshChildVisuals() override;

        void EnsureActorRegistered(RE::FormID actorId) override;

        // Engine Event Sinks & Faction Hooks (Main Thread)
        void OnActorLoadedBatch(const std::vector<RE::FormID>& initActors,
                                const std::vector<RE::FormID>& loadedActors) override;
        void OnActorDeath(RE::FormID victimId, RE::FormID killerId, bool isPlayerKiller) override;
        bool OnQuestCommand(std::string_view commandType,
                            RE::FormID npcId,
                            std::string_view argument,
                            std::string_view context) override;
        [[nodiscard]] bool CanAcceptExternalMarriage(RE::FormID actorId) override;
        void OnExternalMarriageFactionAdded(RE::FormID actorId, std::int8_t rank) override;
        void OnExternalMarriageFactionRemoved(RE::FormID actorId) override;
        void OnDialogueOpened(RE::FormID npcId) override;
        void OnIntimateCheatingCaught(RE::FormID cheaterId,
                                      RE::FormID victimId,
                                      RE::FormID affairPartnerId,
                                      std::string_view framework,
                                      std::int32_t threadId,
                                      bool isWalkIn) override;
        void OnIntimateOpenRelationshipWalkIn(RE::FormID partnerId,
                                              RE::FormID witnessId,
                                              RE::FormID otherPartnerId,
                                              std::string_view framework,
                                              std::int32_t threadId,
                                              bool isWalkIn = true) override;

        void OnPlayerGiftCompleted(
            RE::FormID receiverId,
            std::string_view playerName,
            std::string_view receiverName,
            std::span<const Domain::Relationships::GiftedItemEntry> items) override;

        Domain::Household::SleepRestedPlan OnPlayerSlept(
            std::span<const RE::FormID> sameCellActorIds,
            RE::FormID currentLocationId,
            std::span<const RE::FormID> presentAtLocationRawIds,
            Foundation::ActorSex playerSex) override;

        void OnIntimateThreadEnded(
            std::span<const RE::FormID> recordedPartnerIds,
            bool hadSex) override;

    private:
        AppLifecycleCoordinator() = default;
        ~AppLifecycleCoordinator() override = default;

        void ReleaseCoSaveLoadSuppressions() noexcept;

        std::mutex suppressionMutex_{};
        std::optional<ScopedMemorySuppression> loadMemorySuppression_{};
        std::optional<ScopedProfileSuppression> loadProfileSuppression_{};
    };

}  // namespace MARAS::App
