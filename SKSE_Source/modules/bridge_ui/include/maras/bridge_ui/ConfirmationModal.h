#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <vector>

namespace MARAS::Bridge::Ui {

    /// Papyrus-facing non-blocking confirmation modal helper backed by PrismaModalManager.
    class ConfirmationModal {
    public:
        /// Checks confirmation cooldown and marks the timestamp on accept; returns 0=eligible, 4=on-cooldown.
        static std::int32_t CheckAndSet(const char* actionType, const std::vector<RE::Actor*>& actors);

        /// Returns true if the given action/actor pair is currently within its confirmation cooldown window.
        static bool IsOnCooldown(const char* actionType, const std::vector<RE::Actor*>& actors);

        /// Displays a latent confirmation modal dialog to the player (resolves Papyrus stack with result code).
        static RE::BSScript::LatentStatus Show(
            RE::BSScript::Internal::VirtualMachine* vm,
            RE::VMStackID stackID,
            const char* actionType,
            const std::vector<RE::Actor*>& mainActors,
            const std::vector<RE::Actor*>& secondaryActors,
            const char* paramsJson);
    };

}  // namespace MARAS::Bridge::Ui
