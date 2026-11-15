#include "maras/domain/agency/BanterPlanner.h"

#include "maras/domain/relationships/InfidelityRules.h"
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

                const bool isCheating = Domain::Relationships::IsRelationshipCheating(world, actorA, actorB);
                const bool isOpen = edge->isOpen ||
                                    world.relationships->IsInAnyOpenRelationship(actorA) ||
                                    world.relationships->IsInAnyOpenRelationship(actorB);

                if (isCheating) {
                    if (!settings.enableScenes_Cheating &&
                        !settings.enableScenes_CheatingDiscovered &&
                        !settings.enableScenes_CheatingCaught) {
                        continue;
                    }
                } else if (isOpen) {
                    if (!settings.enableScenes_OpenRelationship) {
                        continue;
                    }
                } else {
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
                cand.isOpen = isOpen;
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

    std::int32_t SelectCheatingSceneKindWithFallbacks(
        const State::WorldStateSnapshot& world,
        ActorId npc1,
        ActorId npc2,
        const CheatingSceneEnvironment& env,
        Foundation::IRng& rng) {
        if (!world.settings || npc1.IsNull() || npc2.IsNull()) {
            return -1;
        }
        const auto& settings = *world.settings;
        const bool allowSecret = settings.enableScenes_Cheating;
        const bool allowDiscovered = settings.enableScenes_CheatingDiscovered;
        const bool allowCaught = env.allowCaught && settings.enableScenes_CheatingCaught;

        if (!allowSecret && !allowDiscovered && !allowCaught) {
            return -1;
        }

        const bool isPlayerVictim =
            world.affairs &&
            (world.affairs->HasCheatedOn(npc1, ActorId::Player()) ||
             world.affairs->HasCheatedOn(npc2, ActorId::Player()));

        const bool canPlayCaught =
            env.hasSexFramework && env.isInterior && !isPlayerVictim && env.hasBedsNearby;

        std::int32_t typeOfCheatingScene =
            canPlayCaught ? rng.NextInt(0, 2) : rng.NextInt(0, 1);

        if (isPlayerVictim) {
            typeOfCheatingScene = 0;
        }

        if (typeOfCheatingScene == 2 && !allowCaught) {
            if (allowDiscovered && !isPlayerVictim) {
                typeOfCheatingScene = 1;
            } else if (allowSecret) {
                typeOfCheatingScene = 0;
            } else {
                return -1;
            }
        }
        if (typeOfCheatingScene == 1 && !allowDiscovered) {
            if (allowSecret) {
                typeOfCheatingScene = 0;
            } else if (allowCaught && canPlayCaught) {
                typeOfCheatingScene = 2;
            } else {
                return -1;
            }
        }
        if (typeOfCheatingScene == 0 && !allowSecret) {
            if (allowDiscovered && !isPlayerVictim) {
                typeOfCheatingScene = 1;
            } else if (allowCaught && canPlayCaught) {
                typeOfCheatingScene = 2;
            } else {
                return -1;
            }
        }

        return typeOfCheatingScene;
    }

    std::int32_t SelectCheatingSceneVariantIndex(
        const State::WorldStateSnapshot& world,
        ActorId npc1,
        ActorId npc2,
        std::int32_t cheatingType,
        std::int32_t initialSceneIdx,
        Foundation::IRng& rng) {
        if (cheatingType == 1) {
            return rng.NextInt(0, 5);
        }
        if (cheatingType == 2) {
            return rng.NextInt(0, 4);
        }

        const bool isNpc1Cheating = Domain::Relationships::IsActorCheating(world, npc1);
        const bool isNpc2Cheating = Domain::Relationships::IsActorCheating(world, npc2);
        if (isNpc1Cheating && isNpc2Cheating) {
            return 4;
        }
        if (initialSceneIdx == 4 || initialSceneIdx < 0 || initialSceneIdx > 3) {
            return rng.NextInt(0, 3);
        }
        return initialSceneIdx;
    }

    BanterSceneSetupPlan PlanBanterSceneSetup(
        const State::WorldStateSnapshot& world,
        const BanterSceneSetupInput& input,
        Foundation::IRng& rng) {
        BanterSceneSetupPlan plan{};
        if (!world.settings || !world.relationships || input.npc1.IsNull() || input.npc2.IsNull()) {
            return plan;
        }

        ActorId npc1 = input.npc1;
        ActorId npc2 = input.npc2;
        std::int32_t sceneIdx = rng.NextInt(0, 4);
        const bool isForced = !input.forcedScenario.empty();
        plan.isForced = isForced;

        if (isForced) {
            plan.npc1 = npc1;
            plan.npc2 = npc2;
            plan.npc1MainPartner = ActorId::Player();

            if (input.forcedScenario == "cheating_talking_in_house" ||
                input.forcedScenario == "cheating_talking_letter") {
                plan.shouldExposeNpc1Affair = true;
                plan.sceneKind = BanterSceneKind::CheatingSecret;
                plan.sceneIdx = SelectCheatingSceneVariantIndex(world, npc1, npc2, 0, sceneIdx, rng);
                if (Domain::Relationships::IsActorCheating(world, npc2)) {
                    plan.npc2MainPartner = world.relationships->GetPrimaryPartner(npc2);
                }
            } else if (input.forcedScenario == "cheating_physical_in_house" ||
                       input.forcedScenario == "cheating_physical_letter") {
                plan.shouldExposeNpc1Affair = true;
                plan.shouldSetupSexScene = true;
                plan.sceneKind = BanterSceneKind::CheatingSecret;
                plan.sceneIdx = SelectCheatingSceneVariantIndex(world, npc1, npc2, 0, sceneIdx, rng);
                if (Domain::Relationships::IsActorCheating(world, npc2)) {
                    plan.npc2MainPartner = world.relationships->GetPrimaryPartner(npc2);
                }
            } else if (input.forcedScenario == "open_in_house" ||
                       input.forcedScenario == "open_date_away") {
                plan.sceneKind = BanterSceneKind::OpenRelationship;
                if (world.relationships->IsInAnyOpenRelationship(npc2)) {
                    if (rng.NextInt(0, 1) == 0) {
                        sceneIdx = 4;
                    }
                    plan.npc2MainPartner = world.relationships->GetPrimaryPartner(npc2);
                } else if (sceneIdx == 4) {
                    sceneIdx = rng.NextInt(0, 3);
                }
                plan.sceneIdx = sceneIdx;
            } else {
                return BanterSceneSetupPlan{};
            }

            plan.valid = (plan.sceneKind != BanterSceneKind::None || plan.shouldSetupSexScene);
            return plan;
        }

        const bool isCheating = Domain::Relationships::IsRelationshipCheating(world, npc1, npc2);
        const bool isNpc1Open = world.relationships->IsInAnyOpenRelationship(npc1);
        const bool isNpc2Open = world.relationships->IsInAnyOpenRelationship(npc2);
        const bool isOpen = isNpc1Open || isNpc2Open;

        if (isCheating) {
            const std::int32_t cheatingType =
                SelectCheatingSceneKindWithFallbacks(world, npc1, npc2, input.cheatingEnv, rng);
            if (cheatingType < 0) {
                return BanterSceneSetupPlan{};
            }

            switch (cheatingType) {
                case 1: plan.sceneKind = BanterSceneKind::CheatingDiscovered; break;
                case 2: plan.sceneKind = BanterSceneKind::CheatingCaught; break;
                default: plan.sceneKind = BanterSceneKind::CheatingSecret; break;
            }

            sceneIdx = SelectCheatingSceneVariantIndex(world, npc1, npc2, cheatingType, sceneIdx, rng);

            const bool isNpc1Cheating = Domain::Relationships::IsActorCheating(world, npc1);
            const bool isNpc2Cheating = Domain::Relationships::IsActorCheating(world, npc2);
            if ((isNpc1Cheating && npc1.IsPlayer()) || (isNpc2Cheating && npc2.IsPlayer())) {
                return BanterSceneSetupPlan{};
            }

            if (isNpc1Cheating && isNpc2Cheating) {
                if (rng.NextInt(0, 1) == 0) {
                    std::swap(npc1, npc2);
                }
                plan.npc1MainPartner = world.relationships->GetPrimaryPartner(npc1);
                plan.npc2MainPartner = world.relationships->GetPrimaryPartner(npc2);
            } else {
                if (isNpc2Cheating) {
                    std::swap(npc1, npc2);
                }
                plan.npc1MainPartner = world.relationships->GetPrimaryPartner(npc1);
            }

            plan.npc1 = npc1;
            plan.npc2 = npc2;
            plan.sceneIdx = sceneIdx;
            plan.isCheating = true;
            plan.shouldSetupSexScene = (cheatingType == 2);
            plan.shouldExposeNpc1Affair = (plan.npc1MainPartner.IsPlayer() || cheatingType >= 1);
            plan.needsPartnerMove =
                (!plan.npc1MainPartner.IsNull() && !plan.npc1MainPartner.IsPlayer() && cheatingType >= 1);
            plan.valid = true;
            return plan;
        }

        if (isOpen) {
            if (!world.settings->enableScenes_OpenRelationship) {
                return BanterSceneSetupPlan{};
            }
            plan.sceneKind = BanterSceneKind::OpenRelationship;
            if (isNpc1Open && isNpc2Open) {
                if (rng.NextInt(0, 1) == 0) {
                    std::swap(npc1, npc2);
                    sceneIdx = 4;
                }
                plan.npc1MainPartner = world.relationships->GetPrimaryPartner(npc1);
                plan.npc2MainPartner = world.relationships->GetPrimaryPartner(npc2);
            } else {
                if (sceneIdx == 4) {
                    sceneIdx = rng.NextInt(0, 3);
                }
                if (isNpc2Open) {
                    std::swap(npc1, npc2);
                }
                plan.npc1MainPartner = world.relationships->GetPrimaryPartner(npc1);
            }
            plan.npc1 = npc1;
            plan.npc2 = npc2;
            plan.sceneIdx = sceneIdx;
            plan.valid = true;
            return plan;
        }

        const auto status = world.relationships->GetStatus(npc1, npc2);
        const auto& settings = *world.settings;
        switch (status) {
            case RelationshipStatus::Lover:
                if (settings.enableScenes_Lover) plan.sceneKind = BanterSceneKind::Lover;
                break;
            case RelationshipStatus::Dating:
                if (settings.enableScenes_Dating) plan.sceneKind = BanterSceneKind::Dating;
                break;
            case RelationshipStatus::Engaged:
                if (settings.enableScenes_Engaged) plan.sceneKind = BanterSceneKind::Engaged;
                break;
            case RelationshipStatus::Married:
                if (settings.enableScenes_Married) plan.sceneKind = BanterSceneKind::Married;
                break;
            default:
                break;
        }

        if (plan.sceneKind == BanterSceneKind::None) {
            return BanterSceneSetupPlan{};
        }

        plan.npc1 = npc1;
        plan.npc2 = npc2;
        plan.sceneIdx = sceneIdx;
        plan.valid = true;
        return plan;
    }

}  // namespace MARAS::Domain::Agency
