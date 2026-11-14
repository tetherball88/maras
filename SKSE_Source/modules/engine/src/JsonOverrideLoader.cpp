#include "maras/engine/JsonOverrideLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormUtils.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        constexpr std::string_view kFormDataPrefix = "__formData|";

        [[nodiscard]] bool IsStrictSocialClass(std::string_view value) {
            const auto parsed = MARAS::Foundation::StringToSocialClass(value);
            return MARAS::Foundation::StringUtils::CaseInsensitiveEquals(
                MARAS::Foundation::SocialClassToString(parsed), value);
        }

        [[nodiscard]] bool IsStrictSkillType(std::string_view value) {
            const auto parsed = MARAS::Foundation::StringToSkillType(value);
            return MARAS::Foundation::StringUtils::CaseInsensitiveEquals(
                MARAS::Foundation::SkillTypeToString(parsed), value);
        }

        [[nodiscard]] bool IsStrictTemperament(std::string_view value) {
            const auto parsed = MARAS::Foundation::StringToTemperament(value);
            return MARAS::Foundation::StringUtils::CaseInsensitiveEquals(
                MARAS::Foundation::TemperamentToString(parsed), value);
        }

        void AssignOptionalStringField(const nlohmann::json& entry,
                                       std::string_view key,
                                       std::optional<std::string>& outField) {
            const std::string keyStr(key);
            if (!entry.contains(keyStr) || !entry[keyStr].is_string()) {
                return;
            }
            std::string trimmed = MARAS::Foundation::StringUtils::TrimStd(entry[keyStr].get<std::string>());
            if (!trimmed.empty()) {
                outField = std::move(trimmed);
            }
        }

    }  // namespace

    bool JsonOverrideLoader::IsValidJsonFile(const std::filesystem::path& filePath) {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
            return false;
        }
        return MARAS::Foundation::StringUtils::CaseInsensitiveEquals(filePath.extension().string(), ".json");
    }

    bool JsonOverrideLoader::LoadJsonFile(const std::filesystem::path& filePath,
                                          nlohmann::json& outJson,
                                          std::string_view context) {
        const std::string tag = context.empty() ? "JsonOverrideLoader" : std::string(context);
        if (!IsValidJsonFile(filePath)) {
            MARAS_LOG_WARN("[{}] Invalid or missing JSON file: {}", tag, filePath.string());
            return false;
        }

        try {
            std::ifstream stream(filePath);
            if (!stream.is_open()) {
                MARAS_LOG_ERROR("[{}] Failed to open JSON file: {}", tag, filePath.string());
                return false;
            }

            // Allow C-style / line comments inside authored mod config files.
            outJson = nlohmann::json::parse(stream, nullptr, true, true);
            return true;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[{}] Failed to parse JSON '{}': {}", tag, filePath.string(), e.what());
            return false;
        }
    }

    std::vector<std::filesystem::path> JsonOverrideLoader::CollectSortedJsonFiles(
        const std::filesystem::path& folderPath,
        bool recursive) {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            return files;
        }

        auto collectEntry = [&](const std::filesystem::directory_entry& entry) {
            if (IsValidJsonFile(entry.path())) {
                files.push_back(entry.path());
            }
        };

        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath, ec)) {
                if (ec) {
                    break;
                }
                collectEntry(entry);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folderPath, ec)) {
                if (ec) {
                    break;
                }
                collectEntry(entry);
            }
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    bool JsonOverrideLoader::ScanFormDataJsonFile(const std::filesystem::path& filePath,
                                                  const FormDataJsonEntryCallback& callback,
                                                  std::size_t* outTotalEntries) {
        if (outTotalEntries) {
            *outTotalEntries = 0;
        }

        nlohmann::json root;
        if (!LoadJsonFile(filePath, root, "ScanFormDataJsonFile")) {
            return false;
        }
        if (!root.is_object()) {
            MARAS_LOG_WARN("[ScanFormDataJsonFile] Root is not a JSON object in '{}'", filePath.string());
            return false;
        }

        const nlohmann::json* container = &root;
        if (root.contains("NPCs") && root["NPCs"].is_object()) {
            container = &root["NPCs"];
        }

        std::size_t count = 0;
        for (const auto& [key, value] : container->items()) {
            if (!MARAS::Foundation::StringUtils::StartsWith(key, kFormDataPrefix, false)) {
                continue;
            }
            ++count;
            if (callback) {
                callback(key, value, filePath);
            }
        }

        if (outTotalEntries) {
            *outTotalEntries = count;
        }
        return true;
    }

    bool JsonOverrideLoader::ScanFormDataJsonFolder(const std::filesystem::path& folderPath,
                                                    const FormDataJsonEntryCallback& callback,
                                                    std::size_t* outTotalFiles,
                                                    std::size_t* outSuccessfulFiles,
                                                    std::size_t* outTotalEntries) {
        if (outTotalFiles) *outTotalFiles = 0;
        if (outSuccessfulFiles) *outSuccessfulFiles = 0;
        if (outTotalEntries) *outTotalEntries = 0;

        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            MARAS_LOG_WARN("[ScanFormDataJsonFolder] Folder does not exist: {}", folderPath.string());
            return false;
        }

        const auto files = CollectSortedJsonFiles(folderPath, false);
        std::size_t successful = 0;
        std::size_t entries = 0;
        for (const auto& file : files) {
            std::size_t fileEntries = 0;
            if (ScanFormDataJsonFile(file, callback, &fileEntries)) {
                ++successful;
                entries += fileEntries;
            }
        }

        if (outTotalFiles) *outTotalFiles = files.size();
        if (outSuccessfulFiles) *outSuccessfulFiles = successful;
        if (outTotalEntries) *outTotalEntries = entries;
        return true;
    }

    bool JsonOverrideLoader::LoadOverridesFromFolder(const std::filesystem::path& folderPath,
                                                     OverrideMap& outOverrides) {
        lastStats_ = LoadStatistics{};

        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            MARAS_LOG_WARN("[JsonOverrideLoader] Overrides folder does not exist: {}", folderPath.string());
            return false;
        }

        const auto files = CollectSortedJsonFiles(folderPath, false);
        lastStats_.totalFiles = files.size();
        for (const auto& file : files) {
            if (LoadOverridesFromFile(file, outOverrides)) {
                ++lastStats_.successfulFiles;
            }
        }

        MARAS_LOG_INFO(
            "[JsonOverrideLoader] Loaded {} valid NPC override entries from {}/{} files in '{}' (invalidKeys={}, unresolved={})",
            lastStats_.validEntries, lastStats_.successfulFiles, lastStats_.totalFiles, folderPath.string(),
            lastStats_.invalidFormKeys, lastStats_.unresolvedForms);
        return lastStats_.successfulFiles > 0 || files.empty();
    }

    bool JsonOverrideLoader::LoadOverridesFromFile(const std::filesystem::path& filePath, OverrideMap& outOverrides) {
        bool parsedAny = false;
        const bool ok = ScanFormDataJsonFile(
            filePath,
            [&](const std::string& formKey, const nlohmann::json& entryJson, const std::filesystem::path& sourceFile) {
                ++lastStats_.totalEntries;
                parsedAny = true;

                const auto parsedKey = MARAS::Utils::ParseFormKey(formKey);
                if (!parsedKey.isValid) {
                    ++lastStats_.invalidFormKeys;
                    MARAS_LOG_WARN("[JsonOverrideLoader] Skipping invalid form key '{}' in '{}'",
                                   formKey, sourceFile.string());
                    return;
                }

                const auto resolvedId = MARAS::Utils::ParseAndResolveFormKey(formKey);
                if (!resolvedId.has_value()) {
                    ++lastStats_.unresolvedForms;
                    MARAS_LOG_DEBUG("[JsonOverrideLoader] Form '{}' in '{}' did not resolve in active load order",
                                    formKey, sourceFile.string());
                    return;
                }

                const NPCOverrideData data = ParseOverrideEntry(entryJson);
                if (!ValidateOverrideData(data)) {
                    MARAS_LOG_WARN("[JsonOverrideLoader] Invalid override fields for '{}' ({:08X}) in '{}'",
                                   formKey, *resolvedId, sourceFile.string());
                    return;
                }
                if (!data.HasAnyOverride()) {
                    return;
                }

                auto& target = outOverrides[*resolvedId];
                if (data.socialClass.has_value()) target.socialClass = data.socialClass;
                if (data.skillType.has_value()) target.skillType = data.skillType;
                if (data.temperament.has_value()) target.temperament = data.temperament;
                ++lastStats_.validEntries;
            });

        (void)parsedAny;
        return ok;
    }

    NPCOverrideData JsonOverrideLoader::ParseOverrideEntry(const nlohmann::json& entry) {
        NPCOverrideData data;
        if (!entry.is_object()) {
            return data;
        }
        AssignOptionalStringField(entry, "socialClass", data.socialClass);
        AssignOptionalStringField(entry, "skillType", data.skillType);
        AssignOptionalStringField(entry, "temperament", data.temperament);
        return data;
    }

    bool JsonOverrideLoader::ValidateOverrideData(const NPCOverrideData& data) {
        if (data.socialClass.has_value() && !IsStrictSocialClass(*data.socialClass)) {
            MARAS_LOG_WARN("[JsonOverrideLoader] Invalid socialClass override value '{}'", *data.socialClass);
            return false;
        }
        if (data.skillType.has_value() && !IsStrictSkillType(*data.skillType)) {
            MARAS_LOG_WARN("[JsonOverrideLoader] Invalid skillType override value '{}'", *data.skillType);
            return false;
        }
        if (data.temperament.has_value() && !IsStrictTemperament(*data.temperament)) {
            MARAS_LOG_WARN("[JsonOverrideLoader] Invalid temperament override value '{}'", *data.temperament);
            return false;
        }
        return true;
    }

    JsonOverrideLoader::LoadStatistics JsonOverrideLoader::GetLastLoadStatistics() {
        return lastStats_;
    }

}  // namespace MARAS::Engine
