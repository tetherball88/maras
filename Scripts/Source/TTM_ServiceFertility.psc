;/
  TTM_ServiceFertility.psc

  Central Facade / Router for fertility mod integration in M.A.R.A.S.
  Routes events and sync routines to either Fertility Mode Reloaded (FMR)
  or Beeing Female NG (BF) based on the active engine in the player's load order.
/;
Scriptname TTM_ServiceFertility

;/
  Engine identifiers
/;
int Function GetEngineNone() global
    return 0
EndFunction

int Function GetEngineFMR() global
    return 1
EndFunction

int Function GetEngineBF() global
    return 2
EndFunction

;/
  GetActiveEngine: Detects which fertility framework is active.
/;
int Function GetActiveEngine() global
    if (Game.GetModByName("BeeingFemale.esm") != 255)
        return 2 ; Beeing Female NG
    elseif (Game.GetModByName("Fertility Mode.esm") != 255)
        return 1 ; Fertility Mode Reloaded
    endif
    return 0 ; None
EndFunction

;/
  Maintenance: Called by TTM_MainController during startup and save loading.
/;
Function Maintenance() global
    int engine = GetActiveEngine()
    if (engine == 2)
        TTM_Debug.info("TTM_ServiceFertility: BeeingFemale.esm detected as active fertility provider.")
        TTM_ServiceFertilityBF.Maintenance()
    elseif (engine == 1)
        TTM_Debug.info("TTM_ServiceFertility: Fertility Mode.esm detected as active fertility provider.")
        TTM_ServiceFertilityFMR.Maintenance()
    else
        TTM_Debug.info("TTM_ServiceFertility: No supported fertility mod detected (neither FMR nor Beeing Female).")
    endif
EndFunction


;/
  FMR Lifecycle ModEvent Handlers
/;
Function OnFMRConception(Form akMother, Form akFather, int iTrackingIndex) global
    TTM_ServiceFertilityFMR.OnConception(akMother, akFather, iTrackingIndex)
EndFunction

Function OnFMRBabyBirth(Form akMother, Form akFather, int iTrackingIndex) global
    TTM_ServiceFertilityFMR.OnBabyBirth(akMother, akFather, iTrackingIndex)
EndFunction

Function OnFMRAbort(Form akMother, Form akFather, int iTrackingIndex) global
    TTM_ServiceFertilityFMR.OnAbort(akMother, akFather, iTrackingIndex)
EndFunction

Function OnFMRPregnancyCancelled(Form akMother, Form akFather, int iTrackingIndex) global
    TTM_ServiceFertilityFMR.OnPregnancyCancelled(akMother, akFather, iTrackingIndex)
EndFunction

Function OnFMRChildGrewUp(int childIndex, string childName, int gender, Form akMother) global
    TTM_ServiceFertilityFMR.OnChildGrewUp(childIndex, childName, gender, akMother as Actor)
EndFunction

Function OnFMRChildGrewUpNpcNpc(Actor mother, Actor father) global
    TTM_ServiceFertilityFMR.OnChildGrewUpNpcNpc(mother, father)
EndFunction

Function OnFMRChildSpawned(Form childActor, int childIndex, string childName) global
    TTM_ServiceFertilityFMR.OnChildSpawned(childActor, childIndex, childName)
EndFunction

Function OnFMRChildSentToTraining(int childIndex, string childName, string trainingLocation, string className) global
    TTM_ServiceFertilityFMR.OnChildSentToTraining(childIndex, childName, trainingLocation, className)
EndFunction

Function OnFMRChildFinishedTraining(int childIndex, string childName, string trainingLocation, string className) global
    TTM_ServiceFertilityFMR.OnChildFinishedTraining(childIndex, childName, trainingLocation, className)
EndFunction

Function OnFMRAdultChildSpawned(Form adultActor, int childIndex, string childName) global
    TTM_ServiceFertilityFMR.OnAdultChildSpawned(adultActor, childIndex, childName)
EndFunction


;/
  OnBeeingFemaleConception: Fired by Beeing Female NG when an actor is impregnated.
/;
Function OnBeeingFemaleConception(Form akMother, int childCount, Form akFather0, Form akFather1, Form akFather2) global
    TTM_ServiceFertilityBF.OnConception(akMother, childCount, akFather0, akFather1, akFather2)
EndFunction

;/
  OnBeeingFemaleLabor: Fired by Beeing Female NG when labor starts.
/;
Function OnBeeingFemaleLabor(Form akMother, int childCount, Form akFather0, Form akFather1, Form akFather2) global
    TTM_ServiceFertilityBF.OnLabor(akMother, childCount, akFather0, akFather1, akFather2)
EndFunction

;/
  OnBeeingFemaleBirth: Fired by Beeing Female NG for every child that reached the world.
/;
Function OnBeeingFemaleBirth(Form akMother, Form akFather, Form akBaby, string asBabyName, int aiBabySex) global
    TTM_ServiceFertilityBF.OnBirth(akMother, akFather, akBaby, asBabyName, aiBabySex)
EndFunction

;/
  OnBeeingFemaleChildSpawned: Fired by Beeing Female NG when a child is physically in the world as an actor.
/;
Function OnBeeingFemaleChildSpawned(Form akMother, Form akFather, Form akChild, string asChildName) global
    TTM_ServiceFertilityBF.OnChildSpawned(akMother, akFather, akChild, asChildName)
EndFunction

;/
  OnBeeingFemaleAdultChildSpawned: Fired by Beeing Female NG when a child reaches adulthood.
/;
Function OnBeeingFemaleAdultChildSpawned(Form akMother, Form akFather, Form akAdult, Form akChild, string asChildName) global
    TTM_ServiceFertilityBF.OnAdultChildSpawned(akMother, akFather, akAdult, akChild, asChildName)
EndFunction

;/
  OnBeeingFemaleAbort: Fired by Beeing Female NG when a pregnancy ends in loss (abortion, miscarriage, missed-abortion stillbirth).
/;
Function OnBeeingFemaleAbort(Form akMother, Form akFather, int aiAbortusState, string asReason) global
    TTM_ServiceFertilityBF.OnAbort(akMother, akFather, aiAbortusState, asReason)
EndFunction

;/
  OnBeeingFemaleStillbirth: Fired by Beeing Female NG once per child lost to the stillbirth roll at delivery.
/;
Function OnBeeingFemaleStillbirth(Form akMother, Form akFather) global
    TTM_ServiceFertilityBF.OnStillbirth(akMother, akFather)
EndFunction

;/
  OnBeeingFemaleStateChange: Fired by Beeing Female NG whenever a woman changes cycle state ("stateChanged").
/;
Function OnBeeingFemaleStateChange(string eventName, string strArg, float numArg, Form sender) global
    TTM_ServiceFertilityBF.OnStateChange(eventName, strArg, numArg, sender)
EndFunction

;/
  OnPaternitySecretResolved: Fired from SKSE C++ when an LLM evaluation resolves a paternity secret.
/;
Function OnPaternitySecretResolved(string eventName, string strArg, float numArg, Form sender) global
    int childID = numArg as int
    string caseType = strArg

    if (caseType == "" && sender != none)
        Actor bioFather = MARAS.GetBioFather(childID)
        if (sender == bioFather)
            caseType = "bio_father_learnt"
        else
            caseType = "presumed_father_learnt"
        endif
    endif

    TTM_Debug.info("TTM_ServiceFertility: OnPaternitySecretResolved child=" + childID + " case=" + caseType)

    TTM_ServiceAffection.ApplyPaternitySecretForChild(childID, caseType)

    if (TTM_Data.GetHasSkyrimNet() && childID != 0)
        TTM_ServiceSkyrimNet.RegisterPaternitySecretEvent(childID, caseType)
    endif
EndFunction

;/
  PromoteAdultFollowerChild: Rebinds an ephemeral child to a spawned adult follower actor.
/;
Function PromoteAdultFollowerChild(Actor adultActor, string childName = "") global
    int engine = GetActiveEngine()
    if (engine == 2)
        TTM_ServiceFertilityBF.PromoteAdultFollowerChild(adultActor, childName)
    else
        TTM_ServiceFertilityFMR.PromoteAdultFollowerChild(adultActor, childName)
    endif
EndFunction

bool Function IsMigrated() global
    int engine = GetActiveEngine()
    if (engine == 1)
        return TTM_ServiceFertilityFMR.IsMigrated()
    elseif (engine == 2)
        return TTM_ServiceFertilityBF.IsMigrated()
    endif
    return true
EndFunction
