#include "PCH.h"

#include <spdlog/sinks/basic_file_sink.h>

#include "core/AdultChildProgression.h"
#include "core/AdultChildTravelService.h"
#include "core/AffectionService.h"
#include "core/BanterCoordinatorService.h"
#include "core/BonusesService.h"
#include "core/DeathEventHandler.h"
#include "core/DeceasedRegistry.h"
#include "core/DialogueEventSink.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "core/ChildrenFleeService.h"
#include "core/IntimateThreadService.h"
#include "core/LoggingService.h"
#include "core/MarriageDifficulty.h"
#include "core/MatchmakingService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/NameService.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "core/QuestEventHandler.h"
#include "core/Serialization.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIManager.h"
#include "utils/ActorUtils.h"
#include "utils/Settings.h"

using namespace SKSE;

// Define the global logger (declared extern in Common.h)
namespace MARAS {
    std::shared_ptr<spdlog::logger> g_Logger;
}

namespace {
    // Event sink marshaling PollingService::Update onto the SKSE main thread from MenuOpenCloseEvent and throttled InputEvent frames
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
            if (MARAS::PrismaModalManager::GetSingleton().HasActiveModal()) {
                MARAS::AddCurrentEpochTask("UpdateEventSink::ResendActiveModal", []() {
                    auto& uiMgr = MARAS::PrismaUIManager::GetSingleton();
                    if (!uiMgr.IsModalDisplayBlocked()) {
                        MARAS::PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/false);
                    }
                    uiMgr.SyncViewVisibilityAndFocus();
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
                if (actorFormID != 0) {
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
                        pendingInitActors_.push_back(actorFormID);
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
            if (a_event && a_event->loaded && a_event->formID != 0) {
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
                    pendingLoadedForms_.push_back(a_event->formID);
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
                    std::lock_guard papyrusLock(MARAS::PapyrusInterface::GetPapyrusMutex());
                    auto& npcIndex = MARAS::NPCIndex::GetSingleton();
                    auto& relMgr = MARAS::NPCRelationshipManager::GetSingleton();

                    std::unordered_set<RE::FormID> processedInit;
                    processedInit.reserve(initIDs.size());
                    for (const RE::FormID actorID : initIDs) {
                        if (actorID == 0 || !processedInit.insert(actorID).second) {
                            continue;
                        }
                        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
                        if (!actor || actor->IsDeleted()) {
                            continue;
                        }
                        npcIndex.RegisterLoadedActor(actor);
                        relMgr.TryResolvePendingRelationships(actor);
                    }

                    std::unordered_set<RE::FormID> processedLoaded;
                    processedLoaded.reserve(loadedIDs.size());
                    for (const RE::FormID loadedFormID : loadedIDs) {
                        if (loadedFormID == 0 || !processedLoaded.insert(loadedFormID).second) {
                            continue;
                        }
                        auto* form = RE::TESForm::LookupByID(loadedFormID);
                        auto* actor = form ? form->As<RE::Actor>() : nullptr;
                        if (!actor || actor->IsDeleted()) {
                            continue;
                        }
                        if (!processedInit.contains(loadedFormID)) {
                            npcIndex.RegisterLoadedActor(actor);
                            relMgr.TryResolvePendingRelationships(actor);
                        }
                        if (actor->Is3DLoaded() && relMgr.IsChildNPC(loadedFormID)) {
                            relMgr.ApplyChildVisualsOn3DLoad(actor);
                        }
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

    std::shared_ptr<spdlog::logger> SetupLogging() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("MARAS: log directory unavailable");
            }
            return nullptr;
        }

        std::filesystem::path logPath = *logDir;
        if (!std::filesystem::is_directory(logPath)) {
            logPath = logPath.parent_path();
        }
        logPath /= "MARAS.log";

        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
        if (ec) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("MARAS: failed to create log folder (%s)", ec.message().c_str());
            }
            return nullptr;
        }

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("MARAS", std::move(sink));
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(3));
        logger->set_pattern("[%H:%M:%S] [%l] %v");

        // Store in global variable - this is the SINGLE source of truth
        MARAS::g_Logger = logger;

        // Load settings early from MARAS.ini to set log level from INI
        MARAS::Settings::GetSingleton().Load();

        logger->info("MARAS logger initialized with pattern: [HH:MM:SS] [level] message");

        return logger;
    }
}

namespace MARAS {
    void ResetPluginEventSinks() noexcept {
        UpdateEventSink::GetSingleton()->Reset();
        ActorInitLoadedEventSink::GetSingleton()->Reset();
    }
}

SKSEPluginLoad(const LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    MARAS::Serialization::InitializeFoundationLogSink();
    MARAS_LOG_INFO("MARAS plugin loading...");

    // Register serialization callbacks
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

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPostLoad:
                        SKSE::AllocTrampoline(256);
                        MARAS::PackageOverrideService::InstallHooks();
                        MARAS::FactionService::InstallHooks();
                        MARAS::RegisterSkyrimNetDecorators();
                        break;

                    case SKSE::MessagingInterface::kInputLoaded:
                        MARAS_LOG_INFO("Input loaded — starting input listening for PrismaUIManager and PollingService");
                        MARAS::PrismaUIManager::GetSingleton().StartListeningInput();
                        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
                            dm->RemoveEventSink(UpdateEventSink::GetSingleton());
                            dm->AddEventSink(UpdateEventSink::GetSingleton());
                        }
                        break;

                    case SKSE::MessagingInterface::kPreLoadGame:
                        MARAS_LOG_INFO("PreLoadGame...");
                        MARAS::BumpSessionEpoch("MessagingInterface::kPreLoadGame");
                        MARAS::ResetPluginEventSinks();
                        MARAS::PollingService::GetSingleton().Shutdown();
                        MARAS::AdultChildProgression::GetSingleton().Revert();
                        MARAS::NPCIndex::GetSingleton().ClearLoadedActors();
                        MARAS::PrismaModalManager::GetSingleton().Revert();
                        MARAS::PrismaUIManager::GetSingleton().Destroy();
                        MARAS::IntimateThreadService::GetSingleton().ClearAll();
                        MARAS::ChildrenFleeService::GetSingleton().Reset();
                        MARAS::SkyrimNetMemoryService::GetSingleton().Revert();
                        MARAS::SkyrimNetProfileService::GetSingleton().Revert();
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame: {
                        const bool isNewGame = (message->type == SKSE::MessagingInterface::kNewGame);
                        MARAS_LOG_INFO("New game/Load (isNewGame={})...", isNewGame);
                        MARAS::NPCIndex::GetSingleton().RefreshIndex();

                        // Load runtime-only bonuses.json, marriage difficulty config, and child training config on game load
                        MARAS::BonusesService::GetSingleton().LoadFromFile();
                        MARAS::MarriageDifficulty::LoadConfig();
                        MARAS::AdultChildProgression::GetSingleton().LoadConfig();

                        // Initialize/reset polling service state to prevent false events from previous save
                        MARAS::PollingService::GetSingleton().Initialize();
                        MARAS::IntimateThreadService::GetSingleton().Initialize();
                        MARAS::ChildrenFleeService::GetSingleton().Initialize();

                        // Re-derive children's package/LinkedRef/pet-LinkedRef state after load
                        MARAS::PlayerChildrenService::GetSingleton().ReassertAllOnLoad();
                        MARAS::AdultChildTravelService::GetSingleton().ReassertAllOnLoad();
                        MARAS::NPCRelationshipManager::GetSingleton().ReassertAllChildVisualsOnLoad();

                        // Ensure PlayerMarriedFaction is removed if polygamy is enabled
                        MARAS::FactionService::GetSingleton().SanitizePlayerMarriedFaction();
                        MARAS::NPCRelationshipManager::GetSingleton().UpdateCanFormNonCheatingMainFaction(MARAS::kPlayerFormID);

                        // On a new game, defer modals and the initial relationship scan until character creation (RaceSex Menu) closes
                        if (isNewGame) {
                            MARAS::PrismaUIManager::GetSingleton().OnNewGameStarted(
                                !MARAS::Settings::GetSingleton().Snapshot()->initialRelationshipScanDone);
                        } else {
                            MARAS::PrismaUIManager::GetSingleton().ResetNewGameDeferral();
                            if (!MARAS::Settings::GetSingleton().Snapshot()->initialRelationshipScanDone) {
                                MARAS::NPCRelationshipManager::GetSingleton().ScanExistingRelationships(/*isNewGame=*/false);
                                MARAS::State::WorldStore::GetSingleton().Modify([](MARAS::State::WorldState& ws) {
                                    ws.settings.Write().initialRelationshipScanDone = true;
                                });
                            }
                        }

                        // Log statistics after save data has been loaded
                        MARAS::NPCRelationshipManager::GetSingleton().LogStatistics();
                        break;
                    }

                    case SKSE::MessagingInterface::kDataLoaded: {
                        MARAS_LOG_INFO("Data loaded successfully.");

                        // Pre-populate cached forms once at kDataLoaded
                        MARAS::FormCache::GetSingleton().Initialize();

                        // Register event sinks for deferred relationship resolution
                        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                            holder->AddEventSink<RE::TESInitScriptEvent>(ActorInitLoadedEventSink::GetSingleton());
                            holder->AddEventSink<RE::TESObjectLoadedEvent>(ActorInitLoadedEventSink::GetSingleton());
                        }

                        // Initialize the NPC relationship manager
                        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

                        // Load override data
                        std::filesystem::path overrideFolder = "Data/SKSE/Plugins/MARAS/spousesTypes";
                        if (manager.LoadOverridesFromFolder(overrideFolder.string())) {
                            auto stats = manager.GetLastOverrideLoadStats();
                            MARAS_LOG_INFO("Loaded {} NPC type overrides from {} files", manager.GetOverrideCount(),
                                           stats.successfulFiles);
                        } else {
                            MARAS_LOG_WARN("Failed to load NPC type overrides from {}", overrideFolder.string());
                        }

                        // Build the home/cell index (doors, persistent actors, furniture owners)
                        MARAS::HomeCellService::GetSingleton().BuildIndex();
                        MARAS::AdultChildTravelService::GetSingleton().BuildDestinationIndex();

                        // Build the centralized NPC index (all unique/named NPCs, base->ref resolution)
                        MARAS::NPCIndex::GetSingleton().BuildIndex();

                        // Load quest event configurations
                        std::filesystem::path questEventFolder = "Data/SKSE/Plugins/MARAS/questEvents";
                        if (MARAS::QuestEventManager::GetSingleton().LoadConfigFromFolder(questEventFolder.string())) {
                            MARAS_LOG_INFO("Loaded {} quest event configurations",
                                           MARAS::QuestEventManager::GetSingleton().GetConfigCount());
                        } else {
                            MARAS_LOG_INFO("No quest event configurations found (folder may not exist: {})",
                                           questEventFolder.string());
                        }

                        // Load package override suppression patterns from INI
                        MARAS::PackageOverrideService::GetSingleton().LoadConfig();

                        // Register quest event sinks
                        auto* scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
                        if (scriptEventSourceHolder) {
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStartStopEvent>(
                                MARAS::QuestStartStopEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStageEvent>(
                                MARAS::QuestStageEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESDeathEvent>(
                                MARAS::DeathEventHandler::GetSingleton());
                            MARAS_LOG_INFO("Registered quest event sinks and TESDeathEvent sink");
                        } else {
                            MARAS_LOG_ERROR("Failed to get ScriptEventSourceHolder for event sink registration");
                        }

                        // Register update event sink for polling service
                        auto ui = RE::UI::GetSingleton();
                        if (ui) {
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(UpdateEventSink::GetSingleton());
                            ui->AddEventSink<RE::MenuOpenCloseEvent>(MARAS::DialogueEventSink::GetSingleton());
                            MARAS_LOG_INFO("Registered polling service update event sink");
                            MARAS_LOG_INFO("Registered dialogue event sink");
                        } else {
                            MARAS_LOG_ERROR("Failed to get UI singleton for event sink registration");
                        }

                        // Initialize PrismaUI
                        MARAS::PrismaUIManager::GetSingleton().Initialize();

                        // Load random child and critter names and child training catalog from JSON configs at startup
                        MARAS::NameService::GetSingleton().LoadFromFile();
                        MARAS::AdultChildProgression::GetSingleton().LoadConfig();

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

    // Register Papyrus functions
    if (const auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(MARAS::PapyrusInterface::RegisterPapyrusFunctions)) {
            MARAS_LOG_ERROR("Failed to register Papyrus functions.");
            return false;
        }
    } else {
        MARAS_LOG_ERROR("Papyrus interface unavailable.");
        return false;
    }

    return true;
}
