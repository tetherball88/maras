#pragma once

#include "PCH.h"

namespace MARAS {

    class NameService {
    public:
        static NameService& GetSingleton();

        // Load or reload both child and critter names from JSON files
        bool LoadFromFile(const std::filesystem::path& customChildPath = "", const std::filesystem::path& customCritterPath = "");

        // Load or reload child names from childrenNames.json (Data/SKSE/Plugins/MARAS/childrenNames.json or SKSE/Plugins/MARAS/childrenNames.json)
        bool LoadChildNamesFromFile(const std::filesystem::path& customPath = "");

        // Load or reload critter names from critterNames.json (Data/SKSE/Plugins/MARAS/critterNames.json or SKSE/Plugins/MARAS/critterNames.json)
        bool LoadCritterNamesFromFile(const std::filesystem::path& customPath = "");

        // ── Child Name Generation ─────────────────────────────────────────────
        // Generate a random child name given race name (optional; empty = any race) and gender flag (true = female, false = male).
        // If isUnique is true, ensures the returned name is not already used by any registered child.
        // Returns empty string if no matching name exists. Never defaults to fake names.
        std::string GetRandomChildName(const std::string& raceName = "", bool isFemale = false, bool isUnique = true);

        // Generate a random child name for an actor (deriving race and gender automatically).
        // If isUnique is true, ensures the returned name is not already used by any registered child.
        // Returns empty string if actor is null or no matching name exists.
        std::string GetRandomChildName(RE::Actor* actor, bool isUnique = true);

        // Generate a random child name given race form (optional; TESRace or TESForm, empty/None = any race) and gender flag.
        // If isUnique is true, ensures the returned name is not already used by any registered child.
        // Returns empty string if no matching name exists.
        std::string GetRandomChildName(RE::TESForm* raceForm, bool isFemale = false, bool isUnique = true);

        // Get all available names for a given race (optional; empty = all races) and gender
        std::vector<std::string> GetChildNames(const std::string& raceName = "", bool isFemale = false) const;

        // Check if child names have been successfully loaded
        bool IsChildLoaded() const;

        // Normalize race name string to canonical JSON key (e.g. "nord", "breton", "altmer", etc.)
        std::string NormalizeRaceKey(std::string_view rawRace) const;

        // Collect all names currently used by registered children (lowercase)
        std::unordered_set<std::string> GetExistingChildNames() const;

        // ── Critter Name Generation ───────────────────────────────────────────
        // Generate a random critter name given critter race name or EDID
        // (e.g. "BYOHRelationshipAdoptionCritter_Fox", "BYOHRelationshipAdoptionCritter_FoxArctic",
        // "BYOHRelationshipAdoptionCritter_Hare", "BYOHRelationshipAdoptionCritter_Mudcrab",
        // "BYOHRelationshipAdoptionCritter_Skeever", "fox", "arctic_fox", etc.).
        // If race is empty, unknown, or not found, picks a random name from all available critter names.
        std::string GetRandomCritterName(const std::string& raceName = "");

        // Generate a random critter name for an actor (deriving race automatically from actor or base). If unknown race, chooses a random name.
        std::string GetRandomCritterName(RE::Actor* actor);

        // Generate a random critter name given race or actor base form. If unknown race, chooses a random name.
        std::string GetRandomCritterName(RE::TESForm* raceForm);

        // Get all available critter names for a given critter race (e.g. "fox", "arctic_fox", "hare", "mudcrab", "skeever"). If empty/unknown, returns all critter names.
        std::vector<std::string> GetCritterNames(const std::string& raceName = "") const;

        // Check if critter names have been successfully loaded
        bool IsCritterLoaded() const;

        // Normalize critter race string/EDID to canonical JSON key (e.g. "arctic_fox", "fox", "hare", "mudcrab", "skeever")
        std::string NormalizeCritterRaceKey(std::string_view rawRace) const;

        // ── General ───────────────────────────────────────────────────────────
        // Check if names have been successfully loaded
        bool IsLoaded() const;

        // Clear all loaded names
        void Clear();

    private:
        NameService();
        ~NameService() = default;

        NameService(const NameService&) = delete;
        NameService& operator=(const NameService&) = delete;

        std::string NormalizeRaceKeyLocked(std::string_view rawRace) const;
        std::string NormalizeCritterRaceKeyLocked(std::string_view rawRace) const;

        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, std::vector<std::string>> maleNames_;
        std::unordered_map<std::string, std::vector<std::string>> femaleNames_;
        std::unordered_map<std::string, std::vector<std::string>> critterNames_;
        std::atomic<bool> isChildLoaded_{ false };
        std::atomic<bool> isCritterLoaded_{ false };

        std::mt19937 rng_;
    };

}  // namespace MARAS
