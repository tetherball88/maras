#pragma once

#include "PCH.h"
#include "core/Serialization.h"

namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    struct ChildHouseholdState {
        RE::FormID childID{ 0 };
        RE::FormID currentHouseFormID{ 0 };     // player house this child is bound to (0 = none)
        RE::FormID homeMarkerFormID{ 0 };       // child's home sandbox marker ref (0 = none)
        RE::FormID petFormID{ 0 };              // per-child pet (dogs/followers); 0 = none
        RE::FormID critterFormID{ 0 };          // per-child critter (fox/hare/etc.); 0 = none

        float currentOrder{ 0.0f };              // mirrors vanilla Variable06 semantics
        float orderExpiryGameDay{ -1.0f };       // -1.0f = no expiry pending
        bool critterBanned{ false };              // per-child critter ban flag
        bool petBanned{ false };                  // per-child pet ban flag
        bool isNewlyAdopted{ false };             // per-child newly adopted flag
        bool forcegreetPending{ false };           // per-child forcegreet slot (Variable07 mirror)
        float critterRetryTimestamp{ 0.0f };      // in-game days timestamp when critter was rejected/died
        float petRetryTimestamp{ 0.0f };          // in-game days timestamp when pet was rejected/died
        bool playedScene1{ false };               // per-child scene 1 completion flag
        bool playedScene2{ false };               // per-child scene 2 completion flag
        float lastInteractionTimestamp{ 0.0f };  // in-game days timestamp when child was last interacted with
    };

    struct FactionRankEntry {
        RE::FormID factionID{ 0 };
        std::int8_t rank{ 0 };
    };

    struct ChildFactionSnapshot {
        RE::FormID crimeFactionID{ 0 };
        std::vector<FactionRankEntry> factions;
    };

    class PlayerChildrenService {
    public:
        static PlayerChildrenService& GetSingleton();

        // Lifecycle
        void OnChildAddedToHousehold(RE::FormID childID, RE::FormID initialHouseFormID = 0);
        void OnChildRemovedFromHousehold(RE::FormID childID);

        // Low-level child house binding — sets tenant, home marker, and child sandbox packages
        bool AssignChildToHouse(RE::FormID childID, RE::FormID playerHouseFormID, bool moveChild = false);
        RE::FormID GetChildHouseFormID(RE::FormID childID) const;
        bool SetChildHomeMarker(RE::FormID childID, RE::FormID markerFormID);
        RE::FormID GetChildHomeMarker(RE::FormID childID) const;
        RE::TESObjectREFR* GetOrCreateChildHomeMarker(RE::Actor* childActor);

        // Package injection (decisions 3, 4, 19) — exclusive authority
        void ApplyPackageForCurrentState(RE::FormID childID);
        void ResetForceGreeting(RE::FormID childID);

        // Orders (decision 7) — central tick expiry
        bool IssueOrder(RE::FormID childID, float orderValue, float durationGameHours);
        void Tick(float currentGameDay);

        // Forcegreet (decision 8) — ONE shared per-household slot, weighted random pick
        void RollForcegreetForHousehold();

        // Pets (decision 11) — per-child follower pets (dogs) CRUD
        bool AssignPet(RE::FormID childID, RE::FormID petActorOrBaseID);
        RE::FormID GetPet(RE::FormID childID) const;
        bool RemovePet(RE::FormID childID);
        RE::FormID GetChildForPet(RE::FormID petID) const;

        // Pet Banned (per-child & combined check)
        void SetChildPetBanned(RE::FormID childID, bool banned);
        bool IsChildPetBanned(RE::FormID childID) const;
        bool IsPetBanned(RE::FormID childID) const; // returns GlobalPetBan || ChildPetBanned || !IsPetRetryAvailable

        // Pet Retry Cooldown (15 days check)
        void SetPetRetryTimestamp(RE::FormID childID, float timestamp);
        void RecordPetRejected(RE::FormID childID);
        float GetPetRetryTimestamp(RE::FormID childID) const;
        bool IsPetRetryAvailable(RE::FormID childID) const;

        // Critters — per-child kid critters (hares, foxes, etc.) CRUD
        bool AssignCritter(RE::FormID childID, RE::FormID critterFormID);
        RE::FormID GetCritter(RE::FormID childID) const;
        bool RemoveCritter(RE::FormID childID);
        RE::FormID GetChildForCritter(RE::FormID critterID) const;

        RE::FormID GetChildForPetOrCritter(RE::FormID petOrCritterID) const;
        std::vector<RE::FormID> GetAllPetAndCritterIDs() const;

        // Critter Banned (per-child & combined check)
        void SetChildCritterBanned(RE::FormID childID, bool banned);
        bool IsChildCritterBanned(RE::FormID childID) const;
        bool IsCritterBanned(RE::FormID childID) const; // returns GlobalCritterBan || ChildCritterBanned || !IsCritterRetryAvailable

        // Newly Adopted
        void SetIsNewlyAdopted(RE::FormID childID, bool isNewlyAdopted, bool clearSameHouse = false);
        bool IsNewlyAdopted(RE::FormID childID) const;

        // Child Chest Introduced (global across save)
        void SetChildChestIntroduced(bool introduced);
        bool IsChildChestIntroduced() const;

        // Critter Retry Cooldown (15 days check)
        void SetCritterRetryTimestamp(RE::FormID childID, float timestamp);
        void RecordCritterRejected(RE::FormID childID);
        float GetCritterRetryTimestamp(RE::FormID childID) const;
        bool IsCritterRetryAvailable(RE::FormID childID) const;

        // Scene tracking & interaction tracking
        void SetChildPlayedScene(RE::FormID childID, int32_t sceneID, bool played = true);
        bool HasChildPlayedScene(RE::FormID childID, int32_t sceneID) const;
        void RecordChildInteraction(RE::FormID childID);
        float GetLastInteractionTimestamp(RE::FormID childID) const;

        // Shared weighted random picker helper
        RE::FormID PickWeightedChild(const std::vector<RE::FormID>& candidateIDs) const;

        // Event Queries per house location
        RE::FormID GetNewlyAdoptedChild(RE::FormID playerHouseFormID) const;
        RE::FormID GetChildWithoutPet(RE::FormID playerHouseFormID) const;
        RE::FormID GetChildWithoutCritter(RE::FormID playerHouseFormID) const;
        std::vector<RE::FormID> GetChildrenWithoutScene(RE::FormID playerHouseFormID, int32_t sceneID) const;
        RE::FormID GetWeightedRandomChild(RE::FormID playerHouseFormID) const;

        // Queries (for Papyrus + Prisma UI, decision 12)
        std::optional<ChildHouseholdState> GetState(RE::FormID childID) const;
        std::vector<RE::FormID> GetAllPlayerChildren(bool onlyRealActors = false) const;
        std::vector<RE::FormID> GetChildrenForHouse(RE::FormID playerHouseFormID, bool onlyRealActors = false) const;

        // Serialization (decision 17 — own independent co-save record)
        [[nodiscard]] bool Save(SKSE::SerializationInterface* serialization) const;
        [[nodiscard]] bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

        // Data dump
        [[nodiscard]] nlohmann::json DumpJson() const;

        // Re-derives each child's package/LinkedRef/pet-LinkedRef state after a save game finishes loading
        void ReassertAllOnLoad();

        // Faction snapshotting & restoration (custody transitions)
        void SnapshotChildFactions(RE::FormID childID);
        void ClearChildExternalFactions(RE::FormID childID);
        void RestoreChildFactions(RE::FormID childID);
        [[nodiscard]] bool HasFactionSnapshot(RE::FormID childID) const;

        // Faction snapshot serialization
        [[nodiscard]] bool SaveFactionSnapshots(SKSE::SerializationInterface* serialization) const;
        [[nodiscard]] bool LoadFactionSnapshots(SKSE::SerializationInterface* serialization);
        void RevertFactionSnapshots();

    private:
        PlayerChildrenService() = default;

        std::unordered_map<RE::FormID, ChildHouseholdState> children_;
        std::unordered_map<RE::FormID, ChildFactionSnapshot> factionSnapshots_;
        bool childChestIntroduced_{ false };
    };

}  // namespace MARAS
