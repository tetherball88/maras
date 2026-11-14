#pragma once

#include "PCH.h"

#include "maras/foundation/DomainEnums.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MARAS::App {

    /**
     * Application-layer workflow coordinator for Fertility Mode Reloaded (FMR) integration,
     * unborn/birth lineage lifecycle, bio-father step-in evaluation queues (SkyrimNet + 3-button
     * retry/dice-roll/skip modal), paternity secret intrigue & phenotype matching, daily discovery
     * ticks, and CQS pregnancy/birth message & JSON projection builders.
     */
    class PaternityWorkflowCoordinator {
    public:
        [[nodiscard]] static PaternityWorkflowCoordinator& GetSingleton();

        PaternityWorkflowCoordinator(const PaternityWorkflowCoordinator&) = delete;
        PaternityWorkflowCoordinator& operator=(const PaternityWorkflowCoordinator&) = delete;
        PaternityWorkflowCoordinator(PaternityWorkflowCoordinator&&) = delete;
        PaternityWorkflowCoordinator& operator=(PaternityWorkflowCoordinator&&) = delete;

        // =====================================================================
        // Lifecycle & Scheduled Polling
        // =====================================================================

        void FinalizeLoad();
        void FinalizeCoSaveLoad(bool loadedAny);
        void PollFertilityTick(float currentGameDay);
        void ProcessDailyTick(float currentGameDay);
        void ProcessDayChange(float currentGameDay);
        void CheckPaternitySecretsDailyTick(float currentDay);
        void Revert();

        // =====================================================================
        // Fertility Mode Reloaded (FMR) Unborn & Infant Lifecycle
        // =====================================================================

        void SetNPCPregnant(
            RE::FormID motherID,
            bool pregnant,
            RE::FormID bioFatherID = 0,
            RE::FormID presumedFatherID = 0,
            float conceptionTime = 0.0f);

        [[nodiscard]] bool IsNPCPregnant(RE::FormID motherID) const;
        [[nodiscard]] std::vector<RE::FormID> GetUnbornChildren(RE::FormID motherID) const;
        [[nodiscard]] RE::FormID FindUnbornChild(RE::FormID motherID) const;

        RE::FormID RegisterUnbornChild(
            RE::FormID motherID,
            RE::FormID bioFatherID = 0,
            float conceptionTime = 0.0f,
            bool evaluateStepIn = true);

        RE::FormID PromoteUnbornToInfant(
            RE::FormID motherID,
            float birthTime = 0.0f,
            const std::string& name = "",
            RE::FormID bioFatherID = 0);

        bool RemoveUnbornChild(RE::FormID motherID, RE::FormID bioFatherID = 0);

        RE::FormID RegisterFertilityChild(
            RE::FormID syntheticChildID,
            RE::FormID motherID,
            RE::FormID presumedFatherID,
            RE::FormID bioFatherID,
            float conceptionTime,
            float birthTime,
            const std::string& name);

        // =====================================================================
        // Paternity Secrets, Evaluation & Bio-Father Step-In Workflows
        // =====================================================================

        bool ResolvePaternitySecret(RE::FormID childID, bool discoveredByPartner, bool sendEvent = false);
        bool ResolvePaternitySecretForPartner(RE::FormID childID, bool discoveredByPartner, bool sendEvent = false);
        bool ResolvePaternitySecretForBioFather(RE::FormID childID, bool discoveredByBioFather, bool sendEvent = false);
        bool RevealPregnancyToPartner(RE::FormID childID, bool byVisibleBelly = false, bool sendEvent = true);
        bool SetPaternitySecretEdges(
            RE::FormID childID,
            bool secretForBioFather,
            bool secretForPartner,
            std::optional<bool> pregnancySecretForPartner = std::nullopt);
        bool SetChildPaternityLastCheck(RE::FormID childID, float currentDay);

        bool EvaluatePaternitySecret(RE::FormID childID);
        bool EvaluatePaternitySecretFallback(RE::FormID childID);

        bool ApplyBioFatherStepIn(RE::FormID motherID, RE::FormID bioFatherID);
        void EvaluateBioFatherStepIn(RE::FormID motherID, RE::FormID formerSpouseID = 0);

        // =====================================================================
        // Strict Read-Only Secret, Phenotype & Narration Queries (CQS)
        // =====================================================================

        [[nodiscard]] bool IsSecretForBioFather(RE::FormID childID) const;
        [[nodiscard]] bool IsSecretForPartner(RE::FormID childID) const;
        [[nodiscard]] bool IsPregnancySecretForPartner(RE::FormID childID) const;

        [[nodiscard]] std::vector<RE::FormID> GetChildrenWithActivePaternitySecrets() const;
        [[nodiscard]] std::vector<RE::FormID> GetChildrenWithActiveSecretsForActor(RE::FormID actorID) const;
        [[nodiscard]] RE::FormID FindChildPaternitySecret(
            RE::FormID parent1ID,
            RE::FormID parent2ID = 0,
            const std::string& childName = "") const;

        [[nodiscard]] Foundation::PhenotypeTier GetActorPhenotypeTier(RE::Actor* actor) const;
        [[nodiscard]] Foundation::PhenotypeTier GetFormIDPhenotypeTier(RE::FormID formID) const;
        [[nodiscard]] bool HasPhenotypeMismatch(RE::FormID father1ID, RE::FormID father2ID) const;
        [[nodiscard]] std::string GetVisualDiscrepancyDescription(
            RE::FormID motherID,
            RE::FormID presumedFatherID,
            RE::FormID bioFatherID) const;

        [[nodiscard]] bool ShouldTriggerNarration(RE::Actor* speaker, RE::Actor* target = nullptr) const;

        [[nodiscard]] std::string BuildPregnancyAnnouncedMessage(
            RE::FormID motherID,
            RE::FormID partnerID = 0,
            RE::FormID fallbackBioID = 0,
            const std::string& fallbackBioName = "",
            std::int32_t childCount = 1) const;

        [[nodiscard]] std::string BuildPregnancyAnnouncedJson(
            RE::FormID motherID,
            RE::FormID partnerID = 0,
            RE::FormID fallbackBioID = 0,
            const std::string& fallbackBioName = "",
            std::int32_t childCount = 1) const;

        [[nodiscard]] std::string BuildChildBirthMessage(
            RE::FormID motherID,
            const std::vector<RE::FormID>& childIDs = {},
            bool maskSecrets = false) const;

        [[nodiscard]] std::string BuildChildBirthJson(
            RE::FormID motherID,
            const std::vector<RE::FormID>& childIDs = {},
            bool maskSecrets = false) const;

    private:
        PaternityWorkflowCoordinator() = default;
        ~PaternityWorkflowCoordinator() = default;
    };

}  // namespace MARAS::App
