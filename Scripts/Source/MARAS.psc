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

;/ ========================================
   SECTION: Debug Functions
   ====================================== /;

;/ Log overall statistics about the marriage system /;
Function LogNPCStatistics() global native

;/ Log detailed information about a specific NPC /;
Function LogNPCDetails(Actor npc) global native
