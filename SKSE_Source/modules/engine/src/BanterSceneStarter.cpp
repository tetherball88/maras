#include "maras/engine/BanterSceneStarter.h"

#include "maras/domain/agency/BanterPlanner.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/PerspectiveEngineAdapters.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <array>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace MARAS {

    namespace {

        class ThreadLocalEngineRng final : public Foundation::IRng {
        public:
            [[nodiscard]] float NextFloat(float minVal = 0.0f, float maxVal = 1.0f) override {
                if (maxVal <= minVal) {
                    return minVal;
                }
                std::uniform_real_distribution<float> dist(minVal, maxVal);
                return dist(Engine());
            }

            [[nodiscard]] std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) override {
                if (maxVal <= minVal) {
                    return minVal;
                }
                std::uniform_int_distribution<std::int32_t> dist(minVal, maxVal);
                return dist(Engine());
            }

        private:
            static std::mt19937& Engine() {
                static thread_local std::mt19937 rng{ std::random_device{}() };
                return rng;
            }
        };

        [[nodiscard]] RE::Actor* LookupLoadedActor(RE::FormID formID) {
            if (formID == 0) {
                return nullptr;
            }
            if (auto* indexed = SpatialWorldIndex::GetSingleton().GetActorFromFormId(formID)) {
                return indexed;
            }
            return RE::TESForm::LookupByID<RE::Actor>(formID);
        }

        [[nodiscard]] float ResolveCurrentGameDay(const State::SpatialActorSnapshot& spatialSnap) noexcept {
            float currentDay = spatialSnap.CurrentGameDay();
            if (currentDay >= 0.0f) {
                return currentDay;
            }
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

    }  // namespace

    BanterSceneStarter& BanterSceneStarter::GetSingleton() {
        static BanterSceneStarter instance;
        return instance;
    }

    bool BanterSceneStarter::IsValidBanterActor(
        RE::Actor* actor,
        bool checkDistanceAndLoaded,
        float scanRadius) const {
        if (!actor) {
            return false;
        }

        const RE::FormID formID = actor->GetFormID();
        if (formID == 0 || formID == kPlayerFormID || actor->IsPlayerRef()) {
            return false;
        }

        if (actor->IsDisabled() || actor->IsDeleted()) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is disabled or deleted", formID, actor->GetName());
            return false;
        }
        if (actor->IsDead()) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is dead", formID, actor->GetName());
            return false;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        Foundation::BaseId baseId{};
        if (const auto* base = actor->GetActorBase()) {
            baseId = Foundation::BaseId{ base->GetFormID() };
            if (base->IsGhost()) {
                MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') base is ghost", formID, actor->GetName());
                return false;
            }
        }

        if (worldSnap && worldSnap->deceased &&
            worldSnap->deceased->IsDeceased(Foundation::ActorId{ formID }, baseId)) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is deceased", formID, actor->GetName());
            return false;
        }

        if (actor->IsChild() || IsSyntheticChildID(formID)) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is child/synthetic child", formID, actor->GetName());
            return false;
        }

        if (Utils::IsPlayerFollower(actor)) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is player teammate/follower", formID, actor->GetName());
            return false;
        }

        if (worldSnap && worldSnap->relationships) {
            const auto playerStatus =
                worldSnap->relationships->GetStatus(Foundation::ActorId::Player(), Foundation::ActorId{ formID });
            if (Foundation::IsMainTierStatus(playerStatus)) {
                MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is an active main partner of the player",
                                formID, actor->GetName());
                return false;
            }
        }

        if (actor->IsGhost()) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is ghost", formID, actor->GetName());
            return false;
        }

        if (actor->IsInCombat()) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is in combat", formID, actor->GetName());
            return false;
        }

        if (actor->GetCurrentScene() != nullptr) {
            MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') is currently in an active scene",
                            formID, actor->GetName());
            return false;
        }

        if (checkDistanceAndLoaded) {
            if (!actor->Is3DLoaded()) {
                MARAS_LOG_TRACE("BanterSceneStarter: Actor {:08X} ('{}') 3D is not loaded", formID, actor->GetName());
                return false;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return false;
            }

            const float dist = player->GetPosition().GetDistance(actor->GetPosition());
            if (dist > scanRadius) {
                MARAS_LOG_TRACE(
                    "BanterSceneStarter: Actor {:08X} ('{}') distance ({:.1f}) exceeds scan radius ({:.1f})",
                    formID, actor->GetName(), dist, scanRadius);
                return false;
            }
        }

        return true;
    }

    std::vector<RE::Actor*> BanterSceneStarter::GetNearbyLivingActors(float scanRadius) const {
        std::vector<RE::Actor*> result;
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!worldSnap || !spatialSnap || scanRadius <= 0.0f) {
            return result;
        }

        std::unordered_set<RE::FormID> seen;

        spatialSnap->ForEachNearbyCandidate(Foundation::RefId::Player(), scanRadius, [&](const State::LoadedActorSample& sample) {
            const RE::FormID formID = sample.refId.raw();
            if (formID == 0 || formID == kPlayerFormID || IsSyntheticChildID(formID)) {
                return;
            }
            if (seen.contains(formID)) {
                return;
            }
            if (!sample.Is3DLoaded() || sample.IsDisabled() || sample.IsDeleted() || sample.IsDead()) {
                return;
            }
            if (sample.IsPlayer() || sample.IsChild() || sample.IsPlayerTeammate()) {
                return;
            }
            if (sample.IsInCombat() || sample.IsInScene()) {
                return;
            }

            if (worldSnap->deceased && worldSnap->deceased->IsDeceased(Foundation::ActorId{ formID }, sample.baseId)) {
                return;
            }
            if (worldSnap->relationships) {
                const auto playerStatus =
                    worldSnap->relationships->GetStatus(Foundation::ActorId::Player(), Foundation::ActorId{ formID });
                if (Foundation::IsMainTierStatus(playerStatus)) {
                    return;
                }
            }

            RE::Actor* actor = LookupLoadedActor(formID);
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

    bool BanterSceneStarter::IsBanterPartnerPresent(RE::Actor* partner, float scanRadius) const {
        if (!partner) {
            return false;
        }
        const RE::FormID partnerID = partner->GetFormID();
        if (partnerID == 0 || partnerID == kPlayerFormID) {
            return false;
        }

        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (spatialSnap && spatialSnap->Sequence() != 0) {
            const auto* sample = spatialSnap->Find(Foundation::RefId{ partnerID });
            if (!sample || !sample->Is3DLoaded() || sample->IsDisabled() || sample->IsDeleted() || sample->IsDead()) {
                return false;
            }
            return spatialSnap->IsWithinRadius(Foundation::RefId::Player(), Foundation::RefId{ partnerID }, scanRadius);
        }

        if (!partner->Is3DLoaded()) {
            return false;
        }
        if (partner->IsDisabled() || partner->IsDeleted() || partner->IsDead()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        const float dist = player->GetPosition().GetDistance(partner->GetPosition());
        return dist <= scanRadius;
    }

    std::vector<RE::Actor*> BanterSceneStarter::FindBanterCoupleCandidates(float scanRadius) const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!worldSnap || !worldSnap->settings || !worldSnap->relationships || !worldSnap->cooldowns || !spatialSnap) {
            return {};
        }

        const auto& settings = *worldSnap->settings;
        if (!settings.enableScenes) {
            MARAS_LOG_DEBUG("BanterSceneStarter: Banter scenes disabled by master setting");
            return {};
        }
        if (!settings.enableScenes_Lover && !settings.enableScenes_Dating &&
            !settings.enableScenes_Engaged && !settings.enableScenes_Married &&
            !settings.enableScenes_OpenRelationship && !settings.enableScenes_Cheating &&
            !settings.enableScenes_CheatingDiscovered && !settings.enableScenes_CheatingCaught) {
            MARAS_LOG_DEBUG("BanterSceneStarter: All banter scene types disabled");
            return {};
        }

        const float currentDay = ResolveCurrentGameDay(*spatialSnap);
        const float cooldownHours = std::max(0.0f, settings.scenesCooldownHours);
        if (cooldownHours > 0.0f) {
            const float lastSceneDay = worldSnap->cooldowns->lastBanterSceneDay;
            if (lastSceneDay >= 0.0f && currentDay >= lastSceneDay &&
                (currentDay - lastSceneDay) < (cooldownHours / 24.0f)) {
                MARAS_LOG_DEBUG("BanterSceneStarter: Banter scenes on cooldown");
                return {};
            }
        }

        const auto plannedCandidates =
            Domain::Agency::FindBanterCoupleCandidates(*worldSnap, *spatialSnap, scanRadius, currentDay);
        if (plannedCandidates.empty()) {
            MARAS_LOG_DEBUG("BanterSceneStarter: No candidate couples found within radius {}", scanRadius);
            return {};
        }

        std::vector<Domain::Agency::BanterCandidate> validCandidates;
        validCandidates.reserve(plannedCandidates.size());
        std::unordered_map<Foundation::CanonicalPairKey, std::pair<RE::Actor*, RE::Actor*>, Foundation::CanonicalPairKeyHash>
            resolvedActors;

        for (auto cand : plannedCandidates) {
            RE::Actor* actorA = LookupLoadedActor(cand.actorA.raw());
            RE::Actor* actorB = LookupLoadedActor(cand.actorB.raw());
            if (!actorA || !actorB) {
                continue;
            }
            if (!IsValidBanterActor(actorA, /*checkDistanceAndLoaded=*/true, scanRadius)) {
                continue;
            }

            const bool bPresent = IsBanterPartnerPresent(actorB, scanRadius);
            cand.tier = bPresent ? 1 : 2;
            if (!IsValidBanterActor(actorB, /*checkDistanceAndLoaded=*/bPresent, scanRadius)) {
                continue;
            }

            const Foundation::CanonicalPairKey key(cand.actorA, cand.actorB);
            resolvedActors.emplace(key, std::pair<RE::Actor*, RE::Actor*>{ actorA, actorB });
            validCandidates.push_back(std::move(cand));
        }

        if (validCandidates.empty()) {
            MARAS_LOG_DEBUG("BanterSceneStarter: No candidate couples passed live engine validation");
            return {};
        }

        ThreadLocalEngineRng rng;
        const auto selected = Domain::Agency::SelectPrioritizedCouple(validCandidates, rng);
        if (!selected.has_value()) {
            return {};
        }

        const Foundation::CanonicalPairKey selectedKey(selected->actorA, selected->actorB);
        const auto actorIt = resolvedActors.find(selectedKey);
        if (actorIt == resolvedActors.end()) {
            return {};
        }

        auto [pickedA, pickedB] = actorIt->second;
        const RE::FormID pickedAId = pickedA->GetFormID();
        const RE::FormID pickedBId = pickedB->GetFormID();
        MARAS_LOG_INFO("BanterSceneStarter: Tier {} selected couple ({:08X}: '{}', {:08X}: '{}')",
                       selected->tier,
                       pickedAId, ResolveActorNameSafe(pickedAId, *worldSnap, *spatialSnap),
                       pickedBId, ResolveActorNameSafe(pickedBId, *worldSnap, *spatialSnap));
        return { pickedA, pickedB };
    }

    bool BanterSceneStarter::IsLocationAllowedForScenes(RE::BGSLocation* location) const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        if (!worldSnap || !worldSnap->settings) {
            return false;
        }
        const auto& s = *worldSnap->settings;
        if (!s.enableScenes || !location) {
            return false;
        }

        auto& formCache = FormCache::GetSingleton();

        struct LocationKeywordRule {
            RE::BGSKeyword* keyword;
            bool isEnabled;
        };

        const std::array<LocationKeywordRule, 15> rules{{
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
        }};

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
