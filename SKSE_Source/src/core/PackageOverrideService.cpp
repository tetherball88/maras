#include "core/PackageOverrideService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "RE/B/BGSScene.h"
#include "RE/B/BGSSceneAction.h"
#include "RE/B/BGSSceneActionPackage.h"
#include "core/FormCache.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "utils/Common.h"

namespace MARAS {

    PackageOverrideService& PackageOverrideService::GetSingleton() {
        static PackageOverrideService instance;
        return instance;
    }

    // ─── Internal helpers ────────────────────────────────────────────────────────

    namespace {

        std::string ToLower(std::string_view s) {
            std::string result(s);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return result;
        }

        std::string Trim(std::string_view s) {
            const auto start = s.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) return {};
            const auto end = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(start, end - start + 1));
        }

        // Returns the quest that owns `pkg` via an actor alias,
        // or nullptr if the package is not alias-driven.
        RE::TESQuest* GetQuestForPackage(RE::ExtraDataList* extraList, RE::TESPackage* pkg) {
            if (!extraList || !pkg) return nullptr;
            const auto* aliasExtra = extraList->GetByType<RE::ExtraAliasInstanceArray>();
            if (!aliasExtra) return nullptr;
            for (const auto* aliasData : aliasExtra->aliases) {
                if (!aliasData || !aliasData->quest || !aliasData->instancedPackages) continue;
                for (const auto* aliasPkg : *aliasData->instancedPackages) {
                    if (aliasPkg == pkg) return aliasData->quest;
                }
            }
            return nullptr;
        }

        // Returns the quest priority of the quest that owns `pkg` via an actor alias,
        // or -1 if the package is not alias-driven (i.e. it's a vanilla NPC package).
        int GetQuestPriorityForPackage(RE::ExtraDataList* extraList, RE::TESPackage* pkg) {
            const auto* quest = GetQuestForPackage(extraList, pkg);
            return quest ? static_cast<int>(quest->data.priority) : -1;
        }

    }  // namespace

    // ─── INI loading ─────────────────────────────────────────────────────────────

    void PackageOverrideService::LoadConfig(std::string_view iniPath) {
        std::unique_lock lock(m_mutex);
        m_whitelistPatterns.clear();
        m_pluginWhitelistPatterns.clear();
        m_questWhitelistPatterns.clear();
        m_questPriorityThreshold = -1;

        std::ifstream file{std::string{iniPath}};
        if (!file.is_open()) {
            MARAS_LOG_INFO("PackageOverrideService: config not found at '{}' (no patterns loaded)", iniPath);
            return;
        }

        // Helper: append comma-separated patterns into a target vector
        auto AppendPatterns = [](const std::string& value, std::vector<std::string>& target) {
            std::istringstream ss(value);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto pattern = ToLower(Trim(token));
                if (!pattern.empty()) {
                    target.push_back(std::move(pattern));
                }
            }
        };

        bool inSection = false;
        std::string line;
        while (std::getline(file, line)) {
            const auto trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

            if (trimmed[0] == '[') {
                const auto end = trimmed.find(']');
                if (end != std::string::npos) {
                    const auto sectionName = ToLower(Trim(trimmed.substr(1, end - 1)));
                    inSection = (sectionName == "packageoverrides");
                }
                continue;
            }

            if (!inSection) continue;

            const auto eqPos = trimmed.find('=');
            if (eqPos == std::string::npos) continue;

            const auto key = ToLower(Trim(trimmed.substr(0, eqPos)));
            const auto value = Trim(trimmed.substr(eqPos + 1));

            if (key == "whitelist") {
                AppendPatterns(value, m_whitelistPatterns);
            } else if (key == "allowedplugins") {
                AppendPatterns(value, m_pluginWhitelistPatterns);
            } else if (key == "questwhitelist") {
                AppendPatterns(value, m_questWhitelistPatterns);
            } else if (key == "questpriority") {
                try {
                    m_questPriorityThreshold = std::stoi(value);
                } catch (...) {
                    MARAS_LOG_WARN("PackageOverrideService: invalid QuestPriority value '{}', ignoring", value);
                }
            }
        }

        MARAS_LOG_INFO("PackageOverrideService: loaded {} whitelist pattern(s), {} allowed plugin(s), {} quest whitelist pattern(s), quest priority threshold {}",
                       m_whitelistPatterns.size(), m_pluginWhitelistPatterns.size(), m_questWhitelistPatterns.size(),
                       m_questPriorityThreshold >= 0 ? std::to_string(m_questPriorityThreshold) : "disabled");
        for (const auto& p : m_whitelistPatterns)
            MARAS_LOG_DEBUG("  whitelist: '{}'", p);
        for (const auto& p : m_pluginWhitelistPatterns)
            MARAS_LOG_DEBUG("  allowedplugins: '{}'", p);
        for (const auto& p : m_questWhitelistPatterns)
            MARAS_LOG_DEBUG("  questwhitelist: '{}'", p);

        // Resolve the hardcoded home sandbox package (TT_MARAS.esp 0x6A) immediately
        // so that RegisterActor calls from PlayerHouseService work from this point forward.
        if (auto* pkg = FormCache::GetSingleton().GetHomeSandboxPackage()) {
            m_baseSandboxPkgID = pkg->GetFormID();
            MARAS_LOG_INFO("PackageOverrideService: base sandbox package auto-resolved to {:08X}",
                           m_baseSandboxPkgID);
        } else {
            MARAS_LOG_WARN("PackageOverrideService: failed to resolve home sandbox package (0x6A) at config load");
        }
    }

    // ─── Wildcard matching ───────────────────────────────────────────────────────

    // Classic greedy wildcard: * matches any sequence, ? matches one character.
    // Both text and pattern must already be lower-cased.
    bool PackageOverrideService::WildcardMatch(std::string_view text, std::string_view pattern) {
        size_t t = 0, p = 0;
        size_t starP = std::string_view::npos, starTSave = 0;

        while (t < text.size()) {
            if (p < pattern.size() && pattern[p] == '*') {
                starP = p++;
                starTSave = t;
            } else if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
                ++p;
                ++t;
            } else if (starP != std::string_view::npos) {
                p = starP + 1;
                t = ++starTSave;
            } else {
                return false;
            }
        }
        // Consume any trailing '*' wildcards
        while (p < pattern.size() && pattern[p] == '*') ++p;
        return p == pattern.size();
    }

    bool PackageOverrideService::MatchesPatternList(std::string_view editorIDLower,
                                                    const std::vector<std::string>& patterns) {
        for (const auto& pattern : patterns) {
            if (WildcardMatch(editorIDLower, pattern)) return true;
        }
        return false;
    }

    bool PackageOverrideService::MatchesWhitelist(const char* editorIDRaw) const {
        if (!editorIDRaw || *editorIDRaw == '\0') return false;
        if (m_whitelistPatterns.empty()) return false;
        const std::string id = ToLower(editorIDRaw);
        if (MatchesPatternList(id, m_whitelistPatterns)) {
            MARAS_LOG_DEBUG("PackageOverrideService: '{}' matched whitelist", editorIDRaw);
            return true;
        }
        return false;
    }

    bool PackageOverrideService::MatchesPluginWhitelist(RE::TESPackage* pkg) const {
        if (!pkg || m_pluginWhitelistPatterns.empty()) return false;
        const auto* file = pkg->GetFile(0);
        if (!file || !file->fileName || *file->fileName == '\0') return false;
        const std::string name = ToLower(file->fileName);
        if (MatchesPatternList(name, m_pluginWhitelistPatterns)) {
            MARAS_LOG_DEBUG("PackageOverrideService: '{}' ({:08X}) matched allowed plugin '{}'",
                            pkg->GetFormEditorID() ? pkg->GetFormEditorID() : "?",
                            pkg->GetFormID(), file->fileName);
            return true;
        }
        return false;
    }

    bool PackageOverrideService::MatchesQuestWhitelist(const char* questEditorIDRaw) const {
        if (!questEditorIDRaw || *questEditorIDRaw == '\0') return false;
        if (m_questWhitelistPatterns.empty()) return false;
        const std::string id = ToLower(questEditorIDRaw);
        if (MatchesPatternList(id, m_questWhitelistPatterns)) {
            MARAS_LOG_DEBUG("PackageOverrideService: quest '{}' matched quest whitelist", questEditorIDRaw);
            return true;
        }
        return false;
    }

    // ─── Package injection ───────────────────────────────────────────────────────

    void PackageOverrideService::ReassertBasePackage(RE::Actor* actor, RE::TESPackage* basePkg) {
        if (!actor || !basePkg) return;

        auto* process = actor->GetActorRuntimeData().currentProcess;
        if (!process) {
            MARAS_LOG_DEBUG("PackageOverrideService: actor {:08X} has no AI process, skipping reassert",
                            actor->GetFormID());
            return;
        }

        process->SetRunOncePackage(basePkg, actor);
        actor->EvaluatePackage(false, false);

        MARAS_LOG_DEBUG("PackageOverrideService: reasserted base package {:08X} on actor {:08X}",
                        basePkg->GetFormID(), actor->GetFormID());
    }

    // ─── Public API ──────────────────────────────────────────────────────────────

    void PackageOverrideService::SetBaseSandboxPackage(RE::FormID pkgID) {
        {
            std::unique_lock lock(m_mutex);
            m_baseSandboxPkgID = pkgID;
        }
        MARAS_LOG_INFO("PackageOverrideService: base sandbox package set to {:08X}", pkgID);
        RebuildFromTenants();
    }

    void PackageOverrideService::RegisterActor(RE::FormID actorID) {
        RE::FormID packageID = 0;
        {
            std::unique_lock lock(m_mutex);
            packageID = m_baseSandboxPkgID;
            if (!packageID) {
                MARAS_LOG_WARN("PackageOverrideService::RegisterActor: base package not set yet, deferring {:08X}",
                               actorID);
                m_registry.insert(actorID);
                return;
            }
            m_registry.insert(actorID);
        }

        // Inject immediately unless the actor is currently following the player —
        // in that case, their follow package will end naturally and kEnd will re-inject.
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
        auto* basePkg = RE::TESForm::LookupByID<RE::TESPackage>(packageID);
        if (actor && basePkg && !actor->IsPlayerTeammate()) {
            ReassertBasePackage(actor, basePkg);
        }

        MARAS_LOG_INFO("PackageOverrideService: registered actor {:08X} → base package {:08X}", actorID, packageID);
    }

    void PackageOverrideService::UnregisterActor(RE::FormID actorID) {
        {
            std::unique_lock lock(m_mutex);
            if (!m_registry.erase(actorID)) return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
        if (actor) {
            actor->EvaluatePackage(false, true);
        }

        MARAS_LOG_INFO("PackageOverrideService: unregistered actor {:08X}", actorID);
    }

    bool PackageOverrideService::IsRegistered(RE::FormID actorID) const {
        std::shared_lock lock(m_mutex);
        return m_registry.count(actorID) > 0;
    }

    // ─── Hook ────────────────────────────────────────────────────────────────────

    // Hooks the call site inside the actor AI update that resolves the "winning" package
    // to run this frame.  Hook point is the same as OStim's PackageStart hook:
    //   RELOCATION_ID(36404, 37398) + 0x47
    // The hooked function has signature: TESPackage*(ExtraDataList*, Actor*)
    // Returning a different package forces the AI to run that package instead.
    struct PackageOverrideService::PackageStartHook {
        static RE::TESPackage* thunk(RE::ExtraDataList* pthis, RE::Actor* actor) {
            auto* candidate = func(pthis, actor);

            // MARAS_LOG_INFO("PackageStartHook: thunk fired, actor={} with candidate={}, is in registry={}", actor ? fmt::format("{:08X}", actor->GetFormID()) : "null",
            //                candidate ? candidate->GetFormEditorID() : "null",
            //                PackageOverrideService::GetSingleton().IsRegistered(actor ? actor->GetFormID() : 0));

            if (!actor || actor->IsPlayerRef()) return candidate;

            auto& svc = PackageOverrideService::GetSingleton();

            {
                std::shared_lock lock(svc.m_mutex);
                if (!svc.m_registry.count(actor->GetFormID())) return candidate;
            }

            // If the actor is currently a follower, skip sandbox override entirely.
            if (PollingService::GetSingleton().IsPlayerTeammate(actor)) {
                MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} is a follower, skipping sandbox override",
                                actor->GetFormID());
                return candidate;
            }

            MARAS_LOG_INFO("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                            actor->GetFormID(),
                            candidate ? (candidate->GetFormEditorID() ? candidate->GetFormEditorID() : "?") : "null",
                            candidate ? candidate->GetFormID() : 0);


            // Log vanilla packages from the actor's base form (TESAIForm::aiPackages / PKID records)
            // if (auto* npc = actor->GetActorBase()) {
            //     int idx = 0;
            //     for (auto* pkg : npc->aiPackages.packages) {
            //         if (pkg) {
            //             MARAS_LOG_INFO("  vanilla pkg[{}]: '{}' ({:08X})", idx,
            //                             pkg->GetFormEditorID() ? pkg->GetFormEditorID() : "?",
            //                             pkg->GetFormID());
            //         }
            //         ++idx;
            //     }
            //     if (idx == 0) {
            //         MARAS_LOG_DEBUG("  vanilla pkg: (none)");
            //     }
            // }

            // Log packages from quest aliases the actor currently belongs to
            // if (pthis) {
            //     if (auto* aliasExtra = pthis->GetByType<RE::ExtraAliasInstanceArray>()) {
            //         for (auto* aliasData : aliasExtra->aliases) {
            //             if (!aliasData || !aliasData->instancedPackages || aliasData->instancedPackages->empty()) continue;
            //             const char* questName = (aliasData->quest && aliasData->quest->GetFormEditorID()) ? aliasData->quest->GetFormEditorID() : "?";
            //             const RE::FormID questID = aliasData->quest ? aliasData->quest->GetFormID() : 0;
            //             const char* aName = (aliasData->alias && !aliasData->alias->aliasName.empty()) ? aliasData->alias->aliasName.c_str() : "?";
            //             for (auto* pkg : *aliasData->instancedPackages) {
            //                 if (pkg) {
            //                     MARAS_LOG_INFO("  alias pkg [quest='{}' ({:08X}) alias='{}']: '{}' ({:08X})",
            //                                     questName, questID, aName,
            //                                     pkg->GetFormEditorID() ? pkg->GetFormEditorID() : "?",
            //                                     pkg->GetFormID());
            //                 }
            //             }
            //         }
            //     }
            // }

            // if (auto* scene = actor->GetCurrentScene()) {
            //     const char* sceneName = scene->GetFormEditorID() ? scene->GetFormEditorID() : "?";
            //     const RE::FormID sceneID = scene->GetFormID();
            //     const char* parentQuestName = (scene->parentQuest && scene->parentQuest->GetFormEditorID()) ? scene->parentQuest->GetFormEditorID() : "?";
            //     const RE::FormID parentQuestID = scene->parentQuest ? scene->parentQuest->GetFormID() : 0;
            //     MARAS_LOG_INFO("  current scene: '{}' ({:08X}) parentQuest='{}' ({:08X})",
            //                     sceneName, sceneID, parentQuestName, parentQuestID);

            //     int scenePkgCount = 0;
            //     for (auto* action : scene->actions) {
            //         if (!action || action->GetType() != RE::BGSSceneAction::Type::kPackage) continue;
            //         auto* pkgAction = static_cast<RE::BGSSceneActionPackage*>(action);
            //         for (auto* pkg : pkgAction->packages) {
            //             if (!pkg) continue;
            //             MARAS_LOG_INFO("  scene pkg [actorAliasID={} phases {}->{}]: '{}' ({:08X})",
            //                             action->actorID,
            //                             action->startPhase,
            //                             action->endPhase,
            //                             pkg->GetFormEditorID() ? pkg->GetFormEditorID() : "?",
            //                             pkg->GetFormID());
            //             ++scenePkgCount;
            //         }
            //     }
            //     if (scenePkgCount == 0) {
            //         MARAS_LOG_INFO("  scene pkg: (none)");
            //     }
            // }

            const RE::FormID sandboxID = svc.m_baseSandboxPkgID;
            if (!sandboxID) {
                MARAS_LOG_WARN("PackageStartHook: sandboxID is 0 for registered actor {:08X}, passing through",
                               actor->GetFormID());
                return candidate;
            }
            if (!candidate) {
                // null means no override — game would fall back to the actor's vanilla package stack.
                // For registered tenants this means our sandbox lost; re-assert it.
                MARAS_LOG_DEBUG("PackageStartHook: candidate is null for registered actor {:08X}, asserting sandbox",
                                actor->GetFormID());
                return RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
            }
            if (candidate->GetFormID() == sandboxID) {
                MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} already running sandbox {:08X}, no override needed",
                                actor->GetFormID(), sandboxID);
                return candidate;
            }

            const char* editorID = candidate->GetFormEditorID();

            // Whitelist takes priority: explicitly permitted packages run freely.
            if (svc.MatchesWhitelist(editorID)) {
                MARAS_LOG_DEBUG("PackageStartHook: allowing whitelisted '{}' ({:08X}) on actor {:08X}",
                                editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID());
                return candidate;
            }

            // Plugin whitelist: allow any package whose originating plugin is permitted.
            if (svc.MatchesPluginWhitelist(candidate)) {
                MARAS_LOG_DEBUG("PackageStartHook: allowing plugin-whitelisted '{}' ({:08X}) on actor {:08X}",
                                editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID());
                return candidate;
            }

            // Quest priority threshold: for alias-driven packages, compare the owning quest's
            // priority against the configured threshold.
            //   quest priority >= threshold  →  allow candidate  (threshold is the lower bound)
            //   quest priority <  threshold  →  redirect to sandbox
            //   no quest association         →  fall through to sandbox (not alias-driven)
            const int questThreshold = svc.m_questPriorityThreshold;
            if (questThreshold >= 0) {
                const int questPriority = GetQuestPriorityForPackage(pthis, candidate);
                if (questPriority >= 0) {
                    if (questPriority >= questThreshold) {
                        MARAS_LOG_DEBUG("PackageStartHook: allowing '{}' ({:08X}) on actor {:08X}, quest priority {} >= threshold {}",
                                        editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID(),
                                        questPriority, questThreshold);
                        return candidate;
                    } else {
                        // Priority below threshold — quest whitelist can still rescue it.
                        auto* quest = GetQuestForPackage(pthis, candidate);
                        if (quest && svc.MatchesQuestWhitelist(quest->GetFormEditorID())) {
                            MARAS_LOG_DEBUG("PackageStartHook: allowing '{}' ({:08X}) on actor {:08X} (quest whitelist '{}', priority {} < threshold {})",
                                            editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID(),
                                            quest->GetFormEditorID() ? quest->GetFormEditorID() : "?",
                                            questPriority, questThreshold);
                            return candidate;
                        }
                        MARAS_LOG_DEBUG("PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, quest priority {} < threshold {}",
                                        editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID(),
                                        questPriority, questThreshold);
                        return RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
                    }
                }
            }

            MARAS_LOG_DEBUG("PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, redirecting to sandbox",
                            editorID ? editorID : "?", candidate->GetFormID(), actor->GetFormID());
            return RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> target{RELOCATION_ID(36404, 37398),
                                                   REL::VariantOffset(0x47, 0x47, 0x47)};
            auto& trampoline = SKSE::GetTrampoline();
            SKSE::AllocTrampoline(14);
            func = trampoline.write_branch<5>(target.address(), thunk);
            MARAS_LOG_INFO("PackageOverrideService: PackageStartHook installed at {:016X}", target.address());
        }
    };

    void PackageOverrideService::InstallHooks() {
        PackageStartHook::Install();
    }

    // ─── Registry rebuild ─────────────────────────────────────────────────────────

    // Derives who should have the base package purely from PlayerHouseService tenants.
    // Called on every game load after PlayerHouseService cosave data is restored.
    void PackageOverrideService::RebuildFromTenants() {
        RE::FormID packageFormID = 0;
        {
            std::shared_lock lock(m_mutex);
            packageFormID = m_baseSandboxPkgID;
        }

        if (!packageFormID) {
            MARAS_LOG_WARN("PackageOverrideService::RebuildFromTenants: base package not set, skipping");
            return;
        }

        // Collect all tenant FormIDs from every registered player house
        std::vector<RE::FormID> tenantIDs;
        const auto houseIDs = PlayerHouseService::GetSingleton().GetAllPlayerHouses();
        for (auto houseID : houseIDs) {
            const auto tenants = PlayerHouseService::GetSingleton().GetPlayerHouseTenants(houseID);
            tenantIDs.insert(tenantIDs.end(), tenants.begin(), tenants.end());
        }

        // Atomically replace the registry
        {
            std::unique_lock lock(m_mutex);
            m_registry.clear();
            for (auto tenantID : tenantIDs) {
                m_registry.insert(tenantID);
            }
        }

        MARAS_LOG_INFO("PackageOverrideService: rebuilt registry with {} tenant(s) from {} house(s)",
                       tenantIDs.size(), houseIDs.size());
        for (auto id : tenantIDs) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            MARAS_LOG_INFO("  registry actor {:08X} ({})", id,
                           actor ? (actor->GetName() ? actor->GetName() : "?") : "not loaded");
        }

        // Inject the package on any already-loaded actors (skip player teammates)
        auto* basePkg = RE::TESForm::LookupByID<RE::TESPackage>(packageFormID);
        if (!basePkg) {
            MARAS_LOG_WARN("PackageOverrideService::RebuildFromTenants: package {:08X} not found", packageFormID);
            return;
        }
        for (auto tenantID : tenantIDs) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(tenantID)) {
                if (!actor->IsPlayerTeammate()) {
                    ReassertBasePackage(actor, basePkg);
                }
            }
        }
    }

    void PackageOverrideService::Revert() {
        std::unique_lock lock(m_mutex);
        m_registry.clear();
        MARAS_LOG_INFO("PackageOverrideService: reverted");
    }

}  // namespace MARAS
