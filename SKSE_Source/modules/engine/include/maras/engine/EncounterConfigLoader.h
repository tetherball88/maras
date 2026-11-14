#pragma once

#include "maras/domain/agency/EncounterTemplate.h"

#include <atomic>
#include <filesystem>
#include <string_view>

namespace MARAS::Engine {

    /// Loads `encounters/*.json` (and optional `encounters/overrides/*.json`) into `EncounterTemplateCatalog`.
    class EncounterConfigLoader {
    public:
        static EncounterConfigLoader& GetSingleton();

        EncounterConfigLoader(const EncounterConfigLoader&) = delete;
        EncounterConfigLoader& operator=(const EncounterConfigLoader&) = delete;
        EncounterConfigLoader(EncounterConfigLoader&&) = delete;
        EncounterConfigLoader& operator=(EncounterConfigLoader&&) = delete;

        /// Loads encounter templates from default search paths (`Data/SKSE/Plugins/MARAS/encounters`).
        bool Initialize();

        /// Clears and loads all `.json` encounter template files from `folderPath` (plus `overrides/` if present).
        bool LoadFromFolder(const std::filesystem::path& folderPath = "Data/SKSE/Plugins/MARAS/encounters");

        void Clear() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

        [[nodiscard]] const MARAS::Domain::Agency::EncounterTemplateCatalog& GetCatalog() const noexcept {
            return catalog_;
        }

        [[nodiscard]] const MARAS::Domain::Agency::EncounterTemplateSpec* FindTemplate(
            std::string_view id) const noexcept {
            return catalog_.FindTemplate(id);
        }

    private:
        EncounterConfigLoader() = default;
        ~EncounterConfigLoader() = default;

        MARAS::Domain::Agency::EncounterTemplateCatalog catalog_;
        std::atomic<bool> loaded_{ false };
    };

}  // namespace MARAS::Engine
