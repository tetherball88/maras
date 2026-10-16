#include "utils/JsonOverrideLoader.h"

#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/StringUtils.h"

namespace MARAS::Utils {

    // Static member initialization
    JsonOverrideLoader::LoadStatistics JsonOverrideLoader::s_lastStats{};

    bool JsonOverrideLoader::ScanFormDataJsonFolder(const std::string& folderPath,
                                                    std::string_view logContext,
                                                    size_t& outTotalFiles,
                                                    size_t& outSuccessfulFiles,
                                                    size_t& outTotalEntries,
                                                    size_t& outValidEntries,
                                                    size_t& outInvalidFormKeys,
                                                    const FormEntryCallback& onEntry) {
        namespace fs = std::filesystem;

        outTotalFiles = 0;
        outSuccessfulFiles = 0;
        outTotalEntries = 0;
        outValidEntries = 0;
        outInvalidFormKeys = 0;

        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
            MARAS_LOG_ERROR("[{}] Config folder does not exist: {}", logContext, folderPath);
            return false;
        }

        MARAS_LOG_INFO("[{}] Loading JSON configurations from folder: {}", logContext, folderPath);

        try {
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (IsValidJsonFile(entry.path())) {
                    outTotalFiles++;

                    if (ScanFormDataJsonFile(entry.path(), logContext, outTotalEntries, outValidEntries, outInvalidFormKeys,
                                             onEntry)) {
                        outSuccessfulFiles++;
                    }
                }
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[{}] Error scanning folder '{}': {}", logContext, folderPath, e.what());
            return false;
        }

        return outSuccessfulFiles > 0;
    }

    bool JsonOverrideLoader::ScanFormDataJsonFile(const std::filesystem::path& filePath,
                                                  std::string_view logContext,
                                                  size_t& ioTotalEntries,
                                                  size_t& ioValidEntries,
                                                  size_t& ioInvalidFormKeys,
                                                  const FormEntryCallback& onEntry) {
        if (!IsValidJsonFile(filePath)) {
            MARAS_LOG_WARN("[{}] Invalid JSON file: {}", logContext, filePath.string());
            return false;
        }

        MARAS_LOG_DEBUG("[{}] Loading JSON file: {}", logContext, filePath.string());

        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                MARAS_LOG_ERROR("[{}] Cannot open file: {}", logContext, filePath.string());
                return false;
            }

            nlohmann::json json = nlohmann::json::parse(file, nullptr, false, true);
            if (json.is_discarded()) {
                MARAS_LOG_ERROR("[{}] Failed to parse JSON syntax in file: {}", logContext, filePath.string());
                return false;
            }

            if (!json.is_object()) {
                MARAS_LOG_ERROR("[{}] JSON file is not an object: {}", logContext, filePath.string());
                return false;
            }

            size_t fileEntries = 0;

            for (const auto& [key, value] : json.items()) {
                if (key.starts_with("__metaInfo")) {
                    continue;
                }

                if (!key.starts_with("__formData|")) {
                    MARAS_LOG_WARN("[{}] Ignoring non-form key: {}", logContext, key);
                    continue;
                }

                ioTotalEntries++;

                auto parsedKey = ParseFormKey(key);
                if (!parsedKey.isValid) {
                    ioInvalidFormKeys++;
                    MARAS_LOG_WARN("[{}] Failed to parse form key: {}", logContext, key);
                    continue;
                }

                if (onEntry && onEntry(key, parsedKey, value)) {
                    ioValidEntries++;
                    fileEntries++;
                }
            }

            MARAS_LOG_INFO("[{}] Loaded {} entries from file: {}", logContext, fileEntries, filePath.string());
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[{}] Failed to load JSON from '{}': {}", logContext, filePath.string(), e.what());
            return false;
        }
    }

    bool JsonOverrideLoader::LoadOverridesFromFolder(const std::string& folderPath, OverrideMap& outOverrides) {
        s_lastStats = LoadStatistics{};

        const bool ok = ScanFormDataJsonFolder(
            folderPath, "JsonOverrideLoader", s_lastStats.totalFiles, s_lastStats.successfulFiles,
            s_lastStats.totalOverrides, s_lastStats.validOverrides, s_lastStats.invalidFormKeys,
            [&](const std::string& key, const ParsedFormKey& parsedKey, const nlohmann::json& value) {
                auto* form = LookupForm<RE::TESForm>(parsedKey.localFormID, parsedKey.pluginName, "JsonOverrideLoader");
                if (!form) {
                    s_lastStats.unresolvedForms++;
                    MARAS_LOG_WARN("Form not found for key: {}", key);
                    return false;
                }

                const FormID resolvedFormID = form->GetFormID();
                NPCOverrideData overrideData = ParseOverrideObject(value);
                if (!ValidateOverrideData(overrideData)) {
                    MARAS_LOG_WARN("Invalid override data for form {:08X}", resolvedFormID);
                    return false;
                }

                if (!overrideData.HasAnyOverride()) {
                    return false;
                }

                MARAS_LOG_DEBUG("Loaded override for {:08X}: social={}, skill={}, temperament={}", resolvedFormID,
                                overrideData.socialClass.empty() ? "none" : overrideData.socialClass,
                                overrideData.skillType.empty() ? "none" : overrideData.skillType,
                                overrideData.temperament.empty() ? "none" : overrideData.temperament);

                outOverrides[resolvedFormID] = std::move(overrideData);
                return true;
            });

        MARAS_LOG_INFO("Override loading complete. Files: {}/{}, Overrides: {}/{}, Invalid keys: {}, Unresolved: {}",
                       s_lastStats.successfulFiles, s_lastStats.totalFiles, s_lastStats.validOverrides,
                       s_lastStats.totalOverrides, s_lastStats.invalidFormKeys, s_lastStats.unresolvedForms);

        return ok;
    }

    bool JsonOverrideLoader::LoadOverridesFromFile(const std::string& filePath, OverrideMap& outOverrides) {
        return ParseJsonFile(filePath, outOverrides);
    }

    bool JsonOverrideLoader::ParseJsonFile(const std::string& filePath, OverrideMap& outOverrides) {
        return ScanFormDataJsonFile(
            filePath, "JsonOverrideLoader", s_lastStats.totalOverrides, s_lastStats.validOverrides,
            s_lastStats.invalidFormKeys,
            [&](const std::string& key, const ParsedFormKey& parsedKey, const nlohmann::json& value) {
                auto* form = LookupForm<RE::TESForm>(parsedKey.localFormID, parsedKey.pluginName, "JsonOverrideLoader");
                if (!form) {
                    s_lastStats.unresolvedForms++;
                    MARAS_LOG_WARN("Form not found for key: {}", key);
                    return false;
                }

                const FormID resolvedFormID = form->GetFormID();
                NPCOverrideData overrideData = ParseOverrideObject(value);
                if (!ValidateOverrideData(overrideData)) {
                    MARAS_LOG_WARN("Invalid override data for form {:08X}", resolvedFormID);
                    return false;
                }

                if (!overrideData.HasAnyOverride()) {
                    return false;
                }

                MARAS_LOG_DEBUG("Loaded override for {:08X}: social={}, skill={}, temperament={}", resolvedFormID,
                                overrideData.socialClass.empty() ? "none" : overrideData.socialClass,
                                overrideData.skillType.empty() ? "none" : overrideData.skillType,
                                overrideData.temperament.empty() ? "none" : overrideData.temperament);

                outOverrides[resolvedFormID] = std::move(overrideData);
                return true;
            });
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
        // Validate social class
        if (!data.socialClass.empty()) {
            auto socialClass = StringToSocialClass(data.socialClass);
            auto expected = SocialClassToString(socialClass);
            if (!StringUtils::CaseInsensitiveEquals(expected, data.socialClass)) {
                MARAS_LOG_WARN("Invalid social class: {}", data.socialClass);
                return false;
            }
        }

        // Validate skill type
        if (!data.skillType.empty()) {
            auto skillType = StringToSkillType(data.skillType);
            auto expected = SkillTypeToString(skillType);
            if (!StringUtils::CaseInsensitiveEquals(expected, data.skillType)) {
                MARAS_LOG_WARN("Invalid skill type: {}", data.skillType);
                return false;
            }
        }

        // Validate temperament
        if (!data.temperament.empty()) {
            auto temperament = StringToTemperament(data.temperament);
            auto expected = TemperamentToString(temperament);
            if (!StringUtils::CaseInsensitiveEquals(expected, data.temperament)) {
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
