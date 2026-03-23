#pragma once

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "PCH.h"

namespace MARAS {

    //
    // PackageOverrideService
    //
    // Replaces ActorUtil.AddPackageOverride for MARAS-managed actors.
    // Maintains a "base" AI package per actor that acts as a persistent fallback:
    //
    //   Whitelist (Whitelist= in INI): packages whose editor IDs match these patterns
    //   are always allowed to run over the MARAS base package.
    //
    //   AllowedPlugins (AllowedPlugins= in INI): packages whose *originating* plugin
    //   (the file that created the form, i.e. GetFile(0)) matches these patterns are
    //   allowed to run over the MARAS base package.
    //
    //   Both lists take priority over the sandbox redirect.
    //
    //   On any package ending: re-injects the base package at low priority so it
    //   activates as soon as nothing more important is competing.
    //
    // Pattern wildcards:
    //   SkyrimNet*      matches any editor ID / plugin name beginning with "SkyrimNet"
    //   *TalkToPlayer   matches any editor ID / plugin name ending with "TalkToPlayer"
    //   *Chat*          matches any editor ID / plugin name containing "Chat"
    //   ExactName       exact match (case-insensitive)
    //
    class PackageOverrideService {
    public:
        static PackageOverrideService& GetSingleton();

        // Install the AI package-selection hook. Must be called during SKSE hook installation.
        static void InstallHooks();

        // Load suppression patterns from INI.
        // Should be called once at kDataLoaded.
        void LoadConfig(std::string_view iniPath = "Data/SKSE/Plugins/MARAS/PackageOverrides.ini");

        // One-time initialisation: store the base sandbox package and rebuild the registry
        // from current PlayerHouseService tenants.  Called automatically from LoadConfig
        // (kDataLoaded) using the hardcoded FormID, and again from PlayerHouseService::Load
        // after cosave restore.
        void SetBaseSandboxPackage(RE::FormID pkgID);

        // Register an actor with the currently configured base package.
        // Immediately injects the package on the live actor (unless they are a player
        // teammate, in which case injection is deferred until their follow package ends).
        void RegisterActor(RE::FormID actorID);

        // Remove an actor from management and strip the base package override.
        void UnregisterActor(RE::FormID actorID);

        // Returns true if the actor is currently managed by this service.
        bool IsRegistered(RE::FormID actorID) const;

        // Rebuild the registry from PlayerHouseService tenant data using the stored
        // base package.  Called automatically after cosave restore.
        void RebuildFromTenants();

        // Clears the registry (call on game revert/new game).
        void Revert();

    private:
        PackageOverrideService() = default;

        // Returns true if editorID matches any pattern in the whitelist.
        bool MatchesWhitelist(const char* editorID) const;

        // Returns true if the package's originating plugin matches any AllowedPlugins pattern.
        bool MatchesPluginWhitelist(RE::TESPackage* pkg) const;

        // Returns true if questEditorID matches any pattern in the quest whitelist.
        bool MatchesQuestWhitelist(const char* questEditorID) const;

        // Checks a pattern vector (must be lower-cased) against a lower-cased editorID.
        static bool MatchesPatternList(std::string_view editorIDLower,
                                       const std::vector<std::string>& patterns);

        // Simple wildcard match: * matches any sequence of characters.
        // Both text and pattern must be lower-cased before calling.
        static bool WildcardMatch(std::string_view text, std::string_view pattern);

        // Run the sandbox package immediately on the actor via SetRunOncePackage + EvaluatePackage.
        void ReassertBasePackage(RE::Actor* actor, RE::TESPackage* basePkg);

        struct PackageStartHook;  // defined in PackageOverrideService.cpp

        mutable std::shared_mutex m_mutex;

        // Set of actor FormIDs currently managed by this service
        std::unordered_set<RE::FormID> m_registry;

        // Base sandbox package — resolved from FormCache at LoadConfig (hardcoded 0x6A)
        RE::FormID m_baseSandboxPkgID{0};

        // Patterns loaded from INI (all lower-cased for case-insensitive comparison)
        std::vector<std::string> m_whitelistPatterns;        // allowed by package editor ID
        std::vector<std::string> m_pluginWhitelistPatterns;  // allowed by originating plugin name
        std::vector<std::string> m_questWhitelistPatterns;   // quest editor IDs exempt from priority threshold

        // Quest priority threshold (-1 = disabled).
        // Candidate packages from alias quests with priority >= threshold are allowed.
        // Candidate packages from alias quests with priority <  threshold are suppressed,
        // unless the quest editor ID matches m_questWhitelistPatterns.
        int m_questPriorityThreshold{-1};

    };

}  // namespace MARAS
