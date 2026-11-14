#pragma once

#include "maras/domain/household/TravelRules.h"
#include "maras/engine/EngineLogging.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MARAS {

    /**
     * Filter options for querying NPCs in the index.
     */
    enum class NPCFilter : std::uint8_t {
        All = 0,
        AdultOnly = 1,
        ChildOnly = 2
    };

    /**
     * Representation of an indexed NPC entry in MARAS.
     */
    struct NPCEntry {
        RE::FormID refFormID{ 0 };         // Placed reference FormID (primary runtime handle)
        RE::FormID baseFormID{ 0 };        // TESNPC base FormID
        std::string editorID{};            // Base form EditorID (if available)
        std::string nameLower{};           // Lowercase name for exact/fuzzy lookups
        std::string nameDisplay{};         // Original display name
        bool isChild{ false };             // True if race flag kChild is set
        bool isUnique{ false };            // True if TESNPC kUnique flag was set
        std::uint8_t sex{ 0 };             // 0 = Male, 1 = Female
        std::string raceName{};            // Display race name
        std::string currentCell{};         // Current cell name if loaded
        std::string pluginName{};          // Mod/plugin where NPC was originally declared
        RE::FormID placementCellID{ 0 };   // Static placement cell FormID (recorded during BuildIndex)
        RE::FormID editorLocationID{ 0 };  // Static editor BGSLocation FormID (from ref or placement cell)
        RE::FormID crimeFactionID{ 0 };    // TESNPC::crimeFaction FormID (hold/town civic affiliation)
    };

    struct HomeCellData {
        std::vector<RE::FormID> doors{};  // Door reference FormIDs inside the cell
        RE::FormID centerMarker{ 0 };     // LocationCenterMarker reference FormID
    };

    struct ActorHomeData {
        RE::FormID homeCell{ 0 };  // Cell FormID where actor has a persistent ref
    };

    /**
     * SpatialWorldIndex - Centralized world/NPC/home-cell/destination spatial index for M.A.R.A.S.
     *
     * Merges static & live NPC indexing, home cell/door/bed ownership indexing,
     * and Skyrim Inn/Tavern travel destination indexing into one thread-safe service
     * guarded by std::shared_mutex.
     */
    class SpatialWorldIndex {
    public:
        static SpatialWorldIndex& GetSingleton();

        // ── Lifecycle ──

        /**
         * Build the full static world index at kDataLoaded:
         * - NPC index (base forms, persistent/interior placed refs, INI overrides, loaded actors)
         * - Home cell index (shareable doors, center markers, persistent actors, owned beds)
         * - Inn/Tavern travel destination index
         */
        void BuildIndex();

        /**
         * Build/rebuild only the static NPC index + loaded actor sweep (writes promotions report
         * outside the mutex when WorldStore settings logLevel <= 1).
         */
        void BuildNpcIndex();

        /**
         * Scan interior cells with dwelling/house keywords to rebuild home cells, doors,
         * persistent actor home associations, and owned beds.
         */
        void BuildHomeCellIndex();

        /**
         * Scan interior cells with LocTypeInn keyword to rebuild inn/tavern travel destinations.
         */
        void BuildInnDestinationIndex();

        /**
         * Quick refresh on save load (kPostLoadGame / kNewGame).
         * Evicts stale loaded pointers and re-sweeps currently loaded actors.
         */
        void RefreshIndex();

        /**
         * Register or update an actor when loaded dynamically in game.
         */
        void RegisterLoadedActor(RE::Actor* actor);

        /**
         * Clear only transient live actor pointers on pre-load (kPreLoadGame).
         * Keeps static NPC, home cell, inn destination, and INI override indexes intact.
         */
        void ClearLoadedActors();

        /**
         * Clear NPC index structures and INI overrides.
         */
        void ClearIndex();

        /**
         * Revert/clear all spatial world index data (NPCs, home cells, inn destinations).
         */
        void Revert();

        [[nodiscard]] bool IsIndexBuilt() const noexcept {
            return m_indexBuilt.load(std::memory_order_acquire);
        }

        // ── NPC Queries ──

        [[nodiscard]] std::optional<NPCEntry> FindByName(
            std::string_view searchTerm,
            NPCFilter filter = NPCFilter::All) const;

        [[nodiscard]] RE::Actor* FindActorByName(
            std::string_view searchTerm,
            NPCFilter filter = NPCFilter::All) const;

        [[nodiscard]] RE::Actor* FindActorByNameNear(
            std::string_view searchTerm,
            RE::Actor* nearActor,
            bool allowSelf = false,
            NPCFilter filter = NPCFilter::All) const;

        [[nodiscard]] std::optional<NPCEntry> FindByFormID(RE::FormID formID) const;

        [[nodiscard]] RE::FormID GetRefID(RE::FormID formID) const;

        [[nodiscard]] RE::FormID GetBaseID(RE::FormID formID) const;

        [[nodiscard]] std::string GetName(RE::FormID formID) const;

        [[nodiscard]] RE::Actor* GetActorFromFormId(RE::FormID formID) const;

        [[nodiscard]] std::vector<NPCEntry> SearchBySubstring(
            std::string_view query,
            std::size_t maxResults = 50,
            NPCFilter filter = NPCFilter::All) const;

        [[nodiscard]] std::vector<NPCEntry> GetAllEntries(NPCFilter filter = NPCFilter::All) const;

        [[nodiscard]] nlohmann::json DumpIndexJson() const;

        [[nodiscard]] std::size_t GetNPCCount() const {
            std::shared_lock lock(m_mutex);
            return GetNPCCountLocked();
        }

        [[nodiscard]] std::size_t GetChildCount() const {
            std::shared_lock lock(m_mutex);
            return GetChildCountLocked();
        }

        [[nodiscard]] std::size_t GetAdultCount() const {
            std::shared_lock lock(m_mutex);
            return GetAdultCountLocked();
        }

        [[nodiscard]] std::size_t GetLoadedCount() const {
            std::shared_lock lock(m_mutex);
            return GetLoadedCountLocked();
        }

        // ── Home Cell Queries (formerly HomeCellService) ──

        [[nodiscard]] RE::FormID GetNpcHome(RE::Actor* npc) const;
        [[nodiscard]] RE::FormID GetNpcHome(RE::FormID actorFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetNpcBeds(RE::Actor* npc) const;
        [[nodiscard]] std::vector<RE::FormID> GetNpcBeds(RE::FormID actorFormID) const;
        [[nodiscard]] std::vector<RE::FormID> GetCellDoors(RE::FormID cellFormID) const;
        [[nodiscard]] RE::FormID GetNpcOriginalHouseCenterMarker(RE::Actor* npc) const;
        [[nodiscard]] RE::FormID GetNpcOriginalHouseCenterMarker(RE::FormID actorFormID) const;
        void LogIndex() const;

        // ── Inn / Travel Destination Queries (formerly AdultChildTravelService index) ──

        [[nodiscard]] std::vector<Domain::Household::TravelDestinationCandidate> GetInnDestinations() const;

    private:
        SpatialWorldIndex() = default;
        ~SpatialWorldIndex() = default;
        SpatialWorldIndex(const SpatialWorldIndex&) = delete;
        SpatialWorldIndex& operator=(const SpatialWorldIndex&) = delete;

        void LoadOverridesLocked();
        void BuildHomeCellIndexLocked();
        void BuildInnDestinationIndexLocked();
        void ClearNpcIndexLocked();

        [[nodiscard]] static bool PassesFilter(const NPCEntry& entry, NPCFilter filter) noexcept {
            switch (filter) {
                case NPCFilter::AdultOnly:
                    return !entry.isChild;
                case NPCFilter::ChildOnly:
                    return entry.isChild;
                case NPCFilter::All:
                default:
                    return true;
            }
        }

        [[nodiscard]] static bool MatchesExclusionRule(
            std::string_view value,
            const std::unordered_set<std::string>& exactSet,
            const std::vector<std::string>& patterns);
        [[nodiscard]] bool IsExcludedName(std::string_view lowerName) const;
        [[nodiscard]] bool IsExcludedEditorID(std::string_view lowerEdid) const;
        [[nodiscard]] std::string GetEditorIDExclusionReason(std::string_view lowerEdid) const;
        [[nodiscard]] bool IsExcludedOriginPlugin(RE::TESForm* form) const;

        [[nodiscard]] bool IsValidNPCCandidate(
            RE::TESNPC* base,
            RE::TESObjectREFR* refr = nullptr,
            std::string* outReason = nullptr) const;

        void IndexLoadedActorLocked(RE::Actor* actor);

        bool ScanHomeCellLocked(RE::TESObjectCELL* cell, HomeCellData& outData);
        void ProcessBedOwnershipLocked(RE::TESObjectREFR& bedRef);
        void ProcessActorHomeLocked(RE::FormID actorFormId, RE::FormID cellId);

        [[nodiscard]] std::string GetLocationInfo(const RE::TESObjectCELL* cell) const;
        [[nodiscard]] std::string GetActorsInCellLocked(RE::FormID cellId) const;
        [[nodiscard]] std::string GetBedsInCellLocked(RE::FormID cellId) const;

        [[nodiscard]] std::size_t GetNPCCountLocked() const noexcept { return m_entriesByRef.size(); }

        [[nodiscard]] std::size_t GetChildCountLocked() const noexcept {
            std::size_t count = 0;
            for (const auto& [_, entry] : m_entriesByRef) {
                if (entry.isChild) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] std::size_t GetAdultCountLocked() const noexcept {
            std::size_t count = 0;
            for (const auto& [_, entry] : m_entriesByRef) {
                if (!entry.isChild) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] std::size_t GetLoadedCountLocked() const noexcept { return m_loadedActors.size(); }

        [[nodiscard]] const NPCEntry* FindByNameLocked(std::string_view searchTerm, NPCFilter filter) const;
        [[nodiscard]] RE::FormID GetRefIDLocked(RE::FormID formID) const;
        [[nodiscard]] RE::FormID GetBaseIDLocked(RE::FormID formID) const;
        [[nodiscard]] RE::Actor* GetActorFromFormIdLocked(RE::FormID formID) const;
        [[nodiscard]] RE::FormID ResolveBaseFormIdForBedsLocked(RE::FormID actorFormID) const;

        mutable std::shared_mutex m_mutex{};

        // ── NPC Index Data ──
        std::unordered_map<RE::FormID, NPCEntry> m_entriesByRef{};
        std::unordered_map<RE::FormID, RE::FormID> m_baseToRefMap{};
        std::unordered_map<std::string, std::vector<RE::FormID>> m_nameToRefMap{};
        std::unordered_map<RE::FormID, RE::Actor*> m_loadedActors{};
        std::vector<std::string> m_allNames{};

        std::unordered_set<std::string> m_excludeExactNames{};
        std::vector<std::string> m_excludeNamePatterns{};
        std::unordered_set<std::string> m_excludeExactEditorIDs{};
        std::vector<std::string> m_excludeEditorIDPatterns{};
        std::unordered_set<std::string> m_excludeExactPlugins{};
        std::vector<std::string> m_excludePluginPatterns{};
        std::unordered_set<RE::FormID> m_iniIncludeForms{};
        std::unordered_set<RE::FormID> m_iniExcludeForms{};

        // ── Home Cell Index Data ──
        std::map<RE::FormID, HomeCellData> m_homeCells{};
        std::map<RE::FormID, ActorHomeData> m_actorsWithHome{};
        std::map<RE::FormID, std::vector<RE::FormID>> m_bedsWithOwners{};

        // ── Inn Travel Destination Index Data ──
        std::vector<Domain::Household::TravelDestinationCandidate> m_innDestinations{};

        std::atomic<bool> m_indexBuilt{ false };
    };

}  // namespace MARAS
