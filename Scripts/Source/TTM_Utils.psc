scriptname TTM_Utils

import TTM_JCDomain

string Function GetActorName(actor akActor) global
    if akActor == TTM_JData.GetPlayer()
      return akActor.GetActorBase().GetName()
    else
      return akActor.GetDisplayName()
    EndIf
EndFunction

string Function SendRelationshipChangeEvent(Actor candidate, string status) global
    int handle = ModEvent.Create("TTM_SpouseRelationshipChanged")
    TTM_debug.trace("SendRelationshipChangeEvent:"+handle+":"+candidate+":"+status)
    if(handle)
        ModEvent.PushForm(handle, candidate as Form)
        ModEvent.PushString(handle, status)
        ModEvent.Send(handle)
    endif
EndFunction



bool Function SpouseOwnsCell(Actor spouse, Cell currentCell) global
    ActorBase ownerAB = currentCell.GetActorOwner()
    Faction ownerF = currentCell.GetFactionOwner()

    if(ownerAB && spouse.GetActorBase() == ownerAB)
        return true
    endif

    if(ownerF && spouse.IsInFaction(ownerF))
        return true
    endif

    return false
EndFunction

bool Function SpouseOwnsObject(Actor spouse, ObjectReference obj) global
    ActorBase ownerAB = obj.GetActorOwner()
    Faction ownerF = obj.GetFactionOwner()

    if(ownerAB && spouse.GetActorBase() == ownerAB)
        return true
    endif

    if(ownerF && spouse.IsInFaction(ownerF))
        return true
    endif

    return false
EndFunction

Function ChangeLeadSpouseRankEvent(Actor spouse, int newRank, int oldRank) global
    int handler = ModEvent.Create("TTM_ChangeLeadSpouseRankEvent")
    if(handler)
        ModEvent.PushForm(handler, spouse as Form)
        ModEvent.PushInt(handler, newRank)
        ModEvent.PushInt(handler, oldRank)
        ModEvent.Send(handler)
    endif
EndFunction

;/ in return arra:
0 - warrior,
1 - mage,
2 - rogue,
3 - craftsman,
4 - ranger,
5 - orator
/;
string[] Function GetTrackedNpcSkillTypeByIndexes() global
    string[] spouseTypes = Utility.CreateStringArray(6)
    spouseTypes[0] = "warrior"
    spouseTypes[1] = "mage"
    spouseTypes[2] = "rogue"
    spouseTypes[3] = "craftsman"
    spouseTypes[4] = "ranger"
    spouseTypes[5] = "orator"

    return spouseTypes
EndFunction

int Function GetSpouseSkillIndexByType(string type) global
    if(type == "warrior")
        return 0
    elseif(type == "mage")
        return 1
    elseif(type == "rogue")
        return 2
    elseif(type == "craftsman")
        return 3
    elseif(type == "ranger")
        return 4
    elseif(type == "orator")
        return 5
    endif

    return -1
EndFunction

;/
0 - outcast,
1 - poverty,
2 - working,
3 - middle,
4 - wealthy,
5 - religious,
6 - nobles,
7 - rulers
/;
string[] Function GetSpouseSocialTypeByIndexes() global
    string[] spouseTypes = Utility.CreateStringArray(8)
    spouseTypes[0] = "outcast"
    spouseTypes[1] = "poverty"
    spouseTypes[2] = "working"
    spouseTypes[3] = "middle"
    spouseTypes[4] = "wealthy"
    spouseTypes[5] = "religious"
    spouseTypes[6] = "nobles"
    spouseTypes[7] = "rulers"

    return spouseTypes
EndFunction

int Function GetSpouseSocialIndexByType(string type) global
    if(type == "outcast")
        return 0
    elseif(type == "poverty")
        return 1
    elseif(type == "working")
        return 2
    elseif(type == "middle")
        return 3
    elseif(type == "wealthy")
        return 4
    elseif(type == "religious")
        return 5
    elseif(type == "nobles")
        return 6
    elseif(type == "rulers")
        return 7
    endif

    return -1
EndFunction

Actor Function GetActorAlias(Quest qst, string name) global
    ReferenceAlias al = qst.GetAliasByName(name) as ReferenceAlias
    return al.GetActorRef()
EndFunction

int Function GetSpouseSocialClassIndex(Actor spouse) global
    Faction socialClassFaction = TTM_JData.GetSpouseSocialClassFaction()
    int index = spouse.GetFactionRank(socialClassFaction)

    return index
EndFunction

string Function GetSpouseSocialClass(Actor spouse) global
    int index = GetSpouseSocialClassIndex(spouse)

    return GetSpouseSocialTypeByIndexes()[index]
EndFunction

string Function GetSpouseSkillType(Actor spouse) global
    int index = GetSpouseSkillTypeIndex(spouse)

    return GetTrackedNpcSkillTypeByIndexes()[index]
EndFunction

int Function GetSpouseSkillTypeIndex(Actor spouse) global
    Faction skillTypeFaction = TTM_JData.GetSpouseSkillTypeFaction()
    int index = spouse.GetFactionRank(skillTypeFaction)

    return index
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

string Function GetBoolStr(bool flag) global
    if(flag)
        return "true"
    else
        return "false"
    endif
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

string Function GetActorsNamesJson(Form[] actors, Actor excluded = none) global
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

        Actor currentActor = actors[i] as Actor

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
        TTM_Debug.trace("CandidateIsReadyToHearProposalAwait: false; isPlayer:"+isPlayer+"; isSpouseNpc:"+isSpouseNpc+"; isFianceNpc:"+isFianceNpc)
        return false
    endif
    int i = 0

    while(i < attempts && !isReady)
        TTM_Debug.trace("CandidateIsReadyToHearProposalAwait: attempt "+i+"; isReady:"+isReady)
        Utility.Wait(wait)
        isReady = CandidateIsReadyToHearProposal(npc)
        i += 1
    endwhile

    return isReady
EndFunction
