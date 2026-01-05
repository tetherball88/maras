;/
  TTM_ServiceSkyrimNet.psc

  Integrates MARAS mod with the SkyrimNet AI system.
  Handles syncing of player and NPC data, configuration, and polygamy state with SkyrimNet via JContainers and Lua.

  Main responsibilities:
    - Syncing tracked NPCs and player stats to SkyrimNet
    - Updating and clearing NPC/quest data in SkyrimNet
    - Handling configuration options and polygamy state
    - All actions are conditional on SkyrimNet being enabled (TTM_Data.GetHasSkyrimNet())
/;
scriptname TTM_ServiceSkyrimNet

;/
  Main maintenance function. Syncs all relevant data to SkyrimNet if enabled.
/;
Function Maintenance() global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    RegisterActions()
    RegisterDecorators()
EndFunction

Function RegisterActions() global
  RegisterAcceptProposalAction()
  RegisterBreakupEngagementAction()
  RegisterDivorseAction()
  RegisterAffectionEstrangedDivorceResolutionAction()
EndFunction

Function RegisterAcceptProposalAction() global
    SkyrimNetApi.RegisterAction("AcceptMarriageProposal", \
    GetActionProp("accept_marriage_proposal_description"), \
    "TTM_ServiceSkyrimNet", "AcceptProposalIsElgigible", "TTM_ServiceSkyrimNet", "AcceptProposalAction", "", "PAPYRUS", 1, \
    GetActionProp("accept_marriage_proposal_params"))
EndFunction

Bool Function AcceptProposalIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    bool isCooldownPassed = TTM_Utils.ActionConfirmationCooldownPassed("ConfirmAcceptProposal", akActor)
    return MARAS.IsNPCStatus(akActor, "any") && !MARAS.IsNPCStatus(akActor, "engaged") && !MARAS.IsNPCStatus(akActor, "married") \
        && TTM_Data.GetPlayer() != akActor && isCooldownPassed && !akActor.HasKeyword(TTM_Data.GetIgnoreProposeKeyword())
EndFunction

Function AcceptProposalAction(Actor akActor, string contextJson, string paramsJson) global
    bool confirm = TTM_Utils.ShowAIConfirmationMessage("ConfirmAcceptProposal", akActor)
    if(!confirm)
        return
    endif
    MARAS.PromoteNPCToStatus(akActor, "engaged")
EndFunction

Function SimulatePostWeddingIfSkippedBehavior(Actor npc) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    DirectNarration("Time passed... " + TTM_Utils.GetActorName(player) + " and " + TTM_Utils.GetActorName(npc) + " got married!", npc)
EndFunction

Function RegisterAffectionEstrangedDivorceResolutionAction() global
    SkyrimNetApi.RegisterAction("AffectionEstrangedDivorceResolution", \
    GetActionProp("affection_estranged_divorce_resolution_description"), \
    "TTM_ServiceSkyrimNet", "AffectionEstrangedDivorceResolutionIsEligible", "TTM_ServiceSkyrimNet", "AffectionEstrangedDivorceResolutionAction", "", "PAPYRUS", 1, \
    GetActionProp("affection_estranged_divorce_resolution_params"))
EndFunction

Function UnregisterAffectionEstrangedDivorceResolutionAction() global
    SkyrimNetApi.UnregisterAction("AffectionEstrangedDivorceResolution")
EndFunction

bool Function CheckIfDivorceQuestIsInPlay(Actor akActor) global
    Quest affectionDivorceQuest = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
    int stage = affectionDivorceQuest.GetCurrentStageID()
    if(stage != 10)
        return false
    endif
    Actor spouse = TTM_Utils.GetActorAlias(affectionDivorceQuest, "spouse")
    return spouse == akActor
EndFunction

Bool Function AffectionEstrangedDivorceResolutionIsEligible(Actor akActor, string contextJson, string paramsJson) global
    if(!MARAS.IsNPCStatus(akActor, "any"))
        return false
    endif

    bool isCooldownPassed = TTM_Utils.ActionConfirmationCooldownPassed("ConfirmDivorceDuringLowAffectionQuest", akActor)

    return CheckIfDivorceQuestIsInPlay(akActor) && isCooldownPassed
EndFunction

Function AffectionEstrangedDivorceResolutionAction(Actor akActor, string contextJson, string paramsJson) global
    bool confirm = TTM_Utils.ShowAIConfirmationMessage("ConfirmDivorceDuringLowAffectionQuest", akActor)
    if(!confirm)
        return
    endif
    string decison = SkyrimNetApi.GetJsonString(paramsJson, "decision", "none")
    if(decison == "reconcile")
        TTM_Data.GetMarasAffectionEstrangedDivorceQuest().SetStage(100)
        return
    elseif(decison == "divorce")
        TTM_Data.GetMarasAffectionEstrangedDivorceQuest().SetStage(200)
    endif
EndFunction

Function RegisterBreakupEngagementAction() global
    SkyrimNetApi.RegisterAction("CancelWeddingEngagement", \
  GetActionProp("cancel_engagement_description"), \
  "TTM_ServiceSkyrimNet", "BreakupEngagementIsElgigible", "TTM_ServiceSkyrimNet", "BreakupEngagementAction", "", "PAPYRUS", 1, \
  GetActionProp("cancel_engagement_params")) == 1
EndFunction

Bool Function BreakupEngagementIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    bool isCooldownPassed = TTM_Utils.ActionConfirmationCooldownPassed("ConfirmBreakupEngagement", akActor)
    return MARAS.IsNPCStatus(akActor, "engaged") && isCooldownPassed
EndFunction

Function BreakupEngagementAction(Actor akActor, string contextJson, string paramsJson) global
    bool confirm = TTM_Utils.ShowAIConfirmationMessage("ConfirmBreakupEngagement", akActor)
    if(!confirm)
        return
    endif
    MARAS.PromoteNPCToStatus(akActor, "jilted")
EndFunction

Function RegisterDivorseAction() global
    SkyrimNetApi.RegisterAction("InitiateDivorce", \
  GetActionProp("initiate_divorce_description"), \
  "TTM_ServiceSkyrimNet", "DivorseIsElgigible", "TTM_ServiceSkyrimNet", "DivorseAction", "", "PAPYRUS", 1, \
  GetActionProp("initiate_divorce_params")) == 1
EndFunction

Bool Function DivorseIsElgigible(Actor akActor, string contextJson, string paramsJson) global
    bool isCooldownPassed = TTM_Utils.ActionConfirmationCooldownPassed("ConfirmDivorce", akActor)
    return MARAS.IsNPCStatus(akActor, "married") && !CheckIfDivorceQuestIsInPlay(akActor) && isCooldownPassed
EndFunction

Function DivorseAction(Actor akActor, string contextJson, string paramsJson) global
    bool confirm = TTM_Utils.ShowAIConfirmationMessage("ConfirmDivorce", akActor)
    if(!confirm)
        return
    endif
    MARAS.PromoteNPCToStatus(akActor, "divorced")
EndFunction

Function RegisterDecorators() global
    SkyrimNetApi.RegisterDecorator("proposal_chance", "TTM_ServiceSkyrimNet", "GetMarriageChance")
    SkyrimNetApi.RegisterDecorator("get_household", "TTM_ServiceSkyrimNet", "GetHousehold")
EndFunction

string Function GetMarriageChance(Actor akActor) global
    string notReady = "{\"chance\": -1}"
    if(!MARAS.IsNPCStatus(akActor, "any") || MARAS.IsNPCStatus(akActor, "engaged") || MARAS.IsNPCStatus(akActor, "married"))
        return notReady
    endif
    if(akActor == TTM_Data.GetPlayer())
        return notReady
    endif

    string json = "{"

    json += "\"chance\": " + TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(akActor)

    return json + "}"
EndFunction

string Function GetHousehold(Actor akActor) global
    Actor player = TTM_Data.GetPlayer()
    if(akActor == player)
        return "{\"current\": \"\",\"future\": \"\", \"exPartner\": \"\"}"
    endif
    string exPartner = GenerateExPartnerLine(akActor)

    string current = "\"current\":"
    string future = "\"future\":"

    if(MARAS.IsNPCStatus(akActor, "any"))
        Actor[] coSpouses = MARAS.GetNPCsByStatus("married")
        Actor[] futureCoSpouses = MARAS.GetNPCsByStatus("engaged")
        current += "\"" + TTM_Utils.GetActorsNamesJson(coSpouses, akActor) + "\""
        future += "\""+TTM_Utils.GetActorsNamesJson(futureCoSpouses, akActor)+"\""
    else
        current += "\"\""
        future += "\"\""
    endif
    return "{"+current+", "+future+", "+exPartner+"}"
EndFunction


string Function GenerateExPartnerLine(Actor akActor) global
    Actor player = TTM_Data.GetPlayer()
    Actor existingSpouse = TTM_ServiceRelationsFinder.GetExistingSpouse(akActor)
    Actor existingCourting = TTM_ServiceRelationsFinder.GetExistingCourting(akActor)
    bool isTracking = MARAS.IsNPCStatus(akActor, "any")
    string finalLine = ""

    if(isTracking) ; NPC's perspective (involved with player)
        if(existingSpouse)
            finalLine = BuildLine(akActor, akActor, "married", existingSpouse)
        elseif(existingCourting)
            finalLine = BuildLine(akActor, akActor, "courting", existingCourting)
        endif
    else ; Ex-partner's perspective (their partner got involved with player)
        if(existingSpouse)
            finalLine = BuildLine(akActor, existingSpouse, "married", akActor)
        elseif(existingCourting)
            finalLine = BuildLine(akActor, existingCourting, "courting", akActor)
       endif
    endif

    if(finalLine == "")
        return "\"exPartner\": \"\""
    endif

    return "\"exPartner\": \"" + finalLine + "\""
EndFunction

string Function BuildLine(Actor akActor, Actor playerPartner, string originalRelationType, Actor originalPartner = none) global
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string playerPartnerName = TTM_Utils.GetActorName(playerPartner)
    string actorName = TTM_Utils.GetActorName(akActor)

    ; Get relationship status with player
    bool isSpouse = MARAS.IsNPCStatus(playerPartner, "married")
    bool isFiance = MARAS.IsNPCStatus(playerPartner, "engaged")
    bool isJilted = MARAS.IsNPCStatus(playerPartner, "jilted")
    bool isDivorced = MARAS.IsNPCStatus(playerPartner, "divorced")

    ; Define player relationship status description
    string playerRelationship = ""
    if(isFiance)
        playerRelationship = "became engaged to"
    elseif(isSpouse)
        playerRelationship = "married"
    elseif(isJilted)
        playerRelationship = "had a brief engagement with"
    elseif(isDivorced)
        playerRelationship = "married and later divorced"
    endif

    string finalLine = ""

    if(akActor == playerPartner) ; NPC's perspective (involved with player)
        if(originalRelationType == "married" && originalPartner)
            string exName = TTM_Utils.GetActorName(originalPartner)
            finalLine = actorName + " divorced " + exName + " when they " + playerRelationship + " " + playerName + "."
        elseif(originalRelationType == "courting" && originalPartner)
            string exName = TTM_Utils.GetActorName(originalPartner)
            finalLine = actorName + " ended their courtship with " + exName + " when they " + playerRelationship + " " + playerName + "."
        endif
    else ; Ex-partner's perspective (their partner became involved with player)
        if(originalRelationType == "married" && originalPartner)
            finalLine = playerPartnerName + " divorced " + actorName + " when " + playerPartnerName + " " + playerRelationship + " " + playerName + "."
        elseif(originalRelationType == "courting" && originalPartner)
            finalLine = actorName + " and " + playerPartnerName + " were courting until " + playerPartnerName + " " + playerRelationship + " " + playerName + "."
        endif
    endif

    return finalLine
EndFunction

Function RegisterPromotionEvent(Actor spouse, bool isDemoted) global
    string type = "ttm_spouse_hierarchy_change"
    string promotionKeyword = "promoted"
    if(isDemoted)
        promotionKeyword = "demoted"
    endif
    Actor player = TTM_Data.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string spouseName = TTM_Utils.GetActorName(spouse)
    string msg = playerName + " has " + promotionKeyword + " " + spouseName + " in the household hierarchy."
    SkyrimNetApi.RegisterEvent(type, msg, player, spouse)
EndFunction

Function DirectNarration(String content, Actor originatorActor = None, Actor targetActor = None) global
    if(!TTM_Data.GetHasSkyrimNet())
        return
    endif
    SkyrimNetApi.DirectNarration(content, originatorActor, targetActor)
EndFunction

string Function GetActionProp(string keyName) global
    return JsonUtil.GetPathStringValue("../MARAS/actions.json", "." + keyName)
EndFunction
