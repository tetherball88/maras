#include "core/PollingService.h"

#include "RE/A/AIProcess.h"
#include "RE/P/PackageLocation.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESPackage.h"
#include "core/AffectionService.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerChildrenService.h"
#include "core/MatchmakingService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    PollingService& PollingService::GetSingleton() {
        static PollingService instance;
        return instance;
    }

    void PollingService::Initialize() {
        auto now = std::chrono::steady_clock::now();
        lastTeammateCheck_ = now;
        lastDayCheck_ = now;
        lastChildOrderCheck_ = now;

        // Initialize/reset state to current values to prevent false change events
        previousTeammates_ = GetCurrentTeammates();
        previousGameDay_ = GetCurrentGameDay();

        SanitizePlayerFactions();

        // Check auto-marriage on load in case an engagement matured while away
        if (previousGameDay_ >= 0.0f) {
            if (auto* taskInterface = SKSE::GetTaskInterface()) {
                float day = previousGameDay_;
                taskInterface->AddTask([day]() {
                    NPCRelationshipManager::GetSingleton().CheckAutoMarriageForEngagedNPCs(day);
                });
            }
        }

        if (!initialized_) {
            initialized_ = true;
            MARAS_LOG_INFO("PollingService initialized");
        } else {
            MARAS_LOG_INFO("PollingService state reset for game load");
        }
    }

    void PollingService::Shutdown() {
        if (!initialized_) return;

        previousTeammates_.clear();
        initialized_ = false;
        MARAS_LOG_INFO("PollingService shutdown");
    }

    void PollingService::Update() {
        if (!initialized_) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) {
            MARAS_LOG_TRACE("Game is paused; skipping PollingService update");
            return;
        }

        auto now = std::chrono::steady_clock::now();

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

        // Check child order expirations every 5 seconds
        if (now - lastChildOrderCheck_ >= kChildOrderCheckInterval) {
            const float currentDay = GetCurrentGameDay();
            if (currentDay >= 0.0f) {
                PlayerChildrenService::GetSingleton().Tick(currentDay);
            }
            lastChildOrderCheck_ = now;
        }
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

            SendDayChangeEvent(currentDay);
            previousGameDay_ = currentDay;
        }
    }


    std::unordered_set<PollingService::FormID> PollingService::GetCurrentTeammates() {
        std::unordered_set<FormID> teammates;

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            MARAS_LOG_ERROR("ProcessLists is null");
            return teammates;
        }

        // Get high actors (closest to player)
        auto& highActorHandles = processLists->highActorHandles;

        auto& manager = NPCRelationshipManager::GetSingleton();

        for (auto& handle : highActorHandles) {
            auto actor = handle.get();
            if (!actor) continue;
            if (!actor->Is3DLoaded()) continue;
            if (actor->IsDeleted()) continue;

            if (!manager.IsRegistered(actor->GetFormID())) {
                continue;
            }

            if (Utils::IsPlayerFollower(actor.get())) {
                teammates.insert(actor->GetFormID());
            }
        }

        // MARAS_LOG_DEBUG("Found {} teammates", teammates.size());
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

}  // namespace MARAS
