#include "maras/domain/relationships/InfidelityRules.h"

#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/foundation/EnumUtils.h"

#include <algorithm>
#include <unordered_set>

namespace MARAS::Domain::Relationships {

    bool IsActorCheating(
        const State::WorldStateSnapshot& snap,
        ActorId actor) noexcept {
        if (actor.IsNull() || !snap.relationships || !snap.affairs) {
            return false;
        }
        if (actor.IsPlayer() && snap.settings && snap.settings->enablePlayerPolygamy) {
            return false;
        }

        std::vector<ActorId> closedMainPartners;
        for (const auto& key : snap.relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap.relationships->FindEdge(key);
            if (edge && Foundation::IsMainTierStatus(edge->status) && !edge->isOpen) {
                closedMainPartners.push_back(key.Other(actor));
            }
        }

        if (closedMainPartners.size() > 1) {
            return true;
        }

        if (!closedMainPartners.empty()) {
            const bool isActorPlayerPartner = IsPlayerActiveMainPartner(snap, actor);
            for (const auto& [partnerId, status] : snap.relationships->GetActivePartners(actor)) {
                (void)status;
                if (partnerId.IsNull() || partnerId == actor) {
                    continue;
                }
                if (std::find(closedMainPartners.begin(), closedMainPartners.end(), partnerId) == closedMainPartners.end()) {
                    if (isActorPlayerPartner &&
                        IsPlayerActiveMainPartner(snap, partnerId) &&
                        closedMainPartners.size() == 1 &&
                        closedMainPartners.front().IsPlayer()) {
                        continue;
                    }
                    return true;
                }
            }
        }

        for (const auto& rec : snap.affairs->GetByCheater(actor)) {
            if (rec.affairStatus == AffairStatus::Ongoing) {
                return true;
            }
        }
        return false;
    }

    bool IsRelationshipCheating(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept {
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB || !snap.relationships || !snap.affairs) {
            return false;
        }

        const bool bothCoSpouses =
            IsPlayerActiveMainPartner(snap, actorA) && IsPlayerActiveMainPartner(snap, actorB);
        const bool polygamyEnabled = snap.settings && snap.settings->enablePlayerPolygamy;

        const auto hasClosedMainWithOther = [&](ActorId self, ActorId other) noexcept -> bool {
            for (const auto& key : snap.relationships->GetAdjacentPairs(self)) {
                if (key.Contains(other)) {
                    continue;
                }
                const auto* edge = snap.relationships->FindEdge(key);
                if (!edge || !Foundation::IsMainTierStatus(edge->status) || edge->isOpen) {
                    continue;
                }
                if (self.IsPlayer() && polygamyEnabled) {
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

        for (const auto& rec : snap.affairs->GetByCheater(actorA)) {
            if (rec.paramour == actorB &&
                (rec.affairStatus == AffairStatus::Ongoing ||
                 rec.affairStatus == AffairStatus::EndedConfronted)) {
                return true;
            }
        }
        for (const auto& rec : snap.affairs->GetByCheater(actorB)) {
            if (rec.paramour == actorA &&
                (rec.affairStatus == AffairStatus::Ongoing ||
                 rec.affairStatus == AffairStatus::EndedConfronted)) {
                return true;
            }
        }

        return false;
    }

    PairOpenOutcome ApplySetPairOpen(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB,
        bool isOpen,
        float currentGameDay) {
        PairOpenOutcome outcome;
        if (actorA.IsNull() || actorB.IsNull() || actorA == actorB) {
            return outcome;
        }

        if (const auto* existingEdge = ws.relationships.Read().FindEdge(actorA, actorB)) {
            outcome.wasOpen = existingEdge->isOpen;
        }

        struct CandidateLover {
            ActorId member{};
            ActorId lover{};
        };
        std::vector<CandidateLover> candidateLovers;
        std::unordered_set<ActorId> candidateIntentHolders{ actorA, actorB };

        if (!isOpen) {
            for (const ActorId member : { actorA, actorB }) {
                if (member.IsNull()) {
                    continue;
                }
                for (const ActorId lover : ws.relationships.Read().GetPartnersWithStatus(member, RelationshipStatus::Lover)) {
                    candidateLovers.push_back(CandidateLover{ member, lover });
                    candidateIntentHolders.insert(lover);
                }
            }
        } else {
            for (const auto& rec : ws.affairs.Read().GetForCouplePair(Foundation::CanonicalPairKey{ actorA, actorB })) {
                if (rec.affairStatus == AffairStatus::Ongoing && rec.deceivedPartner.IsPlayer()) {
                    outcome.allowedCheatersToRefresh.push_back(rec.cheater);
                }
            }
        }

        std::vector<ActorId> activeIntentBefore;
        for (const ActorId holder : candidateIntentHolders) {
            if (!holder.IsNull() && !holder.IsPlayer() &&
                ws.relationships.Read().GetPendingIntent(holder).type != PartnerIntentType::None) {
                activeIntentBefore.push_back(holder);
            }
        }

        const auto mut = ws.SetPairOpen(actorA, actorB, isOpen, currentGameDay);
        outcome.changed = mut.changed;

        if (const auto* updatedEdge = ws.relationships.Read().FindEdge(actorA, actorB)) {
            outcome.pairStatus = updatedEdge->status;
        }

        for (const auto& cand : candidateLovers) {
            if (ws.relationships.Read().GetStatus(cand.member, cand.lover) == RelationshipStatus::ExLover) {
                outcome.brokenLovers.emplace_back(cand.member, cand.lover);
            }
        }

        for (const ActorId holder : activeIntentBefore) {
            if (ws.relationships.Read().GetPendingIntent(holder).type == PartnerIntentType::None) {
                outcome.clearedIntents.push_back(holder);
            }
        }

        return outcome;
    }

    CheatingIncidentOutcome ApplyRecordCheating(
        State::WorldState& ws,
        ActorId cheater,
        ActorId victim,
        ActorId paramour,
        bool isSecret,
        AffairType affairType,
        AffairStatus affairStatus,
        AffairStage affairStage,
        float currentGameDay) {
        CheatingIncidentOutcome outcome;
        if (cheater.IsNull() || victim.IsNull() || paramour.IsNull()) {
            return outcome;
        }

        if (cheater.IsPlayer() && ws.settings.Read().enablePlayerPolygamy) {
            return outcome;
        }

        const auto snap = ws.Snapshot();
        if (victim.IsPlayer() &&
            IsPlayerActiveMainPartner(snap, cheater) &&
            IsPlayerActiveMainPartner(snap, paramour)) {
            return outcome;
        }

        // Dual-stage affair applies strictly to Player's partners cheating on the Player
        if (!victim.IsPlayer()) {
            affairStage = AffairStage::Physical;
        }

        const auto intentBefore = ws.relationships.Read().GetPendingIntent(cheater);
        outcome.recorded = ws.RecordCheatingIncident(
            cheater,
            victim,
            paramour,
            isSecret,
            affairType,
            affairStatus,
            affairStage,
            currentGameDay);

        if (outcome.recorded) {
            if (const auto* rec = ws.affairs.Read().FindTriple(cheater, victim, paramour)) {
                outcome.recordedStage = rec->affairStage;
            } else {
                outcome.recordedStage = affairStage;
            }
            const auto intentAfter = ws.relationships.Read().GetPendingIntent(cheater);
            outcome.intentUpgraded =
                (intentBefore.type == PartnerIntentType::ConfessTalking &&
                 intentAfter.type == PartnerIntentType::ConfessPhysical);
        }

        return outcome;
    }

    AffairStageUpdateOutcome ApplySetAffairStage(
        State::WorldState& ws,
        ActorId cheater,
        ActorId victim,
        ActorId affairPartnerFilter,
        AffairStage stage,
        float currentGameDay) {
        AffairStageUpdateOutcome outcome;
        if (cheater.IsNull() || victim.IsNull()) {
            return outcome;
        }
        const AffairStage targetStage = victim.IsPlayer() ? stage : AffairStage::Physical;
        outcome.appliedStage = targetStage;

        const auto coupleRecords = ws.affairs.Read().GetForCouplePair(Foundation::CanonicalPairKey{ cheater, victim });
        for (const auto& rec : coupleRecords) {
            if (rec.cheater != cheater || rec.deceivedPartner != victim) {
                continue;
            }
            if (!affairPartnerFilter.IsNull() && rec.paramour != affairPartnerFilter) {
                continue;
            }
            if (rec.affairStage != targetStage) {
                (void)ws.affairs.Write().SetStage(cheater, victim, rec.paramour, targetStage, currentGameDay);
                if (targetStage == AffairStage::Physical) {
                    const auto existingIntent = ws.relationships.Read().GetPendingIntent(cheater);
                    if (existingIntent.type == PartnerIntentType::ConfessTalking &&
                        (existingIntent.relatedLover.IsNull() || existingIntent.relatedLover == rec.paramour)) {
                        ws.relationships.Write().SetPendingIntent(
                            cheater,
                            State::PendingPartnerIntentRecord{
                                .type = PartnerIntentType::ConfessPhysical,
                                .createdGameDay = currentGameDay,
                                .relatedLover = rec.paramour,
                            });
                        outcome.intentUpgraded = true;
                        outcome.upgradedLover = rec.paramour;
                    }
                }
            }
            outcome.updatedParamours.push_back(rec.paramour);
            outcome.updated = true;
            if (!affairPartnerFilter.IsNull()) {
                break;
            }
        }
        return outcome;
    }

    AffairStage GetEffectiveAffairStage(
        const State::WorldStateSnapshot& snap,
        ActorId cheater,
        ActorId victim,
        ActorId affairPartnerFilter) noexcept {
        if (cheater.IsNull() || !victim.IsPlayer() || !snap.affairs) {
            return AffairStage::Physical;
        }
        for (const auto& rec : snap.affairs->GetForCouplePair(Foundation::CanonicalPairKey{ cheater, victim })) {
            if (rec.cheater == cheater && rec.deceivedPartner == victim &&
                (affairPartnerFilter.IsNull() || rec.paramour == affairPartnerFilter)) {
                if (!affairPartnerFilter.IsNull() || rec.affairStatus == AffairStatus::Ongoing) {
                    return rec.affairStage;
                }
            }
        }
        return AffairStage::Physical;
    }

    ExposeAffairOutcome ApplyAffairDiscoveryOrResolution(
        State::WorldState& ws,
        ActorId cheater,
        ActorId affairPartner,
        std::string_view statusOrResolution,
        ActorId targetVictim) {
        ExposeAffairOutcome outcome;
        if (cheater.IsNull()) {
            return outcome;
        }

        outcome.discovery = Foundation::StringToAffairDiscovery(statusOrResolution);
        outcome.resolution = Foundation::StringToAffairResolution(statusOrResolution);
        outcome.isDiscovery = (outcome.discovery != AffairDiscovery::None);

        auto recordIntentClear = [&](ActorId partner) {
            const auto prev = ws.relationships.Read().GetPendingIntent(partner);
            if (prev.type != PartnerIntentType::None && ws.relationships.Write().ClearPendingIntent(partner)) {
                outcome.clearedIntents.push_back(partner);
            }
        };

        std::unordered_set<ActorId> victimsSet;
        std::unordered_set<ActorId> openSet;
        std::unordered_set<ActorId> splitSet;
        std::unordered_set<ActorId> breakSet;

        const auto cheaterRecords = ws.affairs.Read().GetByCheater(cheater);
        for (const auto& rec : cheaterRecords) {
            if (rec.cheater != cheater || rec.affairStatus != AffairStatus::Ongoing) {
                continue;
            }
            if (!targetVictim.IsNull() && rec.deceivedPartner != targetVictim) {
                continue;
            }
            if (!affairPartner.IsNull() && rec.paramour != affairPartner) {
                continue;
            }

            if (outcome.isDiscovery) {
                if (!rec.isSecret) {
                    continue;
                }
                ws.affairs.Write().ExposeAndResolve(cheater, rec.paramour, rec.deceivedPartner, AffairResolution::None);
                outcome.updated = true;
                victimsSet.insert(rec.deceivedPartner);
                outcome.events.push_back(AffairDiscoveryEvent{ cheater, rec.deceivedPartner, rec.paramour });
            } else if (outcome.resolution != AffairResolution::None) {
                ws.affairs.Write().ExposeAndResolve(cheater, rec.paramour, rec.deceivedPartner, outcome.resolution);
                outcome.updated = true;
                victimsSet.insert(rec.deceivedPartner);
                outcome.events.push_back(AffairDiscoveryEvent{ cheater, rec.deceivedPartner, rec.paramour });

                switch (outcome.resolution) {
                    case AffairResolution::Confronted:
                        if (!rec.paramour.IsNull()) {
                            breakSet.insert(rec.paramour);
                        }
                        break;
                    case AffairResolution::Allowed:
                        if (!rec.deceivedPartner.IsNull()) {
                            openSet.insert(rec.deceivedPartner);
                        }
                        break;
                    case AffairResolution::Breakup:
                        recordIntentClear(cheater);
                        if (!rec.deceivedPartner.IsNull()) {
                            splitSet.insert(rec.deceivedPartner);
                        }
                        break;
                    default:
                        break;
                }
            } else {
                if (!rec.isSecret) {
                    continue;
                }
                ws.affairs.Write().ExposeAndResolve(cheater, rec.paramour, rec.deceivedPartner, AffairResolution::None);
                outcome.updated = true;
                victimsSet.insert(rec.deceivedPartner);
            }
        }

        if (outcome.updated) {
            const auto curIntent = ws.relationships.Read().GetPendingIntent(cheater);
            if (curIntent.type == PartnerIntentType::ConfessTalking ||
                curIntent.type == PartnerIntentType::ConfessPhysical) {
                recordIntentClear(cheater);
            }
        }

        outcome.victimsToUpdate.assign(victimsSet.begin(), victimsSet.end());
        outcome.victimsToOpen.assign(openSet.begin(), openSet.end());
        outcome.victimsToSplit.assign(splitSet.begin(), splitSet.end());
        outcome.partnersToBreak.assign(breakSet.begin(), breakSet.end());
        return outcome;
    }

    std::vector<ClearedIntentDelta> EvaluateBreakupAffairsAndIntents(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB) {
        std::vector<ClearedIntentDelta> clearedIntents;
        auto recordIntentClear = [&](ActorId partner) {
            const auto prev = ws.relationships.Read().GetPendingIntent(partner);
            if (prev.type != PartnerIntentType::None &&
                ws.relationships.Write().ClearPendingIntent(partner)) {
                clearedIntents.push_back(ClearedIntentDelta{ partner, prev.type });
            }
        };

        std::vector<ActorId> confessIntentCheaters;
        const auto scanDirection = [&](ActorId cheater, ActorId paramour) {
            if (cheater.IsNull() || paramour.IsNull()) {
                return;
            }
            for (const auto& rec : ws.affairs.Read().GetByCheater(cheater)) {
                if (rec.paramour == paramour && rec.affairStatus == AffairStatus::Ongoing) {
                    const auto intent = ws.relationships.Read().GetPendingIntent(cheater);
                    if ((intent.relatedLover.IsNull() || intent.relatedLover == paramour) &&
                        (intent.type == PartnerIntentType::ConfessTalking ||
                         intent.type == PartnerIntentType::ConfessPhysical)) {
                        confessIntentCheaters.push_back(cheater);
                    }
                }
            }
        };
        scanDirection(actorA, actorB);
        scanDirection(actorB, actorA);
        ws.affairs.Write().CloseOngoingBetweenCheaterAndParamour(actorA, actorB);
        for (const ActorId cheater : confessIntentCheaters) {
            recordIntentClear(cheater);
        }

        if (!actorA.IsNull() && !actorB.IsNull()) {
            std::vector<ActorId> victimSplitCheaters;
            for (const auto& rec : ws.affairs.Read().GetForCouplePair(Foundation::CanonicalPairKey{ actorA, actorB })) {
                if (rec.affairStatus == AffairStatus::Ongoing) {
                    victimSplitCheaters.push_back(rec.cheater);
                }
            }
            ws.affairs.Write().CloseOngoingOnVictimSplit(actorA, actorB);
            for (const ActorId cheater : victimSplitCheaters) {
                recordIntentClear(cheater);
            }
        }

        if (actorA.IsPlayer() && !actorB.IsNull()) {
            recordIntentClear(actorB);
        } else if (actorB.IsPlayer() && !actorA.IsNull()) {
            recordIntentClear(actorA);
        }

        return clearedIntents;
    }

    CleanupAffairsOutcome ApplyCleanupActorIntentsAndThirdPartyAffairs(
        State::WorldState& ws,
        ActorId target,
        bool eraseRecords) {
        CleanupAffairsOutcome outcome;
        if (target.IsNull()) {
            return outcome;
        }

        for (const auto& [partner, intent] : ws.relationships.Read().AllPendingIntents()) {
            if (partner == target || intent.relatedLover == target) {
                outcome.clearedIntents.push_back(ClearedIntentDelta{ partner, intent.type });
            }
        }
        ws.relationships.Write().ClearIntentsInvolving(target);

        std::unordered_set<ActorId> affectedSet;
        if (eraseRecords) {
            for (const ActorId aff : ws.affairs.Write().EraseAllInvolvingActor(target)) {
                if (!aff.IsNull() && aff != target) {
                    affectedSet.insert(aff);
                }
            }
        } else {
            for (const auto& rec : ws.affairs.Read().GetAllInvolving(target)) {
                if (!rec.cheater.IsNull() && rec.cheater != target) {
                    affectedSet.insert(rec.cheater);
                }
                if (!rec.deceivedPartner.IsNull() && rec.deceivedPartner != target) {
                    affectedSet.insert(rec.deceivedPartner);
                }
                if (!rec.paramour.IsNull() && rec.paramour != target) {
                    affectedSet.insert(rec.paramour);
                }
            }
            for (const ActorId aff : ws.affairs.Write().CloseOngoingForDeceasedActor(target)) {
                if (!aff.IsNull() && aff != target) {
                    affectedSet.insert(aff);
                }
            }
        }

        outcome.affectedActors.assign(affectedSet.begin(), affectedSet.end());
        return outcome;
    }

    std::vector<ActorId> MarkCheatingAllowedForPair(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB) {
        std::unordered_set<ActorId> cheatersToRefresh;
        for (const auto& rec : ws.affairs.Read().GetForCouplePair(Foundation::CanonicalPairKey{ actorA, actorB })) {
            if (rec.affairStatus == AffairStatus::Ongoing && rec.deceivedPartner.IsPlayer()) {
                cheatersToRefresh.insert(rec.cheater);
            }
        }
        ws.affairs.Write().MarkAllowedForPair(actorA, actorB);
        return std::vector<ActorId>(cheatersToRefresh.begin(), cheatersToRefresh.end());
    }

    std::size_t MarkPlayerCheatingAllowedOnPolygamy(State::WorldState& ws) {
        std::size_t count = 0;
        const auto playerRecords = ws.affairs.Read().GetByCheater(ActorId::Player());
        for (const auto& rec : playerRecords) {
            if (rec.affairStatus == AffairStatus::Ongoing) {
                if (ws.affairs.Write().SetStatus(rec.cheater, rec.deceivedPartner, rec.paramour, AffairStatus::Allowed)) {
                    ++count;
                }
            }
        }
        return count;
    }

}  // namespace MARAS::Domain::Relationships
