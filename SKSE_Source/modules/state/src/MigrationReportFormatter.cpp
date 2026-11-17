#include "maras/state/MigrationReportFormatter.h"

#include "maras/state/CoSaveStream.h"

#include <algorithm>
#include <format>

namespace MARAS::State {

    namespace {

        [[nodiscard]] std::string DescribeCnfgSubVersion(const CoSaveLoadReport& report) {
            if (!report.cnfgPresent) {
                return "absent";
            }
            if (!report.cnfgHasVersionHeader) {
                return "headerless-legacy";
            }
            return std::format("sub-v{}", report.cnfgSubVersion);
        }

        [[nodiscard]] std::string DescribeChunkVersionSpan(const CoSaveLoadReport& report) {
            if (report.minChunkVersion == 0 && report.maxChunkVersion == 0) {
                return "none";
            }
            if (report.minChunkVersion == report.maxChunkVersion) {
                return std::format("v{}", report.maxChunkVersion);
            }
            return std::format("v{}..v{}", report.minChunkVersion, report.maxChunkVersion);
        }

    }  // namespace

    LoadedDomainCounts SummarizeLoadedDomainCounts(const WorldState& state) noexcept {
        LoadedDomainCounts c{};
        c.profiles = state.actors->Size();
        c.edges = state.relationships->EdgeCount();
        for (const auto& [pair, edge] : state.relationships->AllEdges()) {
            (void)pair;
            if (edge.status == RelationshipStatus::Married) {
                ++c.marriedEdges;
            } else if (edge.status == RelationshipStatus::Engaged) {
                ++c.engagedEdges;
            }
        }
        for (const ActorId sp : state.hierarchy->Slots()) {
            if (!sp.IsNull()) {
                ++c.hierarchyOccupiedSlots;
            }
        }
        c.affairs = state.affairs->Size();
        c.childrenTotal = state.lineage->Size();
        for (const auto& [cid, child] : state.lineage->AllChildren()) {
            (void)cid;
            switch (child.stage) {
                case ChildStage::Unborn:
                    ++c.childrenUnborn;
                    break;
                case ChildStage::Infant:
                    ++c.childrenInfant;
                    break;
                case ChildStage::Child:
                    ++c.childrenChild;
                    break;
                case ChildStage::Training:
                    ++c.childrenTraining;
                    break;
                case ChildStage::Adult:
                    ++c.childrenAdult;
                    break;
                default:
                    break;
            }
            if (child.paternitySecret.isSecret && !child.paternitySecret.discovered) {
                ++c.activePaternitySecrets;
            }
        }
        c.houses = state.household->AllHouses().size();
        c.tenants = state.household->AllTenants().size();
        c.sharedCells = state.household->AllSharedCells().size();
        c.deceased = state.deceased->Size();
        c.childFactionSnapshots = state.household->AllChildFactionSnapshots().size();
        c.adultChildTravels = state.household->AllAdultChildTravel().size();
        return c;
    }

    std::string DescribeSaveOriginVersion(const CoSaveLoadReport& report) {
        if (report.loadedChunkCount == 0 && report.failedChunkCount == 0 && report.chunks.empty()) {
            return "New Game / No Prior MARAS Co-Save";
        }
        const std::string chunkSpan = DescribeChunkVersionSpan(report);
        const std::string cnfgDesc = DescribeCnfgSubVersion(report);
        if (!report.savedModVersion.empty()) {
            return std::format("{} ('MARS' {}, CNFG {})", report.savedModVersion, chunkSpan, cnfgDesc);
        }
        const bool isPreV7OrLegacyCnfg =
            (report.minChunkVersion > 0 && report.minChunkVersion < CoSaveConstants::kDataVersion) ||
            (report.cnfgPresent && (!report.cnfgHasVersionHeader ||
                                    report.cnfgSubVersion < CoSaveConstants::kSettingsSubVersion));
        if (isPreV7OrLegacyCnfg) {
            return std::format("Legacy MARAS (<= v2.5.1.alpha | 'MARS' {}, CNFG {})", chunkSpan, cnfgDesc);
        }
        return std::format("MARAS pre-version-stamp ('MARS' {}, CNFG {})", chunkSpan, cnfgDesc);
    }

    std::string FormatCoSaveAuditLogLine(
        const CoSaveLoadReport& report,
        std::string_view currentModVersion) {
        const auto& n = report.normalization;
        return std::format(
            "[MigrationAudit] Origin: {} -> Current: {} ('MARS' v{}, CNFG sub-v{}) | "
            "chunks(loaded={}, failed={}, skipped={}, upgradedFromOlder={}) | "
            "formIDs(resolved={}, remapped={}, unresolved={}) | "
            "repairs(prunedDynChildren={}, prunedDynAdults={}, deadAdults={}, deadChildren={}, evictedTenants={}, inferredPresumedFathers={}, retiredMarkers={})",
            DescribeSaveOriginVersion(report),
            currentModVersion,
            CoSaveConstants::kDataVersion,
            CoSaveConstants::kSettingsSubVersion,
            report.loadedChunkCount,
            report.failedChunkCount,
            report.skippedChunkCount,
            report.upgradedChunkCount,
            report.resolvedFormIdCount,
            report.remappedFormIdCount,
            report.unresolvedFormIdCount,
            n.prunedOrphanedDynamicChildren,
            n.prunedOrphanedDynamicAdults,
            n.reconciledDeadAdults,
            n.reconciledDeadChildren,
            n.evictedStaleTenants,
            n.inferredLegacyPresumedFathers,
            report.retiredSceneMarkers.size());
    }

    std::string FormatDomainCountsAuditLogLine(const WorldState& state) {
        const LoadedDomainCounts c = SummarizeLoadedDomainCounts(state);
        return std::format(
            "[MigrationAudit] Domain counts: profiles={}, edges={} (married={}, engaged={}), hierarchySlots={}, "
            "affairs={}, children={} (unborn={}, infant={}, child={}, training={}, adult={}), paternitySecrets={}, "
            "houses={}, tenants={}, sharedCells={}, deceased={}, childFactionSnapshots={}, adultTravels={}",
            c.profiles,
            c.edges,
            c.marriedEdges,
            c.engagedEdges,
            c.hierarchyOccupiedSlots,
            c.affairs,
            c.childrenTotal,
            c.childrenUnborn,
            c.childrenInfant,
            c.childrenChild,
            c.childrenTraining,
            c.childrenAdult,
            c.activePaternitySecrets,
            c.houses,
            c.tenants,
            c.sharedCells,
            c.deceased,
            c.childFactionSnapshots,
            c.adultChildTravels);
    }

    std::string BuildFailureModalIssuesSummary(
        const CoSaveLoadReport& report,
        std::span<const ExternalMigrationIssue> externalIssues) {
        std::string out;
        for (const auto& ch : report.chunks) {
            if (!ch.succeeded) {
                if (!out.empty()) {
                    out.push_back('\n');
                }
                out.append(std::format("• Co-Save Chunk '{}' (v{}): {}",
                                       FourCCToString(ch.type),
                                       ch.version,
                                       ch.detail));
            }
        }
        for (const auto& issue : externalIssues) {
            if (!out.empty()) {
                out.push_back('\n');
            }
            out.append(std::format("• {}: {}", issue.source, issue.detail));
        }
        if (out.empty()) {
            out = "• Unknown save migration error (see report file for full details).";
        }
        return out;
    }

    std::string BuildMigrationFailureReportText(
        const WorldState& state,
        const CoSaveLoadReport& report,
        std::string_view currentModVersion,
        std::span<const ExternalMigrationIssue> externalIssues,
        std::string_view timestampText,
        bool isSyntheticPreview) {
        std::string text;
        text.reserve(4096);

        text.append("================================================================================\n");
        text.append("M.A.R.A.S — SAVE DATA MIGRATION FAILURE REPORT\n");
        if (isSyntheticPreview) {
            text.append("*** SYNTHETIC CONSOLE TEST PREVIEW (NO REAL SAVE CORRUPTION) ***\n");
        }
        text.append("================================================================================\n");
        text.append(std::format("Generated At        : {}\n", timestampText.empty() ? "Unknown" : timestampText));
        text.append(std::format("Current MARAS Build : {} ('MARS' schema v{}, CNFG sub-v{})\n",
                                currentModVersion,
                                CoSaveConstants::kDataVersion,
                                CoSaveConstants::kSettingsSubVersion));
        text.append(std::format("Detected Save Origin: {}\n", DescribeSaveOriginVersion(report)));
        text.append("\n");

        text.append("PLAYER INSTRUCTIONS:\n");
        text.append("  1. DO NOT save or overwrite your current Skyrim save file.\n");
        text.append("  2. Close Skyrim and reinstall the previous version of M.A.R.A.S you used on this save.\n");
        text.append("  3. Send this file (MARAS_MigrationReport.log) along with MARAS.log to the mod author.\n");
        text.append("\n");

        text.append("--------------------------------------------------------------------------------\n");
        text.append("1. DETECTED MIGRATION ERRORS\n");
        text.append("--------------------------------------------------------------------------------\n");
        text.append(BuildFailureModalIssuesSummary(report, externalIssues));
        text.append("\n\n");

        text.append("--------------------------------------------------------------------------------\n");
        text.append("2. SKSE CO-SAVE CHUNK BREAKDOWN ('MARS')\n");
        text.append("--------------------------------------------------------------------------------\n");
        text.append(std::format("Summary: loaded={}, failed={}, skipped={}, upgradedFromOlder={}\n",
                                report.loadedChunkCount,
                                report.failedChunkCount,
                                report.skippedChunkCount,
                                report.upgradedChunkCount));
        if (report.chunks.empty()) {
            text.append("  (No 'MARS' co-save chunks were present in this save stream.)\n");
        } else {
            for (const auto& ch : report.chunks) {
                text.append(std::format("  - [{}] '{}' (0x{:08X}) | v{} | bytes {}/{} | {}\n",
                                        ch.succeeded ? "OK  " : "FAIL",
                                        FourCCToString(ch.type),
                                        ch.type,
                                        ch.version,
                                        ch.bytesRead,
                                        ch.byteLength,
                                        ch.detail));
            }
        }
        text.append("\n");

        text.append("--------------------------------------------------------------------------------\n");
        text.append("3. FORM-ID RESOLUTION & POST-LOAD NORMALIZATION\n");
        text.append("--------------------------------------------------------------------------------\n");
        const auto& n = report.normalization;
        text.append(std::format("  Resolved FormIDs                 : {}\n", report.resolvedFormIdCount));
        text.append(std::format("  Load-Order Remapped FormIDs      : {}\n", report.remappedFormIdCount));
        text.append(std::format("  Unresolved / Missing Mod FormIDs : {} (normal if 3rd-party mods were removed)\n",
                                report.unresolvedFormIdCount));
        text.append(std::format("  Pruned Orphaned 0xFF Children    : {}\n", n.prunedOrphanedDynamicChildren));
        text.append(std::format("  Pruned Orphaned 0xFF Adults      : {}\n", n.prunedOrphanedDynamicAdults));
        text.append(std::format("  Reconciled Dead Adults           : {}\n", n.reconciledDeadAdults));
        text.append(std::format("  Reconciled Dead Children         : {}\n", n.reconciledDeadChildren));
        text.append(std::format("  Evicted Stale Tenants            : {}\n", n.evictedStaleTenants));
        text.append(std::format("  Inferred Legacy Presumed Fathers : {}\n", n.inferredLegacyPresumedFathers));
        text.append(std::format("  Retired Scene Markers            : {}\n", report.retiredSceneMarkers.size()));
        text.append("\n");

        text.append("--------------------------------------------------------------------------------\n");
        text.append("4. DECODED DOMAIN RECORD COUNTS (PARTIAL / POST-LOAD STATE)\n");
        text.append("--------------------------------------------------------------------------------\n");
        text.append("  ");
        text.append(FormatDomainCountsAuditLogLine(state));
        text.append("\n================================================================================\n");

        return text;
    }

}  // namespace MARAS::State
