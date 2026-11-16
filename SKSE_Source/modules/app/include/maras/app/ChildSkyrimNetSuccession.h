#pragma once

#include "RE/Skyrim.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <string_view>

namespace MARAS::App {

    [[nodiscard]] std::uint64_t ResolveAndLinkSpawnedChildIdentity(
        RE::FormID spawnedActorId,
        Foundation::ChildStage stage,
        std::uint64_t previousUuid,
        RE::FormID previousFormId,
        std::string_view childName);

    void CaptureChildIdentityBeforeDespawn(RE::FormID childActorId, State::ChildRecord& rec);

    void SyncSpawnedChildrenOnPostLoad();

}  // namespace MARAS::App
