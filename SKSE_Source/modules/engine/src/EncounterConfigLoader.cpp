#include "maras/engine/EncounterConfigLoader.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 2> kDefaultEncounterFolders = {
            "Data/SKSE/Plugins/MARAS/encounters",
            "SKSE/Plugins/MARAS/encounters"
        };

        void ReadOptionalString(const nlohmann::json& obj,
                                const char* key,
                                std::string& target,
                                std::string_view fileCtx) {
            if (!obj.contains(key)) {
                return;
            }
            if (!obj[key].is_string()) {
                MARAS_LOG_WARN("[EncounterConfigLoader] Expected string for '{}' in '{}'", key, fileCtx);
                return;
            }
            target = obj[key].get<std::string>();
        }

        void ReadOptionalInt(const nlohmann::json& obj,
                             const char* key,
                             int& target,
                             std::string_view fileCtx) {
            if (!obj.contains(key)) {
                return;
            }
            if (!obj[key].is_number_integer()) {
                MARAS_LOG_WARN("[EncounterConfigLoader] Expected integer for '{}' in '{}'", key, fileCtx);
                return;
            }
            target = obj[key].get<int>();
        }

        void ReadOptionalBool(const nlohmann::json& obj,
                              const char* key,
                              bool& target,
                              std::string_view fileCtx) {
            if (!obj.contains(key)) {
                return;
            }
            if (!obj[key].is_boolean()) {
                MARAS_LOG_WARN("[EncounterConfigLoader] Expected boolean for '{}' in '{}'", key, fileCtx);
                return;
            }
            target = obj[key].get<bool>();
        }

        [[nodiscard]] std::optional<MARAS::Foundation::Temperament> ParseStrictTemperament(std::string_view raw) {
            const std::string_view trimmed = MARAS::Foundation::StringUtils::TrimView(raw);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            const auto parsed = MARAS::Foundation::StringToTemperament(trimmed);
            if (!MARAS::Foundation::StringUtils::CaseInsensitiveEquals(
                    MARAS::Foundation::TemperamentToString(parsed), trimmed)) {
                return std::nullopt;
            }
            return parsed;
        }

        void ApplyVariantFields(const nlohmann::json& obj,
                                MARAS::Domain::Agency::EncounterVariantFields& fields,
                                std::string_view fileCtx) {
            ReadOptionalString(obj, "salutation", fields.salutation, fileCtx);
            ReadOptionalString(obj, "premise", fields.premise, fileCtx);
            ReadOptionalString(obj, "clue", fields.clue, fileCtx);
            ReadOptionalString(obj, "signOff", fields.signOff, fileCtx);
        }

        [[nodiscard]] bool ApplyTemplateObject(const nlohmann::json& root,
                                               const std::filesystem::path& filePath,
                                               MARAS::Domain::Agency::EncounterTemplateCatalog& catalog) {
            const std::string fileStr = filePath.string();
            if (!root.is_object()) {
                MARAS_LOG_ERROR("[EncounterConfigLoader] Root is not a JSON object in '{}'", fileStr);
                return false;
            }

            if (!root.contains("id") || !root["id"].is_string()) {
                MARAS_LOG_ERROR("[EncounterConfigLoader] Missing or invalid string 'id' in '{}'", fileStr);
                return false;
            }

            const std::string id = MARAS::Foundation::StringUtils::TrimStd(root["id"].get<std::string>());
            if (id.empty()) {
                MARAS_LOG_ERROR("[EncounterConfigLoader] Empty 'id' in '{}'", fileStr);
                return false;
            }

            MARAS::Domain::Agency::EncounterTemplateSpec spec;
            if (const auto* existing = catalog.FindTemplate(id)) {
                spec = *existing;
            }
            spec.id = id;

            ReadOptionalInt(root, "schemaVersion", spec.schemaVersion, fileStr);
            ReadOptionalString(root, "modalTone", spec.modalTone, fileStr);
            ReadOptionalString(root, "modalSize", spec.modalSize, fileStr);
            ReadOptionalInt(root, "defaultCancelButtonIndex", spec.defaultCancelButtonIndex, fileStr);
            ReadOptionalBool(root, "allowLoverNameToken", spec.allowLoverNameToken, fileStr);
            ReadOptionalString(root, "modalTitle", spec.modalTitle, fileStr);
            ReadOptionalString(root, "modalSubtitle", spec.modalSubtitle, fileStr);
            ReadOptionalString(root, "envelopeMessage", spec.envelopeMessage, fileStr);
            ReadOptionalString(root, "infoBoxId", spec.infoBoxId, fileStr);
            ReadOptionalString(root, "infoBoxTitle", spec.infoBoxTitle, fileStr);
            ReadOptionalString(root, "buttonId", spec.buttonId, fileStr);
            ReadOptionalString(root, "buttonLabel", spec.buttonLabel, fileStr);

            if (root.contains("defaults")) {
                if (root["defaults"].is_object()) {
                    ApplyVariantFields(root["defaults"], spec.defaults, fileStr);
                } else {
                    MARAS_LOG_WARN("[EncounterConfigLoader] Expected object 'defaults' in '{}'", fileStr);
                }
            }

            if (root.contains("variants")) {
                if (root["variants"].is_object()) {
                    for (const auto& [tempKey, variantJson] : root["variants"].items()) {
                        const auto tempOpt = ParseStrictTemperament(tempKey);
                        if (!tempOpt.has_value()) {
                            MARAS_LOG_WARN(
                                "[EncounterConfigLoader] Ignoring unknown temperament key '{}' in template '{}' ({})",
                                tempKey, id, fileStr);
                            continue;
                        }
                        if (!variantJson.is_object()) {
                            MARAS_LOG_WARN(
                                "[EncounterConfigLoader] Expected object for variant '{}' in template '{}' ({})",
                                tempKey, id, fileStr);
                            continue;
                        }
                        ApplyVariantFields(variantJson, spec.variants[*tempOpt], fileStr);
                    }
                } else {
                    MARAS_LOG_WARN("[EncounterConfigLoader] Expected object 'variants' in '{}'", fileStr);
                }
            }

            catalog.SetTemplate(std::move(spec));
            return true;
        }

    }  // namespace

    EncounterConfigLoader& EncounterConfigLoader::GetSingleton() {
        static EncounterConfigLoader instance;
        return instance;
    }

    bool EncounterConfigLoader::Initialize() {
        std::error_code ec;
        for (const char* candidate : kDefaultEncounterFolders) {
            const std::filesystem::path dir(candidate);
            if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec)) {
                return LoadFromFolder(dir);
            }
        }
        MARAS_LOG_WARN("[EncounterConfigLoader] Encounters folder not found in default search paths");
        return false;
    }

    bool EncounterConfigLoader::LoadFromFolder(const std::filesystem::path& folderPath) {
        auto nextCatalog = std::make_shared<MARAS::Domain::Agency::EncounterTemplateCatalog>();

        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            MARAS_LOG_WARN("[EncounterConfigLoader] Folder does not exist: {}", folderPath.string());
            Clear();
            return false;
        }

        std::vector<std::filesystem::path> files = JsonOverrideLoader::CollectSortedJsonFiles(folderPath, false);
        const std::filesystem::path overridesDir = folderPath / "overrides";
        if (std::filesystem::exists(overridesDir, ec) && std::filesystem::is_directory(overridesDir, ec)) {
            auto overrideFiles = JsonOverrideLoader::CollectSortedJsonFiles(overridesDir, false);
            files.insert(files.end(),
                         std::make_move_iterator(overrideFiles.begin()),
                         std::make_move_iterator(overrideFiles.end()));
        }

        std::size_t loadedFiles = 0;
        for (const auto& file : files) {
            nlohmann::json root;
            if (!JsonOverrideLoader::LoadJsonFile(file, root, "EncounterConfigLoader")) {
                continue;
            }
            if (ApplyTemplateObject(root, file, *nextCatalog)) {
                ++loadedFiles;
            }
        }

        const std::size_t templateCount = nextCatalog->Size();
        const bool anyLoaded = templateCount > 0;
        {
            std::lock_guard lock(mutex_);
            catalog_ = std::move(nextCatalog);
        }
        loaded_.store(anyLoaded, std::memory_order_release);
        MARAS_LOG_INFO("[EncounterConfigLoader] Loaded {} encounter template(s) from {}/{} file(s) in '{}'",
                       templateCount, loadedFiles, files.size(), folderPath.string());
        return anyLoaded;
    }

    void EncounterConfigLoader::Clear() noexcept {
        auto emptyCatalog = std::make_shared<MARAS::Domain::Agency::EncounterTemplateCatalog>();
        {
            std::lock_guard lock(mutex_);
            catalog_ = std::move(emptyCatalog);
        }
        loaded_.store(false, std::memory_order_release);
    }

}  // namespace MARAS::Engine
