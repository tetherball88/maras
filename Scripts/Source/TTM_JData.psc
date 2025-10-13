scriptname TTM_JData

import TTM_JCDomain

;/
  Data structure for MARAS saveData and staticData (see below for details).
/;
; Setter/getters for below object
; All additional logic, like swap lead spouses, comparison, etc... Happens in TTM_JUtils

;/
{
    saveData: {
        trackedNpcs: { ; list of current spouses
            [Actor]:
            {
                name: string,
                level: int,
                socialClass: string,
                skillType: string,
                race: string,
                gender: string,
                status: string,
                homeMarker: ObjectReference
                home: Location
                existingRelationships: [{ // track if has original relationship from game start
                    name: string,
                    isSpouse: "true" | "false",
                    isCourting: "true" | "false",
                    isLover: "true" | "false",
                    isEx: "true" | "false" // if they broke up due to this mod
                }]
        }
        buckets: {
            candidates: {[Actor]: int},
            fiances: {[Actor]: int},
            spouses: {[Actor]: int},
            divorcees: {[Actor]: int},
            ; lovers: {[Actor]: int}, ?
        }
        ; when multiple spouses, keeps track which spouse have which rank, 0 - 1st, 1 - 2nd, 2 - 3rd
        leadSpouses: [Actor, Actor, Actor],
        maritalAssets: { // player's marital assets they get access to after marriage. It also track original ownership and all spouses which have ownership of cell or object(bed)
            cells: {
                [Cell]: {
                    actor: ActorBase
                    faction: Faction
                    spouseOwners: Actor[]
                }
            }
            objects: {
                [ObjectReference]: {
                    actor: ActorBase
                    faction: Faction
                    spouseOwners: Actor[]
                }
            }
            factions: { // factions which player got from spouses who belong to that factions
                [Faction]: Actor[]
            }
        },
        spousesFactions: {
            [Faction]: {
                spouses: {[Actor]: 0 | 1}
            }
        }
    }
    static: {
        "initialData": {...}
    }
}
/;

;/
  Returns the namespace key for MARAS data in JContainers.
/;
string Function GetNamespaceKey() global
    return ".TT_MARAS"
EndFunction

;/
  Returns the static key for MARAS static data in JContainers.
/;
string Function GetStaticKey() global
    return GetNamespaceKey() + ".static"
EndFunction

;/
  Returns the key for initial data in JContainers.
/;
string Function GetInitialDataKey() global
    return GetStaticKey() + ".initialData"
EndFunction

;/
  Returns the key for save data in JContainers.
/;
string Function GetSaveDataKey() global
    return GetNamespaceKey() + ".saveData"
EndFunction

;/ ==============================
   SECTION: Object Top level
============================== /;
;/
  Clears all MARAS data from JContainers.
/;
Function Clear() global
    JDB_solveObjSetter(GetNamespaceKey(), JMap_object())
    ImportStaticData()
EndFunction

;/
  Exports MARAS data to a JSON file.
/;
Function ExportData() global
    JValue_writeToFile(JDB_solveObj(GetNamespaceKey()), JContainers.userDirectory() + "MARAS/store.json")
EndFunction

;/
  Imports MARAS data from a JSON file.
/;
Function ImportData() global
    int jObj = JValue_readFromFile(JContainers.userDirectory() + "MARAS/store.json")
    JDB_solveObjSetter(GetNamespaceKey(), jObj)
EndFunction

;/
  Gets or creates the save data JMap for MARAS.
/;
int Function GetJSaveData() global
    int res = JDB_solveObj(GetSaveDataKey())
    if (!res)
        res = JMap_object()
        JDB_solveObjSetter(GetSaveDataKey(),  res, true)
    endif
    return res
EndFunction

;/
  Imports static data (initialData.json) into JContainers.
/;
Function ImportStaticData() global
    JDB_solveObjSetter(GetStaticKey() + ".initialData", JValue_readFromFile("Data/SKSE/Plugins/MARAS/initialData.json"), true)
    JDB_solveObjSetter(GetStaticKey() + ".initialData.complexityVariables", JValue_readFromFile("Data/SKSE/Plugins/MARAS/marriageComplexityVariables.json"), true)
EndFunction

;/ ==============================
   SECTION: Get Complexity variables
============================== /;

float Function GetComplexityVariable(string variable) global
    return JDB_solveFlt(GetStaticKey() + ".initialData.complexityVariables."+variable)
EndFunction

;/ ==============================
   SECTION: Single Properties
============================== /;
;/
  Returns the player Actor form.
/;
Actor Function GetPlayer() global
    return JDB_solveForm(GetInitialDataKey() + ".player") as Actor
EndFunction

;/
  Returns the Perk used for door checking.
/;
Perk Function GetCheckDoorPerk() global
    return JDB_solveForm(GetInitialDataKey() + ".checkDoorPerk") as Perk
EndFunction

Function _SetInt(string propName, int value = 0) global
    JDB_solveIntSetter(GetSaveDataKey() + "." + propName, value, true)
EndFunction

int Function _GetInt(string propName) global
    return JDB_solveInt(GetSaveDataKey() + "." + propName)
EndFunction

Function _SetBool(string propName, bool flag = true) global
    if(flag)
        _SetInt(propName, 1)
    else
        _SetInt(propName, 0)
    endif
EndFunction


bool Function _GetBool(string propName) global
    return _GetInt(propName) == 1
EndFunction

;/
  Sets the SkyrimNet integration state.
/;
Function SetHasSkyrimNet() global
    _SetBool("hasSkyrimNet")
EndFunction

;/
  Returns true if SkyrimNet integration is enabled.
/;
bool Function GetHasSkyrimNet() global
    return _GetBool("hasSkyrimNet")
EndFunction

;/
  Sets the TTLL integration state.
/;
Function SetHasTTLL() global
    _SetBool("hasTTLL")
EndFunction

;/
  Returns true if TTLL integration is enabled.
/;
bool Function GetHasTTLL() global
    return _GetBool("hasTTLL")
EndFunction

;/
  Sets the TTRF integration state.
/;
Function SetHasTTRF() global
    _SetBool("hasTTRF")
EndFunction

;/
  Returns true if TTRF integration is enabled.
/;
bool Function GetHasTTRF() global
    return _GetBool("hasTTRF")
EndFunction

;/
  Set if player killed spouse or fiance
/;
Function SetPlayerKiller(bool isPlayerKiller = true) global
    _SetBool("playerKiller", isPlayerKiller)
EndFunction

;/
  Get if player killed spouse or fiance
/;
bool Function GetPlayerKiller() global
    return _GetBool("playerKiller")
EndFunction

;/
  Set if player attended at least one wedding
/;
Function SetPlayerHadWedding(bool yes = true) global
    _SetBool("playerHadWedding", yes)
EndFunction

;/
  Get if player attended at least one wedding
/;
bool Function GetPlayerHadWedding() global
    return _GetBool("playerHadWedding")
EndFunction

;/ ==============================
   SECTION: GLOBALS
============================== /;

Int Function GetSetGameGlobal(string globalName, int val = -1) global
    GlobalVariable res = JDB_solveForm(GetInitialDataKey() + ".gameGlobals." + globalName) as GlobalVariable

    if(!res)
        TTM_Debug.trace("Couldn't find GlobalVariable with name " + globalName)
    endif

    if(val == -1)
        return res.GetValueInt()
    else
        res.SetValue(val as int)
        return val
    endif
EndFunction

int Function GetSetSpouseCountGlobal(int val = -1) global
    return GetSetGameGlobal("marasSpousesCount", val)
EndFunction

int Function GetSetPlayerHousesCountGlobal(int val = -1) global
    return GetSetGameGlobal("marasPlayerHousesCount", val)
EndFunction

bool Function GetSetHasSkyrimNetGlobal(int val = -1) global
    int res = GetSetGameGlobal("marasHasSkyrimNet", val)

    return res == 1
EndFunction

;/ ==============================
   SECTION: KEYWORDS
============================== /;

;/
  Returns a game keyword by name.
/;
Keyword Function GetGameKeyword(string keywordName) global
    Keyword res = JDB_solveForm(GetInitialDataKey() + ".gameKeywords." + keywordName) as Keyword

    if(!res)
        TTM_Debug.trace("Couldn't find Keyword with name " + keywordName)
    endif

    return res
EndFunction

Keyword Function GetLocTypeDwellingKeyword() global
    return GetGameKeyword("locTypeDwelling")
EndFunction

Keyword Function GetLocTypeHouseKeyword() global
    return GetGameKeyword("locTypeHouse")
EndFunction

Keyword Function GetHomeSandboxKeyword() global
    return GetGameKeyword("marasHomeSandbox")
EndFunction

;/ ==============================
   SECTION: ARMORS
============================== /;

;/
  Returns a game item (Armor) by name.
/;
Armor Function GetGameArmor(string itemName) global
    Armor res = JDB_solveForm(GetInitialDataKey() + ".gameArmors." + itemName) as Armor
    if(res == none)
        TTM_Debug.trace("Couldn't find Armor with name " + itemName)
    endif
EndFunction

Armor Function GetMatrimonyRingArmor() global
    return GetGameArmor("matrimonyRing")
EndFunction

;/ ==============================
   SECTION: STATICS
============================== /;

;/
  Returns a MARAS static by name.
/;
Static Function GetGameStatic(string staticName) global
    Static res = JDB_solveForm(GetInitialDataKey() + ".gameStatics." + staticName) as Static

    if(res == none)
        TTM_Debug.trace("Couldn't find Static with name " + staticName)
    endif

    return res
EndFunction

Static Function GetHomeSandboxMarkerStatic() global
    return GetGameStatic("marasHomeSandboxMarker")
EndFunction

;/ ==============================
   SECTION: MISCS
============================== /;

;/
  Returns a MARAS static by name.
/;
MiscObject Function GetGameMisc(string miscName) global
    MiscObject res = JDB_solveForm(GetInitialDataKey() + ".gameMiscs." + miscName) as MiscObject

    if(res == none)
        TTM_Debug.trace("Couldn't find MiscObject with name " + miscName)
    endif

    return res
EndFunction

MiscObject Function GetGoldMisc() global
    return GetGameMisc("gold")
EndFunction

;/ ==============================
   SECTION: PACKAGES
============================== /;

;/
  Returns a MARAS package by name.
/;
Package Function GetGamePackage(string packageName) global
    Package res = JDB_solveForm(GetInitialDataKey() + ".gamePackages." + packageName) as Package

    if(res == none)
        TTM_Debug.trace("Couldn't find Package with name " + packageName)
    endif

    return res
EndFunction

Package Function GetHomeSandboxPackage() global
    return GetGamePackage("marasHomeSandbox")
EndFunction

;/ ==============================
   SECTION: SPELLS
============================== /;

;/
  Returns a MARAS package by name.
/;
Spell Function GetGameSpell(string spellName) global
    Spell res = JDB_solveForm(GetInitialDataKey() + ".gameSpells." + spellName) as Spell

    if(res == none)
        TTM_Debug.trace("Couldn't find Spell with name " + spellName)
    endif

    return res
EndFunction

Spell Function GetBreakdownCooldownSpell() global
    return GetGameSpell("marasBreakupCooldown")
EndFunction

Spell Function GetDemotedCooldownSpell() global
    return GetGameSpell("marasDemotedCooldown")
EndFunction

;/ ==============================
   SECTION: QUESTS
============================== /;

;/
  Returns the quest by name.
/;
Quest Function GetGameQuest(string questName) global
    int jQuests = JDB_solveObj(GetInitialDataKey() + ".gameQuests")
    string questId = JMap_getStr(jQuests, questName)

    Quest res = JString.decodeFormStringToForm("__formData|"+questId) as Quest

    if(res == none)
        TTM_Debug.trace("Couldn't find Quest with name " + questName)
    endif

    return res
EndFunction

Quest Function GetMarriageFinQuest() global
    return GetGameQuest("marriageFin")
EndFunction

Quest Function GetMarriageMainQuest() global
    return GetGameQuest("marriageMain")
EndFunction

Quest Function GetMarriageWeddingQuest() global
    return GetGameQuest("marriageWedding")
EndFunction

Quest Function GetMarriageBreakupQuest() global
    return GetGameQuest("marriageBreakup")
EndFunction

Quest Function GetMarasMainQuest() global
    return GetGameQuest("marasMain")
EndFunction

Quest Function GetMarasEnablePolygamyQuest() global
    return GetGameQuest("marasEnablePolygamy")
EndFunction

Quest Function GetMarasDialoguesQuest() global
    return GetGameQuest("marasDialogues")
EndFunction

Quest Function GetMarasCheckSpouseHomeQuest() global
    return GetGameQuest("marasCheckSpouseHome")
EndFunction

;/ ==============================
   SECTION: FACTIONS
============================== /;
;/
  Returns a game faction by name.
/;
Faction Function GetGameFaction(string factionName) global
    Faction res = JDB_solveForm(GetInitialDataKey() + ".gameFactions." + factionName) as Faction
    if(!res)
        TTM_Debug.trace("Couldn't find Faction with name " + factionName)
    endif
    return res
EndFunction

;/
  Returns the player faction.
/;
Faction Function GetPlayerFaction() global
    return GetGameFaction("playerFaction")
EndFunction

Faction Function GetMarriedFaction() global
    return GetGameFaction("married")
EndFunction

Faction Function GetPlayerBedOwnershipFaction() global
    return GetGameFaction("bedOwnership")
EndFunction

Faction Function GetMarriageAskedFaction() global
    return GetGameFaction("marriageAsked")
EndFunction

Faction Function GetCourtingFaction() global
    return GetGameFaction("courting")
EndFunction

Faction Function GetMarriageExcludedFaction() global
    return GetGameFaction("excluded")
EndFunction

Faction Function GetMarriagePotentialFaction() global
    return GetGameFaction("potential")
EndFunction

Faction Function GetPotentialHirelingFaction() global
    return GetGameFaction("potentialHireling")
EndFunction

;/
  Returns the Faction used to check spouse home ownership.
/;
Faction Function GetCheckSpouseHomeFaction() global
    return GetGameFaction("marasCheckSpouseHome")
EndFunction

;/
  Returns the Faction for tracked NPCs.
/;
Faction Function GetTrackedNpcFaction() global
    return GetGameFaction("marasTrackedNpcFaction")
EndFunction

;/
  Returns the Faction for spouse social class.
/;
Faction Function GetSpouseSocialClassFaction() global
    return GetGameFaction("marasSpouseSocialClassFaction")
EndFunction

;/
  Returns the Faction for spouse skill type.
/;
Faction Function GetSpouseSkillTypeFaction() global
    return GetGameFaction("marasSpouseSkillTypeFaction")
EndFunction

;/
  Returns the Faction for spouse which player assigned to their house.
/;
Faction Function GetSpouseHousedFaction() global
    return GetGameFaction("marasSpouseHoused")
EndFunction

;/
  Returns the Faction for spouse which shared their house with player.
/;
Faction Function GetSpouseSharedHouseFaction() global
    return GetGameFaction("marasSpouseSharedHouse")
EndFunction

;/
  Returns the Faction for spouse which didn't have any house originally.
/;
Faction Function GetSpouseNoInitialHouseFaction() global
    return GetGameFaction("marasSpouseNoInitialHome")
EndFunction

;/
  Returns the Faction for spouse which determine their hierarchy in family.
/;
Faction Function GetSpouseHierarchyFaction() global
    return GetGameFaction("marasHierarchy")
EndFunction


;/ ==============================
   SECTION: SPOUSE TYPES FACTIONS and CLASSES FORMLISTS
============================== /;
;/
  Returns the JMap of all factions.
/;
int Function GetSpouseTypesFactions() global
    return JDB_solveObj(GetInitialDataKey() + ".spouseTypesClassesFL")
EndFunction

;/
  Returns the FormList for a faction type.
/;
FormList Function GetSpouseTypesFactionsByType(string type) global
    return JMap_getForm(GetSpouseTypesFactions(), type) as FormList
EndFunction

;/
  Returns the JMap of all classes.
/;
int Function GetSpouseTypesClasses() global
    return JDB_solveObj(GetInitialDataKey() + ".spouseTypesFactionsFL")
EndFunction

;/
  Returns the FormList for a class type.
  @param type The class type key
  @return     The FormList for the class type
/;
FormList Function GetSpouseTypesClassesByType(string type) global
    return JMap_getForm(GetSpouseTypesClasses(), type) as FormList
EndFunction
