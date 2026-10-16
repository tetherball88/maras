#pragma once

#include "utils/Common.h"

namespace MARAS::Utils {

    // Structure to hold parsed form key data
    struct ParsedFormKey {
        std::string pluginName;
        RE::FormID localFormID{0};
        bool isValid{false};

        ParsedFormKey() = default;
        ParsedFormKey(std::string_view plugin, RE::FormID formID)
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
    T* LookupForm(RE::FormID localFormID, std::string_view pluginName, std::string_view callerContext = "") {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("Cannot access TESDataHandler");
            return nullptr;
        }

        // First verify the plugin is loaded
        const auto* file = dataHandler->LookupModByName(pluginName);
        if (!file) {
            MARAS_LOG_ERROR("Plugin '{}' is not loaded in game (requested by context: '{}')",
                            pluginName, callerContext.empty() ? typeid(T).name() : callerContext);
            return nullptr;
        }

        // Mask off high bytes to get the local FormID (12-bit for ESL/light plugins, 24-bit for standard plugins)
        const bool isLight = file->IsLight();
        const RE::FormID maskedFormID = isLight ? (localFormID & 0x00000FFFu) : (localFormID & 0x00FFFFFFu);

        // Try to lookup the form
        auto form = dataHandler->LookupForm<T>(maskedFormID, pluginName);
        if (!form) {
            // Try alternative: use file's compile index (or small file compile index for ESL) to build full FormID
            const RE::FormID fullFormID =
                isLight ? (0xFE000000u | (static_cast<RE::FormID>(file->GetSmallFileCompileIndex()) << 12) | maskedFormID)
                        : ((static_cast<RE::FormID>(file->GetCompileIndex()) << 24) | maskedFormID);
            form = RE::TESForm::LookupByID<T>(fullFormID);
            if (form) {
                return form;
            }
            if (isLight) {
                MARAS_LOG_WARN(
                    "Could not find form {:03X} (full {:08X}, light index=0x{:03X}, type='{}') in ESL plugin '{}' (context: '{}')",
                    maskedFormID, fullFormID, file->GetSmallFileCompileIndex(), typeid(T).name(), pluginName,
                    callerContext.empty() ? "unspecified" : callerContext);
            } else {
                MARAS_LOG_WARN(
                    "Could not find form {:06X} (full {:08X}, compile index=0x{:02X}, type='{}') in plugin '{}' (context: '{}')",
                    maskedFormID, fullFormID, file->GetCompileIndex(), typeid(T).name(), pluginName,
                    callerContext.empty() ? "unspecified" : callerContext);
            }
            return nullptr;
        }

        return form;
    }

    // Get global FormID if you need the ID instead of the form object
    template <typename T = RE::TESForm>
    std::optional<RE::FormID> GetFormID(RE::FormID localFormID, std::string_view pluginName) {
        auto form = LookupForm<T>(localFormID, pluginName);
        return form ? std::make_optional(form->GetFormID()) : std::nullopt;
    }

}  // namespace MARAS::Utils
