# `PerspectiveView` & Epistemic Masking — Architecture & Behavioral Guide

**Layer**: Projection (`SKSE_Source/modules/projection`)  
**Sources**:
- [PerspectiveView.h](../../../SKSE_Source/modules/projection/include/maras/projection/PerspectiveView.h)
- [PerspectiveLens.cpp](../../../SKSE_Source/modules/projection/src/PerspectiveLens.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`PerspectiveView` is M.A.R.A.S.'s centralized epistemic projection boundary over immutable `WorldStateSnapshot` and `SpatialActorSnapshot` views. Every read consumer that must respect **who knows what**—SkyrimNet self-perspectives, SkyrimNet dialogue-target public bios, UI Dashboard/JSON dumps, C API status history queries, and adult-child travel parent lookups—queries `PerspectiveView` instead of reading secret fields directly from raw tables.

---

## 2. Four Projection Modes & Construction Rules

| Mode | Constructor Factory | Intended Consumer |
| :--- | :--- | :--- |
| **`ActorSelf`** | `PerspectiveView::ForActor(world, spatial, viewerActorId, werewolfProvider)` | First-person LLM actor bios (`ttm_npc_relationship_profile` when `render_mode != "dialogue_target"`), action candidate lists, memory restrictions, self-aware travel checks. |
| **`DialogueTargetPublic`** | `PerspectiveView::ForDialogueTarget(world, spatial, werewolfProvider)` | Public knowledge view (`render_mode == "dialogue_target"`): hides all personal secrets even when the target NPC personally knows them. |
| **`DashboardIntrigue`** | `PerspectiveView::ForDashboard(world, spatial, revealSecretsOverride, werewolfProvider)` | UI Dashboard, full JSON dumps (`ProcessDumpData`, `DumpJson`, `DumpDeceasedJson`), and exported C API status history (`MARAS_PublicAPI`). Active whenever `!revealSecretsInUI`. |
| **`Omniscient`** | `PerspectiveView::ForDashboard(..., /*revealSecretsOverride=*/true, ...)` (or default omniscient) | Reveal-all mode (`revealSecretsInUI == true`) and internal game-master candidate eligibility checks where unmasked world state is required. |

> [!IMPORTANT]
> **Zero `0x14` Player Exemption in UI Masking**: `DashboardIntrigue` never exempts the Player (`0x14`) from world-wide secret masking. Secret paternity, unannounced NPC pregnancies, undiscovered affairs involving Player spouses, hidden werewolf inheritance, and `'DCS1'` deceased secrets stay uniformly masked across all dashboard views and dumps until discovered in-game or `SettingsData::revealSecretsInUI` is enabled.

---

## 3. Domain-Specific Masking Rules (`PerspectiveLens.cpp`)

### Affairs & Pending Partner Intents
- **Affairs (`CanViewerKnowAffair`)**:
  - Revealed affairs (`!isSecret || isDiscovered || discoveredByVictim`) are visible in `ActorSelf`, `DashboardIntrigue`, and `Omniscient`, and exposed if discovered in `DialogueTargetPublic` (`is_secret_affair` itself is stripped in `DialogueTargetPublic`).
  - Undiscovered secret affairs (`isSecret && !isDiscovered && !discoveredByVictim`) are hidden in `DialogueTargetPublic` and `DashboardIntrigue`, and visible in `ActorSelf` **only** when the viewer is `cheater` or `paramour` (or in `toldActors`).
  - **Affair Stage Presentation (`ToPerceivedAffair`)**: When projecting an affair record where `deceivedPartner` is not the Player, `effectiveAffairStage` resolves to `AffairStage::Physical`.
- **Pending Partner Intents (`GetPerceivedPendingIntent`)**: Partner intents such as `ConfessTalking` or `ConfessPhysical` are visible only in `Omniscient` or `ActorSelf` when `viewer == partner`.

### Pregnancies (`CanViewerKnowPregnancyImpl`)
- When `isPregnancySecretForPartner && !isPregnancyDiscovered && !pregnancyDiscoveredByPartner`:
  - **`Omniscient`**: Visible.
  - **`DialogueTargetPublic`**: Hidden.
  - **`ActorSelf`**: Visible only to `bioMother`, `bioFather` (when `!isSecretForBioFather` or known), or explicitly told actors.
  - **`DashboardIntrigue`**: Hidden for NPC mothers (`!bioMother.IsPlayer()`); **if the mother itself is the Player (`0x14`)**, her own unborn pregnancy record remains intentionally visible to the UI pregnancy tracker (while secret paternity still stays masked if applicable).

### Secret Paternity & Sibling Lineage (`CanViewerKnowTrueBioFatherImpl`, `ProjectChildRecord`)
- While `isSecretForPartner` is active and neither confessed/discovered nor `paternitySecret->revealedToPlayer`:
  - **`Omniscient`**: Reveals true `bioFather`.
  - **`DialogueTargetPublic`**: Always masks secret paternity and exposes only public sibling links (`siblings_public`).
  - **`ActorSelf`**: Exposes true `bioFather` only to `bioMother`, an aware `bioFather` (`!isSecretForBioFather`), or told witnesses; otherwise `perceivedBioFather` resolves to `motherPartner` (the deceived presumed father) or `UnknownFather`, and perceived `siblings` / `step_parents` / `biological_parents` follow what that viewer believes.
  - **`DashboardIntrigue`**: Strictly masks true `bioFather` (zero Player exemption) and substitutes the public/presumed father (`motherPartner` or `UnknownFather`), mirroring the masking behavior across living children (`ProjectChildRecord`) and deceased child records (`'DCS1'` + [`DeceasedQueries`](DeceasedQueries.md)).

### Secret Werewolf Inheritance (`IsActorKnownWerewolf`)
- **`DialogueTargetPublic`**: Always `false` (never exposes `has_beastblood` on another speaker's target bio).
- **`ActorSelf` & `DashboardIntrigue`**: For children, exposes `hasBeastblood` **only** when `!hideSecretFromViewer || visibleParentHasBeastblood` (preventing a child's werewolf status from leaking secret biological parentage before paternity is known).
- **Non-Child Actors**: Queries the injected `IWerewolfStateProvider` (`EngineWerewolfStateProvider`) except in `DialogueTargetPublic`.

### Event Routing & Masking (`EventDispatchPlan`)
- `EventDispatchPlan` (`maras_projection`) evaluates `PerspectiveView::ForActor` and `PerspectiveView::ForDashboard(..., /*revealSecretsOverride=*/true)` over immutable state snapshots to plan exact-audience splits, `MaskForPartner` / `MaskForChildOnly` legs, `0x14` Player speaker-fallback suppression, and narration suppression across all `ttm_*` SkyrimNet events cleanly off-engine.
