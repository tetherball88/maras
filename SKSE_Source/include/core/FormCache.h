#pragma once

#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>

#include "RE/Skyrim.h"
#include "utils/FormUtils.h"

namespace MARAS {

    class FormCache {
    public:
        static FormCache& GetSingleton();

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

        // Skyrim.esm faction getters
        RE::TESFaction* GetPlayerFaction();
        RE::TESFaction* GetCompanionsFaction();
        RE::TESFaction* GetThievesFaction();
        RE::TESFaction* GetBrotherhoodFaction();
        RE::TESFaction* GetCollegeFaction();
        RE::TESFaction* GetBardsFaction();

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

        // Global getters (TT_MARAS.esp)
        RE::TESGlobal* GetLoveInterestsCount();
        RE::TESGlobal* GetSpousesCount();
        RE::TESGlobal* GetPlayerHousesCount();

        // Special keyword getters
        RE::BGSKeyword* GetClothingRichKeyword();
        RE::BGSKeyword* GetHomeSandboxKeyword();

        // Static object getters
        RE::TESObjectSTAT* GetHomeSandboxMarkerStatic();

    private:
        FormCache() = default;
        ~FormCache() = default;

        FormCache(const FormCache&) = delete;
        FormCache& operator=(const FormCache&) = delete;

        // Cached pointers (initialized in .cpp lazily)
        RE::TESFaction* trackedFaction_{nullptr};
        RE::TESFaction* spouseSocialClassFaction_{nullptr};
        RE::TESFaction* spouseSkillTypeFaction_{nullptr};
        RE::TESFaction* spouseTemperamentFaction_{nullptr};
        RE::TESFaction* spouseHousedFaction_{nullptr};
        RE::TESFaction* hierarchyFaction_{nullptr};
        RE::TESFaction* affectionFaction_{nullptr};

        // Marriage-related factions
        RE::TESFaction* marriagePotentialFaction_{nullptr};
        RE::TESFaction* marriageAskedFaction_{nullptr};
        RE::TESFaction* courtingFaction_{nullptr};
        RE::TESFaction* potentialHirelingFaction_{nullptr};
        RE::TESFaction* marriedFaction_{nullptr};
        RE::TESFaction* playerBedOwnershipFaction_{nullptr};

        RE::BGSListForm* rulerFactions_{nullptr};
        RE::BGSListForm* nobleFactions_{nullptr};
        RE::BGSListForm* religiousFactions_{nullptr};
        RE::BGSListForm* wealthyFactions_{nullptr};
        RE::BGSListForm* middleFactions_{nullptr};
        RE::BGSListForm* povertyFactions_{nullptr};
        RE::BGSListForm* outcastFactions_{nullptr};

        RE::BGSListForm* oratorClasses_{nullptr};
        RE::BGSListForm* rangerClasses_{nullptr};
        RE::BGSListForm* rogueClasses_{nullptr};
        RE::BGSListForm* craftsmanClasses_{nullptr};
        RE::BGSListForm* mageClasses_{nullptr};
        RE::BGSListForm* warriorClasses_{nullptr};

        // MARAS housed merchant factions (cached)
        RE::TESFaction* apothecaryHouseMerchantFaction_{nullptr};
        RE::TESFaction* blacksmithHouseMerchantFaction_{nullptr};
        RE::TESFaction* fletcherHouseMerchantFaction_{nullptr};
        RE::TESFaction* hunterHouseMerchantFaction_{nullptr};
        RE::TESFaction* innkeeperHouseMerchantFaction_{nullptr};
        RE::TESFaction* jewelerHouseMerchantFaction_{nullptr};
        RE::TESFaction* miscHouseMerchantFaction_{nullptr};
        RE::TESFaction* spellsHouseMerchantFaction_{nullptr};
        RE::TESFaction* tailorHouseMerchantFaction_{nullptr};

        // Globals (cached)
        RE::TESGlobal* loveInterestsCount_{nullptr};
        RE::TESGlobal* spousesCount_{nullptr};
        RE::TESGlobal* playerHousesCount_{nullptr};
        // Keywords (cached)
        RE::BGSKeyword* ignoreProposeKeyword_{nullptr};
        RE::BGSKeyword* homeSandboxKeyword_{nullptr};
        // Static objects (cached)
        RE::TESObjectSTAT* homeSandboxMarkerStatic_{nullptr};
        // (No generic cache; use named getters and Utils::LookupForm inside the .cpp)
    };

}  // namespace MARAS
