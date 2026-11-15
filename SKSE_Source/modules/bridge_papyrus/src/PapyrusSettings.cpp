#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/AppContext.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/app/SkyrimNetGateway.h"
#include "maras/engine/EngineLogging.h"

namespace MARAS::PapyrusInterface {

    void SetSettingBool(RE::StaticFunctionTag*, std::string key, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        MARAS_LOG_DEBUG("Papyrus: SetSettingBool({}, {})", key, value);
        App::SettingsCoordinator::GetSingleton().SetBoolByKey(key, value);
    }

    bool GetSettingBool(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetBoolByKey(key);
    }

    void SetSettingInt(RE::StaticFunctionTag*, std::string key, int32_t value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        MARAS_LOG_DEBUG("Papyrus: SetSettingInt({}, {})", key, value);
        App::SettingsCoordinator::GetSingleton().SetIntByKey(key, value);
    }

    int32_t GetSettingInt(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetIntByKey(key);
    }

    void SetSettingFloat(RE::StaticFunctionTag*, std::string key, float value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        MARAS_LOG_DEBUG("Papyrus: SetSettingFloat({}, {})", key, value);
        App::SettingsCoordinator::GetSingleton().SetFloatByKey(key, value);
    }

    float GetSettingFloat(RE::StaticFunctionTag*, std::string key) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetFloatByKey(key);
    }

    bool IsSettingsMigrated(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetBoolByKey("migratedFromStorageUtil");
    }

    void MarkSettingsMigrated(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetBoolByKey("migratedFromStorageUtil", true);
        MARAS_LOG_INFO("Papyrus: Marked settings as migrated from StorageUtil");
    }

    int32_t GetStartDialGender(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetStartDialGender();
    }

    void SetStartDialGender(RE::StaticFunctionTag*, int32_t value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetStartDialGender(value);
    }

    bool GetEnablePolygamy(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetEnablePolygamy();
    }

    void SetEnablePolygamy(RE::StaticFunctionTag*, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetEnablePolygamy(value);
    }

    bool GetDisablePolygamyQuest(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetDisablePolygamyQuest();
    }

    void SetDisablePolygamyQuest(RE::StaticFunctionTag*, bool value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetDisablePolygamyQuest(value);
    }

    int32_t GetMinRelRankForDial(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetMinRelRankForDial();
    }

    void SetMinRelRankForDial(RE::StaticFunctionTag*, int32_t value) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetMinRelRankForDial(value);
    }

    bool GetShouldUseSkyrimNetDecisions(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::GetSkyrimNetGateway().ShouldUseDecisions();
    }

    std::string DumpStoredData(RE::StaticFunctionTag*, std::string tag) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::GetUiBridgeGateway().DumpStoredData(tag);
    }

}  // namespace MARAS::PapyrusInterface
