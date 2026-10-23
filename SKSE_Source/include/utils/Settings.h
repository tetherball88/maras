#pragma once

#include "PCH.h"

namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    class Settings {
    public:
        static Settings& GetSingleton() {
            static Settings singleton;
            return singleton;
        }

        void Load();

        // Hotkey configuration for Prisma UI
        uint32_t toggleUIKey = 0x30; // Default: Alt+B
        bool toggleUIModCtrl = false;
        bool toggleUIModShift = false;
        bool toggleUIModAlt = true;

        // Confirmation modal settings
        bool enableConfirmationModal = true;
        int32_t actionsCooldown = 10;
        int32_t actionsDeclineCooldown = 60;

        // Participant-type confirmation settings (cosave persisted)
        bool enableConfirmPlayerNpc = true;
        bool enableConfirmNpcNpc = true;

        // Per-action confirmation settings (cosave persisted)
        bool confirmStatusChange_Dating = true;
        bool confirmStatusChange_Engagement = true;
        bool confirmStatusChange_Lover = true;
        bool confirmStatusChange_Marry = true;
        bool confirmBreakup_Dating = true;
        bool confirmBreakup_Engagement = true;
        bool confirmBreakup_Lover = true;
        bool confirmDivorce = true;
        bool confirmDivorceDuringLowAffectionQuest = false;
        bool confirmOpenRelationship = true;
        bool confirmCloseRelationship = true;

        // Game settings (cosave persisted)
        bool skipWedding = false;
        bool alwaysSuccessMarriage = false;
        int32_t logDestination = 0;
        int32_t defaultLogLevel = 1; // Default log level loaded from INI
        int32_t logLevel = 1; // Default to debug (overridden by INI / cosave)
        float affectionDecayMultiplier = 1.0f;
        int32_t lonelinessThresholdDays = 2;
        float skyrimnetActionRadius = 3000.0f;
        bool disablePolygamyQuest = false;
        bool migratedFromStorageUtil = false;
        bool initialRelationshipScanDone = false;
        bool simulateNoSkyrimNet = false;
        enum class ChildCustodyGenderPolicy : uint8_t {
            StayWithMother = 0,
            StayWithFather = 1
        };

        enum class ChildCustodyPlayerPolicy : uint8_t {
            UseGenderCustody = 0,
            AlwaysWithPlayer = 1,
            NeverWithPlayer = 2
        };

        ChildCustodyGenderPolicy childCustodyGenderPolicy = ChildCustodyGenderPolicy::StayWithMother;
        ChildCustodyPlayerPolicy childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::UseGenderCustody;
        bool childCustodyPrioritizeBioParent = true;

        enum class MatchmakingOrientation : uint8_t {
            Any = 0,
            Heterosexual = 1,
            Homosexual = 2,
            Gay = 3,
            Lesbian = 4,
            HeteroAndGay = 5,
            HeteroAndLesbian = 6
        };

        // Matchmaking settings (cosave persisted)
        bool matchmakingEnabled = true;
        int32_t matchmakingIntervalDays = 5;
        int32_t matchmakingMaxSuggestions = 5;
        int32_t matchmakingNpcCooldownDays = 14;
        bool matchmakingAllowBreakups = true;
        bool matchmakingAllowOpenClose = true;
        bool matchmakingAllowCheating = true;
        MatchmakingOrientation matchmakingOrientation = MatchmakingOrientation::Heterosexual;
        bool matchmakingExcludePlayerActivePartners = false;

        // Dice Roll matchmaking mode toggle (false = Dice Roll, true = LLM AI)
        // Only meaningful when SkyrimNet is available; non-SkyrimNet always uses Dice Roll
        bool matchmakingUseLLM = true;

        // SkyrimNet scheduled matchmaking toggles
        bool matchmakingEvaluationStep = true;
        bool matchmakingGenerateMemories = true;
        bool matchmakingConfirmResults = false;

        // Player-Partner Infidelity settings (cosave persisted)
        bool playerPartnerCheatingAgencyEnabled = true;
        bool playerPartnerOpenRelAgencyEnabled = true;

        float playerPartnerCheatingChanceMultiplier = 1.0f;
        int32_t playerPartnerCheatingMaxRollCap = 65;
        int32_t playerPartnerCheatingMaxSecretLovers = 1;
        bool playerPartnerCheatingAllowWhenHappy = false;
        bool playerPartnerCheatingEnableNeglect = true;
        bool playerPartnerCheatingEnableRevenge = true;
        bool playerPartnerCheatingEnableHierarchy = true;
        bool playerPartnerCheatingEnableChildrenAnchor = true;
        bool playerPartnerCheatingUseLLM = true;
        bool playerPartnerCheatingConfirmResults = false;
        float playerPartnerAgencyOpenLoverChanceMultiplier = 1.0f;
        float playerPartnerAgencyBreakupChanceMultiplier = 1.0f;
        bool playerPartnerAgencyEnableForceGreets = true;
        bool playerPartnerAgencyEnableHomeArrivalEvents = true;

        // =========================================================================
        // Configurable Loneliness & Partner Agency Day-Threshold Multipliers (cosave persisted)
        // All day thresholds in AffectionService and Tier 2 Partner Agency derive
        // from `lonelinessThresholdDays` (L, default = 2 days) multiplied by these settings.
        // =========================================================================
        float multLonelinessDecayStart      = 1.0f; // 1.0x L (default 2d) - AffectionService daily loneliness decay onset
        float multPartnerAgencyScanInterval = 1.5f; // 1.5x L (default 3d) - Tier 2 Partner Agency evaluation interval
        float multRecentAttentionWindow     = 1.0f; // 1.0x L (default 2d) - Recent player visit mitigation & guilt bonus
        float multVeryRecentVisitWindow     = 0.5f; // 0.5x L (default 1d) - Very recent player visit bonus for open partners
        float multNeglectGrowthStart        = 1.0f; // 1.0x L (default 2d) - Neglect growth onset (Troubled/Estranged & Open)
        float multContentNeglectStart       = 2.0f; // 2.0x L (default 4d) - Neglect growth onset for Content (50-74) partners
        float multProlongedAbsencePenalty   = 2.0f; // 2.0x L (default 4d) - Prolonged player absence penalty for affair breakups
        float multAffairTalkingEscalation   = 2.0f; // 2.0x L (default 4d) - Deterministic Talking -> Physical affair escalation
        float multMinSideFlingAgeForBreakup = 1.5f; // 1.5x L (default 3d) - Minimum age of an affair/open lover before breakup roll
        float multPartnerIntentTTL          = 2.0f; // 2.0x L (default 4d) - PendingPartnerIntent expiration window
        float multHomeArrivalCooldown       = 1.0f; // 1.0x L (default 2d) - Per-location cooldown between home-arrival events
        float multAffairFatigueStart        = 5.0f; // 5.0x L (default 10d) - Affair fatigue / open fling novelty fade onset
        float multRomanticAttendedBreakup   = 1.5f; // 1.5x L (default 3d) - Romantic partner recently attended breakup window
        float multSuggestOpenNeglectMin     = 2.0f; // 2.0x L (default 4d) - Neglect onset for SuggestOpen bonus

        float GetBaseLonelinessDays() const noexcept {
            return static_cast<float>(std::clamp(lonelinessThresholdDays, 1, 14));
        }
        float GetLonelinessDecayStartDays() const noexcept         { return GetBaseLonelinessDays() * multLonelinessDecayStart; }
        float GetPartnerAgencyScanIntervalDays() const noexcept    { return GetBaseLonelinessDays() * multPartnerAgencyScanInterval; }
        float GetAffairTalkingEscalationDays() const noexcept      { return GetBaseLonelinessDays() * multAffairTalkingEscalation; }
        float GetPartnerIntentTTLDays() const noexcept             { return GetBaseLonelinessDays() * multPartnerIntentTTL; }
        float GetHomeArrivalCooldownDays() const noexcept          { return GetBaseLonelinessDays() * multHomeArrivalCooldown; }
        float GetRecentEffortMaxDays() const noexcept              { return GetBaseLonelinessDays() * multRecentAttentionWindow; }
        float GetOpenRecentVisitMitigationMaxDays() const noexcept { return GetBaseLonelinessDays() * multVeryRecentVisitWindow; }
        float GetNeglectTroubledStartDays() const noexcept         { return GetBaseLonelinessDays() * multNeglectGrowthStart; }
        float GetNeglectContentStartDays() const noexcept          { return GetBaseLonelinessDays() * multContentNeglectStart; }
        float GetProlongedAbsencePenaltyDays() const noexcept      { return GetBaseLonelinessDays() * multProlongedAbsencePenalty; }
        float GetMinSideLoverAgeForBreakupDays() const noexcept    { return GetBaseLonelinessDays() * multMinSideFlingAgeForBreakup; }
        float GetAffairFatigueStartDays() const noexcept           { return GetBaseLonelinessDays() * multAffairFatigueStart; }
        float GetOpenFlingNoveltyDays() const noexcept             { return GetBaseLonelinessDays() * multAffairFatigueStart; }
        float GetRomanticAttendedBreakupMaxDays() const noexcept   { return GetBaseLonelinessDays() * multRomanticAttendedBreakup; }
        float GetSuggestOpenNeglectMinDays() const noexcept        { return GetBaseLonelinessDays() * multSuggestOpenNeglectMin; }

        // Dice Roll chance weights (sum enforced = 100 in UI, stored as raw int32_t)
        int32_t diceRollChanceNewDating = 22;        // ~20% normalized
        int32_t diceRollChanceNewEngaged = 17;       // ~15% normalized
        int32_t diceRollChanceNewLoverAllowed = 17;   // ~15% normalized
        int32_t diceRollChanceNewLoverCheating = 6;  // ~5% normalized
        int32_t diceRollChanceAdvance = 22;          // ~20% normalized
        int32_t diceRollChanceBreakup = 11;          // ~10% normalized
        int32_t diceRollChanceOpenClose = 5;         // ~5% normalized (total = 100)

        // Paternity secret evaluation and non-SkyrimNet fallback settings (cosave persisted)
        int32_t paternityBaseChance = 2;               // Subtle physical trait daily check base %
        float paternityDailyGrowth = 2.0f;             // Subtle physical trait daily growth % / day
        int32_t paternityMaxChance = 60;               // Subtle physical trait max cap %
        int32_t paternityPhenotypeBaseChance = 15;     // Phenotype mismatch daily check base %
        float paternityPhenotypeDailyGrowth = 4.0f;    // Phenotype mismatch daily growth % / day
        int32_t paternityPhenotypeMaxChance = 80;      // Phenotype mismatch max cap %
        int32_t paternityPhenotypeBirthChance = 100;   // Phenotype mismatch evaluation check chance on birth %
        int32_t paternityNonSkyrimNetRevealChance = 50; // Non-SkyrimNet fallback reveal % (when no phenotype mismatch)
        int32_t paternityBreakupChance = 50;           // Presumed father breakup/divorce % upon secret reveal
        int32_t paternityBioStepUpChance = 30;         // Biological father step-in % if presumed father breaks up
        bool paternityForceDiceRolls = false;          // Opt out of SkyrimNet LLM evaluation and use dice-roll fallback instead

        // UI secret visibility mode (cosave persisted; false = hide undiscovered secrets by default)
        bool revealSecretsInUI = false;

        // Prisma UI display, accessibility & pause settings (cosave persisted)
        bool uiFullScreen = false;
        float uiScale = 1.0f;
        bool autoPauseOnOpen = false;

        // NPC Scenes settings (cosave persisted)
        bool enableScenes = true;                      // Master toggle for all scenes
        float scenesCooldownHours = 24.0f;             // Minimum game hours between NPC scenes (default 24 = at most one scene per day)
        bool enableScenes_Lover = true;
        bool enableScenes_Dating = true;
        bool enableScenes_Engaged = true;
        bool enableScenes_Married = true;
        bool enableScenes_OpenRelationship = true;
        bool enableScenes_Cheating = true;             // Secret affair
        bool enableScenes_CheatingDiscovered = true;   // Discovered / confrontation
        bool enableScenes_CheatingCaught = true;       // Caught mid-act

        // NPC Scene Location Type settings (cosave persisted)
        // Interiors
        bool enableScenes_LocTypeTemple = true;
        bool enableScenes_LocTypeStore = true;
        bool enableScenes_LocTypeStewardsDwelling = true;
        bool enableScenes_LocTypeInn = true;
        bool enableScenes_LocTypeHouse = true;
        bool enableScenes_LocTypeBarracks = true;
        bool enableScenes_LocTypeGuild = true;
        // Player houses
        bool enableScenes_LocTypePlayerHouse = true;
        bool enableScenes_BYOH_LocTypeHomestead = true;
        // Streets
        bool enableScenes_LocTypeTown = true;
        bool enableScenes_LocTypeSettlement = true;
        bool enableScenes_LocTypeOrcStronghold = true;
        bool enableScenes_LocTypeCity = true;
        bool enableScenes_LocTypeLumberMill = true;
        bool enableScenes_LocTypeFarm = true;

        // GlobalVariable-backed game settings (accessors)


        int32_t GetStartDialGender() const;
        void SetStartDialGender(int32_t value);

        bool GetEnablePolygamy() const;
        void SetEnablePolygamy(bool value);

        bool GetDisablePolygamyQuest() const;
        void SetDisablePolygamyQuest(bool value);

        int32_t GetMinRelRankForDial() const;
        void SetMinRelRankForDial(int32_t value);

        bool GetSimulateNoSkyrimNet() const { return simulateNoSkyrimNet; }
        void SetSimulateNoSkyrimNet(bool value);

        // Global critter & pet banned config (supercedes per-child ban)
        bool bGlobalBanCritter = false;
        bool IsGlobalCritterBanned() const { return bGlobalBanCritter; }
        void SetGlobalCritterBanned(bool banned) { bGlobalBanCritter = banned; }

        bool bGlobalBanPet = false;
        bool IsGlobalPetBanned() const { return bGlobalBanPet; }
        void SetGlobalPetBanned(bool banned) { bGlobalBanPet = banned; }

        // SKSE cosave serialization
        bool SaveGameSettings(SKSE::SerializationInterface* serialization) const;
        bool LoadGameSettings(SKSE::SerializationInterface* serialization);
        void RevertGameSettings();

        // Data dump
        nlohmann::json DumpJson() const;

        // Key-based O(1) setting accessors (case-insensitive, thread-safe)
        bool GetBoolByKey(std::string_view key) const;
        bool SetBoolByKey(std::string_view key, bool value);
        int32_t GetIntByKey(std::string_view key) const;
        bool SetIntByKey(std::string_view key, int32_t value);
        float GetFloatByKey(std::string_view key) const;
        bool SetFloatByKey(std::string_view key, float value);

        // Synchronization for multi-threaded readers/writers
        std::shared_mutex& GetMutex() const noexcept { return settingsMutex_; }

    private:
        Settings() = default;
        Settings(const Settings&) = delete;
        Settings& operator=(const Settings&) = delete;

        void RevertGameSettingsUnlocked();
        void SyncSimulateNoSkyrimNetGlobal(bool value) const;

        void ParseKeyCombo(const std::string& comboStr);
        static uint32_t ParseSingleKey(const std::string& keyStr);

        mutable std::shared_mutex settingsMutex_;
    };

}

