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

## 3. C++ Evaluation Prompt JSON Contracts (`prompts/maras_evaluations/`)

The prompt templates in `prompts/maras_evaluations/` (`matchmaking_global.prompt`, `matchmaking_manual.prompt`, `matchmaking_verification.prompt`, `matchmaking_memory_batch.prompt`, `paternity_secret_eval.prompt`, etc.) are invoked asynchronously by C++ (`MatchmakingService` and `NPCRelationshipManager`) via `PublicSendCustomPrompt` and parsed by C++ JSON parsers (`nlohmann::json`):

- **Strict Output Schema**: Never rename or remove required JSON output keys instructed in `prompts/maras_evaluations/*.prompt` without simultaneously updating the corresponding C++ response parser in `SKSE_Source/src/core/MatchmakingService.cpp` or `NPCRelationshipManager.cpp`.
- **Strict Input Context**: Any new `{{ variable }}` referenced in an evaluation prompt must be populated in the JSON context object passed to `RenderTemplate` in C++.

