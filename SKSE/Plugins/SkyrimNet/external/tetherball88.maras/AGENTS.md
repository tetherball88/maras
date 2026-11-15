# AGENTS.md — SkyrimNet External Bundle (`tetherball88.maras`)

Applies to `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/` (`manifest.json`, `actions/`, `triggers/`, `prompts/`). Supplements root `AGENTS.md`.

---

## 1. Bundle Structure & YAML Parity (SkyrimNet Beta 25+)

- **`manifest.json`**: Must remain valid JSON at the bundle root.
- **`actions/*.yaml` (18 files)**: Filename **must** match internal `name:` (case-sensitive, e.g., `ProposeLover.yaml` $\to$ `name: ProposeLover`), pointing `scriptName` + `executionFunctionName` to valid `TTM_Actions.psc` handlers (which delegate to `MARAS.Execute*Action`; `ManageRelationships.yaml` uses `dynamic`).
- **`triggers/*.yaml` (25 files)**: Filename **must** match internal `name:` (e.g., `ttm_cheating_caught.yaml` $\to$ `name: ttm_cheating_caught`), listening on `eventType: ttm_event` and filtering `event_json.type` serialized in C++ by `SkyrimNetEventPayloads`.
- **Event-Side Secrecy Parity**: Child birth (`ChildBirth`), growth (`ChildGrewUp`), training (`ChildSentToTraining`), adulthood (`ChildBecameAdult`), and paternity-resolution (`PaternitySecretResolved`) events planned in C++ by `Projection::EventDispatchPlan` and dispatched by `SkyrimNetEventService` check both `isSecretForPartner` and `isSecretForBioFather` using `exactAudience = true` and `DispatchMaskMode` (`MaskForPartner` / `MaskForChildOnly`) splits—never weaken prompt isolation assuming all targets are `Nearby`.

---

## 2. Prompt Templates (`prompts/`) & `dialogue_target` Isolation

- **Decorator-First & Registration Guard**: Never hardcode state in `.prompt` files—query C++ decorators (`ttm_is_registered`, `ttm_npc_relationship_profile`, `ttm_get_*_candidates`). Guard bio submodules (`0602_maras_relations.prompt`, `submodules/maras_relations/*.prompt`) with `{% if ttm_is_registered(actorUUID) %}`. Relative template includes resolve from `prompts/`.
- **`render_mode == "dialogue_target"` (Public Knowledge Only)**: Even when `actorUUID` personally knows a secret, `dialogue_target` **must never** emit:
  - Secret affairs (`is_secret_affair` in `0030_partners.prompt` / `0040_ex_partners.prompt`; suppress empty headers).
  - Undiscovered custodial/adopted paternity secrets (`has_undiscovered_paternity_secret`, `"stepdaughter"`/`"stepson"`, `step_parents`, secret `bio_father` in `0020_children.prompt` & `0001_child_bio.prompt`—watch `elif` fallthroughs).
  - Non-custodial secret paternity (`is_paternity_secret`, `is_other_parent_secret` in `0020_children.prompt`; substitute `public_other_parent`).
  - Secret half-siblings (`parents.siblings_public` only) or `has_beastblood` (`0001_child_bio.prompt`).
- **`UNANNOUNCED` Pregnancies**: Action prompts (`maras_action_discover_child_paternity.prompt`) must instruct `{{speakingNpc.name}}` that an `UNANNOUNCED` candidate means they have **zero prior knowledge** unless the mother explicitly reveals it in the current exchange.

---

## 3. Jev Decision & Chat Evaluation Prompts

1. **Jev Decision Prompts (`prompts/decisions/*.prompt` $\leftrightarrow$ [SkyrimNetDecisions.cpp](../../../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetDecisions.cpp))**:
   - Used via `PublicSendCustomDecisionToLLM` when `skyrimnetUseDecisions` is enabled (`maras_step_in_evaluation`, `maras_paternity_evaluation`, `maras_player_partner_cheating` across all 5 `evalMode`s, `maras_matchmaking_verify`).
   - **Strict Jev Syntax**: Every `[ question <id> choice|noul ]` block **must** have a non-empty instruction line immediately before `[ criteria ]`, and every criterion (`key: Single-line description` or `true:`/`false:`) must be single-line with no colons in keys or brackets. Never rename question/option IDs without updating `SkyrimNetDecisions.cpp`.
2. **Chat Evaluation Prompts (`prompts/maras_evaluations/*.prompt`)**:
   - Used via `PublicSendCustomPromptToLLM` (`maras_matchmaking_scheduled`, `maras_matchmaking_manual`, `maras_event_memories`, `maras_npc_couple_banter`, plus decision fallbacks on `"no_decisions_route"`).
   - Keep JSON output keys and `{{ variable }}` inputs strictly synchronized with C++ callers/parsers (`modules/app/src/AgencyWorkflowCoordinator.cpp`, `modules/app/src/AsyncLlmBatchRunner.cpp`, `modules/bridge_skyrimnet/src/SkyrimNetDecisions.cpp`, `modules/bridge_skyrimnet/src/SkyrimNetDecorators.cpp`, `modules/bridge_skyrimnet/src/SkyrimNetMemoryService.cpp`).
