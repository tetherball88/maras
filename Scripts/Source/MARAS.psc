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
  Add an NPC to a faction using string type.

  @param npc - The Actor to add to faction
  @param factionType - "socialClass", "skillType", or "temperament" (case-insensitive)
  @param rank - The rank value (0-7 for socialClass, 0-5 for skillType, 0-4 for temperament)
  @return True if successfully added to faction
/;
bool Function AddToFaction(Actor npc, string factionType, int rank) global native

;/
  Check NPC relationship status using string type.

  @param npc - The Actor to check
  @param statusType - "any", "candidate", "engaged", "married", "divorced", "jilted", "deceased" (case-insensitive)
  @param statusEnum - -1=any, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, 5=Deceased
  @return True if NPC has the specified status
/;
bool Function IsNPCStatus(Actor npc, string statusType) global native
bool Function IsNPCStatusByEnum(Actor npc, int statusEnum) global native

;/
  Promote/demote NPC to a relationship status using string type.

  @param npc - The Actor to promote/demote
  @param statusType - "candidate", "engaged", "married", "divorced", "jilted", "deceased" (case-insensitive)
  @param statusEnum - 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, 5=Deceased
  @return True if successfully changed status
/;
bool Function PromoteNPCToStatus(Actor npc, string statusType) global native
bool Function PromoteNPCToStatusByEnum(Actor npc, int statusEnum) global native

;/
  Get all NPCs with a specific status using string type.

  @param statusType - "all", "candidate", "engaged", "married", "divorced", "jilted", "deceased" (case-insensitive)
  @param statusEnum - -1=all, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, 5=Deceased
  @return Array of FormIDs for NPCs with the specified status
/;
Actor[] Function GetNPCsByStatus(string statusType) global native
Actor[] Function GetNPCsByStatusEnum(int statusEnum) global native

;/
  Get count of NPCs with specific status using string type.

  @param statusType - "all", "candidate", "engaged", "married", "divorced", "jilted", "deceased" (case-insensitive)
  @param statusEnum - -1=all, 0=Candidate, 1=Engaged, 2=Married, 3=Divorced, 4=Jilted, 5=Deceased
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
  @return float in range [0.0, 1.0] representing estimated success probability.
/;
float Function CalculateMarriageSuccessChance(Actor npc, float intimacyAdjustment, float mostGold, float housesOwned, float horsesOwned, float questsCompleted, float dungeonsCleared, float dragonSoulsCollected) global native

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


;/ ========================================
   SECTION: Debug Functions
   ====================================== /;

;/ Log overall statistics about the marriage system /;
Function LogNPCStatistics() global native

;/ Log detailed information about a specific NPC /;
Function LogNPCDetails(Actor npc) global native


