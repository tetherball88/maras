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
    return GetBool("HasSkyrimNet")
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

Function SetPlayerKiller(bool isPlayerKiller = true) global
    SetBool("PlayerKiller", isPlayerKiller)
EndFunction
bool Function GetPlayerKiller() global
    return GetBool("PlayerKiller")
EndFunction

Function SetPlayerHadWedding(bool yes = true) global
    SetBool("PlayerHadWedding", yes)
EndFunction
bool Function GetPlayerHadWedding() global
    return GetBool("PlayerHadWedding")
EndFunction

;/ ==============================
   SECTION: MCM Settings
============================== /;

Function SetLogDestination(int logDestination) global
    StorageUtil.SetIntValue(none, "LogDestination", logDestination)
EndFunction
int Function GetLogDestination() global
    return StorageUtil.GetIntValue(none, "LogDestination")
EndFunction


Function SetSkipWedding(bool toggle) global
    SetBool("SkipWedding", toggle)
EndFunction

bool Function GetSkipWedding() global
    return GetBool("SkipWedding")
EndFunction

Function SetAlwaysSuccessMarriage(bool toggle) global
    SetBool("AlwaysSuccessMarriage", toggle)
EndFunction
bool Function GetAlwaysSuccessMarriage() global
    return GetBool("AlwaysSuccessMarriage")
EndFunction

;/ =============================
   SECTION: DIALOGUE PROMPTS
============================== /;

string Function GetDialoguePrompt(string promptKey) global
    return JsonUtil.GetPathStringValue("../MARAS/promptStrings.json", "." + promptKey)
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


bool Function GetSetHasSkyrimNetGlobal(int val = -1) global
    int res = _GetSetGameGlobal(0xc4, "TT_MARAS.esp", val)

    return res == 1
EndFunction

bool Function GetSetGameNoAIMarriageGlobal(Actor npc) global
    int random = Utility.RandomInt(0, 100)
    int val = 0
    if(random <= TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(npc) * 100)
        val = 1
    endif
    int res = _GetSetGameGlobal(0x15c, "TT_MARAS.esp", val)

    return res == 1
EndFunction

bool Function GetSetAllowAIDialGlobal(int val = -1) global
    int res = _GetSetGameGlobal(0x1c, "TT_MARAS.esp", val)

    return res == 1
EndFunction

bool Function GetSetPreferVanillaAudioGlobal(int val = -1) global
    int res = _GetSetGameGlobal(0x18, "TT_MARAS.esp", val)

    return res == 1
EndFunction

;/ ==============================
   SECTION: KEYWORDS
============================== /;


Keyword Function GetHomeSandboxKeyword() global
    return Game.GetFormFromFile(0x6b, "TT_MARAS.esp") as Keyword
EndFunction

Keyword Function GetAffectionQuestKeyword(string type) global
    if(type == "happy")
        return Game.GetFormFromFile(0x11e + (type as int), "TT_MARAS.esp") as Keyword
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

Spell Function GetBreakupCooldownSpell() global
    return Game.GetFormFromFile(0x102, "TT_MARAS.esp") as Spell
EndFunction

Spell Function GetDemotedCooldownSpell() global
    return Game.GetFormFromFile(0x10e, "TT_MARAS.esp") as Spell
EndFunction

Spell Function GetPacifyFianceOrSpouseSpell() global
    return Game.GetFormFromFile(0x167, "TT_MARAS.esp") as Spell
EndFunction

Spell Function GetDebugSpell() global
    return Game.GetFormFromFile(0xD, "TT_MARAS.esp") as Spell
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

Quest Function GetMarasMainQuest() global
    return Game.GetFormFromFile(0x800, "TT_MARAS.esp") as Quest
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

;/ ==============================
   SECTION: FACTIONS
============================== /;


; Returns the Married Faction
Faction Function GetMarriedFaction() global
    return Game.GetFormFromFile(0xc6472, "Skyrim.esm") as Faction
EndFunction

;/
  Returns the Faction for spouse which player assigned to their house.
/;
Faction Function GetSpouseHousedFaction() global
    return Game.GetFormFromFile(0x6c, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for spouse which shared their house with player.
/;
Faction Function GetSpouseSharedHouseFaction() global
    return Game.GetFormFromFile(0x7b, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for spouse which didn't have any house originally.
/;
Faction Function GetSpouseNoInitialHouseFaction() global
    return Game.GetFormFromFile(0x7f, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetDefaultCrimeFaction() global
    return Game.GetFormFromFile(0x29db0, "Skyrim.esm") as Faction
EndFunction

Message Function GetDivorceSettlementMsg() global
    return Game.GetFormFromFile(0x164, "TT_MARAS.esp") as Message
EndFunction

Message Function GetSkipWeddingMsg() global
    return Game.GetFormFromFile(0x165, "TT_MARAS.esp") as Message
EndFunction
