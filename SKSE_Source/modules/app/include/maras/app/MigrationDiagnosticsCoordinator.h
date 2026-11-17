#pragma once

#include "maras/state/CoSaveCodec.h"
#include "maras/state/MigrationReportFormatter.h"
#include "maras/state/SessionEpoch.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::App {

    /**
     * Single-responsibility coordinator for save-migration audit logging, persistent
     * `MARAS_MigrationReport.log` generation on real migration failures, and the
     * standalone `<ModalHost />` warning modal protecting pre-upgrade saves.
     */
    class MigrationDiagnosticsCoordinator {
    public:
        static MigrationDiagnosticsCoordinator& GetSingleton();

        MigrationDiagnosticsCoordinator(const MigrationDiagnosticsCoordinator&) = delete;
        MigrationDiagnosticsCoordinator& operator=(const MigrationDiagnosticsCoordinator&) = delete;
        MigrationDiagnosticsCoordinator(MigrationDiagnosticsCoordinator&&) = delete;
        MigrationDiagnosticsCoordinator& operator=(MigrationDiagnosticsCoordinator&&) = delete;

        /// Resets per-save diagnostic state when SKSE reverts runtime state.
        void OnCoSaveRevert() noexcept;

        /// Logs post-load migration & domain counts to `MARAS.log` and, if any chunk failed,
        /// writes `MARAS_MigrationReport.log` and queues the standalone warning modal.
        void OnCoSaveLoaded(const State::CoSaveLoadReport& report);

        /// Records an explicit one-time migration failure (e.g. from Papyrus FMR/BF/Hearthfire
        /// legacy migration), updates `MARAS_MigrationReport.log`, and ensures the warning modal shows.
        void ReportMigrationIssue(std::string_view source, std::string_view detail);

        /// Non-destructive synthetic preview for `msanity migration_test` (writes a clearly labeled
        /// preview `MARAS_MigrationReport.log` and opens the warning modal without altering `WorldStore`).
        std::string TriggerPreviewFailureForTesting();

    private:
        MigrationDiagnosticsCoordinator() = default;
        ~MigrationDiagnosticsCoordinator() = default;

        std::string EmitFailureReportAndMaybeShowModal(
            const State::WorldState& worldState,
            const State::CoSaveLoadReport& report,
            const std::vector<State::ExternalMigrationIssue>& externalIssues,
            State::SessionEpoch epoch,
            bool isSyntheticPreview);

        mutable std::mutex mutex_;
        State::SessionEpoch activeEpoch_{ 0 };
        State::CoSaveLoadReport lastLoadReport_{};
        std::vector<State::ExternalMigrationIssue> externalIssues_{};
        bool modalShownForEpoch_{ false };
    };

}  // namespace MARAS::App
