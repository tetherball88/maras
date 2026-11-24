#pragma once

#include "maras/state/WorldTables.h"

#include <cstdint>
#include <string_view>

namespace MARAS::State {

    struct HotkeyParseResult {
        bool disabled{ false };
        bool fellBackToDefault{ false };
    };

    enum class SettingWriteStatus : std::uint8_t {
        Ok = 0,
        UnknownKey = 1,
        OutOfRange = 2
    };

    /// Parses a single key name (e.g. "B", "F9", "Numpad1") or decimal/hex DirectInput scancode (e.g. "48", "0x30").
    /// Returns 0 if unrecognized.
    [[nodiscard]] std::uint32_t ParseSingleKey(std::string_view keyStr);

    /// Parses a ToggleUI hotkey combo string (e.g. "Alt+B", "LCtrl+RShift+0x30", "0", "none", "disabled")
    /// into `SettingsData::{toggleUIKey, toggleUIModCtrl, toggleUIModShift, toggleUIModAlt}`.
    /// Explicit disable tokens set `toggleUIKey = 0` (`disabled = true`, `fellBackToDefault = false`);
    /// unrecognized non-empty combos fall back to `Alt+B` (`0x30`, `fellBackToDefault = true`).
    HotkeyParseResult ParseKeyCombo(std::string_view comboStr, SettingsData& out);

    /// Pure key-based accessors for `SettingsData` fields and legacy Papyrus aliases
    /// (excluding engine-side-effect keys handled by `SettingsCoordinator`).
    [[nodiscard]] bool TryGetBoolSetting(const SettingsData& s, std::string_view key, bool& outValue);
    [[nodiscard]] bool TrySetBoolSetting(SettingsData& s, std::string_view key, bool value);

    [[nodiscard]] bool TryGetIntSetting(const SettingsData& s, std::string_view key, std::int32_t& outValue);
    [[nodiscard]] SettingWriteStatus TrySetIntSetting(SettingsData& s, std::string_view key, std::int32_t value);

    [[nodiscard]] bool TryGetFloatSetting(const SettingsData& s, std::string_view key, float& outValue);
    [[nodiscard]] SettingWriteStatus TrySetFloatSetting(SettingsData& s, std::string_view key, float value);

}  // namespace MARAS::State
