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
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (!mother)
        return
    endif
    Actor[] bioFathers = new Actor[1]
    bioFathers[0] = akFather as Actor
    MARAS.OnFertilityConception("fmr", mother, bioFathers, iTrackingIndex, Utility.GetCurrentGameTime())
EndFunction

;/
  OnActorStatus: Fired by FMR via "FMR_ActorStatus" ModEvent with factionRank (1-100 = pregnancy %, 120 = labor).
/;
Function OnActorStatus(Form akMother, int factionRank, string fatherName, int fatherRaceId) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityProgressStatus("fmr", mother, factionRank, Utility.GetCurrentGameTime())
    endif
EndFunction

Function OnBabyBirth(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityBirth("fmr", mother, akFather as Actor, "Newborn", 2, Utility.GetCurrentGameTime())
    endif
EndFunction

Function OnAbort(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityPregnancyLoss("fmr", mother, akFather as Actor, "abortion", false)
    endif
EndFunction

;/
  OnMiscarriage: Fired by FMR via "FMR_BabyMiscarriage" ModEvent when a pregnancy ends in miscarriage.
/;
Function OnMiscarriage(Form akMother, string cause) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityPregnancyLoss("fmr", mother, none, "miscarriage", true)
    endif
EndFunction

Function OnPregnancyCancelled(Form akMother, Form akFather, int iTrackingIndex) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityPregnancyLoss("fmr", mother, akFather as Actor, "cancelled", false)
    endif
EndFunction

Function OnChildGrewUp(int childIndex, string childName, int gender, Actor mother) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    MARAS.OnFertilityChildGrewUp(mother, childName, gender, false, none)
EndFunction

Function OnChildGrewUpNpcNpc(Actor mother, Actor father) global
    MARAS.OnFertilityChildGrewUp(mother, "", -1, true, father)
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
    if (childID == 0)
        return
    endif
    Actor bioMother = MARAS.GetBioMother(childID)
    Actor bioFather = MARAS.GetBioFather(childID)
    if (MARAS.RegisterNpcFertilityChildSpawn(child, childID, bioMother, bioFather, 2, false))
        child.EvaluatePackage()
        child.MoveToPackageLocation()
    else
        TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
        Actor coSpouse = MARAS.GetFertilitySpawnCoSpouse(childID, bioMother, bioFather)
        childrenLogicQst.AdoptChild(child, spouseToAdopWith = coSpouse, bioMother = bioMother, bioFather = bioFather, syntheticID = childID, childStage = 2)
    endif
    TTM_Debug.info("TTM_ServiceFertilityFMR: Spawned physical child actor " + child.GetDisplayName() + " re-keyed from synthetic ID " + childID)
EndFunction

Function OnChildSentToTraining(int childIndex, string childName, string trainingLocation, string className) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    MARAS.OnFertilityChildSentToTraining(childName, trainingLocation, className)
EndFunction

Function OnChildFinishedTraining(int childIndex, string childName, string trainingLocation, string className) global
    if (TTM_ServiceFertility.GetActiveEngine() != 1)
        return
    endif
    MARAS.OnFertilityChildFinishedTraining(childName, trainingLocation, className)
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
    if (adultActor != none)
        MARAS.OnFertilityAdultChildSpawned("fmr", adultActor, none, none, none, childName, false, fireEvent)
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

    int[] motherIndices = new int[128]
    float[] motherBirthTimes = new float[128]
    int motherCount = 0

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
                if (fmrStorage.ImmersiveEffectsFaction)
                    int fmrRank = mother.GetFactionRank(fmrStorage.ImmersiveEffectsFaction)
                    if (((fmrRank >= 34 && fmrRank <= 100) || fmrRank == 120) && MARAS.IsPregnancySecretForPartner(unbornID))
                        MARAS.RevealPregnancyToPartner(unbornID, true)
                    endif
                endif
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
                    if (motherCount < 128)
                        float bTime = fmrStorage.LastBirth[tIdx]
                        ; Find index to insert in ascending order (oldest to newest)
                        int insertIdx = 0
                        while (insertIdx < motherCount && motherBirthTimes[insertIdx] < bTime)
                            insertIdx += 1
                        endWhile
                        TTM_Debug.debug("TTM_ServiceFertilityFMR: Inserting player children mother at index " + insertIdx + ": " + TTM_Utils.GetActorName(mother))
                        int shiftIdx = motherCount
                        while (shiftIdx > insertIdx)
                            motherIndices[shiftIdx] = motherIndices[shiftIdx - 1]
                            motherBirthTimes[shiftIdx] = motherBirthTimes[shiftIdx - 1]
                            shiftIdx -= 1
                        endWhile
                        motherIndices[insertIdx] = tIdx
                        motherBirthTimes[insertIdx] = bTime
                        motherCount += 1
                    endif
                else
                    int npcChildID = MARAS.MigrateCompletedFmrNpcChild(mother, father, conceptionTime, birthTime)
                    if (npcChildID != 0)
                        TTM_Debug.info("TTM_ServiceFertilityFMR: Migrated completed NPC-NPC birth: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(father) + " (Child ID=" + npcChildID + ")")
                    endif
                endif
            endif
        endif
        tIdx += 1
    endWhile

    ; try to resolve player's children to mothers
    int motherIdx = motherCount - 1
    int childIdx = fmrStorage.PlayerChildName.Length - 1
    TTM_Debug.debug("TTM_ServiceFertilityFMR: Player child mother index: " + motherIdx + "; child index: " + childIdx)
    bool lastFatherUsed = false
    if(motherIdx >= 0 && childIdx >= 0)
        while (motherIdx >= 0 && childIdx >= 0)
            int nIdx = motherIndices[motherIdx]
            Actor bioFather = fmrStorage.LastFatherRef[nIdx] as Actor
            if(lastFatherUsed)
                bioFather = player
            endif
            lastFatherUsed = true
            RegisterPlayerChild(nIdx, motherBirthTimes[motherIdx], childIdx, bioFather, fmrStorage)
            motherIdx -= 1
            childIdx -= 1
        endWhile
        while(childIdx >= 0)
            int nIdx = motherIndices[0]
            RegisterPlayerChild(nIdx, motherBirthTimes[0], childIdx, player, fmrStorage)
            childIdx -= 1
        endWhile
    endif

    MarkMigrated()
EndFunction

Function RegisterPlayerChild(int nIdx, float birthTime, int childIdx, Actor bioFather, _JSW_BB_Storage fmrStorage) global
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
        int childID = MARAS.RegisterFertilityChild(0, mother, none, bioFather, 0.0, birthTime, childName)
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
