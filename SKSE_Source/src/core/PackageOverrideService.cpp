#include "core/PackageOverrideService.h"

#include "core/PackageReconciler.h"

namespace MARAS {

    PackageOverrideService& PackageOverrideService::GetSingleton() {
        static PackageOverrideService instance;
        return instance;
    }

    void PackageOverrideService::InstallHooks() {
        PackageReconciler::InstallHooks();
    }

    void PackageOverrideService::LoadConfig(std::string_view iniPath) {
        PackageReconciler::GetSingleton().LoadConfig(iniPath);
    }

    void PackageOverrideService::SetBaseSandboxPackage(RE::FormID pkgID) {
        PackageReconciler::GetSingleton().SetBaseSandboxPackage(pkgID);
    }

    void PackageOverrideService::RegisterActor(RE::FormID actorID) {
        PackageReconciler::GetSingleton().RegisterActor(actorID);
    }

    void PackageOverrideService::RegisterPetCritter(RE::FormID actorID) {
        PackageReconciler::GetSingleton().RegisterPetCritter(actorID);
    }

    void PackageOverrideService::RegisterNonPlayerChild(RE::FormID actorID) {
        PackageReconciler::GetSingleton().RegisterNonPlayerChild(actorID);
    }

    void PackageOverrideService::RegisterActorWithPackage(RE::FormID actorID, RE::FormID packageID) {
        PackageReconciler::GetSingleton().RegisterActorWithPackage(actorID, packageID);
    }

    void PackageOverrideService::UnregisterActor(RE::FormID actorID) {
        PackageReconciler::GetSingleton().UnregisterActor(actorID);
    }

    bool PackageOverrideService::IsRegistered(RE::FormID actorID) const {
        return PackageReconciler::GetSingleton().IsRegistered(actorID);
    }

    RE::FormID PackageOverrideService::GetBasePackageForActor(RE::FormID actorID) const {
        return PackageReconciler::GetSingleton().GetBasePackageForActor(actorID);
    }

    void PackageOverrideService::RebuildFromTenants() {
        PackageReconciler::GetSingleton().RebuildFromTenants();
    }

    void PackageOverrideService::Revert() {
        PackageReconciler::GetSingleton().Revert();
    }

}  // namespace MARAS
