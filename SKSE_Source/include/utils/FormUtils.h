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

    // Convenience function to parse and resolve in one call
    std::optional<RE::FormID> ParseAndResolveFormKey(std::string_view formKey);

    // Convert hex string to FormID (handles "0x" prefix)
    std::optional<RE::FormID> HexStringToFormID(std::string_view hexStr);

    // Convert FormID to hex string
    std::string FormIDToHexString(RE::FormID formID);

    // Generic TESDataHandler lookup with error handling - handles both lookup and resolution
    template <typename T>
    T* LookupForm(RE::FormID localFormID, const std::string& pluginName) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("Cannot access TESDataHandler");
            return nullptr;
        }

        // First verify the plugin is loaded
        const auto* file = dataHandler->LookupModByName(pluginName);
        if (!file) {
            MARAS_LOG_ERROR("Plugin '{}' is not loaded in game", pluginName);
            return nullptr;
        }

        // Mask off any high bytes to ensure we have a local FormID
        RE::FormID maskedFormID = localFormID & 0x00FFFFFF;

        // Try to lookup the form
        auto form = dataHandler->LookupForm<T>(maskedFormID, pluginName);
        if (!form) {
            // Try alternative: use file's CompileIndex to build full FormID
            RE::FormID fullFormID = file->GetCompileIndex() << 24 | maskedFormID;
            form = RE::TESForm::LookupByID<T>(fullFormID);
            if (form) {
                return form;
            }
            MARAS_LOG_WARN("Could not find form {:08X} (full {:08X}, compile index=0x{:02X}) in plugin '{}'",
                           maskedFormID, fullFormID, file->GetCompileIndex(), pluginName);
            return nullptr;
        }

        return form;
    }

    // Get global FormID if you need the ID instead of the form object
    template <typename T = RE::TESForm>
    std::optional<RE::FormID> GetFormID(RE::FormID localFormID, const std::string& pluginName) {
        auto form = LookupForm<T>(localFormID, pluginName);
        return form ? std::make_optional(form->GetFormID()) : std::nullopt;
    }

}  // namespace MARAS::Utils
