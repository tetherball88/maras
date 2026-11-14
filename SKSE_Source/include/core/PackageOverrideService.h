#pragma once

#include "PCH.h"

namespace MARAS {

    // Thin backward-compatible facade forwarding to PackageReconciler.
    class PackageOverrideService {
    public:
        static PackageOverrideService& GetSingleton();

        static void InstallHooks();

        void LoadConfig(std::string_view iniPath = "Data/SKSE/Plugins/PackageOverrides.ini");

        void SetBaseSandboxPackage(RE::FormID pkgID);

        void RegisterActor(RE::FormID actorID);
        void RegisterPetCritter(RE::FormID actorID);
        void RegisterNonPlayerChild(RE::FormID actorID);
        void RegisterActorWithPackage(RE::FormID actorID, RE::FormID packageID);
        void UnregisterActor(RE::FormID actorID);

        bool IsRegistered(RE::FormID actorID) const;
        RE::FormID GetBasePackageForActor(RE::FormID actorID) const;

        void RebuildFromTenants();
        void Revert();

    private:
        PackageOverrideService() = default;
        ~PackageOverrideService() = default;
        PackageOverrideService(const PackageOverrideService&) = delete;
        PackageOverrideService& operator=(const PackageOverrideService&) = delete;
    };

}  // namespace MARAS
