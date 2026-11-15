#include "maras/app/AppLifecycleCoordinator.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AgencyWorkflowCoordinator.h"
#include "maras/app/BanterSceneCoordinator.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipInteractionCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/app/SkyrimNetActionCoordinator.h"
#include "maras/engine/ChildVisualReconciler.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/RelationshipEngineReconciler.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/state/WorldStore.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::App {

    namespace {

        class ScopedSuppressionGuard {
        public:
            explicit ScopedSuppressionGuard(AppLifecycleCoordinator& owner,
                                            void (AppLifecycleCoordinator::*releaseFn)() noexcept) noexcept
                : owner_(owner), releaseFn_(releaseFn) {}
            ~ScopedSuppressionGuard() { (owner_.*releaseFn_)(); }
            ScopedSuppressionGuard(const ScopedSuppressionGuard&) = delete;
            ScopedSuppressionGuard& operator=(const ScopedSuppressionGuard&) = delete;

        private:
            AppLifecycleCoordinator& owner_;
            void (AppLifecycleCoordinator::*releaseFn_)() noexcept;
        };

    }  // namespace

    AppLifecycleCoordinator& AppLifecycleCoordinator::GetSingleton() {
        static AppLifecycleCoordinator instance;
        return instance;
    }

    void AppLifecycleCoordinator::RegisterWithEngine() noexcept {
        Engine::SetEngineEventHooks(this);
    }

    void AppLifecycleCoordinator::ReleaseCoSaveLoadSuppressions() noexcept {
        std::lock_guard lock(suppressionMutex_);
        loadProfileSuppression_.reset();
        loadMemorySuppression_.reset();
    }

    // =========================================================================
    // SKSE Co-Save Lifecycle
    // =========================================================================

    void AppLifecycleCoordinator::BeginCoSaveLoad() {
        std::lock_guard lock(suppressionMutex_);
        if (!loadMemorySuppression_.has_value()) {
            loadMemorySuppression_.emplace();
        }
        if (!loadProfileSuppression_.has_value()) {
            loadProfileSuppression_.emplace();
        }
        MARAS_LOG_DEBUG("[AppLifecycleCoordinator] BeginCoSaveLoad: active memory/profile suppression scopes acquired");
    }

    void AppLifecycleCoordinator::FinalizeCoSaveLoad(
        bool loadedAny,
        std::span<const Foundation::RefId> retiredSceneMarkers) {
        ScopedSuppressionGuard releaseOnExit(*this, &AppLifecycleCoordinator::ReleaseCoSaveLoadSuppressions);

        ChildVisualReconciler::GetSingleton().FlushRetiredForms();
        BanterSceneCoordinator::GetSingleton().CleanupRetiredSceneMarkers(retiredSceneMarkers);

        if (!loadedAny) {
            GlobalVarReconciler::GetSingleton().ResetSessionGlobalsToDefaults();
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            LineageUseCases::GetSingleton().FinalizeCoSaveLoad(false);
            MARAS_LOG_INFO("[AppLifecycleCoordinator] FinalizeCoSaveLoad: no prior co-save chunks loaded");
            return;
        }

        SettingsCoordinator::GetSingleton().ApplyPostLoadEngineSideEffects();
        RelationshipUseCases::GetSingleton().FinalizeLoad();
        LineageUseCases::GetSingleton().FinalizeCoSaveLoad(true);
        PaternityWorkflowCoordinator::GetSingleton().FinalizeLoad();
        HouseholdUseCases::GetSingleton().FinalizeLoad();
        ChildVisualReconciler::GetSingleton().ReassertAllChildVisualsOnLoad();
        BanterSceneCoordinator::GetSingleton().RecoverStaleMutesOnLoad();

        if (GetSkyrimNetGateway().IsAvailable()) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (snap && snap->lineage) {
                auto& skyrimNet = GetSkyrimNetGateway();
                for (const auto& [childId, child] : snap->lineage->AllChildren()) {
                    if ((child.stage == Foundation::ChildStage::Child ||
                         child.stage == Foundation::ChildStage::Adult) &&
                        HasPhysicalActor(childId.raw())) {
                        skyrimNet.MarkActorStageUpdated(childId.raw(), child.stage);
                    }
                }
            }
        }

        auto& ui = GetUiBridgeGateway();
        if (ui.IsDashboardOpen()) {
            ui.SendOverviewData();
            ui.SendSettingsData();
            ui.SendNPCList();
            ui.SendHouseholdBuffs();
        }

        MARAS_LOG_INFO("[AppLifecycleCoordinator] FinalizeCoSaveLoad: completed post-load coordinator convergence");
    }

    void AppLifecycleCoordinator::OnCoSaveRevert() {
        ReleaseCoSaveLoadSuppressions();

        auto& visualReconciler = ChildVisualReconciler::GetSingleton();
        visualReconciler.Revert();

        GetSkyrimNetGateway().RevertMemories();
        GetSkyrimNetGateway().RevertProfiles();
        GetSkyrimNetGateway().RevertEvents();
        GetModalGateway().Revert();
        GetUiBridgeGateway().Revert();
        GetUiBridgeGateway().DestroyDashboard();

        SettingsCoordinator::GetSingleton().RevertGameSettings();
        RelationshipUseCases::GetSingleton().Revert();
        LineageUseCases::GetSingleton().Revert();
        PaternityWorkflowCoordinator::GetSingleton().Revert();
        HouseholdUseCases::GetSingleton().Revert();
        AgencyWorkflowCoordinator::GetSingleton().Revert();
        SkyrimNetActionCoordinator::GetSingleton().Revert();

        MARAS_LOG_INFO("[AppLifecycleCoordinator] OnCoSaveRevert: reverted all gateways and Layer 4 coordinators");
    }

    // =========================================================================
    // PollingService Timers (Main Thread)
    // =========================================================================

    void AppLifecycleCoordinator::OnPollingFastTick(float deltaSeconds) {
        SkyrimNetActionCoordinator::GetSingleton().TickPendingNarrations(deltaSeconds);
    }

    void AppLifecycleCoordinator::OnPollingUpdate(float currentGameDay) {
        if (GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().TickMemories();
        }

        std::lock_guard lock(GetPapyrusMutex());
        PaternityWorkflowCoordinator::GetSingleton().PollFertilityTick(currentGameDay);

        auto& lineageUseCases = LineageUseCases::GetSingleton();
        if (currentGameDay >= 0.0f) {
            std::vector<RE::FormID> expiredOrderChildren;
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                std::vector<State::ChildId> expiredIds;
                for (const auto& [cid, st] : hh.AllChildStates()) {
                    if (st.orderExpiryGameDay >= 0.0f && currentGameDay >= st.orderExpiryGameDay) {
                        expiredIds.push_back(cid);
                    }
                }
                for (const auto cid : expiredIds) {
                    if (auto* st = hh.FindChildStateMutable(cid)) {
                        st->currentOrder = 0.0f;
                        st->orderExpiryGameDay = -1.0f;
                        expiredOrderChildren.push_back(cid.raw());
                    }
                }
            });
            for (const RE::FormID childId : expiredOrderChildren) {
                MARAS_LOG_INFO("[AppLifecycleCoordinator] Child {:08X} order expired at game day {:.3f}",
                               childId, currentGameDay);
                lineageUseCases.ApplyPackageForCurrentState(childId);
            }
            lineageUseCases.CheckChildrenTrainingProgress(currentGameDay);
        }

        HouseholdUseCases::GetSingleton().PollTick(currentGameDay);
    }

    void AppLifecycleCoordinator::OnPollingAutoMarriageTick(float currentGameDay) {
        std::lock_guard lock(GetPapyrusMutex());
        RelationshipUseCases::GetSingleton().ProcessPollingAutoMarriageTick(currentGameDay);
    }

    void AppLifecycleCoordinator::OnPollingDayChanged(float currentGameDay) {
        std::lock_guard lock(GetPapyrusMutex());
        RelationshipUseCases::GetSingleton().ProcessDailyAffectionTick(currentGameDay);
        LineageUseCases::GetSingleton().ProcessDayChange(currentGameDay);
        PaternityWorkflowCoordinator::GetSingleton().ProcessDailyTick(currentGameDay);
        HouseholdUseCases::GetSingleton().ProcessDailyTick(currentGameDay);

        auto& agency = AgencyWorkflowCoordinator::GetSingleton();
        agency.TryScheduledMatchmaking(currentGameDay);
        agency.TryScheduledPlayerPartnerInfidelity(currentGameDay);
        agency.TryScheduledBanter(currentGameDay);
    }

    void AppLifecycleCoordinator::OnPollingRefreshChildVisuals() {
        std::lock_guard lock(GetPapyrusMutex());
        LineageUseCases::GetSingleton().RefreshAllChildVisuals();
    }

    // =========================================================================
    // Engine Event Sinks & Faction Hooks (Main Thread)
    // =========================================================================

    void AppLifecycleCoordinator::OnActorLoadedBatch(const std::vector<RE::FormID>& initActors,
                                                     const std::vector<RE::FormID>& loadedActors) {
        if (initActors.empty() && loadedActors.empty()) {
            return;
        }

        std::unordered_set<RE::FormID> initSet(initActors.begin(), initActors.end());
        std::unordered_set<RE::FormID> seenIds;
        seenIds.reserve(initActors.size() + loadedActors.size());

        std::lock_guard lock(GetPapyrusMutex());

        auto& spatialIndex = SpatialWorldIndex::GetSingleton();
        auto& deferredResolver = DeferredActorResolver::GetSingleton();
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        auto& lineageUseCases = LineageUseCases::GetSingleton();

        auto processActorId = [&](RE::FormID actorId, bool fromInitScript) {
            if (!HasPhysicalActor(actorId) || !seenIds.insert(actorId).second) {
                return;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor || actor->IsDeleted()) {
                return;
            }

            spatialIndex.RegisterLoadedActor(actor);
            if (auto* base = actor->GetActorBase()) {
                lineageUseCases.TryResolvePendingChildren(base->GetFormID(), actorId);
            }
            deferredResolver.OnActorLoaded(actor);
            relUseCases.OnActorLoaded(actorId, fromInitScript);
            if (actor->Is3DLoaded()) {
                lineageUseCases.ApplyChildVisualsOn3DLoad(actor);
            }
        };

        for (const RE::FormID id : initActors) {
            processActorId(id, true);
        }
        for (const RE::FormID id : loadedActors) {
            processActorId(id, initSet.contains(id));
        }
    }

    void AppLifecycleCoordinator::OnActorDeath(RE::FormID victimId,
                                               RE::FormID killerId,
                                               bool isPlayerKiller) {
        std::lock_guard lock(GetPapyrusMutex());
        LineageUseCases::GetSingleton().RecordActorDeath(victimId, killerId, isPlayerKiller);
    }

    bool AppLifecycleCoordinator::OnQuestCommand(std::string_view commandType,
                                                 RE::FormID npcId,
                                                 std::string_view argument,
                                                 std::string_view context) {
        std::lock_guard lock(GetPapyrusMutex());
        return RelationshipUseCases::GetSingleton().ExecuteQuestCommand(commandType, npcId, argument, context);
    }

    bool AppLifecycleCoordinator::CanAcceptExternalMarriage(RE::FormID actorId) {
        std::lock_guard lock(GetPapyrusMutex());
        return RelationshipUseCases::GetSingleton().CanAcceptExternalMarriage(actorId);
    }

    void AppLifecycleCoordinator::OnExternalMarriageFactionAdded(RE::FormID actorId, std::int8_t rank) {
        std::lock_guard lock(GetPapyrusMutex());
        RelationshipUseCases::GetSingleton().HandleExternalMarriageFactionAdded(actorId, rank);
    }

    void AppLifecycleCoordinator::OnExternalMarriageFactionRemoved(RE::FormID actorId) {
        std::lock_guard lock(GetPapyrusMutex());
        RelationshipUseCases::GetSingleton().HandleExternalMarriageFactionRemoved(actorId);
    }

    void AppLifecycleCoordinator::EnsureActorRegistered(RE::FormID actorId) {
        std::lock_guard lock(GetPapyrusMutex());
        (void)RelationshipUseCases::GetSingleton().RegisterAsCandidate(actorId);
    }

    void AppLifecycleCoordinator::OnDialogueOpened(RE::FormID npcId) {
        std::lock_guard lock(GetPapyrusMutex());
        if (npcId == 0 || npcId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        if (RelationshipUseCases::GetSingleton().GetRelationshipStatus(
                npcId, RelationshipUseCases::kPlayerFormID) == Foundation::RelationshipStatus::Unknown) {
            RelationshipEngineReconciler::GetSingleton().ApplyNoAiDialogueChanceGlobals({});
            return;
        }
        AffectionEventCoordinator::GetSingleton().OnDialogueOpened(npcId);
        RelationshipInteractionCoordinator::GetSingleton().OnDialogueOpened(npcId);
    }

    void AppLifecycleCoordinator::OnIntimateCheatingCaught(RE::FormID cheaterId,
                                                           RE::FormID victimId,
                                                           RE::FormID affairPartnerId,
                                                           std::string_view framework,
                                                           std::int32_t threadId,
                                                           bool isWalkIn) {
        std::lock_guard lock(GetPapyrusMutex());

        AffectionEventCoordinator::GetSingleton().OnCheatingCaught(cheaterId, victimId);

        RelationshipUseCases::GetSingleton().HandleIntimateCheatingCaught(
            cheaterId, victimId, affairPartnerId, isWalkIn);

        if (GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().EmitCheatingCaught(victimId, cheaterId, affairPartnerId, isWalkIn);
            GetSkyrimNetGateway().RecordInfidelityEvent(
                cheaterId, victimId, affairPartnerId, "caught", isWalkIn);
        }

        auto& ui = GetUiBridgeGateway();
        if (ui.IsDashboardOpen()) {
            ui.SendOverviewData();
            ui.SendNPCList();
            if (HasPhysicalActor(cheaterId)) {
                ui.SendNPCDetail(cheaterId);
            }
        }

        MARAS_LOG_INFO(
            "[AppLifecycleCoordinator] Processed intimate cheating caught (cheater={:08X}, victim={:08X}, partner={:08X}, framework='{}', thread={}, walkIn={})",
            cheaterId, victimId, affairPartnerId, framework, threadId, isWalkIn);
    }

    void AppLifecycleCoordinator::OnIntimateOpenRelationshipWalkIn(RE::FormID partnerId,
                                                                   RE::FormID witnessId,
                                                                   RE::FormID otherPartnerId,
                                                                   std::string_view framework,
                                                                   std::int32_t threadId,
                                                                   bool isWalkIn) {
        std::lock_guard lock(GetPapyrusMutex());
        AffectionEventCoordinator::GetSingleton().OnOpenRelationshipWalkIn(partnerId, witnessId);
        if (GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().EmitOpenRelationshipWalkIn(witnessId, partnerId, otherPartnerId, isWalkIn);
        }
        MARAS_LOG_INFO(
            "[AppLifecycleCoordinator] Processed open relationship walk-in affection check (partner={:08X}, witness={:08X}, other={:08X}, framework='{}', thread={}, walkIn={})",
            partnerId, witnessId, otherPartnerId, framework, threadId, isWalkIn);
    }

    void AppLifecycleCoordinator::OnPlayerGiftCompleted(
        RE::FormID receiverId,
        std::string_view playerName,
        std::string_view receiverName,
        std::span<const Domain::Relationships::GiftedItemEntry> items) {
        if (State::IsLegacyMigrationActive()) {
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap) {
            return;
        }
        const auto outcome = Domain::Relationships::EvaluateGiftSession(
            *snap, State::ActorId{ receiverId }, playerName, receiverName, items);
        if (!outcome.valid) {
            return;
        }
        if (GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().EmitAffectionGift(outcome.skyrimNetMessage, receiverId);
        }
        AffectionEventCoordinator::GetSingleton().AddGiftAffection(
            receiverId, static_cast<float>(outcome.totalGoldValue));
        MARAS_LOG_DEBUG("[AppLifecycleCoordinator] Gift completed (receiver={:08X}, value={}, items='{}')",
                        receiverId, outcome.totalGoldValue, outcome.formattedItems);
    }

    Domain::Household::SleepRestedPlan AppLifecycleCoordinator::OnPlayerSlept(
        std::span<const RE::FormID> sameCellActorIds,
        RE::FormID currentLocationId,
        std::span<const RE::FormID> presentAtLocationRawIds,
        Foundation::ActorSex playerSex) {
        if (State::IsLegacyMigrationActive()) {
            return {};
        }
        std::lock_guard lock(GetPapyrusMutex());
        std::vector<State::ActorId> cellActors;
        cellActors.reserve(sameCellActorIds.size());
        for (const RE::FormID rawId : sameCellActorIds) {
            cellActors.emplace_back(rawId);
        }
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (snap) {
                const auto partners =
                    Domain::Household::EvaluateSleepPaidAffectionPartners(*snap, cellActors);
                for (const auto partner : partners) {
                    AffectionEventCoordinator::GetSingleton().AddSleptAffection(partner.raw());
                }
            }
        }
        const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
        if (!postSnap) {
            return {};
        }
        return Domain::Household::EvaluateSleepRestedPlan(
            *postSnap, State::HouseId{ currentLocationId }, presentAtLocationRawIds, playerSex);
    }

    void AppLifecycleCoordinator::OnIntimateThreadEnded(
        std::span<const RE::FormID> recordedPartnerIds,
        bool hadSex) {
        if (State::IsLegacyMigrationActive()) {
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap) {
            return;
        }
        std::vector<State::ActorId> partners;
        partners.reserve(recordedPartnerIds.size());
        for (const RE::FormID rawId : recordedPartnerIds) {
            partners.emplace_back(rawId);
        }
        const auto outcome =
            Domain::Relationships::EvaluateIntimateThreadCompletion(*snap, partners, hadSex);
        for (const auto partner : outcome.eligiblePartners) {
            AffectionEventCoordinator::GetSingleton().AddIntimacyAffection(
                partner.raw(), outcome.baseAffection);
        }
        MARAS_LOG_DEBUG("[AppLifecycleCoordinator] Intimate thread ended (partners={}, hadSex={}, baseAffection={})",
                        outcome.eligiblePartners.size(), hadSex, outcome.baseAffection);
    }

}  // namespace MARAS::App
