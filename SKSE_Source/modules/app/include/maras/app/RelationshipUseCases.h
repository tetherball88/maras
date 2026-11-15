#pragma once

#include "RE/Skyrim.h"
#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/JsonOverrideLoader.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/DomainTypes.h"
#include "maras/state/WorldTables.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::App {

    struct StatusChangeRecord {
        Foundation::RelationshipStatus fromStatus{ Foundation::RelationshipStatus::Unknown };
        Foundation::RelationshipStatus toStatus{ Foundation::RelationshipStatus::Unknown };
        float timestamp{ 0.0f };
        RE::FormID initiatorFormID{ 0 };
    };

    struct CheatingRecord {
        RE::FormID cheaterID{ 0 };
        RE::FormID victimID{ 0 };
        RE::FormID affairPartnerID{ 0 };
        std::uint32_t count{ 1 };
        std::int32_t lastTimestamp{ 0 };
        bool isSecret{ false };
        Foundation::AffairType affairType{ Foundation::AffairType::Casual };
        Foundation::AffairStatus affairStatus{ Foundation::AffairStatus::Ongoing };
        Foundation::AffairStage affairStage{ Foundation::AffairStage::Physical };
        float stageStartDay{ 0.0f };
    };

    struct PendingPartnerIntent {
        Foundation::PartnerIntentType type{ Foundation::PartnerIntentType::None };
        float createdGameDay{ -1.0f };
        RE::FormID relatedLoverID{ 0 };
    };

    struct NPCProfileData {
        RE::FormID formID{ 0 };
        Foundation::SocialClass socialClass{ Foundation::SocialClass::Outcast };
        Foundation::SkillType skillType{ Foundation::SkillType::Warrior };
        Foundation::Temperament temperament{ Foundation::Temperament::Proud };
        std::optional<RE::FormID> originalHome{};
        std::optional<RE::FormID> currentHome{};
        std::optional<RE::FormID> homeMarker{};
        std::unordered_set<RE::FormID> addedKeywords{};
    };

    struct NPCRelationshipData {
        Foundation::CanonicalPairKey pairKey{};
        Foundation::RelationshipStatus status{ Foundation::RelationshipStatus::Candidate };
        Foundation::RelationshipStatus originalStatus{ Foundation::RelationshipStatus::Unknown };
        bool isOpen{ false };
        std::vector<CheatingRecord> cheatingRecords{};
        std::vector<StatusChangeRecord> statusHistory{};
    };

    struct ScannedChildEntry {
        RE::FormID childBaseID{ 0 };
        RE::FormID parentABaseID{ 0 };
        RE::FormID parentBBaseID{ 0 };
        bool isAdopted{ false };
    };

    using VanillaChildScanHandler = std::function<std::size_t(const std::vector<ScannedChildEntry>&, bool isNewGame)>;

    /**
     * Layer 4 application coordinator owning adult relationship lifecycle, classification/overrides,
     * infidelity & open-relationship rules, affection progression, spouse hierarchy, and marriage difficulty.
     * (Child/fertility workflows live in LineageUseCases & PaternityWorkflowCoordinator.)
     */
    class RelationshipUseCases {
    public:
        static constexpr RE::FormID kPlayerFormID = Foundation::kPlayerRefRawId;
        static constexpr RE::FormID kPlayerBaseFormID = Foundation::kPlayerBaseRawId;
        static constexpr RE::FormID kUnknownFatherFormID = Foundation::kUnknownFatherRawId;
        static constexpr int kAffectionMin = 0;
        static constexpr int kAffectionMax = 100;
        static constexpr int kAffectionDefault = 50;
        static constexpr float kNeverRecordedDaysSince = 999.0f;

        static RelationshipUseCases& GetSingleton();

        RelationshipUseCases(const RelationshipUseCases&) = delete;
        RelationshipUseCases& operator=(const RelationshipUseCases&) = delete;
        RelationshipUseCases(RelationshipUseCases&&) = delete;
        RelationshipUseCases& operator=(RelationshipUseCases&&) = delete;

        // ── Lifecycle, Load & Engine Hooks ───────────────────────────────────────
        void FinalizeCoSaveLoad(bool loadedAny = true);
        void FinalizeLoad();
        void Revert();
        void Clear();

        void OnActorLoaded(RE::FormID actorId, bool fromInitScript = false);
        void TryResolvePendingRelationships(RE::FormID baseID, RE::FormID refID);
        void TryResolvePendingRelationships(RE::Actor* actor);

        void ProcessPollingAutoMarriageTick(float currentGameDay);
        void CheckAutoMarriageForEngagedNPCs(float currentDay);
        void ProcessDailyAffectionTick(float currentGameDay);

        bool ExecuteQuestCommand(std::string_view commandType,
                                 RE::FormID npcId,
                                 std::string_view argument,
                                 std::string_view context);

        [[nodiscard]] bool CanAcceptExternalMarriage(RE::FormID actorId) const;
        void HandleExternalMarriageFactionAdded(RE::FormID actorId, std::int8_t rank);
        void HandleExternalMarriageFactionRemoved(RE::FormID actorId);
        void HandleIntimateCheatingCaught(RE::FormID cheaterId,
                                          RE::FormID victimId,
                                          RE::FormID affairPartnerId,
                                          bool isWalkIn);

        void SetVanillaChildScanHandler(VanillaChildScanHandler handler);

        // ── Status Classification Helpers ────────────────────────────────────────
        [[nodiscard]] static bool IsMainTier(Foundation::RelationshipStatus status) noexcept {
            return Foundation::IsMainTierStatus(status);
        }
        [[nodiscard]] static bool IsActivePartnerStatus(Foundation::RelationshipStatus status) noexcept {
            return Foundation::IsActivePartnerStatus(status);
        }
        [[nodiscard]] static bool IsBreakupStatus(Foundation::RelationshipStatus status) noexcept {
            return Foundation::IsBreakupStatus(status);
        }

        // ── Registration, Unregistration & Death ─────────────────────────────────
        bool EnsureRegistered(RE::FormID npcFormID);
        bool RegisterCandidate(RE::FormID npcFormID);
        bool RegisterAsCandidate(RE::FormID npcFormID);
        bool UnregisterCandidate(RE::FormID npcFormID);
        bool UnregisterNPC(RE::FormID npcFormID);
        bool HandleNPCDeath(RE::FormID npcFormID);

        [[nodiscard]] bool IsRegistered(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsChildNPC(RE::FormID formID) const;

        // ── Vanilla Scan & Original Relationship Status ──────────────────────────
        void ScanExistingRelationships(bool isNewGame = false);
        bool ApplyImportedVanillaStatus(RE::FormID actorA,
                                        RE::FormID actorB,
                                        Foundation::RelationshipStatus vanillaStatus,
                                        Foundation::RelationshipStatus targetStatus);
        [[nodiscard]] bool HasMarasMaritalHistory(RE::FormID npcFormID) const;
        [[nodiscard]] Foundation::RelationshipStatus GetOriginalRelationshipStatus(
            RE::FormID actorA, RE::FormID actorB) const;
        bool SetOriginalRelationshipStatus(RE::FormID actorA,
                                           RE::FormID actorB,
                                           Foundation::RelationshipStatus status);

        // ── Status Queries & Availability ────────────────────────────────────────
        [[nodiscard]] bool IsCandidate(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsEngaged(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsMarried(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsDivorced(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsMostRecentRelationshipDivorced(RE::FormID actorA, RE::FormID actorB) const;
        [[nodiscard]] bool IsJilted(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsExDating(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsExLover(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] bool IsPlayerActiveMainPartner(RE::FormID npcFormID) const;

        [[nodiscard]] bool IsAvailableForDating(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForEngaged(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForMarried(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForMarried(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForLover(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForDivorced(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForJilted(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForJilted(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForExDating(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForExDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForExLover(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForExLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForBreakup(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForBreakup(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForBreakupDating(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForBreakupDating(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForBreakupLover(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForBreakupLover(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForOpenRelationship(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForOpenRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForCloseRelationship(RE::FormID npcFormID) const;
        [[nodiscard]] bool IsAvailableForCloseRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const;
        [[nodiscard]] bool IsAvailableForStatus(RE::FormID npcFormID, Foundation::RelationshipStatus status) const;
        [[nodiscard]] bool IsAvailableForStatus(RE::FormID npcFormID, std::string_view statusName) const;
        [[nodiscard]] bool IsAvailableForStatus(RE::FormID npcFormID,
                                                RE::FormID targetFormID,
                                                Foundation::RelationshipStatus status) const;
        [[nodiscard]] bool IsAvailableForStatus(RE::FormID npcFormID,
                                                RE::FormID targetFormID,
                                                std::string_view statusName) const;

        [[nodiscard]] bool CanFormRelationship(RE::FormID actorA,
                                               RE::FormID actorB,
                                               Foundation::RelationshipStatus status,
                                               std::string* outReason = nullptr) const;
        [[nodiscard]] bool CanFormRelationship(RE::FormID actorA,
                                               RE::FormID actorB,
                                               std::string_view statusName,
                                               std::string* outReason = nullptr) const;
        [[nodiscard]] bool AreFamilyMembers(RE::FormID actorA,
                                            RE::FormID actorB,
                                            std::string* outReason = nullptr) const;
        [[nodiscard]] bool CanOpenRelationship(RE::FormID actorA, RE::FormID actorB) const;
        [[nodiscard]] bool CanCloseRelationship(RE::FormID actorA, RE::FormID actorB) const;

        // ── Status Transitions ───────────────────────────────────────────────────
        bool PromoteToStatus(RE::FormID npcFormID,
                             Foundation::RelationshipStatus status,
                             bool sendEvent = true,
                             bool breakLovers = false);
        bool PromoteToStatus(RE::FormID actorA,
                             RE::FormID actorB,
                             Foundation::RelationshipStatus targetStatus,
                             RE::FormID initiatorID = 0,
                             bool bypassDifficulty = false,
                             bool suppressModal = false);
        bool PromoteToStatus(Foundation::ActorId actorA,
                             Foundation::ActorId actorB,
                             Foundation::RelationshipStatus targetStatus,
                             Foundation::ActorId initiatorID = {},
                             bool bypassDifficulty = false,
                             bool suppressModal = false);
        bool PromoteToStatusWith(RE::FormID npcFormID,
                                 RE::FormID targetFormID,
                                 Foundation::RelationshipStatus status,
                                 bool sendEvent = true,
                                 bool breakLovers = true);

        bool PromoteToEngaged(RE::FormID npcFormID,
                              RE::FormID targetFormID = kPlayerFormID,
                              bool sendEvent = true,
                              bool breakLovers = true);
        bool PromoteToMarried(RE::FormID npcFormID,
                              RE::FormID targetFormID = kPlayerFormID,
                              bool sendEvent = true,
                              bool breakLovers = true);
        bool PromoteToDivorced(RE::FormID npcFormID,
                               RE::FormID targetFormID = kPlayerFormID,
                               bool sendEvent = true);
        bool PromoteToJilted(RE::FormID npcFormID,
                             RE::FormID targetFormID = kPlayerFormID,
                             bool sendEvent = true);
        bool PromoteToDating(RE::FormID npcFormID,
                             RE::FormID targetFormID = kPlayerFormID,
                             bool sendEvent = true,
                             bool breakLovers = true);
        bool PromoteToLover(RE::FormID npcFormID,
                            RE::FormID targetFormID = kPlayerFormID,
                            bool sendEvent = true);
        bool PromoteToExDating(RE::FormID npcFormID,
                               RE::FormID targetFormID = kPlayerFormID,
                               bool sendEvent = true);
        bool PromoteToExLover(RE::FormID npcFormID,
                              RE::FormID targetFormID = kPlayerFormID,
                              bool sendEvent = true);
        bool DemoteToCandidate(RE::FormID npcFormID,
                               RE::FormID targetFormID = kPlayerFormID,
                               bool sendEvent = true);
        bool RemoveRelationship(RE::FormID actorA, RE::FormID actorB);
        bool Breakup(RE::FormID actorA, RE::FormID actorB, bool sendEvent = true);
        int BreakAllLoverConnections(RE::FormID actorID, bool onlyCheating = false, bool sendEvent = true);

        // ── Partner, Profile & Edge Queries ──────────────────────────────────────
        [[nodiscard]] std::vector<std::pair<RE::FormID, Foundation::RelationshipStatus>> GetActiveRelationships(
            RE::FormID actorID) const;
        [[nodiscard]] std::vector<RE::FormID> GetPartners(RE::FormID npcFormID,
                                                          Foundation::RelationshipStatus status) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllPartners(RE::FormID npcFormID) const;
        [[nodiscard]] RE::FormID GetPrimaryPartner(RE::FormID actorID) const;
        [[nodiscard]] float GetEngagementTimestamp(RE::FormID actorA, RE::FormID actorB) const;

        [[nodiscard]] std::vector<RE::FormID> GetAllRegisteredNPCs() const;
        [[nodiscard]] std::vector<RE::FormID> GetAllCandidates(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllEngaged(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllMarried(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllDivorced(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllJilted(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllDating(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllLovers(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllExDating(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetAllExLovers(RE::FormID targetFormID = kPlayerFormID) const;

        [[nodiscard]] std::optional<NPCProfileData> GetNPCProfile(RE::FormID npcFormID) const;
        void UpsertNPCProfile(const NPCProfileData& profile);
        [[nodiscard]] std::optional<NPCRelationshipData> GetNPCData(RE::FormID npcFormID,
                                                                    RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::unordered_map<Foundation::CanonicalPairKey,
                                         NPCRelationshipData,
                                         Foundation::CanonicalPairKeyHash>
        GetAllRelationshipPairs() const;
        [[nodiscard]] Foundation::RelationshipStatus GetRelationshipStatus(
            RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;

        [[nodiscard]] std::vector<StatusChangeRecord> GetStatusHistory(
            RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::optional<StatusChangeRecord> GetStatusHistoryEntry(
            RE::FormID npcFormID, RE::FormID targetFormID, std::size_t index) const;
        [[nodiscard]] std::size_t GetStatusHistoryCount(
            RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID) const;
        void ClearStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID = kPlayerFormID);

        [[nodiscard]] std::size_t GetTotalRegisteredCount() const;
        [[nodiscard]] std::size_t GetCandidateCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetEngagedCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetMarriedCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetDivorcedCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetJiltedCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetDatingCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetLoverCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetExDatingCount(RE::FormID targetFormID = kPlayerFormID) const;
        [[nodiscard]] std::size_t GetExLoverCount(RE::FormID targetFormID = kPlayerFormID) const;

        // ── Classification, Overrides, Factions, Home Markers & Keywords ─────────
        [[nodiscard]] Foundation::SocialClass GetSocialClass(RE::FormID npcFormID) const;
        [[nodiscard]] Foundation::SkillType GetSkillType(RE::FormID npcFormID) const;
        [[nodiscard]] Foundation::Temperament GetTemperament(RE::FormID npcFormID) const;

        Foundation::SocialClass DetermineSocialClass(RE::FormID npcFormID);
        Foundation::SkillType DetermineSkillType(RE::FormID npcFormID);
        Foundation::Temperament DetermineTemperament(RE::FormID npcFormID);

        bool SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass);
        bool SetSkillType(RE::FormID npcFormID, std::int8_t skillType);
        bool SetTemperament(RE::FormID npcFormID, std::int8_t temperament);

        bool AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank);
        bool AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank);
        void ManageFactions(RE::FormID npcFormID,
                            RE::FormID targetFormID,
                            Foundation::RelationshipStatus status);

        bool LoadOverridesFromFolder(const std::string& folderPath);
        [[nodiscard]] bool HasSocialClassOverride(RE::FormID npcFormID) const;
        [[nodiscard]] bool HasSkillTypeOverride(RE::FormID npcFormID) const;
        [[nodiscard]] bool HasTemperamentOverride(RE::FormID npcFormID) const;
        [[nodiscard]] std::optional<std::string> GetSocialClassOverride(RE::FormID npcFormID) const;
        [[nodiscard]] std::optional<std::string> GetSkillTypeOverride(RE::FormID npcFormID) const;
        [[nodiscard]] std::optional<std::string> GetTemperamentOverride(RE::FormID npcFormID) const;
        [[nodiscard]] std::size_t GetOverrideCount() const;
        [[nodiscard]] Engine::JsonOverrideLoader::LoadStatistics GetLastOverrideLoadStats() const;

        bool SetHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID);
        [[nodiscard]] std::optional<RE::FormID> GetHomeMarker(RE::FormID npcFormID) const;
        RE::TESObjectREFR* GetOrCreateHomeMarker(RE::Actor* npcActor);
        void ClearCurrentHome(RE::FormID npcFormID);

        bool AddTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID);
        bool RemoveTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID);

        // ── Open Relationships & Infidelity ──────────────────────────────────────
        [[nodiscard]] bool IsNPCInOpenRelationship(RE::FormID actorID) const;
        [[nodiscard]] bool GetPairIsOpen(RE::FormID actorA, RE::FormID actorB) const;
        bool SetPairIsOpen(RE::FormID actorA, RE::FormID actorB, bool isOpen, bool sendEvent = true);
        bool SetPairIsOpen(Foundation::ActorId actorA,
                           Foundation::ActorId actorB,
                           bool isOpen,
                           bool sendEvent = true);
        [[nodiscard]] bool HasAnyOpenMainPair(RE::FormID actorID) const;

        bool RecordCheating(RE::FormID cheaterID,
                            RE::FormID victimID,
                            RE::FormID affairPartnerID,
                            bool isSecret = false,
                            Foundation::AffairType affairType = Foundation::AffairType::Casual,
                            Foundation::AffairStatus affairStatus = Foundation::AffairStatus::Ongoing,
                            Foundation::AffairStage affairStage = Foundation::AffairStage::Physical);
        bool RecordCheatingRecord(RE::FormID cheaterID,
                                  RE::FormID victimID,
                                  RE::FormID affairPartnerID,
                                  bool isSecret = false,
                                  Foundation::AffairType affairType = Foundation::AffairType::Casual,
                                  Foundation::AffairStatus affairStatus = Foundation::AffairStatus::Ongoing,
                                  Foundation::AffairStage affairStage = Foundation::AffairStage::Physical);
        [[nodiscard]] bool HasCheatedOn(RE::FormID cheaterID, RE::FormID victimID) const;
        [[nodiscard]] bool IsNPCCheating(RE::FormID actorID) const;
        [[nodiscard]] bool IsRelationshipCheating(RE::FormID actorA, RE::FormID actorB) const;
        [[nodiscard]] bool IsPairCheating(RE::FormID actorA, RE::FormID actorB) const;
        [[nodiscard]] std::uint32_t GetCheatingCount(RE::FormID cheaterID, RE::FormID victimID) const;
        [[nodiscard]] std::vector<RE::FormID> GetCheatedOnPartners(RE::FormID cheaterID) const;
        [[nodiscard]] std::vector<RE::FormID> GetCheatersWhoCheatedOn(RE::FormID victimID) const;
        [[nodiscard]] std::vector<CheatingRecord> GetCheatingRecords(RE::FormID actorA, RE::FormID actorB) const;
        [[nodiscard]] std::vector<CheatingRecord> GetCheatingRecordsForActor(RE::FormID actorID) const;

        bool SetCheatingAffairStatus(RE::FormID cheaterID,
                                     RE::FormID victimID,
                                     RE::FormID affairPartnerID,
                                     Foundation::AffairStatus status);
        [[nodiscard]] Foundation::AffairStatus GetCheatingAffairStatus(RE::FormID cheaterID,
                                                                        RE::FormID victimID,
                                                                        RE::FormID affairPartnerID) const;
        bool SetCheatingAffairStage(RE::FormID cheaterID,
                                    RE::FormID victimID,
                                    RE::FormID affairPartnerID,
                                    Foundation::AffairStage stage);
        [[nodiscard]] Foundation::AffairStage GetCheatingAffairStage(RE::FormID cheaterID,
                                                                      RE::FormID victimID,
                                                                      RE::FormID affairPartnerID) const;

        void SetPartnerPendingIntent(RE::FormID partnerID,
                                     Foundation::PartnerIntentType type,
                                     RE::FormID relatedLoverID = 0);
        [[nodiscard]] PendingPartnerIntent GetPartnerPendingIntent(RE::FormID partnerID) const;
        void ClearPartnerIntent(RE::FormID partnerID);

        void MarkCheatingAllowedForPair(RE::FormID actorA, RE::FormID actorB);
        void MarkPlayerCheatingAllowedOnPolygamy();
        void EvaluateCheatingOnStatusChange(RE::FormID npcFormID,
                                            RE::FormID targetFormID,
                                            Foundation::RelationshipStatus newStatus);

        bool SetCheatingDiscovered(RE::FormID cheaterID,
                                   RE::FormID affairPartnerID,
                                   Foundation::AffairDiscovery discovery);
        bool SetCheatingDiscovered(RE::FormID cheaterID,
                                   RE::FormID affairPartnerID,
                                   Foundation::AffairResolution resolution);
        bool SetCheatingDiscovered(RE::FormID cheaterID,
                                   RE::FormID affairPartnerID = 0,
                                   std::string_view statusOrResolution = "",
                                   RE::FormID victimID = 0);

        void UpdateSpouseCheatingFaction(RE::FormID npcFormID);
        void UpdateSpouseOpenRelationshipFaction(RE::FormID npcFormID);
        void UpdateCanFormNonCheatingMainFaction(RE::FormID npcFormID);
        void UpdateAllSpousesCheatingAndOpenFactions();
        void UpdateAllCanFormNonCheatingMainFactions();

        // ── Affection Progression ────────────────────────────────────────────────
        void AddDailyAffection(RE::FormID npcFormID, float amount, std::string_view type);
        void AddAffection(RE::FormID npcFormID, float amount, std::string_view type);
        [[nodiscard]] float GetDailyAffection(RE::FormID npcFormID, std::string_view type) const;

        [[nodiscard]] int GetAffection(RE::FormID npcFormID) const;
        [[nodiscard]] int GetPermanentAffection(RE::FormID npcFormID) const;
        void SetAffection(RE::FormID npcFormID, int amount, bool sendEvent = true);
        void SetPermanentAffection(RE::FormID npcFormID, int amount, bool sendEvent = true);
        void ChangeAffection(RE::FormID npcFormID, int delta, bool sendEvent = true);

        void SetAffectionMinMax(std::string_view type, int minVal, int maxVal);
        [[nodiscard]] bool HasMinMaxForType(std::string_view type) const;
        [[nodiscard]] std::pair<int, int> GetMinMaxForType(std::string_view type) const;

        void ApplyDailyAffectionsForAll();
        void OnAffectionDayChanged();

        [[nodiscard]] float GetAffectionMultiplierForValue(int permanentAffection) const;
        [[nodiscard]] float GetAffectionMultiplierForNPC(RE::FormID npcFormID) const;
        void RecordAffectionInteraction(RE::FormID npcFormID);
        [[nodiscard]] float GetDaysSinceLastAffection(RE::FormID npcFormID) const;
        void RemoveNPCAffectionData(RE::FormID npcFormID);

        void SetAffectionDecayMultiplier(float multiplier);
        [[nodiscard]] float GetAffectionDecayMultiplier() const;

        // ── Spouse Hierarchy ─────────────────────────────────────────────────────
        [[nodiscard]] int GetSpouseRank(RE::FormID npcFormID) const;
        [[nodiscard]] RE::FormID GetSpouseAtRank(int rank) const;
        bool SetSpouseRank(RE::FormID npcFormID, int rank);
        bool PromoteSpouse(RE::FormID npcFormID);
        bool DemoteSpouse(RE::FormID npcFormID);
        bool SwapSpouseRanks(RE::FormID spouseA, RE::FormID spouseB);
        void RecomputeHierarchy();
        void OnSpouseAddedToHierarchy(RE::FormID npcFormID);
        void OnSpouseRemovedFromHierarchy(RE::FormID npcFormID);
        void FillHierarchyGaps(RE::FormID excludeNpcFormID = 0, bool sendEvent = true);
        void SyncAllHierarchyFactionsAndGlobals();

        // ── Marriage Difficulty ──────────────────────────────────────────────────
        bool LoadMarriageDifficultyConfig();
        [[nodiscard]] Domain::Relationships::MarriageDifficultyResult EvaluateMarriageDifficulty(
            RE::Actor* npc,
            float intimacyAdjustment,
            float mostGold,
            float housesOwned,
            float horsesOwned,
            float questsCompleted,
            float dungeonsCleared,
            float dragonSoulsCollected,
            bool playerKiller) const;
        [[nodiscard]] float CalculateMarriageSuccessChance(RE::Actor* npc,
                                                           float intimacyAdjustment,
                                                           float mostGold,
                                                           float housesOwned,
                                                           float horsesOwned,
                                                           float questsCompleted,
                                                           float dungeonsCleared,
                                                           float dragonSoulsCollected,
                                                           bool playerKiller) const;
        [[nodiscard]] float CalculatePlayerPrestige(float mostGold,
                                                    float housesOwned,
                                                    float horsesOwned,
                                                    float questsCompleted,
                                                    float dungeonsCleared,
                                                    float dragonSoulsCollected) const;
        [[nodiscard]] float CalculateGuildAlignmentMod(RE::Actor* npc) const;

        // ── Strict Read-Only JSON Dumps & Logging (CQS) ──────────────────────────
        [[nodiscard]] nlohmann::json DumpJson() const;
        [[nodiscard]] nlohmann::json DumpJson(const Projection::PerspectiveView& view) const;
        [[nodiscard]] nlohmann::json DumpAffectionJson() const;
        [[nodiscard]] nlohmann::json DumpHierarchyJson() const;

        void LogStatistics() const;
        void LogNPCDetails(RE::FormID npcFormID) const;

    private:
        RelationshipUseCases() = default;
        ~RelationshipUseCases() = default;

        bool ChangeStatusCommon(RE::FormID npcFormID,
                                RE::FormID targetFormID,
                                Foundation::RelationshipStatus status,
                                RE::FormID explicitInitiatorID,
                                bool sendEvent,
                                bool breakLovers);
        [[nodiscard]] static RE::FormID DetermineDefaultInitiator(RE::FormID actorA, RE::FormID actorB);

        [[nodiscard]] std::size_t CountPartnersWithStatus(RE::FormID npcFormID,
                                                          Foundation::RelationshipStatus status) const;
        [[nodiscard]] const Engine::NPCOverrideData* FindOverrideData(RE::FormID npcFormID,
                                                                      RE::FormID resolvedBaseID) const;

        void ReconcileBreakupOrDivorceEngineState(RE::FormID actorA,
                                                  RE::FormID actorB,
                                                  Foundation::RelationshipStatus oldStatus,
                                                  Foundation::RelationshipStatus newStatus);
        void SyncAllMarriedCouplesChildCustody();
        void UpdateAllSpousesChildrenFaction();

        [[nodiscard]] Domain::Relationships::MarriageDifficultyInputs BuildDifficultyInputs(
            RE::Actor* npc,
            float intimacyAdjustment,
            float mostGold,
            float housesOwned,
            float horsesOwned,
            float questsCompleted,
            float dungeonsCleared,
            float dragonSoulsCollected,
            bool playerKiller) const;

        mutable std::shared_mutex overridesMutex_{};
        Engine::OverrideMap npcOverrides_{};

        mutable std::shared_mutex childScanMutex_{};
        VanillaChildScanHandler vanillaChildScanHandler_{};
    };

}  // namespace MARAS::App
