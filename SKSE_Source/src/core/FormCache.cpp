#include "core/FormCache.h"

#include "spdlog/spdlog.h"
#include "utils/FormUtils.h"

namespace MARAS {

    namespace {
        constexpr const char* PLUGIN_NAME = "TT_MARAS.esp";
        constexpr const char* SKYRIM_ESM = "Skyrim.esm";

        // Helper: Lazy load a form from TT_MARAS.esp with warning
        template <typename T>
        T* LazyLoadForm(T*& cache, RE::FormID formID, const char* description) {
            if (!cache) {
                cache = Utils::LookupForm<T>(formID, PLUGIN_NAME);
                if (!cache) {
                    SPDLOG_WARN("FormCache: failed to load {} ({:#x}) from {}", description, formID, PLUGIN_NAME);
                }
            }
            return cache;
        }

        // Helper: Load a form from Skyrim.esm (no caching) with warning
        template <typename T>
        T* LoadSkyrimForm(RE::FormID formID, const char* description) {
            auto form = RE::TESForm::LookupByID<T>(formID);
            if (!form) {
                SPDLOG_WARN("FormCache: failed to load {} ({:#x}) from {}", description, formID, SKYRIM_ESM);
            }
            return form;
        }
    }  // namespace

    FormCache& FormCache::GetSingleton() {
        static FormCache instance;
        return instance;
    }

    // TT_MARAS.esp Faction getters
    RE::TESFaction* FormCache::GetTrackedFaction() { return LazyLoadForm(trackedFaction_, 0x7, "tracked faction"); }

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

    // Marriage-related faction getters (from Skyrim.esm)
    RE::TESFaction* FormCache::GetMarriagePotentialFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x19809, "marriage potential faction");
    }

    RE::TESFaction* FormCache::GetMarriageAskedFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0xff7f3, "marriage asked faction");
    }

    RE::TESFaction* FormCache::GetCourtingFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x7431a, "courting faction");
    }

    RE::TESFaction* FormCache::GetPotentialHirelingFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0xbcc9a, "potential hireling faction");
    }

    RE::TESFaction* FormCache::GetMarriedFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0xc6472, "married faction");
    }

    RE::TESFaction* FormCache::GetPlayerBedOwnershipFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0xf2073, "player bed ownership faction");
    }

    // TT_MARAS.esp FormList getters (Social Classes)
    RE::BGSListForm* FormCache::GetRulerFactions() { return LazyLoadForm(rulerFactions_, 0xd75, "ruler factions"); }

    RE::BGSListForm* FormCache::GetNobleFactions() { return LazyLoadForm(nobleFactions_, 0xd76, "noble factions"); }

    RE::BGSListForm* FormCache::GetReligiousFactions() {
        return LazyLoadForm(religiousFactions_, 0xd77, "religious factions");
    }

    RE::BGSListForm* FormCache::GetWealthyFactions() {
        return LazyLoadForm(wealthyFactions_, 0xd71, "wealthy factions");
    }

    RE::BGSListForm* FormCache::GetMiddleFactions() { return LazyLoadForm(middleFactions_, 0x4, "middle factions"); }

    RE::BGSListForm* FormCache::GetPovertyFactions() {
        return LazyLoadForm(povertyFactions_, 0xd74, "poverty factions");
    }

    RE::BGSListForm* FormCache::GetOutcastFactions() {
        return LazyLoadForm(outcastFactions_, 0xd70, "outcast factions");
    }

    // TT_MARAS.esp FormList getters (Skill Classes)
    RE::BGSListForm* FormCache::GetOratorClasses() { return LazyLoadForm(oratorClasses_, 0x13, "orator classes"); }

    RE::BGSListForm* FormCache::GetRangerClasses() { return LazyLoadForm(rangerClasses_, 0x10, "ranger classes"); }

    RE::BGSListForm* FormCache::GetRogueClasses() { return LazyLoadForm(rogueClasses_, 0x11, "rogue classes"); }

    RE::BGSListForm* FormCache::GetCraftsmanClasses() {
        return LazyLoadForm(craftsmanClasses_, 0x12, "craftsman classes");
    }

    RE::BGSListForm* FormCache::GetMageClasses() { return LazyLoadForm(mageClasses_, 0xf, "mage classes"); }

    RE::BGSListForm* FormCache::GetWarriorClasses() { return LazyLoadForm(warriorClasses_, 0xe, "warrior classes"); }

    // TT_MARAS.esp Globals
    RE::TESGlobal* FormCache::GetLoveInterestsCount() {
        return LazyLoadForm(loveInterestsCount_, 0x4f, "LoveInterestsCount global");
    }

    RE::TESGlobal* FormCache::GetSpousesCount() { return LazyLoadForm(spousesCount_, 0x117, "SpousesCount global"); }
    RE::TESGlobal* FormCache::GetPlayerHousesCount() {
        return LazyLoadForm(playerHousesCount_, 0xc2, "PlayerHousesCount global");
    }

    // Skyrim.esm Faction getters (not cached)
    RE::TESFaction* FormCache::GetPlayerFaction() { return LoadSkyrimForm<RE::TESFaction>(0xDB1, "player faction"); }

    RE::TESFaction* FormCache::GetCompanionsFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x48362, "Companions faction");
    }

    RE::TESFaction* FormCache::GetThievesFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x29da9, "Thieves faction");
    }

    RE::TESFaction* FormCache::GetBrotherhoodFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x1bdb3, "Dark Brotherhood faction");
    }

    RE::TESFaction* FormCache::GetCollegeFaction() {
        return LoadSkyrimForm<RE::TESFaction>(0x1f259, "College faction");
    }

    RE::TESFaction* FormCache::GetBardsFaction() { return LoadSkyrimForm<RE::TESFaction>(0xc13c7, "Bards faction"); }

    // Skyrim.esm Quest getters (Thane quests)
    RE::TESQuest* FormCache::GetEastmarchThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa2ca6, "Eastmarch Thane quest");
    }

    RE::TESQuest* FormCache::GetFalkreathThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa34de, "Falkreath Thane quest");
    }

    RE::TESQuest* FormCache::GetHaafingarThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa2c9b, "Haafingar Thane quest");
    }

    RE::TESQuest* FormCache::GetHjaalmarchThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa34ce, "Hjaalmarch Thane quest");
    }

    RE::TESQuest* FormCache::GetPaleThane() { return LoadSkyrimForm<RE::TESQuest>(0xa34d4, "Pale Thane quest"); }

    RE::TESQuest* FormCache::GetReachThane() { return LoadSkyrimForm<RE::TESQuest>(0xa2c86, "Reach Thane quest"); }

    RE::TESQuest* FormCache::GetRiftThane() { return LoadSkyrimForm<RE::TESQuest>(0x65bdf, "Rift Thane quest"); }

    RE::TESQuest* FormCache::GetWhiterunThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa2c9e, "Whiterun Thane quest");
    }

    RE::TESQuest* FormCache::GetWinterholdThane() {
        return LoadSkyrimForm<RE::TESQuest>(0xa34d7, "Winterhold Thane quest");
    }

    // Skyrim.esm Quest getters (Guild/Achievement quests)
    RE::TESQuest* FormCache::GetCompanionsQuest() {
        return LoadSkyrimForm<RE::TESQuest>(0x1cef6, "Companions guild leader quest");
    }

    RE::TESQuest* FormCache::GetCollegeQuest() {
        return LoadSkyrimForm<RE::TESQuest>(0x1f258, "College guild leader quest");
    }

    RE::TESQuest* FormCache::GetThievesQuest() {
        return LoadSkyrimForm<RE::TESQuest>(0xd7d69, "Thieves guild leader quest");
    }

    RE::TESQuest* FormCache::GetBardsQuest() { return LoadSkyrimForm<RE::TESQuest>(0x53511, "become bards quest"); }

    RE::TESQuest* FormCache::GetDragonbornQuest() {
        return LoadSkyrimForm<RE::TESQuest>(0x2610c, "become dragonborn quest");
    }

    // Skyrim.esm Keyword getters
    RE::BGSKeyword* FormCache::GetClothingRichKeyword() {
        return LoadSkyrimForm<RE::BGSKeyword>(0xA865D, "clothing rich keyword");
    }

}  // namespace MARAS
