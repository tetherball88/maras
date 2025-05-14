scriptname TTM_ServiceSpouseTypes

import TTM_JCDomain

int Function DetermineSpouseType(Actor spouse) global
    string skillType = DetermineSkillClassBased(spouse)

    if(skillType == "none")
        skillType = DetermineSkillBased(spouse)
    endif

    string socialClass = DetermineSocialClass(spouse)
    Faction socialClassFaction = TTM_JData.GetSpouseSocialClassFaction()

    spouse.AddToFaction(socialClassFaction)
    spouse.SetFactionRank(socialClassFaction, TTM_Utils.GetSpouseSocialIndexByType(socialClass))

    Faction skillTypeFaction = TTM_JData.GetSpouseSkillTypeFaction()

    spouse.AddToFaction(skillTypeFaction)
    spouse.SetFactionRank(skillTypeFaction, TTM_Utils.GetSpouseSkillIndexByType(skillType))
EndFunction

string Function DetermineSocialClass(Actor spouse) global
    Faction[] spouseFactions = spouse.GetFactions(-100, 100)
    if(checkByFaction("rulers", spouseFactions))
        return "rulers"
    elseif(checkByFaction("nobles", spouseFactions))
        return "nobles"
    elseif(checkByFaction("religious", spouseFactions))
        return "religious"
    elseif(checkByFaction("wealthy", spouseFactions))
        return "wealthy"
    elseif(checkByFaction("middle", spouseFactions))
        return "middle"
    elseif(checkByFaction("poverty", spouseFactions))
        return "poverty"
    elseif(checkByFaction("outcast", spouseFactions))
        return "outcast"
    else
        return "working"
    endif
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
            maxVal - skills[i]
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
    elseif(maxIndex == 17)
        return "orator"
    elseif(maxIndex == 4 || maxIndex == 10 || maxIndex || 17)
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

bool Function checkByFaction(string type, Faction[] spouseFactions) global
    FormList factions = TTM_JData.GetSpouseTypesFactionsByType(type)
    int i = 0

    while i < spouseFactions.Length
        if(factions.HasForm(spouseFactions[i]))
            return true
        endif
        i += 1
    endwhile

    return false
EndFunction

bool Function checkByClass(string type, Class spouseClass) global
    FormList classes = TTM_JData.GetSpouseTypesClassesByType(type)
    return classes.HasForm(spouseClass)
EndFunction
