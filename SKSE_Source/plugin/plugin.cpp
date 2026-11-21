#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/spdlog.h>

#include "maras/app/AppContext.h"
#include "maras/app/AppLifecycleCoordinator.h"
#include "maras/app/ChildSkyrimNetSuccession.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/bridge_papyrus/PapyrusInterface.h"
#include "maras/bridge_papyrus/SanityConsoleCommand.h"
#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"
#include "maras/bridge_ui/ModalManager.h"
#include "maras/bridge_ui/UIDataBridge.h"
#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/BonusesConfigLoader.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/ChildVisualReconciler.h"
#include "maras/engine/ChildrenFleeService.h"
#include "maras/engine/DeathEventHandler.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/DialogueEventSink.h"
#include "maras/engine/EncounterConfigLoader.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FertilityMcmClickSink.h"
#include "maras/engine/FertilityNamingMenuSink.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/GiftMenuEventSink.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/IntimateThreadService.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/MarriageDifficultyLoader.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/engine/PlayerMiscStatsTracker.h"
#include "maras/engine/PollingService.h"
#include "maras/engine/PromptStringsLoader.h"
#include "maras/engine/QuestEventHandler.h"
#include "maras/engine/SkseSerializationCallbacks.h"
#include "maras/engine/SleepEventSink.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <vector>

namespace {

    // Marshals PollingService::Update, modal resend checks, and deferred dialogue speaker lookup onto the SKSE main thread.
    class UpdateEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                            public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static UpdateEventSink* GetSingleton() {
            static UpdateEventSink singleton;
            return &singleton;
        }

        void Reset() noexcept {
            pollTaskQueued_.store(false, std::memory_order_release);
            lastInputTick_ = {};
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent*,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            QueuePollOnMainThread();
            if (MARAS::App::GetModalGateway().HasActiveModal()) {
                MARAS::AddCurrentEpochTask("UpdateEventSink::ResendActiveModal", []() {
                    MARAS::App::GetModalGateway().SendActiveModalToUI(/*forceResend=*/false);
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*,
                                              RE::BSTEventSource<RE::InputEvent*>*) override {
            if (MARAS::DialogueEventSink::GetSingleton()->IsWaitingForSpeaker()) {
                MARAS::AddCurrentEpochTask("UpdateEventSink::ResolveDeferredSpeaker", []() {
                    MARAS::DialogueEventSink::GetSingleton()->TryResolveDeferredSpeaker();
                });
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastInputTick_ >= std::chrono::milliseconds(500)) {
                lastInputTick_ = now;
                QueuePollOnMainThread();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        UpdateEventSink() = default;
        UpdateEventSink(const UpdateEventSink&) = delete;
        UpdateEventSink(UpdateEventSink&&) = delete;
        UpdateEventSink& operator=(const UpdateEventSink&) = delete;
        UpdateEventSink& operator=(UpdateEventSink&&) = delete;

        void QueuePollOnMainThread() {
            if (pollTaskQueued_.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            const auto* taskIF = SKSE::GetTaskInterface();
            if (!taskIF) {
                pollTaskQueued_.store(false, std::memory_order_release);
                return;
            }
            const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
            taskIF->AddTask([this, expectedEpoch]() {
                MARAS::ScopedMainThreadTask mainScope;
                pollTaskQueued_.store(false, std::memory_order_release);
                if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                try {
                    MARAS::PollingService::GetSingleton().Update();
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("UpdateEventSink: exception in PollingService::Update: {}", e.what());
                } catch (...) {
                    MARAS_LOG_ERROR("UpdateEventSink: unknown exception in PollingService::Update");
                }
            });
        }

        std::chrono::steady_clock::time_point lastInputTick_{};
        std::atomic<bool> pollTaskQueued_{ false };
    };

    class ActorInitLoadedEventSink : public RE::BSTEventSink<RE::TESInitScriptEvent>,
                                     public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static ActorInitLoadedEventSink* GetSingleton() {
            static ActorInitLoadedEventSink singleton;
            return &singleton;
        }

        void Reset() noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingInitActors_.clear();
            pendingLoadedForms_.clear();
            drainQueued_ = false;
            queuedEpoch_ = MARAS::GetSessionEpoch();
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESInitScriptEvent* a_event,
                                              RE::BSTEventSource<RE::TESInitScriptEvent>*) override {
            if (a_event && a_event->objectInitialized &&
                a_event->objectInitialized->GetFormType() == RE::FormType::ActorCharacter) {
                const RE::FormID actorFormID = a_event->objectInitialized->GetFormID();
                if (MARAS::HasPhysicalActor(actorFormID)) {
                    bool shouldSchedule = false;
                    MARAS::SessionEpoch scheduleEpoch = 0;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const MARAS::SessionEpoch curEpoch = MARAS::GetSessionEpoch();
                        if (queuedEpoch_ != curEpoch) {
                            pendingInitActors_.clear();
                            pendingLoadedForms_.clear();
                            drainQueued_ = false;
                            queuedEpoch_ = curEpoch;
                        }
                        if (std::find(pendingInitActors_.begin(), pendingInitActors_.end(), actorFormID) ==
                            pendingInitActors_.end()) {
                            pendingInitActors_.push_back(actorFormID);
                        }
                        if (!drainQueued_) {
                            drainQueued_ = true;
                            shouldSchedule = true;
                            scheduleEpoch = curEpoch;
                        }
                    }
                    if (shouldSchedule) {
                        ScheduleDrainOnMainThread(scheduleEpoch);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
                                              RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override {
            if (a_event && a_event->loaded && MARAS::HasPhysicalActor(a_event->formID)) {
                auto* form = RE::TESForm::LookupByID(a_event->formID);
                if (!form || form->GetFormType() != RE::FormType::ActorCharacter) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                bool shouldSchedule = false;
                MARAS::SessionEpoch scheduleEpoch = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const MARAS::SessionEpoch curEpoch = MARAS::GetSessionEpoch();
                    if (queuedEpoch_ != curEpoch) {
                        pendingInitActors_.clear();
                        pendingLoadedForms_.clear();
                        drainQueued_ = false;
                        queuedEpoch_ = curEpoch;
                    }
                    if (std::find(pendingLoadedForms_.begin(), pendingLoadedForms_.end(), a_event->formID) ==
                        pendingLoadedForms_.end()) {
                        pendingLoadedForms_.push_back(a_event->formID);
                    }
                    if (!drainQueued_) {
                        drainQueued_ = true;
                        shouldSchedule = true;
                        scheduleEpoch = curEpoch;
                    }
                }
                if (shouldSchedule) {
                    ScheduleDrainOnMainThread(scheduleEpoch);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        ActorInitLoadedEventSink() = default;

        void ScheduleDrainOnMainThread(MARAS::SessionEpoch expectedEpoch) {
            if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queuedEpoch_ == expectedEpoch) {
                    drainQueued_ = false;
                }
                return;
            }
            const auto* taskIF = SKSE::GetTaskInterface();
            if (!taskIF) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queuedEpoch_ == expectedEpoch) {
                    drainQueued_ = false;
                }
                return;
            }
            taskIF->AddTask([this, expectedEpoch]() {
                MARAS::ScopedMainThreadTask mainScope;
                std::vector<RE::FormID> initIDs;
                std::vector<RE::FormID> loadedIDs;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queuedEpoch_ != expectedEpoch || !MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                        return;
                    }
                    initIDs.swap(pendingInitActors_);
                    loadedIDs.swap(pendingLoadedForms_);
                    drainQueued_ = false;
                }
                try {
                    if (auto* hooks = MARAS::Engine::GetEngineEventHooks()) {
                        hooks->OnActorLoadedBatch(initIDs, loadedIDs);
                    }
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("ActorInitLoadedEventSink: exception draining queued events: {}", e.what());
                } catch (...) {
                    MARAS_LOG_ERROR("ActorInitLoadedEventSink: unknown exception draining queued events");
                }
            });
        }

        std::mutex mutex_;
        std::vector<RE::FormID> pendingInitActors_;
        std::vector<RE::FormID> pendingLoadedForms_;
        bool drainQueued_{ false };
        MARAS::SessionEpoch queuedEpoch_{ 0 };
    };

    class CellFullyLoadedEventSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent> {
    public:
        static CellFullyLoadedEventSink* GetSingleton() {
            static CellFullyLoadedEventSink singleton;
            return &singleton;
        }

        void Reset() noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingCells_.clear();
            drainQueued_ = false;
            queuedEpoch_ = MARAS::GetSessionEpoch();
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCellFullyLoadedEvent* a_event,
            RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override {
            if (!a_event || !a_event->cell || !a_event->cell->IsInteriorCell()) {
                return RE::BSEventNotifyControl::kContinue;
            }
            const RE::FormID cellFormID = a_event->cell->GetFormID();
            if (cellFormID == 0) {
                return RE::BSEventNotifyControl::kContinue;
            }

            bool shouldSchedule = false;
            MARAS::SessionEpoch scheduleEpoch = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const MARAS::SessionEpoch curEpoch = MARAS::GetSessionEpoch();
                if (queuedEpoch_ != curEpoch) {
                    pendingCells_.clear();
                    drainQueued_ = false;
                    queuedEpoch_ = curEpoch;
                }
                if (std::find(pendingCells_.begin(), pendingCells_.end(), cellFormID) == pendingCells_.end()) {
                    pendingCells_.push_back(cellFormID);
                }
                if (!drainQueued_) {
                    drainQueued_ = true;
                    shouldSchedule = true;
                    scheduleEpoch = curEpoch;
                }
            }
            if (shouldSchedule) {
                ScheduleDrainOnMainThread(scheduleEpoch);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        CellFullyLoadedEventSink() = default;

        void ScheduleDrainOnMainThread(MARAS::SessionEpoch expectedEpoch) {
            if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queuedEpoch_ == expectedEpoch) {
                    drainQueued_ = false;
                }
                return;
            }
            const auto* taskIF = SKSE::GetTaskInterface();
            if (!taskIF) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queuedEpoch_ == expectedEpoch) {
                    drainQueued_ = false;
                }
                return;
            }
            taskIF->AddTask([this, expectedEpoch]() {
                MARAS::ScopedMainThreadTask mainScope;
                std::vector<RE::FormID> cellIDs;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queuedEpoch_ != expectedEpoch || !MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                        return;
                    }
                    cellIDs.swap(pendingCells_);
                    drainQueued_ = false;
                }
                try {
                    std::lock_guard papyrusLock(MARAS::GetPapyrusMutex());
                    for (const RE::FormID cellID : cellIDs) {
                        MARAS::BedOwnershipReconciler::GetSingleton().OnCellFullyLoaded(cellID);
                    }
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("CellFullyLoadedEventSink: exception draining queued cells: {}", e.what());
                } catch (...) {
                    MARAS_LOG_ERROR("CellFullyLoadedEventSink: unknown exception draining queued cells");
                }
            });
        }

        std::mutex mutex_;
        std::vector<RE::FormID> pendingCells_;
        bool drainQueued_{ false };
        MARAS::SessionEpoch queuedEpoch_{ 0 };
    };

    void ResetAllPluginEventSinks() noexcept {
        UpdateEventSink::GetSingleton()->Reset();
        ActorInitLoadedEventSink::GetSingleton()->Reset();
        CellFullyLoadedEventSink::GetSingleton()->Reset();
    }

    void SetupLoggingAndSettings() {
        MARAS::Engine::InitializeFileLogger();
        if (const auto& logger = MARAS::GetLogger()) {
            logger->set_pattern("[%H:%M:%S] [%l] %v");
        }
        spdlog::flush_every(std::chrono::seconds(3));

        // Load early INI configuration so the configured log level applies immediately.
        MARAS::App::SettingsCoordinator::GetSingleton().Load();
        MARAS_LOG_INFO("MARAS logger initialized with pattern: [HH:MM:SS] [level] message");
    }

    void WireLayerGatewaysAndHooks() noexcept {
        MARAS::Bridge::Ui::UIDataBridge::RegisterAppGateway();
        MARAS::Bridge::Ui::ModalManager::RegisterAppGateway();
        MARAS::Bridge::SkyrimNet::RegisterAppGateway();
        MARAS::App::AppLifecycleCoordinator::GetSingleton().RegisterWithEngine();
        MARAS::RegisterPluginEventSinksResetter(&ResetAllPluginEventSinks);
    }

}  // namespace

SKSEPluginInfo(
    .Version = REL::Version{ 0, 0, 1, 0 },
    .Name = "MARAS",
    .Author = "",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 }
)

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLoggingAndSettings();
    WireLayerGatewaysAndHooks();
    MARAS::DeferredActorResolver::GetSingleton().CaptureMainThread();
    MARAS_LOG_INFO("MARAS plugin loading...");

    if (const auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(MARAS::Serialization::kMarasPluginID);
        serialization->SetSaveCallback(MARAS::Serialization::SaveCallback);
        serialization->SetLoadCallback(MARAS::Serialization::LoadCallback);
        serialization->SetRevertCallback(MARAS::Serialization::RevertCallback);
        MARAS_LOG_INFO("Registered MARAS serialization callbacks");
    } else {
        MARAS_LOG_ERROR("Serialization interface unavailable.");
        return false;
    }

    if (const auto* scaleform = SKSE::GetScaleformInterface()) {
        scaleform->Register(&MARAS::FertilityMcmClickSink::RegisterScaleformCallback, "MARAS");
    }

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPostLoad:
                        SKSE::AllocTrampoline(256);
                        MARAS::PackageReconciler::InstallHooks();
                        MARAS::FactionReconciler::InstallHooks();
                        MARAS::App::GetSkyrimNetGateway().RegisterDecorators();
                        break;

                    case SKSE::MessagingInterface::kInputLoaded:
                        MARAS::DeferredActorResolver::GetSingleton().CaptureMainThread();
                        MARAS_LOG_INFO("Input loaded — starting input listening for UI gateway and PollingService");
                        MARAS::App::GetUiBridgeGateway().StartListeningInput();
                        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
                            dm->RemoveEventSink(UpdateEventSink::GetSingleton());
                            dm->AddEventSink(UpdateEventSink::GetSingleton());
                        }
                        break;

                    case SKSE::MessagingInterface::kPreLoadGame:
                        MARAS_LOG_INFO("PreLoadGame...");
                        MARAS::BumpSessionEpoch("MessagingInterface::kPreLoadGame");
                        MARAS::ResetPluginEventSinks();
                        MARAS::DialogueEventSink::GetSingleton()->Revert();
                        MARAS::GiftMenuEventSink::GetSingleton()->Revert();
                        MARAS::FertilityNamingMenuSink::GetSingleton()->Revert();
                        MARAS::PlayerMiscStatsTracker::GetSingleton()->Revert();
                        MARAS::DeferredActorResolver::GetSingleton().Revert();
                        MARAS::GlobalVarReconciler::GetSingleton().Revert();
                        MARAS::FactionReconciler::GetSingleton().Revert();
                        MARAS::BuffReconciler::GetSingleton().Revert();
                        MARAS::PackageReconciler::GetSingleton().Revert();
                        MARAS::PollingService::GetSingleton().Shutdown();
                        MARAS::ChildVisualReconciler::GetSingleton().Revert();
                        MARAS::SpatialWorldIndex::GetSingleton().ClearLoadedActors();
                        MARAS::App::GetModalGateway().Revert();
                        MARAS::App::GetUiBridgeGateway().Revert();
                        MARAS::IntimateThreadService::GetSingleton().ClearAll();
                        MARAS::ChildrenFleeService::GetSingleton().Reset();
                        MARAS::App::GetSkyrimNetGateway().RevertMemories();
                        MARAS::App::GetSkyrimNetGateway().RevertProfiles();
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame: {
                        const bool isNewGame = (message->type == SKSE::MessagingInterface::kNewGame);
                        MARAS_LOG_INFO("New game/Load (isNewGame={})...", isNewGame);
                        MARAS::SpatialWorldIndex::GetSingleton().RefreshIndex();

                        // Reload runtime configs cleanly per save load / new game
                        MARAS::Engine::BonusesConfigLoader::GetSingleton().Initialize();
                        MARAS::Engine::MarriageDifficultyLoader::GetSingleton().LoadConfig();
                        MARAS::Engine::TrainingCatalogLoader::GetSingleton().Initialize();
                        MARAS::Engine::EncounterConfigLoader::GetSingleton().Initialize();
                        MARAS::Engine::PromptStringsLoader::GetSingleton().Initialize();

                        // Initialize/reset polling and runtime background services
                        MARAS::PollingService::GetSingleton().Initialize();
                        MARAS::IntimateThreadService::GetSingleton().Initialize();
                        MARAS::ChildrenFleeService::GetSingleton().Initialize();
                        MARAS::PlayerMiscStatsTracker::GetSingleton()->SeedFromVmAsync();

                        // Converge child packages, adult travel, child visuals, globals, factions, and buffs
                        MARAS::PlayerChildrenEngineOps::GetSingleton().ReassertChildPackagesOnLoad();
                        MARAS::App::HouseholdUseCases::GetSingleton().ReassertAllTravelOnLoad();
                        MARAS::ChildVisualReconciler::GetSingleton().FlushRetiredForms();
                        MARAS::ChildVisualReconciler::GetSingleton().ReassertAllChildVisualsOnLoad();

                        if (isNewGame) {
                            MARAS::GlobalVarReconciler::GetSingleton().ResetSessionGlobalsToDefaults();
                        }
                        MARAS::GlobalVarReconciler::GetSingleton().ReconcileGlobals();
                        MARAS::FactionReconciler::GetSingleton().ReconcilePlayer();
                        MARAS::BuffReconciler::GetSingleton().ReconcilePlayerBuffs();

                        auto& ui = MARAS::App::GetUiBridgeGateway();
                        if (isNewGame) {
                            ui.OnNewGameStarted(
                                !MARAS::App::SettingsCoordinator::GetSingleton().Snapshot()->initialRelationshipScanDone);
                        } else {
                            ui.ResetNewGameDeferral();
                            if (!MARAS::App::SettingsCoordinator::GetSingleton().Snapshot()->initialRelationshipScanDone) {
                                MARAS::App::RelationshipUseCases::GetSingleton().ScanExistingRelationships(/*isNewGame=*/false);
                                MARAS::State::WorldStore::GetSingleton().Modify([](MARAS::State::WorldState& ws) {
                                    ws.settings.Write().initialRelationshipScanDone = true;
                                });
                            }
                        }

                        MARAS::App::RelationshipUseCases::GetSingleton().LogStatistics();
                        if (!isNewGame && MARAS::App::GetSkyrimNetGateway().IsAvailable()) {
                            MARAS::AddCurrentEpochTask("PostLoad::SyncSpawnedChildrenSkyrimNetUuids", []() {
                                std::lock_guard lock(MARAS::GetPapyrusMutex());
                                MARAS::App::SyncSpawnedChildrenOnPostLoad();
                            });
                        }
                        break;
                    }

                    case SKSE::MessagingInterface::kDataLoaded: {
                        MARAS_LOG_INFO("Data loaded successfully.");

                        MARAS::FormCache::GetSingleton().Initialize();
                        MARAS::PlayerMiscStatsTracker::GetSingleton()->Initialize();

                        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                            holder->AddEventSink<RE::TESInitScriptEvent>(ActorInitLoadedEventSink::GetSingleton());
                            holder->AddEventSink<RE::TESObjectLoadedEvent>(ActorInitLoadedEventSink::GetSingleton());
                            holder->AddEventSink<RE::TESCellFullyLoadedEvent>(CellFullyLoadedEventSink::GetSingleton());
                        }

                        auto& relUseCases = MARAS::App::RelationshipUseCases::GetSingleton();
                        const std::filesystem::path overrideFolder = "Data/SKSE/Plugins/MARAS/spousesTypes";
                        if (relUseCases.LoadOverridesFromFolder(overrideFolder.string())) {
                            const auto stats = relUseCases.GetLastOverrideLoadStats();
                            MARAS_LOG_INFO("Loaded {} NPC type overrides from {} files",
                                           relUseCases.GetOverrideCount(),
                                           stats.successfulFiles);
                        } else {
                            MARAS_LOG_WARN("Failed to load NPC type overrides from {}", overrideFolder.string());
                        }

                        // Build spatial index (NPCs, home cells, inn destinations)
                        MARAS::SpatialWorldIndex::GetSingleton().BuildIndex();

                        // Load quest event configurations
                        const std::filesystem::path questEventFolder = "Data/SKSE/Plugins/MARAS/questEvents";
                        if (MARAS::QuestEventManager::GetSingleton().LoadConfigFromFolder(questEventFolder.string())) {
                            MARAS_LOG_INFO("Loaded {} quest event configurations",
                                           MARAS::QuestEventManager::GetSingleton().GetConfigCount());
                        } else {
                            MARAS_LOG_INFO("No quest event configurations found (folder may not exist: {})",
                                           questEventFolder.string());
                        }

                        // Load package override suppression patterns from INI
                        MARAS::PackageReconciler::GetSingleton().LoadConfig();

                        // Register quest & death event sinks
                        if (auto* scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStartStopEvent>(
                                MARAS::QuestStartStopEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStageEvent>(
                                MARAS::QuestStageEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESDeathEvent>(
                                MARAS::DeathEventHandler::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESTrackedStatsEvent>(
                                MARAS::PlayerMiscStatsTracker::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESContainerChangedEvent>(
                                MARAS::GiftMenuEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESSleepStopEvent>(
                                MARAS::SleepEventSink::GetSingleton());
                            MARAS_LOG_INFO(
                                "Registered quest event sinks, TESDeathEvent, TESTrackedStatsEvent, TESContainerChangedEvent, and TESSleepStopEvent sinks");
                        } else {
                            MARAS_LOG_ERROR("Failed to get ScriptEventSourceHolder for event sink registration");
                        }

                        // Register UI polling, dialogue menu, gift menu & fertility MCM click sinks
                        if (auto* ui = RE::UI::GetSingleton()) {
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(UpdateEventSink::GetSingleton());
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(MARAS::DialogueEventSink::GetSingleton());
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(MARAS::GiftMenuEventSink::GetSingleton());
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(MARAS::FertilityMcmClickSink::GetSingleton());
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(MARAS::FertilityNamingMenuSink::GetSingleton());
                            MARAS_LOG_INFO("Registered polling service update event sink");
                            MARAS_LOG_INFO("Registered dialogue, gift menu, and fertility MCM/naming event sinks");
                        } else {
                            MARAS_LOG_ERROR("Failed to get UI singleton for event sink registration");
                        }

                        // Refresh localization once Skyrim.ini (sLanguage:General) is loaded, then initialize UI & catalogs
                        MARAS::Engine::LocalizationService::GetSingleton().Reload(
                            MARAS::App::SettingsCoordinator::GetSingleton().GetLanguage());

                        // Initialize UI dashboard
                        MARAS::App::GetUiBridgeGateway().InitializeDashboard();

                        // Load static JSON/YAML catalogs at startup
                        MARAS::Engine::BonusesConfigLoader::GetSingleton().Initialize();
                        MARAS::Engine::MarriageDifficultyLoader::GetSingleton().LoadConfig();
                        MARAS::Engine::NamePoolLoader::GetSingleton().Initialize();
                        MARAS::Engine::TrainingCatalogLoader::GetSingleton().Initialize();
                        MARAS::Engine::EncounterConfigLoader::GetSingleton().Initialize();
                        MARAS::Engine::PromptStringsLoader::GetSingleton().Initialize();

                        (void)MARAS::PapyrusInterface::RegisterSanityConsoleCommand();

                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("MARAS: Ready");
                        }
                        break;
                    }

                    default:
                        break;
                }
            })) {
            MARAS_LOG_ERROR("Failed to register messaging listener.");
            return false;
        }
    } else {
        MARAS_LOG_ERROR("Messaging interface unavailable.");
        return false;
    }

    if (const auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(MARAS::PapyrusInterface::RegisterPapyrusFunctions)) {
            MARAS_LOG_ERROR("Failed to register Papyrus functions.");
            return false;
        }
        if (!papyrus->Register(MARAS::FactionReconciler::RegisterPapyrusHooks)) {
            MARAS_LOG_WARN("Failed to register MARAS Papyrus faction hooks.");
        }
    } else {
        MARAS_LOG_ERROR("Papyrus interface unavailable.");
        return false;
    }

    return true;
}
