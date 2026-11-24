#include "maras/state/EngineSyncPlan.h"

#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <vector>

namespace MARAS::State {

    namespace {

        [[nodiscard]] bool CaseInsensitiveEq(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                const auto ca = static_cast<unsigned char>(a[i]);
                const auto cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool HasAnyClosedMainPair(const WorldStateSnapshot& snap, ActorId actor) noexcept {
            if (!snap.relationships || actor.IsNull()) {
                return false;
            }
            for (const auto& pairKey : snap.relationships->GetAdjacentPairs(actor)) {
                const auto* edge = snap.relationships->FindEdge(pairKey);
                if (edge && IsMainTierStatus(edge->status) && !edge->isOpen) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool IsPlayerRawId(ActorId id) noexcept {
            return id.IsPlayer() || id.raw() == Foundation::kPlayerBaseRawId;
        }

        [[nodiscard]] bool HasAnyPlayerParent(const ChildRecord& rec) noexcept {
            return IsPlayerRawId(rec.guardians.slotA) ||
                   IsPlayerRawId(rec.guardians.slotB) ||
                   IsPlayerRawId(rec.bioMother) ||
                   IsPlayerRawId(rec.bioFather);
        }

        [[nodiscard]] bool IsMarasPlayerFamilyInSnapshot(
            const WorldStateSnapshot& snap,
            ActorId actorId) noexcept {
            if (!snap.lineage || actorId.IsNull() || IsPlayerRawId(actorId)) {
                return false;
            }

            const auto* childRec = snap.lineage->FindChild(ChildId{ actorId.raw() });
            if (childRec != nullptr && HasAnyPlayerParent(*childRec)) {
                return true;
            }

            const ChildRecord* playerChildRec =
                snap.lineage->FindChild(ChildId{ Foundation::kPlayerRefRawId });
            if (!playerChildRec) {
                playerChildRec = snap.lineage->FindChild(ChildId{ Foundation::kPlayerBaseRawId });
            }
            if (!playerChildRec) {
                return false;
            }

            BaseId actorBaseId{};
            if (snap.actors) {
                if (const auto* prof = snap.actors->Find(RefId{ actorId.raw() })) {
                    actorBaseId = prof->baseId;
                }
            }

            const std::array<ActorId, 4> playerParents{
                playerChildRec->guardians.slotA,
                playerChildRec->guardians.slotB,
                playerChildRec->bioMother,
                playerChildRec->bioFather
            };
            for (const ActorId p : playerParents) {
                if (!p.IsNull() && !p.IsUnknownFather() &&
                    (p == actorId || (!actorBaseId.IsNull() && p.raw() == actorBaseId.raw()))) {
                    return true;
                }
            }

            if (childRec != nullptr) {
                const std::array<ActorId, 4> actorParents{
                    childRec->guardians.slotA,
                    childRec->guardians.slotB,
                    childRec->bioMother,
                    childRec->bioFather
                };
                for (const ActorId ap : actorParents) {
                    if (ap.IsNull() || ap.IsUnknownFather()) {
                        continue;
                    }
                    for (const ActorId pp : playerParents) {
                        if (!pp.IsNull() && !pp.IsUnknownFather() &&
                            (ap == pp || (IsPlayerRawId(ap) && IsPlayerRawId(pp)))) {
                            return true;
                        }
                    }
                }
            }

            return false;
        }

    }  // namespace

    DesiredGlobalsPlan ComputeDesiredGlobals(
        const WorldStateSnapshot& snap,
        bool skyrimNetDllLoaded) noexcept {
        DesiredGlobalsPlan plan{};
        if (!snap.relationships || !snap.household || !snap.settings) {
            return plan;
        }

        std::int32_t engagedCount = 0;
        std::int32_t marriedCount = 0;
        for (const auto& pairKey : snap.relationships->GetAdjacentPairs(ActorId::Player())) {
            const auto* edge = snap.relationships->FindEdge(pairKey);
            if (!edge) {
                continue;
            }
            if (edge->status == RelationshipStatus::Married) {
                ++marriedCount;
            } else if (edge->status == RelationshipStatus::Engaged) {
                ++engagedCount;
            }
        }

        plan.spousesCount = marriedCount;
        plan.loveInterestsCount = engagedCount + marriedCount;
        plan.playerHousesCount = static_cast<std::int32_t>(snap.household->AllHouses().size());
        plan.enablePolygamy = snap.settings->enablePlayerPolygamy;
        plan.hasSkyrimNet = skyrimNetDllLoaded && !snap.settings->simulateNoSkyrimNet;
        return plan;
    }

    PlayerFactionPlan ComputePlayerFactionPlan(const WorldStateSnapshot& snap) noexcept {
        PlayerFactionPlan plan{};
        if (!snap.relationships || !snap.settings) {
            return plan;
        }

        int spousesCount = 0;
        for (const auto& pairKey : snap.relationships->GetAdjacentPairs(ActorId::Player())) {
            const auto* edge = snap.relationships->FindEdge(pairKey);
            if (edge && edge->status == RelationshipStatus::Married) {
                ++spousesCount;
            }
        }

        plan.hasMarriedFaction = (!snap.settings->enablePlayerPolygamy && spousesCount > 0);
        const bool canFormNonCheating =
            snap.settings->enablePlayerPolygamy || !HasAnyClosedMainPair(snap, ActorId::Player());
        plan.canFormNonCheatingMainRank = canFormNonCheating ? std::int8_t{ 1 } : std::int8_t{ 0 };
        return plan;
    }

    AdultActorFactionPlan ComputeAdultActorFactionPlan(
        const WorldStateSnapshot& snap,
        ActorId actorId,
        std::string_view adultChildClassId,
        bool isBlockedPlayerFamily) {
        AdultActorFactionPlan plan{};
        if (!snap.actors || !snap.relationships || !snap.affairs || !snap.lineage ||
            !snap.hierarchy || !snap.household || !snap.affection || !snap.settings) {
            return plan;
        }
        if (actorId.IsNull() || actorId.IsPlayer() || actorId.IsUnknownFather() || actorId.IsSyntheticChild()) {
            return plan;
        }

        const ChildId asChildId{ actorId.raw() };
        const auto* childRec = snap.lineage->FindChild(asChildId);
        if (childRec != nullptr && childRec->stage != ChildStage::Adult) {
            // Stage 0-3 young children are handled by ChildFactionPlan, never AdultActorFactionPlan
            return plan;
        }

        const RefId refId{ actorId.raw() };
        const bool isRegisteredAdult = snap.actors->IsRegisteredAdult(refId);
        const auto* profile = snap.actors->Find(refId);
        const bool isGraduatedAdultChild = (childRec != nullptr && childRec->stage == ChildStage::Adult);
        const bool isTenant = !snap.household->GetTenantHouse(actorId.raw()).IsNull();
        const bool blockedPlayerFamily =
            isBlockedPlayerFamily || IsMarasPlayerFamilyInSnapshot(snap, actorId);

        if (!isRegisteredAdult && !isGraduatedAdultChild && !isTenant && !blockedPlayerFamily) {
            return plan;
        }

        plan.isRelevantAdult = true;
        plan.isRegisteredAdult = isRegisteredAdult;
        plan.isGraduatedAdultChild = isGraduatedAdultChild;
        plan.isHousedAdultTenant = isTenant;
        plan.isBlockedPlayerFamily = blockedPlayerFamily;
        if (blockedPlayerFamily) {
            plan.playerFamilyBlockedRank = std::int8_t{ 0 };
        }

        const RelationshipStatus playerStatus = snap.relationships->GetStatus(actorId, ActorId::Player());

        if (isRegisteredAdult) {
            if (profile != nullptr) {
                const auto sc = static_cast<int>(profile->socialClass);
                if (sc >= 0 && sc < static_cast<int>(SocialClass::_Count)) {
                    plan.socialClassRank = static_cast<std::int8_t>(sc);
                }
                const auto st = static_cast<int>(profile->skillType);
                if (st >= 0 && st < static_cast<int>(SkillType::_Count)) {
                    plan.skillTypeRank = static_cast<std::int8_t>(st);
                }
                const auto temp = static_cast<int>(profile->temperament);
                if (temp >= 0 && temp < static_cast<int>(Temperament::_Count)) {
                    plan.temperamentRank = static_cast<std::int8_t>(temp);
                }
            }

            plan.canFormNonCheatingMainRank =
                HasAnyClosedMainPair(snap, actorId) ? std::int8_t{ 0 } : std::int8_t{ 1 };

            if (!blockedPlayerFamily) {
                const RelationshipStatus trackedStatus =
                    (playerStatus != RelationshipStatus::Unknown) ? playerStatus : RelationshipStatus::Candidate;
                plan.trackedRank = static_cast<std::int8_t>(trackedStatus);

                if (playerStatus == RelationshipStatus::Married) {
                    const int rank = snap.hierarchy->GetRank(actorId);
                    if (rank >= 0 && rank <= 2) {
                        plan.hierarchyRank = static_cast<std::int8_t>(rank);
                    }

                    const auto childrenWithPlayer =
                        snap.lineage->GetChildrenWithGuardianSet(ActorId::Player(), actorId);
                    if (!childrenWithPlayer.empty()) {
                        const auto clamped = std::min<std::size_t>(childrenWithPlayer.size(), 127u);
                        plan.spouseChildrenRank = static_cast<std::int8_t>(clamped);
                    }
                }

                if (snap.affection->HasPermanent(actorId)) {
                    const int affVal = std::clamp(snap.affection->GetPermanent(actorId, 0), 0, 100);
                    plan.affectionRank = static_cast<std::int8_t>(affVal);
                }

                if (IsMainTierStatus(playerStatus)) {
                    bool hasOngoingKnown = false;
                    bool hasOngoingSecret = false;
                    for (const auto& rec :
                         snap.affairs->GetForCouplePair(CanonicalPairKey{ actorId, ActorId::Player() })) {
                        if (rec.cheater == actorId &&
                            rec.deceivedPartner.IsPlayer() &&
                            rec.affairStatus == AffairStatus::Ongoing) {
                            if (rec.isSecret) {
                                hasOngoingSecret = true;
                            } else {
                                hasOngoingKnown = true;
                            }
                        }
                    }
                    if (hasOngoingKnown) {
                        plan.spouseCheatingRank = std::int8_t{ 1 };
                    } else if (hasOngoingSecret) {
                        plan.spouseCheatingRank = std::int8_t{ 0 };
                    }

                    if (snap.relationships->IsPairOpen(actorId, ActorId::Player())) {
                        plan.spouseOpenRelationshipRank = std::int8_t{ 0 };
                    }
                }

                if (snap.household) {
                    for (const auto& [cellId, rec] : snap.household->AllSharedCells()) {
                        (void)cellId;
                        if (rec.sharedWithPlayer && rec.sharingSpouses.contains(actorId)) {
                            plan.spouseSharedHouseRank = std::int8_t{ 0 };
                            break;
                        }
                    }
                }
            }
        }

        if (isTenant) {
            plan.housedRank = std::int8_t{ 0 };
        }

        // Vanilla marriage & player-house membership convergence
        if (blockedPlayerFamily) {
            plan.marriedFactionOp = FactionMembershipOp::EnsureAbsent;
            plan.playerFactionOp = FactionMembershipOp::EnsureAbsent;
            plan.playerBedOwnershipOp = FactionMembershipOp::EnsureAbsent;
            plan.marriageAskedOp = FactionMembershipOp::EnsureAbsent;
            plan.courtingOp = FactionMembershipOp::EnsureAbsent;
            plan.marriagePotentialOp = FactionMembershipOp::EnsureAbsent;
            plan.forceStripMarriagePotential = true;
            plan.clearIgnoreProposeKeyword = false;
        } else {
            if (playerStatus == RelationshipStatus::Married) {
                plan.marriedFactionOp = FactionMembershipOp::EnsurePresent;
                plan.playerFactionOp = FactionMembershipOp::EnsurePresent;
                plan.playerBedOwnershipOp = FactionMembershipOp::EnsurePresent;
                plan.marriageAskedOp = FactionMembershipOp::EnsureAbsent;
                plan.courtingOp = FactionMembershipOp::EnsureAbsent;
                plan.potentialHirelingOp = FactionMembershipOp::EnsureAbsent;
            } else if (playerStatus == RelationshipStatus::Engaged) {
                plan.marriedFactionOp = FactionMembershipOp::EnsureAbsent;
                plan.playerFactionOp = FactionMembershipOp::EnsureAbsent;
                plan.playerBedOwnershipOp = FactionMembershipOp::EnsureAbsent;
                plan.marriageAskedOp = FactionMembershipOp::EnsurePresent;
                // Vanilla RelationshipMarriage (0x00074793) LoveInterest alias manages
                // MarriageCourtingFaction (0x0007431A) via ALFC. Forcing it early here
                // arms SMQN 0x0007431C (RelationshipMarriageBreakUp) during rank changes
                // and on secondary polygamist fiances waiting for the active wedding.
                plan.courtingOp = FactionMembershipOp::LeaveUntouched;
            } else {
                plan.marriedFactionOp = FactionMembershipOp::EnsureAbsent;
                plan.playerFactionOp = FactionMembershipOp::EnsureAbsent;
                plan.playerBedOwnershipOp = FactionMembershipOp::EnsureAbsent;
                plan.marriageAskedOp = FactionMembershipOp::EnsureAbsent;
                plan.courtingOp = FactionMembershipOp::EnsureAbsent;
            }

            if (isRegisteredAdult) {
                bool marriedToNpc = false;
                bool hasDivorcedOrCandidateNpcEdge = false;
                for (const auto& pairKey : snap.relationships->GetAdjacentPairs(actorId)) {
                    const auto* edge = snap.relationships->FindEdge(pairKey);
                    if (!edge) {
                        continue;
                    }
                    const ActorId other = pairKey.Other(actorId);
                    if (!other.IsPlayer()) {
                        if (edge->status == RelationshipStatus::Married) {
                            marriedToNpc = true;
                        }
                        if (edge->status == RelationshipStatus::Divorced ||
                            edge->status == RelationshipStatus::Candidate) {
                            hasDivorcedOrCandidateNpcEdge = true;
                        }
                    }
                }

                if (marriedToNpc && !snap.settings->enablePlayerPolygamy) {
                    plan.marriagePotentialOp = FactionMembershipOp::EnsureAbsent;
                } else if (playerStatus != RelationshipStatus::Unknown ||
                           hasDivorcedOrCandidateNpcEdge) {
                    plan.marriagePotentialOp = FactionMembershipOp::EnsurePresent;
                }

                plan.clearIgnoreProposeKeyword =
                    (playerStatus == RelationshipStatus::Engaged ||
                     playerStatus == RelationshipStatus::Married ||
                     playerStatus == RelationshipStatus::Divorced ||
                     playerStatus == RelationshipStatus::Jilted);
            }
        }

        if (isGraduatedAdultChild) {
            plan.clearYoungChildFactions = true;
            plan.ensurePotentialFollower = true;
            plan.ensurePlayerWerewolf = childRec->hasBeastblood;
            plan.ensureCompanionsCircle =
                childRec->hasBeastblood && CaseInsensitiveEq(adultChildClassId, "pack_berserker");
            plan.ensureFrostmoonWerewolves =
                childRec->hasBeastblood && CaseInsensitiveEq(adultChildClassId, "pack_hunter");
        }

        return plan;
    }

    ChildFactionPlan ComputeChildFactionPlan(
        const WorldStateSnapshot& snap,
        ChildId childId,
        ActorSex otherSpouseParentSex,
        RefId resolvedFollowTargetId,
        RefId honorhallOrphanageMarkerId) {
        ChildFactionPlan plan{};
        if (!snap.lineage || !snap.household || !snap.relationships) {
            return plan;
        }
        if (childId.IsNull() || childId.IsSynthetic()) {
            return plan;
        }

        const auto* rec = snap.lineage->FindChild(childId);
        if (!rec || rec->stage == ChildStage::Adult) {
            return plan;
        }

        const bool isPhysicalChildOrInfant =
            (rec->stage == ChildStage::Child || rec->stage == ChildStage::Infant);
        plan.isYoungChild = true;
        plan.isPhysicalInfant = (rec->stage == ChildStage::Infant);

        const auto* hhRec = snap.household->FindChildState(childId);
        const bool inPlayerHousehold =
            rec->guardians.HasGuardian(ActorId::Player()) &&
            (hhRec != nullptr || isPhysicalChildOrInfant);
        plan.inPlayerHousehold = inPlayerHousehold;

        if (inPlayerHousehold) {
            plan.restoreSavedCrimeFaction = true;
            plan.playerChildRank = std::int8_t{ 0 };
            plan.byohChildOwnedRank = std::int8_t{ 0 };
            plan.byohAdoptionRank = std::int8_t{ 0 };
            plan.playerFactionRank = std::int8_t{ 0 };
            plan.adoptableOp = FactionMembershipOp::EnsurePresent;
            plan.adoptableRank = std::int8_t{ 25 };
        } else if (childId.IsDynamicRuntimeActor() && isPhysicalChildOrInfant) {
            if (!honorhallOrphanageMarkerId.IsNull() && resolvedFollowTargetId == honorhallOrphanageMarkerId) {
                plan.adoptableOp = FactionMembershipOp::EnsurePresent;
                plan.adoptableRank = std::int8_t{ 0 };
            } else {
                plan.adoptableOp = FactionMembershipOp::EnsureAbsent;
            }
        }

        if (rec->guardians.HasGuardian(ActorId::Player())) {
            const ActorId otherParent = rec->guardians.OtherGuardian(ActorId::Player());
            if (!otherParent.IsNull() &&
                snap.relationships->GetStatus(otherParent, ActorId::Player()) == RelationshipStatus::Married) {
                plan.bothParentsRank =
                    (otherSpouseParentSex == ActorSex::Male) ? std::int8_t{ 2 } : std::int8_t{ 1 };
            }
        }

        return plan;
    }

    CompanionFactionPlan ComputeCompanionFactionPlan(
        const WorldStateSnapshot& snap,
        RefId companionId) {
        CompanionFactionPlan plan{};
        if (!snap.household || companionId.IsNull() || companionId.IsPlayer() ||
            Foundation::IsSyntheticChildRawId(companionId.raw())) {
            return plan;
        }

        ChildId bestChild{};
        for (const auto& [cid, st] : snap.household->AllChildStates()) {
            if (cid.IsNull() || cid.IsSynthetic()) {
                continue;
            }
            if (st.petId == companionId || st.critterId == companionId) {
                if (bestChild.IsNull() || cid.raw() < bestChild.raw()) {
                    bestChild = cid;
                }
            }
        }

        if (!bestChild.IsNull()) {
            plan.isActiveCompanion = true;
            plan.ownerChildId = bestChild;
            plan.playerChildPetRank = std::int8_t{ 0 };
            plan.playerFactionRank = std::int8_t{ 0 };
        }
        return plan;
    }

    ActorPackagePlan ComputeActorPackagePlan(
        const WorldStateSnapshot& snap,
        RefId actorId,
        RefId resolvedChildFollowTargetId) {
        ActorPackagePlan plan{};
        if (!snap.actors || !snap.relationships || !snap.lineage || !snap.household || !snap.deceased) {
            return plan;
        }
        if (actorId.IsNull() || actorId.IsPlayer() || Foundation::IsSyntheticChildRawId(actorId.raw())) {
            return plan;
        }
        if (snap.deceased->IsDeceased(ActorId{ actorId.raw() })) {
            return plan;
        }

        const ChildId childId{ actorId.raw() };
        const auto* childRec = snap.lineage->FindChild(childId);
        const auto* hhChildRec = snap.household->FindChildState(childId);

        // 1. Precedence Tier 1: Household tenant (adult spouse/member or housed Stage 1/2 child)
        const auto& tenants = snap.household->AllTenants();
        if (const auto tenantIt = tenants.find(actorId.raw()); tenantIt != tenants.end()) {
            plan.isTenant = true;
            if (childRec != nullptr &&
                (childRec->stage == ChildStage::Child || childRec->stage == ChildStage::Infant)) {
                plan.homeSandboxTargetId = hhChildRec ? hhChildRec->homeMarkerId : RefId{};
            } else {
                const auto* profile = snap.actors->Find(actorId);
                if (profile != nullptr && profile->legacyHomeMarker.has_value() && !profile->legacyHomeMarker->IsNull()) {
                    plan.homeSandboxTargetId = *profile->legacyHomeMarker;
                } else {
                    plan.needsAdultHomeMarker = true;
                }
            }
            plan.role = tenantIt->second.stayAtHome ? PackageRole::HousedTenant : PackageRole::None;
            return plan;
        }

        // 2. Precedence Tier 2: Active child pet or critter
        const auto compPlan = ComputeCompanionFactionPlan(snap, actorId);
        if (compPlan.isActiveCompanion) {
            plan.role = PackageRole::PetOrCritter;
            plan.homeSandboxTargetId = RefId{ compPlan.ownerChildId.raw() };
            return plan;
        }

        // 3. Precedence Tier 3: Unhoused Stage 1/2 player child or dynamic 0xFF non-player child
        if (childRec != nullptr &&
            (childRec->stage == ChildStage::Child || childRec->stage == ChildStage::Infant)) {
            const bool hasPlayerGuardian = childRec->guardians.HasGuardian(ActorId::Player());
            const bool isUnhousedPlayerChild =
                hasPlayerGuardian && (hhChildRec == nullptr || hhChildRec->currentHouseId.IsNull());
            const bool isDynamicNonPlayerChild =
                !hasPlayerGuardian && childId.IsDynamicRuntimeActor();
            if (isUnhousedPlayerChild || isDynamicNonPlayerChild) {
                if (!resolvedChildFollowTargetId.IsNull()) {
                    plan.role = PackageRole::FollowParentChild;
                    plan.childFollowTargetId = resolvedChildFollowTargetId;
                }
                return plan;
            }
        }

        // 4. Precedence Tier 4: Traveling Stage 4 adult child (unless married to Player)
        if (childRec != nullptr && childRec->stage == ChildStage::Adult) {
            const ActorId actorAsId{ actorId.raw() };
            if (snap.relationships->GetStatus(actorAsId, ActorId::Player()) != RelationshipStatus::Married) {
                if (const auto* travel = snap.household->FindAdultChildTravel(childId);
                    travel != nullptr && !travel->targetMarkerId.IsNull()) {
                    plan.role = PackageRole::AdultChildTravel;
                    plan.childFollowTargetId = travel->targetMarkerId;
                    return plan;
                }
            }
        }

        // 5. Precedence Tier 5: No MARAS package override or LinkedRefs
        return plan;
    }

    BuffMultipliersPlan ComputeBuffMultipliers(
        const WorldStateSnapshot& snap,
        std::span<const RefId> activeTeammates) {
        BuffMultipliersPlan plan{};
        if (!snap.actors || !snap.relationships || !snap.hierarchy || !snap.affection || !snap.deceased) {
            return plan;
        }

        std::vector<ActorId> livingSpouses;
        for (const auto& pairKey : snap.relationships->GetAdjacentPairs(ActorId::Player())) {
            const auto* edge = snap.relationships->FindEdge(pairKey);
            if (!edge || edge->status != RelationshipStatus::Married) {
                continue;
            }
            const ActorId spouseId = pairKey.Other(ActorId::Player());
            if (spouseId.IsNull() || spouseId.IsPlayer()) {
                continue;
            }
            const auto* prof = snap.actors->Find(RefId{ spouseId.raw() });
            if (!prof) {
                continue;
            }
            if (snap.deceased->IsDeceased(spouseId, prof->baseId)) {
                continue;
            }
            livingSpouses.push_back(spouseId);
        }

        std::sort(livingSpouses.begin(), livingSpouses.end(), [](ActorId a, ActorId b) {
            return a.raw() < b.raw();
        });

        const int spouseCount = static_cast<int>(livingSpouses.size());
        plan.livingMarriedSpouseCount = spouseCount;

        for (const ActorId spouseId : livingSpouses) {
            const auto* prof = snap.actors->Find(RefId{ spouseId.raw() });
            if (!prof) {
                continue;
            }
            const auto socialIdx = static_cast<std::size_t>(prof->socialClass);
            if (socialIdx >= BuffMultipliersPlan::kSocialClassCount) {
                continue;
            }
            const int permAff = snap.affection->GetPermanent(spouseId, 0);
            const float affMult = ComputeAffectionMultiplier(permAff);
            const int rank = snap.hierarchy->GetRank(spouseId);
            const float rankMult = ComputeHierarchyRankMultiplier(rank, spouseCount);
            plan.permanentSocialClassMultipliers[socialIdx] += affMult * rankMult;
        }

        for (const RefId teammateRef : activeTeammates) {
            if (teammateRef.IsNull() || teammateRef.IsPlayer()) {
                continue;
            }
            const ActorId actorId{ teammateRef.raw() };
            const auto* prof = snap.actors->Find(teammateRef);
            if (!prof) {
                continue;
            }
            if (snap.deceased->IsDeceased(actorId, prof->baseId)) {
                continue;
            }
            if (snap.relationships->GetStatus(actorId, ActorId::Player()) != RelationshipStatus::Married) {
                continue;
            }
            const auto skillIdx = static_cast<std::size_t>(prof->skillType);
            if (skillIdx >= BuffMultipliersPlan::kSkillTypeCount) {
                continue;
            }
            const int permAff = snap.affection->GetPermanent(actorId, 0);
            plan.followerSkillTypeMultipliers[skillIdx] += ComputeAffectionMultiplier(permAff);
        }

        return plan;
    }

    BonusTargetValue ComputeBonusTargetValue(
        std::string_view bonusType,
        float baseValue,
        float multiplier) noexcept {
        BonusTargetValue out{};
        if (multiplier == 0.0f) {
            out.isActive = false;
            out.rawScaledValue = 0.0f;
            out.engineTargetValue = (bonusType == "multiply") ? 1.0f : 0.0f;
            return out;
        }

        if (bonusType == "multiply") {
            out.rawScaledValue = (baseValue / 100.0f) * multiplier;
            out.engineTargetValue = 1.0f + out.rawScaledValue;
            out.isActive = (out.rawScaledValue != 0.0f);
            return out;
        }

        if (CaseInsensitiveEq(bonusType, "spell")) {
            out.rawScaledValue = std::abs(baseValue) * multiplier;
            out.engineTargetValue = out.rawScaledValue;
            out.isActive = (out.rawScaledValue != 0.0f);
            return out;
        }

        // Non-spell ("add") preserves signed scaling
        out.rawScaledValue = baseValue * multiplier;
        out.engineTargetValue = out.rawScaledValue;
        out.isActive = (out.rawScaledValue != 0.0f);
        return out;
    }

    ExternalFactionWriteAction EvaluateExternalFactionWrite(
        const WorldStateSnapshot& snap,
        ActorId actorId,
        std::uint32_t factionFormId,
        std::int8_t targetRank,
        bool isBlockedEngineFamily) noexcept {
        if (actorId.IsNull() || actorId.IsSyntheticChild() || actorId.IsUnknownFather()) {
            return ExternalFactionWriteAction::Passthrough;
        }

        const bool isRemoval = (targetRank < 0);

        if (IsPlayerRawId(actorId)) {
            if (factionFormId == kVanillaPlayerMarriedFactionRawId &&
                !isRemoval &&
                snap.settings &&
                snap.settings->enablePlayerPolygamy) {
                return ExternalFactionWriteAction::BlockPolygamyPlayerMarried;
            }
            return ExternalFactionWriteAction::Passthrough;
        }

        if ((factionFormId == kVanillaPotentialMarriageFactionRawId ||
             factionFormId == kVanillaMarriageAskedFactionRawId ||
             factionFormId == kVanillaCourtingFactionRawId) &&
            !isRemoval) {
            if (isBlockedEngineFamily || IsMarasPlayerFamilyInSnapshot(snap, actorId)) {
                return ExternalFactionWriteAction::BlockPlayerFamilyCourtship;
            }
            return ExternalFactionWriteAction::Passthrough;
        }

        if (factionFormId == kVanillaPlayerMarriedFactionRawId && snap.relationships) {
            const auto currentStatus = snap.relationships->GetStatus(actorId, ActorId::Player());
            if (!isRemoval) {
                return (currentStatus != RelationshipStatus::Married)
                           ? ExternalFactionWriteAction::PromoteNpcToPlayerMarried
                           : ExternalFactionWriteAction::Passthrough;
            }
            return (currentStatus == RelationshipStatus::Married)
                       ? ExternalFactionWriteAction::DemoteNpcToPlayerDivorced
                       : ExternalFactionWriteAction::Passthrough;
        }

        return ExternalFactionWriteAction::Passthrough;
    }

}  // namespace MARAS::State
