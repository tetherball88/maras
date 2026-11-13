# AGENTS.md — Engine-Free Pure Modules (`SKSE_Source/modules/`)

Applies to all files under `SKSE_Source/modules/` (`maras_foundation`, `maras_state`, and future `maras_projection` / `maras_domain_*`). Supplements `SKSE_Source/AGENTS.md` and root `AGENTS.md`.

---

## 1. Strict Compile & Link Isolation

- **Zero Engine or Third-Party Header Leakage**: Files in `modules/` **MUST NEVER** `#include <RE/...>`, `<SKSE/...>`, `"PCH.h"`, `<nlohmann/json.hpp>`, `<yaml-cpp/yaml.h>`, or `<spdlog/...>`.
- **Header Hygiene**: Never put `using namespace` in any header. Use standard C++23 headers only.
- **Namespaces**: Match physical CMake `STATIC` targets (`MARAS::Foundation`, `MARAS::State`, `MARAS::Projection`, `MARAS::Domain::*`).
- **Logging**: Use `MARAS::Foundation::LogTrace/LogDebug/LogInfo/LogWarn/LogError` (`maras/foundation/Logging.h`). Never use `std::cout` or `MARAS_LOG_*` engine macros inside `modules/`.

## 2. Value Types, Strong IDs & `CowTable` Sharing

- **Strong IDs (`maras/foundation/StrongIds.h`)**: Pass value-typed IDs (`ActorId`, `RefId`, `BaseId`, `SyntheticChildId`, `ChildId`, `LocationId`), never raw engine pointers.
  - Check `IsSyntheticChildRawId(raw)` (`0xFF700000`–`0xFF7FFFFF`) before treating a `0xFF` ID as a dynamically spawned actor, and never advance `nextSyntheticChildId` from `0xFF800000+` FormIDs.
- **Copy-on-Write Tables (`CowTable<T>` in `PerTableSnapshot.h`)**:
  - Read via `.Read()` (zero-copy `const T&` / `std::shared_ptr<const T>`).
  - Mutate via `.Write()` only when a mutation actually occurs so untouched tables retain `std::shared_ptr<const T>` structural sharing across `WorldStateSnapshot` publications.
- **Slot-Neutral Guardians (`GuardianSlots`)**:
  - `GuardianSlots::operator==` is order-independent (`HasSameSet`). Always mutate/query through `GuardianSlots` methods (`Has`, `Assign`, `Remove`, `GetOther`, `Single`) to preserve slot positions and neutrality.
- **Injected Ports Only**: Access actor sex, liveness, FormID resolution, or RNG exclusively through pure interfaces (`IActorMetadataProvider`, `IFormIdResolver`, `MARAS::Foundation::IRng`).
