;/
  TTM_ServiceFertilityFMR.psc

  Integrates Fertility Mode Reloaded (FMR) with M.A.R.A.S.
  Tracks pregnancy state, newborn births, parentage, and paternity intrigue.
/;
Scriptname TTM_ServiceFertilityFMR

;/
  Maintenance: Called by TTM_ServiceFertility during startup and save loading.
  Soft-gates against Fertility Mode.esm.
/;
Function Maintenance() global
    if (Game.GetModByName("Fertility Mode.esm") == 255)
        TTM_Debug.info("TTM_ServiceFertilityFMR: Fertility Mode.esm not detected, FMR listener disabled.")
        return
    endif

    TTM_Debug.info("TTM_ServiceFertilityFMR: Fertility Mode.esm detected, registering listeners.")
EndFunction

;/
  OnConception: Fired by FMR via "FMR_Conception" ModEvent when conception succeeds.
  Provides direct biological father Form.
/;
Function OnConception(Form akMother, Form akFather, int iTrackingIndex) global
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Conception event received for mother " + akMother + ") bioFather=" + akFather)
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        TTM_Debug.debug("TTM_ServiceFertilityFMR: Not active fertility provider, skipping.")
        return
    endif
    Actor mother = akMother as Actor
    if (!mother)
        TTM_Debug.debug("TTM_ServiceFertilityFMR: Mother is not an Actor, skipping.")
        return
    endif
    Actor bioFather = akFather as Actor
    Actor presumedFather = MARAS.GetPrimaryPartner(mother)
    float conceptionTime = Utility.GetCurrentGameTime()

    int existingInfantID = MARAS.FindSyntheticChild(mother, "Newborn")
    int existingUnbornID = MARAS.FindUnbornChild(mother)
    if (!MARAS.IsNPCPregnant(mother) && existingInfantID == 0 && existingUnbornID == 0)
        int unbornID = MARAS.RegisterUnbornChild(mother, bioFather, conceptionTime)
        string bioFatherName = "Unknown"
        if (bioFather != none)
            bioFatherName = TTM_Utils.GetActorName(bioFather)
        endif
        TTM_Debug.info("TTM_ServiceFertilityFMR: " + TTM_Utils.GetActorName(mother) + " conceived unborn child (ID " + unbornID + ", index " + iTrackingIndex + ") bioFather=" + bioFatherName + ", presumedFather=" + TTM_Utils.GetActorName(presumedFather))

        Actor player = TTM_Data.GetPlayer()
        if (mother == player)
            if (presumedFather)
                TTM_ServiceAffection.ApplyPregnancyConceivedAffection(presumedFather, true)
            elseif (bioFather)
                TTM_ServiceAffection.ApplyPregnancyConceivedAffection(bioFather, true)
            endif
        else
            if (presumedFather == player)
                TTM_ServiceAffection.ApplyPregnancyConceivedAffection(mother, false)
            elseif(presumedFather == none && bioFather == player)
                TTM_ServiceAffection.ApplyPregnancyConceivedAffection(mother, false)
            endif
        endif

        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterPregnancyAnnounced(mother, presumedFather, bioFather, bioFatherName)
        endif
    else
        TTM_Debug.debug("TTM_ServiceFertilityFMR: " + TTM_Utils.GetActorName(mother) + " already pregnant or has an unborn child, skipping conception.")
    endif
EndFunction

Function OnBabyBirth(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor bioFather = akFather as Actor
    float birthTime = Utility.GetCurrentGameTime()
    string childName = "Newborn"

    int unbornID = MARAS.FindUnbornChild(mother)
    int childID = 0
    if (unbornID != 0)
        childID = MARAS.PromoteUnbornToInfant(mother, birthTime, childName, bioFather)
    else
        int existingInfantID = MARAS.FindSyntheticChild(mother, "Newborn")
        if (existingInfantID != 0 && MARAS.GetChildStage(existingInfantID) == 1)
            TTM_Debug.warn("TTM_ServiceFertilityFMR: Mother " + TTM_Utils.GetActorName(mother) + " already has an active infant (ID " + existingInfantID + "). Skipping duplicate birth registration.")
            return
        endif

        Actor presumedFather = MARAS.GetPrimaryPartner(mother)
        childID = MARAS.RegisterFertilityChild(0, mother, presumedFather, bioFather, 0.0, birthTime, childName)
    endif

    if (childID == 0)
        return
    endif

    if (bioFather == none)
        bioFather = MARAS.GetBioFather(childID)
    endif
    Actor presumedFather = MARAS.GetMotherPartner(childID)

    TTM_Debug.info("TTM_ServiceFertilityFMR: Promoted/registered newborn child (ID " + childID + ") for mother " + TTM_Utils.GetActorName(mother) + " with bioFather=" + TTM_Utils.GetActorName(bioFather) + ", presumedFather=" + TTM_Utils.GetActorName(presumedFather))

    Actor player = TTM_Data.GetPlayer()
    if (mother == player)
        if (presumedFather)
            TTM_ServiceAffection.ApplyChildBirthAffection(presumedFather, 1)
        elseif (bioFather)
            TTM_ServiceAffection.ApplyChildBirthAffection(bioFather, 1)
        endif
    else
        if (presumedFather == player)
            TTM_ServiceAffection.ApplyChildBirthAffection(mother, 1)
        elseif (presumedFather == none && bioFather == player)
            TTM_ServiceAffection.ApplyChildBirthAffection(mother, 1)
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterChildBirth(childID, childName)
    endif
EndFunction

Function OnAbort(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor bioFather = akFather as Actor
    Actor presumedFather = MARAS.GetPrimaryPartner(mother)

    if (MARAS.IsNPCPregnant(mother) || MARAS.FindUnbornChild(mother) != 0)
        MARAS.RemoveUnbornChild(mother)
    endif
    TTM_Debug.info("TTM_ServiceFertilityFMR: " + TTM_Utils.GetActorName(mother) + " pregnancy aborted (FMR_Abort)")

    Actor player = TTM_Data.GetPlayer()
    if (mother == player)
        if (presumedFather)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(presumedFather, "abortion")
        elseif (bioFather)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(bioFather, "abortion")
        endif
    else
        if (presumedFather == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, "abortion")
        elseif (presumedFather == none && bioFather == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, "abortion")
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        Actor eventPartner = presumedFather
        if (eventPartner == none)
            eventPartner = bioFather
        endif
        TTM_ServiceSkyrimNet.RegisterPregnancyLost(mother, eventPartner, "abortion")
    endif
EndFunction

Function OnPregnancyCancelled(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor bioFather = akFather as Actor
    Actor presumedFather = MARAS.GetPrimaryPartner(mother)

    if (MARAS.IsNPCPregnant(mother) || MARAS.FindUnbornChild(mother) != 0)
        MARAS.RemoveUnbornChild(mother)
        TTM_Debug.info("TTM_ServiceFertilityFMR: " + TTM_Utils.GetActorName(mother) + " pregnancy cancelled (FMR_PregnancyCancelled)")
    endif

    Actor player = TTM_Data.GetPlayer()
    if (mother == player)
        if (presumedFather)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(presumedFather, "cancelled")
        elseif (bioFather)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(bioFather, "cancelled")
        endif
    else
        if (presumedFather == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, "cancelled")
        elseif (presumedFather == none && bioFather == player)
            TTM_ServiceAffection.ApplyPregnancyLostAffection(mother, "cancelled")
        endif
    endif

    if (TTM_Data.GetHasSkyrimNet())
        Actor eventPartner = presumedFather
        if (eventPartner == none)
            eventPartner = bioFather
        endif
        TTM_ServiceSkyrimNet.RegisterPregnancyLost(mother, eventPartner, "cancelled")
    endif
EndFunction

Function OnChildGrewUp(int childIndex, string childName, int gender, Actor mother) global
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Child grew up (FMR_ChildGrewUp)")
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    int childID = 0
    if (mother != none)
        childID = MARAS.FindSyntheticChild(mother, "Newborn")
        if (childID == 0)
            TTM_Debug.warn("TTM_ServiceFertilityFMR: " + mother + " child " + childName + " not found.")
        endif
    else
        TTM_Debug.warn("TTM_ServiceFertilityFMR: No mother provided.")
    endif
    if (childID != 0)
        GrowUpChild(childID, childName, gender)
    endif
EndFunction

Function OnChildGrewUpNpcNpc(Actor mother, Actor father) global
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Child grew up (FMR_ChildGrewUpNpcNpc)")
    int childID = MARAS.FindSyntheticChild(mother, "Newborn")
    if (childID != 0)
        int gender = Utility.RandomInt(0, 1)
        string childName = TTM_Utils.GetChildNameByParents(mother, father, gender)
        GrowUpChild(childID, childName, gender)
    endif
EndFunction

Function GrowUpChild(int childID, string childName, int gender) global
    MARAS.SetChildGender(childID, gender)
    MARAS.UpdateChildName(childID, childName)
    MARAS.SetChildStage(childID, 2) ; Stage 2 = Child (Explicit transition to Stage 2)
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Child grew up from Newborn to '" + childName + "' (ID " + childID + ", gender=" + gender + ")")
    TTM_ServiceAffection.ApplyChildMilestoneForParents(childID, "child")
    if (TTM_Data.GetHasSkyrimNet())
        TTM_ServiceSkyrimNet.RegisterChildGrewUp(childID, childName)
    endif
EndFunction

Function OnChildSpawned(Form childActor, int childIndex, string childName) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor child = childActor as Actor
    if (!child)
        return
    endif
    int childID = MARAS.FindSyntheticChild(none, childName)
    if (childID == 0)
        childID = MARAS.FindSyntheticChild(none, child.GetDisplayName())
    endif
    if (childID != 0)
        Actor bioMother = MARAS.GetBioMother(childID)
        Actor bioFather = MARAS.GetBioFather(childID)
        Actor player = TTM_Data.GetPlayer()
        Actor guardianA = MARAS.GetGuardianA(childID)
        Actor guardianB = MARAS.GetGuardianB(childID)
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
            childrenLogicQst.AdoptChild(child, spouseToAdopWith = coSpouse, bioMother = bioMother, bioFather = bioFather, syntheticID = childID, childStage = 2)
        else
            Actor npcMother = bioMother
            Actor npcFather = MARAS.GetMotherPartner(childID)
            if (npcFather == none)
                npcFather = bioFather
            endif
            Location houseLoc = none
            if (npcMother != none)
                houseLoc = MARAS.GetTenantHouse(npcMother)
            endif
            if (guardianA != none || guardianB != none)
                npcMother = guardianA
                npcFather = guardianB
            endif
            MARAS.RegisterChildActor(child, npcMother, npcFather, houseLoc, bioMother, bioFather, childID, 2)
            child.EvaluatePackage()
            child.MoveToPackageLocation()
        endif
        TTM_Debug.info("TTM_ServiceFertilityFMR: Spawned physical child actor " + child.GetDisplayName() + " re-keyed from synthetic ID " + childID)
    endif
EndFunction

Function OnChildSentToTraining(int childIndex, string childName, string trainingLocation, string className) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    int childID = MARAS.FindSyntheticChild(none, childName)
    if (childID != 0)
        MARAS.SetChildTrainingLocation(childID, trainingLocation)
        int currentStage = MARAS.GetChildStage(childID)
        if (currentStage != 3)
            MARAS.SetChildStage(childID, 3)
        endif
        TTM_Debug.info("TTM_ServiceFertilityFMR: Child " + childName + " (ID " + childID + ") sent to training at " + trainingLocation + " (" + className + ")")
        if (TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterChildSentToTraining(childID, childName, trainingLocation)
        endif
    endif
EndFunction

Function OnChildFinishedTraining(int childIndex, string childName, string trainingLocation, string className) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    int childID = MARAS.FindSyntheticChild(none, childName)
    if (childID != 0)
        int currentStage = MARAS.GetChildStage(childID)
        if (currentStage != 4)
            MARAS.SetChildStage(childID, 4)
            TTM_Debug.info("TTM_ServiceFertilityFMR: Child " + childName + " (ID " + childID + ") finished training, became adult")
            TTM_ServiceAffection.ApplyChildMilestoneForParents(childID, "adult")
            if (TTM_Data.GetHasSkyrimNet())
                TTM_ServiceSkyrimNet.RegisterChildBecameAdult(childID, childName)
            endif
        endif
    endif
EndFunction

Function OnAdultChildSpawned(Form adultActor, int childIndex, string childName) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor adult = adultActor as Actor
    if (adult)
        PromoteAdultFollowerChild(adult, childName)
    endif
EndFunction


Function PromoteAdultFollowerChild(Actor adultActor, string childName = "", bool fireEvent = true) global
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
        TTM_Debug.info("TTM_ServiceFertilityFMR: Promoting synthetic child " + syntheticID + " to adult follower actor " + adultActor.GetDisplayName())
        MARAS.RegisterChildActor(adultActor, none, none, none, bioMother, bioFather, syntheticID, 4) ; Stage 4 = Adult
        if (fireEvent && currentStage != 4 && TTM_Data.GetHasSkyrimNet())
            TTM_ServiceSkyrimNet.RegisterChildBecameAdult(adultActor.GetFormID(), childName)
        endif
    endif
EndFunction

;/
  IsMigrated: Checks whether FMR storage has already been migrated into MARAS.
/;
bool Function IsMigrated() global
    return TTM_Data.GetBool("MARAS_FMR_Migrated")
EndFunction

;/
  MarkMigrated: Flags FMR migration as completed.
/;
Function MarkMigrated() global
    TTM_Data.SetBool("MARAS_FMR_Migrated", true)
    TTM_Debug.info("TTM_ServiceFertilityFMR: Marked FMR data as migrated.")
EndFunction

;/
  ForceMigration: Clears migration flag and forces a full re-migration from FMR storage.
/;
Function ForceMigration() global
    TTM_Data.SetBool("MARAS_FMR_Migrated", false)
    Migrate()
EndFunction

;/
  Migrate: One-time migration of existing FMR storage records into MARAS.
  All parentage (mothers and fathers) is pulled directly from FMR storage.
  Syncs children, active pregnancies, newborn items, and past NPC-NPC births.
/;
Function Migrate() global
    if (IsMigrated())
        return
    endif
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Starting one-time migration from FMR storage")
    if (Game.GetModByName("Fertility Mode.esm") == 255)
        TTM_Debug.debug("TTM_ServiceFertilityFMR: Fertility Mode.esm not found")
        return
    endif
    Quest storageQuest = Game.GetFormFromFile(0xd62, "Fertility Mode.esm") as Quest
    _JSW_BB_Storage fmrStorage = storageQuest as _JSW_BB_Storage
    if (!fmrStorage)
        TTM_Debug.warn("TTM_ServiceFertilityFMR: FMR storage quest not found")
        return
    endif

    _JSW_BB_Utility fmrUtility = storageQuest as _JSW_BB_Utility

    Actor player = TTM_Data.GetPlayer()

    TTM_Debug.debug("TTM_ServiceFertilityFMR: Migrating tracked actors from FMR storage: " + fmrStorage.TrackedActors + ":Length:" + fmrStorage.TrackedActors.Length)
    ; 2. Migrate from FMR TrackedActors (NPC-NPC and NPC-Player pregnancies, infants, and past births)
    int tIdx = 0

    StorageUtil.IntListClear(none, "MARAS_FMR_Migration_PlayerChildrenMothers")
    StorageUtil.FloatListClear(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime")

    while (tIdx < fmrStorage.TrackedActors.Length)
        Actor mother = fmrStorage.TrackedActors[tIdx] as Actor
        TTM_Debug.debug("TTM_ServiceFertilityFMR: Migrating tracked actor record " + tIdx + ": " + TTM_Utils.GetActorName(mother))
        if (mother != none)
            float conceptionTime = fmrStorage.LastConception[tIdx]
            float birthTime = fmrStorage.LastBirth[tIdx]
            float babyAddedTime = fmrStorage.BabyAdded[tIdx]

            if (conceptionTime > 0.0)
                ; Active pregnancy in FMR (NPC-NPC or NPC-Player)
                Actor father = fmrStorage.CurrentFatherRef[tIdx] as Actor
                int unbornID = MARAS.RegisterUnbornChild(mother, father, conceptionTime)
                TTM_Debug.info("TTM_ServiceFertilityFMR: Migrated active FMR pregnancy: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(father) + " (Unborn ID=" + unbornID + ")")
            endif

            if (babyAddedTime > 0.0)
                ; Carried newborn baby item in FMR (NPC-NPC or NPC-Player)
                Actor father = fmrStorage.LastFatherRef[tIdx] as Actor
                int infantID = MARAS.RegisterFertilityChild(0, mother, none, father, 0.0, birthTime, "Newborn")
                TTM_Debug.info("TTM_ServiceFertilityFMR: Migrated carried baby item: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(father) + " (Infant ID=" + infantID + ")")
            endif

            if (birthTime > 0.0 && mother != player)
                ; Completed past birth for an NPC in FMR whose baby item expired (NPC-NPC child)
                Actor father = fmrStorage.LastFatherRef[tIdx] as Actor
                TTM_Debug.debug("TTM_ServiceFertilityFMR: Migrating NPC-NPC past birth: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(father) + " (Birth Time=" + birthTime + ")")
                if(father == player || MARAS.IsNPCStatus(mother, "married"))
                    float bTime = fmrStorage.LastBirth[tIdx]
                    int count = StorageUtil.FloatListCount(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime")
                    ; Find index to insert in ascending order (oldest to newest)
                    int insertIdx = 0
                    while (insertIdx < count && StorageUtil.FloatListGet(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime", insertIdx) < bTime)
                        insertIdx += 1
                    endWhile
                    TTM_Debug.debug("TTM_ServiceFertilityFMR: Inserting player children mother at index " + insertIdx + ": " + TTM_Utils.GetActorName(mother))
                    if(insertIdx >= count)
                        StorageUtil.IntListAdd(none, "MARAS_FMR_Migration_PlayerChildrenMothers", tIdx)
                        StorageUtil.FloatListAdd(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime", bTime)
                    else
                        StorageUtil.IntListInsert(none, "MARAS_FMR_Migration_PlayerChildrenMothers", insertIdx, tIdx)
                        StorageUtil.FloatListInsert(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime", insertIdx, bTime)
                    endif
                else
                    string npcChildName = TTM_Utils.GetActorName(mother) + "'s Child"
                    int npcChildID = MARAS.RegisterFertilityChild(0, mother, none, father, 0.0, birthTime, npcChildName)
                    if (npcChildID != 0)
                        int gender = Utility.RandomInt(0, 1)
                        Race raceForName = mother.GetRace()
                        if(Utility.RandomInt(0,1) == 0 && father)
                            raceForName = father.GetRace()
                        endif
                        string childName = TTM_Utils.GetChildNameByParents(mother, father, gender)
                        MARAS.SetChildStage(npcChildID, 2) ; Stage 2 = Child
                        MARAS.SetChildGender(npcChildID, gender)
                        MARAS.UpdateChildName(npcChildID, childName)
                        TTM_Debug.info("TTM_ServiceFertilityFMR: Migrated completed NPC-NPC birth: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(father) + " (Child ID=" + npcChildID + ")")
                    endif
                endif
            endif
        endif
        tIdx += 1
    endWhile

    ; try to resolve player's children to mothers
    int motherIdx = StorageUtil.IntListCount(none, "MARAS_FMR_Migration_PlayerChildrenMothers") - 1
    int childIdx = fmrStorage.PlayerChildName.Length - 1
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Player child mother index: " + motherIdx + "; child index: " + childIdx)
    bool lastFatherUsed = false
    if(motherIdx >= 0 && childIdx >= 0)
        while (motherIdx >= 0 && childIdx >= 0)
            int nIdx = StorageUtil.IntListGet(none, "MARAS_FMR_Migration_PlayerChildrenMothers", motherIdx)
            Actor bioFather = fmrStorage.LastFatherRef[nIdx] as Actor
            if(lastFatherUsed)
                bioFather = player
            endif
            lastFatherUsed = true
            RegisterPlayerChild(nIdx, motherIdx, childIdx, bioFather, fmrStorage)
            motherIdx -= 1
            childIdx -= 1
        endWhile
        while(childIdx >= 0)
            int nIdx = StorageUtil.IntListGet(none, "MARAS_FMR_Migration_PlayerChildrenMothers", 0)
            RegisterPlayerChild(nIdx, 0, childIdx, player, fmrStorage)
            childIdx -= 1
        endWhile
    endif
    StorageUtil.IntListClear(none, "MARAS_FMR_Migration_PlayerChildrenMothers")
    StorageUtil.FloatListClear(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime")

    MarkMigrated()
EndFunction

Function RegisterPlayerChild(int nIdx, int motherIdx, int childIdx, Actor bioFather, _JSW_BB_Storage fmrStorage) global
    Actor mother = fmrStorage.TrackedActors[nIdx] as Actor
    string childName = fmrStorage.PlayerChildName[childIdx]
    Actor player = TTM_Data.GetPlayer()
    TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic

    TTM_Debug.debug("TTM_ServiceFertilityFMR: Resolving child record " + childIdx + ": " + childName + ": mother: " + TTM_Utils.GetActorName(mother))
    if (childName != "")
        int status = fmrStorage.PlayerChildTrainingStatus[childIdx]
        string trainLocation = fmrStorage.PlayerChildTrainingLocation[childIdx]
        int actorIdx = fmrStorage.PlayerChildActorIndex[childIdx]
        int childGender = 0
        if (childIdx < fmrStorage.PlayerChildGender.Length)
            childGender = fmrStorage.PlayerChildGender[childIdx]
        endif
        TTM_Debug.debug("TTM_ServiceFertilityFMR: Resolving child record " + childIdx + ": " + childName + ": mother: " + TTM_Utils.GetActorName(mother) + ": father: " + TTM_Utils.GetActorName(player) + ": gender: " + childGender)
        int childID = MARAS.RegisterFertilityChild(0, mother, none, bioFather, 0.0, StorageUtil.FloatListGet(none, "MARAS_FMR_Migration_PlayerChildrenMothersBirthTime", motherIdx), childName)
        if (childID != 0)
            MARAS.SetChildGender(childID, childGender)
            MARAS.UpdateChildName(childID, childName)
            ; FMR status codes:
            ; 0 = Available / Small Child
            ; 1 = In Training
            ; 2 = Adult (trained)
            ; 3 = Adopted (living at home as Hearthfire young child)
            if (status == 1) ; In Training
                TTM_Debug.debug("TTM_ServiceFertilityFMR: Child " + childName + " is in training. Setting training location to " + trainLocation + ".")
                MARAS.SetChildTrainingLocation(childID, trainLocation)
                MARAS.SetChildStage(childID, 3) ; Stage 3 = Training
            elseif (status == 2) ; Adult (trained)
                TTM_Debug.debug("TTM_ServiceFertilityFMR: Child " + childName + " is an adult. Setting stage to 4.")
                MARAS.SetChildStage(childID, 4) ; Stage 4 = Adult
                if (actorIdx >= 0 && actorIdx < fmrStorage.SpawnedChildActorRefs.Length)
                    Actor adultActor = fmrStorage.SpawnedChildActorRefs[actorIdx]
                    if (adultActor != none)
                        PromoteAdultFollowerChild(adultActor, childName, false)
                    endif
                endif
            elseif (status == 3) ; Adopted Hearthfire young child
                TTM_Debug.debug("TTM_ServiceFertilityFMR: Child " + childName + " is adopted. Setting stage to 2.")
                Actor childActor = GetAdoptedChildByName(childName)
                childrenLogicQst.AdoptChild(childActor, none, none, 2, true, mother, bioFather, childID)
            elseif (status == 0) ; Small Child (available)
                TTM_Debug.debug("TTM_ServiceFertilityFMR: Child " + childName + " is a small child. Setting stage to 2.")
                MARAS.SetChildStage(childID, 2) ; Stage 2 = Child
            endif
            TTM_Debug.info("TTM_ServiceFertilityFMR: Migrated child [" + childIdx + "]: " + childName + " (ID=" + childID + ", status=" + status + ", mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(player) + ")")
        endif
    endif
EndFunction

Actor Function GetAdoptedChildByName(string name) global
    Quest childrenAdoptionQst = TTM_Data.GetOriginalChildrenAdoptionQuest()
    Actor[] children = new Actor[2]
    if (Game.GetModByName("HearthfireMultiKid.esp") != 255)
        children = new Actor[6]
        children[0] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child1")
        children[1] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child2")
        children[2] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child3")
        children[3] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child4")
        children[4] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child5")
        children[5] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child6")
    else
        children[0] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child1")
        children[1] = TTM_Utils.GetActorAlias(childrenAdoptionQst, "Child2")
    endif
    int i = 0
    while (i < children.Length)
        if (children[i] != none && TTM_Utils.GetActorName(children[i]) == name)
            PreserveFmrSpawnedChildrenBeforeMigration(i+1, children[i])
            return children[i]
        endif
        i += 1
    endWhile
EndFunction

Function PreserveFmrSpawnedChildrenBeforeMigration(int index, Actor child) global
    if(!child)
        return
    endif
    Quest ttmChildrenLogic = TTM_Data.GetChildLogicQuest()
    TTM_Utils.SetActorAlias(ttmChildrenLogic, "PersistMigratedChild00" + index, child)
EndFunction
