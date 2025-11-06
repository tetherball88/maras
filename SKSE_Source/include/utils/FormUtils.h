#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "utils/Common.h"

namespace MARAS::Utils {

    // Structure to hold parsed form key data
    struct ParsedFormKey {
        std::string pluginName;
        RE::FormID localFormID;
        bool isValid;

        ParsedFormKey() : localFormID(0), isValid(false) {}
        ParsedFormKey(const std::string& plugin, RE::FormID formID)
            : pluginName(plugin), localFormID(formID), isValid(true) {}
    };

    // Parse JContainers form key format: "__formData|PluginName|0xFormID"
    ParsedFormKey ParseFormKey(std::string_view formKey);

    // Resolve local form ID to global form ID using plugin name
    std::optional<RE::FormID> ResolveFormID(const std::string& pluginName, RE::FormID localFormID);

    // Convenience function to parse and resolve in one call
    std::optional<RE::FormID> ParseAndResolveFormKey(std::string_view formKey);

    // Get plugin name from form ID (reverse lookup for debugging)
    std::string GetPluginNameFromFormID(RE::FormID formID);

    // Check if a plugin is loaded
    bool IsPluginLoaded(const std::string& pluginName);

    // Convert hex string to FormID (handles "0x" prefix)
    std::optional<RE::FormID> HexStringToFormID(std::string_view hexStr);

    // Convert FormID to hex string
    std::string FormIDToHexString(RE::FormID formID);

    // Generic TESDataHandler lookup with error handling
    template <typename T>
    T* LookupForm(RE::FormID localFormID, const std::string& pluginName) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("Cannot access TESDataHandler");
            return nullptr;
        }

        auto form = dataHandler->LookupForm<T>(localFormID, pluginName);
        if (!form) {
            MARAS_LOG_WARN("Could not find form {:08X} in plugin '{}'", localFormID, pluginName);
            return nullptr;
        }

        return form;
    }

}  // namespace MARAS::Utils
