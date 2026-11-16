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
    Quest _self = self as Quest
    TTM_QuestTracker questTracker = _self as TTM_QuestTracker
    TTM_Conditions conditions = _self as TTM_Conditions
    TTM_ServiceIntimateMoments intimateMoments = _self as TTM_ServiceIntimateMoments
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

    UnregisterForModEvent("maras_status_changed")
    RegisterForModEvent("maras_status_changed_full", "OnRelationshipChangedFull")
    UnregisterForModEvent("maras_hierarchy_changed")
    RegisterForModEvent("maras_change_affection", "OnSpouseAffectionChanged")
    UnregisterForModEvent("maras_teammate_change")
    UnregisterForModEvent("maras_teammate_added")
    UnregisterForModEvent("maras_teammate_removed")
    UnregisterForModEvent("maras_dialogue_opened")
    UnregisterForModEvent("maras_decline")
    RegisterForModEvent("maras_actor_died", "OnActorDied")
    UnregisterForModEvent("maras_child_event")
    UnregisterForModEvent("maras_paternity_secret_resolved")
    RegisterForModEvent("maras_spawn_synthetic_child", "OnSpawnSyntheticChild")

    RegisterForModEvent("maras_cheating_caught", "OnMarasCheatingCaught")
    RegisterForModEvent("maras_open_relationship_walkin", "OnMarasOpenRelationshipWalkIn")
    UnregisterForModEvent("maras_open_relationship_changed")
    UnregisterForModEvent("maras_affair_exposed")
    UnregisterForModEvent("maras_affair_resolved")
    RegisterForModEvent("maras_partner_home_arrival_event", "OnMarasPartnerHomeArrivalEvent")
    RegisterForModEvent("maras_stop_polygamy_quest", "OnStopPolygamyQuest")

    int fertilityEngine = TTM_ServiceFertility.GetActiveEngine()
    if (fertilityEngine == 1)
        RegisterForModEvent("FMR_Conception", "OnFMRConception")
        RegisterForModEvent("FMR_BabyBirth", "OnFMRBabyBirth")
        RegisterForModEvent("FMR_Abort", "OnFMRAbort")
        RegisterForModEvent("FMR_BabyMiscarriage", "OnFMRBabyMiscarriage")
        RegisterForModEvent("FMR_ActorStatus", "OnFMRActorStatus")
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
    questTracker.Maintenance()
    conditions.Maintenance()
    intimateMoments.Maintenance()

    RegisterSkyrimNetEventSchema()

    UnregisterForMenu("GiftMenu")

    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    ; it does check inside to see if spouse alias is empty and quest is running
    TTM_Utils.ReinforceEnablePolygamySpouseAlias()
    if(enablePolygamyQst.IsCompleted() && !MARAS.GetEnablePolygamy())
        MARAS.SetEnablePolygamy(true)
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

    MigrateLegacyRuntimeStorage(childrenLogicQst)

    ; clean outdated StorageUtil data which was moved to SKSE plugin
    StorageUtil.ClearAllPrefix("TTM_Household_")
    StorageUtil.ClearAllPrefix("TTM_MCMState")
    StorageUtil.ClearAllPrefix("Gift")
    StorageUtil.ClearAllPrefix("TTM_IntimacyActors_")
    StorageUtil.ClearAllPrefix("TTM_IntimacyHadSex_")
    StorageUtil.ClearAllPrefix("TTM_CheatersFormList_")
    StorageUtil.ClearAllPrefix("MARAS_OStim_Track_")
    StorageUtil.ClearAllPrefix("MARAS_SexLab_Track_")
    StorageUtil.ClearAllPrefix("MARAS.BF.")
    StorageUtil.ClearAllPrefix("MARAS_FMR_Migration_")
    StorageUtil.ClearAllPrefix("TTM_MovedNpcs")
    StorageUtil.ClearAllPrefix("TTM_NpcOriginalLocation")
    StorageUtil.ClearAllPrefix("SkyrimNet_Blacklisted_Actors")
    StorageUtil.ClearAllPrefix("TransientCritter")
    StorageUtil.ClearAllPrefix("TmpActorNames")
    StorageUtil.ClearAllPrefix("TTM_PartnerResentment")
    StorageUtil.ClearAllPrefix("TTM_PartnerUpset")
    StorageUtil.ClearAllPrefix("BrokeupTime")
    StorageUtil.ClearAllPrefix("LastTimeSharedIncome")
    StorageUtil.ClearAllPrefix("KilledByPlayer")
    StorageUtil.ClearAllPrefix("TTM_LastTimePayedDivorceFee")
    StorageUtil.ClearAllPrefix("TTM_DivorceLetter_")
    StorageUtil.ClearAllPrefix("TTM_SkyrimNet_SuppressActionNarrationUntil")
EndFunction

Function MigrateLegacyRuntimeStorage(TTM_ServiceChildrenLogic childrenLogicQst)
    if(StorageUtil.GetIntValue(none, "PlayerKiller", 0) == 1)
        MARAS.SetPlayerKiller(true)
        StorageUtil.UnsetIntValue(none, "PlayerKiller")
    endif
    if(StorageUtil.GetIntValue(none, "PlayerHadWedding", 0) == 1)
        MARAS.SetPlayerHadWedding(true)
        StorageUtil.UnsetIntValue(none, "PlayerHadWedding")
    endif
    if(StorageUtil.GetIntValue(none, "TTM_DontShowSkipWeddingMessage", 0) == 1)
        MARAS.SetDontShowSkipWeddingMessage(true)
        StorageUtil.UnsetIntValue(none, "TTM_DontShowSkipWeddingMessage")
    endif

    Form[] mutedForms = StorageUtil.FormListToArray(none, "SkyrimNet_Blacklisted_Actors")
    if(mutedForms.Length > 0)
        if(Game.GetModByName("SkyrimNet.esp") != 255)
            Faction filterFac = Game.GetFormFromFile(0x12DB, "SkyrimNet.esp") as Faction
            if(filterFac)
                int i = 0
                while(i < mutedForms.Length)
                    Actor mutedActor = mutedForms[i] as Actor
                    if(mutedActor)
                        mutedActor.RemoveFromFaction(filterFac)
                    endif
                    i += 1
                endwhile
            endif
        endif
        StorageUtil.FormListClear(none, "SkyrimNet_Blacklisted_Actors")
    endif

    Form[] movedForms = StorageUtil.FormListToArray(none, "TTM_MovedNpcs")
    if(movedForms.Length > 0)
        int i = 0
        while(i < movedForms.Length)
            Actor movedNpc = movedForms[i] as Actor
            if(movedNpc)
                ObjectReference marker = StorageUtil.GetFormValue(movedNpc, "TTM_NpcOriginalLocation") as ObjectReference
                if(marker)
                    movedNpc.MoveTo(marker)
                    marker.Disable()
                    marker.Delete()
                    StorageUtil.UnsetFormValue(movedNpc, "TTM_NpcOriginalLocation")
                endif
            endif
            i += 1
        endwhile
        StorageUtil.FormListClear(none, "TTM_MovedNpcs")
    endif

    Actor[] pChildren = MARAS.GetAllPlayerChildren()
    int cIdx = 0
    while(cIdx < pChildren.Length)
        Actor child = pChildren[cIdx]
        if(child)
            Actor legacyCritter = StorageUtil.GetFormValue(child, "TransientCritter") as Actor
            if(legacyCritter)
                legacyCritter.Disable()
                legacyCritter.Delete()
                StorageUtil.UnsetFormValue(child, "TransientCritter")
                if(child.GetActorValue("Variable07") == 5)
                    child.SetActorValue("Variable07", 0)
                    child.EvaluatePackage()
                endif
            endif
        endif
        cIdx += 1
    endwhile

    float legacyDivorceFeeDay = StorageUtil.GetFloatValue(none, "TTM_LastTimePayedDivorceFee", -1.0)
    int legacyPoor = 0
    if(childrenLogicQst)
        legacyPoor = childrenLogicQst.PoorCounter
    endif

    Actor[] allNpcs = MARAS.GetNPCsByStatus("all")
    int matchedCount = 0
    int nIdx = 0
    while(nIdx < allNpcs.Length)
        Actor npc = allNpcs[nIdx]
        if(npc)
            float brokeDay = StorageUtil.GetFloatValue(npc, "BrokeupTime", -1.0)
            float incomeDay = StorageUtil.GetFloatValue(npc, "LastTimeSharedIncome", -1.0)
            if(brokeDay >= 0.0 || incomeDay >= 0.0)
                matchedCount += 1
            endif
        endif
        nIdx += 1
    endwhile

    if(legacyDivorceFeeDay >= 0.0 || legacyPoor > 0 || matchedCount > 0)
        Actor[] seedNpcs = PapyrusUtil.ActorArray(matchedCount)
        float[] brokeupTimes = PapyrusUtil.FloatArray(matchedCount)
        float[] sharedIncomeTimes = PapyrusUtil.FloatArray(matchedCount)
        int writeIdx = 0
        nIdx = 0
        while(nIdx < allNpcs.Length && writeIdx < matchedCount)
            Actor npc = allNpcs[nIdx]
            if(npc)
                float brokeDay = StorageUtil.GetFloatValue(npc, "BrokeupTime", -1.0)
                float incomeDay = StorageUtil.GetFloatValue(npc, "LastTimeSharedIncome", -1.0)
                if(brokeDay >= 0.0 || incomeDay >= 0.0)
                    seedNpcs[writeIdx] = npc
                    brokeupTimes[writeIdx] = brokeDay
                    sharedIncomeTimes[writeIdx] = incomeDay
                    writeIdx += 1
                endif
            endif
            nIdx += 1
        endwhile
        MARAS.SeedLegacyUpgradeState(legacyDivorceFeeDay, legacyPoor, seedNpcs, brokeupTimes, sharedIncomeTimes)
        if(childrenLogicQst)
            childrenLogicQst.PoorCounter = MARAS.GetChildPoorCounter()
        endif
    endif
EndFunction

Event OnMarasCheatingCaught(string eventName, string jsonPayload, float threadID, Form victimSender)
    Actor player = TTM_Data.GetPlayer()
    Actor cheater = MARAS.GetJsonActor(jsonPayload, "cheater")
    Actor affairPartner = MARAS.GetJsonActor(jsonPayload, "affairPartner")
    Actor victim = MARAS.GetJsonActor(jsonPayload, "victim")
    TTM_Debug.debug("OnMarasCheatingCaught:cheater:" + cheater + ":affairPartner:" + affairPartner + ":victim:" + victim)
    TTM_NpcsScenes npcScenes = TTM_Data.GetMarasNpcsScenesQuest() as TTM_NpcsScenes

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

    if(witnessSender == player)
        return ; player will decide how to react
    endif

    if(partner == player)
        npcScenes.PartnerNoticedOpenRelPlayer(witness)
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

Event OnFMRBabyMiscarriage(Form akMother, string cause)
    TTM_ServiceFertility.OnFMRBabyMiscarriage(akMother, cause)
EndEvent

Event OnFMRActorStatus(Form akMother, int factionRank, string fatherName, int fatherRaceId)
    TTM_ServiceFertility.OnFMRActorStatus(akMother, factionRank, fatherName, fatherRaceId)
EndEvent

Event OnSpawnSyntheticChild(string eventName, string strArg, float afChildId, Form sender)
    int childID = afChildId as int
    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
    if (childrenLogicQst)
        int stage = MARAS.GetChildStage(childID)
        if (stage == 4)
            childrenLogicQst.SpawnAdultChild(childID)
        else
            childrenLogicQst.SpawnSyntheticChild(childID)
        endif
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

;/
  Handles relationship change events. Updates tracked NPCs and their status, and syncs with CHIM if enabled.
  @param npc    The NPC whose relationship changed
  @param status The new relationship status (candidate, engaged, married, jilted, divorced)
  @param statusEnum The enum value of the new status
/;
Event OnRelationshipChangedFull(String EventName, String jsonPayload, Float statusEnum, Form npc)
    if (TTM_Data.IsMigrating())
        return
    endif
    Actor initiator = MARAS.GetJsonActor(jsonPayload, "initiator")
    Actor acceptor = MARAS.GetJsonActor(jsonPayload, "acceptor")
    string status = MARAS.GetJsonString(jsonPayload, "status")
    TTM_Debug.info("MainController:OnRelationshipChangedFull: " + TTM_Utils.GetActorName(initiator) + " -> " + TTM_Utils.GetActorName(acceptor) + " (" + status + ")")

    Actor player = TTM_Data.GetPlayer()
    bool isPlayerInvolved = (initiator == player) || (acceptor == player)

    if(isPlayerInvolved)
        Actor nonPlayerPartner = initiator
        if(initiator == player)
            nonPlayerPartner = acceptor
        endif
        if(nonPlayerPartner)
            if(status == "engaged")
                bool alwaysSkipWeddings = MARAS.GetSettingBool("skipWedding")
                int answerSkipWedding = -1
                if(!alwaysSkipWeddings && MARAS.ShouldPromptSkipWedding())
                    answerSkipWedding = MARAS.ShowSkipWeddingModal(nonPlayerPartner)
                endif
                if(alwaysSkipWeddings || answerSkipWedding == 0)
                    TTM_Utils.FadeToBlack()
                    MARAS.PromoteNPCToStatus(nonPlayerPartner, "married")
                    if (TTM_Data.GetHasSkyrimNet())
                        MARAS.DirectNarration("Time passed... " + TTM_Utils.GetActorName(player) + " and " + TTM_Utils.GetActorName(nonPlayerPartner) + " got married!", nonPlayerPartner, player)
                    endif
                else
                    if(TTM_ServiceMarriageQuest.StartEngagement(nonPlayerPartner))
                        MARAS.NotifyEngagementCeremonyStarted(nonPlayerPartner)
                    endif
                endif
            elseif(status == "married")
                TTM_ServiceMarriageQuest.SkipWedding(nonPlayerPartner)
            elseif(status == "divorced")
                bool res = TTM_Data.GetDivorceKeyword().SendStoryEventAndWait(akRef1 = nonPlayerPartner)
                TTM_ServiceSpouseAssets.StopQuest(nonPlayerPartner)
                Quest affectionEstrangedDivorce = TTM_Data.GetMarasAffectionEstrangedDivorceQuest()
                if(affectionEstrangedDivorce && affectionEstrangedDivorce.IsRunning())
                    Actor questSpouse = TTM_Utils.GetActorAlias(affectionEstrangedDivorce, "Spouse")
                    if(questSpouse == nonPlayerPartner)
                        (affectionEstrangedDivorce as TTM_QuestAffectionEstrangedDivorce).FinishQuest(false)
                    endif
                endif
            endif
        endif
    endif
EndEvent

Event OnActorDied(string eventName, string strArg, float isPlayerKillerNum, Form sender)
    Actor victim = sender as Actor
    if (!victim)
        return
    endif
    bool isPlayerKiller = (isPlayerKillerNum > 0.0)
    TTM_Debug.info("MainController:OnActorDied: " + TTM_Utils.GetActorName(victim) + "; isPlayerKiller=" + isPlayerKiller)
    Actor player = TTM_Data.GetPlayer()
    bool spouse = MARAS.IsNPCStatus(victim, "married") || MARAS.WasMarriedTo(victim, player)
    bool engaged = MARAS.IsNPCStatus(victim, "engaged") || MARAS.WasEngagedTo(victim, player)
    if ((spouse || engaged) && isPlayerKiller)
        MARAS.SetPlayerKiller(true)
        TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(victim, "deceased")
    endif
    TTM_ServiceMarriageQuest.ResetMarriageQuests(victim)
EndEvent

Event OnSpouseAffectionChanged(String EventName, String level, Float affectionDiff, Form spouse)
    if (TTM_Data.IsMigrating())
        return
    endif
    Actor spouseA = spouse as Actor

    if(affectionDiff > 0)
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
        if(level == "troubled")
            ; when affection drops to troubled, stop sharing home with player
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
            TTM_Data.GetAffectionQuestKeyword("troubled").SendStoryEvent()
        elseif(level == "estranged")
            ; when affection drops to estranged, stop sharing home with player and stop using player's home
            TTM_ServicePlayerHouse.ReleaseSpouseFromPlayerHome(spouseA, "affection")
            TTM_ServiceSpouseAssets.StopShareHouseWithPlayer(spouseA, "affection")
            TTM_Data.GetAffectionQuestKeyword("estranged").SendStoryEvent()
        endif
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
        MARAS.NotifyLocalized("hud.legacy_migrated_home_markers", npcs.Length as string, count as string)
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
    MARAS.NotifyLocalized("hud.legacy_migrated_settings")
EndFunction

Function RegisterSkyrimNetEventSchema()
    if (!TTM_Data.GetHasSkyrimNet())
        return
    endif
    string name = "MARAS Event"
    string description = "Happens on different occasions, related to marriage life"
    string jsonParams = "[" \
        + "{\"name\": \"type\", \"type\": 0, \"required\": true, \"description\": \"Type of MARAS event.\"}," \
        + "{\"name\": \"declinedAction\", \"type\": 0, \"required\": false, \"description\": \"If player declined what kind of declanation it was.\"}," \
        + "{\"name\": \"msg\", \"type\": 0, \"required\": true, \"description\": \"Event message to send.\"}," \
        + "{\"name\": \"skipTrigger\", \"type\": 2, \"required\": false, \"description\": \"If trigger should be skipped.\"}," \
        + "{\"name\": \"newStatus\", \"type\": 0, \"required\": false, \"description\": \"New marital status in case of marital status change event.\"}," \
        + "{\"name\": \"affectionLevel\", \"type\": 0, \"required\": false, \"description\": \"New affection level in case of marital affection change event.\"}," \
        + "{\"name\": \"affectionDirection\", \"type\": 0, \"required\": false, \"description\": \"Direction of affection change in case of marital affection change event.\"}," \
        + "{\"name\": \"isDemoted\", \"type\": 2, \"required\": false, \"description\": \"Whether the spouse was demoted in case of hierarchy change event.\"}," \
        + "{\"name\": \"child\", \"type\": 0, \"required\": false, \"description\": \"Child name or form ID in case of child event.\"}," \
        + "{\"name\": \"parentA\", \"type\": 0, \"required\": false, \"description\": \"Parent A in case of child event.\"}," \
        + "{\"name\": \"parentB\", \"type\": 0, \"required\": false, \"description\": \"Parent B in case of child event.\"}," \
        + "{\"name\": \"reason\", \"type\": 0, \"required\": false, \"description\": \"Reason for child custody or parent change.\"}," \
        + "{\"name\": \"bioFather\", \"type\": 0, \"required\": false, \"description\": \"Biological father name in paternity events.\"}," \
        + "{\"name\": \"presumedFather\", \"type\": 0, \"required\": false, \"description\": \"Presumed father name in paternity events.\"}," \
        + "{\"name\": \"paternityOutcome\", \"type\": 0, \"required\": false, \"description\": \"Outcome of paternity intrigue evaluation.\"}," \
        + "{\"name\": \"trainingLocation\", \"type\": 0, \"required\": false, \"description\": \"Training location for child training events.\"}," \
        + "{\"name\": \"gender\", \"type\": 0, \"required\": false, \"description\": \"Gender of the child (male/female).\"}," \
        + "{\"name\": \"relation\", \"type\": 0, \"required\": false, \"description\": \"Relationship noun of the child (son/daughter).\"}," \
        + "{\"name\": \"cheater\", \"type\": 0, \"required\": false, \"description\": \"Cheater actor name.\"}," \
        + "{\"name\": \"victim\", \"type\": 0, \"required\": false, \"description\": \"Victim partner actor name.\"}," \
        + "{\"name\": \"affairPartner\", \"type\": 0, \"required\": false, \"description\": \"Affair partner actor name.\"}," \
        + "{\"name\": \"resolution\", \"type\": 0, \"required\": false, \"description\": \"Resolution of affair (confronted, allowed, breakup).\"}," \
        + "{\"name\": \"isOpen\", \"type\": 2, \"required\": false, \"description\": \"Whether open relationship is active.\"}," \
        + "{\"name\": \"isWalkIn\", \"type\": 2, \"required\": false, \"description\": \"Whether event was a walk-in discovery.\"}," \
        + "{\"name\": \"mother\", \"type\": 0, \"required\": false, \"description\": \"Mother actor name.\"}," \
        + "{\"name\": \"partner\", \"type\": 0, \"required\": false, \"description\": \"Partner actor name.\"}," \
        + "{\"name\": \"speaker\", \"type\": 0, \"required\": false, \"description\": \"Speaker actor name.\"}," \
        + "{\"name\": \"initiator\", \"type\": 0, \"required\": false, \"description\": \"Initiator actor name.\"}," \
        + "{\"name\": \"target\", \"type\": 0, \"required\": false, \"description\": \"Target actor name.\"}" \
        + "]"
    string renderParams = "{\"recent_events\":\"{{msg}}\",\"raw\":\"{{msg}}\",\"compact\":\"{{msg}}\",\"verbose\":\"{{msg}}\"}"
    SkyrimnetApi.RegisterEventSchema("ttm_event", name, description, jsonParams, renderParams, false, 0)
EndFunction

