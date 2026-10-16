#include "utils/Settings.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"
#include "core/LoggingService.h"
#include "core/BanterCoordinatorService.h"
#include "core/NPCRelationshipManager.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "utils/ActorUtils.h"
#include <Windows.h>

namespace MARAS {

    namespace {
        constexpr uint8_t kSettingsVersionMarker = 0xFF;
        constexpr uint32_t kSettingsSubVersion = 1;

        const std::unordered_map<std::string, uint32_t> kKeyNameToDxCode = {
            {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E},
            {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},
            {"F9", 0x43}, {"F10", 0x44}, {"F11", 0x57}, {"F12", 0x58},
            {"A", 0x1E}, {"B", 0x30}, {"C", 0x2E}, {"D", 0x20},
            {"E", 0x12}, {"F", 0x21}, {"G", 0x22}, {"H", 0x23},
            {"I", 0x17}, {"J", 0x24}, {"K", 0x25}, {"L", 0x26},
            {"M", 0x32}, {"N", 0x31}, {"O", 0x18}, {"P", 0x19},
            {"Q", 0x10}, {"R", 0x13}, {"S", 0x1F}, {"T", 0x14},
            {"U", 0x16}, {"V", 0x2F}, {"W", 0x11}, {"X", 0x2D},
            {"Y", 0x15}, {"Z", 0x2C},
            {"0", 0x0B}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04},
            {"4", 0x05}, {"5", 0x06}, {"6", 0x07}, {"7", 0x08},
            {"8", 0x09}, {"9", 0x0A},
            {"TAB", 0x0F}, {"SPACE", 0x39}, {"ESC", 0x01}, {"ESCAPE", 0x01}
        };

        void EnsureDefaultIniExists(const std::filesystem::path& iniPath) {
            if (std::filesystem::exists(iniPath)) return;

            std::error_code ec;
            std::filesystem::create_directories(iniPath.parent_path(), ec);

            std::ofstream iniFile(iniPath);
            if (iniFile.is_open()) {
                iniFile << "; =============================================================================\n";
                iniFile << "; M.A.R.A.S Configuration File\n";
                iniFile << "; =============================================================================\n\n";
                iniFile << "[Hotkeys]\n";
                iniFile << "; Key combination for toggling the M.A.R.A.S Prisma UI\n";
                iniFile << "; Supports modifiers like Shift, Ctrl, Alt (e.g. Shift+F10, Ctrl+M, Ctrl+Shift+F10)\n";
                iniFile << "; Also supports key names (F10, M), hex codes (0x44), or DX scan codes (68)\n";
                iniFile << "; Set to 0 to disable\n";
                iniFile << "; Default: Alt+B\n";
                iniFile << "ToggleUI = Alt+B\n\n";
                iniFile << "[Logging]\n";
                iniFile << "; Default log level: 0 = Trace, 1 = Debug, 2 = Info, 3 = Warning, 4 = Error, 5 = Off\n";
                iniFile << "; Default: 2\n";
                iniFile << "LogLevel = 1\n\n";
                MARAS_LOG_INFO("Created default INI configuration at {}", iniPath.string());
            }
        }

        // Single canonical table of all co-save persisted settings in exact binary serialization order.
        // Used by SaveGameSettings, LoadGameSettings, RevertGameSettings, and DumpJson so fields and
        // default values never drift out of sync.
        template <typename Visitor>
        void ForEachPersistedSetting(Visitor&& visit) {
            // Required initial v4 header block (10 fields)
            visit("skipWedding", &Settings::skipWedding, true);
            visit("alwaysSuccessMarriage", &Settings::alwaysSuccessMarriage, true);
            visit("logDestination", &Settings::logDestination, true);
            visit("logLevel", &Settings::logLevel, true);
            visit("affectionDecayMultiplier", &Settings::affectionDecayMultiplier, true);
            visit("confirmStatusChange_Marry", &Settings::confirmStatusChange_Marry, true);
            visit("confirmBreakup_Engagement", &Settings::confirmBreakup_Engagement, true);
            visit("confirmDivorce", &Settings::confirmDivorce, true);
            visit("confirmDivorceDuringLowAffectionQuest", &Settings::confirmDivorceDuringLowAffectionQuest, true);
            visit("migratedFromStorageUtil", &Settings::migratedFromStorageUtil, true);

            // Optional tail fields (72 fields, in exact v4 binary stream order)
            visit("initialRelationshipScanDone", &Settings::initialRelationshipScanDone, false);
            visit("childCustodyGenderPolicy", &Settings::childCustodyGenderPolicy, false);
            visit("childCustodyPlayerPolicy", &Settings::childCustodyPlayerPolicy, false);
            visit("childCustodyPrioritizeBioParent", &Settings::childCustodyPrioritizeBioParent, false);
            visit("bGlobalBanCritter", &Settings::bGlobalBanCritter, false);
            visit("bGlobalBanPet", &Settings::bGlobalBanPet, false);
            visit("enableConfirmPlayerNpc", &Settings::enableConfirmPlayerNpc, false);
            visit("enableConfirmNpcNpc", &Settings::enableConfirmNpcNpc, false);
            visit("confirmStatusChange_Dating", &Settings::confirmStatusChange_Dating, false);
            visit("confirmStatusChange_Engagement", &Settings::confirmStatusChange_Engagement, false);
            visit("confirmStatusChange_Lover", &Settings::confirmStatusChange_Lover, false);
            visit("confirmBreakup_Dating", &Settings::confirmBreakup_Dating, false);
            visit("confirmBreakup_Lover", &Settings::confirmBreakup_Lover, false);
            visit("confirmOpenRelationship", &Settings::confirmOpenRelationship, false);
            visit("confirmCloseRelationship", &Settings::confirmCloseRelationship, false);
            visit("skyrimnetActionRadius", &Settings::skyrimnetActionRadius, false);
            visit("matchmakingEnabled", &Settings::matchmakingEnabled, false);
            visit("matchmakingIntervalDays", &Settings::matchmakingIntervalDays, false);
            visit("matchmakingMaxSuggestions", &Settings::matchmakingMaxSuggestions, false);
            visit("matchmakingAllowBreakups", &Settings::matchmakingAllowBreakups, false);
            visit("matchmakingAllowOpenClose", &Settings::matchmakingAllowOpenClose, false);
            visit("matchmakingOrientation", &Settings::matchmakingOrientation, false);
            visit("matchmakingAllowCheating", &Settings::matchmakingAllowCheating, false);
            visit("matchmakingNpcCooldownDays", &Settings::matchmakingNpcCooldownDays, false);
            visit("matchmakingExcludePlayerActivePartners", &Settings::matchmakingExcludePlayerActivePartners, false);
            visit("matchmakingUseLLM", &Settings::matchmakingUseLLM, false);
            visit("diceRollChanceNewDating", &Settings::diceRollChanceNewDating, false);
            visit("diceRollChanceNewEngaged", &Settings::diceRollChanceNewEngaged, false);
            visit("diceRollChanceNewLoverAllowed", &Settings::diceRollChanceNewLoverAllowed, false);
            visit("diceRollChanceNewLoverCheating", &Settings::diceRollChanceNewLoverCheating, false);
            visit("diceRollChanceAdvance", &Settings::diceRollChanceAdvance, false);
            visit("diceRollChanceBreakup", &Settings::diceRollChanceBreakup, false);
            visit("diceRollChanceOpenClose", &Settings::diceRollChanceOpenClose, false);
            visit("simulateNoSkyrimNet", &Settings::simulateNoSkyrimNet, false);
            visit("matchmakingEvaluationStep", &Settings::matchmakingEvaluationStep, false);
            visit("matchmakingGenerateMemories", &Settings::matchmakingGenerateMemories, false);
            visit("paternityBaseChance", &Settings::paternityBaseChance, false);
            visit("paternityDailyGrowth", &Settings::paternityDailyGrowth, false);
            visit("paternityMaxChance", &Settings::paternityMaxChance, false);
            visit("paternityPhenotypeBaseChance", &Settings::paternityPhenotypeBaseChance, false);
            visit("paternityPhenotypeDailyGrowth", &Settings::paternityPhenotypeDailyGrowth, false);
            visit("paternityPhenotypeMaxChance", &Settings::paternityPhenotypeMaxChance, false);
            visit("paternityPhenotypeBirthChance", &Settings::paternityPhenotypeBirthChance, false);
            visit("paternityNonSkyrimNetRevealChance", &Settings::paternityNonSkyrimNetRevealChance, false);
            visit("paternityBreakupChance", &Settings::paternityBreakupChance, false);
            visit("paternityBioStepUpChance", &Settings::paternityBioStepUpChance, false);
            visit("paternityForceDiceRolls", &Settings::paternityForceDiceRolls, false);
            visit("enableScenes", &Settings::enableScenes, false);
            visit("enableScenes_Lover", &Settings::enableScenes_Lover, false);
            visit("enableScenes_Dating", &Settings::enableScenes_Dating, false);
            visit("enableScenes_Engaged", &Settings::enableScenes_Engaged, false);
            visit("enableScenes_Married", &Settings::enableScenes_Married, false);
            visit("enableScenes_OpenRelationship", &Settings::enableScenes_OpenRelationship, false);
            visit("enableScenes_Cheating", &Settings::enableScenes_Cheating, false);
            visit("enableScenes_CheatingDiscovered", &Settings::enableScenes_CheatingDiscovered, false);
            visit("enableScenes_CheatingCaught", &Settings::enableScenes_CheatingCaught, false);
            visit("enableScenes_LocTypeTemple", &Settings::enableScenes_LocTypeTemple, false);
            visit("enableScenes_LocTypeStore", &Settings::enableScenes_LocTypeStore, false);
            visit("enableScenes_LocTypeStewardsDwelling", &Settings::enableScenes_LocTypeStewardsDwelling, false);
            visit("enableScenes_LocTypeInn", &Settings::enableScenes_LocTypeInn, false);
            visit("enableScenes_LocTypeHouse", &Settings::enableScenes_LocTypeHouse, false);
            visit("enableScenes_LocTypeBarracks", &Settings::enableScenes_LocTypeBarracks, false);
            visit("enableScenes_LocTypeFarm", &Settings::enableScenes_LocTypeFarm, false);
            visit("enableScenes_LocTypeGuild", &Settings::enableScenes_LocTypeGuild, false);
            visit("enableScenes_LocTypePlayerHouse", &Settings::enableScenes_LocTypePlayerHouse, false);
            visit("enableScenes_BYOH_LocTypeHomestead", &Settings::enableScenes_BYOH_LocTypeHomestead, false);
            visit("enableScenes_LocTypeTown", &Settings::enableScenes_LocTypeTown, false);
            visit("enableScenes_LocTypeSettlement", &Settings::enableScenes_LocTypeSettlement, false);
            visit("enableScenes_LocTypeOrcStronghold", &Settings::enableScenes_LocTypeOrcStronghold, false);
            visit("enableScenes_LocTypeCity", &Settings::enableScenes_LocTypeCity, false);
            visit("enableScenes_LocTypeLumberMill", &Settings::enableScenes_LocTypeLumberMill, false);
            visit("scenesCooldownHours", &Settings::scenesCooldownHours, false);
        }
    }

    uint32_t Settings::ParseSingleKey(const std::string& keyStr) {
        if (keyStr.empty()) return 0;

        std::string upperStr = StringUtils::ToUpperStd(keyStr);

        auto it = kKeyNameToDxCode.find(upperStr);
        if (it != kKeyNameToDxCode.end()) {
            return it->second;
        }

        std::string_view sv(upperStr);
        int base = 10;
        if (sv.starts_with("0X")) {
            sv.remove_prefix(2);
            base = 16;
        }

        uint32_t parsed = 0;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed, base);
        if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
            return parsed;
        }
        return 0;
    }

    void Settings::ParseKeyCombo(const std::string& comboStr) {
        toggleUIKey = 0;
        toggleUIModCtrl = false;
        toggleUIModShift = false;
        toggleUIModAlt = false;

        std::string trimmedCombo = StringUtils::TrimStd(comboStr);
        if (trimmedCombo.empty()) return;

        std::stringstream ss(trimmedCombo);
        std::string token;
        while (std::getline(ss, token, '+')) {
            std::string part = StringUtils::ToUpperStd(StringUtils::TrimStd(token));
            if (part.empty()) continue;

            if (part == "CTRL" || part == "CONTROL" || part == "LCTRL" || part == "RCTRL") {
                toggleUIModCtrl = true;
            } else if (part == "SHIFT" || part == "LSHIFT" || part == "RSHIFT") {
                toggleUIModShift = true;
            } else if (part == "ALT" || part == "LALT" || part == "RALT") {
                toggleUIModAlt = true;
            } else {
                toggleUIKey = ParseSingleKey(part);
            }
        }
    }

    void Settings::Load() {
        std::filesystem::path iniPath =
            std::filesystem::current_path() / "Data/SKSE/Plugins/MARAS.ini";

        EnsureDefaultIniExists(iniPath);

        const std::string pathStr = iniPath.string();
        char buf[64] = {};

        GetPrivateProfileStringA("Hotkeys", "ToggleUI", "Alt+B", buf, sizeof(buf), pathStr.c_str());
        std::string toggleVal = buf;

        int32_t iniLogLevel = GetPrivateProfileIntA("Logging", "LogLevel", 1, pathStr.c_str());
        if (iniLogLevel < 0 || iniLogLevel > 5) {
            iniLogLevel = 1;
        }

        {
            std::unique_lock lock(settingsMutex_);
            ParseKeyCombo(toggleVal);
            defaultLogLevel = iniLogLevel;
            logLevel = defaultLogLevel;
        }

        LoggingService::GetSingleton().SetLogLevel(iniLogLevel);

        MARAS_LOG_INFO("Settings loaded from {}", pathStr);
        MARAS_LOG_INFO("  ToggleUI = '{}' -> Key 0x{:02X} (Ctrl={}, Shift={}, Alt={})",
                       toggleVal, toggleUIKey, toggleUIModCtrl, toggleUIModShift, toggleUIModAlt);
        MARAS_LOG_INFO("  Logging: Default LogLevel={}", defaultLogLevel);
    }

    int32_t Settings::GetStartDialGender() const {
        auto* g = FormCache::GetSingleton().GetStartDialGenderGlobal();
        return g ? static_cast<int32_t>(g->value) : 0;
    }

    void Settings::SetStartDialGender(int32_t value) {
        if (auto* g = FormCache::GetSingleton().GetStartDialGenderGlobal()) {
            g->value = static_cast<float>(value);
        }
    }

    bool Settings::GetEnablePolygamy() const {
        auto* g = FormCache::GetSingleton().GetEnablePolygamyGlobal();
        return g ? (g->value != 0.0f) : true;
    }

    void Settings::SetEnablePolygamy(bool value) {
        if (auto* g = FormCache::GetSingleton().GetEnablePolygamyGlobal()) {
            g->value = value ? 1.0f : 0.0f;
        }
        if (value) {
            NPCRelationshipManager::GetSingleton().MarkPlayerCheatingAllowedOnPolygamy();
            FactionService::GetSingleton().SanitizePlayerMarriedFaction();
        } else {
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            if (!relMgr.GetAllMarried(kPlayerFormID).empty()) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* marriedFaction = FormCache::GetSingleton().GetMarriedFaction()) {
                        FactionService::InternalFactionScope scope;
                        Utils::AddToFaction(player, marriedFaction, 0);
                    }
                }
            }
        }
        NPCRelationshipManager::GetSingleton().UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
    }

    int32_t Settings::GetMinRelRankForDial() const {
        auto* g = FormCache::GetSingleton().GetMinRelRankForDialGlobal();
        return g ? static_cast<int32_t>(g->value) : -4;
    }

    void Settings::SetMinRelRankForDial(int32_t value) {
        if (auto* g = FormCache::GetSingleton().GetMinRelRankForDialGlobal()) {
            g->value = static_cast<float>(value);
        }
    }

    void Settings::SyncSimulateNoSkyrimNetGlobal(bool value) const {
        if (auto* g = FormCache::GetSingleton().GetSkyrimNetInstalledGlobal()) {
            g->value = value ? 0.0f : ((GetModuleHandleW(L"SkyrimNet.dll") != nullptr) ? 1.0f : 0.0f);
        }
    }

    void Settings::SetSimulateNoSkyrimNet(bool value) {
        {
            std::unique_lock lock(settingsMutex_);
            simulateNoSkyrimNet = value;
        }
        SyncSimulateNoSkyrimNetGlobal(value);
        MARAS_LOG_INFO("Settings::SetSimulateNoSkyrimNet - simulateNoSkyrimNet set to {} (global 0xC4 updated)", value);
    }

    bool Settings::SaveGameSettings(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        std::shared_lock lock(settingsMutex_);

        if (!serialization->WriteRecordData(kSettingsVersionMarker) ||
            !serialization->WriteRecordData(kSettingsSubVersion)) {
            MARAS_LOG_ERROR("Settings::SaveGameSettings - failed to write version header");
            return false;
        }

        bool ok = true;
        ForEachPersistedSetting([&](std::string_view, auto memberPtr, bool) {
            if (!ok) return;
            const auto& field = this->*memberPtr;
            using FieldType = std::decay_t<decltype(field)>;

            if constexpr (std::is_same_v<FieldType, bool>) {
                const uint8_t raw = field ? 1 : 0;
                ok = serialization->WriteRecordData(raw);
            } else if constexpr (std::is_enum_v<FieldType>) {
                const uint8_t raw = static_cast<uint8_t>(field);
                ok = serialization->WriteRecordData(raw);
            } else {
                ok = serialization->WriteRecordData(field);
            }
        });

        if (!ok) {
            MARAS_LOG_ERROR("Settings::SaveGameSettings - failed to write data");
            return false;
        }

        MARAS_LOG_INFO("Settings::SaveGameSettings - successfully saved settings (subVersion={}, confirmation modal: playerNpc={}, npcNpc={}, dating={}, engagement={}, lover={}, marry={}, breakupDating={}, breakupEngagement={}, breakupLover={}, divorce={}, lowAffDivorce={}, openRel={}, closeRel={})",
                       kSettingsSubVersion, enableConfirmPlayerNpc, enableConfirmNpcNpc, confirmStatusChange_Dating, confirmStatusChange_Engagement,
                       confirmStatusChange_Lover, confirmStatusChange_Marry, confirmBreakup_Dating, confirmBreakup_Engagement,
                       confirmBreakup_Lover, confirmDivorce, confirmDivorceDuringLowAffectionQuest, confirmOpenRelationship, confirmCloseRelationship);
        return true;
    }

    bool Settings::LoadGameSettings(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        int32_t loadedLogLevel = defaultLogLevel;
        bool loadedSimNoSkyrimNet = false;

        {
            std::unique_lock lock(settingsMutex_);
            RevertGameSettingsUnlocked();

            uint8_t firstByte = 0;
            if (!serialization->ReadRecordData(firstByte)) {
                MARAS_LOG_WARN("Settings::LoadGameSettings - failed to read initial byte");
                return false;
            }

            bool hasVersionHeader = (firstByte == kSettingsVersionMarker);
            if (hasVersionHeader) {
                uint32_t subVersion = 0;
                if (!serialization->ReadRecordData(subVersion) || subVersion < 1 || subVersion > kSettingsSubVersion) {
                    MARAS_LOG_ERROR("Settings::LoadGameSettings - invalid or unsupported settings sub-version {}", subVersion);
                    RevertGameSettingsUnlocked();
                    return false;
                }
            }

            bool isFirstField = true;
            bool requiredReadFailed = false;
            bool streamEnded = false;

            ForEachPersistedSetting([&](std::string_view key, auto memberPtr, bool required) {
                if (requiredReadFailed || streamEnded) return;
                auto& field = this->*memberPtr;
                using FieldType = std::decay_t<decltype(field)>;

                bool readOk = false;
                if (isFirstField && !hasVersionHeader) {
                    isFirstField = false;
                    if constexpr (std::is_same_v<FieldType, bool>) {
                        field = (firstByte != 0);
                        readOk = true;
                    }
                } else {
                    isFirstField = false;
                    if constexpr (std::is_same_v<FieldType, bool>) {
                        uint8_t raw = 0;
                        if (serialization->ReadRecordData(raw)) {
                            field = (raw != 0);
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, ChildCustodyGenderPolicy>) {
                        uint8_t raw = 0;
                        if (serialization->ReadRecordData(raw)) {
                            if (raw <= static_cast<uint8_t>(ChildCustodyGenderPolicy::StayWithFather)) {
                                field = static_cast<ChildCustodyGenderPolicy>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, ChildCustodyPlayerPolicy>) {
                        uint8_t raw = 0;
                        if (serialization->ReadRecordData(raw)) {
                            if (raw <= static_cast<uint8_t>(ChildCustodyPlayerPolicy::NeverWithPlayer)) {
                                field = static_cast<ChildCustodyPlayerPolicy>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, MatchmakingOrientation>) {
                        uint8_t raw = 0;
                        if (serialization->ReadRecordData(raw)) {
                            if (raw <= static_cast<uint8_t>(MatchmakingOrientation::HeteroAndLesbian)) {
                                field = static_cast<MatchmakingOrientation>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, float>) {
                        float val = 0.0f;
                        if (serialization->ReadRecordData(val)) {
                            if (key != "skyrimnetActionRadius" || val > 0.0f) {
                                field = val;
                            }
                            readOk = true;
                        }
                    } else {
                        FieldType val{};
                        if (serialization->ReadRecordData(val)) {
                            field = val;
                            readOk = true;
                        }
                    }
                }

                if (!readOk) {
                    if (required) {
                        requiredReadFailed = true;
                    } else {
                        streamEnded = true;
                    }
                }
            });

            if (requiredReadFailed) {
                MARAS_LOG_WARN("Settings::LoadGameSettings - truncated settings record; reverting to defaults");
                RevertGameSettingsUnlocked();
                return false;
            }

            if (logLevel < 0 || logLevel > 5) {
                logLevel = defaultLogLevel;
            }
            loadedLogLevel = logLevel;
            loadedSimNoSkyrimNet = simulateNoSkyrimNet;
        }

        SyncSimulateNoSkyrimNetGlobal(loadedSimNoSkyrimNet);
        LoggingService::GetSingleton().SetLogLevel(loadedLogLevel);

        MARAS_LOG_INFO("Settings::LoadGameSettings - loaded settings (logLevel={}, skipWedding={}, scanDone={}, custodyGender={}, custodyPlayer={}, custodyBio={}, globalBanCritter={}, globalBanPet={}, matchEnabled={}, matchInterval={}, matchOrientation={}, matchCheating={}, matchExcludePlayerPartners={})",
                       loadedLogLevel, skipWedding, initialRelationshipScanDone, static_cast<int>(childCustodyGenderPolicy), static_cast<int>(childCustodyPlayerPolicy), childCustodyPrioritizeBioParent, bGlobalBanCritter, bGlobalBanPet, matchmakingEnabled, matchmakingIntervalDays, static_cast<int>(matchmakingOrientation), matchmakingAllowCheating, matchmakingExcludePlayerActivePartners);
        MARAS_LOG_INFO("Settings::LoadGameSettings - loaded confirmation modal settings: playerNpc={}, npcNpc={}, dating={}, engagement={}, lover={}, marry={}, breakupDating={}, breakupEngagement={}, breakupLover={}, divorce={}, lowAffDivorce={}, openRel={}, closeRel={}",
                       enableConfirmPlayerNpc, enableConfirmNpcNpc, confirmStatusChange_Dating, confirmStatusChange_Engagement,
                       confirmStatusChange_Lover, confirmStatusChange_Marry, confirmBreakup_Dating, confirmBreakup_Engagement,
                       confirmBreakup_Lover, confirmDivorce, confirmDivorceDuringLowAffectionQuest, confirmOpenRelationship, confirmCloseRelationship);

        return true;
    }

    void Settings::RevertGameSettingsUnlocked() {
        const Settings defaults{};
        ForEachPersistedSetting([&](std::string_view, auto memberPtr, bool) {
            this->*memberPtr = defaults.*memberPtr;
        });
        logLevel = defaultLogLevel;
    }

    void Settings::RevertGameSettings() {
        int32_t revertedLogLevel = defaultLogLevel;
        {
            std::unique_lock lock(settingsMutex_);
            RevertGameSettingsUnlocked();
            revertedLogLevel = logLevel;
        }

        SyncSimulateNoSkyrimNetGlobal(false);
        LoggingService::GetSingleton().SetLogLevel(revertedLogLevel);

        MARAS_LOG_INFO("Settings::RevertGameSettings - reverted settings to defaults (logLevel={})", revertedLogLevel);
        MARAS_LOG_INFO("Settings::RevertGameSettings - reverted confirmation modal settings to defaults (playerNpc={}, npcNpc={}, dating={}, engagement={}, lover={}, marry={}, breakupDating={}, breakupEngagement={}, breakupLover={}, divorce={}, lowAffDivorce={}, openRel={}, closeRel={})",
                       enableConfirmPlayerNpc, enableConfirmNpcNpc, confirmStatusChange_Dating, confirmStatusChange_Engagement,
                       confirmStatusChange_Lover, confirmStatusChange_Marry, confirmBreakup_Dating, confirmBreakup_Engagement,
                       confirmBreakup_Lover, confirmDivorce, confirmDivorceDuringLowAffectionQuest, confirmOpenRelationship, confirmCloseRelationship);
    }

    nlohmann::json Settings::DumpJson() const {
        nlohmann::json j;
        {
            std::shared_lock lock(settingsMutex_);
            ForEachPersistedSetting([&](std::string_view key, auto memberPtr, bool) {
                const auto& field = this->*memberPtr;
                using FieldType = std::decay_t<decltype(field)>;
                if constexpr (std::is_enum_v<FieldType>) {
                    j[std::string(key)] = static_cast<uint8_t>(field);
                } else {
                    j[std::string(key)] = field;
                }
            });

            j["defaultLogLevel"] = defaultLogLevel;
            // Legacy keys for backward compatibility
            j["confirmAcceptProposal"] = confirmStatusChange_Marry;
            j["confirmBreakupEngagement"] = confirmBreakup_Engagement;
            j["confirmLowAffectionDivorce"] = confirmDivorceDuringLowAffectionQuest;
            j["toggleUIKey"] = toggleUIKey;
            j["toggleUIModCtrl"] = toggleUIModCtrl;
            j["toggleUIModShift"] = toggleUIModShift;
            j["toggleUIModAlt"] = toggleUIModAlt;
            j["enableConfirmationModal"] = enableConfirmationModal;
            j["actionsCooldown"] = actionsCooldown;
            j["actionsDeclineCooldown"] = actionsDeclineCooldown;
        }

        j["startDialGender"] = GetStartDialGender();
        j["enablePolygamy"] = GetEnablePolygamy();
        j["minRelRankForDial"] = GetMinRelRankForDial();
        return j;
    }

    namespace {
        const std::unordered_map<std::string_view, bool Settings::*>& GetBoolSettingMap() {
            static const std::unordered_map<std::string_view, bool Settings::*> kMap = {
                {"skipwedding", &Settings::skipWedding},
                {"alwayssuccessmarriage", &Settings::alwaysSuccessMarriage},
                {"alwayssuccess", &Settings::alwaysSuccessMarriage},
                {"confirmstatuschange_marry", &Settings::confirmStatusChange_Marry},
                {"confirmacceptproposal", &Settings::confirmStatusChange_Marry},
                {"confirmbreakup_engagement", &Settings::confirmBreakup_Engagement},
                {"confirmbreakupengagement", &Settings::confirmBreakup_Engagement},
                {"confirmdivorce", &Settings::confirmDivorce},
                {"confirmdivorceduringlowaffectionquest", &Settings::confirmDivorceDuringLowAffectionQuest},
                {"confirmlowaffectiondivorce", &Settings::confirmDivorceDuringLowAffectionQuest},
                {"migratedfromstorageutil", &Settings::migratedFromStorageUtil},
                {"initialrelationshipscandone", &Settings::initialRelationshipScanDone},
                {"childcustodyprioritizebioparent", &Settings::childCustodyPrioritizeBioParent},
                {"bglobalbancritter", &Settings::bGlobalBanCritter},
                {"globalbancritter", &Settings::bGlobalBanCritter},
                {"bglobalbanpet", &Settings::bGlobalBanPet},
                {"globalbanpet", &Settings::bGlobalBanPet},
                {"enableconfirmplayernpc", &Settings::enableConfirmPlayerNpc},
                {"enableconfirmnpcnpc", &Settings::enableConfirmNpcNpc},
                {"enableconfirmationmodal", &Settings::enableConfirmationModal},
                {"confirmstatuschange_dating", &Settings::confirmStatusChange_Dating},
                {"confirmstatuschange_engagement", &Settings::confirmStatusChange_Engagement},
                {"confirmstatuschange_lover", &Settings::confirmStatusChange_Lover},
                {"confirmbreakup_dating", &Settings::confirmBreakup_Dating},
                {"confirmbreakup_lover", &Settings::confirmBreakup_Lover},
                {"confirmopenrelationship", &Settings::confirmOpenRelationship},
                {"confirmcloserelationship", &Settings::confirmCloseRelationship},
                {"toggleuimodctrl", &Settings::toggleUIModCtrl},
                {"toggleuimodshift", &Settings::toggleUIModShift},
                {"toggleuimodalt", &Settings::toggleUIModAlt},
                {"matchmakingenabled", &Settings::matchmakingEnabled},
                {"matchmakingallowbreakups", &Settings::matchmakingAllowBreakups},
                {"matchmakingallowopenclose", &Settings::matchmakingAllowOpenClose},
                {"matchmakingallowcheating", &Settings::matchmakingAllowCheating},
                {"matchmakingexcludeplayeractivepartners", &Settings::matchmakingExcludePlayerActivePartners},
                {"matchmakingusellm", &Settings::matchmakingUseLLM},
                {"matchmakingevaluationstep", &Settings::matchmakingEvaluationStep},
                {"matchmakinggeneratememories", &Settings::matchmakingGenerateMemories},
                {"paternityforcedicerolls", &Settings::paternityForceDiceRolls},
                {"enablescenes", &Settings::enableScenes},
                {"enablescenes_lover", &Settings::enableScenes_Lover},
                {"enablescenes_dating", &Settings::enableScenes_Dating},
                {"enablescenes_engaged", &Settings::enableScenes_Engaged},
                {"enablescenes_married", &Settings::enableScenes_Married},
                {"enablescenes_openrelationship", &Settings::enableScenes_OpenRelationship},
                {"enablescenes_cheating", &Settings::enableScenes_Cheating},
                {"enablescenes_cheatingdiscovered", &Settings::enableScenes_CheatingDiscovered},
                {"enablescenes_cheatingcaught", &Settings::enableScenes_CheatingCaught},
                {"enablescenes_loctypetemple", &Settings::enableScenes_LocTypeTemple},
                {"enablescenes_loctypestore", &Settings::enableScenes_LocTypeStore},
                {"enablescenes_loctypestewardsdwelling", &Settings::enableScenes_LocTypeStewardsDwelling},
                {"enablescenes_loctypeinn", &Settings::enableScenes_LocTypeInn},
                {"enablescenes_loctypehouse", &Settings::enableScenes_LocTypeHouse},
                {"enablescenes_loctypebarracks", &Settings::enableScenes_LocTypeBarracks},
                {"enablescenes_loctypefarm", &Settings::enableScenes_LocTypeFarm},
                {"enablescenes_loctypeguild", &Settings::enableScenes_LocTypeGuild},
                {"enablescenes_loctypeplayerhouse", &Settings::enableScenes_LocTypePlayerHouse},
                {"enablescenes_byoh_loctypehomestead", &Settings::enableScenes_BYOH_LocTypeHomestead},
                {"enablescenes_loctypetown", &Settings::enableScenes_LocTypeTown},
                {"enablescenes_loctypesettlement", &Settings::enableScenes_LocTypeSettlement},
                {"enablescenes_loctypeorcstronghold", &Settings::enableScenes_LocTypeOrcStronghold},
                {"enablescenes_loctypecity", &Settings::enableScenes_LocTypeCity},
                {"enablescenes_loctypelumbermill", &Settings::enableScenes_LocTypeLumberMill}
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, int32_t Settings::*>& GetIntSettingMap() {
            static const std::unordered_map<std::string_view, int32_t Settings::*> kMap = {
                {"logdestination", &Settings::logDestination},
                {"actionscooldown", &Settings::actionsCooldown},
                {"actionsdeclinecooldown", &Settings::actionsDeclineCooldown},
                {"matchmakingintervaldays", &Settings::matchmakingIntervalDays},
                {"matchmakingmaxsuggestions", &Settings::matchmakingMaxSuggestions},
                {"matchmakingnpccooldowndays", &Settings::matchmakingNpcCooldownDays},
                {"dicerollchancenewdating", &Settings::diceRollChanceNewDating},
                {"dicerollchancenewengaged", &Settings::diceRollChanceNewEngaged},
                {"dicerollchancenewloverallowed", &Settings::diceRollChanceNewLoverAllowed},
                {"dicerollchancenewlovercheating", &Settings::diceRollChanceNewLoverCheating},
                {"dicerollchanceadvance", &Settings::diceRollChanceAdvance},
                {"dicerollchancebreakup", &Settings::diceRollChanceBreakup},
                {"dicerollchanceopenclose", &Settings::diceRollChanceOpenClose},
                {"paternitybasechance", &Settings::paternityBaseChance},
                {"paternitymaxchance", &Settings::paternityMaxChance},
                {"paternityphenotypebasechance", &Settings::paternityPhenotypeBaseChance},
                {"paternityphenotypemaxchance", &Settings::paternityPhenotypeMaxChance},
                {"paternityphenotypebirthchance", &Settings::paternityPhenotypeBirthChance},
                {"paternitynonskyrimnetrevealchance", &Settings::paternityNonSkyrimNetRevealChance},
                {"paternitybreakupchance", &Settings::paternityBreakupChance},
                {"paternitybiostepupchance", &Settings::paternityBioStepUpChance}
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, float Settings::*>& GetFloatSettingMap() {
            static const std::unordered_map<std::string_view, float Settings::*> kMap = {
                {"affectiondecaymultiplier", &Settings::affectionDecayMultiplier},
                {"affectiondecaymult", &Settings::affectionDecayMultiplier},
                {"skyrimnetactionradius", &Settings::skyrimnetActionRadius},
                {"actionradius", &Settings::skyrimnetActionRadius},
                {"scenescooldownhours", &Settings::scenesCooldownHours},
                {"paternitydailygrowth", &Settings::paternityDailyGrowth},
                {"paternityphenotypedailygrowth", &Settings::paternityPhenotypeDailyGrowth}
            };
            return kMap;
        }
    }

    bool Settings::GetBoolByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "enablepolygamy") {
            return GetEnablePolygamy();
        }
        if (lowerKey == "simulatenoskyrimnet") {
            std::shared_lock lock(settingsMutex_);
            return simulateNoSkyrimNet;
        }

        const auto& map = GetBoolSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            std::shared_lock lock(settingsMutex_);
            return this->*(it->second);
        }

        MARAS_LOG_WARN("Settings::GetBoolByKey: unknown key '{}'", key);
        return false;
    }

    bool Settings::SetBoolByKey(std::string_view key, bool value) {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "enablepolygamy") {
            SetEnablePolygamy(value);
            return true;
        }
        if (lowerKey == "simulatenoskyrimnet") {
            SetSimulateNoSkyrimNet(value);
            return true;
        }
        if (lowerKey == "globalbancritter" || lowerKey == "bglobalbancritter") {
            SetGlobalCritterBanned(value);
            return true;
        }
        if (lowerKey == "globalbanpet" || lowerKey == "bglobalbanpet") {
            SetGlobalPetBanned(value);
            return true;
        }

        const auto& map = GetBoolSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            std::unique_lock lock(settingsMutex_);
            this->*(it->second) = value;
            return true;
        }

        MARAS_LOG_WARN("Settings::SetBoolByKey: unknown key '{}'", key);
        return false;
    }

    int32_t Settings::GetIntByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "loglevel") {
            std::shared_lock lock(settingsMutex_);
            return logLevel;
        }
        if (lowerKey == "toggleuikey") {
            std::shared_lock lock(settingsMutex_);
            return static_cast<int32_t>(toggleUIKey);
        }
        if (lowerKey == "startdialgender") {
            return GetStartDialGender();
        }
        if (lowerKey == "minrelrankfordial") {
            return GetMinRelRankForDial();
        }
        if (lowerKey == "childcustodygenderpolicy") {
            std::shared_lock lock(settingsMutex_);
            return static_cast<int32_t>(childCustodyGenderPolicy);
        }
        if (lowerKey == "childcustodyplayerpolicy") {
            std::shared_lock lock(settingsMutex_);
            return static_cast<int32_t>(childCustodyPlayerPolicy);
        }
        if (lowerKey == "matchmakingorientation") {
            std::shared_lock lock(settingsMutex_);
            return static_cast<int32_t>(matchmakingOrientation);
        }

        const auto& map = GetIntSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            std::shared_lock lock(settingsMutex_);
            return this->*(it->second);
        }

        MARAS_LOG_WARN("Settings::GetIntByKey: unknown key '{}'", key);
        return 0;
    }

    bool Settings::SetIntByKey(std::string_view key, int32_t value) {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "loglevel") {
            LoggingService::GetSingleton().SetLogLevel(value);
            return true;
        }
        if (lowerKey == "toggleuikey") {
            std::unique_lock lock(settingsMutex_);
            toggleUIKey = static_cast<uint32_t>(value);
            return true;
        }
        if (lowerKey == "startdialgender") {
            SetStartDialGender(value);
            return true;
        }
        if (lowerKey == "minrelrankfordial") {
            SetMinRelRankForDial(value);
            return true;
        }
        if (lowerKey == "childcustodygenderpolicy") {
            if (value >= 0 && value <= static_cast<int32_t>(ChildCustodyGenderPolicy::StayWithFather)) {
                std::unique_lock lock(settingsMutex_);
                childCustodyGenderPolicy = static_cast<ChildCustodyGenderPolicy>(value);
                return true;
            }
            return false;
        }
        if (lowerKey == "childcustodyplayerpolicy") {
            if (value >= 0 && value <= static_cast<int32_t>(ChildCustodyPlayerPolicy::NeverWithPlayer)) {
                std::unique_lock lock(settingsMutex_);
                childCustodyPlayerPolicy = static_cast<ChildCustodyPlayerPolicy>(value);
                return true;
            }
            return false;
        }
        if (lowerKey == "matchmakingorientation") {
            if (value >= 0 && value <= static_cast<int32_t>(MatchmakingOrientation::HeteroAndLesbian)) {
                std::unique_lock lock(settingsMutex_);
                matchmakingOrientation = static_cast<MatchmakingOrientation>(value);
                return true;
            }
            return false;
        }

        const auto& map = GetIntSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            std::unique_lock lock(settingsMutex_);
            this->*(it->second) = value;
            return true;
        }

        MARAS_LOG_WARN("Settings::SetIntByKey: unknown key '{}'", key);
        return false;
    }

    float Settings::GetFloatByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            std::shared_lock lock(settingsMutex_);
            return this->*(it->second);
        }

        MARAS_LOG_WARN("Settings::GetFloatByKey: unknown key '{}'", key);
        return 0.0f;
    }

    bool Settings::SetFloatByKey(std::string_view key, float value) {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            {
                std::unique_lock lock(settingsMutex_);
                this->*(it->second) = value;
            }
            if (lowerKey == "scenescooldownhours") {
                BanterCoordinatorService::GetSingleton().SetCooldownHours(value);
            }
            return true;
        }

        MARAS_LOG_WARN("Settings::SetFloatByKey: unknown key '{}'", key);
        return false;
    }

}



