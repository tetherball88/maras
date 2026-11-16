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
#include "maras/state/WorldTables.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace MARAS::App {

    namespace {

        constexpr std::int32_t kMinLogLevel = 0;
        constexpr std::int32_t kMaxLogLevel = 5;

        using BoolGetterFn = bool (*)(const State::SettingsData&);
        using BoolSetterFn = void (*)(SettingsCoordinator&, State::SettingsData&, bool);
        using IntGetterFn = std::int32_t (*)(const State::SettingsData&);
        using IntSetterFn = void (*)(SettingsCoordinator&, State::SettingsData&, std::int32_t);
        using FloatGetterFn = float (*)(const State::SettingsData&);
        using FloatSetterFn = void (*)(SettingsCoordinator&, State::SettingsData&, float);

        struct BoolSettingEntry {
            BoolGetterFn get{ nullptr };
            BoolSetterFn set{ nullptr };
        };

        struct IntSettingEntry {
            IntGetterFn get{ nullptr };
            IntSetterFn set{ nullptr };
        };

        struct FloatSettingEntry {
            FloatGetterFn get{ nullptr };
            FloatSetterFn set{ nullptr };
        };

        template <bool State::SettingsData::*Member>
        constexpr BoolSettingEntry MakeBoolEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> bool { return s.*Member; },
                [](SettingsCoordinator&, State::SettingsData& s, bool v) { s.*Member = v; }
            };
        }

        template <std::int32_t State::SettingsData::*Member>
        constexpr IntSettingEntry MakeIntEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> std::int32_t { return s.*Member; },
                [](SettingsCoordinator&, State::SettingsData& s, std::int32_t v) { s.*Member = v; }
            };
        }

        template <std::int32_t State::SettingsData::*Member, std::int32_t MinV, std::int32_t MaxV>
        constexpr IntSettingEntry MakeClampedIntEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> std::int32_t { return s.*Member; },
                [](SettingsCoordinator&, State::SettingsData& s, std::int32_t v) {
                    s.*Member = std::clamp(v, MinV, MaxV);
                }
            };
        }

        template <typename EnumT, EnumT State::SettingsData::*Member, std::int32_t MaxEnumInt>
        constexpr IntSettingEntry MakeEnumIntEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> std::int32_t {
                    return static_cast<std::int32_t>(s.*Member);
                },
                [](SettingsCoordinator&, State::SettingsData& s, std::int32_t v) {
                    s.*Member = static_cast<EnumT>(std::clamp(v, 0, MaxEnumInt));
                }
            };
        }

        template <float State::SettingsData::*Member>
        constexpr FloatSettingEntry MakeFloatEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> float { return s.*Member; },
                [](SettingsCoordinator&, State::SettingsData& s, float v) { s.*Member = v; }
            };
        }

        template <float State::SettingsData::*Member, float MinV, float MaxV>
        constexpr FloatSettingEntry MakeClampedFloatEntry() noexcept {
            return {
                [](const State::SettingsData& s) -> float { return s.*Member; },
                [](SettingsCoordinator&, State::SettingsData& s, float v) {
                    s.*Member = std::clamp(v, MinV, MaxV);
                }
            };
        }

        const std::unordered_map<std::string_view, BoolSettingEntry>& GetBoolSettingMap() {
            static const std::unordered_map<std::string_view, BoolSettingEntry> kMap = {
                { "toggleuimodctrl", MakeBoolEntry<&State::SettingsData::toggleUIModCtrl>() },
                { "toggleuimodshift", MakeBoolEntry<&State::SettingsData::toggleUIModShift>() },
                { "toggleuimodalt", MakeBoolEntry<&State::SettingsData::toggleUIModAlt>() },

                { "enableconfirmationmodal", MakeBoolEntry<&State::SettingsData::enableConfirmationModal>() },
                { "enableconfirmplayernpc", MakeBoolEntry<&State::SettingsData::enableConfirmPlayerNpc>() },
                { "enableconfirmnpcnpc", MakeBoolEntry<&State::SettingsData::enableConfirmNpcNpc>() },

                { "confirmstatuschange_dating", MakeBoolEntry<&State::SettingsData::confirmStatusChange_Dating>() },
                { "confirmstatuschange_engagement", MakeBoolEntry<&State::SettingsData::confirmStatusChange_Engagement>() },
                { "confirmstatuschange_lover", MakeBoolEntry<&State::SettingsData::confirmStatusChange_Lover>() },
                { "confirmstatuschange_marry", MakeBoolEntry<&State::SettingsData::confirmStatusChange_Marry>() },
                { "confirmacceptproposal", MakeBoolEntry<&State::SettingsData::confirmStatusChange_Marry>() },

                { "confirmbreakup_dating", MakeBoolEntry<&State::SettingsData::confirmBreakup_Dating>() },
                { "confirmbreakup_engagement", MakeBoolEntry<&State::SettingsData::confirmBreakup_Engagement>() },
                { "confirmbreakupengagement", MakeBoolEntry<&State::SettingsData::confirmBreakup_Engagement>() },
                { "confirmbreakup_lover", MakeBoolEntry<&State::SettingsData::confirmBreakup_Lover>() },
                { "confirmdivorce", MakeBoolEntry<&State::SettingsData::confirmDivorce>() },
                { "confirmdivorceduringlowaffectionquest",
                  MakeBoolEntry<&State::SettingsData::confirmDivorceDuringLowAffectionQuest>() },
                { "confirmlowaffectiondivorce",
                  MakeBoolEntry<&State::SettingsData::confirmDivorceDuringLowAffectionQuest>() },
                { "confirmopenrelationship", MakeBoolEntry<&State::SettingsData::confirmOpenRelationship>() },
                { "confirmcloserelationship", MakeBoolEntry<&State::SettingsData::confirmCloseRelationship>() },

                { "skipwedding", MakeBoolEntry<&State::SettingsData::skipWedding>() },
                { "alwayssuccessmarriage", MakeBoolEntry<&State::SettingsData::alwaysSuccessMarriage>() },
                { "skyrimnetusellmmemories", MakeBoolEntry<&State::SettingsData::skyrimnetUseLLMMemories>() },
                { "skyrimnetusedecisions", MakeBoolEntry<&State::SettingsData::skyrimnetUseDecisions>() },
                { "migratedfromstorageutil", MakeBoolEntry<&State::SettingsData::migratedFromStorageUtil>() },
                { "initialrelationshipscandone", MakeBoolEntry<&State::SettingsData::initialRelationshipScanDone>() },

                { "childcustodyprioritizebioparent",
                  MakeBoolEntry<&State::SettingsData::childCustodyPrioritizeBioParent>() },

                { "matchmakingenabled", MakeBoolEntry<&State::SettingsData::matchmakingEnabled>() },
                { "matchmakingallowbreakups", MakeBoolEntry<&State::SettingsData::matchmakingAllowBreakups>() },
                { "matchmakingallowopenclose", MakeBoolEntry<&State::SettingsData::matchmakingAllowOpenClose>() },
                { "matchmakingallowcheating", MakeBoolEntry<&State::SettingsData::matchmakingAllowCheating>() },
                { "matchmakingusellm", MakeBoolEntry<&State::SettingsData::matchmakingUseLLM>() },
                { "matchmakingevaluationstep", MakeBoolEntry<&State::SettingsData::matchmakingEvaluationStep>() },
                { "matchmakinggeneratememories", MakeBoolEntry<&State::SettingsData::matchmakingGenerateMemories>() },
                { "matchmakingconfirmresults", MakeBoolEntry<&State::SettingsData::matchmakingConfirmResults>() },

                { "playerpartnercheatingagencyenabled",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingAgencyEnabled>() },
                { "playerpartneropenrelagencyenabled",
                  MakeBoolEntry<&State::SettingsData::playerPartnerOpenRelAgencyEnabled>() },
                { "playerpartnercheatingallowwhenhappy",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingAllowWhenHappy>() },
                { "playerpartnercheatingenableneglect",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingEnableNeglect>() },
                { "playerpartnercheatingenablerevenge",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingEnableRevenge>() },
                { "playerpartnercheatingenablehierarchy",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingEnableHierarchy>() },
                { "playerpartnercheatingenablechildrenanchor",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingEnableChildrenAnchor>() },
                { "playerpartnercheatingusellm",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingUseLLM>() },
                { "playerpartnercheatingconfirmresults",
                  MakeBoolEntry<&State::SettingsData::playerPartnerCheatingConfirmResults>() },
                { "playerpartneragencyenableforcegreets",
                  MakeBoolEntry<&State::SettingsData::playerPartnerAgencyEnableForceGreets>() },
                { "playerpartneragencyenablehomearrivalevents",
                  MakeBoolEntry<&State::SettingsData::playerPartnerAgencyEnableHomeArrivalEvents>() },

                { "paternityforcedicerolls", MakeBoolEntry<&State::SettingsData::paternityForceDiceRolls>() },
                { "revealsecretsinui", MakeBoolEntry<&State::SettingsData::revealSecretsInUI>() },
                { "uifullscreen", MakeBoolEntry<&State::SettingsData::uiFullScreen>() },
                { "autopauseonopen", MakeBoolEntry<&State::SettingsData::autoPauseOnOpen>() },

                { "enablescenes", MakeBoolEntry<&State::SettingsData::enableScenes>() },
                { "enablescenes_lover", MakeBoolEntry<&State::SettingsData::enableScenes_Lover>() },
                { "enablescenes_dating", MakeBoolEntry<&State::SettingsData::enableScenes_Dating>() },
                { "enablescenes_engaged", MakeBoolEntry<&State::SettingsData::enableScenes_Engaged>() },
                { "enablescenes_married", MakeBoolEntry<&State::SettingsData::enableScenes_Married>() },
                { "enablescenes_openrelationship", MakeBoolEntry<&State::SettingsData::enableScenes_OpenRelationship>() },
                { "enablescenes_cheating", MakeBoolEntry<&State::SettingsData::enableScenes_Cheating>() },
                { "enablescenes_cheatingdiscovered",
                  MakeBoolEntry<&State::SettingsData::enableScenes_CheatingDiscovered>() },
                { "enablescenes_cheatingcaught", MakeBoolEntry<&State::SettingsData::enableScenes_CheatingCaught>() },

                { "enablescenes_loctypetemple", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeTemple>() },
                { "enablescenes_loctypestore", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeStore>() },
                { "enablescenes_loctypestewardsdwelling",
                  MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeStewardsDwelling>() },
                { "enablescenes_loctypeinn", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeInn>() },
                { "enablescenes_loctypehouse", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeHouse>() },
                { "enablescenes_loctypebarracks", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeBarracks>() },
                { "enablescenes_loctypeguild", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeGuild>() },
                { "enablescenes_loctypeplayerhouse",
                  MakeBoolEntry<&State::SettingsData::enableScenes_LocTypePlayerHouse>() },
                { "enablescenes_byoh_loctypehomestead",
                  MakeBoolEntry<&State::SettingsData::enableScenes_BYOH_LocTypeHomestead>() },
                { "enablescenes_loctypetown", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeTown>() },
                { "enablescenes_loctypesettlement",
                  MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeSettlement>() },
                { "enablescenes_loctypeorcstronghold",
                  MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeOrcStronghold>() },
                { "enablescenes_loctypecity", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeCity>() },
                { "enablescenes_loctypelumbermill",
                  MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeLumberMill>() },
                { "enablescenes_loctypefarm", MakeBoolEntry<&State::SettingsData::enableScenes_LocTypeFarm>() },

                { "childtrainingrequirefaction", MakeBoolEntry<&State::SettingsData::childTrainingRequireFaction>() },

                { "bglobalbancritter", MakeBoolEntry<&State::SettingsData::bGlobalBanCritter>() },
                { "globalbancritter", MakeBoolEntry<&State::SettingsData::bGlobalBanCritter>() },
                { "bglobalbanpet", MakeBoolEntry<&State::SettingsData::bGlobalBanPet>() },
                { "globalbanpet", MakeBoolEntry<&State::SettingsData::bGlobalBanPet>() }
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, IntSettingEntry>& GetIntSettingMap() {
            static const std::unordered_map<std::string_view, IntSettingEntry> kMap = {
                { "toggleuikey",
                  { [](const State::SettingsData& s) -> std::int32_t {
                        return static_cast<std::int32_t>(s.toggleUIKey);
                    },
                    [](SettingsCoordinator&, State::SettingsData& s, std::int32_t v) {
                        if (v >= 0) {
                            s.toggleUIKey = static_cast<std::uint32_t>(v);
                        }
                    } } },
                { "actionscooldown", MakeIntEntry<&State::SettingsData::actionsCooldown>() },
                { "actionsdeclinecooldown", MakeIntEntry<&State::SettingsData::actionsDeclineCooldown>() },
                { "logdestination", MakeIntEntry<&State::SettingsData::logDestination>() },
                { "defaultloglevel", MakeIntEntry<&State::SettingsData::defaultLogLevel>() },
                { "loglevel",
                  { [](const State::SettingsData& s) -> std::int32_t { return s.logLevel; },
                    [](SettingsCoordinator&, State::SettingsData& s, std::int32_t v) {
                        const std::int32_t clamped = std::clamp(v, kMinLogLevel, kMaxLogLevel);
                        s.logLevel = clamped;
                        Engine::ApplyLogLevel(clamped);
                    } } },
                { "lonelinessthresholddays", MakeIntEntry<&State::SettingsData::lonelinessThresholdDays>() },

                { "childcustodygenderpolicy",
                  MakeEnumIntEntry<State::ChildCustodyGenderPolicy,
                                   &State::SettingsData::childCustodyGenderPolicy,
                                   static_cast<std::int32_t>(State::ChildCustodyGenderPolicy::StayWithFather)>() },
                { "childcustodyplayerpolicy",
                  MakeEnumIntEntry<State::ChildCustodyPlayerPolicy,
                                   &State::SettingsData::childCustodyPlayerPolicy,
                                   static_cast<std::int32_t>(State::ChildCustodyPlayerPolicy::NeverWithPlayer)>() },

                { "matchmakingintervaldays", MakeIntEntry<&State::SettingsData::matchmakingIntervalDays>() },
                { "matchmakingmaxsuggestions", MakeIntEntry<&State::SettingsData::matchmakingMaxSuggestions>() },
                { "matchmakingsuggestionsmax", MakeIntEntry<&State::SettingsData::matchmakingMaxSuggestions>() },
                { "matchmakingnpccooldowndays", MakeIntEntry<&State::SettingsData::matchmakingNpcCooldownDays>() },
                { "matchmakingorientation",
                  MakeEnumIntEntry<State::MatchmakingOrientation,
                                   &State::SettingsData::matchmakingOrientation,
                                   static_cast<std::int32_t>(State::MatchmakingOrientation::HeteroAndLesbian)>() },

                { "playerpartnercheatingmaxrollcap",
                  MakeIntEntry<&State::SettingsData::playerPartnerCheatingMaxRollCap>() },
                { "playerpartnercheatingmaxsecretlovers",
                  MakeIntEntry<&State::SettingsData::playerPartnerCheatingMaxSecretLovers>() },

                { "dicerollchancenewdating", MakeIntEntry<&State::SettingsData::diceRollChanceNewDating>() },
                { "dicerollchancenewengaged", MakeIntEntry<&State::SettingsData::diceRollChanceNewEngaged>() },
                { "dicerollchancenewloverallowed",
                  MakeIntEntry<&State::SettingsData::diceRollChanceNewLoverAllowed>() },
                { "dicerollchancenewlovercheating",
                  MakeIntEntry<&State::SettingsData::diceRollChanceNewLoverCheating>() },
                { "dicerollchanceadvance", MakeIntEntry<&State::SettingsData::diceRollChanceAdvance>() },
                { "dicerollchancebreakup", MakeIntEntry<&State::SettingsData::diceRollChanceBreakup>() },
                { "dicerollchanceopenclose", MakeIntEntry<&State::SettingsData::diceRollChanceOpenClose>() },

                { "paternitybasechance", MakeIntEntry<&State::SettingsData::paternityBaseChance>() },
                { "paternitymaxchance", MakeIntEntry<&State::SettingsData::paternityMaxChance>() },
                { "paternityphenotypebasechance", MakeIntEntry<&State::SettingsData::paternityPhenotypeBaseChance>() },
                { "paternityphenotypemaxchance", MakeIntEntry<&State::SettingsData::paternityPhenotypeMaxChance>() },
                { "paternityphenotypebirthchance", MakeIntEntry<&State::SettingsData::paternityPhenotypeBirthChance>() },
                { "paternitynonskyrimnetrevealchance",
                  MakeIntEntry<&State::SettingsData::paternityNonSkyrimNetRevealChance>() },
                { "paternitybreakupchance", MakeIntEntry<&State::SettingsData::paternityBreakupChance>() },
                { "paternitybiostepupchance", MakeIntEntry<&State::SettingsData::paternityBioStepUpChance>() },

                { "childtrainingdurationdays",
                  MakeClampedIntEntry<&State::SettingsData::childTrainingDurationDays, 1, 120>() },
                { "childtrainingcost", MakeClampedIntEntry<&State::SettingsData::childTrainingCost, 0, 10000>() }
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, FloatSettingEntry>& GetFloatSettingMap() {
            static const std::unordered_map<std::string_view, FloatSettingEntry> kMap = {
                { "affectiondecaymultiplier", MakeFloatEntry<&State::SettingsData::affectionDecayMultiplier>() },
                { "skyrimnetactionradius", MakeFloatEntry<&State::SettingsData::skyrimnetActionRadius>() },

                { "playerpartnercheatingchancemultiplier",
                  MakeFloatEntry<&State::SettingsData::playerPartnerCheatingChanceMultiplier>() },
                { "playerpartneragencyopenloverchancemultiplier",
                  MakeFloatEntry<&State::SettingsData::playerPartnerAgencyOpenLoverChanceMultiplier>() },
                { "playerpartneragencybreakupchancemultiplier",
                  MakeFloatEntry<&State::SettingsData::playerPartnerAgencyBreakupChanceMultiplier>() },

                { "multlonelinessdecaystart", MakeFloatEntry<&State::SettingsData::multLonelinessDecayStart>() },
                { "multpartneragencyscaninterval",
                  MakeFloatEntry<&State::SettingsData::multPartnerAgencyScanInterval>() },
                { "multrecentattentionwindow", MakeFloatEntry<&State::SettingsData::multRecentAttentionWindow>() },
                { "multveryrecentvisitwindow", MakeFloatEntry<&State::SettingsData::multVeryRecentVisitWindow>() },
                { "multneglectgrowthstart", MakeFloatEntry<&State::SettingsData::multNeglectGrowthStart>() },
                { "multcontentneglectstart", MakeFloatEntry<&State::SettingsData::multContentNeglectStart>() },
                { "multprolongedabsencepenalty",
                  MakeFloatEntry<&State::SettingsData::multProlongedAbsencePenalty>() },
                { "multaffairtalkingescalation",
                  MakeFloatEntry<&State::SettingsData::multAffairTalkingEscalation>() },
                { "multminsideflingageforbreakup",
                  MakeFloatEntry<&State::SettingsData::multMinSideFlingAgeForBreakup>() },
                { "multpartnerintentttl", MakeFloatEntry<&State::SettingsData::multPartnerIntentTTL>() },
                { "multhomearrivalcooldown", MakeFloatEntry<&State::SettingsData::multHomeArrivalCooldown>() },
                { "multaffairfatiguestart", MakeFloatEntry<&State::SettingsData::multAffairFatigueStart>() },
                { "multromanticattendedbreakup",
                  MakeFloatEntry<&State::SettingsData::multRomanticAttendedBreakup>() },
                { "multsuggestopenneglectmin", MakeFloatEntry<&State::SettingsData::multSuggestOpenNeglectMin>() },

                { "paternitydailygrowth", MakeFloatEntry<&State::SettingsData::paternityDailyGrowth>() },
                { "paternityphenotypedailygrowth",
                  MakeFloatEntry<&State::SettingsData::paternityPhenotypeDailyGrowth>() },

                { "uiscale", MakeClampedFloatEntry<&State::SettingsData::uiScale, 0.75f, 1.5f>() }
            };
            return kMap;
        }

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

        const auto& map = GetBoolSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            return it->second.get(*Snapshot());
        }
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

        const auto& map = GetBoolSettingMap();
        const auto it = map.find(lower);
        if (it == map.end()) {
            return false;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            it->second.set(*this, ws.settings.Write(), value);
        });
        return true;
    }

    std::int32_t SettingsCoordinator::GetIntByKey(std::string_view key) const {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "startdialgender") {
            return GetStartDialGender();
        }
        if (lower == "minrelrankfordial") {
            return GetMinRelRankForDial();
        }

        const auto& map = GetIntSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            return it->second.get(*Snapshot());
        }
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

        const auto& map = GetIntSettingMap();
        const auto it = map.find(lower);
        if (it == map.end()) {
            return false;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            it->second.set(*this, ws.settings.Write(), value);
        });
        return true;
    }

    float SettingsCoordinator::GetFloatByKey(std::string_view key) const {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        if (lower == "scenescooldownhours") {
            return Snapshot()->scenesCooldownHours;
        }

        const auto& map = GetFloatSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            return it->second.get(*Snapshot());
        }
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

        const auto& map = GetFloatSettingMap();
        const auto it = map.find(lower);
        if (it == map.end()) {
            return false;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            it->second.set(*this, ws.settings.Write(), value);
        });
        return true;
    }

    void SettingsCoordinator::ParseKeyCombo(const std::string& comboStr, State::SettingsData& out) {
        out.toggleUIModCtrl = false;
        out.toggleUIModShift = false;
        out.toggleUIModAlt = false;
        out.toggleUIKey = 0;

        std::stringstream ss(comboStr);
        std::string token;
        while (std::getline(ss, token, '+')) {
            const std::string trimmed = Foundation::StringUtils::TrimStd(token);
            if (trimmed.empty()) {
                continue;
            }
            const std::string lower = Foundation::StringUtils::ToLowerStd(trimmed);
            if (lower == "ctrl" || lower == "control") {
                out.toggleUIModCtrl = true;
            } else if (lower == "shift") {
                out.toggleUIModShift = true;
            } else if (lower == "alt") {
                out.toggleUIModAlt = true;
            } else {
                const std::uint32_t sc = ParseSingleKey(trimmed);
                if (sc != 0) {
                    out.toggleUIKey = sc;
                }
            }
        }

        if (out.toggleUIKey == 0) {
            out.toggleUIKey = 0x30u;  // Default 'B'
            out.toggleUIModAlt = true;
        }
    }

    std::uint32_t SettingsCoordinator::ParseSingleKey(const std::string& keyStr) {
        static const std::unordered_map<std::string_view, std::uint32_t> kKeyMap = {
            // Letters (DirectInput scancodes)
            { "a", 0x1E }, { "b", 0x30 }, { "c", 0x2E }, { "d", 0x20 }, { "e", 0x12 },
            { "f", 0x21 }, { "g", 0x22 }, { "h", 0x23 }, { "i", 0x17 }, { "j", 0x24 },
            { "k", 0x25 }, { "l", 0x26 }, { "m", 0x32 }, { "n", 0x31 }, { "o", 0x18 },
            { "p", 0x19 }, { "q", 0x10 }, { "r", 0x13 }, { "s", 0x1F }, { "t", 0x14 },
            { "u", 0x16 }, { "v", 0x2F }, { "w", 0x11 }, { "x", 0x2D }, { "y", 0x15 },
            { "z", 0x2C },
            // Numbers
            { "1", 0x02 }, { "2", 0x03 }, { "3", 0x04 }, { "4", 0x05 }, { "5", 0x06 },
            { "6", 0x07 }, { "7", 0x08 }, { "8", 0x09 }, { "9", 0x0A }, { "0", 0x0B },
            // Function keys
            { "f1", 0x3B },  { "f2", 0x3C },  { "f3", 0x3D },  { "f4", 0x3E },
            { "f5", 0x3F },  { "f6", 0x40 },  { "f7", 0x41 },  { "f8", 0x42 },
            { "f9", 0x43 },  { "f10", 0x44 }, { "f11", 0x57 }, { "f12", 0x58 },
            // Common special keys
            { "esc", 0x01 },       { "escape", 0x01 },
            { "enter", 0x1C },     { "return", 0x1C },
            { "space", 0x39 },     { "tab", 0x0F },
            { "backspace", 0x0E }, { "back", 0x0E },
            { "tilde", 0x29 },     { "grave", 0x29 },     { "`", 0x29 },
            { "minus", 0x0C },     { "-", 0x0C },
            { "equals", 0x0D },    { "=", 0x0D },
            { "lbracket", 0x1A },  { "[", 0x1A },
            { "rbracket", 0x1B },  { "]", 0x1B },
            { "backslash", 0x2B }, { "\\", 0x2B },
            { "semicolon", 0x27 }, { ";", 0x27 },
            { "apostrophe", 0x28 },{ "'", 0x28 },
            { "comma", 0x33 },     { ",", 0x33 },
            { "period", 0x34 },    { ".", 0x34 },
            { "slash", 0x35 },     { "/", 0x35 },
            { "capslock", 0x3A },  { "numlock", 0x45 },   { "scrolllock", 0x46 },
            // Navigation
            { "insert", 0xD2 },    { "ins", 0xD2 },
            { "delete", 0xD3 },    { "del", 0xD3 },
            { "home", 0xC7 },      { "end", 0xCF },
            { "pageup", 0xC9 },    { "pgup", 0xC9 },
            { "pagedown", 0xD1 },  { "pgdn", 0xD1 },
            { "up", 0xC8 },        { "down", 0xD0 },
            { "left", 0xCB },      { "right", 0xCD },
            // Numpad
            { "num0", 0x52 },      { "numpad0", 0x52 },
            { "num1", 0x4F },      { "numpad1", 0x4F },
            { "num2", 0x50 },      { "numpad2", 0x50 },
            { "num3", 0x51 },      { "numpad3", 0x51 },
            { "num4", 0x4B },      { "numpad4", 0x4B },
            { "num5", 0x4C },      { "numpad5", 0x4C },
            { "num6", 0x4D },      { "numpad6", 0x4D },
            { "num7", 0x47 },      { "numpad7", 0x47 },
            { "num8", 0x48 },      { "numpad8", 0x48 },
            { "num9", 0x49 },      { "numpad9", 0x49 },
            { "nummult", 0x37 },   { "numminus", 0x4A },
            { "numplus", 0x4E },   { "numdecimal", 0x53 },
            { "numenter", 0x9C },  { "numdivide", 0xB5 }
        };

        const std::string lower = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimStd(keyStr));
        if (const auto it = kKeyMap.find(lower); it != kKeyMap.end()) {
            return it->second;
        }
        return 0;
    }

}  // namespace MARAS::App
