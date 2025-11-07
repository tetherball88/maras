scriptname TTM_Utils

import TTM_JCDomain

;/ =============================
   SECTION: EVENTS
============================== /;

Function SendRelationshipChangeEvent(Actor candidate, string status) global
    if(!TTM_Utils.IsSpouse(candidate) && status == "divorced" || !TTM_Utils.IsFiance(candidate) && status == "jilted" || TTM_Utils.IsSpouse(candidate) && status == "married" || TTM_Utils.IsFiance(candidate) && status == "engaged")
        return
    endif
    int handle = ModEvent.Create("TTM_SpouseRelationshipChanged")
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("SendRelationshipChangeEvent:"+handle+":"+TTM_Utils.GetActorName(candidate)+":"+status)
    endif
    if(handle)
        ModEvent.PushForm(handle, candidate as Form)
        ModEvent.PushString(handle, status)
        ModEvent.Send(handle)
    endif
EndFunction

string Function SendAffectionChangeThresholdEvent(Actor spouse, string level, bool up) global
    int handle = ModEvent.Create("TTM_SpouseAffectionChanged")
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("SendAffectionChangeThresholdEvent:"+handle+":"+TTM_Utils.GetActorName(spouse)+":"+level+":"+up)
    endif
    if(handle)
        ModEvent.PushForm(handle, spouse as Form)
        ModEvent.PushString(handle, level)
        ModEvent.PushBool(handle, up)
        ModEvent.Send(handle)
    endif
EndFunction

Function SendChangeLeadSpouseRankEvent(Actor spouse, int newRank, int oldRank) global
    int handler = ModEvent.Create("TTM_ChangeLeadSpouseRankEvent")
    if(TTM_Debug.IsTrace())
        TTM_Debug.trace("SendChangeLeadSpouseRankEvent:"+handler+":"+TTM_Utils.GetActorName(spouse)+":"+newRank+":"+oldRank)
    endif
    if(handler)
        ModEvent.PushForm(handler, spouse as Form)
        ModEvent.PushInt(handler, newRank)
        ModEvent.PushInt(handler, oldRank)
        ModEvent.Send(handler)
    endif
EndFunction

;/ =============================
   SECTION: SPOUSE TYPES
============================== /;

; in return array: 0 - warrior, 1 - mage, 2 - rogue, 3 - craftsman, 4 - ranger, 5 - orator
string[] Function GetSpouseSkillTypeByIndexes() global
    return JArray_asStringArray(TTM_JMethods.GetObjStaticData("initialData.SpouseSkillTypes"))
EndFunction

int Function GetSpouseSkillIndexByType(string type) global
    return JArray_findStr(TTM_JMethods.GetObjStaticData("initialData.SpouseSkillTypes"), type)
EndFunction

; in return array: 0 - outcast, 1 - poverty, 2 - working, 3 - middle, 4 - wealthy, 5 - religious, 6 - nobles, 7 - rulers
string[] Function GetSpouseSocialTypeByIndexes() global
    return JArray_asStringArray(TTM_JMethods.GetObjStaticData("initialData.SpouseSocialClasses"))
EndFunction

int Function GetSpouseSocialIndexByType(string type) global
    return JArray_findStr(TTM_JMethods.GetObjStaticData("initialData.SpouseSocialClasses"), type)
EndFunction

; in return array: 0 - Proud, 1 - Humble, 2 - Jealous, 3 - Romantic, 4 - Independent
string[] Function GetSpouseTemperamentByIndexes() global
    return JArray_asStringArray(TTM_JMethods.GetObjStaticData("initialData.SpouseTemperaments"))
EndFunction

int Function GetSpouseTemperamentIndexByType(string type) global
    return JArray_findStr(TTM_JMethods.GetObjStaticData("initialData.SpouseTemperaments"), type)
EndFunction


string Function GetActorName(actor akActor) global
    if akActor == TTM_JData.GetPlayer()
      return akActor.GetActorBase().GetName()
    else
      return akActor.GetDisplayName()
    EndIf
EndFunction

Actor Function GetActorAlias(Quest qst, string name) global
    ReferenceAlias al = qst.GetAliasByName(name) as ReferenceAlias
    return al.GetActorRef()
EndFunction

int Function GetSpouseSocialClassIndex(Actor spouse) global
    return spouse.GetFactionRank(TTM_JData.GetSpouseSocialClassFaction())
EndFunction

string Function GetSpouseSocialClass(Actor spouse) global
    return GetSpouseSocialTypeByIndexes()[GetSpouseSocialClassIndex(spouse)]
EndFunction

string Function GetSpouseSkillType(Actor spouse) global
    return GetSpouseSkillTypeByIndexes()[GetSpouseSkillTypeIndex(spouse)]
EndFunction

int Function GetSpouseSkillTypeIndex(Actor spouse) global
    return spouse.GetFactionRank(TTM_JData.GetSpouseSkillTypeFaction())
EndFunction

string Function GetSpouseTemperament(Actor spouse) global
    return GetSpouseTemperamentByIndexes()[GetSpouseTemperamentIndex(spouse)]
EndFunction

int Function GetSpouseTemperamentIndex(Actor spouse) global
    return spouse.GetFactionRank(TTM_JData.GetSpouseTemperamentFaction())
EndFunction

int Function GetTrackingRank(Actor npc) global
    return npc.GetFactionRank(TTM_JData.GetTrackedNpcFaction())
EndFunction

string Function GetRelationshipStatus(Actor npc) global
    int rank = GetTrackingRank(npc)
    if(rank == 0)
        return "candidate"
    elseif(rank == 10)
        return "engaged"
    elseif(rank == 20)
        return "married"
    elseif(rank == 30)
        return "divorced"
    elseif(rank == 40)
        return "jilted"
    endif
EndFunction

string Function SetRelationshipStatus(Actor npc, string status) global
    int rank = 0
    if(status == "engaged")
        rank = 10
    elseif(status == "married")
        rank = 20
    elseif(status == "divorced")
        rank = 30
    elseif(status == "jilted")
        rank = 40
    endif

    npc.SetFactionRank(TTM_JData.GetTrackedNpcFaction(), rank)
EndFunction

bool Function IsTracking(Actor npc) global
    return npc.IsInFaction(TTM_JData.GetTrackedNpcFaction())
EndFunction

bool Function IsFiance(Actor npc) global
    return GetRelationshipStatus(npc) == "engaged"
EndFunction

bool Function IsSpouse(Actor npc) global
    return GetRelationshipStatus(npc) == "married"
EndFunction

bool Function IsDivorced(Actor npc) global
    return GetRelationshipStatus(npc) == "divorced"
EndFunction

bool Function IsJilted(Actor npc) global
    return GetRelationshipStatus(npc) == "jilted"
EndFunction

int Function ToggleGlobalVariable(GlobalVariable gVar) global
    int val = gVar.GetValueInt()
    int newVal = 0
    if(val == 0)
        newVal = 1
    endif
    gVar.SetValueInt(newVal)

    return newVal
EndFunction

string Function GetActorsNamesJson(Actor[] actors, Actor excluded = none) global
    if(actors.length == 0)
        return ""
    endif
    string actorNames = ""
    int i = 0

    int aLength = actors.length

    while(i < actors.length)
        if(i != 0)
            actorNames += ", "
        endif

        Actor currentActor = actors[i]

        if(currentActor && currentActor != excluded)
            actorNames += GetActorName(currentActor)
        endif

        i += 1
    endwhile

    return actorNames
EndFunction

bool Function ShowMessageMessage(string msg) global
    string result = SkyMessage.Show(msg, "Yes", "No")
    if(result != "Yes" && result != "No")
        Debug.Notification("Dynamic Message Box Not Found (Please install Papyrus MessageBox - SKSE NG plugin)")
        return false
    endif

    return result == "Yes"
EndFunction

Function FadeToBlack() global
    ImageSpaceModifier fadeToBlack = Game.GetFormFromFile(0xf756d, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackHold = Game.GetFormFromFile(0xf756e, "Skyrim.esm") as ImageSpaceModifier
    ImageSpaceModifier fadeToBlackBack = Game.GetFormFromFile(0xf756f, "Skyrim.esm") as ImageSpaceModifier

    fadeToBlack.Apply()
    Utility.Wait(2.0)
    fadeToBlack.PopTo(fadeToBlackHold)
    Utility.Wait(2.0)
    fadeToBlackHold.PopTo(fadeToBlackBack)
EndFunction

float Function GetMax(float value1, float value2) global
    if(value1 > value2)
        return value1
    endif

    return value2
EndFunction

float Function GetMin(float value1, float value2) global
    if(value1 < value2)
        return value1
    endif

    return value2
EndFunction

bool Function CandidateIsReadyToHearProposal(Actor npc) global
    int socialClass = GetSpouseSocialClassIndex(npc)
    int skillType = GetSpouseSkillTypeIndex(npc)
    return socialClass != -1 && skillType != -1
EndFunction

bool Function CandidateIsReadyToHearProposalAwait(Actor npc, int attempts = 5, float wait = 0.3) global
    bool isSpouseNpc = IsSpouse(npc)
    bool isFianceNpc = IsFiance(npc)
    bool isPlayer = npc == TTM_JData.GetPlayer()
    bool isReady = CandidateIsReadyToHearProposal(npc)
    if(isPlayer || ((isSpouseNpc || isFianceNpc) && isReady))
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CandidateIsReadyToHearProposalAwait: false; isPlayer:"+isPlayer+"; isSpouseNpc:"+isSpouseNpc+"; isFianceNpc:"+isFianceNpc)
        endif
        return false
    endif
    int i = 0

    while(i < attempts && !isReady)
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("CandidateIsReadyToHearProposalAwait: attempt "+i+"; isReady:"+isReady)
        endif
        Utility.Wait(wait)
        isReady = CandidateIsReadyToHearProposal(npc)
        i += 1
    endwhile

    return isReady
EndFunction
