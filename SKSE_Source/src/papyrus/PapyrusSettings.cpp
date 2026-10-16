#include "papyrus/PapyrusInterface.h"

#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS::PapyrusInterface {

    void SetSettingBool(RE::StaticFunctionTag*, std::string key, bool value) {
        std::lock_guard lock(GetPapyrusMutex());
        Settings::GetSingleton().SetBoolByKey(key, value);
    }

    bool GetSettingBool(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard lock(GetPapyrusMutex());
        return Settings::GetSingleton().GetBoolByKey(key);
    }

    void SetSettingInt(RE::StaticFunctionTag*, std::string key, int32_t value) {
        std::lock_guard lock(GetPapyrusMutex());
        Settings::GetSingleton().SetIntByKey(key, value);
    }

    int32_t GetSettingInt(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard lock(GetPapyrusMutex());
        return Settings::GetSingleton().GetIntByKey(key);
    }

    void SetSettingFloat(RE::StaticFunctionTag*, std::string key, float value) {
        std::lock_guard lock(GetPapyrusMutex());
        Settings::GetSingleton().SetFloatByKey(key, value);
    }

    float GetSettingFloat(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard lock(GetPapyrusMutex());
        return Settings::GetSingleton().GetFloatByKey(key);
    }

    bool IsSettingsMigrated(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetBoolByKey("migratedFromStorageUtil");
    }

    void MarkSettingsMigrated(RE::StaticFunctionTag*) {
        Settings::GetSingleton().SetBoolByKey("migratedFromStorageUtil", true);
        MARAS_LOG_INFO("Marked settings as migrated from StorageUtil");
    }

    int32_t GetStartDialGender(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetStartDialGender();
    }

    void SetStartDialGender(RE::StaticFunctionTag*, int32_t val) {
        Settings::GetSingleton().SetStartDialGender(val);
    }

    bool GetEnablePolygamy(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetEnablePolygamy();
    }

    void SetEnablePolygamy(RE::StaticFunctionTag*, bool val) {
        std::lock_guard lock(GetPapyrusMutex());
        Settings::GetSingleton().SetEnablePolygamy(val);
    }

    int32_t GetMinRelRankForDial(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetMinRelRankForDial();
    }

    void SetMinRelRankForDial(RE::StaticFunctionTag*, int32_t val) {
        Settings::GetSingleton().SetMinRelRankForDial(val);
    }

}  // namespace MARAS::PapyrusInterface

