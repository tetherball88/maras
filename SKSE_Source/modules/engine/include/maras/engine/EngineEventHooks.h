#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/domain/relationships/GiftAndIntimacyRules.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/CoSaveCodec.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
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
        virtual void AbortCoSaveLoad() noexcept {}
        virtual void FinalizeCoSaveLoad(
            bool loadedAny,
            const State::CoSaveLoadReport& report) {
            (void)loadedAny;
            (void)report;
        }
        virtual void OnCoSaveRevert() {}

        // PollingService Timers (Main Thread)
        virtual void OnPollingFastTick(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnPollingUpdate(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingAutoMarriageTick(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingDayChanged(float currentGameDay) { (void)currentGameDay; }
        virtual void OnPollingRefreshChildVisuals() {}

        virtual void EnsureActorRegistered(RE::FormID actorId) { (void)actorId; }

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
        virtual void OnDialogueOpened(RE::FormID npcId) { (void)npcId; }
        virtual bool OnRecordIntimateCheating(RE::FormID cheaterId,
                                              RE::FormID victimId,
                                              RE::FormID affairPartnerId,
                                              bool isSecret) {
            (void)cheaterId;
            (void)victimId;
            (void)affairPartnerId;
            (void)isSecret;
            return false;
        }
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
        virtual void OnIntimateOpenRelationshipWalkIn(RE::FormID partnerId,
                                                      RE::FormID witnessId,
                                                      RE::FormID otherPartnerId,
                                                      std::string_view framework,
                                                      std::int32_t threadId,
                                                      bool isWalkIn = true) {
            (void)partnerId;
            (void)witnessId;
            (void)otherPartnerId;
            (void)framework;
            (void)threadId;
            (void)isWalkIn;
        }

        // Player gift, sleep, and intimate completion lifecycle
        virtual void OnPlayerGiftCompleted(
            RE::FormID receiverId,
            std::string_view playerName,
            std::string_view receiverName,
            std::span<const Domain::Relationships::GiftedItemEntry> items) {
            (void)receiverId;
            (void)playerName;
            (void)receiverName;
            (void)items;
        }

        virtual Domain::Household::SleepRestedPlan OnPlayerSlept(
            std::span<const RE::FormID> sameCellActorIds,
            RE::FormID currentLocationId,
            std::span<const RE::FormID> presentAtLocationRawIds,
            Foundation::ActorSex playerSex) {
            (void)sameCellActorIds;
            (void)currentLocationId;
            (void)presentAtLocationRawIds;
            (void)playerSex;
            return {};
        }

        virtual void OnIntimateThreadEnded(
            std::span<const RE::FormID> recordedPartnerIds,
            bool hadSex) {
            (void)recordedPartnerIds;
            (void)hadSex;
        }

        virtual void OnBlockedFertilityMcmClick(
            Domain::Lineage::BlockedFertilityMcmClick blockedClick,
            std::string_view optionStateName) {
            (void)blockedClick;
            (void)optionStateName;
        }

        struct FertilityChildNamingRequest {
            RE::FormID childId{ 0 };
            RE::FormID motherId{ 0 };
            bool isFemale{ false };
            bool cameFromMcmForceGrowth{ false };
            std::vector<std::string> existingNames{};
        };

        virtual void OnFertilityChildNamingRequested(
            FertilityChildNamingRequest request,
            std::function<void(const std::string&)> onResolved) {
            (void)request;
            if (onResolved) {
                onResolved("Child");
            }
        }
    };

    void SetEngineEventHooks(IEngineEventHooks* hooks) noexcept;
    [[nodiscard]] IEngineEventHooks* GetEngineEventHooks() noexcept;

}  // namespace MARAS::Engine
