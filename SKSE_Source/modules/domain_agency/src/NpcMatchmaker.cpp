#include "maras/domain/agency/NpcMatchmaker.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <unordered_map>

namespace MARAS::Domain::Agency {

    namespace {
        struct WeightedTemplate {
            MatchmakingActionSlot slot{};
            int weight{ 0 };
        };

        [[nodiscard]] bool HasAnyMainPartner(const State::RelationshipEdgeTable& rels, ActorId actorId) noexcept {
            for (const auto& [partnerId, st] : rels.GetActivePartners(actorId)) {
                (void)partnerId;
                if (Foundation::IsMainTierStatus(st)) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    std::vector<MatchmakingActionSlot> RollActionSlots(
        const State::SettingsData& settings,
        int maxSlots,
        Foundation::IRng& rng) {
        std::vector<MatchmakingActionSlot> result;
        if (maxSlots <= 0) {
            return result;
        }

        std::vector<WeightedTemplate> pool = {
            { { MatchmakingActionKind::New, RelationshipStatus::Dating, false, false },
              std::max(0, settings.diceRollChanceNewDating) },
            { { MatchmakingActionKind::New, RelationshipStatus::Engaged, false, false },
              std::max(0, settings.diceRollChanceNewEngaged) },
            { { MatchmakingActionKind::New, RelationshipStatus::Lover, false, false },
              std::max(0, settings.diceRollChanceNewLoverAllowed) },
            { { MatchmakingActionKind::New, RelationshipStatus::Lover, true, false },
              settings.matchmakingAllowCheating ? std::max(0, settings.diceRollChanceNewLoverCheating) : 0 },
            { { MatchmakingActionKind::Advance, RelationshipStatus::Unknown, false, false },
              std::max(0, settings.diceRollChanceAdvance) },
            { { MatchmakingActionKind::Breakup, RelationshipStatus::Unknown, false, false },
              settings.matchmakingAllowBreakups ? std::max(0, settings.diceRollChanceBreakup) : 0 },
            { { MatchmakingActionKind::OpenClose, RelationshipStatus::Unknown, false, false },
              settings.matchmakingAllowOpenClose ? std::max(0, settings.diceRollChanceOpenClose) : 0 }
        };

        std::array<int, 4> counts{};
        for (int i = 0; i < maxSlots; ++i) {
            int totalWeight = 0;
            for (const auto& item : pool) {
                const auto kindIdx = static_cast<std::size_t>(item.slot.action);
                if (counts[kindIdx] < 3 && item.weight > 0) {
                    totalWeight += item.weight;
                }
            }
            if (totalWeight <= 0) {
                break;
            }

            const int roll = rng.NextInt(0, totalWeight - 1);
            int accum = 0;
            for (const auto& item : pool) {
                const auto kindIdx = static_cast<std::size_t>(item.slot.action);
                if (counts[kindIdx] >= 3 || item.weight <= 0) {
                    continue;
                }
                accum += item.weight;
                if (roll < accum) {
                    result.push_back(item.slot);
                    ++counts[kindIdx];
                    break;
                }
            }
        }
        return result;
    }

    std::optional<MatchmakingProposal> FindEligiblePairForSlot(
        const State::WorldStateSnapshot& snap,
        const std::vector<NpcMatchmakingProfile>& npcs,
        const MatchmakingActionSlot& slot,
        float currentGameDay,
        Foundation::IRng& rng) {
        if (!snap.relationships || !snap.settings) {
            return std::nullopt;
        }

        const auto& rels = *snap.relationships;
        const auto orientation = snap.settings->matchmakingOrientation;

        if (slot.action == MatchmakingActionKind::New) {
            std::vector<const NpcMatchmakingProfile*> eligible;
            eligible.reserve(npcs.size());
            for (const auto& p : npcs) {
                if (IsEligibleForScheduledMatchmaking(snap, p.actorId, currentGameDay)) {
                    eligible.push_back(&p);
                }
            }
            if (eligible.size() < 2u) {
                return std::nullopt;
            }

            std::vector<MatchmakingProposal> validPairs;
            for (std::size_t i = 0; i < eligible.size(); ++i) {
                for (std::size_t j = i + 1; j < eligible.size(); ++j) {
                    const auto* a = eligible[i];
                    const auto* b = eligible[j];
                    if (!IsOrientationCompatible(orientation, a->sex, b->sex)) {
                        continue;
                    }
                    const auto cur = rels.GetStatus(a->actorId, b->actorId);
                    if (Foundation::IsActivePartnerStatus(cur)) {
                        continue;
                    }

                    const bool aHasMain = HasAnyMainPartner(rels, a->actorId);
                    const bool bHasMain = HasAnyMainPartner(rels, b->actorId);
                    if (slot.targetStatus == RelationshipStatus::Lover) {
                        const bool aExclusiveMain = aHasMain && !rels.HasAnyOpenMainPair(a->actorId);
                        const bool bExclusiveMain = bHasMain && !rels.HasAnyOpenMainPair(b->actorId);
                        const bool involvesCheating = aExclusiveMain || bExclusiveMain;
                        if (slot.isCheating && !involvesCheating) {
                            continue;
                        }
                        if (!slot.isCheating && involvesCheating) {
                            continue;
                        }
                        if (involvesCheating && !snap.settings->matchmakingAllowCheating) {
                            continue;
                        }
                    } else {
                        if (aHasMain || bHasMain) {
                            continue;
                        }
                    }

                    MatchmakingProposal prop{};
                    prop.slot = slot;
                    prop.actorA = a->actorId;
                    prop.actorB = b->actorId;
                    prop.fromStatus = cur;
                    validPairs.push_back(prop);
                }
            }

            if (validPairs.empty()) {
                return std::nullopt;
            }
            const auto idx = static_cast<std::size_t>(
                rng.NextInt(0, static_cast<std::int32_t>(validPairs.size() - 1)));
            return validPairs[idx];
        }

        std::vector<MatchmakingProposal> existingCandidates;
        for (const auto& [pairKey, edge] : rels.AllEdges()) {
            const ActorId a = pairKey.first;
            const ActorId b = pairKey.second;
            if (a.IsPlayer() || b.IsPlayer() || !Foundation::IsActivePartnerStatus(edge.status)) {
                continue;
            }
            if (!IsEligibleForScheduledMatchmaking(snap, a, currentGameDay) ||
                !IsEligibleForScheduledMatchmaking(snap, b, currentGameDay)) {
                continue;
            }

            if (slot.action == MatchmakingActionKind::Advance) {
                RelationshipStatus nextStatus = RelationshipStatus::Unknown;
                if (edge.status == RelationshipStatus::Dating) {
                    nextStatus = RelationshipStatus::Engaged;
                } else if (edge.status == RelationshipStatus::Engaged) {
                    nextStatus = RelationshipStatus::Married;
                } else if (edge.status == RelationshipStatus::Lover &&
                           !HasAnyMainPartner(rels, a) && !HasAnyMainPartner(rels, b)) {
                    nextStatus = RelationshipStatus::Dating;
                }
                if (nextStatus != RelationshipStatus::Unknown) {
                    MatchmakingProposal prop{};
                    prop.slot = slot;
                    prop.slot.targetStatus = nextStatus;
                    prop.actorA = a;
                    prop.actorB = b;
                    prop.fromStatus = edge.status;
                    existingCandidates.push_back(prop);
                }
            } else if (slot.action == MatchmakingActionKind::Breakup && snap.settings->matchmakingAllowBreakups) {
                RelationshipStatus nextStatus = RelationshipStatus::ExLover;
                if (edge.status == RelationshipStatus::Married) {
                    nextStatus = RelationshipStatus::Divorced;
                } else if (edge.status == RelationshipStatus::Engaged) {
                    nextStatus = RelationshipStatus::Jilted;
                } else if (edge.status == RelationshipStatus::Dating) {
                    nextStatus = RelationshipStatus::ExDating;
                }
                MatchmakingProposal prop{};
                prop.slot = slot;
                prop.slot.targetStatus = nextStatus;
                prop.actorA = a;
                prop.actorB = b;
                prop.fromStatus = edge.status;
                existingCandidates.push_back(prop);
            } else if (slot.action == MatchmakingActionKind::OpenClose &&
                       snap.settings->matchmakingAllowOpenClose &&
                       Foundation::IsMainTierStatus(edge.status)) {
                MatchmakingProposal prop{};
                prop.slot = slot;
                prop.slot.targetStatus = edge.status;
                prop.slot.openTarget = !edge.isOpen;
                prop.actorA = a;
                prop.actorB = b;
                prop.fromStatus = edge.status;
                existingCandidates.push_back(prop);
            }
        }

        if (existingCandidates.empty()) {
            return std::nullopt;
        }
        const auto idx = static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(existingCandidates.size() - 1)));
        return existingCandidates[idx];
    }

    std::vector<MatchmakingProposal> GenerateDiceRollProposals(
        const State::WorldStateSnapshot& snap,
        const std::vector<NpcMatchmakingProfile>& npcs,
        float currentGameDay,
        Foundation::IRng& rng) {
        std::vector<MatchmakingProposal> out;
        if (!snap.settings || !snap.settings->matchmakingEnabled) {
            return out;
        }
        const auto slots = RollActionSlots(*snap.settings, snap.settings->matchmakingMaxSuggestions, rng);
        out.reserve(slots.size());
        for (const auto& slot : slots) {
            if (auto proposal = FindEligiblePairForSlot(snap, npcs, slot, currentGameDay, rng)) {
                out.push_back(std::move(*proposal));
            }
        }
        return out;
    }

}  // namespace MARAS::Domain::Agency
