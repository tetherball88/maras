#include "maras/engine/PackageReconciler.h"

#include "RE/B/BGSScene.h"
#include "RE/B/BGSSceneAction.h"
#include "RE/B/BGSSceneActionPackage.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/ChildrenFleeService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace MARAS {

    PackageReconciler& PackageReconciler::GetSingleton() {
        static PackageReconciler instance;
        return instance;
    }

    namespace {

        // Returns the quest that owns `pkg` via an actor alias under a read lock on ExtraAliasInstanceArray.
        RE::TESQuest* GetQuestForPackage(RE::ExtraDataList* extraList, RE::TESPackage* pkg) {
            if (!extraList || !pkg) {
                return nullptr;
            }
            auto* aliasExtra = extraList->GetByType<RE::ExtraAliasInstanceArray>();
            if (!aliasExtra) {
                return nullptr;
            }
            RE::BSReadLockGuard aliasGuard(aliasExtra->lock);
            for (const auto* aliasData : aliasExtra->aliases) {
                if (!aliasData || !aliasData->quest || !aliasData->instancedPackages) {
                    continue;
                }
                for (const auto* aliasPkg : *aliasData->instancedPackages) {
                    if (aliasPkg == pkg) {
                        return aliasData->quest;
                    }
                }
            }
            return nullptr;
        }

        std::string GetPackageDesc(RE::TESPackage* pkg) {
            if (!pkg) {
                return "null";
            }
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
            if (!quest) {
                return "?";
            }
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
            RE::FormID candidateID{ 0 };
            RE::FormID resultID{ 0 };
            PackageDecision decision{ PackageDecision::NoSandbox };

            bool operator==(const HookEvalState& o) const noexcept {
                return candidateID == o.candidateID && resultID == o.resultID && decision == o.decision;
            }
        };

        struct HookEvalCache {
            std::mutex mutex;
            std::unordered_map<RE::FormID, HookEvalState> lastEvals;
        };

        HookEvalCache& GetHookEvalCache() {
            static HookEvalCache instance;
            return instance;
        }

        void InvalidateHookEvalActor(RE::FormID actorID) {
            auto& cache = GetHookEvalCache();
            std::lock_guard lock(cache.mutex);
            cache.lastEvals.erase(actorID);
        }

        void ClearHookEvalCache() {
            auto& cache = GetHookEvalCache();
            std::lock_guard lock(cache.mutex);
            cache.lastEvals.clear();
        }

        template <typename LogFn>
        void CheckAndLog(RE::FormID actorID, RE::FormID candidateID, RE::FormID resultID,
                         PackageDecision decision, LogFn&& logFn) {
            if (decision != PackageDecision::NoSandbox &&
                State::WorldStore::GetSingleton().Snapshot()->settings->logLevel > 1) {
                return;
            }
            const HookEvalState newState{ candidateID, resultID, decision };
            {
                auto& cache = GetHookEvalCache();
                std::lock_guard lock(cache.mutex);
                auto it = cache.lastEvals.find(actorID);
                if (it != cache.lastEvals.end() && it->second == newState) {
                    return;
                }
                cache.lastEvals[actorID] = newState;
            }
            logFn();
        }

        bool ConvergeActorLinkedRef(RE::Actor* actor, RE::BGSKeyword* keyword, RE::FormID desiredTargetId) {
            if (!actor || !keyword) {
                return false;
            }
            RE::TESObjectREFR* desiredRef = nullptr;
            if (desiredTargetId != 0 && HasPhysicalActor(desiredTargetId)) {
                desiredRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(desiredTargetId);
                if (!desiredRef) {
                    return false;
                }
            }
            auto* existingRef = actor->extraList.GetLinkedRef(keyword);
            if (existingRef == desiredRef) {
                return false;
            }
            actor->extraList.SetLinkedRef(desiredRef, keyword);
            return true;
        }

        void ResolveTenantHomeMarkerOnMainThread(
            RE::Actor* actor,
            RE::FormID actorId,
            const State::WorldStateSnapshot& worldSnap,
            State::ActorPackagePlan& plan) {
            if (!plan.isTenant || !actor) {
                return;
            }
            RE::TESObjectREFR* createdMarker = nullptr;
            if (plan.needsAdultHomeMarker) {
                createdMarker = HouseholdEngineOps::GetSingleton().GetOrCreateHomeMarker(actor);
            } else if (plan.homeSandboxTargetId.IsNull()) {
                createdMarker = PlayerChildrenEngineOps::GetSingleton().GetOrCreateChildHomeMarker(actor);
            } else {
                return;
            }
            if (!createdMarker) {
                return;
            }
            if (const auto houseId = worldSnap.household->GetTenantHouse(actorId); !houseId.IsNull()) {
                if (auto* houseMarker = HouseholdEngineOps::GetSingleton().GetHouseMarkerRef(houseId.raw())) {
                    createdMarker->MoveTo(houseMarker);
                }
            }
            plan.homeSandboxTargetId = Foundation::RefId{ createdMarker->GetFormID() };
        }

        struct ResolvedStandardPackages {
            RE::TESPackage* base{ nullptr };
            RE::TESPackage* pet{ nullptr };
            RE::TESPackage* childFollow{ nullptr };
            RE::TESPackage* adultTravel{ nullptr };
            RE::FormID baseId{ 0 };
            RE::FormID petId{ 0 };
            RE::FormID childFollowId{ 0 };
            RE::FormID adultTravelId{ 0 };
        };

        ResolvedStandardPackages ResolveStandardPackages() noexcept {
            auto& fc = FormCache::GetSingleton();
            ResolvedStandardPackages out{};
            out.base = fc.GetHomeSandboxPackage();
            out.pet = fc.GetPetCritterHomeSandboxPackage();
            out.childFollow = fc.GetChildrenNonPlayerFollowParentPackage();
            out.adultTravel = fc.GetAdultChildTravelSandboxPackage();
            out.baseId = out.base ? out.base->GetFormID() : 0;
            out.petId = out.pet ? out.pet->GetFormID() : 0;
            out.childFollowId = out.childFollow ? out.childFollow->GetFormID() : 0;
            out.adultTravelId = out.adultTravel ? out.adultTravel->GetFormID() : 0;
            return out;
        }

        void ApplyStandardPackagesToDraft(PackageReconciler::PackageRegistrySnapshot& draft,
                                          const ResolvedStandardPackages& resolved) {
            if (draft.baseSandboxPkgID == 0) draft.baseSandboxPkgID = resolved.baseId;
            if (draft.petCritterSandboxPkgID == 0) draft.petCritterSandboxPkgID = resolved.petId;
            if (draft.nonPlayerChildFollowPkgID == 0) draft.nonPlayerChildFollowPkgID = resolved.childFollowId;
            if (draft.adultChildTravelPkgID == 0) draft.adultChildTravelPkgID = resolved.adultTravelId;
            if (resolved.base) draft.resolvedPackages[resolved.baseId] = resolved.base;
            if (resolved.pet) draft.resolvedPackages[resolved.petId] = resolved.pet;
            if (resolved.childFollow) draft.resolvedPackages[resolved.childFollowId] = resolved.childFollow;
            if (resolved.adultTravel) draft.resolvedPackages[resolved.adultTravelId] = resolved.adultTravel;
        }

    }  // namespace

    bool PackageReconciler::MatchesPatternList(std::string_view text, const std::vector<std::string>& patterns) {
        for (const auto& pattern : patterns) {
            if (Foundation::StringUtils::WildcardMatch(text, pattern, true)) {
                return true;
            }
        }
        return false;
    }

    bool PackageReconciler::ShouldSkipForFollower(
        RE::Actor* actor,
        RE::FormID sandboxPkgID,
        RE::FormID childFollowPkgID) {
        if (!actor || actor->IsDeleted()) {
            return false;
        }
        // Sole-player unhoused child package 0x376 targets the Player via 0x377 LinkedRef.
        // Using Utils::IsPlayerFollower(actor) inspects CheckPackageTargetsPlayer and falsely treats
        // the 0x376 package as a follower package, causing a per-frame flip-flop. Check explicit
        // teammate/follower faction flags only for 0x376.
        if (sandboxPkgID != 0 && sandboxPkgID == childFollowPkgID) {
            if (actor->IsPlayerTeammate()) {
                return true;
            }
            if (auto* cff = FormCache::GetSingleton().GetCurrentFollowerFaction();
                cff && actor->IsInFaction(cff)) {
                return true;
            }
            return false;
        }
        return Utils::IsPlayerFollower(actor);
    }

    void PackageReconciler::EnsureCachedPackageIds(PackageRegistrySnapshot& draft) const noexcept {
        ApplyStandardPackagesToDraft(draft, ResolveStandardPackages());
    }

    RE::FormID PackageReconciler::ResolveRolePackageId(
        State::PackageRole role,
        const PackageRegistrySnapshot& snap) const noexcept {
        switch (role) {
            case State::PackageRole::HousedTenant:
                return snap.baseSandboxPkgID;
            case State::PackageRole::PetOrCritter:
                return snap.petCritterSandboxPkgID;
            case State::PackageRole::FollowParentChild:
                return snap.nonPlayerChildFollowPkgID;
            case State::PackageRole::AdultChildTravel:
                return snap.adultChildTravelPkgID;
            case State::PackageRole::None:
            default:
                return 0;
        }
    }

    void PackageReconciler::LoadConfig(std::string_view iniPath) {
        std::vector<std::string> whitelistPatterns;
        std::vector<std::string> pluginWhitelistPatterns;
        std::vector<std::string> questWhitelistPatterns;
        int questPriorityThreshold = -1;

        std::ifstream file{ std::string{ iniPath } };
        const bool fileOpened = file.is_open();
        if (fileOpened) {
            auto AppendPatterns = [](const std::string& value, std::vector<std::string>& target) {
                std::istringstream ss(value);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    auto pattern = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(token));
                    if (!pattern.empty()) {
                        target.push_back(std::move(pattern));
                    }
                }
            };

            bool inSection = false;
            std::string line;
            while (std::getline(file, line)) {
                const auto trimmed = Foundation::StringUtils::TrimView(line);
                if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                    continue;
                }
                if (trimmed[0] == '[') {
                    const auto end = trimmed.find(']');
                    if (end != std::string_view::npos) {
                        const auto sectionName =
                            Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(trimmed.substr(1, end - 1)));
                        inSection = (sectionName == "packageoverrides");
                    }
                    continue;
                }
                if (!inSection) {
                    continue;
                }

                const auto eqPos = trimmed.find('=');
                if (eqPos == std::string_view::npos) {
                    continue;
                }

                const auto key = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(trimmed.substr(0, eqPos)));
                const auto value = Foundation::StringUtils::TrimStd(trimmed.substr(eqPos + 1));

                if (key == "whitelist") {
                    AppendPatterns(value, whitelistPatterns);
                } else if (key == "allowedplugins") {
                    AppendPatterns(value, pluginWhitelistPatterns);
                } else if (key == "questwhitelist") {
                    AppendPatterns(value, questWhitelistPatterns);
                } else if (key == "questpriority") {
                    try {
                        questPriorityThreshold = std::stoi(value);
                    } catch (...) {
                        MARAS_LOG_WARN("PackageReconciler: invalid QuestPriority value '{}', ignoring", value);
                    }
                }
            }
        } else {
            MARAS_LOG_INFO("PackageReconciler: config not found at '{}' (no patterns loaded)", iniPath);
        }

        // Resolve package FormIDs and pointers outside writeMutex_
        const auto stdPkgs = ResolveStandardPackages();

        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            auto next = std::make_shared<PackageRegistrySnapshot>(*current);
            next->whitelistPatterns = whitelistPatterns;
            next->pluginWhitelistPatterns = pluginWhitelistPatterns;
            next->questWhitelistPatterns = questWhitelistPatterns;
            next->questPriorityThreshold = questPriorityThreshold;
            ApplyStandardPackagesToDraft(*next, stdPkgs);
            snapshot_.store(std::move(next), std::memory_order_release);
        }

        if (fileOpened) {
            MARAS_LOG_INFO(
                "PackageReconciler: loaded {} whitelist pattern(s), {} allowed plugin(s), {} quest whitelist pattern(s), quest priority threshold {}",
                whitelistPatterns.size(), pluginWhitelistPatterns.size(), questWhitelistPatterns.size(),
                questPriorityThreshold >= 0 ? std::to_string(questPriorityThreshold) : "disabled");
        }
        if (stdPkgs.baseId != 0) {
            MARAS_LOG_INFO("PackageReconciler: base sandbox package resolved to {:08X}", stdPkgs.baseId);
        }
        if (stdPkgs.petId != 0) {
            MARAS_LOG_INFO("PackageReconciler: pet/critter sandbox package resolved to {:08X}", stdPkgs.petId);
        }
        if (stdPkgs.childFollowId != 0) {
            MARAS_LOG_INFO("PackageReconciler: non-player child follow package resolved to {:08X}", stdPkgs.childFollowId);
        }
        if (stdPkgs.adultTravelId != 0) {
            MARAS_LOG_INFO("PackageReconciler: adult child travel package resolved to {:08X}", stdPkgs.adultTravelId);
        }
    }

    void PackageReconciler::ReassertBasePackage(RE::Actor* actor, RE::TESPackage* basePkg) {
        if (!actor || !basePkg) {
            return;
        }

        auto* process = actor->GetActorRuntimeData().currentProcess;
        if (!process) {
            MARAS_LOG_DEBUG("PackageReconciler: actor {:08X} has no AI process, skipping reassert",
                            actor->GetFormID());
            return;
        }

        process->SetRunOncePackage(basePkg, actor);
        actor->EvaluatePackage(false, false);

        MARAS_LOG_DEBUG("PackageReconciler: reasserted base package {:08X} on actor {:08X}",
                        basePkg->GetFormID(), actor->GetFormID());
    }

    void PackageReconciler::ReconcileLoadedActor(RE::Actor* actor) {
        if (!actor || actor->IsDeleted() || actor->IsPlayerRef()) {
            return;
        }

        const RE::FormID actorId = actor->GetFormID();
        if (actorId == 0 || IsSyntheticChildID(actorId)) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        Foundation::RefId resolvedChildFollowTargetId{};
        if (const auto* childRec = worldSnap->lineage->FindChild(State::ChildId{ actorId });
            childRec && childRec->stage == State::ChildStage::Child) {
            resolvedChildFollowTargetId = Foundation::RefId{
                ChildSpawnService::GetSingleton().ResolveNonPlayerChildTarget(*childRec) };
        }

        auto plan = State::ComputeActorPackagePlan(*worldSnap, Foundation::RefId{ actorId }, resolvedChildFollowTargetId);

        // If tenant (adult or housed child) lacks a home marker yet, create and place one cleanly on the main thread
        ResolveTenantHomeMarkerOnMainThread(actor, actorId, *worldSnap, plan);

        auto& fc = FormCache::GetSingleton();
        auto* homeKw = fc.GetHomeSandboxKeyword();
        auto* followKw = fc.GetChildSandboxAroundParentKeyword();

        const bool linkedHomeChanged =
            ConvergeActorLinkedRef(actor, homeKw, plan.homeSandboxTargetId.raw());
        const bool linkedFollowChanged =
            ConvergeActorLinkedRef(actor, followKw, plan.childFollowTargetId.raw());

        // Resolve package IDs and pointers cleanly outside writeMutex_
        const auto stdPkgs = ResolveStandardPackages();

        RE::FormID desiredPkgId = 0;
        RE::FormID childFollowPkgId = 0;
        bool wasRegistered = false;
        bool registryChanged = false;

        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            auto next = std::make_shared<PackageRegistrySnapshot>(*current);
            ApplyStandardPackagesToDraft(*next, stdPkgs);

            childFollowPkgId = next->nonPlayerChildFollowPkgID;
            desiredPkgId = ResolveRolePackageId(plan.role, *next);

            const auto existingIt = next->registry.find(actorId);
            wasRegistered = (existingIt != next->registry.end());
            const RE::FormID existingPkgId = wasRegistered
                ? ((existingIt->second != 0) ? existingIt->second : next->baseSandboxPkgID)
                : 0;

            if (plan.role == State::PackageRole::None || desiredPkgId == 0) {
                if (wasRegistered) {
                    next->registry.erase(actorId);
                    registryChanged = true;
                    snapshot_.store(std::move(next), std::memory_order_release);
                }
            } else {
                const RE::FormID storedValue =
                    (plan.role == State::PackageRole::HousedTenant) ? RE::FormID{ 0 } : desiredPkgId;
                if (!wasRegistered || existingPkgId != desiredPkgId || existingIt->second != storedValue) {
                    next->registry[actorId] = storedValue;
                    registryChanged = true;
                    snapshot_.store(std::move(next), std::memory_order_release);
                }
            }
        }

        if (registryChanged) {
            InvalidateHookEvalActor(actorId);
        }

        if (plan.role == State::PackageRole::None || desiredPkgId == 0) {
            if (wasRegistered || linkedHomeChanged || linkedFollowChanged) {
                actor->EvaluatePackage(false, true);
            }
            return;
        }

        if (ShouldSkipForFollower(actor, desiredPkgId, childFollowPkgId)) {
            return;
        }

        auto* desiredPkg = RE::TESForm::LookupByID<RE::TESPackage>(desiredPkgId);
        if (!desiredPkg) {
            return;
        }

        const bool alreadyRunning = (actor->GetCurrentPackage() == desiredPkg);
        if (registryChanged || linkedHomeChanged || linkedFollowChanged || !alreadyRunning) {
            ReassertBasePackage(actor, desiredPkg);
        }
    }

    void PackageReconciler::ReconcileActor(RE::FormID actorId) {
        if (actorId == 0 || actorId == kPlayerFormID || IsSyntheticChildID(actorId)) {
            return;
        }

        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            DeferredActorResolver::GetSingleton().RequestSync(actorId, DeferredActorResolver::SyncFlags::Package);
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor) {
            DeferredActorResolver::GetSingleton().MarkUnloaded(actorId, DeferredActorResolver::SyncFlags::Package);
            return;
        }
        if (actor->IsDeleted()) {
            return;
        }

        ReconcileLoadedActor(actor);
    }

    void PackageReconciler::ReconcileWorld() {
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (!pendingWorldReconcile_.exchange(true, std::memory_order_acq_rel)) {
                if (!AddEpochTask(GetSessionEpoch(), "PackageReconciler::ReconcileWorld", []() {
                        std::lock_guard lock(GetPapyrusMutex());
                        PackageReconciler::GetSingleton().ReconcileWorldOnMainThread();
                    })) {
                    pendingWorldReconcile_.store(false, std::memory_order_release);
                }
            }
            return;
        }
        ReconcileWorldOnMainThread();
    }

    void PackageReconciler::ReconcileWorldOnMainThread() {
        pendingWorldReconcile_.store(false, std::memory_order_release);
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        auto& fc = FormCache::GetSingleton();
        auto* homeKw = fc.GetHomeSandboxKeyword();
        auto* followKw = fc.GetChildSandboxAroundParentKeyword();

        const auto stdPkgs = ResolveStandardPackages();

        // Collect all candidates in precedence order plus any previously registered actors
        std::unordered_set<RE::FormID> candidateIds;
        candidateIds.reserve(
            worldSnap->household->AllTenants().size() +
            worldSnap->household->AllChildStates().size() * 2 +
            worldSnap->lineage->AllChildren().size());

        for (const auto& [tenantId, tenantRec] : worldSnap->household->AllTenants()) {
            (void)tenantRec;
            if (tenantId != 0 && tenantId != kPlayerFormID && !IsSyntheticChildID(tenantId)) {
                candidateIds.insert(tenantId);
            }
        }
        for (const auto& [childId, state] : worldSnap->household->AllChildStates()) {
            (void)childId;
            if (!state.petId.IsNull() && !IsSyntheticChildID(state.petId.raw())) {
                candidateIds.insert(state.petId.raw());
            }
            if (!state.critterId.IsNull() && !IsSyntheticChildID(state.critterId.raw())) {
                candidateIds.insert(state.critterId.raw());
            }
        }
        for (const auto& [childId, childRec] : worldSnap->lineage->AllChildren()) {
            (void)childRec;
            if (!childId.IsNull() && !childId.IsSynthetic()) {
                candidateIds.insert(childId.raw());
            }
        }

        const auto oldSnapshot = snapshot_.load(std::memory_order_acquire);
        for (const auto& [prevId, prevPkg] : oldSnapshot->registry) {
            (void)prevPkg;
            if (prevId != 0 && prevId != kPlayerFormID && !IsSyntheticChildID(prevId)) {
                candidateIds.insert(prevId);
            }
        }

        auto nextSnapshot = std::make_shared<PackageRegistrySnapshot>(*oldSnapshot);
        ApplyStandardPackagesToDraft(*nextSnapshot, stdPkgs);
        nextSnapshot->registry.clear();

        if (nextSnapshot->baseSandboxPkgID == 0) {
            MARAS_LOG_WARN("PackageReconciler::ReconcileWorld: base sandbox package not resolved, skipping");
            return;
        }

        struct PendingInject {
            RE::Actor* actor{ nullptr };
            RE::FormID pkgId{ 0 };
            bool linkedChanged{ false };
        };
        std::vector<PendingInject> toInject;
        std::vector<RE::Actor*> toClearEval;

        for (const RE::FormID actorId : candidateIds) {
            Foundation::RefId resolvedChildFollowTargetId{};
            if (const auto* childRec = worldSnap->lineage->FindChild(State::ChildId{ actorId });
                childRec && childRec->stage == State::ChildStage::Child) {
                resolvedChildFollowTargetId = Foundation::RefId{
                    ChildSpawnService::GetSingleton().ResolveNonPlayerChildTarget(*childRec) };
            }

            auto plan = State::ComputeActorPackagePlan(
                *worldSnap, Foundation::RefId{ actorId }, resolvedChildFollowTargetId);

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor) {
                if (plan.role != State::PackageRole::None) {
                    const RE::FormID targetPkgId = ResolveRolePackageId(plan.role, *nextSnapshot);
                    if (targetPkgId != 0) {
                        nextSnapshot->registry[actorId] =
                            (plan.role == State::PackageRole::HousedTenant) ? RE::FormID{ 0 } : targetPkgId;
                    }
                    DeferredActorResolver::GetSingleton().MarkUnloaded(
                        actorId, DeferredActorResolver::SyncFlags::Package);
                }
                continue;
            }
            if (actor->IsDeleted()) {
                continue;
            }

            ResolveTenantHomeMarkerOnMainThread(actor, actorId, *worldSnap, plan);

            const bool linkedHomeChanged =
                ConvergeActorLinkedRef(actor, homeKw, plan.homeSandboxTargetId.raw());
            const bool linkedFollowChanged =
                ConvergeActorLinkedRef(actor, followKw, plan.childFollowTargetId.raw());

            const RE::FormID desiredPkgId = ResolveRolePackageId(plan.role, *nextSnapshot);
            if (plan.role != State::PackageRole::None && desiredPkgId != 0) {
                nextSnapshot->registry[actorId] =
                    (plan.role == State::PackageRole::HousedTenant) ? RE::FormID{ 0 } : desiredPkgId;
                toInject.push_back({ actor, desiredPkgId, linkedHomeChanged || linkedFollowChanged });
            } else if (oldSnapshot->registry.contains(actorId) || linkedHomeChanged || linkedFollowChanged) {
                toClearEval.push_back(actor);
            }
        }

        const RE::FormID childFollowPkgId = nextSnapshot->nonPlayerChildFollowPkgID;
        const std::size_t totalRegistered = nextSnapshot->registry.size();

        {
            std::lock_guard lock(writeMutex_);
            // Preserve any updated INI patterns if LoadConfig raced during load
            const auto latest = snapshot_.load(std::memory_order_acquire);
            nextSnapshot->whitelistPatterns = latest->whitelistPatterns;
            nextSnapshot->pluginWhitelistPatterns = latest->pluginWhitelistPatterns;
            nextSnapshot->questWhitelistPatterns = latest->questWhitelistPatterns;
            nextSnapshot->questPriorityThreshold = latest->questPriorityThreshold;
            for (const auto& [pkgId, pkgPtr] : latest->resolvedPackages) {
                nextSnapshot->resolvedPackages.try_emplace(pkgId, pkgPtr);
            }
            snapshot_.store(std::move(nextSnapshot), std::memory_order_release);
        }

        ClearHookEvalCache();

        for (const auto& item : toInject) {
            if (ShouldSkipForFollower(item.actor, item.pkgId, childFollowPkgId)) {
                continue;
            }
            if (auto* pkg = RE::TESForm::LookupByID<RE::TESPackage>(item.pkgId)) {
                if (item.linkedChanged || item.actor->GetCurrentPackage() != pkg) {
                    ReassertBasePackage(item.actor, pkg);
                }
            }
        }
        for (auto* actor : toClearEval) {
            actor->EvaluatePackage(false, true);
        }

        MARAS_LOG_INFO("PackageReconciler::ReconcileWorld converged {} active package-managed actor(s)",
                       totalRegistered);
    }

    void PackageReconciler::SetBaseSandboxPackage(RE::FormID pkgID) {
        RE::TESPackage* pkgPtr = (pkgID != 0) ? RE::TESForm::LookupByID<RE::TESPackage>(pkgID) : nullptr;
        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            auto next = std::make_shared<PackageRegistrySnapshot>(*current);
            next->baseSandboxPkgID = pkgID;
            if (pkgPtr) {
                next->resolvedPackages[pkgID] = pkgPtr;
            }
            snapshot_.store(std::move(next), std::memory_order_release);
        }
        MARAS_LOG_INFO("PackageReconciler: base sandbox package set to {:08X}", pkgID);
        ReconcileWorld();
    }

    void PackageReconciler::RegisterActor(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return;
        }
        RegisterActorWithPackage(actorID, 0);
    }

    void PackageReconciler::RegisterPetCritter(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return;
        }
        auto snap = snapshot_.load(std::memory_order_acquire);
        RE::FormID packageID = snap->petCritterSandboxPkgID;
        if (packageID == 0) {
            if (auto* petPkg = FormCache::GetSingleton().GetPetCritterHomeSandboxPackage()) {
                packageID = petPkg->GetFormID();
            }
        }
        if (packageID == 0) {
            MARAS_LOG_WARN(
                "PackageReconciler::RegisterPetCritter: pet/critter sandbox package (0xC0) not resolved, skipping {:08X}",
                actorID);
            return;
        }
        RegisterActorWithPackage(actorID, packageID);
    }

    void PackageReconciler::RegisterNonPlayerChild(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return;
        }
        auto snap = snapshot_.load(std::memory_order_acquire);
        RE::FormID packageID = snap->nonPlayerChildFollowPkgID;
        if (packageID == 0) {
            if (auto* childPkg = FormCache::GetSingleton().GetChildrenNonPlayerFollowParentPackage()) {
                packageID = childPkg->GetFormID();
            }
        }
        if (packageID == 0) {
            MARAS_LOG_WARN(
                "PackageReconciler::RegisterNonPlayerChild: non-player child follow package (0x376) not resolved, skipping {:08X}",
                actorID);
            return;
        }
        RegisterActorWithPackage(actorID, packageID);
    }

    void PackageReconciler::RegisterActorWithPackage(RE::FormID actorID, RE::FormID targetPackageID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return;
        }

        const auto stdPkgs = ResolveStandardPackages();
        RE::TESPackage* targetPkgPtr =
            (targetPackageID != 0) ? RE::TESForm::LookupByID<RE::TESPackage>(targetPackageID) : nullptr;

        RE::FormID effectivePkgID = 0;
        RE::FormID childFollowPkgID = 0;
        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            auto next = std::make_shared<PackageRegistrySnapshot>(*current);
            ApplyStandardPackagesToDraft(*next, stdPkgs);
            if (targetPkgPtr) {
                next->resolvedPackages[targetPackageID] = targetPkgPtr;
            }

            next->registry[actorID] = targetPackageID;
            effectivePkgID = (targetPackageID != 0) ? targetPackageID : next->baseSandboxPkgID;
            childFollowPkgID = next->nonPlayerChildFollowPkgID;
            snapshot_.store(std::move(next), std::memory_order_release);
        }

        if (effectivePkgID == 0) {
            MARAS_LOG_WARN("PackageReconciler::RegisterActorWithPackage: base package not set yet, deferring {:08X}",
                           actorID);
            return;
        }

        InvalidateHookEvalActor(actorID);

        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID); actor && !actor->IsDeleted()) {
                if (auto* basePkg = RE::TESForm::LookupByID<RE::TESPackage>(effectivePkgID);
                    basePkg && !ShouldSkipForFollower(actor, effectivePkgID, childFollowPkgID)) {
                    ReassertBasePackage(actor, basePkg);
                }
            }
        } else {
            AddCurrentEpochTask("PackageReconciler::RegisterActorWithPackage", [actorID, effectivePkgID, childFollowPkgID]() {
                std::lock_guard taskLock(GetPapyrusMutex());
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID); actor && !actor->IsDeleted()) {
                    if (auto* basePkg = RE::TESForm::LookupByID<RE::TESPackage>(effectivePkgID);
                        basePkg && !ShouldSkipForFollower(actor, effectivePkgID, childFollowPkgID)) {
                        PackageReconciler::GetSingleton().ReassertBasePackage(actor, basePkg);
                    }
                }
            });
        }

        MARAS_LOG_INFO("PackageReconciler: registered actor {:08X} -> base package {:08X}", actorID, effectivePkgID);
    }

    void PackageReconciler::UnregisterActor(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return;
        }

        bool erased = false;
        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            if (current->registry.contains(actorID)) {
                auto next = std::make_shared<PackageRegistrySnapshot>(*current);
                erased = (next->registry.erase(actorID) > 0);
                snapshot_.store(std::move(next), std::memory_order_release);
            }
        }
        if (!erased) {
            return;
        }

        InvalidateHookEvalActor(actorID);

        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID); actor && !actor->IsDeleted()) {
                actor->EvaluatePackage(false, true);
            }
        } else {
            AddCurrentEpochTask("PackageReconciler::UnregisterActor", [actorID]() {
                std::lock_guard taskLock(GetPapyrusMutex());
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID); actor && !actor->IsDeleted()) {
                    actor->EvaluatePackage(false, true);
                }
            });
        }

        MARAS_LOG_INFO("PackageReconciler: unregistered actor {:08X}", actorID);
    }

    bool PackageReconciler::IsRegistered(RE::FormID actorID) const {
        const auto snap = snapshot_.load(std::memory_order_acquire);
        return snap->registry.find(actorID) != snap->registry.end();
    }

    RE::FormID PackageReconciler::GetBasePackageForActor(RE::FormID actorID) const {
        const auto snap = snapshot_.load(std::memory_order_acquire);
        auto it = snap->registry.find(actorID);
        if (it == snap->registry.end()) {
            return 0;
        }
        return (it->second != 0) ? it->second : snap->baseSandboxPkgID;
    }

    void PackageReconciler::RebuildFromTenants() {
        ReconcileWorld();
    }

    void PackageReconciler::Revert() {
        pendingWorldReconcile_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(writeMutex_);
            const auto current = snapshot_.load(std::memory_order_acquire);
            auto next = std::make_shared<PackageRegistrySnapshot>(*current);
            next->registry.clear();
            next->resolvedPackages.clear();
            snapshot_.store(std::move(next), std::memory_order_release);
        }
        ClearHookEvalCache();
        MARAS_LOG_INFO("PackageReconciler: reverted");
    }

    // ─── Hook ────────────────────────────────────────────────────────────────────

    struct PackageReconciler::PackageStartHook {
        static RE::TESPackage* thunk(RE::ExtraDataList* pthis, RE::Actor* actor) {
            auto* candidate = func(pthis, actor);

            if (!actor || actor->IsPlayerRef()) {
                return candidate;
            }

            // Highest priority: children near an active OStim/SexLab scene flee
            if (ChildrenFleeService::GetSingleton().IsFleeing(actor->GetFormID())) {
                if (auto* fleePkg = FormCache::GetSingleton().GetChildrenFleeIntimateScenePackage()) {
                    return fleePkg;
                }
            }

            // Lock-free RCU snapshot load on AI frame tick
            const auto snap = PackageReconciler::GetSingleton().snapshot_.load(std::memory_order_acquire);
            const RE::FormID actorID = actor->GetFormID();
            const auto regIt = snap->registry.find(actorID);
            if (regIt == snap->registry.end()) {
                return candidate;
            }

            const RE::FormID sandboxID =
                (regIt->second != 0) ? regIt->second : snap->baseSandboxPkgID;
            const RE::FormID candidateID = candidate ? candidate->GetFormID() : 0;
            const char* rawEditorID = candidate ? candidate->GetFormEditorID() : nullptr;
            const char* editorID = (rawEditorID && rawEditorID[0] != '\0') ? rawEditorID : nullptr;

            if (ShouldSkipForFollower(actor, sandboxID, snap->nonPlayerChildFollowPkgID)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::Follower, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} is a follower, skipping sandbox override",
                                    actorID);
                });
                return candidate;
            }

            if (sandboxID == 0) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::NoSandbox, [&]() {
                    MARAS_LOG_WARN("PackageStartHook: sandboxID is 0 for registered actor {:08X}, passing through",
                                   actorID);
                });
                return candidate;
            }

            if (!candidate) {
                auto* sandboxPkg = snap->FindPackage(sandboxID);
                const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
                CheckAndLog(actorID, candidateID, resultID, PackageDecision::CandidateNullAssertSandbox, [&]() {
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='null' (00000000)", actorID);
                    MARAS_LOG_DEBUG(
                        "PackageStartHook: candidate is null for registered actor {:08X}, asserting sandbox({:08X})",
                        actorID, sandboxID);
                });
                return sandboxPkg;
            }

            if (candidateID == sandboxID) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::AlreadyRunningSandbox, [&]() {
                    const std::string candidateName = GetPackageDesc(candidate);
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: actor {:08X} already running sandbox {:08X}, no override needed",
                                    actorID, sandboxID);
                });
                return candidate;
            }

            if (editorID && !snap->whitelistPatterns.empty() &&
                MatchesPatternList(editorID, snap->whitelistPatterns)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::Whitelist, [&]() {
                    const std::string candidateName = GetPackageDesc(candidate);
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: allowing whitelisted '{}' ({:08X}) on actor {:08X}",
                                    candidateName, candidateID, actorID);
                });
                return candidate;
            }

            if (!snap->pluginWhitelistPatterns.empty()) {
                const auto* file = candidate->GetFile(0);
                if (file && file->fileName && file->fileName[0] != '\0' &&
                    MatchesPatternList(file->fileName, snap->pluginWhitelistPatterns)) {
                    CheckAndLog(actorID, candidateID, candidateID, PackageDecision::PluginWhitelist, [&]() {
                        const std::string candidateName = GetPackageDesc(candidate);
                        MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                        actorID, candidateName, candidateID);
                        MARAS_LOG_DEBUG("PackageStartHook: allowing plugin-whitelisted '{}' ({:08X}) on actor {:08X}",
                                        candidateName, candidateID, actorID);
                    });
                    return candidate;
                }
            }

            auto* quest = GetQuestForPackage(pthis, candidate);
            const char* rawQuestEdid = quest ? quest->GetFormEditorID() : nullptr;
            const char* questEdid = (rawQuestEdid && rawQuestEdid[0] != '\0') ? rawQuestEdid : nullptr;
            if (quest && questEdid && !snap->questWhitelistPatterns.empty() &&
                MatchesPatternList(questEdid, snap->questWhitelistPatterns)) {
                CheckAndLog(actorID, candidateID, candidateID, PackageDecision::QuestWhitelist, [&]() {
                    const std::string candidateName = GetPackageDesc(candidate);
                    MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                    actorID, candidateName, candidateID);
                    MARAS_LOG_DEBUG("PackageStartHook: allowing '{}' ({:08X}) on actor {:08X} (quest whitelist '{}')",
                                    candidateName, candidateID, actorID, GetQuestDesc(quest));
                });
                return candidate;
            }

            if (snap->questPriorityThreshold >= 0 && quest) {
                const int questPriority = static_cast<int>(quest->data.priority);
                if (questPriority >= snap->questPriorityThreshold) {
                    CheckAndLog(actorID, candidateID, candidateID, PackageDecision::QuestPriorityAllowed, [&]() {
                        const std::string candidateName = GetPackageDesc(candidate);
                        MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                        actorID, candidateName, candidateID);
                        MARAS_LOG_DEBUG(
                            "PackageStartHook: allowing '{}' ({:08X}) on actor {:08X}, quest priority {} >= threshold {}",
                            candidateName, candidateID, actorID, questPriority, snap->questPriorityThreshold);
                    });
                    return candidate;
                } else {
                    auto* sandboxPkg = snap->FindPackage(sandboxID);
                    const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
                    CheckAndLog(actorID, candidateID, resultID, PackageDecision::QuestPrioritySuppressed, [&]() {
                        const std::string candidateName = GetPackageDesc(candidate);
                        MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                        actorID, candidateName, candidateID);
                        MARAS_LOG_DEBUG(
                            "PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, quest priority {} < threshold {}",
                            candidateName, candidateID, actorID, questPriority, snap->questPriorityThreshold);
                    });
                    return sandboxPkg;
                }
            }

            auto* sandboxPkg = snap->FindPackage(sandboxID);
            const RE::FormID resultID = sandboxPkg ? sandboxPkg->GetFormID() : 0;
            CheckAndLog(actorID, candidateID, resultID, PackageDecision::SuppressedRedirectSandbox, [&]() {
                const std::string candidateName = GetPackageDesc(candidate);
                MARAS_LOG_DEBUG("PackageStartHook: evaluating actor {:08X}, candidate='{}' ({:08X})",
                                actorID, candidateName, candidateID);
                MARAS_LOG_DEBUG("PackageStartHook: suppressing '{}' ({:08X}) on actor {:08X}, redirecting to sandbox",
                                candidateName, candidateID, actorID);
            });
            return sandboxPkg;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(36404, 37398),
                                                    REL::VariantOffset(0x47, 0x47, 0x47) };
            auto& trampoline = SKSE::GetTrampoline();
            if (trampoline.empty()) {
                SKSE::AllocTrampoline(14);
            }
            func = trampoline.write_branch<5>(target.address(), thunk);
            MARAS_LOG_INFO("PackageReconciler: PackageStartHook installed at {:016X}", target.address());
        }
    };

    void PackageReconciler::InstallHooks() {
        PackageStartHook::Install();
    }

}  // namespace MARAS
