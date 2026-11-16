#pragma once

#include "maras/domain/agency/EncounterTemplate.h"
#include "maras/foundation/LocalizationCatalog.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Engine {

    /// Loads `Data/SKSE/Plugins/MARAS/localization/en/*.json` + active `<lang>/*.json`
    /// and publishes an immutable `Foundation::LocalizationCatalog` snapshot for lock-free reads.
    class LocalizationService {
    public:
        static LocalizationService& GetSingleton();

        LocalizationService(const LocalizationService&) = delete;
        LocalizationService& operator=(const LocalizationService&) = delete;
        LocalizationService(LocalizationService&&) = delete;
        LocalizationService& operator=(LocalizationService&&) = delete;

        /// Discovers installed language packs and loads `selectedSetting` ("auto" or language code).
        bool Initialize(std::string_view selectedSetting = "auto",
                        const std::filesystem::path& customRootDir = {});

        /// Reloads localization from disk using `selectedSetting` ("auto" or explicit language code).
        bool Reload(std::string_view selectedSetting);

        /// Re-reads the currently selected language pack from disk (for live in-game translator reload).
        bool ReloadCurrent();

        /// Lock-free snapshot of the active localization catalog (never null).
        [[nodiscard]] std::shared_ptr<const Foundation::LocalizationCatalog> Snapshot() const noexcept {
            return catalog_.load(std::memory_order_acquire);
        }

        /// Installed language packs discovered under `localization/*/_meta.json` ("en" always first).
        [[nodiscard]] std::vector<Foundation::LocalizationMeta> GetAvailableLanguages() const;

        /// Reads Skyrim's `[General] sLanguage` INI setting and maps it to a MARAS language code.
        [[nodiscard]] static std::string DetectSkyrimLanguageCode();

        // Catalog display-string overlay helpers (fall back to base YAML/JSON English when untranslated)
        [[nodiscard]] std::string ResolveBonusName(
            std::string_view categoryKey,
            int effectIndex,
            std::string_view fallbackName) const;
        [[nodiscard]] std::string ResolveBonusDescription(
            std::string_view categoryKey,
            int effectIndex,
            std::string_view fallbackDesc) const;
        [[nodiscard]] std::string ResolveTrainingClassName(
            std::string_view classId,
            std::string_view fallbackName) const;
        [[nodiscard]] std::string ResolveTrainingLocation(
            std::string_view classId,
            std::string_view fallbackLocation) const;
        [[nodiscard]] std::string ResolveTrainingDescription(
            std::string_view classId,
            std::string_view fallbackDesc) const;
        [[nodiscard]] std::string ResolveTrainingRequirementLabel(
            std::string_view classId,
            std::string_view fallbackLabel) const;
        [[nodiscard]] std::string ResolveDaedricBlessingName(
            std::string_view blessingId,
            std::string_view fallbackName) const;
        [[nodiscard]] Domain::Agency::EncounterTemplateSpec LocalizeEncounterTemplate(
            Domain::Agency::EncounterTemplateSpec spec) const;

    private:
        LocalizationService();
        ~LocalizationService() = default;

        [[nodiscard]] static std::filesystem::path ResolveLocalizationRoot(
            const std::filesystem::path& customRootDir);

        std::atomic<std::shared_ptr<const Foundation::LocalizationCatalog>> catalog_;
        mutable std::mutex mutex_;
        std::filesystem::path rootDir_{};
        std::vector<Foundation::LocalizationMeta> availableLanguages_{};
    };

}  // namespace MARAS::Engine

namespace MARAS {

    [[nodiscard]] inline std::string Tr(std::string_view key) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->Get(key);
    }

    [[nodiscard]] inline std::string TrOr(std::string_view key, std::string_view fallback) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->GetOr(key, fallback);
    }

    [[nodiscard]] inline std::string TrFmt(
        std::string_view key,
        std::span<const Foundation::TemplateArg> args) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->Format(key, args);
    }

    [[nodiscard]] inline std::string TrFmt(
        std::string_view key,
        std::initializer_list<Foundation::TemplateArg> args) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->Format(key, args);
    }

    [[nodiscard]] inline std::string TrFmtOr(
        std::string_view key,
        std::string_view fallbackTemplate,
        std::initializer_list<Foundation::TemplateArg> args) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->FormatOr(key, fallbackTemplate, args);
    }

    [[nodiscard]] inline std::string TrPlural(
        std::string_view keyStem,
        std::int64_t count,
        std::initializer_list<Foundation::TemplateArg> args = {}) {
        return Engine::LocalizationService::GetSingleton().Snapshot()->FormatPlural(keyStem, count, args);
    }

}  // namespace MARAS
