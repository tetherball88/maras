#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace MARAS::Engine {

    /**
     * Pure virtual inversion port allowing Layer 3 (modules/engine) sinks, hooks,
     * timers, and SKSE co-save callbacks to notify Layer 4 (modules/app) without
     * downward-violating `#include "maras/app/..."` dependencies.
     */
    class IEngineEventHooks {
    public:
        virtual ~IEngineEventHooks() = default;

        // SKSE Co-Save Lifecycle
        virtual void BeginCoSaveLoad() {}
        virtual void FinalizeCoSaveLoad(bool loadedAny) { (void)loadedAny; }
        virtual void OnCoSaveRevert() {}

        // PollingService Timers (Main Thread)
        virtual void OnPollingUpdate(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingAutoMarriageTick(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingDayChanged(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingRefreshChildVisuals() {}

        // Engine Event Sinks & Faction Hooks (Main Thread)
        virtual void OnActorLoadedBatch(const std::vector<RE::FormID>& initActors,
                                        const std::vector<RE::FormID>& loadedActors) {
            (void)initActors;
            (void)loadedActors;
        }
        virtual void OnActorDeath(RE::FormID victimId, RE::FormID killerId, bool isPlayerKiller) {
            (void)victimId;
            (void)killerId;
            (void)isPlayerKiller;
        }
        virtual bool OnQuestCommand(std::string_view commandType,
                                    RE::FormID npcId,
                                    std::string_view argument,
                                    std::string_view context) {
            (void)commandType;
            (void)npcId;
            (void)argument;
            (void)context;
            return false;
        }
        [[nodiscard]] virtual bool CanAcceptExternalMarriage(RE::FormID actorId) {
            (void)actorId;
            return true;
        }
        virtual void OnExternalMarriageFactionAdded(RE::FormID actorId, std::int8_t rank) {
            (void)actorId;
            (void)rank;
        }
        virtual void OnExternalMarriageFactionRemoved(RE::FormID actorId) { (void)actorId; }
        virtual void OnIntimateCheatingCaught(RE::FormID cheaterId,
                                              RE::FormID victimId,
                                              RE::FormID affairPartnerId,
                                              std::string_view framework,
                                              std::int32_t threadId,
                                              bool isWalkIn) {
            (void)cheaterId;
            (void)victimId;
            (void)affairPartnerId;
            (void)framework;
            (void)threadId;
            (void)isWalkIn;
        }
    };

    void SetEngineEventHooks(IEngineEventHooks* hooks) noexcept;
    [[nodiscard]] IEngineEventHooks* GetEngineEventHooks() noexcept;

}  // namespace MARAS::Engine
