#include "maras/bridge_papyrus/SanityConsoleCommand.h"

#include "RE/Skyrim.h"

#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/MigrationDiagnosticsCoordinator.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StrongIds.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <array>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::PapyrusInterface {

    namespace {

        constexpr std::string_view kSanityScriptName = "TTM_SanitySuite";

        // Unused developer/debug console stubs in shipping Skyrim SE/AE/VR that carry a valid
        // ObScript compileFunction. LocateConsoleCommand skips any slot already renamed by another SKSE plugin.
        constexpr std::array<std::string_view, 6> kHostCandidates = {
            "ClearAdaptedLight",
            "DumpTexturePalette",
            "ToggleWaterRadius",
            "DumpNiUpdates",
            "ToggleCastShadows",
            "TestSeenData",
        };

        void PrintConsoleLine(std::string_view message) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                const std::string owned(message);
                console->Print("%s", owned.c_str());
            }
        }

        [[nodiscard]] std::string_view StripSurroundingQuotes(std::string_view value) noexcept {
            value = Foundation::StringUtils::TrimView(value);
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = Foundation::StringUtils::TrimView(value.substr(1, value.size() - 2));
            }
            return value;
        }

        [[nodiscard]] std::string_view ResolveSanityFunctionName(std::string_view step) noexcept {
            using Foundation::StringUtils::CaseInsensitiveEquals;

            if (CaseInsensitiveEquals(step, "prep") || CaseInsensitiveEquals(step, "prep_environment")) {
                return "PrepEnvironment";
            }
            if (CaseInsensitiveEquals(step, "s1") || CaseInsensitiveEquals(step, "scenario1")) {
                return "RunScenario1";
            }
            if (CaseInsensitiveEquals(step, "s2") || CaseInsensitiveEquals(step, "scenario2")) {
                return "RunScenario2";
            }
            if (CaseInsensitiveEquals(step, "s3") || CaseInsensitiveEquals(step, "scenario3")) {
                return "RunScenario3";
            }
            if (CaseInsensitiveEquals(step, "s4_pre") || CaseInsensitiveEquals(step, "scenario4_pre")) {
                return "RunScenario4PreSave";
            }
            if (CaseInsensitiveEquals(step, "s4_post") || CaseInsensitiveEquals(step, "scenario4_post")) {
                return "RunScenario4PostLoad";
            }
            if (CaseInsensitiveEquals(step, "all_pre") || CaseInsensitiveEquals(step, "all")) {
                return "RunAllPreSave";
            }
            return {};
        }

        [[nodiscard]] bool IsLoadedSessionReady() {
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                return false;
            }
            return RE::PlayerCharacter::GetSingleton() != nullptr;
        }

        void CleanupPreviousDynamicSanityChildren() {
            std::lock_guard<MARAS::PapyrusDomainMutex> lock(MARAS::GetPapyrusMutex());
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (!snap || !snap->lineage) {
                return;
            }
            std::vector<RE::FormID> toRemove;
            for (const auto& [cid, rec] : snap->lineage->AllChildren()) {
                if (rec.name == "SanityAdultFF" || rec.name == "SanityOrphanFF") {
                    toRemove.push_back(cid.raw());
                }
            }
            for (const RE::FormID rawId : toRemove) {
                (void)App::LineageUseCases::GetSingleton().RemoveChild(rawId);
                if (Foundation::IsDynamicRuntimeActorRawId(rawId)) {
                    (void)ChildSpawnService::GetSingleton().DisableAndDeleteActor(rawId);
                }
            }
        }

        void MarkSanityOrphanDynamicChildrenDeleted() {
            std::lock_guard<MARAS::PapyrusDomainMutex> lock(MARAS::GetPapyrusMutex());
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (!snap || !snap->lineage) {
                return;
            }
            for (const auto& [cid, rec] : snap->lineage->AllChildren()) {
                if (rec.name == "SanityOrphanFF" && Foundation::IsDynamicRuntimeActorRawId(cid.raw())) {
                    if (ChildSpawnService::GetSingleton().DisableAndDeleteActor(cid.raw())) {
                        MARAS_LOG_INFO(
                            "[SanityConsoleCommand] Marked dynamic child {:08X} ('SanityOrphanFF') deleted for co-save orphan-pruning check",
                            cid.raw());
                    }
                }
            }
        }

        class SanityCallCallback final : public RE::BSScript::IStackCallbackFunctor {
        public:
            SanityCallCallback(SessionEpoch expectedEpoch, std::string functionName) noexcept
                : expectedEpoch_(expectedEpoch), functionName_(std::move(functionName)) {}

            ~SanityCallCallback() override = default;

            void operator()(RE::BSScript::Variable /*a_result*/) override {
                if (!IsSessionEpochCurrent(expectedEpoch_)) {
                    return;
                }
                MARAS_LOG_INFO("[SanityConsoleCommand] Completed {}.{}()", kSanityScriptName, functionName_);
                if (functionName_ == "RunScenario4PreSave" || functionName_ == "RunAllPreSave") {
                    const SessionEpoch ep = expectedEpoch_;
                    (void)AddEpochTask(ep, "SanityConsoleCommand::MarkOrphanDeleted", [ep]() {
                        if (!IsSessionEpochCurrent(ep)) {
                            return;
                        }
                        MarkSanityOrphanDynamicChildrenDeleted();
                    });
                }
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& /*a_object*/) override {}

        private:
            SessionEpoch expectedEpoch_{ 0 };
            std::string functionName_;
        };

        void PrintUsage() {
            PrintConsoleLine(
                "Usage: maras_sanity <prep | s1 | s2 | s3 | s4_pre | s4_post | all_pre | dump [tag] | migration_test>");
        }

        bool ExecuteSanityConsoleCommand(
            const RE::SCRIPT_PARAMETER* a_paramInfo,
            RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
            RE::TESObjectREFR* a_thisObj,
            RE::TESObjectREFR* a_containingObj,
            RE::Script* a_scriptObj,
            RE::ScriptLocals* a_locals,
            double& a_result,
            std::uint32_t& a_opcodeOffsetPtr) {
            a_result = 0.0;

            char rawStep[0x400]{};
            char rawTag[0x400]{};
            if (a_paramInfo && a_scriptData) {
                if (!RE::Script::ParseParameters(
                        a_paramInfo,
                        a_scriptData,
                        a_opcodeOffsetPtr,
                        a_thisObj,
                        a_containingObj,
                        a_scriptObj,
                        a_locals,
                        static_cast<char*>(rawStep),
                        static_cast<char*>(rawTag))) {
                    PrintConsoleLine("MARAS Sanity: failed to parse console parameters.");
                    PrintUsage();
                    return true;
                }
            }
            rawStep[sizeof(rawStep) - 1] = '\0';
            rawTag[sizeof(rawTag) - 1] = '\0';

            const std::string_view step = StripSurroundingQuotes(rawStep);
            const std::string_view extraTag = StripSurroundingQuotes(rawTag);

            if (step.empty() ||
                Foundation::StringUtils::CaseInsensitiveEquals(step, "help") ||
                step == "?") {
                PrintUsage();
                return true;
            }

            if (!IsLoadedSessionReady()) {
                PrintConsoleLine("MARAS Sanity: load a save game before running scenarios or dumps.");
                MARAS_LOG_WARN("[SanityConsoleCommand] Ignored 'maras_sanity {}' while no save session is active", step);
                return true;
            }

            if (Foundation::StringUtils::CaseInsensitiveEquals(step, "dump")) {
                const std::string dumpTag = extraTag.empty() ? "console_manual" : std::string(extraTag);
                std::string dumpPath;
                try {
                    std::lock_guard<MARAS::PapyrusDomainMutex> lock(MARAS::GetPapyrusMutex());
                    dumpPath = App::GetUiBridgeGateway().DumpStoredData(dumpTag);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[SanityConsoleCommand] Exception during DumpStoredData('{}'): {}", dumpTag, e.what());
                    PrintConsoleLine(std::format("MARAS Sanity ERROR: dump '{}' failed ({})", dumpTag, e.what()));
                    return true;
                }

                if (dumpPath.empty()) {
                    PrintConsoleLine(std::format("MARAS Sanity ERROR: dump '{}' failed (check MARAS.log).", dumpTag));
                } else {
                    PrintConsoleLine(std::format("MARAS Sanity: wrote dump -> {}", dumpPath));
                }
                return true;
            }

            if (Foundation::StringUtils::CaseInsensitiveEquals(step, "migration_test")) {
                const std::string reportPath =
                    App::MigrationDiagnosticsCoordinator::GetSingleton().TriggerPreviewFailureForTesting();
                PrintConsoleLine(std::format(
                    "MARAS Sanity: queued migration failure preview modal & wrote report -> {} (close console ~ to view modal).",
                    reportPath));
                return true;
            }

            const std::string_view fnName = ResolveSanityFunctionName(step);
            if (fnName.empty()) {
                PrintConsoleLine(std::format("MARAS Sanity: unknown step '{}'.", step));
                PrintUsage();
                return true;
            }

            if (fnName == "PrepEnvironment" || fnName == "RunAllPreSave") {
                CleanupPreviousDynamicSanityChildren();
            }

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                MARAS_LOG_ERROR("[SanityConsoleCommand] VirtualMachine singleton unavailable for {}.{}()",
                                kSanityScriptName,
                                fnName);
                PrintConsoleLine("MARAS Sanity ERROR: Papyrus VirtualMachine unavailable.");
                return true;
            }

            const SessionEpoch expectedEpoch = GetSessionEpoch();
            auto* args = RE::MakeFunctionArguments();
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                new SanityCallCallback(expectedEpoch, std::string(fnName)));

            if (!vm->DispatchStaticCall(
                    RE::BSFixedString(kSanityScriptName),
                    RE::BSFixedString(fnName),
                    args,
                    callback)) {
                MARAS_LOG_ERROR(
                    "[SanityConsoleCommand] DispatchStaticCall({}.{}) failed — ensure Scripts/TTM_SanitySuite.pex is deployed",
                    kSanityScriptName,
                    fnName);
                PrintConsoleLine(std::format(
                    "MARAS Sanity ERROR: failed to dispatch {}.{}() (ensure Scripts/TTM_SanitySuite.pex is deployed).",
                    kSanityScriptName,
                    fnName));
                return true;
            }

            MARAS_LOG_INFO("[SanityConsoleCommand] Dispatched {}.{}() via console step '{}'",
                           kSanityScriptName,
                           fnName,
                           step);
            PrintConsoleLine(std::format(
                "MARAS Sanity: queued {}.{}() - close console (~) to let Papyrus execute.",
                kSanityScriptName,
                fnName));
            return true;
        }

    }  // namespace

    bool RegisterSanityConsoleCommand() {
        static bool s_registered = false;
        if (s_registered) {
            return true;
        }

        RE::SCRIPT_FUNCTION* cmd = nullptr;
        std::string_view claimedSlot{};
        for (const std::string_view candidate : kHostCandidates) {
            if (auto* found = RE::SCRIPT_FUNCTION::LocateConsoleCommand(candidate);
                found != nullptr && found->compileFunction != nullptr) {
                cmd = found;
                claimedSlot = candidate;
                break;
            }
        }

        if (!cmd) {
            MARAS_LOG_ERROR("[SanityConsoleCommand] Could not locate an available host SCRIPT_FUNCTION slot");
            return false;
        }

        static RE::SCRIPT_PARAMETER s_params[] = {
            { "Step (prep|s1|s2|s3|s4_pre|s4_post|all_pre|dump)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Tag (optional, for dump)", RE::SCRIPT_PARAM_TYPE::kChar, true },
        };

        cmd->functionName = "maras_sanity";
        cmd->shortName = "msanity";
        cmd->helpString = "maras_sanity <prep|s1|s2|s3|s4_pre|s4_post|all_pre|dump> [tag]";
        cmd->referenceFunction = false;
        cmd->SetParameters(s_params);
        cmd->executeFunction = &ExecuteSanityConsoleCommand;
        cmd->conditionFunction = nullptr;
        cmd->editorFilter = false;
        cmd->invalidatesCellList = false;

        s_registered = true;
        MARAS_LOG_INFO("[SanityConsoleCommand] Registered console command 'maras_sanity' ('msanity') on slot '{}'",
                       claimedSlot);
        return true;
    }

}  // namespace MARAS::PapyrusInterface
