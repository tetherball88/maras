#pragma once

#include "RE/Skyrim.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::App {

    /**
     * Application-layer coordinator for child lifecycle (Stage 0-4), synthetic-to-placed promotion,
     * player household child custody/orders/pets/critters, training & graduation, visual/RPG application,
     * and unified actor death + deceased archival queries.
     */
    class LineageUseCases {
    public:
        [[nodiscard]] static LineageUseCases& GetSingleton();

        LineageUseCases(const LineageUseCases&) = delete;
        LineageUseCases& operator=(const LineageUseCases&) = delete;
        LineageUseCases(LineageUseCases&&) = delete;
        LineageUseCases& operator=(LineageUseCases&&) = delete;

        // =========================================================================
        // Lifecycle & Shared Engine Metadata Helpers
        // =========================================================================

        void FinalizeCoSaveLoad(bool loadedAny);
        void ProcessDayChange(float currentGameDay);
        void RefreshAllChildVisuals();
        void Revert();

        [[nodiscard]] static std::unique_ptr<State::IActorMetadataProvider> CaptureActorMetadata(
            const std::vector<RE::FormID>& actorIDs);
        [[nodiscard]] static std::unique_ptr<State::IActorMetadataProvider> CaptureWorldActorMetadata(
            const std::shared_ptr<const State::WorldStateSnapshot>& snap,
            const std::vector<RE::FormID>& extraActorIDs = {});
        [[nodiscard]] static Foundation::DeterministicRng MakeEngineRng();

        // =========================================================================
        // Child Registration, Promotion, Custody & Removal
        // =========================================================================

        bool RegisterChildActor(
            RE::FormID childID,
            RE::FormID guardianAID,
            RE::FormID guardianBID = 0,
            RE::FormID houseLocationID = 0,
            RE::FormID bioMotherID = 0,
            RE::FormID bioFatherID = 0,
            std::int32_t syntheticID = 0,
            std::uint8_t stage = 2,
            std::optional<bool> explicitIsAdopted = std::nullopt,
            bool sendEvent = true);

        bool RemoveChild(RE::FormID childID);
        bool SetChildGuardian(RE::FormID childID, RE::FormID guardianID, bool isAdd);
        bool AddChildGuardian(RE::FormID childID, RE::FormID guardianID);
        bool RemoveChildGuardian(RE::FormID childID, RE::FormID guardianID);
        bool SetBioMother(RE::FormID childID, RE::FormID motherID);
        bool SetBioFather(RE::FormID childID, RE::FormID fatherID);
        bool SetPresumedFather(RE::FormID childID, RE::FormID fatherID);

        void AddPendingChild(RE::FormID childBaseID, RE::FormID parentABaseID, RE::FormID parentBBaseID, bool isAdopted);
        void TryResolvePendingChildren(RE::FormID baseID, RE::FormID refID);
        void ClearPendingMarriageAdoptions();
        void DispatchPlayerSpouseMarriageAdoptions(
            RE::FormID spouseID,
            std::unordered_set<RE::FormID>& skippedChildIds);

        void SyncChildHouseholdState(
            RE::FormID childID,
            RE::FormID guardianAID,
            RE::FormID guardianBID,
            RE::FormID explicitHouseID = 0);
        void EvaluateChildCustodyOnMarriage(RE::FormID actorA, RE::FormID actorB);
        void EvaluateChildCustodyOnDivorce(RE::FormID actorA, RE::FormID actorB);
        void SyncAllMarriedCouplesChildCustody();
        void EvaluateChildCustodyOnDeath(RE::FormID deceasedID);

        [[nodiscard]] RE::FormID ResolveNonPlayerChildTarget(const State::ChildRecord& child) const;
        [[nodiscard]] RE::FormID ResolveNonPlayerChildTargetById(RE::FormID childID) const;
        void UpdateNonPlayerChildPackageOverride(RE::FormID childID);
        void SetChildSandboxAroundParentLinkedRef(RE::FormID childID, RE::FormID targetRefID);
        void RemoveChildSandboxAroundParentLinkedRef(RE::FormID childID);
        void UpdateChildBothParentsFaction(RE::FormID childID);
        void UpdateSpouseChildrenFaction(RE::FormID spouseID);
        void UpdateAllSpousesChildrenFaction();

        [[nodiscard]] bool IsChildBlockedFromAdoption(RE::FormID childID, std::string* outReason = nullptr) const;
        void SendChildEvent(
            const std::string& eventType,
            RE::FormID childID,
            RE::FormID guardianAID,
            RE::FormID guardianBID,
            RE::FormID otherID = 0,
            const std::string& reason = "");

        // =========================================================================
        // Strict Read-Only Child & Naming Queries (CQS)
        // =========================================================================

        [[nodiscard]] std::optional<State::ChildRecord> GetChildEntry(RE::FormID childID) const;
        [[nodiscard]] std::unordered_map<RE::FormID, State::ChildRecord> GetAllChildren() const;
        [[nodiscard]] std::vector<RE::FormID> GetChildrenOfParent(RE::FormID parentID) const;
        [[nodiscard]] RE::FormID GetGuardianA(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetGuardianB(RE::FormID childID) const;
        [[nodiscard]] std::pair<RE::FormID, RE::FormID> GetGuardians(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetCoGuardian(RE::FormID childID, RE::FormID guardianID) const;
        [[nodiscard]] bool IsGuardian(RE::FormID childID, RE::FormID actorID) const;
        [[nodiscard]] RE::FormID GetBioMother(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetBioFather(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetPresumedFather(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetMotherPartner(RE::FormID childID) const;
        [[nodiscard]] std::size_t GetChildCount() const;

        [[nodiscard]] std::string GetChildName(RE::FormID childID) const;
        bool UpdateChildName(RE::FormID childID, const std::string& newName);
        [[nodiscard]] std::string PickRandomChildName(RE::Actor* actor, bool isFemale) const;
        [[nodiscard]] std::string PickRandomChildName(std::string_view raceName, bool isFemale) const;
        [[nodiscard]] std::string PickRandomCritterName(RE::TESForm* critterOrRace) const;

        // =========================================================================
        // Player Children Household, Orders, Pets, Critters & Interaction
        // =========================================================================

        bool OnChildAddedToHousehold(RE::FormID childID, RE::FormID houseID = 0);
        bool OnChildRemovedFromHousehold(RE::FormID childID);
        bool MoveChildToHouse(RE::FormID childID, RE::FormID houseID, bool moveActor = true);
        void ReconcileChildHouseholdOnLoad();

        bool IssueOrder(RE::FormID childID, float orderValue, float durationGameHours = 24.0f);
        [[nodiscard]] float GetCurrentOrder(RE::FormID childID) const;
        void ClearOrder(RE::FormID childID);
        void ApplyPackageForCurrentState(RE::FormID childID);
        [[nodiscard]] bool IsForcegreetPending(RE::FormID childID) const;
        void ClearForcegreet(RE::FormID childID);
        void RollForcegreetForHousehold();

        bool AssignPet(RE::FormID childID, RE::FormID petActorOrBaseID);
        bool RemovePet(RE::FormID childID);
        void RecordPetRejected(RE::FormID childID);
        [[nodiscard]] bool IsChildPetBanned(RE::FormID childID) const;
        [[nodiscard]] bool IsPetBanned(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetPet(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetChildForPet(RE::FormID petID) const;

        bool AssignCritter(RE::FormID childID, RE::FormID critterFormID);
        bool RemoveCritter(RE::FormID childID);
        void RecordCritterRejected(RE::FormID childID);
        [[nodiscard]] bool IsChildCritterBanned(RE::FormID childID) const;
        [[nodiscard]] bool IsCritterBanned(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetCritter(RE::FormID childID) const;
        [[nodiscard]] RE::FormID GetChildForCritter(RE::FormID critterID) const;
        [[nodiscard]] RE::FormID GetChildForPetOrCritter(RE::FormID animalID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllPetAndCritterIDs() const;

        [[nodiscard]] RE::FormID GetChildHouseFormID(RE::FormID childID) const;
        void SetIsNewlyAdopted(RE::FormID childID, bool newlyAdopted, bool clearSameHouse = false);
        [[nodiscard]] bool IsNewlyAdopted(RE::FormID childID) const;
        void SetChildPlayedScene(RE::FormID childID, std::int32_t sceneID, bool played = true);
        [[nodiscard]] bool HasChildPlayedScene(RE::FormID childID, std::int32_t sceneID) const;
        void RecordChildInteraction(RE::FormID childID);
        [[nodiscard]] float GetLastInteractionTimestamp(RE::FormID childID) const;

        [[nodiscard]] RE::FormID PickForcegreetChild(RE::FormID houseID = 0) const;
        [[nodiscard]] RE::FormID PickWeightedChild(const std::vector<RE::FormID>& candidateIDs) const;
        [[nodiscard]] RE::FormID GetNewlyAdoptedChild(RE::FormID playerHouseFormID) const;
        [[nodiscard]] RE::FormID GetChildWithoutPet(RE::FormID playerHouseFormID) const;
        [[nodiscard]] RE::FormID GetChildWithoutCritter(RE::FormID playerHouseFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetChildrenWithoutScene(RE::FormID playerHouseFormID, std::int32_t sceneID) const;
        [[nodiscard]] RE::FormID GetWeightedRandomChild(RE::FormID playerHouseFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllPlayerChildren(bool onlyRealActors = false) const;
        [[nodiscard]] std::vector<RE::FormID> GetChildrenForHouse(RE::FormID playerHouseFormID, bool onlyRealActors = false) const;

        // =========================================================================
        // Training, Stage 3->4 Graduation, Beastblood, Blessings & Visuals
        // =========================================================================

        [[nodiscard]] bool IsPlayerEligibleForTrainingClass(std::int32_t classIndex, std::string* outReason = nullptr) const;
        [[nodiscard]] std::string_view GetTrainingClassName(std::int32_t classIndex) const;
        [[nodiscard]] std::string_view GetTrainingLocationName(std::int32_t classIndex) const;
        [[nodiscard]] std::int32_t GetChildTrainingClass(RE::FormID childID) const;
        [[nodiscard]] std::uint8_t GetChildSavedRaceIndex(RE::FormID childID) const;
        bool SetChildStage(RE::FormID childID, Foundation::ChildStage stage);
        bool SetChildGender(RE::FormID childID, std::uint8_t gender);
        [[nodiscard]] std::uint8_t GetChildGender(RE::FormID childID) const;
        bool SetChildTrainingLocation(RE::FormID childID, const std::string& location);
        [[nodiscard]] std::string GetChildTrainingLocation(RE::FormID childID) const;

        bool SendChildToTraining(RE::FormID childID, std::int32_t trainingClassIndex, std::string* outReason = nullptr);
        bool CompleteChildTraining(RE::FormID childID);
        void CheckChildrenTrainingProgress(float currentGameDay);

        [[nodiscard]] bool HasChildBeastblood(RE::FormID childID) const;
        bool SetChildBeastblood(RE::FormID childID, bool hasBeastblood);
        bool SetChildDaedricBlessing(RE::FormID childID, const std::string& blessingId);
        [[nodiscard]] std::string GetChildDaedricBlessing(RE::FormID childID) const;

        void InitializeAndApplyChildVisualsAndRPG(RE::FormID childID);
        void ApplyChildVisualsOn3DLoad(RE::Actor* actor);
        void EvaluateChildBioLineageAtBirth(RE::FormID childID);

        // =========================================================================
        // Unified Death Handling & Deceased Archival Queries
        // =========================================================================

        bool RecordActorDeath(RE::FormID victimId, RE::FormID killerId = 0, bool isPlayerKiller = false);

        [[nodiscard]] bool IsDeceased(RE::FormID formID) const;
        [[nodiscard]] bool IsWidowOrWidower(RE::FormID actorFormID) const;
        [[nodiscard]] bool WasMarriedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        [[nodiscard]] bool WasEngagedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        [[nodiscard]] bool WasExOf(RE::FormID deceasedFormID, RE::FormID partnerFormID) const;
        [[nodiscard]] std::vector<State::DeceasedActorRecord> GetDeceasedSpousesOf(RE::FormID actorFormID) const;
        [[nodiscard]] std::vector<State::DeceasedActorRecord> GetDeceasedPartnersOf(RE::FormID actorFormID) const;
        [[nodiscard]] std::vector<State::DeceasedActorRecord> GetDeceasedChildrenOf(RE::FormID parentFormID) const;
        [[nodiscard]] bool HasDeceasedChildren(RE::FormID parentFormID) const;
        [[nodiscard]] std::optional<State::DeceasedActorRecord> FindDeceasedRecord(RE::FormID formID) const;
        [[nodiscard]] std::size_t GetDeceasedCount() const;
        [[nodiscard]] nlohmann::json DumpDeceasedJson() const;
        [[nodiscard]] nlohmann::json DumpDeceasedJson(const Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json DumpPlayerChildrenJson() const;

    private:
        LineageUseCases() = default;
        ~LineageUseCases() = default;

        void HandleDivorceCustodySideEffects(
            const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
            const State::WorldStateSnapshot& postSnap,
            RE::FormID actorA,
            RE::FormID actorB);
        void HandleDeathCustodySideEffects(
            const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
            const State::WorldStateSnapshot& postSnap,
            RE::FormID deceasedID);
        void RefreshCustodyChildrenAfterTransition(
            const std::unordered_map<RE::FormID, State::ChildRecord>& preChildren,
            const State::WorldStateSnapshot& postSnap,
            const std::unordered_set<RE::FormID>& skippedChildIds);
    };

}  // namespace MARAS::App
