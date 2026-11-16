#include "maras/foundation/LocalizationCatalog.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace MARAS::Foundation {

    namespace {

        [[nodiscard]] constexpr bool IsPlaceholderIdentifierChar(char c) noexcept {
            const auto uc = static_cast<unsigned char>(c);
            return (uc >= 'a' && uc <= 'z') ||
                   (uc >= 'A' && uc <= 'Z') ||
                   (uc >= '0' && uc <= '9') ||
                   c == '_' || c == '.' || c == '-';
        }

        [[nodiscard]] std::string ExtractPrimaryLangSubtagLower(std::string_view langCode) {
            const std::string_view trimmed = StringUtils::TrimView(langCode);
            if (trimmed.empty()) {
                return "en";
            }
            const std::size_t sep = trimmed.find_first_of("-_");
            const std::string_view sub = (sep == std::string_view::npos) ? trimmed : trimmed.substr(0, sep);
            return StringUtils::ToLowerStd(sub);
        }

        [[nodiscard]] std::optional<std::string_view> StripPluralSuffix(std::string_view key) noexcept {
            constexpr std::array<std::string_view, 6> kSuffixes = {
                "_other", "_many", "_zero", "_one", "_two", "_few"
            };
            for (const std::string_view suf : kSuffixes) {
                if (key.size() > suf.size() && key.ends_with(suf)) {
                    return key.substr(0, key.size() - suf.size());
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsUiNamespaceKey(std::string_view key) noexcept {
            return key.starts_with("ui.");
        }

    }  // namespace

    std::string MapSkyrimIniLanguageToCode(std::string_view skyrimIniLang) {
        const std::string lower = StringUtils::ToLowerStd(StringUtils::TrimView(skyrimIniLang));
        if (lower.empty() || lower == "english" || lower == "en") {
            return "en";
        }
        if (lower == "german" || lower == "de") {
            return "de";
        }
        if (lower == "french" || lower == "fr") {
            return "fr";
        }
        if (lower == "spanish" || lower == "spanish (latin america)" || lower == "es") {
            return "es";
        }
        if (lower == "italian" || lower == "it") {
            return "it";
        }
        if (lower == "russian" || lower == "ru") {
            return "ru";
        }
        if (lower == "polish" || lower == "pl") {
            return "pl";
        }
        if (lower == "japanese" || lower == "ja") {
            return "ja";
        }
        if (lower == "traditional chinese" || lower == "tchinese" || lower == "zh-tw" || lower == "zh_tw") {
            return "zh-TW";
        }
        if (lower == "chinese" || lower == "simplified chinese" || lower == "schinese" ||
            lower == "zh-cn" || lower == "zh_cn" || lower == "zh") {
            return "zh-CN";
        }
        if (lower == "korean" || lower == "ko") {
            return "ko";
        }
        if (lower == "czech" || lower == "cs") {
            return "cs";
        }
        if (lower == "portuguese" || lower == "brazilian" || lower == "portuguese-brazil" ||
            lower == "pt-br" || lower == "pt_br" || lower == "pt") {
            return "pt-BR";
        }
        if (lower == "turkish" || lower == "tr") {
            return "tr";
        }
        if (lower == "ukrainian" || lower == "uk") {
            return "uk";
        }
        return "en";
    }

    PluralCategory SelectPluralCategory(std::string_view langCode, std::int64_t count) noexcept {
        const std::uint64_t n = (count < 0)
                                    ? static_cast<std::uint64_t>(-(count + 1)) + 1ULL
                                    : static_cast<std::uint64_t>(count);
        const std::string primary = ExtractPrimaryLangSubtagLower(langCode);

        // East Asian & Turkic/Austronesian languages with invariant cardinal form
        if (primary == "ja" || primary == "zh" || primary == "ko" ||
            primary == "vi" || primary == "tr" || primary == "th" || primary == "id") {
            return PluralCategory::Other;
        }

        // French / Brazilian Portuguese: 0 and 1 use singular ("one")
        if (primary == "fr" || primary == "pt") {
            return (n == 0ULL || n == 1ULL) ? PluralCategory::One : PluralCategory::Other;
        }

        // East & South Slavic (Russian, Ukrainian, Belarusian, Serbian, Croatian, Bosnian)
        if (primary == "ru" || primary == "uk" || primary == "be" ||
            primary == "sr" || primary == "hr" || primary == "bs") {
            const std::uint64_t mod10 = n % 10ULL;
            const std::uint64_t mod100 = n % 100ULL;
            if (mod10 == 1ULL && mod100 != 11ULL) {
                return PluralCategory::One;
            }
            if (mod10 >= 2ULL && mod10 <= 4ULL && (mod100 < 12ULL || mod100 > 14ULL)) {
                return PluralCategory::Few;
            }
            return PluralCategory::Many;
        }

        // Polish
        if (primary == "pl") {
            if (n == 1ULL) {
                return PluralCategory::One;
            }
            const std::uint64_t mod10 = n % 10ULL;
            const std::uint64_t mod100 = n % 100ULL;
            if (mod10 >= 2ULL && mod10 <= 4ULL && (mod100 < 12ULL || mod100 > 14ULL)) {
                return PluralCategory::Few;
            }
            return PluralCategory::Many;
        }

        // Czech & Slovak
        if (primary == "cs" || primary == "sk") {
            if (n == 1ULL) {
                return PluralCategory::One;
            }
            if (n >= 2ULL && n <= 4ULL) {
                return PluralCategory::Few;
            }
            return PluralCategory::Other;
        }

        // Default Germanic / Romance (en, de, es, it, nl, sv, da, no, etc.)
        return (n == 1ULL) ? PluralCategory::One : PluralCategory::Other;
    }

    std::string FormatTemplate(std::string_view tmpl, std::span<const TemplateArg> args) {
        if (tmpl.empty()) {
            return {};
        }

        std::string out;
        out.reserve(tmpl.size() + 16);

        const std::size_t len = tmpl.size();
        std::size_t i = 0;
        while (i < len) {
            const char ch = tmpl[i];
            if (ch == '{') {
                if (i + 1 < len && tmpl[i + 1] == '{') {
                    out.push_back('{');
                    i += 2;
                    continue;
                }

                std::size_t closePos = i + 1;
                bool validToken = false;
                while (closePos < len) {
                    if (tmpl[closePos] == '}') {
                        validToken = (closePos > i + 1);
                        break;
                    }
                    if (!IsPlaceholderIdentifierChar(tmpl[closePos])) {
                        break;
                    }
                    ++closePos;
                }

                if (validToken && closePos < len && tmpl[closePos] == '}') {
                    const std::string_view tokenName = tmpl.substr(i + 1, closePos - (i + 1));
                    bool matched = false;
                    for (const TemplateArg& arg : args) {
                        if (arg.key == tokenName) {
                            out.append(arg.value);
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) {
                        out.append(tmpl.substr(i, closePos - i + 1));
                    }
                    i = closePos + 1;
                    continue;
                }

                out.push_back('{');
                ++i;
                continue;
            }

            if (ch == '}' && i + 1 < len && tmpl[i + 1] == '}') {
                out.push_back('}');
                i += 2;
                continue;
            }

            out.push_back(ch);
            ++i;
        }

        return out;
    }

    std::vector<std::string> ExtractPlaceholders(std::string_view tmpl) {
        std::vector<std::string> tokens;
        const std::size_t len = tmpl.size();
        std::size_t i = 0;
        while (i < len) {
            if (tmpl[i] == '{') {
                if (i + 1 < len && tmpl[i + 1] == '{') {
                    i += 2;
                    continue;
                }
                std::size_t closePos = i + 1;
                bool validToken = false;
                while (closePos < len) {
                    if (tmpl[closePos] == '}') {
                        validToken = (closePos > i + 1);
                        break;
                    }
                    if (!IsPlaceholderIdentifierChar(tmpl[closePos])) {
                        break;
                    }
                    ++closePos;
                }
                if (validToken && closePos < len && tmpl[closePos] == '}') {
                    tokens.emplace_back(tmpl.substr(i + 1, closePos - (i + 1)));
                    i = closePos + 1;
                    continue;
                }
            } else if (tmpl[i] == '}' && i + 1 < len && tmpl[i + 1] == '}') {
                i += 2;
                continue;
            }
            ++i;
        }

        std::sort(tokens.begin(), tokens.end());
        tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
        return tokens;
    }

    OverlayValidationReport ValidateOverlayAgainstBase(
        const StringDictionary& baseMap,
        const StringDictionary& overlayMap) {
        OverlayValidationReport report{};
        report.baseKeyCount = baseMap.size();

        for (const auto& [baseKey, _] : baseMap) {
            if (overlayMap.contains(baseKey)) {
                ++report.translatedBaseKeyCount;
            } else {
                ++report.fallbackKeyCount;
            }
        }

        for (const auto& [ovKey, ovVal] : overlayMap) {
            auto baseIt = baseMap.find(ovKey);
            bool isPluralSibling = false;
            if (baseIt == baseMap.end()) {
                if (const auto stem = StripPluralSuffix(ovKey)) {
                    const std::string otherKey = std::string(*stem) + "_other";
                    baseIt = baseMap.find(otherKey);
                    isPluralSibling = (baseIt != baseMap.end());
                }
            }

            if (baseIt == baseMap.end()) {
                ++report.unknownOverlayKeyCount;
                continue;
            }

            const std::vector<std::string> baseTokens = ExtractPlaceholders(baseIt->second);
            const std::vector<std::string> ovTokens = ExtractPlaceholders(ovVal);
            if (baseTokens == ovTokens) {
                continue;
            }

            OverlayValidationIssue issue{};
            issue.key = ovKey;
            std::set_difference(
                baseTokens.begin(), baseTokens.end(),
                ovTokens.begin(), ovTokens.end(),
                std::back_inserter(issue.missingPlaceholders));
            std::set_difference(
                ovTokens.begin(), ovTokens.end(),
                baseTokens.begin(), baseTokens.end(),
                std::back_inserter(issue.extraPlaceholders));

            // Singular forms (e.g. `_one` = "Yesterday" vs `_other` = "{count} days ago") may legitimately omit `{count}`
            if (isPluralSibling || StripPluralSuffix(ovKey).has_value()) {
                issue.missingPlaceholders.erase(
                    std::remove(issue.missingPlaceholders.begin(), issue.missingPlaceholders.end(), "count"),
                    issue.missingPlaceholders.end());
            }

            if (!issue.missingPlaceholders.empty() || !issue.extraPlaceholders.empty()) {
                report.placeholderMismatches.push_back(std::move(issue));
            }
        }

        std::sort(
            report.placeholderMismatches.begin(),
            report.placeholderMismatches.end(),
            [](const OverlayValidationIssue& a, const OverlayValidationIssue& b) { return a.key < b.key; });

        return report;
    }

    LocalizationCatalog::LocalizationCatalog(
        std::string selectedSetting,
        LocalizationMeta activeMeta,
        StringDictionary baseEntries,
        const StringDictionary& overlayEntries)
        : selectedSetting_(std::move(selectedSetting)),
          activeMeta_(std::move(activeMeta)),
          entries_(std::move(baseEntries)),
          overlayEntries_(overlayEntries) {
        for (const auto& [k, v] : overlayEntries_) {
            entries_[k] = v;
        }
        for (const auto& [k, v] : entries_) {
            if (IsUiNamespaceKey(k)) {
                uiEntries_.emplace(k.substr(3), v);
            }
        }
    }

    bool LocalizationCatalog::Contains(std::string_view key) const noexcept {
        return entries_.find(key) != entries_.end();
    }

    bool LocalizationCatalog::ContainsOverlay(std::string_view key) const noexcept {
        return overlayEntries_.find(key) != overlayEntries_.end();
    }

    std::optional<std::string_view> LocalizationCatalog::Find(std::string_view key) const noexcept {
        if (const auto it = entries_.find(key); it != entries_.end()) {
            return std::string_view{ it->second };
        }
        return std::nullopt;
    }

    std::optional<std::string_view> LocalizationCatalog::FindOverlay(std::string_view key) const noexcept {
        if (const auto it = overlayEntries_.find(key); it != overlayEntries_.end()) {
            return std::string_view{ it->second };
        }
        return std::nullopt;
    }

    std::string LocalizationCatalog::Get(std::string_view key) const {
        if (const auto found = Find(key)) {
            return std::string(*found);
        }
        return std::string(key);
    }

    std::string LocalizationCatalog::GetOr(std::string_view key, std::string_view fallback) const {
        if (const auto found = Find(key)) {
            return std::string(*found);
        }
        return std::string(fallback);
    }

    std::string LocalizationCatalog::GetOverlayOr(std::string_view key, std::string_view fallback) const {
        if (const auto over = FindOverlay(key)) {
            return std::string(*over);
        }
        if (!fallback.empty()) {
            return std::string(fallback);
        }
        return GetOr(key, fallback);
    }

    std::string LocalizationCatalog::Format(std::string_view key, std::span<const TemplateArg> args) const {
        if (const auto found = Find(key)) {
            return FormatTemplate(*found, args);
        }
        return FormatTemplate(key, args);
    }

    std::string LocalizationCatalog::FormatOr(
        std::string_view key,
        std::string_view fallbackTemplate,
        std::span<const TemplateArg> args) const {
        if (const auto found = Find(key)) {
            return FormatTemplate(*found, args);
        }
        return FormatTemplate(fallbackTemplate, args);
    }

    std::string LocalizationCatalog::FormatPlural(
        std::string_view keyStem,
        std::int64_t count,
        std::span<const TemplateArg> args) const {
        const PluralCategory cat = SelectPluralCategory(activeMeta_.code, count);
        const std::string catKey = std::string(keyStem) + std::string(PluralCategorySuffix(cat));

        std::string_view chosenTemplate = keyStem;
        if (const auto exact = Find(catKey)) {
            chosenTemplate = *exact;
        } else if (cat != PluralCategory::Other) {
            const std::string otherKey = std::string(keyStem) + "_other";
            if (const auto otherFound = Find(otherKey)) {
                chosenTemplate = *otherFound;
            } else if (const auto plainFound = Find(keyStem)) {
                chosenTemplate = *plainFound;
            }
        } else if (const auto plainFound = Find(keyStem)) {
            chosenTemplate = *plainFound;
        }

        bool hasCountArg = false;
        for (const TemplateArg& a : args) {
            if (a.key == "count") {
                hasCountArg = true;
                break;
            }
        }
        if (hasCountArg) {
            return FormatTemplate(chosenTemplate, args);
        }

        const std::string countStr = std::to_string(count);
        std::vector<TemplateArg> mergedArgs;
        mergedArgs.reserve(args.size() + 1);
        mergedArgs.push_back(TemplateArg{ "count", countStr });
        mergedArgs.insert(mergedArgs.end(), args.begin(), args.end());
        return FormatTemplate(chosenTemplate, mergedArgs);
    }

}  // namespace MARAS::Foundation
