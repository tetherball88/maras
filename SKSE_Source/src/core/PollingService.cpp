#include "core/PollingService.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#include "RE/A/AIProcess.h"
#include "core/AffectionService.h"
#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"

namespace MARAS {

    PollingService& PollingService::GetSingleton() {
        static PollingService instance;
        return instance;
    }

    void PollingService::Initialize() {
        auto now = std::chrono::steady_clock::now();
        lastTeammateCheck_ = now;
        lastDayCheck_ = now;

        // Initialize/reset state to current values to prevent false change events
        previousTeammates_ = GetCurrentTeammates();
        previousGameDay_ = GetCurrentGameDay();

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
        if (RE::UI::GetSingleton()->GameIsPaused()) {
            SKSE::log::debug("Game is paused; skipping PollingService update");
            return;
        }

        auto now = std::chrono::steady_clock::now();

        // Check teammates every 15 seconds
        if (now - lastTeammateCheck_ >= kTeammateCheckInterval) {
            CheckTeammateChanges();
            lastTeammateCheck_ = now;
        }

        // Check day change every 60 seconds
        if (now - lastDayCheck_ >= kDayCheckInterval) {
            CheckDayChanged();
            lastDayCheck_ = now;
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

        // Check if day has changed (comparing integer part)
        if (static_cast<int>(currentDay) != static_cast<int>(previousGameDay_)) {
            MARAS_LOG_INFO("Game day changed: {} -> {}", previousGameDay_, currentDay);

            // Notify AffectionService to process decay
            AffectionService::GetSingleton().OnDayChanged();

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

        auto player = RE::PlayerCharacter::GetSingleton();
        auto& manager = NPCRelationshipManager::GetSingleton();

        for (auto& handle : highActorHandles) {
            auto actor = handle.get();
            if (!actor) continue;
            if (!actor->Is3DLoaded()) continue;
            if (actor->IsDeleted()) continue;

            if (!manager.IsRegistered(actor->GetFormID())) {
                continue;
            }

            bool isTeammate = false;

            // Native teammate flag
            if (actor->IsPlayerTeammate()) {
                SKSE::log::info("Actor {:08X} is a native teammate", actor->GetFormID());
                isTeammate = true;
            }

            // Also treat actors running a follow package as teammates
            if (!isTeammate) {
                if (auto* pkg = actor->GetCurrentPackage()) {
                    if (pkg->packData.packType == RE::PACKAGE_PROCEDURE_TYPE::kFollow) {
                        SKSE::log::info("Actor {:08X} is following player via package", actor->GetFormID());
                        isTeammate = true;
                    }
                }
                auto& runtimeData = actor->GetActorRuntimeData();
                if (RE::AIProcess* ai = runtimeData.currentProcess) {
                    // Only consider as teammate if the actor's follow target is the player
                    if (auto targetRef = RE::TESObjectREFR::LookupByHandle(ai->followTarget)) {
                        if (!targetRef) {
                            SKSE::log::info("Actor {:08X} follow target is null", actor->GetFormID());
                        } else if (targetRef.get() == player) {
                            SKSE::log::info("Actor {:08X} is following player via AI state", actor->GetFormID());
                            isTeammate = true;
                        }
                    }
                }
            }

            if (isTeammate) {
                teammates.insert(actor->GetFormID());
            }
        }

        MARAS_LOG_DEBUG("Found {} teammates", teammates.size());
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

}  // namespace MARAS
