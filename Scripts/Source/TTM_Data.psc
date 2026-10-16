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
    return StorageUtil.GetIntValue(none, "TTM_IsMigrating", 0) != 0
EndFunction

Function SetIsMigrating(bool migrating) global
    if (migrating)
        StorageUtil.SetIntValue(none, "TTM_IsMigrating", 1)
    else
        StorageUtil.UnsetIntValue(none, "TTM_IsMigrating")
    endif
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

GlobalVariable Function GetLoverChanceGV() global
    return Game.GetFormFromFile(0x5d1, "TT_MARAS.esp") as GlobalVariable
EndFunction

GlobalVariable Function GetDatingChanceGV() global
    return Game.GetFormFromFile(0x5d0, "TT_MARAS.esp") as GlobalVariable
EndFunction


GlobalVariable Function GetMarriageChanceGV() global
    return Game.GetFormFromFile(0x15c, "TT_MARAS.esp") as GlobalVariable
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

Function SetGameNoAIChancesGlobals(Actor npc) global
    TTM_Debug.debug("calcMarriageSuccessChance: Setting AI chance globals for " + TTM_Utils.GetActorName(npc))
    GlobalVariable loverGV = GetLoverChanceGV()
    GlobalVariable datingGV = GetDatingChanceGV()
    GlobalVariable marriageGV = GetMarriageChanceGV()
    if(MARAS.GetSettingBool("alwaysSuccessMarriage"))
        TTM_Debug.debug("calcMarriageSuccessChance: Always success enabled, returning 1.0")
        loverGV.SetValue(100.0)
        datingGV.SetValue(100.0)
        marriageGV.SetValue(100.0)
        return
    endif

    Faction canFormMainWithoutCheatingFaction = GetCanFormNonCheatingMainFaction()
    bool isNpcCheating = npc.GetFactionRank(canFormMainWithoutCheatingFaction) == 0
    bool isPlayerCheating = GetPlayer().GetFactionRank(canFormMainWithoutCheatingFaction) == 0

    float chance = TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(npc) as int
    chance = chance
    bool isLover = MARAS.IsNPCStatus(npc, "lover")
    bool isDating = MARAS.IsNPCStatus(npc, "dating")
    float exMult = 1.0
    if(MARAS.IsNPCStatus(npc, "exlover") || MARAS.IsNPCStatus(npc, "exdating") || MARAS.IsNPCStatus(npc, "jilted") || MARAS.IsNPCStatus(npc, "divorced") )
        exMult = 0.75
    endif
    if(isNpcCheating || isPlayerCheating)
        chance = chance * 0.6
    endif
    chance = chance * exMult
    ; become lover easiest
    float loverChance = chance * 1.3
    ; become dating easier than engagement
    float datingChance = chance * 1.15
    float marriageChance = chance

    if(isLover)
        marriageChance = marriageChance * 1.2
        datingChance = datingChance * 1.5
    endif

    if(isDating)
        marriageChance = marriageChance * 1.5
    endif

    TTM_Debug.debug("calcMarriageSuccessChance:LoverChance:"+loverChance)
    TTM_Debug.debug("calcMarriageSuccessChance:DatingChance:"+datingChance)
    TTM_Debug.debug("calcMarriageSuccessChance:EngagementChance:"+marriageChance)

    loverGV.SetValue(loverChance)
    datingGV.SetValue(datingChance)
    marriageGV.SetValue(marriageChance)
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

Keyword Function GetIgnoreProposeKeyword() global
    return Game.GetFormFromFile(0xc0b, "TT_MARAS.esp") as Keyword
EndFunction

Keyword Function GetUseNpcVoiceInMiscKeyword() global
    return Game.GetFormFromFile(0xc20, "TT_MARAS.esp") as Keyword
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

Static Function GetRegularMarker() global
    return Game.GetFormFromFile(0x3b, "Skyrim.esm") as Static
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

Quest Function GetMarasNpcsScenesQuest() global
    return Game.GetFormFromFile(0x378, "TT_MARAS.esp") as Quest
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

Faction Function GetPlayerChildFaction() global
    return Game.GetFormFromFile(0xc23, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetPlayerFaction() global
    return Game.GetFormFromFile(0xdb1, "Skyrim.esm") as Faction
EndFunction

Faction Function GetAdoptionFaction() global
    return Game.GetFormFromFile(0x42b0, "HearthFires.esm") as Faction
EndFunction

Faction Function GetChildOwnedFaction() global
    return Game.GetFormFromFile(0x183a8, "HearthFires.esm") as Faction
EndFunction

;/
  Returns the Faction for spouse who has 1 or more children with player.
/;
Faction Function GetSpouseChildrenFaction() global
    return Game.GetFormFromFile(0xc24, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction tracking cheating state on spouse/partner.
  -1 = not cheating, 0 = cheating secret, 1 = cheating partner knows
/;
Faction Function GetSpouseCheatingFaction() global
    return Game.GetFormFromFile(0x2bd, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetSkyrimNetFilterFaction() global
    return Game.GetFormFromFile(0x12db, "SkyrimNet.esp") as Faction
EndFunction

;/
  Returns the Faction tracking open relationship state on spouse/partner.
  -1 = no open relationship, 0 = open relationship
/;
Faction Function GetSpouseOpenRelationshipFaction() global
    return Game.GetFormFromFile(0x2dc, "TT_MARAS.esp") as Faction
EndFunction

;/
  Returns the Faction tracking whether an NPC can form a relationship without cheating.
  0 = can't (in closed main-tier relationship), 1 = can (open main-tier, lover, or single)
/;
Faction Function GetCanFormNonCheatingMainFaction() global
    return Game.GetFormFromFile(0x5d2, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetOpenRelPlayerCaughtFaction() global
    return Game.GetFormFromFile(0x5a4, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetCheatingPlayerCaughtFaction() global
    return Game.GetFormFromFile(0x5a3, "TT_MARAS.esp") as Faction
EndFunction

Faction Function GetCheatingNpcCaughtFaction() global
    return Game.GetFormFromFile(0x5c4, "TT_MARAS.esp") as Faction
EndFunction

Message Function GetDivorceSettlementMsg() global
    return Game.GetFormFromFile(0x164, "TT_MARAS.esp") as Message
EndFunction

Message Function GetSkipWeddingMsg() global
    return Game.GetFormFromFile(0x165, "TT_MARAS.esp") as Message
EndFunction
