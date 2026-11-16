#include "maras/app/LineageUseCases.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/BanterSceneCoordinator.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/household/DeceasedQueries.h"
#include "maras/domain/lineage/ChildGrowthRules.h"
#include "maras/domain/lineage/LineageRules.h"
#include "maras/domain/lineage/NamePool.h"
#include "maras/domain/lineage/TrainingCatalog.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/ChildVisualReconciler.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <mutex>
#include <random>

namespace MARAS::App {

    namespace {

        class CachedActorMetadataProvider final : public State::IActorMetadataProvider {
        public:
            struct Entry {
                State::ActorSex sex{ State::ActorSex::Unknown };
                bool aliveAndValid{ false };
                bool dynamicPresent{ false };
                std::string name{};
                State::FormRawId raceId{};
                State::BaseId baseId{};
            };

            void Capture(RE::FormID rawId) {
                if (rawId == 0 || entries_.contains(rawId)) {
                    return;
                }
                Entry e{};
                const State::ActorId actorId{ rawId };
                if (actorId.IsUnknownFather() || actorId.IsSyntheticChild()) {
                    entries_.emplace(rawId, std::move(e));
                    return;
                }
                if (actorId.IsPlayer()) {
                    e.aliveAndValid = true;
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        if (auto* base = player->GetActorBase()) {
                            e.sex = static_cast<State::ActorSex>(static_cast<std::int8_t>(base->GetSex()));
                        }
                        if (auto* race = player->GetRace()) {
                            e.raceId = State::FormRawId{ race->GetFormID() };
                        }
                        const char* nm = player->GetName();
                        e.name = (nm && nm[0] != '\0') ? nm : "Player";
                    } else {
                        e.name = "Player";
                    }
                    e.baseId = State::BaseId{ Foundation::kPlayerBaseRawId };
                    entries_.emplace(rawId, std::move(e));
                    return;
                }

                const std::uint8_t s = ActorSvc::GetSex(rawId);
                if (s == 0) {
                    e.sex = State::ActorSex::Male;
                } else if (s == 1) {
                    e.sex = State::ActorSex::Female;
                }

                e.name = ActorSvc::GetName(rawId);
                if (auto* race = ActorSvc::GetRace(rawId)) {
                    e.raceId = State::FormRawId{ race->GetFormID() };
                }

                // Dynamic 0xFFxxxxxx spawned/cloned actors must never index under shared template BaseIDs
                if (Foundation::IsDynamicRuntimeActorRawId(rawId)) {
                    e.baseId = State::BaseId{ rawId };
                    if (auto* form = RE::TESForm::LookupByID(rawId); form && !form->IsDeleted()) {
                        if (auto* act = form->As<RE::Actor>()) {
                            e.dynamicPresent = true;
                            e.aliveAndValid = !act->IsDead();
                        }
                    }
                } else {
                    RE::FormID bId = SpatialWorldIndex::GetSingleton().GetBaseID(rawId);
                    if (bId == 0) {
                        if (auto* base = ActorSvc::GetActorBase(rawId)) {
                            bId = base->GetFormID();
                        }
                    }
                    if (bId != 0) {
                        e.baseId = State::BaseId{ bId };
                    }
                    if (auto* loaded = SpatialWorldIndex::GetSingleton().GetActorFromFormId(rawId)) {
                        e.aliveAndValid = !loaded->IsDead() && !loaded->IsDeleted();
                    } else if (auto* form = RE::TESForm::LookupByID(rawId)) {
                        if (!form->IsDeleted()) {
                            if (auto* act = form->As<RE::Actor>()) {
                                e.aliveAndValid = !act->IsDead();
                            } else {
                                e.aliveAndValid = ActorSvc::IsValid(rawId);
                            }
                        }
                    } else {
                        e.aliveAndValid = ActorSvc::IsValid(rawId);
                    }
                }
                entries_.emplace(rawId, std::move(e));
            }

            State::ActorSex GetSex(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.sex;
                }
                return State::ActorSex::Unknown;
            }

            bool IsAliveAndValid(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.aliveAndValid;
                }
                return false;
            }

            bool IsDynamicActorPresent(State::RefId refId) const noexcept override {
                if (const auto it = entries_.find(refId.raw()); it != entries_.end()) {
                    return it->second.dynamicPresent;
                }
                return false;
            }

            std::string GetActorName(State::ActorId actor) const override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.name;
                }
                return {};
            }

            State::FormRawId GetActorRaceId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.raceId;
                }
                return {};
            }

            State::BaseId GetActorBaseId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.baseId;
                }
                return {};
            }

        private:
            std::unordered_map<RE::FormID, Entry> entries_{};
        };

        struct PendingMarriageAdoptionsState {
            std::mutex mutex;
            std::unordered_set<RE::FormID> childIDs;
        };

        PendingMarriageAdoptionsState& GetPendingMarriageAdoptionsState() {
            static PendingMarriageAdoptionsState instance;
            return instance;
        }

        void MarkMarriageAdoptionPending(RE::FormID childID) {
            auto& state = GetPendingMarriageAdoptionsState();
            std::lock_guard lock(state.mutex);
            state.childIDs.insert(childID);
        }

        void ClearMarriageAdoptionPending(RE::FormID childID) {
            auto& state = GetPendingMarriageAdoptionsState();
            std::lock_guard lock(state.mutex);
            state.childIDs.erase(childID);
        }

        bool IsMarriageAdoptionPending(RE::FormID childID) {
            auto& state = GetPendingMarriageAdoptionsState();
            std::lock_guard lock(state.mutex);
            return state.childIDs.contains(childID);
        }

        void ClearAllPendingMarriageAdoptions() {
            auto& state = GetPendingMarriageAdoptionsState();
            std::lock_guard lock(state.mutex);
            state.childIDs.clear();
        }

        class MarriageAdoptionCallback final : public RE::BSScript::IStackCallbackFunctor {
        public:
            MarriageAdoptionCallback(RE::FormID childID, RE::FormID spouseID)
                : childID_(childID), spouseID_(spouseID), epoch_(GetSessionEpoch()) {}
            ~MarriageAdoptionCallback() override = default;

            void operator()(RE::BSScript::Variable /*a_result*/) override {
                if (!IsSessionEpochCurrent(epoch_)) {
                    return;
                }
                ClearMarriageAdoptionPending(childID_);
                const RE::FormID cID = childID_;
                const RE::FormID spID = spouseID_;
                const SessionEpoch expectedEpoch = epoch_;
                AddEpochTask(expectedEpoch, "MarriageAdoptionCallback::CompleteAdoption", [cID, spID, expectedEpoch]() {
                    std::lock_guard lock(GetPapyrusMutex());
                    if (!IsSessionEpochCurrent(expectedEpoch)) {
                        return;
                    }
                    const auto snap = State::WorldStore::GetSingleton().Snapshot();
                    const auto* edge = snap->relationships->FindEdge(
                        State::CanonicalPairKey{ State::ActorId::Player(), State::ActorId{ spID } });
                    if (!edge || edge->status != Foundation::RelationshipStatus::Married) {
                        return;
                    }
                    const auto* childRec = snap->lineage->FindChild(State::ChildId{ cID });
                    if (!childRec) {
                        return;
                    }
                    if (!childRec->guardians.HasGuardian(State::ActorId::Player())) {
                        LineageUseCases::GetSingleton().RegisterChildActor(
                            cID,
                            kPlayerFormID,
                            spID,
                            0,
                            childRec->bioMother.raw(),
                            childRec->bioFather.raw(),
                            0,
                            static_cast<std::uint8_t>(childRec->stage),
                            childRec->isAdopted,
                            true);
                    }
                });
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& /*a_object*/) override {}

        private:
            RE::FormID childID_{ 0 };
            RE::FormID spouseID_{ 0 };
            SessionEpoch epoch_{ 0 };
        };

        [[nodiscard]] bool IsSyntheticId(RE::FormID id) noexcept {
            return Foundation::IsSyntheticChildRawId(id);
        }

        [[nodiscard]] float GetCurrentGameDays() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

        [[nodiscard]] bool IsActorDeadInWorld(RE::FormID actorID) {
            if (actorID == 0) {
                return false;
            }
            if (LineageUseCases::GetSingleton().IsDeceased(actorID)) {
                return true;
            }
            if (auto* loaded = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actorID)) {
                return loaded->IsDead();
            }
            if (auto* act = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                return act->IsDead();
            }
            return false;
        }

        [[nodiscard]] bool IsCoupleMarriedInSnap(
            const State::WorldStateSnapshot& snap,
            RE::FormID actorA,
            RE::FormID actorB) noexcept {
            if (actorA == 0 || actorB == 0) {
                return false;
            }
            const auto* edge = snap.relationships->FindEdge(
                State::CanonicalPairKey{ State::ActorId{ actorA }, State::ActorId{ actorB } });
            return edge != nullptr && edge->status == Foundation::RelationshipStatus::Married;
        }

        [[nodiscard]] RE::FormID GetPrimaryPartnerInSnap(
            const State::WorldStateSnapshot& snap,
            RE::FormID actorID) noexcept {
            if (actorID == 0) {
                return 0;
            }
            const State::ActorId target{ actorID };
            RE::FormID engaged = 0;
            RE::FormID dating = 0;
            for (const auto& pair : snap.relationships->GetAdjacentPairs(target)) {
                const auto* edge = snap.relationships->FindEdge(pair);
                if (!edge) {
                    continue;
                }
                const RE::FormID other = pair.Other(target).raw();
                if (other == 0) {
                    continue;
                }
                if (edge->status == Foundation::RelationshipStatus::Married) {
                    return other;
                }
                if (edge->status == Foundation::RelationshipStatus::Engaged && engaged == 0) {
                    engaged = other;
                } else if (edge->status == Foundation::RelationshipStatus::Dating && dating == 0) {
                    dating = other;
                }
            }
            return engaged != 0 ? engaged : dating;
        }

        void ResolveFormIDsForDeceasedQuery(RE::FormID inputFormID, RE::FormID& outBaseID, RE::FormID& outRefID) {
            outBaseID = 0;
            outRefID = 0;
            if (inputFormID == 0) {
                return;
            }
            if (Utils::MatchesActor(inputFormID, kPlayerFormID)) {
                outBaseID = kPlayerFormID;
                outRefID = kPlayerFormID;
                return;
            }
            if (Foundation::IsDynamicRuntimeActorRawId(inputFormID)) {
                outRefID = inputFormID;
                outBaseID = inputFormID;
                return;
            }
            auto& idx = SpatialWorldIndex::GetSingleton();
            outRefID = idx.GetRefID(inputFormID);
            if (outRefID == 0) {
                outRefID = inputFormID;
            }
            outBaseID = idx.GetBaseID(inputFormID);
            if (outBaseID == 0) {
                if (auto* base = ActorSvc::GetActorBase(inputFormID)) {
                    outBaseID = base->GetFormID();
                }
            }
            if (outBaseID == 0) {
                outBaseID = inputFormID;
            }
        }

        void StripVanillaMarriageFactionsOnMainThread(RE::FormID npcFormID) {
            if (npcFormID == 0 || IsSyntheticId(npcFormID)) {
                return;
            }
            auto strip = [npcFormID]() {
                FactionReconciler::InternalFactionScope scope;
                auto& cache = FormCache::GetSingleton();
                Utils::RemoveFromFaction(npcFormID, cache.GetMarriageAskedFaction());
                Utils::RemoveFromFaction(npcFormID, cache.GetCourtingFaction());
                Utils::RemoveFromFaction(npcFormID, cache.GetMarriedFaction());
                Utils::RemoveFromFaction(npcFormID, cache.GetPlayerFaction());
                Utils::RemoveFromFaction(npcFormID, cache.GetPlayerBedOwnershipFaction());
            };
            if (DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                strip();
            } else {
                AddCurrentEpochTask("LineageUseCases::StripVanillaMarriageFactions", [strip]() {
                    std::lock_guard lock(GetPapyrusMutex());
                    strip();
                });
            }
        }

        [[nodiscard]] RE::FormID ResolveSingleCustodianCandidate(
            const State::WorldStateSnapshot& snap,
            const State::ChildRecord& rec,
            RE::FormID parentA,
            RE::FormID parentB) {
            if (Utils::MatchesActor(parentA, kPlayerFormID) || Utils::MatchesActor(parentB, kPlayerFormID)) {
                return kPlayerFormID;
            }
            auto tryParent = [&](RE::FormID candidate) -> RE::FormID {
                if (candidate == 0) {
                    return 0;
                }
                if (Utils::MatchesActor(rec.bioMother.raw(), candidate) &&
                    !LineageUseCases::GetSingleton().IsDeceased(candidate)) {
                    return candidate;
                }
                return 0;
            };
            if (RE::FormID m = tryParent(parentA); m != 0) {
                return m;
            }
            if (RE::FormID m = tryParent(parentB); m != 0) {
                return m;
            }
            if (parentA != 0 && !LineageUseCases::GetSingleton().IsDeceased(parentA)) {
                return parentA;
            }
            if (parentB != 0 && !LineageUseCases::GetSingleton().IsDeceased(parentB)) {
                return parentB;
            }
            (void)snap;
            return parentA != 0 ? parentA : parentB;
        }

        void RefreshGuardiansAndChildFactions(RE::FormID childID, RE::FormID gA, RE::FormID gB) {
            if (childID != 0 && !IsSyntheticId(childID)) {
                FactionReconciler::GetSingleton().ReconcileActor(childID);
            }
            if (gA != 0 && !Utils::MatchesActor(gA, kPlayerFormID)) {
                FactionReconciler::GetSingleton().ReconcileActor(gA);
            }
            if (gB != 0 && !Utils::MatchesActor(gB, kPlayerFormID) && gB != gA) {
                FactionReconciler::GetSingleton().ReconcileActor(gB);
            }
        }

        void ClearAdoptableFactionDirect(RE::FormID childID) {
            if (childID == 0 || IsSyntheticId(childID)) {
                return;
            }
            PlayerChildrenEngineOps::GetSingleton().ClearAdoptableFactionOnRemove(childID);
        }

        [[nodiscard]] nlohmann::json SerializeChildHouseholdState(
            RE::FormID childID,
            const State::ChildHouseholdRecord& st) {
            nlohmann::json c;
            c["childFormId"] = std::format("{:08X}", childID);
            c["name"] = ActorSvc::GetName(childID);
            c["currentHouseFormId"] = st.currentHouseId.IsNull() ? "" : std::format("{:08X}", st.currentHouseId.raw());
            c["currentHouseName"] = st.currentHouseId.IsNull()
                ? ""
                : HouseholdEngineOps::GetHouseName(st.currentHouseId.raw());
            c["homeMarkerFormId"] = st.homeMarkerId.IsNull() ? "" : std::format("{:08X}", st.homeMarkerId.raw());
            c["petFormId"] = st.petId.IsNull() ? "" : std::format("{:08X}", st.petId.raw());
            c["critterFormId"] = st.critterId.IsNull() ? "" : std::format("{:08X}", st.critterId.raw());
            c["currentOrder"] = st.currentOrder;
            c["orderExpiryGameDay"] = st.orderExpiryGameDay;
            c["critterBanned"] = st.critterBanned;
            c["petBanned"] = st.petBanned;
            c["isNewlyAdopted"] = st.isNewlyAdopted;
            c["forcegreetPending"] = st.forcegreetPending;
            c["playedScene1"] = st.playedScene1;
            c["playedScene2"] = st.playedScene2;
            c["lastInteractionTimestamp"] = st.lastInteractionTimestamp;
            return c;
        }

    }  // namespace

    LineageUseCases& LineageUseCases::GetSingleton() {
        static LineageUseCases instance;
        return instance;
    }

    std::unique_ptr<State::IActorMetadataProvider> LineageUseCases::CaptureActorMetadata(
        const std::vector<RE::FormID>& actorIDs) {
        auto provider = std::make_unique<CachedActorMetadataProvider>();
        for (const RE::FormID id : actorIDs) {
            provider->Capture(id);
        }
        return provider;
    }

    std::unique_ptr<State::IActorMetadataProvider> LineageUseCases::CaptureWorldActorMetadata(
        const std::shared_ptr<const State::WorldStateSnapshot>& snap,
        const std::vector<RE::FormID>& extraActorIDs) {
        auto provider = std::make_unique<CachedActorMetadataProvider>();
        provider->Capture(kPlayerFormID);
        for (const RE::FormID id : extraActorIDs) {
            provider->Capture(id);
        }
        if (snap) {
            for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
                (void)prof;
                provider->Capture(refId.raw());
            }
            for (const auto& [pair, edge] : snap->relationships->AllEdges()) {
                (void)edge;
                provider->Capture(pair.first.raw());
                provider->Capture(pair.second.raw());
            }
            for (const auto& [cId, rec] : snap->lineage->AllChildren()) {
                (void)cId;
                provider->Capture(rec.childId.raw());
                provider->Capture(rec.guardians.slotA.raw());
                provider->Capture(rec.guardians.slotB.raw());
                provider->Capture(rec.bioMother.raw());
                provider->Capture(rec.bioFather.raw());
                provider->Capture(rec.presumedFather.raw());
            }
        }
        return provider;
    }

    Foundation::DeterministicRng LineageUseCases::MakeEngineRng() {
        std::random_device rd;
        const std::uint32_t seed =
            static_cast<std::uint32_t>(rd()) ^ (static_cast<std::uint32_t>(rd()) << 16);
        return Foundation::DeterministicRng(seed != 0 ? seed : 0x517CC1B7u);
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void LineageUseCases::FinalizeCoSaveLoad(bool loadedAny) {
        ClearAllPendingMarriageAdoptions();
        if (!loadedAny) {
            return;
        }
        ReconcileChildHouseholdOnLoad();
        UpdateAllSpousesChildrenFaction();
        RefreshAllChildVisuals();
        MARAS_LOG_INFO("LineageUseCases::FinalizeCoSaveLoad completed");
    }

    void LineageUseCases::ProcessDayChange(float currentGameDay) {
        if (currentGameDay <= 0.0f) {
            return;
        }

        std::vector<State::ChildId> expiredChildren;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            expiredChildren = Domain::Lineage::ApplyExpiredOrders(ws, currentGameDay);
        });

        for (const State::ChildId cid : expiredChildren) {
            PlayerChildrenEngineOps::GetSingleton().ApplyOrderOnActorFromWorld(cid.raw());
        }

        CheckChildrenTrainingProgress(currentGameDay);
    }

    void LineageUseCases::RefreshAllChildVisuals() {
        ChildVisualReconciler::GetSingleton().ReassertAllChildVisualsOnLoad();
    }

    void LineageUseCases::Revert() {
        ClearAllPendingMarriageAdoptions();
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.lineage.Write().Clear();
            ws.deceased.Write().Clear();
            ws.household.Write().ClearOnlyPlayerChildrenRecords();
            ws.household.Write().ClearChildFactionSnapshots();
            ws.household.Write().ClearAdultChildTravel();
            ws.household.Write().SetChildChestIntroduced(false);
        });
        ChildVisualReconciler::GetSingleton().Revert();
        MARAS_LOG_INFO("LineageUseCases reverted to clean state");
    }

    // =========================================================================
    // Child Registration, Promotion, Custody & Removal
    // =========================================================================

    bool LineageUseCases::IsChildBlockedFromAdoption(RE::FormID childID, std::string* outReason) const {
        if (childID == 0) {
            return false;
        }
        Foundation::RefId refId{ childID };
        Foundation::BaseId baseId{};
        if (auto* form = RE::TESForm::LookupByID(childID)) {
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                refId = Foundation::RefId{ refr->GetFormID() };
                if (auto* baseObj = refr->GetBaseObject()) {
                    baseId = Foundation::BaseId{ baseObj->GetFormID() };
                }
            } else if (auto* npc = form->As<RE::TESNPC>()) {
                baseId = Foundation::BaseId{ npc->GetFormID() };
            }
        }
        bool db01ActiveUncompleted = false;
        if (auto* db01 = FormCache::GetSingleton().GetDB01()) {
            db01ActiveUncompleted = db01->IsActive() && !db01->IsCompleted();
        }
        return Domain::Lineage::IsChildBlockedFromAdoption(refId, baseId, db01ActiveUncompleted, outReason);
    }

    bool LineageUseCases::RegisterChildActor(
        RE::FormID childID,
        RE::FormID guardianAID,
        RE::FormID guardianBID,
        RE::FormID houseLocationID,
        RE::FormID bioMotherID,
        RE::FormID bioFatherID,
        std::int32_t syntheticID,
        std::uint8_t stage,
        std::optional<bool> explicitIsAdopted,
        bool sendEvent) {
        if (childID == 0) {
            return false;
        }
        ClearMarriageAdoptionPending(childID);

        std::string blockReason;
        if (IsChildBlockedFromAdoption(childID, &blockReason)) {
            MARAS_LOG_WARN("[Children] RegisterChildActor blocked - child {:08X} cannot be adopted: {}",
                           childID, blockReason);
            return false;
        }

        if (guardianAID != 0) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(guardianAID);
        }
        if (guardianBID != 0) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(guardianBID);
        }

        const auto currentDay = static_cast<std::int32_t>(GetCurrentGameDays());
        const std::string childName = ActorSvc::GetName(childID);
        const auto childStage = static_cast<Foundation::ChildStage>(stage);

        State::ChildRecord rec{};
        rec.childId = State::ChildId{ childID };
        rec.name = childName;
        rec.timestamp = currentDay;
        rec.isAdopted = false;
        rec.bioMother = State::ActorId{ bioMotherID };
        rec.bioFather = State::ActorId{ bioFatherID };
        rec.stage = childStage;

        RE::FormID oldGuardianA = 0;
        RE::FormID oldGuardianB = 0;
        bool wasAlreadyAdopted = false;
        bool hadPreExistingEntry = false;
        bool hasSynthErase = false;
        RE::FormID synthFormID = static_cast<RE::FormID>(syntheticID);

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* preChildRec = preSnap->lineage->FindChild(State::ChildId{ childID });
        const bool wasAlreadyAtStage =
            (syntheticID == 0) && (preChildRec != nullptr) && (preChildRec->stage == childStage);

        if (syntheticID != 0) {
            if (const auto* synthRec = preSnap->lineage->FindChild(State::ChildId{ synthFormID })) {
                hasSynthErase = true;
                hadPreExistingEntry = true;
                rec = *synthRec;
                oldGuardianA = rec.guardians.slotA.raw();
                oldGuardianB = rec.guardians.slotB.raw();
                wasAlreadyAdopted = rec.isAdopted;
                rec.childId = State::ChildId{ childID };
                rec.stage = childStage;
                if (!childName.empty()) {
                    rec.name = childName;
                }
                if (guardianAID != 0 || guardianBID != 0) {
                    rec.guardians.Assign(State::ActorId{ guardianAID }, State::ActorId{ guardianBID });
                }
                if (bioMotherID != 0) {
                    rec.bioMother = State::ActorId{ bioMotherID };
                }
                if (bioFatherID != 0) {
                    rec.bioFather = State::ActorId{ bioFatherID };
                }
            } else {
                rec.guardians.Assign(State::ActorId{ guardianAID }, State::ActorId{ guardianBID });
            }
        } else if (preChildRec != nullptr) {
            hadPreExistingEntry = true;
            rec = *preChildRec;
            oldGuardianA = rec.guardians.slotA.raw();
            oldGuardianB = rec.guardians.slotB.raw();
            wasAlreadyAdopted = rec.isAdopted;
            rec.stage = childStage;
            if (!childName.empty()) {
                rec.name = childName;
            }
            rec.guardians.Assign(State::ActorId{ guardianAID }, State::ActorId{ guardianBID });
            if (bioMotherID != 0) {
                rec.bioMother = State::ActorId{ bioMotherID };
            }
            if (bioFatherID != 0) {
                rec.bioFather = State::ActorId{ bioFatherID };
            }
        } else {
            rec.guardians.Assign(State::ActorId{ guardianAID }, State::ActorId{ guardianBID });
        }

        if (explicitIsAdopted.has_value()) {
            rec.isAdopted = *explicitIsAdopted;
        } else if (syntheticID != 0) {
            rec.isAdopted = false;
        } else {
            const bool hasAnyGuardian = rec.guardians.HasAnyGuardian();
            const bool gAIsBio = !rec.guardians.slotA.IsNull() &&
                                 (Utils::MatchesActor(rec.guardians.slotA.raw(), rec.bioMother.raw()) ||
                                  Utils::MatchesActor(rec.guardians.slotA.raw(), rec.bioFather.raw()));
            const bool gBIsBio = !rec.guardians.slotB.IsNull() &&
                                 (Utils::MatchesActor(rec.guardians.slotB.raw(), rec.bioMother.raw()) ||
                                  Utils::MatchesActor(rec.guardians.slotB.raw(), rec.bioFather.raw()));
            rec.isAdopted = hasAnyGuardian && !gAIsBio && !gBIsBio;
        }

        if (rec.stage != Foundation::ChildStage::Unborn && rec.stage != Foundation::ChildStage::Adult &&
            rec.guardians.Count() == 2 &&
            !IsCoupleMarriedInSnap(*preSnap, rec.guardians.slotA.raw(), rec.guardians.slotB.raw())) {
            const RE::FormID soleCustodian = ResolveSingleCustodianCandidate(
                *preSnap, rec, rec.guardians.slotA.raw(), rec.guardians.slotB.raw());
            rec.guardians.Assign(State::ActorId{ soleCustodian }, State::ActorId{});
        }

        if (!rec.guardians.slotA.IsNull()) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(rec.guardians.slotA.raw());
        }
        if (!rec.guardians.slotB.IsNull()) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(rec.guardians.slotB.raw());
        }

        // Capture werewolf status BEFORE WorldStore::Modify (engine queries stay outside lock)
        if (!rec.beastbloodEvaluated &&
            rec.stage != Foundation::ChildStage::Unborn &&
            !IsSyntheticId(childID)) {
            const bool motherWW = !rec.bioMother.IsNull() && ChildSpawnService::IsActorWerewolf(rec.bioMother.raw());
            const bool fatherWW = (!rec.bioFather.IsNull() && !rec.bioFather.IsUnknownFather()) &&
                                  ChildSpawnService::IsActorWerewolf(rec.bioFather.raw());
            Domain::Lineage::EvaluateChildBeastbloodInheritance(rec, motherWW, fatherWW);
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& lineageW = ws.lineage.Write();
            if (hasSynthErase) {
                const State::ChildId synthKey{ synthFormID };
                lineageW.EraseChild(synthKey);
                if (synthFormID != childID) {
                    ws.household.Write().EvictTenant(synthFormID);
                    ws.household.Write().EraseChildState(synthKey);
                    ws.household.Write().EraseChildFactionSnapshot(synthKey);
                }
            }
            lineageW.UpsertChild(rec);
        });

        if (hasSynthErase) {
            if (synthFormID != childID) {
                if (!IsSyntheticId(synthFormID)) {
                    ChildVisualReconciler::GetSingleton().ReleaseRuntimeBaseForActor(synthFormID);
                }
                OnChildRemovedFromHousehold(synthFormID);
                PackageReconciler::GetSingleton().ReconcileActor(synthFormID);
            }
            GetUiBridgeGateway().OnChildRemoved(synthFormID);
            GetUiBridgeGateway().OnChildPromoted(synthFormID, childID);
        }

        if (!IsSyntheticId(childID) &&
            (rec.stage == Foundation::ChildStage::Child || rec.stage == Foundation::ChildStage::Adult)) {
            InitializeAndApplyChildVisualsAndRPG(childID);
            if (auto refreshed = GetChildEntry(childID)) {
                rec = *refreshed;
            }
        }

        MARAS_LOG_INFO(
            "[Children] Registered child actor {:08X} ('{}', stage={}, adopted={}, bioM={:08X}, bioF={:08X}) to guardians {:08X} / {:08X}",
            childID, rec.name, static_cast<int>(rec.stage), rec.isAdopted,
            rec.bioMother.raw(), rec.bioFather.raw(),
            rec.guardians.slotA.raw(), rec.guardians.slotB.raw());

        const auto guardianDiff = Utils::DiffGuardianSets(
            oldGuardianA, oldGuardianB, rec.guardians.slotA.raw(), rec.guardians.slotB.raw());

        UpdateChildBothParentsFaction(childID);
        if (!guardianDiff.removedA.IsNull()) {
            UpdateSpouseChildrenFaction(guardianDiff.removedA.raw());
        }
        if (!guardianDiff.removedB.IsNull()) {
            UpdateSpouseChildrenFaction(guardianDiff.removedB.raw());
        }
        if (!rec.guardians.slotA.IsNull()) {
            UpdateSpouseChildrenFaction(rec.guardians.slotA.raw());
        }
        if (!rec.guardians.slotB.IsNull()) {
            UpdateSpouseChildrenFaction(rec.guardians.slotB.raw());
        }

        SyncChildHouseholdState(childID, rec.guardians.slotA.raw(), rec.guardians.slotB.raw(), houseLocationID);

        // Trigger SkyrimNet profile generation strictly when a placed Stage 2 or Stage 4 actor is newly spawned/promoted
        if (sendEvent && !IsSyntheticId(childID) && !wasAlreadyAtStage &&
            (rec.stage == Foundation::ChildStage::Child || rec.stage == Foundation::ChildStage::Adult) &&
            (syntheticID != 0 || !rec.bioMother.IsNull() || !rec.bioFather.IsNull())) {
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().RequestChildProfileUpdate(childID, rec.stage);
            }
        }

        if (sendEvent && rec.isAdopted && !wasAlreadyAdopted) {
            if (syntheticID == 0 && rec.guardians.HasGuardian(State::ActorId::Player())) {
                const auto coG = rec.guardians.OtherGuardian(State::ActorId::Player());
                if (!coG.IsNull() && !coG.IsPlayer()) {
                    AffectionEventCoordinator::GetSingleton().OnChildAdopted(coG.raw());
                }
            }
            SendChildEvent("adopted", childID, rec.guardians.slotA.raw(), rec.guardians.slotB.raw(), 0, "adoption");
        } else if (sendEvent && hadPreExistingEntry && guardianDiff.changed) {
            SendChildEvent(
                "guardians_changed", childID,
                rec.guardians.slotA.raw(), rec.guardians.slotB.raw(),
                guardianDiff.PrimaryRemoved().raw(), "custody_updated");
        }

        if (sendEvent) {
            GetUiBridgeGateway().OnChildUpdated(rec);
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCDetail(childID);
            if (!rec.guardians.slotA.IsNull() && !rec.guardians.slotA.IsPlayer()) {
                GetUiBridgeGateway().SendNPCDetail(rec.guardians.slotA.raw());
            }
            if (!rec.guardians.slotB.IsNull() && !rec.guardians.slotB.IsPlayer()) {
                GetUiBridgeGateway().SendNPCDetail(rec.guardians.slotB.raw());
            }
        }
        return true;
    }

    bool LineageUseCases::RemoveChild(RE::FormID childID) {
        if (childID == 0) {
            return false;
        }
        ClearMarriageAdoptionPending(childID);

        std::optional<State::ChildRecord> preRec;
        Domain::Lineage::ChildGuardianMutationOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (const auto* found = ws.lineage.Read().FindChild(State::ChildId{ childID })) {
                preRec = *found;
            }
            outcome = Domain::Lineage::ApplyRemoveChild(ws, State::ChildId{ childID });
        });

        if (!outcome.changed || !preRec.has_value()) {
            return false;
        }

        const bool inPlayerHousehold = preRec->guardians.HasGuardian(State::ActorId::Player());
        if (inPlayerHousehold) {
            OnChildRemovedFromHousehold(childID);
            ClearAdoptableFactionDirect(childID);
        } else {
            RemoveChildSandboxAroundParentLinkedRef(childID);
        }

        if (!IsSyntheticId(childID)) {
            PackageReconciler::GetSingleton().UnregisterActor(childID);
            ChildVisualReconciler::GetSingleton().ReleaseRuntimeBaseForActor(childID);
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }

        for (const auto affected : outcome.affectedActors) {
            if (!affected.IsNull() && !affected.IsPlayer()) {
                UpdateSpouseChildrenFaction(affected.raw());
            }
        }
        UpdateAllSpousesChildrenFaction();

        GetUiBridgeGateway().OnChildRemoved(childID);
        GetUiBridgeGateway().SendOverviewData();
        if (!preRec->guardians.slotA.IsNull() && !preRec->guardians.slotA.IsPlayer()) {
            GetUiBridgeGateway().SendNPCDetail(preRec->guardians.slotA.raw());
        }
        if (!preRec->guardians.slotB.IsNull() && !preRec->guardians.slotB.IsPlayer()) {
            GetUiBridgeGateway().SendNPCDetail(preRec->guardians.slotB.raw());
        }

        MARAS_LOG_INFO("[Children] Removed child {:08X} ('{}')", childID, preRec->name);
        return true;
    }

    bool LineageUseCases::SetChildGuardian(RE::FormID childID, RE::FormID guardianID, bool isAdd) {
        if (childID == 0 || guardianID == 0) {
            return false;
        }
        if (isAdd) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(guardianID);
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        auto meta = CaptureWorldActorMetadata(preSnap, { guardianID });

        Domain::Lineage::ChildGuardianMutationOutcome outcome{};
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplySetChildGuardian(
                ws, State::ChildId{ childID }, State::ActorId{ guardianID }, isAdd, *meta);
            if (outcome.changed) {
                ws.SyncChildHouseholdState(State::ChildId{ childID });
                if (const auto* rec = ws.lineage.Read().FindChild(State::ChildId{ childID })) {
                    updatedRec = *rec;
                }
            }
        });

        if (!outcome.changed || !updatedRec.has_value()) {
            return false;
        }

        const RE::FormID gA = updatedRec->guardians.slotA.raw();
        const RE::FormID gB = updatedRec->guardians.slotB.raw();
        SyncChildHouseholdState(childID, gA, gB);
        UpdateChildBothParentsFaction(childID);
        for (const auto actor : outcome.affectedActors) {
            if (!actor.IsNull() && !actor.IsPlayer()) {
                UpdateSpouseChildrenFaction(actor.raw());
            }
        }
        UpdateAllSpousesChildrenFaction();

        SendChildEvent(
            isAdd ? "guardian_added" : "guardian_removed",
            childID, gA, gB, guardianID,
            isAdd ? "guardian_added" : "guardian_removed");

        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendOverviewData();
        GetUiBridgeGateway().SendNPCDetail(childID);
        if (!Utils::MatchesActor(guardianID, kPlayerFormID)) {
            GetUiBridgeGateway().SendNPCDetail(guardianID);
        }
        return true;
    }

    bool LineageUseCases::AddChildGuardian(RE::FormID childID, RE::FormID guardianID) {
        return SetChildGuardian(childID, guardianID, true);
    }

    bool LineageUseCases::RemoveChildGuardian(RE::FormID childID, RE::FormID guardianID) {
        return SetChildGuardian(childID, guardianID, false);
    }

    bool LineageUseCases::SetBioMother(RE::FormID childID, RE::FormID motherID) {
        if (childID == 0) {
            return false;
        }
        if (motherID != 0) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(motherID);
        }
        std::optional<State::ChildRecord> updatedRec;
        const bool motherWW = (motherID != 0) && ChildSpawnService::IsActorWerewolf(motherID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            rec->bioMother = State::ActorId{ motherID };
            if (motherWW && !rec->hasBeastblood) {
                rec->hasBeastblood = true;
            }
            updatedRec = *rec;
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    bool LineageUseCases::SetBioFather(RE::FormID childID, RE::FormID fatherID) {
        if (childID == 0) {
            return false;
        }
        if (fatherID != 0 && fatherID != kUnknownFatherFormID) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(fatherID);
        }
        std::optional<State::ChildRecord> updatedRec;
        const bool fatherWW = (fatherID != 0 && fatherID != kUnknownFatherFormID) &&
                              ChildSpawnService::IsActorWerewolf(fatherID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            rec->bioFather = State::ActorId{ fatherID };
            if (fatherWW && !rec->hasBeastblood) {
                rec->hasBeastblood = true;
            }
            updatedRec = *rec;
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    bool LineageUseCases::SetPresumedFather(RE::FormID childID, RE::FormID fatherID) {
        if (childID == 0) {
            return false;
        }
        if (fatherID != 0) {
            RelationshipUseCases::GetSingleton().EnsureRegistered(fatherID);
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            rec->presumedFather = State::ActorId{ fatherID };
            updatedRec = *rec;
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    void LineageUseCases::AddPendingChild(
        RE::FormID childBaseID,
        RE::FormID parentABaseID,
        RE::FormID parentBBaseID,
        bool isAdopted) {
        if (childBaseID == 0) {
            return;
        }
        State::PendingChildEntry pending{};
        pending.childBaseId = State::BaseId{ childBaseID };
        pending.parentABaseId = State::BaseId{ parentABaseID };
        pending.parentBBaseId = State::BaseId{ parentBBaseID };
        pending.isAdopted = isAdopted;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.lineage.Write().AddPendingChild(State::BaseId{ childBaseID }, pending);
        });
    }

    void LineageUseCases::TryResolvePendingChildren(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0) {
            return;
        }

        auto resolveBioParents = [](RE::FormID gA, RE::FormID gB, bool isAdopted) -> std::pair<RE::FormID, RE::FormID> {
            if (isAdopted) {
                return { 0, 0 };
            }
            RE::FormID bioM = 0;
            RE::FormID bioF = 0;
            auto assignBySex = [&](RE::FormID pID) {
                if (pID == 0) {
                    return;
                }
                const std::uint8_t s = ActorSvc::GetSex(pID);
                if (s == 1 && bioM == 0) {
                    bioM = pID;
                } else if (s == 0 && bioF == 0) {
                    bioF = pID;
                }
            };
            assignBySex(gA);
            assignBySex(gB);
            return { bioM, bioF };
        };

        std::optional<State::PendingChildEntry> matchingSelfPending;
        std::vector<State::PendingChildEntry> parentPendingSnapshot;
        std::shared_ptr<const State::WorldStateSnapshot> snap;
        {
            snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& allPending = snap->lineage->AllPendingChildren();
            if (const auto it = allPending.find(State::BaseId{ baseID }); it != allPending.end()) {
                matchingSelfPending = it->second;
            }
            for (const auto& [cBase, entry] : allPending) {
                if (cBase.raw() != baseID &&
                    (entry.parentABaseId.raw() == baseID || entry.parentBBaseId.raw() == baseID)) {
                    parentPendingSnapshot.push_back(entry);
                }
            }
        }

        auto& idx = SpatialWorldIndex::GetSingleton();
        ScopedMemorySuppression suppressMemories;

        if (matchingSelfPending.has_value()) {
            State::PendingChildEntry pending = *matchingSelfPending;
            pending.childRefId = State::RefId{ refID };

            RE::FormID pARef = !pending.parentARefId.IsNull()
                ? pending.parentARefId.raw()
                : idx.GetRefID(pending.parentABaseId.raw());
            RE::FormID pBRef = !pending.parentBRefId.IsNull()
                ? pending.parentBRefId.raw()
                : idx.GetRefID(pending.parentBBaseId.raw());
            if (pARef == 0 && pending.parentABaseId.raw() == kPlayerFormID) {
                pARef = kPlayerFormID;
            }
            if (pBRef == 0 && pending.parentBBaseId.raw() == kPlayerFormID) {
                pBRef = kPlayerFormID;
            }
            pending.parentARefId = State::RefId{ pARef };
            pending.parentBRefId = State::RefId{ pBRef };

            if (pARef != 0 || pBRef != 0) {
                const bool awaitingOtherParent =
                    (!pending.parentABaseId.IsNull() && pARef == 0) ||
                    (!pending.parentBBaseId.IsNull() && pBRef == 0);
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (awaitingOtherParent) {
                        ws.lineage.Write().AddPendingChild(State::BaseId{ baseID }, pending);
                    } else {
                        (void)ws.lineage.Write().ErasePendingChild(State::BaseId{ baseID });
                    }
                });

                auto [bioM, bioF] = resolveBioParents(pARef, pBRef, pending.isAdopted);
                RE::FormID gA = pARef;
                RE::FormID gB = pBRef;
                if (!awaitingOtherParent && ((gA != 0 && gB == 0) || (gA == 0 && gB != 0))) {
                    const RE::FormID soleG = (gA != 0) ? gA : gB;
                    const RE::FormID spouseID = GetPrimaryPartnerInSnap(*snap, soleG);
                    if (spouseID != 0 && IsCoupleMarriedInSnap(*snap, soleG, spouseID)) {
                        gA = soleG;
                        gB = spouseID;
                    }
                }
                RegisterChildActor(
                    refID, gA, gB, 0, bioM, bioF, 0,
                    static_cast<std::uint8_t>(Foundation::ChildStage::Child),
                    pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO(
                    "TryResolvePendingChildren: Resolved pending child {:08X} for guardians {:08X} / {:08X}",
                    refID, gA, gB);
            } else {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.lineage.Write().AddPendingChild(State::BaseId{ baseID }, pending);
                });
            }
        }

        for (State::PendingChildEntry pending : parentPendingSnapshot) {
            if (pending.parentABaseId.raw() == baseID) {
                pending.parentARefId = State::RefId{ refID };
            }
            if (pending.parentBBaseId.raw() == baseID) {
                pending.parentBRefId = State::RefId{ refID };
            }
            const RE::FormID cRef = !pending.childRefId.IsNull()
                ? pending.childRefId.raw()
                : idx.GetRefID(pending.childBaseId.raw());
            pending.childRefId = State::RefId{ cRef };

            if (cRef != 0 && (!pending.parentARefId.IsNull() || !pending.parentBRefId.IsNull())) {
                const bool awaitingOtherParent =
                    (!pending.parentABaseId.IsNull() && pending.parentARefId.IsNull()) ||
                    (!pending.parentBBaseId.IsNull() && pending.parentBRefId.IsNull());
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (awaitingOtherParent) {
                        ws.lineage.Write().AddPendingChild(pending.childBaseId, pending);
                    } else {
                        (void)ws.lineage.Write().ErasePendingChild(pending.childBaseId);
                    }
                });

                auto [bioM, bioF] = resolveBioParents(
                    pending.parentARefId.raw(), pending.parentBRefId.raw(), pending.isAdopted);
                RE::FormID gA = pending.parentARefId.raw();
                RE::FormID gB = pending.parentBRefId.raw();
                if (!awaitingOtherParent && ((gA != 0 && gB == 0) || (gA == 0 && gB != 0))) {
                    const RE::FormID soleG = (gA != 0) ? gA : gB;
                    const RE::FormID spouseID = GetPrimaryPartnerInSnap(*snap, soleG);
                    if (spouseID != 0 && IsCoupleMarriedInSnap(*snap, soleG, spouseID)) {
                        gA = soleG;
                        gB = spouseID;
                    }
                }
                RegisterChildActor(
                    cRef, gA, gB, 0, bioM, bioF, 0,
                    static_cast<std::uint8_t>(Foundation::ChildStage::Child),
                    pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO(
                    "TryResolvePendingChildren: Resolved pending child {:08X} on parent load (guardians {:08X} / {:08X})",
                    cRef, gA, gB);
            } else {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.lineage.Write().AddPendingChild(pending.childBaseId, pending);
                });
            }
        }
    }

    void LineageUseCases::ClearPendingMarriageAdoptions() {
        ClearAllPendingMarriageAdoptions();
    }

    void LineageUseCases::DispatchPlayerSpouseMarriageAdoptions(
        RE::FormID spouseID,
        std::unordered_set<RE::FormID>& skippedChildIds) {
        if (spouseID == 0 || Utils::MatchesActor(spouseID, kPlayerFormID)) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cKey, rec] : snap->lineage->AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Adult || rec.stage == Foundation::ChildStage::Unborn) {
                continue;
            }
            const RE::FormID cID = cKey.raw();
            if (IsSyntheticId(cID)) {
                continue;
            }
            const State::ActorId spouseActorId{ spouseID };
            if (!rec.guardians.HasGuardian(spouseActorId) ||
                rec.guardians.HasGuardian(State::ActorId::Player())) {
                continue;
            }
            const RE::FormID otherGuardianID = rec.guardians.OtherGuardian(spouseActorId).raw();
            const bool otherGuardianDead = (otherGuardianID != 0) && IsActorDeadInWorld(otherGuardianID);
            const bool canLinkPlayerAsCoParent =
                (otherGuardianID == 0) ||
                Utils::MatchesActor(otherGuardianID, spouseID) ||
                otherGuardianDead;
            if (!canLinkPlayerAsCoParent) {
                continue;
            }
            if (IsMarriageAdoptionPending(cID)) {
                skippedChildIds.insert(cID);
                continue;
            }
            auto* childActor = RE::TESForm::LookupByID<RE::Actor>(cID);
            if (!childActor) {
                childActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(cID);
            }
            auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(spouseID);
            if (!spouseActor) {
                spouseActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(spouseID);
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (childActor && spouseActor && vm) {
                MarkMarriageAdoptionPending(cID);
                MARAS_LOG_INFO(
                    "DispatchPlayerSpouseMarriageAdoptions: Dispatching Papyrus AdoptChildGlobal for spawned child {:08X} ('{}') with spouse {:08X}",
                    cID, rec.name, spouseID);
                if (otherGuardianID != 0 && !Utils::MatchesActor(otherGuardianID, spouseID)) {
                    UpdateSpouseChildrenFaction(otherGuardianID);
                }
                const SessionEpoch expectedEpoch = GetSessionEpoch();
                DispatchPostLock(
                    expectedEpoch,
                    [cID, spouseID]() {
                        auto* innerVm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                        auto* cActor = cID != 0 ? RE::TESForm::LookupByID<RE::Actor>(cID) : nullptr;
                        auto* spActor = spouseID != 0 ? RE::TESForm::LookupByID<RE::Actor>(spouseID) : nullptr;
                        if (!innerVm || !cActor || !spActor) {
                            ClearMarriageAdoptionPending(cID);
                            return;
                        }
                        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                            new MarriageAdoptionCallback(cID, spouseID));
                        RE::TESForm* houseArg = nullptr;
                        auto args = RE::MakeFunctionArguments(
                            std::move(cActor), std::move(spActor), std::move(houseArg));
                        if (!innerVm->DispatchStaticCall(
                                "TTM_ServiceChildrenLogic", "AdoptChildGlobal", args, callback)) {
                            ClearMarriageAdoptionPending(cID);
                        }
                    },
                    "EvaluateChildCustodyOnMarriage::AdoptChildGlobal");
                skippedChildIds.insert(cID);
            }
        }
    }

    void LineageUseCases::SyncChildHouseholdState(
        RE::FormID childID,
        RE::FormID guardianAID,
        RE::FormID guardianBID,
        RE::FormID explicitHouseID) {
        if (childID == 0) {
            return;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = preSnap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            OnChildRemovedFromHousehold(childID);
            UpdateNonPlayerChildPackageOverride(childID);
            return;
        }
        if (rec->stage == Foundation::ChildStage::Adult) {
            OnChildRemovedFromHousehold(childID);
            if (!IsSyntheticId(childID)) {
                RemoveChildSandboxAroundParentLinkedRef(childID);
                PackageReconciler::GetSingleton().ReconcileActor(childID);
            }
            return;
        }
        if (rec->stage == Foundation::ChildStage::Unborn) {
            return;
        }
        const bool inPlayerHousehold =
            Utils::MatchesActor(guardianAID, kPlayerFormID) ||
            Utils::MatchesActor(guardianBID, kPlayerFormID);
        if (inPlayerHousehold) {
            RE::FormID houseID = explicitHouseID;
            if (houseID == 0) {
                const RE::FormID coGuardianID =
                    Utils::MatchesActor(guardianAID, kPlayerFormID) ? guardianBID : guardianAID;
                if (coGuardianID != 0 && !Utils::MatchesActor(coGuardianID, kPlayerFormID)) {
                    houseID = HouseholdUseCases::GetSingleton().GetTenantHouseFormID(coGuardianID);
                }
            }
            OnChildAddedToHousehold(childID, houseID);
            UpdateNonPlayerChildPackageOverride(childID);
        } else {
            OnChildRemovedFromHousehold(childID);
            UpdateNonPlayerChildPackageOverride(childID);
        }
    }

    void LineageUseCases::RefreshCustodyChildrenAfterTransition(
        const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
        const State::WorldStateSnapshot& postSnap,
        const std::unordered_set<RE::FormID>& skippedChildIds) {
        std::unordered_set<RE::FormID> spousesToRefresh;
        for (const auto& [cID, preRec] : preChildren) {
            if (skippedChildIds.contains(cID)) {
                continue;
            }
            const auto* postRec = postSnap.lineage->FindChild(State::ChildId{ cID });
            if (!postRec) {
                continue;
            }
            const auto gDiff = Utils::DiffGuardianSets(
                preRec.guardians.slotA.raw(), preRec.guardians.slotB.raw(),
                postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
            if (!gDiff.changed) {
                continue;
            }
            if (!preRec.guardians.slotA.IsNull()) {
                spousesToRefresh.insert(preRec.guardians.slotA.raw());
            }
            if (!preRec.guardians.slotB.IsNull()) {
                spousesToRefresh.insert(preRec.guardians.slotB.raw());
            }
            if (!postRec->guardians.slotA.IsNull()) {
                spousesToRefresh.insert(postRec->guardians.slotA.raw());
            }
            if (!postRec->guardians.slotB.IsNull()) {
                spousesToRefresh.insert(postRec->guardians.slotB.raw());
            }

            SyncChildHouseholdState(
                cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
            UpdateChildBothParentsFaction(cID);
            SendChildEvent(
                "guardians_changed", cID,
                postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                0, "marriage_custody_sync");
            GetUiBridgeGateway().OnChildUpdated(*postRec);
        }
        for (const RE::FormID sp : spousesToRefresh) {
            if (sp != 0 && !Utils::MatchesActor(sp, kPlayerFormID)) {
                UpdateSpouseChildrenFaction(sp);
            }
        }
    }

    void LineageUseCases::EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0) {
            return;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        for (const auto& [k, v] : preSnap->lineage->AllChildren()) {
            preChildren.emplace(k.raw(), v);
        }

        std::unordered_set<RE::FormID> skippedChildIds;
        if (Utils::MatchesActor(actorA, kPlayerFormID) || Utils::MatchesActor(actorB, kPlayerFormID)) {
            const RE::FormID spouseID = Utils::MatchesActor(actorA, kPlayerFormID) ? actorB : actorA;
            DispatchPlayerSpouseMarriageAdoptions(spouseID, skippedChildIds);
        }

        auto meta = CaptureWorldActorMetadata(preSnap, { actorA, actorB });
        State::WorldMutationResult mut{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.EvaluateCustodyOnMarriage(State::ActorId{ actorA }, State::ActorId{ actorB }, *meta);
        });

        if (mut.changed) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cID, preRec] : preChildren) {
                if (skippedChildIds.contains(cID)) {
                    continue;
                }
                const auto* postRec = postSnap->lineage->FindChild(State::ChildId{ cID });
                if (!postRec) {
                    continue;
                }
                const auto gDiff = Utils::DiffGuardianSets(
                    preRec.guardians.slotA.raw(), preRec.guardians.slotB.raw(),
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
                if (!gDiff.changed) {
                    continue;
                }
                SyncChildHouseholdState(
                    cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
                UpdateChildBothParentsFaction(cID);
                SendChildEvent(
                    "guardians_changed", cID,
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                    0, "marriage_custody");
                GetUiBridgeGateway().OnChildUpdated(*postRec);
            }
            if (!Utils::MatchesActor(actorA, kPlayerFormID)) {
                UpdateSpouseChildrenFaction(actorA);
            }
            if (!Utils::MatchesActor(actorB, kPlayerFormID)) {
                UpdateSpouseChildrenFaction(actorB);
            }
            GetUiBridgeGateway().SendOverviewData();
        }
    }

    void LineageUseCases::HandleDivorceCustodySideEffects(
        const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
        const State::WorldStateSnapshot& postSnap,
        RE::FormID actorA,
        RE::FormID actorB) {
        const RE::FormID formerSpouseID = Utils::MatchesActor(actorA, kPlayerFormID)
            ? actorB
            : (Utils::MatchesActor(actorB, kPlayerFormID) ? actorA : 0);

        for (const auto& [cID, preRec] : preChildren) {
            const auto* postRec = postSnap.lineage->FindChild(State::ChildId{ cID });
            if (!postRec) {
                continue;
            }
            const auto gDiff = Utils::DiffGuardianSets(
                preRec.guardians.slotA.raw(), preRec.guardians.slotB.raw(),
                postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
            const bool secretBecameDiscovered =
                preRec.paternitySecret.HasActiveSecret() &&
                !preRec.paternitySecret.discovered &&
                postRec->paternitySecret.discovered;
            if (!gDiff.changed && !secretBecameDiscovered) {
                continue;
            }

            if (gDiff.changed) {
                SyncChildHouseholdState(
                    cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
                UpdateChildBothParentsFaction(cID);
                const RE::FormID removedOther =
                    !gDiff.PrimaryRemoved().IsNull() ? gDiff.PrimaryRemoved().raw() : formerSpouseID;
                SendChildEvent(
                    "custody_divorce", cID,
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                    removedOther, "divorce");
            }

            if (secretBecameDiscovered) {
                const bool wasSecretPartner = preRec.paternitySecret.isSecretForPartner;
                const bool wasPregSecretPartner = preRec.paternitySecret.isPregnancySecretForPartner;
                const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(cID));
                if (wasSecretPartner) {
                    const std::string baseOutcome = "presumed_father_learnt";
                    const std::string eventOutcome = wasPregSecretPartner
                        ? (baseOutcome + "_unannounced")
                        : baseOutcome;
                    AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(cID, eventOutcome);
                    EmitModCallbackEvent(
                        "maras_paternity_secret_resolved", eventOutcome, childIdFloat, RE::FormID{ 0 });
                    if (GetSkyrimNetGateway().IsAvailable()) {
                        GetSkyrimNetGateway().EmitPaternitySecretResolved(cID, eventOutcome);
                        GetSkyrimNetGateway().RecordPaternityReveal(
                            cID, baseOutcome, "none", wasPregSecretPartner);
                    }
                } else {
                    AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(cID, "divorce_revealed");
                    EmitModCallbackEvent(
                        "maras_paternity_secret_resolved", "divorce_revealed", childIdFloat, RE::FormID{ 0 });
                    if (GetSkyrimNetGateway().IsAvailable()) {
                        GetSkyrimNetGateway().EmitPaternitySecretResolved(cID, "divorce_revealed");
                        GetSkyrimNetGateway().RecordPaternityReveal(cID, "divorce_revealed");
                    }
                }
                const std::string notifyMsg = std::format(
                    "Paternity secret for {} has been resolved following breakup.",
                    postRec->name.empty() ? "Child" : postRec->name);
                QueueHUDMessage(notifyMsg);
            }

            GetUiBridgeGateway().OnChildUpdated(*postRec);
        }

        if (!Utils::MatchesActor(actorA, kPlayerFormID)) {
            UpdateSpouseChildrenFaction(actorA);
        }
        if (!Utils::MatchesActor(actorB, kPlayerFormID)) {
            UpdateSpouseChildrenFaction(actorB);
        }

        if (!Utils::MatchesActor(actorA, kPlayerFormID)) {
            PaternityWorkflowCoordinator::GetSingleton().EvaluateBioFatherStepIn(actorA, actorB);
        }
        if (!Utils::MatchesActor(actorB, kPlayerFormID)) {
            PaternityWorkflowCoordinator::GetSingleton().EvaluateBioFatherStepIn(actorB, actorA);
        }
        GetUiBridgeGateway().SendOverviewData();
    }

    void LineageUseCases::EvaluateChildCustodyOnDivorce(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0) {
            return;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        for (const auto& [k, v] : preSnap->lineage->AllChildren()) {
            preChildren.emplace(k.raw(), v);
        }

        auto meta = CaptureWorldActorMetadata(preSnap, { actorA, actorB });
        auto rng = MakeEngineRng();
        State::WorldMutationResult mut{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.EvaluateCustodyOnDivorce(
                State::ActorId{ actorA }, State::ActorId{ actorB }, *meta, rng);
        });

        if (mut.changed) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            HandleDivorceCustodySideEffects(preChildren, *postSnap, actorA, actorB);
        }
    }

    void LineageUseCases::SyncAllMarriedCouplesChildCustody() {
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> skippedChildIds;
        for (const auto& key : preSnap->relationships->GetAdjacentPairs(State::ActorId::Player())) {
            const auto* edge = preSnap->relationships->FindEdge(key);
            if (!edge || edge->status != Foundation::RelationshipStatus::Married) {
                continue;
            }
            const RE::FormID spouseID = key.Other(State::ActorId::Player()).raw();
            DispatchPlayerSpouseMarriageAdoptions(spouseID, skippedChildIds);
        }

        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        for (const auto& [k, v] : preSnap->lineage->AllChildren()) {
            preChildren.emplace(k.raw(), v);
        }

        auto meta = CaptureWorldActorMetadata(preSnap);
        auto rng = MakeEngineRng();
        State::WorldMutationResult mut{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.SyncAllMarriedCouplesChildCustody(*meta, rng);
        });

        if (mut.changed) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            std::unordered_set<RE::FormID> spousesToRefresh;
            for (const auto& [cID, preRec] : preChildren) {
                if (skippedChildIds.contains(cID)) {
                    continue;
                }
                const auto* postRec = postSnap->lineage->FindChild(State::ChildId{ cID });
                if (!postRec) {
                    continue;
                }
                const auto gDiff = Utils::DiffGuardianSets(
                    preRec.guardians.slotA.raw(), preRec.guardians.slotB.raw(),
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
                if (!gDiff.changed) {
                    continue;
                }
                if (!preRec.guardians.slotA.IsNull()) {
                    spousesToRefresh.insert(preRec.guardians.slotA.raw());
                }
                if (!preRec.guardians.slotB.IsNull()) {
                    spousesToRefresh.insert(preRec.guardians.slotB.raw());
                }
                if (!postRec->guardians.slotA.IsNull()) {
                    spousesToRefresh.insert(postRec->guardians.slotA.raw());
                }
                if (!postRec->guardians.slotB.IsNull()) {
                    spousesToRefresh.insert(postRec->guardians.slotB.raw());
                }

                RE::FormID childHouseID = 0;
                if (const auto* st = postSnap->household->FindChildState(State::ChildId{ cID })) {
                    childHouseID = st->currentHouseId.raw();
                }
                SyncChildHouseholdState(
                    cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(), childHouseID);
                UpdateChildBothParentsFaction(cID);
                SendChildEvent(
                    "guardians_changed", cID,
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                    0, "marriage_custody_sync");
                GetUiBridgeGateway().OnChildUpdated(*postRec);
            }
            for (const RE::FormID sp : spousesToRefresh) {
                if (sp != 0 && !Utils::MatchesActor(sp, kPlayerFormID)) {
                    UpdateSpouseChildrenFaction(sp);
                }
            }
            GetUiBridgeGateway().SendOverviewData();
        }
    }

    void LineageUseCases::HandleDeathCustodySideEffects(
        const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
        const State::WorldStateSnapshot& postSnap,
        RE::FormID deceasedID) {
        std::unordered_set<RE::FormID> widowedMothers;
        std::unordered_set<RE::FormID> spousesToRefresh;

        for (const auto& [cID, preRec] : preChildren) {
            if (cID == deceasedID) {
                continue;
            }
            const auto* postRec = postSnap.lineage->FindChild(State::ChildId{ cID });
            if (!postRec) {
                // Unborn fetus died with deceased mother
                if (preRec.stage == Foundation::ChildStage::Unborn) {
                    ClearMarriageAdoptionPending(cID);
                    OnChildRemovedFromHousehold(cID);
                    if (!preRec.guardians.slotA.IsNull()) {
                        spousesToRefresh.insert(preRec.guardians.slotA.raw());
                    }
                    if (!preRec.guardians.slotB.IsNull()) {
                        spousesToRefresh.insert(preRec.guardians.slotB.raw());
                    }
                    GetUiBridgeGateway().OnChildRemoved(cID);
                }
                continue;
            }

            if (postRec->stage == Foundation::ChildStage::Unborn) {
                const bool changedUnborn =
                    preRec.guardians != postRec->guardians ||
                    preRec.presumedFather != postRec->presumedFather ||
                    preRec.paternitySecret != postRec->paternitySecret;
                if (changedUnborn) {
                    const RE::FormID survivingMother = postRec->guardians.OtherGuardian(
                        State::ActorId{ deceasedID }).raw();
                    if (survivingMother != 0 && !Utils::MatchesActor(survivingMother, kPlayerFormID) &&
                        ActorSvc::GetSex(survivingMother) == 1 &&
                        preRec.guardians.HasGuardian(State::ActorId{ deceasedID })) {
                        widowedMothers.insert(survivingMother);
                    }
                    if (!postRec->guardians.slotA.IsNull()) {
                        spousesToRefresh.insert(postRec->guardians.slotA.raw());
                    }
                    if (!postRec->guardians.slotB.IsNull()) {
                        spousesToRefresh.insert(postRec->guardians.slotB.raw());
                    }
                    GetUiBridgeGateway().OnChildUpdated(*postRec);
                }
                continue;
            }

            const bool guardiansChanged = (preRec.guardians != postRec->guardians);
            if (!guardiansChanged) {
                continue;
            }

            const RE::FormID survivingParent = preRec.guardians.OtherGuardian(State::ActorId{ deceasedID }).raw();
            if (survivingParent != 0 && !Utils::MatchesActor(survivingParent, kPlayerFormID) &&
                ActorSvc::GetSex(survivingParent) == 1) {
                widowedMothers.insert(survivingParent);
            }

            if (!postRec->guardians.slotA.IsNull()) {
                spousesToRefresh.insert(postRec->guardians.slotA.raw());
            }
            if (!postRec->guardians.slotB.IsNull()) {
                spousesToRefresh.insert(postRec->guardians.slotB.raw());
            }

            if (Utils::MatchesActor(survivingParent, kPlayerFormID)) {
                const RE::FormID reassignedSpouse = postRec->guardians.OtherGuardian(State::ActorId::Player()).raw();
                RE::FormID targetHouseID = 0;
                if (const auto* st = postSnap.household->FindChildState(State::ChildId{ cID })) {
                    targetHouseID = st->currentHouseId.raw();
                }
                if (reassignedSpouse != 0 && !Utils::MatchesActor(reassignedSpouse, deceasedID)) {
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(
                        cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(), targetHouseID);
                    if (targetHouseID != 0) {
                        HouseholdUseCases::GetSingleton().RelocateFamilyUnit(cID, targetHouseID, false);
                    }
                    spousesToRefresh.insert(reassignedSpouse);
                    SendChildEvent(
                        "inherited", cID,
                        postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                        deceasedID, "spouse_death");
                    GetUiBridgeGateway().OnChildUpdated(*postRec);
                } else {
                    UpdateChildBothParentsFaction(cID);
                    SyncChildHouseholdState(
                        cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(), targetHouseID);
                    SendChildEvent(
                        "inherited", cID,
                        postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                        deceasedID, "spouse_death");
                    GetUiBridgeGateway().OnChildUpdated(*postRec);
                }
            } else if (survivingParent != 0) {
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(
                    cID, postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw());
                spousesToRefresh.insert(survivingParent);
                SendChildEvent(
                    "inherited", cID,
                    postRec->guardians.slotA.raw(), postRec->guardians.slotB.raw(),
                    deceasedID, "parent_death");
                GetUiBridgeGateway().OnChildUpdated(*postRec);
            } else {
                UpdateChildBothParentsFaction(cID);
                SyncChildHouseholdState(cID, 0, 0);
                SendChildEvent("inherited", cID, 0, 0, deceasedID, "parent_death");
                GetUiBridgeGateway().OnChildUpdated(*postRec);
            }
        }

        for (const RE::FormID sp : spousesToRefresh) {
            if (sp != 0 && !Utils::MatchesActor(sp, kPlayerFormID)) {
                UpdateSpouseChildrenFaction(sp);
            }
        }
        UpdateSpouseChildrenFaction(deceasedID);

        for (const RE::FormID motherID : widowedMothers) {
            PaternityWorkflowCoordinator::GetSingleton().EvaluateBioFatherStepIn(motherID, deceasedID);
        }
    }

    void LineageUseCases::EvaluateChildCustodyOnDeath(RE::FormID deceasedID) {
        if (deceasedID == 0 || Utils::MatchesActor(deceasedID, kPlayerFormID)) {
            return;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        for (const auto& [k, v] : preSnap->lineage->AllChildren()) {
            preChildren.emplace(k.raw(), v);
        }

        auto meta = CaptureWorldActorMetadata(preSnap, { deceasedID });
        State::WorldMutationResult mut{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.EvaluateCustodyOnDeath(State::ActorId{ deceasedID }, *meta);
        });

        if (mut.changed) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            HandleDeathCustodySideEffects(preChildren, *postSnap, deceasedID);
        }
    }

    RE::FormID LineageUseCases::ResolveNonPlayerChildTarget(const State::ChildRecord& child) const {
        return ChildSpawnService::GetSingleton().ResolveNonPlayerChildTarget(child);
    }

    RE::FormID LineageUseCases::ResolveNonPlayerChildTargetById(RE::FormID childID) const {
        return ChildSpawnService::GetSingleton().ResolveNonPlayerChildTargetById(childID);
    }

    void LineageUseCases::UpdateNonPlayerChildPackageOverride(RE::FormID childID) {
        if (childID == 0 || IsSyntheticId(childID)) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            ChildSpawnService::GetSingleton().RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageReconciler::GetSingleton().UnregisterActor(childID);
            return;
        }
        if (rec->stage == Foundation::ChildStage::Adult ||
            rec->guardians.HasGuardian(State::ActorId::Player())) {
            ChildSpawnService::GetSingleton().RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageReconciler::GetSingleton().ReconcileActor(childID);
            return;
        }

        const RE::FormID targetRefID = ResolveNonPlayerChildTarget(*rec);
        if (targetRefID != 0) {
            SetChildSandboxAroundParentLinkedRef(childID, targetRefID);
            PackageReconciler::GetSingleton().RegisterNonPlayerChild(childID);
        } else {
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageReconciler::GetSingleton().ReconcileActor(childID);
        }
    }

    void LineageUseCases::SetChildSandboxAroundParentLinkedRef(RE::FormID childID, RE::FormID targetRefID) {
        ChildSpawnService::GetSingleton().SetChildSandboxAroundParentLinkedRef(childID, targetRefID);
    }

    void LineageUseCases::RemoveChildSandboxAroundParentLinkedRef(RE::FormID childID) {
        ChildSpawnService::GetSingleton().RemoveChildSandboxAroundParentLinkedRef(childID);
    }

    void LineageUseCases::UpdateChildBothParentsFaction(RE::FormID childID) {
        if (childID == 0) {
            return;
        }
        const auto entryOpt = GetChildEntry(childID);
        if (!IsSyntheticId(childID)) {
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }
        if (entryOpt.has_value()) {
            const RE::FormID otherParentID = entryOpt->guardians.OtherGuardian(State::ActorId::Player()).raw();
            if (otherParentID != 0 && !Utils::MatchesActor(otherParentID, kPlayerFormID)) {
                UpdateSpouseChildrenFaction(otherParentID);
            }
        }
    }

    void LineageUseCases::UpdateSpouseChildrenFaction(RE::FormID spouseID) {
        if (spouseID == 0 || Utils::MatchesActor(spouseID, kPlayerFormID) || IsSyntheticId(spouseID)) {
            return;
        }
        FactionReconciler::GetSingleton().ReconcileActor(spouseID);
    }

    void LineageUseCases::UpdateAllSpousesChildrenFaction() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> seen;
        for (const auto& pair : snap->relationships->GetAdjacentPairs(State::ActorId::Player())) {
            const auto* edge = snap->relationships->FindEdge(pair);
            if (edge && edge->status == Foundation::RelationshipStatus::Married) {
                seen.insert(pair.Other(State::ActorId::Player()).raw());
            }
        }
        for (const auto& [cKey, rec] : snap->lineage->AllChildren()) {
            (void)cKey;
            if (rec.guardians.HasGuardian(State::ActorId::Player())) {
                const RE::FormID coGuardian = rec.guardians.OtherGuardian(State::ActorId::Player()).raw();
                if (coGuardian != 0) {
                    seen.insert(coGuardian);
                }
            }
        }
        for (const RE::FormID sp : seen) {
            UpdateSpouseChildrenFaction(sp);
        }
    }

    void LineageUseCases::SendChildEvent(
        const std::string& eventType,
        RE::FormID childID,
        RE::FormID guardianAID,
        RE::FormID guardianBID,
        RE::FormID otherID,
        const std::string& reason) {
        nlohmann::json payload;
        payload["event"] = eventType;
        payload["eventType"] = eventType;
        payload["child"] = std::format("{:08X}", childID);
        payload["childId"] = static_cast<std::int32_t>(childID);
        payload["childName"] = GetChildName(childID);
        if (guardianAID != 0) {
            payload["guardianA"] = std::format("{:08X}", guardianAID);
            payload["guardianAName"] = ActorSvc::GetName(guardianAID);
        } else {
            payload["guardianA"] = "";
            payload["guardianAName"] = "";
        }
        if (guardianBID != 0) {
            payload["guardianB"] = std::format("{:08X}", guardianBID);
            payload["guardianBName"] = ActorSvc::GetName(guardianBID);
        } else {
            payload["guardianB"] = "";
            payload["guardianBName"] = "";
        }
        if (otherID != 0) {
            payload["other"] = std::format("{:08X}", otherID);
            payload["otherName"] = ActorSvc::GetName(otherID);
        } else {
            payload["other"] = "";
            payload["otherName"] = "";
        }
        payload["reason"] = reason;

        const std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        const RE::FormID senderID =
            (!IsSyntheticId(childID) && RE::TESForm::LookupByID(childID) != nullptr) ? childID : 0;
        EmitModCallbackEvent("maras_child_event", jsonStr, 0.0f, senderID);
        MARAS_LOG_INFO("Sent maras_child_event: {}", jsonStr);
        if (GetSkyrimNetGateway().IsAvailable()) {
            if (eventType == "adopted") {
                GetSkyrimNetGateway().EmitChildAdopted(childID, guardianAID, guardianBID);
            } else if (eventType == "inherited") {
                GetSkyrimNetGateway().EmitChildInherited(childID, otherID, guardianAID, guardianBID);
            } else if (eventType == "guardians_changed" || eventType == "custody_divorce") {
                GetSkyrimNetGateway().EmitChildParentsChanged(childID, guardianAID, guardianBID, reason);
            } else if (eventType == "sent_to_training") {
                GetSkyrimNetGateway().EmitChildSentToTraining(childID, GetChildName(childID), reason);
            } else if (eventType == "became_adult") {
                GetSkyrimNetGateway().EmitChildBecameAdult(childID, GetChildName(childID));
            }
            GetSkyrimNetGateway().RecordChildCustodyEvent(
                eventType, childID, guardianAID, guardianBID, otherID, reason);
        }
    }

    // =========================================================================
    // Strict Read-Only Child & Naming Queries (CQS)
    // =========================================================================

    std::optional<State::ChildRecord> LineageUseCases::GetChildEntry(RE::FormID childID) const {
        if (childID == 0) {
            return std::nullopt;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->lineage->FindChild(State::ChildId{ childID })) {
            return *rec;
        }
        return std::nullopt;
    }

    std::unordered_map<RE::FormID, State::ChildRecord> LineageUseCases::GetAllChildren() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<RE::FormID, State::ChildRecord> out;
        out.reserve(snap->lineage->Size());
        for (const auto& [k, rec] : snap->lineage->AllChildren()) {
            out.emplace(k.raw(), rec);
        }
        return out;
    }

    std::vector<RE::FormID> LineageUseCases::GetChildrenOfParent(RE::FormID parentID) const {
        std::vector<RE::FormID> out;
        if (parentID == 0) {
            return out;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [k, rec] : snap->lineage->AllChildren()) {
            if (Utils::MatchesActor(rec.guardians.slotA.raw(), parentID) ||
                Utils::MatchesActor(rec.guardians.slotB.raw(), parentID) ||
                Utils::MatchesActor(rec.bioMother.raw(), parentID) ||
                Utils::MatchesActor(rec.bioFather.raw(), parentID) ||
                Utils::MatchesActor(rec.presumedFather.raw(), parentID)) {
                out.push_back(k.raw());
            }
        }
        return out;
    }

    RE::FormID LineageUseCases::GetGuardianA(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->guardians.slotA.raw() : 0;
    }

    RE::FormID LineageUseCases::GetGuardianB(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->guardians.slotB.raw() : 0;
    }

    std::pair<RE::FormID, RE::FormID> LineageUseCases::GetGuardians(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            return { 0, 0 };
        }
        return { rec->guardians.slotA.raw(), rec->guardians.slotB.raw() };
    }

    RE::FormID LineageUseCases::GetCoGuardian(RE::FormID childID, RE::FormID guardianID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->guardians.OtherGuardian(State::ActorId{ guardianID }).raw() : 0;
    }

    bool LineageUseCases::IsGuardian(RE::FormID childID, RE::FormID actorID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec != nullptr && rec->guardians.HasGuardian(State::ActorId{ actorID });
    }

    RE::FormID LineageUseCases::GetBioMother(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->bioMother.raw() : 0;
    }

    RE::FormID LineageUseCases::GetBioFather(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->bioFather.raw() : 0;
    }

    RE::FormID LineageUseCases::GetPresumedFather(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->presumedFather.raw() : 0;
    }

    RE::FormID LineageUseCases::GetMotherPartner(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            return 0;
        }
        if (!rec->presumedFather.IsNull()) {
            return rec->presumedFather.raw();
        }
        const RE::FormID mID = rec->bioMother.raw();
        if (mID == 0) {
            return 0;
        }
        const RE::FormID partner = GetPrimaryPartnerInSnap(*snap, mID);
        if (partner != 0 && !Utils::MatchesActor(partner, mID)) {
            return partner;
        }
        const RE::FormID coGuardian = rec->guardians.OtherGuardian(State::ActorId{ mID }).raw();
        if (coGuardian != 0 && !Utils::MatchesActor(coGuardian, mID)) {
            return coGuardian;
        }
        return 0;
    }

    std::size_t LineageUseCases::GetChildCount() const {
        return State::WorldStore::GetSingleton().Snapshot()->lineage->Size();
    }

    std::string LineageUseCases::GetChildName(RE::FormID childID) const {
        if (childID == 0) {
            return "";
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->lineage->FindChild(State::ChildId{ childID })) {
            if (!rec->name.empty()) {
                return rec->name;
            }
        }
        return IsSyntheticId(childID) ? "Child" : ActorSvc::GetName(childID);
    }

    bool LineageUseCases::UpdateChildName(RE::FormID childID, const std::string& newName) {
        if (childID == 0 || newName.empty()) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            rec->name = newName;
            updatedRec = *rec;
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendOverviewData();
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    std::string LineageUseCases::PickRandomChildName(RE::Actor* actor, bool isFemale) const {
        return Engine::NamePoolLoader::GetSingleton().GetRandomChildName(actor, isFemale);
    }

    std::string LineageUseCases::PickRandomChildName(std::string_view raceName, bool isFemale) const {
        return Engine::NamePoolLoader::GetSingleton().GetRandomChildName(raceName, isFemale);
    }

    std::string LineageUseCases::PickRandomCritterName(RE::TESForm* critterOrRace) const {
        return Engine::NamePoolLoader::GetSingleton().GetRandomCritterName(critterOrRace);
    }

    // =========================================================================
    // Player Children Household, Orders, Pets, Critters & Interaction
    // =========================================================================

    bool LineageUseCases::OnChildAddedToHousehold(RE::FormID childID, RE::FormID houseID) {
        if (childID == 0 || IsSyntheticId(childID)) {
            return false;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* childRec = preSnap->lineage->FindChild(State::ChildId{ childID });
        if (childRec && childRec->stage == Foundation::ChildStage::Adult) {
            return false;
        }

        RE::FormID resolvedHouseID = 0;
        if (houseID != 0 && preSnap->household->FindHouse(State::HouseId{ houseID })) {
            resolvedHouseID = houseID;
        }
        if (resolvedHouseID == 0) {
            if (const auto* existing = preSnap->household->FindChildState(State::ChildId{ childID });
                existing && !existing->currentHouseId.IsNull() &&
                preSnap->household->FindHouse(existing->currentHouseId)) {
                resolvedHouseID = existing->currentHouseId.raw();
            }
        }
        if (resolvedHouseID == 0) {
            const auto& houses = preSnap->household->AllHouses();
            if (!houses.empty()) {
                resolvedHouseID = houses.front().houseId.raw();
            }
        }

        bool isNewToHousehold = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            if (!hh.FindChildState(cid)) {
                isNewToHousehold = true;
                State::ChildHouseholdRecord initRec{};
                initRec.childId = cid;
                initRec.isNewlyAdopted = true;
                hh.UpsertChildState(std::move(initRec));
            }
        });

        auto& ops = PlayerChildrenEngineOps::GetSingleton();
        ops.SnapshotChildFactions(childID);
        if (isNewToHousehold) {
            ops.ClearChildExternalFactions(childID);
        }
        if (resolvedHouseID != 0) {
            const auto relocated = HouseholdUseCases::GetSingleton().RelocateFamilyUnit(
                childID, resolvedHouseID, /*moveParents=*/true);
            if (relocated.empty()) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.household.Write().AssignTenant(
                        childID, State::HouseId{ resolvedHouseID }, /*stayAtHome=*/true, /*isChildTenant=*/true);
                });
                ops.MoveChildAndMarkerToHouse(childID, resolvedHouseID, isNewToHousehold);
                PackageReconciler::GetSingleton().ReconcileActor(childID);
                GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            } else {
                for (const RE::FormID fid : relocated) {
                    if (fid != childID) {
                        GetUiBridgeGateway().SendNPCDetail(fid);
                    }
                }
            }
        } else if (auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
            if (auto* marker = ops.GetOrCreateChildHomeMarker(childActor)) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                        st->homeMarkerId = State::RefId{ marker->GetFormID() };
                    }
                });
                ops.SetChildHomeMarkerLinkedRef(childID, marker->GetFormID());
            }
        }

        ops.ApplyOrderOnActorFromWorld(childID);
        FactionReconciler::GetSingleton().ReconcileActor(childID);
        return true;
    }

    bool LineageUseCases::OnChildRemovedFromHousehold(RE::FormID childID) {
        if (childID == 0) {
            return false;
        }
        bool hadState = false;
        Foundation::RefId transientCritterId{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            if (const auto* st = hh.FindChildState(cid)) {
                hadState = true;
                transientCritterId = st->transientCritterId;
            }
            hh.EvictTenant(childID);
            hh.EraseChildState(cid);
        });

        if (!transientCritterId.IsNull()) {
            (void)ChildSpawnService::GetSingleton().DisableAndDeleteActor(transientCritterId.raw());
        }

        auto& ops = PlayerChildrenEngineOps::GetSingleton();
        ops.RestoreChildFactions(childID);
        if (!IsSyntheticId(childID)) {
            PackageReconciler::GetSingleton().UnregisterActor(childID);
            FactionReconciler::GetSingleton().ReconcileActor(childID);
        }
        return hadState;
    }

    bool LineageUseCases::MoveChildToHouse(RE::FormID childID, RE::FormID houseID, bool moveActor) {
        if (childID == 0 || houseID == 0 || IsSyntheticId(childID)) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->household->FindHouse(State::HouseId{ houseID })) {
            return false;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            hh.AssignTenant(childID, State::HouseId{ houseID }, /*stayAtHome=*/true, /*isChildTenant=*/true);
            if (auto* st = hh.FindChildStateMutable(State::ChildId{ childID })) {
                st->currentHouseId = State::HouseId{ houseID };
            }
        });
        return PlayerChildrenEngineOps::GetSingleton().MoveChildAndMarkerToHouse(childID, houseID, moveActor);
    }

    void LineageUseCases::ReconcileChildHouseholdOnLoad() {
        PlayerChildrenEngineOps::GetSingleton().ReassertChildPackagesOnLoad();
    }

    bool LineageUseCases::IssueOrder(RE::FormID childID, float orderValue, float durationGameHours) {
        if (childID == 0) {
            return false;
        }
        const float nowDay = GetCurrentGameDays();
        const float durationDays = durationGameHours > 0.0f ? (durationGameHours / 24.0f) : 0.0f;
        Domain::Lineage::ChildOrderOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyOrderIssue(
                ws, State::ChildId{ childID }, orderValue, durationDays, nowDay);
        });
        if (!outcome.applied) {
            return false;
        }
        ApplyPackageForCurrentState(childID);
        MARAS_LOG_INFO("IssueOrder: Issued order {} to child {:08X} for {:.1f}h",
                       orderValue, childID, durationGameHours);
        return true;
    }

    float LineageUseCases::GetCurrentOrder(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->currentOrder : 0.0f;
    }

    void LineageUseCases::ClearOrder(RE::FormID childID) {
        if (childID == 0) {
            return;
        }
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                if (st->currentOrder != 0.0f || st->orderExpiryGameDay >= 0.0f) {
                    st->currentOrder = 0.0f;
                    st->orderExpiryGameDay = -1.0f;
                    updated = true;
                }
            }
        });
        if (updated) {
            ApplyPackageForCurrentState(childID);
        }
    }

    void LineageUseCases::ApplyPackageForCurrentState(RE::FormID childID) {
        PlayerChildrenEngineOps::GetSingleton().ApplyOrderOnActorFromWorld(childID);
    }

    bool LineageUseCases::IsForcegreetPending(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->forcegreetPending : false;
    }

    void LineageUseCases::ClearForcegreet(RE::FormID childID) {
        if (childID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->forcegreetPending = false;
            }
        });
        PlayerChildrenEngineOps::GetSingleton().ResetForceGreetingOnActor(childID);
    }

    void LineageUseCases::RollForcegreetForHousehold() {
        const RE::FormID chosenID = PickForcegreetChild(0);
        if (chosenID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ chosenID })) {
                st->forcegreetPending = true;
            }
        });
        ApplyPackageForCurrentState(chosenID);
    }

    bool LineageUseCases::AssignPet(RE::FormID childID, RE::FormID petActorOrBaseID) {
        if (childID == 0) {
            return false;
        }
        bool ok = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->petId = State::RefId{ petActorOrBaseID };
                ok = true;
            }
        });
        if (ok && petActorOrBaseID != 0 && !IsSyntheticId(petActorOrBaseID)) {
            PackageReconciler::GetSingleton().RegisterPetCritter(petActorOrBaseID);
            FactionReconciler::GetSingleton().ReconcileActor(petActorOrBaseID);
        }
        return ok;
    }

    bool LineageUseCases::RemovePet(RE::FormID childID) {
        RE::FormID removedPet = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                removedPet = st->petId.raw();
                st->petId = State::RefId{};
            }
        });
        if (removedPet != 0) {
            PackageReconciler::GetSingleton().UnregisterActor(removedPet);
            FactionReconciler::GetSingleton().ReconcileActor(removedPet);
            return true;
        }
        return false;
    }

    void LineageUseCases::RecordPetRejected(RE::FormID childID) {
        const float nowDays = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->petRetryTimestamp = nowDays;
            }
        });
    }

    bool LineageUseCases::IsChildPetBanned(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->petBanned : false;
    }

    bool LineageUseCases::IsPetBanned(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Lineage::IsPetBannedForChild(*snap, State::ChildId{ childID }, GetCurrentGameDays());
    }

    RE::FormID LineageUseCases::GetPet(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->petId.raw() : 0;
    }

    RE::FormID LineageUseCases::GetChildForPet(RE::FormID petID) const {
        if (petID == 0) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (st.petId.raw() == petID) {
                return cid.raw();
            }
        }
        return 0;
    }

    bool LineageUseCases::AssignCritter(RE::FormID childID, RE::FormID critterFormID) {
        if (childID == 0) {
            return false;
        }
        bool ok = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->critterId = State::RefId{ critterFormID };
                ok = true;
            }
        });
        if (ok && critterFormID != 0 && !IsSyntheticId(critterFormID)) {
            PackageReconciler::GetSingleton().RegisterPetCritter(critterFormID);
        }
        return ok;
    }

    bool LineageUseCases::RemoveCritter(RE::FormID childID) {
        RE::FormID removedCritter = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                removedCritter = st->critterId.raw();
                st->critterId = State::RefId{};
            }
        });
        if (removedCritter != 0) {
            PackageReconciler::GetSingleton().UnregisterActor(removedCritter);
            return true;
        }
        return false;
    }

    void LineageUseCases::RecordCritterRejected(RE::FormID childID) {
        const float nowDays = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->critterRetryTimestamp = nowDays;
            }
        });
    }

    bool LineageUseCases::IsChildCritterBanned(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->critterBanned : false;
    }

    bool LineageUseCases::IsCritterBanned(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Lineage::IsCritterBannedForChild(*snap, State::ChildId{ childID }, GetCurrentGameDays());
    }

    RE::FormID LineageUseCases::GetCritter(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->critterId.raw() : 0;
    }

    RE::FormID LineageUseCases::GetChildForCritter(RE::FormID critterID) const {
        if (critterID == 0) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (st.critterId.raw() == critterID) {
                return cid.raw();
            }
        }
        return 0;
    }

    RE::FormID LineageUseCases::GetChildForPetOrCritter(RE::FormID animalID) const {
        if (const RE::FormID p = GetChildForPet(animalID); p != 0) {
            return p;
        }
        return GetChildForCritter(animalID);
    }

    std::vector<RE::FormID> LineageUseCases::GetAllPetAndCritterIDs() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> out;
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            (void)cid;
            if (!st.petId.IsNull()) {
                out.push_back(st.petId.raw());
            }
            if (!st.critterId.IsNull()) {
                out.push_back(st.critterId.raw());
            }
        }
        return out;
    }

    RE::FormID LineageUseCases::GetChildHouseFormID(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* st = snap->household->FindChildState(State::ChildId{ childID })) {
            return st->currentHouseId.raw();
        }
        return 0;
    }

    void LineageUseCases::SetIsNewlyAdopted(RE::FormID childID, bool newlyAdopted, bool clearSameHouse) {
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const State::ChildId cid{ childID };
            auto* st = hh.FindChildStateMutable(cid);
            if (!st) {
                return;
            }
            const State::HouseId targetHouse = st->currentHouseId;
            st->isNewlyAdopted = newlyAdopted;
            if (clearSameHouse && !targetHouse.IsNull()) {
                for (const auto& [otherId, otherSt] : hh.AllChildStates()) {
                    if (otherId != cid && otherSt.currentHouseId == targetHouse) {
                        if (auto* mutOther = hh.FindChildStateMutable(otherId)) {
                            mutOther->isNewlyAdopted = false;
                        }
                    }
                }
            }
        });
    }

    bool LineageUseCases::IsNewlyAdopted(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->isNewlyAdopted : false;
    }

    void LineageUseCases::SetChildPlayedScene(RE::FormID childID, std::int32_t sceneID, bool played) {
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                if (sceneID == 1) {
                    st->playedScene1 = played;
                } else if (sceneID == 2) {
                    st->playedScene2 = played;
                }
            }
        });
    }

    bool LineageUseCases::HasChildPlayedScene(RE::FormID childID, std::int32_t sceneID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        if (!st) {
            return false;
        }
        if (sceneID == 1) {
            return st->playedScene1;
        }
        if (sceneID == 2) {
            return st->playedScene2;
        }
        return false;
    }

    void LineageUseCases::RecordChildInteraction(RE::FormID childID) {
        if (childID == 0) {
            return;
        }
        const float currentDays = GetCurrentGameDays();
        bool found = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                st->lastInteractionTimestamp = currentDays;
                found = true;
            }
        });
        if (!found) {
            return;
        }

        MARAS_LOG_INFO("RecordChildInteraction: child {:08X} timestamp = {:.2f}", childID, currentDays);

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerCell = player ? player->GetParentCell() : nullptr;
        if (!playerCell) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            return;
        }

        std::unordered_set<RE::FormID> coParentIDs;
        auto addCandidate = [&](RE::FormID id) {
            if (id != 0 && !Utils::MatchesActor(id, kPlayerFormID)) {
                coParentIDs.insert(id);
            }
        };
        addCandidate(rec->guardians.slotA.raw());
        addCandidate(rec->guardians.slotB.raw());
        addCandidate(rec->bioMother.raw());
        if (rec->paternitySecret.isSecretForPartner) {
            addCandidate(!rec->presumedFather.IsNull() ? rec->presumedFather.raw() : GetMotherPartner(childID));
        }
        if (!rec->paternitySecret.isSecretForBioFather) {
            addCandidate(rec->bioFather.raw());
        }

        std::vector<RE::FormID> partnersToUpdate;
        for (const RE::FormID partnerID : coParentIDs) {
            const auto* edge = snap->relationships->FindEdge(
                State::CanonicalPairKey{ State::ActorId::Player(), State::ActorId{ partnerID } });
            if (!edge || !Foundation::IsActivePartnerStatus(edge->status)) {
                continue;
            }
            auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(partnerID);
            if (!partnerActor || partnerActor->GetParentCell() != playerCell) {
                continue;
            }
            partnersToUpdate.push_back(partnerID);
        }

        if (partnersToUpdate.empty()) {
            return;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& aff = ws.affection.Write();
            const auto bounds = aff.GetMinMaxForType("family").value_or(std::pair<int, int>{ 0, 6 });
            for (const RE::FormID partnerID : partnersToUpdate) {
                const State::ActorId pId{ partnerID };
                const auto* prof = ws.actors.Read().Find(pId.AsRefId());
                const auto temp = prof ? prof->temperament : Foundation::Temperament::Proud;
                const float rawBonus = Domain::Lineage::CalculateChildInteractionFamilyAffection(temp);

                const float prevDaily = aff.GetDailyAccumulation(pId, "family");
                const float clampedDaily = std::clamp(
                    prevDaily + rawBonus, static_cast<float>(bounds.first), static_cast<float>(bounds.second));
                const float appliedDelta = clampedDaily - prevDaily;
                if (appliedDelta != 0.0f) {
                    const int prevPerm = aff.GetPermanent(pId, 50);
                    aff.SetPermanent(pId, prevPerm + static_cast<int>(std::lround(appliedDelta)));
                    aff.SetDailyAccumulation(pId, "family", clampedDaily);
                }
                aff.SetLastInteractionDay(pId, currentDays);
            }
        });

        for (const RE::FormID partnerID : partnersToUpdate) {
            FactionReconciler::GetSingleton().ReconcileActor(partnerID);
            GetUiBridgeGateway().SendNPCDetail(partnerID);
        }
    }

    float LineageUseCases::GetLastInteractionTimestamp(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household->FindChildState(State::ChildId{ childID });
        return st ? st->lastInteractionTimestamp : 0.0f;
    }

    RE::FormID LineageUseCases::PickForcegreetChild(RE::FormID houseID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto candidates = Domain::Lineage::SelectForcegreetChildrenByHouse(
            *snap, State::HouseId{ houseID }, /*onlyPhysicalActors=*/true);
        if (candidates.empty()) {
            return 0;
        }
        auto rng = MakeEngineRng();
        return Domain::Lineage::PickWeightedChild(*snap, candidates, GetCurrentGameDays(), rng).raw();
    }

    RE::FormID LineageUseCases::PickWeightedChild(const std::vector<RE::FormID>& candidateIDs) const {
        if (candidateIDs.empty()) {
            return 0;
        }
        std::vector<State::ChildId> ids;
        ids.reserve(candidateIDs.size());
        for (const RE::FormID id : candidateIDs) {
            if (!IsSyntheticId(id)) {
                ids.emplace_back(id);
            }
        }
        if (ids.empty()) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        auto rng = MakeEngineRng();
        return Domain::Lineage::PickWeightedChild(*snap, ids, GetCurrentGameDays(), rng).raw();
    }

    RE::FormID LineageUseCases::GetNewlyAdoptedChild(RE::FormID playerHouseFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (IsSyntheticId(cid.raw())) {
                continue;
            }
            if ((playerHouseFormID == 0 || st.currentHouseId.raw() == playerHouseFormID) && st.isNewlyAdopted) {
                return cid.raw();
            }
        }
        return 0;
    }

    RE::FormID LineageUseCases::GetChildWithoutPet(RE::FormID playerHouseFormID) const {
        if (IsPetBanned(0)) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float now = GetCurrentGameDays();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (IsSyntheticId(cid.raw())) {
                continue;
            }
            if (playerHouseFormID != 0 && st.currentHouseId.raw() != playerHouseFormID) {
                continue;
            }
            if (st.petId.IsNull() && !Domain::Lineage::IsPetBannedForChild(*snap, cid, now)) {
                return cid.raw();
            }
        }
        return 0;
    }

    RE::FormID LineageUseCases::GetChildWithoutCritter(RE::FormID playerHouseFormID) const {
        if (IsCritterBanned(0)) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float now = GetCurrentGameDays();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (IsSyntheticId(cid.raw())) {
                continue;
            }
            if (playerHouseFormID != 0 && st.currentHouseId.raw() != playerHouseFormID) {
                continue;
            }
            if (st.critterId.IsNull() && !Domain::Lineage::IsCritterBannedForChild(*snap, cid, now)) {
                return cid.raw();
            }
        }
        return 0;
    }

    std::vector<RE::FormID> LineageUseCases::GetChildrenWithoutScene(
        RE::FormID playerHouseFormID,
        std::int32_t sceneID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> out;
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (IsSyntheticId(cid.raw())) {
                continue;
            }
            if (playerHouseFormID != 0 && st.currentHouseId.raw() != playerHouseFormID) {
                continue;
            }
            const bool played = (sceneID == 1) ? st.playedScene1 : ((sceneID == 2) ? st.playedScene2 : false);
            if (!played) {
                out.push_back(cid.raw());
            }
        }
        return out;
    }

    RE::FormID LineageUseCases::GetWeightedRandomChild(RE::FormID playerHouseFormID) const {
        return PickWeightedChild(GetChildrenForHouse(playerHouseFormID, /*onlyRealActors=*/true));
    }

    std::vector<RE::FormID> LineageUseCases::GetAllPlayerChildren(bool onlyRealActors) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> out;
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            (void)st;
            if (!onlyRealActors || !IsSyntheticId(cid.raw())) {
                out.push_back(cid.raw());
            }
        }
        return out;
    }

    std::vector<RE::FormID> LineageUseCases::GetChildrenForHouse(
        RE::FormID playerHouseFormID,
        bool onlyRealActors) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> out;
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            if (onlyRealActors && IsSyntheticId(cid.raw())) {
                continue;
            }
            if (playerHouseFormID == 0 || st.currentHouseId.raw() == playerHouseFormID) {
                out.push_back(cid.raw());
            }
        }
        return out;
    }

    // =========================================================================
    // Training, Stage 3->4 Graduation, Beastblood, Blessings & Visuals
    // =========================================================================

    bool LineageUseCases::IsPlayerEligibleForTrainingClass(
        std::int32_t classIndex,
        std::string* outReason) const {
        const auto& loader = Engine::TrainingCatalogLoader::GetSingleton();
        const bool playerWW = ChildSpawnService::IsActorWerewolf(kPlayerFormID);
        return loader.GetCatalog().EvaluatePlayerRequirements(
            classIndex, playerWW, loader.GetAffiliationPort(), outReason);
    }

    std::string_view LineageUseCases::GetTrainingClassName(std::int32_t classIndex) const {
        if (const auto* spec = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog().FindClass(classIndex)) {
            return spec->className;
        }
        return "";
    }

    std::string_view LineageUseCases::GetTrainingLocationName(std::int32_t classIndex) const {
        if (const auto* spec = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog().FindClass(classIndex)) {
            return spec->locationName;
        }
        return "";
    }

    std::int32_t LineageUseCases::GetChildTrainingClass(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->trainingClassIndex : -1;
    }

    std::uint8_t LineageUseCases::GetChildSavedRaceIndex(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->savedRaceIndex : 255;
    }

    bool LineageUseCases::SetChildStage(RE::FormID childID, Foundation::ChildStage stage) {
        if (childID == 0) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->stage = stage;
                updatedRec = *rec;
            }
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendOverviewData();
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    bool LineageUseCases::SetChildGender(RE::FormID childID, std::uint8_t gender) {
        if (childID == 0) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->gender = gender;
                updatedRec = *rec;
            }
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    std::uint8_t LineageUseCases::GetChildGender(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->lineage->FindChild(State::ChildId{ childID })) {
            if (rec->gender <= 1) {
                return rec->gender;
            }
        }
        if (!IsSyntheticId(childID)) {
            return ActorSvc::GetSex(childID);
        }
        return 2;
    }

    bool LineageUseCases::SetChildTrainingLocation(RE::FormID childID, const std::string& location) {
        if (childID == 0) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->trainingLocation = location;
                updatedRec = *rec;
            }
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    std::string LineageUseCases::GetChildTrainingLocation(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->trainingLocation : "";
    }

    bool LineageUseCases::SendChildToTraining(
        RE::FormID childID,
        std::int32_t trainingClassIndex,
        std::string* outReason) {
        const auto& loader = Engine::TrainingCatalogLoader::GetSingleton();
        const auto* spec = loader.GetCatalog().FindClass(trainingClassIndex);
        if (!spec) {
            if (outReason) {
                *outReason = "Invalid training class";
            }
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* existing = snap->lineage->FindChild(State::ChildId{ childID });
        if (!existing) {
            if (outReason) {
                *outReason = "Child not found";
            }
            return false;
        }
        if (existing->stage == Foundation::ChildStage::Unborn ||
            existing->stage == Foundation::ChildStage::Adult) {
            if (outReason) {
                *outReason = "Child stage is not eligible for training";
            }
            return false;
        }

        State::ChildRecord rec = *existing;
        const bool isPlayerChild = rec.guardians.HasGuardian(State::ActorId::Player());
        const auto dashboardView = Projection::PerspectiveView::ForDashboard(
            snap,
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider());

        bool allowBeastbloodTraining = rec.hasBeastblood;
        if (isPlayerChild) {
            const auto perceivedOpt = dashboardView.GetPerceivedChild(State::ChildId{ childID });
            const bool unpersistedFirstEvalVisible =
                !existing->beastbloodEvaluated && rec.hasBeastblood &&
                (dashboardView.IsActorKnownWerewolf(rec.bioMother) ||
                 (perceivedOpt.has_value() && dashboardView.IsActorKnownWerewolf(perceivedOpt->perceivedBioFather)));
            allowBeastbloodTraining =
                (perceivedOpt.has_value() && perceivedOpt->hasBeastblood) || unpersistedFirstEvalVisible;
        }
        if (spec->requiresBeastblood && !allowBeastbloodTraining) {
            if (outReason) {
                *outReason = "Requires child to carry Hircine's Beastblood (inherited Lycanthropy)";
            }
            MARAS_LOG_WARN("[Training] Blocked for child {:08X} ('{}'): classIdx={} requires Beastblood",
                           childID, rec.name, trainingClassIndex);
            return false;
        }

        std::int32_t paidTuitionGold = 0;
        std::int32_t appliedDiscountPercent = 0;
        std::string appliedDiscountReason;
        if (isPlayerChild) {
            std::string eligReason;
            if (!IsPlayerEligibleForTrainingClass(trainingClassIndex, &eligReason)) {
                if (outReason) {
                    *outReason = eligReason;
                }
                MARAS_LOG_WARN("[Training] Blocked for child {:08X} ('{}'): {}", childID, rec.name, eligReason);
                return false;
            }
            const std::int32_t baseCost = std::max(0, snap->settings->childTrainingCost);
            const auto discountInfo = loader.GetCatalog().EvaluateChildTrainingDiscount(
                State::ChildId{ childID }, trainingClassIndex, dashboardView, loader.GetAffiliationPort());
            const std::int32_t cost = loader.GetCatalog().GetDiscountedTrainingCost(
                trainingClassIndex, baseCost, discountInfo.discountPercent);
            appliedDiscountPercent = discountInfo.discountPercent;
            appliedDiscountReason = discountInfo.reason;
            if (cost > 0) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* goldForm = FormCache::GetSingleton().GetGoldBaseObject();
                const std::int32_t playerGold = player ? player->GetGoldAmount() : 0;
                if (!player || !goldForm || playerGold < cost) {
                    const std::string goldMsg = std::format(
                        "Not enough gold ({} required, {} available)", cost, playerGold);
                    if (outReason) {
                        *outReason = goldMsg;
                    }
                    MARAS_LOG_WARN("[Training] Blocked for child {:08X} ('{}'): {}", childID, rec.name, goldMsg);
                    return false;
                }
                player->RemoveItem(goldForm, cost, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                paidTuitionGold = cost;
            }
        }

        if (rec.gender > 1) {
            rec.gender = GetChildGender(childID);
        }
        if (rec.savedRaceIndex > 9) {
            std::uint8_t resolvedRace = ChildSpawnService::ResolveActorFMRRaceIndex(childID);
            if (resolvedRace > 9 && !rec.bioMother.IsNull()) {
                resolvedRace = ChildSpawnService::ResolveActorFMRRaceIndex(rec.bioMother.raw());
            }
            if (resolvedRace > 9 && !rec.bioFather.IsNull() && !rec.bioFather.IsUnknownFather()) {
                resolvedRace = ChildSpawnService::ResolveActorFMRRaceIndex(rec.bioFather.raw());
            }
            if (resolvedRace <= 9) {
                rec.savedRaceIndex = resolvedRace;
            }
        }

        rec.trainingClassIndex = trainingClassIndex;
        rec.trainingLocation = spec->locationName;
        rec.trainingStartGameDay = GetCurrentGameDays();
        rec.stage = Foundation::ChildStage::Training;

        RE::FormID targetChildID = childID;
        const bool wasPhysicalChild = !IsSyntheticId(childID);
        if (wasPhysicalChild) {
            if (GetSkyrimNetGateway().IsAvailable()) {
                const std::string bioTpl = GetSkyrimNetGateway().GetActorBioTemplateName(childID);
                if (!bioTpl.empty()) {
                    rec.savedBioTemplateName = bioTpl;
                    MARAS_LOG_INFO("[Training] Captured SkyrimNet bioTemplate='{}' for child {:08X}",
                                   bioTpl, childID);
                }
            }

            if (const RE::FormID critterID = GetCritter(childID); critterID != 0) {
                (void)ChildSpawnService::GetSingleton().DisableAndDeleteActor(critterID);
                RemoveCritter(childID);
            }
            if (GetPet(childID) != 0) {
                RemovePet(childID);
            }
            OnChildRemovedFromHousehold(childID);
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageReconciler::GetSingleton().UnregisterActor(childID);
            (void)ChildSpawnService::GetSingleton().DisableAndDeleteActor(childID);

            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                RE::FormID allocated = ws.lineage.Write().AllocateSyntheticChildId().raw();
                if (allocated == 0) {
                    allocated = Foundation::kSyntheticChildMaxRawId;
                }
                targetChildID = allocated;
                rec.childId = State::ChildId{ targetChildID };
                ws.lineage.Write().EraseChild(State::ChildId{ childID });
                ws.lineage.Write().UpsertChild(rec);
            });

            if (!rec.guardians.slotA.IsNull()) {
                UpdateSpouseChildrenFaction(rec.guardians.slotA.raw());
            }
            if (!rec.guardians.slotB.IsNull()) {
                UpdateSpouseChildrenFaction(rec.guardians.slotB.raw());
            }
            GetUiBridgeGateway().OnChildRemoved(childID);
            GetUiBridgeGateway().OnChildPromoted(childID, targetChildID);
            GetUiBridgeGateway().OnChildUpdated(rec);
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCDetail(targetChildID);
        } else {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                ws.lineage.Write().UpsertChild(rec);
            });
            GetUiBridgeGateway().OnChildUpdated(rec);
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCDetail(childID);
        }

        MARAS_LOG_INFO(
            "[Training] Sent child {:08X} -> {:08X} ('{}') to '{}' as '{}' (tuition={}g, discount={}% [{}])",
            childID, targetChildID, rec.name, rec.trainingLocation, spec->className,
            paidTuitionGold, appliedDiscountPercent,
            appliedDiscountReason.empty() ? "None" : appliedDiscountReason);

        QueueHUDMessage(std::format("{} has been sent to {} to train as a {}.",
                                    rec.name.empty() ? "Child" : rec.name,
                                    rec.trainingLocation,
                                    spec->className));

        AffectionEventCoordinator::GetSingleton().OnChildMilestone(targetChildID, "training");
        SendChildEvent(
            "sent_to_training", targetChildID,
            rec.guardians.slotA.raw(), rec.guardians.slotB.raw(), 0, rec.trainingLocation);
        return true;
    }

    bool LineageUseCases::CompleteChildTraining(RE::FormID childID) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* existing = snap->lineage->FindChild(State::ChildId{ childID });
        if (!existing) {
            MARAS_LOG_WARN("[Training] CompleteChildTraining failed: child {:08X} not found", childID);
            return false;
        }
        if (existing->stage != Foundation::ChildStage::Training) {
            MARAS_LOG_WARN("[Training] CompleteChildTraining failed for {:08X}: stage={} (expected Training)",
                           childID, static_cast<int>(existing->stage));
            return false;
        }

        State::ChildRecord rec = *existing;
        rec.stage = Foundation::ChildStage::Adult;
        const bool motherWW = !rec.bioMother.IsNull() && ChildSpawnService::IsActorWerewolf(rec.bioMother.raw());
        const bool fatherWW = (!rec.bioFather.IsNull() && !rec.bioFather.IsUnknownFather()) &&
                              ChildSpawnService::IsActorWerewolf(rec.bioFather.raw());
        Domain::Lineage::EvaluateChildBeastbloodInheritance(rec, motherWW, fatherWW);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.lineage.Write().UpsertChild(rec);
        });

        const std::string className = std::string(GetTrainingClassName(rec.trainingClassIndex));
        MARAS_LOG_INFO("[Training] Child {:08X} ('{}') graduated to Stage 4 (Adult)", childID, rec.name);

        QueueHUDMessage(std::format("{} has completed {} training at {} and is ready to return as an adult!",
                                    rec.name.empty() ? "Your child" : rec.name,
                                    className.empty() ? "adolescent" : className,
                                    rec.trainingLocation.empty() ? "their academy" : rec.trainingLocation));

        AffectionEventCoordinator::GetSingleton().OnChildMilestone(childID, "adult");
        SendChildEvent(
            "became_adult", childID,
            rec.guardians.slotA.raw(), rec.guardians.slotB.raw(), 0, rec.trainingLocation);

        GetUiBridgeGateway().OnChildUpdated(rec);
        GetUiBridgeGateway().SendOverviewData();
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    void LineageUseCases::CheckChildrenTrainingProgress(float currentGameDay) {
        if (currentGameDay <= 0.0f) {
            return;
        }
        std::vector<RE::FormID> graduatedIDs;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const float requiredDays = static_cast<float>(std::max(1, snap->settings->childTrainingDurationDays));
            for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
                if (rec.stage != Foundation::ChildStage::Training || rec.trainingStartGameDay <= 0.0f) {
                    continue;
                }
                const float elapsedDays = currentGameDay - rec.trainingStartGameDay;
                if (elapsedDays >= requiredDays) {
                    MARAS_LOG_INFO("[Training] Child {:08X} ('{}') reached {:.2f} / {:.1f} days — graduating",
                                   cIdKey.raw(), rec.name, elapsedDays, requiredDays);
                    graduatedIDs.push_back(cIdKey.raw());
                }
            }
        }
        for (const RE::FormID cID : graduatedIDs) {
            CompleteChildTraining(cID);
        }
    }

    bool LineageUseCases::HasChildBeastblood(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec != nullptr && rec->hasBeastblood;
    }

    bool LineageUseCases::SetChildBeastblood(RE::FormID childID, bool hasBeastblood) {
        if (childID == 0) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->hasBeastblood = hasBeastblood;
                rec->beastbloodEvaluated = true;
                updatedRec = *rec;
            }
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    bool LineageUseCases::SetChildDaedricBlessing(RE::FormID childID, const std::string& blessingId) {
        if (childID == 0) {
            return false;
        }
        std::optional<State::ChildRecord> updatedRec;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->daedricBlessingId = blessingId;
                updatedRec = *rec;
            }
        });
        if (!updatedRec.has_value()) {
            return false;
        }
        GetUiBridgeGateway().OnChildUpdated(*updatedRec);
        GetUiBridgeGateway().SendNPCDetail(childID);
        return true;
    }

    std::string LineageUseCases::GetChildDaedricBlessing(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->daedricBlessingId : "";
    }

    void LineageUseCases::InitializeAndApplyChildVisualsAndRPG(RE::FormID childID) {
        ChildVisualReconciler::GetSingleton().InitializeAndApplyChildVisualsAndRPG(childID);
    }

    void LineageUseCases::ApplyChildVisualsOn3DLoad(RE::Actor* actor) {
        ChildVisualReconciler::GetSingleton().ApplyChildVisualsOn3DLoad(actor);
    }

    void LineageUseCases::EvaluateChildBioLineageAtBirth(RE::FormID childID) {
        if (childID == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* existing = snap->lineage->FindChild(State::ChildId{ childID });
        if (!existing || existing->bioLineageEvaluated) {
            return;
        }
        State::ChildRecord rec = *existing;
        ChildVisualReconciler::GetSingleton().EvaluateChildBioLineageAtBirth(rec);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* mut = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                mut->bioLineageEvaluated = rec.bioLineageEvaluated;
                mut->bioSkillBonuses = rec.bioSkillBonuses;
            }
        });
    }

    // =========================================================================
    // Unified Death Handling & Deceased Archival Queries
    // =========================================================================

    bool LineageUseCases::RecordActorDeath(RE::FormID victimId, RE::FormID killerId, bool isPlayerKiller) {
        if (victimId == 0 || Utils::MatchesActor(victimId, kPlayerFormID)) {
            return false;
        }

        // 1. Check if victim is a Pet or Critter belonging to a child
        RE::FormID petChildID = GetChildForPetOrCritter(victimId);
        if (petChildID != 0) {
            while (petChildID != 0) {
                MARAS_LOG_INFO("RecordActorDeath: Animal {:08X} belonging to child {:08X} died", victimId, petChildID);
                if (GetChildForPet(victimId) == petChildID) {
                    RemovePet(petChildID);
                    RecordPetRejected(petChildID);
                }
                if (GetChildForCritter(victimId) == petChildID) {
                    RemoveCritter(petChildID);
                    RecordCritterRejected(petChildID);
                }
                if (isPlayerKiller) {
                    IssueOrder(petChildID, 10.0f, 24.0f);
                }
                petChildID = GetChildForPetOrCritter(victimId);
            }
            EmitModCallbackEvent("maras_child_pet_died", "", isPlayerKiller ? 1.0f : 0.0f, victimId);
            return true;
        }

        // 2. Capture pre-death snapshot & metadata OUTSIDE WorldStore::Modify
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const bool isDynamicVictim = Foundation::IsDynamicRuntimeActorRawId(victimId);
        RE::FormID victimBaseID = 0;
        if (!isDynamicVictim) {
            if (auto* actorBase = ActorSvc::GetActorBase(victimId)) {
                victimBaseID = actorBase->GetFormID();
            } else {
                victimBaseID = SpatialWorldIndex::GetSingleton().GetBaseID(victimId);
            }
        }

        const auto* preChild = preSnap->lineage->FindChild(State::ChildId{ victimId });
        const bool wasYoungChild = (preChild != nullptr && preChild->stage != Foundation::ChildStage::Adult);
        const bool wasAdultChild = (preChild != nullptr && preChild->stage == Foundation::ChildStage::Adult);

        const State::ActorId victimActorId{ victimId };
        const bool hadAdultProfile =
            preSnap->actors->Find(victimActorId.AsRefId()) != nullptr &&
            preSnap->actors->Find(victimActorId.AsRefId())->isRegisteredAdult;
        const auto prePairs = preSnap->relationships->GetAdjacentPairs(victimActorId);
        const bool wasTrackedAdult = hadAdultProfile || !prePairs.empty() || wasAdultChild;

        RE::FormID canonicalVictimId = victimId;
        if (!wasYoungChild && !wasTrackedAdult && !isDynamicVictim && victimBaseID != 0 && victimBaseID != victimId) {
            const State::ActorId baseActorId{ victimBaseID };
            const auto* baseProf = preSnap->actors->Find(baseActorId.AsRefId());
            if ((baseProf && baseProf->isRegisteredAdult) || !preSnap->relationships->GetAdjacentPairs(baseActorId).empty()) {
                canonicalVictimId = victimBaseID;
            }
        }

        const State::ActorId canonicalActor{ canonicalVictimId };
        const bool alreadyDeceased = Domain::Household::IsDeceased(
            *preSnap, canonicalActor, isDynamicVictim ? State::BaseId{ victimId } : State::BaseId{ victimBaseID });

        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        for (const auto& [k, v] : preSnap->lineage->AllChildren()) {
            preChildren.emplace(k.raw(), v);
        }

        std::unordered_set<RE::FormID> survivingPartners;
        bool wasMarriedToPlayer = false;
        for (const auto& pair : preSnap->relationships->GetAdjacentPairs(canonicalActor)) {
            const RE::FormID otherID = pair.Other(canonicalActor).raw();
            if (otherID != 0) {
                survivingPartners.insert(otherID);
                if (Utils::MatchesActor(otherID, kPlayerFormID)) {
                    if (const auto* edge = preSnap->relationships->FindEdge(pair)) {
                        wasMarriedToPlayer = (edge->status == Foundation::RelationshipStatus::Married);
                    }
                }
            }
        }

        auto meta = CaptureWorldActorMetadata(preSnap, { victimId, canonicalVictimId, killerId });
        const float deathDay = GetCurrentGameDays();

        State::WorldMutationResult mut{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.RecordActorDeath(
                canonicalActor,
                deathDay,
                isPlayerKiller,
                State::ActorId{ killerId },
                *meta);
        });

        // If untracked by RefID yet known by BaseID on children, evaluate base custody cleanly
        if (!wasYoungChild && !wasTrackedAdult && !isDynamicVictim &&
            victimBaseID != 0 && victimBaseID != canonicalVictimId) {
            auto metaBase = CaptureWorldActorMetadata(State::WorldStore::GetSingleton().Snapshot(), { victimBaseID });
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                mut.Merge(ws.EvaluateCustodyOnDeath(State::ActorId{ victimBaseID }, *metaBase));
            });
        }

        const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
        const bool newlyArchived =
            !alreadyDeceased &&
            postSnap->deceased->IsDeceased(
                canonicalActor, isDynamicVictim ? State::BaseId{ victimId } : State::BaseId{ victimBaseID });

        // 3. Engine, SkyrimNet & UI cleanup outside lock
        if (wasYoungChild) {
            ClearMarriageAdoptionPending(victimId);
            OnChildRemovedFromHousehold(victimId);
            ClearAdoptableFactionDirect(victimId);
            RemoveChildSandboxAroundParentLinkedRef(victimId);
            if (!IsSyntheticId(victimId)) {
                PackageReconciler::GetSingleton().UnregisterActor(victimId);
                ChildVisualReconciler::GetSingleton().ReleaseRuntimeBaseForActor(victimId);
                FactionReconciler::GetSingleton().ReconcileActor(victimId);
            }
            if (preChild && !preChild->guardians.slotA.IsNull()) {
                UpdateSpouseChildrenFaction(preChild->guardians.slotA.raw());
            }
            if (preChild && !preChild->guardians.slotB.IsNull()) {
                UpdateSpouseChildrenFaction(preChild->guardians.slotB.raw());
            }
            if (newlyArchived) {
                EmitModCallbackEvent("maras_child_died", "", isPlayerKiller ? 1.0f : 0.0f, victimId);
            }
            GetUiBridgeGateway().OnChildRemoved(victimId);
            GetUiBridgeGateway().SendOverviewData();
            return true;
        }

        if (wasAdultChild) {
            ClearMarriageAdoptionPending(victimId);
            OnChildRemovedFromHousehold(victimId);
            HouseholdUseCases::GetSingleton().UnregisterAdultChild(victimId);
            if (!IsSyntheticId(victimId)) {
                ChildVisualReconciler::GetSingleton().ReleaseRuntimeBaseForActor(victimId);
            }
            GetUiBridgeGateway().OnChildRemoved(victimId);
        }

        if (newlyArchived) {
            EmitModCallbackEvent("maras_actor_died", "", isPlayerKiller ? 1.0f : 0.0f, victimId);
        }

        HandleDeathCustodySideEffects(preChildren, *postSnap, canonicalVictimId);
        if (!isDynamicVictim && victimBaseID != 0 && victimBaseID != canonicalVictimId) {
            HandleDeathCustodySideEffects(preChildren, *postSnap, victimBaseID);
        }

        if (wasTrackedAdult || wasAdultChild) {
            BedOwnershipReconciler::GetSingleton().ReconcileStoppedSharingFromMutation(
                canonicalVictimId, mut, "deceased");
            BanterSceneCoordinator::GetSingleton().CleanupRetiredSceneMarkers(mut.retiredSceneMarkers);
            HouseholdEngineOps::GetSingleton().ClearHomeMarkerLinkedRef(canonicalVictimId);
            FactionReconciler::GetSingleton().ReconcileActor(canonicalVictimId);
            PackageReconciler::GetSingleton().UnregisterActor(canonicalVictimId);
            StripVanillaMarriageFactionsOnMainThread(canonicalVictimId);
            if (victimId != canonicalVictimId) {
                FactionReconciler::GetSingleton().ReconcileActor(victimId);
                PackageReconciler::GetSingleton().UnregisterActor(victimId);
                StripVanillaMarriageFactionsOnMainThread(victimId);
            }
            for (const RE::FormID partnerID : survivingPartners) {
                if (partnerID != 0 && !Utils::MatchesActor(partnerID, kPlayerFormID)) {
                    FactionReconciler::GetSingleton().ReconcileActor(partnerID);
                    PackageReconciler::GetSingleton().ReconcileActor(partnerID);
                }
            }
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            if (wasMarriedToPlayer) {
                BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
                GetUiBridgeGateway().SendHouseholdBuffs();
            }
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCList();
        }

        return mut.changed || newlyArchived;
    }

    bool LineageUseCases::IsDeceased(RE::FormID formID) const {
        if (formID == 0) {
            return false;
        }
        RE::FormID baseID = 0;
        RE::FormID refID = 0;
        ResolveFormIDsForDeceasedQuery(formID, baseID, refID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::IsDeceased(*snap, State::ActorId{ formID }, State::BaseId{ baseID });
    }

    bool LineageUseCases::IsWidowOrWidower(RE::FormID actorFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::IsWidowOrWidower(*snap, State::ActorId{ actorFormID });
    }

    bool LineageUseCases::WasMarriedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        RE::FormID decBase = 0;
        RE::FormID decRef = 0;
        RE::FormID pBase = 0;
        RE::FormID pRef = 0;
        ResolveFormIDsForDeceasedQuery(deceasedFormID, decBase, decRef);
        ResolveFormIDsForDeceasedQuery(partnerFormID, pBase, pRef);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::WasMarriedTo(
            *snap,
            State::ActorId{ deceasedFormID },
            State::ActorId{ partnerFormID },
            State::BaseId{ decBase },
            State::BaseId{ pBase },
            State::RefId{ pRef });
    }

    bool LineageUseCases::WasEngagedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        RE::FormID decBase = 0;
        RE::FormID decRef = 0;
        RE::FormID pBase = 0;
        RE::FormID pRef = 0;
        ResolveFormIDsForDeceasedQuery(deceasedFormID, decBase, decRef);
        ResolveFormIDsForDeceasedQuery(partnerFormID, pBase, pRef);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::WasEngagedTo(
            *snap,
            State::ActorId{ deceasedFormID },
            State::ActorId{ partnerFormID },
            State::BaseId{ decBase },
            State::BaseId{ pBase },
            State::RefId{ pRef });
    }

    bool LineageUseCases::WasExOf(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        RE::FormID decBase = 0;
        RE::FormID decRef = 0;
        RE::FormID pBase = 0;
        RE::FormID pRef = 0;
        ResolveFormIDsForDeceasedQuery(deceasedFormID, decBase, decRef);
        ResolveFormIDsForDeceasedQuery(partnerFormID, pBase, pRef);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Household::WasExOf(
            *snap,
            State::ActorId{ deceasedFormID },
            State::ActorId{ partnerFormID },
            State::BaseId{ decBase },
            State::BaseId{ pBase },
            State::RefId{ pRef });
    }

    std::vector<State::DeceasedActorRecord> LineageUseCases::GetDeceasedSpousesOf(RE::FormID actorFormID) const {
        RE::FormID baseID = 0;
        RE::FormID refID = 0;
        ResolveFormIDsForDeceasedQuery(actorFormID, baseID, refID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto keys = Domain::Household::GetDeceasedSpousesOf(
            *snap, State::ActorId{ actorFormID }, State::BaseId{ baseID }, State::RefId{ refID });
        std::vector<State::DeceasedActorRecord> out;
        out.reserve(keys.size());
        for (const auto key : keys) {
            if (const auto* rec = snap->deceased->FindByActor(State::ActorId{ key.raw() }, key)) {
                out.push_back(*rec);
            }
        }
        return out;
    }

    std::vector<State::DeceasedActorRecord> LineageUseCases::GetDeceasedPartnersOf(RE::FormID actorFormID) const {
        RE::FormID baseID = 0;
        RE::FormID refID = 0;
        ResolveFormIDsForDeceasedQuery(actorFormID, baseID, refID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto keys = Domain::Household::GetDeceasedPartnersOf(
            *snap, State::ActorId{ actorFormID }, State::BaseId{ baseID }, State::RefId{ refID });
        std::vector<State::DeceasedActorRecord> out;
        out.reserve(keys.size());
        for (const auto key : keys) {
            if (const auto* rec = snap->deceased->FindByActor(State::ActorId{ key.raw() }, key)) {
                out.push_back(*rec);
            }
        }
        return out;
    }

    std::vector<State::DeceasedActorRecord> LineageUseCases::GetDeceasedChildrenOf(RE::FormID parentFormID) const {
        RE::FormID baseID = 0;
        RE::FormID refID = 0;
        ResolveFormIDsForDeceasedQuery(parentFormID, baseID, refID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto childKeys = Domain::Household::GetDeceasedChildrenOf(
            *snap, State::ActorId{ parentFormID }, State::BaseId{ baseID }, State::RefId{ refID });
        std::vector<State::DeceasedActorRecord> out;
        out.reserve(childKeys.size());
        for (const auto cid : childKeys) {
            if (const auto* rec = snap->deceased->FindByActor(
                    State::ActorId{ cid.raw() }, State::BaseId{ cid.raw() })) {
                out.push_back(*rec);
            }
        }
        return out;
    }

    bool LineageUseCases::HasDeceasedChildren(RE::FormID parentFormID) const {
        return !GetDeceasedChildrenOf(parentFormID).empty();
    }

    std::optional<State::DeceasedActorRecord> LineageUseCases::FindDeceasedRecord(RE::FormID formID) const {
        if (formID == 0) {
            return std::nullopt;
        }
        RE::FormID baseID = 0;
        RE::FormID refID = 0;
        ResolveFormIDsForDeceasedQuery(formID, baseID, refID);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->deceased->FindByActor(State::ActorId{ formID }, State::BaseId{ baseID })) {
            return *rec;
        }
        return std::nullopt;
    }

    std::size_t LineageUseCases::GetDeceasedCount() const {
        return State::WorldStore::GetSingleton().Snapshot()->deceased->Size();
    }

    nlohmann::json LineageUseCases::DumpDeceasedJson(const Projection::PerspectiveView& view) const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [key, rec] : view.Deceased().AllRecords()) {
            (void)key;
            nlohmann::json r;
            r["baseFormId"] = std::format("{:08X}", rec.baseId.raw());
            r["refFormId"] = std::format("{:08X}", rec.refId.raw());
            r["name"] = rec.name;
            r["deathTimestamp"] = rec.deathTimestamp;
            r["isChild"] = rec.isChild;
            r["wasKilledByPlayer"] = rec.wasKilledByPlayer;
            r["killer"] = {
                {"baseFormId", !rec.killerBaseId.IsNull() ? std::format("{:08X}", rec.killerBaseId.raw()) : ""},
                {"refFormId", !rec.killerRefId.IsNull() ? std::format("{:08X}", rec.killerRefId.raw()) : ""},
                {"name", rec.killerName}
            };

            if (!rec.isChild) {
                r["socialClass"] = {
                    {"id", static_cast<std::uint8_t>(rec.socialClass)},
                    {"name", Foundation::SocialClassToString(rec.socialClass)}
                };
                r["skillType"] = {
                    {"id", static_cast<std::uint8_t>(rec.skillType)},
                    {"name", Foundation::SkillTypeToString(rec.skillType)}
                };
                r["temperament"] = {
                    {"id", static_cast<std::uint8_t>(rec.temperament)},
                    {"name", Foundation::TemperamentToString(rec.temperament)}
                };

                nlohmann::json partnersArr = nlohmann::json::array();
                for (const auto& p : rec.partnersAtDeath) {
                    State::RelationshipStatus visibleStatus = p.statusAtDeath;
                    if (!view.IsOmniscient() && p.isSecretAffair) {
                        if (!Foundation::IsRomanticOrExStatus(p.publicStatusAtDeath)) {
                            continue;
                        }
                        visibleStatus = p.publicStatusAtDeath;
                    }
                    nlohmann::json pObj;
                    pObj["partnerBaseId"] = std::format("{:08X}", p.partnerBaseId.raw());
                    pObj["partnerRefId"] = std::format("{:08X}", p.partnerRefId.raw());
                    pObj["name"] = p.partnerName;
                    pObj["statusAtDeath"] = {
                        {"id", static_cast<std::uint8_t>(visibleStatus)},
                        {"name", Foundation::RelationshipStatusToString(visibleStatus)}
                    };
                    pObj["isPlayer"] = p.isPlayer;
                    partnersArr.push_back(std::move(pObj));
                }
                r["partnersAtDeath"] = std::move(partnersArr);
            }

            if (rec.childInfo.has_value()) {
                const auto& ci = *rec.childInfo;
                RE::FormID visibleBioFatherBaseId = ci.bioFatherBaseId.raw();
                if (!view.IsOmniscient()) {
                    if (ci.hasUndiscoveredPaternitySecret) {
                        visibleBioFatherBaseId = ci.publicBioFatherBaseId.raw();
                    } else {
                        const RE::FormID lookupId = !rec.refId.IsNull() ? rec.refId.raw() : rec.baseId.raw();
                        if (const auto perceivedChild = view.GetPerceivedChild(State::ChildId{ lookupId })) {
                            const RE::FormID perceivedFatherRaw = perceivedChild->perceivedBioFather.raw();
                            visibleBioFatherBaseId =
                                (perceivedFatherRaw != 0 && perceivedFatherRaw != kUnknownFatherFormID)
                                ? ResolveBaseFormIDSafe(perceivedFatherRaw, view.Spatial())
                                : 0;
                        }
                    }
                }
                nlohmann::json cInfo;
                cInfo["guardianA"] = {
                    {"baseFormId", std::format("{:08X}", ci.guardianABaseId.raw())},
                    {"refFormId", std::format("{:08X}", ci.guardianARefId.raw())},
                    {"name", ci.guardianAName}
                };
                cInfo["guardianB"] = {
                    {"baseFormId", std::format("{:08X}", ci.guardianBBaseId.raw())},
                    {"refFormId", std::format("{:08X}", ci.guardianBRefId.raw())},
                    {"name", ci.guardianBName}
                };
                cInfo["bioMotherBaseId"] = std::format("{:08X}", ci.bioMotherBaseId.raw());
                cInfo["bioFatherBaseId"] = std::format("{:08X}", visibleBioFatherBaseId);
                cInfo["isAdopted"] = ci.isAdopted;
                cInfo["houseFormId"] = !ci.houseId.IsNull() ? std::format("{:08X}", ci.houseId.raw()) : "";
                r["childInfo"] = std::move(cInfo);
            }

            j.push_back(std::move(r));
        }
        return j;
    }

    nlohmann::json LineageUseCases::DumpDeceasedJson() const {
        return DumpDeceasedJson(Projection::PerspectiveView::ForDashboard(
            State::WorldStore::GetSingleton().Snapshot(),
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider()));
    }

    nlohmann::json LineageUseCases::DumpPlayerChildrenJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json j;
        j["childChestIntroduced"] = snap->household->IsChildChestIntroduced();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [cid, st] : snap->household->AllChildStates()) {
            arr.push_back(SerializeChildHouseholdState(cid.raw(), st));
        }
        j["children"] = std::move(arr);

        nlohmann::json factionSnapshotsArr = nlohmann::json::array();
        std::vector<State::ChildId> snapshotIds;
        snapshotIds.reserve(snap->household->AllChildFactionSnapshots().size());
        for (const auto& [cid, _] : snap->household->AllChildFactionSnapshots()) {
            snapshotIds.push_back(cid);
        }
        std::sort(snapshotIds.begin(), snapshotIds.end(),
                  [](State::ChildId a, State::ChildId b) { return a.raw() < b.raw(); });
        for (const State::ChildId cid : snapshotIds) {
            const auto& fs = snap->household->AllChildFactionSnapshots().at(cid);
            nlohmann::json sj = nlohmann::json::object();
            sj["childFormID"] = cid.raw();
            sj["childHex"] = std::format("{:08X}", cid.raw());
            sj["crimeFactionFormID"] = fs.crimeFactionId.raw();
            sj["crimeFactionHex"] = std::format("{:08X}", fs.crimeFactionId.raw());
            nlohmann::json factionsArr = nlohmann::json::array();
            for (const auto& entry : fs.factions) {
                nlohmann::json fj = nlohmann::json::object();
                fj["factionFormID"] = entry.factionId.raw();
                fj["factionHex"] = std::format("{:08X}", entry.factionId.raw());
                fj["rank"] = entry.rank;
                factionsArr.push_back(std::move(fj));
            }
            sj["factions"] = std::move(factionsArr);
            factionSnapshotsArr.push_back(std::move(sj));
        }
        j["childFactionSnapshots"] = std::move(factionSnapshotsArr);
        return j;
    }

}  // namespace MARAS::App
