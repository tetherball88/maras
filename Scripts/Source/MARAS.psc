Scriptname MARAS

;/ ========================================
   MARAS - Native SKSE Function Interface (Consolidated API)

   This script provides access to the native MARAS C++ functionality
   using consolidated functions that accept string types or enum values.

   Dependencies: MARAS SKSE Plugin
   ====================================== /;

;/ Register an NPC as a marriage candidate with auto-determined attributes /;
bool Function RegisterCandidate(Actor npc) global native

;/ Unregister an NPC from the marriage system /;
bool Function UnregisterNPC(Actor npc) global native

;/
  Change an NPC attribute (social class, skill type, or temperament) and update the
  corresponding faction membership. The native implementation now sets the stored
  attribute on the NPC and updates faction ranks so game-visible faction data
  stays consistent with MARAS internal state.

  @param npc - The Actor to modify
  @param factionType - "socialClass", "skillType", or "temperament" (case-insensitive)
  @param rank - The enum value to set for the chosen attribute:
      - socialClass: 0=Outcast, 1=Poverty, 2=Working, 3=Middle, 4=Wealthy, 5=Religious, 6=Nobles, 7=Rulers
      - skillType:  0=Warrior, 1=Mage, 2=Rogue, 3=Craftsman, 4=Ranger, 5=Orator
      - temperament:0=Proud, 1=Humble, 2=Jealous, 3=Romantic, 4=Independent
  @return True if the attribute and factions were successfully updated
/;
bool Function SetNpcCharacteristics(Actor npc, string factionType, int rank) global native

;/
  Check NPC relationship status using string type.

  @param npc - The Actor to check
  @param statusType - "any", "candidate", "engaged", "married", "divorced", "jilted" (case-insensitive)
  @param statusEnum - -1=any, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted
  @return True if NPC has the specified status
/;
bool Function IsNPCStatus(Actor npc, string statusType) global native
bool Function IsNPCStatusByEnum(Actor npc, int statusEnum) global native

;/
  Promote/demote NPC to a relationship status using string type.

  @param npc - The Actor to promote/demote
  @param statusType - "candidate", "engaged", "married", "divorced", "jilted" (case-insensitive)
  @param statusEnum - 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted
  @return True if successfully changed status
/;
bool Function PromoteNPCToStatus(Actor npc, string statusType) global native
bool Function PromoteNPCToStatusByEnum(Actor npc, int statusEnum) global native

;/
  Get all NPCs with a specific status using string type.

  @param statusType - "all", "candidate", "engaged", "married", "divorced", "jilted" (case-insensitive)
  @param statusEnum - -1=all, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted
  @return Array of FormIDs for NPCs with the specified status
/;
Actor[] Function GetNPCsByStatus(string statusType) global native
Actor[] Function GetNPCsByStatusEnum(int statusEnum) global native

;/
  Get count of NPCs with specific status using string type.

  @param statusType - "all", "candidate", "engaged", "married", "divorced", "jilted" (case-insensitive)
  @param statusEnum - -1=all, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted
  @return Count of NPCs with the specified status
/;
int Function GetStatusCount(string statusType) global native
int Function GetStatusCountByEnum(int statusEnum) global native

;/
  CalculateMarriageSuccessChance

  Estimate the chance (0.0 - 1.0) that a proposal / marriage action will succeed for the
  provided NPC given a set of player-relative metrics. This mirrors the native C++
  implementation and is intended for use in Papyrus scripts that need the same
  deterministic calculation used by the plugin.

  @param npc - Actor being proposed to / evaluated
  @param intimacyAdjustment - float adjustment to NPC intimacy (positive increases chance)
  @param mostGold - player's most gold carried (or relevant gold metric)
  @param housesOwned - number of houses player owns
  @param horsesOwned - number of horses player owns
  @param questsCompleted - number of quests completed by player
  @param dungeonsCleared - number of dungeons cleared by player
  @param dragonSoulsCollected - number of dragon souls collected by player
  @param playerKiller - whether the player has killed fiance or spouse before (negative impact)
  @return float in range [0.0, 1.0] representing estimated success probability.
/;
float Function CalculateMarriageSuccessChance(Actor npc, float intimacyAdjustment, float mostGold, float housesOwned, float horsesOwned, float questsCompleted, float dungeonsCleared, float dragonSoulsCollected, bool playerKiller) global native

;/ ========================================
  Spouse hierarchy bindings (native C++)
  ====================================== /;

;/
  SetHierarchyRank

  Set a spouse's hierarchy rank.

  @param npc - Actor spouse to modify
  @param rank - desired rank: 0..2 = top slots (0 = lead). Use 4 or -1 to remove from top slots
  @return True if the operation was successful (assignment or removal applied)
/;
bool Function SetHierarchyRank(Actor npc, int rank) global native

;/
  GetHierarchyRank

  Get a spouse's hierarchy rank.

  @param npc - Actor spouse to query
  @return int - 0..2 if in top-three slots, otherwise 4 to indicate 4+
/;
int Function GetHierarchyRank(Actor npc) global native

;/ ========================================
   Spouse buff / service bindings (native C++)
   ====================================== /;

;/
  GetSpouseMultiplier

  Returns the multiplier for a single spouse actor based on hierarchy, count, and (future) affection.

  @param spouse - Actor to evaluate
  @return float multiplier (0.0 if null)
/;
float Function GetSpouseMultiplier(Actor spouse) global native

;/
  GetFollowersMultipliers

  Returns an array of multipliers aggregated by skill-type buckets for the provided follower actors.

  @param followers - array of Actor followers to evaluate
  @return float[] array of multipliers per skill-type bucket (size = 6)
/;
float[] Function GetFollowersMultipliers(Actor[] followers) global native

;/
  GetPermanentMultipliers

  Returns an array of permanent multipliers aggregated by social-class for all married spouses.

  @return float[] array of multipliers per social-class bucket (size = 8)
/;
float[] Function GetPermanentMultipliers() global native

Actor[] Function GetCurrentTeammates() global native

;/
  IsPlayerTeammate

  Check if a specific actor is currently a player teammate using the same logic as GetCurrentTeammates.
  This checks:
  - Native teammate flag
  - CurrentFollowerFaction membership
  - Follow package active
  - AI follow target set to player

  @param npc - Actor to check
  @return True if the actor is currently a teammate
/;
bool Function IsPlayerTeammate(Actor npc) global native


;/ ========================================
   SECTION: Affection system bindings (native C++)
   ====================================== /;

;/ ========================================
   Bonuses JSON runtime accessors (native C++)
   ====================================== /;

;/ GetBonusCount

  Returns the number of bonus entries for the specified bonus type key (case-insensitive).
  Example keys: "ttm_craftsman", "ttm_mage", etc.
/;
int Function GetBonusCount(string type) global native

;/ GetBonusPerk

  Returns the Perk form referenced by the bonus entry for the given type/index, or None if unresolved.
  Perk is returned as native Perk type in Papyrus.
/;
Perk Function GetBonusPerk(string type, int index = 0) global native

;/ GetBonusEffectIndex

  Returns the numeric effect index (ttm_effectIndex) for the given bonus entry.
/;
int Function GetBonusEffectIndex(string type, int index = 0) global native

;/ GetBonusPerkValue

  Returns the numeric ttm_value for the bonus entry.
/;
float Function GetBonusPerkValue(string type, int index = 0) global native

;/ GetBonusPerkType

  Returns the ttm_type string for the bonus entry (e.g. "spell", "multiply", "add").
/;
string Function GetBonusPerkType(string type, int index = 0) global native

;/ GetBonusPerkUnit

  Returns the ttm_unit string for the bonus entry (e.g. "%", "points").
/;
string Function GetBonusPerkUnit(string type, int index = 0) global native

;/ GetBonusPerkDescription

  Returns the ttm_description string for the bonus entry (may be empty).
/;
string Function GetBonusPerkDescription(string type, int index = 0) global native


;/
  AddAffection

  Accumulate daily affection for an NPC under a named type. This is kept in-memory
  and will be applied to permanent affection when ApplyDailyAffection is called.

  @param npc - Actor to receive affection
  @param amount - float amount to add (can be negative)
  @param type - string type name (e.g., "gift", "intimacy", "interaction")
/;
Function AddAffection(Actor npc, float amount, string type) global native

;/ Get the current daily accumulated affection for npc/type (float) /;
float Function GetAffection(Actor npc, string type) global native

;/ Get permanent affection stored for an NPC (int) /;
int Function GetPermanentAffection(Actor npc) global native

;/ Replace permanent affection for NPC with given amount /;
Function SetPermanentAffection(Actor npc, int amount) global native

;/ Set min/max clamp for a specific affection type; call on game load to configure /;
Function SetAffectionMinMax(string type, int minVal, int maxVal) global native

;/ Apply all daily affection to permanent for all registered NPCs (should be called once per in-game day) /;
Function ApplyDailyAffection() global native

;/ Set the multiplier for daily affection decay (0.0 = disabled, 1.0 = default, 2.0 = double) /;
Function SetAffectionDecayMultiplier(float multiplier) global native

;/ Get the current affection decay multiplier /;
float Function GetAffectionDecayMultiplier() global native

;/ ========================================
   SECTION: Player House Functions (native C++)
   ====================================== /;

;/ Register a player-placed house marker for a BGSLocation
  @param loc - The BGSLocation representing the player house
  @param homeMarker - The base object (ObjectReference) placed/shared as the house marker
  @return True on success
/;
bool Function RegisterPlayerHouseCell(Location loc, ObjectReference homeMarker) global native

;/ GetAllPlayerHouses

  Returns all registered player house locations as an array of BGSLocation
/;
Location[] Function GetAllPlayerHouses() global native

;/ RegisterTenantInPlayerHouse

  Registers an Actor (tenant/spouse) to a player house location
  @param spouse - Actor to register
  @return True on success
/;
bool Function RegisterTenantInPlayerHouse(Actor spouse, Location playerHouse) global native

;/ RemoveTenantFromPlayerHouse

  Removes an Actor from any registered player house
  @param spouse - Actor to remove
  @return True if a removal occurred
/;

bool Function RemoveTenantFromPlayerHouse(Actor spouse) global native

;/ GetPlayerHouseTenants

  Returns list of Actor tenants for a given player house
  @param playerHouse - BGSLocation to query
  @return Actor[] tenants
/;
Actor[] Function GetPlayerHouseTenants(Location playerHouse) global native


;/ GetHouseMarker

  Returns the stored ObjectReference (base) marker for the given house, or None
  @param playerHouse - BGSLocation to query
  @return ObjectReference base object or None
/;
ObjectReference Function GetHouseMarker(Location playerHouse) global native

;/ ========================================
   SECTION: Player House Helpers
   ====================================== /;


;/ GetAllPlayerHousesNames

  Returns array of display names for all registered player houses. Empty string for unnamed houses.
/;
string[] Function GetAllPlayerHousesNames() global native

;/ GetTenantHouse


  Returns the Location for the house a tenant (Actor) is registered in, or None.
  @param npc - Actor to query
  @return Location or None
/;
Location Function GetTenantHouse(Actor npc) global native

;/ CountPlayerHouses

  Returns number of registered player houses
/;
int Function CountPlayerHouses() global native

;/ ========================================
   SECTION: Debug Functions
   ====================================== /;

;/ Log a message to the SKSE log file with the prefix "Papyrus"
  @param msg - The message to log
  @param logLevel - The log level: 0=trace, 1=debug, 2=warning, 3=error, 4=none (no logging)
/;
Function Log(string msg, int logLevel) global native

;/ SetLogLevel

  Set the logging level for the SKSE plugin.

  @param logLevel - 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=off
/;
Function SetLogLevel(int logLevel) global native

;/ GetLogLevel

  Get the current logging level for the SKSE plugin.

  @return int - 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=off
/;
int Function GetLogLevel() global native

;/ ========================================
   Spouse assets bindings (native C++)
   ====================================== /;

;/ GetCellDoors

  Returns an array of ObjectReferences for doors previously discovered in the
  specified cell via scanning. If the cell wasn't scanned this will return an empty array.

  @param currentCell - Cell to query
  @return ObjectReference[] array of door references (may be empty)
/;
ObjectReference[] Function GetCellDoors(Cell currentCell) global native

;/ GetNpcOriginalHouse

  Returns the original interior Cell recorded for the NPC when the index was built, or None if unknown.
/;
Cell Function GetNpcOriginalHouse(Actor npc) global native

;/ GetNpcOriginalHouseCenterMarker

  Returns the LocationCenterMarker reference for the NPC's original home, or None if unknown.
/;
ObjectReference Function GetNpcOriginalHouseCenterMarker(Actor npc) global native

;/ GetNpcBeds

  Returns an array of ObjectReferences (bed references) that were recorded as owned by the given NPC.
/;
ObjectReference[] Function GetNpcBeds(Actor npc) global native

;/ ShareHouseWithPlayer

  Share ownership of furniture owned by the specified NPC in the provided cell with the player.
  StopShareHouseWithPlayer (or automatic unshare when no sharing spouses remain married to the player) will restore ownership.

  @param npc - Actor requesting the share
  @return True on success
/;
bool Function ShareHouseWithPlayer(Actor npc) global native

;/ StopShareHouseWithPlayer

  Stop sharing any houses/cells the given NPC previously requested be shared. This restores that NPC's
  furniture ownership and removes them from the cell's sharing set.

  @param npc - Actor to stop sharing for
  @return True if any sharing was stopped
/;
bool Function StopShareHouseWithPlayer(Actor npc) global native

;/ IsHouseSharedWithPlayer

  Query whether the given cell is currently shared with the player (true if at least one spouse
  is actively sharing the cell).

  @param currentCell - Cell to query
  @return True if the cell is shared with the player
/;
bool Function IsHouseSharedWithPlayer(Cell currentCell) global native

;/ HasSpouseSharedHouseWithPlayer

  Returns whether the specified spouse's original home is currently shared with the player.
  @param spouse - Actor to query
  @return True if the spouse's home cell is shared with the player
/;
bool Function HasSpouseSharedHouseWithPlayer(Actor spouse) global native

;/ ========================================
   SECTION: Home Marker Management (replaces PO3's SetLinkedRef and StorageUtil)
   ====================================== /;

;/ GetTrackedNpcHomeMarker

  Get the home sandbox marker for a tracked NPC.

  @param npc - Actor to get marker for
  @return ObjectReference - The home marker reference or None
/;
ObjectReference Function GetTrackedNpcHomeMarker(Actor npc) global native

;/ SetTrackedNpcHomeMarker

  Manually set the home sandbox marker for a tracked NPC.

  @param npc - Actor to set marker for
  @param marker - ObjectReference marker to set
  @return True on success
/;
bool Function SetTrackedNpcHomeMarker(Actor npc, ObjectReference marker) global native

;/ Log overall statistics about the marriage system /;
Function LogNPCStatistics() global native

;/ Log detailed information about a specific NPC /;
Function LogNPCDetails(Actor npc) global native

;/ ========================================
   SECTION: NPC Type and Status Queries (native C++)
   ====================================== /;

;/
  GetNpcStatusName

  Get the current relationship status of an NPC as a string.

  @param npc - Actor to query
  @return string representing the status: "Candidate", "Engaged", "Married", "Divorced", "Jilted", or "Unknown"
/;
string Function GetNpcStatusName(Actor npc) global native

;/
  GetNpcStatusEnum

  Get the current relationship status of an NPC as an enum value.

  @param npc - Actor to query
  @return int enum: 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, -1=Unknown/Not Registered
/;
int Function GetNpcStatusEnum(Actor npc) global native

;/
  GetNpcTypes

  Get array of enum string names for a specific type category.

  @param type - "socialClass", "skillType", or "temperament" (case-insensitive)
  @return string[] array of names in enum order:
      - socialClass: ["Outcast", "Poverty", "Working", "Middle", "Wealthy", "Religious", "Nobles", "Rulers"]
      - skillType: ["Warrior", "Mage", "Rogue", "Craftsman", "Ranger", "Orator"]
      - temperament: ["Proud", "Humble", "Jealous", "Romantic", "Independent"]
/;
string[] Function GetNpcTypes(string type) global native

;/
  GetNpcTypeEnum

  Get the enum value for a specific type string.

  @param type - "socialClass", "skillType", or "temperament" (case-insensitive)
  @param name - The name to look up (e.g., "Warrior", "Rulers", "Romantic")
  @return int enum value (0-based index), or -1 if not found
/;
int Function GetNpcTypeEnum(string type, string name) global native

;/
  GetNpcCurrentTypeEnum

  Get the current enum value for an NPC's attribute of a specific type.

  @param npc - Actor to query
  @param type - "socialClass", "skillType", or "temperament" (case-insensitive)
  @return int enum value for the NPC's current attribute, or -1 if not found/NPC not registered
/;
int Function GetNpcCurrentTypeEnum(Actor npc, string type) global native

;/
  GetNpcCurrentTypeName

  Get the current string name for an NPC's attribute of a specific type.

  @param npc - Actor to query
  @param type - "socialClass", "skillType", or "temperament" (case-insensitive)
  @return string name of the NPC's current attribute, or "Unknown" if not found/NPC not registered
/;
string Function GetNpcCurrentTypeName(Actor npc, string type) global native

;/
  DetermineSocialClass / DetermineSkillType / DetermineTemperament

  Expose the native determiners that compute an NPC's inferred attribute based on
  game data and any configured overrides. Each returns an int enum value matching
  the native C++ enums (0-based).

  @param npc - Actor to evaluate
  @return int enum value for the respective type
    - DetermineSocialClass: 0=Outcast,1=Poverty,2=Working,3=Middle,4=Wealthy,5=Religious,6=Nobles,7=Rulers
    - DetermineSkillType:  0=Warrior,1=Mage,2=Rogue,3=Craftsman,4=Ranger,5=Orator
    - DetermineTemperament:0=Proud,1=Humble,2=Jealous,3=Romantic,4=Independent
/;
int Function DetermineSocialClass(Actor npc) global native
int Function DetermineSkillType(Actor npc) global native
int Function DetermineTemperament(Actor npc) global native


;EVENTS
; Youcan register to listen them in your script file. You can find examples in TTM_MainController.psc
;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param EventName - "maras_status_changed"
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
  @param statusEnum The enum value of the new status(0, 1, 2, 3, 4, 5)
/;
; Event (String EventName, String status, Float statusEnum, Form npc)
; EndEvent

;/
    Handles hierarchy rank change events for spouses.
    @param EventName - "maras_hierarchy_changed"
    @param promotDemote - "promote" or "demote"
    @param rankDiff - positive for promote, negative for demote
    @param spouse - The spouse Actor whose hierarchy rank changed
/;
; Event (String EventName, String promotDemote, Float rankDiff, Form spouse)
; EndEvent

;/
    Handles spouse affection change events.
    @param EventName - "maras_change_affection"
    @param level - "happy", "neutral", "unhappy", "estranged"
    @param affectionDiff - positive for increase, negative for decrease
    @param spouse - The spouse Actor whose affection changed
/;
; Event (String EventName, String level, Float affectionDiff, Form spouse)
; EndEvent

;/
    Handles teammate status change events.
    @param EventName - "maras_teammate_change"
    @param strArg - unused
    @param fltArg - unused
    @param sender - unused
/;
; Event (String eventName, string strArg, float fltArg, Form sender)
; EndEvent


;/
    Handles teammate added events.
    @param EventName - "maras_teammate_added"
    @param strArg - unused
    @param fltArg - unused
    @param sender - Form of actor added as teammate
/;
; Event (String eventName, string strArg, float fltArg, Form sender)
; EndEvent

;/
    Handles teammate removed events.
    @param EventName - "maras_teammate_removed"
    @param strArg - unused
    @param fltArg - unused
    @param sender - Form of actor removed as teammate
/;
; Event (String eventName, string strArg, float fltArg, Form sender)
; EndEvent
