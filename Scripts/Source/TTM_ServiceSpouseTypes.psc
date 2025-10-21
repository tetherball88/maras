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
        if(TTM_Utils.IsTracking(npc) && !TTM_ServiceRelationships.GetTrackedNpcMcmTypeChanged(npc))
            string overrideSocial = GetSocialClassOverride(npc)
            SetSpouseSocialClass(npc, overrideSocial)

            string overrideSkill = GetSkillTypeOverride(npc)
            SetSpouseSkillType(npc, overrideSkill)

            string overrideTemperament = GetTemperamentOverride(npc)
            SetSpouseTemperament(npc, overrideTemperament)
        endif
        npc = JFormMap_nextKey(JTypes, npc) as Actor
    endwhile
EndFunction

int Function DetermineSpouseType(Actor spouse) global
    TTM_Debug.trace("DetermineSpouseType:"+TTM_Utils.GetActorName(spouse))
    SetSpouseSkillType(spouse, DetermineSkillType(spouse))
    SetSpouseSocialClass(spouse, DetermineSocialClass(spouse))
    SetSpouseTemperament(spouse, DetermineTemperament(spouse))
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
            TTM_Debug.trace("DetermineSpouseType:"+TTM_Utils.GetActorName(npc)+":SocialClass:"+socialIndex)
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
            TTM_Debug.trace("DetermineSpouseType:"+TTM_Utils.GetActorName(npc)+":SkillType:"+skillIndex)
        endif
    endif
EndFunction

Function SetSpouseTemperament(Actor npc, string temperament) global
    if(temperament != "none")
        int temperamentIndex = TTM_Utils.GetSpouseTemperamentIndexByType(temperament)
        if(temperamentIndex != -1)
            Faction temperamentFaction = TTM_JData.GetSpouseTemperamentFaction()
            if(npc.IsInFaction(temperamentFaction))
                npc.AddToFaction(temperamentFaction)
            endif
            npc.SetFactionRank(temperamentFaction, temperamentIndex)
            TTM_Debug.trace("DetermineSpouseType:"+TTM_Utils.GetActorName(npc)+":Temperament:"+temperamentIndex)
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

string Function DetermineTemperament(Actor spouse) global
    string temperament = GetTemperamentOverride(spouse)
    if(temperament != "none")
        return temperament
    endif

    return CheckTemperament(spouse)
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
    FormList rulerFactions = Game.GetFormFromFile(0xd75, "TT_MARAS.esp") as FormList
    FormList nobleFactions = Game.GetFormFromFile(0xd76, "TT_MARAS.esp") as FormList
    FormList religiousFactions = Game.GetFormFromFile(0xd77, "TT_MARAS.esp") as FormList
    FormList wealthyFactions = Game.GetFormFromFile(0xd71, "TT_MARAS.esp") as FormList
    FormList middleFactions = Game.GetFormFromFile(0x4, "TT_MARAS.esp") as FormList
    FormList povertyFactions = Game.GetFormFromFile(0xd74, "TT_MARAS.esp") as FormList
    FormList outcastFactions = Game.GetFormFromFile(0xd70, "TT_MARAS.esp") as FormList
    int i = 0
    int maxClassIndex = 0

    while(i < spouseFactions.Length)
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

            if(spouse.WornHasKeyword(TTM_JData.GetClothingRichKeyword()))
                maxClassIndex = TTM_Utils.GetMax(maxClassIndex, TTM_Utils.GetSpouseSocialIndexByType("wealthy")) as int
            endif
        endif
        i += 1
    endwhile

    return TTM_Utils.GetSpouseSocialTypeByIndexes()[maxClassIndex]
EndFunction

bool Function checkByClass(string type, Class spouseClass) global
    FormList classes
    if(type == "warrior")
        classes = Game.GetFormFromFile(0xe, "TT_MARAS.esp") as FormList
    elseif(type == "mage")
        classes = Game.GetFormFromFile(0xf, "TT_MARAS.esp") as FormList
    elseif(type == "rogue")
        classes = Game.GetFormFromFile(0x11, "TT_MARAS.esp") as FormList
    elseif(type == "ranger")
        classes = Game.GetFormFromFile(0x10, "TT_MARAS.esp") as FormList
    elseif(type == "orator")
        classes = Game.GetFormFromFile(0x13, "TT_MARAS.esp") as FormList
    elseif(type == "craftsman")
        classes = Game.GetFormFromFile(0x12, "TT_MARAS.esp") as FormList
    endif
    return classes.HasForm(spouseClass)
EndFunction

;/
                Warrior(0)  Mage(1)     Rogue(2)    Craftsman(3) Ranger(4)   Orator(5)
Outcast(0)      Independent Jealous     Jealous     Humble       Independent Romantic
Poverty(1)      Humble      Romantic    Jealous     Independent  Proud       Romantic
Working(2)      Proud       Humble      Romantic    Independent  Independent Proud
Middle(3)       Proud       Romantic    Independent Romantic     Humble      Jealous
Wealthy(4)      Proud       Jealous     Romantic    Romantic     Independent Jealous
Religious(5)    Independent Humble      Romantic    Jealous      Humble      Proud
Nobles(6)       Jealous     Romantic    Independent Proud        Humble      Proud
Rulers(7)       Proud       Independent Humble      Romantic     Jealous     Independent
/;
string Function CheckTemperament(Actor akNpc) global
    int socialClass = TTM_Utils.GetSpouseSocialClassIndex(akNpc)
    int skillType = TTM_Utils.GetSpouseSkillTypeIndex(akNpc)

    ; Group conditions by temperament for cleaner logic
    ; Independent: Outcast(Warrior,Ranger), Poverty(Craftsman), Working(Craftsman,Ranger), Middle(Rogue), Wealthy(Ranger), Religious(Warrior), Nobles(Rogue), Rulers(Mage,Orator)
    if((socialClass == 0 && (skillType == 0 || skillType == 4)) || \
       (socialClass == 1 && skillType == 3) || \
       (socialClass == 2 && (skillType == 3 || skillType == 4)) || \
       (socialClass == 3 && skillType == 2) || \
       (socialClass == 4 && skillType == 4) || \
       (socialClass == 5 && skillType == 0) || \
       (socialClass == 6 && skillType == 2) || \
       (socialClass == 7 && (skillType == 1 || skillType == 5)))
        return "independent"

    ; Jealous: Outcast(Mage,Rogue), Poverty(Rogue), Middle(Orator), Wealthy(Mage,Orator), Religious(Craftsman), Nobles(Warrior), Rulers(Ranger)
    elseif((socialClass == 0 && (skillType == 1 || skillType == 2)) || \
           (socialClass == 1 && skillType == 2) || \
           (socialClass == 3 && skillType == 5) || \
           (socialClass == 4 && (skillType == 1 || skillType == 5)) || \
           (socialClass == 5 && skillType == 3) || \
           (socialClass == 6 && skillType == 0) || \
           (socialClass == 7 && skillType == 4))
        return "jealous"

    ; Humble: Outcast(Craftsman), Poverty(Warrior), Working(Mage), Middle(Ranger), Religious(Mage,Ranger), Nobles(Ranger), Rulers(Rogue)
    elseif((socialClass == 0 && skillType == 3) || \
           (socialClass == 1 && skillType == 0) || \
           (socialClass == 2 && skillType == 1) || \
           (socialClass == 3 && skillType == 4) || \
           (socialClass == 5 && (skillType == 1 || skillType == 4)) || \
           (socialClass == 6 && skillType == 4) || \
           (socialClass == 7 && skillType == 2))
        return "humble"

    ; Proud: Poverty(Ranger), Working(Warrior,Orator), Middle(Warrior), Wealthy(Warrior), Religious(Orator), Nobles(Craftsman,Orator), Rulers(Warrior)
    elseif((socialClass == 1 && skillType == 4) || \
           (socialClass == 2 && (skillType == 0 || skillType == 5)) || \
           (socialClass == 3 && skillType == 0) || \
           (socialClass == 4 && skillType == 0) || \
           (socialClass == 5 && skillType == 5) || \
           (socialClass == 6 && (skillType == 3 || skillType == 5)) || \
           (socialClass == 7 && skillType == 0))
        return "proud"

    ; Romantic: Outcast(Orator), Poverty(Mage,Orator), Working(Rogue), Middle(Mage,Craftsman), Wealthy(Rogue,Craftsman), Religious(Rogue), Nobles(Mage), Rulers(Craftsman)
    elseif((socialClass == 0 && skillType == 5) || \
           (socialClass == 1 && (skillType == 1 || skillType == 5)) || \
           (socialClass == 2 && skillType == 2) || \
           (socialClass == 3 && (skillType == 1 || skillType == 3)) || \
           (socialClass == 4 && (skillType == 2 || skillType == 3)) || \
           (socialClass == 5 && skillType == 2) || \
           (socialClass == 6 && skillType == 1) || \
           (socialClass == 7 && skillType == 3))
        return "romantic"

    ; Default fallback
    else
        return "independent"
    endif
EndFunction

;/ ==============================
   SECTION: JContainers functions
============================== /;

string Function GetTypesNamespace() global
    return "TT_MARAS.spouseTypeOverrides"
EndFunction

Function ImportJson() global
    TTM_JMethods.ImportDataFromFile("spouseTypesOverrides", "Data/SKSE/Plugins/MARAS/spousesTypes", true, true)
EndFunction

int Function GetActor(Actor npc) global
    int JTypes = TTM_JMethods.GetObjStaticData("spouseTypesOverrides")
    return JFormMap_getObj(JTypes, npc)
EndFunction

string Function GetSocialClassOverride(Actor npc) global
    return JMap_getStr(GetActor(npc), "social", "none")
EndFunction

string Function GetSkillTypeOverride(Actor npc) global
    return JMap_getStr(GetActor(npc), "skill", "none")
EndFunction

string Function GetTemperamentOverride(Actor npc) global
    return JMap_getStr(GetActor(npc), "temperament", "none")
EndFunction
