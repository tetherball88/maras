# SkyrimNetMemoryService — Feature & Mechanics Guide

**Implementation**: [SkyrimNetMemoryService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetMemoryService.cpp)
**Prompt Template**: [maras_event_memories.prompt](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/SkyrimNet/external/tetherball88.maras/prompts/maras_evaluations/maras_event_memories.prompt)

`SkyrimNetMemoryService` is the centralized C++ service responsible for generating and ingesting LLM-crafted first-person long-term memories (`PublicAddMemory`) across all M.A.R.A.S life events—including live relationship transitions, matchmaking changes, open/closed relationship toggles, 3-party infidelity discoveries and resolutions, pregnancy & paternity reveals, child births, and child adoption/inheritance/custody changes.

---

## 1. 90-Second Connected-Participant Episode Buffer (`MemoryEpisode`)

Many M.A.R.A.S gameplay moments unfold as rapid multi-step chains within seconds of each other—for example:
- **Affair Caught $\rightarrow$ Resolved $\rightarrow$ Divorce $\rightarrow$ Paternity Secret Auto-Revealed $\rightarrow$ Child Custody Transferred**
- **Marriage $\rightarrow$ Step-Parent Child Custody Shared**
- **Paternity Secret Revealed to Partner $\rightarrow$ Partner Breaks Up / Divorces Mother**

To prevent firing redundant LLM requests for each sub-step and to ensure every participant's memory describes the **same shared scene** (location, atmosphere, emotional tone, and sequence of events) from their own first-person perspective:
1. When an event is recorded (`RecordEvent`), `SkyrimNetMemoryService` checks if any active `MemoryEpisode` in `activeEpisodes_` shares at least one participant (`CanMergeWithEpisode`).
2. If no connected episode exists, a new `MemoryEpisode` is created with a **90-second grace window** (`flushTime = firstEventTime + 90s`).
3. If one or more connected episodes exist, the incoming event is appended to the episode's chronological `events` timeline (and any episodes bridged by the new event's participants are merged together into a single episode).
4. Once `now >= flushTime` (checked every second by `PollingService::Update()`), the episode is flushed in **one single LLM call** (`maras_evaluations/maras_event_memories`, `"memory"` variant) requesting one cohesive first-person memory per non-Player living NPC target (`targetFormIDs`).
   - **Programmatic Mode (`skyrimnetUseLLMMemories` / `matchmakingGenerateMemories` == `false`)**: Live gameplay episodes check `skyrimnetUseLLMMemories` (in **Settings $\rightarrow$ SkyrimNet**) and matchmaking episodes check `matchmakingGenerateMemories` (in **Settings $\rightarrow$ Matchmaking**). When the respective toggle is disabled, `SkyrimNetMemoryService` uses a short **5-second coalescing window** (`kProgrammaticGraceWindow`) to merge same-moment cascaded events, bypasses `SendCustomPromptToLLM`, and builds first-person memories programmatically (`BuildProgrammaticMemories`) with deterministic `emotion`, `importanceScore`, `type`, `tags`, and perspective-scoped secret masking before ingesting them via `AddActorMemory`. Manual UI memory previews (`PreviewRelationshipMemories`) similarly check `matchmakingGenerateMemories` and return programmatic memories immediately without an LLM call when disabled.
5. On game load or revert (`Serialization::RevertCallback` / `kPreLoadGame`), `Revert()` clears all pending episodes so stale events never leak across saves.

---

## 2. Single-Request Shared Scene + Per-NPC Knowledge Restrictions (Zero Secret Bleed)

When an event or multi-event episode involves a hidden secret (`isSecretForPartner`, `isSecretForBioFather`, or an undiscovered affair), `SkyrimNetMemoryService` **keeps all connected participants in the same `MemoryEpisode` and single LLM call** so their memories of shared scenes (such as a pregnancy announcement or child birth) remain mutually consistent, while explicitly instructing the LLM on each NPC's exact epistemic boundaries:

1. **Unified Chronological `Recent Events` + Per-Target `knowledgeRestrictions` in `maras_event_memories.prompt`**:
   - Each involved NPC participant is rendered with `render_character_profile("ttm_gm", p.uuid)` (biography, relationships, and long-term memories), followed by a single unified `## Recent Events & Dialogue History (Chronological)` section (`helpers/maras_actor_bio_recent_events.prompt`) that passes all involved participants' UUIDs (including the Player when involved) into `get_recent_events(40, _event_filter)` so back-and-forth dialogue exchanges between NPC A, NPC B, and the Player appear in a single chronological timeline.
   - Each target NPC in the prompt has both `What <Name> Knows & Experienced` (`perspectiveNotes`) and `What <Name> DOES NOT Know (Strict Restrictions — DO NOT BLEED INTO MEMORY)` (`knowledgeRestrictions`), including strict isolation against bleeding private exchanges between other people into an unaware NPC's memory.
   - When `presumedFather` is duped (`isSecretForPartner == true` during **Pregnancy Shared / Visibly Noticed** or **Child Birth**), `mother`, `presumedFather`, and (when aware) `bioFather` are evaluated in the **same event and same LLM prompt**:
     - `mother` receives perspective instructions reflecting the full truth (including letting `presumedFather` believe the child is his while keeping `bioFather`'s paternity secret).
     - `presumedFather` receives perspective instructions reflecting his genuine belief that the child is his own with `mother`, plus an explicit `knowledgeRestriction` stating that he **DOES NOT KNOW** `bioFather` is the biological father or that `mother` had an affair, forbidding any mention of `bioFather`, infidelity, deception, or doubt in `presumedFather`'s memory or tags.
2. **Automatic Unwitnessed Event & Undiscovered Affair Restrictions**:
   - When multiple events merge into a single episode (e.g., Event 1: `mother` privately tells `bioFather` he is the father; Event 2: `presumedFather` notices `mother`'s pregnancy), `BuildEpisodePromptContext` automatically adds a `knowledgeRestriction` for any event in the episode that a target NPC did not participate in (`[Event k] <Name> was NOT present for and DOES NOT KNOW about Event k...`), and automatically restricts any unaware victim of an active undiscovered `CheatingRecord` from knowing about that secret affair.
3. **`hiddenActorFormIDs` Filtering on `AddActorMemory`**:
   - Each `MemoryParticipant` can specify `hiddenActorFormIDs` (e.g., `bioFatherID` for a duped `presumedFather`), and `DispatchEpisodeToLLM` scopes `relatedActorFormIDs` per event while stripping any `hiddenActorFormIDs` so `PublicAddMemory` never links a secret affair partner's UUID to an unaware partner's memory.

---

## 3. Supported Event Categories & Participant Perspectives

### Category 1: Relationship Status Transitions & Open/Close Toggles (2 Participants)
- Hooked in `NPCRelationshipManager::ChangeStatusCommon` and `NPCRelationshipManager::SetPairIsOpen` whenever `sendEvent == true` or `SkyrimNetMemoryService::IsMatchmakingApplyActive() == true`.
- Covers `Dating`, `Lover` (distinguishing consensual lovers, secret physical affairs, and secret `Talking`-stage emotional affairs), `Engaged`, `Married`, `ExDating`, `ExLover`, `Jilted`, `Divorced`, and `Open`/`Close` relationship agreements.
- Also powers manual Prisma UI matchmaking memory previews (`PreviewRelationshipMemories`).

### Category 2: Infidelity Discovery, Confession, Exposure & Resolution (3 Participants)
- Hooked in `IntimateThreadService::SendCheatingCaughtEvent` (`caught`) and `NPCRelationshipManager::SetCheatingDiscovered` (`confessed`, `exposed`, `confronted`, `allowed`, `breakup`).
- Generates perspective-specific memories for all 3 involved parties: **Cheater**, **Betrayed Partner (Victim)**, and **Affair Partner**.

### Category 3: Pregnancy Discovery & Paternity Secret Reveals (1, 2, or 3 Participants)
- **`RecordPregnancyDiscovered`** (hooked in `NPCRelationshipManager::BuildPregnancyAnnouncedJson`):
  - **Normal Pregnancy with Partner**: Generates memories for both **Mother** and **Partner** discovering they are expecting a child together.
  - **Secret Pregnancy with Non-Partner Father**: Generates a memory for **Mother** discovering she is pregnant by the biological father while in a relationship with her partner (and merges into the same 90-second episode if she immediately confesses or shares the pregnancy).
  - **Unpartnered Mother**: Generates a memory for **Mother** discovering her pregnancy (merging with any immediate bio-father step-in transition within 90 seconds).
- **`RecordPaternityReveal`** (hooked in `ResolvePaternitySecret`, `ResolvePaternitySecretForPartner`, `RevealPregnancyToPartner`, `ResolvePaternitySecretForBioFather`, `SetPaternitySecretEdges`, `EvaluatePaternitySecret`, and `EvaluateChildCustodyOnDivorce`):
  - Dynamically targets 2 participants (Mother + Bio Father, or Mother + Presumed Father) when revealed to one man, or 3 participants (Mother + Presumed Father + Bio Father) when revealed to both (`mother_confessed_both` / `divorce_revealed`), and distinguishes whether an unborn pregnancy was previously unannounced to the partner (`wasPregnancySecretForPartner`) or already known and presumed to be his.

### Category 4: Child Birth (1 to 3 Adult Participants — Never the Newborn)
- Hooked in `NPCRelationshipManager::BuildChildBirthJson`.
- Groups twins/triplets by paternity bucket so multiple babies born in the same labor produce one memory per adult participant, covering all 5 paternity knowledge combinations (no discrepancy, both fathers unaware/duped, bio father aware while presumed father duped, presumed father knowingly accepting while bio father unaware, and all 3 aware).
- Never generates a memory for the newborn infant.

### Category 5: Child Adoption, Inheritance & Custody Changes (Placed Stage 2 Child + All Involved Adults)
- Hooked in `NPCRelationshipManager::SendChildEvent` (`adopted`, `inherited`, `guardians_changed` with reason `marriage`, `divorce`, or `custody_change`).
- Generates memories for the **placed Stage 2 Child** (`!IsSyntheticChildID && stage == ChildStage::Child`) as well as adoptive guardians, surviving/inheriting guardians, new step-guardians on marriage, and former guardians who lost custody on divorce or marriage transfer.
