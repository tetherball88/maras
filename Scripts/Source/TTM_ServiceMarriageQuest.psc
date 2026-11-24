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
    if (!fiance)
        return false
    endif

    Quest main = TTM_Data.GetMarriageMainQuest()
    ; If a previous engagement failed at stage 250 (RelationshipMarriageBreakUp),
    ; reset the stuck quest so a new engagement can start cleanly.
    if (main.IsRunning() && main.GetStage() >= 250)
        ResetMarriageQuests()
    endif
    ; Wedding quest in progress, try to re-start it when current wedding finished
    if (main.IsRunning() && main.GetStage() >= 20)
        return false
    endif

    ; Raise vanilla relationship rank BEFORE filling LoveInterest (which grants ALFC
    ; MarriageCourtingFaction 0x0007431A) so CHRR cannot trigger RelationshipMarriageBreakUp.
    Actor player = TTM_Data.GetPlayer()
    bool removedCourting = false
    Faction courtingFac = Game.GetFormFromFile(0x7431A, "Skyrim.esm") as Faction
    if (fiance.GetRelationshipRank(player) < 3)
        if (courtingFac && fiance.IsInFaction(courtingFac))
            fiance.RemoveFromFaction(courtingFac)
            removedCourting = true
        endif
        fiance.SetRelationshipRank(player, 3)
        ; Allow Story Manager to drain CHRR before LoveInterest adds MarriageCourtingFaction
        Utility.Wait(0.1)
    endif

    ReferenceAlias fianceAl = main.GetAliasByName("LoveInterest") as ReferenceAlias
    fianceAl.ForceRefTo(fiance)
    if (removedCourting && courtingFac)
        fiance.AddToFaction(courtingFac)
    endif
    main.SetStage(20)

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
        TTM_Debug.debug("TTM_ServiceMarriageQuest:ResetMarriageQuests:Skip - LoveInterest is different from target npc " + npcName)
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
    if(currentMarriageTarget)
        currentMarriageTarget.Clear()
    endif
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

    if(loveInterest == fiance && mainMarr.GetStage() >= 20 && mainMarr.GetStage() < 100 && mainMarr.IsRunning() && (!wedding.IsRunning() || !wedding.GetStageDone(100)))
        ResetMarriageQuests(fiance, true)
        Actor player = TTM_Data.GetPlayer()

        ; this code is almost same as in original RelationshipMarriageWedding quest on stage 100 when player is officially married
        Game.AddAchievement(33)
        if (loveInterest.GetRelationshipRank(player) < 4)
            loveInterest.SetRelationshipRank(player, 4)
        endif

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
    TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckOngoingMarriage")
    ; Check if we have already performed this initial check for this save
    if TTM_Data.GetBool("MARAS_MarriageCheckDone")
        TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckOngoingMarriage:SKIP")
        return
    endif
    TTM_Data.SetBool("MARAS_MarriageCheckDone", true)

    Quest marriageFin = TTM_Data.GetMarriageFinQuest()
    if(marriageFin.IsRunning())

        Actor loveInterest = TTM_Utils.GetActorAlias(marriageFin, "LoveInterest")
        TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Married:"+loveInterest)
        if(loveInterest)
            bool isStage20Done = marriageFin.GetStageDone(20)
            OnMarriageFinQstStart()
            loveInterest.SetRelationshipRank(TTM_Data.GetPlayer(), 4)
            MARAS.PromoteNPCToStatus(loveInterest, "married", sendEvent = false)
            MARAS.SetPermanentAffection(loveInterest, 100, sendEvent = false)
            MARAS.PacifyFianceOrSpouse(loveInterest)
            Location playerHouseLoc = TTM_Utils.GetSpouseMigrationPlayerHouse()
            if(playerHouseLoc)
                TTM_ServicePlayerHouse.MoveSpouseToHouse(loveInterest, playerHouseLoc, true)
            endif
            ; player moved to spouse's house
            if(isStage20Done)
                TTM_ServiceSpouseAssets.StartShareHomeWithPlayer(loveInterest)
            endif
        endif
        return
    endif

    Quest marriageMain = TTM_Data.GetMarriageMainQuest()
    int currentMarriageMainStage = marriageMain.GetCurrentStageID()
    if(currentMarriageMainStage == 20 || currentMarriageMainStage == 30 || TTM_Data.GetMarriageBreakupQuest().GetCurrentStageID() == 100)
        Actor loveInterest = TTM_Utils.GetActorAlias(marriageMain, "LoveInterest")
        TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Engaged:"+loveInterest)
        if(loveInterest)
            loveInterest.SetRelationshipRank(TTM_Data.GetPlayer(), 3)
            MARAS.PromoteNPCToStatus(loveInterest, "engaged", sendEvent = false)
            MARAS.SetPermanentAffection(loveInterest, 100, sendEvent = false)
            MARAS.PacifyFianceOrSpouse(loveInterest)
        endif
        return
    endif

    if(marriageMain.GetStageDone(250) && TTM_Data.GetMarriageBreakupQuest().GetCurrentStageID() != 100)
        Actor loveInterest = TTM_Utils.GetActorAlias(marriageMain, "LoveInterest")
        TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckOngoingMarriage:Jilted:"+loveInterest)
        if(loveInterest)
            MARAS.PromoteNPCToStatus(loveInterest, "jilted", sendEvent = false)
            MARAS.SetPermanentAffection(loveInterest, 0, sendEvent = false)
        endif
        return
    endif
EndFunction

;/
  Resolves the active spouse/fiance across Skyrim's marriage and MARAS dialogue quests.
/;
Actor Function ResolveActiveWeddingSpouse() global
    Quest weddingQst = TTM_Data.GetMarriageWeddingQuest()
    Actor spouse = none
    if(weddingQst)
        spouse = TTM_Utils.GetActorAlias(weddingQst, "LoveInterest")
    endif
    if(!spouse)
        Quest finQst = TTM_Data.GetMarriageFinQuest()
        if(finQst)
            spouse = TTM_Utils.GetActorAlias(finQst, "LoveInterest")
        endif
    endif
    if(!spouse)
        Quest mainQst = TTM_Data.GetMarriageMainQuest()
        if(mainQst)
            spouse = TTM_Utils.GetActorAlias(mainQst, "LoveInterest")
        endif
    endif
    if(!spouse)
        Quest dialQst = TTM_Data.GetMarasDialoguesQuest()
        if(dialQst)
            spouse = TTM_Utils.GetActorAlias(dialQst, "CurrentMarriageTarget")
        endif
    endif
    return spouse
EndFunction

;/
  Records wedding completion and ensures the spouse is promoted to married in MARAS.
/;
Function CompleteWeddingForSpouse(Actor spouse) global
    MARAS.SetPlayerHadWedding(true)
    if(!spouse)
        TTM_Debug.warn("TTM_ServiceMarriageQuest:CompleteWeddingForSpouse:No spouse found")
        return
    endif
    if(!MARAS.IsNPCStatus(spouse, "married"))
        TTM_Debug.info("TTM_ServiceMarriageQuest:CompleteWeddingForSpouse:Promoting " + TTM_Utils.GetActorName(spouse) + " to married")
        MARAS.PromoteNPCToStatus(spouse, "married")
    endif
EndFunction

;/
  Called when the wedding quest finishes. Sends relationship change event for marriage.
/;
Function OnWeddingQstFinish() global
    Actor spouse = ResolveActiveWeddingSpouse()
    CompleteWeddingForSpouse(spouse)
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
    TTM_Debug.debug("TTM_ServiceMarriageQuest:WeddingGotCanceled:"+TTM_Utils.GetActorName(spouse))
    MARAS.PromoteNPCToStatus(spouse, "jilted")
EndFunction

;/
  Called when the marriage quest stops. Checks for the next fiance.
/;
Function OnMarriageQuestStop() global
    TTM_Debug.debug("TTM_ServiceMarriageQuest:OnMarriageQuestStop")
    Quest wedding = TTM_Data.GetMarriageWeddingQuest()
    if(!TTM_Data.IsMigrating() && wedding && wedding.IsRunning() && wedding.GetStageDone(100))
        CompleteWeddingForSpouse(ResolveActiveWeddingSpouse())
    endif
    ResetMarriageQuests(fromOnstop = true)
    CheckNextFiance()
EndFunction

;/
  Checks for the next fiance and starts engagement if found.
/;
Function CheckNextFiance() global
    Actor[] fiances = MARAS.GetNPCsByStatus("engaged")
    Actor nextFiance = none
    if(fiances.Length > 0)
        nextFiance = fiances[0] as Actor
    endif
    TTM_Debug.debug("TTM_ServiceMarriageQuest:CheckNextFiance:"+TTM_Utils.GetActorName(nextFiance))
    if(nextFiance)
        Utility.Wait(0.3)
        if(StartEngagement(nextFiance))
            MARAS.NotifyEngagementCeremonyStarted(nextFiance)
        endif
    endif
EndFunction

;/
  Called when the marriage fin quest starts. Stops the quest and can start post-marriage logic.
/;
Function OnMarriageFinQstStart() global
    TTM_Debug.debug("TTM_ServiceMarriageQuest:OnMarriageFinQstStart:stops vanilla post marriage quest")
    Quest fin = TTM_Data.GetMarriageFinQuest()
    Actor finSpouse = none
    if(!TTM_Data.IsMigrating())
        if(fin)
            finSpouse = TTM_Utils.GetActorAlias(fin, "LoveInterest")
        endif
        if(!finSpouse)
            Quest wedding = TTM_Data.GetMarriageWeddingQuest()
            if(wedding && wedding.GetStageDone(100))
                finSpouse = ResolveActiveWeddingSpouse()
            endif
        endif
    endif
    if(fin)
        fin.stop()
    endif
    if(finSpouse)
        CompleteWeddingForSpouse(finSpouse)
    endif
EndFunction

Function OnMarriageBreakupReconcile() global
    TTM_Debug.debug("TTM_ServiceMarriageQuest:OnMarriageBreakupReconcile")
    Actor loveInterest = TTM_Utils.GetActorAlias(TTM_Data.GetMarriageBreakupQuest(), "LoveInterest")
    if(!loveInterest)
        return
    endif
    if(MARAS.IsNPCStatus(loveInterest, "engaged"))
        if(StartEngagement(loveInterest))
            MARAS.NotifyEngagementCeremonyStarted(loveInterest)
        endif
    else
        MARAS.PromoteNPCToStatus(loveInterest, "engaged")
    endif
EndFunction
