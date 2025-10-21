;/
  Handles calculation and application of follower and spouse buffs for the MARAS mod.

  Main responsibilities:
    - Importing bonus data from JSON
    - Calculating multipliers for followers and spouses
    - Updating player perks and spell effects
    - Providing utility functions for buff descriptions and values
/;
Scriptname TTM_ServiceBuff

;/
  Runs all maintenance functions: imports data and recalculates all multipliers.
/;
Function Maintenance() global
    TTM_JMethods.ImportDataFromFile("bonuses", "Data/SKSE/Plugins/MARAS/bonuses.json")
    CalculateFollowerMultipliers()
    CalculatePermanentMultipliers()
EndFunction

;/
  Calculates and applies follower-based multipliers to player bonuses.
/;
Function CalculateFollowerMultipliers() global
    string[] types = TTM_Utils.GetSpouseSkillTypeByIndexes()
    float[] multipliers = GetFollowersMultipliers()
    int i = 0

    while(i < types.length)
        UpdateBonus(types[i], multipliers[i])
        i += 1
    endwhile
EndFunction

;/
  Calculates and applies permanent (spouse) multipliers to player bonuses.
/;
Function CalculatePermanentMultipliers() global
    string[] types = TTM_Utils.GetSpouseSocialTypeByIndexes()
    float[] multipliers = GetPermanentMultipliers()

    int i = 0

    while(i < multipliers.Length)
        UpdateBonus(types[i], multipliers[i])
        i += 1
    endwhile
EndFunction

;/
  Updates all bonus perks of a given type with the provided multiplier.
/;
Function UpdateBonus(string type, float multiplier) global
    int count = GetBonusCount(type)
    int i = 0

    while(i < count)
        UpdatePerk(type, i, multiplier)
        i += 1
    endwhile
EndFunction

;/
  Updates a single perk or spell effect for the player based on the bonus type and multiplier.
/;
Function UpdatePerk(string type, int index, float multiplier) global
    Actor PlayerRef = TTM_JData.GetPlayer()
    Perk bonusPerk = GetBonusPerk(type, index)
    int bonusPerkEffIndex = GetBonusEffectIndex(type, index)
    string bonusType = GetBonusPerkType(type, index)
    float bonusValue = GetBonusPerkValue(type, index)
    float newVal = 0
    float oldVal = 0
    if(bonusType == "spell")
        Spell bonusSpell = bonusPerk.GetNthEntrySpell(bonusPerkEffIndex)
        newVal = bonusValue * multiplier
        oldVal = bonusSpell.GetNthEffectMagnitude(0)
        UpdateSpellEffect(bonusSpell, newVal)
    elseif(bonusType == "multiply")
        newVal = (bonusValue / 100) * multiplier
        oldVal = bonusPerk.GetNthEntryValue(0, 0) - 1
        bonusPerk.SetNthEntryValue(0, 0, 1 + newVal)
    elseif(bonusType == "add")
        newVal = bonusValue * multiplier
        oldVal = bonusPerk.GetNthEntryValue(0, 0)
        bonusPerk.SetNthEntryValue(0, 0, newVal)
    endif

    Trace("CalculateValues:"+bonusPerk+":"+oldVal+":"+newVal)

    ; new val(spell magnitude or perk value) is 0, means effect will provide 0 benefit - remove perk
    if(newVal == 0.0)
        if(PlayerRef.HasPerk(bonusPerk))
            Trace("RemovePerk:"+bonusPerk)
            PlayerRef.removePerk(bonusPerk)
        else
            Trace("SkipPerk:"+bonusPerk)
        endif
        return
    endif

    ; update perk only if new value is different from old
    if(newVal != oldVal)
        PlayerRef.removePerk(bonusPerk)
        PlayerRef.addPerk(bonusPerk)
        Trace("UpdatePerk:"+bonusPerk)
    ; if new value is same as old and it is not 0, but for some reason player doesn't have perk add it
    elseif(!PlayerRef.HasPerk(bonusPerk))
        PlayerRef.addPerk(bonusPerk)
        Trace("AddPerk:"+bonusPerk)
    endif
EndFunction

;/
  Updates the magnitude of the first effect of a spell if it has changed.
/;
Function UpdateSpellEffect(Spell updateSpell, float magnitude) global
    float currentMagn = updateSpell.GetNthEffectMagnitude(0)
    ; magnitude didn't change
    if(currentMagn == magnitude)
        return
    endif
    updateSpell.SetNthEffectMagnitude(0, magnitude)
EndFunction

;/
  Returns the multiplier for a spouse based on their rank and total spouse count.
/;
float Function GetSpouseMultiplier(Actor spouse) global
    int spouseRank = TTM_ServiceHierarchy.GetSpouseRank(spouse)
    int spouseCount = TTM_ServiceRelationships.GetSpousesCount()
    float affectionBuffMult = TTM_ServiceAffection.GetAffectionBuffMultiplier(spouse)

    if(spouseCount == 1)
        return 2.0 * affectionBuffMult
    endif

    if(spouseRank == 0)
        return 1.0 * affectionBuffMult
    elseif(spouseRank == 1)
        return 0.5 * affectionBuffMult
    elseif(spouseRank == 2)
        return 0.25 * affectionBuffMult
    else
        return 0.1 * affectionBuffMult
    endif
EndFunction

;/
  Returns a string describing the follower buff for a spouse.
/;
string Function GetSpouseFollowerBuffs(Actor spouse) global
    string skillType = TTM_Utils.GetSpouseSkillType(spouse)
    string description = GetBonusPerkDescription(skillType)
    float value = GetBonusPerkValue(skillType)
    string unit = GetBonusPerkUnit(skillType)

    return description + " " + value + " " + unit
EndFunction

;/
  Returns a string describing the permanent buff for a spouse.
/;
string Function GetSpousePermanentBuffs(Actor spouse) global
    string socialClass = TTM_Utils.GetSpouseSocialClass(spouse)
    string description = GetBonusPerkDescription(socialClass)
    float value = GetBonusPerkValue(socialClass)
    string unit = GetBonusPerkUnit(socialClass)

    return description + " " + value + " " + unit
EndFunction

;/
  Calculates follower multipliers for each skill type based on current followers.
/;
float[] Function GetFollowersMultipliers() global
    Actor[] followers = PO3_SKSEFunctions.GetPlayerFollowers()
    float[] multipliers = Utility.CreateFloatArray(6)
    int i = 0
    ; 5 is last index for skill based spouse, only skill based types are accountet for follower bonuses
    while(i < followers.Length)
        Actor follower = followers[i]
        if(TTM_Utils.IsSpouse(follower))
            string skillType = TTM_Utils.GetSpouseSkillType(follower)
            float spouseMult = TTM_ServiceAffection.GetAffectionBuffMultiplier(follower)
            int index = TTM_Utils.GetSpouseSkillIndexByType(skillType)
            if(index != -1)
                multipliers[index] = multipliers[index] + spouseMult
            endif
        endif
        i += 1
    endwhile

    return multipliers
EndFunction

;/
  Calculates permanent multipliers for each social type based on all spouses.
/;
float[] Function GetPermanentMultipliers() global
    float[] multipliers = Utility.CreateFloatArray(8)
    Form[] spouses = TTM_ServiceRelationships.GetSpouses()
    int i = 0

    while(i < spouses.Length)
        Actor spouse = spouses[i] as Actor
        string socialType = TTM_Utils.GetSpouseSocialClass(spouse)
        int spouseRank = TTM_ServiceHierarchy.GetSpouseRank(spouse)
        float multiplier = GetSpouseMultiplier(spouse)
        Trace("GetPermanentMultipliers:SpousesCount:"+TTM_Utils.GetActorName(spouse)+":socialType" + socialType + ":rank" + spouseRank + ":multiplier" + multiplier)

        int index = TTM_Utils.GetSpouseSocialIndexByType(socialType)
        if(index != -1)
            multipliers[index] = multipliers[index] + multiplier
        endif

        i += 1
    endwhile

    return multipliers
EndFunction

;/ ==============================
   SECTION: JContainers functions
============================== /;

;/
  Returns the number of bonus entries for a given type.
/;
int Function GetBonusCount(string type) global
    return TTM_JMethods.CountStaticData("bonuses." + type + "")
EndFunction

;/
  Returns the Perk form for a bonus entry.
/;
Perk Function GetBonusPerk(string type, int index = 0) global
    return TTM_JMethods.GetFormStaticData("bonuses." + type + "[" + index + "].perk") as Perk
EndFunction

;/
  Returns the effect index for a bonus entry.
/;
int Function GetBonusEffectIndex(string type, int index = 0) global
    return TTM_JMethods.GetIntStaticData("bonuses." + type + "[" + index + "].effectIndex")
EndFunction

;/
  Returns the value for a bonus entry.
/;
float Function GetBonusPerkValue(string type, int index = 0) global
    return TTM_JMethods.GetFltStaticData("bonuses." + type + "[" + index + "].value")
EndFunction

;/
  Returns the type string for a bonus entry (e.g., "spell", "multiply", "add").
/;
string Function GetBonusPerkType(string type, int index = 0) global
    return TTM_JMethods.GetStrStaticData("bonuses." + type + "[" + index + "].type")
EndFunction

;/
  Returns the unit string for a bonus entry.
/;
string Function GetBonusPerkUnit(string type, int index = 0) global
    return TTM_JMethods.GetStrStaticData("bonuses." + type + "[" + index + "].unit")
EndFunction

;/
  Returns the description string for a bonus entry.
/;
string Function GetBonusPerkDescription(string type, int index = 0) global
    return TTM_JMethods.GetStrStaticData("bonuses." + type + "[" + index + "].description")
EndFunction

Function Trace(string msg) global
    ; TTM_Debug.trace("TTM_ServiceBuff:" + msg)
EndFunction
