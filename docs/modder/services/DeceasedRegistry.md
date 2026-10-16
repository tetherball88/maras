# DeceasedRegistry — Feature & Mechanics Guide

**Implementation**: [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp)

`DeceasedRegistry` is M.A.R.A.S's permanent historical archive (`'DCSD'` SKSE co-save record) for tracked adults and children who die during a playthrough. Because dying NPCs must be removed from active household, hierarchy, buff, and matchmaking rosters, `DeceasedRegistry` captures a complete pre-cleanup snapshot of their relationships, archetypes, family links, and cause of death so surviving partners, parents, dialogue conditions, and the Prisma UI Dashboard can remember them as late spouses, fiancés, exes, or deceased children.

---

## 1. Pre-Cleanup Snapshot Archival on Death

When `TESDeathEvent` fires for an actor, `DeceasedRegistry` archives their state **before** `NPCRelationshipManager` tears down their live relationship and household entries:

### 1. Deceased Adult Archival (`RecordDeceasedAdult`)
- **Eligibility**: Only archives adult NPCs who are registered in [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp) (ignoring random untracked enemies or bandits).
- **Identity & Killer Metadata**:
  - Records both the victim's **Base FormID (`TESNPC`)** and **Reference FormID**, display name, in-game death day (`Calendar::GetDaysPassed`), whether they were killed directly by the Player (`wasKilledByPlayer`), and the killer's Base FormID, Reference FormID, and display name.
- **Archetype Preservation**:
  - Snapshots the NPC's `SocialClass`, `SkillType`, and `Temperament` from their `NPCRelationshipManager` profile before it is cleared.
- **Partner Bonds Snapshot (`partnersAtDeath`)**:
  - Captures every partner (both the Player and other NPCs) with whom the deceased had an active or historical romantic relationship (`Married`, `Engaged`, `Dating`, `Lover`, `Divorced`, `Jilted`, `ExDating`, `ExLover`, excluding `Candidate`) at the moment of death, recording each partner's Base FormID, Reference FormID, display name, and exact `RelationshipStatus` at death.

### 2. Deceased Child Archival (`RecordDeceasedChild`)
- Records the child's Base and Reference FormIDs, name, death timestamp, and killer metadata.
- **Custodial & Genetic Lineage Snapshot (`childInfo`)**:
  - Captures both custodial guardians (`guardianA` and `guardianB` Base IDs, Ref IDs, and names), biological parents (`bioMotherBaseID`, `bioFatherBaseID`), whether the child was adopted (`isAdopted`), and which player house they lived in (`currentHouseFormID` from [PlayerChildrenService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp)).

---

## 2. Widowhood, Bereavement & Historical Relationship Queries

Because all records are indexed by **Base FormID** with a secondary **Reference-to-Base lookup map**, queries work reliably regardless of whether callers pass a Base FormID or a Reference FormID (even after the dead actor's 3D reference has been cleaned up by the engine):

- **Widow / Widower Detection (`IsWidowOrWidower`)**:
  - Determines whether the Player or any surviving NPC lost a partner to whom they were `Married` at the time of death.
- **Pair-Specific Historical Queries**:
  - Allows dialogue conditions, AI decorators, and Papyrus scripts to check whether a surviving actor `WasMarriedTo`, `WasEngagedTo`, or `WasExOf` (`Divorced`, `Jilted`, `ExDating`, or `ExLover`) a specific deceased NPC, or retrieve the full list of `GetDeceasedSpousesOf` / `GetDeceasedPartnersOf` any character.
- **Bereaved Parent Queries (`GetDeceasedChildrenOf` / `HasDeceasedChildren`)**:
  - Identifies all deceased children for whom the queried actor (Player or NPC) was a registered custodial guardian (`guardianA` or `guardianB`) or biological parent (`bioMotherBaseID` or `bioFatherBaseID`) at the time of the child's death.
- **Exclusion from Live Systems (`IsDeceased`)**:
  - Queried by [BanterCoordinatorService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp) and other runtime systems to guarantee that dead actors are never selected for ambient scenes or live activities.

