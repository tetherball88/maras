# AGENTS.md — SkyrimNet External Bundle (`tetherball88.maras`)

These rules apply to all files in `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/` (`manifest.json`, `actions/`, `triggers/`, and `prompts/`). They supplement the root `AGENTS.md`.

---

## 1. Bundle Structure & Naming Parity (SkyrimNet Beta 25+)

- **`manifest.json`**: Must remain valid JSON at the bundle root (`external/tetherball88.maras/manifest.json`).
- **Action YAML Filenames (`actions/*.yaml`)**:
  - Every file in `actions/` **MUST** have a filename that strictly matches its internal `name:` attribute (case-sensitive), e.g., `actions/ProposeLover.yaml` must declare `name: ProposeLover`.
  - Action `scriptName` and `functionName` fields must point to valid `global` or quest functions in `TTM_Actions.psc` / `TTM_ServiceRelationships.psc`.
- **Trigger YAML Filenames (`triggers/*.yaml`)**:
  - Every file in `triggers/` **MUST** have a filename that strictly matches its internal `name:` attribute (e.g., `triggers/ttm_caught_cheating.yaml` must declare `name: ttm_caught_cheating`).
  - All M.A.R.A.S triggers listen to `eventType: ttm_event` and filter on `event_json.type` matching the payload constructed by `TTM_ServiceSkyrimNet.BuildJson(...)`.

---

## 2. Prompt Templates (`prompts/`) & `dialogue_target` Secret Isolation

- **Decorator-First Data Access**:
  - Never hardcode relationship states, archetypes, or family assumptions in `.prompt` files.
  - Always query runtime state via the C++ decorators registered in `SKSE_Source/src/core/SkyrimNetIntegration.cpp` (`ttm_is_registered(actorUUID)`, `ttm_npc_relationship_profile(actorUUID)`, `ttm_get_*_candidates(actorUUID)`, etc.).
- **Guard Submodules with `ttm_is_registered(actorUUID)`**:
  - Character bio and relationship submodules (`submodules/character_bio/0602_maras_relations.prompt`, `submodules/maras_relations/*.prompt`) must guard actor lookups with `{% if ttm_is_registered(actorUUID) %}` so unregistered actors emit nothing.
- **Strict `render_mode == "dialogue_target"` Public-Knowledge Isolation**:
  - In SkyrimNet, `render_mode != "dialogue_target"` builds `actorUUID`'s **own** private bio, whereas `render_mode == "dialogue_target"` describes `actorUUID` **to another speaker** (public knowledge only).
  - Even when `actorUUID` personally knows an active secret (e.g., a cheating partner, affair paramour, aware biological mother, or household-aware child), `render_mode == "dialogue_target"` **MUST NEVER** leak:
    - Secret affairs (`is_secret_affair` in `0030_partners.prompt` and `0040_ex_partners.prompt`, avoiding empty section headers when all entries are filtered).
    - Undiscovered custodial/adopted paternity secrets (`has_undiscovered_paternity_secret`, `"stepdaughter"`/`"stepson"`, `step_parents`, or secret `bio_father` in `0020_children.prompt` and `0001_child_bio.prompt`—watch `elif` fallthroughs!).
    - Non-custodial secret paternity (`is_paternity_secret` and `is_other_parent_secret` in `0020_children.prompt`, substituting `public_other_parent` when present).
    - Secret half-siblings or lycanthropy (`0001_child_bio.prompt` must use `parents.siblings_public` in `dialogue_target` mode and keep `has_beastblood` gated to `render_mode != "dialogue_target"`).
- **Unannounced Pregnancy Candidates in Action Prompts**:
  - When an action candidate (such as `maras_action_discover_child_paternity.prompt` for a Female Player confession) carries an `UNANNOUNCED` tag, the prompt must explicitly instruct `{{speakingNpc.name}}` that they have **zero prior knowledge** of the pregnancy unless the mother explicitly confesses/reveals it in the current exchange.
- **Relative Template Includes**:
  - Internal template `render_template(...)` or include calls resolve relative to `prompts/`.

---

## 3. C++ Evaluation & Jev Decision Prompts (`prompts/decisions/` and `prompts/maras_evaluations/`)

M.A.R.A.S uses two categories of C++-invoked prompts:

1. **Jev Decision Prompts (`prompts/decisions/*.prompt`)**:
   - Invoked via `PublicSendCustomDecisionToLLM` (`SkyrimNetDecisions.cpp`) when `skyrimnetUseDecisions` is enabled (`true` by default):
     - `maras_step_in_evaluation.prompt` — Evaluates whether a biological father steps in after a paternity-driven divorce (`[ question bio_father_steps_in noul ]`).
     - `maras_paternity_evaluation.prompt` — Evaluates paternity secret exposure and partner reaction (`[ question outcome choice ]`, `[ question partner_reaction choice ]`).
     - `maras_player_partner_cheating.prompt` — Evaluates Tier 2 Player-Partner Agency across all 5 `evalMode`s (`candidate_select` via `[ question decision choice ]`, and `breakup_affair` / `breakup_open_lover` / `suggest_open` / `suggest_close` via `noul` questions `will_breakup`, `will_confess`, `will_suggest`).
     - `maras_matchmaking_verify.prompt` — Verifies proposed matchmaking transitions using per-actor consent (`willing_a && willing_b` for `new`/`advance`/`open`/`close`) or unilateral initiation (`wants_breakup_a || wants_breakup_b` for `breakup`).
   - **Strict Jev Schema Formatting**: Every `[ question <id> choice|noul ]` block **must** include a non-empty instruction line immediately after `[ question ... ]` before opening `[ criteria ]`, and every criterion (`key: Single-line description`, or `true:` / `false:` for `noul`) must remain on a single line with no embedded newlines, colons in keys, or brackets. Never rename question IDs or option keys without updating `SKSE_Source/src/core/SkyrimNetDecisions.cpp`.

2. **Chat Evaluation Prompts (`prompts/maras_evaluations/*.prompt`)**:
   - Invoked via `PublicSendCustomPromptToLLM` for open-ended JSON generation (`maras_matchmaking_scheduled.prompt`, `maras_matchmaking_manual.prompt`, `maras_event_memories.prompt`, `maras_npc_couple_banter.prompt`) and as automatic fallbacks when `skyrimnetUseDecisions` is disabled or SkyrimNet's `decisions` model slot returns `"error": "no_decisions_route"`.
   - **Strict Output Schema**: Never rename or remove required JSON output keys instructed in `prompts/maras_evaluations/*.prompt` without simultaneously updating the corresponding C++ response parser in `SKSE_Source/src/core/MatchmakingService.cpp`, `SkyrimNetDecisions.cpp`, or `SkyrimNetIntegration.cpp`.
   - **Strict Input Context**: Any new `{{ variable }}` referenced in a decision or evaluation prompt must be populated in the JSON context object passed from C++.

