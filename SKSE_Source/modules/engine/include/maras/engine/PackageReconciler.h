#pragma once

#include "PCH.h"
#include "maras/state/EngineSyncPlan.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS {

    /**
     * PackageReconciler
     * -----------------
     * Lock-free RCU AI package override hook & idempotent main-thread package/LinkedRef reconciler.
     *
     * Replaces ActorUtil.AddPackageOverride for MARAS-managed actors across 5 deterministic
     * precedence tiers (`ComputeActorPackagePlan`):
     *   1. Housed tenant with stayAtHome=true -> 0x6A + 0x6B LinkedRef (home marker)
     *   2. Child pet/critter                 -> 0xC0 + 0x6B LinkedRef (owner child)
     *   3. Unhoused/0xFF Stage 2 child       -> 0x376 + 0x377 LinkedRef (parent / orphanage)
     *   4. Traveling Stage 4 adult child     -> 0xDD3 + 0x377 LinkedRef (travel marker)
     *   5. None                              -> clears 0x6B/0x377 LinkedRefs & unregisters
     *
     * Thread safety:
     * - `PackageStartHook::thunk` reads immutable `PackageRegistrySnapshot` via lock-free
     *   `std::atomic<std::shared_ptr<const PackageRegistrySnapshot>>` (`memory_order_acquire`).
     * - `GetQuestForPackage` acquires `RE::BSReadLockGuard` on `ExtraAliasInstanceArray::lock`.
     * - Writers update copy-on-write under leaf `writeMutex_` (`memory_order_release`) without
     *   calling engine/WorldStore APIs while holding `writeMutex_`.
     */
    class PackageReconciler {
    public:
        struct PackageRegistrySnapshot {
            std::unordered_map<RE::FormID, RE::FormID> registry{};
            std::unordered_map<RE::FormID, RE::TESPackage*> resolvedPackages{};
            RE::FormID baseSandboxPkgID{ 0 };            // 0x6A
            RE::FormID petCritterSandboxPkgID{ 0 };      // 0xC0
            RE::FormID nonPlayerChildFollowPkgID{ 0 };   // 0x376
            RE::FormID adultChildTravelPkgID{ 0 };       // 0xDD3
            std::vector<std::string> whitelistPatterns{};
            std::vector<std::string> pluginWhitelistPatterns{};
            std::vector<std::string> questWhitelistPatterns{};
            int questPriorityThreshold{ -1 };

            [[nodiscard]] RE::TESPackage* FindPackage(RE::FormID pkgId) const noexcept {
                if (pkgId == 0) {
                    return nullptr;
                }
                const auto it = resolvedPackages.find(pkgId);
                return (it != resolvedPackages.end()) ? it->second : nullptr;
            }
        };

        static PackageReconciler& GetSingleton();

        static void InstallHooks();

        void LoadConfig(std::string_view iniPath = "Data/SKSE/Plugins/PackageOverrides.ini");

        void SetBaseSandboxPackage(RE::FormID pkgID);

        // Converges LinkedRefs (0x6B/0x377), registry entry, and active run-once package for an actor.
        void ReconcileLoadedActor(RE::Actor* actor);
        void ReconcileActor(RE::FormID actorId);

        // Converges all world actors (tenants, pets/critters, Stage 2 children, Stage 4 travelers)
        // in one atomic RCU snapshot replacement + main-thread injection pass.
        void ReconcileWorld();

        // Explicit package override registry helpers
        void RegisterActor(RE::FormID actorID);
        void RegisterPetCritter(RE::FormID actorID);
        void RegisterNonPlayerChild(RE::FormID actorID);
        void RegisterActorWithPackage(RE::FormID actorID, RE::FormID packageID);
        void UnregisterActor(RE::FormID actorID);

        [[nodiscard]] bool IsRegistered(RE::FormID actorID) const;
        [[nodiscard]] RE::FormID GetBasePackageForActor(RE::FormID actorID) const;

        void RebuildFromTenants();
        void Revert();

    private:
        PackageReconciler() = default;
        ~PackageReconciler() = default;
        PackageReconciler(const PackageReconciler&) = delete;
        PackageReconciler& operator=(const PackageReconciler&) = delete;

        static bool MatchesPatternList(std::string_view text, const std::vector<std::string>& patterns);
        static bool ShouldSkipForFollower(RE::Actor* actor, RE::FormID sandboxPkgID, RE::FormID childFollowPkgID);

        void ReassertBasePackage(RE::Actor* actor, RE::TESPackage* basePkg);
        void ReconcileWorldOnMainThread();
        RE::FormID ResolveRolePackageId(State::PackageRole role, const PackageRegistrySnapshot& snap) const noexcept;
        void EnsureCachedPackageIds(PackageRegistrySnapshot& draft) const noexcept;

        struct PackageStartHook;

        mutable std::mutex writeMutex_{};
        std::atomic<std::shared_ptr<const PackageRegistrySnapshot>> snapshot_{
            std::make_shared<const PackageRegistrySnapshot>() };
        std::atomic<bool> pendingWorldReconcile_{ false };
    };

}  // namespace MARAS
