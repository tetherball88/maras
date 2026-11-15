# DeceasedQueries

**Sources**: [DeceasedQueries.h](../../../SKSE_Source/modules/domain_household/include/maras/domain/household/DeceasedQueries.h) · [DeceasedQueries.cpp](../../../SKSE_Source/modules/domain_household/src/DeceasedQueries.cpp) · [WorldTables.h](../../../SKSE_Source/modules/state/include/maras/state/WorldTables.h) · [CoSaveCodec.cpp](../../../SKSE_Source/modules/state/src/CoSaveCodec.cpp)  
**Cross-References**: [WorldStoreAndCoSave.md](WorldStoreAndCoSave.md) · [PerspectiveView.md](PerspectiveView.md) · [LineageUseCases.md](LineageUseCases.md) · [ARCHITECTURE.md](../ARCHITECTURE.md)

## Overview

`MARAS::Domain::Household::DeceasedQueries` provides pure, read-only queries over `WorldStateSnapshot::deceased` (`DeceasedTable`). When a registered adult partner or tracked child dies (or is promoted to `deceased` via quest events), M.A.R.A.S archives a snapshot in `DeceasedActorRecord` so widows/widowers, ex-partners, former spouses, and grieving parents retain historical queries without keeping dead actors active in living relationship or household tables.

---

## Deceased Archive Semantics

Each `DeceasedActorRecord` in `DeceasedTable` preserves:

| Aspect | Behavior |
| :--- | :--- |
| **Actor & Child Identity** | Tracks placed `refId`, base `baseId`, and `isChild` flag. Lookup predicates match both base FormIDs and placed reference FormIDs so uniquely named static actors and dynamic `0xFF` spawned actors resolve consistently. |
| **Dynamic `0xFF` Keying** | `DeceasedTable::ComputePrimaryKey` keys dynamic `0xFFxxxxxx` runtime actors (`refId.IsDynamicRuntimeRef()`) strictly by unique `refId` instead of shared template `baseId`. Child queries likewise fall back to `rec.refId` over `baseId` when returning `ChildId`. |
| **Partners at Death** | Stores a list of `DeceasedPartnerEntry` records (`partnersAtDeath`), recording each partner's `partnerBaseId`, `partnerRefId`, `partnerName`, `statusAtDeath`, `isPlayer`, plus `'DCS1'` secrecy fields (`isSecretAffair`, `publicStatusAtDeath`). |
| **Deceased Child Metadata** | Stores optional `childInfo` (`DeceasedChildMetadata`) preserving `guardianA/B` slots, `bioMotherBaseId`, `bioFatherBaseId`, `isAdopted`, `houseId`, plus `'DCS1'` secrecy fields (`hasUndiscoveredPaternitySecret`, `publicBioFatherBaseId`). |

---

## Pure Query Functions

All query functions operate exclusively on `const State::WorldStateSnapshot&` and never mutate state:

| Query | Behavior |
| :--- | :--- |
| `IsDeceased` | Checks whether `actorId` (or optional fallback `BaseId`) exists in `DeceasedTable`. |
| `IsWidowOrWidower` | Returns true if `survivingActor` appears as a surviving partner with `Married` status at death in any non-child `DeceasedActorRecord`. |
| `WasMarriedTo` | Matches a deceased adult against a candidate partner (accepting optional `BaseId`/`RefId` hints) where `statusAtDeath == RelationshipStatus::Married`. |
| `WasEngagedTo` | Same match criteria restricted to `RelationshipStatus::Engaged`. |
| `WasExOf` | Same match criteria restricted to breakup statuses (`Foundation::IsBreakupStatus(statusAtDeath)`). |
| `GetDeceasedSpousesOf` | Returns unique `BaseId` identifiers (falling back to `BaseId{ rec.refId.raw() }`) for every deceased adult whose `partnersAtDeath` includes `survivingPartner` with `Married` status. |
| `GetDeceasedPartnersOf` | Returns unique identifiers for deceased adults whose **visible** status at death (`isSecretAffair ? publicStatusAtDeath : statusAtDeath`) satisfies `IsActivePartnerStatus`, preventing undiscovered affairs from leaking through public partner queries. |
| `GetDeceasedChildrenOf` | Returns unique `ChildId` entries for deceased children whose `childInfo` matches `parentActor` via `MatchesChildParentOrGuardian` (any recorded guardian or biological parent ID/base/ref match), preferring `refId` over `baseId` to preserve `0xFF` identity. |

---

## Co-Save Persistence (`'DCSD'` & `'DCS1'`)

`CoSaveCodec` persists `DeceasedTable` inside the `'DCSD'` chunk of the `'MARS'` v7 co-save:

- **Base v7 Record (`'DCSD'`)**: Stores deceased identity, `deathTimestamp`, `partnersAtDeath`, and `childInfo` custody/biological lineage snapshots.
- **Secrecy Extension Sub-Chunk (`'DCS1'`)**: Stored inline inside `'DCSD'` to persist partner affair secrecy (`isSecretAffair`, `publicStatusAtDeath`) and child paternity secrecy (`hasUndiscoveredPaternitySecret`, `publicBioFatherBaseId`) while keeping v7 load paths clean for older records without `'DCS1'`.

---

## Secrecy & Masking Boundaries

- **`GetDeceasedPartnersOf`** masks secret affairs directly in the domain query layer using `publicStatusAtDeath`.
- **Dashboard / JSON Dumps & SkyrimNet**: Deceased partner/child dumps in `LineageUseCases` (`DumpDeceasedPartnersJson`, `DumpChildrenJson`) and `PerspectiveView` apply epistemic/Intrigue masking (`revealedSecrets`, `CanObserverKnowTrueBioFather`) so undiscovered affairs and secret paternity on deceased records never leak to unaware observers or Dashboard Intrigue Mode (see [PerspectiveView.md](PerspectiveView.md) and [LineageUseCases.md](LineageUseCases.md)).
