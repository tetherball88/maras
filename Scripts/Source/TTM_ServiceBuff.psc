;/
  Handles calculation and application of follower and spouse buffs for the MARAS mod.

  Main responsibilities:
    - Importing bonus data from JSON
    - Calculating multipliers for followers and spouses
    - Updating player perks and spell effects
    - Providing utility functions for buff descriptions and values

  Dependencies:
    - TTM_JCDomain
    - TTM_Utils
    - TTM_ServiceNpcs
    - JContainers (JDB, JMap, JArray, etc)
    - PO3_SKSEFunctions
/;
Scriptname TTM_ServiceBuff

import TTM_JCDomain

;/
  Runs all maintenance functions: imports data and recalculates all multipliers.
/;
Function Maintenance() global
    ImportData()
    CalculateFollowerMultipliers()
    CalculatePermanentMultipliers()
EndFunction

;/
  Calculates and applies follower-based multipliers to player bonuses.
/;
Function CalculateFollowerMultipliers() global
    string[] types = TTM_Utils.GetTrackedNpcSkillTypeByIndexes()
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

    TTM_Debug.trace("CalculateValues:"+bonusPerk+":"+oldVal+":"+newVal)

    ; new val(spell magnitude or perk value) is 0, means effect will provide 0 benefit - remove perk
    if(newVal == 0.0)
        if(PlayerRef.HasPerk(bonusPerk))
            TTM_Debug.trace("RemovePerk:"+bonusPerk)
            PlayerRef.removePerk(bonusPerk)
        else
            TTM_Debug.trace("SkipPerk:"+bonusPerk)
        endif
        return
    endif

    ; update perk only if new value is different from old
    if(newVal != oldVal)
        PlayerRef.removePerk(bonusPerk)
        PlayerRef.addPerk(bonusPerk)
        TTM_Debug.trace("UpdatePerk:"+bonusPerk)
    ; if new value is same as old and it is not 0, but for some reason player doesn't have perk add it
    elseif(!PlayerRef.HasPerk(bonusPerk))
        PlayerRef.addPerk(bonusPerk)
        TTM_Debug.trace("AddPerk:"+bonusPerk)
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
    int spouseRank = TTM_ServiceNpcs.GetSpouseRank(spouse)
    int spouseCount = TTM_ServiceNpcs.GetSpousesCount()

    if(spouseCount == 1)
        return 2.0
    endif

    if(spouseRank == 0)
        return 1.0
    elseif(spouseRank == 1)
        return 0.5
    elseif(spouseRank == 2)
        return 0.25
    else
        return 0.1
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
        if(TTM_ServiceNpcs.GetSpouse(follower) != 0)
            string skillType = TTM_Utils.GetSpouseSkillType(follower)
            int index = TTM_Utils.GetSpouseSkillIndexByType(skillType)
            if(index != -1)
                multipliers[index] = multipliers[index] + 1
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
    int jSpouses = TTM_ServiceNpcs.GetSpouses()
    int count = JArray_count(jSpouses)
    int i = 0

    while(i < count)
        Actor spouse = JArray_getForm(jSpouses, i) as Actor
        string socialType = TTM_Utils.GetSpouseSocialClass(spouse)
        int spouseRank = TTM_ServiceNpcs.GetSpouseRank(spouse)
        float multiplier = GetSpouseMultiplier(spouse)

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
  Returns the JContainers namespace key for bonuses.
/;
string Function GetBonusesNamespace() global
    return TTM_JData.GetNamespaceKey() + ".bonuses"
EndFunction

;/
  Imports bonus data from bonuses.json into JContainers DB.
/;
Function ImportData() global
    JDB_solveObjSetter(GetBonusesNamespace(), JValue_readFromFile("Data/SKSE/Plugins/MARAS/bonuses.json"), true)
EndFunction

;/
  Returns the number of bonus entries for a given type.
/;
int Function GetBonusCount(string type) global
    return JArray_count(JDB_solveObj(GetBonusesNamespace() + "." + type))
EndFunction

;/
  Returns the Perk form for a bonus entry.
/;
Perk Function GetBonusPerk(string type, int index = 0) global
    return JDB_solveForm(GetBonusesNamespace() + "." + type + "[" + index + "].perk") as Perk
EndFunction

;/
  Returns the effect index for a bonus entry.
/;
int Function GetBonusEffectIndex(string type, int index = 0) global
    return JDB_solveInt(GetBonusesNamespace() + "." + type + "[" + index + "].effectIndex")
EndFunction

;/
  Returns the value for a bonus entry.
/;
float Function GetBonusPerkValue(string type, int index = 0) global
    return JDB_solveFlt(GetBonusesNamespace() + "." + type + "[" + index + "].value")
EndFunction

;/
  Returns the type string for a bonus entry (e.g., "spell", "multiply", "add").
/;
string Function GetBonusPerkType(string type, int index = 0) global
    return JDB_solveStr(GetBonusesNamespace() + "." + type + "[" + index + "].type")
EndFunction

;/
  Returns the unit string for a bonus entry.
/;
string Function GetBonusPerkUnit(string type, int index = 0) global
    return JDB_solveStr(GetBonusesNamespace() + "." + type + "[" + index + "].unit")
EndFunction

;/
  Returns the description string for a bonus entry.
/;
string Function GetBonusPerkDescription(string type, int index = 0) global
    return JDB_solveStr(GetBonusesNamespace() + "." + type + "[" + index + "].description")
EndFunction
