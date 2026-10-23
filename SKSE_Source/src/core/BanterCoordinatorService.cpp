#include "core/BanterCoordinatorService.h"

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "core/PollingService.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {
        constexpr std::uint32_t kMaxSerializedCooldowns = 100000;

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
        std::unordered_set<RE::FormID> seen;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return result;

        // 1. High process actors (nearby loaded actors)
        if (auto* processLists = RE::ProcessLists::GetSingleton()) {
            for (auto& handle : processLists->highActorHandles) {
                auto actor = handle.get();
                if (!actor) continue;

                auto formID = actor->GetFormID();
                if (formID == 0 || formID == kPlayerFormID || seen.contains(formID)) continue;
                if (IsValidBanterActor(actor.get(), true, scanRadius)) {
                    seen.insert(formID);
                    result.push_back(actor.get());
                }
            }
        }

        // 2. Interior cell references (if inside an interior cell, ensure no local actor is missed)
        if (auto* parentCell = player->GetParentCell()) {
            if (parentCell->IsInteriorCell()) {
                parentCell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                    if (auto* actor = ref->As<RE::Actor>()) {
                        auto formID = actor->GetFormID();
                        if (formID != 0 && formID != kPlayerFormID && !seen.contains(formID)) {
                            if (IsValidBanterActor(actor, true, scanRadius)) {
                                seen.insert(formID);
                                result.push_back(actor);
                            }
                        }
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
        }

        return result;
    }

    std::vector<RE::Actor*> BanterCoordinatorService::FindBanterCoupleCandidates(float scanRadius) {
        const auto& settings = Settings::GetSingleton();
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

        float currentDay = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentDay = calendar->GetDaysPassed();
        }

        if (IsSceneOnCooldown(currentDay)) {
            MARAS_LOG_DEBUG("BanterCoordinator: Banter scenes on cooldown");
            return {};
        }

        auto localActors = GetNearbyLivingActors(scanRadius);
        if (localActors.empty()) {
            MARAS_LOG_DEBUG("BanterCoordinator: No eligible local actors found within radius {}", scanRadius);
            return {};
        }

        std::unordered_set<RE::FormID> localActorIDs;
        for (auto* actor : localActors) {
            localActorIDs.insert(actor->GetFormID());
        }

        auto& relManager = NPCRelationshipManager::GetSingleton();

        // ── Tier 1: Check for couples where BOTH partners are in localActors ─────
        std::vector<std::pair<RE::Actor*, RE::Actor*>> tier1Candidates;

        for (size_t i = 0; i < localActors.size(); ++i) {
            for (size_t j = i + 1; j < localActors.size(); ++j) {
                auto* actorA = localActors[i];
                auto* actorB = localActors[j];
                auto idA = actorA->GetFormID();
                auto idB = actorB->GetFormID();

                if (relManager.IsPlayerActiveMainPartner(idA) || relManager.IsPlayerActiveMainPartner(idB)) {
                    continue;
                }

                auto status = relManager.GetRelationshipStatus(idA, idB);
                if (IsBanterQualifyingStatus(status)) {
                    if (!IsOnCooldown(idA, idB, currentDay)) {
                        tier1Candidates.emplace_back(actorA, actorB);
                    } else {
                        MARAS_LOG_DEBUG("BanterCoordinator: Pair ({:08X}, {:08X}) is on cooldown", idA, idB);
                    }
                }
            }
        }

        if (!tier1Candidates.empty()) {
            auto [pickedA, pickedB] = SelectPrioritizedCouple(tier1Candidates);
            MARAS_LOG_INFO("BanterCoordinator: Tier 1 selected couple ({:08X}: '{}', {:08X}: '{}')",
                           pickedA->GetFormID(), pickedA->GetName(),
                           pickedB->GetFormID(), pickedB->GetName());
            return { pickedA, pickedB };
        }

        // ── Tier 2: Check singular local NPCs whose partner is absent from radius ──
        std::vector<std::pair<RE::Actor*, RE::Actor*>> tier2Candidates;
        std::unordered_set<RelationshipPairKey, RelationshipPairKeyHash> seenPairs;

        for (auto* localActor : localActors) {
            auto localID = localActor->GetFormID();
            if (relManager.IsPlayerActiveMainPartner(localID)) continue;

            auto activeRels = relManager.GetActiveRelationships(localID);

            for (const auto& [partnerID, status] : activeRels) {
                if (partnerID == 0 || partnerID == kPlayerFormID || IsSyntheticChildID(partnerID)) continue;
                if (relManager.IsPlayerActiveMainPartner(partnerID)) continue;
                if (!IsBanterQualifyingStatus(status)) continue;

                // If partner was already locally present, it was handled in Tier 1
                if (localActorIDs.contains(partnerID)) continue;

                RelationshipPairKey pairKey(localID, partnerID);
                if (seenPairs.contains(pairKey)) continue;

                if (IsOnCooldown(localID, partnerID, currentDay)) {
                    MARAS_LOG_DEBUG("BanterCoordinator: Pair ({:08X}, {:08X}) is on cooldown", localID, partnerID);
                    continue;
                }

                // Lookup absent partner reference
                auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(partnerID);
                if (!partnerActor) continue;

                // Validate absent partner
                if (!IsValidBanterActor(partnerActor, false /*checkDistanceAndLoaded*/)) {
                    continue;
                }

                seenPairs.insert(pairKey);
                tier2Candidates.emplace_back(localActor, partnerActor);
            }
        }

        if (!tier2Candidates.empty()) {
            auto [pickedLocal, pickedAbsent] = SelectPrioritizedCouple(tier2Candidates);
            MARAS_LOG_INFO("BanterCoordinator: Tier 2 selected couple (Local {:08X}: '{}', Absent {:08X}: '{}')",
                           pickedLocal->GetFormID(), pickedLocal->GetName(),
                           pickedAbsent->GetFormID(), pickedAbsent->GetName());
            return { pickedLocal, pickedAbsent };
        }

        MARAS_LOG_DEBUG("BanterCoordinator: No candidate couples found (all on cooldown or no couples)");
        return {};
    }

    float BanterCoordinatorService::GetLastPlayedDay(RE::FormID actorA, RE::FormID actorB) const {
        std::shared_lock lock(m_mutex);
        RelationshipPairKey key(actorA, actorB);
        auto it = m_coupleCooldowns.find(key);
        return (it != m_coupleCooldowns.end()) ? it->second : -1.0f;
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
        if (!partner->Is3DLoaded()) return false;
        if (partner->IsDisabled() || partner->IsDeleted() || partner->IsDead()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        float dist = player->GetPosition().GetDistance(partner->GetPosition());
        return dist <= scanRadius;
    }

    float BanterCoordinatorService::GetCooldownHours() const {
        return std::max(0.0f, Settings::GetSingleton().scenesCooldownHours);
    }

    void BanterCoordinatorService::SetCooldownHours(float hours) {
        const float clamped = std::max(0.0f, hours);
        {
            std::unique_lock lock(m_mutex);
            m_cooldownHours = clamped;
        }
        Settings::GetSingleton().scenesCooldownHours = clamped;
        MARAS_LOG_INFO("BanterCoordinator: Cooldown set to {} hours", clamped);
    }

    bool BanterCoordinatorService::IsSceneOnCooldown(float currentDay) const {
        const float cooldownHours = GetCooldownHours();
        if (cooldownHours <= 0.0f) {
            return false;
        }

        std::shared_lock lock(m_mutex);
        if (m_lastSceneDay < 0.0f || currentDay < m_lastSceneDay) {
            return false;
        }
        const float cooldownDays = cooldownHours / 24.0f;
        return (currentDay - m_lastSceneDay) < cooldownDays;
    }

    void BanterCoordinatorService::RecordScenePlayed(RE::Actor* actorA, RE::Actor* actorB) {
        float currentDay = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentDay = calendar->GetDaysPassed();
        }

        std::unique_lock lock(m_mutex);
        m_lastSceneDay = currentDay;

        if (actorA && actorB) {
            auto idA = actorA->GetFormID();
            auto idB = actorB->GetFormID();
            if (idA != 0 && idB != 0) {
                RelationshipPairKey key(idA, idB);
                m_coupleCooldowns[key] = currentDay;
                MARAS_LOG_INFO("BanterCoordinator: Recorded scene played for pair ({:08X}: '{}', {:08X}: '{}') on day {:.3f}",
                               idA, actorA->GetName(), idB, actorB->GetName(), currentDay);
                return;
            }
        }
        MARAS_LOG_INFO("BanterCoordinator: Recorded scene played on day {:.3f}", currentDay);
    }

    bool BanterCoordinatorService::IsOnCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay) const {
        const float cooldownHours = GetCooldownHours() * kCoupleCooldownMultiplier;
        if (cooldownHours <= 0.0f) {
            return false;
        }

        std::shared_lock lock(m_mutex);
        RelationshipPairKey key(actorA, actorB);
        auto it = m_coupleCooldowns.find(key);
        if (it == m_coupleCooldowns.end()) {
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
        std::unique_lock lock(m_mutex);
        RelationshipPairKey key(actorA, actorB);
        m_coupleCooldowns[key] = currentDay;
    }

    void BanterCoordinatorService::ResetCooldown(RE::FormID actorA, RE::FormID actorB) {
        std::unique_lock lock(m_mutex);
        RelationshipPairKey key(actorA, actorB);
        m_coupleCooldowns.erase(key);
        MARAS_LOG_INFO("BanterCoordinator: Reset cooldown for pair ({:08X}, {:08X})", actorA, actorB);
    }

    void BanterCoordinatorService::ClearAllCooldowns() {
        std::unique_lock lock(m_mutex);
        m_coupleCooldowns.clear();
        m_lastSceneDay = -1.0f;
        MARAS_LOG_INFO("BanterCoordinator: Cleared all couple and scene cooldowns");
    }

    bool BanterCoordinatorService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;
        const float cooldownHours = GetCooldownHours();
        std::shared_lock lock(m_mutex);

        if (!serialization->WriteRecordData(cooldownHours)) {
            MARAS_LOG_ERROR("BanterCoordinator::Save: Failed to write cooldown hours");
            return false;
        }

        std::uint32_t count = static_cast<std::uint32_t>(m_coupleCooldowns.size());
        if (!serialization->WriteRecordData(count)) {
            MARAS_LOG_ERROR("BanterCoordinator::Save: Failed to write count");
            return false;
        }

        for (const auto& [key, timestamp] : m_coupleCooldowns) {
            if (!serialization->WriteRecordData(key.actorA) ||
                !serialization->WriteRecordData(key.actorB) ||
                !serialization->WriteRecordData(timestamp)) {
                MARAS_LOG_ERROR("BanterCoordinator::Save: Failed to write record data for pair ({:08X}, {:08X})",
                                key.actorA, key.actorB);
                return false;
            }
        }

        if (!serialization->WriteRecordData(m_lastSceneDay)) {
            MARAS_LOG_ERROR("BanterCoordinator::Save: Failed to write last scene day");
            return false;
        }

        MARAS_LOG_INFO("BanterCoordinator::Save: Saved {} couple cooldowns, lastSceneDay={}", count, m_lastSceneDay);
        return true;
    }

    bool BanterCoordinatorService::Load(SKSE::SerializationInterface* serialization, std::uint32_t version) {
        if (!serialization) return false;
        std::unique_lock lock(m_mutex);

        m_coupleCooldowns.clear();
        m_lastSceneDay = -1.0f;

        if (version < 1) {
            MARAS_LOG_ERROR("BanterCoordinator::Load: Invalid data version {}", version);
            return false;
        }

        float savedCooldownHours = 24.0f;
        if (!serialization->ReadRecordData(savedCooldownHours)) {
            MARAS_LOG_ERROR("BanterCoordinator::Load: Failed to read cooldown hours");
            return false;
        }
        m_cooldownHours = std::max(0.0f, savedCooldownHours);

        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) {
            MARAS_LOG_ERROR("BanterCoordinator::Load: Failed to read count");
            return false;
        }

        if (count > kMaxSerializedCooldowns) {
            MARAS_LOG_ERROR("BanterCoordinator::Load: Cooldown count {} exceeds maximum sanity limit {}",
                            count, kMaxSerializedCooldowns);
            return false;
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID actorA = 0;
            RE::FormID actorB = 0;
            float timestamp = 0.0f;

            if (!serialization->ReadRecordData(actorA) ||
                !serialization->ReadRecordData(actorB) ||
                !serialization->ReadRecordData(timestamp)) {
                MARAS_LOG_ERROR("BanterCoordinator::Load: Failed to read record entry {}", i);
                return false;
            }

            if (IsSyntheticChildID(actorA) || IsSyntheticChildID(actorB)) {
                continue;
            }

            RE::FormID resolvedA = 0;
            RE::FormID resolvedB = 0;
            if (!serialization->ResolveFormID(actorA, resolvedA) ||
                !serialization->ResolveFormID(actorB, resolvedB) ||
                resolvedA == 0 || resolvedB == 0 || resolvedA == resolvedB) {
                MARAS_LOG_WARN("BanterCoordinator::Load: Failed to resolve forms ({:08X}, {:08X}), skipping entry",
                               actorA, actorB);
                continue;
            }

            m_coupleCooldowns[RelationshipPairKey(resolvedA, resolvedB)] = timestamp;
        }

        if (version >= 2) {
            if (!serialization->ReadRecordData(m_lastSceneDay)) {
                MARAS_LOG_WARN("BanterCoordinator::Load: Failed to read last scene day (version 2)");
                m_lastSceneDay = -1.0f;
            }
        }

        MARAS_LOG_INFO("BanterCoordinator::Load: Successfully loaded {} couple cooldowns (cooldown: {} hrs, lastSceneDay: {})",
                       m_coupleCooldowns.size(), m_cooldownHours, m_lastSceneDay);
        return true;
    }

    void BanterCoordinatorService::Revert() {
        std::unique_lock lock(m_mutex);
        m_coupleCooldowns.clear();
        m_cooldownHours = std::max(0.0f, Settings::GetSingleton().scenesCooldownHours);
        m_lastSceneDay = -1.0f;
        MARAS_LOG_INFO("BanterCoordinator::Revert: Reverted to clean state");
    }

    bool BanterCoordinatorService::IsLocationAllowedForScenes(RE::BGSLocation* location) const {
        const auto& s = Settings::GetSingleton();
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
