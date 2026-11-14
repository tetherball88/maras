#include "utils/Settings.h"
#include "utils/Common.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"
#include "core/LoggingService.h"
#include "core/BanterCoordinatorService.h"
#include "core/NPCRelationshipManager.h"
#include "core/FactionReconciler.h"
#include "core/FactionService.h"
#include "core/DeferredActorResolver.h"
#include "core/FormCache.h"
#include "core/GlobalVarReconciler.h"
#include "core/SessionEpoch.h"
#include "papyrus/PapyrusInterface.h"
#include "utils/ActorUtils.h"
#include <Windows.h>

namespace MARAS {

    namespace {
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

        const std::unordered_map<std::string_view, bool State::SettingsData::*>& GetBoolSettingMap() {
            static const std::unordered_map<std::string_view, bool State::SettingsData::*> kMap = {
                {"disablepolygamyquest", &State::SettingsData::disablePolygamyQuest},
                {"skipwedding", &State::SettingsData::skipWedding},
                {"alwayssuccessmarriage", &State::SettingsData::alwaysSuccessMarriage},
                {"alwayssuccess", &State::SettingsData::alwaysSuccessMarriage},
                {"confirmstatuschange_marry", &State::SettingsData::confirmStatusChange_Marry},
                {"confirmacceptproposal", &State::SettingsData::confirmStatusChange_Marry},
                {"confirmbreakup_engagement", &State::SettingsData::confirmBreakup_Engagement},
                {"confirmbreakupengagement", &State::SettingsData::confirmBreakup_Engagement},
                {"confirmdivorce", &State::SettingsData::confirmDivorce},
                {"confirmdivorceduringlowaffectionquest", &State::SettingsData::confirmDivorceDuringLowAffectionQuest},
                {"confirmlowaffectiondivorce", &State::SettingsData::confirmDivorceDuringLowAffectionQuest},
                {"migratedfromstorageutil", &State::SettingsData::migratedFromStorageUtil},
                {"initialrelationshipscandone", &State::SettingsData::initialRelationshipScanDone},
                {"childcustodyprioritizebioparent", &State::SettingsData::childCustodyPrioritizeBioParent},
                {"bglobalbancritter", &State::SettingsData::bGlobalBanCritter},
                {"globalbancritter", &State::SettingsData::bGlobalBanCritter},
                {"bglobalbanpet", &State::SettingsData::bGlobalBanPet},
                {"globalbanpet", &State::SettingsData::bGlobalBanPet},
                {"enableconfirmplayernpc", &State::SettingsData::enableConfirmPlayerNpc},
                {"enableconfirmnpcnpc", &State::SettingsData::enableConfirmNpcNpc},
                {"enableconfirmationmodal", &State::SettingsData::enableConfirmationModal},
                {"confirmstatuschange_dating", &State::SettingsData::confirmStatusChange_Dating},
                {"confirmstatuschange_engagement", &State::SettingsData::confirmStatusChange_Engagement},
                {"confirmstatuschange_lover", &State::SettingsData::confirmStatusChange_Lover},
                {"confirmbreakup_dating", &State::SettingsData::confirmBreakup_Dating},
                {"confirmbreakup_lover", &State::SettingsData::confirmBreakup_Lover},
                {"confirmopenrelationship", &State::SettingsData::confirmOpenRelationship},
                {"confirmcloserelationship", &State::SettingsData::confirmCloseRelationship},
                {"toggleuimodctrl", &State::SettingsData::toggleUIModCtrl},
                {"toggleuimodshift", &State::SettingsData::toggleUIModShift},
                {"toggleuimodalt", &State::SettingsData::toggleUIModAlt},
                {"matchmakingenabled", &State::SettingsData::matchmakingEnabled},
                {"matchmakingallowbreakups", &State::SettingsData::matchmakingAllowBreakups},
                {"matchmakingallowopenclose", &State::SettingsData::matchmakingAllowOpenClose},
                {"matchmakingallowcheating", &State::SettingsData::matchmakingAllowCheating},
                {"matchmakingusellm", &State::SettingsData::matchmakingUseLLM},
                {"matchmakingevaluationstep", &State::SettingsData::matchmakingEvaluationStep},
                {"matchmakinggeneratememories", &State::SettingsData::matchmakingGenerateMemories},
                {"skyrimnetusellmmemories", &State::SettingsData::skyrimnetUseLLMMemories},
                {"skyrimnetusedecisions", &State::SettingsData::skyrimnetUseDecisions},
                {"matchmakingconfirmresults", &State::SettingsData::matchmakingConfirmResults},
                {"paternityforcedicerolls", &State::SettingsData::paternityForceDiceRolls},
                {"revealsecretsinui", &State::SettingsData::revealSecretsInUI},
                {"uifullscreen", &State::SettingsData::uiFullScreen},
                {"autopauseonopen", &State::SettingsData::autoPauseOnOpen},
                {"enablescenes", &State::SettingsData::enableScenes},
                {"enablescenes_lover", &State::SettingsData::enableScenes_Lover},
                {"enablescenes_dating", &State::SettingsData::enableScenes_Dating},
                {"enablescenes_engaged", &State::SettingsData::enableScenes_Engaged},
                {"enablescenes_married", &State::SettingsData::enableScenes_Married},
                {"enablescenes_openrelationship", &State::SettingsData::enableScenes_OpenRelationship},
                {"enablescenes_cheating", &State::SettingsData::enableScenes_Cheating},
                {"enablescenes_cheatingdiscovered", &State::SettingsData::enableScenes_CheatingDiscovered},
                {"enablescenes_cheatingcaught", &State::SettingsData::enableScenes_CheatingCaught},
                {"enablescenes_loctypetemple", &State::SettingsData::enableScenes_LocTypeTemple},
                {"enablescenes_loctypestore", &State::SettingsData::enableScenes_LocTypeStore},
                {"enablescenes_loctypestewardsdwelling", &State::SettingsData::enableScenes_LocTypeStewardsDwelling},
                {"enablescenes_loctypeinn", &State::SettingsData::enableScenes_LocTypeInn},
                {"enablescenes_loctypehouse", &State::SettingsData::enableScenes_LocTypeHouse},
                {"enablescenes_loctypebarracks", &State::SettingsData::enableScenes_LocTypeBarracks},
                {"enablescenes_loctypefarm", &State::SettingsData::enableScenes_LocTypeFarm},
                {"enablescenes_loctypeguild", &State::SettingsData::enableScenes_LocTypeGuild},
                {"enablescenes_loctypeplayerhouse", &State::SettingsData::enableScenes_LocTypePlayerHouse},
                {"enablescenes_byoh_loctypehomestead", &State::SettingsData::enableScenes_BYOH_LocTypeHomestead},
                {"enablescenes_loctypetown", &State::SettingsData::enableScenes_LocTypeTown},
                {"enablescenes_loctypesettlement", &State::SettingsData::enableScenes_LocTypeSettlement},
                {"enablescenes_loctypeorcstronghold", &State::SettingsData::enableScenes_LocTypeOrcStronghold},
                {"enablescenes_loctypecity", &State::SettingsData::enableScenes_LocTypeCity},
                {"enablescenes_loctypelumbermill", &State::SettingsData::enableScenes_LocTypeLumberMill},
                {"playerpartnercheatingagencyenabled", &State::SettingsData::playerPartnerCheatingAgencyEnabled},
                {"playerpartneropenrelagencyenabled", &State::SettingsData::playerPartnerOpenRelAgencyEnabled},
                {"playerpartnercheatingallowwhenhappy", &State::SettingsData::playerPartnerCheatingAllowWhenHappy},
                {"playerpartnercheatingenableneglect", &State::SettingsData::playerPartnerCheatingEnableNeglect},
                {"playerpartnercheatingenablerevenge", &State::SettingsData::playerPartnerCheatingEnableRevenge},
                {"playerpartnercheatingenablehierarchy", &State::SettingsData::playerPartnerCheatingEnableHierarchy},
                {"playerpartnercheatingenablechildrenanchor", &State::SettingsData::playerPartnerCheatingEnableChildrenAnchor},
                {"playerpartnercheatingusellm", &State::SettingsData::playerPartnerCheatingUseLLM},
                {"playerpartnercheatingconfirmresults", &State::SettingsData::playerPartnerCheatingConfirmResults},
                {"playerpartneragencyenableforcegreets", &State::SettingsData::playerPartnerAgencyEnableForceGreets},
                {"playerpartneragencyenablehomearrivalevents", &State::SettingsData::playerPartnerAgencyEnableHomeArrivalEvents},
                {"childtrainingrequirefaction", &State::SettingsData::childTrainingRequireFaction}
            };
            return kMap;
        }

        using IntSettingMemberPtr = std::int32_t State::SettingsData::*;
        const std::unordered_map<std::string_view, IntSettingMemberPtr>& GetIntSettingMap() {
            static const std::unordered_map<std::string_view, IntSettingMemberPtr> kMap = {
                {"logdestination", &State::SettingsData::logDestination},
                {"actionscooldown", &State::SettingsData::actionsCooldown},
                {"actionsdeclinecooldown", &State::SettingsData::actionsDeclineCooldown},
                {"matchmakingintervaldays", &State::SettingsData::matchmakingIntervalDays},
                {"matchmakingmaxsuggestions", &State::SettingsData::matchmakingMaxSuggestions},
                {"matchmakingnpccooldowndays", &State::SettingsData::matchmakingNpcCooldownDays},
                {"dicerollchancenewdating", &State::SettingsData::diceRollChanceNewDating},
                {"dicerollchancenewengaged", &State::SettingsData::diceRollChanceNewEngaged},
                {"dicerollchancenewloverallowed", &State::SettingsData::diceRollChanceNewLoverAllowed},
                {"dicerollchancenewlovercheating", &State::SettingsData::diceRollChanceNewLoverCheating},
                {"dicerollchanceadvance", &State::SettingsData::diceRollChanceAdvance},
                {"dicerollchancebreakup", &State::SettingsData::diceRollChanceBreakup},
                {"dicerollchanceopenclose", &State::SettingsData::diceRollChanceOpenClose},
                {"paternitybasechance", &State::SettingsData::paternityBaseChance},
                {"paternitymaxchance", &State::SettingsData::paternityMaxChance},
                {"paternityphenotypebasechance", &State::SettingsData::paternityPhenotypeBaseChance},
                {"paternityphenotypemaxchance", &State::SettingsData::paternityPhenotypeMaxChance},
                {"paternityphenotypebirthchance", &State::SettingsData::paternityPhenotypeBirthChance},
                {"paternitynonskyrimnetrevealchance", &State::SettingsData::paternityNonSkyrimNetRevealChance},
                {"paternitybreakupchance", &State::SettingsData::paternityBreakupChance},
                {"paternitybiostepupchance", &State::SettingsData::paternityBioStepUpChance},
                {"playerpartnercheatingmaxrollcap", &State::SettingsData::playerPartnerCheatingMaxRollCap},
                {"playerpartnercheatingmaxsecretlovers", &State::SettingsData::playerPartnerCheatingMaxSecretLovers},
                {"lonelinessthresholddays", &State::SettingsData::lonelinessThresholdDays},
                {"childtrainingdurationdays", &State::SettingsData::childTrainingDurationDays},
                {"childtrainingcost", &State::SettingsData::childTrainingCost}
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, float State::SettingsData::*>& GetFloatSettingMap() {
            static const std::unordered_map<std::string_view, float State::SettingsData::*> kMap = {
                {"affectiondecaymultiplier", &State::SettingsData::affectionDecayMultiplier},
                {"affectiondecaymult", &State::SettingsData::affectionDecayMultiplier},
                {"skyrimnetactionradius", &State::SettingsData::skyrimnetActionRadius},
                {"actionradius", &State::SettingsData::skyrimnetActionRadius},
                {"scenescooldownhours", &State::SettingsData::scenesCooldownHours},
                {"paternitydailygrowth", &State::SettingsData::paternityDailyGrowth},
                {"paternityphenotypedailygrowth", &State::SettingsData::paternityPhenotypeDailyGrowth},
                {"uiscale", &State::SettingsData::uiScale},
                {"playerpartnercheatingchancemultiplier", &State::SettingsData::playerPartnerCheatingChanceMultiplier},
                {"playerpartneragencyopenloverchancemultiplier", &State::SettingsData::playerPartnerAgencyOpenLoverChanceMultiplier},
                {"playerpartneragencybreakupchancemultiplier", &State::SettingsData::playerPartnerAgencyBreakupChanceMultiplier},
                {"multlonelinessdecaystart", &State::SettingsData::multLonelinessDecayStart},
                {"multpartneragencyscaninterval", &State::SettingsData::multPartnerAgencyScanInterval},
                {"multrecentattentionwindow", &State::SettingsData::multRecentAttentionWindow},
                {"multveryrecentvisitwindow", &State::SettingsData::multVeryRecentVisitWindow},
                {"multneglectgrowthstart", &State::SettingsData::multNeglectGrowthStart},
                {"multcontentneglectstart", &State::SettingsData::multContentNeglectStart},
                {"multprolongedabsencepenalty", &State::SettingsData::multProlongedAbsencePenalty},
                {"multaffairtalkingescalation", &State::SettingsData::multAffairTalkingEscalation},
                {"multminsideflingageforbreakup", &State::SettingsData::multMinSideFlingAgeForBreakup},
                {"multpartnerintentttl", &State::SettingsData::multPartnerIntentTTL},
                {"multhomearrivalcooldown", &State::SettingsData::multHomeArrivalCooldown},
                {"multaffairfatiguestart", &State::SettingsData::multAffairFatigueStart},
                {"multromanticattendedbreakup", &State::SettingsData::multRomanticAttendedBreakup},
                {"multsuggestopenneglectmin", &State::SettingsData::multSuggestOpenNeglectMin}
            };
            return kMap;
        }
    }  // namespace

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

    void Settings::ParseKeyCombo(const std::string& comboStr, State::SettingsData& out) {
        out.toggleUIKey = 0;
        out.toggleUIModCtrl = false;
        out.toggleUIModShift = false;
        out.toggleUIModAlt = false;

        std::string trimmedCombo = StringUtils::TrimStd(comboStr);
        if (trimmedCombo.empty()) return;

        std::stringstream ss(trimmedCombo);
        std::string token;
        while (std::getline(ss, token, '+')) {
            std::string part = StringUtils::ToUpperStd(StringUtils::TrimStd(token));
            if (part.empty()) continue;

            if (part == "CTRL" || part == "CONTROL" || part == "LCTRL" || part == "RCTRL") {
                out.toggleUIModCtrl = true;
            } else if (part == "SHIFT" || part == "LSHIFT" || part == "RSHIFT") {
                out.toggleUIModShift = true;
            } else if (part == "ALT" || part == "LALT" || part == "RALT") {
                out.toggleUIModAlt = true;
            } else {
                out.toggleUIKey = ParseSingleKey(part);
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

        State::SettingsData baseline = State::WorldStore::GetSingleton().GetIniBaselineSettings();
        ParseKeyCombo(toggleVal, baseline);
        baseline.defaultLogLevel = iniLogLevel;
        baseline.logLevel = iniLogLevel;
        State::WorldStore::GetSingleton().SetIniBaselineSettings(baseline);

        LoggingService::GetSingleton().SetLogLevel(iniLogLevel);

        MARAS_LOG_INFO("Settings loaded from {}", pathStr);
        MARAS_LOG_INFO("  ToggleUI = '{}' -> Key 0x{:02X} (Ctrl={}, Shift={}, Alt={})",
                       toggleVal, baseline.toggleUIKey, baseline.toggleUIModCtrl,
                       baseline.toggleUIModShift, baseline.toggleUIModAlt);
        MARAS_LOG_INFO("  Logging: Default LogLevel={}", baseline.defaultLogLevel);
    }

    int32_t Settings::GetStartDialGender() const {
        return GlobalVarReconciler::GetSingleton().GetStartDialGender();
    }

    void Settings::SetStartDialGender(int32_t value) {
        GlobalVarReconciler::GetSingleton().SetStartDialGender(value);
    }

    bool Settings::GetEnablePolygamy() const {
        return Snapshot()->enablePlayerPolygamy;
    }

    void Settings::SetEnablePolygamy(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.settings.Write().enablePlayerPolygamy = value;
        });

        if (value) {
            NPCRelationshipManager::GetSingleton().MarkPlayerCheatingAllowedOnPolygamy();
        }
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        FactionReconciler::GetSingleton().ReconcileAllOnLoad();
    }

    void Settings::SetDisablePolygamyQuest(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.settings.Write().disablePolygamyQuest = value;
        });
        if (value) {
            auto dispatchEvent = []() {
                if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                    SKSE::ModCallbackEvent modEvent("maras_stop_polygamy_quest", "", 0.0f, nullptr);
                    eventSource->SendEvent(&modEvent);
                }
            };
            if (DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                dispatchEvent();
            } else {
                AddCurrentEpochTask("Settings::StopPolygamyQuestEvent", [dispatchEvent]() {
                    std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                    dispatchEvent();
                });
            }
        }
        MARAS_LOG_INFO("Settings::SetDisablePolygamyQuest - disablePolygamyQuest set to {}", value);
    }

    int32_t Settings::GetMinRelRankForDial() const {
        return GlobalVarReconciler::GetSingleton().GetMinRelRankForDial();
    }

    void Settings::SetMinRelRankForDial(int32_t value) {
        GlobalVarReconciler::GetSingleton().SetMinRelRankForDial(value);
    }

    bool Settings::GetPreferVanillaAudio() const {
        return GlobalVarReconciler::GetSingleton().GetPreferVanillaAudio();
    }

    void Settings::SetPreferVanillaAudio(bool value) {
        GlobalVarReconciler::GetSingleton().SetPreferVanillaAudio(value);
    }

    bool Settings::GetAllowAIDial() const {
        return GlobalVarReconciler::GetSingleton().GetAllowAIDial();
    }

    void Settings::SetAllowAIDial(bool value) {
        GlobalVarReconciler::GetSingleton().SetAllowAIDial(value);
    }

    void Settings::SyncSimulateNoSkyrimNetGlobal(bool /*value*/) const {
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
    }

    void Settings::SetSimulateNoSkyrimNet(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.settings.Write().simulateNoSkyrimNet = value;
        });
        SyncSimulateNoSkyrimNetGlobal(value);
        MARAS_LOG_INFO("Settings::SetSimulateNoSkyrimNet - simulateNoSkyrimNet set to {} (global 0xC4 updated)", value);
    }

    void Settings::SetGlobalCritterBanned(bool banned) {
        State::WorldStore::GetSingleton().Modify([banned](State::WorldState& ws) {
            ws.settings.Write().bGlobalBanCritter = banned;
        });
    }

    void Settings::SetGlobalPetBanned(bool banned) {
        State::WorldStore::GetSingleton().Modify([banned](State::WorldState& ws) {
            ws.settings.Write().bGlobalBanPet = banned;
        });
    }

    void Settings::ApplyPostLoadEngineSideEffects() {
        const auto snap = Snapshot();
        const auto& s = *snap;

        LoggingService::GetSingleton().SetLogLevel(s.logLevel);
        GlobalVarReconciler::GetSingleton().SyncExternalPolygamyGlobalToStore();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();

        MARAS_LOG_INFO(
            "Settings::ApplyPostLoadEngineSideEffects - loaded settings (logLevel={}, skipWedding={}, scanDone={}, custodyGender={}, custodyPlayer={}, custodyBio={}, globalBanCritter={}, globalBanPet={}, matchEnabled={}, matchInterval={}, matchOrientation={}, matchCheating={})",
            s.logLevel, s.skipWedding, s.initialRelationshipScanDone, static_cast<int>(s.childCustodyGenderPolicy),
            static_cast<int>(s.childCustodyPlayerPolicy), s.childCustodyPrioritizeBioParent, s.bGlobalBanCritter,
            s.bGlobalBanPet, s.matchmakingEnabled, s.matchmakingIntervalDays, static_cast<int>(s.matchmakingOrientation),
            s.matchmakingAllowCheating);
        MARAS_LOG_INFO(
            "Settings::ApplyPostLoadEngineSideEffects - confirmation modal settings: playerNpc={}, npcNpc={}, dating={}, engagement={}, lover={}, marry={}, breakupDating={}, breakupEngagement={}, breakupLover={}, divorce={}, lowAffDivorce={}, openRel={}, closeRel={}",
            s.enableConfirmPlayerNpc, s.enableConfirmNpcNpc, s.confirmStatusChange_Dating, s.confirmStatusChange_Engagement,
            s.confirmStatusChange_Lover, s.confirmStatusChange_Marry, s.confirmBreakup_Dating, s.confirmBreakup_Engagement,
            s.confirmBreakup_Lover, s.confirmDivorce, s.confirmDivorceDuringLowAffectionQuest, s.confirmOpenRelationship,
            s.confirmCloseRelationship);
    }

    void Settings::RevertGameSettings() {
        auto& store = State::WorldStore::GetSingleton();
        State::SettingsData reverted = store.GetIniBaselineSettings();
        reverted.logLevel = reverted.defaultLogLevel;

        store.Modify([&reverted](State::WorldState& ws) {
            ws.settings.Reset(reverted);
        });

        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        LoggingService::GetSingleton().SetLogLevel(reverted.logLevel);

        MARAS_LOG_INFO("Settings::RevertGameSettings - reverted settings to defaults (logLevel={})", reverted.logLevel);
        MARAS_LOG_INFO(
            "Settings::RevertGameSettings - reverted confirmation modal settings to defaults (playerNpc={}, npcNpc={}, dating={}, engagement={}, lover={}, marry={}, breakupDating={}, breakupEngagement={}, breakupLover={}, divorce={}, lowAffDivorce={}, openRel={}, closeRel={})",
            reverted.enableConfirmPlayerNpc, reverted.enableConfirmNpcNpc, reverted.confirmStatusChange_Dating,
            reverted.confirmStatusChange_Engagement, reverted.confirmStatusChange_Lover, reverted.confirmStatusChange_Marry,
            reverted.confirmBreakup_Dating, reverted.confirmBreakup_Engagement, reverted.confirmBreakup_Lover,
            reverted.confirmDivorce, reverted.confirmDivorceDuringLowAffectionQuest, reverted.confirmOpenRelationship,
            reverted.confirmCloseRelationship);
    }

    nlohmann::json Settings::DumpJson() const {
        nlohmann::json j;
        const auto snap = Snapshot();
        const auto& s = *snap;

        State::ForEachPersistedSettingField([&](std::string_view key, auto memberPtr, bool) {
            const auto& field = s.*memberPtr;
            using FieldType = std::decay_t<decltype(field)>;
            if constexpr (std::is_enum_v<FieldType>) {
                j[std::string(key)] = static_cast<uint8_t>(field);
            } else {
                j[std::string(key)] = field;
            }
        });

        j["defaultLogLevel"] = s.defaultLogLevel;
        // Legacy keys for backward compatibility
        j["confirmAcceptProposal"] = s.confirmStatusChange_Marry;
        j["confirmBreakupEngagement"] = s.confirmBreakup_Engagement;
        j["confirmLowAffectionDivorce"] = s.confirmDivorceDuringLowAffectionQuest;
        j["toggleUIKey"] = s.toggleUIKey;
        j["toggleUIModCtrl"] = s.toggleUIModCtrl;
        j["toggleUIModShift"] = s.toggleUIModShift;
        j["toggleUIModAlt"] = s.toggleUIModAlt;
        j["enableConfirmationModal"] = s.enableConfirmationModal;
        j["actionsCooldown"] = s.actionsCooldown;
        j["actionsDeclineCooldown"] = s.actionsDeclineCooldown;

        j["startDialGender"] = GetStartDialGender();
        j["enablePolygamy"] = GetEnablePolygamy();
        j["minRelRankForDial"] = GetMinRelRankForDial();
        j["preferVanillaAudio"] = GetPreferVanillaAudio();
        j["allowAIDial"] = GetAllowAIDial();
        return j;
    }

    bool Settings::GetBoolByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "enablepolygamy") {
            return GetEnablePolygamy();
        }
        if (lowerKey == "prefervanillaaudio") {
            return GetPreferVanillaAudio();
        }
        if (lowerKey == "allowaidial") {
            return GetAllowAIDial();
        }
        if (lowerKey == "simulatenoskyrimnet") {
            return Snapshot()->simulateNoSkyrimNet;
        }

        const auto& map = GetBoolSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            return (*Snapshot()).*(it->second);
        }

        MARAS_LOG_WARN("Settings::GetBoolByKey: unknown key '{}'", key);
        return false;
    }

    bool Settings::SetBoolByKey(std::string_view key, bool value) {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        if (lowerKey == "disablepolygamyquest") {
            SetDisablePolygamyQuest(value);
            return true;
        }
        if (lowerKey == "enablepolygamy") {
            SetEnablePolygamy(value);
            return true;
        }
        if (lowerKey == "prefervanillaaudio") {
            SetPreferVanillaAudio(value);
            return true;
        }
        if (lowerKey == "allowaidial") {
            SetAllowAIDial(value);
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
            const auto memberPtr = it->second;
            State::WorldStore::GetSingleton().Modify([memberPtr, value](State::WorldState& ws) {
                ws.settings.Write().*memberPtr = value;
            });
            return true;
        }

        MARAS_LOG_WARN("Settings::SetBoolByKey: unknown key '{}'", key);
        return false;
    }

    int32_t Settings::GetIntByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        const auto snap = Snapshot();
        const auto& s = *snap;

        if (lowerKey == "loglevel") {
            return s.logLevel;
        }
        if (lowerKey == "toggleuikey") {
            return static_cast<int32_t>(s.toggleUIKey);
        }
        if (lowerKey == "startdialgender") {
            return GetStartDialGender();
        }
        if (lowerKey == "minrelrankfordial") {
            return GetMinRelRankForDial();
        }
        if (lowerKey == "childcustodygenderpolicy") {
            return static_cast<int32_t>(s.childCustodyGenderPolicy);
        }
        if (lowerKey == "childcustodyplayerpolicy") {
            return static_cast<int32_t>(s.childCustodyPlayerPolicy);
        }
        if (lowerKey == "matchmakingorientation") {
            return static_cast<int32_t>(s.matchmakingOrientation);
        }

        const auto& map = GetIntSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            return s.*(it->second);
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
            State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
                ws.settings.Write().toggleUIKey = static_cast<uint32_t>(value);
            });
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
                State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
                    ws.settings.Write().childCustodyGenderPolicy = static_cast<ChildCustodyGenderPolicy>(value);
                });
                return true;
            }
            return false;
        }
        if (lowerKey == "childcustodyplayerpolicy") {
            if (value >= 0 && value <= static_cast<int32_t>(ChildCustodyPlayerPolicy::NeverWithPlayer)) {
                State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
                    ws.settings.Write().childCustodyPlayerPolicy = static_cast<ChildCustodyPlayerPolicy>(value);
                });
                return true;
            }
            return false;
        }
        if (lowerKey == "matchmakingorientation") {
            if (value >= 0 && value <= static_cast<int32_t>(MatchmakingOrientation::HeteroAndLesbian)) {
                State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
                    ws.settings.Write().matchmakingOrientation = static_cast<MatchmakingOrientation>(value);
                });
                return true;
            }
            return false;
        }

        const auto& map = GetIntSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            if (lowerKey == "childtrainingdurationdays") {
                value = std::clamp(value, 1, 120);
            } else if (lowerKey == "childtrainingcost") {
                value = std::clamp(value, 0, 10000);
            }
            const auto memberPtr = it->second;
            State::WorldStore::GetSingleton().Modify([memberPtr, value](State::WorldState& ws) {
                ws.settings.Write().*memberPtr = value;
            });
            return true;
        }

        MARAS_LOG_WARN("Settings::SetIntByKey: unknown key '{}'", key);
        return false;
    }

    float Settings::GetFloatByKey(std::string_view key) const {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            return (*Snapshot()).*(it->second);
        }

        MARAS_LOG_WARN("Settings::GetFloatByKey: unknown key '{}'", key);
        return 0.0f;
    }

    bool Settings::SetFloatByKey(std::string_view key, float value) {
        const std::string lowerKey = StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        if (auto it = map.find(lowerKey); it != map.end()) {
            if (lowerKey == "uiscale") {
                value = std::clamp(value, 0.75f, 1.5f);
            }
            const auto memberPtr = it->second;
            State::WorldStore::GetSingleton().Modify([memberPtr, value](State::WorldState& ws) {
                ws.settings.Write().*memberPtr = value;
            });
            if (lowerKey == "scenescooldownhours") {
                BanterCoordinatorService::GetSingleton().SetCooldownHours(value);
            }
            return true;
        }

        MARAS_LOG_WARN("Settings::SetFloatByKey: unknown key '{}'", key);
        return false;
    }

}
