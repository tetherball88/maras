#include "maras/engine/FormCache.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"

namespace MARAS {

    namespace {
        constexpr std::string_view PLUGIN_NAME = "TT_MARAS.esp";
        constexpr std::string_view SKYRIMNET_ESP = "SkyrimNet.esp";
        constexpr std::string_view SKYUI_SE_ESP = "SkyUI_SE.esp";
        constexpr std::string_view SKYUI_ESP = "SkyUI.esp";
        constexpr std::string_view FERTILITY_MODE_ESM = "Fertility Mode.esm";
        constexpr std::string_view BEEING_FEMALE_ESM = "BeeingFemale.esm";
        constexpr std::string_view HEARTHFIRES_ESM = "HearthFires.esm";
        constexpr std::string_view DAWNGUARD_ESM = "Dawnguard.esm";
        constexpr std::string_view DRAGONBORN_ESM = "Dragonborn.esm";
        constexpr std::string_view SKYRIM_ESM = "Skyrim.esm";

        template <typename T>
        T* LazyLoadPluginForm(T*& cache, RE::FormID formID, std::string_view pluginName, const char* description) {
            if (!cache) {
                cache = Utils::LookupForm<T>(formID, pluginName, description);
                if (!cache) {
                    MARAS_LOG_WARN("FormCache: failed to load {} ({:#x}) from {}", description, formID, pluginName);
                }
            }
            return cache;
        }

        template <typename T>
        T* LazyLoadForm(T*& cache, RE::FormID formID, const char* description) {
            return LazyLoadPluginForm(cache, formID, PLUGIN_NAME, description);
        }

        template <typename T>
        T* LazyLoadSkyrimForm(T*& cache, RE::FormID formID, const char* description) {
            if (!cache) {
                cache = RE::TESForm::LookupByID<T>(formID);
                if (!cache) {
                    MARAS_LOG_WARN("FormCache: failed to load {} ({:#x}) from {}", description, formID, SKYRIM_ESM);
                }
            }
            return cache;
        }

        RE::BGSKeyword* LazyLoadKeywordByEditorID(RE::BGSKeyword*& cache, std::string_view editorID) {
            if (!cache) {
                cache = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID);
                if (!cache) {
                    MARAS_LOG_WARN("FormCache: failed to load keyword '{}'", editorID);
                }
            }
            return cache;
        }
    }  // namespace

    FormCache& FormCache::GetSingleton() {
        static FormCache instance;
        return instance;
    }

    void FormCache::Initialize() {
        // Pre-populate all cached forms once on the main thread at kDataLoaded
        (void)GetTrackedFaction();
        (void)GetSpouseSocialClassFaction();
        (void)GetSpouseSkillTypeFaction();
        (void)GetSpouseTemperamentFaction();
        (void)GetSpouseHousedFaction();
        (void)GetHierarchyFaction();
        (void)GetAffectionFaction();
        (void)GetPlayerChildFaction();
        (void)GetPlayerChildBothParentsFaction();
        (void)GetSpouseChildrenFaction();
        (void)GetPlayerChildPetFaction();
        (void)GetSpouseCheatingFaction();
        (void)GetSpouseOpenRelationshipFaction();
        (void)GetCanFormNonCheatingMainFaction();
        (void)GetSpouseSharedHouseFaction();
        (void)GetSpouseNoInitialHouseFaction();
        (void)GetCheatingNpcCaughtFaction();
        (void)GetBYOHRelationshipAdoptionChildOwnedFaction();
        (void)GetBYOHRelationshipAdoptionFaction();
        (void)GetBYOHRelationshipAdoptableFaction();

        (void)GetMarriagePotentialFaction();
        (void)GetMarriageAskedFaction();
        (void)GetCourtingFaction();
        (void)GetPotentialHirelingFaction();
        (void)GetMarriedFaction();
        (void)GetPlayerBedOwnershipFaction();
        (void)GetCurrentFollowerFaction();

        (void)GetPlayerFaction();
        (void)GetCompanionsFaction();
        (void)GetThievesFaction();
        (void)GetBrotherhoodFaction();
        (void)GetCollegeFaction();
        (void)GetBardsFaction();
        (void)GetBladesFaction();
        (void)GetDawnguardFaction();
        (void)GetPotentialFollowerFaction();
        (void)GetWerewolfFaction();
        (void)GetPlayerWerewolfFaction();
        (void)GetCompanionsCircleFaction();
        (void)GetFrostmoonWerewolvesFaction();

        (void)GetMerchantFaction();
        (void)GetApothecaryFaction();
        (void)GetBlacksmithFaction();
        (void)GetFletcherFaction();
        (void)GetInnKeeperFaction();
        (void)GetJewelerFaction();
        (void)GetMiscFaction();
        (void)GetSpellFaction();
        (void)GetTailorFaction();
        (void)GetHunterFaction();

        (void)GetRulerFactions();
        (void)GetNobleFactions();
        (void)GetReligiousFactions();
        (void)GetWealthyFactions();
        (void)GetMiddleFactions();
        (void)GetPovertyFactions();
        (void)GetOutcastFactions();

        (void)GetOratorClasses();
        (void)GetRangerClasses();
        (void)GetRogueClasses();
        (void)GetCraftsmanClasses();
        (void)GetMageClasses();
        (void)GetWarriorClasses();

        (void)GetApothecaryMerchantHousedFaction();
        (void)GetBlacksmithMerchantHousedFaction();
        (void)GetFletcherMerchantHousedFaction();
        (void)GetHunterMerchantHousedFaction();
        (void)GetInnkeeperMerchantHousedFaction();
        (void)GetJewelerMerchantHousedFaction();
        (void)GetMiscMerchantHousedFaction();
        (void)GetSpellsMerchantHousedFaction();
        (void)GetTailorMerchantHousedFaction();

        (void)GetEastmarchThane();
        (void)GetFalkreathThane();
        (void)GetHaafingarThane();
        (void)GetHjaalmarchThane();
        (void)GetPaleThane();
        (void)GetReachThane();
        (void)GetRiftThane();
        (void)GetWhiterunThane();
        (void)GetWinterholdThane();

        (void)GetCompanionsQuest();
        (void)GetCollegeQuest();
        (void)GetThievesQuest();
        (void)GetBardsQuest();
        (void)GetDragonbornQuest();
        (void)GetDB01();
        (void)GetC03Quest();
        (void)GetCR13Quest();
        (void)GetCR14Quest();
        (void)GetDA05Quest();

        (void)GetWerewolfChangeSpell();
        (void)GetWerewolfImmunitySpell();
        (void)GetWerewolfBeastRace();

        (void)GetLoveInterestsCount();
        (void)GetSpousesCount();
        (void)GetPlayerHousesCount();
        (void)GetEnablePolygamyGlobal();
        (void)GetStartDialGenderGlobal();
        (void)GetSkyrimNetInstalledGlobal();
        (void)GetMinRelRankForDialGlobal();
        (void)GetPreferVanillaAudioGlobal();
        (void)GetAllowAIDialGlobal();
        (void)GetDatingChanceGlobal();
        (void)GetLoverChanceGlobal();
        (void)GetMarriageChanceGlobal();

        (void)GetBreakupCooldownSpell();
        (void)GetDemotedCooldownSpell();
        (void)GetPacifyFianceOrSpouseSpell();
        (void)GetDefaultCrimeFaction();

        (void)GetChildLogicQuest();
        (void)GetLoversRadianceAbility();
        (void)GetPlayerSleepQuest();
        (void)GetRestedSpell();
        (void)GetWellRestedSpell();
        (void)GetMarriageSleepAbility();
        (void)GetAdoptionSleepAbilityMale();
        (void)GetAdoptionSleepAbilityFemale();

        (void)GetIgnoreProposeKeyword();
        (void)GetClothingRichKeyword();
        (void)GetHomeSandboxKeyword();
        (void)GetChildSandboxAroundParentKeyword();

        (void)GetLocTypeTempleKeyword();
        (void)GetLocTypeStoreKeyword();
        (void)GetLocTypeStewardsDwellingKeyword();
        (void)GetLocTypeInnKeyword();
        (void)GetLocTypeHouseKeyword();
        (void)GetLocTypeBarracksKeyword();
        (void)GetLocTypeGuildKeyword();
        (void)GetLocTypePlayerHouseKeyword();
        (void)GetBYOHLocTypeHomesteadKeyword();
        (void)GetLocTypeTownKeyword();
        (void)GetLocTypeSettlementKeyword();
        (void)GetLocTypeOrcStrongholdKeyword();
        (void)GetLocTypeCityKeyword();
        (void)GetLocTypeLumberMillKeyword();
        (void)GetLocTypeFarmKeyword();

        (void)GetHomeSandboxMarkerStatic();
        (void)GetXMarkerStatic();
        (void)GetSkyrimNetFilterFaction();
        (void)GetLocationCenterMarkerRefType();
        (void)GetGoldBaseObject();
        (void)GetHonorhallOrphanageCenterMarker();

        (void)GetHomeSandboxPackage();
        (void)GetPetCritterHomeSandboxPackage();
        (void)GetChildrenNonPlayerFollowParentPackage();
        (void)GetChildrenFleeIntimateScenePackage();
        (void)GetAdultChildTravelSandboxPackage();

        if (!optionalMcmQuestsProbed_) {
            optionalMcmQuestsProbed_ = true;
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                if (dataHandler->LookupModByName(SKYUI_SE_ESP) != nullptr) {
                    skyUiConfigManagerQuest_ = Utils::LookupForm<RE::TESQuest>(
                        0x000802, SKYUI_SE_ESP, "SKI_ConfigManagerInstance");
                } else if (dataHandler->LookupModByName(SKYUI_ESP) != nullptr) {
                    skyUiConfigManagerQuest_ = Utils::LookupForm<RE::TESQuest>(
                        0x000802, SKYUI_ESP, "SKI_ConfigManagerInstance");
                }
                if (dataHandler->LookupModByName(FERTILITY_MODE_ESM) != nullptr) {
                    fmrMcmQuest_ = Utils::LookupForm<RE::TESQuest>(
                        0x0115C8, FERTILITY_MODE_ESM, "_JSW_BB_ConfigQuest");
                }
                if (dataHandler->LookupModByName(BEEING_FEMALE_ESM) != nullptr) {
                    beeingFemaleMcmQuest_ = Utils::LookupForm<RE::TESQuest>(
                        0x001828, BEEING_FEMALE_ESM, "FWSystemConfig");
                }
            }
        }
    }

    // TT_MARAS.esp Faction getters
    RE::TESFaction* FormCache::GetTrackedFaction() {
        return LazyLoadForm(trackedFaction_, 0x7, "tracked faction");
    }

    RE::TESFaction* FormCache::GetSpouseSocialClassFaction() {
        return LazyLoadForm(spouseSocialClassFaction_, 0x66, "spouse social class faction");
    }

    RE::TESFaction* FormCache::GetSpouseSkillTypeFaction() {
        return LazyLoadForm(spouseSkillTypeFaction_, 0x4e, "spouse skill type faction");
    }

    RE::TESFaction* FormCache::GetSpouseTemperamentFaction() {
        return LazyLoadForm(spouseTemperamentFaction_, 0x118, "spouse temperament faction");
    }

    RE::TESFaction* FormCache::GetSpouseHousedFaction() {
        return LazyLoadForm(spouseHousedFaction_, 0x6c, "spouse housed faction");
    }

    RE::TESFaction* FormCache::GetHierarchyFaction() {
        return LazyLoadForm(hierarchyFaction_, 0x111, "hierarchy faction");
    }

    RE::TESFaction* FormCache::GetAffectionFaction() {
        return LazyLoadForm(affectionFaction_, 0x119, "affection faction");
    }

    RE::TESFaction* FormCache::GetPlayerChildFaction() {
        return LazyLoadForm(playerChildFaction_, 0xC23, "player child faction");
    }

    RE::TESFaction* FormCache::GetPlayerChildBothParentsFaction() {
        return LazyLoadForm(playerChildBothParentsFaction_, 0x28A, "player child both parents faction");
    }

    RE::TESFaction* FormCache::GetSpouseChildrenFaction() {
        return LazyLoadForm(spouseChildrenFaction_, 0xC24, "spouse children faction");
    }

    RE::TESFaction* FormCache::GetPlayerChildPetFaction() {
        return LazyLoadForm(playerChildPetFaction_, 0x2BB, "player child pet faction");
    }

    RE::TESFaction* FormCache::GetSpouseCheatingFaction() {
        return LazyLoadForm(spouseCheatingFaction_, 0x2BD, "spouse cheating faction");
    }

    RE::TESFaction* FormCache::GetSpouseOpenRelationshipFaction() {
        return LazyLoadForm(spouseOpenRelationshipFaction_, 0x2DC, "spouse open relationship faction");
    }

    RE::TESFaction* FormCache::GetCanFormNonCheatingMainFaction() {
        return LazyLoadForm(CanFormNonCheatingMainFaction_, 0x5D2, "NPC taken faction");
    }

    RE::TESFaction* FormCache::GetSpouseSharedHouseFaction() {
        return LazyLoadForm(spouseSharedHouseFaction_, 0x7B, "spouse shared house faction");
    }

    RE::TESFaction* FormCache::GetSpouseNoInitialHouseFaction() {
        return LazyLoadForm(spouseNoInitialHouseFaction_, 0x7F, "spouse no initial house faction");
    }

    RE::TESFaction* FormCache::GetCheatingNpcCaughtFaction() {
        return LazyLoadForm(cheatingNpcCaughtFaction_, 0x5C4, "cheating NPC caught faction");
    }

    RE::TESFaction* FormCache::GetBYOHRelationshipAdoptionChildOwnedFaction() {
        return LazyLoadPluginForm(byohRelationshipAdoptionChildOwnedFaction_, 0x183a8, HEARTHFIRES_ESM,
                                  "BYOHRelationshipAdoptionChildOwnedFaction");
    }

    RE::TESFaction* FormCache::GetBYOHRelationshipAdoptionFaction() {
        return LazyLoadPluginForm(byohRelationshipAdoptionFaction_, 0x42b0, HEARTHFIRES_ESM,
                                  "BYOHRelationshipAdoptionFaction");
    }

    RE::TESFaction* FormCache::GetBYOHRelationshipAdoptableFaction() {
        return LazyLoadPluginForm(byohRelationshipAdoptableFaction_, 0x4290, HEARTHFIRES_ESM,
                                  "BYOHRelationshipAdoptableFaction");
    }

    RE::TESFaction* FormCache::GetSkyrimNetFilterFaction() {
        if (!skyrimNetFilterFaction_) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler && dataHandler->LookupModByName(SKYRIMNET_ESP) != nullptr) {
                skyrimNetFilterFaction_ =
                    Utils::LookupForm<RE::TESFaction>(0x12DB, SKYRIMNET_ESP, "SkyrimNet filter faction");
            }
        }
        return skyrimNetFilterFaction_;
    }

    RE::BGSKeyword* FormCache::GetIgnoreProposeKeyword() {
        return LazyLoadForm(ignoreProposeKeyword_, 0xC0B, "TTM ignore propose keyword");
    }

    RE::BGSKeyword* FormCache::GetHomeSandboxKeyword() {
        return LazyLoadForm(homeSandboxKeyword_, 0x6B, "home sandbox keyword");
    }

    RE::BGSKeyword* FormCache::GetChildSandboxAroundParentKeyword() {
        return LazyLoadForm(childSandboxAroundParentKeyword_, 0x377, "child sandbox around parent keyword");
    }

    RE::TESObjectSTAT* FormCache::GetHomeSandboxMarkerStatic() {
        return LazyLoadForm(homeSandboxMarkerStatic_, 0x76, "home sandbox marker static");
    }

    RE::TESObjectSTAT* FormCache::GetXMarkerStatic() {
        return LazyLoadSkyrimForm(xMarkerStatic_, 0x3B, "XMarker static");
    }

    RE::BGSLocationRefType* FormCache::GetLocationCenterMarkerRefType() {
        return LazyLoadSkyrimForm(locationCenterMarkerRefType_, 0x1bdf1, "LocationCenterMarker ref type");
    }

    RE::TESBoundObject* FormCache::GetGoldBaseObject() {
        return LazyLoadSkyrimForm(goldBaseObject_, 0x0000000f, "Gold001 base object");
    }

    RE::TESObjectREFR* FormCache::GetHonorhallOrphanageCenterMarker() {
        return LazyLoadSkyrimForm(honorhallOrphanageCenterMarker_, 0x1f357, "Honorhall Orphanage center marker");
    }

    RE::TESPackage* FormCache::GetHomeSandboxPackage() {
        return LazyLoadForm(homeSandboxPackage_, 0x6a, "home sandbox package");
    }

    RE::TESPackage* FormCache::GetPetCritterHomeSandboxPackage() {
        return LazyLoadForm(petCritterHomeSandboxPackage_, 0xc0, "pet critter home sandbox package");
    }

    RE::TESPackage* FormCache::GetChildrenNonPlayerFollowParentPackage() {
        return LazyLoadForm(childrenNonPlayerFollowParentPackage_, 0x376, "children non-player follow parent package");
    }

    RE::TESPackage* FormCache::GetChildrenFleeIntimateScenePackage() {
        return LazyLoadForm(childrenFleeIntimateScenePackage_, 0x5DE, "children flee intimate scene package");
    }

    RE::TESPackage* FormCache::GetAdultChildTravelSandboxPackage() {
        return LazyLoadForm(adultChildTravelSandboxPackage_, 0xDD3, "adult child travel sandbox package");
    }

    // MARAS housed spouses merchant factions
    RE::TESFaction* FormCache::GetApothecaryMerchantHousedFaction() {
        return LazyLoadForm(apothecaryHouseMerchantFaction_, 0xc0c, "apothecary housed merchant faction");
    }
    RE::TESFaction* FormCache::GetBlacksmithMerchantHousedFaction() {
        return LazyLoadForm(blacksmithHouseMerchantFaction_, 0xc0d, "blacksmith housed merchant faction");
    }
    RE::TESFaction* FormCache::GetFletcherMerchantHousedFaction() {
        return LazyLoadForm(fletcherHouseMerchantFaction_, 0xc12, "fletcher housed merchant faction");
    }
    RE::TESFaction* FormCache::GetHunterMerchantHousedFaction() {
        return LazyLoadForm(hunterHouseMerchantFaction_, 0xc14, "hunter housed merchant faction");
    }
    RE::TESFaction* FormCache::GetInnkeeperMerchantHousedFaction() {
        return LazyLoadForm(innkeeperHouseMerchantFaction_, 0xc0e, "innkeeper housed merchant faction");
    }
    RE::TESFaction* FormCache::GetJewelerMerchantHousedFaction() {
        return LazyLoadForm(jewelerHouseMerchantFaction_, 0xc13, "jeweler housed merchant faction");
    }
    RE::TESFaction* FormCache::GetMiscMerchantHousedFaction() {
        return LazyLoadForm(miscHouseMerchantFaction_, 0xc0f, "misc housed merchant faction");
    }
    RE::TESFaction* FormCache::GetSpellsMerchantHousedFaction() {
        return LazyLoadForm(spellsHouseMerchantFaction_, 0xc10, "spells housed merchant faction");
    }
    RE::TESFaction* FormCache::GetTailorMerchantHousedFaction() {
        return LazyLoadForm(tailorHouseMerchantFaction_, 0xc11, "tailor housed merchant faction");
    }

    // Skyrim merchant factions (cached)
    RE::TESFaction* FormCache::GetMerchantFaction() {
        return LazyLoadSkyrimForm(merchantFaction_, 0x51596, "Merchant faction");
    }
    RE::TESFaction* FormCache::GetApothecaryFaction() {
        return LazyLoadSkyrimForm(apothecaryFaction_, 0x5091c, "Apothecary merchant faction");
    }
    RE::TESFaction* FormCache::GetBlacksmithFaction() {
        return LazyLoadSkyrimForm(blacksmithFaction_, 0x5091d, "Blacksmith merchant faction");
    }
    RE::TESFaction* FormCache::GetFletcherFaction() {
        return LazyLoadSkyrimForm(fletcherFaction_, 0x51592, "Fletcher merchant faction");
    }
    RE::TESFaction* FormCache::GetInnKeeperFaction() {
        return LazyLoadSkyrimForm(innKeeperFaction_, 0x5091b, "InnKeeper merchant faction");
    }
    RE::TESFaction* FormCache::GetJewelerFaction() {
        return LazyLoadSkyrimForm(jewelerFaction_, 0x806a9, "Jeweler merchant faction");
    }
    RE::TESFaction* FormCache::GetMiscFaction() {
        return LazyLoadSkyrimForm(miscFaction_, 0x51599, "Misc merchant faction");
    }
    RE::TESFaction* FormCache::GetSpellFaction() {
        return LazyLoadSkyrimForm(spellFaction_, 0x50921, "Spell merchant faction");
    }
    RE::TESFaction* FormCache::GetTailorFaction() {
        return LazyLoadSkyrimForm(tailorFaction_, 0xa6c00, "Tailor merchant faction");
    }
    RE::TESFaction* FormCache::GetHunterFaction() {
        return LazyLoadSkyrimForm(hunterFaction_, 0xac9c2, "Hunter merchant faction");
    }

    // Marriage-related faction getters (from Skyrim.esm, cached)
    RE::TESFaction* FormCache::GetMarriagePotentialFaction() {
        return LazyLoadSkyrimForm(marriagePotentialFaction_, 0x19809, "marriage potential faction");
    }

    RE::TESFaction* FormCache::GetMarriageAskedFaction() {
        return LazyLoadSkyrimForm(marriageAskedFaction_, 0xff7f3, "marriage asked faction");
    }

    RE::TESFaction* FormCache::GetCourtingFaction() {
        return LazyLoadSkyrimForm(courtingFaction_, 0x7431a, "courting faction");
    }

    RE::TESFaction* FormCache::GetPotentialHirelingFaction() {
        return LazyLoadSkyrimForm(potentialHirelingFaction_, 0xbcc9a, "potential hireling faction");
    }

    RE::TESFaction* FormCache::GetMarriedFaction() {
        return LazyLoadSkyrimForm(marriedFaction_, 0xc6472, "married faction");
    }

    RE::TESFaction* FormCache::GetPlayerBedOwnershipFaction() {
        return LazyLoadSkyrimForm(playerBedOwnershipFaction_, 0xf2073, "player bed ownership faction");
    }

    // TT_MARAS.esp FormList getters (Social Classes)
    RE::BGSListForm* FormCache::GetRulerFactions() {
        return LazyLoadForm(rulerFactions_, 0xd75, "ruler factions");
    }

    RE::BGSListForm* FormCache::GetNobleFactions() {
        return LazyLoadForm(nobleFactions_, 0xd76, "noble factions");
    }

    RE::BGSListForm* FormCache::GetReligiousFactions() {
        return LazyLoadForm(religiousFactions_, 0xd77, "religious factions");
    }

    RE::BGSListForm* FormCache::GetWealthyFactions() {
        return LazyLoadForm(wealthyFactions_, 0xd71, "wealthy factions");
    }

    RE::BGSListForm* FormCache::GetMiddleFactions() {
        return LazyLoadForm(middleFactions_, 0x4, "middle factions");
    }

    RE::BGSListForm* FormCache::GetPovertyFactions() {
        return LazyLoadForm(povertyFactions_, 0xd74, "poverty factions");
    }

    RE::BGSListForm* FormCache::GetOutcastFactions() {
        return LazyLoadForm(outcastFactions_, 0xd70, "outcast factions");
    }

    // TT_MARAS.esp FormList getters (Skill Classes)
    RE::BGSListForm* FormCache::GetOratorClasses() {
        return LazyLoadForm(oratorClasses_, 0x13, "orator classes");
    }

    RE::BGSListForm* FormCache::GetRangerClasses() {
        return LazyLoadForm(rangerClasses_, 0x10, "ranger classes");
    }

    RE::BGSListForm* FormCache::GetRogueClasses() {
        return LazyLoadForm(rogueClasses_, 0x11, "rogue classes");
    }

    RE::BGSListForm* FormCache::GetCraftsmanClasses() {
        return LazyLoadForm(craftsmanClasses_, 0x12, "craftsman classes");
    }

    RE::BGSListForm* FormCache::GetMageClasses() {
        return LazyLoadForm(mageClasses_, 0xf, "mage classes");
    }

    RE::BGSListForm* FormCache::GetWarriorClasses() {
        return LazyLoadForm(warriorClasses_, 0xe, "warrior classes");
    }

    // TT_MARAS.esp Globals
    RE::TESGlobal* FormCache::GetLoveInterestsCount() {
        return LazyLoadForm(loveInterestsCount_, 0x4f, "LoveInterestsCount global");
    }

    RE::TESGlobal* FormCache::GetSpousesCount() {
        return LazyLoadForm(spousesCount_, 0x117, "SpousesCount global");
    }

    RE::TESGlobal* FormCache::GetPlayerHousesCount() {
        return LazyLoadForm(playerHousesCount_, 0xc2, "PlayerHousesCount global");
    }

    RE::TESGlobal* FormCache::GetEnablePolygamyGlobal() {
        return LazyLoadForm(enablePolygamyGlobal_, 0x6, "TTM_EnablePolygamyToggle global");
    }

    RE::TESGlobal* FormCache::GetStartDialGenderGlobal() {
        return LazyLoadForm(startDialGenderGlobal_, 0x4b, "TTM_MCM_StartDialGender global");
    }

    RE::TESGlobal* FormCache::GetSkyrimNetInstalledGlobal() {
        return LazyLoadForm(skyrimNetInstalledGlobal_, 0xc4, "TTM_SkyrimNetInstalled global");
    }

    RE::TESGlobal* FormCache::GetMinRelRankForDialGlobal() {
        return LazyLoadForm(minRelRankForDialGlobal_, 0xe4, "TTM_MCM_MinRelRankForDial global");
    }

    RE::TESGlobal* FormCache::GetPreferVanillaAudioGlobal() {
        return LazyLoadForm(preferVanillaAudioGlobal_, 0x18, "TTM_MCM_PreferVanillaAudio global");
    }

    RE::TESGlobal* FormCache::GetAllowAIDialGlobal() {
        return LazyLoadForm(allowAIDialGlobal_, 0x1c, "TTM_MCM_AllowAIDial global");
    }

    RE::TESGlobal* FormCache::GetDatingChanceGlobal() {
        return LazyLoadForm(datingChanceGlobal_, 0x5d0, "TTM_DatingChance global");
    }

    RE::TESGlobal* FormCache::GetLoverChanceGlobal() {
        return LazyLoadForm(loverChanceGlobal_, 0x5d1, "TTM_LoverChance global");
    }

    RE::TESGlobal* FormCache::GetMarriageChanceGlobal() {
        return LazyLoadForm(marriageChanceGlobal_, 0x15c, "TTM_MarriageChance global");
    }

    RE::SpellItem* FormCache::GetBreakupCooldownSpell() {
        return LazyLoadForm(breakupCooldownSpell_, 0x102, "TTM_BreakupCooldownAbility spell");
    }

    RE::SpellItem* FormCache::GetDemotedCooldownSpell() {
        return LazyLoadForm(demotedCooldownSpell_, 0x10e, "TTM_SpouseDemotedAbility spell");
    }

    RE::SpellItem* FormCache::GetPacifyFianceOrSpouseSpell() {
        return LazyLoadForm(pacifyFianceOrSpouseSpell_, 0x167, "TTM_PacifyFianceOrSpouseAbility spell");
    }

    RE::TESFaction* FormCache::GetDefaultCrimeFaction() {
        return LazyLoadSkyrimForm(defaultCrimeFaction_, 0x29db0, "Default crime faction (Whiterun)");
    }

    RE::TESQuest* FormCache::GetChildLogicQuest() {
        return LazyLoadForm(childLogicQuest_, 0xC25, "TTM_ServiceChildrenLogic quest");
    }

    RE::TESQuest* FormCache::GetSkyUiConfigManagerQuest() {
        return skyUiConfigManagerQuest_;
    }

    RE::TESQuest* FormCache::GetFmrMcmQuest() {
        return fmrMcmQuest_;
    }

    RE::TESQuest* FormCache::GetBeeingFemaleMcmQuest() {
        return beeingFemaleMcmQuest_;
    }

    RE::SpellItem* FormCache::GetLoversRadianceAbility() {
        return LazyLoadForm(loversRadianceAbility_, 0xDA, "TTM_LoversRadianceAbility spell");
    }

    RE::TESQuest* FormCache::GetPlayerSleepQuest() {
        return LazyLoadSkyrimForm(playerSleepQuest_, 0xFC1A2, "PlayerSleepQuest");
    }

    RE::SpellItem* FormCache::GetRestedSpell() {
        return LazyLoadSkyrimForm(restedSpell_, 0xFB981, "Rested spell");
    }

    RE::SpellItem* FormCache::GetWellRestedSpell() {
        return LazyLoadSkyrimForm(wellRestedSpell_, 0xFB984, "WellRested spell");
    }

    RE::SpellItem* FormCache::GetMarriageSleepAbility() {
        return LazyLoadSkyrimForm(marriageSleepAbility_, 0xCDA1D, "MarriageSleepAbility spell");
    }

    RE::SpellItem* FormCache::GetAdoptionSleepAbilityMale() {
        return LazyLoadPluginForm(adoptionSleepAbilityMale_, 0x2F54, HEARTHFIRES_ESM,
                                  "BYOHAdoptionSleepAbilityMale spell");
    }

    RE::SpellItem* FormCache::GetAdoptionSleepAbilityFemale() {
        return LazyLoadPluginForm(adoptionSleepAbilityFemale_, 0x4292, HEARTHFIRES_ESM,
                                    "BYOHAdoptionSleepAbilityFemale spell");
    }

    // Skyrim.esm Faction getters (cached)
    RE::TESFaction* FormCache::GetPlayerFaction() {
        return LazyLoadSkyrimForm(playerFaction_, 0xDB1, "player faction");
    }

    RE::TESFaction* FormCache::GetCompanionsFaction() {
        return LazyLoadSkyrimForm(companionsFaction_, 0x48362, "Companions faction");
    }

    RE::TESFaction* FormCache::GetThievesFaction() {
        return LazyLoadSkyrimForm(thievesFaction_, 0x29da9, "Thieves faction");
    }

    RE::TESFaction* FormCache::GetBrotherhoodFaction() {
        return LazyLoadSkyrimForm(brotherhoodFaction_, 0x1bdb3, "Dark Brotherhood faction");
    }

    RE::TESFaction* FormCache::GetCollegeFaction() {
        return LazyLoadSkyrimForm(collegeFaction_, 0x1f259, "College faction");
    }

    RE::TESFaction* FormCache::GetBardsFaction() {
        return LazyLoadSkyrimForm(bardsFaction_, 0xc13c7, "Bards faction");
    }

    RE::TESFaction* FormCache::GetBladesFaction() {
        return LazyLoadSkyrimForm(bladesFaction_, 0x72834, "Blades faction");
    }

    RE::TESFaction* FormCache::GetDawnguardFaction() {
        return LazyLoadPluginForm(dawnguardFaction_, 0x14217, DAWNGUARD_ESM, "Dawnguard faction");
    }

    RE::TESFaction* FormCache::GetPotentialFollowerFaction() {
        return LazyLoadSkyrimForm(potentialFollowerFaction_, 0x5C84D, "Potential follower faction");
    }

    RE::TESFaction* FormCache::GetCurrentFollowerFaction() {
        return LazyLoadSkyrimForm(currentFollowerFaction_, 0x5C84E, "Current follower faction");
    }

    RE::TESFaction* FormCache::GetWerewolfFaction() {
        return LazyLoadSkyrimForm(werewolfFaction_, 0x43594, "Werewolf faction");
    }

    RE::TESFaction* FormCache::GetPlayerWerewolfFaction() {
        return LazyLoadSkyrimForm(playerWerewolfFaction_, 0x91822, "Player werewolf faction");
    }

    RE::TESFaction* FormCache::GetCompanionsCircleFaction() {
        return LazyLoadSkyrimForm(companionsCircleFaction_, 0xFF234, "Companions Circle faction");
    }

    RE::TESFaction* FormCache::GetFrostmoonWerewolvesFaction() {
        return LazyLoadPluginForm(frostmoonWerewolvesFaction_, 0x1DC61, DRAGONBORN_ESM, "Frostmoon Werewolves faction");
    }

    // Skyrim.esm Quest getters (Thane quests, cached)
    RE::TESQuest* FormCache::GetEastmarchThane() {
        return LazyLoadSkyrimForm(eastmarchThane_, 0xa2ca6, "Eastmarch Thane quest");
    }

    RE::TESQuest* FormCache::GetFalkreathThane() {
        return LazyLoadSkyrimForm(falkreathThane_, 0xa34de, "Falkreath Thane quest");
    }

    RE::TESQuest* FormCache::GetHaafingarThane() {
        return LazyLoadSkyrimForm(haafingarThane_, 0xa2c9b, "Haafingar Thane quest");
    }

    RE::TESQuest* FormCache::GetHjaalmarchThane() {
        return LazyLoadSkyrimForm(hjaalmarchThane_, 0xa34ce, "Hjaalmarch Thane quest");
    }

    RE::TESQuest* FormCache::GetPaleThane() {
        return LazyLoadSkyrimForm(paleThane_, 0xa34d4, "Pale Thane quest");
    }

    RE::TESQuest* FormCache::GetReachThane() {
        return LazyLoadSkyrimForm(reachThane_, 0xa2c86, "Reach Thane quest");
    }

    RE::TESQuest* FormCache::GetRiftThane() {
        return LazyLoadSkyrimForm(riftThane_, 0x65bdf, "Rift Thane quest");
    }

    RE::TESQuest* FormCache::GetWhiterunThane() {
        return LazyLoadSkyrimForm(whiterunThane_, 0xa2c9e, "Whiterun Thane quest");
    }

    RE::TESQuest* FormCache::GetWinterholdThane() {
        return LazyLoadSkyrimForm(winterholdThane_, 0xa34d7, "Winterhold Thane quest");
    }

    // Skyrim.esm Quest getters (Guild/Achievement quests, cached)
    RE::TESQuest* FormCache::GetCompanionsQuest() {
        return LazyLoadSkyrimForm(companionsQuest_, 0x1cef6, "Companions guild leader quest");
    }

    RE::TESQuest* FormCache::GetCollegeQuest() {
        return LazyLoadSkyrimForm(collegeQuest_, 0x1f258, "College guild leader quest");
    }

    RE::TESQuest* FormCache::GetThievesQuest() {
        return LazyLoadSkyrimForm(thievesQuest_, 0xd7d69, "Thieves guild leader quest");
    }

    RE::TESQuest* FormCache::GetBardsQuest() {
        return LazyLoadSkyrimForm(bardsQuest_, 0x53511, "become bards quest");
    }

    RE::TESQuest* FormCache::GetDragonbornQuest() {
        return LazyLoadSkyrimForm(dragonbornQuest_, 0x2610c, "become dragonborn quest");
    }

    RE::TESQuest* FormCache::GetDB01() {
        return LazyLoadSkyrimForm(db01Quest_, 0x1ea50, "DB01 Innocence Lost quest");
    }

    RE::TESQuest* FormCache::GetC03Quest() {
        return LazyLoadSkyrimForm(c03Quest_, 0x1CEF4, "C03 The Silver Hand quest");
    }

    RE::TESQuest* FormCache::GetCR13Quest() {
        return LazyLoadSkyrimForm(cr13Quest_, 0xE3163, "CR13 Farkas Purity quest");
    }

    RE::TESQuest* FormCache::GetCR14Quest() {
        return LazyLoadSkyrimForm(cr14Quest_, 0xE3156, "CR14 Vilkas Purity quest");
    }

    RE::TESQuest* FormCache::GetDA05Quest() {
        return LazyLoadSkyrimForm(da05Quest_, 0x2A49A, "DA05 Ill Met By Moonlight quest");
    }

    RE::SpellItem* FormCache::GetWerewolfChangeSpell() {
        return LazyLoadSkyrimForm(werewolfChangeSpell_, 0x92C48, "WerewolfChange Beast Form spell");
    }

    RE::SpellItem* FormCache::GetWerewolfImmunitySpell() {
        return LazyLoadSkyrimForm(werewolfImmunitySpell_, 0xF5BA0, "WerewolfImmunity ability");
    }

    RE::TESRace* FormCache::GetWerewolfBeastRace() {
        return LazyLoadSkyrimForm(werewolfBeastRace_, 0xCDD84, "WerewolfBeastRace");
    }

    // Skyrim.esm Keyword getters (cached)
    RE::BGSKeyword* FormCache::GetClothingRichKeyword() {
        return LazyLoadSkyrimForm(clothingRichKeyword_, 0xA865D, "clothing rich keyword");
    }

    // Location type Keyword getters (Skyrim.esm & HearthFires.esm, cached)
    RE::BGSKeyword* FormCache::GetLocTypeTempleKeyword() {
        return LazyLoadKeywordByEditorID(locTypeTempleKeyword_, "LocTypeTemple");
    }
    RE::BGSKeyword* FormCache::GetLocTypeStoreKeyword() {
        return LazyLoadKeywordByEditorID(locTypeStoreKeyword_, "LocTypeStore");
    }
    RE::BGSKeyword* FormCache::GetLocTypeStewardsDwellingKeyword() {
        return LazyLoadKeywordByEditorID(locTypeStewardsDwellingKeyword_, "LocTypeStewardsDwelling");
    }
    RE::BGSKeyword* FormCache::GetLocTypeInnKeyword() {
        return LazyLoadKeywordByEditorID(locTypeInnKeyword_, "LocTypeInn");
    }
    RE::BGSKeyword* FormCache::GetLocTypeHouseKeyword() {
        return LazyLoadKeywordByEditorID(locTypeHouseKeyword_, "LocTypeHouse");
    }
    RE::BGSKeyword* FormCache::GetLocTypeBarracksKeyword() {
        return LazyLoadKeywordByEditorID(locTypeBarracksKeyword_, "LocTypeBarracks");
    }
    RE::BGSKeyword* FormCache::GetLocTypeGuildKeyword() {
        return LazyLoadKeywordByEditorID(locTypeGuildKeyword_, "LocTypeGuild");
    }
    RE::BGSKeyword* FormCache::GetLocTypePlayerHouseKeyword() {
        return LazyLoadKeywordByEditorID(locTypePlayerHouseKeyword_, "LocTypePlayerHouse");
    }
    RE::BGSKeyword* FormCache::GetBYOHLocTypeHomesteadKeyword() {
        return LazyLoadKeywordByEditorID(byohLocTypeHomesteadKeyword_, "BYOH_LocTypeHomestead");
    }
    RE::BGSKeyword* FormCache::GetLocTypeTownKeyword() {
        return LazyLoadKeywordByEditorID(locTypeTownKeyword_, "LocTypeTown");
    }
    RE::BGSKeyword* FormCache::GetLocTypeSettlementKeyword() {
        return LazyLoadKeywordByEditorID(locTypeSettlementKeyword_, "LocTypeSettlement");
    }
    RE::BGSKeyword* FormCache::GetLocTypeOrcStrongholdKeyword() {
        return LazyLoadKeywordByEditorID(locTypeOrcStrongholdKeyword_, "LocTypeOrcStronghold");
    }
    RE::BGSKeyword* FormCache::GetLocTypeCityKeyword() {
        return LazyLoadKeywordByEditorID(locTypeCityKeyword_, "LocTypeCity");
    }
    RE::BGSKeyword* FormCache::GetLocTypeLumberMillKeyword() {
        return LazyLoadKeywordByEditorID(locTypeLumberMillKeyword_, "LocTypeLumberMill");
    }
    RE::BGSKeyword* FormCache::GetLocTypeFarmKeyword() {
        return LazyLoadKeywordByEditorID(locTypeFarmKeyword_, "LocTypeFarm");
    }

}  // namespace MARAS
