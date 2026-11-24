#include "maras/domain/relationships/RelationshipRules.h"

#include "maras/domain/relationships/RelationshipSettlementRules.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/Logging.h"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_set>

namespace MARAS::Domain::Relationships {

    namespace {

        [[nodiscard]] std::string ResolveDisplayName(
            const State::IActorMetadataProvider& meta,
            ActorId actor) {
            if (actor.IsPlayer()) {
                return "Player";
            }
            std::string name = meta.GetActorName(actor);
            if (!name.empty() && name != "None") {
                return name;
            }
            return std::format("0x{:08X}", actor.raw());
        }

        void CollectNormalizedParentIds(
            const State::WorldStateSnapshot& snap,
            const State::IActorMetadataProvider& meta,
            ActorId actorId,
            std::unordered_set<Foundation::FormRawId>& out) {
            if (actorId.IsNull() || actorId.IsUnknownFather()) {
                return;
            }
            out.insert(Foundation::FormRawId{ actorId.raw() });
            if (actorId.IsPlayer() || actorId.raw() == Foundation::kPlayerBaseRawId) {
                out.insert(Foundation::FormRawId{ Foundation::kPlayerRefRawId });
                out.insert(Foundation::FormRawId{ Foundation::kPlayerBaseRawId });
                return;
            }
            if (snap.actors) {
                if (const auto* profile = snap.actors->Find(Foundation::RefId{ actorId.raw() })) {
                    if (!profile->refId.IsNull()) {
                        out.insert(Foundation::FormRawId{ profile->refId.raw() });
                    }
                    if (!profile->baseId.IsNull()) {
                        out.insert(Foundation::FormRawId{ profile->baseId.raw() });
                    }
                }
            }
            if (const auto base = meta.GetActorBaseId(actorId); !base.IsNull()) {
                out.insert(Foundation::FormRawId{ base.raw() });
            }
        }

        [[nodiscard]] std::unordered_set<Foundation::FormRawId> CollectChildParentIds(
            const State::WorldStateSnapshot& snap,
            const State::IActorMetadataProvider& meta,
            const State::ChildRecord& rec) {
            std::unordered_set<Foundation::FormRawId> ids;
            const std::array<ActorId, 4> rawParents{
                rec.guardians.slotA,
                rec.guardians.slotB,
                rec.bioMother,
                rec.bioFather
            };
            for (const ActorId parent : rawParents) {
                CollectNormalizedParentIds(snap, meta, parent, ids);
            }
            return ids;
        }

        [[nodiscard]] const State::ChildRecord* FindChildEntry(
            const State::WorldStateSnapshot& snap,
            ActorId actor) noexcept {
            if (!snap.lineage || actor.IsNull()) {
                return nullptr;
            }
            return snap.lineage->FindChild(Foundation::ChildId{ actor.raw() });
        }

    }  // namespace

    bool IsPlayerActiveMainPartner(const State::WorldStateSnapshot& snap, ActorId actor) noexcept {
        if (!snap.relationships || actor.IsNull() || actor.IsPlayer()) {
            return false;
        }
        const auto status = snap.relationships->GetStatus(ActorId::Player(), actor);
        return Foundation::IsMainTierStatus(status);
    }

    bool CanOpenRelationship(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept {
        if (!snap.relationships || actorA.IsNull() || actorB.IsNull()) {
            return false;
        }
        const auto* edge = snap.relationships->FindEdge(actorA, actorB);
        return edge != nullptr && Foundation::IsMainTierStatus(edge->status) && !edge->isOpen;
    }

    bool CanCloseRelationship(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept {
        if (!snap.relationships || actorA.IsNull() || actorB.IsNull()) {
            return false;
        }
        const auto* edge = snap.relationships->FindEdge(actorA, actorB);
        return edge != nullptr && Foundation::IsMainTierStatus(edge->status) && edge->isOpen;
    }

    bool AreMarasFamilyMembers(
        const State::WorldStateSnapshot& snap,
        const State::IActorMetadataProvider& meta,
        ActorId actorA,
        ActorId actorB,
        std::string* outReason) {
        if (outReason) {
            outReason->clear();
        }
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB || !snap.lineage) {
            return false;
        }

        const auto checkParentChild = [&](ActorId childCandidate, ActorId parentCandidate) -> bool {
            const auto* entry = FindChildEntry(snap, childCandidate);
            if (!entry) {
                return false;
            }
            const auto entryParents = CollectChildParentIds(snap, meta, *entry);
            std::unordered_set<Foundation::FormRawId> candidateIds;
            CollectNormalizedParentIds(snap, meta, parentCandidate, candidateIds);
            for (const auto id : candidateIds) {
                if (entryParents.contains(id)) {
                    return true;
                }
            }
            return false;
        };

        if (checkParentChild(actorA, actorB) || checkParentChild(actorB, actorA)) {
            if (outReason) {
                *outReason = std::format("{} and {} have a parent/child relationship.",
                                         ResolveDisplayName(meta, actorA),
                                         ResolveDisplayName(meta, actorB));
            }
            return true;
        }

        const auto* entryA = FindChildEntry(snap, actorA);
        const auto* entryB = FindChildEntry(snap, actorB);
        if (entryA != nullptr && entryB != nullptr) {
            const auto parentsA = CollectChildParentIds(snap, meta, *entryA);
            const auto parentsB = CollectChildParentIds(snap, meta, *entryB);
            for (const auto pId : parentsB) {
                if (parentsA.contains(pId)) {
                    if (outReason) {
                        *outReason = std::format("{} and {} are siblings (brother/sister).",
                                                 ResolveDisplayName(meta, actorA),
                                                 ResolveDisplayName(meta, actorB));
                    }
                    return true;
                }
            }
        }

        return false;
    }

    bool CanFormRelationship(
        const State::WorldStateSnapshot& snap,
        const State::IActorMetadataProvider& meta,
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus targetStatus,
        const RelationshipCheckContext& ctx,
        std::string* outReason) {
        if (outReason) {
            outReason->clear();
        }

        if (actorA == actorB && !actorA.IsNull()) {
            if (outReason) {
                *outReason = "An actor cannot form a relationship with themselves.";
            }
            return false;
        }

        if (ctx.isChildA || ctx.isChildB) {
            if (outReason) {
                *outReason = "Children cannot enter adult relationships.";
            }
            return false;
        }

        const auto* existingDirectEdge = snap.relationships ? snap.relationships->FindEdge(actorA, actorB) : nullptr;
        const bool isDemotingActiveToCandidate =
            targetStatus == RelationshipStatus::Candidate &&
            existingDirectEdge != nullptr &&
            Foundation::IsActivePartnerStatus(existingDirectEdge->status);
        if (Foundation::IsActivePartnerStatus(targetStatus) ||
            (targetStatus == RelationshipStatus::Candidate && !isDemotingActiveToCandidate)) {
            if (AreMarasFamilyMembers(snap, meta, actorA, actorB, outReason)) {
                return false;
            }
            if (ctx.areEngineFamily) {
                if (outReason) {
                    *outReason = !ctx.engineFamilyReason.empty()
                        ? ctx.engineFamilyReason
                        : std::format("{} and {} are related family members.",
                                      ResolveDisplayName(meta, actorA),
                                      ResolveDisplayName(meta, actorB));
                }
                return false;
            }
        }

        if (!snap.relationships) {
            return false;
        }

        const std::string nameA = ResolveDisplayName(meta, actorA);
        const std::string nameB = ResolveDisplayName(meta, actorB);
        const auto* directEdge = snap.relationships->FindEdge(actorA, actorB);

        if (directEdge) {
            const RelationshipStatus directStatus = directEdge->status;
            if (targetStatus == RelationshipStatus::Lover) {
                if (directStatus == RelationshipStatus::Lover) {
                    if (outReason) {
                        *outReason = std::format("{} and {} are already lovers.", nameA, nameB);
                    }
                    return false;
                }
                if (Foundation::IsMainTierStatus(directStatus)) {
                    if (outReason) {
                        *outReason = std::format("{} and {} already have an active relationship ({}).",
                                                 nameA, nameB, Foundation::RelationshipStatusToString(directStatus));
                    }
                    return false;
                }
            }
            if (targetStatus == RelationshipStatus::Dating && Foundation::IsMainTierStatus(directStatus)) {
                if (outReason) {
                    *outReason = std::format("{} and {} already have an active relationship ({}).",
                                             nameA, nameB, Foundation::RelationshipStatusToString(directStatus));
                }
                return false;
            }
            if (targetStatus == RelationshipStatus::Engaged &&
                (directStatus == RelationshipStatus::Engaged || directStatus == RelationshipStatus::Married)) {
                if (outReason) {
                    *outReason = std::format("{} and {} are already engaged or married.", nameA, nameB);
                }
                return false;
            }
            if (targetStatus == RelationshipStatus::Married && directStatus == RelationshipStatus::Married) {
                if (outReason) {
                    *outReason = std::format("{} and {} are already married.", nameA, nameB);
                }
                return false;
            }
        }

        if (targetStatus == RelationshipStatus::Lover) {
            return true;
        }
        if (targetStatus == RelationshipStatus::Divorced) {
            if (!directEdge || directEdge->status != RelationshipStatus::Married) {
                if (outReason) {
                    *outReason = "Can only divorce if currently Married.";
                }
                return false;
            }
            return true;
        }
        if (targetStatus == RelationshipStatus::Jilted) {
            if (!directEdge || directEdge->status != RelationshipStatus::Engaged) {
                if (outReason) {
                    *outReason = "Can only be jilted if currently Engaged.";
                }
                return false;
            }
            return true;
        }
        if (targetStatus == RelationshipStatus::ExDating) {
            if (!directEdge || directEdge->status != RelationshipStatus::Dating) {
                if (outReason) {
                    *outReason = "Can only transition to Ex-Dating if currently Dating.";
                }
                return false;
            }
            return true;
        }
        if (targetStatus == RelationshipStatus::ExLover) {
            if (!directEdge || directEdge->status != RelationshipStatus::Lover) {
                if (outReason) {
                    *outReason = "Can only transition to Ex-Lover if currently Lovers.";
                }
                return false;
            }
            return true;
        }

        if (!Foundation::IsMainTierStatus(targetStatus)) {
            return true;
        }

        const bool polygamyEnabled = snap.settings && snap.settings->enablePlayerPolygamy;

        const auto validateActorWithOtherPartners =
            [&](ActorId self, ActorId other, const std::string& selfName) -> bool {
            if (self.IsPlayer() && polygamyEnabled) {
                return true;
            }
            for (const auto& key : snap.relationships->GetAdjacentPairs(self)) {
                if (key.Contains(other)) {
                    continue;
                }
                const auto* edge = snap.relationships->FindEdge(key);
                if (!edge) {
                    continue;
                }
                const RelationshipStatus edgeStatus = edge->status;
                const ActorId existingPartner = key.Other(self);
                const std::string partnerName = ResolveDisplayName(meta, existingPartner);

                if (targetStatus == RelationshipStatus::Dating || targetStatus == RelationshipStatus::Married) {
                    if (Foundation::IsMainTierStatus(edgeStatus)) {
                        if (outReason) {
                            *outReason = self.IsPlayer()
                                ? "Cannot enter another main relationship: Polygamy is disabled in settings."
                                : std::format("{} already has a committed main partner ({}).", selfName, partnerName);
                        }
                        return false;
                    }
                } else if (targetStatus == RelationshipStatus::Engaged) {
                    if (edgeStatus == RelationshipStatus::Engaged || edgeStatus == RelationshipStatus::Married) {
                        if (outReason) {
                            *outReason = self.IsPlayer()
                                ? "Cannot enter another engagement/marriage: Polygamy is disabled in settings."
                                : std::format("{} is already engaged or married to {}.", selfName, partnerName);
                        }
                        return false;
                    }
                    if (edgeStatus == RelationshipStatus::Dating) {
                        if (outReason) {
                            *outReason = self.IsPlayer()
                                ? "Cannot propose engagement to this actor while dating someone else."
                                : std::format("{} is dating {} and can only get engaged to their dating partner.",
                                              selfName, partnerName);
                        }
                        return false;
                    }
                }
            }
            return true;
        };

        return validateActorWithOtherPartners(actorA, actorB, nameA) &&
               validateActorWithOtherPartners(actorB, actorA, nameB);
    }

    bool IsAvailableForStatus(
        const State::WorldStateSnapshot& snap,
        ActorId actor,
        RelationshipStatus status) noexcept {
        if (!snap.relationships || actor.IsNull()) {
            return false;
        }
        const bool polygamyEnabled = snap.settings && snap.settings->enablePlayerPolygamy;
        const auto hasAnyEdgeMatching = [&](auto&& pred) noexcept {
            for (const auto& key : snap.relationships->GetAdjacentPairs(actor)) {
                if (const auto* edge = snap.relationships->FindEdge(key)) {
                    if (pred(edge->status, edge->isOpen)) {
                        return true;
                    }
                }
            }
            return false;
        };

        switch (status) {
            case RelationshipStatus::Dating:
                if (actor.IsPlayer() && polygamyEnabled) {
                    return true;
                }
                return !hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return Foundation::IsMainTierStatus(st);
                });
            case RelationshipStatus::Engaged:
                if (actor.IsPlayer() && polygamyEnabled) {
                    return true;
                }
                return !hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Engaged || st == RelationshipStatus::Married;
                });
            case RelationshipStatus::Married:
                if (actor.IsPlayer() && polygamyEnabled) {
                    return true;
                }
                return !hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Married;
                });
            case RelationshipStatus::Lover:
            case RelationshipStatus::Candidate:
                return true;
            case RelationshipStatus::Divorced:
                return hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Married;
                });
            case RelationshipStatus::Jilted:
                return hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Engaged;
                });
            case RelationshipStatus::ExDating:
                return hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Dating;
                });
            case RelationshipStatus::ExLover:
                return hasAnyEdgeMatching([](RelationshipStatus st, bool) {
                    return st == RelationshipStatus::Lover;
                });
            default:
                return true;
        }
    }

    RelationshipTransitionOutcome ApplyRelationshipTransition(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus targetStatus,
        float currentGameDay,
        ActorId initiator,
        const State::IActorMetadataProvider& meta,
        const RelationshipCheckContext& ctx,
        Foundation::IRng* optionalRng) {
        RelationshipTransitionOutcome outcome;
        outcome.newStatus = targetStatus;

        const auto snap = ws.Snapshot();
        const auto* preEdge = snap.relationships ? snap.relationships->FindEdge(actorA, actorB) : nullptr;
        const bool hadPriorEdge = (preEdge != nullptr);
        const RelationshipStatus preStatus = preEdge ? preEdge->status : RelationshipStatus::Candidate;
        const bool wasOpenBefore = (preEdge != nullptr && preEdge->isOpen);
        outcome.oldStatus = preStatus;

        if (preEdge != nullptr && preStatus == targetStatus) {
            outcome.skippedDuplicate = true;
            return outcome;
        }

        if (!CanFormRelationship(snap, meta, actorA, actorB, targetStatus, ctx, &outcome.blockReason)) {
            return outcome;
        }

        const bool involvesPlayer = actorA.IsPlayer() || actorB.IsPlayer();
        const ActorId partnerId = actorA.IsPlayer() ? actorB : (actorB.IsPlayer() ? actorA : ActorId{});

        bool wasCheatingOnPlayerBeforeTransition = ctx.partnerCheatedOnPlayer;
        if (!wasCheatingOnPlayerBeforeTransition && involvesPlayer && !partnerId.IsNull() && !partnerId.IsPlayer() &&
            snap.affairs != nullptr) {
            for (const auto& rec : snap.affairs->GetForCouplePair(CanonicalPairKey{ partnerId, ActorId::Player() })) {
                if (rec.cheater == partnerId &&
                    rec.deceivedPartner.IsPlayer() &&
                    rec.affairStatus == Foundation::AffairStatus::Ongoing) {
                    wasCheatingOnPlayerBeforeTransition = true;
                    break;
                }
            }
        }

        const bool openMainExists =
            (preStatus != targetStatus && Foundation::IsMainTierStatus(targetStatus)) &&
            (ws.relationships.Read().HasAnyOpenMainPair(actorA) ||
             ws.relationships.Read().HasAnyOpenMainPair(actorB));

        outcome.mutations = ws.TransitionRelationship(
            actorA,
            actorB,
            targetStatus,
            currentGameDay,
            initiator,
            meta,
            optionalRng);

        outcome.allowed = true;
        outcome.recordedTransition = (!hadPriorEdge && targetStatus == RelationshipStatus::Candidate) ||
                                     (preStatus != targetStatus);

        if (outcome.recordedTransition) {
            if (const auto* postEdge = ws.relationships.Read().FindEdge(actorA, actorB)) {
                if (Foundation::IsMainTierStatus(targetStatus) && openMainExists && !postEdge->isOpen) {
                    outcome.mutations.Merge(ws.SetPairOpen(actorA, actorB, true, currentGameDay));
                    outcome.appliedAutoOpen = true;
                } else if (IsBreakupStatusIncludingCandidate(targetStatus) && (wasOpenBefore || postEdge->isOpen)) {
                    if (postEdge->isOpen) {
                        ws.relationships.Write().GetOrCreateEdge(actorA, actorB).isOpen = false;
                        outcome.mutations.changed = true;
                        outcome.mutations.MarkActorDirty(actorA);
                        outcome.mutations.MarkActorDirty(actorB);
                    }
                    outcome.resetOpenOnBreakup = true;
                }
            }
        }

        if (involvesPlayer && !partnerId.IsNull() && !partnerId.IsPlayer() && outcome.recordedTransition) {
            if (Foundation::IsActivePartnerStatus(targetStatus)) {
                ws.affection.Write().SetLastInteractionDay(partnerId, currentGameDay);
            }

            std::optional<int> targetAffection;
            switch (targetStatus) {
                case RelationshipStatus::Married:
                case RelationshipStatus::Engaged:
                    targetAffection = 100;
                    break;
                case RelationshipStatus::Dating:
                    if (ws.affection.Read().GetPermanent(partnerId, 0) < 80) {
                        targetAffection = 80;
                    }
                    break;
                case RelationshipStatus::Lover:
                    if (ws.affection.Read().GetPermanent(partnerId, 0) < 70) {
                        targetAffection = 70;
                    }
                    break;
                case RelationshipStatus::Divorced:
                case RelationshipStatus::Jilted:
                case RelationshipStatus::ExDating:
                case RelationshipStatus::ExLover:
                    targetAffection = 0;
                    break;
                case RelationshipStatus::Candidate:
                    targetAffection = !hadPriorEdge
                        ? ComputeInitialCandidateAffection(ctx.playerRelationshipRank)
                        : 50;
                    break;
                default:
                    break;
            }
            if (targetAffection.has_value()) {
                ws.affection.Write().SetPermanent(partnerId, *targetAffection);
                outcome.offlineSyncedAffection = *targetAffection;
                outcome.mutations.MarkActorDirty(partnerId);
            }

            const bool shouldApplyBreakupCooldown =
                targetStatus == RelationshipStatus::Divorced ||
                targetStatus == RelationshipStatus::Jilted ||
                ((targetStatus == RelationshipStatus::ExDating || targetStatus == RelationshipStatus::ExLover) &&
                 wasCheatingOnPlayerBeforeTransition);
            if (shouldApplyBreakupCooldown) {
                ws.cooldowns.Write().SetBrokeupTime(partnerId, currentGameDay);
                outcome.appliedBreakupCooldown = true;
            }

            if (preStatus == RelationshipStatus::Married || targetStatus == RelationshipStatus::Married) {
                outcome.reconcilePlayerBuffs = true;
            }
        }

        if (outcome.recordedTransition &&
            targetStatus != RelationshipStatus::Candidate &&
            targetStatus != RelationshipStatus::Unknown) {
            outcome.recordMemory = true;
        }

        return outcome;
    }

    std::vector<AutoMarriageCandidate> CollectAutoMarriageCandidates(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        float thresholdDays) {
        std::vector<AutoMarriageCandidate> results;
        if (!snap.relationships) {
            return results;
        }

        for (const auto& [key, edge] : snap.relationships->AllEdges()) {
            if (key.first.IsNull() || key.second.IsNull() || key.first.IsPlayer() || key.second.IsPlayer()) {
                continue;
            }
            if (edge.status != RelationshipStatus::Engaged) {
                continue;
            }

            float engagementTime = -1.0f;
            for (auto it = edge.statusHistory.rbegin(); it != edge.statusHistory.rend(); ++it) {
                if (it->toStatus == RelationshipStatus::Engaged) {
                    engagementTime = it->timestamp;
                    break;
                }
            }

            if (engagementTime < 0.0f) {
                results.push_back(AutoMarriageCandidate{
                    .pair = key,
                    .needsTimestampBackfill = true,
                    .readyForAutoMarriage = false
                });
            } else if ((currentGameDay - engagementTime) >= thresholdDays) {
                results.push_back(AutoMarriageCandidate{
                    .pair = key,
                    .needsTimestampBackfill = false,
                    .readyForAutoMarriage = true
                });
            }
        }

        return results;
    }

    void BackfillMissingEngagementTimestamps(
        State::WorldState& ws,
        std::span<const CanonicalPairKey> pairs,
        float currentGameDay) {
        if (pairs.empty()) {
            return;
        }
        auto& rels = ws.relationships.Write();
        for (const auto& key : pairs) {
            const auto* existing = rels.FindEdge(key);
            if (!existing || existing->status != RelationshipStatus::Engaged) {
                continue;
            }
            auto updated = *existing;
            updated.statusHistory.push_back(State::StatusTransitionEntry{
                .fromStatus = RelationshipStatus::Unknown,
                .toStatus = RelationshipStatus::Engaged,
                .timestamp = currentGameDay,
                .initiator = key.first
            });
            rels.UpsertEdge(std::move(updated));
        }
    }

}  // namespace MARAS::Domain::Relationships
