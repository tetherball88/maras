#pragma once

#include "RE/Skyrim.h"

#include <array>
#include <cstdint>
#include <string>

namespace MARAS::App {

    /**
     * Application-layer coordinator for child welcome-home forcegreets, pet suitability evaluation,
     * transient critter lifecycle ('PLC1'), allowance & PoorCounter updates, gift giving, and
     * child chest refilling.
     */
    class ChildInteractionCoordinator {
    public:
        [[nodiscard]] static ChildInteractionCoordinator& GetSingleton();

        ChildInteractionCoordinator(const ChildInteractionCoordinator&) = delete;
        ChildInteractionCoordinator& operator=(const ChildInteractionCoordinator&) = delete;
        ChildInteractionCoordinator(ChildInteractionCoordinator&&) = delete;
        ChildInteractionCoordinator& operator=(ChildInteractionCoordinator&&) = delete;

        [[nodiscard]] std::string SelectChildForcegreetEvent(
            RE::BGSLocation* currentLoc,
            bool hasAnimalCompanion,
            std::int32_t critterChance,
            std::int32_t nameCallingChance);

        void CommitNameCallingForcegreet(std::int32_t eventNumber);

        [[nodiscard]] std::string SelectGenericChildForcegreetFallback(RE::BGSLocation* currentLoc);

        [[nodiscard]] std::int32_t EvaluateChildPetSuitability(
            RE::Actor* pet,
            RE::BGSListForm* dogRacesList,
            RE::BGSListForm* allowedRacesList,
            RE::TESFaction* potentialPetFaction);

        [[nodiscard]] RE::Actor* SpawnChildTransientCritter(
            RE::Actor* child,
            RE::BGSListForm* maleCritters,
            RE::BGSListForm* femaleCritters);

        bool AdoptChildTransientCritter(RE::Actor* child);

        void DismissChildTransientCritter(RE::Actor* child);

        [[nodiscard]] std::int32_t GetChildPoorCounter() const;

        std::int32_t ProcessChildAllowance(RE::Actor* child, std::int32_t allowanceAmount);

        std::int32_t ProcessChildPoorGift(RE::Actor* child, RE::BGSListForm* poorGiftsList);

        void ProcessChildRegularGift(
            RE::Actor* child,
            const std::array<RE::BGSListForm*, 7>& tieredGiftLists);

        void RefillChildChestFromLists(
            RE::TESObjectREFR* childChest,
            RE::BGSLocation* houseLoc,
            RE::BGSListForm* junkList,
            const std::array<RE::BGSListForm*, 7>& tieredGiftLists);

    private:
        ChildInteractionCoordinator() = default;
        ~ChildInteractionCoordinator() = default;
    };

}  // namespace MARAS::App
