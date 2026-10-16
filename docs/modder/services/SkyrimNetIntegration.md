# SkyrimNetIntegration — Feature & Mechanics Guide

**Implementation**: [SkyrimNetIntegration.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)

`SkyrimNetIntegration` bridges M.A.R.A.S's C++ relationship, household, pregnancy, and drama state with **SkyrimNet.dll** via dynamic runtime DLL exports (`GetProcAddress`). It registers prompt decorators (`ttm_*`), builds knowledge-filtered relationship profile JSONs for NPC dialogue prompts, dispatches custom LLM evaluations (with interactive retry modals), and injects episodic memories into SkyrimNet's memory database.

---

## 1. Zero-Dependency Runtime Detection & UUID Resolution

- **Dynamic DLL Discovery (`IsSkyrimNetAvailable`)**:
  - Checks whether `SkyrimNet.dll` is mapped into the Skyrim process (`GetModuleHandleW`) and respects the developer testing override `simulateNoSkyrimNet`.
  - Resolves all SkyrimNet C API functions (`PublicRegisterDecorator`, `PublicFormIDToUUID`, `PublicGetPluginConfigValue`, `PublicSendCustomPromptToLLM`, `PublicAddMemory`) dynamically so M.A.R.A.S runs without hard DLL dependencies when SkyrimNet is not installed.
- **Base-First UUID Resolution**:
  - When converting an actor's FormID to a 64-bit SkyrimNet UUID (`FormIDToUUID`), the service first attempts to resolve the actor's **Base NPC FormID (`TESNPC`)** via `PublicFormIDToUUID`, falling back to the placed Reference FormID if the base returns `0`.
- **Action Proximity Radius**:
  - Determines how close two NPCs must be to trigger proximity-gated relationship actions by checking M.A.R.A.S's `skyrimnetActionRadius` setting first, then querying SkyrimNet's `nearbyActors.radius` plugin config, and defaulting to `3000.0` units.

---

## 2. Schema-v2 Actor-Relative Relationship Profile (`ttm_npc_relationship_profile`)

The centerpiece of SkyrimNet prompt injection is `BuildRelationshipProfileJSON`, which generates an **actor-relative, knowledge-filtered** JSON profile (`schema_version: 2`) for whichever NPC is currently speaking or thinking:

### 1. Pregnancy & Superfecundation Grouping
- If the NPC is pregnant, groups all unborn fetuses (`0xFF` synthetic records) by biological father (`PregnancyFatherGroup`), supporting **superfecundation** (multiple fetuses fathered by different men in the same pregnancy) as well as twins/triplets.
- Computes whether the pregnancy carries an active paternity secret from the mother's committed partner (`is_secret_for_partner`) or from the biological father (`is_secret_for_bio_father`).

### 2. Current & Past Relationships with Permission Context
- Lists all active (`married`, `engaged`, `dating`, `lover`) and past (`divorced`, `jilted`, `exdating`, `exlover`) relationships—**excluding** ongoing secret affair partners from `current_relationships` (those appear under `infidelity` instead).
- Tags each active relationship with its **consent/permission model**:
  - `player_polygamy`: Permitted because the actor is the Player and polygamy is enabled.
  - `open_primary`: A secondary `lover` relationship permitted because the actor has an open primary partner (`permission_source`).
  - `primary` / `unrestricted`: Standard primary or single-lover relationship.
- **Player Co-Partners (`player_co_partners`)**: If the NPC is an active partner of the Player and polygamy is enabled, lists all of the Player's *other* spouses (`co_spouse`) and partners (`co_partner`) sorted by hierarchy (`Married > Engaged > Dating > Lover`) so sister-wives/co-spouses know about each other in dialogue.

### 3. Strictly Knowledge-Filtered Children, Step-Children & Paternity Secrets
- **Custodial Family Branches (`family`)**: Groups the NPC's custodial children by co-guardian (`other_parent` or `single_parent`), including each child's life stage (`infant`, `child`, `training`, `adult`), whether they are ephemeral (`0xFF` non-spawned actor), their training location, and their assigned pet or woodland critter.
- **Non-Custodial Biological Children (`non_custodial_children`)**: Lists biological children raised by another household—**unless** the speaking NPC is the biological father and the mother is keeping his paternity a secret from him (`isSecretForBioFather && !discoveredByBioFather`), in which case the child is completely hidden from his prompt!
- **Epistemic Paternity Secret Filtering**:
  - A child's `paternity_secret` block is **only** included in an NPC's prompt if that specific NPC is allowed to know about it:
    - The biological mother (and Player) always know.
    - The deceived partner only sees the secret if `isSecretForPartner == false` or `discovered == true`. If the deceived step-parent still believes the child is biologically theirs, the child's `relation` is presented to them as `"son"` / `"daughter"` rather than `"stepson"` / `"stepdaughter"`.
    - The biological father only sees the secret if `isSecretForBioFather == false` or `discovered == true`.
- **Child's Own Perspective (`parents`)**: If the speaking actor is themselves a registered child (or grown child), populates their `parents` block with their biological parents, current guardians, step-parents, siblings (across all shared guardians and bio parents), pets/critters, and any paternity intrigue.

### 4. Epistemic Infidelity Filtering (`infidelity`)
- Lists cheating records where the actor is the `cheater`, `affair_partner`, or `victim`.
- **Victim Ignorance Guarantee**: If the speaking NPC is the **victim** of an affair and `record.isSecret == true`, the affair record is **completely omitted** from their JSON profile so the LLM cannot accidentally make a betrayed spouse act omniscient before discovering the affair.

---

## 3. Proximity & Eligibility Action Decorators

In addition to `ttm_npc_relationship_profile` and `ttm_is_registered`, the service registers specialized decorators that dynamically compute which relationship and drama actions an NPC is currently eligible to perform with nearby actors:

- **Vanilla Prior-Relationship Backstory (`ttm_get_ex_partner`)**:
  - Inspects `TESNPC` relationship records (`Spouse`, `Courting`, `kLover`) from two perspectives:
    - **Perspective 1**: The speaking NPC left a vanilla spouse/courtship/lover to marry or date the Player.
    - **Perspective 2**: The speaking NPC was left behind when their vanilla partner entered a relationship with the Player.
  - Generates a natural-language backstory sentence explaining who left whom for the Dragonborn.
- **Composite Available Actions List (`ttm_get_available_actions`)**:
  - Scans actors within `GetNearbyActorsRadius()` and returns the exact list of action identifiers (`ProposeDating`, `ConsiderDatingProposal`, `ProposeLover`, `ConsiderLoverProposal`, `ProposeEngagement`, `ConsiderEngagementProposal`, `BreakupDating`, `BreakupLover`, `CallOffEngagement`, `Divorce`, `OpenRelationship`, `CloseRelationship`, `RevealChildSecret`, `DiscoverChildPaternity`, `ConfessAffair`, `ExposeAffair`, `ResolveAffair`) currently valid for the speaker.
- **Per-Action Candidate Target Lists**:
  - Individual decorators (`ttm_get_propose_dating_candidates`, `ttm_get_propose_lover_candidates`, `ttm_get_propose_engagement_candidates`, `ttm_get_breakup_*`, `ttm_get_divorce_candidates`, `ttm_get_open_relationship_candidates`, `ttm_get_close_relationship_candidates`) list the exact names of valid targets so YAML action prompts can constrain LLM target selection.
  - **Consequence Warning Decorators**: `ttm_get_exclusive_main_partners` lists partners who would be betrayed if the speaker takes a new lover, and `ttm_get_lovers_becoming_ex` lists casual lovers who will automatically become ex-lovers if the speaker enters an exclusive dating/engaged relationship.
- **Affair & Paternity Drama Candidate Lists**:
  - `ttm_get_confess_affair_candidates` (cheater -> nearby victim), `ttm_get_expose_affair_candidates` (paramour -> nearby victim), and `ttm_get_resolve_affair_candidates` (cheater/victim -> nearby counterpart for an already-exposed affair).
  - `ttm_get_reveal_child_secret_candidates` (mother -> nearby deceived partner, bio father, or `Both`—deduplicating unborn twins/triplets) and `ttm_get_discover_child_paternity_candidates` (deceived partner or bio father -> nearby mother or child, flagging `"noticeable physical difference"` when `hasPhenotypeMismatch` is true on a born child).

---

## 4. LLM Evaluations & Paternity Secret Resolution

1. **Relationship Status Change Evaluation (`EvaluateRelationshipStatusChange`)**:
   - Sends `maras_evaluations/maras_evaluate_relationship_change` (`gamemaster_evaluation` variant) to decide whether an NPC pair should transition to `targetStatus`.
   - If the LLM returns `allow: true` (or `start: true`), promotes the pair in `NPCRelationshipManager`.
   - If the HTTP request fails or returns unrepairable JSON, displays an in-game `Retry / Ignore` modal (`ShowLLMRetryModal`).
2. **Paternity Secret Evaluation (`EvaluatePaternitySecret`)**:
   - Triggered at birth or from the Prisma UI (`maras_evaluations/maras_paternity_evaluation`), passing mother, presumed father, and biological father profiles plus any racial phenotype mismatch description (`GetVisualDiscrepancyDescription`).
   - Resolves five canonical outcomes on the main thread:
     - `mother_confessed_presumed` / `presumed_father_learnt`: Resolves the secret for the deceived partner and applies `partner_reaction` (`divorce` or `breakup` -> `ExDating`).
     - `mother_confessed_bio` / `bio_father_learnt`: Resolves the secret for the biological father.
     - `mother_confessed_both`: Resolves both edges simultaneously and applies `partner_reaction`.
   - Fires the `maras_paternity_secret_resolved` SKSE ModCallbackEvent and refreshes the Prisma UI.

