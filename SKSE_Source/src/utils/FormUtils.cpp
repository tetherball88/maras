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

    std::optional<RE::FormID> ResolveFormID(const std::string& pluginName, RE::FormID localFormID) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("TESDataHandler not available");
            return std::nullopt;
        }

        // Find the mod file
        const RE::TESFile* modFile = nullptr;
        for (const auto& file : dataHandler->files) {
            if (file && _stricmp(std::string(file->GetFilename()).c_str(), pluginName.c_str()) == 0) {
                modFile = file;
                break;
            }
        }

        if (!modFile) {
            MARAS_LOG_WARN("Plugin '{}' not found or not loaded", pluginName);
            return std::nullopt;
        }

        // Calculate global form ID
        RE::FormID globalFormID =
            (static_cast<RE::FormID>(modFile->GetCompileIndex()) << 24) | (localFormID & 0x00FFFFFF);

        // Verify the form exists
        auto form = RE::TESForm::LookupByID(globalFormID);
        if (!form) {
            MARAS_LOG_WARN("Form {:08X} not found in plugin '{}'", localFormID, pluginName);
            return std::nullopt;
        }

        MARAS_LOG_DEBUG("Resolved {}|{:08X} -> {:08X}", pluginName, localFormID, globalFormID);
        return globalFormID;
    }

    std::optional<RE::FormID> ParseAndResolveFormKey(std::string_view formKey) {
        auto parsed = ParseFormKey(formKey);
        if (!parsed.isValid) {
            return std::nullopt;
        }

        return ResolveFormID(parsed.pluginName, parsed.localFormID);
    }

    std::string GetPluginNameFromFormID(RE::FormID formID) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return "Unknown";
        }

        uint8_t modIndex = (formID >> 24) & 0xFF;

        for (const auto& file : dataHandler->files) {
            if (file && file->GetCompileIndex() == modIndex) {
                return std::string(file->GetFilename());
            }
        }

        return fmt::format("Unknown_{:02X}", modIndex);
    }

    bool IsPluginLoaded(const std::string& pluginName) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return false;
        }

        for (const auto& file : dataHandler->files) {
            if (file && _stricmp(std::string(file->GetFilename()).c_str(), pluginName.c_str()) == 0) {
                return true;
            }
        }

        return false;
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
