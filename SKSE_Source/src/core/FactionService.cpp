#include "core/FactionService.h"

namespace MARAS {

    FactionService& FactionService::GetSingleton() {
        static FactionService instance;
        return instance;
    }

    void FactionService::InstallHooks() {
        FactionReconciler::InstallHooks();
    }

    void FactionService::SanitizePlayerMarriedFaction() {
        FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
    }

    bool FactionService::IsInternalFactionUpdate() {
        return FactionReconciler::IsInternalFactionUpdate();
    }

    void FactionService::RegisterFactionAddedListener(FactionAddedCallback callback) {
        FactionReconciler::GetSingleton().RegisterFactionAddedListener(std::move(callback));
    }

    void FactionService::RegisterFactionRemovedListener(FactionRemovedCallback callback) {
        FactionReconciler::GetSingleton().RegisterFactionRemovedListener(std::move(callback));
    }

    void FactionService::NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank) {
        FactionReconciler::GetSingleton().NotifyFactionAdded(a_actor, a_faction, a_rank);
    }

    void FactionService::NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction) {
        FactionReconciler::GetSingleton().NotifyFactionRemoved(a_actor, a_faction);
    }

}  // namespace MARAS
