#include "core/NPCIndexReportWriter.h"

#include "utils/StringUtils.h"

namespace MARAS {

    namespace NPCIndexReportWriter {

        void DumpPromotionsReport(std::vector<UniqueNPCInfo>&& uniqueBase,
                                  std::vector<UpgradedRefInfo>&& upgraded,
                                  std::vector<ExcludedNPCInfo>&& excludedUnique,
                                  std::vector<ExcludedNPCInfo>&& excludedNonUnique) {
            std::filesystem::path dumpsDir = "Data/SKSE/Plugins/MARAS/dumps";
            std::error_code ec;
            std::filesystem::create_directories(dumpsDir, ec);
            if (ec) {
                MARAS_LOG_WARN("NPCIndex: Failed to create dumps directory '{}': {}", dumpsDir.string(), ec.message());
                return;
            }

            std::filesystem::path fullPath = dumpsDir / "npc_index_promotions.txt";
            std::ofstream out(fullPath);
            if (!out.is_open()) {
                MARAS_LOG_WARN("NPCIndex: Could not open dump file at '{}'", fullPath.string());
                return;
            }

            auto nameComparator = [](const auto& a, const auto& b) {
                int cmp = StringUtils::CaseInsensitiveCompare(a.name, b.name);
                if (cmp != 0) return cmp < 0;
                return a.baseID < b.baseID;
            };

            auto reasonNameComparator = [](const auto& a, const auto& b) {
                if (a.reason != b.reason) return a.reason < b.reason;
                int cmp = StringUtils::CaseInsensitiveCompare(a.name, b.name);
                if (cmp != 0) return cmp < 0;
                return a.baseID < b.baseID;
            };

            std::sort(uniqueBase.begin(), uniqueBase.end(), nameComparator);
            std::sort(upgraded.begin(), upgraded.end(), nameComparator);
            std::sort(excludedUnique.begin(), excludedUnique.end(), reasonNameComparator);
            std::sort(excludedNonUnique.begin(), excludedNonUnique.end(), reasonNameComparator);

            out << "================================================================================\n";
            out << "                     MARAS NPC INDEX PROMOTIONS & FILTER REPORT                 \n";
            out << "================================================================================\n\n";

            out << "[PHASE 1: Unique Base NPCs Indexed]\n";
            out << "Total Unique Base Forms: " << uniqueBase.size() << "\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << std::left << std::setw(12) << "Base FormID"
                << std::setw(66) << "EditorID"
                << std::setw(8)  << "Type"
                << std::setw(8)  << "Sex"
                << std::setw(18) << "Race"
                << "Name\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (const auto& u : uniqueBase) {
                std::string edid = u.editorID.empty() ? "(None)" : u.editorID;
                out << fmt::format("0x{:08X}  {:<42} {:<6} {:<6} {:<16} {}\n",
                                   u.baseID, edid, (u.isChild ? "Child" : "Adult"), u.sex, u.raceName, u.name);
            }

            out << "\n========================================================================================================================\n";
            out << "[PHASE 1.5 & 1.6: Base FormIDs Upgraded to Placed References]\n";
            out << "Total Upgraded: " << upgraded.size() << "\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << std::left << std::setw(12) << "Base FormID"
                << std::setw(14) << "Ref FormID"
                << std::setw(66) << "EditorID"
                << std::setw(30) << "Name"
                << "Cell\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            for (const auto& u : upgraded) {
                std::string edid = u.editorID.empty() ? "(None)" : u.editorID;
                out << fmt::format("0x{:08X}  0x{:08X}  {:<42} {:<28} {}\n",
                                   u.baseID, u.refID, edid, u.name, u.cellName);
            }

            out << "\n========================================================================================================================\n";
            out << "[FILTERED OUT: Initially Unique NPCs Excluded (INI / Rules / Flags)]\n";
            out << "Total Excluded Unique: " << excludedUnique.size() << "\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << std::left << std::setw(12) << "Base FormID"
                << std::setw(66) << "EditorID"
                << std::setw(8)  << "Type"
                << std::setw(30) << "Reason"
                << "Name\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            {
                std::string lastReason;
                for (const auto& eu : excludedUnique) {
                    if (eu.reason != lastReason) {
                        lastReason = eu.reason;
                        out << fmt::format("\n>>> Reason: {}\n", lastReason);
                    }
                    std::string edid = eu.editorID.empty() ? "(None)" : eu.editorID;
                    out << fmt::format("0x{:08X}  {:<42} {:<6} {:<28} {}\n",
                                       eu.baseID, edid, (eu.isChild ? "Child" : "Adult"), eu.reason, eu.name);
                }
            }

            out << "\n========================================================================================================================\n";
            out << "[FILTERED OUT: Non-Unique NPCs Excluded by INI Overrides / Candidate Rules]\n";
            out << "Total Excluded Non-Unique: " << excludedNonUnique.size() << "\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            out << std::left << std::setw(12) << "Base FormID"
                << std::setw(66) << "EditorID"
                << std::setw(8)  << "Type"
                << std::setw(30) << "Reason"
                << "Name\n";
            out << "------------------------------------------------------------------------------------------------------------------------\n";
            {
                std::string lastReason;
                for (const auto& enu : excludedNonUnique) {
                    if (enu.reason != lastReason) {
                        lastReason = enu.reason;
                        out << fmt::format("\n>>> Reason: {}\n", lastReason);
                    }
                    std::string edid = enu.editorID.empty() ? "(None)" : enu.editorID;
                    out << fmt::format("0x{:08X}  {:<42} {:<6} {:<28} {}\n",
                                       enu.baseID, edid, (enu.isChild ? "Child" : "Adult"), enu.reason, enu.name);
                }
            }

            out << "================================================================================\n";
            out.close();

            MARAS_LOG_INFO("NPCIndex: Dumped promotions & filter report to '{}' ({} unique indexed, {} upgraded, {} unique excluded, {} non-unique excluded)",
                           fullPath.string(), uniqueBase.size(), upgraded.size(), excludedUnique.size(), excludedNonUnique.size());
        }

    }  // namespace NPCIndexReportWriter

}  // namespace MARAS
