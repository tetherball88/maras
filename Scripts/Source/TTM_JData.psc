scriptname TTM_JData

Function Maintenance() global
    ImportStaticData()
EndFunction

Function ImportStaticData() global
    TTM_JMethods.ImportDataFromFile("initialData", "Data/SKSE/Plugins/MARAS/initialData.json")
    TTM_JMethods.ImportDataFromFile("marriageVariables", "Data/SKSE/Plugins/MARAS/marriageComplexityVariables.json")
    TTM_JMethods.ImportDataFromFile("dialoguePrompts", "Data/SKSE/Plugins/MARAS/promptStrings.json")
EndFunction

Function Clear() global
    TTM_JMethods.ClearStorage()
    ImportStaticData()
EndFunction

Function ExportData() global
    TTM_JMethods.ExportStorage()
EndFunction

Function ImportData() global
    TTM_JMethods.ImportStorage()
EndFunction

Function SetBool(string propName, bool value = true) global
    int val = 0
    if(value)
        val = 1
    endif
    TTM_JMethods.SetIntValue(none, propName, val)
EndFunction

bool Function GetBool(string propName) global
    return TTM_JMethods.GetIntValue(none, propName) == 1
EndFunction

Function SetHasSkyrimNet() global
    SetBool("HasSkyrimNet")
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

Function SetHasTTRF() global
    SetBool("HasTTRF")
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
    TTM_JMethods.SetIntValue(none, "LogDestination", logDestination)
EndFunction
int Function GetLogDestination() global
    return TTM_JMethods.GetIntValue(none, "LogDestination")
EndFunction

Function SetLogLevel(int logLevel) global
    TTM_JMethods.SetIntValue(none, "LogLevel", logLevel)
EndFunction
int Function GetLogLevel() global
    return TTM_JMethods.GetIntValue(none, "LogLevel")
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

;/ ==============================
   SECTION: Get Complexity variables
============================== /;

float Function GetComplexityVariable(string variable) global
    return TTM_JMethods.GetFltStaticData("marriageVariables." + variable)
EndFunction

;/ =============================
   SECTION: DIALOGUE PROMPTS
============================== /;

string Function GetDialoguePrompt(string promptKey) global
    return TTM_JMethods.GetStrStaticData("dialoguePrompts." + promptKey)
EndFunction

;/ ==============================
   SECTION: READ INITIAL DATA
============================== /;

Actor Function GetPlayer() global
    return Game.GetPlayer()
EndFunction

Perk Function GetCheckDoorPerk() global
    return Game.GetFormFromFile(0x77, "TT_MARAS.esp") as Perk
EndFunction

;/ ==============================
   SECTION: GLOBALS
============================== /;
Int Function _GetSetGameGlobal(int formId, string fileName, int val = -1) global
    GlobalVariable res = Game.GetFormFromFile(formId, fileName) as GlobalVariable

    if(!res)
        if(TTM_Debug.IsTrace())
            TTM_Debug.trace("Couldn't find GlobalVariable " + formId + " in " + fileName)
        endif
    endif

    if(val == -1)
        return res.GetValueInt()
    else
        res.SetValue(val as int)
        return val
    endif
EndFunction

int Function GetSetSpouseCountGlobal(int val = -1) global
    return _GetSetGameGlobal(0x4f, "TT_MARAS.esp", val)
EndFunction

int Function GetSetPlayerHousesCountGlobal(int val = -1) global
    return _GetSetGameGlobal(0xc2, "TT_MARAS.esp", val)
EndFunction

bool Function GetSetHasSkyrimNetGlobal(int val = -1) global
    int res = _GetSetGameGlobal(0xc4, "TT_MARAS.esp", val)

    return res == 1
EndFunction

;/ ==============================
   SECTION: KEYWORDS
============================== /;
Keyword Function GetLocTypeDwellingKeyword() global
    return Game.GetFormFromFile(0x130dc, "Skyrim.esm") as Keyword
EndFunction

Keyword Function GetLocTypeHouseKeyword() global
    return Game.GetFormFromFile(0x1cb85, "Skyrim.esm") as Keyword
EndFunction

Keyword Function GetClothingRichKeyword() global
    return Game.GetFormFromFile(0xa865d, "Skyrim.esm") as Keyword
EndFunction

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

Spell Function GetBreakdownCooldownSpell() global
    return Game.GetFormFromFile(0x102, "TT_MARAS.esp") as Spell
EndFunction

Spell Function GetDemotedCooldownSpell() global
    return Game.GetFormFromFile(0x10e, "TT_MARAS.esp") as Spell
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

; Return Player Faction
Faction Function GetPlayerFaction() global
    return Game.GetFormFromFile(0xdb1, "Skyrim.esm") as Faction
EndFunction

; Returns the Married Faction
Faction Function GetMarriedFaction() global
    return Game.GetFormFromFile(0xc6472, "Skyrim.esm") as Faction
EndFunction

; Returns the Bed Ownership Faction
Faction Function GetPlayerBedOwnershipFaction() global
    return Game.GetFormFromFile(0xf2073, "Skyrim.esm") as Faction
EndFunction

; Returns the Marriage Asked Faction
Faction Function GetMarriageAskedFaction() global
    return Game.GetFormFromFile(0xff7f3, "Skyrim.esm") as Faction
EndFunction

; Returns the Courting Faction
Faction Function GetCourtingFaction() global
    return Game.GetFormFromFile(0x7431a, "Skyrim.esm") as Faction
EndFunction

; Returns the Excluded Faction
Faction Function GetMarriageExcludedFaction() global
    return Game.GetFormFromFile(0x47a01, "Skyrim.esm") as Faction
EndFunction

Faction Function GetMarriagePotentialFaction() global
    return Game.GetFormFromFile(0x19809, "Skyrim.esm") as Faction
EndFunction

Faction Function GetPotentialHirelingFaction() global
    return Game.GetFormFromFile(0xbcc9a, "Skyrim.esm") as Faction
EndFunction

Faction Function GetCurrentFollowerFaction() global
    return Game.GetFormFromFile(0x5c84e, "Skyrim.esm") as Faction
EndFunction

;/
  Returns the Faction used to check spouse home ownership.
/;
Faction Function GetCheckSpouseHomeFaction() global
    return Game.GetFormFromFile(0x2e, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for tracked NPCs.
/;
Faction Function GetTrackedNpcFaction() global
    return Game.GetFormFromFile(0x7, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for spouse social class.
/;
Faction Function GetSpouseSocialClassFaction() global
    return Game.GetFormFromFile(0x66, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for spouse skill type.
/;
Faction Function GetSpouseSkillTypeFaction() global
    return Game.GetFormFromFile(0x4e, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction for spouse temperament.
/;
Faction Function GetSpouseTemperamentFaction() global
    return Game.GetFormFromFile(0x118, "TT_MARAS.esp") as Faction
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

;/
  Returns the Faction for spouse which determine their hierarchy in family.
/;
Faction Function GetSpouseHierarchyFaction() global
    return Game.GetFormFromFile(0x111, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction used for affection system.
/;
Faction Function GetAffectionFaction() global
    return Game.GetFormFromFile(0x119, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetCompanionsFaction() global
    return Game.GetFormFromFile(0x48362, "Skyrim.esm") as Faction
EndFunction

Faction Function GetThievesFaction() global
    return Game.GetFormFromFile(0x29da9, "Skyrim.esm") as Faction
EndFunction

Faction Function GetBrotherhoodFaction() global
    return Game.GetFormFromFile(0x1bdb3, "Skyrim.esm") as Faction
EndFunction

Faction Function GetCollegeFaction() global
    return Game.GetFormFromFile(0x1f259, "Skyrim.esm") as Faction
EndFunction

Faction Function GetBardsFaction() global
    return Game.GetFormFromFile(0xc13c7, "Skyrim.esm") as Faction
EndFunction
