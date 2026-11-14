#pragma once

#include "maras/engine/EngineLogging.h"

#include <string>
#include <vector>

namespace MARAS {

    struct UniqueNPCInfo {
        std::string name;
        std::string editorID;
        RE::FormID baseID{ 0 };
        std::string raceName;
        std::string sex;
        bool isChild{ false };
    };

    struct ExcludedNPCInfo {
        std::string name;
        std::string editorID;
        RE::FormID baseID{ 0 };
        std::string reason;
        bool isChild{ false };
    };

    struct UpgradedRefInfo {
        std::string name;
        std::string editorID;
        RE::FormID baseID{ 0 };
        RE::FormID refID{ 0 };
        std::string cellName;
    };

    namespace NPCIndexReportWriter {
        void WriteReport(std::vector<UniqueNPCInfo> uniqueBase,
                         std::vector<UpgradedRefInfo> upgraded,
                         std::vector<ExcludedNPCInfo> excludedUnique,
                         std::vector<ExcludedNPCInfo> excludedNonUnique);
    }  // namespace NPCIndexReportWriter

}  // namespace MARAS
