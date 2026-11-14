#include "core/PollingService.h"

#include "RE/A/AIProcess.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESPackage.h"
#include "core/AdultChildTravelService.h"
#include "core/AffectionService.h"
#include "core/ChildrenFleeService.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/IntimateThreadService.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerChildrenService.h"
#include "core/MatchmakingService.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetMemoryService.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    namespace {

        [[nodiscard]] bool IsLoadedActorWerewolfNoAlloc(
            RE::Actor* actor,
            RE::TESNPC* base,
            RE::FormID refID,
            RE::FormID baseID,
            bool isPlayer,
            const State::WorldStateSnapshot* worldSnap,
            FormCache& cache) {
            if (refID == 0 || refID == kUnknownFatherFormID) {
                return false;
            }
            if (worldSnap && worldSnap->lineage) {
                if (const auto* childRec = worldSnap->lineage->FindChild(State::ChildId{ refID });
                    childRec && childRec->hasBeastblood) {
                    return true;
                }
            }

            auto* wwChangeSpell = cache.GetWerewolfChangeSpell();
            auto* wwImmunitySpell = cache.GetWerewolfImmunitySpell();
            auto* wwBeastRace = cache.GetWerewolfBeastRace();

            if (isPlayer) {
                if (wwBeastRace && actor->GetRace() == wwBeastRace) {
                    return true;
                }
                if (wwChangeSpell && actor->HasSpell(wwChangeSpell)) {
                    return true;
                }
                if (wwImmunitySpell && actor->HasSpell(wwImmunitySpell)) {
                    return true;
                }
                return false;
            }

            if (baseID == 0x0001A692 || refID == 0x0001A693) {
                if (auto* cr13 = cache.GetCR13Quest(); cr13 && cr13->IsCompleted()) {
                    return false;
                }
            }
            if (baseID == 0x0001A694 || refID == 0x0001A695) {
                if (auto* cr14 = cache.GetCR14Quest(); cr14 && cr14->IsCompleted()) {
                    return false;
                }
            }

            if (baseID == 0x0001BDB0 || refID == 0x0001BDE7 ||
                baseID == 0x000136AC || refID == 0x0002ABBD) {
                return true;
            }

            auto* playerWwFaction = cache.GetPlayerWerewolfFaction();
            auto* wwFaction = cache.GetWerewolfFaction();
            auto* circleFaction = cache.GetCompanionsCircleFaction();
            auto* frostmoonFaction = cache.GetFrostmoonWerewolvesFaction();

            if (wwBeastRace && actor->GetRace() == wwBeastRace) {
                return true;
            }
            if (wwChangeSpell && actor->HasSpell(wwChangeSpell)) {
                return true;
            }
            if (wwImmunitySpell && actor->HasSpell(wwImmunitySpell)) {
                return true;
            }
            if (playerWwFaction && actor->IsInFaction(playerWwFaction)) {
                return true;
            }
            if (wwFaction && actor->IsInFaction(wwFaction)) {
                return true;
            }
            if (circleFaction && actor->IsInFaction(circleFaction)) {
                return true;
            }
            if (frostmoonFaction && actor->IsInFaction(frostmoonFaction)) {
                return true;
            }

            if (base) {
                if (wwBeastRace && base->race == wwBeastRace) {
                    return true;
                }
                if (auto* spellData = base->GetSpellList()) {
                    for (std::uint32_t i = 0; i < spellData->numSpells; ++i) {
                        auto* sp = spellData->spells ? spellData->spells[i] : nullptr;
                        if (sp && (sp == wwChangeSpell || sp == wwImmunitySpell)) {
                            return true;
                        }
                    }
                }
                for (const auto& f : base->factions) {
                    if (f.rank < 0 || !f.faction) {
                        continue;
                    }
                    if (f.faction == playerWwFaction ||
                        f.faction == wwFaction ||
                        f.faction == circleFaction ||
                        f.faction == frostmoonFaction) {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] State::LoadedActorSample BuildLoadedActorSample(
            RE::Actor* actor,
            bool isPlayer,
            const State::WorldStateSnapshot* worldSnap,
            FormCache& cache,
            RE::BGSKeyword* kwActorTypeNpc,
            RE::TESFaction* trackedFaction) {
            State::LoadedActorSample sample{};
            const RE::FormID refID = actor->GetFormID();
            RE::TESNPC* base = actor->GetActorBase();
            const RE::FormID baseID = base ? base->GetFormID() : 0;

            sample.refId = State::RefId{ refID };
            sample.baseId = State::BaseId{ baseID };

            if (auto* cell = actor->GetParentCell()) {
                sample.parentCellId = State::CellId{ cell->GetFormID() };
            }
            if (auto* loc = actor->GetCurrentLocation()) {
                sample.currentLocationId = State::LocationId{ loc->GetFormID() };
            }

            auto* race = actor->GetRace();
            if (race) {
                sample.raceId = State::FormRawId{ race->GetFormID() };
            }

            const auto pos = actor->GetPosition();
            sample.x = pos.x;
            sample.y = pos.y;
            sample.z = pos.z;

            if (base) {
                const auto sex = base->GetSex();
                if (sex == RE::SEX::kFemale) {
                    sample.sex = State::ActorSex::Female;
                } else if (sex == RE::SEX::kMale) {
                    sample.sex = State::ActorSex::Male;
                }
            }

            const char* rawName = actor->GetDisplayFullName();
            if (!rawName || rawName[0] == '\0') {
                rawName = actor->GetName();
            }
            if ((!rawName || rawName[0] == '\0') && base) {
                rawName = base->GetName();
            }
            if (rawName && rawName[0] != '\0') {
                sample.SetDisplayName(std::string_view{ rawName });
            }

            const bool isDeleted = actor->IsDeleted();
            const bool is3DLoaded = actor->Is3DLoaded();

            sample.SetFlag(State::LoadedActorFlags::IsPlayer, isPlayer);
            sample.SetFlag(State::LoadedActorFlags::HasCurrentProcess, actor->GetActorRuntimeData().currentProcess != nullptr);
            sample.SetFlag(State::LoadedActorFlags::Is3DLoaded, is3DLoaded);
            sample.SetFlag(State::LoadedActorFlags::IsDead, actor->IsDead());
            sample.SetFlag(State::LoadedActorFlags::IsDisabled, actor->IsDisabled());
            sample.SetFlag(State::LoadedActorFlags::IsDeleted, isDeleted);
            sample.SetFlag(State::LoadedActorFlags::IsChild, actor->IsChild());
            sample.SetFlag(State::LoadedActorFlags::IsInCombat, actor->IsInCombat());
            sample.SetFlag(State::LoadedActorFlags::IsInScene, actor->GetCurrentScene() != nullptr);
            sample.SetFlag(State::LoadedActorFlags::IsMannequin, Utils::IsMannequin(actor));
            sample.SetFlag(
                State::LoadedActorFlags::HasActorTypeNpc,
                race != nullptr && (!kwActorTypeNpc || race->HasKeyword(kwActorTypeNpc)));
            sample.SetFlag(
                State::LoadedActorFlags::IsPlayerTeammate,
                !isPlayer && is3DLoaded && !isDeleted && Utils::IsPlayerFollower(actor));
            sample.SetFlag(
                State::LoadedActorFlags::IsWerewolf,
                IsLoadedActorWerewolfNoAlloc(actor, base, refID, baseID, isPlayer, worldSnap, cache));
            sample.SetFlag(
                State::LoadedActorFlags::IsTrackedFaction,
                trackedFaction != nullptr && actor->IsInFaction(trackedFaction));
            sample.SetFlag(
                State::LoadedActorFlags::IsRegisteredAdult,
                worldSnap && worldSnap->actors && worldSnap->actors->IsRegisteredAdult(sample.refId));

            return sample;
        }

    }  // namespace

    PollingService& PollingService::GetSingleton() {
        static PollingService instance;
        return instance;
    }

    void PollingService::Initialize() {
        auto now = std::chrono::steady_clock::now();
        lastSpatialSnapshot_ = now;
        lastTeammateCheck_ = now;
        lastDayCheck_ = now;
        lastChildOrderCheck_ = now;

        PublishSpatialSnapshot();

        // Initialize/reset state to current values to prevent false change events
        previousTeammates_ = GetCurrentTeammates();
        previousGameDay_ = GetCurrentGameDay();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            previousPlayerLevel_ = player->GetLevel();
        } else {
            previousPlayerLevel_ = 0;
        }

        SanitizePlayerFactions();

        // Check auto-marriage on load in case an engagement matured while away
        if (previousGameDay_ >= 0.0f) {
            const float day = previousGameDay_;
            AddCurrentEpochTask("PollingService::CheckAutoMarriageOnLoad", [day]() {
                NPCRelationshipManager::GetSingleton().CheckAutoMarriageForEngagedNPCs(day);
            });
        }

        if (!initialized_) {
            initialized_ = true;
            MARAS_LOG_INFO("PollingService initialized");
        } else {
            MARAS_LOG_INFO("PollingService state reset for game load");
        }
    }

    void PollingService::Shutdown() {
        const bool wasInitialized = initialized_;
        previousTeammates_.clear();
        previousPlayerLevel_ = 0;
        previousGameDay_ = -1.0f;
        lastSpatialSnapshot_ = TimePoint{};
        initialized_ = false;
        State::SpatialSnapshotStore::GetSingleton().Reset();
        if (wasInitialized) {
            MARAS_LOG_INFO("PollingService shutdown");
        }
    }

    void PollingService::Update() {
        if (!initialized_) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) {
            MARAS_LOG_TRACE("Game is paused; skipping PollingService update");
            return;
        }

        std::lock_guard papyrusLock(PapyrusInterface::GetPapyrusMutex());

        auto now = std::chrono::steady_clock::now();

        if (now - lastSpatialSnapshot_ >= kSpatialSnapshotInterval) {
            PublishSpatialSnapshot();
            lastSpatialSnapshot_ = now;
        }

        // Check if Player leveled up (handles LevelUpMenu close, Experience mod, or console)
        CheckPlayerLevelChanged();

        // Poll active intimate threads and child flee state (~1.5s interval when active)
        IntimateThreadService::GetSingleton().Tick(now);
        ChildrenFleeService::GetSingleton().Tick(now);

        // Check teammates and sanitize player factions every 15 seconds
        if (now - lastTeammateCheck_ >= kTeammateCheckInterval) {
            CheckTeammateChanges();
            SanitizePlayerFactions();
            lastTeammateCheck_ = now;
        }

        // Check day changes every 5 seconds
        if (now - lastDayCheck_ >= kDayCheckInterval) {
            CheckDayChanged();
            lastDayCheck_ = now;
        }

        // Check child order expirations, child training progress, and adult child travel arrival every 5 seconds
        if (now - lastChildOrderCheck_ >= kChildOrderCheckInterval) {
            const float currentDay = GetCurrentGameDay();
            if (currentDay >= 0.0f) {
                PlayerChildrenService::GetSingleton().Tick(currentDay);
                NPCRelationshipManager::GetSingleton().CheckChildrenTrainingProgress(currentDay);
                AdultChildTravelService::GetSingleton().Tick(currentDay);
            }
            lastChildOrderCheck_ = now;
        }

        // Flush expired 90s memory episodes
        SkyrimNetMemoryService::GetSingleton().Tick();
    }

    void PollingService::CheckTeammateChanges() {
        auto currentTeammates = GetCurrentTeammates();

        // Find added and removed teammates
        std::unordered_set<FormID> added;
        std::unordered_set<FormID> removed;

        // Find new teammates (in current but not in previous)
        for (auto formID : currentTeammates) {
            if (previousTeammates_.find(formID) == previousTeammates_.end()) {
                added.insert(formID);
            }
        }

        // Find removed teammates (in previous but not in current)
        for (auto formID : previousTeammates_) {
            if (currentTeammates.find(formID) == currentTeammates.end()) {
                removed.insert(formID);
            }
        }

        // Send event if there are changes
        if (!added.empty() || !removed.empty()) {
            MARAS_LOG_INFO("Teammate changes detected: {} added, {} removed", added.size(), removed.size());
            SendTeammateChangeEvent(added, removed);
            previousTeammates_ = std::move(currentTeammates);
        }
    }

    void PollingService::CheckDayChanged() {
        float currentDay = GetCurrentGameDay();
        if (currentDay < 0.0f) {
            return;
        }
        if (previousGameDay_ < 0.0f) {
            previousGameDay_ = currentDay;
            return;
        }

        // Check if day has changed (comparing integer part)
        if (static_cast<int>(currentDay) != static_cast<int>(previousGameDay_)) {
            MARAS_LOG_INFO("Game day changed: {} -> {}", previousGameDay_, currentDay);

            // Notify AffectionService to process decay
            AffectionService::GetSingleton().OnDayChanged();

            // Roll forcegreet chance for children households once per game day
            PlayerChildrenService::GetSingleton().RollForcegreetForHousehold();

            // Trigger scheduled matchmaking check
            MatchmakingService::GetSingleton().TryScheduledMatchmaking(currentDay);
            MatchmakingService::GetSingleton().TryScheduledPlayerPartnerInfidelity(currentDay);

            // Daily paternity discovery roll for active secret paternity intrigue
            NPCRelationshipManager::GetSingleton().CheckPaternitySecretsDailyTick(currentDay);

            // Check auto-marriage for engaged NPC couples after 5 days
            NPCRelationshipManager::GetSingleton().CheckAutoMarriageForEngagedNPCs(currentDay);

            // Check if any children in training have completed their training duration
            NPCRelationshipManager::GetSingleton().CheckChildrenTrainingProgress(currentDay);

            // Rotate travel destinations for independent Stage 4 adult children
            AdultChildTravelService::GetSingleton().OnDayChanged(currentDay);

            SendDayChangeEvent(currentDay);
            previousGameDay_ = currentDay;
        }
    }

    void PollingService::CheckPlayerLevelChanged() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        const std::uint16_t currentLevel = player->GetLevel();
        if (currentLevel == 0) {
            return;
        }
        if (previousPlayerLevel_ == 0) {
            previousPlayerLevel_ = currentLevel;
            return;
        }
        if (currentLevel != previousPlayerLevel_) {
            MARAS_LOG_INFO("Player level changed: {} -> {} — scaling active adult children",
                           previousPlayerLevel_, currentLevel);
            previousPlayerLevel_ = currentLevel;
            NPCRelationshipManager::GetSingleton().ReassertAllChildVisualsOnLoad();
        }
    }


    std::unordered_set<PollingService::FormID> PollingService::GetCurrentTeammates() {
        std::unordered_set<FormID> teammates;
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!spatialSnap) {
            return teammates;
        }

        const auto teammateIds = spatialSnap->GetPlayerTeammates(worldSnap ? worldSnap->actors.get() : nullptr);
        teammates.reserve(teammateIds.size());
        for (const auto refId : teammateIds) {
            if (!refId.IsNull()) {
                teammates.insert(refId.raw());
            }
        }

        return teammates;
    }

    float PollingService::GetCurrentGameDay() {
        auto calendar = RE::Calendar::GetSingleton();
        if (!calendar) {
            MARAS_LOG_ERROR("Calendar is null");
            return -1.0f;
        }

        // Get days passed since game start
        float daysPassed = calendar->GetDaysPassed();
        return daysPassed;
    }

    void PollingService::SendTeammateChangeEvent(const std::unordered_set<FormID>& added,
                                                 const std::unordered_set<FormID>& removed) {
        auto eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send teammate change event");
            return;
        }

        if (!added.empty() || !removed.empty()) {
            MARAS_LOG_INFO("Sending teammate change event: {} added, {} removed", added.size(), removed.size());
            SKSE::ModCallbackEvent modEvent("maras_teammate_change", "", 0.0f, nullptr);
            eventSource->SendEvent(&modEvent);
        }

        // Send event for added teammates
        for (auto formID : added) {
            auto actorForm = RE::TESForm::LookupByID(formID);
            if (actorForm) {
                SKSE::ModCallbackEvent modEvent("maras_teammate_added", "", 0.0f, actorForm);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("Sent maras_teammate_added event for actor {:08X}", formID);
            }
        }

        // Send event for removed teammates
        for (auto formID : removed) {
            auto actorForm = RE::TESForm::LookupByID(formID);
            if (actorForm) {
                SKSE::ModCallbackEvent modEvent("maras_teammate_removed", "", 0.0f, actorForm);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("Sent maras_teammate_removed event for actor {:08X}", formID);
            }
        }
    }

    void PollingService::SendDayChangeEvent(float newDay) {
        auto eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send day change event");
            return;
        }

        SKSE::ModCallbackEvent modEvent("maras_day_changed", "", newDay, nullptr);
        eventSource->SendEvent(&modEvent);
        MARAS_LOG_INFO("Sent maras_day_changed event for day {}", newDay);
    }

    void PollingService::SanitizePlayerFactions() {
        FactionService::GetSingleton().SanitizePlayerMarriedFaction();
    }

    void PollingService::PublishSpatialSnapshot() {
        auto* cal = RE::Calendar::GetSingleton();
        const float currentGameDay = cal ? cal->GetDaysPassed() : -1.0f;

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        auto& cache = FormCache::GetSingleton();
        auto* trackedFaction = cache.GetTrackedFaction();
        static RE::BGSKeyword* s_kwActorTypeNpc =
            RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* processLists = RE::ProcessLists::GetSingleton();

        State::SpatialSnapshotStore::GetSingleton().Publish(
            currentGameDay,
            [&](State::SpatialActorSnapshot& outSnap) {
                if (player && player->GetFormID() != 0) {
                    outSnap.AddSample(BuildLoadedActorSample(
                        player,
                        /*isPlayer=*/true,
                        worldSnap.get(),
                        cache,
                        s_kwActorTypeNpc,
                        trackedFaction));
                }

                if (!processLists) {
                    return;
                }

                for (auto& handle : processLists->highActorHandles) {
                    if (!handle) {
                        continue;
                    }
                    auto actorPtr = handle.get();
                    RE::Actor* actor = actorPtr.get();
                    if (!actor || actor == player || actor->GetFormID() == 0) {
                        continue;
                    }
                    outSnap.AddSample(BuildLoadedActorSample(
                        actor,
                        /*isPlayer=*/false,
                        worldSnap.get(),
                        cache,
                        s_kwActorTypeNpc,
                        trackedFaction));
                }
            });
    }

}  // namespace MARAS
