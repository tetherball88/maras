#include "core/BanterCoordinatorService.h"

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PollingService.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {
        inline bool IsBanterQualifyingStatus(RelationshipStatus status) {
            return status == RelationshipStatus::Dating ||
                   status == RelationshipStatus::Lover ||
                   status == RelationshipStatus::Engaged ||
                   status == RelationshipStatus::Married;
        }

        template <typename T>
        const T& PickRandomElement(const std::vector<T>& vec) {
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, vec.size() - 1);
            return vec[dist(rng)];
        }
    }  // namespace

    bool BanterCoordinatorService::IsValidBanterActor(RE::Actor* actor, bool checkDistanceAndLoaded, float scanRadius) const {
        if (!actor) return false;

        auto formID = actor->GetFormID();
        if (formID == 0 || formID == kPlayerFormID) return false;
        if (actor->IsPlayerRef()) return false;

        // Placed ref validity
        if (actor->IsDisabled() || actor->IsDeleted()) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is disabled or deleted", formID, actor->GetName());
            return false;
        }
        if (actor->IsDead()) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is dead", formID, actor->GetName());
            return false;
        }

        // Check DeceasedRegistry (by placed ref ID and base FormID)
        auto& deceasedReg = DeceasedRegistry::GetSingleton();
        if (deceasedReg.IsDeceased(formID)) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is in DeceasedRegistry (ref)", formID, actor->GetName());
            return false;
        }

        if (auto* base = actor->GetActorBase()) {
            if (deceasedReg.IsDeceased(base->GetFormID())) {
                MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is in DeceasedRegistry (base {:08X})", formID, actor->GetName(), base->GetFormID());
                return false;
            }
            if (base->IsGhost()) {
                MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') base is ghost", formID, actor->GetName());
                return false;
            }
        }

        // Child checks (CommonLib Actor method & synthetic unborn/infant)
        if (actor->IsChild() || IsSyntheticChildID(formID)) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is child/synthetic child", formID, actor->GetName());
            return false;
        }

        // Exclude active player teammates/followers
        if (Utils::IsPlayerFollower(actor)) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is player teammate/follower", formID, actor->GetName());
            return false;
        }

        // Exclude active main partners of the player (governed by Tier 2 Partner Agency mechanics)
        if (NPCRelationshipManager::GetSingleton().IsPlayerActiveMainPartner(formID)) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is an active main partner of the player", formID, actor->GetName());
            return false;
        }

        // Exclude ghosts
        if (actor->IsGhost()) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is ghost", formID, actor->GetName());
            return false;
        }

        // Exclude combat
        if (actor->IsInCombat()) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is in combat", formID, actor->GetName());
            return false;
        }

        // Exclude scene participation (actor must not already be in an active scene)
        if (actor->GetCurrentScene() != nullptr) {
            MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') is currently in an active scene", formID, actor->GetName());
            return false;
        }

        // Distance and 3D loaded check if required
        if (checkDistanceAndLoaded) {
            if (!actor->Is3DLoaded()) {
                MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') 3D is not loaded", formID, actor->GetName());
                return false;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;

            float dist = player->GetPosition().GetDistance(actor->GetPosition());
            if (dist > scanRadius) {
                MARAS_LOG_TRACE("BanterCoordinator: Actor {:08X} ('{}') distance ({:.1f}) exceeds scan radius ({:.1f})", formID, actor->GetName(), dist, scanRadius);
                return false;
            }
        }

        return true;
    }

    std::vector<RE::Actor*> BanterCoordinatorService::GetNearbyLivingActors(float scanRadius) const {
        std::vector<RE::Actor*> result;
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!worldSnap || !spatialSnap || scanRadius <= 0.0f) {
            return result;
        }

        std::unordered_set<RE::FormID> seen;
        auto& npcIndex = NPCIndex::GetSingleton();

        spatialSnap->ForEachNearbyCandidate(State::RefId::Player(), scanRadius, [&](const State::LoadedActorSample& sample) {
            const RE::FormID formID = sample.refId.raw();
            if (formID == 0 || formID == kPlayerFormID || IsSyntheticChildID(formID)) return;
            if (seen.contains(formID)) return;
            if (!sample.Is3DLoaded() || sample.IsDisabled() || sample.IsDeleted() || sample.IsDead()) return;
            if (sample.IsPlayer() || sample.IsChild() || sample.IsPlayerTeammate()) return;
            if (sample.IsInCombat() || sample.IsInScene()) return;

            if (worldSnap->deceased && worldSnap->deceased->IsDeceased(State::ActorId{ formID }, sample.baseId)) {
                return;
            }
            if (worldSnap->relationships) {
                const auto playerStatus = worldSnap->relationships->GetStatus(State::ActorId::Player(), State::ActorId{ formID });
                if (State::IsMainTierStatus(playerStatus)) {
                    return;
                }
            }

            RE::Actor* actor = npcIndex.GetActorFromFormId(formID);
            if (!actor) {
                actor = RE::TESForm::LookupByID<RE::Actor>(formID);
            }
            if (!actor || actor->IsGhost()) {
                return;
            }
            if (const auto* base = actor->GetActorBase(); base && base->IsGhost()) {
                return;
            }

            seen.insert(formID);
            result.push_back(actor);
        });

        return result;
    }

    std::vector<RE::Actor*> BanterCoordinatorService::FindBanterCoupleCandidates(float scanRadius) {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!worldSnap || !worldSnap->settings || !worldSnap->relationships || !worldSnap->cooldowns || !spatialSnap) {
            return {};
        }

        const auto& settings = *worldSnap->settings;
        if (!settings.enableScenes) {
            MARAS_LOG_DEBUG("BanterCoordinator: Banter scenes disabled by master setting");
            return {};
        }
        if (!settings.enableScenes_Lover && !settings.enableScenes_Dating &&
            !settings.enableScenes_Engaged && !settings.enableScenes_Married &&
            !settings.enableScenes_OpenRelationship && !settings.enableScenes_Cheating &&
            !settings.enableScenes_CheatingDiscovered && !settings.enableScenes_CheatingCaught) {
            MARAS_LOG_DEBUG("BanterCoordinator: All banter scene types disabled");
            return {};
        }

        float currentDay = spatialSnap->CurrentGameDay();
        if (currentDay < 0.0f) {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                currentDay = calendar->GetDaysPassed();
            } else {
                currentDay = 0.0f;
            }
        }

        const float cooldownHours = std::max(0.0f, settings.scenesCooldownHours);
        if (cooldownHours > 0.0f) {
            const float lastSceneDay = worldSnap->cooldowns->lastBanterSceneDay;
            if (lastSceneDay >= 0.0f && currentDay >= lastSceneDay &&
                (currentDay - lastSceneDay) < (cooldownHours / 24.0f)) {
                MARAS_LOG_DEBUG("BanterCoordinator: Banter scenes on cooldown");
                return {};
            }
        }

        auto isPairOnCooldown = [&](RE::FormID idA, RE::FormID idB) -> bool {
            const float pairHours = cooldownHours * kCoupleCooldownMultiplier;
            if (pairHours <= 0.0f) {
                return false;
            }
            const State::CanonicalPairKey key(State::ActorId{ idA }, State::ActorId{ idB });
            const auto it = worldSnap->cooldowns->banterCoupleCooldowns.find(key);
            if (it == worldSnap->cooldowns->banterCoupleCooldowns.end()) {
                return false;
            }
            const float lastDay = it->second;
            if (currentDay < lastDay) {
                return false;
            }
            return (currentDay - lastDay) < (pairHours / 24.0f);
        };

        std::vector<RE::Actor*> localActors;
        std::unordered_set<RE::FormID> localActorIDs;
        auto& npcIndex = NPCIndex::GetSingleton();

        spatialSnap->ForEachNearbyCandidate(State::RefId::Player(), scanRadius, [&](const State::LoadedActorSample& sample) {
            const RE::FormID formID = sample.refId.raw();
            if (formID == 0 || formID == kPlayerFormID || IsSyntheticChildID(formID)) return;
            if (localActorIDs.contains(formID)) return;
            if (!sample.Is3DLoaded() || sample.IsDisabled() || sample.IsDeleted() || sample.IsDead()) return;
            if (sample.IsPlayer() || sample.IsChild() || sample.IsPlayerTeammate()) return;
            if (sample.IsInCombat() || sample.IsInScene()) return;

            if (worldSnap->deceased && worldSnap->deceased->IsDeceased(State::ActorId{ formID }, sample.baseId)) {
                return;
            }
            const auto playerStatus = worldSnap->relationships->GetStatus(State::ActorId::Player(), State::ActorId{ formID });
            if (State::IsMainTierStatus(playerStatus)) {
                return;
            }

            RE::Actor* actor = npcIndex.GetActorFromFormId(formID);
            if (!actor) {
                actor = RE::TESForm::LookupByID<RE::Actor>(formID);
            }
            if (!actor || actor->IsGhost()) {
                return;
            }
            if (const auto* base = actor->GetActorBase(); base && base->IsGhost()) {
                return;
            }

            localActorIDs.insert(formID);
            localActors.push_back(actor);
        });

        if (localActors.empty()) {
            MARAS_LOG_DEBUG("BanterCoordinator: No eligible local actors found within radius {}", scanRadius);
            return {};
        }

        // ── Tier 1: Check for couples where BOTH partners are in localActors ─────
        std::vector<std::pair<RE::Actor*, RE::Actor*>> tier1Candidates;

        for (size_t i = 0; i < localActors.size(); ++i) {
            for (size_t j = i + 1; j < localActors.size(); ++j) {
                auto* actorA = localActors[i];
                auto* actorB = localActors[j];
                const auto idA = actorA->GetFormID();
                const auto idB = actorB->GetFormID();

                const auto status = static_cast<RelationshipStatus>(
                    worldSnap->relationships->GetStatus(State::ActorId{ idA }, State::ActorId{ idB }));
                if (IsBanterQualifyingStatus(status)) {
                    if (!isPairOnCooldown(idA, idB)) {
                        tier1Candidates.emplace_back(actorA, actorB);
                    } else {
                        MARAS_LOG_DEBUG("BanterCoordinator: Pair ({:08X}, {:08X}) is on cooldown", idA, idB);
                    }
                }
            }
        }

        if (!tier1Candidates.empty()) {
            auto [pickedA, pickedB] = SelectPrioritizedCouple(tier1Candidates);
            const auto pickedAId = pickedA->GetFormID();
            const auto pickedBId = pickedB->GetFormID();
            MARAS_LOG_INFO("BanterCoordinator: Tier 1 selected couple ({:08X}: '{}', {:08X}: '{}')",
                           pickedAId, ResolveActorNameSafe(pickedAId, *worldSnap, *spatialSnap),
                           pickedBId, ResolveActorNameSafe(pickedBId, *worldSnap, *spatialSnap));
            return { pickedA, pickedB };
        }

        // ── Tier 2: Check singular local NPCs whose partner is absent from radius ──
        std::vector<std::pair<RE::Actor*, RE::Actor*>> tier2Candidates;
        std::unordered_set<RelationshipPairKey, RelationshipPairKeyHash> seenPairs;

        for (auto* localActor : localActors) {
            const auto localID = localActor->GetFormID();
            const auto activeRels = worldSnap->relationships->GetActivePartners(State::ActorId{ localID });

            for (const auto& [partnerActorId, rawStatus] : activeRels) {
                const RE::FormID partnerID = partnerActorId.raw();
                if (partnerID == 0 || partnerID == kPlayerFormID || IsSyntheticChildID(partnerID)) continue;
                const auto playerStatus = worldSnap->relationships->GetStatus(State::ActorId::Player(), partnerActorId);
                if (State::IsMainTierStatus(playerStatus)) continue;
                if (!IsBanterQualifyingStatus(static_cast<RelationshipStatus>(rawStatus))) continue;

                // If partner was already locally present, it was handled in Tier 1
                if (localActorIDs.contains(partnerID)) continue;

                RelationshipPairKey pairKey(localID, partnerID);
                if (seenPairs.contains(pairKey)) continue;

                if (isPairOnCooldown(localID, partnerID)) {
                    MARAS_LOG_DEBUG("BanterCoordinator: Pair ({:08X}, {:08X}) is on cooldown", localID, partnerID);
                    continue;
                }

                // Lookup absent partner reference
                RE::Actor* partnerActor = npcIndex.GetActorFromFormId(partnerID);
                if (!partnerActor) {
                    partnerActor = RE::TESForm::LookupByID<RE::Actor>(partnerID);
                }
                if (!partnerActor) continue;

                // Validate absent partner (distance/3D loaded not required)
                if (!IsValidBanterActor(partnerActor, false /*checkDistanceAndLoaded*/)) {
                    continue;
                }

                seenPairs.insert(pairKey);
                tier2Candidates.emplace_back(localActor, partnerActor);
            }
        }

        if (!tier2Candidates.empty()) {
            auto [pickedLocal, pickedAbsent] = SelectPrioritizedCouple(tier2Candidates);
            const auto pickedLocalId = pickedLocal->GetFormID();
            const auto pickedAbsentId = pickedAbsent->GetFormID();
            MARAS_LOG_INFO("BanterCoordinator: Tier 2 selected couple (Local {:08X}: '{}', Absent {:08X}: '{}')",
                           pickedLocalId, ResolveActorNameSafe(pickedLocalId, *worldSnap, *spatialSnap),
                           pickedAbsentId, ResolveActorNameSafe(pickedAbsentId, *worldSnap, *spatialSnap));
            return { pickedLocal, pickedAbsent };
        }

        MARAS_LOG_DEBUG("BanterCoordinator: No candidate couples found (all on cooldown or no couples)");
        return {};
    }

    float BanterCoordinatorService::GetLastPlayedDay(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& coupleCooldowns = snap->cooldowns->banterCoupleCooldowns;
        const State::CanonicalPairKey key(State::ActorId{ actorA }, State::ActorId{ actorB });
        auto it = coupleCooldowns.find(key);
        return (it != coupleCooldowns.end()) ? it->second : -1.0f;
    }

    std::pair<RE::Actor*, RE::Actor*> BanterCoordinatorService::SelectPrioritizedCouple(
        const std::vector<std::pair<RE::Actor*, RE::Actor*>>& candidates) const {
        if (candidates.size() == 1) {
            return candidates.front();
        }

        std::vector<std::pair<RE::Actor*, RE::Actor*>> neverPlayed;
        std::vector<std::pair<std::pair<RE::Actor*, RE::Actor*>, float>> playedBefore;
        neverPlayed.reserve(candidates.size());
        playedBefore.reserve(candidates.size());

        float oldestDay = std::numeric_limits<float>::max();

        for (const auto& pair : candidates) {
            const float lastDay = GetLastPlayedDay(pair.first->GetFormID(), pair.second->GetFormID());
            if (lastDay < 0.0f) {
                neverPlayed.push_back(pair);
            } else {
                if (lastDay < oldestDay) {
                    oldestDay = lastDay;
                }
                playedBefore.emplace_back(pair, lastDay);
            }
        }

        // Priority 1: Couples that have never played a scene
        if (!neverPlayed.empty()) {
            return PickRandomElement(neverPlayed);
        }

        // Priority 2: Least-recently-played couple(s) (with random tie-breaking)
        std::vector<std::pair<RE::Actor*, RE::Actor*>> leastRecent;
        leastRecent.reserve(playedBefore.size());
        constexpr float kTieEpsilon = 1e-4f;
        for (const auto& [pair, lastDay] : playedBefore) {
            if (lastDay <= oldestDay + kTieEpsilon) {
                leastRecent.push_back(pair);
            }
        }

        return !leastRecent.empty() ? PickRandomElement(leastRecent) : PickRandomElement(candidates);
    }

    bool BanterCoordinatorService::IsBanterPartnerPresent(RE::Actor* partner, float scanRadius) const {
        if (!partner) return false;
        const RE::FormID partnerID = partner->GetFormID();
        if (partnerID == 0 || partnerID == kPlayerFormID) return false;

        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (spatialSnap && spatialSnap->Sequence() != 0) {
            const auto* sample = spatialSnap->Find(State::RefId{ partnerID });
            if (!sample || !sample->Is3DLoaded() || sample->IsDisabled() || sample->IsDeleted() || sample->IsDead()) {
                return false;
            }
            return spatialSnap->IsWithinRadius(State::RefId::Player(), State::RefId{ partnerID }, scanRadius);
        }

        if (!partner->Is3DLoaded()) return false;
        if (partner->IsDisabled() || partner->IsDeleted() || partner->IsDead()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        float dist = player->GetPosition().GetDistance(partner->GetPosition());
        return dist <= scanRadius;
    }

    float BanterCoordinatorService::GetCooldownHours() const {
        return std::max(0.0f, Settings::GetSingleton().Snapshot()->scenesCooldownHours);
    }

    void BanterCoordinatorService::SetCooldownHours(float hours) {
        const float clamped = std::max(0.0f, hours);
        State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
            ws.settings.Write().scenesCooldownHours = clamped;
            ws.cooldowns.Write().savedBanterCooldownHours = clamped;
        });
        MARAS_LOG_INFO("BanterCoordinator: Cooldown set to {} hours", clamped);
    }

    bool BanterCoordinatorService::IsSceneOnCooldown(float currentDay) const {
        const float cooldownHours = GetCooldownHours();
        if (cooldownHours <= 0.0f) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float lastSceneDay = snap->cooldowns->lastBanterSceneDay;
        if (lastSceneDay < 0.0f || currentDay < lastSceneDay) {
            return false;
        }
        const float cooldownDays = cooldownHours / 24.0f;
        return (currentDay - lastSceneDay) < cooldownDays;
    }

    void BanterCoordinatorService::RecordScenePlayed(RE::Actor* actorA, RE::Actor* actorB) {
        float currentDay = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentDay = calendar->GetDaysPassed();
        }

        const RE::FormID idA = actorA ? actorA->GetFormID() : 0;
        const RE::FormID idB = actorB ? actorB->GetFormID() : 0;
        const std::string nameA = (actorA && actorA->GetName()) ? actorA->GetName() : "";
        const std::string nameB = (actorB && actorB->GetName()) ? actorB->GetName() : "";

        State::WorldStore::GetSingleton().Modify([currentDay, idA, idB](State::WorldState& ws) {
            auto& cd = ws.cooldowns.Write();
            cd.lastBanterSceneDay = currentDay;
            if (idA != 0 && idB != 0) {
                cd.banterCoupleCooldowns[State::CanonicalPairKey(State::ActorId{ idA }, State::ActorId{ idB })] = currentDay;
            }
        });

        if (idA != 0 && idB != 0) {
            MARAS_LOG_INFO("BanterCoordinator: Recorded scene played for pair ({:08X}: '{}', {:08X}: '{}') on day {:.3f}",
                           idA, nameA, idB, nameB, currentDay);
            return;
        }
        MARAS_LOG_INFO("BanterCoordinator: Recorded scene played on day {:.3f}", currentDay);
    }

    bool BanterCoordinatorService::IsOnCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay) const {
        const float cooldownHours = GetCooldownHours() * kCoupleCooldownMultiplier;
        if (cooldownHours <= 0.0f) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& coupleCooldowns = snap->cooldowns->banterCoupleCooldowns;
        const State::CanonicalPairKey key(State::ActorId{ actorA }, State::ActorId{ actorB });
        auto it = coupleCooldowns.find(key);
        if (it == coupleCooldowns.end()) {
            return false;
        }
        const float lastDay = it->second;
        if (currentDay < lastDay) {
            return false;
        }
        const float cooldownDays = cooldownHours / 24.0f;
        return (currentDay - lastDay) < cooldownDays;
    }

    void BanterCoordinatorService::SetCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay) {
        State::WorldStore::GetSingleton().Modify([actorA, actorB, currentDay](State::WorldState& ws) {
            const State::CanonicalPairKey key(State::ActorId{ actorA }, State::ActorId{ actorB });
            ws.cooldowns.Write().banterCoupleCooldowns[key] = currentDay;
        });
    }

    void BanterCoordinatorService::ResetCooldown(RE::FormID actorA, RE::FormID actorB) {
        State::WorldStore::GetSingleton().Modify([actorA, actorB](State::WorldState& ws) {
            const State::CanonicalPairKey key(State::ActorId{ actorA }, State::ActorId{ actorB });
            ws.cooldowns.Write().banterCoupleCooldowns.erase(key);
        });
        MARAS_LOG_INFO("BanterCoordinator: Reset cooldown for pair ({:08X}, {:08X})", actorA, actorB);
    }

    void BanterCoordinatorService::ClearAllCooldowns() {
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            auto& cd = ws.cooldowns.Write();
            cd.banterCoupleCooldowns.clear();
            cd.lastBanterSceneDay = -1.0f;
        });
        MARAS_LOG_INFO("BanterCoordinator: Cleared all couple and scene cooldowns");
    }

    void BanterCoordinatorService::Revert() {
        MARAS_LOG_INFO("BanterCoordinator::Revert: Reverted to clean state");
    }

    nlohmann::json BanterCoordinatorService::DumpJson(const Projection::PerspectiveView& view) const {
        const auto& cd = view.Cooldowns();
        const float effectiveCooldownHours = GetCooldownHours();

        nlohmann::json j = nlohmann::json::object();
        j["cooldownHours"] = effectiveCooldownHours;
        j["savedCooldownHours"] = cd.savedBanterCooldownHours;
        j["lastSceneDay"] = cd.lastBanterSceneDay;

        nlohmann::json cooldownsArray = nlohmann::json::array();
        for (const auto& [key, lastDay] : cd.banterCoupleCooldowns) {
            if (!view.IsOmniscient()) {
                const auto perceived = view.GetPerceivedPair(key.first, key.second);
                if (!perceived.has_value() || perceived->isSecretAffair) {
                    continue;
                }
            }
            const RE::FormID idA = key.first.raw();
            const RE::FormID idB = key.second.raw();
            nlohmann::json entry = nlohmann::json::object();
            entry["actorAFormId"] = std::format("{:08X}", idA);
            entry["actorAName"] = ActorSvc::GetName(idA);
            entry["actorBFormId"] = std::format("{:08X}", idB);
            entry["actorBName"] = ActorSvc::GetName(idB);
            entry["lastSceneDay"] = lastDay;
            cooldownsArray.push_back(std::move(entry));
        }
        j["coupleCooldowns"] = std::move(cooldownsArray);
        return j;
    }

    nlohmann::json BanterCoordinatorService::DumpJson() const {
        return DumpJson(Projection::PerspectiveView::ForDashboard(
            State::WorldStore::GetSingleton().Snapshot(),
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider()));
    }

    bool BanterCoordinatorService::IsLocationAllowedForScenes(RE::BGSLocation* location) const {
        const auto settingsSnap = Settings::GetSingleton().Snapshot();
        const auto& s = *settingsSnap;
        if (!s.enableScenes || !location) {
            return false;
        }

        auto& formCache = FormCache::GetSingleton();

        struct LocationKeywordRule {
            RE::BGSKeyword* keyword;
            bool isEnabled;
        };

        const LocationKeywordRule rules[] = {
            // Interiors
            { formCache.GetLocTypeTempleKeyword(), s.enableScenes_LocTypeTemple },
            { formCache.GetLocTypeStoreKeyword(), s.enableScenes_LocTypeStore },
            { formCache.GetLocTypeStewardsDwellingKeyword(), s.enableScenes_LocTypeStewardsDwelling },
            { formCache.GetLocTypeInnKeyword(), s.enableScenes_LocTypeInn },
            { formCache.GetLocTypeHouseKeyword(), s.enableScenes_LocTypeHouse },
            { formCache.GetLocTypeBarracksKeyword(), s.enableScenes_LocTypeBarracks },
            { formCache.GetLocTypeGuildKeyword(), s.enableScenes_LocTypeGuild },
            // Player Houses
            { formCache.GetLocTypePlayerHouseKeyword(), s.enableScenes_LocTypePlayerHouse },
            { formCache.GetBYOHLocTypeHomesteadKeyword(), s.enableScenes_BYOH_LocTypeHomestead },
            // Streets
            { formCache.GetLocTypeTownKeyword(), s.enableScenes_LocTypeTown },
            { formCache.GetLocTypeSettlementKeyword(), s.enableScenes_LocTypeSettlement },
            { formCache.GetLocTypeOrcStrongholdKeyword(), s.enableScenes_LocTypeOrcStronghold },
            { formCache.GetLocTypeCityKeyword(), s.enableScenes_LocTypeCity },
            { formCache.GetLocTypeLumberMillKeyword(), s.enableScenes_LocTypeLumberMill },
            { formCache.GetLocTypeFarmKeyword(), s.enableScenes_LocTypeFarm },
        };

        bool hasMatchingKeyword = false;

        for (const auto& rule : rules) {
            if (rule.keyword && location->HasKeyword(rule.keyword)) {
                if (!rule.isEnabled) {
                    return false;
                }
                hasMatchingKeyword = true;
            }
        }

        return hasMatchingKeyword;
    }

}  // namespace MARAS
