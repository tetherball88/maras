;/
  TTM_MainController.psc

  Main quest controller for the MARAS mod.

  Main responsibilities:
    - Initializes and maintains core MARAS systems
    - Handles mod events for relationship changes and AI commands
    - Integrates with SkyrimNet and TTLL if present
    - Triggers maintenance for buffs, quest tracking, and conditions
/;
Scriptname TTM_MainController extends Quest

Quest Property BYOHRelationshipAdoptionCWSiegeHandler Auto
Quest Property BYOHRelationshipAdoption  Auto
ReferenceAlias Property AdoptionSpouse  Auto
ReferenceAlias Property AdoptionCWSpouse  Auto

;/
  OnInit event: Called when the quest initializes. Triggers maintenance.
/;
Event OnInit()
    Maintenance()
EndEvent

;/
  Main maintenance function. Imports static data, checks for integrations, and triggers maintenance for all subsystems.
/;
Function Maintenance()
    TTM_ServiceAffection.Maintenance()
    Quest _self = self as Quest
    TTM_QuestTracker questTracker = _self as TTM_QuestTracker
    TTM_Conditions conditions = _self as TTM_Conditions
    TTM_ServiceIntimateMoments intimateMoments = _self as TTM_ServiceIntimateMoments
    TTM_MCM mcmScript = _self as TTM_MCM
    mcmScript.OnConfigInit()
    MigrateOldNpcHomeMarkers()
    MigrateSettingsToCpp()
    TTM_ServicePlayerHouse.CleanPapyrusUtilPackages()
    TTM_Data.ResetDLC1RV06DoOnceGlobal()

    ; check if SkyrimNet present
    ; otherwise all SkyrimNet related logic will be bypassed
    bool hasSkyrimNet = false
    int hasSkyrimNetGlobal = 0
    if(Game.GetModByName("SkyrimNet.esp") != 255)
        hasSkyrimNet = true
        hasSkyrimNetGlobal = 1
        RegisterForModEvent("SkyrimNet_OnPackageRemoved", "OnPackageRemoved")
    endif

    TTM_Data.SetHasSkyrimNet(hasSkyrimNet)
    TTM_Data.GetSetHasSkyrimNetGlobal(hasSkyrimNetGlobal)
    TTM_Data.GetSetHasAdultFramework()

    RegisterForModEvent("maras_status_changed", "OnRelationshipChanged")
    RegisterForModEvent("maras_status_changed_full", "OnRelationshipChangedFull")
    RegisterForModEvent("maras_hierarchy_changed", "OnChangeHierarchyRank")
    RegisterForModEvent("maras_change_affection", "OnSpouseAffectionChanged")
    RegisterForModEvent("maras_teammate_change", "OnTeammateChange")
    RegisterForModEvent("maras_teammate_added", "OnTeammateAdded")
    RegisterForModEvent("maras_teammate_removed", "OnTeammateRemoved")
    RegisterForModEvent("maras_dialogue_opened", "OnDialogueOpened")
    RegisterForModEvent("maras_decline", "OnActionDecline")
    RegisterForModEvent("maras_actor_died", "OnActorDied")
    RegisterForModEvent("maras_child_event", "OnChildEvent")
    RegisterForModEvent("maras_paternity_secret_resolved", "OnPaternitySecretResolved")
    RegisterForModEvent("maras_spawn_synthetic_child", "OnSpawnSyntheticChild")

    RegisterForModEvent("maras_cheating_caught", "OnMarasCheatingCaught")
    RegisterForModEvent("maras_open_relationship_walkin", "OnMarasOpenRelationshipWalkIn")
    RegisterForModEvent("maras_open_relationship_changed", "OnMarasOpenRelationshipChanged")
    RegisterForModEvent("maras_affair_exposed", "OnMarasAffairExposed")
    RegisterForModEvent("maras_affair_resolved", "OnMarasAffairResolved")
    RegisterForModEvent("maras_partner_home_arrival_event", "OnMarasPartnerHomeArrivalEvent")
    RegisterForModEvent("maras_stop_polygamy_quest", "OnStopPolygamyQuest")

    int fertilityEngine = TTM_ServiceFertility.GetActiveEngine()
    if (fertilityEngine == 1)
        RegisterForModEvent("FMR_Conception", "OnFMRConception")
        RegisterForModEvent("FMR_BabyBirth", "OnFMRBabyBirth")
        RegisterForModEvent("FMR_Abort", "OnFMRAbort")
        RegisterForModEvent("FMR_PregnancyCancelled", "OnFMRPregnancyCancelled")
        UnregisterForModEvent("FMR_ChildGrewUp")
        RegisterForModEvent("FMR_ChildGrewUpWithMother", "OnFMRChildGrewUp")
        RegisterForModEvent("FMR_ChildGrewUpNpcNpc", "OnFMRChildGrewUpNpcNpc")
        RegisterForModEvent("FMR_ChildSpawned", "OnFMRChildSpawned")
        RegisterForModEvent("FMR_ChildSentToTraining", "OnFMRChildSentToTraining")
        RegisterForModEvent("FMR_ChildFinishedTraining", "OnFMRChildFinishedTraining")
        RegisterForModEvent("FMR_AdultChildSpawned", "OnFMRAdultChildSpawned")
    elseif (fertilityEngine == 2)
        RegisterForModEvent("BeeingFemaleConception", "OnBeeingFemaleConception")
        RegisterForModEvent("BeeingFemaleLabor", "OnBeeingFemaleLabor")
        RegisterForModEvent("BeeingFemaleBirth", "OnBeeingFemaleBirth")
        RegisterForModEvent("BeeingFemaleChildSpawned", "OnBeeingFemaleChildSpawned")
        RegisterForModEvent("BeeingFemaleAdultChildSpawned", "OnBeeingFemaleAdultChildSpawned")
        RegisterForModEvent("BeeingFemaleAbort", "OnBeeingFemaleAbort")
        RegisterForModEvent("BeeingFemaleStillbirth", "OnBeeingFemaleStillbirth")
        RegisterForModEvent("BeeingFemale", "OnBeeingFemaleStateChange")
    endif
    ; ensure player has debug spell and check door perk

    Actor player = TTM_Data.GetPlayer()

    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    bool isInitialMigration = !TTM_Data.GetBool("MARAS_MarriageCheckDone") || !childrenLogicQst.GetMigrationDone() || !TTM_ServiceFertility.IsMigrated()
    if (isInitialMigration)
        TTM_Data.SetIsMigrating(true)
    endif

    TTM_ServiceMarriageQuest.CheckOngoingMarriage()
    TTM_ServiceBuff.Maintenance()
    questTracker.Maintenance()
    conditions.Maintenance()
    intimateMoments.Maintenance()

    TTM_ServiceSkyrimNet.Maintenance()

    RegisterForMenu("GiftMenu")

    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    ; it does check inside to see if spouse alias is empty and quest is running
    TTM_Utils.ReinforceEnablePolygamySpouseAlias()
    if(enablePolygamyQst.IsCompleted())
        Quest mcmQ = self as Quest
        TTM_MCM mcm = mcmQ as TTM_MCM
        mcm.TTM_EnablePolygamyToggle.SetValue(1)
    endif

    if(MARAS.GetDisablePolygamyQuest())
        StopAndResetPolygamyQuest()
    endif

    ; CheckAdoptionSpouse()

    childrenLogicQst.MigrateAllChildren()
    TTM_ServiceFertility.Maintenance()

    if (isInitialMigration)
        TTM_Data.SetIsMigrating(false)
    endif

    ; clean outdated StorageUtil data which was moved to SKSE plugin
    StorageUtil.ClearAllPrefix("TTM_Household_")
EndFunction

Event OnMarasCheatingCaught(string eventName, string jsonPayload, float threadID, Form victimSender)
    Actor player = TTM_Data.GetPlayer()
    Actor cheater = MARAS.GetJsonActor(jsonPayload, "cheater")
    Actor affairPartner = MARAS.GetJsonActor(jsonPayload, "affairPartner")
    Actor victim = MARAS.GetJsonActor(jsonPayload, "victim")
    TTM_Debug.debug("OnMarasCheatingCaught:cheater:" + cheater + ":affairPartner:" + affairPartner + ":victim:" + victim)
    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes

    ; Apply affection & SkyrimNet events
    if (victim == player)
        ; Player caught their partner cheating
        TTM_ServiceAffection.ApplyCheatingCaughtAffection(cheater, false)
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterCheatingCaught(player, cheater, affairPartner, true)
        endif
    elseif (cheater == player)
        ; Partner caught player cheating
        TTM_ServiceAffection.ApplyCheatingCaughtAffection(victim, true)
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterCheatingCaught(victim, player, affairPartner, true)
        endif
    else
        ; NPC-NPC affair caught
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterCheatingCaught(victim, cheater, affairPartner, true)
        endif
    endif

    if(victimSender == player)
        return ; player will decide how to react
    endif

    if(cheater == player)
        npcScenes.PartnerNoticedCheatingPlayer(victim)
    elseif(cheater != player && victim != player && affairPartner != player)
        npcScenes.StartCheatingScene(cheater, affairPartner, victim)
    endif
EndEvent

Event OnMarasOpenRelationshipWalkIn(string eventName, string jsonPayload, float threadID, Form witnessSender)
    Actor player = TTM_Data.GetPlayer()
    Actor partner = MARAS.GetJsonActor(jsonPayload, "partner")
    Actor sexPartner = MARAS.GetJsonActor(jsonPayload, "sexPartner")
    Actor witness = MARAS.GetJsonActor(jsonPayload, "witness")
    TTM_Debug.debug("OnMarasOpenRelationshipWalkIn:partner:" + partner + ":sexPartner:" + sexPartner + ":witness:" + witness)
    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes

    ; If witness is player's partner and walks in on player having open sex:
    if (partner == player)
        TTM_ServiceAffection.ApplyOpenRelationshipWalkInAffection(witness)
    endif

    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterOpenRelationshipWalkIn(witness, partner, sexPartner, true)
    endif

    if(witnessSender == player)
        return ; player will decide how to react
    endif

    if(partner == player)
        npcScenes.PartnerNoticedOpenRelPlayer(witness)
    endif
EndEvent

Event OnMarasOpenRelationshipChanged(string eventName, string jsonPayload, float numArg, Form sender)
    Actor player = TTM_Data.GetPlayer()
    Actor actorA = MARAS.GetJsonActor(jsonPayload, "actorA")
    Actor actorB = MARAS.GetJsonActor(jsonPayload, "actorB")
    bool isOpen = MARAS.GetJsonBool(jsonPayload, "isOpen")

    TTM_Debug.debug("OnMarasOpenRelationshipChanged: actorA=" + actorA + ", actorB=" + actorB + ", isOpen=" + isOpen)

    Actor playerPartner = None
    if (actorA == player)
        playerPartner = actorB
    elseif (actorB == player)
        playerPartner = actorA
    endif

    if (playerPartner != None)
        TTM_ServiceAffection.ApplyOpenRelationshipToggleAffection(playerPartner, isOpen)
    endif

    if (TTM_Data.GetHasSkyrimNet() && actorA != None && actorB != None)
        TTM_ServiceSkyrimNet.RegisterOpenRelationshipChangeEvent(actorA, actorB, isOpen)
    endif
EndEvent

Event OnMarasAffairExposed(string eventName, string jsonPayload, float numArg, Form sender)
    Actor cheater = MARAS.GetJsonActor(jsonPayload, "cheater")
    Actor victim = MARAS.GetJsonActor(jsonPayload, "victim")
    Actor affairPartner = MARAS.GetJsonActor(jsonPayload, "affairPartner")
    string discType = MARAS.GetJsonString(jsonPayload, "type")

    TTM_Debug.info("OnMarasAffairExposed: cheater=" + TTM_Utils.GetActorName(cheater) + ", victim=" + TTM_Utils.GetActorName(victim) + ", affairPartner=" + TTM_Utils.GetActorName(affairPartner) + ", type=" + discType)

    if (!cheater || !victim)
        return
    endif

    if (discType == "confessed")
        TTM_ServiceAffection.ApplyAffairConfessedAffection(cheater, victim)
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterAffairConfessed(cheater, victim, affairPartner)
        endif
    elseif (discType == "exposed")
        TTM_ServiceAffection.ApplyAffairExposedAffection(affairPartner, victim, cheater)
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterAffairExposed(affairPartner, victim, cheater)
        endif
    endif
EndEvent

Event OnMarasAffairResolved(string eventName, string jsonPayload, float numArg, Form sender)
    Actor cheater = MARAS.GetJsonActor(jsonPayload, "cheater")
    Actor victim = MARAS.GetJsonActor(jsonPayload, "victim")
    Actor affairPartner = MARAS.GetJsonActor(jsonPayload, "affairPartner")
    string resolution = MARAS.GetJsonString(jsonPayload, "resolution")

    TTM_Debug.info("OnMarasAffairResolved: cheater=" + TTM_Utils.GetActorName(cheater) + ", victim=" + TTM_Utils.GetActorName(victim) + ", affairPartner=" + TTM_Utils.GetActorName(affairPartner) + ", res=" + resolution)

    if (!cheater || !victim)
        return
    endif

    if (resolution == "confronted" || resolution == "allowed" || resolution == "breakup")
        TTM_ServiceAffection.ApplyAffairResolvedAffection(cheater, victim, resolution)
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterAffairResolved(cheater, victim, affairPartner, resolution)
        endif
    endif
EndEvent

Event OnMarasPartnerHomeArrivalEvent(string eventName, string jsonPayload, float isPriorityIntent, Form sender)
    Actor partner = sender as Actor
    if (!partner)
        partner = MARAS.GetJsonActor(jsonPayload, "partnerFormId")
    endif
    Actor lover = MARAS.GetJsonActor(jsonPayload, "loverFormId")
    string scenario = MARAS.GetJsonString(jsonPayload, "scenario")
    TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: scenario=" + scenario + ", isPriorityIntent=" + isPriorityIntent + ", partner=" + TTM_Utils.GetActorName(partner) + " (" + partner + "), lover=" + TTM_Utils.GetActorName(lover) + " (" + lover + "), payload=" + jsonPayload)
    if (!partner)
        return
    endif

    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes
    if (!npcScenes)
        TTM_Debug.warn("OnMarasPartnerHomeArrivalEvent: Aborting — TTM_NpcsScenes quest is None!")
        return
    endif

    if (scenario == "intent_suggest_open")
        TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing '" + scenario + "' -> npcScenes.PartnerSuggestOpen(" + TTM_Utils.GetActorName(partner) + ")")
        npcScenes.PartnerSuggestOpen(partner)
    elseif (scenario == "intent_suggest_close")
        TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing '" + scenario + "' -> npcScenes.PartnerSuggestClose(" + TTM_Utils.GetActorName(partner) + ")")
        npcScenes.PartnerSuggestClose(partner)
    elseif (scenario == "intent_confess_talking")
        TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing '" + scenario + "' -> npcScenes.PartnerConfessTalking(" + TTM_Utils.GetActorName(partner) + ")")
        npcScenes.PartnerConfessTalking(partner)
    elseif (scenario == "intent_confess_physical")
        TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing '" + scenario + "' -> npcScenes.PartnerConfessPhysical(" + TTM_Utils.GetActorName(partner) + ")")
        npcScenes.PartnerConfessPhysical(partner)
    elseif (scenario == "cheating_talking_in_house" || scenario == "cheating_physical_in_house" || scenario == "open_in_house")
        if (lover)
            TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing in-house '" + scenario + "' -> npcScenes.SetupSceneOnLocationChange(" + TTM_Utils.GetActorName(partner) + ", " + TTM_Utils.GetActorName(lover) + ")")
            npcScenes.SetupSceneOnLocationChange(partner, lover, scenario)
        else
            TTM_Debug.warn("OnMarasPartnerHomeArrivalEvent: Cannot start in-house '" + scenario + "' for " + TTM_Utils.GetActorName(partner) + " — resolved lover Actor is None!")
        endif
    elseif (scenario == "open_date_away" || scenario == "cheating_talking_letter" || scenario == "cheating_physical_letter")
        Location targetLoc = MARAS.GetJsonForm(jsonPayload, "targetLocationFormId") as Location
        ObjectReference targetMarker = MARAS.GetJsonForm(jsonPayload, "targetMarkerFormId") as ObjectReference
        TTM_QuestPartnerAway partnerAwayQst = TTM_Data.GetMarasPartnerAwayQuest() as TTM_QuestPartnerAway
        if (partnerAwayQst && lover && targetLoc && targetMarker)
            TTM_Debug.info("OnMarasPartnerHomeArrivalEvent: Routing away '" + scenario + "' -> partnerAwayQst.StartRendezvous(" + TTM_Utils.GetActorName(partner) + ", " + TTM_Utils.GetActorName(lover) + ", loc=" + targetLoc + ", marker=" + targetMarker + ")")
            partnerAwayQst.StartRendezvous(partner, lover, scenario, targetLoc, targetMarker)
        else
            TTM_Debug.warn("OnMarasPartnerHomeArrivalEvent: Cannot start away '" + scenario + "' — partnerAwayQst=" + partnerAwayQst + ", lover=" + lover + ", targetLoc=" + targetLoc + ", targetMarker=" + targetMarker)
        endif
    endif
EndEvent

Event OnStopPolygamyQuest(string eventName, string strArg, float numArg, Form sender)
    StopAndResetPolygamyQuest()
EndEvent

Function StopAndResetPolygamyQuest()
    TTM_QuestEnablePolygamy polygamyQst = TTM_Data.GetMarasEnablePolygamyQuest() as TTM_QuestEnablePolygamy
    if (polygamyQst && polygamyQst.IsRunning())
        polygamyQst.StopAndReset()
    endif
EndFunction

Event OnFMRConception(Form akMother, Form akFather, int iTrackingIndex)
    TTM_ServiceFertility.OnFMRConception(akMother, akFather, iTrackingIndex)
EndEvent

Event OnFMRBabyBirth(Form akMother, Form akFather, int iTrackingIndex)
    TTM_ServiceFertility.OnFMRBabyBirth(akMother, akFather, iTrackingIndex)
EndEvent

Event OnFMRAbort(Form akMother, Form akFather, int iTrackingIndex)
    TTM_ServiceFertility.OnFMRAbort(akMother, akFather, iTrackingIndex)
EndEvent

Event OnSpawnSyntheticChild(string eventName, string strArg, float afChildId, Form sender)
    int childID = afChildId as int
    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    if (childrenLogicQst)
        childrenLogicQst.SpawnSyntheticChild(childID)
    endif
EndEvent

Event OnFMRPregnancyCancelled(Form akMother, Form akFather, int iTrackingIndex)
    TTM_ServiceFertility.OnFMRPregnancyCancelled(akMother, akFather, iTrackingIndex)
EndEvent

Event OnFMRChildGrewUp(int childIndex, string childName, int gender, Form akMother)
    TTM_ServiceFertility.OnFMRChildGrewUp(childIndex, childName, gender, akMother)
EndEvent

Event OnFMRChildGrewUpNpcNpc(Form mother, Form father)
    TTM_ServiceFertility.OnFMRChildGrewUpNpcNpc(mother as Actor, father as Actor)
EndEvent

Event OnFMRChildSpawned(Form childActor, int childIndex, string childName)
    TTM_ServiceFertility.OnFMRChildSpawned(childActor, childIndex, childName)
EndEvent

Event OnFMRChildSentToTraining(int childIndex, string childName, string trainingLocation, string className)
    TTM_ServiceFertility.OnFMRChildSentToTraining(childIndex, childName, trainingLocation, className)
EndEvent

Event OnFMRChildFinishedTraining(int childIndex, string childName, string trainingLocation, string className)
    TTM_ServiceFertility.OnFMRChildFinishedTraining(childIndex, childName, trainingLocation, className)
EndEvent

Event OnFMRAdultChildSpawned(Form adultActor, int childIndex, string childName)
    TTM_ServiceFertility.OnFMRAdultChildSpawned(adultActor, childIndex, childName)
EndEvent

Event OnBeeingFemaleConception(Form akMother, Int aiChildCount, Form akFather0, Form akFather1, Form akFather2)
    TTM_ServiceFertility.OnBeeingFemaleConception(akMother, aiChildCount, akFather0, akFather1, akFather2)
EndEvent

Event OnBeeingFemaleLabor(Form akMother, Int aiChildCount, Form akFather0, Form akFather1, Form akFather2)
    TTM_ServiceFertility.OnBeeingFemaleLabor(akMother, aiChildCount, akFather0, akFather1, akFather2)
EndEvent

Event OnBeeingFemaleBirth(Form akMother, Form akFather, Form akBaby, string asBabyName, int aiBabySex)
    TTM_ServiceFertility.OnBeeingFemaleBirth(akMother, akFather, akBaby, asBabyName, aiBabySex)
EndEvent

Event OnBeeingFemaleChildSpawned(Form akMother, Form akFather, Form akChild, string asChildName)
    TTM_ServiceFertility.OnBeeingFemaleChildSpawned(akMother, akFather, akChild, asChildName)
EndEvent

Event OnBeeingFemaleAdultChildSpawned(Form akMother, Form akFather, Form akAdult, Form akChild, string asChildName)
    TTM_ServiceFertility.OnBeeingFemaleAdultChildSpawned(akMother, akFather, akAdult, akChild, asChildName)
EndEvent

Event OnBeeingFemaleAbort(Form akMother, Form akFather, int aiAbortusState, string asReason)
    TTM_ServiceFertility.OnBeeingFemaleAbort(akMother, akFather, aiAbortusState, asReason)
EndEvent

Event OnBeeingFemaleStillbirth(Form akMother, Form akFather)
    TTM_ServiceFertility.OnBeeingFemaleStillbirth(akMother, akFather)
EndEvent

Event OnBeeingFemaleStateChange(string eventName, string strArg, float numArg, Form sender)
    TTM_ServiceFertility.OnBeeingFemaleStateChange(eventName, strArg, numArg, sender)
EndEvent

Event OnPaternitySecretResolved(string eventName, string strArg, float numArg, Form sender)
    TTM_ServiceFertility.OnPaternitySecretResolved(eventName, strArg, numArg, sender)
EndEvent

;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
  @param statusEnum The enum value of the new status
/;
Event OnRelationshipChanged(String EventName, String status, Float statusEnum, Form npc)
    Actor npcA = npc as Actor

EndEvent

Event OnRelationshipChangedFull(String EventName, String jsonPayload, Float statusEnum, Form npc)
    if (TTM_Data.IsMigrating())
        return
    endif
    Actor initiator = MARAS.GetJsonActor(jsonPayload, "initiator")
    string initiatorName = MARAS.GetJsonString(jsonPayload, "initiatorName")
    Actor acceptor = MARAS.GetJsonActor(jsonPayload, "acceptor")
    string acceptorName = MARAS.GetJsonString(jsonPayload, "acceptorName")
    string status = MARAS.GetJsonString(jsonPayload, "status")
    TTM_Debug.info("MainController:OnRelationshipChangedFull: " + TTM_Utils.GetActorName(initiator) + " -> " + TTM_Utils.GetActorName(acceptor) + " (" + status + ")")

    bool isPlayerInvolved = initiator == TTM_Data.GetPlayer() || acceptor == TTM_Data.GetPlayer()

    if(isPlayerInvolved)
        Actor nonPlayerPartner = initiator
        if(initiator == TTM_Data.GetPlayer())
            nonPlayerPartner = acceptor
        endif
        if(status == "candidate")
            TTM_ServiceRelationships.MakeNpcCandidate(nonPlayerPartner)
        elseif(status == "dating")
            TTM_ServiceRelationships.MakeNpcDating(nonPlayerPartner)
        elseif(status == "lover")
            TTM_ServiceRelationships.MakeNpcLover(nonPlayerPartner)
        elseif(status == "engaged")
            TTM_ServiceRelationships.MakeNpcEngaged(nonPlayerPartner)
        elseif(status == "married")
            TTM_ServiceRelationships.MakeNpcMarried(nonPlayerPartner)
        elseif(status == "jilted")
            TTM_ServiceRelationships.MakeNpcJilted(nonPlayerPartner)
        elseif(status == "divorced")
            TTM_ServiceRelationships.MakeNpcDivorced(nonPlayerPartner)
        elseif(status == "exdating")
            TTM_ServiceRelationships.MakeNpcExDating(nonPlayerPartner)
        elseif(status == "exlover")
            TTM_ServiceRelationships.MakeNpcExLover(nonPlayerPartner)
        endif
    endif

    TTM_ServiceSkyrimNet.RegisterMaritalStatusChangeEvent(initiator, acceptor, status)
EndEvent

Event OnActionDecline(string eventName, string strArg, float numArg, Form akSpeaker)
    TTM_Debug.debug("Received MARAS decline event: " + strArg)
    Actor target = MARAS.GetJsonActor(strArg, "initiator")
    if(!target || target == akSpeaker)
        target = MARAS.GetJsonActor(strArg, "target")
    endif
    TTM_ServiceSkyrimNet.RegisterActionDeclineEvent(strArg, akSpeaker as Actor, target)
EndEvent

Event OnActorDied(string eventName, string strArg, float isPlayerKillerNum, Form sender)
    Actor victim = sender as Actor
    bool isPlayerKiller = (isPlayerKillerNum > 0.0)
    TTM_Debug.info("MainController:OnActorDied: " + TTM_Utils.GetActorName(victim) + "; isPlayerKiller=" + isPlayerKiller)
    TTM_ServiceRelationships.HandleSpouseDeath(victim, isPlayerKiller)
EndEvent

Event OnChildEvent(string eventName, string strArg, float numArg, Form sender)
    if (TTM_Data.IsMigrating())
        return
    endif
    string eventType = MARAS.GetJsonString(strArg, "eventType")
    Actor child = sender as Actor
    if(!child)
        child = MARAS.GetJsonActor(strArg, "child")
    endif
    Actor parentA = MARAS.GetJsonActor(strArg, "parentA")
    Actor parentB = MARAS.GetJsonActor(strArg, "parentB")
    Actor other = MARAS.GetJsonActor(strArg, "other")
    string reason = MARAS.GetJsonString(strArg, "reason")

    TTM_Debug.info("MainController:OnChildEvent: " + eventType + " for child " + TTM_Utils.GetActorName(child))

    if(eventType == "adopted")
        Actor player = TTM_Data.GetPlayer()
        if (parentA == player && parentB != player && parentB != None)
            TTM_ServiceAffection.ApplyChildAdoptedAffection(parentB)
        elseif (parentB == player && parentA != player && parentA != None)
            TTM_ServiceAffection.ApplyChildAdoptedAffection(parentA)
        endif
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterChildAdopted(child, parentA, parentB)
        endif
    elseif(eventType == "inherited")
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterChildInherited(child, other, parentA, parentB)
        endif
    elseif(eventType == "parents_changed")
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterChildParentsChanged(child, parentA, parentB, reason)
        endif
    endif
EndEvent


Event OnChangeHierarchyRank(String EventName, String promoteDemote, Float rankDiff, Form spouse)
    if (TTM_Data.IsMigrating())
        return
    endif
    Actor spouseA = spouse as Actor
    string spouseName = TTM_Utils.GetActorName(spouseA)
    TTM_Debug.debug("MainController:OnChangeHierarchyRank:"+promoteDemote+" " + spouseName + ";  rankDiff: " + rankDiff)
    if(rankDiff == 0)
        return
    endif

    TTM_Messages.RankChangeMsg(spouseName, rankDiff > 0)

    TTM_ServiceAffection.AddPromotionAffection(spouseA, rankDiff)
    if(rankDiff < 0)
        spouseA.AddSpell(TTM_Data.GetDemotedCooldownSpell())
    endif
    TTM_ServiceSkyrimNet.RegisterMaritalHiearchyChangeEvent(spouseA, rankDiff < 0)
EndEvent

Event OnSpouseAffectionChanged(String EventName, String level, Float affectionDiff, Form spouse)
    if (TTM_Data.IsMigrating())
        return
    endif
    Actor spouseA = spouse as Actor
    string spouseName = TTM_Utils.GetActorName(spouseA)

    string direction
    if(affectionDiff > 0)
        direction = "improved"
        ; if affection level restored complete affection estranged divorce quest
        if(level == "content" || level == "happy")
            Quest affectionEstrangedDivorce = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
            if(affectionEstrangedDivorce.IsRunning())
                Actor questSpouse = TTM_Utils.GetActorAlias(affectionEstrangedDivorce, "Spouse")
                if(questSpouse == spouseA)
                    affectionEstrangedDivorce.SetStage(150)
                endif
            endif
        endif
    elseif(affectionDiff < 0)
        direction = "worsened"
        if(level == "troubled")
            ; when affection drops to troubled, stop sharing home with player
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
            TTM_Data.GetAffectionQuestKeyword("troubled").SendStoryEvent()
        elseif(level == "estranged")
            ; when affection drops to estranged, stop sharing home with player and stop using player's home
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouseA, "affection")
            TTM_Data.GetAffectionQuestKeyword("estranged").SendStoryEvent()
        endif
    endif

    TTM_ServiceSkyrimNet.RegisterMariatalAffectionChangeEvent(spouseA, level, direction)

    TTM_Messages.AffectionChangeMsg(spouseName, level, affectionDiff > 0)
EndEvent

Event OnTeammateChange(String eventName, string strArg, float fltArg, Form sender)
    TTM_ServiceBuff.CalculateFollowerMultipliers()
EndEvent

Event OnMenuOpen(string menuName)
    if(menuName == "GiftMenu")
        TTM_ServiceGift.OnGiftMenuOpen()
    endif
endEvent


Event OnMenuClose(string menuName)
    if(menuName == "GiftMenu")
        TTM_ServiceGift.OnGiftMenuClose()
    endif
endEvent

Event OnDialogueOpened(string eventName, string strArg, float fltArg, Form sender)
    Actor player = TTM_Data.GetPlayer()
    Actor akActor = sender as Actor
    bool isMarasRegistered = MARAS.IsNPCStatus(akActor, "any")
    TTM_Debug.debug("OnDialogueOpened: Dialogue Menu with " + TTM_Utils.GetActorName(akActor) + " registered:" + isMarasRegistered)
    if(isMarasRegistered)
        TTM_ServiceAffection.AddDialogueStartedAffection(akActor)
        bool notMarried = !MARAS.IsNPCStatus(akActor, "married")
        bool notEngaged = !MARAS.IsNPCStatus(akActor, "engaged")
        ; calculate and apply NoAIMarriage if applicable for this dialogue partner
        TTM_Debug.debug("Checking NoAIMarriage for " + TTM_Utils.GetActorName(akActor))
        TTM_Data.SetGameNoAIChancesGlobals(akActor)
    endif
EndEvent

; One time migration to new markers, registering them in cpp code
Function MigrateOldNpcHomeMarkers()
    if(StorageUtil.GetIntValue(none, "TTM_NewNpcHomeMarkers", 0) == 0)
        Actor[] npcs = MARAS.GetNPCsByStatus("all")
        int i = 0
        int count = 0
        while(i < npcs.Length)
            ObjectReference npcHomeMarker = StorageUtil.GetFormValue(npcs[i], "HomeMarker") as ObjectReference
            if(npcHomeMarker)
                MARAS.SetTrackedNpcHomeMarker(npcs[i], npcHomeMarker)
                Utility.Wait(1.0)
                count += 1
            endif
            i += 1
        endwhile
        Debug.Notification("MARAS: Checked "+npcs.Length+" NPCs and migrated " + count + " home markers.")
    endif

    StorageUtil.SetIntValue(none, "TTM_NewNpcHomeMarkers", 1)
EndFunction

Function MigrateSettingsToCpp()
    if(MARAS.IsSettingsMigrated())
        return
    endif

    ; Migrate StorageUtil-backed settings to C++
    MARAS.SetSettingBool("skipWedding", StorageUtil.GetIntValue(none, "SkipWedding", 0) == 1)
    MARAS.SetSettingBool("alwaysSuccessMarriage", StorageUtil.GetIntValue(none, "AlwaysSuccessMarriage", 0) == 1)
    MARAS.SetSettingInt("logDestination", StorageUtil.GetIntValue(none, "LogDestination", 0))

    ; Migrate MCM_State confirmation toggles
    int valAccept = StorageUtil.GetIntValue(none, "TTM_PermanentMCMState.ConfirmAcceptProposal", -1)
    if(valAccept != -1)
        MARAS.SetSettingBool("confirmAcceptProposal", valAccept == 1)
    endif
    int valBreakup = StorageUtil.GetIntValue(none, "TTM_PermanentMCMState.ConfirmBreakupEngagement", -1)
    if(valBreakup != -1)
        MARAS.SetSettingBool("confirmBreakupEngagement", valBreakup == 1)
    endif
    int valDivorce = StorageUtil.GetIntValue(none, "TTM_PermanentMCMState.ConfirmDivorce", -1)
    if(valDivorce != -1)
        MARAS.SetSettingBool("confirmDivorce", valDivorce == 1)
    endif
    int valLowAffDivorce = StorageUtil.GetIntValue(none, "TTM_PermanentMCMState.ConfirmDivorceDuringLowAffectionQuest", -1)
    if(valLowAffDivorce != -1)
        MARAS.SetSettingBool("confirmDivorceDuringLowAffectionQuest", valLowAffDivorce == 1)
    endif

    MARAS.MarkSettingsMigrated()
    Debug.Notification("MARAS: Settings migrated to native storage.")
EndFunction

