;/
  TTM_ServiceMarriageQuest.psc

  Handles logic for engagement, wedding, and marriage quest progression in the MARAS mod.

  Main responsibilities:
    - Starting and resetting engagement and wedding quests
    - Skipping wedding sequences and updating relationship state
    - Handling quest events for marriage, wedding, and post-marriage logic
    - Integrating with MARAS data and utility functions
/;
scriptname TTM_ServiceMarriageQuest

;/
  Starts the engagement process for a fiance, setting up quest aliases and stages.
  @param fiance The actor to engage
  @return       True if engagement started, false if wedding quest is already running
/;
bool Function StartEngagement(Actor fiance) global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:StartEngagement")

    Quest main = TTM_Data.GetMarriageMainQuest()
    ; Wedding quest in progress, try to re-start it when current wedding finished
    if(main.IsRunning() && main.GetStage() >= 20)
        return false
    endif

    ReferenceAlias fianceAl = main.GetAliasByName("LoveInterest") as ReferenceAlias

    main.setStage(20)
    fianceAl.ForceRefTo(fiance)

    ReferenceAlias currentMarriageTarget = TTM_Data.GetMarasDialoguesQuest().GetAliasByName("CurrentMarriageTarget") as ReferenceAlias
    currentMarriageTarget.ForceRefTo(fiance)

    return true
EndFunction

;/
  Resets all marriage-related quests, optionally completing or failing them.
  @param npc      The actor to reset quests for (optional)
  @param complete Whether to complete (true) or fail (false) objectives
/;
Function ResetMarriageQuests(Actor npc = none, bool complete = false, bool fromOnstop = false) global
    string npcName = TTM_Utils.GetActorName(npc)
    TTM_Debug.trace("TTM_ServiceMarriageQuest:ResetMarriageQuests:"+npcName+":complete:"+complete)
    Quest mainMarr = TTM_Data.GetMarriageMainQuest()
    Quest finMarr = TTM_Data.GetMarriageFinQuest()
    Actor loveInterest = TTM_Utils.GetActorAlias(mainMarr, "LoveInterest")
    if(!loveInterest)
        loveInterest = TTM_Utils.GetActorAlias(finMarr, "LoveInterest")
    endif
    if(npc && npc != loveInterest)
        TTM_Debug.warn("TTM_ServiceMarriageQuest:ResetMarriageQuests:Skip - LoveInterest is different from target npc " + npcName)
        return
    endif
    if(mainMarr.IsRunning() && !fromOnstop)
        TTM_Debug.trace("TTM_ServiceMarriageQuest:ResetMarriageQuests:MainMarriageQuest")
        if(complete)
            mainMarr.CompleteAllObjectives()
        else
            mainMarr.FailAllObjectives()
        endif

        mainMarr.Reset()
        mainMarr.Stop()
        ; restore main marriage stage done, some quests/mods uses it to find out if player is "ready to marry"
        mainMarr.SetStage(10)
        mainMarr.SetStage(15)

        mainMarr.SetObjectiveDisplayed(10, false)
        mainMarr.SetObjectiveDisplayed(20, false)
    endif
    Quest wedding = TTM_Data.GetMarriageWeddingQuest()
    if(wedding.IsRunning())
        TTM_Debug.trace("TTM_ServiceMarriageQuest:ResetMarriageQuests:Wedding")
        wedding.Reset()
        wedding.Stop()
    endif
    Quest fin = TTM_Data.GetMarriageFinQuest()
    if(fin.IsRunning())
        TTM_Debug.trace("TTM_ServiceMarriageQuest:ResetMarriageQuests:Fin")
        if(complete)
            fin.CompleteAllObjectives()
        else
            fin.FailAllObjectives()
        endif
        fin.Reset()
        fin.Stop()
        fin.SetObjectiveDisplayed(10, false)
    endif

    Quest breakUp = TTM_Data.GetMarriageBreakupQuest()
    if(breakUp.IsRunning())
        TTM_Debug.trace("TTM_ServiceMarriageQuest:ResetMarriageQuests:BreakUp")
        breakUp.Reset()
        breakUp.Stop()
    endif

    ReferenceAlias currentMarriageTarget = TTM_Data.GetMarasDialoguesQuest().GetAliasByName("CurrentMarriageTarget") as ReferenceAlias
    currentMarriageTarget.ForceRefTo(none)
EndFunction

;/
  Skips the wedding sequence for a fiance, directly applying marriage logic and rewards.
  @param fiance The actor to marry
/;
Function SkipWedding(Actor fiance) global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:SkipWedding:"+fiance)
    Quest mainMarr = TTM_Data.GetMarriageMainQuest()
    ReferenceAlias loveInterestAl = mainMarr.GetAliasByName("LoveInterest") as ReferenceAlias
    Actor loveInterest = loveInterestAl.GetActorRef()
    Quest wedding = TTM_Data.GetMarriageWeddingQuest()

    if(loveInterest == fiance && mainMarr.GetStage() >= 20 && mainMarr.IsRunning())
        ResetMarriageQuests(fiance, true)
        Actor player = TTM_Data.GetPlayer()

        ; this code is almost same as in original RelationshipMarriageWedding quest on stage 100 when player is officially married
        Game.AddAchievement(33)
        loveInterest.SetRelationshipRank(player, 4)

        Armor matrimonyRing = TTM_Data.GetMatrimonyRingArmor()
        if(player.GetItemCount(matrimonyRing) == 0)
            player.AddItem(matrimonyRing, 1)
        endif

        if(loveInterest.GetItemCount(matrimonyRing) == 0)
            loveInterest.AddItem(matrimonyRing, 1)
        endif
    endif
EndFunction

Function CheckOngoingMarriage() global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckOngoingMarriage")
    ; means MARAS mod is already in use and we can skip checking current marriage quests
    if(MARAS.GetStatusCount("all") != 0)
        TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckOngoingMarriage:SKIP")
        return
    endif

    Quest marriageFin = TTM_Data.GetMarriageFinQuest()
    if(marriageFin.IsRunning())
        Actor loveInterest = TTM_Utils.GetActorAlias(marriageFin, "LoveInterest")
        TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Married:"+loveInterest)
        if(loveInterest)
            bool isStage20Done = marriageFin.GetStageDone(20)
            MARAS.PromoteNPCToStatus(loveInterest, "married")
            ; player moved to spouse's house
            if(isStage20Done)
                TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(loveInterest)
            endif
            OnMarriageFinQstStart()
        endif
        return
    endif

    Quest marriageMain = TTM_Data.GetMarriageMainQuest()
    int currentMarriageMainStage = marriageMain.GetCurrentStageID()
    if(currentMarriageMainStage == 20 || currentMarriageMainStage == 30 || TTM_Data.GetMarriageBreakupQuest().GetCurrentStageID() == 100)
        Actor loveInterest = TTM_Utils.GetActorAlias(marriageMain, "LoveInterest")
        TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Engaged:"+loveInterest)
        if(loveInterest)
            MARAS.PromoteNPCToStatus(loveInterest, "engaged")
        endif
        return
    endif

    if(marriageMain.GetStageDone(250) && TTM_Data.GetMarriageBreakupQuest().GetCurrentStageID() != 100)
        Actor loveInterest = TTM_Utils.GetActorAlias(marriageMain, "LoveInterest")
        TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Jilted:"+loveInterest)
        if(loveInterest)
            MARAS.PromoteNPCToStatus(loveInterest, "jilted")
        endif
        return
    endif
EndFunction

;/
  Called when the wedding quest finishes. Sends relationship change event for marriage.
/;
Function OnWeddingQstFinish() global
    Actor spouse = TTM_Utils.GetActorAlias(TTM_Data.GetMarriageWeddingQuest(), "LoveInterest")
    TTM_Data.SetPlayerHadWedding()
    if(!spouse)
        TTM_Debug.err("TTM_ServiceMarriageQuest:OnWeddingQstFinish:But no spouse was found!")
        return
    endif
    MARAS.PromoteNPCToStatus(spouse, "married")
    TTM_Debug.trace("TTM_ServiceMarriageQuest:PlayerGotMarried:"+TTM_Utils.GetActorName(spouse))
    Quest enablePolygamyQst = TTM_Data.GetMarasEnablePolygamyQuest()
    Quest main = TTM_Data.GetMarasMainQuest()
    TTM_MCM mcm = main as TTM_MCM
    if(enablePolygamyQst.IsCompleted() || mcm.TTM_EnablePolygamyToggle.GetValue() == 1)
        Actor player = TTM_Data.GetPlayer()
        player.RemoveFromFaction(TTM_Data.GetMarriedFaction())
        TTM_Debug.trace("TTM_ServiceMarriageQuest:PlayerGotMarried:RemoveFromMarriedFactionForRemarrying")
    endif
EndFunction

;/
  Called when the wedding quest is cancelled. Sends relationship change event for jilted.
/;
Function OnWeddingQstCancel() global
    Actor spouse = TTM_Utils.GetActorAlias(TTM_Data.GetMarriageMainQuest(), "LoveInterest")
    if(!spouse)
        TTM_Debug.warn("TTM_ServiceMarriageQuest:WeddingGotCanceled:But no spouse was found")
        return
    endif
    TTM_Debug.trace("TTM_ServiceMarriageQuest:WeddingGotCanceled:"+TTM_Utils.GetActorName(spouse))
    MARAS.PromoteNPCToStatus(spouse, "jilted")
EndFunction

;/
  Called when the marriage quest stops. Checks for the next fiance.
/;
Function OnMarriageQuestStop() global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:OnMarriageQuestStop")
    ResetMarriageQuests(fromOnstop = true)
    CheckNextFiance()
EndFunction

;/
  Checks for the next fiance and starts engagement if found.
/;
Function CheckNextFiance() global
    Actor[] fiances = MARAS.GetNPCsByStatus("engaged")
    Actor nextFiance = fiances[0] as Actor
    TTM_Debug.trace("TTM_ServiceMarriageQuest:CheckNextFiance:"+TTM_Utils.GetActorName(nextFiance))
    if(nextFiance)
        Utility.Wait(0.3)
        MARAS.PromoteNPCToStatus(nextFiance, "engaged")
    endif
EndFunction

;/
  Called when the marriage fin quest starts. Stops the quest and can start post-marriage logic.
/;
Function OnMarriageFinQstStart() global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:OnMarriageFinQstStart:stops vanilla post marriage quest")
    TTM_Data.GetMarriageFinQuest().stop()
EndFunction

Function OnMarriageBreakupReconcile() global
    TTM_Debug.trace("TTM_ServiceMarriageQuest:OnMarriageBreakupReconcile")
    MARAS.PromoteNPCToStatus(TTM_Utils.GetActorAlias(TTM_Data.GetMarriageBreakupQuest(), "LoveInterest"), "engaged")
EndFunction
