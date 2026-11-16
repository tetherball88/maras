#include "maras/engine/LocalizationService.h"

#include "maras/engine/EngineLogging.h"
#include "maras/engine/JsonOverrideLoader.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <system_error>

namespace MARAS::Engine {

    namespace {

        constexpr std::array<const char*, 2> kDefaultLocalizationRoots = {
            "Data/SKSE/Plugins/MARAS/localization",
            "SKSE/Plugins/MARAS/localization"
        };

        [[nodiscard]] std::string ReadJsonStringField(
            const nlohmann::json& obj,
            std::string_view field,
            std::string_view fallback = {}) {
            const auto it = obj.find(field);
            if (it != obj.end() && it->is_string()) {
                return Foundation::StringUtils::TrimStd(it->get<std::string>());
            }
            return std::string(fallback);
        }

        void FlattenJsonStringsRecursive(
            const nlohmann::json& node,
            const std::string& prefix,
            Foundation::StringDictionary& outMap,
            const std::filesystem::path& sourceFile) {
            if (!node.is_object()) {
                return;
            }
            for (const auto& [key, val] : node.items()) {
                if (key.empty() || key.starts_with("$comment") || key.starts_with("_comment")) {
                    continue;
                }
                const std::string fullKey = prefix.empty() ? key : (prefix + "." + key);
                if (val.is_string()) {
                    outMap[fullKey] = val.get<std::string>();
                } else if (val.is_object()) {
                    FlattenJsonStringsRecursive(val, fullKey, outMap, sourceFile);
                } else {
                    MARAS_LOG_WARN(
                        "[LocalizationService] Ignoring non-string/non-object entry '{}' in '{}'",
                        fullKey,
                        sourceFile.string());
                }
            }
        }

        [[nodiscard]] bool IsReservedSpecialJsonFile(std::string_view filenameLower) noexcept {
            return filenameLower.starts_with("_") || filenameLower == "names.json" ||
                   filenameLower.ends_with(".todo.json") || filenameLower.ends_with("_todo.json");
        }

        [[nodiscard]] bool IsBuiltInBaseEnFile(std::string_view filenameLower) noexcept {
            return filenameLower == "catalogs.json" || filenameLower == "hud_and_modals.json" ||
                   filenameLower == "ui_dashboard.json" || filenameLower == "ui_settings.json";
        }

        [[nodiscard]] bool IsUiDomainFile(std::string_view stemLower) noexcept {
            return stemLower == "ui" || stemLower.starts_with("ui_") || stemLower.starts_with("ui.");
        }

        [[nodiscard]] Foundation::LocalizationMeta ParseMetaForDirectory(
            const std::filesystem::path& langDir,
            std::string_view fallbackCode) {
            Foundation::LocalizationMeta meta{};
            meta.code = std::string(fallbackCode);
            meta.name = std::string(fallbackCode);
            meta.nativeName = std::string(fallbackCode);
            if (Foundation::StringUtils::CaseInsensitiveEquals(fallbackCode, "en")) {
                meta.code = "en";
                meta.name = "English";
                meta.nativeName = "English";
            }

            const std::filesystem::path metaPath = langDir / "_meta.json";
            std::error_code ec;
            if (!std::filesystem::exists(metaPath, ec)) {
                return meta;
            }

            nlohmann::json root;
            if (!JsonOverrideLoader::LoadJsonFile(metaPath, root, "LocalizationService") || !root.is_object()) {
                return meta;
            }

            if (std::string code = ReadJsonStringField(root, "code"); !code.empty()) {
                meta.code = std::move(code);
            }
            if (std::string name = ReadJsonStringField(root, "name"); !name.empty()) {
                meta.name = std::move(name);
            }
            if (std::string nativeName = ReadJsonStringField(root, "nativeName"); !nativeName.empty()) {
                meta.nativeName = std::move(nativeName);
            } else {
                meta.nativeName = meta.name;
            }
            meta.author = ReadJsonStringField(root, "author");
            meta.marasVersion = ReadJsonStringField(root, "marasVersion");
            if (meta.marasVersion.empty()) {
                meta.marasVersion = ReadJsonStringField(root, "version");
            }
            meta.fontFamily = ReadJsonStringField(root, "fontFamily");
            return meta;
        }

        [[nodiscard]] std::size_t LoadLanguageDirectoryStrings(
            const std::filesystem::path& langDir,
            Foundation::StringDictionary& outDict,
            Foundation::StringDictionary* outNonBuiltInAddons = nullptr) {
            std::error_code ec;
            if (!std::filesystem::exists(langDir, ec) || !std::filesystem::is_directory(langDir, ec)) {
                return 0;
            }

            std::vector<std::filesystem::path> jsonFiles;
            for (const auto& entry : std::filesystem::directory_iterator(langDir, ec)) {
                if (ec || !entry.is_regular_file(ec)) {
                    continue;
                }
                const std::filesystem::path p = entry.path();
                const std::string extLower = Foundation::StringUtils::ToLowerStd(p.extension().string());
                if (extLower != ".json") {
                    continue;
                }
                const std::string filenameLower = Foundation::StringUtils::ToLowerStd(p.filename().string());
                if (IsReservedSpecialJsonFile(filenameLower)) {
                    continue;
                }
                jsonFiles.push_back(p);
            }

            std::sort(jsonFiles.begin(), jsonFiles.end(), [](const auto& a, const auto& b) {
                return Foundation::StringUtils::ToLowerStd(a.filename().string()) <
                       Foundation::StringUtils::ToLowerStd(b.filename().string());
            });

            std::size_t loadedFiles = 0;
            for (const auto& filePath : jsonFiles) {
                nlohmann::json root;
                if (!JsonOverrideLoader::LoadJsonFile(filePath, root, "LocalizationService") || !root.is_object()) {
                    continue;
                }

                const std::string filenameLower = Foundation::StringUtils::ToLowerStd(filePath.filename().string());
                const bool isAddonDropIn = (outNonBuiltInAddons != nullptr) && !IsBuiltInBaseEnFile(filenameLower);
                const std::string stemLower = Foundation::StringUtils::ToLowerStd(filePath.stem().string());
                if (IsUiDomainFile(stemLower)) {
                    Foundation::StringDictionary rawUi;
                    FlattenJsonStringsRecursive(root, "", rawUi, filePath);
                    for (auto& [k, v] : rawUi) {
                        const std::string normalizedKey = k.starts_with("ui.") ? k : ("ui." + k);
                        if (isAddonDropIn) {
                            (*outNonBuiltInAddons)[normalizedKey] = v;
                        }
                        outDict[normalizedKey] = std::move(v);
                    }
                } else {
                    if (isAddonDropIn) {
                        FlattenJsonStringsRecursive(root, "", *outNonBuiltInAddons, filePath);
                    }
                    FlattenJsonStringsRecursive(root, "", outDict, filePath);
                }
                ++loadedFiles;
            }
            return loadedFiles;
        }

    }  // namespace

    LocalizationService::LocalizationService()
        : catalog_(std::make_shared<const Foundation::LocalizationCatalog>()) {}

    LocalizationService& LocalizationService::GetSingleton() {
        static LocalizationService instance;
        return instance;
    }

    std::filesystem::path LocalizationService::ResolveLocalizationRoot(
        const std::filesystem::path& customRootDir) {
        if (!customRootDir.empty()) {
            return customRootDir;
        }
        std::error_code ec;
        for (const char* candidate : kDefaultLocalizationRoots) {
            const std::filesystem::path p(candidate);
            if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
                return p;
            }
        }
        return std::filesystem::path(kDefaultLocalizationRoots[0]);
    }

    std::string LocalizationService::DetectSkyrimLanguageCode() {
        if (auto* ini = RE::INISettingCollection::GetSingleton()) {
            if (auto* setting = ini->GetSetting("sLanguage:General")) {
                if (const char* str = setting->GetString(); str && *str != '\0') {
                    return Foundation::MapSkyrimIniLanguageToCode(str);
                }
            }
        }
        return "en";
    }

    bool LocalizationService::Initialize(
        std::string_view selectedSetting,
        const std::filesystem::path& customRootDir) {
        {
            std::lock_guard lock(mutex_);
            rootDir_ = ResolveLocalizationRoot(customRootDir);
        }
        return Reload(selectedSetting);
    }

    bool LocalizationService::ReloadCurrent() {
        const auto snap = Snapshot();
        return Reload(snap ? snap->GetSelectedSetting() : std::string_view{ "auto" });
    }

    bool LocalizationService::Reload(std::string_view selectedSetting) {
        std::filesystem::path rootDir;
        {
            std::lock_guard lock(mutex_);
            if (rootDir_.empty()) {
                rootDir_ = ResolveLocalizationRoot({});
            }
            rootDir = rootDir_;
        }

        std::string normalizedSelection = Foundation::StringUtils::TrimStd(selectedSetting);
        if (normalizedSelection.empty() ||
            Foundation::StringUtils::CaseInsensitiveEquals(normalizedSelection, "auto")) {
            normalizedSelection = "auto";
        }

        // 1. Discover all installed language folders under rootDir
        struct DiscoveredPack {
            Foundation::LocalizationMeta meta;
            std::filesystem::path dirPath;
        };
        std::vector<DiscoveredPack> packs;

        std::error_code ec;
        if (std::filesystem::exists(rootDir, ec) && std::filesystem::is_directory(rootDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(rootDir, ec)) {
                if (ec || !entry.is_directory(ec)) {
                    continue;
                }
                const std::string folderName = entry.path().filename().string();
                if (folderName.empty() || folderName.front() == '.') {
                    continue;
                }
                DiscoveredPack pack{};
                pack.dirPath = entry.path();
                pack.meta = ParseMetaForDirectory(entry.path(), folderName);
                packs.push_back(std::move(pack));
            }
        }

        std::sort(packs.begin(), packs.end(), [](const DiscoveredPack& a, const DiscoveredPack& b) {
            const bool aEn = Foundation::StringUtils::CaseInsensitiveEquals(a.meta.code, "en");
            const bool bEn = Foundation::StringUtils::CaseInsensitiveEquals(b.meta.code, "en");
            if (aEn != bEn) {
                return aEn;
            }
            return Foundation::StringUtils::CaseInsensitiveCompare(a.meta.code, b.meta.code) < 0;
        });

        auto findPack = [&packs](std::string_view code) -> const DiscoveredPack* {
            for (const auto& p : packs) {
                if (Foundation::StringUtils::CaseInsensitiveEquals(p.meta.code, code) ||
                    Foundation::StringUtils::CaseInsensitiveEquals(p.dirPath.filename().string(), code)) {
                    return &p;
                }
            }
            return nullptr;
        };

        // Ensure English is always represented in availableLanguages even if files are missing
        std::vector<Foundation::LocalizationMeta> metas;
        metas.reserve(packs.size() + 1);
        if (!findPack("en")) {
            metas.push_back(Foundation::LocalizationMeta{});
        }
        for (const auto& p : packs) {
            metas.push_back(p.meta);
        }

        // 2. Resolve target language code
        const std::string desiredCode =
            (normalizedSelection == "auto") ? DetectSkyrimLanguageCode() : normalizedSelection;

        const DiscoveredPack* enPack = findPack("en");
        const DiscoveredPack* activePack = findPack(desiredCode);

        Foundation::LocalizationMeta activeMeta{};
        if (activePack) {
            activeMeta = activePack->meta;
        } else if (enPack) {
            activeMeta = enPack->meta;
            if (!Foundation::StringUtils::CaseInsensitiveEquals(desiredCode, "en")) {
                MARAS_LOG_INFO(
                    "[LocalizationService] Language pack '{}' not installed; falling back to 'en'",
                    desiredCode);
            }
        }

        // 3. Load base English dictionary (+ any third-party `en/*.json` drop-in files), then active language overlay
        Foundation::StringDictionary baseEntries;
        Foundation::StringDictionary enAddonEntries;
        const std::filesystem::path enDir = enPack ? enPack->dirPath : (rootDir / "en");
        const std::size_t enFiles = LoadLanguageDirectoryStrings(enDir, baseEntries, &enAddonEntries);

        Foundation::StringDictionary overlayEntries = enAddonEntries;
        std::size_t overlayFiles = 0;
        if (activePack && !Foundation::StringUtils::CaseInsensitiveEquals(activePack->meta.code, "en")) {
            Foundation::StringDictionary langEntries;
            overlayFiles = LoadLanguageDirectoryStrings(activePack->dirPath, langEntries);
            for (const auto& [k, v] : langEntries) {
                overlayEntries[k] = v;
            }

            const auto report = Foundation::ValidateOverlayAgainstBase(baseEntries, langEntries);
            MARAS_LOG_INFO(
                "[LocalizationService] Loaded '{}' ({} files): {}/{} base keys translated ({} fallback to 'en', {} extra keys)",
                activeMeta.code,
                overlayFiles,
                report.translatedBaseKeyCount,
                report.baseKeyCount,
                report.fallbackKeyCount,
                report.unknownOverlayKeyCount);

            for (const auto& issue : report.placeholderMismatches) {
                std::string missingJoined;
                for (std::size_t i = 0; i < issue.missingPlaceholders.size(); ++i) {
                    if (i > 0) missingJoined += ", ";
                    missingJoined += "{" + issue.missingPlaceholders[i] + "}";
                }
                std::string extraJoined;
                for (std::size_t i = 0; i < issue.extraPlaceholders.size(); ++i) {
                    if (i > 0) extraJoined += ", ";
                    extraJoined += "{" + issue.extraPlaceholders[i] + "}";
                }
                MARAS_LOG_WARN(
                    "[LocalizationService] Placeholder mismatch in '{}' key '{}' (missing=[{}], unexpected=[{}])",
                    activeMeta.code,
                    issue.key,
                    missingJoined,
                    extraJoined);
            }
        } else {
            MARAS_LOG_INFO(
                "[LocalizationService] Loaded base 'en' ({} files, {} keys; setting='{}')",
                enFiles,
                baseEntries.size(),
                normalizedSelection);
        }

        auto nextCatalog = std::make_shared<const Foundation::LocalizationCatalog>(
            normalizedSelection,
            activeMeta,
            std::move(baseEntries),
            overlayEntries);

        {
            std::lock_guard lock(mutex_);
            availableLanguages_ = std::move(metas);
        }
        catalog_.store(std::move(nextCatalog), std::memory_order_release);
        return enFiles > 0 || overlayFiles > 0;
    }

    std::vector<Foundation::LocalizationMeta> LocalizationService::GetAvailableLanguages() const {
        std::lock_guard lock(mutex_);
        if (availableLanguages_.empty()) {
            return { Foundation::LocalizationMeta{} };
        }
        return availableLanguages_;
    }

    std::string LocalizationService::ResolveBonusName(
        std::string_view categoryKey,
        int effectIndex,
        std::string_view fallbackName) const {
        const auto snap = Snapshot();
        const std::string indexedKey = std::format("bonuses.{}.{}.name", categoryKey, effectIndex);
        if (const auto over = snap->FindOverlay(indexedKey)) {
            return std::string(*over);
        }
        if (effectIndex == 0) {
            const std::string catKey = std::format("bonuses.{}.name", categoryKey);
            if (const auto over = snap->FindOverlay(catKey)) {
                return std::string(*over);
            }
        }
        if (!fallbackName.empty()) {
            return std::string(fallbackName);
        }
        if (const auto found = snap->Find(indexedKey)) {
            return std::string(*found);
        }
        return std::string(fallbackName);
    }

    std::string LocalizationService::ResolveBonusDescription(
        std::string_view categoryKey,
        int effectIndex,
        std::string_view fallbackDesc) const {
        const auto snap = Snapshot();
        const std::string indexedKey = std::format("bonuses.{}.{}.description", categoryKey, effectIndex);
        if (const auto over = snap->FindOverlay(indexedKey)) {
            return std::string(*over);
        }
        if (effectIndex == 0) {
            const std::string catKey = std::format("bonuses.{}.description", categoryKey);
            if (const auto over = snap->FindOverlay(catKey)) {
                return std::string(*over);
            }
        }
        if (!fallbackDesc.empty()) {
            return std::string(fallbackDesc);
        }
        if (const auto found = snap->Find(indexedKey)) {
            return std::string(*found);
        }
        return std::string(fallbackDesc);
    }

    std::string LocalizationService::ResolveTrainingClassName(
        std::string_view classId,
        std::string_view fallbackName) const {
        if (classId.empty()) {
            return std::string(fallbackName);
        }
        return Snapshot()->GetOverlayOr(std::format("training.{}.name", classId), fallbackName);
    }

    std::string LocalizationService::ResolveTrainingLocation(
        std::string_view classId,
        std::string_view fallbackLocation) const {
        if (classId.empty()) {
            return std::string(fallbackLocation);
        }
        return Snapshot()->GetOverlayOr(std::format("training.{}.location", classId), fallbackLocation);
    }

    std::string LocalizationService::ResolveTrainingDescription(
        std::string_view classId,
        std::string_view fallbackDesc) const {
        if (classId.empty()) {
            return std::string(fallbackDesc);
        }
        return Snapshot()->GetOverlayOr(std::format("training.{}.description", classId), fallbackDesc);
    }

    std::string LocalizationService::ResolveTrainingRequirementLabel(
        std::string_view classId,
        std::string_view fallbackLabel) const {
        if (classId.empty()) {
            return std::string(fallbackLabel);
        }
        return Snapshot()->GetOverlayOr(std::format("training.{}.factionLabel", classId), fallbackLabel);
    }

    std::string LocalizationService::ResolveDaedricBlessingName(
        std::string_view blessingId,
        std::string_view fallbackName) const {
        if (blessingId.empty()) {
            return std::string(fallbackName);
        }
        return Snapshot()->GetOverlayOr(std::format("blessing.{}.name", blessingId), fallbackName);
    }

    Domain::Agency::EncounterTemplateSpec LocalizationService::LocalizeEncounterTemplate(
        Domain::Agency::EncounterTemplateSpec spec) const {
        if (spec.id.empty()) {
            return spec;
        }
        const auto snap = Snapshot();
        const std::string prefix = std::format("encounters.{}", spec.id);

        spec.modalTitle = snap->GetOverlayOr(prefix + ".modalTitle", spec.modalTitle);
        spec.modalSubtitle = snap->GetOverlayOr(prefix + ".modalSubtitle", spec.modalSubtitle);
        spec.envelopeMessage = snap->GetOverlayOr(prefix + ".envelopeMessage", spec.envelopeMessage);
        spec.infoBoxTitle = snap->GetOverlayOr(prefix + ".infoBoxTitle", spec.infoBoxTitle);
        spec.buttonLabel = snap->GetOverlayOr(prefix + ".buttonLabel", spec.buttonLabel);

        auto overlayVariant = [&snap](const std::string& vPrefix, Domain::Agency::EncounterVariantFields& vf) {
            if (const auto s = snap->FindOverlay(vPrefix + ".salutation")) vf.salutation = std::string(*s);
            if (const auto p = snap->FindOverlay(vPrefix + ".premise")) vf.premise = std::string(*p);
            if (const auto c = snap->FindOverlay(vPrefix + ".clue")) vf.clue = std::string(*c);
            if (const auto o = snap->FindOverlay(vPrefix + ".signOff")) vf.signOff = std::string(*o);
        };

        overlayVariant(prefix + ".defaults", spec.defaults);
        constexpr std::array<Foundation::Temperament, 5> kTemperaments = {
            Foundation::Temperament::Romantic,
            Foundation::Temperament::Humble,
            Foundation::Temperament::Independent,
            Foundation::Temperament::Proud,
            Foundation::Temperament::Jealous
        };
        for (const Foundation::Temperament temp : kTemperaments) {
            const std::string tempName = std::string(Foundation::TemperamentToString(temp));
            const std::string vPrefix = std::format("{}.variants.{}", prefix, tempName);
            const bool hasAnyLocalized =
                snap->ContainsOverlay(vPrefix + ".salutation") ||
                snap->ContainsOverlay(vPrefix + ".premise") ||
                snap->ContainsOverlay(vPrefix + ".clue") ||
                snap->ContainsOverlay(vPrefix + ".signOff");
            if (hasAnyLocalized || spec.variants.contains(temp)) {
                overlayVariant(vPrefix, spec.variants[temp]);
            }
        }
        return spec;
    }

}  // namespace MARAS::Engine
