#include "utils/JsonOverrideLoader.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS::Utils {

    // Static member initialization
    JsonOverrideLoader::LoadStatistics JsonOverrideLoader::s_lastStats{};

    bool JsonOverrideLoader::LoadOverridesFromFolder(const std::string& folderPath, OverrideMap& outOverrides) {
        namespace fs = std::filesystem;

        s_lastStats = LoadStatistics{};

        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
            MARAS_LOG_ERROR("Override folder does not exist: {}", folderPath);
            return false;
        }

        MARAS_LOG_INFO("Loading overrides from folder: {}", folderPath);

        try {
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (IsValidJsonFile(entry.path())) {
                    s_lastStats.totalFiles++;

                    if (LoadOverridesFromFile(entry.path().string(), outOverrides)) {
                        s_lastStats.successfulFiles++;
                    }
                }
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Error scanning override folder '{}': {}", folderPath, e.what());
            return false;
        }

        MARAS_LOG_INFO("Override loading complete. Files: {}/{}, Overrides: {}/{}, Invalid keys: {}, Unresolved: {}",
                       s_lastStats.successfulFiles, s_lastStats.totalFiles, s_lastStats.validOverrides,
                       s_lastStats.totalOverrides, s_lastStats.invalidFormKeys, s_lastStats.unresolvedForms);

        return s_lastStats.successfulFiles > 0;
    }

    bool JsonOverrideLoader::LoadOverridesFromFile(const std::string& filePath, OverrideMap& outOverrides) {
        if (!IsValidJsonFile(filePath)) {
            MARAS_LOG_WARN("Invalid JSON file: {}", filePath);
            return false;
        }

        MARAS_LOG_DEBUG("Loading overrides from file: {}", filePath);

        try {
            return ParseJsonFile(filePath, outOverrides);
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Failed to load overrides from '{}': {}", filePath, e.what());
            return false;
        }
    }

    bool JsonOverrideLoader::ParseJsonFile(const std::string& filePath, OverrideMap& outOverrides) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            MARAS_LOG_ERROR("Cannot open file: {}", filePath);
            return false;
        }

        nlohmann::json json;
        file >> json;

        if (!json.is_object()) {
            MARAS_LOG_ERROR("JSON file is not an object: {}", filePath);
            return false;
        }

        size_t fileOverrides = 0;

        for (const auto& [key, value] : json.items()) {
            // Skip metadata entries
            if (key.starts_with("__metaInfo")) {
                continue;
            }

            // Only process form data keys
            if (!key.starts_with("__formData|")) {
                MARAS_LOG_WARN("Ignoring non-form key: {}", key);
                continue;
            }

            s_lastStats.totalOverrides++;

            // Parse the form key
            auto formID = ParseAndResolveFormKey(key);
            if (!formID.has_value()) {
                s_lastStats.invalidFormKeys++;
                MARAS_LOG_WARN("Failed to parse form key: {}", key);
                continue;
            }

            // Verify form exists
            auto form = RE::TESForm::LookupByID(formID.value());
            if (!form) {
                s_lastStats.unresolvedForms++;
                MARAS_LOG_WARN("Form not found for ID {:08X} from key: {}", formID.value(), key);
                continue;
            }

            // Parse override data
            NPCOverrideData overrideData = ParseOverrideObject(value);

            if (!ValidateOverrideData(overrideData)) {
                MARAS_LOG_WARN("Invalid override data for form {:08X}", formID.value());
                continue;
            }

            // Only store if there are actual overrides
            if (overrideData.HasAnyOverride()) {
                MARAS_LOG_DEBUG("Loaded override for {:08X}: social={}, skill={}, temperament={}", formID.value(),
                                overrideData.socialClass.empty() ? "none" : overrideData.socialClass,
                                overrideData.skillType.empty() ? "none" : overrideData.skillType,
                                overrideData.temperament.empty() ? "none" : overrideData.temperament);

                outOverrides[formID.value()] = std::move(overrideData);
                s_lastStats.validOverrides++;
                fileOverrides++;
            }
        }

        MARAS_LOG_INFO("Loaded {} overrides from file: {}", fileOverrides, filePath);
        return true;
    }

    NPCOverrideData JsonOverrideLoader::ParseOverrideObject(const nlohmann::json& jsonObj) {
        NPCOverrideData data;

        if (jsonObj.is_object()) {
            // Parse comment field
            if (jsonObj.contains("comment") && jsonObj["comment"].is_string()) {
                data.comment = jsonObj["comment"].get<std::string>();
            }

            // Parse override fields
            if (jsonObj.contains("social") && jsonObj["social"].is_string()) {
                data.socialClass = jsonObj["social"].get<std::string>();
            }

            if (jsonObj.contains("skill") && jsonObj["skill"].is_string()) {
                data.skillType = jsonObj["skill"].get<std::string>();
            }

            if (jsonObj.contains("temperament") && jsonObj["temperament"].is_string()) {
                data.temperament = jsonObj["temperament"].get<std::string>();
            }
        }

        return data;
    }

    bool JsonOverrideLoader::ValidateOverrideData(const NPCOverrideData& data) {
        // Helper lambda for case-insensitive comparison
        auto caseInsensitiveEquals = [](std::string_view a, std::string_view b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char ca, char cb) {
                unsigned char ua = static_cast<unsigned char>(ca);
                unsigned char ub = static_cast<unsigned char>(cb);
                return std::tolower(ua) == std::tolower(ub);
            });
        };

        // Validate social class
        if (!data.socialClass.empty()) {
            auto socialClass = StringToSocialClass(data.socialClass);
            auto expected = SocialClassToString(socialClass);
            if (!caseInsensitiveEquals(expected, data.socialClass)) {
                MARAS_LOG_WARN("Invalid social class: {}", data.socialClass);
                return false;
            }
        }

        // Validate skill type
        if (!data.skillType.empty()) {
            auto skillType = StringToSkillType(data.skillType);
            auto expected = SkillTypeToString(skillType);
            if (!caseInsensitiveEquals(expected, data.skillType)) {
                MARAS_LOG_WARN("Invalid skill type: {}", data.skillType);
                return false;
            }
        }

        // Validate temperament
        if (!data.temperament.empty()) {
            auto temperament = StringToTemperament(data.temperament);
            auto expected = TemperamentToString(temperament);
            if (!caseInsensitiveEquals(expected, data.temperament)) {
                MARAS_LOG_WARN("Invalid temperament: {}", data.temperament);
                return false;
            }
        }

        return true;
    }

    bool JsonOverrideLoader::IsValidJsonFile(const std::filesystem::path& filePath) {
        return filePath.extension() == ".json" && std::filesystem::is_regular_file(filePath);
    }

    JsonOverrideLoader::LoadStatistics JsonOverrideLoader::GetLastLoadStatistics() { return s_lastStats; }

}  // namespace MARAS::Utils
