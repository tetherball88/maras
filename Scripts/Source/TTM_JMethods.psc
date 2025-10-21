scriptname TTM_JMethods

import TTM_JCDomain

string Function _GetNamespaceKey() global
    return ".TT_MARAS"
EndFunction

Function ClearStorage() global
    JDB_solveObjSetter(_GetNamespaceKey(), JMap_object())
    JDB_solveObjSetter(_GetNamespaceKey() + "Forms", JMap_object())
EndFunction

Function ExportStorage() global
    int regular = JDB_solveObj(_GetNamespaceKey())
    int forms = JDB_solveObj(_GetNamespaceKey() + "Forms")
    int jObj = JMap_object()
    JMap_setObj(jObj, "regular", regular)
    JMap_setObj(jObj, "forms", forms)
    JValue_writeToFile(jObj, JContainers.userDirectory() + "MARAS/store.json")
EndFunction

Function ImportStorage() global
    int storedData = JValue.readFromFile(JContainers.userDirectory() + "MARAS/store.json")
    int regular = JMap_getObj(storedData, "regular")
    int forms = JMap_getObj(storedData, "forms")
    JDB_solveObjSetter(_GetNamespaceKey(), regular, true)
    JDB_solveObjSetter(_GetNamespaceKey() + "Forms", forms, true)
EndFunction

;/ ==============================
   Static Data Storage Helpers
   ==============================/;

Function _setStaticData(string path, int JObj) global
    JDB_solveObjSetter(_GetNamespaceKey() + ".staticData." + path, JObj, true)
EndFunction

Function ImportDataFromFile(string propertyName, string jsonPath, bool folder = false, bool isFormMap = false) global
    int initialData
    if(folder)
        initialData = LoadMultipleFiles(jsonPath, isFormMap)
    else
        initialData = JValue_readFromFile(jsonPath)
    endif
    _setStaticData(propertyName, initialData)
EndFunction

int Function LoadMultipleFiles(string folderPath, bool isFormMap = false) global
    int JTarget
    if(isFormMap)
        JTarget = JFormMap_object()
    else
        JTarget = JMap_object()
    endif
    int JFilesMap = JValue_readFromDirectory(folderPath)
    string nextKey = JMap_nextKey(JFilesMap)
    while(nextKey)
      if(isFormMap)
          JFormMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      else
          JMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      endif
          nextKey = JMap_nextKey(JFilesMap, nextKey)
    endwhile
    JValue_release(JFilesMap)
    return JTarget
EndFunction

int Function getStaticData(string path) global
    return JDB_solveObj(_GetNamespaceKey() + ".staticData." + path)
EndFunction

int Function countStaticData(string path) global
    int jObj = getStaticData(path)
    return JArray_count(jObj)
EndFunction

int Function GetObjStaticData(string path) global
    return JDB_solveObj(_GetNamespaceKey() + ".staticData." + path)
EndFunction

int Function GetIntStaticData(string path, int default = 0) global
    return JDB_solveInt(_GetNamespaceKey() + ".staticData." + path, default)
EndFunction

float Function GetFltStaticData(string path, float default = 0.0) global
    return JDB_solveFlt(_GetNamespaceKey() + ".staticData." + path, default)
EndFunction

string Function GetStrStaticData(string path, string default = "") global
    return JDB_solveStr(_GetNamespaceKey() + ".staticData." + path, default)
EndFunction

Form Function GetFormStaticData(string path, Form default = none) global
    return JDB_solveForm(_GetNamespaceKey() + ".staticData." + path, default)
EndFunction

;/ ==============================
   Save Data Storage Helpers
   ==============================/;
; use Player Form as global instance
Form Function ProcessFormKey(Form formKey, bool isSet = false) global
    if(formKey == none)
        formKey = Game.GetPlayer()
    endif

    if(isSet)
        AddFormName(formKey)
    endif

    return formKey
EndFunction

Function AddFormName(Form formKey) global
    if(JFormDB_hasPath(formKey, _GetNamespaceKey() + "Forms") == "")
        string name = formKey.GetName()
        if(name == "")
            Cell c
            ObjectReference ref = formKey as ObjectReference
            name = ref.GetDisplayName()
            if(name == "")
                name = "No name"
            endif
        endif
        string type = GetStrStaticData("initialData.FormTypes." + formKey.GetType())
        JFormDB_solveStrSetter(formKey, _GetNamespaceKey() + "Forms._name", type + "::" +name, true)
    endif
EndFunction

string Function BuildPath(string propertyName) global
    return _GetNamespaceKey() + "Forms" + ".saveData." + propertyName
EndFunction

; Clear object structure at given path
Function ClearValue(Form formKey, string propertyName) global
    JFormDB_solveObjSetter(ProcessFormKey(formKey), BuildPath(propertyName), 0)
EndFunction

;/==============================
   Setters and Getters for single properties
   ==============================/;

Function SetObjValue(Form formKey, string propertyName, int value) global
    JFormDB_solveObjSetter(ProcessFormKey(formKey, true), BuildPath(propertyName), value, true)
EndFunction
int Function GetObjValue(Form formKey, string propertyName, int default = 0) global
    return JFormDB_solveObj(ProcessFormKey(formKey), BuildPath(propertyName), default)
EndFunction
bool Function HasObjValue(Form formKey, string propertyName) global
    return JFormDB_hasPath(ProcessFormKey(formKey), BuildPath(propertyName))
EndFunction

Function SetIntValue(Form formKey, string propertyName, int value) global
    JFormDB_solveIntSetter(ProcessFormKey(formKey, true), BuildPath(propertyName), value, true)
EndFunction
int Function GetIntValue(Form formKey, string propertyName, int default = 0) global
    return JFormDB_solveInt(ProcessFormKey(formKey), BuildPath(propertyName), default)
EndFunction

Function SetFloatValue(Form formKey, string propertyName, float value) global
    JFormDB_solveFltSetter(ProcessFormKey(formKey, true), BuildPath(propertyName), value, true)
EndFunction
float Function GetFloatValue(Form formKey, string propertyName, float default = 0.0) global
    return JFormDB_solveFlt(ProcessFormKey(formKey), BuildPath(propertyName), default)
EndFunction

Function SetStringValue(Form formKey, string propertyName, string value) global
    JFormDB_solveStrSetter(ProcessFormKey(formKey, true), BuildPath(propertyName), value, true)
EndFunction
string Function GetStringValue(Form formKey, string propertyName, string default = "") global
    return JFormDB_solveStr(ProcessFormKey(formKey), BuildPath(propertyName), default)
EndFunction

Function SetFormValue(Form formKey, string propertyName, Form value) global
    JFormDB_solveFormSetter(ProcessFormKey(formKey, true), BuildPath(propertyName), value, true)
EndFunction
Form Function GetFormValue(Form formKey, string propertyName, Form default = none) global
    return JFormDB_solveForm(ProcessFormKey(formKey), BuildPath(propertyName), default)
EndFunction
bool Function HasFormValue(Form formKey, string propertyName) global
    return JFormDB_hasPath(ProcessFormKey(formKey), BuildPath(propertyName))
EndFunction

;/==============================
   Helpers to work with lists
   ==============================/;
Function FormListSet(Form formKey, string propertyName, int index, Form value) global
    int JArr = _GetOrCreateJArray(formKey, propertyName)
    JArray_setForm(JArr, index, value)
EndFunction
Form Function FormListGet(Form ObjKey, string KeyName, int index) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    return JArray_getForm(JArr, index)
EndFunction
Form[] Function FormListToArray(Form ObjKey, string KeyName) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    return JArray_asFormArray(JArr)
EndFunction
int Function FormListAdd(Form ObjKey, string KeyName, Form value) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName)
    JArray_addForm(JArr, value)
    return JArray_count(JArr) - 1
EndFunction
function FormListRemove(Form ObjKey, string KeyName, Form value) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    JArray_eraseForm(JArr, value)
EndFunction
int function FormListFind(Form ObjKey, string KeyName, Form value) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    return JArray_findForm(JArr, value)
EndFunction
Form function FormListRandom(Form ObjKey, string KeyName) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    return JArray_getForm(JArr, Utility.RandomInt(0, JArray_count(JArr) - 1))
EndFunction

function StringListSet(Form ObjKey, string KeyName, int index, string value) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName)
    JArray_setStr(JArr, index, value)
EndFunction
string function StringListGet(Form ObjKey, string KeyName, int index) global
    int JArr = _GetOrCreateJArray(ObjKey, KeyName, false)
    return JArray_getStr(JArr, index)
EndFunction

int Function _GetOrCreateJArray(Form formKey, string propertyName, bool createIfMissing = true) global
    int res = GetObjValue(formKey, propertyName, 0)
    if (res == 0 && createIfMissing)
        res = JArray_object()
        SetObjValue(formKey, propertyName, res)
    endif
    return res
EndFunction
