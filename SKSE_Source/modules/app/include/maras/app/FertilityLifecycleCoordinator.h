#pragma once

#include "RE/Skyrim.h"
#include "maras/domain/lineage/FertilityLifecycleRules.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::App {

    /**
     * Application-layer coordinator consolidating FMR and Beeing Female NG fertility
     * lifecycle orchestration: conception, visibility/trimester reveal, labor & multi-fetus
     * delivery batching, pregnancy loss/stillbirth, child growth/training/graduation,
     * spawn routing, and SkyrimNet/affection dispatch.
     */
    class FertilityLifecycleCoordinator {
    public:
        [[nodiscard]] static FertilityLifecycleCoordinator& GetSingleton();

        FertilityLifecycleCoordinator(const FertilityLifecycleCoordinator&) = delete;
        FertilityLifecycleCoordinator& operator=(const FertilityLifecycleCoordinator&) = delete;
        FertilityLifecycleCoordinator(FertilityLifecycleCoordinator&&) = delete;
        FertilityLifecycleCoordinator& operator=(FertilityLifecycleCoordinator&&) = delete;

        void OnConception(
            std::string_view provider,
            RE::Actor* mother,
            const std::vector<RE::Actor*>& bioFathers,
            std::int32_t trackingOrChildCount,
            float currentGameDay);

        void OnProgressStatus(
            std::string_view provider,
            RE::Actor* mother,
            std::int32_t stageOrRank,
            float currentGameDay);

        void OnLaborStarted(
            RE::Actor* mother,
            std::int32_t childCount,
            RE::Actor* fallbackFather,
            float currentGameDay);

        void OnBirth(
            std::string_view provider,
            RE::Actor* mother,
            RE::Actor* bioFather,
            const std::string& childName,
            std::int32_t babySex,
            float currentGameDay);

        void OnPregnancyLoss(
            std::string_view provider,
            RE::Actor* mother,
            RE::Actor* bioFather,
            const std::string& reason,
            bool requireRemovedRecord);

        void OnChildGrewUp(
            RE::Actor* mother,
            const std::string& childName,
            std::int32_t gender,
            bool isNpcNpc,
            RE::Actor* fatherHint = nullptr);

        void OnChildGrewUpActor(
            RE::Actor* childActor,
            const std::string& childName);

        void OnChildSentToTraining(
            const std::string& childName,
            const std::string& trainingLocation,
            const std::string& className);

        void OnChildFinishedTraining(const std::string& childName);

        void OnAdultChildSpawned(
            std::string_view provider,
            RE::Actor* adultActor,
            RE::Actor* oldChildActor,
            RE::Actor* motherHint,
            RE::Actor* fatherHint,
            const std::string& childName,
            bool allowFallbackToAdultId,
            bool fireEvent = true);

        [[nodiscard]] std::int32_t FindInfantForSpawn(
            RE::Actor* mother,
            const std::string& childName) const;

        std::int32_t ResolveFertilitySpawnSyntheticId(
            RE::Actor* childActor,
            RE::Actor* bioMother,
            RE::Actor* bioFather,
            std::int32_t existingSyntheticId,
            float dob,
            const std::string& childName);

        bool RegisterNpcFertilityChildSpawn(
            RE::Actor* childActor,
            std::int32_t childId,
            RE::Actor* bioMother,
            RE::Actor* bioFather,
            std::int32_t stage,
            bool isMigrating);

        [[nodiscard]] RE::Actor* GetFertilitySpawnCoSpouse(
            std::int32_t childId,
            RE::Actor* bioMother = nullptr,
            RE::Actor* bioFather = nullptr) const;

        std::int32_t MigrateCompletedFmrNpcChild(
            RE::Actor* mother,
            RE::Actor* father,
            float conceptionTime,
            float birthTime);

        void ShowBlockedFertilityMcmModal(
            Domain::Lineage::BlockedFertilityMcmClick blockedClick,
            std::string_view optionStateName);

    private:
        FertilityLifecycleCoordinator() = default;
        ~FertilityLifecycleCoordinator() = default;

        void FinalizeCompletedDeliveryBatch(
            RE::FormID motherId,
            const std::vector<Foundation::ChildId>& bornChildIds);

        void ApplyGrowUpTransition(
            RE::FormID childId,
            const std::string& childName,
            std::int32_t gender);

        [[nodiscard]] std::string PickNpcChildNameForParents(
            RE::Actor* mother,
            RE::Actor* father,
            bool isFemale) const;
    };

}  // namespace MARAS::App
