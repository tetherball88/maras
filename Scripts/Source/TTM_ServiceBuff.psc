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
    CalculateFollowerMultipliers()
    CalculatePermanentMultipliers()
EndFunction

;/
  Calculates and applies follower-based multipliers to player bonuses.
/;
Function CalculateFollowerMultipliers() global
    string[] types = MARAS.GetNpcTypes("skillType")
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
    string[] types = MARAS.GetNpcTypes("socialClass")
    float[] multipliers = MARAS.GetPermanentMultipliers()

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
    int count = MARAS.GetBonusCount(type)
    int i = 0

    Trace("UpdateBonus:"+type+":"+multiplier+":"+count)

    while(i < count)

        UpdatePerk(type, i, multiplier)
        i += 1
    endwhile
EndFunction

;/
  Updates a single perk or spell effect for the player based on the bonus type and multiplier.
/;
Function UpdatePerk(string type, int index, float multiplier) global
    Actor PlayerRef = TTM_Data.GetPlayer()
    Perk bonusPerk = MARAS.GetBonusPerk(type, index)
    int bonusPerkEffIndex = MARAS.GetBonusEffectIndex(type, index)
    string bonusType = MARAS.GetBonusPerkType(type, index)
    float bonusValue = MARAS.GetBonusPerkValue(type, index)
    float newVal = 0
    float oldVal = 0
    Trace("UpdatePerk:"+type+":"+index+":"+bonusPerk+":"+bonusType+":"+bonusValue+":"+multiplier)
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
  Returns a string describing the follower buff for a spouse.
/;
string Function GetSpouseFollowerBuffs(Actor spouse) global
    string skillType = MARAS.GetNpcCurrentTypeName(spouse, "skillType")
    string description = MARAS.GetBonusPerkDescription(skillType)
    float value = MARAS.GetBonusPerkValue(skillType)
    string unit = MARAS.GetBonusPerkUnit(skillType)

    return description + " " + value + " " + unit
EndFunction

;/
  Returns a string describing the permanent buff for a spouse.
/;
string Function GetSpousePermanentBuffs(Actor spouse) global
    string socialClass = MARAS.GetNpcCurrentTypeName(spouse, "socialClass")
    string description = MARAS.GetBonusPerkDescription(socialClass)
    float value = MARAS.GetBonusPerkValue(socialClass)
    string unit = MARAS.GetBonusPerkUnit(socialClass)

    return description + " " + value + " " + unit
EndFunction

;/
  Calculates follower multipliers for each skill type based on current followers.
/;
float[] Function GetFollowersMultipliers() global
    Actor[] followers = MARAS.GetCurrentTeammates()
    return MARAS.GetFollowersMultipliers(followers)
EndFunction

Function Trace(string msg) global
    TTM_Debug.Trace("TTM_ServiceBuff:" + msg)
EndFunction
