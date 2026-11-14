#include "maras/domain/agency/BanterPlanner.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace MARAS::Domain::Agency {

    namespace {
        inline constexpr float kTieEpsilon = 1e-4f;
    }  // namespace

    bool IsBanterQualifyingStatus(RelationshipStatus status) noexcept {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Lover ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married;
    }

    bool IsLocationKeywordAllowedForScenes(
        const State::SettingsData& settings,
        std::string_view keywordEditorId) noexcept {
        if (keywordEditorId.empty()) {
            return true;
        }
        using Foundation::StringUtils::CaseInsensitiveEquals;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeTemple")) return settings.enableScenes_LocTypeTemple;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeStore")) return settings.enableScenes_LocTypeStore;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeStewardsDwelling")) return settings.enableScenes_LocTypeStewardsDwelling;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeInn")) return settings.enableScenes_LocTypeInn;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeHouse")) return settings.enableScenes_LocTypeHouse;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeBarracks")) return settings.enableScenes_LocTypeBarracks;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeGuild")) return settings.enableScenes_LocTypeGuild;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypePlayerHouse")) return settings.enableScenes_LocTypePlayerHouse;
        if (CaseInsensitiveEquals(keywordEditorId, "BYOH_LocTypeHomestead")) return settings.enableScenes_BYOH_LocTypeHomestead;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeTown")) return settings.enableScenes_LocTypeTown;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeSettlement")) return settings.enableScenes_LocTypeSettlement;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeOrcStronghold")) return settings.enableScenes_LocTypeOrcStronghold;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeCity")) return settings.enableScenes_LocTypeCity;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeLumberMill")) return settings.enableScenes_LocTypeLumberMill;
        if (CaseInsensitiveEquals(keywordEditorId, "LocTypeFarm")) return settings.enableScenes_LocTypeFarm;
        return true;
    }

    bool AreAllLocationKeywordsAllowedForScenes(
        const State::SettingsData& settings,
        std::span<const std::string_view> keywordEditorIds) noexcept {
        for (std::string_view kw : keywordEditorIds) {
            if (!IsLocationKeywordAllowedForScenes(settings, kw)) {
                return false;
            }
        }
        return true;
    }

    bool IsCoupleOnBanterCooldown(
        const State::WorldStateSnapshot& world,
        ActorId actorA,
        ActorId actorB,
        float currentGameDay) noexcept {
        if (!world.cooldowns || !world.settings || actorA.IsNull() || actorB.IsNull()) {
            return false;
        }
        const float cooldownDays = (std::max(0.0f, world.settings->scenesCooldownHours) * 2.0f) / 24.0f;
        if (cooldownDays <= 0.0f) {
            return false;
        }
        const Foundation::CanonicalPairKey key(actorA, actorB);
        const auto it = world.cooldowns->banterCoupleCooldowns.find(key);
        if (it == world.cooldowns->banterCoupleCooldowns.end() || it->second < 0.0f) {
            return false;
        }
        return (currentGameDay - it->second) < cooldownDays;
    }

    std::vector<BanterCandidate> FindBanterCoupleCandidates(
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial,
        float scanRadius,
        float currentGameDay) {
        std::vector<BanterCandidate> result;
        if (!world.settings || !world.relationships || !world.settings->enableScenes || scanRadius <= 0.0f) {
            return result;
        }
        const auto& settings = *world.settings;

        std::vector<ActorId> nearbyActors;
        std::unordered_set<ActorId> nearbySet;
        spatial.ForEachNearbyCandidate(State::RefId::Player(), scanRadius, [&](const State::LoadedActorSample& s) {
            if (!s.Is3DLoaded() || s.IsDead() || s.IsDeleted() || s.IsDisabled() ||
                s.IsPlayer() || s.IsChild() || s.IsGhost() || s.IsPlayerTeammate() ||
                s.IsInCombat() || s.IsInScene()) {
                return;
            }
            const ActorId aid{ s.refId.raw() };
            if (world.deceased && world.deceased->IsDeceased(aid, s.baseId)) {
                return;
            }
            if (Foundation::IsMainTierStatus(world.relationships->GetStatus(ActorId::Player(), aid))) {
                return;
            }
            if (nearbySet.insert(aid).second) {
                nearbyActors.push_back(aid);
            }
        });

        std::unordered_set<Foundation::CanonicalPairKey, Foundation::CanonicalPairKeyHash> seenPairs;
        for (ActorId actorA : nearbyActors) {
            for (const auto& pairKey : world.relationships->GetAdjacentPairs(actorA)) {
                const auto* edge = world.relationships->FindEdge(pairKey);
                if (!edge || !IsBanterQualifyingStatus(edge->status)) {
                    continue;
                }
                const ActorId actorB = pairKey.Other(actorA);
                if (actorB.IsNull() || actorB.IsPlayer()) {
                    continue;
                }
                if (Foundation::IsMainTierStatus(world.relationships->GetStatus(ActorId::Player(), actorB))) {
                    continue;
                }
                if (world.deceased && world.deceased->IsDeceased(actorB)) {
                    continue;
                }
                if (!seenPairs.insert(pairKey).second) {
                    continue;
                }

                bool statusEnabled = false;
                switch (edge->status) {
                    case RelationshipStatus::Lover:   statusEnabled = settings.enableScenes_Lover; break;
                    case RelationshipStatus::Dating:  statusEnabled = settings.enableScenes_Dating; break;
                    case RelationshipStatus::Engaged: statusEnabled = settings.enableScenes_Engaged; break;
                    case RelationshipStatus::Married: statusEnabled = settings.enableScenes_Married; break;
                    default: break;
                }
                if (!statusEnabled) {
                    continue;
                }
                if (edge->isOpen && !settings.enableScenes_OpenRelationship) {
                    continue;
                }

                const bool aCheating = world.affairs && world.affairs->HasCheatedOn(actorA, actorB);
                const bool bCheating = world.affairs && world.affairs->HasCheatedOn(actorB, actorA);
                const bool isCheating = aCheating || bCheating;
                if (isCheating && !settings.enableScenes_Cheating) {
                    continue;
                }

                if (IsCoupleOnBanterCooldown(world, actorA, actorB, currentGameDay)) {
                    continue;
                }

                const bool bIsNear = nearbySet.contains(actorB);
                if (bIsNear) {
                    if (const auto* sampleB = spatial.Find(actorB.AsRefId())) {
                        if (sampleB->IsInScene() || sampleB->IsInCombat()) {
                            continue;
                        }
                    }
                }

                float lastPlayed = -1.0f;
                if (world.cooldowns) {
                    if (const auto it = world.cooldowns->banterCoupleCooldowns.find(pairKey);
                        it != world.cooldowns->banterCoupleCooldowns.end()) {
                        lastPlayed = it->second;
                    }
                }

                BanterCandidate cand{};
                cand.actorA = actorA;
                cand.actorB = actorB;
                cand.tier = bIsNear ? 1 : 2;
                cand.lastPlayedDay = lastPlayed;
                cand.isOpen = edge->isOpen;
                cand.isCheating = isCheating;
                result.push_back(cand);
            }
        }

        return result;
    }

    std::optional<BanterCandidate> SelectPrioritizedCouple(
        const std::vector<BanterCandidate>& candidates,
        Foundation::IRng& rng) {
        if (candidates.empty()) {
            return std::nullopt;
        }

        int bestTier = std::numeric_limits<int>::max();
        for (const auto& c : candidates) {
            bestTier = std::min(bestTier, c.tier);
        }

        std::vector<const BanterCandidate*> tierPool;
        for (const auto& c : candidates) {
            if (c.tier == bestTier) {
                tierPool.push_back(&c);
            }
        }
        if (tierPool.empty()) {
            return std::nullopt;
        }

        std::vector<const BanterCandidate*> neverPlayed;
        float oldestDay = std::numeric_limits<float>::max();
        for (const auto* c : tierPool) {
            if (c->lastPlayedDay < 0.0f) {
                neverPlayed.push_back(c);
            } else {
                oldestDay = std::min(oldestDay, c->lastPlayedDay);
            }
        }

        if (!neverPlayed.empty()) {
            const auto idx = static_cast<std::size_t>(
                rng.NextInt(0, static_cast<std::int32_t>(neverPlayed.size() - 1)));
            return *neverPlayed[idx];
        }

        std::vector<const BanterCandidate*> leastRecent;
        for (const auto* c : tierPool) {
            if (std::abs(c->lastPlayedDay - oldestDay) <= kTieEpsilon) {
                leastRecent.push_back(c);
            }
        }
        const auto idx = static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(leastRecent.size() - 1)));
        return *leastRecent[idx];
    }

}  // namespace MARAS::Domain::Agency
