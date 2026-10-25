;/
  TTM_ServiceFertilityBF.psc

  Integrates Beeing Female NG with M.A.R.A.S.
  Fully event-driven: conception, labor, birth, abort/stillbirth, father replacement (Baby NTR),
  child hatch/rename/removal and adult graduation. Existing Beeing Female data is imported once by Migrate().
  MARAS forcefully owns the behavior of Beeing Female children (packages, factions, dialogue).
/;
Scriptname TTM_ServiceFertilityBF

;/
  Maintenance: Called by TTM_ServiceFertility during startup and save loading.
  Soft-gates against BeeingFemale.esm. Nothing is polled; state arrives through ModEvents.
/;
Function Maintenance() global
    if (Game.GetModByName("BeeingFemale.esm") == 255)
        TTM_Debug.info("TTM_ServiceFertilityBF: BeeingFemale.esm not detected, BF listener disabled.")
        return
    endif
    TTM_Debug.info("TTM_ServiceFertilityBF: BeeingFemale.esm detected, listening for Beeing Female events.")
EndFunction

;/
  TakeOverChild: MARAS owns the behavior of a Beeing Female child. Beeing Female would keep the child a
  player teammate (default order 31 = follow and play), which makes MARAS skip its package override.
  Order 11 (go home) is persisted so Beeing Female itself keeps the teammate flag off on every load.
/;
Function TakeOverChild(Actor child) global
    if (!child)
        return
    endif
    child.SetPlayerTeammate(false)
    Faction followerFaction = Game.GetFormFromFile(0x05C84E, "Skyrim.esm") as Faction
    if (followerFaction)
        child.RemoveFromFaction(followerFaction)
    endif
    StorageUtil.SetIntValue(child, "FW.Child.Order", 11)
EndFunction

;/
  RegisterBeeingFemaleChild: Registers a physical Beeing Female child actor in MARAS (player household adoption
  when the player is involved, NPC household otherwise) and takes over its behavior.
/;
Function RegisterBeeingFemaleChild(Actor childActor, Actor bioMother, Actor bioFather, int syntheticID, int stage, bool isMigrating = false) global
    if (!childActor)
        return
    endif
    Actor player = TTM_Data.GetPlayer()
    TakeOverChild(childActor)
    Actor guardianA = none
    Actor guardianB = none
    if (syntheticID != 0)
        guardianA = MARAS.GetGuardianA(syntheticID)
        guardianB = MARAS.GetGuardianB(syntheticID)
    else
        Actor presumedFather = none
        if (bioMother != none)
            presumedFather = MARAS.GetPrimaryPartner(bioMother)
        endif
        float dob = StorageUtil.GetFloatValue(childActor, "FW.Child.Birthday", 0.0)
        string cName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
        if (cName == "")
            cName = childActor.GetDisplayName()
        endif
        syntheticID = MARAS.RegisterFertilityChild(0, bioMother, presumedFather, bioFather, dob, dob, cName)
        if (syntheticID != 0)
            guardianA = MARAS.GetGuardianA(syntheticID)
            guardianB = MARAS.GetGuardianB(syntheticID)
        endif
    endif
    bool isPlayerChild = false
    if (guardianA != none || guardianB != none)
        if (guardianA == player || guardianB == player)
            isPlayerChild = true
        elseif ((guardianA != none && MARAS.IsNPCStatus(guardianA, "married")) || (guardianB != none && MARAS.IsNPCStatus(guardianB, "married")))
            isPlayerChild = true
        endif
    elseif (player == bioMother || player == bioFather)
        isPlayerChild = true
    endif
    if (guardianA == none && guardianB == none && !isPlayerChild && ((bioMother != none && MARAS.IsNPCStatus(bioMother, "married")) || (bioFather != none && MARAS.IsNPCStatus(bioFather, "married"))))
        isPlayerChild = true
    endif

    if (isPlayerChild)
        TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
        Actor coSpouse = none
        if (guardianA != none && guardianA != player && MARAS.IsNPCStatus(guardianA, "married"))
            coSpouse = guardianA
        elseif (guardianB != none && guardianB != player && MARAS.IsNPCStatus(guardianB, "married"))
            coSpouse = guardianB
        endif
        childrenLogicQst.AdoptChild(childActor, spouseToAdopWith = coSpouse, bioMother = bioMother, bioFather = bioFather, syntheticID = syntheticID, childStage = stage, isMigrating = isMigrating)
    else
        Actor gA = guardianA
        Actor gB = guardianB
        if (gA == none && gB == none)
            gA = bioMother
        endif
        Location houseLoc = none
        if (gA != none)
            houseLoc = MARAS.GetTenantHouse(gA)
        endif
        MARAS.RegisterChildActor(childActor, gA, gB, houseLoc, bioMother, bioFather, syntheticID, stage, sendEvent = !isMigrating)
        childActor.EvaluatePackage()
        childActor.MoveToPackageLocation()
    endif
EndFunction

;/
  StopDialogueQuests: Beeing Female children are talked to through two dialogue quests. MARAS has its own child
  dialogue, so both are stopped (never reset). A stopped quest stays stopped in the save and Beeing Female never restarts it.
/;
Function StopDialogueQuests() global
    Quest npcChildren = Game.GetFormFromFile(0x023EAD, "BeeingFemale.esm") as Quest
    if (npcChildren && npcChildren.IsRunning())
        npcChildren.Stop()
        TTM_Debug.info("TTM_ServiceFertilityBF: Stopped Beeing Female NPC children dialogue quest")
    endif
    Quest playerChildren = Game.GetFormFromFile(0x01ACB8, "BeeingFemale.esm") as Quest
    if (playerChildren && playerChildren.IsRunning())
        playerChildren.Stop()
        TTM_Debug.info("TTM_ServiceFertilityBF: Stopped Beeing Female player children dialogue quest")
    endif
EndFunction

;/
  OnConception: Fired by Beeing Female NG ("BeeingFemaleConception" ModEvent) when an actor is impregnated.
  One unborn record is registered per fetus, each with its own biological father read from FW.ChildFather.
/;
Function OnConception(Form akMother, int childCount, Form akFather0, Form akFather1, Form akFather2) global
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif

    if (childCount <= 0)
        childCount = StorageUtil.GetIntValue(mother, "FW.NumChilds", 1)
    endif
    if (childCount <= 0)
        childCount = 1
    endif

    if (MARAS.GetUnbornChildCount(mother) > 0)
        TTM_Debug.debug("TTM_ServiceFertilityBF: " + TTM_Utils.GetActorName(mother) + " already has unborn children, skipping conception.")
        return
    endif

    Actor presumedFather = MARAS.GetPrimaryPartner(mother)
    float conceptionTime = Utility.GetCurrentGameTime()
    int fatherListCount = StorageUtil.FormListCount(mother, "FW.ChildFather")

    int i = 0
    while (i < childCount)
        Actor bioFather = None
        if (i < fatherListCount)
            bioFather = StorageUtil.FormListGet(mother, "FW.ChildFather", i) as Actor
        endif
        if (!bioFather)
            if (i == 0)
                bioFather = akFather0 as Actor
            elseif (i == 1)
                bioFather = akFather1 as Actor
            elseif (i == 2)
                bioFather = akFather2 as Actor
            endif
        endif
        if (!bioFather && fatherListCount > 0)
            bioFather = StorageUtil.FormListGet(mother, "FW.ChildFather", 0) as Actor
        endif

        ; The bio-father step-in is evaluated once, after the last fetus is registered, so every father is a candidate
        int unbornID = MARAS.RegisterUnbornChild(mother, bioFather, conceptionTime, (i == childCount - 1))
        TTM_Debug.debug("TTM_ServiceFertilityBF: " + TTM_Utils.GetActorName(mother) + " conceived unborn child #" + (i + 1) + " (ID " + unbornID + ", total " + childCount + ") bioFather=" + TTM_Utils.GetActorName(bioFather) + ", presumedFather=" + TTM_Utils.GetActorName(presumedFather))
        i += 1
    endWhile

    Actor player = TTM_Data.GetPlayer()
    Actor primaryBioFather = none
    if (fatherListCount > 0)
        primaryBioFather = StorageUtil.FormListGet(mother, "FW.ChildFather", 0) as Actor
    endif
    if (!primaryBioFather)
        primaryBioFather = akFather0 as Actor
    endif
    if (mother == player)
        if (presumedFather)
            TTM_ServiceAffection.ApplyPregnancyConceivedAffection(presumedFather, true)
        elseif (primaryBioFather)
            TTM_ServiceAffection.ApplyPregnancyConceivedAffection(primaryBioFather, true)
        endif
    else
        if (presumedFather == player)
            TTM_ServiceAffection.ApplyPregnancyConceivedAffection(mother, false)
        elseif (presumedFather == none && primaryBioFather == player)
            TTM_ServiceAffection.ApplyPregnancyConceivedAffection(mother, false)
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        string bioFatherName = ""
        if (primaryBioFather != none)
            bioFatherName = TTM_Utils.GetActorName(primaryBioFather)
        endif
        TTM_ServiceSkyrimNet.RegisterPregnancyAnnounced(mother, presumedFather, primaryBioFather, bioFatherName, childCount)
    endif
EndFunction

;/
  OnLabor: Fired by Beeing Female NG ("BeeingFemaleLabor" ModEvent) when labor begins.
/;
Function OnLabor(Form akMother, int childCount, Form akFather0, Form akFather1, Form akFather2) global
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif

    ; Beeing Female may fire this event again for each child of the same delivery. A labor session that was started
    ; recently and is not finalized yet is the same labor: keep its batch and announce nothing new.
    float now = Utility.GetCurrentGameTime()
    int sessionTotal = StorageUtil.GetIntValue(mother, "MARAS.BF.LaborTotal", 0)
    float sessionStart = StorageUtil.GetFloatValue(mother, "MARAS.BF.LaborStart", 0.0)
    if (sessionTotal > 0 && (now - sessionStart) < 0.5)
        TTM_Debug.info("TTM_ServiceFertilityBF: Mother " + TTM_Utils.GetActorName(mother) + " labor event continues ongoing labor (" + sessionTotal + " children)")
        return
    endif

    StorageUtil.IntListClear(mother, "MARAS.BF.BirthBatch")
    int total = MARAS.GetUnbornChildCount(mother)
    if (total < childCount)
        total = childCount
    endif
    if (total < 1)
        total = 1
    endif
    StorageUtil.SetIntValue(mother, "MARAS.BF.LaborTotal", total)
    StorageUtil.SetFloatValue(mother, "MARAS.BF.LaborStart", now)

    Actor presumedFather = MARAS.GetPrimaryPartner(mother)
    if (presumedFather == none)
        presumedFather = akFather0 as Actor
    endif

    TTM_Debug.info("TTM_ServiceFertilityBF: Mother " + TTM_Utils.GetActorName(mother) + " entered labor (count=" + total + ")")
    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterLaborStarted(mother, presumedFather, total)
    endif
EndFunction

;/
  OnBirth: Fired by Beeing Female NG ("BeeingFemaleBirth" ModEvent) once per child that reached the world.
  Beeing Female delivers in reverse fetus order, so the unborn child is matched by its biological father.
  If Baby NTR replaced the father mid-pregnancy (no event is emitted for that), the first unborn record is
  corrected to the real father first. A physical baby actor is adopted by OnChildSpawned, which Beeing Female
  fires right after this event; a baby item stays an Infant until it hatches.
/;
Function OnBirth(Form akMother, Form akFather, Form akBaby, string asBabyName, int aiBabySex) global
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor bioFather = akFather as Actor
    float birthTime = Utility.GetCurrentGameTime()
    string childName = asBabyName
    if (childName == "")
        childName = "Newborn"
    endif
    int babySex = aiBabySex
    if (babySex != 0 && babySex != 1)
        babySex = 2 ; unknown
    endif

    int childID = 0
    bool fromUnborn = false
    int[] unbornIDs = MARAS.GetUnbornChildren(mother)
    if (unbornIDs.Length > 0)
        bool fatherMatched = false
        int i = 0
        while (i < unbornIDs.Length && !fatherMatched)
            if (MARAS.GetBioFather(unbornIDs[i]) == bioFather)
                fatherMatched = true
            endif
            i += 1
        endWhile
        if (!fatherMatched && bioFather != none)
            MARAS.SetBioFather(unbornIDs[0], bioFather)
        endif
        childID = MARAS.PromoteUnbornToInfant(mother, birthTime, childName, bioFather)
        fromUnborn = (childID != 0)
    endif
    if (childID == 0)
        Actor presumedFatherGuess = MARAS.GetPrimaryPartner(mother)
        childID = MARAS.RegisterFertilityChild(0, mother, presumedFatherGuess, bioFather, 0.0, birthTime, childName)
    endif
    if (childID == 0)
        return
    endif
    MARAS.SetChildGender(childID, babySex)

    if (bioFather == none)
        bioFather = MARAS.GetBioFather(childID)
    endif
    TTM_Debug.info("TTM_ServiceFertilityBF: Born newborn child (ID " + childID + ", name='" + childName + "', sex=" + babySex + ") for mother " + TTM_Utils.GetActorName(mother) + " with bioFather=" + TTM_Utils.GetActorName(bioFather))

    StorageUtil.IntListAdd(mother, "MARAS.BF.BirthBatch", childID)

    ; Beeing Female reports every fetus of a delivery (a birth, or a stillbirth event), so the batch is complete once
    ; no unborn record is left. Without any known unborn record each birth is announced on its own.
    if (!fromUnborn || MARAS.GetUnbornChildCount(mother) == 0)
        FinalizeBirthBatch(mother)
    elseif (TTM_Data.GetHasSkyrimNet())
        int bornSoFar = StorageUtil.IntListCount(mother, "MARAS.BF.BirthBatch")
        int laborTotal = StorageUtil.GetIntValue(mother, "MARAS.BF.LaborTotal", 0)
        int remaining = MARAS.GetUnbornChildCount(mother)
        if (laborTotal < bornSoFar + remaining)
            laborTotal = bornSoFar + remaining
        endif
        TTM_ServiceSkyrimNet.RegisterLaborProgress(mother, bornSoFar, laborTotal)
    endif
EndFunction

;/
  FinalizeBirthBatch: Called once every fetus of a delivery has been born (or lost). Applies affection and
  announces the whole batch (twins/triplets, possibly from different fathers) as a single birth.
/;
Function FinalizeBirthBatch(Actor mother) global
    int[] ids = StorageUtil.IntListToArray(mother, "MARAS.BF.BirthBatch")
    StorageUtil.IntListClear(mother, "MARAS.BF.BirthBatch")
    StorageUtil.UnsetIntValue(mother, "MARAS.BF.LaborTotal")
    StorageUtil.UnsetFloatValue(mother, "MARAS.BF.LaborStart")
    int count = ids.Length
    if (count == 0)
        return
    endif

    Actor bioFather = MARAS.GetBioFather(ids[0])
    Actor presumedFather = MARAS.GetMotherPartner(ids[0])
    Actor player = TTM_Data.GetPlayer()
    if (mother == player)
        if (presumedFather)
            TTM_ServiceAffection.ApplyChildBirthAffection(presumedFather, count)
        elseif (bioFather)
            TTM_ServiceAffection.ApplyChildBirthAffection(bioFather, count)
        endif
    else
        if (presumedFather == player)
            TTM_ServiceAffection.ApplyChildBirthAffection(mother, count)
        elseif (presumedFather == none && bioFather == player)
            TTM_ServiceAffection.ApplyChildBirthAffection(mother, count)
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        if (count > 1)
            TTM_ServiceSkyrimNet.RegisterChildBirthBatch(mother, ids)
        else
            TTM_ServiceSkyrimNet.RegisterChildBirth(ids[0], MARAS.GetChildName(ids[0]))
        endif
    endif
EndFunction

;/
  OnChildSpawned: Fired by Beeing Female NG ("BeeingFemaleChildSpawned" ModEvent) once a child is physically in the world:
  a carried baby that finished growing, or a baby born directly as an actor (fired right after BeeingFemaleBirth).
  The Infant becomes a Stage 2 child bound to that actor.
/;
Function OnChildSpawned(Form akMother, Form akFather, Form akChild, string asChildName) global
    Actor childActor = akChild as Actor
    if (!childActor)
        return
    endif
    Actor mother = akMother as Actor
    string childName = asChildName
    if (childName == "")
        childName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
    endif
    if (childName == "")
        childName = childActor.GetDisplayName()
    endif

    if (MARAS.GetChildStage(childActor.GetFormID()) != 0)
        return ; already registered
    endif

    ; A baby born straight as an actor fires this event right after BeeingFemaleBirth: let the birth handler create the
    ; Infant and announce the batch before the record is re-keyed to the child actor.
    Utility.Wait(1.0)
    int childID = FindInfant(mother, childName)
    if (childID == 0)
        Utility.Wait(2.0)
        childID = FindInfant(mother, childName)
    endif

    Actor bioMother = mother
    Actor bioFather = akFather as Actor
    if (childID != 0)
        Actor recordedMother = MARAS.GetBioMother(childID)
        Actor recordedFather = MARAS.GetBioFather(childID)
        if (recordedMother != none)
            bioMother = recordedMother
        endif
        if (recordedFather != none)
            bioFather = recordedFather
        endif
    endif

    RegisterBeeingFemaleChild(childActor, bioMother, bioFather, childID, 2)
    TTM_Debug.info("TTM_ServiceFertilityBF: Child actor " + childName + " is in the world (FormID " + childActor.GetFormID() + ")")
    int registeredID = childActor.GetFormID()
    TTM_ServiceAffection.ApplyChildMilestoneForParents(registeredID, "child")
    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterChildGrewUp(registeredID, childName)
    endif
EndFunction

;/
  FindInfant: Finds the synthetic Infant record created for a newborn of this mother by name.
/;
int Function FindInfant(Actor mother, string childName) global
    int childID = 0
    if (mother != none)
        childID = MARAS.FindSyntheticChild(mother, childName)
    endif
    if (childID == 0)
        childID = MARAS.FindSyntheticChild(none, childName)
    endif
    if (childID != 0 && MARAS.GetChildStage(childID) != 1)
        childID = 0
    endif
    return childID
EndFunction

;/
  OnAdultChildSpawned: Fired by Beeing Female NG ("BeeingFemaleAdultChildSpawned" ModEvent) when a child reaches adulthood.
  akChild is the child actor that was swapped out (it is the same actor as akAdult when the child graduated in place).
  It is already queued for deletion, so it is only used to find the record and is never stored.
  The child record moves to the adult actor at Stage 4 (Adult).
/;
Function OnAdultChildSpawned(Form akMother, Form akFather, Form akAdult, Form akChild, string asChildName) global
    Actor adultActor = akAdult as Actor
    if (!adultActor)
        return
    endif
    Actor mother = akMother as Actor
    string childName = asChildName
    if (childName == "")
        childName = adultActor.GetDisplayName()
    endif

    int previousID = 0
    if (akChild != none && MARAS.GetChildStage(akChild.GetFormID()) != 0)
        previousID = akChild.GetFormID()
    endif
    if (previousID == 0 && MARAS.GetChildStage(adultActor.GetFormID()) != 0)
        previousID = adultActor.GetFormID()
    endif
    if (previousID == 0)
        previousID = MARAS.FindSyntheticChild(mother, childName)
    endif
    if (previousID == 0)
        previousID = MARAS.FindSyntheticChild(none, childName)
    endif
    if (previousID == 0)
        previousID = adultActor.GetFormID()
    endif

    if (previousID == adultActor.GetFormID() && MARAS.GetChildStage(previousID) == 4)
        return
    endif
    Actor bioMother = MARAS.GetBioMother(previousID)
    Actor bioFather = MARAS.GetBioFather(previousID)
    if (bioMother == none)
        bioMother = mother
    endif
    if (bioFather == none)
        bioFather = akFather as Actor
    endif
    MARAS.RegisterChildActor(adultActor, none, none, none, bioMother, bioFather, previousID, 4)
    TTM_Debug.info("TTM_ServiceFertilityBF: Child " + childName + " promoted to adult (FormID " + adultActor.GetFormID() + ")")
    TTM_ServiceAffection.ApplyChildMilestoneForParents(adultActor.GetFormID(), "adult")
    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterChildBecameAdult(adultActor.GetFormID(), childName)
    endif
EndFunction

;/
  PromoteAdultFollowerChild: Rebinds a child record to a spawned adult follower actor.
/;
Function PromoteAdultFollowerChild(Actor adultActor, string childName = "") global
    if (adultActor == none)
        return
    endif
    if (childName == "")
        childName = adultActor.GetDisplayName()
    endif
    int syntheticID = MARAS.FindSyntheticChild(none, childName)
    if (syntheticID != 0)
        int currentStage = MARAS.GetChildStage(syntheticID)
        Actor bioMother = MARAS.GetBioMother(syntheticID)
        Actor bioFather = MARAS.GetBioFather(syntheticID)
        TTM_Debug.info("TTM_ServiceFertilityBF: Promoting synthetic child " + syntheticID + " to adult follower actor " + adultActor.GetDisplayName())
        MARAS.RegisterChildActor(adultActor, none, none, none, bioMother, bioFather, syntheticID, 4) ; Stage 4 = Adult
        if (currentStage != 4)
            TTM_ServiceAffection.ApplyChildMilestoneForParents(syntheticID, "adult")
            if (TTM_Data.GetHasSkyrimNet())
                TTM_ServiceSkyrimNet.RegisterChildBecameAdult(adultActor.GetFormID(), childName)
            endif
        endif
    endif
EndFunction

;/
  OnAbort: Fired by Beeing Female NG ("BeeingFemaleAbort" ModEvent) once the pregnancy has actually ended in loss
  (abortion, miscarriage or a missed-abortion stillbirth). The whole pregnancy is lost.
/;
Function OnAbort(Form akMother, Form akFather, int aiAbortusState, string asReason) global
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    RegisterPregnancyLoss(mother, akFather as Actor, asReason, MARAS.RemoveUnbornChild(mother))
EndFunction

;/
  OnStillbirth: Fired by Beeing Female NG ("BeeingFemaleStillbirth" ModEvent) once per child lost to the stillbirth
  roll during delivery. Only the unborn child fathered by akFather is removed; the siblings are still delivered.
/;
Function OnStillbirth(Form akMother, Form akFather) global
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor bioFather = akFather as Actor
    RegisterPregnancyLoss(mother, bioFather, "stillbirth", MARAS.RemoveUnbornChild(mother, bioFather))

    ; The stillborn child can be the last one of the delivery: announce the siblings that were born
    if (MARAS.GetUnbornChildCount(mother) == 0)
        FinalizeBirthBatch(mother)
    endif
EndFunction

;/
  RegisterPregnancyLoss: Affection and SkyrimNet notification for a lost pregnancy or stillborn child.
  Only runs when a record was actually removed, so repeated events for one loss are harmless.
/;
Function RegisterPregnancyLoss(Actor mother, Actor bioFather, string reason, bool removed) global
    if (!removed)
        return
    endif
    TTM_Debug.info("TTM_ServiceFertilityBF: Pregnancy lost for " + TTM_Utils.GetActorName(mother) + " (" + reason + ")")
    Actor presumed = MARAS.GetPrimaryPartner(mother)
    Actor player = TTM_Data.GetPlayer()
    if (mother == player)
        if (presumed)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(presumed, reason)
        elseif (bioFather)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(bioFather, reason)
        endif
    else
        if (presumed == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, reason)
        elseif (presumed == none && bioFather == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, reason)
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        Actor eventPartner = presumed
        if (eventPartner == none)
            eventPartner = bioFather
        endif
        TTM_ServiceSkyrimNet.RegisterPregnancyLost(mother, eventPartner, reason)
    endif
EndFunction

;/
  OnStateChange: Fired by Beeing Female NG ("BeeingFemale" ModEvent, strArg "stateChanged", numArg = the woman's FormID)
  whenever a woman changes cycle state. Beeing Female emits no birth event for a child it does not spawn (baby gem,
  NPC babies disabled), so when a woman reaches the postpartum state (8) with unborn records left, those children were
  born without a baby: they become Infants with a generated name and unknown sex.
/;
Function OnStateChange(string eventName, string strArg, float numArg, Form sender) global
    if (strArg != "stateChanged")
        return
    endif
    Actor mother = Game.GetForm(numArg as int) as Actor
    if (!mother)
        return
    endif
    if (StorageUtil.GetIntValue(mother, "FW.CurrentState", -1) != 8)
        return
    endif
    int leftover = MARAS.GetUnbornChildCount(mother)
    if (leftover == 0)
        return
    endif

    float birthTime = Utility.GetCurrentGameTime()
    while (MARAS.GetUnbornChildCount(mother) > 0)
        string childName = MARAS.GenerateRandomChildNameForActor(mother, true)
        if (childName == "")
            childName = "Newborn"
        endif
        int childID = MARAS.PromoteUnbornToInfant(mother, birthTime, childName)
        if (childID == 0)
            return
        endif
        MARAS.SetChildGender(childID, 2)
        StorageUtil.IntListAdd(mother, "MARAS.BF.BirthBatch", childID)
        TTM_Debug.info("TTM_ServiceFertilityBF: " + TTM_Utils.GetActorName(mother) + " gave birth without a spawned baby (ID " + childID + ", name='" + childName + "')")
    endWhile
    FinalizeBirthBatch(mother)
EndFunction

;/
  IsMigrated: Checks whether Beeing Female storage has already been migrated into MARAS.
/;
bool Function IsMigrated() global
    return TTM_Data.GetBool("MARAS_BF_Migrated")
EndFunction

;/
  MarkMigrated: Flags Beeing Female migration as completed.
/;
Function MarkMigrated() global
    TTM_Data.SetBool("MARAS_BF_Migrated", true)
    TTM_Debug.info("TTM_ServiceFertilityBF: Marked Beeing Female data as migrated.")
EndFunction

;/
  ForceMigration: Clears the migration flag and imports Beeing Female data again.
/;
Function ForceMigration() global
    TTM_Data.SetBool("MARAS_BF_Migrated", false)
    Migrate()
EndFunction

;/
  Migrate: One-time import of whatever Beeing Female already holds when MARAS first runs with it:
  stops the Beeing Female dialogue quests, then imports active pregnancies, carried baby items,
  physical child actors and births that left no baby behind. Every later change arrives through events.
/;
Function Migrate() global
    if (IsMigrated())
        return
    endif
    if (Game.GetModByName("BeeingFemale.esm") == 255)
        TTM_Debug.debug("TTM_ServiceFertilityBF: BeeingFemale.esm not found")
        return
    endif
    TTM_Debug.debug("TTM_ServiceFertilityBF: Starting one-time migration from Beeing Female storage")

    StopDialogueQuests()

    Actor player = TTM_Data.GetPlayer()

    ; Pregnancies, carried baby items and birth history of every tracked mother
    MigrateMother(player)
    int nSaved = StorageUtil.FormListCount(none, "FW.SavedNPCs")
    int sIdx = 0
    while (sIdx < nSaved)
        Actor m = StorageUtil.FormListGet(none, "FW.SavedNPCs", sIdx) as Actor
        if (m != none && m != player)
            MigrateMother(m)
        endif
        sIdx += 1
    endWhile

    ; Physical child (and grown adult) actors
    int nBabys = StorageUtil.FormListCount(none, "FW.Babys")
    int bIdx = 0
    while (bIdx < nBabys)
        Actor childActor = StorageUtil.FormListGet(none, "FW.Babys", bIdx) as Actor
        if (childActor != none && !childActor.IsDead() && MARAS.GetChildStage(childActor.GetFormID()) == 0)
            MigrateChildActor(childActor)
        endif
        bIdx += 1
    endWhile

    MarkMigrated()
EndFunction

Function MigrateMother(Actor mother) global
    if (!mother)
        return
    endif
    Actor player = TTM_Data.GetPlayer()

    ; 1. Active pregnancy (trimesters 1-3 and labor): one unborn child per fetus with its own father
    int currentState = StorageUtil.GetIntValue(mother, "FW.CurrentState", 0)
    int numChilds = StorageUtil.GetIntValue(mother, "FW.NumChilds", 0)
    if (numChilds > 0 && currentState >= 4 && currentState <= 7)
        int fatherCount = StorageUtil.FormListCount(mother, "FW.ChildFather")
        float conceptionTime = StorageUtil.GetFloatValue(mother, "FW.LastConception", 0.0)
        if (conceptionTime <= 0.0)
            conceptionTime = Utility.GetCurrentGameTime()
        endif
        int i = MARAS.GetUnbornChildCount(mother)
        while (i < numChilds)
            Actor bioFather = none
            if (i < fatherCount)
                bioFather = StorageUtil.FormListGet(mother, "FW.ChildFather", i) as Actor
            elseif (fatherCount > 0)
                bioFather = StorageUtil.FormListGet(mother, "FW.ChildFather", 0) as Actor
            endif
            int unbornID = MARAS.RegisterUnbornChild(mother, bioFather, conceptionTime, false)
            TTM_Debug.info("TTM_ServiceFertilityBF: Migrated active pregnancy: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(bioFather) + " (Unborn ID=" + unbornID + ")")
            i += 1
        endWhile
    endif

    ; 2. Carried baby items: each becomes an Infant
    int itemCount = StorageUtil.StringListCount(mother, "FW.BabyItemName")
    int j = 0
    while (j < itemCount)
        string babyName = StorageUtil.StringListGet(mother, "FW.BabyItemName", j)
        int babySex = StorageUtil.IntListGet(mother, "FW.BabyItemSex", j)
        Actor babyFather = StorageUtil.FormListGet(mother, "FW.BabyItemFather", j) as Actor
        float babyDOB = StorageUtil.FloatListGet(mother, "FW.BabyItemDOB", j)
        if (babyName == "")
            babyName = "Newborn"
        endif
        if (MARAS.FindSyntheticChild(mother, babyName) == 0)
            int infantID = MARAS.RegisterFertilityChild(0, mother, none, babyFather, babyDOB, babyDOB, babyName)
            if (infantID != 0)
                MARAS.SetChildGender(infantID, babySex)
                TTM_Debug.info("TTM_ServiceFertilityBF: Migrated carried baby item: mother=" + TTM_Utils.GetActorName(mother) + ", name=" + babyName + " (Infant ID=" + infantID + ")")
            endif
        endif
        j += 1
    endWhile

    ; 3. Births that left no baby item or actor behind. Beeing Female records only a father and a time per birth,
    ; so only NPC-NPC births are reconstructed, and only as many as no item or child actor accounts for.
    int bornCount = StorageUtil.FloatListCount(mother, "FW.BornChildTime")
    if (bornCount == 0)
        return
    endif
    int legacyCount = bornCount
    if (legacyCount > 0)
        int represented = itemCount
        int nBabys = StorageUtil.FormListCount(none, "FW.Babys")
        int b = 0
        while (b < nBabys)
            Actor cand = StorageUtil.FormListGet(none, "FW.Babys", b) as Actor
            if (cand != none && (StorageUtil.GetFormValue(cand, "FW.Child.Mother", none) as Actor) == mother)
                represented += 1
            endif
            b += 1
        endWhile
        int missing = legacyCount - represented
        int first = legacyCount - missing
        int m = first
        while (missing > 0 && m < legacyCount)
            Actor legacyFather = StorageUtil.FormListGet(mother, "FW.BornChildFather", m) as Actor
            if (mother != player && legacyFather != player)
                float legacyTime = StorageUtil.FloatListGet(mother, "FW.BornChildTime", m)
                string legacyName = MARAS.GenerateRandomChildNameForActor(mother, true)
                if (legacyName == "")
                    legacyName = "Newborn"
                endif
                int legacyID = MARAS.RegisterFertilityChild(0, mother, none, legacyFather, legacyTime, legacyTime, legacyName)
                if (legacyID != 0)
                    TTM_Debug.info("TTM_ServiceFertilityBF: Migrated legacy birth: mother=" + TTM_Utils.GetActorName(mother) + ", name=" + legacyName + " (Infant ID=" + legacyID + ")")
                endif
            endif
            m += 1
        endWhile
    endif
EndFunction

Function MigrateChildActor(Actor childActor) global
    Actor mother = StorageUtil.GetFormValue(childActor, "FW.Child.Mother", none) as Actor
    Actor father = StorageUtil.GetFormValue(childActor, "FW.Child.Father", none) as Actor
    string childName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
    if (childName == "")
        childName = childActor.GetDisplayName()
    endif

    int syntheticID = 0
    if (mother != none)
        syntheticID = MARAS.FindSyntheticChild(mother, childName)
    endif
    if (syntheticID == 0)
        syntheticID = MARAS.FindSyntheticChild(none, childName)
    endif

    if (StorageUtil.GetIntValue(childActor, "FW.Child.GrownUp", 0) == 1)
        MARAS.RegisterChildActor(childActor, none, none, none, mother, father, syntheticID, 4, sendEvent = false) ; Stage 4 = Adult
    else
        RegisterBeeingFemaleChild(childActor, mother, father, syntheticID, 2, isMigrating = true)
    endif
    TTM_Debug.info("TTM_ServiceFertilityBF: Migrated child actor " + childName + " (FormID " + childActor.GetFormID() + ")")
EndFunction
