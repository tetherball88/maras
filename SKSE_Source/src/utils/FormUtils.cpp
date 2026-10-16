#include "utils/FormUtils.h"

namespace MARAS::Utils {

    ParsedFormKey ParseFormKey(std::string_view formKey) {
        // Expected format: "__formData|PluginName|0xFormID"
        constexpr std::string_view kPrefix = "__formData|";
        if (!formKey.starts_with(kPrefix)) {
            MARAS_LOG_WARN("Invalid form key format: {}", formKey);
            return {};
        }

        std::string_view rest = formKey.substr(kPrefix.size());
        auto pipePos = rest.find('|');
        if (pipePos == std::string_view::npos || pipePos == 0 || pipePos + 1 >= rest.size()) {
            MARAS_LOG_WARN("Invalid form key format: {}", formKey);
            return {};
        }

        std::string_view pluginName = rest.substr(0, pipePos);
        std::string_view hexFormID = rest.substr(pipePos + 1);
        if (hexFormID.find('|') != std::string_view::npos ||
            !(hexFormID.starts_with("0x") || hexFormID.starts_with("0X"))) {
            MARAS_LOG_WARN("Invalid form key format: {}", formKey);
            return {};
        }

        auto formID = HexStringToFormID(hexFormID);
        if (!formID.has_value()) {
            MARAS_LOG_ERROR("Invalid form ID in key: {}", formKey);
            return {};
        }

        MARAS_LOG_TRACE("Parsed form key: {} -> {}|{:08X}", formKey, pluginName, *formID);
        return ParsedFormKey(pluginName, *formID);
    }

    std::optional<RE::FormID> ParseAndResolveFormKey(std::string_view formKey) {
        auto parsed = ParseFormKey(formKey);
        if (!parsed.isValid) {
            return std::nullopt;
        }

        auto form = LookupForm<RE::TESForm>(parsed.localFormID, parsed.pluginName, "ParseAndResolveFormKey");
        return form ? std::make_optional(form->GetFormID()) : std::nullopt;
    }

    std::optional<RE::FormID> HexStringToFormID(std::string_view hexStr) {
        std::string_view trimmed = hexStr;

        // Remove "0x" or "0X" prefix if present
        if (trimmed.size() >= 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
            trimmed.remove_prefix(2);
        }

        if (trimmed.empty()) {
            MARAS_LOG_ERROR("Failed to parse empty hex string '{}'", hexStr);
            return std::nullopt;
        }

        RE::FormID formID = 0;
        const char* begin = trimmed.data();
        const char* end = begin + trimmed.size();
        auto [ptr, ec] = std::from_chars(begin, end, formID, 16);
        if (ec != std::errc{} || ptr != end) {
            MARAS_LOG_ERROR("Failed to parse hex string '{}'", hexStr);
            return std::nullopt;
        }

        return formID;
    }

    std::string FormIDToHexString(RE::FormID formID) { return fmt::format("0x{:08X}", formID); }

}  // namespace MARAS::Utils
