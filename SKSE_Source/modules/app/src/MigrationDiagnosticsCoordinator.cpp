#include "maras/app/MigrationDiagnosticsCoordinator.h"

#include "RE/Skyrim.h"

#include "maras/app/AppContext.h"
#include "maras/app/ModalTypes.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>

namespace MARAS::App {

    namespace {

        [[nodiscard]] std::string CurrentLocalTimestampString() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTm{};
            if (localtime_s(&localTm, &rawTime) != 0) {
                return "Unknown";
            }
            char buf[32]{};
            if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTm) == 0) {
                return "Unknown";
            }
            return std::string(buf);
        }

    }  // namespace

    MigrationDiagnosticsCoordinator& MigrationDiagnosticsCoordinator::GetSingleton() {
        static MigrationDiagnosticsCoordinator instance;
        return instance;
    }

    void MigrationDiagnosticsCoordinator::OnCoSaveRevert() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        activeEpoch_ = MARAS::GetSessionEpoch();
        lastLoadReport_ = {};
        externalIssues_.clear();
        modalShownForEpoch_ = false;
    }

    void MigrationDiagnosticsCoordinator::OnCoSaveLoaded(const State::CoSaveLoadReport& report) {
        const State::WorldState worldCopy = State::WorldStore::GetSingleton().ReadLive(
            [](const State::WorldState& ws) { return ws; });
        const std::string_view currentModVer = Engine::GetInstalledModVersion();

        MARAS_LOG_INFO("{}", State::FormatCoSaveAuditLogLine(report, currentModVer));
        if (report.loadedChunkCount > 0 || report.failedChunkCount > 0) {
            MARAS_LOG_INFO("{}", State::FormatDomainCountsAuditLogLine(worldCopy));
        }
        if (report.unresolvedFormIdCount > 0) {
            MARAS_LOG_INFO(
                "[MigrationAudit] {} saved FormID(s) did not resolve in the current load order and were pruned cleanly",
                report.unresolvedFormIdCount);
        }

        const State::SessionEpoch epoch = MARAS::GetSessionEpoch();
        std::vector<State::ExternalMigrationIssue> issuesSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeEpoch_ != epoch) {
                activeEpoch_ = epoch;
                externalIssues_.clear();
                modalShownForEpoch_ = false;
            }
            lastLoadReport_ = report;
            issuesSnapshot = externalIssues_;
        }

        if (!report.allChunksSucceeded || report.failedChunkCount > 0 || !issuesSnapshot.empty()) {
            (void)EmitFailureReportAndMaybeShowModal(
                worldCopy,
                report,
                issuesSnapshot,
                epoch,
                /*isSyntheticPreview=*/false);
        }
    }

    void MigrationDiagnosticsCoordinator::ReportMigrationIssue(
        std::string_view source,
        std::string_view detail) {
        const std::string srcStr = source.empty() ? std::string("Legacy Migration") : std::string(source);
        const std::string detStr = detail.empty() ? std::string("Unspecified migration failure") : std::string(detail);

        MARAS_LOG_ERROR("[MigrationAudit] Failure reported by '{}': {}", srcStr, detStr);

        const State::WorldState worldCopy = State::WorldStore::GetSingleton().ReadLive(
            [](const State::WorldState& ws) { return ws; });
        const State::SessionEpoch epoch = MARAS::GetSessionEpoch();

        State::CoSaveLoadReport reportCopy{};
        std::vector<State::ExternalMigrationIssue> issuesCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeEpoch_ != epoch) {
                activeEpoch_ = epoch;
                lastLoadReport_ = {};
                externalIssues_.clear();
                modalShownForEpoch_ = false;
            }
            externalIssues_.push_back(State::ExternalMigrationIssue{
                .source = srcStr,
                .detail = detStr,
            });
            reportCopy = lastLoadReport_;
            issuesCopy = externalIssues_;
        }

        (void)EmitFailureReportAndMaybeShowModal(
            worldCopy,
            reportCopy,
            issuesCopy,
            epoch,
            /*isSyntheticPreview=*/false);
    }

    std::string MigrationDiagnosticsCoordinator::TriggerPreviewFailureForTesting() {
        const State::WorldState worldCopy = State::WorldStore::GetSingleton().ReadLive(
            [](const State::WorldState& ws) { return ws; });
        const State::SessionEpoch epoch = MARAS::GetSessionEpoch();

        State::CoSaveLoadReport syntheticReport{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            syntheticReport = lastLoadReport_;
        }
        syntheticReport.allChunksSucceeded = false;
        syntheticReport.failedChunkCount += 1;
        if (syntheticReport.minChunkVersion == 0) {
            syntheticReport.minChunkVersion = 5;
            syntheticReport.maxChunkVersion = 5;
            syntheticReport.upgradedChunkCount = 1;
        }
        syntheticReport.chunks.push_back(State::CoSaveChunkDiagnostic{
            .type = State::CoSaveConstants::kChunkPLCH,
            .version = 5,
            .byteLength = 256,
            .bytesRead = 148,
            .succeeded = false,
            .detail = "Synthetic preview: simulated decode failure at byte 148 of 256 (no real save data altered)",
        });

        const std::vector<State::ExternalMigrationIssue> previewIssues{
            State::ExternalMigrationIssue{
                .source = "Console Preview (msanity migration_test)",
                .detail = "This is a non-destructive UI & log test. Your actual save data was NOT harmed.",
            },
        };

        return EmitFailureReportAndMaybeShowModal(
            worldCopy,
            syntheticReport,
            previewIssues,
            epoch,
            /*isSyntheticPreview=*/true);
    }

    std::string MigrationDiagnosticsCoordinator::EmitFailureReportAndMaybeShowModal(
        const State::WorldState& worldState,
        const State::CoSaveLoadReport& report,
        const std::vector<State::ExternalMigrationIssue>& externalIssues,
        State::SessionEpoch epoch,
        bool isSyntheticPreview) {
        const std::string_view currentModVer = Engine::GetInstalledModVersion();
        const std::string timestamp = CurrentLocalTimestampString();
        const std::string reportText = State::BuildMigrationFailureReportText(
            worldState,
            report,
            currentModVer,
            externalIssues,
            timestamp,
            isSyntheticPreview);

        const std::filesystem::path writtenPath = Engine::WriteMigrationReportFile(reportText);
        const std::string displayPath = writtenPath.empty()
                                            ? std::string("Documents\\My Games\\Skyrim Special Edition\\SKSE\\MARAS_MigrationReport.log")
                                            : writtenPath.string();

        MARAS_LOG_ERROR(
            "[MigrationAudit] Wrote save migration failure report to '{}'",
            displayPath);

        if (!isSyntheticPreview) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeEpoch_ == epoch && modalShownForEpoch_) {
                return displayPath;
            }
            activeEpoch_ = epoch;
            modalShownForEpoch_ = true;
        }

        const std::string originVersion = State::DescribeSaveOriginVersion(report);
        const std::string issuesSummary = State::BuildFailureModalIssuesSummary(report, externalIssues);

        ModalRequest req{};
        req.modalType = "save_migration_failed";
        req.tone = ModalTone::Danger;
        req.size = ModalSize::Lg;
        req.defaultCancelButtonIndex = 1;
        req.title = MARAS::TrOr(
            "modals.save_migration_failed.title",
            "M.A.R.A.S — Save Data Migration Failed");
        req.subtitle = MARAS::TrFmtOr(
            "modals.save_migration_failed.subtitle",
            "Saved: {origin}  ->  Current: {current}",
            { { "origin", originVersion }, { "current", std::string(currentModVer) } });
        req.message = MARAS::TrOr(
            "modals.save_migration_failed.message",
            "M.A.R.A.S could not cleanly migrate all data from this save. Playing further and saving over your game may permanently erase unmigrated relationships, children, or household records.");

        req.controls.push_back(ModalControl::MakeInfoBox(
            "migration_issues",
            MARAS::TrOr("modals.save_migration_failed.issues_label", "Detected Migration Issues"),
            issuesSummary,
            ModalTone::Danger));

        const std::string instructionsBody = MARAS::TrFmtOr(
            "modals.save_migration_failed.instructions_body",
            "Diagnostic report saved to:\n{path}\n\n"
            "• Step 1: DO NOT save your game. Click 'Quit to Desktop' below.\n"
            "• Step 2: Reinstall the previous version of M.A.R.A.S you used on this save.\n"
            "• Step 3: Send MARAS_MigrationReport.log (and MARAS.log) to the mod author and wait for help.",
            { { "path", displayPath } });

        req.controls.push_back(ModalControl::MakeInfoBox(
            "migration_instructions",
            MARAS::TrOr("modals.save_migration_failed.instructions_label", "How to Protect Your Save & Get Help"),
            instructionsBody,
            ModalTone::Warning));

        req.buttons.push_back(ModalButton::Make(
            "quit_game",
            MARAS::TrOr("modals.save_migration_failed.btn_quit", "Quit to Desktop (Recommended)"),
            ModalButtonVariant::Danger));
        req.buttons.push_back(ModalButton::Make(
            "close_warning",
            MARAS::TrOr("modals.save_migration_failed.btn_close", "Close Warning (Do Not Save)"),
            ModalButtonVariant::Secondary));

        GetModalGateway().ShowModal(std::move(req), [epoch](const ModalResponse& resp) {
            if (resp.aborted || resp.buttonId != "quit_game") {
                return;
            }
            MARAS::AddEpochTask(epoch, "MigrationDiagnosticsCoordinator::QuitGame", []() {
                MARAS_LOG_WARN("[MigrationAudit] User clicked 'Quit to Desktop' from save migration failure modal");
                if (auto* main = RE::Main::GetSingleton()) {
                    main->GetRuntimeData().quitGame = true;
                }
            });
        });

        return displayPath;
    }

}  // namespace MARAS::App
