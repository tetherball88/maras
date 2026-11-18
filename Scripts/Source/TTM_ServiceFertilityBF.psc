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
    StorageUtil.SetIntValue(child, "FW.Child.GrownUp", 1)
EndFunction

;/
  RegisterBeeingFemaleChild: Registers a physical Beeing Female child actor in MARAS (player household adoption
  when the player is involved, NPC household otherwise) and takes over its behavior.
/;
Function RegisterBeeingFemaleChild(Actor childActor, Actor bioMother, Actor bioFather, int syntheticID, int stage, bool isMigrating = false) global
    if (!childActor)
        return
    endif
    TakeOverChild(childActor)
    float dob = 0.0
    string cName = ""
    if (syntheticID == 0)
        dob = StorageUtil.GetFloatValue(childActor, "FW.Child.Birthday", 0.0)
        cName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
    endif
    syntheticID = MARAS.ResolveFertilitySpawnSyntheticId(childActor, bioMother, bioFather, syntheticID, dob, cName)
    if (MARAS.RegisterNpcFertilityChildSpawn(childActor, syntheticID, bioMother, bioFather, stage, isMigrating))
        childActor.EvaluatePackage()
        childActor.MoveToPackageLocation()
    else
        TTM_ServiceChildrenLogic childrenLogicQst = TTM_Data.GetChildLogicQuest() as TTM_ServiceChildrenLogic
        Actor coSpouse = MARAS.GetFertilitySpawnCoSpouse(syntheticID, bioMother, bioFather)
        childrenLogicQst.AdoptChild(childActor, spouseToAdopWith = coSpouse, bioMother = bioMother, bioFather = bioFather, syntheticID = syntheticID, childStage = stage, isMigrating = isMigrating)
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
  Reads FW.ChildFather per fetus and delegates registration/affection/announcement to C++.
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

    int fatherListCount = StorageUtil.FormListCount(mother, "FW.ChildFather")
    Actor[] bioFathers = PapyrusUtil.ActorArray(childCount)
    int i = 0
    while (i < childCount)
        Actor bioFather = none
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
        bioFathers[i] = bioFather
        i += 1
    endWhile

    MARAS.OnFertilityConception("bf", mother, bioFathers, childCount, Utility.GetCurrentGameTime())
EndFunction

;/
  OnLabor: Fired by Beeing Female NG ("BeeingFemaleLabor" ModEvent) when labor begins.
/;
Function OnLabor(Form akMother, int childCount, Form akFather0, Form akFather1, Form akFather2) global
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityLaborStarted(mother, childCount, akFather0 as Actor, Utility.GetCurrentGameTime())
    endif
EndFunction

;/
  OnBirth: Fired by Beeing Female NG ("BeeingFemaleBirth" ModEvent) once per child that reached the world.
/;
Function OnBirth(Form akMother, Form akFather, Form akBaby, string asBabyName, int aiBabySex) global
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityBirth("bf", mother, akFather as Actor, asBabyName, aiBabySex, Utility.GetCurrentGameTime())
    endif
EndFunction

;/
  ApplyChildName: Syncs a name chosen via the M.A.R.A.S. Child Naming Modal back to Beeing Female NG's
  child actor storage (BabySpawn = 1) or carried baby item list (BabySpawn = 2).
/;
Function ApplyChildName(Actor childActor, Actor mother, string oldName, string newName) global
    if (newName == "")
        return
    endif
    if (childActor != none)
        StorageUtil.SetStringValue(childActor, "FW.Child.Name", newName)
        childActor.SetDisplayName(newName, true)
    endif
    if (mother != none)
        int itemCount = StorageUtil.StringListCount(mother, "FW.BabyItemName")
        int i = itemCount - 1
        while (i >= 0)
            string entry = StorageUtil.StringListGet(mother, "FW.BabyItemName", i)
            if (oldName == "" || entry == oldName)
                StorageUtil.StringListSet(mother, "FW.BabyItemName", i, newName)
                i = -1
            else
                i -= 1
            endif
        endWhile

        if (childActor == none)
            int nBabys = StorageUtil.FormListCount(none, "FW.Babys")
            int b = nBabys - 1
            while (b >= 0)
                Actor cand = StorageUtil.FormListGet(none, "FW.Babys", b) as Actor
                if (cand != none && (StorageUtil.GetFormValue(cand, "FW.Child.Mother", none) as Actor) == mother)
                    string storedName = StorageUtil.GetStringValue(cand, "FW.Child.Name", "")
                    if (oldName == "" || storedName == oldName)
                        StorageUtil.SetStringValue(cand, "FW.Child.Name", newName)
                        cand.SetDisplayName(newName, true)
                        b = -1
                    else
                        b -= 1
                    endif
                else
                    b -= 1
                endif
            endWhile
        endif
    endif
EndFunction

;/
  FinishInfantGrowth: Stops Beeing Female's scale-up loop on a physical BF Infant promoted early to Stage 2.
/;
Function FinishInfantGrowth(Actor childActor) global
    if (!childActor)
        return
    endif
    StorageUtil.SetIntValue(childActor, "FW.AddOn.StartGrowing", 0)
    childActor.SetScale(1.0)
    childActor.AllowPCDialogue(true)
EndFunction

;/
  RemoveCarriedBabyItem: Removes a carried BF BabySpawn = 2 baby item from the mother when M.A.R.A.S.
  manually grows the infant early so Beeing Female will not hatch a duplicate copy later.
/;
Function RemoveCarriedBabyItem(Actor mother, string babyName) global
    if (!mother)
        return
    endif
    int itemCount = StorageUtil.StringListCount(mother, "FW.BabyItemName")
    if (itemCount <= 0)
        return
    endif
    int matchIdx = -1
    int i = itemCount - 1
    while (i >= 0 && matchIdx < 0)
        string entry = StorageUtil.StringListGet(mother, "FW.BabyItemName", i)
        if (babyName == "" || entry == babyName)
            matchIdx = i
        endif
        i -= 1
    endWhile
    if (matchIdx < 0)
        return
    endif

    Armor arm = none
    if (StorageUtil.FormListCount(mother, "FW.BabyItemArmor") > matchIdx)
        arm = StorageUtil.FormListGet(mother, "FW.BabyItemArmor", matchIdx) as Armor
        StorageUtil.FormListRemoveAt(mother, "FW.BabyItemArmor", matchIdx)
    endif
    if (StorageUtil.StringListCount(mother, "FW.BabyItemName") > matchIdx)
        StorageUtil.StringListRemoveAt(mother, "FW.BabyItemName", matchIdx)
    endif
    if (StorageUtil.IntListCount(mother, "FW.BabyItemSex") > matchIdx)
        StorageUtil.IntListRemoveAt(mother, "FW.BabyItemSex", matchIdx)
    endif
    if (StorageUtil.FormListCount(mother, "FW.BabyItemRace") > matchIdx)
        StorageUtil.FormListRemoveAt(mother, "FW.BabyItemRace", matchIdx)
    endif
    if (StorageUtil.FormListCount(mother, "FW.BabyItemFather") > matchIdx)
        StorageUtil.FormListRemoveAt(mother, "FW.BabyItemFather", matchIdx)
    endif
    if (StorageUtil.FloatListCount(mother, "FW.BabyItemDOB") > matchIdx)
        StorageUtil.FloatListRemoveAt(mother, "FW.BabyItemDOB", matchIdx)
    endif

    if (arm != none)
        if (mother == TTM_Data.GetPlayer())
            StorageUtil.FormListRemove(none, "FW.Babys", arm, false)
        endif
        mother.UnequipItem(arm)
        mother.RemoveItem(arm, 1, true)
    endif
EndFunction

;/
  OnChildSpawned: Fired by Beeing Female NG ("BeeingFemaleChildSpawned" ModEvent) once a child is physically in the world.
  Scaled-down growing BF actors remain Stage 1 Infants (non-conversational) until reaching normal scale (1.0),
  while full-scale child actors immediately enter Stage 2 Young Child.
/;
Function OnChildSpawned(Form akMother, Form akFather, Form akChild, string asChildName) global
    Actor childActor = akChild as Actor
    if (!childActor)
        return
    endif
    Actor mother = akMother as Actor

    if (MARAS.GetChildStage(childActor.GetFormID()) != 0)
        return ; already registered
    endif

    ; A baby born straight as an actor fires this event right after BeeingFemaleBirth: let the birth handler create the
    ; Infant (and any player-household Child Naming Modal) finish before re-keying to the child actor.
    Utility.Wait(1.0)

    string childName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
    if (childName == "")
        childName = asChildName
    endif
    if (childName == "")
        childName = childActor.GetDisplayName()
    endif

    int childID = MARAS.FindFertilityInfantForSpawn(mother, childName)
    if (childID == 0 && asChildName != "" && asChildName != childName)
        childID = MARAS.FindFertilityInfantForSpawn(mother, asChildName)
    endif
    if (childID == 0)
        Utility.Wait(2.0)
        childName = StorageUtil.GetStringValue(childActor, "FW.Child.Name", "")
        if (childName == "")
            childName = asChildName
        endif
        childID = MARAS.FindFertilityInfantForSpawn(mother, childName)
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
        string recordedName = MARAS.GetChildName(childID)
        if (recordedName != "" && recordedName != "Newborn" && recordedName != childName)
            childName = recordedName
            StorageUtil.SetStringValue(childActor, "FW.Child.Name", childName)
            childActor.SetDisplayName(childName, true)
        endif
    endif

    float refScale = MARAS.GetActorRefScale(childActor)
    bool isGrowingInfant = (refScale < 0.99) || (StorageUtil.GetIntValue(childActor, "FW.AddOn.StartGrowing", 0) == 1)
    if (isGrowingInfant)
        childActor.AllowPCDialogue(false)
        RegisterBeeingFemaleChild(childActor, bioMother, bioFather, childID, 1)
        TTM_Debug.info("TTM_ServiceFertilityBF: Scaled-down BF infant actor " + childName + " registered at Stage 1 (FormID " + childActor.GetFormID() + ", refScale=" + refScale + ")")
    else
        RegisterBeeingFemaleChild(childActor, bioMother, bioFather, childID, 2)
        TTM_Debug.info("TTM_ServiceFertilityBF: Child actor " + childName + " is in the world at Stage 2 (FormID " + childActor.GetFormID() + ", refScale=" + refScale + ")")
        MARAS.OnFertilityChildGrewUpActor(childActor, childName)
    endif
EndFunction

;/
  OnAdultChildSpawned: Fired by Beeing Female NG ("BeeingFemaleAdultChildSpawned" ModEvent) when a child reaches adulthood.
/;
Function OnAdultChildSpawned(Form akMother, Form akFather, Form akAdult, Form akChild, string asChildName) global
    Actor adultActor = akAdult as Actor
    if (adultActor)
        MARAS.OnFertilityAdultChildSpawned("bf", adultActor, akChild as Actor, akMother as Actor, akFather as Actor, asChildName, true, true)
    endif
EndFunction

;/
  OnAbort: Fired by Beeing Female NG ("BeeingFemaleAbort" ModEvent) once the pregnancy has actually ended in loss.
/;
Function OnAbort(Form akMother, Form akFather, int aiAbortusState, string asReason) global
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityPregnancyLoss("bf", mother, akFather as Actor, asReason, true)
    endif
EndFunction

;/
  OnStillbirth: Fired by Beeing Female NG ("BeeingFemaleStillbirth" ModEvent) once per child lost to the stillbirth roll.
/;
Function OnStillbirth(Form akMother, Form akFather) global
    Actor mother = akMother as Actor
    if (mother)
        MARAS.OnFertilityPregnancyLoss("bf", mother, akFather as Actor, "stillbirth", true)
    endif
EndFunction

;/
  OnStateChange: Fired by Beeing Female NG ("BeeingFemale" ModEvent, strArg "stateChanged", numArg = the woman's FormID).
/;
Function OnStateChange(string eventName, string strArg, float numArg, Form sender) global
    if (strArg != "stateChanged")
        return
    endif
    Actor mother = Game.GetForm(numArg as int) as Actor
    if (!mother)
        return
    endif
    int currentState = StorageUtil.GetIntValue(mother, "FW.CurrentState", -1)
    if (currentState >= 5 && currentState <= 8)
        MARAS.OnFertilityProgressStatus("bf", mother, currentState, Utility.GetCurrentGameTime())
    endif
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
    TTM_Debug.info("TTM_ServiceFertilityBF: Starting one-time migration from Beeing Female storage")

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
            if (unbornID == 0 && !mother.IsDead())
                MARAS.ReportMigrationIssue("Beeing Female Migration", "Failed to register active BF pregnancy for mother '" + TTM_Utils.GetActorName(mother) + "'.")
            else
                if (currentState >= 5 && MARAS.IsPregnancySecretForPartner(unbornID))
                    MARAS.RevealPregnancyToPartner(unbornID, true)
                endif
                TTM_Debug.info("TTM_ServiceFertilityBF: Migrated active pregnancy: mother=" + TTM_Utils.GetActorName(mother) + ", father=" + TTM_Utils.GetActorName(bioFather) + " (Unborn ID=" + unbornID + ")")
            endif
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
            if (infantID == 0 && !mother.IsDead())
                MARAS.ReportMigrationIssue("Beeing Female Migration", "Failed to register carried BF newborn '" + babyName + "' for mother '" + TTM_Utils.GetActorName(mother) + "'.")
            elseif (infantID != 0)
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

    if (!childActor.IsChild() && StorageUtil.GetIntValue(childActor, "FW.Child.GrownUp", 0) == 1)
        MARAS.RegisterChildActor(childActor, none, none, none, mother, father, syntheticID, 4, sendEvent = false) ; Stage 4 = Adult
    elseif ((MARAS.GetActorRefScale(childActor) < 0.99) || (StorageUtil.GetIntValue(childActor, "FW.AddOn.StartGrowing", 0) == 1))
        childActor.AllowPCDialogue(false)
        RegisterBeeingFemaleChild(childActor, mother, father, syntheticID, 1, isMigrating = true)
    else
        RegisterBeeingFemaleChild(childActor, mother, father, syntheticID, 2, isMigrating = true)
    endif
    TTM_Debug.info("TTM_ServiceFertilityBF: Migrated child actor " + childName + " (FormID " + childActor.GetFormID() + ")")
EndFunction
