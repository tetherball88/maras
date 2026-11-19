scriptname TTM_Data

Function SetBool(string propName, bool value = true) global
    int val = 0
    if(value)
        val = 1
    endif
    StorageUtil.SetIntValue(none, propName, val)
EndFunction

bool Function GetBool(string propName) global
    return StorageUtil.GetIntValue(none, propName) == 1
EndFunction

Function SetHasSkyrimNet(bool has) global
    SetBool("HasSkyrimNet", has)
EndFunction
bool Function GetHasSkyrimNet() global
    if (MARAS.GetSettingBool("simulateNoSkyrimNet") || IsMigrating())
        return false
    endif
    return GetBool("HasSkyrimNet")
EndFunction

bool Function IsMigrating() global
    return MARAS.IsMigrating()
EndFunction

Function SetIsMigrating(bool migrating) global
    MARAS.SetIsMigrating(migrating)
EndFunction

Function SetHasTTLL() global
    SetBool("HasTTLL")
EndFunction
bool Function GetHasTTLL() global
    return GetBool("HasTTLL")
EndFunction

Function SetHasTTRF(bool has) global
    SetBool("HasTTRF", has)
EndFunction
bool Function GetHasTTRF() global
    return GetBool("HasTTRF")
EndFunction

;/ =============================
   SECTION: DIALOGUE PROMPTS
============================== /;

string Function GetDialoguePrompt(string promptKey) global
    return MARAS.GetDialoguePrompt(promptKey)
EndFunction

;/ ==============================
   SECTION: READ INITIAL DATA
============================== /;

Actor Function GetPlayer() global
    return Game.GetPlayer()
EndFunction

;/ ==============================
   SECTION: GLOBALS
============================== /;

Int Function _GetSetGameGlobal(int formId, string fileName, int val = -1) global
    GlobalVariable res = Game.GetFormFromFile(formId, fileName) as GlobalVariable

    if(!res)
        TTM_Debug.warn("Couldn't find GlobalVariable " + formId + " in " + fileName)
    endif

    if(val == -1)
        return res.GetValueInt()
    else
        res.SetValue(val as int)
        return val
    endif
EndFunction

Function GetSetHasAdultFramework() global
    bool hasFramework = TTM_AdultUtils.GetEngine() != -1
    int hasFrameworkFlag = 0
    if(hasFramework)
        hasFrameworkFlag = 1
    endif
    _GetSetGameGlobal(0x5dd, "TT_MARAS.esp", hasFrameworkFlag)
EndFunction


bool Function GetSetHasSkyrimNetGlobal(int val = -1) global
    if (MARAS.GetSettingBool("simulateNoSkyrimNet"))
        val = 0
    endif
    int res = _GetSetGameGlobal(0xc4, "TT_MARAS.esp", val)

    return res == 1
EndFunction

int Function GetSetDLC1RV06DoOnceGlobal(int val = -1) global
    return _GetSetGameGlobal(0x14c9d, "Dawnguard.esm", val)
EndFunction

Function ResetDLC1RV06DoOnceGlobal() global
    GetSetDLC1RV06DoOnceGlobal(0)
EndFunction

;/ ==============================
   SECTION: KEYWORDS
============================== /;


Keyword Function GetAffectionQuestKeyword(string type) global
    if(type == "happy")
        return Game.GetFormFromFile(0x11e, "TT_MARAS.esp") as Keyword
    elseif(type == "content")
        return Game.GetFormFromFile(0x11f, "TT_MARAS.esp") as Keyword
    elseif(type == "troubled")
        return Game.GetFormFromFile(0x120, "TT_MARAS.esp") as Keyword
    elseif(type == "estranged")
        return Game.GetFormFromFile(0x121, "TT_MARAS.esp") as Keyword
    endif

    return none
EndFunction

Keyword Function GetDivorceKeyword() global
    return Game.GetFormFromFile(0x160, "TT_MARAS.esp") as Keyword
EndFunction

;/ ==============================
   SECTION: ARMORS
============================== /;

Armor Function GetMatrimonyRingArmor() global
    return Game.GetFormFromFile(0xc5809, "Skyrim.esm") as Armor
EndFunction

;/ ==============================
   SECTION: STATICS
============================== /;

Static Function GetHomeSandboxMarkerStatic() global
    return Game.GetFormFromFile(0x76, "TT_MARAS.esp") as Static
EndFunction

;/ ==============================
   SECTION: MISCS
============================== /;

MiscObject Function GetGoldMisc() global
    return Game.GetFormFromFile(0xf, "Skyrim.esm") as MiscObject
EndFunction

;/ ==============================
   SECTION: PACKAGES
============================== /;

Package Function GetHomeSandboxPackage() global
    return Game.GetFormFromFile(0x6a, "TT_MARAS.esp") as Package
EndFunction

;/ ==============================
   SECTION: SPELLS
============================== /;

Spell Function GetHMKSpell() global
    return Game.GetFormFromFile(0x9a17, "HearthfireMultiKid.esp") as Spell
EndFunction

;/ ==============================
   SECTION: QUESTS
============================== /;

Quest Function GetDialogueFavorQuest() global
    return Game.GetFormFromFile(0x5a6dc, "Skyrim.esm") as Quest
EndFunction

Quest Function GetMarriageFinQuest() global
    return Game.GetFormFromFile(0x21382, "Skyrim.esm") as Quest
EndFunction

Quest Function GetMarriageMainQuest() global
    return Game.GetFormFromFile(0x74793, "Skyrim.esm") as Quest
EndFunction

Quest Function GetMarriageWeddingQuest() global
    return Game.GetFormFromFile(0x7404e, "Skyrim.esm") as Quest
EndFunction

Quest Function GetMarriageBreakupQuest() global
    return Game.GetFormFromFile(0x7431b, "Skyrim.esm") as Quest
EndFunction

Quest Function GetMarasEnablePolygamyQuest() global
    return Game.GetFormFromFile(0x5c, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetMarasDialoguesQuest() global
    return Game.GetFormFromFile(0x67, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetMarasCheckSpouseHomeQuest() global
    return Game.GetFormFromFile(0x30, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetMarasAffectionEstrangedDivorceQuest() global
    return Game.GetFormFromFile(0x11a, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetMarasNpcsScenesQuest() global
    return Game.GetFormFromFile(0x378, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetMarasPartnerAwayQuest() global
    return Game.GetFormFromFile(0xDC4, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetTurnSpouseToVampireQuest() global
    return Game.GetFormFromFile(0x58ca, "Dawnguard.esm") as Quest
EndFunction

Quest Function GetChildLogicQuest() global
    return Game.GetFormFromFile(0xc25, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetChildreSandboxQuest() global
    return Game.GetFormFromFile(0x2bc, "TT_MARAS.esp") as Quest
EndFunction

Quest Function GetOriginalChildrenSandboxQuest() global
    return Game.GetFormFromFile(0x401c, "HearthFires.esm") as Quest
EndFunction

Quest Function GetOriginalChildrenAdoptionQuest() global
    return Game.GetFormFromFile(0x42b4, "HearthFires.esm") as Quest
EndFunction

Quest Function GetOriginalChildrenNewAdoptionQuest() global
    return Game.GetFormFromFile(0x3f6e, "HearthFires.esm") as Quest
EndFunction

Quest Function GetOriginalChildrenAdoptableQuest() global
    return Game.GetFormFromFile(0x4291, "HearthFires.esm") as Quest
EndFunction

Quest Function GetHMKCustomHomeManager() global
    return Game.GetFormFromFile(0x9a15, "HearthfireMultiKid.esp") as Quest
EndFunction

Quest Function GetHMKCustomHomeValidator() global
    return Game.GetFormFromFile(0x9a18, "HearthfireMultiKid.esp") as Quest
EndFunction


;/ ==============================
   SECTION: FACTIONS
============================== /;


; Returns the Married Faction
Faction Function GetMarriedFaction() global
    return Game.GetFormFromFile(0xc6472, "Skyrim.esm") as Faction
EndFunction

Faction Function GetDLC1PotentialVampireFaction() global
    return Game.GetFormFromFile(0x142e6, "Dawnguard.esm") as Faction
EndFunction

Faction Function GetDLC1PlayerTurnedVampireFaction() global
    return Game.GetFormFromFile(0x588d, "Dawnguard.esm") as Faction
EndFunction
