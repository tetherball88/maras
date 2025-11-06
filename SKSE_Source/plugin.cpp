#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#include "PCH.h"
#include "core/NPCRelationshipManager.h"
#include "core/Serialization.h"
#include "papyrus/PapyrusInterface.h"

using namespace SKSE;

namespace {
    void SetupLogging() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("MARAS: log directory unavailable");
            }
            return;
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
            return;
        }

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("MARAS", std::move(sink));
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%H:%M:%S] [%l] %v");

        spdlog::set_default_logger(std::move(logger));
        spdlog::info("Logging to {}", logPath.string());
    }

    void PrintToConsole(std::string_view message) {
        SKSE::log::info("{}", message);
        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print("%s", message.data());
        }
    }

    // SKSE Serialization callbacks
    void SaveCallback(SKSE::SerializationInterface* serialization) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

        if (!serialization->OpenRecord(MARAS::Serialization::kNPCRelationshipData,
                                       MARAS::Serialization::kDataVersion)) {
            SKSE::log::error("Failed to open record for saving");
            return;
        }

        if (!manager.Save(serialization)) {
            SKSE::log::error("Failed to save NPC relationship data");
        } else {
            SKSE::log::info("Successfully saved NPC relationship data");
        }
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

        std::uint32_t type, version, length;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (type == MARAS::Serialization::kNPCRelationshipData) {
                if (version != MARAS::Serialization::kDataVersion) {
                    SKSE::log::error("Invalid data version {} (expected {})", version,
                                     MARAS::Serialization::kDataVersion);
                    continue;
                }

                if (!manager.Load(serialization)) {
                    SKSE::log::error("Failed to load NPC relationship data");
                } else {
                    SKSE::log::info("Successfully loaded NPC relationship data");
                }
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();
        manager.Revert();
        SKSE::log::info("Reverted NPC relationship data");
    }
}

SKSEPluginLoad(const LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    SKSE::log::info("MARAS plugin loading...");

    // Register serialization callbacks
    if (const auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(MARAS::Serialization::kMarasPluginID);
        serialization->SetSaveCallback(SaveCallback);
        serialization->SetLoadCallback(LoadCallback);
        serialization->SetRevertCallback(RevertCallback);
        SKSE::log::info("Registered MARAS serialization callbacks");
    } else {
        SKSE::log::critical("Serialization interface unavailable.");
        return false;
    }

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPreLoadGame:
                        SKSE::log::info("PreLoadGame...");
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        SKSE::log::info("New game/Load...");
                        break;

                    case SKSE::MessagingInterface::kDataLoaded: {
                        SKSE::log::info("Data loaded successfully.");

                        // Initialize the NPC relationship manager
                        auto& manager = MARAS::NPCRelationshipManager::GetSingleton();

                        // Load override data
                        std::filesystem::path overrideFolder = "Data/SKSE/Plugins/MARAS/spousesTypes";
                        if (manager.LoadOverridesFromFolder(overrideFolder.string())) {
                            auto stats = manager.GetLastOverrideLoadStats();
                            SKSE::log::info("Loaded {} NPC type overrides from {} files", manager.GetOverrideCount(),
                                            stats.successfulFiles);
                        } else {
                            SKSE::log::warn("Failed to load NPC type overrides from {}", overrideFolder.string());
                        }

                        manager.LogStatistics();

                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("MARAS: Ready");
                        }
                        break;
                    }

                    default:
                        break;
                }
            })) {
            SKSE::log::critical("Failed to register messaging listener.");
            return false;
        }
    } else {
        SKSE::log::critical("Messaging interface unavailable.");
        return false;
    }

    // Register Papyrus functions
    if (const auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(MARAS::PapyrusInterface::RegisterPapyrusFunctions)) {
            SKSE::log::critical("Failed to register Papyrus functions.");
            return false;
        }
    } else {
        SKSE::log::critical("Papyrus interface unavailable.");
        return false;
    }

    return true;
}
