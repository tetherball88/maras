#include "maras/app/ChildSkyrimNetSuccession.h"

#include "maras/app/AppContext.h"
#include "maras/app/SkyrimNetGateway.h"
#include "maras/engine/EngineLogging.h"
#include "maras/state/WorldStore.h"

#include <utility>
#include <vector>

namespace MARAS::App {

    std::uint64_t ResolveAndLinkSpawnedChildIdentity(
        RE::FormID spawnedActorId,
        Foundation::ChildStage stage,
        std::uint64_t previousUuid,
        RE::FormID previousFormId,
        std::string_view childName) {
        auto& sn = GetSkyrimNetGateway();
        if (!sn.IsAvailable() || spawnedActorId == 0 || IsSyntheticChildID(spawnedActorId) ||
            (stage != Foundation::ChildStage::Child && stage != Foundation::ChildStage::Adult)) {
            return previousUuid;
        }

        if (previousUuid == 0 && previousFormId != 0 && !IsSyntheticChildID(previousFormId) &&
            previousFormId != spawnedActorId) {
            previousUuid = sn.ResolvePlacedActorUUID(previousFormId);
        }

        const std::uint64_t newUuid = sn.ResolvePlacedActorUUID(spawnedActorId);
        if (newUuid == 0) {
            return previousUuid;
        }

        const std::string_view safeName = childName.empty() ? std::string_view{ "Child" } : childName;
        if (previousUuid != 0 && previousUuid != newUuid) {
            if (sn.IsActorSuccessionAvailable() && sn.RecordActorSuccessionByUUID(previousUuid, newUuid)) {
                MARAS_LOG_INFO(
                    "[Children] Linked SkyrimNet actor succession for '{}' ({:08X}, stage={}): {:016X} -> {:016X}",
                    safeName, spawnedActorId, static_cast<int>(stage), previousUuid, newUuid);
                return newUuid;
            }
            MARAS_LOG_WARN(
                "[Children] Could not link SkyrimNet actor succession yet for '{}' ({:08X}, stage={}): {:016X} -> {:016X}; preserving previous UUID",
                safeName, spawnedActorId, static_cast<int>(stage), previousUuid, newUuid);
            return previousUuid;
        }

        if (previousUuid == 0) {
            MARAS_LOG_DEBUG(
                "[Children] Captured initial SkyrimNet UUID for '{}' ({:08X}, stage={}): {:016X}",
                safeName, spawnedActorId, static_cast<int>(stage), newUuid);
        }
        return newUuid;
    }

    void CaptureChildIdentityBeforeDespawn(RE::FormID childActorId, State::ChildRecord& rec) {
        auto& sn = GetSkyrimNetGateway();
        if (!sn.IsAvailable() || childActorId == 0 || IsSyntheticChildID(childActorId)) {
            return;
        }

        const std::string bioTpl = sn.GetActorBioTemplateName(childActorId);
        if (!bioTpl.empty()) {
            rec.savedBioTemplateName = bioTpl;
            MARAS_LOG_INFO("[Training] Captured SkyrimNet bioTemplate='{}' for child {:08X}",
                           bioTpl, childActorId);
        }
        if (rec.skyrimNetUuid == 0) {
            rec.skyrimNetUuid = sn.ResolvePlacedActorUUID(childActorId);
            if (rec.skyrimNetUuid != 0) {
                MARAS_LOG_INFO("[Training] Captured SkyrimNet UUID {:016X} for child {:08X}",
                               rec.skyrimNetUuid, childActorId);
            }
        }
    }

    void SyncSpawnedChildrenOnPostLoad() {
        auto& sn = GetSkyrimNetGateway();
        if (!sn.IsAvailable()) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return;
        }

        std::vector<std::pair<State::ChildId, std::uint64_t>> updates;
        for (const auto& [childId, child] : snap->lineage->AllChildren()) {
            if (childId.IsSynthetic() || !HasPhysicalActor(childId.raw())) {
                continue;
            }
            if (child.stage != Foundation::ChildStage::Child && child.stage != Foundation::ChildStage::Adult) {
                continue;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(childId.raw());
            if (!actor || actor->IsDeleted()) {
                continue;
            }

            const std::uint64_t resolvedUuid = ResolveAndLinkSpawnedChildIdentity(
                childId.raw(), child.stage, child.skyrimNetUuid, 0, child.name);
            if (resolvedUuid != 0 && resolvedUuid != child.skyrimNetUuid) {
                updates.emplace_back(childId, resolvedUuid);
            }
        }

        if (updates.empty()) {
            return;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& lineageW = ws.lineage.Write();
            for (const auto& [childId, resolvedUuid] : updates) {
                if (auto* rec = lineageW.FindChildMutable(childId)) {
                    rec->skyrimNetUuid = resolvedUuid;
                }
            }
        });
    }

}  // namespace MARAS::App
