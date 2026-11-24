#include "maras/app/SettingsCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SettingsKeyParser.h"
#include "maras/state/WorldTables.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

namespace MARAS::App {

    namespace {

        constexpr std::int32_t kMinLogLevel = 0;
        constexpr std::int32_t kMaxLogLevel = 5;

    }  // namespace

    SettingsCoordinator& SettingsCoordinator::GetSingleton() {
        static SettingsCoordinator instance;
        return instance;
    }

    void SettingsCoordinator::Load() {
        State::SettingsData baseline;

        const std::filesystem::path iniPath = std::filesystem::current_path() / "Data/SKSE/Plugins/MARAS.ini";
        std::error_code ec;
        if (!std::filesystem::exists(iniPath, ec)) {
            std::filesystem::create_directories(iniPath.parent_path(), ec);
            std::ofstream file(iniPath);
            if (file.is_open()) {
                file << "[Hotkeys]\n";
                file << "ToggleUI = Alt+B\n";
                file << "\n[Logging]\n";
                file << "LogLevel = 1\n";
                file << "\n[Localization]\n";
                file << "Language = auto\n";
            }
        }

        char buffer[256] = {};
        GetPrivateProfileStringA("Hotkeys", "ToggleUI", "Alt+B", buffer, sizeof(buffer), iniPath.string().c_str());
        ParseKeyCombo(std::string(buffer), baseline);

        char langBuffer[64] = {};
        GetPrivateProfileStringA("Localization", "Language", "auto", langBuffer, sizeof(langBuffer), iniPath.string().c_str());
        std::string iniLanguage = Foundation::StringUtils::TrimStd(langBuffer);
        if (iniLanguage.empty() || Foundation::StringUtils::CaseInsensitiveEquals(iniLanguage, "auto")) {
            iniLanguage = "auto";
        }
        baseline.language = iniLanguage;

        const std::int32_t rawLogLevel = GetPrivateProfileIntA(
            "Logging", "LogLevel", baseline.defaultLogLevel, iniPath.string().c_str());
        const std::int32_t iniLogLevel = std::clamp(rawLogLevel, kMinLogLevel, kMaxLogLevel);
        baseline.defaultLogLevel = iniLogLevel;
        baseline.logLevel = iniLogLevel;

        Engine::ApplyLogLevel(iniLogLevel);

        auto& store = State::WorldStore::GetSingleton();
        store.SetIniBaselineSettings(baseline);
        store.Modify([&baseline, iniLogLevel](State::WorldState& ws) {
            auto& s = ws.settings.Write();
            s.toggleUIKey = baseline.toggleUIKey;
            s.toggleUIModCtrl = baseline.toggleUIModCtrl;
            s.toggleUIModShift = baseline.toggleUIModShift;
            s.toggleUIModAlt = baseline.toggleUIModAlt;
            s.language = baseline.language;
            s.defaultLogLevel = iniLogLevel;
            s.logLevel = iniLogLevel;
        });

        Engine::LocalizationService::GetSingleton().Initialize(baseline.language);

        MARAS_LOG_INFO("[SettingsCoordinator] Loaded INI settings (logLevel={}, language='{}', hotkey={:02X}, alt={}, ctrl={}, shift={})",
                       iniLogLevel, baseline.language, baseline.toggleUIKey, baseline.toggleUIModAlt,
                       baseline.toggleUIModCtrl, baseline.toggleUIModShift);
    }

    std::int32_t SettingsCoordinator::GetStartDialGender() const {
        return GlobalVarReconciler::GetSingleton().GetStartDialGender();
    }

    void SettingsCoordinator::SetStartDialGender(std::int32_t value) {
        GlobalVarReconciler::GetSingleton().SetStartDialGender(value);
    }

    bool SettingsCoordinator::GetEnablePolygamy() const {
        return Snapshot()->enablePlayerPolygamy;
    }

    void SettingsCoordinator::SetEnablePolygamy(bool value) {
        std::size_t allowedAffairs = 0;
        bool changed = false;

        State::WorldStore::GetSingleton().Modify([value, &allowedAffairs, &changed](State::WorldState& ws) {
            auto& s = ws.settings.Write();
            changed = (s.enablePlayerPolygamy != value);
            s.enablePlayerPolygamy = value;
            if (value) {
                allowedAffairs = Domain::Relationships::MarkPlayerCheatingAllowedOnPolygamy(ws);
            }
        });

        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
        FactionReconciler::GetSingleton().ReconcilePlayer();
        if (changed || allowedAffairs > 0) {
            FactionReconciler::GetSingleton().ReconcileAllOnLoad();
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }

        MARAS_LOG_INFO("[SettingsCoordinator] SetEnablePolygamy -> {} (allowedAffairs={})", value, allowedAffairs);
    }

    void SettingsCoordinator::SetDisablePolygamyQuest(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.settings.Write().disablePolygamyQuest = value;
        });

        if (value) {
            EmitModCallbackEvent("maras_stop_polygamy_quest", "", 0.0f, RE::FormID{ 0 });
        }
        MARAS_LOG_INFO("[SettingsCoordinator] SetDisablePolygamyQuest -> {}", value);
    }

    std::int32_t SettingsCoordinator::GetMinRelRankForDial() const {
        return GlobalVarReconciler::GetSingleton().GetMinRelRankForDial();
    }

    void SettingsCoordinator::SetMinRelRankForDial(std::int32_t value) {
        GlobalVarReconciler::GetSingleton().SetMinRelRankForDial(value);
    }

    bool SettingsCoordinator::GetPreferVanillaAudio() const {
        return GlobalVarReconciler::GetSingleton().GetPreferVanillaAudio();
    }

    void SettingsCoordinator::SetPreferVanillaAudio(bool value) {
        GlobalVarReconciler::GetSingleton().SetPreferVanillaAudio(value);
    }

    bool SettingsCoordinator::GetAllowAIDial() const {
        return GlobalVarReconciler::GetSingleton().GetAllowAIDial();
    }

    void SettingsCoordinator::SetAllowAIDial(bool value) {
        GlobalVarReconciler::GetSingleton().SetAllowAIDial(value);
    }

    void SettingsCoordinator::SetSimulateNoSkyrimNet(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.settings.Write().simulateNoSkyrimNet = value;
        });
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        MARAS_LOG_INFO("[SettingsCoordinator] SetSimulateNoSkyrimNet -> {}", value);
    }

    void SettingsCoordinator::SetGlobalCritterBanned(bool banned) {
        State::WorldStore::GetSingleton().Modify([banned](State::WorldState& ws) {
            ws.settings.Write().bGlobalBanCritter = banned;
        });
        MARAS_LOG_INFO("[SettingsCoordinator] SetGlobalCritterBanned -> {}", banned);
    }

    void SettingsCoordinator::SetGlobalPetBanned(bool banned) {
        State::WorldStore::GetSingleton().Modify([banned](State::WorldState& ws) {
            ws.settings.Write().bGlobalBanPet = banned;
        });
        MARAS_LOG_INFO("[SettingsCoordinator] SetGlobalPetBanned -> {}", banned);
    }

    void SettingsCoordinator::SetLanguage(std::string_view code) {
        std::string normalized = Foundation::StringUtils::TrimStd(code);
        if (normalized.empty() || Foundation::StringUtils::CaseInsensitiveEquals(normalized, "auto")) {
            normalized = "auto";
        }

        auto& store = State::WorldStore::GetSingleton();
        State::SettingsData baseline = store.GetIniBaselineSettings();
        baseline.language = normalized;
        store.SetIniBaselineSettings(baseline);

        store.Modify([&normalized](State::WorldState& ws) {
            ws.settings.Write().language = normalized;
        });

        const std::filesystem::path iniPath = std::filesystem::current_path() / "Data/SKSE/Plugins/MARAS.ini";
        std::error_code ec;
        std::filesystem::create_directories(iniPath.parent_path(), ec);
        WritePrivateProfileStringA("Localization", "Language", normalized.c_str(), iniPath.string().c_str());

        Engine::LocalizationService::GetSingleton().Reload(normalized);
        Engine::NamePoolLoader::GetSingleton().Initialize();
        GetUiBridgeGateway().SendLocaleData();

        MARAS_LOG_INFO("[SettingsCoordinator] SetLanguage -> '{}' (resolved='{}')",
                       normalized,
                       Engine::LocalizationService::GetSingleton().Snapshot()->GetResolvedCode());
    }

    void SettingsCoordinator::ReloadLocalization() {
        const std::string selected = GetLanguage();
        Engine::LocalizationService::GetSingleton().Reload(selected);
        Engine::NamePoolLoader::GetSingleton().Initialize();
        GetUiBridgeGateway().SendLocaleData();

        MARAS_LOG_INFO("[SettingsCoordinator] Reloaded localization (selected='{}', resolved='{}')",
                       selected,
                       Engine::LocalizationService::GetSingleton().Snapshot()->GetResolvedCode());
    }

    void SettingsCoordinator::ApplyPostLoadEngineSideEffects() {
        const auto s = Snapshot();
        Engine::ApplyLogLevel(s->logLevel);
        GlobalVarReconciler::GetSingleton().SyncExternalPolygamyGlobalToStore();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
    }

    void SettingsCoordinator::RevertGameSettings() {
        auto& store = State::WorldStore::GetSingleton();
        State::SettingsData reverted = store.GetIniBaselineSettings();
        reverted.logLevel = std::clamp(reverted.defaultLogLevel, kMinLogLevel, kMaxLogLevel);

        store.Modify([&reverted](State::WorldState& ws) {
            ws.settings.Reset(reverted);
        });

        Engine::ApplyLogLevel(reverted.logLevel);
        MARAS_LOG_INFO("[SettingsCoordinator] Reverted runtime game settings to INI baseline (logLevel={})",
                       reverted.logLevel);
    }

    nlohmann::json SettingsCoordinator::DumpJson() const {
        const auto s = Snapshot();
        nlohmann::json j = nlohmann::json::object();

        State::ForEachPersistedSettingField([&](const char* fieldName, auto memberPtr, bool) {
            const auto& val = (*s).*memberPtr;
            using FieldT = std::decay_t<decltype(val)>;
            if constexpr (std::is_enum_v<FieldT>) {
                j[fieldName] = static_cast<std::int32_t>(val);
            } else {
                j[fieldName] = val;
            }
        });

        j["defaultLogLevel"] = s->defaultLogLevel;
        j["confirmAcceptProposal"] = s->confirmStatusChange_Marry;
        j["confirmBreakupEngagement"] = s->confirmBreakup_Engagement;
        j["confirmLowAffectionDivorce"] = s->confirmDivorceDuringLowAffectionQuest;

        j["toggleUIKey"] = s->toggleUIKey;
        j["toggleUIModCtrl"] = s->toggleUIModCtrl;
        j["toggleUIModShift"] = s->toggleUIModShift;
        j["toggleUIModAlt"] = s->toggleUIModAlt;
        j["language"] = s->language;
        j["resolvedLanguage"] = Engine::LocalizationService::GetSingleton().Snapshot()->GetResolvedCode();
        j["enableConfirmationModal"] = s->enableConfirmationModal;
        j["actionsCooldown"] = s->actionsCooldown;
        j["actionsDeclineCooldown"] = s->actionsDeclineCooldown;

        j["startDialGender"] = GetStartDialGender();
        j["enablePolygamy"] = GetEnablePolygamy();
        j["minRelRankForDial"] = GetMinRelRankForDial();
        j["preferVanillaAudio"] = GetPreferVanillaAudio();
        j["allowAIDial"] = GetAllowAIDial();
        return j;
    }

    bool SettingsCoordinator::GetBoolByKey(std::string_view key) const {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "enablepolygamy" || lower == "enableplayerpolygamy") {
            return GetEnablePolygamy();
        }
        if (lower == "disablepolygamyquest") {
            return GetDisablePolygamyQuest();
        }
        if (lower == "simulatenoskyrimnet") {
            return GetSimulateNoSkyrimNet();
        }
        if (lower == "prefervanillaaudio") {
            return GetPreferVanillaAudio();
        }
        if (lower == "allowaidial") {
            return GetAllowAIDial();
        }

        bool outVal = false;
        if (State::TryGetBoolSetting(*Snapshot(), lower, outVal)) {
            return outVal;
        }
        MARAS_LOG_WARN("[SettingsCoordinator] GetBoolByKey: unknown setting key '{}'", key);
        return false;
    }

    bool SettingsCoordinator::SetBoolByKey(std::string_view key, bool value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "enablepolygamy" || lower == "enableplayerpolygamy") {
            SetEnablePolygamy(value);
            return true;
        }
        if (lower == "disablepolygamyquest") {
            SetDisablePolygamyQuest(value);
            return true;
        }
        if (lower == "simulatenoskyrimnet") {
            SetSimulateNoSkyrimNet(value);
            return true;
        }
        if (lower == "prefervanillaaudio") {
            SetPreferVanillaAudio(value);
            return true;
        }
        if (lower == "allowaidial") {
            SetAllowAIDial(value);
            return true;
        }

        bool applied = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            applied = State::TrySetBoolSetting(ws.settings.Write(), lower, value);
        });
        if (!applied) {
            MARAS_LOG_WARN("[SettingsCoordinator] SetBoolByKey: unknown setting key '{}'", key);
        }
        return applied;
    }

    std::int32_t SettingsCoordinator::GetIntByKey(std::string_view key) const {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "startdialgender") {
            return GetStartDialGender();
        }
        if (lower == "minrelrankfordial") {
            return GetMinRelRankForDial();
        }

        std::int32_t outVal = 0;
        if (State::TryGetIntSetting(*Snapshot(), lower, outVal)) {
            return outVal;
        }
        MARAS_LOG_WARN("[SettingsCoordinator] GetIntByKey: unknown setting key '{}'", key);
        return 0;
    }

    bool SettingsCoordinator::SetIntByKey(std::string_view key, std::int32_t value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "startdialgender") {
            SetStartDialGender(value);
            return true;
        }
        if (lower == "minrelrankfordial") {
            SetMinRelRankForDial(value);
            return true;
        }
        if (lower == "loglevel") {
            const std::int32_t clamped = std::clamp(value, kMinLogLevel, kMaxLogLevel);
            State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
                ws.settings.Write().logLevel = clamped;
            });
            Engine::ApplyLogLevel(clamped);
            return true;
        }

        State::SettingWriteStatus status = State::SettingWriteStatus::UnknownKey;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            status = State::TrySetIntSetting(ws.settings.Write(), lower, value);
        });
        if (status == State::SettingWriteStatus::UnknownKey) {
            MARAS_LOG_WARN("[SettingsCoordinator] SetIntByKey: unknown setting key '{}'", key);
            return false;
        }
        if (status == State::SettingWriteStatus::OutOfRange) {
            MARAS_LOG_WARN("[SettingsCoordinator] SetIntByKey: out-of-range value {} for key '{}'", value, key);
            return false;
        }
        return true;
    }

    float SettingsCoordinator::GetFloatByKey(std::string_view key) const {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "scenescooldownhours") {
            return Snapshot()->scenesCooldownHours;
        }

        float outVal = 0.0f;
        if (State::TryGetFloatSetting(*Snapshot(), lower, outVal)) {
            return outVal;
        }
        MARAS_LOG_WARN("[SettingsCoordinator] GetFloatByKey: unknown setting key '{}'", key);
        return 0.0f;
    }

    bool SettingsCoordinator::SetFloatByKey(std::string_view key, float value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "scenescooldownhours") {
            const float clamped = std::max(0.0f, value);
            State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
                ws.settings.Write().scenesCooldownHours = clamped;
                ws.cooldowns.Write().savedBanterCooldownHours = clamped;
            });
            return true;
        }

        State::SettingWriteStatus status = State::SettingWriteStatus::UnknownKey;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            status = State::TrySetFloatSetting(ws.settings.Write(), lower, value);
        });
        if (status != State::SettingWriteStatus::Ok) {
            MARAS_LOG_WARN("[SettingsCoordinator] SetFloatByKey: unknown setting key '{}'", key);
            return false;
        }
        return true;
    }

    void SettingsCoordinator::ParseKeyCombo(const std::string& comboStr, State::SettingsData& out) {
        const auto res = State::ParseKeyCombo(comboStr, out);
        if (res.fellBackToDefault) {
            MARAS_LOG_WARN("[SettingsCoordinator] Invalid ToggleUI hotkey '{}'; falling back to Alt+B", comboStr);
        }
    }

    std::uint32_t SettingsCoordinator::ParseSingleKey(const std::string& keyStr) {
        return State::ParseSingleKey(keyStr);
    }

}  // namespace MARAS::App
