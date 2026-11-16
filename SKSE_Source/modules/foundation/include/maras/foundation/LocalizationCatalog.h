#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MARAS::Foundation {

    struct TransparentStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
        [[nodiscard]] std::size_t operator()(const char* s) const noexcept {
            return std::hash<std::string_view>{}(s ? std::string_view{ s } : std::string_view{});
        }
    };

    using StringDictionary =
        std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>;

    struct LocalizationMeta {
        std::string code{ "en" };
        std::string name{ "English" };
        std::string nativeName{ "English" };
        std::string author{};
        std::string marasVersion{};
        std::string fontFamily{};

        bool operator==(const LocalizationMeta&) const = default;
    };

    struct TemplateArg {
        std::string_view key{};
        std::string_view value{};
    };

    enum class PluralCategory : std::uint8_t {
        One = 0,
        Few = 1,
        Many = 2,
        Other = 3
    };

    [[nodiscard]] constexpr std::string_view PluralCategorySuffix(PluralCategory category) noexcept {
        switch (category) {
            case PluralCategory::One:
                return "_one";
            case PluralCategory::Few:
                return "_few";
            case PluralCategory::Many:
                return "_many";
            case PluralCategory::Other:
            default:
                return "_other";
        }
    }

    /// Maps Skyrim's `[General] sLanguage` setting (e.g. "RUSSIAN", "GERMAN", "TRADITIONAL CHINESE")
    /// to a canonical MARAS localization folder code (e.g. "ru", "de", "zh-TW"). Defaults to "en".
    [[nodiscard]] std::string MapSkyrimIniLanguageToCode(std::string_view skyrimIniLang);

    /// Selects the CLDR cardinal plural category for `count` under `langCode` (e.g. "en", "ru", "pl", "fr", "ja").
    [[nodiscard]] PluralCategory SelectPluralCategory(std::string_view langCode, std::int64_t count) noexcept;

    /// Crash-safe `{named}` template interpolation.
    /// - `{{` and `}}` render as literal `{` and `}`.
    /// - Unknown `{tokens}` or malformed/unclosed braces are preserved verbatim and never throw.
    [[nodiscard]] std::string FormatTemplate(std::string_view tmpl, std::span<const TemplateArg> args);

    [[nodiscard]] inline std::string FormatTemplate(
        std::string_view tmpl,
        std::initializer_list<TemplateArg> args) {
        return FormatTemplate(tmpl, std::span<const TemplateArg>{ args.begin(), args.size() });
    }

    /// Extracts sorted unique `{identifier}` placeholder names from a template string.
    [[nodiscard]] std::vector<std::string> ExtractPlaceholders(std::string_view tmpl);

    struct OverlayValidationIssue {
        std::string key{};
        std::vector<std::string> missingPlaceholders{};
        std::vector<std::string> extraPlaceholders{};
    };

    struct OverlayValidationReport {
        std::size_t baseKeyCount{ 0 };
        std::size_t translatedBaseKeyCount{ 0 };
        std::size_t fallbackKeyCount{ 0 };
        std::size_t unknownOverlayKeyCount{ 0 };
        std::vector<OverlayValidationIssue> placeholderMismatches{};
    };

    /// Validates an overlay translation dictionary against the canonical `en` base dictionary.
    /// Recognizes CLDR plural variants (`_zero`, `_one`, `_two`, `_few`, `_many`, `_other`) against `<stem>_other`.
    [[nodiscard]] OverlayValidationReport ValidateOverlayAgainstBase(
        const StringDictionary& baseMap,
        const StringDictionary& overlayMap);

    /// Immutable snapshot of active localized strings (`en` base merged with `<resolvedLanguage>` overlay).
    class LocalizationCatalog {
    public:
        LocalizationCatalog() = default;

        LocalizationCatalog(
            std::string selectedSetting,
            LocalizationMeta activeMeta,
            StringDictionary baseEntries,
            const StringDictionary& overlayEntries);

        [[nodiscard]] const std::string& GetSelectedSetting() const noexcept { return selectedSetting_; }
        [[nodiscard]] const std::string& GetResolvedCode() const noexcept { return activeMeta_.code; }
        [[nodiscard]] const LocalizationMeta& GetMeta() const noexcept { return activeMeta_; }

        [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
        [[nodiscard]] bool Contains(std::string_view key) const noexcept;
        [[nodiscard]] bool ContainsOverlay(std::string_view key) const noexcept;

        /// Returns a view into the catalog if `key` exists (`std::nullopt` otherwise).
        [[nodiscard]] std::optional<std::string_view> Find(std::string_view key) const noexcept;

        /// Returns a view into the active language/addon overlay only (`std::nullopt` for base `en/catalogs.json`).
        [[nodiscard]] std::optional<std::string_view> FindOverlay(std::string_view key) const noexcept;

        /// Returns the localized string for `key`, or `std::string(key)` when absent.
        [[nodiscard]] std::string Get(std::string_view key) const;

        /// Returns the localized string for `key`, or `fallback` when absent.
        [[nodiscard]] std::string GetOr(std::string_view key, std::string_view fallback) const;

        /// Prefers active language/addon overlay (`FindOverlay`), then live config `fallback` (so English edits in
        /// `bonuses.json`, `childTraining/*.yaml`, or `encounters/overrides/*.json` are never clobbered by `en/catalogs.json`),
        /// and finally `Find(key)` only if `fallback` itself is empty.
        [[nodiscard]] std::string GetOverlayOr(std::string_view key, std::string_view fallback) const;

        /// Looks up `key` (falling back to `key` literal) and interpolates `{named}` arguments.
        [[nodiscard]] std::string Format(std::string_view key, std::span<const TemplateArg> args) const;

        [[nodiscard]] std::string Format(
            std::string_view key,
            std::initializer_list<TemplateArg> args) const {
            return Format(key, std::span<const TemplateArg>{ args.begin(), args.size() });
        }

        /// Looks up `key` (falling back to `fallbackTemplate`) and interpolates `{named}` arguments.
        [[nodiscard]] std::string FormatOr(
            std::string_view key,
            std::string_view fallbackTemplate,
            std::span<const TemplateArg> args) const;

        [[nodiscard]] std::string FormatOr(
            std::string_view key,
            std::string_view fallbackTemplate,
            std::initializer_list<TemplateArg> args) const {
            return FormatOr(key, fallbackTemplate, std::span<const TemplateArg>{ args.begin(), args.size() });
        }

        /// Resolves `<keyStem>_<plural>` -> `<keyStem>_other` -> `<keyStem>` for `count`, automatically binding `{count}`.
        [[nodiscard]] std::string FormatPlural(
            std::string_view keyStem,
            std::int64_t count,
            std::span<const TemplateArg> args = {}) const;

        [[nodiscard]] std::string FormatPlural(
            std::string_view keyStem,
            std::int64_t count,
            std::initializer_list<TemplateArg> args) const {
            return FormatPlural(keyStem, count, std::span<const TemplateArg>{ args.begin(), args.size() });
        }

        [[nodiscard]] const StringDictionary& AllEntries() const noexcept { return entries_; }
        [[nodiscard]] const StringDictionary& UiEntries() const noexcept { return uiEntries_; }

    private:
        std::string selectedSetting_{ "auto" };
        LocalizationMeta activeMeta_{};
        StringDictionary entries_{};
        StringDictionary overlayEntries_{};
        StringDictionary uiEntries_{};
    };

}  // namespace MARAS::Foundation
