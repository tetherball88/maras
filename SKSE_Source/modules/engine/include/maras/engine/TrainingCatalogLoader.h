#pragma once

#include "maras/domain/lineage/TrainingCatalog.h"
#include "maras/foundation/StrongIds.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Engine {

    /// Engine adapter resolving parent faction ranks, quest states, and leader NPC matches from Skyrim forms.
    class EngineParentAffiliationPort final : public MARAS::Domain::Lineage::IParentAffiliationPort {
    public:
        [[nodiscard]] bool MatchesNpcKey(MARAS::Foundation::ActorId parentId,
                                         std::string_view npcKey) const override;
        [[nodiscard]] std::int32_t GetFactionRank(MARAS::Foundation::ActorId parentId,
                                                  std::string_view factionKey) const override;
        [[nodiscard]] bool IsQuestCompleted(std::string_view questKey) const override;
        [[nodiscard]] std::int32_t GetQuestStage(std::string_view questKey) const override;
        [[nodiscard]] std::string GetParentDisplayName(MARAS::Foundation::ActorId parentId) const override;
    };

    /// Loads `childTraining.yaml` and `childTraining/*.yaml` into the domain `TrainingCatalog`.
    class TrainingCatalogLoader {
    public:
        static TrainingCatalogLoader& GetSingleton();

        TrainingCatalogLoader(const TrainingCatalogLoader&) = delete;
        TrainingCatalogLoader& operator=(const TrainingCatalogLoader&) = delete;
        TrainingCatalogLoader(TrainingCatalogLoader&&) = delete;
        TrainingCatalogLoader& operator=(TrainingCatalogLoader&&) = delete;

        /// Loads the default `childTraining.yaml` and `childTraining/` extension folder.
        bool Initialize();

        /// Loads base training YAML and sorted `.yaml` extension files into `TrainingCatalog`.
        bool LoadCatalog(
            const std::filesystem::path& configPath = "Data/SKSE/Plugins/MARAS/childTraining.yaml",
            const std::filesystem::path& extensionFolder = "Data/SKSE/Plugins/MARAS/childTraining");

        void Clear() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_.load(std::memory_order_acquire); }

        [[nodiscard]] const MARAS::Domain::Lineage::TrainingCatalog& GetCatalog() const noexcept {
            return catalog_;
        }

        [[nodiscard]] const std::vector<MARAS::Domain::Lineage::TrainingClassSpec>& GetTrainingClasses()
            const noexcept {
            return catalog_.AllClasses();
        }

        [[nodiscard]] const std::vector<MARAS::Domain::Lineage::DaedricBlessingSpec>& GetDaedricBlessings()
            const noexcept {
            return catalog_.AllDaedricBlessings();
        }

        [[nodiscard]] const EngineParentAffiliationPort& GetAffiliationPort() const noexcept {
            return affiliationPort_;
        }

    private:
        TrainingCatalogLoader() = default;
        ~TrainingCatalogLoader() = default;

        MARAS::Domain::Lineage::TrainingCatalog catalog_;
        EngineParentAffiliationPort affiliationPort_;
        std::atomic<bool> loaded_{ false };
    };

}  // namespace MARAS::Engine
