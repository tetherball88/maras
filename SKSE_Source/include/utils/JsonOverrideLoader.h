#pragma once

#include <functional>

#include "utils/Common.h"
#include "utils/FormUtils.h"

namespace MARAS::Utils {

    // Structure to hold override data for a single NPC
    struct NPCOverrideData {
        std::string comment;      // Optional comment field
        std::string socialClass;  // Empty if no override
        std::string skillType;    // Empty if no override
        std::string temperament;  // Empty if no override

        NPCOverrideData() = default;

        bool HasSocialClassOverride() const { return !socialClass.empty(); }
        bool HasSkillTypeOverride() const { return !skillType.empty(); }
        bool HasTemperamentOverride() const { return !temperament.empty(); }
        bool HasAnyOverride() const {
            return HasSocialClassOverride() || HasSkillTypeOverride() || HasTemperamentOverride();
        }
    };

    // Container for all loaded overrides
    using OverrideMap = std::unordered_map<FormID, NPCOverrideData>;

    class JsonOverrideLoader {
    public:
        using FormEntryCallback =
            std::function<bool(const std::string& rawKey, const ParsedFormKey& parsedKey, const nlohmann::json& entryValue)>;

        // Shared helpers for scanning __formData|-keyed JSON files/folders
        static bool ScanFormDataJsonFolder(const std::string& folderPath,
                                           std::string_view logContext,
                                           size_t& outTotalFiles,
                                           size_t& outSuccessfulFiles,
                                           size_t& outTotalEntries,
                                           size_t& outValidEntries,
                                           size_t& outInvalidFormKeys,
                                           const FormEntryCallback& onEntry);

        static bool ScanFormDataJsonFile(const std::filesystem::path& filePath,
                                         std::string_view logContext,
                                         size_t& ioTotalEntries,
                                         size_t& ioValidEntries,
                                         size_t& ioInvalidFormKeys,
                                         const FormEntryCallback& onEntry);

        static bool IsValidJsonFile(const std::filesystem::path& filePath);

        // Load all override files from a directory
        static bool LoadOverridesFromFolder(const std::string& folderPath, OverrideMap& outOverrides);

        // Load overrides from a single JSON file
        static bool LoadOverridesFromFile(const std::string& filePath, OverrideMap& outOverrides);

        // Validate override values against known enum types
        static bool ValidateOverrideData(const NPCOverrideData& data);

        // Get statistics about loaded overrides
        struct LoadStatistics {
            size_t totalFiles{0};
            size_t successfulFiles{0};
            size_t totalOverrides{0};
            size_t validOverrides{0};
            size_t invalidFormKeys{0};
            size_t unresolvedForms{0};
        };

        static LoadStatistics GetLastLoadStatistics();

    private:
        static LoadStatistics s_lastStats;

        // Helper methods
        static bool ParseJsonFile(const std::string& filePath, OverrideMap& outOverrides);
        static NPCOverrideData ParseOverrideObject(const nlohmann::json& jsonObj);
    };

}  // namespace MARAS::Utils
