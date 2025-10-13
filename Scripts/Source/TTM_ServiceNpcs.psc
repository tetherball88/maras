;/
  TTM_ServiceNpcs.psc

  Manages tracked NPCs, their relationship states, and related data for the MARAS mod.

  Main responsibilities:
    - Add, update, and remove tracked NPCs for all relationship states (candidate, engaged, married, jilted, divorced)
    - Manage lead spouses, and buckets for relationship logic
    - Integrate with JContainers for persistent storage
    - Provide utility functions for NPC home, objects, and status
/;
scriptname TTM_ServiceNpcs

import TTM_JCDomain

;/
  Marks an NPC as a candidate and adds to the candidate bucket.
  @param npc The NPC actor
/;
Function MakeNpcCandidate(Actor npc) global
    AddCandidate(npc)
EndFunction

;/
  Marks an NPC as engaged and adds to the fiances bucket.
  @param npc The NPC actor
/;
Function MakeNpcEngaged(Actor npc) global
    bool alwaysSkipWeddings = TTM_MCM_State.GetSkipWedding()
    bool answerSkipWedding
    if(TTM_JData.GetPlayerHadWedding() && !alwaysSkipWeddings)
        answerSkipWedding = TTM_Utils.ShowMessageMessage("Do you want skip Wedding Ceremony next time you get married? You can set in MCM to always skip weddings.")
    endif
    if(alwaysSkipWeddings || answerSkipWedding)
        TTM_Utils.FadeToBlack()
        Debug.Notification("Congratulations! You and " + TTM_Utils.GetActorName(npc) + " got married!")
        TTM_Utils.SendRelationshipChangeEvent(npc, "married")
        TTM_ServiceSkyrimNet.SimulatePostWeddingIfSkippedBehavior(npc)
        return
    endif
    Actor player = TTM_JData.GetPlayer()
    AddFiance(npc)
    npc.SetRelationshipRank(player, 3)
    TTM_ServiceMarriageQuest.StartEngagement(npc)
    if(npc.HasSpell(TTM_JData.GetBreakdownCooldownSpell()))
        npc.RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
    endif
EndFunction

;/
  Marks an NPC as married, triggers wedding logic.
  @param npc The NPC actor
/;
Function MakeNpcMarried(Actor npc) global
    Actor player = TTM_JData.GetPlayer()

    AddSpouse(npc)
    AddLeadSpouse(npc)

    npc.SetRelationshipRank(player, 4)
    ;re-evaluate spouses bonuses on each spouse added
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    ; it will check if npc who becomes spouse actually is current fiance with wedding about to happen
    TTM_ServiceMarriageQuest.SkipWedding(npc)

    ; from this moment spouse is supposed to share income
    SetLastTimeSharedIncome(npc)

    TTM_ServiceSpouseAssets.FindSpouseHome(npc)
    if(npc.HasSpell(TTM_JData.GetBreakdownCooldownSpell()))
        npc.RemoveSpell(TTM_JData.GetBreakdownCooldownSpell())
    endif
EndFunction

;/
  Marks an NPC as divorced, removes from spouse lists.
  @param npc The NPC actor
/;
Function MakeNpcDivorced(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    Faction playerFaction = TTM_JData.GetPlayerFaction()

    RemoveLeadSpouse(npc)
    AddDivorcee(npc)
    npc.SetRelationshipRank(player, -2)
    ;re-evaluate spouses bonuses on each spouse removed
    TTM_ServiceBuff.CalculatePermanentMultipliers()
    TTM_ServiceBuff.CalculateFollowerMultipliers()

    TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(npc)
    TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(npc)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_JData.GetBreakdownCooldownSpell())
EndFunction

;/
  Marks an NPC as jilted and resets marriage quests.
  @param npc The NPC actor
/;
Function MakeNpcJilted(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    AddJilted(npc)
    npc.SetRelationshipRank(player, -1)
    SetBrokeupTime(npc)
    npc.AddSpell(TTM_JData.GetBreakdownCooldownSpell())
EndFunction

;/
  Marks an NPC as deceased
  @param npc The NPC actor
/;
Function MakeNpcDeceased(Actor npc, bool isPlayerKiller) global
    bool spouse = TTM_Utils.IsSpouse(npc)
    bool engaged = TTM_Utils.IsFiance(npc)
    if(spouse || engaged)
        if(spouse)
            RemoveLeadSpouse(npc)
            ;re-evaluate spouses bonuses on each spouse removed
            TTM_ServiceBuff.CalculatePermanentMultipliers()
            TTM_ServiceBuff.CalculateFollowerMultipliers()
        endif

        if(isPlayerKiller)
            TTM_JData.SetPlayerKiller(isPlayerKiller)
            TTM_ServiceNpcs.SetKilledByPlayer(npc)
            TTM_ServiceSpouseAssets.StopShareHomeWithPlayer(npc)
        endif
    endif

    AddDeceased(npc)
    ; re-check marriage related quests and stop if any were ongoing for killed npc
    TTM_ServiceMarriageQuest.ResetMarriageQuests(npc)

EndFunction

; Manage TTM utility factions related to marriage status
Function ManageFactions(Actor npc, string status) global
    TTM_Debug.trace("ManageFactions:npc:"+npc+":"+status)
    TTM_Utils.SetRelationshipStatus(npc, status)
    if(status == "candidate")
        npc.AddToFaction(TTM_JData.GetMarriagePotentialFaction())
    elseif(status == "engaged")
        npc.AddToFaction(TTM_JData.GetMarriageAskedFaction())
        npc.AddToFaction(TTM_JData.GetCourtingFaction())
    elseif(status == "married")
        npc.RemoveFromFaction(TTM_JData.GetMarriageAskedFaction())
        npc.RemoveFromFaction(TTM_JData.GetCourtingFaction())
        ; double check if hirelings are still can be followers after marriage
        npc.RemoveFromFaction(TTM_JData.GetPotentialHirelingFaction())

        npc.AddToFaction(TTM_JData.GetMarriedFaction())
        npc.AddToFaction(TTM_JData.GetPlayerFaction())
        npc.AddToFaction(TTM_JData.GetPlayerBedOwnershipFaction())
    elseif(status == "jilted")
        npc.RemoveFromFaction(TTM_JData.GetMarriageAskedFaction())
        npc.RemoveFromFaction(TTM_JData.GetCourtingFaction())
    elseif(status == "divorced")
        npc.RemoveFromFaction(TTM_JData.GetMarriedFaction())
        npc.RemoveFromFaction(TTM_JData.GetPlayerFaction())
        npc.RemoveFromFaction(TTM_JData.GetPlayerBedOwnershipFaction())
    endif
EndFunction

Function ShareIncome(Actor spouse) global
    float currentTime = Utility.GetCurrentGameTime()
    float lastTime = GetLastTimeSharedIncome(spouse)
    float diff = currentTime - lastTime

    if(lastTime == -1)
        diff = 1
    endif

    TTM_Debug.trace("ShareIncome:"+diff+":"+lastTime)

    if(diff >= 1)
        int diffInt = diff as int
        TTM_JData.GetPlayer().AddItem(TTM_JData.GetGoldMisc(), diffInt * 100)
        SetLastTimeSharedIncome(spouse)
    endif
EndFunction

;/ ==============================
   SECTION: JContainers methods
============================== /;

;/ ==============================
   SECTION: Hierarchy
============================== /;
int Function GetHierarchy() global
    return TTM_JUtils._GetOrCreateJArray(TTM_JData.GetJSaveData(), "hierarchy")
EndFunction

Actor Function GetSpouseByHierarchyRank(int rank) global
    return JArray_getForm(GetHierarchy(), rank) as Actor
EndFunction

int Function GetSpouseRank(Actor spouse) global
    return spouse.GetFactionRank(TTM_JData.GetSpouseHierarchyFaction())
EndFunction

Function AddLeadSpouse(Actor spouse) global
    int jHierarchy = GetHierarchy()
    ; if not all top 3 spots are filled, add this spouse to the list
    if(JArray_count(jHierarchy) < 3)
        JArray_addForm(jHierarchy, spouse)
        spouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), JArray_count(jHierarchy) - 1)
        TTM_ServiceBuff.CalculateFollowerMultipliers()
        TTM_ServiceBuff.CalculatePermanentMultipliers()
        return
    endif
EndFunction

Function RemoveLeadSpouse(Actor spouse) global
    int jHierarchy = GetHierarchy()
    int jSpouses = GetSpouses()
    int currentRank = GetSpouseRank(spouse)
    ; if this spouse is not in the list, do nothing
    if(currentRank == -1)
        return
    endif

    ; remove this spouse from the lead list
    JArray_eraseForm(jHierarchy, spouse)
    spouse.RemoveFromFaction(TTM_JData.GetSpouseHierarchyFaction())

    ; attempt to find a new lead spouse
    Actor leadCandidateSpouse = JFormMap_nextKey(jSpouses) as Actor

    while(leadCandidateSpouse)
        if(leadCandidateSpouse != spouse && JArray_findForm(jHierarchy, leadCandidateSpouse) == -1)
            AddLeadSpouse(leadCandidateSpouse)
            TTM_Utils.ChangeLeadSpouseRankEvent(leadCandidateSpouse, JArray_count(jHierarchy) - 1, -1)
            return
        endif
        leadCandidateSpouse = JFormMap_nextKey(jSpouses, leadCandidateSpouse) as Actor
    endwhile

    TTM_ServiceBuff.CalculateFollowerMultipliers()
    TTM_ServiceBuff.CalculatePermanentMultipliers()
EndFunction

Function ChangeSpouseRank(Actor spouse, int newRank = -1) global
    int jHierarchy = GetHierarchy()
    int jSpouses = GetSpouses()

    ; if total number of spouses is less than 2, do nothing
    if(JArray_count(jSpouses) < 2)
        return
    endif

    ; if specific order is not specified, do nothing
    if(newRank == -1)
        return
    endif

    int currentRank = GetSpouseRank(spouse)

    ; if this spouse is already in the list, do nothing
    if(currentRank == newRank)
        return
    endif

    Actor newRankSpouse = JArray_getForm(jHierarchy, newRank) as Actor
    ; swap their places if this spouse was already on top 3 list
    JArray_setForm(jHierarchy, newRank, spouse)
    spouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), newRank)
    TTM_Debug.trace("ChangeSpouseRank:"+spouse+":"+newRank)
    TTM_Utils.ChangeLeadSpouseRankEvent(spouse, newRank, currentRank)
    if(newRankSpouse != none)
        if(currentRank == -1)
            newRankSpouse.RemoveFromFaction(TTM_JData.GetSpouseHierarchyFaction())
            TTM_Debug.trace("ChangeSpouseRank:"+newRankSpouse+":-1")
        else
            JArray_setForm(jHierarchy, currentRank, newRankSpouse)
            newRankSpouse.SetFactionRank(TTM_JData.GetSpouseHierarchyFaction(), currentRank)
            TTM_Debug.trace("ChangeSpouseRank:"+newRankSpouse+":"+currentRank)
        endif
        TTM_Utils.ChangeLeadSpouseRankEvent(newRankSpouse, currentRank, newRank)
    endif
    TTM_ServiceBuff.CalculateFollowerMultipliers()
    TTM_ServiceBuff.CalculatePermanentMultipliers()
EndFunction

;/ ==============================
   SECTION: trackedNpcs map
============================== /;
int Function GetTrackedNpcs() global
    return TTM_JUtils._GetOrCreateJFormMap(TTM_JData.GetJSaveData(), "trackedNpcs")
EndFunction

int Function CountTrackedNpcs() global
    return JFormMap_count(GetTrackedNpcs())
EndFunction

Actor Function NextTrackedNpcs(Actor prev = none) global
    return JFormMap_nextKey(GetTrackedNpcs(), prev) as Actor
EndFunction

int Function CreateNewTrackedNpc(Actor npc) global
    Actor player = TTM_JData.GetPlayer()
    int jNpc = JMap_object()
    TTM_debug.trace("CreateNewTrackedNpc:"+npc+":"+jNpc)
    TTM_ServiceSpouseTypes.DetermineSpouseType(npc)
    if(!npc.IsInFaction(TTM_JData.GetTrackedNpcFaction()))
        npc.AddToFaction(TTM_JData.GetTrackedNpcFaction())
    endif


    JMap_setStr(jNpc, "name", TTM_Utils.GetActorName(npc))
    JMap_setFlt(jNpc, "lastTimeSharedIncome", -1)
    JMap_setObj(jNpc, "existingRelationships", JArray_object())

    TTM_ServiceLoversLedger.UpdateNpcCurrentRelationships(jNpc, npc)

    JFormMap_setObj(GetTrackedNpcs(), npc, jNpc)

    return jNpc
EndFunction

int Function AddTrackedNpc(Actor npc) global
    TTM_Debug.trace("AddTrackedNpc:"+npc)
    int jNpcs = GetTrackedNpcs()

    if(!JFormMap_hasKey(jNpcs, npc))
        int jNpc = CreateNewTrackedNpc(npc)

        return jNpc
    endif

    return 0
EndFunction

int Function GetTrackedNpc(Actor npc) global
    TTM_Debug.trace("GetTrackedNpc:"+npc)
    int jNpc = JFormMap_getObj(GetTrackedNpcs(), npc)
    if(jNpc == 0)
        jNpc = AddTrackedNpc(npc)
    endif

    return jNpc
EndFunction

ObjectReference Function GetTrackedNpcHomeMarker(Actor npc) global
    int jNpc = GetTrackedNpc(npc)
    if(JMap_hasKey(jNpc, "homeMarker"))
        return JMap_getForm(jNpc, "homeMarker") as ObjectReference
    else
        ; create unique marker for npc which will be used for after marriage sandboxing package
        ObjectReference homeMarker = npc.PlaceAtMe(TTM_JData.GetHomeSandboxMarkerStatic(), 1, true)
        ; link npc and marker by keyword which is used by package
        PO3_SKSEFunctions.SetLinkedRef(npc, homeMarker, TTM_JData.GetHomeSandboxKeyword())
        JMap_setForm(jNpc, "homeMarker", homeMarker)
        return homeMarker
    endif
EndFunction


Function SetTrackedNpcHome(Actor npc, Location home) global
    JMap_setForm(GetTrackedNpc(npc), "home", home)
EndFunction

Location Function GetTrackedNpcHome(Actor npc) global
    return JMap_getForm(GetTrackedNpc(npc), "home") as Location
EndFunction

Function SetTrackedNpcMcmTypeChanged(Actor npc) global
    int jNpc = GetTrackedNpc(npc)
    if(jNpc != 0)
        JMap_setInt(jNpc, "mcmTypeChanged", 1)
    endif
EndFunction

bool Function GetTrackedNpcMcmTypeChanged(Actor npc) global
    return JMap_getInt(GetTrackedNpc(npc), "mcmTypeChanged") == 1
EndFunction

int Function GetTrackedNpcCells(Actor npc) global
    return TTM_JUtils._GetOrCreateJArray(GetTrackedNpc(npc), "ownedCells")
EndFunction

Function AddTrackedNpcCell(Actor npc, Cell currentCell) global
    int jNpcCells = GetTrackedNpcCells(npc)

    if(JArray_findForm(jNpcCells, currentCell) == -1)
        JArray_addForm(jNpcCells, currentCell)
    endif
EndFunction

bool Function TrackedNpcHasCell(Actor npc, Cell currentCell) global
    return JArray_findForm(GetTrackedNpcCells(npc), currentCell) != -1
EndFunction

int Function GetTrackedNpcObjects(Actor npc) global
    return TTM_JUtils._GetOrCreateJArray(GetTrackedNpc(npc), "ownedObjects")
EndFunction

Function AddTrackedNpcObject(Actor npc, ObjectReference obj) global
    int jNpcObjects = GetTrackedNpcObjects(npc)

    if(JArray_findForm(jNpcObjects, obj) == -1)
        JArray_addForm(jNpcObjects, obj)
    endif
EndFunction

bool Function TrackedNpcHasObject(Actor npc, ObjectReference obj) global
    return JArray_findForm(GetTrackedNpcObjects(npc), obj) != -1
EndFunction

Function SetLastTimeSharedIncome(Actor npc) global
    JMap_setFlt(GetTrackedNpc(npc), "lastTimeSharedIncome", Utility.GetCurrentGameTime())
EndFunction

float Function GetLastTimeSharedIncome(Actor npc) global
    float lastTime = JMap_getFlt(GetTrackedNpc(npc), "lastTimeSharedIncome")
    if(lastTime == 0.0)
        return -1.0
    endif
    return lastTime
EndFunction

Function SetBrokeupTime(Actor npc) global
    JMap_setFlt(GetTrackedNpc(npc), "brokeupTime", Utility.GetCurrentGameTime())
EndFunction

float Function GetBrokeupTime(Actor npc) global
    return JMap_getFlt(GetTrackedNpc(npc), "brokeupTime")
EndFunction

Function SetKilledByPlayer(Actor npc) global
    JMap_setInt(GetTrackedNpc(npc), "killedByPlayer", 1)
EndFunction

bool Function GetKilledByPlayer(Actor npc) global
    return JMap_getInt(GetTrackedNpc(npc), "killedByPlayer") == 1
EndFunction

;/ ==============================
   SECTION: BUCKETS
============================== /;

int Function GetBucket(string type) global
    int jBuckets = TTM_JUtils._GetOrCreateJMap(TTM_JData.GetJSaveData(), "buckets")
    return TTM_JUtils._GetOrCreateJArray(jBuckets, type)
EndFunction

Function RemoveFromBucket(string type, Actor npc) global
    int jBucket = GetBucket(type)

    JArray_eraseForm(jBucket, npc)
EndFunction

Function AddToBucket(string type, Actor npc) global
    int jBucket = GetBucket(type)

    if(JArray_findForm(jBucket, npc) == -1)
        JArray_addForm(jBucket, npc)
    endif
EndFunction

Function AddToBucketAndCleanFromOthers(string type, Actor npc) global
    string[] buckets = new string[7]
    buckets[0] = "candidate"
    buckets[1] = "engaged"
    buckets[2] = "married"
    buckets[3] = "jilted"
    buckets[4] = "divorced"
    buckets[5] = "lovers"
    buckets[6] = "deceased"
    int i = 0

    while(i < buckets.Length)
        if(buckets[i] == type)
            AddToBucket(type, npc)
        else
            RemoveFromBucket(buckets[i], npc)
        endif
        i += 1
    endwhile

    CountLoveInterests()
EndFunction

int Function CountBucket(string type) global
    return JArray_count(GetBucket(type))
EndFunction

Form[] Function GetAllActorsFromBucket(string type) global
    return JArray_asFormArray(GetBucket(type))
EndFunction

;/ ==============================
   SECTION: candidate bucket map
============================== /;
int Function GetCandidates() global
    return GetBucket("candidate")
EndFunction

; Helper function to reduce code duplication for bucket management
Function AddNpcToBucketAndSetStatus(string status, Actor npc) global
    if(npc == none)
        TTM_Debug.trace("[ERROR] AddNpcToBucketAndSetStatus: npc is none")
        return
    endif
    int jBucket = GetBucket(status)
    if(jBucket == 0)
        TTM_Debug.trace("[ERROR] AddNpcToBucketAndSetStatus: bucket '" + status + "' not found")
        return
    endif
    if(JArray_findForm(jBucket, npc) != -1)
        return
    endif
    AddToBucketAndCleanFromOthers(status, npc)
EndFunction

bool Function IsInBucket(Actor npc, string status) global
    return JArray_findForm(GetBucket(status), npc) != -1
EndFunction

Function AddCandidate(Actor candidate) global
    AddNpcToBucketAndSetStatus("candidate", candidate)
EndFunction

Function IsCandidate(Actor candidate) global
    IsInBucket(candidate, "candidate")
EndFunction

;/ ==============================
   SECTION: fiances bucket map
============================== /;
int Function GetFiances() global
    return GetBucket("engaged")
EndFunction

; Refactored AddFiance
Function AddFiance(Actor fiance) global
    AddNpcToBucketAndSetStatus("engaged", fiance)
EndFunction

Actor Function GetNextFiance() global
    return JArray_getForm(GetFiances(), 0) as Actor
EndFunction

;/ ==============================
   SECTION: spouses bucket map
============================== /;
int Function GetSpouses() global
    return GetBucket("married")
EndFunction

int Function GetSpousesCount() global
    return CountBucket("married")
EndFunction

; Refactored AddSpouse
Function AddSpouse(Actor spouse) global
    AddNpcToBucketAndSetStatus("married", spouse)
EndFunction

int Function GetSpouse(Actor spouse) global
    int jSpouses = GetSpouses()
    if(JArray_findForm(jSpouses, spouse) == -1)
        return 0
    endif
    return GetTrackedNpc(spouse)
EndFunction

Actor Function GetRandomSpouse() global
    int count = GetSpousesCount()
    if(count == 0)
        return none
    endif
    int index = Utility.RandomInt(0, count - 1)
    Actor spouse = JArray_getForm(GetSpouses(), index) as Actor
    return spouse
EndFunction

;/ ==============================
   SECTION: divorcees bucket map
============================== /;
int Function GetDivorcees() global
    return GetBucket("divorced")
EndFunction

; Refactored AddDivorcee
Function AddDivorcee(Actor divorcee) global
    AddNpcToBucketAndSetStatus("divorced", divorcee)
EndFunction

;/ ==============================
   SECTION: jilted bucket map
============================== /;
int Function GetJilted() global
    return GetBucket("jilted")
EndFunction

; Refactored AddJilted
Function AddJilted(Actor jilted) global
    AddNpcToBucketAndSetStatus("jilted", jilted)
EndFunction

;/ ==============================
   SECTION: deceased bucket map
============================== /;
int Function GetDeceased() global
    return GetBucket("deceased")
EndFunction

; Refactored AddJilted
Function AddDeceased(Actor deceased) global
    AddNpcToBucketAndSetStatus("deceased", deceased)
EndFunction

bool Function IsDeceased(Actor npc) global
    return IsInBucket(npc, "deceased")
EndFunction

Function CountLoveInterests() global
    int count = CountBucket("engaged") + CountBucket("married")
    TTM_JData.GetSetSpouseCountGlobal(count)
EndFunction
