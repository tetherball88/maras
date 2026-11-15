# AGENTS.md — Modular C++23 Layers (`SKSE_Source/modules/`)

Applies to all 13 module targets under `SKSE_Source/modules/` (`7 STATIC` pure engine-free modules + `6 OBJECT` engine/app/bridge modules). Supplements [SKSE_Source/AGENTS.md](../AGENTS.md) and root [AGENTS.md](../../AGENTS.md).

---

## 1. Layer Taxonomy & Tiered PCH Isolation

- **Layers 0–2 — Pure `STATIC` Modules (Reusing `pch_core.h` from `maras_foundation`)**:
  - Targets: `maras_foundation` (`foundation/`), `maras_state` (`state/`), `maras_projection` (`projection/`), `maras_domain_relationships` (`domain_relationships/`), `maras_domain_lineage` (`domain_lineage/`), `maras_domain_household` (`domain_household/`), `maras_domain_agency` (`domain_agency/`).
  - **Zero Engine or Third-Party Header Leakage**: Pure `STATIC` modules **MUST NEVER** `#include <RE/...>`, `<SKSE/...>`, `"pch_engine.h"`, `<nlohmann/json.hpp>`, `<yaml-cpp/yaml.h>`, `<spdlog/...>`, or `<Windows.h>`.
  - **Logging**: Use `MARAS::Foundation::LogTrace/LogDebug/LogInfo/LogWarn/LogError` (`maras/foundation/Logging.h`). Never use `std::cout` or `MARAS_LOG_*` engine macros inside `STATIC` modules.
- **Layers 3–5 — `OBJECT` Engine, App & Bridge Modules (Reusing `pch_engine.h` from `maras_pch_engine`)**:
  - Targets: `maras_engine` (`engine/`), `maras_app` (`app/`), `maras_bridge_papyrus` (`bridge_papyrus/`), `maras_bridge_ui` (`bridge_ui/`), `maras_bridge_skyrimnet` (`bridge_skyrimnet/`), `maras_bridge_c_api` (`bridge_c_api/`).
  - **Bridge & Layer Boundaries**: `bridge_ui` and `bridge_skyrimnet` must read masked views through `MARAS::Projection::PerspectiveView` (`modules/projection/include/maras/projection/PerspectiveView.h`), never expose raw secret tables directly. Never pass `RE::*` pointers down into `STATIC` modules.
  - **Zero Domain Logic & Zero Monolith Growth in `OBJECT` Modules**: Even though `OBJECT` targets compile with `pch_engine.h`, never write pure gameplay math, eligibility rules, or state-transition policies directly inside `app/` or `bridge_*/`—author them in `domain_*/` (`STATIC`) and invoke them from thin coordinators. Add dedicated `.h`/`.cpp` translation units in [../CMakeLists.txt](../CMakeLists.txt) (`-Reconfigure`) for distinct workflows or payload builders rather than appending to large `*UseCases.cpp`, `*Coordinator.cpp`, or `UIDataBridge.cpp` files.
- **Header Hygiene**: Never put `using namespace` in any header. Use C++23 standard headers and target-matched namespaces (`MARAS::Foundation`, `MARAS::State`, `MARAS::Projection`, `MARAS::Domain::*`, `MARAS::Engine` / `MARAS`, `MARAS::App`, `MARAS::Bridge::*` / `MARAS::PapyrusInterface`).

## 2. Value Types, Strong IDs, `WorldStore` & `CowTable`

- **Strong IDs (`maras/foundation/StrongIds.h`)**: Pass value-typed IDs (`ActorId`, `RefId`, `BaseId`, `SyntheticChildId`, `ChildId`, `LocationId`), never raw engine pointers across domain/state boundaries.
  - Check `IsSyntheticChildRawId(raw)` (`0xFF700000`–`0xFF7FFFFF`) before treating a `0xFF` ID as a dynamically spawned actor, and never advance `nextSyntheticChildId` from `0xFF800000+` FormIDs.
- **Copy-on-Write Tables (`CowTable<T>` in `maras/state/PerTableSnapshot.h`)**:
  - Read via `.Read()` (zero-copy `const T&` / `std::shared_ptr<const T>`).
  - Mutate via `.Write()` only when a mutation actually occurs so untouched tables retain `std::shared_ptr<const T>` structural sharing across `WorldStateSnapshot` publications.
- **`WorldStore::Modify` Contract (`maras/state/WorldStore.h`)**:
  - `Modify` and `ReadLive` forbid reference/pointer returns at compile time (`static_assert(!std::is_reference_v<ReturnT> && !std::is_pointer_v<ReturnT>)`).
  - Callbacks inside `WorldStore::Modify` / `ReadLive` **must never** acquire external mutexes or call SKSE/`TESForm::LookupByID`/Papyrus/UI/network APIs.
- **Slot-Neutral Guardians (`GuardianSlots` in `maras/state/WorldTables.h`)**:
  - `GuardianSlots::operator==` is order-independent (`HasSameSet`). Always query/mutate through `GuardianSlots` methods (`HasGuardian`, `HasAnyGuardian`, `Count`, `IsSingleGuardian`, `SingleGuardian`, `IsSoleGuardian`, `OtherGuardian`, `HasSameSet`, `Assign`, `Remove`) to preserve slot positions and neutrality.
- **Injected Ports Only (`STATIC` Layers)**: Access actor sex, werewolf blood, FormID resolution, or RNG exclusively through pure interfaces (`IActorMetadataProvider`, `IFormIdResolver`, `IWerewolfStateProvider`, `MARAS::Foundation::IRng`).
