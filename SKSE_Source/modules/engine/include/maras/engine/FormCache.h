#pragma once

#include "RE/Skyrim.h"

namespace MARAS {

    class FormCache {
    public:
        static FormCache& GetSingleton();

        // Pre-populate all cached forms once at kDataLoaded
        void Initialize();

        // NOTE: explicit getters are preferred. Keep any plugin form ids
        // centralized here and cached by named getters below.

        // Faction getters
        RE::TESFaction* GetTrackedFaction();
        RE::TESFaction* GetSpouseSocialClassFaction();
        RE::TESFaction* GetSpouseSkillTypeFaction();
        RE::TESFaction* GetSpouseTemperamentFaction();
        RE::TESFaction* GetSpouseHousedFaction();
        RE::TESFaction* GetHierarchyFaction();
        RE::TESFaction* GetAffectionFaction();
        RE::TESFaction* GetPlayerChildFaction();
        RE::TESFaction* GetPlayerChildBothParentsFaction();
        RE::TESFaction* GetSpouseChildrenFaction();
        RE::TESFaction* GetPlayerChildPetFaction();
        RE::TESFaction* GetSpouseCheatingFaction();
        RE::TESFaction* GetSpouseOpenRelationshipFaction();
        RE::TESFaction* GetCanFormNonCheatingMainFaction();
        RE::TESFaction* GetSpouseSharedHouseFaction();
        RE::TESFaction* GetSpouseNoInitialHouseFaction();
        RE::TESFaction* GetCheatingNpcCaughtFaction();
        RE::TESFaction* GetBYOHRelationshipAdoptionChildOwnedFaction();
        RE::TESFaction* GetBYOHRelationshipAdoptionFaction();
        RE::TESFaction* GetBYOHRelationshipAdoptableFaction();
        [[nodiscard]] RE::TESFaction* GetSkyrimNetFilterFaction();

        // Marriage-related faction getters
        RE::TESFaction* GetMarriagePotentialFaction();
        RE::TESFaction* GetMarriageAskedFaction();
        RE::TESFaction* GetCourtingFaction();
        RE::TESFaction* GetPotentialHirelingFaction();
        RE::TESFaction* GetMarriedFaction();
        RE::TESFaction* GetPlayerBedOwnershipFaction();

        // FormList getters
        RE::BGSListForm* GetRulerFactions();
        RE::BGSListForm* GetNobleFactions();
        RE::BGSListForm* GetReligiousFactions();
        RE::BGSListForm* GetWealthyFactions();
        RE::BGSListForm* GetMiddleFactions();
        RE::BGSListForm* GetPovertyFactions();
        RE::BGSListForm* GetOutcastFactions();

        // Class list getters
        RE::BGSListForm* GetOratorClasses();
        RE::BGSListForm* GetRangerClasses();
        RE::BGSListForm* GetRogueClasses();
        RE::BGSListForm* GetCraftsmanClasses();
        RE::BGSListForm* GetMageClasses();
        RE::BGSListForm* GetWarriorClasses();

        // Keywords getters
        RE::BGSKeyword* GetIgnoreProposeKeyword();

        // Skyrim.esm & Dawnguard.esm faction getters
        RE::TESFaction* GetPlayerFaction();
        RE::TESFaction* GetCompanionsFaction();
        RE::TESFaction* GetThievesFaction();
        RE::TESFaction* GetBrotherhoodFaction();
        RE::TESFaction* GetCollegeFaction();
        RE::TESFaction* GetBardsFaction();
        RE::TESFaction* GetBladesFaction();
        RE::TESFaction* GetDawnguardFaction();
        RE::TESFaction* GetPotentialFollowerFaction();
        RE::TESFaction* GetCurrentFollowerFaction();
        RE::TESFaction* GetWerewolfFaction();
        RE::TESFaction* GetPlayerWerewolfFaction();
        RE::TESFaction* GetCompanionsCircleFaction();
        RE::TESFaction* GetFrostmoonWerewolvesFaction();

        // Skyrim merchant factions
        RE::TESFaction* GetMerchantFaction();
        RE::TESFaction* GetApothecaryFaction();
        RE::TESFaction* GetBlacksmithFaction();
        RE::TESFaction* GetFletcherFaction();
        RE::TESFaction* GetInnKeeperFaction();
        RE::TESFaction* GetJewelerFaction();
        RE::TESFaction* GetMiscFaction();
        RE::TESFaction* GetSpellFaction();
        RE::TESFaction* GetTailorFaction();
        RE::TESFaction* GetHunterFaction();

        // MARAS housed spouses merchant factions
        RE::TESFaction* GetApothecaryMerchantHousedFaction();
        RE::TESFaction* GetBlacksmithMerchantHousedFaction();
        RE::TESFaction* GetFletcherMerchantHousedFaction();
        RE::TESFaction* GetHunterMerchantHousedFaction();
        RE::TESFaction* GetInnkeeperMerchantHousedFaction();
        RE::TESFaction* GetJewelerMerchantHousedFaction();
        RE::TESFaction* GetMiscMerchantHousedFaction();
        RE::TESFaction* GetSpellsMerchantHousedFaction();
        RE::TESFaction* GetTailorMerchantHousedFaction();

        // Skyrim.esm quest getters
        RE::TESQuest* GetEastmarchThane();
        RE::TESQuest* GetFalkreathThane();
        RE::TESQuest* GetHaafingarThane();
        RE::TESQuest* GetHjaalmarchThane();
        RE::TESQuest* GetPaleThane();
        RE::TESQuest* GetReachThane();
        RE::TESQuest* GetRiftThane();
        RE::TESQuest* GetWhiterunThane();
        RE::TESQuest* GetWinterholdThane();

        RE::TESQuest* GetCompanionsQuest();
        RE::TESQuest* GetCollegeQuest();
        RE::TESQuest* GetThievesQuest();
        RE::TESQuest* GetBardsQuest();
        RE::TESQuest* GetDragonbornQuest();
        RE::TESQuest* GetDB01();
        RE::TESQuest* GetC03Quest();
        RE::TESQuest* GetCR13Quest();
        RE::TESQuest* GetCR14Quest();
        RE::TESQuest* GetDA05Quest();

        // Werewolf / Lycanthropy spell & race getters (Skyrim.esm)
        RE::SpellItem* GetWerewolfChangeSpell();
        RE::SpellItem* GetWerewolfImmunitySpell();
        RE::TESRace* GetWerewolfBeastRace();

        // Global getters (TT_MARAS.esp)
        RE::TESGlobal* GetLoveInterestsCount();
        RE::TESGlobal* GetSpousesCount();
        RE::TESGlobal* GetPlayerHousesCount();
        RE::TESGlobal* GetEnablePolygamyGlobal();
        RE::TESGlobal* GetStartDialGenderGlobal();
        RE::TESGlobal* GetSkyrimNetInstalledGlobal();
        RE::TESGlobal* GetMinRelRankForDialGlobal();
        RE::TESGlobal* GetPreferVanillaAudioGlobal();
        RE::TESGlobal* GetAllowAIDialGlobal();
        RE::TESGlobal* GetDatingChanceGlobal();
        RE::TESGlobal* GetLoverChanceGlobal();
        RE::TESGlobal* GetMarriageChanceGlobal();

        // Spell & Crime faction getters
        RE::SpellItem* GetBreakupCooldownSpell();
        RE::SpellItem* GetDemotedCooldownSpell();
        RE::SpellItem* GetPacifyFianceOrSpouseSpell();
        RE::TESFaction* GetDefaultCrimeFaction();

        // Quests & sleep/rested spells (TT_MARAS.esp, Skyrim.esm, HearthFires.esm)
        RE::TESQuest* GetChildLogicQuest();
        [[nodiscard]] RE::TESQuest* GetSkyUiConfigManagerQuest();
        [[nodiscard]] RE::TESQuest* GetFmrMcmQuest();
        [[nodiscard]] RE::TESQuest* GetFmrHandlerQuest();
        [[nodiscard]] RE::TESQuest* GetBeeingFemaleMcmQuest();
        [[nodiscard]] RE::TESQuest* GetUiExtListMenuQuest();
        RE::SpellItem* GetLoversRadianceAbility();
        RE::TESQuest* GetPlayerSleepQuest();
        RE::SpellItem* GetRestedSpell();
        RE::SpellItem* GetWellRestedSpell();
        RE::SpellItem* GetMarriageSleepAbility();
        RE::SpellItem* GetAdoptionSleepAbilityMale();
        RE::SpellItem* GetAdoptionSleepAbilityFemale();

        // Special keyword getters
        RE::BGSKeyword* GetClothingRichKeyword();
        RE::BGSKeyword* GetHomeSandboxKeyword();
        RE::BGSKeyword* GetChildSandboxAroundParentKeyword();

        // Location type keyword getters (Skyrim.esm & HearthFires.esm)
        RE::BGSKeyword* GetLocTypeTempleKeyword();
        RE::BGSKeyword* GetLocTypeStoreKeyword();
        RE::BGSKeyword* GetLocTypeStewardsDwellingKeyword();
        RE::BGSKeyword* GetLocTypeInnKeyword();
        RE::BGSKeyword* GetLocTypeHouseKeyword();
        RE::BGSKeyword* GetLocTypeBarracksKeyword();
        RE::BGSKeyword* GetLocTypeGuildKeyword();
        RE::BGSKeyword* GetLocTypePlayerHouseKeyword();
        RE::BGSKeyword* GetBYOHLocTypeHomesteadKeyword();
        RE::BGSKeyword* GetLocTypeTownKeyword();
        RE::BGSKeyword* GetLocTypeSettlementKeyword();
        RE::BGSKeyword* GetLocTypeOrcStrongholdKeyword();
        RE::BGSKeyword* GetLocTypeCityKeyword();
        RE::BGSKeyword* GetLocTypeLumberMillKeyword();
        RE::BGSKeyword* GetLocTypeFarmKeyword();

        // Static object & ref type getters
        RE::TESObjectSTAT* GetHomeSandboxMarkerStatic();
        [[nodiscard]] RE::TESObjectSTAT* GetXMarkerStatic();
        RE::BGSLocationRefType* GetLocationCenterMarkerRefType();
        RE::TESBoundObject* GetGoldBaseObject();

        // Object reference getters
        RE::TESObjectREFR* GetHonorhallOrphanageCenterMarker();

        // Package getters
        RE::TESPackage* GetHomeSandboxPackage();
        RE::TESPackage* GetPetCritterHomeSandboxPackage();
        RE::TESPackage* GetChildrenNonPlayerFollowParentPackage();
        RE::TESPackage* GetChildrenFleeIntimateScenePackage();
        RE::TESPackage* GetAdultChildTravelSandboxPackage();

    private:
        FormCache() = default;
        ~FormCache() = default;

        FormCache(const FormCache&) = delete;
        FormCache& operator=(const FormCache&) = delete;

        // Cached pointers (populated eagerly in Initialize() at kDataLoaded)
        RE::TESFaction* trackedFaction_{ nullptr };
        RE::TESFaction* spouseSocialClassFaction_{ nullptr };
        RE::TESFaction* spouseSkillTypeFaction_{ nullptr };
        RE::TESFaction* spouseTemperamentFaction_{ nullptr };
        RE::TESFaction* spouseHousedFaction_{ nullptr };
        RE::TESFaction* hierarchyFaction_{ nullptr };
        RE::TESFaction* affectionFaction_{ nullptr };
        RE::TESFaction* playerChildFaction_{ nullptr };
        RE::TESFaction* playerChildBothParentsFaction_{ nullptr };
        RE::TESFaction* spouseChildrenFaction_{ nullptr };
        RE::TESFaction* playerChildPetFaction_{ nullptr };
        RE::TESFaction* spouseCheatingFaction_{ nullptr };
        RE::TESFaction* spouseOpenRelationshipFaction_{ nullptr };
        RE::TESFaction* CanFormNonCheatingMainFaction_{ nullptr };
        RE::TESFaction* spouseSharedHouseFaction_{ nullptr };
        RE::TESFaction* spouseNoInitialHouseFaction_{ nullptr };
        RE::TESFaction* cheatingNpcCaughtFaction_{ nullptr };
        RE::TESFaction* byohRelationshipAdoptionChildOwnedFaction_{ nullptr };
        RE::TESFaction* byohRelationshipAdoptionFaction_{ nullptr };
        RE::TESFaction* byohRelationshipAdoptableFaction_{ nullptr };
        RE::TESFaction* skyrimNetFilterFaction_{ nullptr };

        // Marriage-related factions (Skyrim.esm)
        RE::TESFaction* marriagePotentialFaction_{ nullptr };
        RE::TESFaction* marriageAskedFaction_{ nullptr };
        RE::TESFaction* courtingFaction_{ nullptr };
        RE::TESFaction* potentialHirelingFaction_{ nullptr };
        RE::TESFaction* marriedFaction_{ nullptr };
        RE::TESFaction* playerBedOwnershipFaction_{ nullptr };
        RE::TESFaction* currentFollowerFaction_{ nullptr };

        // Skyrim.esm & Dawnguard.esm general & guild factions
        RE::TESFaction* playerFaction_{ nullptr };
        RE::TESFaction* companionsFaction_{ nullptr };
        RE::TESFaction* thievesFaction_{ nullptr };
        RE::TESFaction* brotherhoodFaction_{ nullptr };
        RE::TESFaction* collegeFaction_{ nullptr };
        RE::TESFaction* bardsFaction_{ nullptr };
        RE::TESFaction* bladesFaction_{ nullptr };
        RE::TESFaction* dawnguardFaction_{ nullptr };
        RE::TESFaction* potentialFollowerFaction_{ nullptr };
        RE::TESFaction* werewolfFaction_{ nullptr };
        RE::TESFaction* playerWerewolfFaction_{ nullptr };
        RE::TESFaction* companionsCircleFaction_{ nullptr };
        RE::TESFaction* frostmoonWerewolvesFaction_{ nullptr };

        // Skyrim.esm merchant factions
        RE::TESFaction* merchantFaction_{ nullptr };
        RE::TESFaction* apothecaryFaction_{ nullptr };
        RE::TESFaction* blacksmithFaction_{ nullptr };
        RE::TESFaction* fletcherFaction_{ nullptr };
        RE::TESFaction* innKeeperFaction_{ nullptr };
        RE::TESFaction* jewelerFaction_{ nullptr };
        RE::TESFaction* miscFaction_{ nullptr };
        RE::TESFaction* spellFaction_{ nullptr };
        RE::TESFaction* tailorFaction_{ nullptr };
        RE::TESFaction* hunterFaction_{ nullptr };

        RE::BGSListForm* rulerFactions_{ nullptr };
        RE::BGSListForm* nobleFactions_{ nullptr };
        RE::BGSListForm* religiousFactions_{ nullptr };
        RE::BGSListForm* wealthyFactions_{ nullptr };
        RE::BGSListForm* middleFactions_{ nullptr };
        RE::BGSListForm* povertyFactions_{ nullptr };
        RE::BGSListForm* outcastFactions_{ nullptr };

        RE::BGSListForm* oratorClasses_{ nullptr };
        RE::BGSListForm* rangerClasses_{ nullptr };
        RE::BGSListForm* rogueClasses_{ nullptr };
        RE::BGSListForm* craftsmanClasses_{ nullptr };
        RE::BGSListForm* mageClasses_{ nullptr };
        RE::BGSListForm* warriorClasses_{ nullptr };

        // MARAS housed merchant factions (cached)
        RE::TESFaction* apothecaryHouseMerchantFaction_{ nullptr };
        RE::TESFaction* blacksmithHouseMerchantFaction_{ nullptr };
        RE::TESFaction* fletcherHouseMerchantFaction_{ nullptr };
        RE::TESFaction* hunterHouseMerchantFaction_{ nullptr };
        RE::TESFaction* innkeeperHouseMerchantFaction_{ nullptr };
        RE::TESFaction* jewelerHouseMerchantFaction_{ nullptr };
        RE::TESFaction* miscHouseMerchantFaction_{ nullptr };
        RE::TESFaction* spellsHouseMerchantFaction_{ nullptr };
        RE::TESFaction* tailorHouseMerchantFaction_{ nullptr };

        // Skyrim.esm quests (cached)
        RE::TESQuest* eastmarchThane_{ nullptr };
        RE::TESQuest* falkreathThane_{ nullptr };
        RE::TESQuest* haafingarThane_{ nullptr };
        RE::TESQuest* hjaalmarchThane_{ nullptr };
        RE::TESQuest* paleThane_{ nullptr };
        RE::TESQuest* reachThane_{ nullptr };
        RE::TESQuest* riftThane_{ nullptr };
        RE::TESQuest* whiterunThane_{ nullptr };
        RE::TESQuest* winterholdThane_{ nullptr };
        RE::TESQuest* companionsQuest_{ nullptr };
        RE::TESQuest* collegeQuest_{ nullptr };
        RE::TESQuest* thievesQuest_{ nullptr };
        RE::TESQuest* bardsQuest_{ nullptr };
        RE::TESQuest* dragonbornQuest_{ nullptr };
        RE::TESQuest* db01Quest_{ nullptr };
        RE::TESQuest* c03Quest_{ nullptr };
        RE::TESQuest* cr13Quest_{ nullptr };
        RE::TESQuest* cr14Quest_{ nullptr };
        RE::TESQuest* da05Quest_{ nullptr };

        // Werewolf / Lycanthropy spells & race (cached)
        RE::SpellItem* werewolfChangeSpell_{ nullptr };
        RE::SpellItem* werewolfImmunitySpell_{ nullptr };
        RE::TESRace* werewolfBeastRace_{ nullptr };

        // Globals (cached)
        RE::TESGlobal* loveInterestsCount_{ nullptr };
        RE::TESGlobal* spousesCount_{ nullptr };
        RE::TESGlobal* playerHousesCount_{ nullptr };
        RE::TESGlobal* enablePolygamyGlobal_{ nullptr };
        RE::TESGlobal* startDialGenderGlobal_{ nullptr };
        RE::TESGlobal* skyrimNetInstalledGlobal_{ nullptr };
        RE::TESGlobal* minRelRankForDialGlobal_{ nullptr };
        RE::TESGlobal* preferVanillaAudioGlobal_{ nullptr };
        RE::TESGlobal* allowAIDialGlobal_{ nullptr };
        RE::TESGlobal* datingChanceGlobal_{ nullptr };
        RE::TESGlobal* loverChanceGlobal_{ nullptr };
        RE::TESGlobal* marriageChanceGlobal_{ nullptr };

        // Relationship lifecycle spells & default crime faction (cached)
        RE::SpellItem* breakupCooldownSpell_{ nullptr };
        RE::SpellItem* demotedCooldownSpell_{ nullptr };
        RE::SpellItem* pacifyFianceOrSpouseSpell_{ nullptr };
        RE::TESFaction* defaultCrimeFaction_{ nullptr };

        // Quests & sleep/rested spells (cached)
        RE::TESQuest* childLogicQuest_{ nullptr };
        RE::TESQuest* skyUiConfigManagerQuest_{ nullptr };
        RE::TESQuest* fmrMcmQuest_{ nullptr };
        RE::TESQuest* fmrHandlerQuest_{ nullptr };
        RE::TESQuest* beeingFemaleMcmQuest_{ nullptr };
        RE::TESQuest* uiExtListMenuQuest_{ nullptr };
        bool optionalMcmQuestsProbed_{ false };
        RE::SpellItem* loversRadianceAbility_{ nullptr };
        RE::TESQuest* playerSleepQuest_{ nullptr };
        RE::SpellItem* restedSpell_{ nullptr };
        RE::SpellItem* wellRestedSpell_{ nullptr };
        RE::SpellItem* marriageSleepAbility_{ nullptr };
        RE::SpellItem* adoptionSleepAbilityMale_{ nullptr };
        RE::SpellItem* adoptionSleepAbilityFemale_{ nullptr };

        // Keywords (cached)
        RE::BGSKeyword* ignoreProposeKeyword_{ nullptr };
        RE::BGSKeyword* clothingRichKeyword_{ nullptr };
        RE::BGSKeyword* homeSandboxKeyword_{ nullptr };
        RE::BGSKeyword* childSandboxAroundParentKeyword_{ nullptr };
        RE::BGSKeyword* locTypeTempleKeyword_{ nullptr };
        RE::BGSKeyword* locTypeStoreKeyword_{ nullptr };
        RE::BGSKeyword* locTypeStewardsDwellingKeyword_{ nullptr };
        RE::BGSKeyword* locTypeInnKeyword_{ nullptr };
        RE::BGSKeyword* locTypeHouseKeyword_{ nullptr };
        RE::BGSKeyword* locTypeBarracksKeyword_{ nullptr };
        RE::BGSKeyword* locTypeGuildKeyword_{ nullptr };
        RE::BGSKeyword* locTypePlayerHouseKeyword_{ nullptr };
        RE::BGSKeyword* byohLocTypeHomesteadKeyword_{ nullptr };
        RE::BGSKeyword* locTypeTownKeyword_{ nullptr };
        RE::BGSKeyword* locTypeSettlementKeyword_{ nullptr };
        RE::BGSKeyword* locTypeOrcStrongholdKeyword_{ nullptr };
        RE::BGSKeyword* locTypeCityKeyword_{ nullptr };
        RE::BGSKeyword* locTypeLumberMillKeyword_{ nullptr };
        RE::BGSKeyword* locTypeFarmKeyword_{ nullptr };

        // Static objects & refs (cached)
        RE::TESObjectSTAT* homeSandboxMarkerStatic_{ nullptr };
        RE::TESObjectSTAT* xMarkerStatic_{ nullptr };
        RE::BGSLocationRefType* locationCenterMarkerRefType_{ nullptr };
        RE::TESBoundObject* goldBaseObject_{ nullptr };
        RE::TESObjectREFR* honorhallOrphanageCenterMarker_{ nullptr };

        // Packages (cached)
        RE::TESPackage* homeSandboxPackage_{ nullptr };
        RE::TESPackage* petCritterHomeSandboxPackage_{ nullptr };
        RE::TESPackage* childrenNonPlayerFollowParentPackage_{ nullptr };
        RE::TESPackage* childrenFleeIntimateScenePackage_{ nullptr };
        RE::TESPackage* adultChildTravelSandboxPackage_{ nullptr };
    };

}  // namespace MARAS
