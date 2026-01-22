#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#include "PCH.h"
#include "core/AffectionService.h"
#include "core/BonusesService.h"
#include "core/HomeCellService.h"
#include "core/LoggingService.h"
#include "core/MarriageDifficulty.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "core/QuestEventHandler.h"
#include "core/Serialization.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "papyrus/PapyrusInterface.h"

using namespace SKSE;

// Define the global logger (declared extern in Common.h)
namespace MARAS {
    std::shared_ptr<spdlog::logger> g_Logger;
}

namespace {
    // Event sink for OnUpdate to drive polling service
    class UpdateEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static UpdateEventSink* GetSingleton() {
            static UpdateEventSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent*,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            // Called frequently enough to drive our polling service
            MARAS::PollingService::GetSingleton().Update();
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        UpdateEventSink() = default;
        UpdateEventSink(const UpdateEventSink&) = delete;
        UpdateEventSink(UpdateEventSink&&) = delete;
        UpdateEventSink& operator=(const UpdateEventSink&) = delete;
        UpdateEventSink& operator=(UpdateEventSink&&) = delete;
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
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%H:%M:%S] [%l] %v");

        // Store in global variable - this is the SINGLE source of truth
        MARAS::g_Logger = logger;

        logger->info("Logging to {}", logPath.string());
        logger->info("MARAS logger initialized with pattern: [HH:MM:SS] [level] message");

        return logger;
    }

    void PrintToConsole(std::string_view message) {
        MARAS_LOG_INFO("{}", message);
        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print("%s", message.data());
        }
    }

    // SKSE Serialization callbacks
    void SaveCallback(SKSE::SerializationInterface* serialization) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

        if (!serialization->OpenRecord(MARAS::Serialization::kNPCRelationshipData,
                                       MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving");
            return;
        }

        if (!manager.Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save NPC relationship data");
        } else {
            MARAS_LOG_INFO("Successfully saved NPC relationship data");
        }

        // Save spouse hierarchy
        if (!serialization->OpenRecord(MARAS::Serialization::kSpouseHierarchyData,
                                       MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving spouse hierarchy");
            return;
        }
        if (!MARAS::SpouseHierarchyManager::GetSingleton().Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save spouse hierarchy data");
        } else {
            MARAS_LOG_INFO("Successfully saved spouse hierarchy data");
        }

        // Save affection data
        if (!serialization->OpenRecord(MARAS::Serialization::kAffectionData, MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving affection data");
            return;
        }
        if (!MARAS::AffectionService::GetSingleton().Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save affection data");
        } else {
            MARAS_LOG_INFO("Successfully saved affection data");
        }

        // Save player house data
        if (!serialization->OpenRecord(MARAS::Serialization::kPlayerHouseData, MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving player house data");
            return;
        }
        if (!MARAS::PlayerHouseService::GetSingleton().Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save player house data");
        } else {
            MARAS_LOG_INFO("Successfully saved player house data");
        }

        // Save spouse assets data
        if (!serialization->OpenRecord(MARAS::Serialization::kSpouseAssetsData, MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving spouse assets data");
            return;
        }
        if (!MARAS::SpouseAssetsService::GetSingleton().Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save spouse assets data");
        } else {
            MARAS_LOG_INFO("Successfully saved spouse assets data");
        }

        // Save plugin settings (log level / global plugin config)
        if (!serialization->OpenRecord(MARAS::Serialization::kPluginSettingsData, MARAS::Serialization::kDataVersion)) {
            MARAS_LOG_ERROR("Failed to open record for saving plugin settings");
            return;
        }
        if (!MARAS::LoggingService::GetSingleton().Save(serialization)) {
            MARAS_LOG_ERROR("Failed to save plugin settings (log level)");
        } else {
            MARAS_LOG_INFO("Successfully saved plugin settings");
        }
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

        std::uint32_t type, version, length;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (type == MARAS::Serialization::kNPCRelationshipData) {
                // Support version 1 (original), 2 (added homeMarker), 3 (removed deceased tracking)
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Unsupported data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!manager.Load(serialization, version)) {
                    MARAS_LOG_ERROR("Failed to load NPC relationship data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded NPC relationship data (version {})", version);
                }
            } else if (type == MARAS::Serialization::kSpouseHierarchyData) {
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Invalid spouse hierarchy data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!MARAS::SpouseHierarchyManager::GetSingleton().Load(serialization)) {
                    MARAS_LOG_ERROR("Failed to load spouse hierarchy data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded spouse hierarchy data");
                }
            } else if (type == MARAS::Serialization::kAffectionData) {
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Invalid affection data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!MARAS::AffectionService::GetSingleton().Load(serialization)) {
                    MARAS_LOG_ERROR("Failed to load affection data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded affection data");
                }
            } else if (type == MARAS::Serialization::kPlayerHouseData) {
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Invalid player house data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!MARAS::PlayerHouseService::GetSingleton().Load(serialization)) {
                    MARAS_LOG_ERROR("Failed to load player house data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded player house data");
                }
            } else if (type == MARAS::Serialization::kSpouseAssetsData) {
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Invalid spouse assets data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!MARAS::SpouseAssetsService::GetSingleton().Load(serialization)) {
                    MARAS_LOG_ERROR("Failed to load spouse assets data");
                } else {
                    MARAS_LOG_INFO("Successfully loaded spouse assets data");
                }
            } else if (type == MARAS::Serialization::kPluginSettingsData) {
                if (version < 1 || version > MARAS::Serialization::kDataVersion) {
                    MARAS_LOG_ERROR("Invalid plugin settings data version {} (expected 1-{})", version,
                                    MARAS::Serialization::kDataVersion);
                    continue;
                }
                if (!MARAS::LoggingService::GetSingleton().Load(serialization)) {
                    MARAS_LOG_ERROR("Failed to load plugin settings (log level)");
                } else {
                    MARAS_LOG_INFO("Successfully loaded plugin settings");
                }
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();
        manager.Revert();
        MARAS::SpouseHierarchyManager::GetSingleton().Revert();
        MARAS::AffectionService::GetSingleton().Revert();
        MARAS::PlayerHouseService::GetSingleton().Revert();
        MARAS::SpouseAssetsService::GetSingleton().Revert();
        MARAS::LoggingService::GetSingleton().Revert();

        // Load marriage difficulty configuration for new game
        MARAS::MarriageDifficulty::LoadConfig();

        MARAS_LOG_INFO("Reverted NPC relationship data");
    }
}

SKSEPluginLoad(const LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    MARAS_LOG_INFO("MARAS plugin loading...");

    // Register serialization callbacks
    if (const auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(MARAS::Serialization::kMarasPluginID);
        serialization->SetSaveCallback(SaveCallback);
        serialization->SetLoadCallback(LoadCallback);
        serialization->SetRevertCallback(RevertCallback);
        MARAS_LOG_INFO("Registered MARAS serialization callbacks");
    } else {
        MARAS_LOG_ERROR("Serialization interface unavailable.");
        return false;
    }

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPreLoadGame:
                        MARAS_LOG_INFO("PreLoadGame...");
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        MARAS_LOG_INFO("New game/Load...");
                        // Load runtime-only bonuses.json each time a save is loaded or a new game starts
                        MARAS::BonusesService::GetSingleton().LoadFromFile();

                        // Load marriage difficulty configuration on game load
                        MARAS::MarriageDifficulty::LoadConfig();

                        // Initialize/reset polling service state to prevent false events from previous save
                        MARAS::PollingService::GetSingleton().Initialize();

                        // Log statistics after save data has been loaded
                        MARAS::NPCRelationshipManager::GetSingleton().LogStatistics();
                        break;

                    case SKSE::MessagingInterface::kDataLoaded: {
                        MARAS_LOG_INFO("Data loaded successfully.");

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

                        // Load quest event configurations
                        std::filesystem::path questEventFolder = "Data/SKSE/Plugins/MARAS/questEvents";
                        if (MARAS::QuestEventManager::GetSingleton().LoadConfigFromFolder(questEventFolder.string())) {
                            MARAS_LOG_INFO("Loaded {} quest event configurations",
                                           MARAS::QuestEventManager::GetSingleton().GetConfigCount());
                        } else {
                            MARAS_LOG_INFO("No quest event configurations found (folder may not exist: {})",
                                           questEventFolder.string());
                        }

                        // Register quest event sinks
                        auto* scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
                        if (scriptEventSourceHolder) {
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStartStopEvent>(
                                MARAS::QuestStartStopEventSink::GetSingleton());
                            scriptEventSourceHolder->AddEventSink<RE::TESQuestStageEvent>(
                                MARAS::QuestStageEventSink::GetSingleton());
                            MARAS_LOG_INFO("Registered quest event sinks");
                        } else {
                            MARAS_LOG_ERROR("Failed to get ScriptEventSourceHolder for quest event registration");
                        }

                        // Register update event sink for polling service
                        auto ui = RE::UI::GetSingleton();
                        if (ui) {
                            ui->AddEventSink(UpdateEventSink::GetSingleton());
                            MARAS_LOG_INFO("Registered polling service update event sink");
                        } else {
                            MARAS_LOG_ERROR("Failed to get UI singleton for event sink registration");
                        }

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
