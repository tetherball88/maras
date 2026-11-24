#include "maras/engine/FactionReconciler.h"

#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/state/WorldStore.h"

#include <array>
#include <unordered_set>

namespace MARAS {

    FactionReconciler& FactionReconciler::GetSingleton() {
        static FactionReconciler instance;
        return instance;
    }

    void FactionReconciler::RegisterFactionAddedListener(FactionAddedCallback callback) {
        if (!callback) {
            return;
        }
        std::unique_lock lock(mutex_);
        addedListeners_.push_back(std::move(callback));
        hasListeners_.store(true, std::memory_order_release);
    }

    void FactionReconciler::RegisterFactionRemovedListener(FactionRemovedCallback callback) {
        if (!callback) {
            return;
        }
        std::unique_lock lock(mutex_);
        removedListeners_.push_back(std::move(callback));
        hasListeners_.store(true, std::memory_order_release);
    }

    void FactionReconciler::NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        if (!HasListeners()) {
            return;
        }
        std::vector<FactionAddedCallback> listeners;
        {
            std::shared_lock lock(mutex_);
            listeners = addedListeners_;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction, a_rank);
            }
        }
    }

    void FactionReconciler::NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        if (!HasListeners()) {
            return;
        }
        std::vector<FactionRemovedCallback> listeners;
        {
            std::shared_lock lock(mutex_);
            listeners = removedListeners_;
        }
        for (const auto& listener : listeners) {
            if (listener) {
                listener(a_actor, a_faction);
            }
        }
    }

    namespace {

        class EngineFamilyMetadataProvider final : public State::IActorMetadataProvider {
        public:
            [[nodiscard]] State::BaseId GetActorBaseId(Foundation::ActorId actor) const noexcept override {
                if (actor.IsNull()) {
                    return {};
                }
                if (actor.IsPlayer() || actor.raw() == Foundation::kPlayerBaseRawId) {
                    return State::BaseId{ Foundation::kPlayerBaseRawId };
                }
                if (const RE::FormID baseId = SpatialWorldIndex::GetSingleton().GetBaseID(actor.raw()); baseId != 0) {
                    return State::BaseId{ baseId };
                }
                if (auto* base = ActorSvc::GetActorBase(actor.raw())) {
                    return State::BaseId{ base->GetFormID() };
                }
                return {};
            }
        };

        [[nodiscard]] State::ActorSex LookupActorSex(Foundation::ActorId actorId) noexcept {
            if (actorId.IsNull() || actorId.IsSyntheticChild() || actorId.IsUnknownFather()) {
                return State::ActorSex::Unknown;
            }
            const std::uint8_t sex = ActorSvc::GetSex(actorId.raw());
            if (sex == 0) {
                return State::ActorSex::Male;
            }
            if (sex == 1) {
                return State::ActorSex::Female;
            }
            return State::ActorSex::Unknown;
        }

        [[nodiscard]] std::string_view ResolveAdultChildClassId(const State::ChildRecord& childRec) noexcept {
            const auto& catalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
            if (const auto* spec = catalog.FindClass(childRec.trainingClassIndex)) {
                return spec->id;
            }
            if (childRec.trainingLocation.empty()) {
                return {};
            }
            const std::int32_t classIdx = catalog.FindClassByLocation(childRec.trainingLocation);
            if (const auto* spec = catalog.FindClass(classIdx)) {
                return spec->id;
            }
            return {};
        }

        void ClearKeywordOnBase(RE::Actor* actor, RE::BGSKeyword* keyword) {
            if (!actor || !keyword) {
                return;
            }
            if (auto* base = actor->GetActorBase()) {
                if (base->HasKeyword(keyword)) {
                    base->RemoveKeyword(keyword);
                    MARAS_LOG_DEBUG("[FactionReconciler] Removed keyword {:08X} from actor {} ({:08X})",
                                    keyword->GetFormID(), actor->GetName() ? actor->GetName() : "Unknown",
                                    actor->GetFormID());
                }
            }
        }

    }  // namespace

    bool FactionReconciler::IsBlockedPlayerFamilyActor(
        RE::FormID actorId,
        const State::WorldStateSnapshot& snap) {
        if (actorId == 0 || actorId == kPlayerFormID || actorId == kPlayerBaseID ||
            IsSyntheticChildID(actorId)) {
            return false;
        }
        const EngineFamilyMetadataProvider meta{};
        if (Domain::Relationships::AreMarasFamilyMembers(
                snap, meta, Foundation::ActorId{ actorId }, Foundation::ActorId::Player())) {
            return true;
        }
        return Utils::AreVanillaEngineFamily(actorId, kPlayerFormID);
    }

    bool FactionReconciler::IsNativeBaseFaction(const RE::Actor* actor, const RE::TESFaction* faction) noexcept {
        if (!actor || !faction) {
            return false;
        }
        const auto* base = actor->GetActorBase();
        if (!base) {
            return false;
        }
        for (const auto& entry : base->factions) {
            if (entry.faction == faction && entry.rank >= 0) {
                return true;
            }
        }
        return false;
    }

    bool FactionReconciler::SyncActorFaction(
        RE::Actor* actor,
        RE::TESFaction* faction,
        std::optional<std::int8_t> desiredRank,
        bool preserveIfNativeOnBase) {
        if (!actor || !faction) {
            return false;
        }

        const bool isPlayer = actor->IsPlayerRef();
        const bool inFaction = actor->IsInFaction(faction);

        if (desiredRank.has_value()) {
            const std::int32_t currentRank = inFaction ? actor->GetFactionRank(faction, isPlayer) : -2;
            if (inFaction && currentRank == static_cast<std::int32_t>(*desiredRank)) {
                return false;
            }
            Utils::AddToFaction(actor, faction, *desiredRank);
            MARAS_LOG_TRACE("[FactionReconciler] Set faction {:08X} rank {} on {} ({:08X})",
                            faction->GetFormID(), *desiredRank,
                            actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
            return true;
        }

        if (!inFaction) {
            return false;
        }

        if (preserveIfNativeOnBase && IsNativeBaseFaction(actor, faction)) {
            return false;
        }

        Utils::RemoveFromFaction(actor, faction);
        MARAS_LOG_TRACE("[FactionReconciler] Removed faction {:08X} from {} ({:08X})",
                        faction->GetFormID(),
                        actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
        return true;
    }

    bool FactionReconciler::ApplyMembershipOp(
        RE::Actor* actor,
        RE::TESFaction* faction,
        State::FactionMembershipOp op,
        std::int8_t defaultRank,
        bool preserveIfNativeOnBase) {
        if (!actor || !faction || op == State::FactionMembershipOp::LeaveUntouched) {
            return false;
        }
        if (op == State::FactionMembershipOp::EnsurePresent) {
            if (actor->IsInFaction(faction)) {
                return false;
            }
            Utils::AddToFaction(actor, faction, defaultRank);
            MARAS_LOG_TRACE("[FactionReconciler] Added faction {:08X} (rank {}) to {} ({:08X})",
                            faction->GetFormID(), defaultRank,
                            actor->GetName() ? actor->GetName() : "Unknown", actor->GetFormID());
            return true;
        }
        return SyncActorFaction(actor, faction, std::nullopt, preserveIfNativeOnBase);
    }

    void FactionReconciler::ReconcilePlayer() {
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            DeferredActorResolver::GetSingleton().RequestSync(kPlayerFormID, DeferredActorResolver::SyncFlags::Factions);
            return;
        }

        ReconcilePlayerOnMainThread();
    }

    void FactionReconciler::ReconcilePlayerOnMainThread() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto plan = State::ComputePlayerFactionPlan(*snap);
        auto& fc = FormCache::GetSingleton();

        if (auto* marriedFac = fc.GetMarriedFaction()) {
            (void)ApplyMembershipOp(
                player, marriedFac,
                plan.hasMarriedFaction ? State::FactionMembershipOp::EnsurePresent : State::FactionMembershipOp::EnsureAbsent,
                0, false);
        }
        if (auto* nonCheatingFac = fc.GetCanFormNonCheatingMainFaction()) {
            (void)SyncActorFaction(player, nonCheatingFac, plan.canFormNonCheatingMainRank);
        }
    }

    void FactionReconciler::SanitizePlayerMarriedFaction() {
        ReconcilePlayer();
    }

    void FactionReconciler::ReconcileMerchantFactions(
        RE::Actor* actor,
        bool isHousedAdultTenant,
        bool stripNonNativeDefaultMerchants) {
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        struct MerchantMapping {
            RE::TESFaction* skyrimFaction;
            RE::TESFaction* housedFaction;
        };

        const std::array<MerchantMapping, 8> mappings{{
            { fc.GetApothecaryFaction(), fc.GetApothecaryMerchantHousedFaction() },
            { fc.GetBlacksmithFaction(), fc.GetBlacksmithMerchantHousedFaction() },
            { fc.GetFletcherFaction(), fc.GetFletcherMerchantHousedFaction() },
            { fc.GetHunterFaction(), fc.GetHunterMerchantHousedFaction() },
            { fc.GetInnKeeperFaction(), fc.GetInnkeeperMerchantHousedFaction() },
            { fc.GetJewelerFaction(), fc.GetJewelerMerchantHousedFaction() },
            { fc.GetSpellFaction(), fc.GetSpellsMerchantHousedFaction() },
            { fc.GetTailorFaction(), fc.GetTailorMerchantHousedFaction() }
        }};

        auto* miscHousedFac = fc.GetMiscMerchantHousedFaction();
        const bool hadMarasMisc = (miscHousedFac && actor->IsInFaction(miscHousedFac));

        if (isHousedAdultTenant) {
            bool foundSpecific = false;
            for (const auto& m : mappings) {
                const bool hasSpecialty =
                    m.skyrimFaction &&
                    (actor->IsInFaction(m.skyrimFaction) || IsNativeBaseFaction(actor, m.skyrimFaction));
                if (hasSpecialty) {
                    foundSpecific = true;
                    (void)SyncActorFaction(actor, m.housedFaction, std::int8_t{ 0 });
                } else {
                    (void)SyncActorFaction(actor, m.housedFaction, std::nullopt);
                }
            }

            if (!foundSpecific) {
                (void)ApplyMembershipOp(actor, fc.GetMerchantFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
                (void)ApplyMembershipOp(actor, fc.GetMiscFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
                (void)SyncActorFaction(actor, miscHousedFac, std::int8_t{ 0 });
            } else {
                (void)SyncActorFaction(actor, miscHousedFac, std::nullopt);
                (void)SyncActorFaction(actor, fc.GetMerchantFaction(), std::nullopt, true);
                (void)SyncActorFaction(actor, fc.GetMiscFaction(), std::nullopt, true);
            }
            return;
        }

        // Not a housed adult tenant: remove all 9 MARAS *MerchantHousedFaction factions,
        // and remove MerchantFaction/MiscFaction only when non-native and MARAS-relevant/previously MARAS-misc.
        for (const auto& m : mappings) {
            (void)SyncActorFaction(actor, m.housedFaction, std::nullopt);
        }
        (void)SyncActorFaction(actor, miscHousedFac, std::nullopt);

        if (stripNonNativeDefaultMerchants || hadMarasMisc) {
            (void)SyncActorFaction(actor, fc.GetMerchantFaction(), std::nullopt, true);
            (void)SyncActorFaction(actor, fc.GetMiscFaction(), std::nullopt, true);
        }
    }

    void FactionReconciler::ReconcileAdultFactions(
        RE::Actor* actor,
        const State::WorldStateSnapshot& snap,
        const State::AdultActorFactionPlan& plan) {
        (void)snap;
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        (void)SyncActorFaction(actor, fc.GetTrackedFaction(), plan.trackedRank);
        (void)SyncActorFaction(actor, fc.GetSpouseSocialClassFaction(), plan.socialClassRank);
        (void)SyncActorFaction(actor, fc.GetSpouseSkillTypeFaction(), plan.skillTypeRank);
        (void)SyncActorFaction(actor, fc.GetSpouseTemperamentFaction(), plan.temperamentRank);
        (void)SyncActorFaction(actor, fc.GetHierarchyFaction(), plan.hierarchyRank);
        (void)SyncActorFaction(actor, fc.GetAffectionFaction(), plan.affectionRank);
        (void)SyncActorFaction(actor, fc.GetSpouseChildrenFaction(), plan.spouseChildrenRank);
        (void)SyncActorFaction(actor, fc.GetSpouseCheatingFaction(), plan.spouseCheatingRank);
        (void)SyncActorFaction(actor, fc.GetSpouseOpenRelationshipFaction(), plan.spouseOpenRelationshipRank);
        (void)SyncActorFaction(actor, fc.GetCanFormNonCheatingMainFaction(), plan.canFormNonCheatingMainRank);
        (void)SyncActorFaction(actor, fc.GetSpouseHousedFaction(), plan.housedRank);
        (void)SyncActorFaction(actor, fc.GetSpouseSharedHouseFaction(), plan.spouseSharedHouseRank);
        (void)SyncActorFaction(actor, fc.GetPlayerFamilyBlockedFaction(), plan.playerFamilyBlockedRank);

        // Mirrored vanilla relationship & house factions
        (void)ApplyMembershipOp(actor, fc.GetMarriedFaction(), plan.marriedFactionOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetPlayerFaction(), plan.playerFactionOp, 0, true);
        (void)ApplyMembershipOp(actor, fc.GetPlayerBedOwnershipFaction(), plan.playerBedOwnershipOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetMarriageAskedFaction(), plan.marriageAskedOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetCourtingFaction(), plan.courtingOp, 0, false);
        (void)ApplyMembershipOp(actor, fc.GetPotentialHirelingFaction(), plan.potentialHirelingOp, 0, true);
        (void)ApplyMembershipOp(
            actor, fc.GetMarriagePotentialFaction(), plan.marriagePotentialOp, 0, !plan.forceStripMarriagePotential);

        if (plan.clearIgnoreProposeKeyword) {
            ClearKeywordOnBase(actor, fc.GetIgnoreProposeKeyword());
        }

        if (plan.clearYoungChildFactions) {
            ClearChildHouseholdFactionsOnMainThread(actor);
        }

        if (plan.ensurePotentialFollower) {
            (void)ApplyMembershipOp(actor, fc.GetPotentialFollowerFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensurePlayerWerewolf) {
            (void)ApplyMembershipOp(actor, fc.GetPlayerWerewolfFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensureCompanionsCircle) {
            (void)ApplyMembershipOp(actor, fc.GetCompanionsCircleFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }
        if (plan.ensureFrostmoonWerewolves) {
            (void)ApplyMembershipOp(actor, fc.GetFrostmoonWerewolvesFaction(), State::FactionMembershipOp::EnsurePresent, 0, true);
        }

        ReconcileMerchantFactions(actor, plan.isHousedAdultTenant, plan.isRelevantAdult);
    }

    void FactionReconciler::ReconcileChildFactions(
        RE::Actor* actor,
        const State::WorldStateSnapshot& snap,
        const State::ChildFactionPlan& plan) {
        if (!actor) {
            return;
        }

        auto& fc = FormCache::GetSingleton();

        (void)SyncActorFaction(actor, fc.GetPlayerChildFaction(), plan.playerChildRank);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionChildOwnedFaction(), plan.byohChildOwnedRank);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionFaction(), plan.byohAdoptionRank);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), plan.playerFactionRank, true);
        (void)SyncActorFaction(actor, fc.GetPlayerChildBothParentsFaction(), plan.bothParentsRank);

        if (plan.adoptableOp != State::FactionMembershipOp::LeaveUntouched) {
            if (auto* adoptableFac = fc.GetBYOHRelationshipAdoptableFaction()) {
                (void)SyncActorFaction(actor, adoptableFac, plan.DesiredAdoptableRank(), false);
            }
        }

        if (plan.restoreSavedCrimeFaction) {
            const State::ChildId childKey{ actor->GetFormID() };
            const auto& snaps = snap.household->AllChildFactionSnapshots();
            if (auto it = snaps.find(childKey); it != snaps.end() && !it->second.crimeFactionId.IsNull()) {
                if (auto* crimeFac = RE::TESForm::LookupByID<RE::TESFaction>(it->second.crimeFactionId.raw())) {
                    if (actor->GetCrimeFaction() != crimeFac) {
                        Utils::SetCrimeFaction(actor, crimeFac);
                        MARAS_LOG_DEBUG("[FactionReconciler] Restored crime faction {:08X} for household child {:08X}",
                                        crimeFac->GetFormID(), actor->GetFormID());
                    }
                }
            }
        }

        // Physical scaled-down BF Infants stay non-conversational (both vanilla/MARAS dialogue and SkyrimNet)
        // until promoted to Stage 2 Young Child at normal scale.
        if (plan.isPhysicalInfant) {
            actor->AllowPCDialogue(false);
            if (auto* filterFac = fc.GetSkyrimNetFilterFaction()) {
                (void)SyncActorFaction(actor, filterFac, std::int8_t{ 0 }, false);
            }
        } else if (plan.isYoungChild) {
            actor->AllowPCDialogue(true);
            if (auto* filterFac = fc.GetSkyrimNetFilterFaction()) {
                (void)SyncActorFaction(actor, filterFac, std::nullopt, false);
            }
        }
    }

    void FactionReconciler::ReconcileCompanionFactions(
        RE::Actor* actor,
        const State::CompanionFactionPlan& plan) {
        if (!actor) {
            return;
        }
        auto& fc = FormCache::GetSingleton();
        (void)SyncActorFaction(actor, fc.GetPlayerChildPetFaction(), plan.playerChildPetRank);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), plan.playerFactionRank, true);
    }

    namespace {
        [[nodiscard]] bool HasCandidateMarasFactionsOnUnknownActor(const RE::Actor* actor) noexcept {
            if (!actor) {
                return false;
            }
            if (actor->extraList.HasType<RE::ExtraFactionChanges>()) {
                return true;
            }
            const auto* base = actor->GetActorBase();
            if (!base || base->factions.empty()) {
                return false;
            }
            const auto* trackedFac = FormCache::GetSingleton().GetTrackedFaction();
            const auto* marasFile = trackedFac ? trackedFac->GetFile(0) : nullptr;
            if (!marasFile) {
                return true;
            }
            for (const auto& entry : base->factions) {
                if (entry.faction && entry.rank >= 0 && entry.faction->GetFile(0) == marasFile) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    void FactionReconciler::ClearUnknownActorMarasFactions(RE::Actor* actor) {
        if (!HasCandidateMarasFactionsOnUnknownActor(actor)) {
            return;
        }

        auto& fc = FormCache::GetSingleton();
        const std::array<RE::TESFaction*, 15> marasFactions{{
            fc.GetTrackedFaction(),
            fc.GetSpouseSocialClassFaction(),
            fc.GetSpouseSkillTypeFaction(),
            fc.GetSpouseTemperamentFaction(),
            fc.GetHierarchyFaction(),
            fc.GetAffectionFaction(),
            fc.GetSpouseChildrenFaction(),
            fc.GetSpouseCheatingFaction(),
            fc.GetSpouseOpenRelationshipFaction(),
            fc.GetCanFormNonCheatingMainFaction(),
            fc.GetSpouseHousedFaction(),
            fc.GetSpouseSharedHouseFaction(),
            fc.GetPlayerFamilyBlockedFaction(),
            fc.GetPlayerChildBothParentsFaction(),
            fc.GetPlayerChildPetFaction()
        }};

        for (auto* f : marasFactions) {
            (void)SyncActorFaction(actor, f, std::nullopt);
        }

        ReconcileMerchantFactions(actor, false, false);
    }

    void FactionReconciler::ClearChildHouseholdFactions(RE::Actor* actor) {
        if (!actor) {
            return;
        }
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            const RE::FormID actorId = actor->GetFormID();
            if (actorId != 0) {
                AddCurrentEpochTask("FactionReconciler::ClearChildHouseholdFactions", [actorId]() {
                    std::lock_guard lock(GetPapyrusMutex());
                    if (auto* a = RE::TESForm::LookupByID<RE::Actor>(actorId); a && !a->IsDeleted()) {
                        FactionReconciler::GetSingleton().ClearChildHouseholdFactionsOnMainThread(a);
                    }
                });
            }
            return;
        }
        ClearChildHouseholdFactionsOnMainThread(actor);
    }

    void FactionReconciler::ClearChildHouseholdFactionsOnMainThread(RE::Actor* actor) {
        if (!actor || actor->IsDeleted()) {
            return;
        }
        auto& fc = FormCache::GetSingleton();
        (void)SyncActorFaction(actor, fc.GetPlayerChildFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionChildOwnedFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptionFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetPlayerChildBothParentsFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetBYOHRelationshipAdoptableFaction(), std::nullopt);
        (void)SyncActorFaction(actor, fc.GetPlayerFaction(), std::nullopt, true);
    }

    void FactionReconciler::ReconcileLoadedActor(RE::Actor* actor) {
        if (!actor || actor->IsDeleted()) {
            return;
        }

        if (actor->IsPlayerRef()) {
            ReconcilePlayerOnMainThread();
            return;
        }

        const RE::FormID actorId = actor->GetFormID();
        if (actorId == 0 || IsSyntheticChildID(actorId)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::RefId refId{ actorId };
        const State::ChildId childKey{ actorId };

        // 1. Companion (pet/critter) role
        if (const auto companionPlan = State::ComputeCompanionFactionPlan(*snap, refId);
            companionPlan.isActiveCompanion) {
            ReconcileCompanionFactions(actor, companionPlan);
            return;
        }

        // 2. Young child (Stage 0-3)
        if (const auto* childRec = snap->lineage->FindChild(childKey)) {
            if (childRec->stage != State::ChildStage::Adult) {
                const Foundation::ActorId otherParent = childRec->guardians.OtherGuardian(Foundation::ActorId::Player());
                const State::ActorSex otherParentSex = LookupActorSex(otherParent);

                Foundation::RefId resolvedFollowTargetId{};
                if (childRec->stage == State::ChildStage::Child ||
                    childRec->stage == State::ChildStage::Infant) {
                    resolvedFollowTargetId = Foundation::RefId{
                        ChildSpawnService::GetSingleton().ResolveNonPlayerChildTarget(*childRec) };
                }

                auto* orphanageMarker = FormCache::GetSingleton().GetHonorhallOrphanageCenterMarker();
                const Foundation::RefId orphanageMarkerId{
                    orphanageMarker ? orphanageMarker->GetFormID() : RE::FormID{ 0 } };

                const auto childPlan = State::ComputeChildFactionPlan(
                    *snap,
                    childKey,
                    otherParentSex,
                    resolvedFollowTargetId,
                    orphanageMarkerId);

                ReconcileChildFactions(actor, *snap, childPlan);
                return;
            }
        }

        // 3. Adult / graduated Stage 4 child / tenant
        std::string_view adultClassId{};
        if (const auto* childRec = snap->lineage->FindChild(childKey);
            childRec && childRec->stage == State::ChildStage::Adult) {
            adultClassId = ResolveAdultChildClassId(*childRec);
        }

        const bool isBlockedFamily = IsBlockedPlayerFamilyActor(actorId, *snap);
        const auto adultPlan = State::ComputeAdultActorFactionPlan(
            *snap, Foundation::ActorId{ actorId }, adultClassId, isBlockedFamily);
        if (adultPlan.isRelevantAdult) {
            ReconcileAdultFactions(actor, *snap, adultPlan);
            return;
        }

        // 4. Unregistered / removed actor: strip any lingering MARAS factions safely
        if (!HasCandidateMarasFactionsOnUnknownActor(actor)) {
            return;
        }
        if (auto* petFac = FormCache::GetSingleton().GetPlayerChildPetFaction();
            petFac && actor->IsInFaction(petFac)) {
            ReconcileCompanionFactions(actor, State::CompanionFactionPlan{});
        }
        ClearUnknownActorMarasFactions(actor);
    }

    void FactionReconciler::ReconcileActor(RE::FormID actorId) {
        if (actorId == 0 || IsSyntheticChildID(actorId)) {
            return;
        }

        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            DeferredActorResolver::GetSingleton().RequestSync(actorId, DeferredActorResolver::SyncFlags::Factions);
            return;
        }

        if (actorId == kPlayerFormID) {
            ReconcilePlayerOnMainThread();
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor) {
            DeferredActorResolver::GetSingleton().MarkUnloaded(actorId, DeferredActorResolver::SyncFlags::Factions);
            return;
        }
        if (actor->IsDeleted()) {
            return;
        }

        ReconcileLoadedActor(actor);
    }

    void FactionReconciler::ReconcileActors(std::span<const RE::FormID> actorIds) {
        for (const RE::FormID actorId : actorIds) {
            ReconcileActor(actorId);
        }
    }

    void FactionReconciler::Revert() noexcept {
        pendingAllOnLoad_.store(false, std::memory_order_release);
    }

    void FactionReconciler::ReconcileAllOnLoad() {
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (!pendingAllOnLoad_.exchange(true, std::memory_order_acq_rel)) {
                if (!AddEpochTask(GetSessionEpoch(), "FactionReconciler::ReconcileAllOnLoad", []() {
                        std::lock_guard lock(GetPapyrusMutex());
                        FactionReconciler::GetSingleton().ReconcileAllOnLoadOnMainThread();
                    })) {
                    pendingAllOnLoad_.store(false, std::memory_order_release);
                }
            }
            return;
        }
        ReconcileAllOnLoadOnMainThread();
    }

    void FactionReconciler::ReconcileAllOnLoadOnMainThread() {
        pendingAllOnLoad_.store(false, std::memory_order_release);
        ReconcilePlayerOnMainThread();

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> targets;
        targets.reserve(
            snap->actors->AllProfiles().size() +
            snap->lineage->AllChildren().size() +
            snap->household->AllChildStates().size() * 2 +
            snap->household->AllHouses().size() * 4);

        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            (void)prof;
            if (!refId.IsNull() && !refId.IsPlayer() && !IsSyntheticChildID(refId.raw())) {
                targets.insert(refId.raw());
            }
        }

        for (const auto& [childId, rec] : snap->lineage->AllChildren()) {
            (void)rec;
            if (!childId.IsNull() && !childId.IsSynthetic()) {
                targets.insert(childId.raw());
            }
        }

        for (const auto& [childId, state] : snap->household->AllChildStates()) {
            (void)childId;
            if (!state.petId.IsNull() && !IsSyntheticChildID(state.petId.raw())) {
                targets.insert(state.petId.raw());
            }
            if (!state.critterId.IsNull() && !IsSyntheticChildID(state.critterId.raw())) {
                targets.insert(state.critterId.raw());
            }
        }

        for (const auto& house : snap->household->AllHouses()) {
            for (const std::uint32_t tenantRawId : house.tenantOrder) {
                if (tenantRawId != 0 && tenantRawId != kPlayerFormID && !IsSyntheticChildID(tenantRawId)) {
                    targets.insert(tenantRawId);
                }
            }
        }

        if (auto* processLists = RE::ProcessLists::GetSingleton()) {
            for (const auto& handle : processLists->highActorHandles) {
                if (!handle) {
                    continue;
                }
                auto actorPtr = handle.get();
                auto* actor = actorPtr.get();
                if (!actor || actor->IsPlayerRef() || actor->IsDeleted()) {
                    continue;
                }
                const RE::FormID id = actor->GetFormID();
                if (id != 0 && id != kPlayerFormID && !IsSyntheticChildID(id) &&
                    Utils::AreVanillaEngineFamily(id, kPlayerFormID)) {
                    targets.insert(id);
                }
            }
        }

        std::size_t reconciledCount = 0;
        for (const RE::FormID actorId : targets) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor) {
                DeferredActorResolver::GetSingleton().MarkUnloaded(
                    actorId, DeferredActorResolver::SyncFlags::Factions);
                continue;
            }
            if (!actor->IsDeleted()) {
                ReconcileLoadedActor(actor);
                ++reconciledCount;
            }
        }

        MARAS_LOG_INFO("[FactionReconciler] ReconcileAllOnLoad converged {} loaded actor(s) ({} deferred until load)",
                       reconciledCount, targets.size() - reconciledCount);
    }

}  // namespace MARAS
