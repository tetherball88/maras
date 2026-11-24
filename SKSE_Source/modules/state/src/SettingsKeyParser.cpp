#include "maras/state/SettingsKeyParser.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string>
#include <unordered_map>

namespace MARAS::State {

    namespace {

        constexpr std::int32_t kMinLogLevel = 0;
        constexpr std::int32_t kMaxLogLevel = 5;

        struct BoolSettingEntry {
            bool (*get)(const SettingsData&){ nullptr };
            void (*set)(SettingsData&, bool){ nullptr };
        };

        struct IntSettingEntry {
            std::int32_t (*get)(const SettingsData&){ nullptr };
            void (*set)(SettingsData&, std::int32_t){ nullptr };
            bool (*validate)(std::int32_t){ nullptr };
        };

        struct FloatSettingEntry {
            float (*get)(const SettingsData&){ nullptr };
            void (*set)(SettingsData&, float){ nullptr };
        };

        template <bool SettingsData::*Member>
        constexpr BoolSettingEntry MakeBoolEntry() noexcept {
            return {
                [](const SettingsData& s) -> bool { return s.*Member; },
                [](SettingsData& s, bool v) { s.*Member = v; }
            };
        }

        template <std::int32_t SettingsData::*Member>
        constexpr IntSettingEntry MakeIntEntry() noexcept {
            return {
                [](const SettingsData& s) -> std::int32_t { return s.*Member; },
                [](SettingsData& s, std::int32_t v) { s.*Member = v; }
            };
        }

        template <std::int32_t SettingsData::*Member, std::int32_t MinV, std::int32_t MaxV>
        constexpr IntSettingEntry MakeClampedIntEntry() noexcept {
            return {
                [](const SettingsData& s) -> std::int32_t { return s.*Member; },
                [](SettingsData& s, std::int32_t v) { s.*Member = std::clamp(v, MinV, MaxV); }
            };
        }

        template <typename EnumT, EnumT SettingsData::*Member, std::int32_t MaxEnumInt>
        constexpr IntSettingEntry MakeEnumIntEntry() noexcept {
            return {
                [](const SettingsData& s) -> std::int32_t { return static_cast<std::int32_t>(s.*Member); },
                [](SettingsData& s, std::int32_t v) { s.*Member = static_cast<EnumT>(v); },
                [](std::int32_t v) -> bool { return v >= 0 && v <= MaxEnumInt; }
            };
        }

        template <float SettingsData::*Member>
        constexpr FloatSettingEntry MakeFloatEntry() noexcept {
            return {
                [](const SettingsData& s) -> float { return s.*Member; },
                [](SettingsData& s, float v) { s.*Member = v; }
            };
        }

        template <float SettingsData::*Member, float MinV, float MaxV>
        constexpr FloatSettingEntry MakeClampedFloatEntry() noexcept {
            return {
                [](const SettingsData& s) -> float { return s.*Member; },
                [](SettingsData& s, float v) { s.*Member = std::clamp(v, MinV, MaxV); }
            };
        }

        const std::unordered_map<std::string_view, BoolSettingEntry>& GetBoolSettingMap() {
            static const std::unordered_map<std::string_view, BoolSettingEntry> kMap = {
                { "toggleuimodctrl", MakeBoolEntry<&SettingsData::toggleUIModCtrl>() },
                { "toggleuimodshift", MakeBoolEntry<&SettingsData::toggleUIModShift>() },
                { "toggleuimodalt", MakeBoolEntry<&SettingsData::toggleUIModAlt>() },

                { "enableconfirmationmodal", MakeBoolEntry<&SettingsData::enableConfirmationModal>() },
                { "enableconfirmplayernpc", MakeBoolEntry<&SettingsData::enableConfirmPlayerNpc>() },
                { "enableconfirmnpcnpc", MakeBoolEntry<&SettingsData::enableConfirmNpcNpc>() },

                { "confirmstatuschange_dating", MakeBoolEntry<&SettingsData::confirmStatusChange_Dating>() },
                { "confirmstatuschange_engagement", MakeBoolEntry<&SettingsData::confirmStatusChange_Engagement>() },
                { "confirmstatuschange_lover", MakeBoolEntry<&SettingsData::confirmStatusChange_Lover>() },
                { "confirmstatuschange_marry", MakeBoolEntry<&SettingsData::confirmStatusChange_Marry>() },
                { "confirmacceptproposal", MakeBoolEntry<&SettingsData::confirmStatusChange_Marry>() },

                { "confirmbreakup_dating", MakeBoolEntry<&SettingsData::confirmBreakup_Dating>() },
                { "confirmbreakup_engagement", MakeBoolEntry<&SettingsData::confirmBreakup_Engagement>() },
                { "confirmbreakupengagement", MakeBoolEntry<&SettingsData::confirmBreakup_Engagement>() },
                { "confirmbreakup_lover", MakeBoolEntry<&SettingsData::confirmBreakup_Lover>() },
                { "confirmdivorce", MakeBoolEntry<&SettingsData::confirmDivorce>() },
                { "confirmdivorceduringlowaffectionquest",
                  MakeBoolEntry<&SettingsData::confirmDivorceDuringLowAffectionQuest>() },
                { "confirmlowaffectiondivorce",
                  MakeBoolEntry<&SettingsData::confirmDivorceDuringLowAffectionQuest>() },
                { "confirmopenrelationship", MakeBoolEntry<&SettingsData::confirmOpenRelationship>() },
                { "confirmcloserelationship", MakeBoolEntry<&SettingsData::confirmCloseRelationship>() },

                { "skipwedding", MakeBoolEntry<&SettingsData::skipWedding>() },
                { "alwayssuccessmarriage", MakeBoolEntry<&SettingsData::alwaysSuccessMarriage>() },
                { "alwayssuccess", MakeBoolEntry<&SettingsData::alwaysSuccessMarriage>() },
                { "skyrimnetusellmmemories", MakeBoolEntry<&SettingsData::skyrimnetUseLLMMemories>() },
                { "skyrimnetusedecisions", MakeBoolEntry<&SettingsData::skyrimnetUseDecisions>() },
                { "migratedfromstorageutil", MakeBoolEntry<&SettingsData::migratedFromStorageUtil>() },
                { "initialrelationshipscandone", MakeBoolEntry<&SettingsData::initialRelationshipScanDone>() },

                { "childcustodyprioritizebioparent",
                  MakeBoolEntry<&SettingsData::childCustodyPrioritizeBioParent>() },

                { "matchmakingenabled", MakeBoolEntry<&SettingsData::matchmakingEnabled>() },
                { "matchmakingallowbreakups", MakeBoolEntry<&SettingsData::matchmakingAllowBreakups>() },
                { "matchmakingallowopenclose", MakeBoolEntry<&SettingsData::matchmakingAllowOpenClose>() },
                { "matchmakingallowcheating", MakeBoolEntry<&SettingsData::matchmakingAllowCheating>() },
                { "matchmakingusellm", MakeBoolEntry<&SettingsData::matchmakingUseLLM>() },
                { "matchmakingevaluationstep", MakeBoolEntry<&SettingsData::matchmakingEvaluationStep>() },
                { "matchmakinggeneratememories", MakeBoolEntry<&SettingsData::matchmakingGenerateMemories>() },
                { "matchmakingconfirmresults", MakeBoolEntry<&SettingsData::matchmakingConfirmResults>() },

                { "playerpartnercheatingagencyenabled",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingAgencyEnabled>() },
                { "playerpartneropenrelagencyenabled",
                  MakeBoolEntry<&SettingsData::playerPartnerOpenRelAgencyEnabled>() },
                { "playerpartnercheatingallowwhenhappy",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingAllowWhenHappy>() },
                { "playerpartnercheatingenableneglect",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingEnableNeglect>() },
                { "playerpartnercheatingenablerevenge",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingEnableRevenge>() },
                { "playerpartnercheatingenablehierarchy",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingEnableHierarchy>() },
                { "playerpartnercheatingenablechildrenanchor",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingEnableChildrenAnchor>() },
                { "playerpartnercheatingusellm",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingUseLLM>() },
                { "playerpartnercheatingconfirmresults",
                  MakeBoolEntry<&SettingsData::playerPartnerCheatingConfirmResults>() },
                { "playerpartneragencyenableforcegreets",
                  MakeBoolEntry<&SettingsData::playerPartnerAgencyEnableForceGreets>() },
                { "playerpartneragencyenablehomearrivalevents",
                  MakeBoolEntry<&SettingsData::playerPartnerAgencyEnableHomeArrivalEvents>() },

                { "paternityforcedicerolls", MakeBoolEntry<&SettingsData::paternityForceDiceRolls>() },
                { "revealsecretsinui", MakeBoolEntry<&SettingsData::revealSecretsInUI>() },
                { "uifullscreen", MakeBoolEntry<&SettingsData::uiFullScreen>() },
                { "autopauseonopen", MakeBoolEntry<&SettingsData::autoPauseOnOpen>() },

                { "enablescenes", MakeBoolEntry<&SettingsData::enableScenes>() },
                { "enablescenes_lover", MakeBoolEntry<&SettingsData::enableScenes_Lover>() },
                { "enablescenes_dating", MakeBoolEntry<&SettingsData::enableScenes_Dating>() },
                { "enablescenes_engaged", MakeBoolEntry<&SettingsData::enableScenes_Engaged>() },
                { "enablescenes_married", MakeBoolEntry<&SettingsData::enableScenes_Married>() },
                { "enablescenes_openrelationship", MakeBoolEntry<&SettingsData::enableScenes_OpenRelationship>() },
                { "enablescenes_cheating", MakeBoolEntry<&SettingsData::enableScenes_Cheating>() },
                { "enablescenes_cheatingdiscovered",
                  MakeBoolEntry<&SettingsData::enableScenes_CheatingDiscovered>() },
                { "enablescenes_cheatingcaught", MakeBoolEntry<&SettingsData::enableScenes_CheatingCaught>() },

                { "enablescenes_loctypetemple", MakeBoolEntry<&SettingsData::enableScenes_LocTypeTemple>() },
                { "enablescenes_loctypestore", MakeBoolEntry<&SettingsData::enableScenes_LocTypeStore>() },
                { "enablescenes_loctypestewardsdwelling",
                  MakeBoolEntry<&SettingsData::enableScenes_LocTypeStewardsDwelling>() },
                { "enablescenes_loctypeinn", MakeBoolEntry<&SettingsData::enableScenes_LocTypeInn>() },
                { "enablescenes_loctypehouse", MakeBoolEntry<&SettingsData::enableScenes_LocTypeHouse>() },
                { "enablescenes_loctypebarracks", MakeBoolEntry<&SettingsData::enableScenes_LocTypeBarracks>() },
                { "enablescenes_loctypeguild", MakeBoolEntry<&SettingsData::enableScenes_LocTypeGuild>() },
                { "enablescenes_loctypeplayerhouse",
                  MakeBoolEntry<&SettingsData::enableScenes_LocTypePlayerHouse>() },
                { "enablescenes_byoh_loctypehomestead",
                  MakeBoolEntry<&SettingsData::enableScenes_BYOH_LocTypeHomestead>() },
                { "enablescenes_loctypetown", MakeBoolEntry<&SettingsData::enableScenes_LocTypeTown>() },
                { "enablescenes_loctypesettlement",
                  MakeBoolEntry<&SettingsData::enableScenes_LocTypeSettlement>() },
                { "enablescenes_loctypeorcstronghold",
                  MakeBoolEntry<&SettingsData::enableScenes_LocTypeOrcStronghold>() },
                { "enablescenes_loctypecity", MakeBoolEntry<&SettingsData::enableScenes_LocTypeCity>() },
                { "enablescenes_loctypelumbermill",
                  MakeBoolEntry<&SettingsData::enableScenes_LocTypeLumberMill>() },
                { "enablescenes_loctypefarm", MakeBoolEntry<&SettingsData::enableScenes_LocTypeFarm>() },

                { "childtrainingrequirefaction", MakeBoolEntry<&SettingsData::childTrainingRequireFaction>() },

                { "bglobalbancritter", MakeBoolEntry<&SettingsData::bGlobalBanCritter>() },
                { "globalbancritter", MakeBoolEntry<&SettingsData::bGlobalBanCritter>() },
                { "bglobalbanpet", MakeBoolEntry<&SettingsData::bGlobalBanPet>() },
                { "globalbanpet", MakeBoolEntry<&SettingsData::bGlobalBanPet>() }
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, IntSettingEntry>& GetIntSettingMap() {
            static const std::unordered_map<std::string_view, IntSettingEntry> kMap = {
                { "toggleuikey",
                  { [](const SettingsData& s) -> std::int32_t {
                        return static_cast<std::int32_t>(s.toggleUIKey);
                    },
                    [](SettingsData& s, std::int32_t v) {
                        if (v >= 0) {
                            s.toggleUIKey = static_cast<std::uint32_t>(v);
                        }
                    } } },
                { "actionscooldown", MakeIntEntry<&SettingsData::actionsCooldown>() },
                { "actionsdeclinecooldown", MakeIntEntry<&SettingsData::actionsDeclineCooldown>() },
                { "logdestination", MakeIntEntry<&SettingsData::logDestination>() },
                { "defaultloglevel", MakeIntEntry<&SettingsData::defaultLogLevel>() },
                { "loglevel",
                  { [](const SettingsData& s) -> std::int32_t { return s.logLevel; },
                    [](SettingsData& s, std::int32_t v) {
                        s.logLevel = std::clamp(v, kMinLogLevel, kMaxLogLevel);
                    } } },
                { "lonelinessthresholddays", MakeIntEntry<&SettingsData::lonelinessThresholdDays>() },

                { "childcustodygenderpolicy",
                  MakeEnumIntEntry<ChildCustodyGenderPolicy,
                                   &SettingsData::childCustodyGenderPolicy,
                                   static_cast<std::int32_t>(ChildCustodyGenderPolicy::StayWithFather)>() },
                { "childcustodyplayerpolicy",
                  MakeEnumIntEntry<ChildCustodyPlayerPolicy,
                                   &SettingsData::childCustodyPlayerPolicy,
                                   static_cast<std::int32_t>(ChildCustodyPlayerPolicy::NeverWithPlayer)>() },

                { "matchmakingintervaldays", MakeIntEntry<&SettingsData::matchmakingIntervalDays>() },
                { "matchmakingmaxsuggestions", MakeIntEntry<&SettingsData::matchmakingMaxSuggestions>() },
                { "matchmakingsuggestionsmax", MakeIntEntry<&SettingsData::matchmakingMaxSuggestions>() },
                { "matchmakingnpccooldowndays", MakeIntEntry<&SettingsData::matchmakingNpcCooldownDays>() },
                { "matchmakingorientation",
                  MakeEnumIntEntry<MatchmakingOrientation,
                                   &SettingsData::matchmakingOrientation,
                                   static_cast<std::int32_t>(MatchmakingOrientation::HeteroAndLesbian)>() },

                { "playerpartnercheatingmaxrollcap",
                  MakeIntEntry<&SettingsData::playerPartnerCheatingMaxRollCap>() },
                { "playerpartnercheatingmaxsecretlovers",
                  MakeIntEntry<&SettingsData::playerPartnerCheatingMaxSecretLovers>() },

                { "dicerollchancenewdating", MakeIntEntry<&SettingsData::diceRollChanceNewDating>() },
                { "dicerollchancenewengaged", MakeIntEntry<&SettingsData::diceRollChanceNewEngaged>() },
                { "dicerollchancenewloverallowed",
                  MakeIntEntry<&SettingsData::diceRollChanceNewLoverAllowed>() },
                { "dicerollchancenewlovercheating",
                  MakeIntEntry<&SettingsData::diceRollChanceNewLoverCheating>() },
                { "dicerollchanceadvance", MakeIntEntry<&SettingsData::diceRollChanceAdvance>() },
                { "dicerollchancebreakup", MakeIntEntry<&SettingsData::diceRollChanceBreakup>() },
                { "dicerollchanceopenclose", MakeIntEntry<&SettingsData::diceRollChanceOpenClose>() },

                { "paternitybasechance", MakeIntEntry<&SettingsData::paternityBaseChance>() },
                { "paternitymaxchance", MakeIntEntry<&SettingsData::paternityMaxChance>() },
                { "paternityphenotypebasechance", MakeIntEntry<&SettingsData::paternityPhenotypeBaseChance>() },
                { "paternityphenotypemaxchance", MakeIntEntry<&SettingsData::paternityPhenotypeMaxChance>() },
                { "paternityphenotypebirthchance", MakeIntEntry<&SettingsData::paternityPhenotypeBirthChance>() },
                { "paternitynonskyrimnetrevealchance",
                  MakeIntEntry<&SettingsData::paternityNonSkyrimNetRevealChance>() },
                { "paternitybreakupchance", MakeIntEntry<&SettingsData::paternityBreakupChance>() },
                { "paternitybiostepupchance", MakeIntEntry<&SettingsData::paternityBioStepUpChance>() },

                { "childtrainingdurationdays",
                  MakeClampedIntEntry<&SettingsData::childTrainingDurationDays, 1, 120>() },
                { "childtrainingcost", MakeClampedIntEntry<&SettingsData::childTrainingCost, 0, 10000>() }
            };
            return kMap;
        }

        const std::unordered_map<std::string_view, FloatSettingEntry>& GetFloatSettingMap() {
            static const std::unordered_map<std::string_view, FloatSettingEntry> kMap = {
                { "affectiondecaymultiplier", MakeFloatEntry<&SettingsData::affectionDecayMultiplier>() },
                { "affectiondecaymult", MakeFloatEntry<&SettingsData::affectionDecayMultiplier>() },
                { "skyrimnetactionradius", MakeFloatEntry<&SettingsData::skyrimnetActionRadius>() },
                { "actionradius", MakeFloatEntry<&SettingsData::skyrimnetActionRadius>() },

                { "playerpartnercheatingchancemultiplier",
                  MakeFloatEntry<&SettingsData::playerPartnerCheatingChanceMultiplier>() },
                { "playerpartneragencyopenloverchancemultiplier",
                  MakeFloatEntry<&SettingsData::playerPartnerAgencyOpenLoverChanceMultiplier>() },
                { "playerpartneragencybreakupchancemultiplier",
                  MakeFloatEntry<&SettingsData::playerPartnerAgencyBreakupChanceMultiplier>() },

                { "multlonelinessdecaystart", MakeFloatEntry<&SettingsData::multLonelinessDecayStart>() },
                { "multpartneragencyscaninterval",
                  MakeFloatEntry<&SettingsData::multPartnerAgencyScanInterval>() },
                { "multrecentattentionwindow", MakeFloatEntry<&SettingsData::multRecentAttentionWindow>() },
                { "multveryrecentvisitwindow", MakeFloatEntry<&SettingsData::multVeryRecentVisitWindow>() },
                { "multneglectgrowthstart", MakeFloatEntry<&SettingsData::multNeglectGrowthStart>() },
                { "multcontentneglectstart", MakeFloatEntry<&SettingsData::multContentNeglectStart>() },
                { "multprolongedabsencepenalty",
                  MakeFloatEntry<&SettingsData::multProlongedAbsencePenalty>() },
                { "multaffairtalkingescalation",
                  MakeFloatEntry<&SettingsData::multAffairTalkingEscalation>() },
                { "multminsideflingageforbreakup",
                  MakeFloatEntry<&SettingsData::multMinSideFlingAgeForBreakup>() },
                { "multpartnerintentttl", MakeFloatEntry<&SettingsData::multPartnerIntentTTL>() },
                { "multhomearrivalcooldown", MakeFloatEntry<&SettingsData::multHomeArrivalCooldown>() },
                { "multaffairfatiguestart", MakeFloatEntry<&SettingsData::multAffairFatigueStart>() },
                { "multromanticattendedbreakup",
                  MakeFloatEntry<&SettingsData::multRomanticAttendedBreakup>() },
                { "multsuggestopenneglectmin", MakeFloatEntry<&SettingsData::multSuggestOpenNeglectMin>() },

                { "paternitydailygrowth", MakeFloatEntry<&SettingsData::paternityDailyGrowth>() },
                { "paternityphenotypedailygrowth",
                  MakeFloatEntry<&SettingsData::paternityPhenotypeDailyGrowth>() },

                { "uiscale", MakeClampedFloatEntry<&SettingsData::uiScale, 0.75f, 1.5f>() }
            };
            return kMap;
        }

    }  // namespace

    std::uint32_t ParseSingleKey(std::string_view keyStr) {
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

        const std::string lower = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(keyStr));
        if (const auto it = kKeyMap.find(lower); it != kKeyMap.end()) {
            return it->second;
        }
        if (lower.size() > 2 && lower[0] == '0' && lower[1] == 'x') {
            std::uint32_t hexVal = 0;
            const auto [ptr, ec] = std::from_chars(lower.data() + 2, lower.data() + lower.size(), hexVal, 16);
            if (ec == std::errc{} && ptr == lower.data() + lower.size()) {
                return hexVal;
            }
        } else if (!lower.empty()) {
            std::uint32_t decVal = 0;
            const auto [ptr, ec] = std::from_chars(lower.data(), lower.data() + lower.size(), decVal, 10);
            if (ec == std::errc{} && ptr == lower.data() + lower.size()) {
                return decVal;
            }
        }
        return 0;
    }

    HotkeyParseResult ParseKeyCombo(std::string_view comboStr, SettingsData& out) {
        HotkeyParseResult res{};
        out.toggleUIModCtrl = false;
        out.toggleUIModShift = false;
        out.toggleUIModAlt = false;
        out.toggleUIKey = 0;

        const std::string trimmedWhole = Foundation::StringUtils::TrimStd(std::string(comboStr));
        const std::string lowerWhole = Foundation::StringUtils::ToLowerStd(trimmedWhole);
        if (trimmedWhole.empty() || lowerWhole == "0" || lowerWhole == "0x0" || lowerWhole == "0x00" ||
            lowerWhole == "none" || lowerWhole == "disabled") {
            res.disabled = true;
            return res;
        }

        std::stringstream ss(trimmedWhole);
        std::string token;
        while (std::getline(ss, token, '+')) {
            const std::string trimmed = Foundation::StringUtils::TrimStd(token);
            if (trimmed.empty()) {
                continue;
            }
            const std::string lower = Foundation::StringUtils::ToLowerStd(trimmed);
            if (lower == "ctrl" || lower == "control" || lower == "lctrl" || lower == "rctrl") {
                out.toggleUIModCtrl = true;
            } else if (lower == "shift" || lower == "lshift" || lower == "rshift") {
                out.toggleUIModShift = true;
            } else if (lower == "alt" || lower == "lalt" || lower == "ralt") {
                out.toggleUIModAlt = true;
            } else {
                const std::uint32_t sc = ParseSingleKey(trimmed);
                if (sc != 0) {
                    out.toggleUIKey = sc;
                }
            }
        }

        if (out.toggleUIKey == 0) {
            out.toggleUIModCtrl = false;
            out.toggleUIModShift = false;
            out.toggleUIModAlt = true;
            out.toggleUIKey = 0x30u;  // Default 'B'
            res.fellBackToDefault = true;
        }
        return res;
    }

    bool TryGetBoolSetting(const SettingsData& s, std::string_view key, bool& outValue) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetBoolSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            outValue = it->second.get(s);
            return true;
        }
        return false;
    }

    bool TrySetBoolSetting(SettingsData& s, std::string_view key, bool value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetBoolSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            it->second.set(s, value);
            return true;
        }
        return false;
    }

    bool TryGetIntSetting(const SettingsData& s, std::string_view key, std::int32_t& outValue) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetIntSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            outValue = it->second.get(s);
            return true;
        }
        return false;
    }

    SettingWriteStatus TrySetIntSetting(SettingsData& s, std::string_view key, std::int32_t value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetIntSettingMap();
        const auto it = map.find(lower);
        if (it == map.end()) {
            return SettingWriteStatus::UnknownKey;
        }
        if (it->second.validate && !it->second.validate(value)) {
            return SettingWriteStatus::OutOfRange;
        }
        it->second.set(s, value);
        return SettingWriteStatus::Ok;
    }

    bool TryGetFloatSetting(const SettingsData& s, std::string_view key, float& outValue) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        if (const auto it = map.find(lower); it != map.end()) {
            outValue = it->second.get(s);
            return true;
        }
        return false;
    }

    SettingWriteStatus TrySetFloatSetting(SettingsData& s, std::string_view key, float value) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(key);
        const auto& map = GetFloatSettingMap();
        const auto it = map.find(lower);
        if (it == map.end()) {
            return SettingWriteStatus::UnknownKey;
        }
        it->second.set(s, value);
        return SettingWriteStatus::Ok;
    }

}  // namespace MARAS::State
