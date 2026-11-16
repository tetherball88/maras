#pragma once

#include "maras/state/DomainTypes.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace MARAS::App {

    /**
     * Layer 4 application coordinator for M.A.R.A.S configuration settings.
     * Owns INI/defaults bootstrap, cross-layer settings mutations via WorldStore,
     * engine TESGlobal/faction/log-level synchronization, and JSON serialization.
     */
    class SettingsCoordinator {
    public:
        static SettingsCoordinator& GetSingleton();

        SettingsCoordinator(const SettingsCoordinator&) = delete;
        SettingsCoordinator& operator=(const SettingsCoordinator&) = delete;
        SettingsCoordinator(SettingsCoordinator&&) = delete;
        SettingsCoordinator& operator=(SettingsCoordinator&&) = delete;

        using ChildCustodyGenderPolicy = State::ChildCustodyGenderPolicy;
        using ChildCustodyPlayerPolicy = State::ChildCustodyPlayerPolicy;
        using MatchmakingOrientation = State::MatchmakingOrientation;

        void Load();

        [[nodiscard]] std::shared_ptr<const State::SettingsData> Snapshot() const noexcept {
            return State::WorldStore::GetSingleton().Snapshot()->settings;
        }

        // GlobalVariable-backed & side-effecting settings accessors
        [[nodiscard]] std::int32_t GetStartDialGender() const;
        void SetStartDialGender(std::int32_t value);

        [[nodiscard]] bool GetEnablePolygamy() const;
        void SetEnablePolygamy(bool value);

        [[nodiscard]] bool GetDisablePolygamyQuest() const {
            return Snapshot()->disablePolygamyQuest;
        }
        void SetDisablePolygamyQuest(bool value);

        [[nodiscard]] std::int32_t GetMinRelRankForDial() const;
        void SetMinRelRankForDial(std::int32_t value);

        [[nodiscard]] bool GetPreferVanillaAudio() const;
        void SetPreferVanillaAudio(bool value);

        [[nodiscard]] bool GetAllowAIDial() const;
        void SetAllowAIDial(bool value);

        [[nodiscard]] bool GetSimulateNoSkyrimNet() const {
            return Snapshot()->simulateNoSkyrimNet;
        }
        void SetSimulateNoSkyrimNet(bool value);

        [[nodiscard]] float GetSkyrimNetActionRadius() const {
            return Snapshot()->skyrimnetActionRadius;
        }

        // Global critter & pet ban config (supersedes per-child ban)
        [[nodiscard]] bool IsGlobalCritterBanned() const {
            return Snapshot()->bGlobalBanCritter;
        }
        void SetGlobalCritterBanned(bool banned);

        [[nodiscard]] bool IsGlobalPetBanned() const {
            return Snapshot()->bGlobalBanPet;
        }
        void SetGlobalPetBanned(bool banned);

        // Global localization setting (persisted in MARAS.ini [Localization] Language = auto)
        [[nodiscard]] std::string GetLanguage() const {
            return Snapshot()->language;
        }
        void SetLanguage(std::string_view code);
        void ReloadLocalization();

        // Post-load engine sync and runtime revert helpers
        void ApplyPostLoadEngineSideEffects();
        void RevertGameSettings();

        // Read-only data dump (CQS)
        [[nodiscard]] nlohmann::json DumpJson() const;

        // Key-based O(1) setting accessors (case-insensitive, thread-safe via WorldStore)
        [[nodiscard]] bool GetBoolByKey(std::string_view key) const;
        bool SetBoolByKey(std::string_view key, bool value);
        [[nodiscard]] std::int32_t GetIntByKey(std::string_view key) const;
        bool SetIntByKey(std::string_view key, std::int32_t value);
        [[nodiscard]] float GetFloatByKey(std::string_view key) const;
        bool SetFloatByKey(std::string_view key, float value);

    private:
        SettingsCoordinator() = default;
        ~SettingsCoordinator() = default;

        static void ParseKeyCombo(const std::string& comboStr, State::SettingsData& out);
        static std::uint32_t ParseSingleKey(const std::string& keyStr);
    };

}  // namespace MARAS::App
