scriptname TTM_ServiceSpouseTypes

import TTM_JCDomain

Function Maintenance() global
    ImportJson()

    CheckOverrides()
EndFunction

Function CheckOverrides() global
    int JTypes = JDB_solveObj(GetTypesNamespace())
    Actor npc = JFormMap_nextKey(JTypes) as Actor

    while(npc)
        if(TTM_Utils.IsTracking(npc) && !TTM_ServiceNpcs.GetTrackedNpcMcmTypeChanged(npc))
            string overrideSocial = GetSocialClassOverride(npc)
            SetSpouseSocialClass(npc, overrideSocial)

            string overrideSkill = GetSkillTypeOverride(npc)
            SetSpouseSkillType(npc, overrideSkill)
        endif
        npc = JFormMap_nextKey(JTypes, npc) as Actor
    endwhile
EndFunction

int Function DetermineSpouseType(Actor spouse) global
    TTM_Debug.trace("DetermineSpouseType:"+spouse)
    SetSpouseSkillType(spouse, DetermineSkillType(spouse))
    SetSpouseSocialClass(spouse, DetermineSocialClass(spouse))
EndFunction

Function SetSpouseSocialClass(Actor npc, string socialClass) global
    if(socialClass != "none")
        int socialIndex = TTM_Utils.GetSpouseSocialIndexByType(socialClass)
        if(socialIndex != -1)
            Faction socialClassFaction = TTM_JData.GetSpouseSocialClassFaction()
            if(npc.IsInFaction(socialClassFaction))
                npc.AddToFaction(socialClassFaction)
            endif
            npc.SetFactionRank(socialClassFaction, socialIndex)
            TTM_Debug.trace("DetermineSpouseType:"+npc+":SocialClass:"+socialIndex)
        endif
    endif
EndFunction

Function SetSpouseSkillType(Actor npc, string skillType) global
    if(skillType != "none")
        int skillIndex = TTM_Utils.GetSpouseSkillIndexByType(skillType)
        if(skillIndex != -1)
            Faction skillTypeFaction = TTM_JData.GetSpouseSkillTypeFaction()
            if(npc.IsInFaction(skillTypeFaction))
                npc.AddToFaction(skillTypeFaction)
            endif
            npc.SetFactionRank(skillTypeFaction, skillIndex)
            TTM_Debug.trace("DetermineSpouseType:"+npc+":SkillType:"+skillIndex)
        endif
    endif
EndFunction

string Function DetermineSocialClass(Actor spouse) global
    string socialClass = GetSocialClassOverride(spouse)
    if(socialClass != "none")
        return socialClass
    endif

    return GetSocialClassByFaction(spouse)
EndFunction

string Function DetermineSkillType(Actor spouse) global
    string skillType = GetSkillTypeOverride(spouse)

    if(skillType == "none")
        skillType = DetermineSkillClassBased(spouse)
    endif

    if(skillType == "none")
        skillType = DetermineSkillBased(spouse)
    endif

    return skillType
EndFunction

string Function DetermineSkillClassBased(Actor spouse) global
    Class spouseClass = spouse.GetActorBase().GetClass()
    if(checkByClass("orator", spouseClass))
        return "orator"
    elseif(checkByClass("ranger", spouseClass))
        return "ranger"
    elseif(checkByClass("rogue", spouseClass))
        return "rogue"
    elseif(checkByClass("craftsman", spouseClass))
        return "craftsman"
    elseif(checkByClass("mage", spouseClass))
        return "mage"
    elseif(checkByClass("warrior", spouseClass))
        return "warrior"
    endif

    return "none"
EndFunction

string Function DetermineSkillBased(Actor spouse) global
    float[] skills = new float[18]

    skills[0] = spouse.GetActorValue("OneHanded")
    skills[1] = spouse.GetActorValue("TwoHanded")
    skills[2] = spouse.GetActorValue("Marksman")
    skills[3] = spouse.GetActorValue("Block")
    skills[4] = spouse.GetActorValue("Smithing")
    skills[5] = spouse.GetActorValue("HeavyArmor")
    skills[6] = spouse.GetActorValue("LightArmor")
    skills[7] = spouse.GetActorValue("Pickpocket")
    skills[8] = spouse.GetActorValue("Lockpicking")
    skills[9] = spouse.GetActorValue("Sneak")
    skills[10] = spouse.GetActorValue("Alchemy")
    skills[11] = spouse.GetActorValue("Speechcraft")
    skills[12] = spouse.GetActorValue("Alteration")
    skills[13] = spouse.GetActorValue("Conjuration")
    skills[14] = spouse.GetActorValue("Destruction")
    skills[15] = spouse.GetActorValue("Illusion")
    skills[16] = spouse.GetActorValue("Restoration")
    skills[17] = spouse.GetActorValue("Enchanting")

    int i = 1
    int maxIndex = 0
    float maxVal = skills[0]

    while i < skills.Length
        if(skills[i] > maxVal)
            maxIndex = i
            maxVal = skills[i]
        endif
        i += 1
    endwhile

    if(maxIndex == 0 || maxIndex == 1 || maxIndex == 3 || maxIndex == 5)
        return "warrior"
    elseif(maxIndex == 2)
        return "ranger"
    elseif(maxIndex == 7 || maxIndex == 8 || maxIndex == 9)
        return "rogue"
    elseif(maxIndex == 12 || maxIndex == 13 || maxIndex == 14 || maxIndex == 15 || maxIndex == 16)
        return "mage"
    elseif(maxIndex == 11)
        return "orator"
    elseif(maxIndex == 4 || maxIndex == 10 || maxIndex == 17)
        return "craftsman"
    elseif(maxIndex == 6)
        ; if marksman is higher than lockpick and pickpocket - archer
        ; otherwise rogue
        If (skills[2] > skills[7] && skills[2] > skills[8])
            return "ranger"
        else
            return "rogue"
        EndIf
    endif
EndFunction

string Function GetSocialClassByFaction(Actor spouse) global
    Faction[] spouseFactions = spouse.GetFactions(-100, 100)
    FormList rulerFactions = TTM_JData.GetSpouseTypesFactionsByType("rulers")
    FormList nobleFactions = TTM_JData.GetSpouseTypesFactionsByType("nobles")
    FormList religiousFactions = TTM_JData.GetSpouseTypesFactionsByType("religious")
    FormList wealthyFactions = TTM_JData.GetSpouseTypesFactionsByType("wealthy")
    FormList middleFactions = TTM_JData.GetSpouseTypesFactionsByType("middle")
    FormList povertyFactions = TTM_JData.GetSpouseTypesFactionsByType("poverty")
    FormList outcastFactions = TTM_JData.GetSpouseTypesFactionsByType("outcast")
    int i = 0
    int maxClassIndex = 0

    while i < spouseFactions.Length
        Faction spouseFaction = spouseFactions[i]

        if(rulerFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("rulers")) as int
        elseif(nobleFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("nobles")) as int
        elseif(religiousFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("religious")) as int
        elseif(wealthyFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("wealthy")) as int
        elseif(middleFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("middle")) as int
        elseif(povertyFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("poverty")) as int
        elseif(outcastFactions.HasForm(spouseFaction))
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("outcast")) as int
        else
            maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("working")) as int

            if(spouse.WornHasKeyword(TTM_JData.GetGameKeyword("clothingRich")))
                maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("wealthy")) as int
            endif
        endif
        i += 1
    endwhile

    return TTM_Utils.GetSpouseSocialTypeByIndexes()[maxClassIndex]
EndFunction

; bool Function checkByFaction(string type, Faction[] spouseFactions) global
;     FormList factions = TTM_JData.GetSpouseTypesFactionsByType(type)
;     int i = 0

;     while i < spouseFactions.Length
;         TTM_Debug.trace("checkByFaction:"+type+":"+spouseFactions[i])
;         if(factions.HasForm(spouseFactions[i]))
;             return true
;         endif
;         i += 1
;     endwhile

;     return false
; EndFunction

bool Function checkByClass(string type, Class spouseClass) global
    FormList classes = TTM_JData.GetSpouseTypesClassesByType(type)
    return classes.HasForm(spouseClass)
EndFunction

;/ ==============================
   SECTION: JContainers functions
============================== /;

string Function GetTypesNamespace() global
    return TTM_JData.GetInitialDataKey() + ".spouseTypeOverrides"
EndFunction

Function ImportJson() global
    int JTypesFromFiles = TTM_JUtils.LoadMultipleFiles("Data/SKSE/Plugins/MARAS/spousesTypes", true)
    JDB_solveObjSetter(GetTypesNamespace(), JTypesFromFiles, true)
EndFunction

int Function GetActor(Actor npc) global
    int JTypes = JDB_solveObj(GetTypesNamespace())
    return JFormMap_getObj(JTypes, npc)
EndFunction

string Function GetSocialClassOverride(Actor npc) global
    return JMap_getStr(GetActor(npc), "social", "none")
EndFunction

string Function GetSkillTypeOverride(Actor npc) global
    return JMap_getStr(GetActor(npc), "skill", "none")
EndFunction
