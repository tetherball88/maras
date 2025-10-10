;/
  TTM_ServiceSkyrimNet.psc

  Integrates MARAS mod with the SkyrimNet AI system.
  Handles syncing of player and NPC data, configuration, and polygamy state with SkyrimNet via JContainers and Lua.

  Main responsibilities:
    - Syncing tracked NPCs and player stats to SkyrimNet
    - Updating and clearing NPC/quest data in SkyrimNet
    - Handling configuration options and polygamy state
    - All actions are conditional on SkyrimNet being enabled (TTM_JData.GetHasSkyrimNet())

  Dependencies:
    - TTM_JCDomain
    - TTM_JData
    - TTM_ServiceNpcs
    - TTM_Utils
    - AIAgentFunctions
    - JContainers (JValue, JMap, JLua, etc)
/;
scriptname TTM_ServiceSkyrimNet

import TTM_JCDomain

;/
  Main maintenance function. Syncs all relevant data to SkyrimNet if enabled.
/;
Function Maintenance() global
    if(!TTM_JData.GetHasSkyrimNet())
        return
    endif
    RegisterActions()
    RegisterDecorators()
EndFunction

Function RegisterActions() global
  RegisterAcceptProposalAction()
  RegisterBreakupEngagementAction()
  RegisterDivorseAction()
EndFunction

Function RegisterAcceptProposalAction() global
    SkyrimNetApi.RegisterAction("AcceptMarriageProposal", \
    "{{decnpc(npc.UUID).name}} accepts {{player.name}}'s marriage proposal.", \
    "TTM_ServiceSkyrimNet", "AcceptProposalIsElgigible", "TTM_ServiceSkyrimNet", "AcceptProposalAction", "", "PAPYRUS", 1, \
    "{}")
EndFunction

Bool Function AcceptProposalIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    TTM_Debug.trace("AcceptProposalIsElgigible:"+TTM_ServiceNpcs.IsTrackingNpc(akActor))
    return TTM_ServiceNpcs.IsTrackingNpc(akActor) && !TTM_ServiceNpcs.IsSpouse(akActor) && !TTM_ServiceNpcs.IsFiance(akActor)
EndFunction

Function AcceptProposalAction(Actor akActor, string contextJson, string paramsJson) global
    TTM_Utils.SendRelationshipChangeEvent(akActor, "engaged")
EndFunction

Function SimulatePostWeddingIfSkippedBehavior(Actor npc) global
    if(!TTM_JData.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_JData.GetPlayer()
    SkyrimNetApi.DirectNarration("Time passed... " + TTM_Utils.GetActorName(player) + " and " + TTM_Utils.GetActorName(npc) + " got married!", npc)
EndFunction

Bool Function RegisterBreakupEngagementAction() global
    SkyrimNetApi.RegisterAction("CancelWeddingEngagement", \
  "{{decnpc(npc.UUID).name}} evaluates and potentially ends their engagement to {{player.name}} before the wedding takes place.", \
  "TTM_ServiceSkyrimNet", "BreakupEngagementIsElgigible", "TTM_ServiceSkyrimNet", "BreakupEngagementAction", "", "PAPYRUS", 1, \
  "") == 1
EndFunction

Bool Function BreakupEngagementIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    return TTM_ServiceNpcs.IsFiance(akActor)
EndFunction

Function BreakupEngagementAction(Actor akActor, string contextJson, string paramsJson) global
    TTM_Utils.SendRelationshipChangeEvent(akActor, "jilted")
EndFunction

Bool Function RegisterDivorseAction() global
    SkyrimNetApi.RegisterAction("InitiateDivorce", \
  "{{decnpc(npc.UUID).name}} terminates their marriage bond with {{player.name}}, ending their spousal relationship.", \
  "TTM_ServiceSkyrimNet", "DivorseIsElgigible", "TTM_ServiceSkyrimNet", "DivorseAction", "", "PAPYRUS", 1, \
  "") == 1
EndFunction

Bool Function DivorseIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    return TTM_ServiceNpcs.IsSpouse(akActor)
EndFunction

Function DivorseAction(Actor akActor, string contextJson, string paramsJson) global
    TTM_Utils.SendRelationshipChangeEvent(akActor, "divorced")
EndFunction

Function RegisterDecorators() global
    SkyrimNetApi.RegisterDecorator("get_marriage_chance", "TTM_ServiceSkyrimNet", "GetMarriageChance")
    SkyrimNetApi.RegisterDecorator("get_co_spouses", "TTM_ServiceSkyrimNet", "GetCoSpouses")
EndFunction

string Function GetMarriageChance(Actor akActor) global
    string json = "{"

    json += "\"chance\": " + TTM_MarriageDifficulty.calcMarriageSuccessChance(akActor)

    return json + "}"
EndFunction

string Function GetCoSpouses(Actor akActor) global
    Form[] coSpouses = TTM_ServiceNPCs.GetAllActorsFromBucket("married")
    Form[] futureCoSpouses = TTM_ServiceNPCs.GetAllActorsFromBucket("engaged")
    return "{\"current\": \"" + TTM_Utils.GetActorsNamesJson(coSpouses, akActor) + "\", \"future\": \""+TTM_Utils.GetActorsNamesJson(futureCoSpouses, akActor)+"\"}"
EndFunction
