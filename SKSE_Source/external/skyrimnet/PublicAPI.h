// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Fateless.Ai. SkyrimNet's plugin API headers are MIT licensed: include them
// in any mod, open or closed source. SkyrimNet itself is not open source.

#pragma once
#include <string>
#include <windows.h>
#include <functional>
#include "PublicAPIMemoryQuery.h"
#include "PublicAPIDiaryQuery.h"

/**
 * SkyrimNet Public API — loaded at runtime via LoadLibraryA + GetProcAddress.
 *
 * Drop this header into your SKSE plugin project and call FindFunctions() once during kDataLoaded.
 * Action registration works immediately; data queries return empty results until a save is loaded
 * (check PublicIsMemorySystemReady()).
 *
 * Quick start:
 * @code
 *   // In your SKSE kDataLoaded handler — resolve function pointers.
 *   void OnDataLoaded() {
 *       if (!FindFunctions()) {
 *           logger::warn("SkyrimNet not found");
 *           return;
 *       }
 *       logger::info("SkyrimNet API v{}", PublicGetVersion());
 *
 *       // Register actions here — they're available before a save loads.
 *       PublicRegisterCPPAction("MyMod_Wave", "Wave at someone",
 *           [](RE::Actor* a) { return a != nullptr; },
 *           [](RE::Actor* a, std::string params) { return true; },
 *           "", "CPP", 50, "{}", "", "", "");
 *   }
 *
 *   // Query data later, after a save is loaded.
 *   void SomeGameplayFunction(uint32_t formId) {
 *       if (!PublicIsMemorySystemReady || !PublicIsMemorySystemReady()) return;
 *       std::string memories = PublicGetMemoriesForActor(formId, 10, "");
 *       std::string events   = PublicGetRecentEvents(formId, 20, "dialogue");
 *   }
 * @endcode
 *
 * ABI requirement: std::string and std::function cross the DLL boundary, so build with the same MSVC
 * toolset as SkyrimNet (Visual Studio 2022, v143). SkyrimNet links the static CRT (/MT); matching it
 * is the safe choice.
 *
 * The function pointers below are inline variables, so this header can be included from any number of
 * .cpp files. Copies from SkyrimNet 0.29 and earlier are not: include those from one .cpp only.
 *
 * Thread safety: All data query functions (v3+) are thread-safe and return
 * empty results gracefully if called before the database is ready. Action
 * registration should be done during plugin initialization only.
 */
extern "C" {

// =============================================================================
// Core (v2+)
// =============================================================================

/**
 * Returns the runtime API version (currently 15).
 * Version history: 2 = action registration, 3 = data queries + UUID + config,
 *                  4 = diary queries,
 *                  5 = decorator registration + event callbacks + memory creation,
 *                  6 = actor busy state,
 *                  7 = save unique ID + world knowledge CRUD,
 *                  8 = send custom prompt to LLM,
 *                  9 = per-actor world knowledge for prompt enrichment,
 *                 10 = filtered memory queries,
 *                 11 = timeline state, filtered diary queries, diary add/edit/delete,
 *                      memory edit/delete, actor search, event registration with an audience,
 *                 12 = send custom decision request to the decision model,
 *                 13 = named places (add, remove, sync, list, place at),
 *                 14 = dynamic profile update request with optional instructions and updatable block overrides,
 *                 15 = runtime actor succession (identity migration across FormIDs/UUIDs).
 */
inline int (*PublicGetVersion)() = nullptr;

/**
 * Register a custom action that NPCs can perform via the LLM action system.
 *
 * @param name                  Unique action identifier (e.g., "MyMod_DoThing").
 * @param description           Human-readable description for LLM context.
 * @param eligibleCallback      Called to test if an NPC can perform this action.
 *                              Receives the NPC's Actor*. Must be thread-safe.
 * @param executeCallback       Called when the NPC executes this action.
 *                              Receives Actor* and a JSON params string.
 *                              Return true on success.
 * @param triggeringEventTypesCSV  Comma-separated event types that trigger
 *                              eligibility checks (e.g., "combat_hit,dialogue").
 * @param categoryStr           Where the action sits: "CPP" for a top-level action, or
 *                              "CPP_NESTED_ACTION" for one inside the category named by
 *                              customCategory. Any other value is treated as "CPP".
 * @param priority              Execution priority (higher = checked first). 50
 *                              is a reasonable default.
 * @param parameterSchemaJSON   JSON Schema describing parameters your action
 *                              accepts. The LLM uses this to generate params.
 *                              Pass "{}" if no parameters.
 * @param customCategory        For "CPP_NESTED_ACTION": the category it belongs to (the
 *                              customCategory a PublicRegisterCPPSubCategory call used).
 *                              Empty for a top-level action.
 * @param customParentCategory  Parent category for nesting (empty = top-level).
 * @param tagsCSV               Comma-separated tags for filtering.
 * @return true if registration succeeded.
 */
inline bool (*PublicRegisterCPPAction)(const std::string name, const std::string description, std::function<bool(RE::Actor *)> eligibleCallback,
                                std::function<bool(RE::Actor *, std::string json_params)> executeCallback,
                                const std::string triggeringEventTypesCSV, std::string categoryStr, int priority,
                                std::string parameterSchemaJSON, std::string customCategory, std::string customParentCategory,
                                std::string tagsCSV) = nullptr;

/**
 * Register an action subcategory for organizational grouping.
 *
 * Subcategories appear as folders in the action tree. They don't execute
 * on their own but provide structure for related actions.
 *
 * @param name                  Unique subcategory identifier.
 * @param description           Human-readable description.
 * @param eligibleCallback      Controls visibility — shown only when this
 *                              returns true for the current NPC.
 * @param triggeringEventTypesCSV  Comma-separated triggering event types.
 * @param priority              Display priority within the parent category.
 * @param parameterSchemaJSON   Reserved, pass "{}".
 * @param customCategory        The key actions use to join this category. Usually the
 *                              same as name.
 * @param customParentCategory  Parent category (empty = top-level).
 * @param tagsCSV               Comma-separated tags.
 * @return true if registration succeeded.
 */
inline bool (*PublicRegisterCPPSubCategory)(const std::string name, const std::string description,
                                     std::function<bool(RE::Actor *)> eligibleCallback, const std::string triggeringEventTypesCSV,
                                     int priority, std::string parameterSchemaJSON, std::string customCategory,
                                     std::string customParentCategory, std::string tagsCSV) = nullptr;

// =============================================================================
// UUID Resolution (v3+)
// =============================================================================

/**
 * Convert a Skyrim FormID to SkyrimNet's internal UUID.
 * @param formId  Actor FormID (e.g., 0x00000014 for the player).
 * @return UUID, or 0 if the actor is unknown.
 */
inline uint64_t (*PublicFormIDToUUID)(uint32_t formId) = nullptr;

/**
 * Convert a SkyrimNet UUID back to a Skyrim FormID.
 * @return FormID, or 0 if unknown.
 */
inline uint32_t (*PublicUUIDToFormID)(uint64_t uuid) = nullptr;

/**
 * Get an actor's display name from their UUID.
 * @return Actor name, or "" if unknown.
 */
inline std::string (*PublicGetActorNameByUUID)(uint64_t uuid) = nullptr;

// =============================================================================
// Bio Template (v3+)
// =============================================================================

/**
 * Get the bio template name assigned to an actor.
 * @param formId  Actor FormID.
 * @return Template name, or "" if none assigned.
 */
inline std::string (*PublicGetBioTemplateName)(uint32_t formId) = nullptr;

// =============================================================================
// Data Queries (v3+)
//
// All functions in this section are thread-safe.
// Functions returning arrays return "[]" on error.
// Functions returning objects return a default JSON object on error.
// =============================================================================

/**
 * Retrieve memories stored for an actor.
 *
 * @param formId       Actor FormID.
 * @param maxCount     Maximum memories to return (<=0 defaults to 50).
 * @param contextQuery If non-empty, performs semantic (vector) search ranked
 *                     by relevance. If empty, returns most recent first.
 *
 * @return JSON array of memory objects:
 * @code
 * [
 *   {
 *     "id": 42,
 *     "text": "I saw the Dragonborn defeat a dragon at Whiterun",
 *     "importance": 0.85,
 *     "timestamp": 1234.5,
 *     "type": "observation"
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetMemoriesForActor)(uint32_t formId, int maxCount, const char* contextQuery) = nullptr;

/**
 * Retrieve memories for an actor with explicit filtering and ordering (v10+). Filters apply in SQL
 * before truncation. Prefer the typed QueryMemoriesForActor() wrapper at the bottom of this header.
 *
 * @param formId    Actor FormID.
 * @param queryJSON JSON filter object. Empty or "{}" behaves like PublicGetMemoriesForActor(formId, 50, "").
 * @return The same JSON array shape PublicGetMemoriesForActor returns; "[]" on error or malformed JSON.
 */
inline std::string (*PublicQueryMemoriesForActor)(uint32_t formId, const char* queryJSON) = nullptr;

/**
 * Retrieve recent world events, optionally filtered.
 *
 * @param formId          Actor FormID (0 = all events, non-zero = events
 *                        involving this actor).
 * @param maxCount        Maximum events to return (<=0 defaults to 50).
 * @param eventTypeFilter Comma-separated event types to include
 *                        (e.g., "dialogue,direct_narration"). Empty = all.
 *
 * @return JSON array of event objects. The event's content is in `data`, parsed as JSON when it is
 *         JSON (a dialogue line is {"dialogue": "..."}), otherwise the raw string. Actors are UUIDs;
 *         the two *Name fields are added for convenience.
 * @code
 * [
 *   {
 *     "id": 4211,
 *     "type": "dialogue",
 *     "data": { "dialogue": "Hello, traveler!" },
 *     "location": "Whiterun",
 *     "actors": [1234..., 5678...],
 *     "originatingActor": 1234...,
 *     "targetActor": 5678...,
 *     "originatingActorName": "Lydia",
 *     "targetActorName": "Player",
 *     "gameTime": 1234.5,
 *     "gameTimeStr": "...",
 *     "localTime": 1727000000.0,
 *     "localTimeStr": "...",
 *     "playtime": 3600.0,
 *     "gameTimeEnd": null
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetRecentEvents)(uint32_t formId, int maxCount, const char* eventTypeFilter) = nullptr;

/**
 * Retrieve recent dialogue between the player and an NPC.
 *
 * @param formId       NPC's FormID.
 * @param maxExchanges Maximum exchanges to return (<=0 defaults to 10).
 *
 * @return JSON array in chronological order (oldest first). `speaker` is the role ("player" or
 *         "npc"), not a name; the NPC's name is in `npcName` and the line in `data`:
 * @code
 * [
 *   {
 *     "speaker": "player",
 *     "npcName": "Lydia",
 *     "data": "What do you think about the war?",
 *     "gameTime": 1234.5
 *   },
 *   {
 *     "speaker": "npc",
 *     "npcName": "Lydia",
 *     "data": "I follow you, my Thane.",
 *     "gameTime": 1234.6
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetRecentDialogue)(uint32_t formId, int maxExchanges) = nullptr;

/**
 * Get info about the most recent NPC who spoke to the player.
 *
 * @return JSON object:
 * @code
 * {
 *   "npcFormId": 655544,
 *   "gameTime": 1234.5,
 *   "npcName": "Lydia"
 * }
 * @endcode
 */
inline std::string (*PublicGetLatestDialogueInfo)() = nullptr;

/**
 * Check if the memory/database system is ready for queries. False until a save is loaded or a new game starts.
 * @return true if the database is ready.
 */
inline bool (*PublicIsMemorySystemReady)() = nullptr;

/**
 * Get per-actor engagement statistics for scoring and prioritization.
 *
 * @param maxCount             Max actors to return (0 = all with any activity).
 * @param excludePlayer        Omit the player (FormID 0x14) from results.
 * @param playerEventsOnly     Only count events involving the player. NPC-to-NPC
 *                             events still tracked as npcToNpcEventCount.
 * @param shortWindowSeconds   Short recency window in game-seconds
 *                             (e.g., 86400 = 1 game-day).
 * @param mediumWindowSeconds  Medium recency window in game-seconds
 *                             (e.g., 604800 = 7 game-days).
 *
 * @return JSON array:
 * @code
 * [
 *   {
 *     "formId": 655544,
 *     "name": "Lydia",
 *     "memoryCount": 12,
 *     "totalMemoryImportance": 8.5,
 *     "recentMemoryImportanceShort": 2.1,
 *     "recentMemoryImportanceMedium": 5.3,
 *     "eventCount": 30,
 *     "recentEventCountShort": 5,
 *     "recentEventCountMedium": 18,
 *     "lastEventTime": 1234.5,
 *     "npcToNpcEventCount": 4
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetActorEngagement)(int maxCount, bool excludePlayer, bool playerEventsOnly, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

/**
 * Get actors related to a given actor via shared event history.
 *
 * @param formId               Anchor actor's FormID.
 * @param maxCount             Max related actors to return (0 = all).
 * @param shortWindowSeconds   Short recency window in game-seconds.
 * @param mediumWindowSeconds  Medium recency window in game-seconds.
 *
 * @return JSON array:
 * @code
 * [
 *   {
 *     "formId": 655545,
 *     "name": "Ulfric Stormcloak",
 *     "sharedEventCount": 15,
 *     "recentSharedEventsShort": 3,
 *     "recentSharedEventsMedium": 10,
 *     "lastSharedEventTime": 1234.5
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetRelatedActors)(uint32_t formId, int maxCount, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

/**
 * Get comprehensive player context: current time, recent interactions,
 * and relationship data.
 *
 * @param withinGameHours  Time window in game-hours for recent interactions.
 *                         0 = all time.
 *
 * @return JSON object:
 * @code
 * {
 *   "currentTime": 1234.5,
 *   "recentInteractionNames": ["Lydia", "Farengar"],
 *   "relationships": [
 *     {
 *       "name": "Lydia",
 *       "formId": 655544,
 *       "interactionCount": 12
 *     }
 *   ]
 * }
 * @endcode
 */
inline std::string (*PublicGetPlayerContext)(float withinGameHours) = nullptr;

/**
 * Get NPC-to-NPC event pair counts within a candidate pool.
 *
 * @param formIdListCSV   Comma-separated FormIDs defining the pool
 *                        (e.g., "655544,655545,655546").
 * @param minSharedEvents Minimum shared events to include a pair (0 = all).
 *
 * @return JSON array:
 * @code
 * [
 *   {
 *     "formId1": 655544,
 *     "formId2": 655545,
 *     "sharedEvents": 8
 *   }
 * ]
 * @endcode
 */
inline std::string (*PublicGetEventPairCounts)(const char* formIdListCSV, int minSharedEvents) = nullptr;

// =============================================================================
// Diary Queries (v4+)
// =============================================================================

/**
 * Retrieve diary entries for an actor, optionally filtered by time range.
 *
 * @param formId    Actor FormID (0 = all actors).
 * @param maxCount  Maximum entries to return (<=0 defaults to 50).
 * @param startTime Only entries at or after this time (seconds since epoch).
 *                  0.0 = no lower bound.
 * @param endTime   Only entries at or before this time (seconds since epoch).
 *                  0.0 = no upper bound.
 *
 * @return JSON array of diary entry objects. Each entry includes all fields
 *         from DiaryEntry::ToJson() plus an `actor_name` field. v11+ adds
 *         `updated_at` (seconds since epoch of the last edit, null if never edited).
 */
inline std::string (*PublicGetDiaryEntries)(uint32_t formId, int maxCount, double startTime, double endTime) = nullptr;

// =============================================================================
// Memory Creation (v5+)
// =============================================================================

/**
 * Create and store a first-class memory (with vector embedding) for an actor.
 *
 * @param formId             Actor FormID (the memory owner).
 * @param contentText        Memory content text (embedded for semantic search).
 * @param importance         Importance score, clamped to 0.0-1.0.
 * @param memoryType         One of: "EXPERIENCE", "RELATIONSHIP", "KNOWLEDGE",
 *                           "LOCATION", "SKILL", "TRAUMA", "JOY".
 * @param emotion            Optional emotion tag (e.g., "happy", "anxious").
 * @param location           Optional location name (e.g., "Whiterun").
 * @param tagsJSON           Optional JSON array of tags: ["combat", "dragon"].
 * @param relatedActorsJSON  Optional JSON array of related actor FormIDs.
 * @return Memory ID (>0) on success, 0 on error.
 */
inline int (*PublicAddMemory)(uint32_t formId, const char* contentText, float importance,
                       const char* memoryType, const char* emotion, const char* location,
                       const char* tagsJSON, const char* relatedActorsJSON) = nullptr;

// =============================================================================
// Plugin Configuration (v3+)
// =============================================================================

/**
 * Get the full JSON configuration for a registered plugin.
 *
 * Plugins register YAML config files under "Plugin_<name>" in ConfigManager.
 *
 * @param pluginName  Plugin name (without "Plugin_" prefix).
 * @return JSON object of the config, or "{}" if not found.
 */
inline std::string (*PublicGetPluginConfig)(const char* pluginName) = nullptr;

/**
 * Get a single config value by dot-path from a plugin's settings.
 *
 * @param pluginName    Plugin name (without "Plugin_" prefix).
 * @param path          Dot-separated path (e.g., "feature.enabled").
 * @param defaultValue  Returned if the path doesn't exist.
 * @return The config value as a string, or defaultValue if not found.
 */
inline std::string (*PublicGetPluginConfigValue)(const char* pluginName, const char* path, const char* defaultValue) = nullptr;

// =============================================================================
// Decorator Registration (v5+)
// =============================================================================

/**
 * Register a custom decorator for Inja templates ({{ my_decorator(actor_uuid) }}) and action eligibility rules.
 *
 * @param name         Unique decorator identifier (e.g., "intel_standing").
 *                     Must not conflict with built-in decorators.
 * @param description  Human-readable description (for documentation generation).
 * @param callback     Function receiving RE::Actor* and returning a string value.
 *                     Must be thread-safe. Return "" for invalid/unknown actors.
 *
 * @return true if registration succeeded, false if name conflicts or callback is null.
 *
 * @code
 *   // Register during plugin initialization (kDataLoaded):
 *   PublicRegisterDecorator("intel_standing", "Player's standing with this NPC's faction",
 *       [](RE::Actor* actor) -> std::string {
 *           auto* fp = FactionPolitics::GetSingleton();
 *           auto factionId = fp->GetNPCFactionId(actor);
 *           if (factionId.empty()) return "";
 *           return std::to_string(fp->GetPlayerStanding(factionId));
 *       });
 *
 *   // Use in Inja templates:
 *   //   {{ intel_standing(actor_uuid) }}
 *   //
 *   // Use in action YAML eligibility rules:
 *   //   eligibilityRules:
 *   //     - conditions:
 *   //       - decoratorName: intel_standing
 *   //         arguments: ["currentActor"]
 *   //         comparisonOperator: ">="
 *   //         expectedValue: 20
 * @endcode
 */
inline bool (*PublicRegisterDecorator)(const char* name, const char* description,
                                std::function<std::string(RE::Actor*)> callback) = nullptr;

/**
 * Check if a decorator with the given name exists (built-in or external).
 *
 * @param name  Decorator name to check.
 * @return true if registered, false otherwise.
 */
inline bool (*PublicHasDecorator)(const char* name) = nullptr;

// =============================================================================
// Event Callbacks (v5+)
// =============================================================================

/**
 * Register a callback for a specific event type (e.g., "dialogue", "combat", "death").
 *
 * The callback fires on SkyrimNet's ThreadPool whenever an event of the specified type
 * is registered. The callback receives a JSON string with event data:
 *   {
 *     "type": "dialogue",
 *     "data": "{\"text\":\"...\",\"speaker\":\"Lydia\"}",
 *     "originatingActorUUID": 12345,
 *     "targetActorUUID": 67890,
 *     "originatingActorFormId": 1234,
 *     "targetActorFormId": 5678,
 *     "id": 42
 *   }
 *
 * Common event types: "dialogue", "dialogue_player_text", "dialogue_player_stt",
 * "combat", "death", "hit", "activation", "spell_cast", "equip"
 *
 * @param eventType  Event type to listen for. Must not be empty.
 * @param callback   Function receiving JSON string. Must be thread-safe.
 *                   Called on ThreadPool — do NOT call RE:: functions directly.
 *                   The const char* is only valid for the duration of the call — copy if needed.
 * @return Callback ID (> 0) for unregistration, or 0 on failure.
 *
 * @code
 *   auto id = PublicRegisterEventCallback("dialogue", [](const char* json) {
 *       auto j = nlohmann::json::parse(json);
 *       uint32_t formId = j["originatingActorFormId"].get<uint32_t>();
 *       // increment counter, check threshold, etc.
 *   });
 * @endcode
 */
inline uint64_t (*PublicRegisterEventCallback)(const char* eventType,
                                        std::function<void(const char*)> callback) = nullptr;

/**
 * Unregister a previously registered event callback.
 *
 * @param callbackId  The ID returned by PublicRegisterEventCallback.
 * @return true if the callback was found and removed.
 */
inline bool (*PublicUnregisterEventCallback)(uint64_t callbackId) = nullptr;

// =============================================================================
// Actor Busy State (v6+)
// =============================================================================

/**
 * Mark an actor as busy with a multi-step action. While busy, is_busy() returns true and busy_reason()
 * returns the reason string for eligibility rules. The plugin must call PublicClearActorBusy() when done.
 *
 * @param formId   The actor's FormID.
 * @param reason   Short reason string (e.g., "arrest", "travel", "crafting").
 * @return true on success, false if the FormID couldn't be resolved.
 */
inline bool (*PublicSetActorBusy)(uint32_t formId, const char* reason) = nullptr;

/**
 * Clear an actor's busy state.
 * @return true on success, false if the FormID couldn't be resolved.
 */
inline bool (*PublicClearActorBusy)(uint32_t formId) = nullptr;

/** Check if an actor is currently busy. */
inline bool (*PublicIsActorBusy)(uint32_t formId) = nullptr;

// =============================================================================
// Current save UUID (v7+)
// =============================================================================

/**
 * Get the unique save ID for the current save.
 * @return the unique save ID, empty string if a save is not loaded.
*/
inline std::string (*PublicGetSaveUniqueID)() = nullptr;

// =============================================================================
// World Knowledge (v7+)
// =============================================================================

/**
 * Create a world knowledge entry: a shared fact retrieved for any NPC whose condition expression is true.
 *
 * @param content       Knowledge text (required, non-empty). Embedded for semantic search.
 * @param conditionExpr Inja condition expression controlling which NPCs receive this.
 *                      Examples:
 *                        - "" (empty = applies to all NPCs)
 *                        - "is_in_faction(actorUUID, \"CompanionsFaction\")"
 *                        - "decnpc(actorUUID).race == \"Nord\""
 *                        - "is_in_npc_group(actorUUID, \"MyGroup\")"
 *                        - "get_quest_stage(\"MQ101\") >= 50"
 * @param alwaysInject  If true, always included when condition passes (deterministic).
 *                      If false, only surfaces via semantic search (probabilistic).
 * @param importance    Importance score 0.0-1.0 (clamped). Affects semantic ranking.
 * @param displayName   Optional human-readable label. Pass "" to omit.
 * @return Memory ID (>0) on success, 0 on error.
 *
 * @code
 *   // All Companions learn about an event:
 *   int id = PublicAddWorldKnowledge(
 *       "Silver Hand attacked Jorrvaskr last night.",
 *       "is_in_faction(actorUUID, \"CompanionsFaction\")",
 *       true, 0.8, "Silver Hand Attack");
 *
 *   // Everyone learns about the dragon:
 *   PublicAddWorldKnowledge(
 *       "A dragon was slain at the Western Watchtower.",
 *       "", true, 0.9, "Dragon Attack");
 * @endcode
 */
inline int (*PublicAddWorldKnowledge)(const char* content, const char* conditionExpr,
                               bool alwaysInject, float importance,
                               const char* displayName) = nullptr;

/**
 * Remove a world knowledge entry by ID.
 * Refuses to delete per-actor memories (actor_uuid != 0).
 *
 * @param memoryId  The ID returned by PublicAddWorldKnowledge.
 * @return true on success, false if not found or not a world knowledge entry.
 */
inline bool (*PublicRemoveWorldKnowledge)(int memoryId) = nullptr;

/**
 * Get all world knowledge entries as a JSON array.
 *
 * @param maxCount  Maximum entries to return (<=0 defaults to 100).
 * @return JSON array of knowledge entry objects. Each includes id, content,
 *         condition_expr, always_inject, importance, display_name, is_active.
 */
inline std::string (*PublicGetWorldKnowledge)(int maxCount) = nullptr;

/**
 * Get world knowledge entries applicable to a specific actor (the C++ side of the get_world_knowledge() decorator).
 * An empty searchQuery returns always-inject entries only (cheap, no HNSW); a non-empty one adds semantic search.
 *
 * @param formId       Actor FormID. 0 returns "[]".
 * @param maxResults   Maximum entries to return (<=0 defaults to 5).
 * @param searchQuery  Semantic search query, or "" for always-inject only.
 * @return JSON array: [{"content":"...","always_inject":true,"importance":0.8,"display_name":"..."}, ...]. "[]" if none.
 */
inline std::string (*PublicGetWorldKnowledgeForActor)(uint32_t formId, int maxResults,
                                               const char* searchQuery) = nullptr;

// =============================================================================
// Custom LLM Prompts (v8+)
// =============================================================================

/**
 * Render the named prompt template, send it to the LLM, and call the callback on a ThreadPool worker.
 * The callback must be thread-safe; do not call RE:: functions from it.
 *
 * @param promptName   Template name registered with ContextEngine (required).
 * @param variant      OpenRouter variant to use. Pass "" for the default variant.
 * @param contextJson  JSON object of context variables injected before rendering.
 *                     Pass "" or "{}" if no extra variables are needed.
 *                     Example: "{\"actorName\":\"Lydia\",\"topic\":\"dragons\"}"
 * @param callback     Called on completion with (response, success).
 *                     success == 1 on success, 0 on failure.
 *                     response is the LLM text, or an error string on failure.
 *                     The const char* is only valid for the call — copy if needed.
 * @return true if the task was successfully queued, false on immediate error.
 *
 * @code
 *   PublicSendCustomPromptToLLM("my_prompt", "", "{\"name\":\"Lydia\"}",
 *       [](const char* response, int success) {
 *           if (success) logger::info("LLM: {}", response);
 *       });
 * @endcode
 */
inline bool (*PublicSendCustomPromptToLLM)(const char* promptName, const char* variant,
                                    const char* contextJson,
                                    std::function<void(const char* response, int success)> callback) = nullptr;

// =============================================================================
// Timeline State, Diary Writes, Memory Edits and Actor Search (v11+)
// =============================================================================

/**
 * Outcome of the keep/clear timeline check SkyrimNet runs after a save load.
 *
 * @return 0 = none (no load yet, new game, or the save had no future history),
 *         1 = pending (check running, prompt open, or a Clear still deleting),
 *         2 = kept, 3 = cleared (all future events, memories and diary entries deleted).
 *         While 1, diary and event queries may still return history a Clear is about to delete,
 *         and the diary write functions below refuse (return 0/false) until it resolves.
 *         When the check finishes SkyrimNet fires the ModEvent SkyrimNet_TimelineResolved
 *         (numArg = state, JSON {"state", "stateName"}); poll this only if you may miss it.
 */
inline int (*PublicGetTimelineState)() = nullptr;

/**
 * One actor's diary entries with explicit filtering and ordering, the diary counterpart of
 * PublicQueryMemoriesForActor. Filters apply in SQL before truncation. Prefer the typed
 * QueryDiaryEntries() wrapper at the bottom of this header (struct in PublicAPIDiaryQuery.h).
 *
 * Write order: `orderBy: "IdAsc"` with `minId: lastId + 1` reads the entries stored after one you
 * have. Ids are AUTOINCREMENT, so id order is the order entries were stored, whatever their
 * in-game `entry_date`. Guarantees, within one save's database (each playthrough has its own;
 * ids from different playthroughs are not comparable):
 *   - A new entry always gets a higher id than every existing entry. No path inserts an explicit id.
 *   - Ids are never reused, not after a delete and not after a timeline Clear.
 *   - Editing an entry (PublicUpdateDiaryEntry, the dashboard) keeps its id.
 * Ids come from one counter for the whole table, so one actor's ids have gaps (12, 15, 31...).
 * The actor's co-identities (renames, identity links, drifted UUIDs) are merged in. If that set
 * grows later, older entries can appear with ids below ones you have already read.
 *
 * @param formId    Actor FormID (required).
 * @param queryJSON JSON filter object (see DiaryQuery). Empty or "{}" behaves like
 *                  PublicGetDiaryEntries(formId, 50, 0, 0).
 * Unknown keys, mistyped values and unknown orderBy names are ignored, and each one is logged as a
 * DiaryQueryService warning in SkyrimNet.log with the formId, so check the log if results look unfiltered.
 *
 * @return JSON array in the same shape as PublicGetDiaryEntries (id, entry_date, creation_time,
 *         tags, updated_at, actor_name...). "[]" on error or malformed JSON.
 */
inline std::string (*PublicQueryDiaryEntries)(uint32_t formId, const char* queryJSON) = nullptr;

/**
 * Add a diary entry for any actor, e.g. one the player wrote. It is treated like a generated
 * entry: it gets a memory and fires SkyrimNet_DiaryCreated. Blocks while the memory's embedding
 * is computed; avoid the game thread.
 * Like the edit and delete below, it returns 0 (and logs why) while PublicGetTimelineState() is 1
 * or before a save is loaded. Wait for SkyrimNet_TimelineResolved after a load before writing.
 *
 * @param formId    Actor FormID whose diary this is. Must be an actor SkyrimNet has already seen
 *                  (any actor it ever registered, across saves); unknown actors return 0.
 * @param content   Entry text (required).
 * @param entryDate In-game date the entry describes, in the units of `entry_date`. 0.0 = now.
 * @param tagsCSV   Comma-separated tags, stored as given (SkyrimNet adds none). May be null.
 *                  Use them to mark authorship, e.g. "mymod_player_written".
 * @param emotion    Entry emotion, e.g. "hopeful". Shown in the event line "(feeling <emotion>)". May be null.
 * @param importance 0.0-1.0 (clamped); negative = default 0.5. Also the memory's importance.
 * @param location   Where it was written. May be null.
 * @return The new entry id, or 0 on error.
 */
inline int (*PublicAddDiaryEntry)(uint32_t formId, const char* content, double entryDate, const char* tagsCSV,
                           const char* emotion, float importance, const char* location) = nullptr;

/**
 * Edit any diary entry's text and optionally its tags. Emotion, importance and dates are kept.
 * Stamps `updated_at`, rebuilds the entry's memory and fires SkyrimNet_DiaryUpdated.
 * Refused (false) while PublicGetTimelineState() is 1. Blocks while the new text is embedded;
 * avoid the game thread.
 *
 * @param entryId Entry id (`id` in query results).
 * @param content New text (required; delete the entry instead of blanking it).
 * @param tagsCSV New comma-separated tags. null = keep the current tags, "" = clear them.
 * @return true on success.
 */
inline bool (*PublicUpdateDiaryEntry)(int entryId, const char* content, const char* tagsCSV) = nullptr;

/**
 * Delete any diary entry and its memory. Fires SkyrimNet_DiaryDeleted.
 * Refused (false) while PublicGetTimelineState() is 1.
 * @return true on success, false if refused, the entry does not exist, or the delete failed.
 */
inline bool (*PublicDeleteDiaryEntry)(int entryId) = nullptr;

/**
 * Edit an actor memory (`id` from PublicGetMemoriesForActor / PublicQueryMemoriesForActor).
 * Only the keys present change; a new `content` is re-embedded for semantic search. Stamps the
 * memory's `updated_at` and fires SkyrimNet_MemoryUpdated. Blocks while a new `content` is
 * embedded; avoid the game thread.
 *
 * @param memoryId    Memory id.
 * @param changesJSON JSON object with any of:
 *                    "content" (string, non-empty), "importance" (0.0-1.0, clamped), "emotion" (string),
 *                    "location" (string), "tags" (string array, replaces all),
 *                    "type" ("EXPERIENCE", "RELATIONSHIP", "KNOWLEDGE", "LOCATION", "SKILL", "TRAUMA", "JOY"),
 *                    "relatedActorFormIds" (FormID array, replaces all).
 *                    An unknown or mistyped key rejects the whole edit.
 * @return true on success. false (with the reason in SkyrimNet.log) if the memory does not exist,
 *         the JSON is invalid, PublicGetTimelineState() is 1, or the memory can't be edited:
 *         world knowledge (use the world knowledge functions), a plugin-installed row, or a
 *         diary entry's memory (tag "diary_entry"; edit the entry with PublicUpdateDiaryEntry).
 *
 * @code
 *   PublicUpdateMemory(id, R"({"content":"Lydia saved my life at Bleak Falls.","importance":0.9})");
 * @endcode
 */
inline bool (*PublicUpdateMemory)(int memoryId, const char* changesJSON) = nullptr;

/**
 * Delete an actor memory and its embedding. Fires SkyrimNet_MemoryDeleted. A diary entry's memory
 * can be deleted (the entry stays; editing the entry recreates it).
 * @return true on success. false if it does not exist, PublicGetTimelineState() is 1, or it is
 *         world knowledge or plugin-installed.
 */
inline bool (*PublicDeleteMemory)(int memoryId) = nullptr;

/**
 * Search the actors SkyrimNet has resolved (every actor it has registered in this save, loaded or not)
 * by name. Use it to find an actor you can't see, e.g. an address line as the player types.
 * Thread-safe, touches no game objects. Returns "[]" before a save is loaded. Cost grows with the
 * number of known actors: call it off the game thread, and debounce per-keystroke searches.
 *
 * @param nameContains Case-insensitive substring of the actor's name (Unicode-aware). "" = everyone.
 * @param maxCount     Result cap (<=0 = 25, at most 500).
 * @return JSON array, best match first (exact name, then prefix, then a word starting with the
 *         query, then anywhere; ties by name):
 *         [{"uuid": 1234..., "formId": 106097, "name": "Mikael", "isPlayer": false}, ...]
 *         One entry per actor under its current UUID. Duplicate names are all returned; telling
 *         them apart (alive, location, unique base) is up to the caller. `formId` is the runtime
 *         FormID SkyrimNet last saw for the actor, the same value PublicUUIDToFormID returns.
 */
inline std::string (*PublicSearchActors)(const char* nameContains, int maxCount) = nullptr;

/**
 * Register an event, the C++ counterpart of Papyrus SkyrimNetApi.RegisterEvent, optionally perceived by
 * only a chosen list of actors. Persistent unless the event type is configured as ephemeral.
 *
 * Audience, i.e. who has the event in their history, prompts and memories, and who may react to it:
 *   - audienceCount 0: everyone near the originator, plus the player (same as Papyrus RegisterEvent).
 *   - audienceCount > 0: exactly the listed actors plus originator and target. Nobody else nearby, no
 *     virtual NPCs, and not the player unless listed. A `direct_narration` originator still speaks; its
 *     spoken reply is ordinary dialogue that bystanders hear.
 *   Only each character's own history, memories and dialogue context are limited to the audience. The
 *   GameMaster agent, speaker/target selection and combined history views still see the event.
 *
 * @param eventType        A registered event type (`direct_narration`, `persistent_generic`, `custom`, ...).
 * @param content          The type's JSON payload. Plain text is wrapped: `{"narration": ...}` for
 *                         direct_narration, `{"line": ...}` for persistent_generic, `{"description", "data"}`
 *                         for custom.
 * @param originatorFormId Actor the event comes from; 0 = none. Not a loaded actor = ignored (logged).
 * @param targetFormId     Actor it is aimed at; 0 = none.
 * @param audienceFormIds  Actors who perceive it (array of audienceCount FormIDs). Unloaded ones are skipped;
 *                         if none is loaded, nothing is registered (it never falls back to nearby).
 * @param audienceCount    Number of FormIDs in audienceFormIds; 0 = nearby audience.
 * @return The stored event's id, or 0 when nothing was stored: empty type, unresolvable audience, a type that
 *         is disabled or filtered, or an ephemeral type (which only notifies event callbacks).
 *
 * Callable from worker threads, like SkyrimNet's own event registration. FormIDs resolve through live
 * actors, so the originator, target and audience must be loaded.
 *
 * @code
 *   // Only Katarina learns what the letter says; she reacts to it out loud.
 *   uint32_t audience[] = {katarinaFormId};
 *   PublicRegisterEvent("direct_narration", "Cole hands Katarina a letter from Hulda. It reads: ...",
 *                       katarinaFormId, 0, audience, 1);
 * @endcode
 */
inline int (*PublicRegisterEvent)(const char* eventType, const char* content, uint32_t originatorFormId,
                           uint32_t targetFormId, const uint32_t* audienceFormIds, uint32_t audienceCount) = nullptr;

// =============================================================================
// Custom Decision Requests (v12+)
// =============================================================================

/**
 * Ask the decision model a typed question. Render your decision template, send it on SkyrimNet's
 * `decisions` variant, and call the callback on a ThreadPool worker. The callback must be thread-safe;
 * do not call RE:: functions from it.
 *
 * The template is a decision template (`[ state ]` / `[ question <id> choice|score|noul ]` /
 * `[ criteria ]` blocks, not chat sections) under `prompts/decisions/` in your plugin, the same format
 * as SkyrimNet's own decision templates. There is no chat fallback: when the user has no decision
 * provider set up, the callback gets error `no_decisions_route` and nothing is sent, so fall back to
 * PublicSendCustomPromptToLLM yourself if you need an answer anyway.
 *
 * @param templateName     Path under prompts/decisions/, without `.prompt`: "my_mod/is_hostile".
 *                         A leading "decisions/" is accepted.
 * @param contextJson      JSON object of context variables injected before rendering, or "".
 * @param maxProbabilities Likeliest options kept per answer's `probabilities`; <=0 keeps all.
 * @param callback         Called exactly once with (answer, json, success). The const char*s are only valid
 *                         for the call.
 *                         answer: the first question's answer as a string, ready to compare: the chosen key
 *                         (choice), "true"/"false" (noul, yes when p >= 0.5) or the score; "" on failure.
 *                         success == 1: json = {"model":"<dated build>","answers":{"<question id>":{
 *                             "type":"choice","choice":"yes","confidence":0.82,"probabilities":{"yes":0.82,"no":0.18}},
 *                             ...}}. A score answer has "score", a noul (yes/no) answer has "noul" (probability of
 *                         yes); `confidence` and `probabilities` appear only when the model sent them.
 *                         success == 0: {"error":"<code>","message":"..."} with code one of ai_disabled,
 *                         no_decisions_route, template_failed, interface_failed, request_failed,
 *                         cancelled, exception.
 * @return true if the task was queued, false on immediate error (empty name, null callback).
 *
 * @code
 *   PublicSendCustomDecisionToLLM("my_mod/is_hostile", "{\"npcName\":\"Lydia\"}", 3,
 *       [](const char* answer, const char* json, int success) {
 *           if (success && std::string_view(answer) == "yes") logger::info("hostile ({})", json);
 *       });
 * @endcode
 */
inline bool (*PublicSendCustomDecisionToLLM)(const char* templateName, const char* contextJson, int maxProbabilities,
                                      std::function<void(const char* answer, const char* resultJson, int success)> callback) = nullptr;

// =============================================================================
// Named places (v13)
// =============================================================================
//
// Named spots inside a location ("the kitchen") that go in an actor's location string, after its first part, while
// they stand in one. A place is a position in a space (an interior cell's FormID, or the worldspace outdoors) and matches
// within its radius (<= 0 = the default, 350 indoors and 1500 outdoors, clamped to 50-5000) and 200 units up
// or down; the smallest containing place wins. The player's places and the ones plugins ship are `places/` records
// in the content library. Yours live in memory only: send them after every load (PublicSyncPlaces), since a load or
// a new game forgets them. Every place you send belongs to your source, your mod's id (required, never "player" or
// "library"), and is keyed by your own key, unique within it. All mods together hold at most 256.

/**
 * Add or update one of your places by your own key: the place of (source, key) is updated in place.
 * @return false (no source, a reserved one, empty key or name, space 0, 256 places held).
 */
inline bool (*PublicAddPlace)(const char* source, const char* key, const char* name, uint32_t spaceFormId,
                              float x, float y, float z, float radius, uint32_t ownerLocationFormId) = nullptr;

/** Remove one of your places by its key. False when you have none under it. */
inline bool (*PublicRemovePlace)(const char* source, const char* key) = nullptr;

/**
 * Make your places exactly this list: a JSON array of
 * {"key","name","space","x","y","z","radius","ownerLocation"} (key, name and space required). Each entry
 * is upserted by its key and every other place of your source removed, so calling it after every change
 * and after every load keeps SkyrimNet in step with your own data.
 * @return the places kept, or -1 (no source, a reserved one, text that is not an array; nothing changes).
 */
inline int (*PublicSyncPlaces)(const char* source, const char* placesJson) = nullptr;

/**
 * The places of a source ("" = every source, "library" = the content library's): a JSON array of
 * {id, key, name, source, space, ownerLocation, x, y, z, radius}. `id` names a library place (its record's file stem),
 * `key` a mod's.
 */
inline std::string (*PublicListPlaces)(const char* sourceFilter) = nullptr;

/** The place an actor stands in as {"id","key","name","source"}, or "" for none (or the setting off). */
inline std::string (*PublicGetPlaceAt)(uint32_t actorFormId) = nullptr;

// =============================================================================
// Package manager probe (independent of PublicGetVersion)
// =============================================================================

/**
 * Version of SkyrimNet's AI-package contract, resolved by name whatever PublicGetVersion returns; nullptr
 * on a SkyrimNet that predates it. 1 = the Papyrus natives RegisterPackageEx / UnregisterPackageEx /
 * GetPackageOwner / ListPackage / HoldPackages / ReleaseHeldPackages / IsHoldingPackages on SkyrimNetApi and
 * the SkyrimNet_OnPackageAdded / Removed / OnPackagesHeld / Released ModEvents (contract: SkyrimNetApi.psc).
 * There are no C++ package functions; a DLL uses this to know the Papyrus contract is there.
 */
inline uint32_t (*PublicGetPackageApiVersion)() = nullptr;

// =============================================================================
// Dynamic Character Profile Updates (v14+)
// =============================================================================

/**
 * Request an asynchronous dynamic character profile (bio) update for an actor.
 *
 * Queues a background update using SkyrimNet's `dynamic_bio_update` pipeline on
 * the internal worker thread pool and returns immediately without blocking the
 * game thread. The updated dynamic bio is persisted to the active playthrough's
 * content layer (`prompts/characters/dynamic/<name>.dynamic.prompt`).
 *
 * @param formId                 Actor's Skyrim FormID (non-zero; must resolve to a
 *                               valid actor; if no bio file exists yet, an initial
 *                               profile is generated automatically).
 * @param additionalInstructions Optional caller-provided guidance for the LLM
 *                               describing what changed or what should be updated
 *                               (nullptr or "" for standard event/memory-driven update).
 * @param updatableBlocksCSV     Optional comma-separated list of bio block names
 *                               allowed to be modified during this update (case-insensitive).
 *                               Valid block names: "summary", "interject_summary",
 *                               "background", "personality", "appearance",
 *                               "aspirations", "relationships", "occupation",
 *                               "skills", "speech_style", or "all" ("*") to allow
 *                               updating every block (nullptr or "" to use the
 *                               user's configured `updatable_blocks` default).
 * Only one profile update per actor may be in flight at a time; concurrent calls
 * for the same actor while an update is already running return false.
 *
 * @return true if the actor and dynamic bio system were validated and the update
 *         task was queued on the background thread pool; false if @p formId could
 *         not be resolved, the dynamic bio manager is not initialized, or an
 *         update for this actor is already in flight.
 *
 * @code
 *   if (SkyrimNet::PublicAPI::PublicRequestProfileUpdate) {
 *       // Update using default configured updatable blocks with extra guidance:
 *       SkyrimNet::PublicAPI::PublicRequestProfileUpdate(
 *           actorFormId,
 *           "Lydia was promoted to Steward of Dragonsreach and now distrusts Ulfric.",
 *           nullptr);
 *
 *       // Restrict this update to specific blocks:
 *       SkyrimNet::PublicAPI::PublicRequestProfileUpdate(
 *           actorFormId,
 *           "Reflect her new scar across the left cheek from the Frost Troll ambush.",
 *           "appearance");
 *   }
 * @endcode
 */
inline bool (*PublicRequestProfileUpdate)(uint32_t formId, const char* additionalInstructions,
                                          const char* updatableBlocksCSV) = nullptr;

// =============================================================================
// Runtime Actor Succession / Identity Migration (v15+)
// =============================================================================

/**
 * Record that @p toFormId is the successor of @p fromFormId (the same person across a record
 * swap or dynamic 0xFF actor respawn, e.g., young child -> adult child).
 *
 * Unions both identities in SkyrimNet's per-save `identity_aliases` table so memories, recent
 * events, diaries, and character profiles (static and dynamic bios) carry over to @p toFormId
 * and persist across save/reload and timeline reverts. Multiple stages can be chained
 * (`A -> B`, then `B -> C`).
 *
 * When the earlier actor has already been despawned/deleted before the successor spawns, cache
 * its UUID while it is still alive via `PublicFormIDToUUID(fromFormId)` and call
 * `PublicRecordActorSuccessionByUUID(fromUuid, toUuid)` instead.
 *
 * @param fromFormId  FormID of the earlier actor identity (must resolve to a non-player UUID).
 * @param toFormId    FormID of the new/current actor identity (must resolve to a non-player UUID).
 * @return true if the succession was recorded or was already recorded (idempotent on repeat calls;
 *         re-recording an earlier link in a chain does not rewind the live self).
 */
inline bool (*PublicRecordActorSuccession)(uint32_t fromFormId, uint32_t toFormId) = nullptr;

/**
 * Record that @p toUuid is the successor of @p fromUuid (same person across dynamic 0xFF spawns,
 * despawned actors, or Virtual NPCs -> spawned actors).
 *
 * Works even when @p fromUuid's actor reference was despawned/deleted in an earlier session or
 * was a Virtual NPC. Persists to the active save's `identity_aliases` table.
 *
 * @param fromUuid  SkyrimNet EntityUUID of the earlier self (non-zero, non-player).
 * @param toUuid    SkyrimNet EntityUUID of the new/current self (non-zero, non-player).
 * @return true if the succession was recorded or was already recorded (idempotent on repeat calls;
 *         re-recording an earlier link in a chain does not rewind the live self).
 */
inline bool (*PublicRecordActorSuccessionByUUID)(uint64_t fromUuid, uint64_t toUuid) = nullptr;

// =============================================================================
// Initialization
// =============================================================================

/**
 * Load SkyrimNet and resolve all exported function pointers. Call once during kDataLoaded.
 * Functions from newer API versions stay nullptr when the installed SkyrimNet is older, so check before use.
 * @return true if SkyrimNet.dll was loaded and at least PublicGetVersion resolved.
 */
inline bool FindFunctions() {
    auto hDLL = LoadLibraryA("SkyrimNet");
    if (hDLL != nullptr) {
        PublicGetVersion = (int (*)()) GetProcAddress(hDLL, "PublicGetVersion");
        PublicGetPackageApiVersion = reinterpret_cast<uint32_t (*)()>(GetProcAddress(hDLL, "PublicGetPackageApiVersion"));
        if (PublicGetVersion != nullptr) {
            int version = PublicGetVersion();

            // v2+ functions
            if (version >= 2) {
                PublicRegisterCPPAction =
                    (bool (*)(const std::string name, const std::string description, std::function<bool(RE::Actor *)> eligibleCallback,
                    std::function<bool(RE::Actor *, std::string json_params)> executeCallback, const std::string triggeringEventTypesCSV,
                              std::string categoryStr, int priority, std::string parameterSchemaJSON, std::string customCategory,
                              std::string customParentCategory, std::string tagsCSV)) GetProcAddress(hDLL, "PublicRegisterCPPAction");
                PublicRegisterCPPSubCategory = (bool (*)(
                    const std::string name, const std::string description, std::function<bool(RE::Actor *)> eligibleCallback,
                    const std::string triggeringEventTypesCSV, int priority, std::string parameterSchemaJSON, std::string customCategory,
                    std::string customParentCategory, std::string tagsCSV)) GetProcAddress(hDLL, "PublicRegisterCPPSubCategory");
            }

            // v3+ functions
            if (version >= 3) {
                // UUID resolution
                PublicFormIDToUUID = reinterpret_cast<uint64_t(*)(uint32_t)>(
                    GetProcAddress(hDLL, "PublicFormIDToUUID"));
                PublicUUIDToFormID = reinterpret_cast<uint32_t(*)(uint64_t)>(
                    GetProcAddress(hDLL, "PublicUUIDToFormID"));
                PublicGetActorNameByUUID = reinterpret_cast<std::string(*)(uint64_t)>(
                    GetProcAddress(hDLL, "PublicGetActorNameByUUID"));

                // Bio template
                PublicGetBioTemplateName = reinterpret_cast<std::string(*)(uint32_t)>(
                    GetProcAddress(hDLL, "PublicGetBioTemplateName"));

                // Data API
                PublicGetMemoriesForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
                    GetProcAddress(hDLL, "PublicGetMemoriesForActor"));
                PublicGetRecentEvents = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
                    GetProcAddress(hDLL, "PublicGetRecentEvents"));
                PublicGetRecentDialogue = reinterpret_cast<std::string(*)(uint32_t, int)>(
                    GetProcAddress(hDLL, "PublicGetRecentDialogue"));
                PublicGetLatestDialogueInfo = reinterpret_cast<std::string(*)()>(
                    GetProcAddress(hDLL, "PublicGetLatestDialogueInfo"));
                PublicIsMemorySystemReady = reinterpret_cast<bool(*)()>(
                    GetProcAddress(hDLL, "PublicIsMemorySystemReady"));
                PublicGetActorEngagement = reinterpret_cast<std::string(*)(int, bool, bool, double, double)>(
                    GetProcAddress(hDLL, "PublicGetActorEngagement"));
                PublicGetRelatedActors = reinterpret_cast<std::string(*)(uint32_t, int, double, double)>(
                    GetProcAddress(hDLL, "PublicGetRelatedActors"));
                PublicGetPlayerContext = reinterpret_cast<std::string(*)(float)>(
                    GetProcAddress(hDLL, "PublicGetPlayerContext"));
                PublicGetEventPairCounts = reinterpret_cast<std::string(*)(const char*, int)>(
                    GetProcAddress(hDLL, "PublicGetEventPairCounts"));

                // Plugin config API
                PublicGetPluginConfig = reinterpret_cast<std::string(*)(const char*)>(
                    GetProcAddress(hDLL, "PublicGetPluginConfig"));
                PublicGetPluginConfigValue = reinterpret_cast<std::string(*)(const char*, const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicGetPluginConfigValue"));
            }

            // v4+ functions
            if (version >= 4) {
                PublicGetDiaryEntries = reinterpret_cast<std::string(*)(uint32_t, int, double, double)>(
                    GetProcAddress(hDLL, "PublicGetDiaryEntries"));
            }

            // v5+ functions
            if (version >= 5) {
                // Decorator registration
                PublicRegisterDecorator = reinterpret_cast<bool(*)(const char*, const char*,
                    std::function<std::string(RE::Actor*)>)>(
                    GetProcAddress(hDLL, "PublicRegisterDecorator"));
                PublicHasDecorator = reinterpret_cast<bool(*)(const char*)>(
                    GetProcAddress(hDLL, "PublicHasDecorator"));

                // Event callbacks
                PublicRegisterEventCallback = reinterpret_cast<uint64_t(*)(const char*,
                    std::function<void(const char*)>)>(
                    GetProcAddress(hDLL, "PublicRegisterEventCallback"));
                PublicUnregisterEventCallback = reinterpret_cast<bool(*)(uint64_t)>(
                    GetProcAddress(hDLL, "PublicUnregisterEventCallback"));

                // Memory creation
                PublicAddMemory = reinterpret_cast<int(*)(uint32_t, const char*, float,
                    const char*, const char*, const char*, const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicAddMemory"));
            }

            // v6+ functions: Actor busy state
            if (version >= 6) {
                PublicSetActorBusy = reinterpret_cast<bool(*)(uint32_t, const char*)>(
                    GetProcAddress(hDLL, "PublicSetActorBusy"));
                PublicClearActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
                    GetProcAddress(hDLL, "PublicClearActorBusy"));
                PublicIsActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
                    GetProcAddress(hDLL, "PublicIsActorBusy"));
            }

            // v7+ functions
            if (version >= 7) {
                PublicGetSaveUniqueID = reinterpret_cast<std::string(*)()>(
                    GetProcAddress(hDLL, "PublicGetSaveUniqueID"));
                PublicAddWorldKnowledge = reinterpret_cast<int(*)(const char*, const char*,
                    bool, float, const char*)>(
                    GetProcAddress(hDLL, "PublicAddWorldKnowledge"));
                PublicRemoveWorldKnowledge = reinterpret_cast<bool(*)(int)>(
                    GetProcAddress(hDLL, "PublicRemoveWorldKnowledge"));
                PublicGetWorldKnowledge = reinterpret_cast<std::string(*)(int)>(
                    GetProcAddress(hDLL, "PublicGetWorldKnowledge"));
            }

            // v8+ functions
            if (version >= 8) {
                PublicSendCustomPromptToLLM = reinterpret_cast<bool(*)(const char*, const char*,
                    const char*, std::function<void(const char*, int)>)>(
                    GetProcAddress(hDLL, "PublicSendCustomPromptToLLM"));
            }

            // v9+ functions
            if (version >= 9) {
                PublicGetWorldKnowledgeForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
                    GetProcAddress(hDLL, "PublicGetWorldKnowledgeForActor"));
            }

            // v10+ functions
            if (version >= 10) {
                PublicQueryMemoriesForActor = reinterpret_cast<std::string(*)(uint32_t, const char*)>(
                    GetProcAddress(hDLL, "PublicQueryMemoriesForActor"));
            }

            // v11+ functions
            if (version >= 11) {
                PublicGetTimelineState = reinterpret_cast<int(*)()>(
                    GetProcAddress(hDLL, "PublicGetTimelineState"));
                PublicQueryDiaryEntries = reinterpret_cast<std::string(*)(uint32_t, const char*)>(
                    GetProcAddress(hDLL, "PublicQueryDiaryEntries"));
                PublicAddDiaryEntry = reinterpret_cast<int(*)(uint32_t, const char*, double, const char*,
                    const char*, float, const char*)>(
                    GetProcAddress(hDLL, "PublicAddDiaryEntry"));
                PublicUpdateDiaryEntry = reinterpret_cast<bool(*)(int, const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicUpdateDiaryEntry"));
                PublicDeleteDiaryEntry = reinterpret_cast<bool(*)(int)>(
                    GetProcAddress(hDLL, "PublicDeleteDiaryEntry"));
                PublicUpdateMemory = reinterpret_cast<bool(*)(int, const char*)>(
                    GetProcAddress(hDLL, "PublicUpdateMemory"));
                PublicDeleteMemory = reinterpret_cast<bool(*)(int)>(
                    GetProcAddress(hDLL, "PublicDeleteMemory"));
                PublicSearchActors = reinterpret_cast<std::string(*)(const char*, int)>(
                    GetProcAddress(hDLL, "PublicSearchActors"));
                PublicRegisterEvent = reinterpret_cast<int(*)(const char*, const char*, uint32_t, uint32_t,
                    const uint32_t*, uint32_t)>(GetProcAddress(hDLL, "PublicRegisterEvent"));
            }

            // v12+ functions
            if (version >= 12) {
                PublicSendCustomDecisionToLLM = reinterpret_cast<bool(*)(const char*, const char*, int,
                    std::function<void(const char*, const char*, int)>)>(
                    GetProcAddress(hDLL, "PublicSendCustomDecisionToLLM"));
            }

            // v13+ functions
            if (version >= 13) {
                PublicAddPlace = reinterpret_cast<bool(*)(const char*, const char*, const char*, uint32_t,
                    float, float, float, float, uint32_t)>(GetProcAddress(hDLL, "PublicAddPlace"));
                PublicRemovePlace = reinterpret_cast<bool(*)(const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicRemovePlace"));
                PublicSyncPlaces = reinterpret_cast<int(*)(const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicSyncPlaces"));
                PublicListPlaces = reinterpret_cast<std::string(*)(const char*)>(
                    GetProcAddress(hDLL, "PublicListPlaces"));
                PublicGetPlaceAt = reinterpret_cast<std::string(*)(uint32_t)>(
                    GetProcAddress(hDLL, "PublicGetPlaceAt"));
            }

            // v14+ functions
            if (version >= 14) {
                PublicRequestProfileUpdate = reinterpret_cast<bool(*)(uint32_t, const char*, const char*)>(
                    GetProcAddress(hDLL, "PublicRequestProfileUpdate"));
            }

            // v15+ functions
            if (version >= 15) {
                PublicRecordActorSuccession = reinterpret_cast<bool(*)(uint32_t, uint32_t)>(
                    GetProcAddress(hDLL, "PublicRecordActorSuccession"));
                PublicRecordActorSuccessionByUUID = reinterpret_cast<bool(*)(uint64_t, uint64_t)>(
                    GetProcAddress(hDLL, "PublicRecordActorSuccessionByUUID"));
            }
        }
        return true;
    }
    return false;
}
}

/**
 * Typed wrapper over PublicQueryMemoriesForActor (v10+). Returns "[]" when the installed SkyrimNet predates v10.
 *
 * @code
 *   MemoryQuery q;
 *   q.excludeTags   = {"mymod_writeback"};
 *   q.minImportance = 0.4f;
 *   q.orderBy       = MemoryOrder::ImportanceDesc;
 *   q.maxCount      = 10;
 *
 *   std::string memories = QueryMemoriesForActor(formId, q);
 * @endcode
 */
inline std::string QueryMemoriesForActor(uint32_t formId, const MemoryQuery& query) {
    if (PublicQueryMemoriesForActor == nullptr) return "[]";
    return PublicQueryMemoriesForActor(formId, MemoryQueryToJSON(query).c_str());
}

/**
 * Typed wrapper over PublicQueryDiaryEntries (v11+). Returns "[]" when the installed SkyrimNet predates v11.
 *
 * @code
 *   DiaryQuery q;
 *   q.minId    = lastIdIHave + 1;
 *   q.orderBy  = DiaryOrder::IdAsc;
 *   q.maxCount = 20;
 *
 *   std::string entries = QueryDiaryEntries(formId, q);
 * @endcode
 */
inline std::string QueryDiaryEntries(uint32_t formId, const DiaryQuery& query) {
    if (PublicQueryDiaryEntries == nullptr) return "[]";
    return PublicQueryDiaryEntries(formId, DiaryQueryToJSON(query).c_str());
}
