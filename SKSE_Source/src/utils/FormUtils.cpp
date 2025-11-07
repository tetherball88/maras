#include "utils/FormUtils.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace MARAS::Utils {

    ParsedFormKey ParseFormKey(std::string_view formKey) {
        // Expected format: "__formData|PluginName|0xFormID"
        static const std::regex formKeyRegex(R"(__formData\|([^|]+)\|(0x[0-9A-Fa-f]+))");

        std::string keyStr(formKey);
        std::smatch matches;

        if (std::regex_match(keyStr, matches, formKeyRegex)) {
            if (matches.size() >= 3) {
                std::string pluginName = matches[1].str();
                std::string hexFormID = matches[2].str();

                auto formID = HexStringToFormID(hexFormID);
                if (formID.has_value()) {
                    MARAS_LOG_DEBUG("Parsed form key: {} -> {}|{:08X}", formKey, pluginName, formID.value());
                    return ParsedFormKey(pluginName, formID.value());
                } else {
                    MARAS_LOG_ERROR("Invalid form ID in key: {}", formKey);
                }
            }
        } else {
            MARAS_LOG_WARN("Invalid form key format: {}", formKey);
        }

        return ParsedFormKey();  // Invalid
    }

    std::optional<RE::FormID> ParseAndResolveFormKey(std::string_view formKey) {
        auto parsed = ParseFormKey(formKey);
        if (!parsed.isValid) {
            return std::nullopt;
        }

        // Use the simplified approach - just get the form and return its ID
        auto form = LookupForm<RE::TESForm>(parsed.localFormID, parsed.pluginName);
        return form ? std::make_optional(form->GetFormID()) : std::nullopt;
    }

    std::optional<RE::FormID> HexStringToFormID(std::string_view hexStr) {
        std::string str(hexStr);

        // Remove "0x" prefix if present
        if (str.size() >= 2 && str.substr(0, 2) == "0x") {
            str = str.substr(2);
        }

        // Convert hex string to FormID
        try {
            RE::FormID formID = std::stoul(str, nullptr, 16);
            return formID;
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Failed to parse hex string '{}': {}", hexStr, e.what());
            return std::nullopt;
        }
    }

    std::string FormIDToHexString(RE::FormID formID) { return fmt::format("0x{:08X}", formID); }

}  // namespace MARAS::Utils
