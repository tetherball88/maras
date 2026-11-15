#pragma once

#include "RE/Skyrim.h"
#include "maras/state/SessionEpoch.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::App {

    /**
     * Layer 4 application coordinator for SkyrimNet YAML actions (`TTM_Actions.psc`):
     * relationship proposals, responses, breakups, open-relationship toggles,
     * delayed NPC-to-NPC proposal narration, paternity reveals/discoveries,
     * and affair confession/exposure/resolution.
     */
    class SkyrimNetActionCoordinator {
    public:
        static SkyrimNetActionCoordinator& GetSingleton();

        SkyrimNetActionCoordinator(const SkyrimNetActionCoordinator&) = delete;
        SkyrimNetActionCoordinator& operator=(const SkyrimNetActionCoordinator&) = delete;
        SkyrimNetActionCoordinator(SkyrimNetActionCoordinator&&) = delete;
        SkyrimNetActionCoordinator& operator=(SkyrimNetActionCoordinator&&) = delete;

        /// Clears pending delayed narrations on co-save revert/load.
        void Revert() noexcept;

        /// Advances and emits any NPC-to-NPC proposal narrations whose 5-second unpaused delay has elapsed.
        void TickPendingNarrations(float deltaSeconds);

        /// Resolves a player alias ("Player", "Dragonborn", player display name) or unique loaded/world NPC by name.
        [[nodiscard]] static RE::Actor* ResolveActorByName(std::string_view name);

        void ProposeRelationshipChange(
            RE::Actor* akActor,
            RE::Actor* partner,
            std::string suggestedStatus,
            std::function<void(bool)> onComplete);

        void ConsiderRelationshipChange(
            RE::Actor* akActor,
            RE::Actor* partner,
            std::string decision,
            std::string status,
            std::string reasoning,
            std::function<void(bool)> onComplete);

        void BreakupRelationship(
            RE::Actor* akActor,
            std::string partnerName,
            std::string status,
            std::string reasoning,
            std::function<void(bool)> onComplete);

        void SetOpenRelationshipChange(
            RE::Actor* akActor,
            RE::Actor* partner,
            bool isOpen,
            std::string reasoning,
            std::function<void(bool)> onComplete);

        bool RevealChildSecret(
            RE::Actor* akActor,
            std::string_view targetFather,
            std::string_view childName,
            std::string_view revealMode);

        bool DiscoverChildPaternity(
            RE::Actor* akActor,
            std::string_view childName,
            std::string_view motherName);

        bool ConfessAffair(
            RE::Actor* akActor,
            std::string_view victimName,
            std::string_view affairPartnerName,
            std::string_view reasoning);

        bool ExposeAffair(
            RE::Actor* akActor,
            std::string_view victimName,
            std::string_view cheaterName,
            std::string_view reasoning);

        bool ResolveAffair(
            RE::Actor* akActor,
            std::string_view partnerName,
            std::string_view affairPartnerName,
            std::string_view resolution,
            std::string_view reasoning);

    private:
        SkyrimNetActionCoordinator() = default;
        ~SkyrimNetActionCoordinator() = default;

        struct PendingProposalNarration {
            RE::FormID proposerId{ 0 };
            RE::FormID partnerId{ 0 };
            std::string message{};
            float remainingSeconds{ 5.0f };
            State::SessionEpoch epoch{ 0 };
        };

        std::mutex pendingMutex_{};
        std::vector<PendingProposalNarration> pendingNarrations_{};
    };

}  // namespace MARAS::App
