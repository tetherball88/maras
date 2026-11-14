#pragma once

#include "PCH.h"
#include "core/FactionReconciler.h"

namespace MARAS {

    // Thin backward-compatible facade forwarding to FactionReconciler.
    class FactionService {
    public:
        using FactionAddedCallback = FactionReconciler::FactionAddedCallback;
        using FactionRemovedCallback = FactionReconciler::FactionRemovedCallback;
        using InternalFactionScope = FactionReconciler::InternalFactionScope;

        static FactionService& GetSingleton();

        static void InstallHooks();

        // Removes PlayerMarriedFaction from the player if polygamy is currently enabled.
        void SanitizePlayerMarriedFaction();

        static bool IsInternalFactionUpdate();

        // Listener registration for external systems (e.g. child adoption tracking)
        void RegisterFactionAddedListener(FactionAddedCallback callback);
        void RegisterFactionRemovedListener(FactionRemovedCallback callback);

        // Internal dispatcher called from the engine hooks
        void NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank);
        void NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction);

    private:
        FactionService() = default;
        ~FactionService() = default;
        FactionService(const FactionService&) = delete;
        FactionService& operator=(const FactionService&) = delete;
    };

}  // namespace MARAS
