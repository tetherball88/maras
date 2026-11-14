#pragma once

#include "PCH.h"
#include "maras/state/WorldStore.h"

namespace MARAS {

    class Settings {
    public:
        static Settings& GetSingleton() {
            static Settings singleton;
            return singleton;
        }

        using ChildCustodyGenderPolicy = State::ChildCustodyGenderPolicy;
        using ChildCustodyPlayerPolicy = State::ChildCustodyPlayerPolicy;
        using MatchmakingOrientation = State::MatchmakingOrientation;

        void Load();

        [[nodiscard]] std::shared_ptr<const State::SettingsData> Snapshot() const noexcept {
            return State::WorldStore::GetSingleton().Snapshot()->settings;
        }

        // GlobalVariable-backed & side-effecting settings accessors
        int32_t GetStartDialGender() const;
        void SetStartDialGender(int32_t value);

        bool GetEnablePolygamy() const;
        void SetEnablePolygamy(bool value);

        bool GetDisablePolygamyQuest() const {
            return Snapshot()->disablePolygamyQuest;
        }
        void SetDisablePolygamyQuest(bool value);

        int32_t GetMinRelRankForDial() const;
        void SetMinRelRankForDial(int32_t value);

        bool GetSimulateNoSkyrimNet() const {
            return Snapshot()->simulateNoSkyrimNet;
        }
        void SetSimulateNoSkyrimNet(bool value);

        float GetSkyrimNetActionRadius() const {
            return Snapshot()->skyrimnetActionRadius;
        }

        // Global critter & pet banned config (supercedes per-child ban)
        bool IsGlobalCritterBanned() const {
            return Snapshot()->bGlobalBanCritter;
        }
        void SetGlobalCritterBanned(bool banned);

        bool IsGlobalPetBanned() const {
            return Snapshot()->bGlobalBanPet;
        }
        void SetGlobalPetBanned(bool banned);

        // Post-load engine sync and runtime revert helpers
        void ApplyPostLoadEngineSideEffects();
        void RevertGameSettings();

        // Data dump
        nlohmann::json DumpJson() const;

        // Key-based O(1) setting accessors (case-insensitive, thread-safe via WorldStore)
        bool GetBoolByKey(std::string_view key) const;
        bool SetBoolByKey(std::string_view key, bool value);
        int32_t GetIntByKey(std::string_view key) const;
        bool SetIntByKey(std::string_view key, int32_t value);
        float GetFloatByKey(std::string_view key) const;
        bool SetFloatByKey(std::string_view key, float value);

    private:
        Settings() = default;
        Settings(const Settings&) = delete;
        Settings& operator=(const Settings&) = delete;

        void SyncSimulateNoSkyrimNetGlobal(bool value) const;

        static void ParseKeyCombo(const std::string& comboStr, State::SettingsData& out);
        static uint32_t ParseSingleKey(const std::string& keyStr);
    };

}
