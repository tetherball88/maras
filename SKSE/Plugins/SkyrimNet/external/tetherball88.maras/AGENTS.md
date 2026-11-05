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

## 2. Prompt Templates (`prompts/`)

- **Decorator-First Data Access**:
  - Never hardcode relationship states, archetypes, or family assumptions in `.prompt` files.
  - Always query runtime state via the C++ decorators registered in `SKSE_Source/src/core/SkyrimNetIntegration.cpp` (such as `maras_is_enabled()`, `maras_get_actor_summary(actor)`, `maras_get_all_relationships(actor)`, `maras_get_affair_secrets(actor)`, `maras_get_children_info(actor)`, `maras_get_household_context(actor)`, `maras_get_marriage_difficulty(actor)`, `maras_get_deceased_partners(actor)`).
- **Guard with `maras_is_enabled`**:
  - Top-level prompt submodules (`submodules/character_bio/0450_maras_character_bio.prompt`, `submodules/guidelines/0900_maras_guidelines.prompt`) must remain wrapped in `{% if maras_is_enabled() %}` so nothing is emitted if M.A.R.A.S is inactive.
- **Relative Template Includes**:
  - Internal template `render_template(...)` or include calls resolve relative to `prompts/`.

---

## 3. C++ Evaluation & Jev Decision Prompts (`prompts/decisions/` and `prompts/maras_evaluations/`)

M.A.R.A.S uses two categories of C++-invoked prompts:

1. **Jev Decision Prompts (`prompts/decisions/*.prompt`)**:
   - Invoked via `PublicSendCustomDecisionToLLM` (`SkyrimNetDecisions.cpp`) when `skyrimnetUseDecisions` is enabled (`true` by default):
     - `maras_step_in_evaluation.prompt` — Evaluates whether a biological father steps in after a paternity-driven divorce (`[DECISION: step_in]`).
     - `maras_paternity_evaluation.prompt` — Evaluates paternity secret exposure and partner reaction (`[DECISION: mother_confesses]`, `[DECISION: partner_reaction]`, `[DECISION: presumed_discovers]`, `[DECISION: bio_father_learns]`).
     - `maras_player_partner_cheating.prompt` — Evaluates Tier 2 Player-Partner Agency across all 5 `evalMode`s (`candidate_select`, `breakup_affair`, `breakup_open_lover`, `suggest_open`, `suggest_close`).
     - `maras_matchmaking_verify.prompt` — Verifies proposed matchmaking transitions using per-actor consent (`willing_a && willing_b` for `new`/`advance`/`open`/`close`) or unilateral initiation (`wants_breakup_a || wants_breakup_b` for `breakup`).
   - **Strict Jev Schema Formatting**: Every `[DECISION: key]` block after `# DECISION SCHEMA` must keep each option (`option_key: Single-line criterion`) on a single line with no embedded newlines, colons in keys, or brackets. Never rename decision keys or option keys without updating `SKSE_Source/src/core/SkyrimNetDecisions.cpp`.

2. **Chat Evaluation Prompts (`prompts/maras_evaluations/*.prompt`)**:
   - Invoked via `PublicSendCustomPromptToLLM` for open-ended JSON generation (`maras_matchmaking_scheduled.prompt`, `maras_matchmaking_manual.prompt`, `maras_event_memories.prompt`, `maras_npc_couple_banter.prompt`) and as automatic fallbacks when `skyrimnetUseDecisions` is disabled or SkyrimNet's `decisions` model slot returns `"error": "no_decisions_route"`.
   - **Strict Output Schema**: Never rename or remove required JSON output keys instructed in `prompts/maras_evaluations/*.prompt` without simultaneously updating the corresponding C++ response parser in `SKSE_Source/src/core/MatchmakingService.cpp`, `SkyrimNetDecisions.cpp`, or `SkyrimNetIntegration.cpp`.
   - **Strict Input Context**: Any new `{{ variable }}` referenced in a decision or evaluation prompt must be populated in the JSON context object passed from C++.

