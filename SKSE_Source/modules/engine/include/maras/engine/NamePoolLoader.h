#pragma once

#include "maras/domain/lineage/NamePool.h"

#include <RE/Skyrim.h>

#include <filesystem>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Engine {

    /// Loads child and critter name JSON pools and serves RNG-backed name selection.
    class NamePoolLoader {
    public:
        static NamePoolLoader& GetSingleton();

        NamePoolLoader(const NamePoolLoader&) = delete;
        NamePoolLoader& operator=(const NamePoolLoader&) = delete;
        NamePoolLoader(NamePoolLoader&&) = delete;
        NamePoolLoader& operator=(NamePoolLoader&&) = delete;

        /// Loads both child and critter name pools from default search paths (or custom paths if supplied).
        bool Initialize(const std::filesystem::path& childNamesPath = {},
                        const std::filesystem::path& critterNamesPath = {});

        bool LoadChildNamesFromFile(const std::filesystem::path& customPath = {});
        bool LoadCritterNamesFromFile(const std::filesystem::path& customPath = {});

        /// Applies optional per-language `localization/<langCode>/names.json` overrides on top of base pools.
        bool ApplyLocalizationOverlay(std::string_view langCode);

        void Clear() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] bool IsChildLoaded() const noexcept;
        [[nodiscard]] bool IsCritterLoaded() const noexcept;

        /// Returns the owned domain `NamePool` (populated at load/initialize time).
        [[nodiscard]] const MARAS::Domain::Lineage::NamePool& GetPool() const noexcept { return pool_; }

        [[nodiscard]] std::vector<std::string> GetChildNames(std::string_view raceName, bool isFemale) const;
        [[nodiscard]] std::vector<std::string> GetChildNames(RE::Actor* actor, bool isFemale) const;
        [[nodiscard]] std::vector<std::string> GetCritterNames(std::string_view raceName) const;

        [[nodiscard]] std::string GetRandomChildName(std::string_view raceName,
                                                       bool isFemale,
                                                       bool isUnique = true) const;
        [[nodiscard]] std::string GetRandomChildName(RE::Actor* actor,
                                                       bool isFemale,
                                                       bool isUnique = true) const;

        [[nodiscard]] std::string GetRandomCritterName(std::string_view raceName) const;
        [[nodiscard]] std::string GetRandomCritterName(RE::TESForm* critterOrRace) const;

    private:
        NamePoolLoader();
        ~NamePoolLoader() = default;

        MARAS::Domain::Lineage::NamePool pool_;
        mutable std::shared_mutex poolMutex_;
        mutable std::mutex rngMutex_;
        mutable std::mt19937_64 rng_;
    };

}  // namespace MARAS::Engine
