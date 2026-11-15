#include "maras/state/WorldState.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace MARAS::State {

    namespace {

        template <typename Vec, typename IdType>
        void InsertSortedUnique(Vec& vec, IdType id) {
            if (id.IsNull()) {
                return;
            }
            const auto it = std::lower_bound(vec.begin(), vec.end(), id);
            if (it == vec.end() || *it != id) {
                vec.insert(it, id);
            }
        }

        void ClearResolvedPaternitySecret(ChildRecord& child, bool discoveredByPartner) noexcept {
            child.paternitySecret.discovered = true;
            if (discoveredByPartner) {
                child.paternitySecret.discoveredByPartner = true;
            }
            child.paternitySecret.isSecret = false;
            child.paternitySecret.isSecretForPartner = false;
            child.paternitySecret.isSecretForBioFather = false;
            child.paternitySecret.isPregnancySecretForPartner = false;
        }

        void ClearPartnerPaternitySecretSilently(ChildRecord& child) noexcept {
            child.paternitySecret.isSecretForPartner = false;
            child.paternitySecret.isPregnancySecretForPartner = false;
            if (!child.paternitySecret.isSecretForBioFather) {
                child.paternitySecret.isSecret = false;
            }
        }

    }  // namespace

    // =========================================================================
    // WorldMutationResult
    // =========================================================================

    void WorldMutationResult::MarkActorDirty(ActorId actor) {
        if (!actor.IsNull() && !actor.IsUnknownFather() && !actor.IsSyntheticChild()) {
            InsertSortedUnique(dirtyActors, actor);
        }
    }

    void WorldMutationResult::MarkChildDirty(ChildId child) {
        if (!child.IsNull()) {
            InsertSortedUnique(dirtyChildren, child);
        }
    }

    void WorldMutationResult::Merge(const WorldMutationResult& other) {
        if (other.changed) {
            changed = true;
        }
        for (const ActorId actor : other.dirtyActors) {
            MarkActorDirty(actor);
        }
        for (const ChildId child : other.dirtyChildren) {
            MarkChildDirty(child);
        }
    }

    // =========================================================================
    // WorldState — Snapshots & Read-Only Queries
    // =========================================================================

    WorldStateSnapshot WorldState::Snapshot() const noexcept {
        return WorldStateSnapshot{
            .actors = actors.Share(),
            .relationships = relationships.Share(),
            .affairs = affairs.Share(),
            .lineage = lineage.Share(),
            .hierarchy = hierarchy.Share(),
            .household = household.Share(),
            .affection = affection.Share(),
            .deceased = deceased.Share(),
            .cooldowns = cooldowns.Share(),
            .settings = settings.Share(),
        };
    }

    bool WorldState::IsMarried(ActorId a, ActorId b) const noexcept {
        return relationships->GetStatus(a, b) == RelationshipStatus::Married;
    }

    bool WorldState::IsDivorced(ActorId a, ActorId b) const noexcept {
        return relationships->GetStatus(a, b) == RelationshipStatus::Divorced;
    }

    bool WorldState::IsPlayerActiveMainPartner(ActorId npc) const noexcept {
        if (npc.IsNull() || npc.IsPlayer()) {
            return false;
        }
        return IsMainTierStatus(relationships->GetStatus(ActorId::Player(), npc));
    }

    std::vector<ActorId> WorldState::GetPlayerSpousesInMarriageOrder() const {
        struct RankedSpouse {
            ActorId id{};
            float marriedDay{ 0.0f };
        };

        std::vector<RankedSpouse> spouses;
        for (const CanonicalPairKey& key : relationships->GetAdjacentPairs(ActorId::Player())) {
            const auto* edge = relationships->FindEdge(key);
            if (!edge || edge->status != RelationshipStatus::Married) {
                continue;
            }
            const ActorId spouse = key.Other(ActorId::Player());
            if (spouse.IsNull() || deceased->IsDeceased(spouse)) {
                continue;
            }
            float marriageDay = 0.0f;
            for (const auto& h : edge->statusHistory) {
                if (h.toStatus == RelationshipStatus::Married) {
                    marriageDay = h.timestamp;
                }
            }
            spouses.push_back(RankedSpouse{ spouse, marriageDay });
        }

        std::sort(spouses.begin(), spouses.end(), [](const RankedSpouse& lhs, const RankedSpouse& rhs) {
            if (lhs.marriedDay != rhs.marriedDay) {
                return lhs.marriedDay < rhs.marriedDay;
            }
            return lhs.id < rhs.id;
        });

        std::vector<ActorId> out;
        out.reserve(spouses.size());
        for (const auto& sp : spouses) {
            out.push_back(sp.id);
        }
        return out;
    }

    ActorId WorldState::GetHighestRankingSurvivingPlayerSpouse(ActorId excludeSpouse) const {
        const auto marriedOrder = GetPlayerSpousesInMarriageOrder();
        for (int rank = 0; rank < static_cast<int>(SpouseHierarchyTable::kSlotCount); ++rank) {
            const ActorId slotOccupant = hierarchy->GetSpouseAtSlot(static_cast<std::size_t>(rank));
            if (!slotOccupant.IsNull() &&
                slotOccupant != excludeSpouse &&
                !deceased->IsDeceased(slotOccupant) &&
                std::find(marriedOrder.begin(), marriedOrder.end(), slotOccupant) != marriedOrder.end()) {
                return slotOccupant;
            }
        }
        for (const ActorId spouse : marriedOrder) {
            if (spouse != excludeSpouse && !deceased->IsDeceased(spouse)) {
                return spouse;
            }
        }
        return ActorId{};
    }

    ActorId WorldState::GetMotherPartner(ChildId childId) const noexcept {
        const auto* child = lineage->FindChild(childId);
        if (!child || child->bioMother.IsNull()) {
            return ActorId{};
        }
        if (!child->presumedFather.IsNull()) {
            return child->presumedFather;
        }
        const ActorId otherGuardian = child->guardians.OtherGuardian(child->bioMother);
        if (!otherGuardian.IsNull()) {
            return otherGuardian;
        }
        return relationships->GetPrimaryPartner(child->bioMother);
    }

    bool WorldState::IsActorCheating(ActorId actor) const {
        if (actor.IsNull()) {
            return false;
        }
        if (actor.IsPlayer() && settings->enablePlayerPolygamy) {
            return false;
        }

        std::vector<ActorId> closedMainPartners;
        for (const CanonicalPairKey& key : relationships->GetAdjacentPairs(actor)) {
            const auto* edge = relationships->FindEdge(key);
            if (edge && IsMainTierStatus(edge->status) && !edge->isOpen) {
                closedMainPartners.push_back(key.Other(actor));
            }
        }

        if (closedMainPartners.size() > 1) {
            return true;
        }

        if (!closedMainPartners.empty()) {
            const bool isActorPlayerPartner = IsPlayerActiveMainPartner(actor);
            for (const auto& [partner, st] : relationships->GetActivePartners(actor)) {
                (void)st;
                if (partner.IsNull() || partner == actor) {
                    continue;
                }
                if (std::find(closedMainPartners.begin(), closedMainPartners.end(), partner) ==
                    closedMainPartners.end()) {
                    if (isActorPlayerPartner &&
                        IsPlayerActiveMainPartner(partner) &&
                        closedMainPartners.size() == 1 &&
                        closedMainPartners.front().IsPlayer()) {
                        continue;
                    }
                    return true;
                }
            }
        }

        for (const AffairRecord& rec : affairs->GetByCheater(actor)) {
            if (rec.affairStatus == AffairStatus::Ongoing) {
                return true;
            }
        }

        return false;
    }

    bool WorldState::IsRelationshipCheating(ActorId actorA, ActorId actorB) const {
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB) {
            return false;
        }

        const bool bothCoSpouses = IsPlayerActiveMainPartner(actorA) && IsPlayerActiveMainPartner(actorB);

        auto hasClosedMainWithOther = [&](ActorId self, ActorId partner) -> bool {
            for (const CanonicalPairKey& key : relationships->GetAdjacentPairs(self)) {
                if (key.Contains(partner)) {
                    continue;
                }
                const auto* edge = relationships->FindEdge(key);
                if (!edge || !IsMainTierStatus(edge->status) || edge->isOpen) {
                    continue;
                }
                if (self.IsPlayer() && settings->enablePlayerPolygamy) {
                    continue;
                }
                if (bothCoSpouses && key.Other(self).IsPlayer()) {
                    continue;
                }
                return true;
            }
            return false;
        };

        if (hasClosedMainWithOther(actorA, actorB) || hasClosedMainWithOther(actorB, actorA)) {
            return true;
        }

        // Check active ongoing paramour record between actorA and actorB
        for (const AffairRecord& rec : affairs->GetByCheater(actorA)) {
            if (rec.paramour == actorB && rec.affairStatus == AffairStatus::Ongoing) {
                return true;
            }
        }
        for (const AffairRecord& rec : affairs->GetByCheater(actorB)) {
            if (rec.paramour == actorA && rec.affairStatus == AffairStatus::Ongoing) {
                return true;
            }
        }

        return false;
    }

    ActorId WorldState::ResolveSingleCustodian(
        ChildId childId,
        const ChildRecord& child,
        ActorId parentA,
        ActorId parentB,
        const IActorMetadataProvider& meta,
        Foundation::IRng& rng) const {
        if (parentA.IsNull()) {
            return parentB;
        }
        if (parentB.IsNull() || parentA == parentB) {
            return parentA;
        }

        const SettingsData& s = settings.Read();

        // 1. Biological parent priority (including unexposed presumed father when secret is still active)
        if (s.childCustodyPrioritizeBioParent) {
            const ActorId presumed = !child.presumedFather.IsNull() ? child.presumedFather : GetMotherPartner(childId);
            const bool isSecretPresumedA =
                child.paternitySecret.isSecretForPartner && !presumed.IsNull() && presumed == parentA;
            const bool isSecretPresumedB =
                child.paternitySecret.isSecretForPartner && !presumed.IsNull() && presumed == parentB;

            const bool bioA = (child.bioMother == parentA) || (child.bioFather == parentA) || isSecretPresumedA;
            const bool bioB = (child.bioMother == parentB) || (child.bioFather == parentB) || isSecretPresumedB;

            if (bioA && !bioB) {
                return parentA;
            }
            if (bioB && !bioA) {
                return parentB;
            }
        }

        // 2. Player custody policy override
        if (parentA.IsPlayer() || parentB.IsPlayer()) {
            const ActorId otherParent = parentA.IsPlayer() ? parentB : parentA;
            if (s.childCustodyPlayerPolicy == ChildCustodyPlayerPolicy::AlwaysWithPlayer) {
                return ActorId::Player();
            }
            if (s.childCustodyPlayerPolicy == ChildCustodyPlayerPolicy::NeverWithPlayer) {
                return otherParent;
            }
        }

        // 3. Gender custody policy (slot-neutral: inspects ActorSex via IActorMetadataProvider, NEVER slot position!)
        const ActorSex sexA = meta.GetSex(parentA);
        const ActorSex sexB = meta.GetSex(parentB);
        const bool aIsFemale = (sexA == ActorSex::Female);
        const bool bIsFemale = (sexB == ActorSex::Female);
        const bool aIsMale = (sexA == ActorSex::Male);
        const bool bIsMale = (sexB == ActorSex::Male);

        if ((aIsFemale && bIsMale) || (aIsMale && bIsFemale)) {
            const ActorId mother = aIsFemale ? parentA : parentB;
            const ActorId father = aIsFemale ? parentB : parentA;
            return (s.childCustodyGenderPolicy == ChildCustodyGenderPolicy::StayWithFather) ? father : mother;
        }

        // 4. Same-sex couple (or unknown sex): deterministic 50/50 coin flip via injected IRng
        return (rng.NextFloat(0.0f, 100.0f) < 50.0f) ? parentA : parentB;
    }

    // =========================================================================
    // Canonical Cross-Table Lifecycle Mutations
    // =========================================================================

    WorldMutationResult WorldState::TransitionRelationship(
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus targetStatus,
        float currentGameDay,
        ActorId initiator,
        const IActorMetadataProvider& meta,
        Foundation::IRng* optionalRng) {
        switch (targetStatus) {
            case RelationshipStatus::Married:
                return PromoteToMarried(actorA, actorB, currentGameDay, initiator, meta);
            case RelationshipStatus::Divorced:
                return PromoteToDivorced(actorA, actorB, currentGameDay, initiator, meta, optionalRng);
            case RelationshipStatus::Jilted:
            case RelationshipStatus::ExDating:
            case RelationshipStatus::ExLover:
                return BreakupPair(actorA, actorB, targetStatus, currentGameDay, initiator, meta, optionalRng);
            default:
                break;
        }

        WorldMutationResult result{};
        const CanonicalPairKey key{ actorA, actorB };
        if (!key.IsValid()) {
            return result;
        }

        if (!actorA.IsPlayer() && actorA.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(actorA.AsRefId(), true);
        }
        if (!actorB.IsPlayer() && actorB.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(actorB.AsRefId(), true);
        }

        auto& rels = relationships.Write();
        auto& edge = rels.GetOrCreateEdge(actorA, actorB);
        const RelationshipStatus prevStatus = edge.status;
        if (edge.originalStatus == RelationshipStatus::Unknown) {
            edge.originalStatus = prevStatus;
        }
        if (prevStatus != targetStatus) {
            edge.statusHistory.push_back(StatusTransitionEntry{
                .fromStatus = prevStatus,
                .toStatus = targetStatus,
                .timestamp = currentGameDay,
                .initiator = initiator,
            });
            edge.status = targetStatus;
            result.changed = true;
        }

        result.MarkActorDirty(actorA);
        result.MarkActorDirty(actorB);
        return result;
    }

    WorldMutationResult WorldState::PromoteToMarried(
        ActorId actorA,
        ActorId actorB,
        float currentGameDay,
        ActorId initiator,
        const IActorMetadataProvider& meta) {
        WorldMutationResult result{};
        const CanonicalPairKey key{ actorA, actorB };
        if (!key.IsValid()) {
            return result;
        }

        if (!actorA.IsPlayer() && actorA.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(actorA.AsRefId(), true);
        }
        if (!actorB.IsPlayer() && actorB.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(actorB.AsRefId(), true);
        }

        {
            auto& rels = relationships.Write();
            auto& edge = rels.GetOrCreateEdge(actorA, actorB);
            const RelationshipStatus prevStatus = edge.status;
            if (edge.originalStatus == RelationshipStatus::Unknown) {
                edge.originalStatus = prevStatus;
            }
            if (prevStatus != RelationshipStatus::Married) {
                edge.statusHistory.push_back(StatusTransitionEntry{
                    .fromStatus = prevStatus,
                    .toStatus = RelationshipStatus::Married,
                    .timestamp = currentGameDay,
                    .initiator = initiator,
                });
                edge.status = RelationshipStatus::Married;
                result.changed = true;
            }
        }

        if (actorA.IsPlayer() || actorB.IsPlayer()) {
            const ActorId spouse = key.Other(ActorId::Player());
            hierarchy.Write().OnSpouseAdded(spouse);
            if (!affection->HasPermanent(spouse)) {
                affection.Write().SetPermanent(spouse, 75);
            }
            result.changed = true;
        }

        result.MarkActorDirty(actorA);
        result.MarkActorDirty(actorB);
        result.Merge(EvaluateCustodyOnMarriage(actorA, actorB, meta));
        return result;
    }

    WorldMutationResult WorldState::PromoteToDivorced(
        ActorId actorA,
        ActorId actorB,
        float currentGameDay,
        ActorId initiator,
        const IActorMetadataProvider& meta,
        Foundation::IRng* optionalRng) {
        Foundation::DeterministicRng fallbackRng{};
        Foundation::IRng& rng = optionalRng ? *optionalRng : fallbackRng;

        WorldMutationResult result{};
        const CanonicalPairKey key{ actorA, actorB };
        if (!key.IsValid()) {
            return result;
        }

        {
            auto& rels = relationships.Write();
            auto& edge = rels.GetOrCreateEdge(actorA, actorB);
            const RelationshipStatus prevStatus = edge.status;
            if (edge.originalStatus == RelationshipStatus::Unknown) {
                edge.originalStatus = prevStatus;
            }
            if (prevStatus != RelationshipStatus::Divorced || edge.isOpen) {
                edge.statusHistory.push_back(StatusTransitionEntry{
                    .fromStatus = prevStatus,
                    .toStatus = RelationshipStatus::Divorced,
                    .timestamp = currentGameDay,
                    .initiator = initiator,
                });
                edge.status = RelationshipStatus::Divorced;
                edge.isOpen = false;
                result.changed = true;
            }
        }

        // Close ongoing infidelity records on the divorced couple & any paramour link between them
        for (const ActorId dirty : affairs.Write().CloseOngoingOnVictimSplit(actorA, actorB)) {
            result.MarkActorDirty(dirty);
            result.changed = true;
        }
        for (const ActorId dirty : affairs.Write().CloseOngoingBetweenCheaterAndParamour(actorA, actorB)) {
            result.MarkActorDirty(dirty);
            result.changed = true;
        }

        // Player-spouse divorce cleanup: hierarchy + Player House tenancy eviction + spouse home cell unsharing
        if (actorA.IsPlayer() || actorB.IsPlayer()) {
            const ActorId exSpouse = key.Other(ActorId::Player());
            relationships.Write().ClearPendingIntent(exSpouse);
            hierarchy.Write().OnSpouseRemoved(exSpouse, GetPlayerSpousesInMarriageOrder());
            if (household.Write().EvictTenant(exSpouse.raw())) {
                result.changed = true;
            }
            if (household.Write().StopSharingForSpouse(exSpouse)) {
                result.changed = true;
            }
        } else {
            if (relationships->GetPendingIntent(actorA).relatedLover == actorB) {
                relationships.Write().ClearPendingIntent(actorA);
            }
            if (relationships->GetPendingIntent(actorB).relatedLover == actorA) {
                relationships.Write().ClearPendingIntent(actorB);
            }
        }

        result.MarkActorDirty(actorA);
        result.MarkActorDirty(actorB);
        result.Merge(EvaluateCustodyOnDivorce(actorA, actorB, meta, rng));
        return result;
    }

    WorldMutationResult WorldState::BreakupPair(
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus breakupStatus,
        float currentGameDay,
        ActorId initiator,
        const IActorMetadataProvider& meta,
        Foundation::IRng* optionalRng) {
        if (breakupStatus == RelationshipStatus::Divorced) {
            return PromoteToDivorced(actorA, actorB, currentGameDay, initiator, meta, optionalRng);
        }

        Foundation::DeterministicRng fallbackRng{};
        Foundation::IRng& rng = optionalRng ? *optionalRng : fallbackRng;

        WorldMutationResult result{};
        const CanonicalPairKey key{ actorA, actorB };
        if (!key.IsValid()) {
            return result;
        }

        RelationshipStatus prevStatus = RelationshipStatus::Unknown;
        {
            auto& rels = relationships.Write();
            auto& edge = rels.GetOrCreateEdge(actorA, actorB);
            prevStatus = edge.status;
            if (edge.originalStatus == RelationshipStatus::Unknown) {
                edge.originalStatus = prevStatus;
            }
            edge.statusHistory.push_back(StatusTransitionEntry{
                .fromStatus = prevStatus,
                .toStatus = breakupStatus,
                .timestamp = currentGameDay,
                .initiator = initiator,
            });
            edge.status = breakupStatus;
            edge.isOpen = false;
            result.changed = true;
        }

        // Close ongoing affairs where (actorA, actorB) was the couple OR where one was cheater & the other paramour
        for (const ActorId dirty : affairs.Write().CloseOngoingOnVictimSplit(actorA, actorB)) {
            result.MarkActorDirty(dirty);
        }
        for (const ActorId dirty : affairs.Write().CloseOngoingBetweenCheaterAndParamour(actorA, actorB)) {
            result.MarkActorDirty(dirty);
        }

        if (actorA.IsPlayer() || actorB.IsPlayer()) {
            const ActorId exPartner = key.Other(ActorId::Player());
            relationships.Write().ClearPendingIntent(exPartner);
            hierarchy.Write().OnSpouseRemoved(exPartner, GetPlayerSpousesInMarriageOrder());
            if (household.Write().EvictTenant(exPartner.raw())) {
                result.changed = true;
            }
            if (household.Write().StopSharingForSpouse(exPartner)) {
                result.changed = true;
            }
            if (breakupStatus == RelationshipStatus::ExLover) {
                relationships.Write().ClearIntentsInvolving(exPartner);
            }
        } else {
            if (relationships->GetPendingIntent(actorA).relatedLover == actorB) {
                relationships.Write().ClearPendingIntent(actorA);
            }
            if (relationships->GetPendingIntent(actorB).relatedLover == actorA) {
                relationships.Write().ClearPendingIntent(actorB);
            }
        }

        if (IsMainTierStatus(prevStatus)) {
            result.Merge(EvaluateCustodyOnDivorce(actorA, actorB, meta, rng));
        }

        result.MarkActorDirty(actorA);
        result.MarkActorDirty(actorB);
        return result;
    }

    WorldMutationResult WorldState::SetPairOpen(
        ActorId actorA,
        ActorId actorB,
        bool isOpen,
        float currentGameDay) {
        WorldMutationResult result{};
        const CanonicalPairKey key{ actorA, actorB };
        if (!key.IsValid()) {
            return result;
        }

        auto& rels = relationships.Write();
        auto& edge = rels.GetOrCreateEdge(actorA, actorB);
        if (edge.isOpen != isOpen) {
            edge.isOpen = isOpen;
            result.changed = true;
        }

        if (isOpen) {
            affairs.Write().MarkAllowedForPair(actorA, actorB);
        } else {
            auto breakSideLoversIfClosed = [&](ActorId member) {
                if (member.IsPlayer() && settings->enablePlayerPolygamy) {
                    return;
                }
                if (rels.HasAnyOpenMainPair(member)) {
                    return;
                }
                const auto lovers = rels.GetPartnersWithStatus(member, RelationshipStatus::Lover);
                for (const ActorId lover : lovers) {
                    auto& loverEdge = rels.GetOrCreateEdge(member, lover);
                    loverEdge.statusHistory.push_back(StatusTransitionEntry{
                        .fromStatus = RelationshipStatus::Lover,
                        .toStatus = RelationshipStatus::ExLover,
                        .timestamp = currentGameDay,
                        .initiator = member,
                    });
                    loverEdge.status = RelationshipStatus::ExLover;
                    loverEdge.isOpen = false;
                    if (rels.GetPendingIntent(member).relatedLover == lover) {
                        rels.ClearPendingIntent(member);
                    }
                    if (rels.GetPendingIntent(lover).relatedLover == member) {
                        rels.ClearPendingIntent(lover);
                    }
                    for (const ActorId dirty : affairs.Write().CloseOngoingBetweenCheaterAndParamour(member, lover)) {
                        result.MarkActorDirty(dirty);
                    }
                    result.MarkActorDirty(lover);
                    result.changed = true;
                }
            };
            breakSideLoversIfClosed(actorA);
            breakSideLoversIfClosed(actorB);
        }

        if (actorA.IsPlayer() || actorB.IsPlayer()) {
            rels.ClearPendingIntent(key.Other(ActorId::Player()));
        }

        result.MarkActorDirty(actorA);
        result.MarkActorDirty(actorB);
        return result;
    }

    bool WorldState::RecordCheatingIncident(
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        bool isSecret,
        AffairType affairType,
        AffairStatus affairStatus,
        AffairStage affairStage,
        float currentGameDay,
        WorldMutationResult* outMutations) {
        if (cheater.IsNull() || deceivedPartner.IsNull() || paramour.IsNull() ||
            cheater == deceivedPartner || cheater == paramour || deceivedPartner == paramour) {
            return false;
        }

        // Exemption 1: Player polygamy
        if (cheater.IsPlayer() && settings->enablePlayerPolygamy) {
            return false;
        }

        // Exemption 2: Open relationship between cheater and deceivedPartner
        if (relationships->IsPairOpen(cheater, deceivedPartner) && affairStatus != AffairStatus::Allowed) {
            return false;
        }

        // Exemption 3: Co-spouses / active main partners of the Player are not cheating on the Player with each other
        if (deceivedPartner.IsPlayer() &&
            IsPlayerActiveMainPartner(cheater) &&
            IsPlayerActiveMainPartner(paramour)) {
            return false;
        }

        if (!cheater.IsPlayer() && cheater.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(cheater.AsRefId(), true);
        }
        if (!deceivedPartner.IsPlayer() && deceivedPartner.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(deceivedPartner.AsRefId(), true);
        }
        if (!paramour.IsPlayer() && paramour.HasPhysicalActor()) {
            actors.Write().SetRegisteredAdult(paramour.AsRefId(), true);
        }

        // Ensure canonical couple edge exists in RelationshipEdgeTable
        (void)relationships.Write().GetOrCreateEdge(cheater, deceivedPartner);

        const AffairIncidentOutcome outcome = affairs.Write().RecordIncident(
            cheater,
            deceivedPartner,
            paramour,
            isSecret,
            affairType,
            affairStatus,
            affairStage,
            currentGameDay);
        if (outcome.affairId.IsNull()) {
            return false;
        }

        // Auto-upgrade ConfessTalking -> ConfessPhysical when a Player-partner's Talking affair escalates
        if (outcome.escalatedTalkingToPhysical) {
            const PendingPartnerIntentRecord existingIntent = relationships->GetPendingIntent(cheater);
            if (existingIntent.type == PartnerIntentType::ConfessTalking &&
                (existingIntent.relatedLover.IsNull() || existingIntent.relatedLover == paramour)) {
                relationships.Write().SetPendingIntent(
                    cheater,
                    PendingPartnerIntentRecord{
                        .type = PartnerIntentType::ConfessPhysical,
                        .createdGameDay = currentGameDay,
                        .relatedLover = paramour,
                    });
            }
        }

        if (outMutations) {
            outMutations->changed = true;
            outMutations->MarkActorDirty(cheater);
            outMutations->MarkActorDirty(deceivedPartner);
            outMutations->MarkActorDirty(paramour);
        }
        return true;
    }

    // =========================================================================
    // Custody & Household Synchronization
    // =========================================================================

    WorldMutationResult WorldState::SyncChildHouseholdState(ChildId childId, HouseId explicitHouseId) {
        WorldMutationResult result{};
        if (childId.IsNull()) {
            return result;
        }

        const auto* child = lineage->FindChild(childId);
        if (!child) {
            if (household.Write().EvictTenant(childId.raw())) {
                result.changed = true;
            }
            if (household.Write().EraseChildState(childId)) {
                result.changed = true;
            }
            return result;
        }

        // Stage 4 Adult Children live/travel independently and never occupy young-child household slots
        if (child->stage == ChildStage::Adult) {
            if (household.Write().EvictTenant(childId.raw())) {
                result.changed = true;
            }
            if (household.Write().EraseChildState(childId)) {
                result.changed = true;
            }
            result.MarkChildDirty(childId);
            return result;
        }

        if (child->stage == ChildStage::Unborn) {
            return result;
        }

        const bool inPlayerHousehold = child->guardians.HasGuardian(ActorId::Player());
        if (!inPlayerHousehold) {
            if (household.Write().EvictTenant(childId.raw())) {
                result.changed = true;
            }
            if (household.Write().EraseChildState(childId)) {
                result.changed = true;
            }
            result.MarkChildDirty(childId);
            return result;
        }

        HouseId targetHouse = explicitHouseId;
        if (targetHouse.IsNull()) {
            const ActorId coGuardian = child->guardians.OtherGuardian(ActorId::Player());
            if (!coGuardian.IsNull() && !coGuardian.IsPlayer()) {
                targetHouse = household->GetTenantHouse(coGuardian.raw());
            }
        }
        if (targetHouse.IsNull()) {
            if (const auto* existingState = household->FindChildState(childId)) {
                if (!existingState->currentHouseId.IsNull() &&
                    household->FindHouse(existingState->currentHouseId) != nullptr) {
                    targetHouse = existingState->currentHouseId;
                }
            }
        }

        auto& hh = household.Write();
        if (hh.FindChildState(childId) == nullptr) {
            ChildHouseholdRecord stateRec{};
            stateRec.childId = childId;
            stateRec.currentHouseId = targetHouse;
            hh.UpsertChildState(std::move(stateRec));
            result.changed = true;
        }

        if (!targetHouse.IsNull() && hh.FindHouse(targetHouse) != nullptr) {
            if (hh.AssignTenant(childId.raw(), targetHouse, /*stayAtHome=*/true, /*isChildTenant=*/true)) {
                result.changed = true;
            }
        } else if (auto* mutableState = hh.FindChildStateMutable(childId)) {
            mutableState->currentHouseId = HouseId{};
        }

        result.MarkChildDirty(childId);
        return result;
    }

    WorldMutationResult WorldState::ApplyBirthCustodyRules(
        ChildId childId,
        ActorId motherId,
        ActorId presumedFatherId,
        ActorId bioFatherId,
        const IActorMetadataProvider& meta,
        Foundation::IRng& rng) {
        WorldMutationResult result{};
        if (childId.IsNull() || motherId.IsNull()) {
            return result;
        }

        auto* child = lineage.Write().FindChildMutable(childId);
        if (!child) {
            return result;
        }

        // 1. Married mother shares household with spouse
        if (!presumedFatherId.IsNull() && IsMarried(motherId, presumedFatherId)) {
            const auto diff = child->guardians.Assign(motherId, presumedFatherId);
            if (diff.changed) {
                result.changed = true;
            }
            result.MarkActorDirty(motherId);
            result.MarkActorDirty(presumedFatherId);
            result.Merge(SyncChildHouseholdState(childId));
            return result;
        }
        if (!bioFatherId.IsNull() && !bioFatherId.IsUnknownFather() && IsMarried(motherId, bioFatherId)) {
            const auto diff = child->guardians.Assign(motherId, bioFatherId);
            if (diff.changed) {
                result.changed = true;
            }
            result.MarkActorDirty(motherId);
            result.MarkActorDirty(bioFatherId);
            result.Merge(SyncChildHouseholdState(childId));
            return result;
        }
        if (const auto marriedSpouses = relationships->GetPartnersWithStatus(motherId, RelationshipStatus::Married);
            !marriedSpouses.empty()) {
            const ActorId spouse = marriedSpouses.front();
            const auto diff = child->guardians.Assign(motherId, spouse);
            if (diff.changed) {
                result.changed = true;
            }
            result.MarkActorDirty(motherId);
            result.MarkActorDirty(spouse);
            result.Merge(SyncChildHouseholdState(childId));
            return result;
        }

        // 2. Unmarried mother: resolve sole custodian according to custody policies
        ActorId otherParent{};
        if (!presumedFatherId.IsNull() && child->paternitySecret.isSecretForPartner) {
            otherParent = presumedFatherId;
        } else if (!bioFatherId.IsNull() && !bioFatherId.IsUnknownFather() &&
                   !child->paternitySecret.isSecretForBioFather) {
            otherParent = bioFatherId;
        }

        if (otherParent.IsNull() || otherParent == motherId) {
            const auto diff = child->guardians.Assign(motherId, ActorId{});
            if (diff.changed) {
                result.changed = true;
            }
            result.MarkActorDirty(motherId);
            result.Merge(SyncChildHouseholdState(childId));
            return result;
        }

        const ActorId custodian = ResolveSingleCustodian(childId, *child, motherId, otherParent, meta, rng);
        ActorId coGuardian{};
        if (custodian != motherId && !custodian.IsPlayer()) {
            const auto custodianSpouses = relationships->GetPartnersWithStatus(custodian, RelationshipStatus::Married);
            if (!custodianSpouses.empty()) {
                coGuardian = custodianSpouses.front();
            }
        }

        const auto diff = child->guardians.Assign(custodian, coGuardian);
        if (diff.changed) {
            result.changed = true;
        }
        result.MarkActorDirty(motherId);
        result.MarkActorDirty(otherParent);
        result.MarkActorDirty(custodian);
        result.MarkActorDirty(coGuardian);
        result.Merge(SyncChildHouseholdState(childId));
        return result;
    }

    WorldMutationResult WorldState::EvaluateCustodyOnMarriage(
        ActorId actorA,
        ActorId actorB,
        const IActorMetadataProvider& meta) {
        WorldMutationResult result{};
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB) {
            return result;
        }

        std::vector<ChildId> childIds;
        childIds.reserve(lineage->Size());
        for (const auto& [cId, rec] : lineage->AllChildren()) {
            (void)rec;
            childIds.push_back(cId);
        }
        std::sort(childIds.begin(), childIds.end());

        // 1. If biological mother and biological father marry each other, resolve any paternity secret on their child
        for (const ChildId cId : childIds) {
            const auto* existing = lineage->FindChild(cId);
            if (!existing) {
                continue;
            }
            const bool bioParentsMarried =
                (existing->bioMother == actorA && existing->bioFather == actorB) ||
                (existing->bioMother == actorB && existing->bioFather == actorA);
            if (bioParentsMarried && (existing->paternitySecret.HasActiveSecret() || !existing->presumedFather.IsNull())) {
                auto* mutableChild = lineage.Write().FindChildMutable(cId);
                mutableChild->presumedFather = ActorId{};
                mutableChild->paternitySecret = PaternitySecretState{};
                result.changed = true;
                result.MarkChildDirty(cId);
            }
        }

        // 2. Player x NPC Marriage
        if (actorA.IsPlayer() || actorB.IsPlayer()) {
            const ActorId spouse = actorA.IsPlayer() ? actorB : actorA;

            for (const ChildId cId : childIds) {
                const auto* childView = lineage->FindChild(cId);
                if (!childView || childView->stage == ChildStage::Adult) {
                    continue;
                }

                // Case A: Spouse brings single-parent or widowed children into Player's household
                if (childView->guardians.HasGuardian(spouse) && !childView->guardians.HasGuardian(ActorId::Player())) {
                    const ActorId otherGuardian = childView->guardians.OtherGuardian(spouse);
                    const bool otherGuardianDead =
                        !otherGuardian.IsNull() &&
                        (deceased->IsDeceased(otherGuardian) || !meta.IsAliveAndValid(otherGuardian));
                    const bool canLinkPlayer =
                        otherGuardian.IsNull() || otherGuardian == spouse || otherGuardianDead;
                    if (!canLinkPlayer) {
                        continue;
                    }

                    auto* mutableChild = lineage.Write().FindChildMutable(cId);
                    if (!mutableChild->isAdopted && mutableChild->bioMother.IsNull() && mutableChild->bioFather.IsNull()) {
                        if (meta.GetSex(spouse) == ActorSex::Female) {
                            mutableChild->bioMother = spouse;
                        } else if (meta.GetSex(spouse) == ActorSex::Male) {
                            mutableChild->bioFather = spouse;
                        }
                    }

                    const GuardianSetDiff diff = mutableChild->guardians.Assign(ActorId::Player(), spouse);
                    if (diff.changed) {
                        result.changed = true;
                        result.MarkActorDirty(diff.PrimaryRemoved());
                    }
                    result.MarkActorDirty(ActorId::Player());
                    result.MarkActorDirty(spouse);
                    result.MarkChildDirty(cId);

                    if (mutableChild->stage != ChildStage::Unborn) {
                        result.Merge(SyncChildHouseholdState(cId));
                    }
                }
                // Case B: Player has single-parent child (or marries child's bio-parent) -> link new spouse as co-guardian
                else if (childView->guardians.HasGuardian(ActorId::Player()) && !childView->guardians.HasGuardian(spouse)) {
                    if (childView->stage == ChildStage::Unborn &&
                        !childView->bioMother.IsPlayer() &&
                        childView->bioMother != spouse) {
                        continue;
                    }

                    const bool isSinglePlayerChild = childView->guardians.IsSingleGuardian();
                    const bool spouseIsBioParent =
                        !childView->isAdopted &&
                        (childView->bioMother == spouse || childView->bioFather == spouse);
                    if (!isSinglePlayerChild && !spouseIsBioParent) {
                        continue;
                    }

                    auto* mutableChild = lineage.Write().FindChildMutable(cId);
                    const GuardianSetDiff diff = mutableChild->guardians.Assign(ActorId::Player(), spouse);
                    if (diff.changed) {
                        result.changed = true;
                        result.MarkActorDirty(diff.PrimaryRemoved());
                    }
                    result.MarkActorDirty(ActorId::Player());
                    result.MarkActorDirty(spouse);
                    result.MarkChildDirty(cId);

                    if (mutableChild->stage != ChildStage::Unborn) {
                        HouseId childHouse{};
                        if (const auto* childState = household->FindChildState(cId)) {
                            childHouse = childState->currentHouseId;
                        }
                        if (!childHouse.IsNull() && household->GetTenantHouse(spouse.raw()).IsNull()) {
                            household.Write().AssignTenant(spouse.raw(), childHouse, /*stayAtHome=*/true, /*isChildTenant=*/false);
                        }
                        result.Merge(SyncChildHouseholdState(cId, childHouse));
                    }
                }
            }
            return result;
        }

        // 3. NPC x NPC Marriage
        for (const ChildId cId : childIds) {
            const auto* childView = lineage->FindChild(cId);
            if (!childView || childView->stage == ChildStage::Adult) {
                continue;
            }
            const bool hasA = childView->guardians.HasGuardian(actorA);
            const bool hasB = childView->guardians.HasGuardian(actorB);
            if (hasA && hasB) {
                continue;
            }
            if ((hasA || hasB) && childView->guardians.IsSingleGuardian()) {
                const ActorId existingParent = childView->guardians.SingleGuardian();
                const ActorId newCoParent = hasA ? actorB : actorA;

                auto* mutableChild = lineage.Write().FindChildMutable(cId);
                if (!mutableChild->isAdopted && mutableChild->bioMother.IsNull() && mutableChild->bioFather.IsNull()) {
                    if (meta.GetSex(existingParent) == ActorSex::Female) {
                        mutableChild->bioMother = existingParent;
                    } else if (meta.GetSex(existingParent) == ActorSex::Male) {
                        mutableChild->bioFather = existingParent;
                    }
                }
                const GuardianSetDiff diff = mutableChild->guardians.Assign(existingParent, newCoParent);
                if (diff.changed) {
                    result.changed = true;
                }
                result.MarkActorDirty(actorA);
                result.MarkActorDirty(actorB);
                result.MarkChildDirty(cId);
                if (mutableChild->stage != ChildStage::Unborn) {
                    result.Merge(SyncChildHouseholdState(cId));
                }
            }
        }

        return result;
    }

    WorldMutationResult WorldState::EvaluateCustodyOnDivorce(
        ActorId actorA,
        ActorId actorB,
        const IActorMetadataProvider& meta,
        Foundation::IRng& rng) {
        WorldMutationResult result{};
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB) {
            return result;
        }

        std::vector<ChildId> childIds;
        childIds.reserve(lineage->Size());
        for (const auto& [cId, rec] : lineage->AllChildren()) {
            (void)rec;
            childIds.push_back(cId);
        }
        std::sort(childIds.begin(), childIds.end());

        // 1. Unborn pregnancies & active paternity secrets involving the splitting couple
        for (const ChildId cId : childIds) {
            const auto* childView = lineage->FindChild(cId);
            if (!childView) {
                continue;
            }
            const bool motherIsA = (childView->bioMother == actorA);
            const bool motherIsB = (childView->bioMother == actorB);
            if (!motherIsA && !motherIsB) {
                continue;
            }
            const ActorId formerSpouse = motherIsA ? actorB : actorA;

            if (childView->stage == ChildStage::Unborn) {
                if (childView->guardians.HasGuardian(formerSpouse) || childView->presumedFather == formerSpouse) {
                    auto* mutableChild = lineage.Write().FindChildMutable(cId);
                    if (mutableChild->paternitySecret.HasActiveSecret() ||
                        mutableChild->paternitySecret.isSecretForPartner ||
                        mutableChild->paternitySecret.isSecretForBioFather) {
                        ClearResolvedPaternitySecret(*mutableChild, /*discoveredByPartner=*/true);
                    }
                    (void)mutableChild->guardians.Remove(formerSpouse);
                    if (!mutableChild->guardians.HasGuardian(mutableChild->bioMother) &&
                        !mutableChild->bioMother.IsNull()) {
                        (void)mutableChild->guardians.Assign(mutableChild->bioMother, ActorId{});
                    }
                    if (mutableChild->presumedFather == formerSpouse) {
                        mutableChild->presumedFather = ActorId{};
                    }
                    result.changed = true;
                    result.MarkChildDirty(cId);
                    result.MarkActorDirty(actorA);
                    result.MarkActorDirty(actorB);
                }
            } else if (childView->stage != ChildStage::Adult &&
                       childView->presumedFather == formerSpouse &&
                       childView->paternitySecret.HasActiveSecret()) {
                auto* mutableChild = lineage.Write().FindChildMutable(cId);
                ClearResolvedPaternitySecret(*mutableChild, /*discoveredByPartner=*/true);
                result.changed = true;
                result.MarkChildDirty(cId);

                if (mutableChild->guardians.IsSoleGuardian(formerSpouse)) {
                    const ActorId custodian =
                        ResolveSingleCustodian(cId, *mutableChild, mutableChild->bioMother, formerSpouse, meta, rng);
                    const GuardianSetDiff diff = mutableChild->guardians.Assign(custodian, ActorId{});
                    if (diff.changed) {
                        result.MarkActorDirty(mutableChild->bioMother);
                        result.MarkActorDirty(formerSpouse);
                        result.Merge(SyncChildHouseholdState(cId));
                    }
                }
            }
        }

        // 2. Shared non-adult children of (actorA, actorB) -> resolve single custodian
        for (const ChildId cId : childIds) {
            const auto* childView = lineage->FindChild(cId);
            if (!childView || childView->stage == ChildStage::Adult || childView->stage == ChildStage::Unborn) {
                continue;
            }
            if (!childView->guardians.HasSameSet(actorA, actorB)) {
                continue;
            }

            auto* mutableChild = lineage.Write().FindChildMutable(cId);
            if (mutableChild->paternitySecret.HasActiveSecret() ||
                mutableChild->paternitySecret.isSecretForPartner ||
                mutableChild->paternitySecret.isSecretForBioFather) {
                ClearResolvedPaternitySecret(*mutableChild, /*discoveredByPartner=*/true);
            }

            const ActorId custodian = ResolveSingleCustodian(cId, *mutableChild, actorA, actorB, meta, rng);
            const GuardianSetDiff diff = mutableChild->guardians.Assign(custodian, ActorId{});
            if (diff.changed) {
                result.changed = true;
            }
            result.MarkActorDirty(actorA);
            result.MarkActorDirty(actorB);
            result.MarkChildDirty(cId);
            result.Merge(SyncChildHouseholdState(cId));
        }

        return result;
    }

    WorldMutationResult WorldState::EvaluateCustodyOnDeath(
        ActorId deceasedActor,
        const IActorMetadataProvider& meta) {
        (void)meta;
        WorldMutationResult result{};
        if (deceasedActor.IsNull() || deceasedActor.IsPlayer()) {
            return result;
        }

        const ActorId highestSurvivingSpouse = GetHighestRankingSurvivingPlayerSpouse(deceasedActor);
        const HouseId nextSpouseHouse =
            !highestSurvivingSpouse.IsNull() ? household->GetTenantHouse(highestSurvivingSpouse.raw()) : HouseId{};

        std::vector<ChildId> childIds;
        childIds.reserve(lineage->Size());
        for (const auto& [cId, rec] : lineage->AllChildren()) {
            (void)rec;
            childIds.push_back(cId);
        }
        std::sort(childIds.begin(), childIds.end());

        std::vector<ChildId> unbornToRemove;

        for (const ChildId cId : childIds) {
            const auto* childView = lineage->FindChild(cId);
            if (!childView || childView->stage == ChildStage::Adult) {
                continue;
            }

            // Unborn fetus dies if biological mother dies
            if (childView->stage == ChildStage::Unborn && childView->bioMother == deceasedActor) {
                unbornToRemove.push_back(cId);
                continue;
            }

            const bool isDeceasedGuardian = childView->guardians.HasGuardian(deceasedActor);
            const bool isBioParent =
                (childView->bioMother == deceasedActor) || (childView->bioFather == deceasedActor);
            const bool isPresumedFather = (childView->presumedFather == deceasedActor);

            if (!isDeceasedGuardian && !isBioParent && !isPresumedFather) {
                continue;
            }

            auto* mutableChild = lineage.Write().FindChildMutable(cId);
            if (mutableChild->stage == ChildStage::Unborn) {
                if (isPresumedFather) {
                    mutableChild->presumedFather = ActorId{};
                }
                if (isDeceasedGuardian) {
                    (void)mutableChild->guardians.Remove(deceasedActor);
                }
                if (!mutableChild->guardians.HasAnyGuardian() && !mutableChild->bioMother.IsNull()) {
                    (void)mutableChild->guardians.Assign(mutableChild->bioMother, ActorId{});
                }
                if (mutableChild->paternitySecret.HasActiveSecret()) {
                    if (isDeceasedGuardian || isPresumedFather) {
                        ClearPartnerPaternitySecretSilently(*mutableChild);
                    } else if (mutableChild->bioFather == deceasedActor &&
                               mutableChild->paternitySecret.isSecretForBioFather) {
                        mutableChild->paternitySecret.isSecretForBioFather = false;
                        if (!mutableChild->paternitySecret.isSecretForPartner) {
                            mutableChild->paternitySecret.isSecret = false;
                        }
                    }
                }
                result.changed = true;
                result.MarkChildDirty(cId);
                continue;
            }

            if (!isDeceasedGuardian) {
                if (isPresumedFather) {
                    mutableChild->presumedFather = ActorId{};
                    result.changed = true;
                    result.MarkChildDirty(cId);
                }
                if (!mutableChild->guardians.HasAnyGuardian()) {
                    result.Merge(SyncChildHouseholdState(cId));
                }
                continue;
            }

            const ActorId survivingParent = mutableChild->guardians.OtherGuardian(deceasedActor);
            if (survivingParent.IsPlayer()) {
                if (!highestSurvivingSpouse.IsNull()) {
                    (void)mutableChild->guardians.Assign(ActorId::Player(), highestSurvivingSpouse);
                    result.MarkActorDirty(ActorId::Player());
                    result.MarkActorDirty(highestSurvivingSpouse);
                    result.Merge(SyncChildHouseholdState(cId, nextSpouseHouse));
                } else {
                    (void)mutableChild->guardians.Remove(deceasedActor);
                    if (mutableChild->paternitySecret.HasActiveSecret()) {
                        ClearPartnerPaternitySecretSilently(*mutableChild);
                    }
                    result.MarkActorDirty(ActorId::Player());
                    result.Merge(SyncChildHouseholdState(cId));
                }
            } else if (!survivingParent.IsNull()) {
                (void)mutableChild->guardians.Remove(deceasedActor);
                if (mutableChild->paternitySecret.HasActiveSecret()) {
                    ClearPartnerPaternitySecretSilently(*mutableChild);
                }
                result.MarkActorDirty(survivingParent);
                result.Merge(SyncChildHouseholdState(cId));
            } else {
                // Sole guardian died -> child becomes an orphan (0, 0)
                (void)mutableChild->guardians.Remove(deceasedActor);
                result.Merge(SyncChildHouseholdState(cId));
            }

            result.changed = true;
            result.MarkChildDirty(cId);
        }

        for (const ChildId unbornId : unbornToRemove) {
            if (const auto* unborn = lineage->FindChild(unbornId)) {
                result.MarkActorDirty(unborn->guardians.slotA);
                result.MarkActorDirty(unborn->guardians.slotB);
            }
            lineage.Write().EraseChild(unbornId);
            household.Write().EraseChildState(unbornId);
            result.changed = true;
            result.MarkChildDirty(unbornId);
        }

        return result;
    }

    WorldMutationResult WorldState::SyncAllMarriedCouplesChildCustody(
        const IActorMetadataProvider& meta,
        Foundation::IRng& rng) {
        WorldMutationResult result{};

        std::vector<ChildId> childIds;
        childIds.reserve(lineage->Size());
        for (const auto& [cId, rec] : lineage->AllChildren()) {
            (void)rec;
            childIds.push_back(cId);
        }
        std::sort(childIds.begin(), childIds.end());

        // 1. Repair invalid presumedFather & normalize unmarried 2-guardian children / unmarried Player+NPC births
        for (const ChildId cId : childIds) {
            const auto* childView = lineage->FindChild(cId);
            if (!childView) {
                continue;
            }

            if (!childView->presumedFather.IsNull() &&
                ((!childView->paternitySecret.isSecret && !childView->paternitySecret.discovered) ||
                 childView->presumedFather == childView->bioMother ||
                 (!childView->bioFather.IsNull() && childView->presumedFather == childView->bioFather))) {
                lineage.Write().FindChildMutable(cId)->presumedFather = ActorId{};
                result.changed = true;
                result.MarkChildDirty(cId);
                childView = lineage->FindChild(cId);
            }

            if (childView->stage == ChildStage::Unborn || childView->stage == ChildStage::Adult) {
                continue;
            }

            if (childView->guardians.Count() == 2) {
                const ActorId gA = childView->guardians.slotA;
                const ActorId gB = childView->guardians.slotB;
                if (!IsMarried(gA, gB)) {
                    const ActorId custodian = ResolveSingleCustodian(cId, *childView, gA, gB, meta, rng);
                    auto* mutableChild = lineage.Write().FindChildMutable(cId);
                    const GuardianSetDiff diff = mutableChild->guardians.Assign(custodian, ActorId{});
                    if (diff.changed) {
                        result.changed = true;
                        result.MarkActorDirty(gA);
                        result.MarkActorDirty(gB);
                        result.MarkChildDirty(cId);
                        result.Merge(SyncChildHouseholdState(cId));
                    }
                }
            } else if (!childView->isAdopted &&
                       childView->isBiologicalChild &&
                       childView->guardians.IsSoleGuardian(ActorId::Player()) &&
                       !childView->bioMother.IsNull() &&
                       !childView->bioFather.IsNull() &&
                       !childView->bioFather.IsUnknownFather() &&
                       (childView->bioMother.IsPlayer() || childView->bioFather.IsPlayer())) {
                const ActorId npcBioParent =
                    childView->bioMother.IsPlayer() ? childView->bioFather : childView->bioMother;
                if (!npcBioParent.IsNull() &&
                    !npcBioParent.IsPlayer() &&
                    !IsMarried(ActorId::Player(), npcBioParent) &&
                    !IsDivorced(ActorId::Player(), npcBioParent) &&
                    !deceased->IsDeceased(npcBioParent) &&
                    meta.IsAliveAndValid(npcBioParent)) {
                    const ActorId expectedCustodian =
                        ResolveSingleCustodian(cId, *childView, childView->bioMother, childView->bioFather, meta, rng);
                    if (expectedCustodian == npcBioParent) {
                        auto* mutableChild = lineage.Write().FindChildMutable(cId);
                        (void)mutableChild->guardians.Assign(npcBioParent, ActorId{});
                        result.changed = true;
                        result.MarkActorDirty(ActorId::Player());
                        result.MarkActorDirty(npcBioParent);
                        result.MarkChildDirty(cId);
                        result.Merge(SyncChildHouseholdState(cId));
                    }
                }
            }
        }

        // 2. Synchronize shared custody across all married couples in deterministic order
        std::vector<CanonicalPairKey> marriedPairs;
        for (const auto& [pair, edge] : relationships->AllEdges()) {
            if (edge.status == RelationshipStatus::Married) {
                marriedPairs.push_back(pair);
            }
        }
        std::sort(marriedPairs.begin(), marriedPairs.end());

        for (const CanonicalPairKey& pair : marriedPairs) {
            result.Merge(EvaluateCustodyOnMarriage(pair.first, pair.second, meta));
        }

        return result;
    }

    // =========================================================================
    // Death & Unregistration
    // =========================================================================

    WorldMutationResult WorldState::RecordActorDeath(
        ActorId deceasedActor,
        float deathGameDay,
        bool killedByPlayer,
        ActorId killerActor,
        const IActorMetadataProvider& meta) {
        WorldMutationResult result{};
        if (deceasedActor.IsNull() || deceasedActor.IsPlayer() || deceasedActor.IsUnknownFather()) {
            return result;
        }

        auto resolveStorageBase = [&](ActorId id) -> BaseId {
            if (id.IsNull() || id.IsUnknownFather() || id.IsSyntheticChild()) {
                return BaseId{ id.raw() };
            }
            if (id.IsPlayer()) {
                return BaseId{ Foundation::kPlayerRefRawId };
            }
            if (id.IsDynamicRuntimeActor()) {
                return BaseId{ id.raw() };
            }
            if (const auto* prof = actors->Find(id.AsRefId()); prof && !prof->baseId.IsNull()) {
                return prof->baseId;
            }
            const BaseId metaBase = meta.GetActorBaseId(id);
            return !metaBase.IsNull() ? metaBase : BaseId{ id.raw() };
        };

        auto buildChildMeta = [&](const ChildRecord& c) -> DeceasedChildMetadata {
            DeceasedChildMetadata cMeta{};
            cMeta.guardianARefId = c.guardians.slotA.AsRefId();
            cMeta.guardianABaseId = resolveStorageBase(c.guardians.slotA);
            cMeta.guardianAName = meta.GetActorName(c.guardians.slotA);

            cMeta.guardianBRefId = c.guardians.slotB.AsRefId();
            cMeta.guardianBBaseId = resolveStorageBase(c.guardians.slotB);
            cMeta.guardianBName = meta.GetActorName(c.guardians.slotB);

            cMeta.bioMotherBaseId = resolveStorageBase(c.bioMother);
            cMeta.bioFatherBaseId = resolveStorageBase(c.bioFather);
            cMeta.isAdopted = c.isAdopted;

            {
                ActorId motherPartner = c.presumedFather;
                if (motherPartner.IsNull()) {
                    motherPartner = c.guardians.OtherGuardian(c.bioMother);
                }
                if (motherPartner.IsNull() && !c.bioMother.IsNull()) {
                    motherPartner = relationships->GetPrimaryPartner(c.bioMother);
                }
                if (!motherPartner.IsNull() &&
                    !c.bioMother.IsNull() &&
                    c.presumedFather.IsNull() &&
                    !c.paternitySecret.isSecretForPartner &&
                    !c.guardians.HasGuardian(motherPartner) &&
                    IsBreakupStatus(relationships->GetStatus(c.bioMother, motherPartner))) {
                    motherPartner = ActorId{};
                }
                const ActorId effectivePresumedFather = !c.presumedFather.IsNull() ? c.presumedFather : motherPartner;
                const bool isBioSameAsPresumed =
                    !effectivePresumedFather.IsNull() &&
                    !c.bioFather.IsNull() &&
                    effectivePresumedFather == c.bioFather;
                const bool hasPresumedFather =
                    !effectivePresumedFather.IsNull() &&
                    !isBioSameAsPresumed &&
                    (c.bioMother.IsNull() || effectivePresumedFather != c.bioMother);
                const auto& sec = c.paternitySecret;
                const bool secretFromPartnerActive =
                    !sec.discovered &&
                    (sec.isSecretForPartner || (c.stage == ChildStage::Unborn && sec.isPregnancySecretForPartner)) &&
                    !sec.discoveredByPartner;
                const bool secretFromBioFatherActive =
                    !sec.discovered &&
                    sec.isSecretForBioFather &&
                    !sec.discoveredByBioFather;
                const bool rawHasUndiscoveredSecret =
                    sec.HasActiveSecret() && (secretFromPartnerActive || secretFromBioFatherActive);
                if (!isBioSameAsPresumed && rawHasUndiscoveredSecret) {
                    cMeta.hasUndiscoveredPaternitySecret = true;
                    cMeta.publicBioFatherBaseId =
                        (!c.isAdopted && secretFromPartnerActive && hasPresumedFather)
                            ? resolveStorageBase(effectivePresumedFather)
                            : BaseId{};
                }
            }

            if (const auto* cState = household->FindChildState(c.childId); cState && !cState->currentHouseId.IsNull()) {
                cMeta.houseId = cState->currentHouseId;
            } else {
                cMeta.houseId = household->GetTenantHouse(c.childId.raw());
            }
            return cMeta;
        };

        const ChildId maybeChildId{ deceasedActor.raw() };
        const ChildRecord* existingChild = lineage->FindChild(maybeChildId);

        // 1. Young Child / Infant / Training death (Stage != Adult)
        if (existingChild != nullptr && existingChild->stage != ChildStage::Adult) {
            const ActorId gA = existingChild->guardians.slotA;
            const ActorId gB = existingChild->guardians.slotB;

            if (deceasedActor.HasPhysicalActor()) {
                DeceasedActorRecord rec{};
                rec.refId = deceasedActor.AsRefId();
                rec.baseId = resolveStorageBase(deceasedActor);
                rec.name = !existingChild->name.empty() ? existingChild->name : meta.GetActorName(deceasedActor);
                rec.deathTimestamp = deathGameDay;
                rec.isChild = true;
                rec.wasKilledByPlayer = killedByPlayer;
                if (!killerActor.IsNull() && killerActor.HasPhysicalActor()) {
                    rec.killerRefId = killerActor.AsRefId();
                    rec.killerBaseId = resolveStorageBase(killerActor);
                    rec.killerName = meta.GetActorName(killerActor);
                }
                rec.childInfo = buildChildMeta(*existingChild);
                deceased.Write().UpsertRecord(std::move(rec));
            }

            lineage.Write().EraseChild(maybeChildId);
            household.Write().EvictTenant(maybeChildId.raw());
            household.Write().EraseChildState(maybeChildId);
            household.Write().EraseChildFactionSnapshot(maybeChildId);
            household.Write().EraseAdultChildTravel(maybeChildId);

            result.changed = true;
            result.MarkChildDirty(maybeChildId);
            result.MarkActorDirty(gA);
            result.MarkActorDirty(gB);
            return result;
        }

        // 2. Adult death (including graduated Stage 4 Adult Children — NO early return!)
        std::optional<DeceasedChildMetadata> adultChildLineageMeta{};
        std::string fallbackChildName{};
        if (existingChild != nullptr && existingChild->stage == ChildStage::Adult) {
            adultChildLineageMeta = buildChildMeta(*existingChild);
            fallbackChildName = existingChild->name;
        }

        const RefId deceasedRef = deceasedActor.AsRefId();
        const ActorProfileRecord* profile = actors->Find(deceasedRef);
        const auto adjacentPairs = relationships->GetAdjacentPairs(deceasedActor);
        const bool isTrackedAdult =
            (profile != nullptr && profile->isRegisteredAdult) ||
            !adjacentPairs.empty() ||
            adultChildLineageMeta.has_value();

        if (isTrackedAdult && !deceasedRef.IsNull() && !deceased->IsDeceased(deceasedActor)) {
            DeceasedActorRecord rec{};
            rec.refId = deceasedRef;
            rec.baseId = resolveStorageBase(deceasedActor);
            rec.name = !fallbackChildName.empty() ? fallbackChildName : meta.GetActorName(deceasedActor);
            rec.deathTimestamp = deathGameDay;
            rec.isChild = false;
            rec.wasKilledByPlayer = killedByPlayer;
            if (!killerActor.IsNull() && killerActor.HasPhysicalActor()) {
                rec.killerRefId = killerActor.AsRefId();
                rec.killerBaseId = resolveStorageBase(killerActor);
                rec.killerName = meta.GetActorName(killerActor);
            }
            if (profile) {
                rec.socialClass = profile->socialClass;
                rec.skillType = profile->skillType;
                rec.temperament = profile->temperament;
            }
            rec.childInfo = std::move(adultChildLineageMeta);

            std::vector<CanonicalPairKey> sortedPairs = adjacentPairs;
            std::sort(sortedPairs.begin(), sortedPairs.end());
            for (const CanonicalPairKey& pair : sortedPairs) {
                const auto* edge = relationships->FindEdge(pair);
                if (!edge || !Foundation::IsRomanticOrExStatus(edge->status)) {
                    continue;
                }
                const ActorId partner = pair.Other(deceasedActor);
                if (partner.IsNull()) {
                    continue;
                }
                DeceasedPartnerEntry pEntry{};
                pEntry.isPlayer = partner.IsPlayer();
                pEntry.partnerRefId = partner.AsRefId();
                pEntry.partnerBaseId = resolveStorageBase(partner);
                pEntry.partnerName = meta.GetActorName(partner);
                pEntry.statusAtDeath = edge->status;

                std::uint32_t secretCount = 0;
                std::uint32_t publicCount = 0;
                auto tallyAffairs = [&](const std::vector<AffairRecord>& recs, ActorId expectedParamour) {
                    for (const auto& ar : recs) {
                        if (ar.paramour != expectedParamour) {
                            continue;
                        }
                        if (ar.isSecret &&
                            ar.affairStatus != AffairStatus::Allowed &&
                            ar.affairStatus != AffairStatus::EndedConfronted) {
                            ++secretCount;
                        } else {
                            ++publicCount;
                        }
                    }
                };
                tallyAffairs(affairs->GetByCheater(deceasedActor), partner);
                tallyAffairs(affairs->GetByCheater(partner), deceasedActor);
                const bool isSecretAffairPair = (secretCount > 0 && publicCount == 0);
                const bool isMaskEligible =
                    edge->status == RelationshipStatus::Lover ||
                    edge->status == RelationshipStatus::Dating ||
                    edge->status == RelationshipStatus::ExLover ||
                    edge->status == RelationshipStatus::ExDating ||
                    edge->status == RelationshipStatus::Candidate;
                if (isSecretAffairPair && isMaskEligible) {
                    pEntry.isSecretAffair = true;
                    for (auto it = edge->statusHistory.rbegin(); it != edge->statusHistory.rend(); ++it) {
                        if (it->toStatus == RelationshipStatus::Divorced ||
                            it->toStatus == RelationshipStatus::Jilted) {
                            pEntry.publicStatusAtDeath = it->toStatus;
                            break;
                        }
                    }
                }

                rec.partnersAtDeath.push_back(std::move(pEntry));
            }

            deceased.Write().UpsertRecord(std::move(rec));
            result.changed = true;
        }

        // If deceased was a graduated Stage 4 Adult Child, clean up their lineage & travel state
        if (existingChild != nullptr) {
            result.MarkActorDirty(existingChild->guardians.slotA);
            result.MarkActorDirty(existingChild->guardians.slotB);
            lineage.Write().EraseChild(maybeChildId);
            household.Write().EraseAdultChildTravel(maybeChildId);
            household.Write().EraseChildFactionSnapshot(maybeChildId);
            household.Write().EraseChildState(maybeChildId);
            household.Write().EvictTenant(maybeChildId.raw());
            result.MarkChildDirty(maybeChildId);
            result.changed = true;
        }

        // Reassign custody of any children belonging to the deceased adult BEFORE erasing spouse hierarchy/edges
        result.Merge(EvaluateCustodyOnDeath(deceasedActor, meta));

        // Close ongoing affairs in O(1) across all 3 participant roles (cheater, deceivedPartner, paramour)
        for (const ActorId affected : affairs.Write().CloseOngoingForDeceasedActor(deceasedActor)) {
            result.MarkActorDirty(affected);
            result.changed = true;
        }

        // Remove active/historical relationship edges & partner intents involving the deceased actor
        {
            const std::vector<CanonicalPairKey> pairsCopy = relationships->GetAdjacentPairs(deceasedActor);
            if (!pairsCopy.empty()) {
                auto& rels = relationships.Write();
                for (const CanonicalPairKey& pair : pairsCopy) {
                    result.MarkActorDirty(pair.Other(deceasedActor));
                    rels.EraseEdge(pair);
                }
                result.changed = true;
            }
            relationships.Write().ClearIntentsInvolving(deceasedActor);
        }

        // Reconcile SpouseHierarchy, Household, Affection, Cooldowns, and ActorProfile
        hierarchy.Write().OnSpouseRemoved(deceasedActor, GetPlayerSpousesInMarriageOrder());
        if (household.Write().EvictTenant(deceasedActor.raw())) {
            result.changed = true;
        }
        if (household.Write().StopSharingForSpouse(deceasedActor)) {
            result.changed = true;
        }
        affection.Write().EraseActor(deceasedActor);
        cooldowns.Write().EraseActor(deceasedActor);
        if (!deceasedRef.IsNull() && actors.Write().Erase(deceasedRef)) {
            result.changed = true;
        }

        result.MarkActorDirty(deceasedActor);
        return result;
    }

    WorldMutationResult WorldState::UnregisterActor(ActorId actor) {
        WorldMutationResult result{};
        if (actor.IsNull() || actor.IsPlayer()) {
            return result;
        }

        for (const ActorId affected : affairs.Write().EraseAllInvolvingActor(actor)) {
            result.MarkActorDirty(affected);
            result.changed = true;
        }

        const std::vector<CanonicalPairKey> pairsCopy = relationships->GetAdjacentPairs(actor);
        if (!pairsCopy.empty()) {
            auto& rels = relationships.Write();
            for (const CanonicalPairKey& pair : pairsCopy) {
                result.MarkActorDirty(pair.Other(actor));
                rels.EraseEdge(pair);
            }
            result.changed = true;
        }
        relationships.Write().ClearIntentsInvolving(actor);

        hierarchy.Write().OnSpouseRemoved(actor, GetPlayerSpousesInMarriageOrder());
        household.Write().EvictTenant(actor.raw());
        household.Write().StopSharingForSpouse(actor);
        affection.Write().EraseActor(actor);
        cooldowns.Write().EraseActor(actor);

        if (actor.HasPhysicalActor() && actors.Write().Erase(actor.AsRefId())) {
            result.changed = true;
        }

        result.MarkActorDirty(actor);
        return result;
    }

}  // namespace MARAS::State
