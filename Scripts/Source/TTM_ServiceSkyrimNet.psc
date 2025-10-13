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
    RegisterEventSchemas()
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
    if(!akActor.IsInFaction(TTM_JData.GetTrackedNpcFaction()))
        TTM_Debug.trace("AcceptProposalIsElgigible:DoesntHaveTrackingFaction:SKIP"+akActor)
        return false
    endif
    bool isEligible = TTM_Utils.CandidateIsReadyToHearProposalAwait(akActor)
    TTM_Debug.trace("AcceptProposalIsElgigible:"+isEligible)
    return isEligible
EndFunction

Function AcceptProposalAction(Actor akActor, string contextJson, string paramsJson) global
    TTM_Debug.trace("AcceptProposalAction:"+akActor)
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
    return TTM_Utils.IsFiance(akActor)
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
    return TTM_Utils.IsSpouse(akActor)
EndFunction

Function DivorseAction(Actor akActor, string contextJson, string paramsJson) global
    TTM_Utils.SendRelationshipChangeEvent(akActor, "divorced")
EndFunction

Function RegisterDecorators() global
    SkyrimNetApi.RegisterDecorator("proposal_chance", "TTM_ServiceSkyrimNet", "GetMarriageChance")
    SkyrimNetApi.RegisterDecorator("marrital_life", "TTM_ServiceSkyrimNet", "GetCoSpouses")
EndFunction

string Function GetMarriageChance(Actor akActor) global
    string notReady = "{\"chance\": -1}"
    if(!akActor.IsInFaction(TTM_JData.GetTrackedNpcFaction()))
        TTM_Debug.trace("GetMarriageChance:Doesn'tHaveTrackingFaction:SKIP"+akActor)
        return notReady
    endif
    if(akActor == TTM_JData.GetPlayer())
        TTM_Debug.trace("GetMarriageChance:IsPlayer:SKIP:"+akActor)
        return notReady
    endif
    if(!TTM_Utils.CandidateIsReadyToHearProposalAwait(akActor))
        TTM_Debug.trace("GetMarriageChance:IsntReady:SKIP"+akActor)
        return notReady
    endif

    string json = "{"

    json += "\"chance\": " + TTM_MarriageDifficulty.calcMarriageSuccessChance(akActor)

    return json + "}"
EndFunction

string Function GetCoSpouses(Actor akActor) global
    Actor player = TTM_JData.GetPlayer()
    if(akActor == player)
        return "{\"current\": \"\",\"future\": \"\", \"exPartner\": \"\"}"
    endif
    string exPartner = GenerateExPartnerLine(akActor)

    string current = "\"current\":"
    string future = "\"future\":"

    if(TTM_Utils.IsTracking(akActor))
        Form[] coSpouses = TTM_ServiceNPCs.GetAllActorsFromBucket("married")
        Form[] futureCoSpouses = TTM_ServiceNPCs.GetAllActorsFromBucket("engaged")
        current += "\"" + TTM_Utils.GetActorsNamesJson(coSpouses, akActor) + "\""
        future += "\""+TTM_Utils.GetActorsNamesJson(futureCoSpouses, akActor)+"\""
    else
        current += "\"\""
        future += "\"\""
    endif


    return "{"+current+", "+future+", "+exPartner+"}"
EndFunction


string Function GenerateExPartnerLine(Actor akActor) global
    Actor player = TTM_JData.GetPlayer()
    Actor existingSpouse = TTRF_Store.GetSpouse(akActor)
    Actor existingCourting = TTRF_Store.GetCourting(akActor)
    Form[] existingLovers = TTRF_Store.GetLovers(akActor)
    bool isTracking = TTM_Utils.IsTracking(akActor)
    string finalLine = ""

    if(isTracking) ; NPC's perspective (involved with player)
        if(existingSpouse)
            finalLine = BuildLine(akActor, akActor, "married", existingSpouse)
        elseif(existingCourting)
            finalLine = BuildLine(akActor, akActor, "courting", existingCourting)
        elseif(existingLovers.Length > 0)
            finalLine = BuildLine(akActor, akActor, "lover", none, existingLovers)
        endif
    else ; Ex-partner's perspective (their partner got involved with player)
        if(existingSpouse)
            finalLine = BuildLine(akActor, existingSpouse, "married", akActor)
        elseif(existingCourting)
            finalLine = BuildLine(akActor, existingCourting, "courting", akActor)
        elseif(existingLovers.Length > 0)
            ; Handle just one example lover for simplicity
            finalLine = BuildLine(akActor, existingLovers[0] as Actor, "lover", akActor)
        endif
    endif

    if(finalLine == "")
        return "\"exPartner\": \"\""
    endif

    return "\"exPartner\": \"" + finalLine + "\""
EndFunction

string Function BuildLine(Actor akActor, Actor playerPartner, string originalRelationType, Actor originalPartner = none, Form[] originalPartners = none) global
    Actor player = TTM_JData.GetPlayer()
    string playerName = TTM_Utils.GetActorName(player)
    string playerPartnerName = TTM_Utils.GetActorName(playerPartner)
    string actorName = TTM_Utils.GetActorName(akActor)

    ; Get relationship status with player
    bool isSpouse = TTM_Utils.IsSpouse(playerPartner)
    bool isFiance = TTM_Utils.IsFiance(playerPartner)
    bool isJilted = TTM_Utils.IsJilted(playerPartner)
    bool isDivorced = TTM_Utils.IsDivorced(playerPartner)

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
        elseif(originalRelationType == "lover")
            string loversNames = ""
            if(originalPartners && originalPartners.Length > 0)
                loversNames = TTM_Utils.GetActorsNamesJson(originalPartners)
                if(originalPartners.Length == 1)
                    finalLine = actorName + " ended romantic relationship with " + loversNames + " when " + playerRelationship + " " + playerName + "."
                else
                    finalLine = actorName + " ended romantic relationships with " + loversNames + " when " + playerRelationship + " " + playerName + "."
                endif
            endif
        endif
    else ; Ex-partner's perspective (their partner became involved with player)
        if(originalRelationType == "married" && originalPartner)
            finalLine = playerPartnerName + " divorced " + actorName + " when " + playerPartnerName + " " + playerRelationship + " " + playerName + "."
        elseif(originalRelationType == "courting" && originalPartner)
            finalLine = actorName + " and " + playerPartnerName + " were courting until " + playerPartnerName + " " + playerRelationship + " " + playerName + "."
        elseif(originalRelationType == "lover" && originalPartner)
            finalLine = playerPartnerName + " ended romantic relationship with " + actorName + " when " + playerPartnerName + " " + playerRelationship + " " + playerName + "."
        endif
    endif

    return finalLine
EndFunction

Function RegisterEventSchemas() global
    RegisterEventSchemaDemoted()
    RegisterEventSchemaPromoted()
EndFunction

Function RegisterEventSchemaDemoted() global
    string type = "ttm_spouse_demoted"
    string name = "Spouse Demoted"
    string description = "Happens when a spouse is demoted in rank"
    string msg = "SPOUSE DEMOTED: {{actor}} has been demoted from rank {{oldRank}} to {{newRank}}."
    string jsonParams = "[{\"name\": \"actor\", \"type\": 0, \"required\": true, \"description\": \"The actor who was demoted.\"}, {\"name\": \"oldRank\", \"type\": 1, \"required\": true, \"description\": \"The actor's old rank.\"}, {\"name\": \"newRank\", \"type\": 1, \"required\": true, \"description\": \"The actor's new rank.\"}]"
    string renderParams = "{\"recent_events\":\""+msg+"\",\"raw\":\""+msg+"\",\"compact\":\""+msg+"\",\"verbose\":\""+msg+"\"}"
    SkyrimnetApi.RegisterEventSchema(type, name, description, jsonParams, renderParams, false, 0)
EndFunction

Function RegisterDemotedEvent(Actor spouse, int newRank, int oldRank) global
    string type = "ttm_spouse_demoted"
    string jsonData = "{\"actor\": \""+TTM_Utils.GetActorName(spouse)+"\", \"oldRank\": "+oldRank+", \"newRank\": "+newRank+"}"
    SkyrimNetApi.RegisterEvent(type, jsonData, spouse, none)
EndFunction

; todo promoted schema
Function RegisterEventSchemaPromoted() global
    string type = "ttm_spouse_promoted"
    string name = "Spouse Promoted"
    string description = "Happens when a spouse is promoted in rank"
    string msg = "SPOUSE PROMOTED: {{actor}} has been promoted from rank {{oldRank}} to {{newRank}}."
    string jsonParams = "[{\"name\": \"actor\", \"type\": 0, \"required\": true, \"description\": \"The actor who was promoted.\"}, {\"name\": \"oldRank\", \"type\": 1, \"required\": true, \"description\": \"The actor's old rank.\"}, {\"name\": \"newRank\", \"type\": 1, \"required\": true, \"description\": \"The actor's new rank.\"}]"
    string renderParams = "{\"recent_events\":\""+msg+"\",\"raw\":\""+msg+"\",\"compact\":\""+msg+"\",\"verbose\":\""+msg+"\"}"
    SkyrimnetApi.RegisterEventSchema(type, name, description, jsonParams, renderParams, false, 0)
EndFunction

Function RegisterPromotedEvent(Actor spouse, int newRank, int oldRank) global
    string type = "ttm_spouse_promoted"
    string jsonData = "{\"actor\": \""+TTM_Utils.GetActorName(spouse)+"\", \"oldRank\": "+oldRank+", \"newRank\": "+newRank+"}"
    SkyrimNetApi.RegisterEvent(type, jsonData, spouse, none)
EndFunction
