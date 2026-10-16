#pragma once

#include "utils/Common.h"

namespace MARAS {

    class FactionService {
    public:
        using FactionAddedCallback = std::function<void(RE::Actor*, RE::TESFaction*, std::int8_t)>;
        using FactionRemovedCallback = std::function<void(RE::Actor*, RE::TESFaction*)>;

        static FactionService& GetSingleton();

        static void InstallHooks();

        // Removes PlayerMarriedFaction from the player if polygamy is currently enabled.
        void SanitizePlayerMarriedFaction();

        // RAII scope to mark internal MARAS faction mutations so engine hooks ignore them
        struct InternalFactionScope {
            InternalFactionScope();
            ~InternalFactionScope();
        };

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

        std::shared_mutex m_mutex;
        std::vector<FactionAddedCallback> m_addedListeners;
        std::vector<FactionRemovedCallback> m_removedListeners;
    };

}  // namespace MARAS
