#include "maras/state/WorldTables.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace MARAS::State {

    namespace {

        template <typename Vec, typename Value>
        void PushUnique(Vec& vec, const Value& val) {
            if (std::find(vec.begin(), vec.end(), val) == vec.end()) {
                vec.push_back(val);
            }
        }

        template <typename Vec, typename Value>
        void EraseValue(Vec& vec, const Value& val) noexcept {
            vec.erase(std::remove(vec.begin(), vec.end(), val), vec.end());
        }

        GuardianSetDiff ComputeGuardianSetDiff(ActorId oldA, ActorId oldB, ActorId newA, ActorId newB) noexcept {
            if (oldA == oldB) {
                oldB = ActorId{};
            }
            if (newA == newB) {
                newB = ActorId{};
            }

            auto inPair = [](ActorId target, ActorId p1, ActorId p2) noexcept -> bool {
                return !target.IsNull() && (target == p1 || target == p2);
            };

            GuardianSetDiff diff{};
            if (!oldA.IsNull() && !inPair(oldA, newA, newB)) {
                diff.removedA = oldA;
                diff.changed = true;
            }
            if (!oldB.IsNull() && !inPair(oldB, newA, newB)) {
                if (diff.removedA.IsNull()) {
                    diff.removedA = oldB;
                } else {
                    diff.removedB = oldB;
                }
                diff.changed = true;
            }
            if (!newA.IsNull() && !inPair(newA, oldA, oldB)) {
                diff.addedA = newA;
                diff.changed = true;
            }
            if (!newB.IsNull() && !inPair(newB, oldA, oldB)) {
                if (diff.addedA.IsNull()) {
                    diff.addedA = newB;
                } else {
                    diff.addedB = newB;
                }
                diff.changed = true;
            }
            return diff;
        }

    }  // namespace

    // =========================================================================
    // 1. ActorProfileTable
    // =========================================================================

    const ActorProfileRecord* ActorProfileTable::Find(RefId refId) const noexcept {
        if (refId.IsNull()) {
            return nullptr;
        }
        const auto it = profiles_.find(refId);
        return it != profiles_.end() ? &it->second : nullptr;
    }

    ActorProfileRecord* ActorProfileTable::FindMutable(RefId refId) noexcept {
        if (refId.IsNull()) {
            return nullptr;
        }
        const auto it = profiles_.find(refId);
        return it != profiles_.end() ? &it->second : nullptr;
    }

    ActorProfileRecord& ActorProfileTable::Upsert(ActorProfileRecord record) {
        const RefId key = record.refId;
        auto [it, inserted] = profiles_.insert_or_assign(key, std::move(record));
        (void)inserted;
        return it->second;
    }

    bool ActorProfileTable::Erase(RefId refId) noexcept {
        return profiles_.erase(refId) > 0;
    }

    bool ActorProfileTable::IsRegisteredAdult(RefId refId) const noexcept {
        const auto* rec = Find(refId);
        return rec != nullptr && rec->isRegisteredAdult;
    }

    void ActorProfileTable::SetRegisteredAdult(RefId refId, bool registered) {
        if (refId.IsNull()) {
            return;
        }
        auto it = profiles_.find(refId);
        if (it != profiles_.end()) {
            it->second.isRegisteredAdult = registered;
        } else if (registered) {
            ActorProfileRecord rec{};
            rec.refId = refId;
            rec.isRegisteredAdult = true;
            profiles_.emplace(refId, std::move(rec));
        }
    }

    void ActorProfileTable::AddPendingRelationship(BaseId actorBaseId, const PendingRelationshipEntry& entry) {
        if (actorBaseId.IsNull() || entry.partnerBaseId.IsNull()) {
            return;
        }
        auto& list = pendingByBase_[actorBaseId];
        for (auto& existing : list) {
            if (existing.partnerBaseId == entry.partnerBaseId) {
                existing = entry;
                return;
            }
        }
        list.push_back(entry);
    }

    bool ActorProfileTable::ErasePendingRelationshipsForBase(BaseId actorBaseId) noexcept {
        return pendingByBase_.erase(actorBaseId) > 0;
    }

    void ActorProfileTable::Clear() noexcept {
        profiles_.clear();
        pendingByBase_.clear();
    }

    // =========================================================================
    // 2. RelationshipEdgeTable
    // =========================================================================

    void RelationshipEdgeTable::IndexKey(CanonicalPairKey key) {
        if (!key.IsValid()) {
            return;
        }
        PushUnique(adjacency_[key.first], key);
        PushUnique(adjacency_[key.second], key);
    }

    void RelationshipEdgeTable::UnindexKey(CanonicalPairKey key) noexcept {
        auto removeFromActor = [&](ActorId actor) noexcept {
            auto it = adjacency_.find(actor);
            if (it != adjacency_.end()) {
                EraseValue(it->second, key);
                if (it->second.empty()) {
                    adjacency_.erase(it);
                }
            }
        };
        removeFromActor(key.first);
        removeFromActor(key.second);
    }

    const RelationshipEdgeRecord* RelationshipEdgeTable::FindEdge(ActorId a, ActorId b) const noexcept {
        return FindEdge(CanonicalPairKey{ a, b });
    }

    const RelationshipEdgeRecord* RelationshipEdgeTable::FindEdge(CanonicalPairKey key) const noexcept {
        if (!key.IsValid()) {
            return nullptr;
        }
        const auto it = edges_.find(key);
        return it != edges_.end() ? &it->second : nullptr;
    }

    RelationshipEdgeRecord& RelationshipEdgeTable::GetOrCreateEdge(ActorId a, ActorId b) {
        const CanonicalPairKey key{ a, b };
        auto [it, inserted] = edges_.try_emplace(key);
        if (inserted) {
            it->second.pair = key;
            IndexKey(key);
        }
        return it->second;
    }

    void RelationshipEdgeTable::UpsertEdge(RelationshipEdgeRecord edge) {
        if (!edge.pair.IsValid()) {
            return;
        }
        const CanonicalPairKey key = edge.pair;
        const bool isNew = !edges_.contains(key);
        edges_[key] = std::move(edge);
        if (isNew) {
            IndexKey(key);
        }
    }

    bool RelationshipEdgeTable::EraseEdge(ActorId a, ActorId b) noexcept {
        return EraseEdge(CanonicalPairKey{ a, b });
    }

    bool RelationshipEdgeTable::EraseEdge(CanonicalPairKey key) noexcept {
        if (edges_.erase(key) == 0) {
            return false;
        }
        UnindexKey(key);
        return true;
    }

    RelationshipStatus RelationshipEdgeTable::GetStatus(ActorId a, ActorId b) const noexcept {
        const auto* edge = FindEdge(a, b);
        return edge ? edge->status : RelationshipStatus::Unknown;
    }

    bool RelationshipEdgeTable::IsPairOpen(ActorId a, ActorId b) const noexcept {
        const auto* edge = FindEdge(a, b);
        return edge != nullptr && edge->isOpen;
    }

    bool RelationshipEdgeTable::HasAnyOpenMainPair(ActorId actor) const noexcept {
        for (const CanonicalPairKey& key : GetAdjacentPairs(actor)) {
            const auto* edge = FindEdge(key);
            if (edge && IsMainTierStatus(edge->status) && edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    bool RelationshipEdgeTable::IsInAnyOpenRelationship(ActorId actor) const noexcept {
        for (const CanonicalPairKey& key : GetAdjacentPairs(actor)) {
            const auto* edge = FindEdge(key);
            if (edge && edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    const std::vector<CanonicalPairKey>& RelationshipEdgeTable::GetAdjacentPairs(ActorId actor) const noexcept {
        static const std::vector<CanonicalPairKey> kEmpty{};
        if (actor.IsNull()) {
            return kEmpty;
        }
        const auto it = adjacency_.find(actor);
        return it != adjacency_.end() ? it->second : kEmpty;
    }

    std::vector<ActorId> RelationshipEdgeTable::GetPartnersWithStatus(ActorId actor, RelationshipStatus status) const {
        std::vector<ActorId> result;
        for (const CanonicalPairKey& key : GetAdjacentPairs(actor)) {
            const auto* edge = FindEdge(key);
            if (edge && edge->status == status) {
                result.push_back(key.Other(actor));
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<std::pair<ActorId, RelationshipStatus>> RelationshipEdgeTable::GetActivePartners(ActorId actor) const {
        std::vector<std::pair<ActorId, RelationshipStatus>> result;
        for (const CanonicalPairKey& key : GetAdjacentPairs(actor)) {
            const auto* edge = FindEdge(key);
            if (edge && IsActivePartnerStatus(edge->status)) {
                result.emplace_back(key.Other(actor), edge->status);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        return result;
    }

    ActorId RelationshipEdgeTable::GetPrimaryPartner(ActorId actor) const noexcept {
        ActorId bestPartner{};
        int bestPriority = 99;
        auto priorityOf = [](RelationshipStatus st) noexcept -> int {
            switch (st) {
                case RelationshipStatus::Married:
                    return 0;
                case RelationshipStatus::Engaged:
                    return 1;
                case RelationshipStatus::Dating:
                    return 2;
                default:
                    return 99;
            }
        };

        for (const CanonicalPairKey& key : GetAdjacentPairs(actor)) {
            const auto* edge = FindEdge(key);
            if (!edge) {
                continue;
            }
            const int prio = priorityOf(edge->status);
            if (prio < bestPriority) {
                bestPriority = prio;
                bestPartner = key.Other(actor);
            } else if (prio == bestPriority && prio < 99) {
                const ActorId other = key.Other(actor);
                if (other < bestPartner) {
                    bestPartner = other;
                }
            }
        }
        return bestPartner;
    }

    void RelationshipEdgeTable::SetPendingIntent(ActorId partner, PendingPartnerIntentRecord intent) {
        if (partner.IsNull()) {
            return;
        }
        if (intent.type == PartnerIntentType::None) {
            pendingPartnerIntents_.erase(partner);
        } else {
            pendingPartnerIntents_[partner] = intent;
        }
    }

    PendingPartnerIntentRecord RelationshipEdgeTable::GetPendingIntent(ActorId partner) const noexcept {
        const auto it = pendingPartnerIntents_.find(partner);
        return it != pendingPartnerIntents_.end() ? it->second : PendingPartnerIntentRecord{};
    }

    bool RelationshipEdgeTable::ClearPendingIntent(ActorId partner) noexcept {
        return pendingPartnerIntents_.erase(partner) > 0;
    }

    void RelationshipEdgeTable::ClearIntentsInvolving(ActorId actor) noexcept {
        if (actor.IsNull()) {
            return;
        }
        pendingPartnerIntents_.erase(actor);
        for (auto it = pendingPartnerIntents_.begin(); it != pendingPartnerIntents_.end();) {
            if (it->second.relatedLover == actor) {
                it = pendingPartnerIntents_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void RelationshipEdgeTable::Clear() noexcept {
        edges_.clear();
        adjacency_.clear();
        pendingPartnerIntents_.clear();
    }

    bool RelationshipEdgeTable::operator==(const RelationshipEdgeTable& other) const {
        return edges_ == other.edges_ && pendingPartnerIntents_ == other.pendingPartnerIntents_;
    }

    // =========================================================================
    // 3. AffairLedger
    // =========================================================================

    void AffairLedger::IndexRecord(const AffairRecord& rec) {
        PushUnique(byCouplePair_[rec.CouplePair()], rec.id);
        PushUnique(byCheater_[rec.cheater], rec.id);
        PushUnique(byDeceivedPartner_[rec.deceivedPartner], rec.id);
        PushUnique(byParamour_[rec.paramour], rec.id);
    }

    void AffairLedger::UnindexRecord(const AffairRecord& rec) noexcept {
        auto eraseFromActorMap = [](auto& map, const auto& key, AffairId id) noexcept {
            auto it = map.find(key);
            if (it != map.end()) {
                EraseValue(it->second, id);
                if (it->second.empty()) {
                    map.erase(it);
                }
            }
        };
        eraseFromActorMap(byCouplePair_, rec.CouplePair(), rec.id);
        eraseFromActorMap(byCheater_, rec.cheater, rec.id);
        eraseFromActorMap(byDeceivedPartner_, rec.deceivedPartner, rec.id);
        eraseFromActorMap(byParamour_, rec.paramour, rec.id);
    }

    AffairIncidentOutcome AffairLedger::RecordIncident(
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        bool isSecret,
        AffairType affairType,
        AffairStatus affairStatus,
        AffairStage affairStage,
        float currentGameDay) {
        if (cheater.IsNull() || deceivedPartner.IsNull() || paramour.IsNull() ||
            cheater == deceivedPartner || cheater == paramour || deceivedPartner == paramour) {
            return {};
        }

        // Talking stage applies exclusively when Player is the deceived partner
        if (!deceivedPartner.IsPlayer()) {
            affairStage = AffairStage::Physical;
        }

        const std::int32_t intDay = static_cast<std::int32_t>(currentGameDay);
        const CanonicalPairKey coupleKey{ cheater, deceivedPartner };
        if (auto itPair = byCouplePair_.find(coupleKey); itPair != byCouplePair_.end()) {
            for (const AffairId existingId : itPair->second) {
                auto itRec = records_.find(existingId);
                if (itRec == records_.end()) {
                    continue;
                }
                AffairRecord& rec = itRec->second;
                if (rec.cheater == cheater && rec.deceivedPartner == deceivedPartner && rec.paramour == paramour) {
                    const bool wasReopened =
                        (rec.affairStatus != AffairStatus::Ongoing && affairStatus == AffairStatus::Ongoing);
                    bool escalated = false;
                    ++rec.incidentCount;
                    rec.lastTimestamp = intDay;
                    if (wasReopened) {
                        rec.affairStatus = AffairStatus::Ongoing;
                        rec.isSecret = isSecret;
                    } else if (!isSecret) {
                        rec.isSecret = false;
                    }
                    if (affairType != AffairType::Casual) {
                        rec.affairType = affairType;
                    }
                    if (rec.affairStatus == AffairStatus::Ongoing) {
                        if (affairStage == AffairStage::Physical && rec.affairStage == AffairStage::Talking) {
                            rec.affairStage = AffairStage::Physical;
                            rec.stageStartDay = currentGameDay;
                            escalated = true;
                        } else if (rec.incidentCount == 1 || wasReopened) {
                            rec.affairStage = affairStage;
                            rec.stageStartDay = currentGameDay;
                        }
                    }
                    return AffairIncidentOutcome{ rec.id, escalated };
                }
            }
        }

        const AffairId newId{ nextAffairId_++ };
        AffairRecord rec{};
        rec.id = newId;
        rec.cheater = cheater;
        rec.deceivedPartner = deceivedPartner;
        rec.paramour = paramour;
        rec.incidentCount = 1;
        rec.lastTimestamp = intDay;
        rec.isSecret = isSecret;
        rec.affairType = affairType;
        rec.affairStatus = affairStatus;
        rec.affairStage = affairStage;
        rec.stageStartDay = currentGameDay;

        records_.emplace(newId, rec);
        IndexRecord(rec);
        return AffairIncidentOutcome{ newId, false };
    }

    AffairId AffairLedger::ImportSerializedRecord(
        CanonicalPairKey couplePair,
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        std::uint32_t count,
        std::int32_t lastTimestamp,
        bool isSecret,
        AffairType affairType,
        AffairStatus affairStatus,
        AffairStage affairStage,
        float stageStartDay) {
        if (!couplePair.IsValid() || cheater.IsNull() || deceivedPartner.IsNull() || paramour.IsNull()) {
            return AffairId{};
        }
        const AffairId id{ nextAffairId_++ };
        AffairRecord rec{};
        rec.id = id;
        rec.cheater = cheater;
        rec.deceivedPartner = deceivedPartner;
        rec.paramour = paramour;
        rec.incidentCount = count;
        rec.lastTimestamp = lastTimestamp;
        rec.isSecret = isSecret;
        rec.affairType = affairType;
        rec.affairStatus = affairStatus;
        rec.affairStage = deceivedPartner.IsPlayer() ? affairStage : AffairStage::Physical;
        rec.stageStartDay = stageStartDay >= 0.0f ? stageStartDay : static_cast<float>(lastTimestamp);

        records_.emplace(id, rec);
        PushUnique(byCouplePair_[couplePair], id);
        PushUnique(byCheater_[cheater], id);
        PushUnique(byDeceivedPartner_[deceivedPartner], id);
        PushUnique(byParamour_[paramour], id);
        return id;
    }

    bool AffairLedger::UpdateStageMetadata(
        CanonicalPairKey couplePair,
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        AffairStage stage,
        float stageStartDay) noexcept {
        const auto itPair = byCouplePair_.find(couplePair);
        if (itPair == byCouplePair_.end()) {
            return false;
        }
        for (const AffairId id : itPair->second) {
            auto itRec = records_.find(id);
            if (itRec != records_.end() &&
                itRec->second.cheater == cheater &&
                itRec->second.deceivedPartner == deceivedPartner &&
                itRec->second.paramour == paramour) {
                itRec->second.affairStage = deceivedPartner.IsPlayer() ? stage : AffairStage::Physical;
                itRec->second.stageStartDay = stageStartDay;
                return true;
            }
        }
        return false;
    }

    const AffairRecord* AffairLedger::FindById(AffairId id) const noexcept {
        const auto it = records_.find(id);
        return it != records_.end() ? &it->second : nullptr;
    }

    const AffairRecord* AffairLedger::FindTriple(
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour) const noexcept {
        const CanonicalPairKey coupleKey{ cheater, deceivedPartner };
        const auto itPair = byCouplePair_.find(coupleKey);
        if (itPair == byCouplePair_.end()) {
            return nullptr;
        }
        for (const AffairId id : itPair->second) {
            const auto* rec = FindById(id);
            if (rec && rec->cheater == cheater && rec->deceivedPartner == deceivedPartner && rec->paramour == paramour) {
                return rec;
            }
        }
        return nullptr;
    }

    std::vector<AffairRecord> AffairLedger::GetForCouplePair(CanonicalPairKey pair) const {
        std::vector<AffairRecord> out;
        const auto it = byCouplePair_.find(pair);
        if (it == byCouplePair_.end()) {
            return out;
        }
        out.reserve(it->second.size());
        for (const AffairId id : it->second) {
            if (const auto* rec = FindById(id)) {
                out.push_back(*rec);
            }
        }
        return out;
    }

    std::vector<AffairRecord> AffairLedger::GetByCheater(ActorId cheater) const {
        std::vector<AffairRecord> out;
        if (const auto it = byCheater_.find(cheater); it != byCheater_.end()) {
            out.reserve(it->second.size());
            for (const AffairId id : it->second) {
                if (const auto* rec = FindById(id)) {
                    out.push_back(*rec);
                }
            }
        }
        return out;
    }

    std::vector<AffairRecord> AffairLedger::GetByDeceivedPartner(ActorId deceivedPartner) const {
        std::vector<AffairRecord> out;
        if (const auto it = byDeceivedPartner_.find(deceivedPartner); it != byDeceivedPartner_.end()) {
            out.reserve(it->second.size());
            for (const AffairId id : it->second) {
                if (const auto* rec = FindById(id)) {
                    out.push_back(*rec);
                }
            }
        }
        return out;
    }

    std::vector<AffairRecord> AffairLedger::GetByParamour(ActorId paramour) const {
        std::vector<AffairRecord> out;
        if (const auto it = byParamour_.find(paramour); it != byParamour_.end()) {
            out.reserve(it->second.size());
            for (const AffairId id : it->second) {
                if (const auto* rec = FindById(id)) {
                    out.push_back(*rec);
                }
            }
        }
        return out;
    }

    std::vector<AffairRecord> AffairLedger::GetAllInvolving(ActorId actor) const {
        std::unordered_set<AffairId> seen;
        std::vector<AffairRecord> out;
        auto collectFrom = [&](const std::unordered_map<ActorId, std::vector<AffairId>>& index) {
            if (const auto it = index.find(actor); it != index.end()) {
                for (const AffairId id : it->second) {
                    if (seen.insert(id).second) {
                        if (const auto* rec = FindById(id)) {
                            out.push_back(*rec);
                        }
                    }
                }
            }
        };
        collectFrom(byCheater_);
        collectFrom(byDeceivedPartner_);
        collectFrom(byParamour_);
        return out;
    }

    bool AffairLedger::HasCheatedOn(ActorId cheater, ActorId deceivedPartner) const noexcept {
        const auto it = byCouplePair_.find(CanonicalPairKey{ cheater, deceivedPartner });
        if (it == byCouplePair_.end()) {
            return false;
        }
        for (const AffairId id : it->second) {
            const auto* rec = FindById(id);
            if (rec && rec->cheater == cheater && rec->deceivedPartner == deceivedPartner) {
                return true;
            }
        }
        return false;
    }

    bool AffairLedger::HasOngoingAffairForPair(CanonicalPairKey pair) const noexcept {
        const auto it = byCouplePair_.find(pair);
        if (it == byCouplePair_.end()) {
            return false;
        }
        for (const AffairId id : it->second) {
            const auto* rec = FindById(id);
            if (rec && rec->affairStatus == AffairStatus::Ongoing) {
                return true;
            }
        }
        return false;
    }

    bool AffairLedger::SetStatus(
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        AffairStatus nextStatus) noexcept {
        const auto it = byCouplePair_.find(CanonicalPairKey{ cheater, deceivedPartner });
        if (it == byCouplePair_.end()) {
            return false;
        }
        for (const AffairId id : it->second) {
            auto itRec = records_.find(id);
            if (itRec != records_.end() &&
                itRec->second.cheater == cheater &&
                itRec->second.deceivedPartner == deceivedPartner &&
                itRec->second.paramour == paramour) {
                itRec->second.affairStatus = nextStatus;
                return true;
            }
        }
        return false;
    }

    bool AffairLedger::SetStage(
        ActorId cheater,
        ActorId deceivedPartner,
        ActorId paramour,
        AffairStage nextStage,
        float currentGameDay) noexcept {
        const auto it = byCouplePair_.find(CanonicalPairKey{ cheater, deceivedPartner });
        if (it == byCouplePair_.end()) {
            return false;
        }
        for (const AffairId id : it->second) {
            auto itRec = records_.find(id);
            if (itRec != records_.end() &&
                itRec->second.cheater == cheater &&
                itRec->second.deceivedPartner == deceivedPartner &&
                itRec->second.paramour == paramour) {
                itRec->second.affairStage = deceivedPartner.IsPlayer() ? nextStage : AffairStage::Physical;
                itRec->second.stageStartDay = currentGameDay;
                return true;
            }
        }
        return false;
    }

    void AffairLedger::MarkAllowedForPair(ActorId a, ActorId b) noexcept {
        const auto it = byCouplePair_.find(CanonicalPairKey{ a, b });
        if (it == byCouplePair_.end()) {
            return;
        }
        for (const AffairId id : it->second) {
            if (auto itRec = records_.find(id); itRec != records_.end()) {
                if (itRec->second.affairStatus == AffairStatus::Ongoing) {
                    itRec->second.affairStatus = AffairStatus::Allowed;
                    itRec->second.isSecret = false;
                }
            }
        }
    }

    std::size_t AffairLedger::ExposeAndResolve(
        ActorId cheater,
        ActorId paramour,
        ActorId deceivedFilter,
        AffairResolution resolution) noexcept {
        const auto it = byCheater_.find(cheater);
        if (it == byCheater_.end()) {
            return 0;
        }
        std::size_t updated = 0;
        for (const AffairId id : it->second) {
            auto itRec = records_.find(id);
            if (itRec == records_.end()) {
                continue;
            }
            AffairRecord& rec = itRec->second;
            if (!paramour.IsNull() && rec.paramour != paramour) {
                continue;
            }
            if (!deceivedFilter.IsNull() && rec.deceivedPartner != deceivedFilter) {
                continue;
            }
            rec.isSecret = false;
            switch (resolution) {
                case AffairResolution::Confronted:
                    rec.affairStatus = AffairStatus::EndedConfronted;
                    break;
                case AffairResolution::Allowed:
                    rec.affairStatus = AffairStatus::Allowed;
                    break;
                case AffairResolution::Breakup:
                    rec.affairStatus = AffairStatus::EndedVictimSplit;
                    break;
                case AffairResolution::None:
                case AffairResolution::_Count:
                    break;
            }
            ++updated;
        }
        return updated;
    }

    std::vector<ActorId> AffairLedger::CloseOngoingForDeceasedActor(ActorId deceased) noexcept {
        std::unordered_set<ActorId> affected;
        if (deceased.IsNull()) {
            return {};
        }

        // 1. Deceased was the cheater -> EndedBreakup
        if (const auto it = byCheater_.find(deceased); it != byCheater_.end()) {
            for (const AffairId id : it->second) {
                if (auto itRec = records_.find(id); itRec != records_.end() && itRec->second.affairStatus == AffairStatus::Ongoing) {
                    itRec->second.affairStatus = AffairStatus::EndedBreakup;
                    affected.insert(itRec->second.deceivedPartner);
                    affected.insert(itRec->second.paramour);
                }
            }
        }

        // 2. Deceased was the deceived partner -> EndedVictimSplit
        if (const auto it = byDeceivedPartner_.find(deceased); it != byDeceivedPartner_.end()) {
            for (const AffairId id : it->second) {
                if (auto itRec = records_.find(id); itRec != records_.end() && itRec->second.affairStatus == AffairStatus::Ongoing) {
                    itRec->second.affairStatus = AffairStatus::EndedVictimSplit;
                    affected.insert(itRec->second.cheater);
                    affected.insert(itRec->second.paramour);
                }
            }
        }

        // 3. Deceased was the third-party paramour -> EndedBreakup (O(1) via byParamour_!)
        if (const auto it = byParamour_.find(deceased); it != byParamour_.end()) {
            for (const AffairId id : it->second) {
                if (auto itRec = records_.find(id); itRec != records_.end() && itRec->second.affairStatus == AffairStatus::Ongoing) {
                    itRec->second.affairStatus = AffairStatus::EndedBreakup;
                    affected.insert(itRec->second.cheater);
                    affected.insert(itRec->second.deceivedPartner);
                }
            }
        }

        affected.erase(deceased);
        return std::vector<ActorId>(affected.begin(), affected.end());
    }

    std::vector<ActorId> AffairLedger::CloseOngoingBetweenCheaterAndParamour(ActorId a, ActorId b) noexcept {
        std::unordered_set<ActorId> affected;
        auto closeDirection = [&](ActorId cheater, ActorId paramour) {
            const auto it = byCheater_.find(cheater);
            if (it == byCheater_.end()) {
                return;
            }
            for (const AffairId id : it->second) {
                auto itRec = records_.find(id);
                if (itRec != records_.end() &&
                    itRec->second.paramour == paramour &&
                    itRec->second.affairStatus == AffairStatus::Ongoing) {
                    itRec->second.affairStatus = AffairStatus::EndedBreakup;
                    affected.insert(itRec->second.cheater);
                    affected.insert(itRec->second.deceivedPartner);
                }
            }
        };
        closeDirection(a, b);
        closeDirection(b, a);
        return std::vector<ActorId>(affected.begin(), affected.end());
    }

    std::vector<ActorId> AffairLedger::CloseOngoingOnVictimSplit(ActorId a, ActorId b) noexcept {
        std::unordered_set<ActorId> affected;
        const auto it = byCouplePair_.find(CanonicalPairKey{ a, b });
        if (it == byCouplePair_.end()) {
            return {};
        }
        for (const AffairId id : it->second) {
            auto itRec = records_.find(id);
            if (itRec != records_.end() && itRec->second.affairStatus == AffairStatus::Ongoing) {
                itRec->second.affairStatus = AffairStatus::EndedVictimSplit;
                affected.insert(itRec->second.cheater);
                affected.insert(itRec->second.deceivedPartner);
            }
        }
        return std::vector<ActorId>(affected.begin(), affected.end());
    }

    std::vector<ActorId> AffairLedger::EraseAllInvolvingActor(ActorId unregistered) noexcept {
        std::unordered_set<AffairId> toErase;
        auto collectIds = [&](const std::unordered_map<ActorId, std::vector<AffairId>>& map) {
            if (const auto it = map.find(unregistered); it != map.end()) {
                toErase.insert(it->second.begin(), it->second.end());
            }
        };
        collectIds(byCheater_);
        collectIds(byDeceivedPartner_);
        collectIds(byParamour_);

        std::unordered_set<ActorId> affected;
        for (const AffairId id : toErase) {
            const auto itRec = records_.find(id);
            if (itRec == records_.end()) {
                continue;
            }
            const AffairRecord copy = itRec->second;
            affected.insert(copy.cheater);
            affected.insert(copy.deceivedPartner);
            affected.insert(copy.paramour);
            UnindexRecord(copy);
            records_.erase(itRec);
        }
        affected.erase(unregistered);
        return std::vector<ActorId>(affected.begin(), affected.end());
    }

    void AffairLedger::EraseForCouplePair(CanonicalPairKey pair) noexcept {
        const auto it = byCouplePair_.find(pair);
        if (it == byCouplePair_.end()) {
            return;
        }
        const std::vector<AffairId> ids = it->second;
        for (const AffairId id : ids) {
            if (const auto itRec = records_.find(id); itRec != records_.end()) {
                const AffairRecord copy = itRec->second;
                UnindexRecord(copy);
                records_.erase(itRec);
            }
        }
    }

    void AffairLedger::Clear() noexcept {
        nextAffairId_ = 1;
        records_.clear();
        byCouplePair_.clear();
        byCheater_.clear();
        byDeceivedPartner_.clear();
        byParamour_.clear();
    }

    bool AffairLedger::operator==(const AffairLedger& other) const {
        if (records_.size() != other.records_.size() || byCouplePair_.size() != other.byCouplePair_.size()) {
            return false;
        }
        for (const auto& [pair, ids] : byCouplePair_) {
            const auto otherVec = other.GetForCouplePair(pair);
            const auto selfVec = GetForCouplePair(pair);
            if (selfVec.size() != otherVec.size()) {
                return false;
            }
            for (std::size_t i = 0; i < selfVec.size(); ++i) {
                AffairRecord lhs = selfVec[i];
                AffairRecord rhs = otherVec[i];
                lhs.id = AffairId{};
                rhs.id = AffairId{};
                if (!(lhs == rhs)) {
                    return false;
                }
            }
        }
        return true;
    }

    // =========================================================================
    // 4. GuardianSlots & LineageTable
    // =========================================================================

    GuardianSetDiff GuardianSlots::Assign(ActorId newA, ActorId newB) noexcept {
        if (!newA.IsNull() && newA == newB) {
            newB = ActorId{};
        }

        const GuardianSetDiff diff = ComputeGuardianSetDiff(slotA, slotB, newA, newB);
        if (!diff.changed && HasAnyGuardian()) {
            return diff;
        }

        // Preserve surviving guardian's slot position whenever possible
        if (!slotA.IsNull() && slotA == newA) {
            slotB = newB;
        } else if (!slotA.IsNull() && slotA == newB) {
            slotB = newA;
        } else if (!slotB.IsNull() && slotB == newA) {
            slotB = newA;
            slotA = newB;
        } else if (!slotB.IsNull() && slotB == newB) {
            slotA = newA;
            slotB = newB;
        } else {
            slotA = newA;
            slotB = newB;
        }
        return diff;
    }

    GuardianSetDiff GuardianSlots::Remove(ActorId actor) noexcept {
        if (actor.IsNull()) {
            return {};
        }
        const ActorId nextA = (slotA == actor) ? ActorId{} : slotA;
        const ActorId nextB = (slotB == actor) ? ActorId{} : slotB;
        return Assign(nextA, nextB);
    }

    SyntheticChildId LineageTable::AllocateSyntheticChildId() {
        constexpr std::uint32_t kFirst = Foundation::kSyntheticChildMinRawId + 1u;
        constexpr std::uint32_t kLast = Foundation::kSyntheticChildMaxRawId;
        const std::uint32_t poolSpan = (kLast - kFirst) + 1u;

        if (nextSyntheticChildRaw_ < kFirst || nextSyntheticChildRaw_ > kLast) {
            nextSyntheticChildRaw_ = kFirst;
        }

        for (std::uint32_t attempt = 0; attempt < poolSpan; ++attempt) {
            const std::uint32_t candidateRaw = nextSyntheticChildRaw_;
            nextSyntheticChildRaw_ = (candidateRaw < kLast) ? (candidateRaw + 1u) : kFirst;
            const ChildId candidateChild{ candidateRaw };
            if (!children_.contains(candidateChild)) {
                return SyntheticChildId{ candidateRaw };
            }
        }
        return SyntheticChildId{};
    }

    void LineageTable::ObserveLoadedSyntheticId(SyntheticChildId synId) noexcept {
        if (!synId.IsValid()) {
            return;
        }
        if (synId.raw() < Foundation::kSyntheticChildMaxRawId) {
            nextSyntheticChildRaw_ = std::max(nextSyntheticChildRaw_, synId.raw() + 1u);
        } else {
            nextSyntheticChildRaw_ = Foundation::kSyntheticChildMaxRawId;
        }
    }

    const ChildRecord* LineageTable::FindChild(ChildId id) const noexcept {
        if (id.IsNull()) {
            return nullptr;
        }
        const auto it = children_.find(id);
        return it != children_.end() ? &it->second : nullptr;
    }

    ChildRecord* LineageTable::FindChildMutable(ChildId id) noexcept {
        if (id.IsNull()) {
            return nullptr;
        }
        const auto it = children_.find(id);
        return it != children_.end() ? &it->second : nullptr;
    }

    ChildRecord& LineageTable::UpsertChild(ChildRecord record) {
        const ChildId id = record.childId;
        if (const auto syn = id.AsSynthetic()) {
            ObserveLoadedSyntheticId(*syn);
        }
        auto [it, inserted] = children_.insert_or_assign(id, std::move(record));
        (void)inserted;
        return it->second;
    }

    bool LineageTable::EraseChild(ChildId id) noexcept {
        for (auto& [motherId, batch] : activeDeliveryBatches_) {
            (void)motherId;
            auto& born = batch.bornChildIds;
            born.erase(std::remove(born.begin(), born.end(), id), born.end());
        }
        return children_.erase(id) > 0;
    }

    const ActiveDeliveryBatch* LineageTable::FindActiveDeliveryBatch(ActorId mother) const noexcept {
        if (mother.IsNull()) {
            return nullptr;
        }
        const auto it = activeDeliveryBatches_.find(mother);
        return it == activeDeliveryBatches_.end() ? nullptr : &it->second;
    }

    ActiveDeliveryBatch* LineageTable::FindActiveDeliveryBatchMutable(ActorId mother) noexcept {
        if (mother.IsNull()) {
            return nullptr;
        }
        const auto it = activeDeliveryBatches_.find(mother);
        return it == activeDeliveryBatches_.end() ? nullptr : &it->second;
    }

    ActiveDeliveryBatch& LineageTable::UpsertActiveDeliveryBatch(ActorId mother, ActiveDeliveryBatch batch) {
        auto [it, inserted] = activeDeliveryBatches_.insert_or_assign(mother, std::move(batch));
        (void)inserted;
        return it->second;
    }

    bool LineageTable::EraseActiveDeliveryBatch(ActorId mother) noexcept {
        return activeDeliveryBatches_.erase(mother) > 0;
    }

    std::vector<ChildId> LineageTable::GetChildrenOfGuardian(ActorId guardian) const {
        std::vector<ChildId> out;
        if (guardian.IsNull()) {
            return out;
        }
        for (const auto& [id, rec] : children_) {
            if (rec.guardians.HasGuardian(guardian)) {
                out.push_back(id);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<ChildId> LineageTable::GetChildrenWithGuardianSet(ActorId gA, ActorId gB) const {
        std::vector<ChildId> out;
        for (const auto& [id, rec] : children_) {
            if (rec.guardians.HasSameSet(gA, gB)) {
                out.push_back(id);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<ChildId> LineageTable::GetUnbornOfMother(ActorId mother) const {
        std::vector<ChildId> out;
        if (mother.IsNull()) {
            return out;
        }
        for (const auto& [id, rec] : children_) {
            if (rec.stage == ChildStage::Unborn && rec.bioMother == mother) {
                out.push_back(id);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    void LineageTable::AddPendingChild(BaseId childBaseId, const PendingChildEntry& entry) {
        if (childBaseId.IsNull()) {
            return;
        }
        pendingChildrenByBase_[childBaseId] = entry;
    }

    bool LineageTable::ErasePendingChild(BaseId childBaseId) noexcept {
        return pendingChildrenByBase_.erase(childBaseId) > 0;
    }

    void LineageTable::Clear() noexcept {
        nextSyntheticChildRaw_ = Foundation::kSyntheticChildMinRawId + 1u;
        children_.clear();
        pendingChildrenByBase_.clear();
        activeDeliveryBatches_.clear();
    }

    // =========================================================================
    // 5. SpouseHierarchyTable
    // =========================================================================

    int SpouseHierarchyTable::GetRank(ActorId spouse) const noexcept {
        if (spouse.IsNull()) {
            return 4;
        }
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i] == spouse) {
                return static_cast<int>(i);
            }
        }
        return 4;
    }

    ActorId SpouseHierarchyTable::GetSpouseAtSlot(std::size_t slotIndex) const noexcept {
        return slotIndex < slots_.size() ? slots_[slotIndex] : ActorId{};
    }

    void SpouseHierarchyTable::SetRawSlot(std::size_t slotIndex, ActorId spouse) noexcept {
        if (slotIndex < slots_.size()) {
            slots_[slotIndex] = spouse;
        }
    }

    bool SpouseHierarchyTable::SetRank(ActorId spouse, int rank, const std::vector<ActorId>& marriedToPlayerOrder) {
        if (spouse.IsNull()) {
            return false;
        }
        const int cur = GetRank(spouse);
        if (rank < 0 || rank >= static_cast<int>(kSlotCount)) {
            if (cur >= 0 && cur < static_cast<int>(kSlotCount)) {
                for (auto& s : slots_) {
                    if (s == spouse) {
                        s = ActorId{};
                    }
                }
                FillGaps(marriedToPlayerOrder, spouse);
            }
            return true;
        }

        if (cur == rank) {
            return true;
        }

        const ActorId occupant = slots_[static_cast<std::size_t>(rank)];
        if (occupant.IsNull()) {
            if (cur >= 0 && cur < static_cast<int>(kSlotCount)) {
                slots_[static_cast<std::size_t>(cur)] = ActorId{};
            }
            slots_[static_cast<std::size_t>(rank)] = spouse;
            return true;
        }

        if (cur >= 0 && cur < static_cast<int>(kSlotCount)) {
            slots_[static_cast<std::size_t>(cur)] = occupant;
            slots_[static_cast<std::size_t>(rank)] = spouse;
            return true;
        }

        slots_[static_cast<std::size_t>(rank)] = spouse;
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i] == occupant && static_cast<int>(i) != rank) {
                slots_[i] = ActorId{};
            }
        }
        return true;
    }

    void SpouseHierarchyTable::OnSpouseAdded(ActorId spouse) noexcept {
        if (spouse.IsNull() || GetRank(spouse) <= 2) {
            return;
        }
        for (auto& s : slots_) {
            if (s.IsNull()) {
                s = spouse;
                return;
            }
        }
    }

    void SpouseHierarchyTable::OnSpouseRemoved(ActorId spouse, const std::vector<ActorId>& remainingMarriedOrder) {
        bool wasPresent = false;
        for (auto& s : slots_) {
            if (s == spouse) {
                s = ActorId{};
                wasPresent = true;
            }
        }
        if (wasPresent) {
            FillGaps(remainingMarriedOrder, spouse);
        }
    }

    void SpouseHierarchyTable::FillGaps(
        const std::vector<ActorId>& marriedToPlayerOrder,
        ActorId excludeActor,
        const std::function<bool(ActorId)>& isExcludedOrDeceased) {
        const std::unordered_set<ActorId> marriedSet(marriedToPlayerOrder.begin(), marriedToPlayerOrder.end());
        std::vector<ActorId> candidates;
        candidates.reserve(kSlotCount + marriedToPlayerOrder.size());
        std::unordered_set<ActorId> seen;

        auto eligible = [&](ActorId id) -> bool {
            if (id.IsNull() || id == excludeActor || !marriedSet.contains(id)) {
                return false;
            }
            if (isExcludedOrDeceased && isExcludedOrDeceased(id)) {
                return false;
            }
            return true;
        };

        for (const ActorId id : slots_) {
            if (eligible(id) && seen.insert(id).second) {
                candidates.push_back(id);
            }
        }
        for (const ActorId id : marriedToPlayerOrder) {
            if (eligible(id) && seen.insert(id).second) {
                candidates.push_back(id);
            }
        }

        slots_.fill(ActorId{});
        for (std::size_t i = 0; i < kSlotCount && i < candidates.size(); ++i) {
            slots_[i] = candidates[i];
        }
    }

    // =========================================================================
    // 6. HouseholdTable
    // =========================================================================

    bool HouseholdTable::RegisterHouse(HouseId houseId, RefId markerId, bool initialMoveDone) {
        if (houseId.IsNull()) {
            return false;
        }
        if (auto* existing = FindHouseMutable(houseId)) {
            existing->markerId = markerId;
            return true;
        }
        PlayerHouseRecord rec{};
        rec.houseId = houseId;
        rec.markerId = markerId;
        rec.initialMoveDone = initialMoveDone;
        houses_.push_back(std::move(rec));
        return true;
    }

    const PlayerHouseRecord* HouseholdTable::FindHouse(HouseId houseId) const noexcept {
        for (const auto& h : houses_) {
            if (h.houseId == houseId) {
                return &h;
            }
        }
        return nullptr;
    }

    PlayerHouseRecord* HouseholdTable::FindHouseMutable(HouseId houseId) noexcept {
        for (auto& h : houses_) {
            if (h.houseId == houseId) {
                return &h;
            }
        }
        return nullptr;
    }

    bool HouseholdTable::AssignTenant(
        std::uint32_t tenantRawId,
        HouseId houseId,
        bool stayAtHome,
        bool isChildTenant) {
        if (tenantRawId == 0 || tenantRawId == Foundation::kPlayerRefRawId || houseId.IsNull()) {
            return false;
        }
        auto* dstHouse = FindHouseMutable(houseId);
        if (!dstHouse) {
            return false;
        }

        // Remove from previous house if reassigned
        if (const auto itPrev = tenants_.find(tenantRawId); itPrev != tenants_.end()) {
            if (itPrev->second.houseId != houseId) {
                if (auto* prevHouse = FindHouseMutable(itPrev->second.houseId)) {
                    EraseValue(prevHouse->tenantOrder, tenantRawId);
                }
            }
        }

        PushUnique(dstHouse->tenantOrder, tenantRawId);
        tenants_[tenantRawId] = TenantAssignment{ houseId, stayAtHome };

        // Keep ChildHouseholdRecord::currentHouseId normalized in lockstep
        const ChildId maybeChildId{ tenantRawId };
        if (isChildTenant || childStates_.contains(maybeChildId) || maybeChildId.IsSynthetic()) {
            auto& childState = childStates_[maybeChildId];
            childState.childId = maybeChildId;
            childState.currentHouseId = houseId;
        }
        return true;
    }

    bool HouseholdTable::EvictTenant(std::uint32_t tenantRawId) noexcept {
        if (tenantRawId == 0) {
            return false;
        }
        bool removed = false;
        if (const auto it = tenants_.find(tenantRawId); it != tenants_.end()) {
            if (auto* house = FindHouseMutable(it->second.houseId)) {
                EraseValue(house->tenantOrder, tenantRawId);
            }
            tenants_.erase(it);
            removed = true;
        } else {
            for (auto& house : houses_) {
                const std::size_t before = house.tenantOrder.size();
                EraseValue(house.tenantOrder, tenantRawId);
                if (house.tenantOrder.size() != before) {
                    removed = true;
                }
            }
        }

        // Always clear ChildHouseholdRecord::currentHouseId so housing never split-brains
        if (auto itChild = childStates_.find(ChildId{ tenantRawId }); itChild != childStates_.end()) {
            if (!itChild->second.currentHouseId.IsNull()) {
                itChild->second.currentHouseId = HouseId{};
                itChild->second.homeMarkerId = RefId{};
                removed = true;
            }
        }
        return removed;
    }

    HouseId HouseholdTable::GetTenantHouse(std::uint32_t tenantRawId) const noexcept {
        const auto it = tenants_.find(tenantRawId);
        return it != tenants_.end() ? it->second.houseId : HouseId{};
    }

    bool HouseholdTable::GetTenantStayAtHome(std::uint32_t tenantRawId) const noexcept {
        const auto it = tenants_.find(tenantRawId);
        return it != tenants_.end() ? it->second.stayAtHome : true;
    }

    void HouseholdTable::SetTenantStayAtHome(std::uint32_t tenantRawId, bool stayAtHome) noexcept {
        if (auto it = tenants_.find(tenantRawId); it != tenants_.end()) {
            it->second.stayAtHome = stayAtHome;
        }
    }

    SharedCellRecord& HouseholdTable::UpsertSharedCell(CellId cellId, SharedCellRecord record) {
        auto [it, inserted] = sharedCells_.insert_or_assign(cellId, std::move(record));
        (void)inserted;
        return it->second;
    }

    bool HouseholdTable::StopSharingForSpouse(ActorId spouse) noexcept {
        if (spouse.IsNull()) {
            return false;
        }
        bool changed = false;
        for (auto it = sharedCells_.begin(); it != sharedCells_.end();) {
            if (it->second.sharingSpouses.erase(spouse) > 0) {
                changed = true;
                it->second.sharedWithPlayer = !it->second.sharingSpouses.empty();
                if (!it->second.sharedWithPlayer) {
                    it = sharedCells_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        return changed;
    }

    bool HouseholdTable::IsCellSharedWithPlayer(CellId cellId) const noexcept {
        const auto it = sharedCells_.find(cellId);
        return it != sharedCells_.end() && it->second.sharedWithPlayer;
    }

    const ChildHouseholdRecord* HouseholdTable::FindChildState(ChildId childId) const noexcept {
        const auto it = childStates_.find(childId);
        return it != childStates_.end() ? &it->second : nullptr;
    }

    ChildHouseholdRecord* HouseholdTable::FindChildStateMutable(ChildId childId) noexcept {
        const auto it = childStates_.find(childId);
        return it != childStates_.end() ? &it->second : nullptr;
    }

    ChildHouseholdRecord& HouseholdTable::UpsertChildState(ChildHouseholdRecord record) {
        const ChildId id = record.childId;
        auto [it, inserted] = childStates_.insert_or_assign(id, std::move(record));
        (void)inserted;
        return it->second;
    }

    bool HouseholdTable::EraseChildState(ChildId childId) noexcept {
        return childStates_.erase(childId) > 0;
    }

    bool HouseholdTable::HasAnyPlc1State() const noexcept {
        if (poorCounter_ > 0 || nameCallingUsedLast_) {
            return true;
        }
        for (const auto& [cId, st] : childStates_) {
            (void)cId;
            if (!st.transientCritterId.IsNull()) {
                return true;
            }
        }
        return false;
    }

    void HouseholdTable::ClearOnlyPlayerChildrenRecords() noexcept {
        childChestIntroduced_ = false;
        poorCounter_ = 0;
        nameCallingUsedLast_ = false;
        childStates_.clear();
    }

    void HouseholdTable::UpsertChildFactionSnapshot(ChildId childId, ChildFactionSnapshotRecord snapshot) {
        if (!childId.IsNull()) {
            childFactionSnapshots_.insert_or_assign(childId, std::move(snapshot));
        }
    }

    bool HouseholdTable::EraseChildFactionSnapshot(ChildId childId) noexcept {
        return childFactionSnapshots_.erase(childId) > 0;
    }

    void HouseholdTable::UpsertAdultChildTravel(ChildId childId, AdultChildTravelRecord record) {
        if (!childId.IsNull()) {
            adultChildTravel_.insert_or_assign(childId, std::move(record));
        }
    }

    bool HouseholdTable::EraseAdultChildTravel(ChildId childId) noexcept {
        return adultChildTravel_.erase(childId) > 0;
    }

    const AdultChildTravelRecord* HouseholdTable::FindAdultChildTravel(ChildId childId) const noexcept {
        const auto it = adultChildTravel_.find(childId);
        return it != adultChildTravel_.end() ? &it->second : nullptr;
    }

    void HouseholdTable::Clear() noexcept {
        houses_.clear();
        tenants_.clear();
        sharedCells_.clear();
        originalBedOwners_.clear();
        childChestIntroduced_ = false;
        poorCounter_ = 0;
        nameCallingUsedLast_ = false;
        childStates_.clear();
        childFactionSnapshots_.clear();
        adultChildTravel_.clear();
    }

    // =========================================================================
    // 7. AffectionTable
    // =========================================================================

    AffectionTable::AffectionTable() {
        ResetDefaultMinMax();
    }

    std::string AffectionTable::NormalizeTypeKey(std::string_view rawType) {
        std::string lower;
        lower.reserve(rawType.size());
        for (const char ch : rawType) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
        }
        return lower;
    }

    void AffectionTable::ResetDefaultMinMax() {
        minMaxByType_ = {
            { "intimacy", { -22, 22 } },
            { "gift", { 0, 15 } },
            { "dialoguestarted", { 0, 5 } },
            { "slept", { 0, 8 } },
            { "promotion", { -24, 24 } },
            { "loneliness", { -16, 6 } },
            { "family", { 0, 6 } },
        };
    }

    void AffectionTable::SetPermanent(ActorId actor, int score) {
        if (!actor.IsNull() && !actor.IsSyntheticChild()) {
            permanent_[actor] = ClampScore(score);
        }
    }

    int AffectionTable::GetPermanent(ActorId actor, int defaultVal) const noexcept {
        const auto it = permanent_.find(actor);
        return it != permanent_.end() ? it->second : defaultVal;
    }

    void AffectionTable::SetLastInteractionDay(ActorId actor, float gameDay) {
        if (!actor.IsNull() && !actor.IsSyntheticChild()) {
            lastDay_[actor] = gameDay;
        }
    }

    std::optional<float> AffectionTable::GetLastInteractionDay(ActorId actor) const noexcept {
        const auto it = lastDay_.find(actor);
        if (it != lastDay_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void AffectionTable::SetDailyAccumulation(ActorId actor, std::string_view type, float amount) {
        if (actor.IsNull() || actor.IsSyntheticChild() || std::isnan(amount) || std::isinf(amount)) {
            return;
        }
        const std::string key = NormalizeTypeKey(type);
        if (!key.empty()) {
            daily_[actor][key] = amount;
        }
    }

    float AffectionTable::GetDailyAccumulation(ActorId actor, std::string_view type) const {
        const auto itActor = daily_.find(actor);
        if (itActor == daily_.end()) {
            return 0.0f;
        }
        const std::string key = NormalizeTypeKey(type);
        const auto itType = itActor->second.find(key);
        return itType != itActor->second.end() ? itType->second : 0.0f;
    }

    void AffectionTable::SetMinMaxForType(std::string_view type, int minVal, int maxVal) {
        const std::string key = NormalizeTypeKey(type);
        if (!key.empty()) {
            if (minVal > maxVal) {
                std::swap(minVal, maxVal);
            }
            minMaxByType_[key] = { minVal, maxVal };
        }
    }

    std::optional<std::pair<int, int>> AffectionTable::GetMinMaxForType(std::string_view type) const {
        const std::string key = NormalizeTypeKey(type);
        const auto it = minMaxByType_.find(key);
        if (it != minMaxByType_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void AffectionTable::EraseActor(ActorId actor) noexcept {
        permanent_.erase(actor);
        lastDay_.erase(actor);
        daily_.erase(actor);
    }

    void AffectionTable::Clear() {
        permanent_.clear();
        lastDay_.clear();
        daily_.clear();
        ResetDefaultMinMax();
    }

    // =========================================================================
    // 8. DeceasedTable
    // =========================================================================

    bool DeceasedTable::UpsertRecord(DeceasedActorRecord record) {
        // Dynamic 0xFFxxxxxx spawned/cloned actors MUST never key under a shared template BaseId
        if (record.refId.IsDynamicRuntimeRef()) {
            record.baseId = BaseId{ record.refId.raw() };
        } else if (record.baseId.IsNull() && !record.refId.IsNull()) {
            record.baseId = BaseId{ record.refId.raw() };
        }

        const std::uint32_t primaryKey = ComputePrimaryKey(record.baseId, record.refId);
        if (primaryKey == 0) {
            return false;
        }

        if (!record.refId.IsNull()) {
            refToKey_[record.refId] = primaryKey;
        }
        recordsByKey_.insert_or_assign(primaryKey, std::move(record));
        return true;
    }

    const DeceasedActorRecord* DeceasedTable::FindByActor(
        ActorId actor,
        BaseId optionalKnownBase) const noexcept {
        if (actor.IsNull()) {
            return nullptr;
        }

        // Direct key match
        if (const auto itDirect = recordsByKey_.find(actor.raw()); itDirect != recordsByKey_.end()) {
            return &itDirect->second;
        }

        // RefId -> primaryKey match
        if (actor.HasPhysicalActor()) {
            const RefId ref = actor.AsRefId();
            if (const auto itRef = refToKey_.find(ref); itRef != refToKey_.end()) {
                if (const auto itRec = recordsByKey_.find(itRef->second); itRec != recordsByKey_.end()) {
                    return &itRec->second;
                }
            }

            // Dynamic 0xFFxxxxxx actors (e.g., FMR template children / cloned Stage 4 adults) NEVER fall back to shared BaseId!
            if (ref.IsDynamicRuntimeRef()) {
                return nullptr;
            }
        }

        if (!optionalKnownBase.IsNull()) {
            if (const auto itBase = recordsByKey_.find(optionalKnownBase.raw()); itBase != recordsByKey_.end()) {
                return &itBase->second;
            }
        }
        return nullptr;
    }

    bool DeceasedTable::IsWidowOrWidower(ActorId survivingActor) const noexcept {
        if (survivingActor.IsNull()) {
            return false;
        }
        for (const auto& [key, rec] : recordsByKey_) {
            (void)key;
            if (rec.isChild) {
                continue;
            }
            for (const auto& partner : rec.partnersAtDeath) {
                if (partner.statusAtDeath == RelationshipStatus::Married &&
                    (partner.partnerRefId.raw() == survivingActor.raw() ||
                     partner.partnerBaseId.raw() == survivingActor.raw() ||
                     (survivingActor.IsPlayer() && partner.isPlayer))) {
                    return true;
                }
            }
        }
        return false;
    }

    void DeceasedTable::Clear() noexcept {
        recordsByKey_.clear();
        refToKey_.clear();
    }

    // =========================================================================
    // 9. CooldownTable
    // =========================================================================

    void CooldownTable::EraseActor(ActorId actor) noexcept {
        npcLastMatchmakingChangeDay.erase(actor);
        npcBrokeupTime.erase(actor);
        npcLastTimeSharedIncome.erase(actor);
        movedSceneActors.erase(actor);
        EraseValue(skyrimNetMutedActors, actor);
        for (auto it = banterCoupleCooldowns.begin(); it != banterCoupleCooldowns.end();) {
            if (it->first.Contains(actor)) {
                it = banterCoupleCooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }

}  // namespace MARAS::State
