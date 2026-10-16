#include "core/PackageOverrideService.h"

#include "RE/B/BGSScene.h"
#include "RE/B/BGSSceneAction.h"
#include "RE/B/BGSSceneActionPackage.h"
#include "core/FormCache.h"
#include "core/ChildrenFleeService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/StringUtils.h"

namespace MARAS {

    PackageOverrideService& PackageOverrideService::GetSingleton() {
        static PackageOverrideService instance;
        return instance;
    }

    // ─── Internal helpers ────────────────────────────────────────────────────────

    namespace {

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

        std::string GetPackageDesc(RE::TESPackage* pkg) {
            if (!pkg) return "null";
            const char* edid = pkg->GetFormEditorID();
            if (edid && edid[0] != '\0') {
                return edid;
            }
            const auto* file = pkg->GetFile(0);
            if (file && file->fileName && file->fileName[0] != '\0') {
                return fmt::format("{}~{:08X}", file->fileName, pkg->GetFormID());
            }
            return fmt::format("{:08X}", pkg->GetFormID());
        }

        std::string GetQuestDesc(RE::TESQuest* quest) {
            if (!quest) return "?";
            const char* edid = quest->GetFormEditorID();
            if (edid && edid[0] != '\0') {
                return edid;
            }
            const auto* file = quest->GetFile(0);
            if (file && file->fileName && file->fileName[0] != '\0') {
                return fmt::format("{}~{:08X}", file->fileName, quest->GetFormID());
            }
            return fmt::format("{:08X}", quest->GetFormID());
        }

        enum class PackageDecision : std::uint8_t {
            Follower,
            NoSandbox,
            CandidateNullAssertSandbox,
            AlreadyRunningSandbox,
            Whitelist,
            PluginWhitelist,
            QuestWhitelist,
            QuestPriorityAllowed,
            QuestPrioritySuppressed,
            SuppressedRedirectSandbox
        };

        struct HookEvalState {
            RE::FormID candidateID{0};
            RE::FormID resultID{0};
            PackageDecision decision{PackageDecision::NoSandbox};

            bool operator==(const HookEvalState& o) const noexcept {
                return candidateID == o.candidateID && resultID == o.resultID && decision == o.decision;
            }
        };

        std::mutex s_evalMutex;
        std::unordered_map<RE::FormID, HookEvalState> s_lastEvals;

        void InvalidateHookEvalActor(RE::FormID actorID) {
            std::lock_guard lock(s_evalMutex);
            s_lastEvals.erase(actorID);
        }

        void ClearHookEvalCache() {
            std::lock_guard lock(s_evalMutex);
            s_lastEvals.clear();
        }

        template <typename LogFn>
        void CheckAndLog(RE::FormID actorID, RE::FormID candidateID, RE::FormID resultID,
                         PackageDecision decision, LogFn&& logFn) {
            const HookEvalState newState{candidateID, resultID, decision};
            {
                std::lock_guard lock(s_evalMutex);
                auto it = s_lastEvals.find(actorID);
                if (it != s_lastEvals.end() && it->second == newState) {
                    return;
                }
                s_lastEvals[actorID] = newState;
            }
            logFn();
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
                auto pattern = StringUtils::ToLowerStd(StringUtils::TrimView(token));
                if (!pattern.empty()) {
                    target.push_back(std::move(pattern));
                }
            }
        };

        bool inSection = false;
        std::string line;
        while (std::getline(file, line)) {
            const auto trimmed = StringUtils::TrimView(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

            if (trimmed[0] == '[') {
                const auto end = trimmed.find(']');
                if (end != std::string_view::npos) {
                    const auto sectionName = StringUtils::ToLowerStd(StringUtils::TrimView(trimmed.substr(1, end - 1)));
                    inSection = (sectionName == "packageoverrides");
                }
                continue;
            }

            if (!inSection) continue;

            const auto eqPos = trimmed.find('=');
            if (eqPos == std::string_view::npos) continue;

            const auto key = StringUtils::ToLowerStd(StringUtils::TrimView(trimmed.substr(0, eqPos)));
            const auto value = StringUtils::TrimStd(trimmed.substr(eqPos + 1));

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

        // Resolve the hardcoded pet/critter home sandbox package (TT_MARAS.esp 0xC0) immediately
        if (auto* petPkg = FormCache::GetSingleton().GetPetCritterHomeSandboxPackage()) {
            m_petCritterSandboxPkgID = petPkg->GetFormID();
            MARAS_LOG_INFO("PackageOverrideService: pet/critter sandbox package auto-resolved to {:08X}",
                           m_petCritterSandboxPkgID);
        } else {
            MARAS_LOG_WARN("PackageOverrideService: failed to resolve pet/critter sandbox package (0xC0) at config load");
        }

        // Resolve the hardcoded non-player child follow package (TT_MARAS.esp 0x376) immediately
        if (auto* childPkg = FormCache::GetSingleton().GetChildrenNonPlayerFollowParentPackage()) {
            m_nonPlayerChildFollowPkgID = childPkg->GetFormID();
            MARAS_LOG_INFO("PackageOverrideService: non-player child follow package auto-resolved to {:08X}",
                           m_nonPlayerChildFollowPkgID);
        } else {
            MARAS_LOG_WARN("PackageOverrideService: failed to resolve non-player child follow package (0x376) at config load");
        }
    }

    // ─── Wildcard matching ───────────────────────────────────────────────────────

    bool PackageOverrideService::MatchesPatternList(std::string_view text,
                                                    const std::vector<std::string>& patterns) {
        for (const auto& pattern : patterns) {
            if (StringUtils::WildcardMatch(text, pattern, true)) return true;
        }
        return false;
    }

    bool PackageOverrideService::MatchesWhitelist(const char* editorIDRaw) const {
        if (!editorIDRaw || *editorIDRaw == '\0') return false;
        std::shared_lock lock(m_mutex);
        if (m_whitelistPatterns.empty()) return false;
        return MatchesPatternList(editorIDRaw, m_whitelistPatterns);
    }

    bool PackageOverrideService::MatchesPluginWhitelist(RE::TESPackage* pkg) const {
        if (!pkg) return false;
        const auto* file = pkg->GetFile(0);
        if (!file || !file->fileName || *file->fileName == '\0') return false;
        std::shared_lock lock(m_mutex);
        if (m_pluginWhitelistPatterns.empty()) return false;
        return MatchesPatternList(file->fileName, m_pluginWhitelistPatterns);
    }

    bool PackageOverrideService::MatchesQuestWhitelist(const char* questEditorIDRaw) const {
        if (!questEditorIDRaw || *questEditorIDRaw == '\0') return false;
        std::shared_lock lock(m_mutex);
        if (m_questWhitelistPatterns.empty()) return false;
        return MatchesPatternList(questEditorIDRaw, m_questWhitelistPatterns);
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
        if (actorID == 0 || IsSyntheticChildID(actorID)) return;
        RegisterActorWithPackage(actorID, 0);
    }

    void PackageOverrideService::RegisterPetCritter(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) return;
        RE::FormID packageID = 0;
        {
            std::shared_lock lock(m_mutex);
            packageID = m_petCritterSandboxPkgID;
        }
        if (!packageID) {
            if (auto* petPkg = FormCache::GetSingleton().GetPetCritterHomeSandboxPackage()) {
                packageID = petPkg->GetFormID();
                std::unique_lock lock(m_mutex);
                m_petCritterSandboxPkgID = packageID;
            }
        }
        if (!packageID) {
            MARAS_LOG_WARN("PackageOverrideService::RegisterPetCritter: pet/critter sandbox package (0xC0) not resolved, skipping {:08X}",
                           actorID);
            return;
        }
        RegisterActorWithPackage(actorID, packageID);
    }

    void PackageOverrideService::RegisterNonPlayerChild(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) return;
        RE::FormID packageID = 0;
        {
            std::shared_lock lock(m_mutex);
            packageID = m_nonPlayerChildFollowPkgID;
        }
        if (!packageID) {
            if (auto* childPkg = FormCache::GetSingleton().GetChildrenNonPlayerFollowParentPackage()) {
                packageID = childPkg->GetFormID();
                std::unique_lock lock(m_mutex);
                m_nonPlayerChildFollowPkgID = packageID;
            }
        }
        if (!packageID) {
            MARAS_LOG_WARN("PackageOverrideService::RegisterNonPlayerChild: non-player child follow package (0x376) not resolved, skipping {:08X}",
                           actorID);
            return;
        }
        RegisterActorWithPackage(actorID, packageID);
    }

    void PackageOverrideService::RegisterActorWithPackage(RE::FormID actorID, RE::FormID targetPackageID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) return;
        RE::FormID packageID = targetPackageID;
        {
            std::unique_lock lock(m_mutex);
            if (!packageID) {
                if (!m_baseSandboxPkgID) {
                    if (auto* pkg = FormCache::GetSingleton().GetHomeSandboxPackage()) {
                        m_baseSandboxPkgID = pkg->GetFormID();
                    }
                }
                packageID = m_baseSandboxPkgID;
            }
            if (!packageID) {
                MARAS_LOG_WARN("PackageOverrideService::RegisterActorWithPackage: base package not set yet, deferring {:08X}",
                               actorID);
                m_registry[actorID] = targetPackageID;
                return;
            }
            m_registry[actorID] = targetPackageID;
        }

        // Inject immediately unless the actor is currently following the player —
        // in that case, their follow package will end naturally and kEnd will re-inject.
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
        auto* basePkg = RE::TESForm::LookupByID<RE::TESPackage>(packageID);
        if (actor && basePkg && !Utils::IsPlayerFollower(actor)) {
            ReassertBasePackage(actor, basePkg);
        }

        MARAS_LOG_INFO("PackageOverrideService: registered actor {:08X} → base package {:08X}", actorID, packageID);
    }

    void PackageOverrideService::UnregisterActor(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) return;
        {
            std::unique_lock lock(m_mutex);
            if (!m_registry.erase(actorID)) return;
        }

        InvalidateHookEvalActor(actorID);

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
        if (actor) {
            actor->EvaluatePackage(false, true);
        }

        MARAS_LOG_INFO("PackageOverrideService: unregistered actor {:08X}", actorID);
    }

    bool PackageOverrideService::IsRegistered(RE::FormID actorID) const {
        std::shared_lock lock(m_mutex);
        return m_registry.find(actorID) != m_registry.end();
    }

    RE::FormID PackageOverrideService::GetBasePackageForActor(RE::FormID actorID) const {
        std::shared_lock lock(m_mutex);
        auto it = m_registry.find(actorID);
        if (it == m_registry.end()) {
            return 0;
        }
        return (it->second != 0) ? it->second : m_baseSandboxPkgID;
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

            if (!actor || actor->IsPlayerRef()) return candidate;

            // Highest priority: children near an active OStim/SexLab scene flee (applies to any
            // child, MARAS-registered or not). Cheap atomic gate inside IsFleeing.
            if (ChildrenFleeService::GetSingleton().IsFleeing(actor->GetFormID())) {
                if (auto* fleePkg = FormCache::GetSingleton().GetChildrenFleeIntimateScenePackage()) {
                    return fleePkg;
                }
            }

            auto& svc = PackageOverrideService::GetSingleton();
            const RE::FormID actorID = actor->GetFormID();

            RE::FormID sandboxID = 0;
            int questThreshold = -1;
            {
                std::shared_lock lock(svc.m_mutex);
                auto it = svc.m_registry.find(actorID);
                if (it == svc.m_registry.end()) return candidate;
                sandboxID = (it->second != 0) ? it->second : svc.m_baseSandboxPkgID;
                questThreshold = svc.m_questPriorityThreshold;
            }

            const RE::FormID candidateID = candidate ? candidate->GetFormID() : 0;
            const char* rawEditorID = candidate ? candidate->GetFormEditorID() : nullptr;
            const char* editorID = (rawEditorID && rawEditorID[0] != '\0') ? rawEditorID : nullptr;
            const std::string candidateName = GetPackageDesc(candidate);

            // If the actor is currently a follower, skip sandbox override entirely.
            if (Utils::IsPlayerFollower(actor)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::Follower, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} is a follower, skipping sandbox override",
                                    actorID);
                });
                return candidate;
            }

            if (!sandboxID) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::NoSandbox, [&]() {
                    MARAS_LOG_WARN("PackageStartHook: sandboxID is 0 for registered actor {:08X}, passing through",
                                   actorID);
                });
                return candidate;
            }
            if (!candidate) {
                // null means no override — game would fall back to the actor's vanilla package stack.
                // For registered tenants this means our sandbox lost; re-assert it.
                auto* sandboxPkg = RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
                const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
                CheckAndLog(actorID, candidateID, resultID, PackageDecision::CandidateNullAssertSandbox, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='null' (00000000)", actorID);
                    MARAS_LOG_DEBUG("PackageStartHook: candidate is null for registered actor {:08X}, asserting sandbox({:08X})",
                                    actorID, sandboxID);
                });
                return sandboxPkg;
            }
            if (candidateID == sandboxID) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::AlreadyRunningSandbox, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} already running sandbox {:08X}, no override needed",
                                    actorID, sandboxID);
                });
                return candidate;
            }

            // Whitelist takes priority: explicitly permitted packages run freely.
            if (svc.MatchesWhitelist(editorID)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::Whitelist, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: allowing whitelisted '{}' ({:08X}) on actor {:08X}",
                                    candidateName, candidateID, actorID);
                });
                return candidate;
            }

            // Plugin whitelist: allow any package whose originating plugin is permitted.
            if (svc.MatchesPluginWhitelist(candidate)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::PluginWhitelist, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: allowing plugin-whitelisted '{}' ({:08X}) on actor {:08X}",
                                    candidateName, candidateID, actorID);
                });
                return candidate;
            }

            // Quest whitelist: allow any package whose owning quest matches quest whitelist.
            auto* quest = GetQuestForPackage(pthis, candidate);
            const char* rawQuestEdid = quest ? quest->GetFormEditorID() : nullptr;
            const char* questEdid = (rawQuestEdid && rawQuestEdid[0] != '\0') ? rawQuestEdid : nullptr;
            if (quest && svc.MatchesQuestWhitelist(questEdid)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::QuestWhitelist, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: allowing '{}' ({:08X}) on actor {:08X} (quest whitelist '{}')",
                                    candidateName, candidateID, actorID,
                                    GetQuestDesc(quest));
                });
                return candidate;
            }

            // Quest priority threshold: for alias-driven packages, compare the owning quest's
            // priority against the configured threshold.
            //   quest priority >= threshold  →  allow candidate  (threshold is the lower bound)
            //   quest priority <  threshold  →  redirect to sandbox
            //   no quest association         →  fall through to sandbox (not alias-driven)
            if (questThreshold >= 0 && quest) {
                const int questPriority = static_cast<int>(quest->data.priority);
                if (questPriority >= questThreshold) {
                    CheckAndLog(actorID, candidateID, candidateID, PackageDecision::QuestPriorityAllowed, [&]() {
                        MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                        actorID, candidateName, candidateID);
                        MARAS_LOG_DEBUG("PackageStartHook: allowing '{}' ({:08X}) on actor {:08X}, quest priority {} >= threshold {}",
                                        candidateName, candidateID, actorID,
                                        questPriority, questThreshold);
                    });
                    return candidate;
                } else {
                    auto* sandboxPkg = RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
                    const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
                    CheckAndLog(actorID, candidateID, resultID, PackageDecision::QuestPrioritySuppressed, [&]() {
                        MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                        actorID, candidateName, candidateID);
                        MARAS_LOG_DEBUG("PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, quest priority {} < threshold {}",
                                        candidateName, candidateID, actorID,
                                        questPriority, questThreshold);
                    });
                    return sandboxPkg;
                }
            }

            auto* sandboxPkg = RE::TESForm::LookupByID<RE::TESPackage>(sandboxID);
            const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
            CheckAndLog(actorID, candidateID, resultID, PackageDecision::SuppressedRedirectSandbox, [&]() {
                MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                actorID, candidateName, candidateID);
                MARAS_LOG_DEBUG("PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, redirecting to sandbox",
                                candidateName, candidateID, actorID);
            });
            return sandboxPkg;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> target{RELOCATION_ID(36404, 37398),
                                                   REL::VariantOffset(0x47, 0x47, 0x47)};
            auto& trampoline = SKSE::GetTrampoline();
            if (trampoline.empty()) {
                SKSE::AllocTrampoline(14);
            }
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
        RE::FormID petPkgID = 0;
        RE::FormID childFollowPkgID = 0;
        {
            std::shared_lock lock(m_mutex);
            packageFormID = m_baseSandboxPkgID;
            petPkgID = m_petCritterSandboxPkgID;
            childFollowPkgID = m_nonPlayerChildFollowPkgID;
        }

        if (!packageFormID) {
            if (auto* pkg = FormCache::GetSingleton().GetHomeSandboxPackage()) {
                packageFormID = pkg->GetFormID();
            }
        }
        if (!packageFormID) {
            MARAS_LOG_WARN("PackageOverrideService::RebuildFromTenants: base package not set, skipping");
            return;
        }

        std::unordered_map<RE::FormID, RE::FormID> newRegistry;

        // Collect all tenant FormIDs from every registered player house
        const auto houseIDs = PlayerHouseService::GetSingleton().GetAllPlayerHouses();
        for (auto houseID : houseIDs) {
            const auto tenants = PlayerHouseService::GetSingleton().GetPlayerHouseTenants(houseID);
            for (auto tenantID : tenants) {
                if (tenantID == 0 || IsSyntheticChildID(tenantID)) continue;
                if (PlayerHouseService::GetSingleton().GetTenantStayAtHome(tenantID)) {
                    newRegistry[tenantID] = 0;  // defaults to m_baseSandboxPkgID (0x6A)
                }
            }
        }

        // Also collect all pet and critter FormIDs assigned to children
        if (!petPkgID) {
            if (auto* petPkg = FormCache::GetSingleton().GetPetCritterHomeSandboxPackage()) {
                petPkgID = petPkg->GetFormID();
            }
        }
        if (petPkgID != 0) {
            const auto petCritterIDs = PlayerChildrenService::GetSingleton().GetAllPetAndCritterIDs();
            for (auto petCritterID : petCritterIDs) {
                if (petCritterID == 0 || IsSyntheticChildID(petCritterID)) continue;
                newRegistry[petCritterID] = petPkgID;
            }
        }

        // Also collect all non-player 0xFF Stage 2 children
        if (!childFollowPkgID) {
            if (auto* childPkg = FormCache::GetSingleton().GetChildrenNonPlayerFollowParentPackage()) {
                childFollowPkgID = childPkg->GetFormID();
            }
        }
        if (childFollowPkgID != 0) {
            const auto& allChildren = NPCRelationshipManager::GetSingleton().GetAllChildren();
            for (const auto& [childID, entry] : allChildren) {
                if (!IsSyntheticChildID(childID) && (childID >> 24) == 0xFF && entry.stage == ChildStage::Child) {
                    if (entry.guardianAID != kPlayerFormID && entry.guardianBID != kPlayerFormID) {
                        newRegistry[childID] = childFollowPkgID;
                        // Proactively re-assert 0x377 linked reference on save load
                        RE::FormID targetRefID = NPCRelationshipManager::GetSingleton().ResolveNonPlayerChildTarget(entry);
                        NPCRelationshipManager::GetSingleton().SetChildSandboxAroundParentLinkedRef(childID, targetRefID);
                    }
                }
            }
        }

        // Atomically replace the registry while keeping `newRegistry` as a local snapshot for safe iteration
        {
            std::unique_lock lock(m_mutex);
            m_baseSandboxPkgID = packageFormID;
            if (petPkgID != 0) m_petCritterSandboxPkgID = petPkgID;
            if (childFollowPkgID != 0) m_nonPlayerChildFollowPkgID = childFollowPkgID;
            m_registry = newRegistry;
        }

        MARAS_LOG_INFO("PackageOverrideService: rebuilt registry with {} actor(s) (tenants + pets/critters)",
                       newRegistry.size());
        for (const auto& [id, customPkgID] : newRegistry) {
            MARAS_LOG_INFO("  registry actor {:08X} ({}) -> base package {:08X}", id, ActorSvc::GetName(id),
                           customPkgID != 0 ? customPkgID : packageFormID);
        }

        // Inject the package on any already-loaded actors (skip player teammates)
        for (const auto& [actorID, customPkgID] : newRegistry) {
            if (actorID == 0 || IsSyntheticChildID(actorID)) continue;
            RE::FormID effPkgID = (customPkgID != 0) ? customPkgID : packageFormID;
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                if (!Utils::IsPlayerFollower(actor)) {
                    if (auto* pkg = RE::TESForm::LookupByID<RE::TESPackage>(effPkgID)) {
                        ReassertBasePackage(actor, pkg);
                    }
                }
            }
        }
    }

    void PackageOverrideService::Revert() {
        {
            std::unique_lock lock(m_mutex);
            m_registry.clear();
        }
        ClearHookEvalCache();
        MARAS_LOG_INFO("PackageOverrideService: reverted");
    }

}  // namespace MARAS
