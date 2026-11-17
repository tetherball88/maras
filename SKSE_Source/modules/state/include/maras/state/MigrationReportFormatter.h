#pragma once

#include "maras/state/CoSaveCodec.h"
#include "maras/state/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace MARAS::State {

    struct ExternalMigrationIssue {
        std::string source{};
        std::string detail{};
    };

    struct LoadedDomainCounts {
        std::size_t profiles{ 0 };
        std::size_t edges{ 0 };
        std::size_t marriedEdges{ 0 };
        std::size_t engagedEdges{ 0 };
        std::size_t hierarchyOccupiedSlots{ 0 };
        std::size_t affairs{ 0 };
        std::size_t childrenTotal{ 0 };
        std::size_t childrenUnborn{ 0 };
        std::size_t childrenInfant{ 0 };
        std::size_t childrenChild{ 0 };
        std::size_t childrenTraining{ 0 };
        std::size_t childrenAdult{ 0 };
        std::size_t activePaternitySecrets{ 0 };
        std::size_t houses{ 0 };
        std::size_t tenants{ 0 };
        std::size_t sharedCells{ 0 };
        std::size_t deceased{ 0 };
        std::size_t childFactionSnapshots{ 0 };
        std::size_t adultChildTravels{ 0 };
    };

    [[nodiscard]] LoadedDomainCounts SummarizeLoadedDomainCounts(const WorldState& state) noexcept;

    [[nodiscard]] std::string DescribeSaveOriginVersion(const CoSaveLoadReport& report);

    [[nodiscard]] std::string FormatCoSaveAuditLogLine(
        const CoSaveLoadReport& report,
        std::string_view currentModVersion);

    [[nodiscard]] std::string FormatDomainCountsAuditLogLine(const WorldState& state);

    [[nodiscard]] std::string BuildFailureModalIssuesSummary(
        const CoSaveLoadReport& report,
        std::span<const ExternalMigrationIssue> externalIssues);

    [[nodiscard]] std::string BuildMigrationFailureReportText(
        const WorldState& state,
        const CoSaveLoadReport& report,
        std::string_view currentModVersion,
        std::span<const ExternalMigrationIssue> externalIssues,
        std::string_view timestampText,
        bool isSyntheticPreview = false);

}  // namespace MARAS::State
