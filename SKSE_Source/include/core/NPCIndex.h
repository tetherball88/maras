#pragma once

#include "utils/Common.h"

namespace MARAS {

    /**
     * Filter options for querying NPCs in the index.
     */
    enum class NPCFilter : uint8_t {
        All = 0,
        AdultOnly = 1,
        ChildOnly = 2
    };

    /**
     * Representation of an indexed NPC entry in MARAS.
     */
    struct NPCEntry {
        RE::FormID  refFormID = 0;       // Placed reference FormID (primary runtime handle)
        RE::FormID  baseFormID = 0;      // TESNPC base FormID
        std::string editorID;            // Base form EditorID (if available)
        std::string nameLower;           // Lowercase name for exact/fuzzy lookups
        std::string nameDisplay;         // Original display name
        bool        isChild = false;     // True if race flag kChild is set
        bool        isUnique = false;    // True if TESNPC kUnique flag was set
        uint8_t     sex = 0;             // 0 = Male, 1 = Female
        std::string raceName;            // Display race name
        std::string currentCell;         // Current cell name if loaded
        std::string pluginName;          // Mod/plugin where NPC was originally declared
    };

    /**
     * NPC Index - Centralized NPC database and search system for M.A.R.A.S.
     *
     * Provides:
     * - Fast O(1) hash lookups by FormID or name
     * - Multi-phase base->ref resolution during BuildIndex()
     * - Full fuzzy matching (Levenshtein + substring + live scan)
     * - Adult vs Child filtering
     * - Configurable INI overrides (Data/SKSE/Plugins/MARAS/npc_index_overrides.ini)
     * - Thread-safe access via std::shared_mutex
     */
    class NPCIndex {
    public:
        static NPCIndex& GetSingleton() {
            static NPCIndex instance;
            return instance;
        }

        /**
         * Build the NPC index from scratch on kDataLoaded.
         * Scans base NPC forms, persistent cells across all worldspaces,
         * interior cells, relationship records, INI overrides, and loaded actors.
         */
        void BuildIndex();

        /**
         * Quick refresh on save load (kPostLoadGame / kNewGame).
         * Evicts stale loaded pointers and re-sweeps currently loaded actors.
         */
        void RefreshIndex();

        /**
         * Register or update an actor when loaded dynamically in game.
         * Thread-safe (acquires exclusive lock).
         * Resolves base FormID -> placed ref FormID, updates live actor cache,
         * and ensures entry exists in m_entriesByRef and lookup maps.
         */
        void RegisterLoadedActor(RE::Actor* actor);

        /**
         * Clear only transient live actor pointers on pre-load.
         * Keeps static master index data and INI overrides intact.
         */
        void ClearLoadedActors();

        /**
         * Clear all index data (called on full reset or shutdown).
         */
        void ClearIndex();

        /**
         * Check if the index has been built.
         */
        bool IsIndexBuilt() const { return m_indexBuilt.load(std::memory_order_acquire); }

        /**
         * Find an NPCEntry by name using fuzzy search cascade:
         * 1. Exact match (case-insensitive)
         * 2. Fuzzy match (Levenshtein distance <= threshold)
         * 3. Substring match
         * 4. Live process list scan fallback
         */
        std::optional<NPCEntry> FindByName(std::string_view searchTerm, NPCFilter filter = NPCFilter::All);

        /**
         * Find a live RE::Actor* by name using fuzzy search cascade.
         */
        RE::Actor* FindActorByName(std::string_view searchTerm, NPCFilter filter = NPCFilter::All);

        /**
         * Proximity-aware search. Returns the matching NPC closest to nearActor.
         */
        RE::Actor* FindActorByNameNear(std::string_view searchTerm, RE::Actor* nearActor,
                                      bool allowSelf = false, NPCFilter filter = NPCFilter::All);

        /**
         * Find an NPCEntry by FormID (accepts either placed Ref FormID or Base FormID).
         */
        std::optional<NPCEntry> FindByFormID(RE::FormID formID);

        /**
         * Resolve a base FormID to its placed reference FormID.
         * If formID is already a reference FormID, returns it directly.
         */
        RE::FormID GetRefID(RE::FormID formID);

        /**
         * Resolve a reference FormID to its base TESNPC FormID.
         */
        RE::FormID GetBaseID(RE::FormID formID);

        /**
         * Get live RE::Actor* from FormID (resolves base or ref).
         * Returns nullptr if NPC is unloaded or form is invalid.
         */
        RE::Actor* GetActorFromFormId(RE::FormID formID);

        /**
         * Search for NPCs whose name contains the query string (case-insensitive).
         * Used for UI auto-complete and search dialogs.
         */
        std::vector<NPCEntry> SearchBySubstring(std::string_view query, size_t maxResults = 50,
                                               NPCFilter filter = NPCFilter::All);

        /**
         * Retrieve all indexed NPC entries matching the given filter.
         */
        std::vector<NPCEntry> GetAllEntries(NPCFilter filter = NPCFilter::All);

        /**
         * Export current NPC index database and metadata to JSON.
         */
        nlohmann::json DumpIndexJson() const;

        /**
         * Statistics.
         */
        size_t GetNPCCount() const {
            std::shared_lock lock(m_mutex);
            return GetNPCCountLocked();
        }

        size_t GetChildCount() const {
            std::shared_lock lock(m_mutex);
            return GetChildCountLocked();
        }

        size_t GetAdultCount() const {
            std::shared_lock lock(m_mutex);
            return GetAdultCountLocked();
        }

        size_t GetLoadedCount() const {
            std::shared_lock lock(m_mutex);
            return GetLoadedCountLocked();
        }

    private:
        NPCIndex() = default;
        ~NPCIndex() = default;
        NPCIndex(const NPCIndex&) = delete;
        NPCIndex& operator=(const NPCIndex&) = delete;

        // INI override loading helpers
        void LoadOverrides();

        // Helper to check if an entry passes the filter
        static bool PassesFilter(const NPCEntry& entry, NPCFilter filter) {
            switch (filter) {
                case NPCFilter::AdultOnly: return !entry.isChild;
                case NPCFilter::ChildOnly: return entry.isChild;
                case NPCFilter::All:
                default: return true;
            }
        }

        // Helper to check if a name, editor ID, or origin plugin is excluded by INI overrides
        static bool MatchesExclusionRule(std::string_view value,
                                         const std::unordered_set<std::string>& exactSet,
                                         const std::vector<std::string>& patterns);
        bool        IsExcludedName(std::string_view lowerName) const;
        bool        IsExcludedEditorID(std::string_view lowerEdid) const;
        std::string GetEditorIDExclusionReason(std::string_view lowerEdid) const;
        bool        IsExcludedOriginPlugin(RE::TESForm* form) const;

        // Unified authoritative candidate filter running all INI, uniqueness, humanoid, ghost, dead, and name rules
        bool        IsValidNPCCandidate(RE::TESNPC* base, RE::TESObjectREFR* refr = nullptr, std::string* outReason = nullptr) const;

        // Helper to register / index a loaded actor (caller must hold m_mutex)
        void IndexLoadedActorLocked(RE::Actor* actor);

        // Internal locked implementations
        size_t GetNPCCountLocked() const {
            return m_entriesByRef.size();
        }

        size_t GetChildCountLocked() const {
            size_t count = 0;
            for (const auto& [_, entry] : m_entriesByRef) {
                if (entry.isChild) count++;
            }
            return count;
        }

        size_t GetAdultCountLocked() const {
            size_t count = 0;
            for (const auto& [_, entry] : m_entriesByRef) {
                if (!entry.isChild) count++;
            }
            return count;
        }

        size_t GetLoadedCountLocked() const {
            return m_loadedActors.size();
        }

        const NPCEntry* FindByNameLocked(std::string_view searchTerm, NPCFilter filter) const;
        RE::FormID      GetRefIDLocked(RE::FormID formID) const;
        RE::FormID      GetBaseIDLocked(RE::FormID formID) const;
        RE::Actor*      GetActorFromFormIdLocked(RE::FormID formID) const;

        mutable std::shared_mutex m_mutex;

        // FormID -> NPCEntry (keyed by ref FormID)
        std::unordered_map<RE::FormID, NPCEntry> m_entriesByRef;

        // Base FormID -> Placed Ref FormID mapping
        std::unordered_map<RE::FormID, RE::FormID> m_baseToRefMap;

        // Lowercase Name -> Ref FormIDs (multimap for duplicate name support)
        std::unordered_map<std::string, std::vector<RE::FormID>> m_nameToRefMap;

        // Ref FormID -> live Actor* (only loaded NPCs)
        std::unordered_map<RE::FormID, RE::Actor*> m_loadedActors;

        // List of unique indexed lowercase names for Levenshtein fuzzy search
        std::vector<std::string> m_allNames;

        // Name exact matches and patterns to exclude
        std::unordered_set<std::string> m_excludeExactNames;
        std::vector<std::string>        m_excludeNamePatterns;

        // EditorID exact matches and patterns to exclude
        std::unordered_set<std::string> m_excludeExactEditorIDs;
        std::vector<std::string>        m_excludeEditorIDPatterns;

        // Origin plugins to exclude (exact and wildcard patterns)
        std::unordered_set<std::string> m_excludeExactPlugins;
        std::vector<std::string>        m_excludePluginPatterns;

        // Force include / exclude lists from INI
        std::unordered_set<RE::FormID> m_iniIncludeForms;
        std::unordered_set<RE::FormID> m_iniExcludeForms;

        std::atomic<bool> m_indexBuilt{ false };
    };

}  // namespace MARAS
