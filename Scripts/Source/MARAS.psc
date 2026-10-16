Scriptname MARAS

;/ =====================================================================
;   MARAS - Native SKSE Function Interface (Consolidated API)
;
;   This script provides access to native MARAS C++ functionality.
;   Organized into 9 distinct functional groups and mod events.
;
;   Dependencies: MARAS SKSE Plugin
;  ===================================================================== /;


;/ =====================================================================
;                    ██████  ███████ ██       █████  ████████ ██  ██████  ███    ██ ███████ ██   ██ ██ ██████  ███████
;                    ██   ██ ██      ██      ██   ██    ██    ██ ██    ██ ████   ██ ██      ██   ██ ██ ██   ██ ██
;                    ██████  █████   ██      ███████    ██    ██ ██    ██ ██ ██  ██ ███████ ███████ ██ ██████  ███████
;                    ██   ██ ██      ██      ██   ██    ██    ██ ██    ██ ██  ██ ██      ██ ██   ██ ██ ██          ██
;                    ██   ██ ███████ ███████ ██   ██    ██    ██  ██████  ██   ████ ███████ ██   ██ ██ ██     ███████
;
;   SECTION 1: Relationships
;   Registration, status queries/changes, partner discovery, availability,
;   open/close relationships, cheating tracking, deceased registry.
;  ===================================================================== /;

; ── Registration ──────────────────────────────────────────────────────
;/ Register an NPC as a marriage candidate with auto-determined attributes /;
bool Function RegisterCandidate(Actor npc) global native

;/ Unregister an NPC from the marriage system entirely /;
bool Function UnregisterNPC(Actor npc) global native


; ── Status Queries (string-based) ─────────────────────────────────────
;/ Check if NPC has a specific relationship status (player-relative: "candidate", "dating", "lover", "engaged", "married", "divorced", "jilted", "exdating", "exlover") /;
bool Function IsNPCStatus(Actor npc, string statusType) global native

;/ Check if NPC has a specific relationship status with a target /;
bool Function IsNPCStatusWith(Actor npc, Actor target, string statusType) global native

;/ Get NPC's current status as a display string (player-relative) /;
string Function GetNpcStatusName(Actor npc) global native

;/ Get NPC's current status as a display string with a specific target /;
string Function GetNpcStatusNameWith(Actor npc, Actor target) global native

;/ Get the original (pre-MARAS) relationship rank between two NPCs /;
int Function GetOriginalRelationshipStatus(Actor npc1, Actor npc2) global native

;/ Get the original relationship rank as a string /;
string Function GetOriginalRelationshipStatusString(Actor npc1, Actor npc2) global native


; ── Status Queries (enum-based) ───────────────────────────────────────
;/ Check if NPC has a specific status by enum (0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, 5=Dating, 6=Lover, 7=Exdating, 8=Exlover) /;
bool Function IsNPCStatusByEnum(Actor npc, int statusEnum) global native

;/ Check if NPC has a specific status by enum with a target /;
bool Function IsNPCStatusWithByEnum(Actor npc, Actor target, int statusEnum) global native

;/ Get NPC's current status as an enum int (player-relative) /;
int Function GetNpcStatusEnum(Actor npc) global native

;/ Get NPC's current status as an enum int with a specific target /;
int Function GetNpcStatusEnumWith(Actor npc, Actor target) global native


; ── Status Changes ────────────────────────────────────────────────────
;/ Promote/demote NPC to a status by string (player-relative) /;
bool Function PromoteNPCToStatus(Actor npc, string statusType, bool sendEvent = true) global native

;/ Promote/demote NPC to a status by enum (player-relative) /;
bool Function PromoteNPCToStatusByEnum(Actor npc, int statusEnum, bool sendEvent = true) global native

;/ Promote/demote NPC to a status by string with a specific target /;
bool Function PromoteNPCToStatusWith(Actor npc, Actor target, string statusType, bool sendEvent = true) global native

;/ Promote/demote NPC to a status by enum with a specific target /;
bool Function PromoteNPCToStatusWithByEnum(Actor npc, Actor target, int statusEnum, bool sendEvent = true) global native

;/ Evaluate whether a status change is valid and apply it /;
bool Function EvaluateRelationshipStatusChange(Actor akActor, Actor partner, string targetStatus) global native

;/ Break up the relationship between two actors based on their current relationship:
   Married -> Divorced, Engaged -> Jilted, Dating -> Ex-Dating, Lover -> Ex-Lover /;
bool Function Breakup(Actor actorA, Actor actorB) global native

;/ Break all lover relationships for an NPC. If onlyCheating is true, breaks only lover relationships that constitute cheating. Returns the number of broken relationships. /;
int Function BreakAllLoverConnections(Actor npc, bool onlyCheating = false) global native


; ── Partner / Status Listing ──────────────────────────────────────────
;/ Get all NPCs with a given status (player-relative) /;
Actor[] Function GetNPCsByStatus(string statusType) global native

;/ Get all NPCs with a given status by enum (player-relative) /;
Actor[] Function GetNPCsByStatusEnum(int statusEnum) global native

;/ Get all NPCs with a given status relative to a target /;
Actor[] Function GetNPCsByStatusWith(Actor target, string statusType) global native

;/ Get all relationship partners of an NPC with a given status /;
Actor[] Function GetRelationshipPartners(Actor npc, string statusType) global native

;/ Count NPCs with a specific status (string) /;
int Function GetStatusCount(string statusType) global native

;/ Count NPCs with a specific status (enum) /;
int Function GetStatusCountByEnum(int statusEnum) global native

;/ Count status-change history entries for an NPC pair /;
int Function GetStatusHistoryCount(Actor npc, Actor target = None) global native

;/ Get a specific history entry as "from|to|timestamp|initiator" /;
string Function GetStatusHistoryEntry(Actor npc, Actor target = None, int index = 0) global native


; ── Availability ──────────────────────────────────────────────────────
;/ Check if NPC is available for a given status (player-relative) /;
bool Function IsAvailableForStatus(Actor npc, string status) global native

;/ Check if NPC is available for a given status with a target /;
bool Function IsAvailableForStatusWith(Actor npc, Actor target, string status) global native

;/ Check if BOTH actors can form a relationship of the given status /;
bool Function CanFormRelationship(Actor actorA, Actor actorB, string status) global native


; ── Open / Close Relationships ────────────────────────────────────────
;/ Check if an NPC is in an open relationship /;
bool Function IsNPCInOpenRelationship(Actor npc) global native
;/ Check if a pair's relationship is marked as open /;
bool Function GetPairIsOpen(Actor actorA, Actor actorB) global native

;/ Set whether a pair's relationship is open or closed /;
bool Function SetPairIsOpen(Actor actorA, Actor actorB, bool isOpen) global native


; ── Cheating ──────────────────────────────────────────────────────────
;/ Record a cheating event between three actors /;
bool Function RecordCheating(Actor cheater, Actor victim, Actor affairPartner, bool isSecret = false) global native

;/ Mark an ongoing cheating affair as discovered (isSecret = false), optionally resolving it or dispatching a discovery/resolution event.
   If statusOrResolution is a discovery type ("confessed", "exposed", "caught"), fires maras_affair_exposed.
   If statusOrResolution is a resolution ("confronted", "breakup", "allowed"), the affair is resolved and fires maras_affair_resolved.
   If empty (""), the affair remains ongoing but is marked known across affected pairs without firing an event.
   Also updates the TTM_SpouseCheating faction for any affected player partner. /;
bool Function SetCheatingDiscovered(Actor cheater, Actor affairPartner = none, string statusOrResolution = "") global native

;/ Set the affair status for a cheating record /;
bool Function SetCheatingAffairStatus(Actor cheater, Actor victim, Actor affairPartner, int affairStatus) global native

;/ Get the affair status for a cheating record /;
int Function GetCheatingAffairStatus(Actor cheater, Actor victim, Actor affairPartner) global native

;/ Check if cheater has cheated on victim /;
bool Function HasCheatedOn(Actor cheater, Actor victim) global native

;/ Get how many times cheater has cheated on victim /;
int Function GetCheatingCount(Actor cheater, Actor victim) global native

;/ Get all partners that cheater has cheated on /;
Actor[] Function GetCheatedOnPartners(Actor cheater) global native

;/ Get all actors who have cheated on victim /;
Actor[] Function GetCheatersWhoCheatedOn(Actor victim) global native

;/ Check if an NPC is currently cheating (in a cheating relationship or active ongoing affair) /;
bool Function IsNPCCheating(Actor npc) global native

;/ Check if a relationship between two actors constitutes cheating (i.e. either actor has an exclusive/closed main partner with someone else, or an active affair record exists between them) /;
bool Function IsRelationshipCheating(Actor actorA, Actor actorB) global native


; ── Intimate Threads & Cheating Detection ──────────────────────────────
;/ Registers an intimate scene thread with its participants.
   Evaluates whether any participant has an absent committed partner.
   Returns true if any participant is a potential cheater (instructing Papyrus to track scenes). /;
bool Function RegisterIntimateThread(string framework, int threadID, Actor[] actors) global native

;/ Reports a confirmed sexual action between two actors within an active thread.
   Checks if either actor is a potential cheater, records the affair, and tests for immediate exposure. /;
Function ReportIntimateInteraction(string framework, int threadID, Actor actorA, Actor actorB) global native

;/ Cleans up an intimate scene thread when the encounter ends or is cancelled.
   Automatically stops the background polling loop when no active threads remain. /;
Function ClearIntimateThread(string framework, int threadID) global native

;/ Makes every loaded child in the player's current cell run away (TTM_ChildrenFleeIntimateScene package)
   until StopChildrenFleeing() is called or the player leaves the cell. Children carried out of the cell keep fleeing; children still next to the
   player after about 12 seconds are teleported out of the way (through the nearest load door indoors, onto
   walkable ground away from the player outdoors). Intimate scenes use the same mechanism automatically. /;
Function FleeChildrenFromPlayerCell() global native

;/ Ends a FleeChildrenFromPlayerCell() request (it also ends by itself when the player changes cell). Children stay fleeing only while an intimate scene is still running in their cell. /;
Function StopChildrenFleeing() global native


; ── Deceased Registry ─────────────────────────────────────────────────
;/ Check if actor has lost a spouse to death /;
bool Function IsWidow(Actor akActor) global native

;/ Check if an NPC is in the deceased registry /;
bool Function IsNPCDeceased(Actor akActor) global native

;/ Check if deceased was married to partner /;
bool Function WasMarriedTo(Actor akDeceased, Actor akPartner) global native

;/ Check if deceased was engaged to partner /;
bool Function WasEngagedTo(Actor akDeceased, Actor akPartner) global native

;/ Check if deceased was an ex of partner /;
bool Function WasExOf(Actor akDeceased, Actor akPartner) global native

;/ Get display names of all deceased spouses /;
string[] Function GetDeceasedSpouseNames(Actor akActor) global native

;/ Get display names of all deceased children /;
string[] Function GetDeceasedChildrenNames(Actor akActor) global native

;/ Check if actor has any deceased children /;
bool Function HasDeceasedChildren(Actor akActor) global native

;/ Get total count of deceased registry entries /;
int Function GetDeceasedCount() global native


; ── Marriage Evaluation ───────────────────────────────────────────────
;/ Estimate marriage success chance (0.0-1.0) given player metrics /;
float Function CalculateMarriageSuccessChance(Actor npc, float intimacyAdjustment, float mostGold, float housesOwned, float horsesOwned, float questsCompleted, float dungeonsCleared, float dragonSoulsCollected, bool playerKiller) global native


;/ =====================================================================
;    ██████ ██   ██ ██ ██      ██████  ██████  ███████ ███    ██
;   ██      ██   ██ ██ ██      ██   ██ ██   ██ ██      ████   ██
;   ██      ███████ ██ ██      ██   ██ ██████  █████   ██ ██  ██
;   ██      ██   ██ ██ ██      ██   ██ ██   ██ ██      ██  ██ ██
;    ██████ ██   ██ ██ ███████ ██████  ██   ██ ███████ ██   ████
;
;   SECTION 2: Children & Adoption
;   Core child management, pet/critter system, scene tracking,
;   house assignment, adoption state.
;  ===================================================================== /;

; ── Core Child Management ─────────────────────────────────────────────
;/ Register a physical child actor with custodial guardians, optional house location, optional biological parents, synthetic ID rekeying, and stage /;
bool Function RegisterChildActor(Actor child, Actor guardianA, Actor guardianB = None, Location houseLocation = None, Actor bioMother = None, Actor bioFather = None, int syntheticID = 0, int stage = 2, bool sendEvent = true) global native

;/ Remove a child from the system /;
bool Function RemoveChild(Actor child) global native

;/ Get children of a specific parent pair /;
Actor[] Function GetChildren(Actor parentA, Actor parentB = None) global native

;/ Get all children of a single parent /;
Actor[] Function GetChildrenOfActor(Actor parent) global native

;/ Update the children faction for a spouse (sync after status changes) /;
Function UpdateSpouseChildrenFaction(Actor spouse) global native

;/ Get all player children across all houses /;
Actor[] Function GetAllPlayerChildren() global native

; ── House Assignment & Orders ─────────────────────────────────────────
;/ Assign a child to a specific player house /;
bool Function AssignChildToHouse(Actor child, Location houseLoc, bool moveChild = false) global native

;/ Issue a timed order to a child (e.g. go to bed, play) /;
bool Function IssueChildOrder(Actor child, float orderValue, float durationHours) global native

;/ Reset the force-greeting flag for a child /;
Function ResetChildForceGreeting(Actor child) global native

;/ Get children assigned to a specific house /;
Actor[] Function GetChildrenAssignedToHouse(Location playerHouse) global native

;/ Get the most recently adopted child in a house /;
Actor Function GetNewlyAdoptedChild(Location playerHouse) global native

;/ Get a random child in a house, weighted by interaction recency /;
Actor Function GetWeightedRandomChild(Location playerHouse) global native


; ── Scene Tracking ────────────────────────────────────────────────────
;/ Get children in a house who haven't played a specific scene /;
Actor[] Function GetChildrenWithoutScene(Location playerHouse, int sceneID) global native

;/ Mark a scene as played for a child /;
Function SetChildPlayedScene(Actor child, int sceneID, bool played = true) global native

;/ Check if a child has played a specific scene /;
bool Function HasChildPlayedScene(Actor child, int sceneID) global native

;/ Record that an interaction occurred with a child (timestamp) /;
Function RecordChildInteraction(Actor child) global native

;/ Get the timestamp of a child's last interaction /;
float Function GetChildLastInteractionTimestamp(Actor child) global native


; ── Adoption State ────────────────────────────────────────────────────
;/ Set whether a child is newly adopted (triggers intro sequences) /;
Function SetChildIsNewlyAdopted(Actor child, bool isNewlyAdopted, bool clearSameHouse = false) global native

;/ Check if a child is newly adopted /;
bool Function IsChildNewlyAdopted(Actor child) global native

;/ Check if a child was adopted by the player /;
bool Function IsChildPlayerAdopted(Actor child) global native

;/ Set whether initial move-in for a house is done /;
Function SetHouseInitialMoveDone(Location houseLoc, bool done) global native

;/ Check if initial move-in for a house is done /;
bool Function IsHouseInitialMoveDone(Location houseLoc) global native

;/ Set whether the child chest tutorial has been shown /;
Function SetChildChestIntroduced(bool introduced) global native

;/ Check if the child chest tutorial has been shown /;
bool Function IsChildChestIntroduced() global native


; ── Pets ──────────────────────────────────────────────────────────────
;/ Assign a pet to a child /;
bool Function AssignChildPet(Actor child, Actor pet) global native

;/ Get the pet assigned to a child /;
Actor Function GetChildPet(Actor child) global native

;/ Get the child who owns a specific pet /;
Actor Function GetChildForPet(Actor pet) global native

;/ Remove a child's pet assignment /;
bool Function RemoveChildPet(Actor child) global native

;/ Set the per-child pet ban flag /;
Function SetChildPetBanned(Actor child, bool banned) global native

;/ Check the per-child pet ban flag only /;
bool Function IsChildPetBanned(Actor child) global native

;/ Check effective pet ban (global OR per-child OR retry exhausted) /;
bool Function IsPetBanned(Actor child) global native

;/ Set the global pet ban (affects all children) /;
Function SetGlobalPetBanned(bool banned) global native

;/ Check the global pet ban flag /;
bool Function IsGlobalPetBanned() global native

;/ Set the pet retry cooldown timestamp for a child /;
Function SetChildPetRetryTimestamp(Actor child, float timestamp) global native

;/ Record a pet retry attempt for a child /;
Function RecordChildPetRetry(Actor child) global native

;/ Get the pet retry cooldown timestamp /;
float Function GetChildPetRetryTimestamp(Actor child) global native

;/ Check if a pet retry is currently available /;
bool Function IsPetRetryAvailable(Actor child) global native

;/ Get a child in a house who doesn't have a pet /;
Actor Function GetChildWithoutPet(Location playerHouse) global native


; ── Critters ──────────────────────────────────────────────────────────
;/ Assign a critter to a child /;
bool Function AssignChildCritter(Actor child, Actor critter) global native

;/ Get the critter assigned to a child /;
Actor Function GetChildCritter(Actor child) global native

;/ Get the child who owns a specific critter /;
Actor Function GetChildForCritter(Actor critter) global native

;/ Get the child who owns a specific pet OR critter /;
Actor Function GetChildForPetOrCritter(Actor petOrCritter) global native

;/ Remove a child's critter assignment /;
bool Function RemoveChildCritter(Actor child) global native

;/ Set the per-child critter ban flag /;
Function SetChildCritterBanned(Actor child, bool banned) global native

;/ Check the per-child critter ban flag only /;
bool Function IsChildCritterBanned(Actor child) global native

;/ Check effective critter ban (global OR per-child OR retry exhausted) /;
bool Function IsCritterBanned(Actor child) global native

;/ Set the global critter ban (affects all children) /;
Function SetGlobalCritterBanned(bool banned) global native

;/ Check the global critter ban flag /;
bool Function IsGlobalCritterBanned() global native

;/ Set the critter retry cooldown timestamp for a child /;
Function SetChildCritterRetryTimestamp(Actor child, float timestamp) global native

;/ Record a critter retry attempt for a child /;
Function RecordChildCritterRetry(Actor child) global native

;/ Get the critter retry cooldown timestamp /;
float Function GetChildCritterRetryTimestamp(Actor child) global native

;/ Check if a critter retry is currently available /;
bool Function IsChildCritterRetryAvailable(Actor child) global native

;/ Get a child in a house who doesn't have a critter /;
Actor Function GetChildWithoutCritter(Location playerHouse) global native


; ── Child Name Generation ─────────────────────────────────────────────
;/ Generate a random child name based on race name and gender (isFemale: true = daughter, false = son).
   raceName is optional (pass "" for any race). If isUnique is true, ensures the chosen name is not already in use by any child.
   Returns "" if no matching name is available. /;
string Function GenerateRandomChildName(string raceName = "", bool isFemale = false, bool isUnique = true) global native

;/ Generate a random child name based on an actor's race and gender. If isUnique is true, ensures name is not already in use.
   Returns "" if unavailable. /;
string Function GenerateRandomChildNameForActor(Actor akActor, bool isUnique = true) global native

;/ Generate a random child name based on race Form and gender (isFemale: true = daughter, false = son).
   akRace is optional (pass None for any race). If isUnique is true, ensures name is not already in use.
   Returns "" if no matching name is available. /;
string Function GenerateRandomChildNameForRace(Form akRace, bool isFemale = false, bool isUnique = true) global native

;/ Get all available child names for a given race and gender. raceName is optional (pass "" for all races). /;
string[] Function GetChildNames(string raceName = "", bool isFemale = false) global native

;/ Reload child names from childrenNames /;
bool Function ReloadChildNames() global native


; ── Critter Name Generation ───────────────────────────────────────────
;/ Generate a random critter name based on critter race name / EDID
   (e.g. "BYOHRelationshipAdoptionCritter_Fox", "BYOHRelationshipAdoptionCritter_FoxArctic",
   "BYOHRelationshipAdoptionCritter_Hare", "BYOHRelationshipAdoptionCritter_Mudcrab", "BYOHRelationshipAdoptionCritter_Skeever").
   raceName is optional (pass "" for any critter race). If race is unknown or omitted, chooses a random name across all critter types.
   Returns "" if no matching name is available. /;
string Function GenerateRandomCritterName(string raceName = "") global native

;/ Generate a random critter name for a critter Actor. If race is unknown, chooses a random name across all critter types.
   Returns "" if unavailable. /;
string Function GenerateRandomCritterNameForActor(Actor akActor) global native

;/ Generate a random critter name based on race or actor base Form. If race is unknown, chooses a random name across all critter types.
   Returns "" if no matching name is available. /;
string Function GenerateRandomCritterNameForRace(Form akRace) global native

;/ Get all available critter names for a given critter race. raceName is optional (pass "" for all critter names). /;
string[] Function GetCritterNames(string raceName = "") global native

;/ Reload critter names from critterNames.json /;
bool Function ReloadCritterNames() global native


;/ =====================================================================
;    █████  ███████ ███████ ███████  ██████ ████████ ██  ██████  ███    ██
;   ██   ██ ██      ██      ██      ██         ██    ██ ██    ██ ████   ██
;   ███████ █████   █████   █████   ██         ██    ██ ██    ██ ██ ██  ██
;   ██   ██ ██      ██      ██      ██         ██    ██ ██    ██ ██  ██ ██
;   ██   ██ ██      ██      ███████  ██████    ██    ██  ██████  ██   ████
;
;   SECTION 3: Affection
;   Daily affection accumulation, permanent affection storage,
;   type-based min/max clamping, daily apply, decay multiplier.
;  ===================================================================== /;

; ── Daily Affection ───────────────────────────────────────────────────
;/ Accumulate daily affection for an NPC under a named type (e.g. "gift", "intimacy") /;
Function AddAffection(Actor npc, float amount, string type) global native

;/ Get current daily accumulated affection for npc/type /;
float Function GetAffection(Actor npc, string type) global native


; ── Permanent Affection ───────────────────────────────────────────────
;/ Get permanent (persisted) affection for an NPC /;
int Function GetPermanentAffection(Actor npc) global native

;/ Replace permanent affection for NPC with given amount /;
Function SetPermanentAffection(Actor npc, int amount, bool sendEvent = true) global native


; ── Configuration ─────────────────────────────────────────────────────
;/ Set min/max clamp for a specific affection type; call on game load /;
Function SetAffectionMinMax(string type, int minVal, int maxVal) global native

;/ Apply all daily affection to permanent for all NPCs (call once per day) /;
Function ApplyDailyAffection() global native

;/ Set the multiplier for daily affection decay (0.0=disabled, 1.0=default) /;
Function SetAffectionDecayMultiplier(float multiplier) global native

;/ Get the current affection decay multiplier /;
float Function GetAffectionDecayMultiplier() global native


;/ =====================================================================
;   ██   ██  ██████  ██    ██ ███████ ██ ███    ██  ██████
;   ██   ██ ██    ██ ██    ██ ██      ██ ████   ██ ██
;   ███████ ██    ██ ██    ██ ███████ ██ ██ ██  ██ ██   ███
;   ██   ██ ██    ██ ██    ██      ██ ██ ██  ██ ██ ██    ██
;   ██   ██  ██████   ██████  ███████ ██ ██   ████  ██████
;
;   SECTION 4: Housing & Property
;   Player house registration, tenant management, stay-at-home routines,
;   location queries, NPC/child home markers, spouse original homes,
;   cell and bed ownership sharing.
;  ===================================================================== /;

; ── Player House Registration ──────────────────────────────────────────
;/ Register a player-placed house marker for a BGSLocation /;
bool Function RegisterPlayerHouseCell(Location loc, ObjectReference homeMarker) global native

;/ Get all registered player house locations /;
Location[] Function GetAllPlayerHouses() global native

;/ Get display names of all registered player houses /;
string[] Function GetAllPlayerHousesNames() global native

;/ Get total number of registered player houses /;
int Function CountPlayerHouses() global native

;/ Get the stored base ObjectReference marker for a player house /;
ObjectReference Function GetHouseMarker(Location playerHouse) global native


; ── Tenant Management ─────────────────────────────────────────────────
;/ Register an NPC (tenant/spouse) to a player house /;
bool Function RegisterTenantInPlayerHouse(Actor spouse, Location playerHouse, bool stayAtHome = true) global native

;/ Remove an NPC from any registered player house /;
bool Function RemoveTenantFromPlayerHouse(Actor spouse) global native

;/ Configure whether a tenant sandboxes at home or keeps original routine /;
Function SetTenantStayAtHome(Actor spouse, bool stayAtHome) global native

;/ Check if tenant is configured to stay at home /;
bool Function GetTenantStayAtHome(Actor spouse) global native

;/ Get all tenant Actors registered to a player house /;
Actor[] Function GetPlayerHouseTenants(Location playerHouse) global native

;/ Get the Location of the house where an NPC is registered as a tenant /;
Location Function GetTenantHouse(Actor npc) global native


; ── Spouse Assets & Home Sharing ──────────────────────────────────────
;/ Get discovered door references in a cell /;
ObjectReference[] Function GetCellDoors(Cell currentCell) global native

;/ Get original interior home cell recorded for an NPC /;
Cell Function GetNpcOriginalHouse(Actor npc) global native

;/ Get center marker reference for an NPC's original home /;
ObjectReference Function GetNpcOriginalHouseCenterMarker(Actor npc) global native

;/ Get bed references recorded as owned by an NPC /;
ObjectReference[] Function GetNpcBeds(Actor npc) global native

;/ Share NPC's home furniture and cell ownership with player /;
bool Function ShareHouseWithPlayer(Actor npc) global native

;/ Stop sharing NPC's home furniture and restore original ownership /;
bool Function StopShareHouseWithPlayer(Actor npc) global native

;/ Check if a cell is currently shared with the player /;
bool Function IsHouseSharedWithPlayer(Cell currentCell) global native

;/ Check if a specific spouse's home is shared with the player /;
bool Function HasSpouseSharedHouseWithPlayer(Actor spouse) global native


; ── Home Marker Management ────────────────────────────────────
;/ Get the home sandbox marker for a tracked NPC /;
ObjectReference Function GetTrackedNpcHomeMarker(Actor npc) global native

;/ Manually set the home sandbox marker for a tracked NPC /;
bool Function SetTrackedNpcHomeMarker(Actor npc, ObjectReference marker) global native

;/ Get the home sandbox marker for a child /;
ObjectReference Function GetChildHomeMarker(Actor child) global native


;/ =====================================================================
;   ███████ ███████ ██████  ████████ ██ ██      ██ ████████ ██    ██
;   ██      ██      ██   ██    ██    ██ ██      ██    ██     ██  ██
;   █████   █████   ██████     ██    ██ ██      ██    ██      ████
;   ██      ██      ██   ██    ██    ██ ██      ██    ██       ██
;   ██      ███████ ██   ██    ██    ██ ███████ ██    ██       ██
;
;   SECTION 5: Fertility & Paternity
;   Pregnancy tracking, unborn/infant child lifecycle, synthetic child IDs,
;   paternity secrets, guardians, parent lineage, message/JSON builders.
;  ===================================================================== /;

; ── Pregnancy Tracking ────────────────────────────────────────────────
;/ Mark an NPC as pregnant with optional bio father, partner, and timestamp /;
Function SetNPCPregnant(Actor mother, bool pregnant, Actor bioFather = None, Actor partner = None, float conceptionTime = 0.0) global native

;/ Check whether an NPC is currently pregnant /;
bool Function IsNPCPregnant(Actor mother) global native


; ── Unborn Child Lifecycle ────────────────────────────────────────────
;/ Register an unborn child for a mother; returns synthetic child ID.
   evaluateStepIn=false defers the bio-father step-in evaluation (use it while registering the fetuses of a multi-child pregnancy; pass true for the last one) /;
int Function RegisterUnbornChild(Actor mother, Actor bioFather = None, float conceptionTime = 0.0, bool evaluateStepIn = true) global native

;/ Promote ONE unborn child to infant stage on birth. When bioFather is given, the unborn child fathered by that actor is promoted
   (falls back to the first unborn child). Returns the promoted child ID, or 0 if the mother has no unborn child /;
int Function PromoteUnbornToInfant(Actor mother, float birthTime = 0.0, string name = "Newborn", Actor bioFather = None) global native

;/ Remove unborn child records. Without bioFather all unborn children of the mother are removed;
   with bioFather only one unborn child fathered by that actor is removed (e.g. one stillbirth in a multi-father pregnancy) /;
bool Function RemoveUnbornChild(Actor mother, Actor bioFather = None) global native

;/ Find the unborn synthetic child ID for a mother /;
int Function FindUnbornChild(Actor mother) global native

;/ Get all unborn child IDs for a mother /;
int[] Function GetUnbornChildren(Actor mother) global native

;/ Get total number of unborn children for a mother /;
int Function GetUnbornChildCount(Actor mother) global native

;/ Register a fertility child with complete conception and birth parameters /;
int Function RegisterFertilityChild(int syntheticID, Actor mother, Actor partner, Actor bioFather, float conception, float birth, string name) global native


; ── Child Lifecycle & Metadata ────────────────────────────────────────
;/ Update the display name of a child record /;
bool Function UpdateChildName(int childID, string newName) global native

;/ Set the developmental stage of a child (e.g. unborn, infant, toddler, child) /;
bool Function SetChildStage(int childID, int stage) global native

;/ Get the developmental stage of a child /;
int Function GetChildStage(int childID) global native

;/ Set the gender of a child record /;
bool Function SetChildGender(int childID, int gender) global native

;/ Get the gender of a child record /;
int Function GetChildGender(int childID) global native

;/ Set the training/schooling location string for a child /;
bool Function SetChildTrainingLocation(int childID, string location) global native

;/ Get the training/schooling location string for a child /;
string Function GetChildTrainingLocation(int childID) global native

;/ Look up a synthetic child ID by mother and name /;
int Function FindSyntheticChild(Actor mother, string name = "") global native


; ── Paternity Secrets ─────────────────────────────────────────────────
;/ Resolve paternity secret for a child ID with partner discovery flag /;
bool Function ResolvePaternitySecret(int childID, bool discoveredByPartner) global native

;/ Resolve paternity secret specifically from partner perspective /;
bool Function ResolvePaternitySecretForPartner(int childID, bool discoveredByPartner) global native

;/ Resolve paternity secret specifically from biological father perspective /;
bool Function ResolvePaternitySecretForBioFather(int childID, bool discoveredByBioFather = false) global native

;/ Set knowledge edges for biological father and partner /;
bool Function SetPaternitySecretEdges(int childID, bool secretForBioFather, bool secretForPartner) global native

;/ Check if paternity is still secret from the biological father /;
bool Function IsSecretForBioFather(int childID) global native

;/ Check if paternity is still secret from the mother's partner /;
bool Function IsSecretForPartner(int childID) global native

;/ Find paternity secret record for parents and child name /;
int Function FindChildPaternitySecret(Actor parent1, Actor parent2 = None, string childName = "") global native


; ── Guardians & Lineage (by child ID) ─────────────────────────────────
;/ Assign a guardian actor to slot (0 or 1) for a child ID /;
bool Function SetChildGuardian(int childID, int slot, Actor guardian) global native

;/ Get Guardian A (primary guardian) by child ID /;
Actor Function GetGuardianA(int childID) global native

;/ Get Guardian B (secondary guardian) by child ID /;
Actor Function GetGuardianB(int childID) global native

;/ Get array of both guardians by child ID /;
Actor[] Function GetGuardians(int childID) global native

;/ Get the other guardian paired with the given actor /;
Actor Function GetCoGuardian(int childID, Actor actor) global native

;/ Check if the given actor is a registered guardian for the child ID /;
bool Function IsGuardian(int childID, Actor actor) global native

;/ Get biological mother by child ID /;
Actor Function GetBioMother(int childID) global native

;/ Get biological father by child ID /;
Actor Function GetBioFather(int childID) global native

;/ Get mother's partner at time of conception/birth by child ID /;
Actor Function GetMotherPartner(int childID) global native

;/ Set biological mother by child ID /;
bool Function SetBioMother(int childID, Actor mother) global native

;/ Set biological father by child ID /;
bool Function SetBioFather(int childID, Actor father) global native

;/ Get child name string by child ID /;
string Function GetChildName(int childID) global native

;/ Get the primary relationship partner for an actor /;
Actor Function GetPrimaryPartner(Actor akActor) global native


; ── Message & Notification Builders ───────────────────────────────────
;/ Build localized display text announcing pregnancy /;
string Function BuildPregnancyAnnouncedMessage(Actor mother, Actor partner = None, Actor bioFather = None, string bioFatherName = "", int childCount = 1) global native

;/ Build JSON payload announcing pregnancy /;
string Function BuildPregnancyAnnouncedJson(Actor mother, Actor partner = None, Actor bioFather = None, string bioFatherName = "", int childCount = 1) global native

;/ Build localized display text for child birth /;
string Function BuildChildBirthMessage(Actor mother, int[] childIDs = None) global native

;/ Build JSON payload for child birth /;
string Function BuildChildBirthJson(Actor mother, int[] childIDs = None) global native


;/ =====================================================================
;   ██████  ███████ ██████  ███████  ██████  ███    ██  █████  ██      ██ ████████ ██    ██
;   ██   ██ ██      ██   ██ ██      ██    ██ ████   ██ ██   ██ ██      ██    ██     ██  ██
;   ██████  █████   ██████  ███████ ██    ██ ██ ██  ██ ███████ ██      ██    ██      ████
;   ██      ██      ██   ██      ██ ██    ██ ██  ██ ██ ██   ██ ██      ██    ██       ██
;   ██      ███████ ██   ██ ███████  ██████  ██   ████ ██   ██ ███████ ██    ██       ██
;
;   SECTION 6: NPC Characteristics & Personality
;   Inferred personality traits, social class ranks, skill categories,
;   faction synchronization, and dynamic demographic queries.
;  ===================================================================== /;

; ── Faction & Attribute Setup ─────────────────────────────────────────
;/ Set an NPC attribute (socialClass, skillType, temperament) and update faction ranks /;
bool Function SetNpcCharacteristics(Actor npc, string factionType, int rank) global native


; ── Attribute Type & Enum Lookups ─────────────────────────────────────
;/ Get array of string names for an attribute type category /;
string[] Function GetNpcTypes(string type) global native

;/ Get enum integer value for a specific attribute name /;
int Function GetNpcTypeEnum(string type, string name) global native

;/ Get the current enum integer for an NPC's attribute /;
int Function GetNpcCurrentTypeEnum(Actor npc, string type) global native

;/ Get the current string name for an NPC's attribute /;
string Function GetNpcCurrentTypeName(Actor npc, string type) global native


; ── Attribute Inferences & Determiners ────────────────────────────────
;/ Evaluate and return inferred social class enum (0..7: Outcast..Rulers) /;
int Function DetermineSocialClass(Actor npc) global native

;/ Evaluate and return inferred skill type enum (0..5: Warrior..Orator) /;
int Function DetermineSkillType(Actor npc) global native

;/ Evaluate and return inferred temperament enum (0..4: Proud..Independent) /;
int Function DetermineTemperament(Actor npc) global native


;/ =====================================================================
;   ██   ██ ██ ███████ ██████   █████  ██████   ██████ ██   ██ ██    ██
;   ██   ██ ██ ██      ██   ██ ██   ██ ██   ██ ██      ██   ██  ██  ██
;   ███████ ██ █████   ██████  ███████ ██████  ██      ███████   ████
;   ██   ██ ██ ██      ██   ██ ██   ██ ██   ██ ██      ██   ██    ██
;   ██   ██ ██ ███████ ██   ██ ██   ██ ██   ██  ██████ ██   ██    ██
;
;   SECTION 7: Spouse Hierarchy & Buffs
;   Spouse rank hierarchy slots, team skill multipliers, permanent social
;   class multipliers, teammate queries, and bonus perk JSON accessors.
;  ===================================================================== /;

; ── Spouse Hierarchy Slots ────────────────────────────────────────────
;/ Set spouse hierarchy rank (0..2 for top slots, 4 or -1 to remove) /;
bool Function SetHierarchyRank(Actor npc, int rank) global native

;/ Get spouse hierarchy rank (0..2, or 4 for rank 4+) /;
int Function GetHierarchyRank(Actor npc) global native


; ── Buffs & Multipliers ───────────────────────────────────────────────
;/ Get affection/hierarchy/count stat multiplier for a spouse /;
float Function GetSpouseMultiplier(Actor spouse) global native

;/ Get aggregated stat multipliers for followers by skill bucket /;
float[] Function GetFollowersMultipliers(Actor[] followers) global native

;/ Get aggregated permanent multipliers for married spouses by social class /;
float[] Function GetPermanentMultipliers() global native

;/ Get array of all current player teammates /;
Actor[] Function GetCurrentTeammates() global native

;/ Check if an actor is currently recognized as a player follower /;
bool Function IsPlayerFollower(Actor npc) global native


; ── Bonuses JSON Accessors ────────────────────────────────────────────
;/ Get count of bonus entries for a given bonus type key /;
int Function GetBonusCount(string type) global native

;/ Get the Perk form for a bonus entry /;
Perk Function GetBonusPerk(string type, int index = 0) global native

;/ Get effect index for a bonus entry /;
int Function GetBonusEffectIndex(string type, int index = 0) global native

;/ Get numeric value for a bonus entry /;
float Function GetBonusPerkValue(string type, int index = 0) global native

;/ Get type string ("spell", "multiply", "add") for a bonus entry /;
string Function GetBonusPerkType(string type, int index = 0) global native

;/ Get unit string ("%", "points") for a bonus entry /;
string Function GetBonusPerkUnit(string type, int index = 0) global native

;/ Get description string for a bonus entry /;
string Function GetBonusPerkDescription(string type, int index = 0) global native


;/ =====================================================================
;   ███████ ███████ ████████ ████████ ██ ███    ██  ██████  ███████
;   ██      ██         ██       ██    ██ ████   ██ ██       ██
;   ███████ █████      ██       ██    ██ ██ ██  ██ ██   ███ ███████
;        ██ ██         ██       ██    ██ ██  ██ ██ ██    ██      ██
;   ███████ ███████    ██       ██    ██ ██   ████  ██████  ███████
;
;   SECTION 8: Settings & Configuration
;   Mod configuration variables (bool, int, float), legacy MCM migration,
;   dialogue filtering rules, polygamy toggles, relationship rank thresholds.
;  ===================================================================== /;

; ── General Configuration Storage ─────────────────────────────────────
;/ Set boolean configuration setting /;
Function SetSettingBool(string key, bool value) global native

;/ Get boolean configuration setting /;
bool Function GetSettingBool(string key) global native

;/ Set integer configuration setting /;
Function SetSettingInt(string key, int value) global native

;/ Get integer configuration setting /;
int Function GetSettingInt(string key) global native

;/ Set float configuration setting /;
Function SetSettingFloat(string key, float value) global native

;/ Get float configuration setting /;
float Function GetSettingFloat(string key) global native

;/ Check if legacy MCM settings have been migrated /;
bool Function IsSettingsMigrated() global native

;/ Mark legacy settings migration as completed /;
Function MarkSettingsMigrated() global native


; ── Relationship Rules & Dialogue Filters ─────────────────────────────
;/ Get gender filter for starting relationship dialogues /;
int Function GetStartDialGender() global native

;/ Set gender filter for starting relationship dialogues /;
Function SetStartDialGender(int value) global native

;/ Check if polygamy is enabled /;
bool Function GetEnablePolygamy() global native

;/ Set whether polygamy is enabled /;
Function SetEnablePolygamy(bool value) global native

;/ Check if the polygamy unlock quest is disabled /;
bool Function GetDisablePolygamyQuest() global native

;/ Set whether the polygamy unlock quest is disabled /;
Function SetDisablePolygamyQuest(bool value) global native

;/ Get minimum relationship rank required for dialogue /;
int Function GetMinRelRankForDial() global native

;/ Set minimum relationship rank required for dialogue /;
Function SetMinRelRankForDial(int value) global native


;/ =====================================================================
;   ██    ██ ████████ ██ ██      ██ ████████ ██    ██
;   ██    ██    ██    ██ ██      ██    ██     ██  ██
;   ██    ██    ██    ██ ██      ██    ██      ████
;   ██    ██    ██    ██ ██      ██    ██       ██
;    ██████     ██    ██ ███████ ██    ██       ██
;
;   SECTION 9: System & Utility
;   Low-level JSON helpers, keyword manipulation, confirmation UI modals,
;   action cooldowns, logging and diagnostics, actor discovery.
;  ===================================================================== /;

; ── JSON Utilities ────────────────────────────────────────────────────
;/ Extract string property from a JSON payload by path /;
string Function GetJsonString(string json, string path, string defaultVal = "") global native

;/ Extract Actor reference from a JSON payload by path /;
Actor Function GetJsonActor(string json, string path) global native

;/ Extract int property from a JSON payload by path /;
int Function GetJsonInt(string json, string path, int defaultVal = 0) global native

;/ Extract float property from a JSON payload by path /;
float Function GetJsonFloat(string json, string path, float defaultVal = 0.0) global native

;/ Extract bool property from a JSON payload by path /;
bool Function GetJsonBool(string json, string path, bool defaultVal = false) global native

;/ Get length of array at path in a JSON payload /;
int Function GetJsonArrayLength(string json, string path) global native

;/ Extract Actor array from a JSON payload by path /;
Actor[] Function GetJsonActorArray(string json, string path) global native


; ── Keyword Management ────────────────────────────────────────────────
;/ Check whether the selected NPC (or their base record) has a specific keyword /;
bool Function HasNpcKeyword(Actor npc, Keyword kw) global native

;/ Add a keyword to the NPC's base record at runtime /;
bool Function AddNpcKeyword(Actor npc, Keyword kw) global native

;/ Remove a keyword from the NPC's base record at runtime /;
bool Function RemoveNpcKeyword(Actor npc, Keyword kw) global native


; ── Diagnostics & Logging ─────────────────────────────────────────────
;/ Log a message to the SKSE log file with specified log level: 0=trace, 1=debug, 2=warn, 3=error, 4=none /;
Function Log(string msg, int logLevel) global native

;/ Set the logging level for the SKSE plugin: 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=off /;
Function SetLogLevel(int logLevel) global native

;/ Get the current logging level for the SKSE plugin /;
int Function GetLogLevel() global native

;/ Log overall statistics about the marriage system to SKSE log /;
Function LogNPCStatistics() global native

;/ Log detailed internal state for a specific NPC to SKSE log /;
Function LogNPCDetails(Actor npc) global native


; ── UI Modals & Action Cooldowns ──────────────────────────────────────
;/ Show the Prisma UI dashboard /;
Function ShowUI() global native

;/ Hide the Prisma UI dashboard /;
Function HideUI() global native

;/ Check if the Prisma UI dashboard is currently visible /;
bool Function IsUIVisible() global native

;/ Show a non-blocking confirmation modal dialog (returns 0=accept, 1=cancel, 2=decline with explain) /;
int Function ShowConfirmationModal(string actionType, Actor[] mainActors, Actor[] secondaryActors, string paramsJson = "") global native

;/ Check if an action is on cooldown for given actors, and set cooldown if ready /;
int Function CheckAndSetActionCooldown(string actionType, Actor[] actors) global native


; ── Actor Discovery ───────────────────────────────────────────────────
;/ Search active game world for an actor by display name /;
Actor Function FindActorByName(string name) global native


;/ =====================================================================
;   SECTION 10: Banter Coordinator
;   Find candidate couples around player for CK banter scenes,
;   partner presence checks, and repetition cooldown management.
;  ===================================================================== /;

;/ Scans for candidate couples around the player within scan radius.
   Returns Actor[2] where Actor[0] is the local NPC and Actor[1] is their partner.
   Returns an empty array (Length == 0) if no eligible couples are found or all are on cooldown. /;
Actor[] Function FindBanterCoupleCandidates(float afScanRadius = 3000.0) global native

;/ Checks if a partner actor is physically loaded and within scan radius of the player.
   Returns true if partner is present (Tier 1 couple), false if absent (Tier 2 couple). /;
bool Function IsBanterPartnerPresent(Actor akPartner, float afScanRadius = 3000.0) global native

;/ Clears the banter repetition cooldown for a specific couple pair /;
Function ResetBanterCoupleCooldown(Actor akActorA, Actor akActorB) global native

;/ Clears all active banter repetition cooldowns across all couples /;
Function ClearAllBanterCooldowns() global native

;/ Gets the banter couple cooldown duration in in-game hours (default: 24.0) /;
float Function GetBanterCoupleCooldownHours() global native

;/ Sets the banter couple cooldown duration in in-game hours /;
Function SetBanterCoupleCooldownHours(float afHours) global native

;/ Records that an NPC scene was actually played, updating the scene cooldown (and couple cooldown if participants are provided) /;
Function RecordScenePlayed(Actor akActorA = None, Actor akActorB = None) global native

;/ Checks if a location is allowed for banter/ambient scenes based on MARAS location settings.
   Evaluates interior vs street context and resolves multiple conflicting keywords. /;
bool Function IsLocationAllowedForScenes(Location akLocation) global native


;/ =====================================================================
;   ███████ ██    ██ ███████ ███    ██ ████████ ███████
;   ██      ██    ██ ██      ████   ██    ██    ██
;   █████   ██    ██ █████   ██ ██  ██    ██    ███████
;   ██       ██  ██  ██      ██  ██ ██    ██         ██
;   ███████   ████   ███████ ██   ████    ██    ███████
;
;   SECTION 11: Mod Events
;   Script events dispatched by the MARAS SKSE plugin. Papyrus scripts
;   can register to listen for these events via RegisterForModEvent().
;  ===================================================================== /;

; ── Relationship & Social Events ──────────────────────────────────────
;/ Handles relationship change events.
   @param EventName - "maras_status_changed"
   @param status - New relationship status ("candidate", "dating", "lover", "engaged", "married", "jilted", "divorced", "exdating", "exlover")
   @param statusEnum - Enum value of the new status (0..6)
   @param npc - The Actor whose relationship changed /;
; Event OnMarasStatusChanged(string eventName, string status, float statusEnum, Form npc)
; EndEvent

;/ Handles full relationship change events with JSON payload containing both participants.
   @param EventName - "maras_status_changed_full"
   @param jsonPayload - JSON formatted as {"initiator":"0x...","acceptor":"0x...","initiatorName":"...","acceptorName":"...","status":"..."}
   @param statusEnum - Enum value of the new status
   @param npc - The initiator Actor /;
; Event OnMarasStatusChangedFull(string eventName, string jsonPayload, float statusEnum, Form npc)
; EndEvent

;/ Handles hierarchy rank change events for spouses.
   @param EventName - "maras_hierarchy_changed"
   @param promotDemote - "promote" or "demote"
   @param rankDiff - Positive for promote, negative for demote
   @param spouse - The spouse Actor whose hierarchy rank changed /;
; Event OnMarasHierarchyChanged(string eventName, string promotDemote, float rankDiff, Form spouse)
; EndEvent

;/ Handles spouse affection change events.
   @param EventName - "maras_change_affection"
   @param level - "happy", "neutral", "unhappy", "estranged"
   @param affectionDiff - Positive for increase, negative for decrease
   @param spouse - The spouse Actor whose affection changed /;
; Event OnMarasChangeAffection(string eventName, string level, float affectionDiff, Form spouse)
; EndEvent

;/ Handles cheating caught events when a committed partner is caught in the act or walked in on.
   @param EventName - "maras_cheating_caught"
   @param jsonPayload - JSON formatted as {"victim":"0x...","cheater":"0x...","affairPartner":"0x...","victimName":"...","cheaterName":"...","affairPartnerName":"...","framework":"...","threadID":...,"isWalkIn":true/false}
   @param threadID - Intimate animation thread ID
   @param victim - The victim Actor who caught them (sender) /;
; Event OnMarasCheatingCaught(string eventName, string jsonPayload, float threadID, Form victim)
; EndEvent

;/ Handles walk-in / detection events when a partner in an open relationship detects their partner being intimate with someone else.
   @param EventName - "maras_open_relationship_walkin"
   @param jsonPayload - JSON formatted as {"witness":"0x...","partner":"0x...","sexPartner":"0x...","witnessName":"...","partnerName":"...","sexPartnerName":"...","framework":"...","threadID":...,"isWalkIn":true/false}
   @param threadID - Intimate animation thread ID
   @param witness - The open relationship partner Actor who detected them (sender) /;
; Event OnMarasOpenRelationshipWalkIn(string eventName, string jsonPayload, float threadID, Form witness)
; EndEvent

;/ Handles relationship open/closed toggle events.
   @param EventName - "maras_open_relationship_changed"
   @param jsonPayload - JSON formatted as {"actorA":"0x...","actorB":"0x...","actorAName":"...","actorBName":"...","isOpen":true/false}
   @param numArg - Unused
   @param sender - Actor form of actorA /;
; Event OnMarasOpenRelationshipChanged(string eventName, string jsonPayload, float numArg, Form sender)
; EndEvent

;/ Handles cheating/affair discovery or resolution events.
   @param EventName - "maras_affair_resolved"
   @param jsonPayload - JSON formatted as {"cheater":"0x...","victim":"0x...","affairPartner":"0x...","cheaterName":"...","victimName":"...","affairPartnerName":"...","resolution":"..."}
   @param numArg - Unused (0.0)
   @param sender - Actor form of cheater /;
; Event OnMarasAffairResolved(string eventName, string jsonPayload, float numArg, Form sender)
; EndEvent



; ── Follower & Teammate Events ────────────────────────────────────────
;/ Handles teammate status change events.
   @param EventName - "maras_teammate_change"
   @param strArg - Unused
   @param fltArg - Unused
   @param sender - Unused /;
; Event OnMarasTeammateChange(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Handles teammate added events.
   @param EventName - "maras_teammate_added"
   @param strArg - Unused
   @param fltArg - Unused
   @param sender - Form of the Actor added as teammate /;
; Event OnMarasTeammateAdded(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Handles teammate removed events.
   @param EventName - "maras_teammate_removed"
   @param strArg - Unused
   @param fltArg - Unused
   @param sender - Form of the Actor removed as teammate /;
; Event OnMarasTeammateRemoved(string eventName, string strArg, float fltArg, Form sender)
; EndEvent


; ── Dialogue & World Events ───────────────────────────────────────────
;/ Fires when the player opens a dialogue menu with an NPC.
   @param EventName - "maras_dialogue_opened"
   @param strArg - Unused ("")
   @param fltArg - Unused (0.0)
   @param sender - Form of the NPC the player is speaking with (cast to Actor) /;
; Event OnMarasDialogueOpened(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Fires when an in-game day changes.
   @param EventName - "maras_day_changed"
   @param strArg - Unused ("")
   @param fltArg - Current GameDaysPassed float
   @param sender - Unused (None) /;
; Event OnMarasDayChanged(string eventName, string strArg, float fltArg, Form sender)
; EndEvent


; ── Children & Lifecycle Events ───────────────────────────────────────
;/ Fires when a child is added, removed, or relocated.
   @param EventName - "maras_child_event"
   @param strArg - JSON payload with child/guardian/reason fields
   @param fltArg - Unused (0.0)
   @param sender - Form of the child Actor /;
; Event OnMarasChildEvent(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Fires when a registered child dies.
   @param EventName - "maras_child_died"
   @param strArg - Unused ("")
   @param fltArg - 1.0 if player killed the child, 0.0 otherwise
   @param sender - Form of the deceased child Actor /;
; Event OnMarasChildDied(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Fires when a child's pet or critter dies.
   @param EventName - "maras_child_pet_died"
   @param strArg - Unused ("")
   @param fltArg - 1.0 if player killed, 0.0 otherwise
   @param sender - Form of the deceased pet/critter Actor /;
; Event OnMarasChildPetDied(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Fires when a tracked adult NPC dies.
   @param EventName - "maras_actor_died"
   @param strArg - Unused ("")
   @param fltArg - 1.0 if player killed, 0.0 otherwise
   @param sender - Form of the deceased Actor /;
; Event OnMarasActorDied(string eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/ Fires when a paternity secret is resolved.
   @param EventName - "maras_paternity_secret_resolved"
   @param strArg - Outcome string: "bio_father_learnt", "presumed_father_learnt",
                   "divorce_revealed", or SkyrimNet outcome
   @param fltArg - Child ID (cast from int to float)
   @param sender - None /;
; Event OnMarasPaternitySecretResolved(string eventName, string strArg, float fltArg, Form sender)
; EndEvent
