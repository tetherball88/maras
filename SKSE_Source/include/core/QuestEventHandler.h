#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/Common.h"

namespace MARAS {

    // Represents a single command to execute
    struct QuestCommand {
        std::string commandType;   // e.g., "promoteToStatus", "setAffection", "changeAffection"
        std::string npcSpecifier;  // Either alias name (e.g., "LoveInterest") or form key (e.g., "__formData|...")
        std::string argument;      // The argument for the command (e.g., keyword, value)

        QuestCommand() = default;
        QuestCommand(std::string type, std::string npc, std::string arg)
            : commandType(std::move(type)), npcSpecifier(std::move(npc)), argument(std::move(arg)) {}
    };

    // Configuration for a single quest's event handlers
    struct QuestEventConfig {
        RE::FormID questFormID;

        std::vector<QuestCommand> onStartCommands;
        std::vector<QuestCommand> onStopCommands;

        // Map stage ID to commands
        std::unordered_map<uint16_t, std::vector<QuestCommand>> onStageChangeCommands;

        QuestEventConfig() : questFormID(0) {}
    };

    // Forward declaration for config loader
    class QuestEventConfigLoader;

    // Manages quest event configurations and command execution
    class QuestEventManager {
    public:
        static QuestEventManager& GetSingleton();

        // Load configuration from JSON files in a folder
        bool LoadConfigFromFolder(const std::string& folderPath);

        // Clear all configurations
        void ClearConfig();

        // Get configuration for a specific quest (for debugging)
        const QuestEventConfig* GetConfigForQuest(RE::FormID questFormID) const;

        // Get statistics
        size_t GetConfigCount() const { return questConfigs_.size(); }

        // Execute commands for quest events (called by event sink)
        void ExecuteQuestStartCommands(RE::TESQuest* quest);
        void ExecuteQuestStopCommands(RE::TESQuest* quest);
        void ExecuteQuestStageCommands(RE::TESQuest* quest, uint16_t stage);

    private:
        friend class QuestEventConfigLoader;  // Allow loader to access questConfigs_
        QuestEventManager() = default;

        // Execute a list of commands
        void ExecuteCommands(const std::vector<QuestCommand>& commands, RE::TESQuest* quest,
                             const std::string& context);

        // Execute a single command
        bool ExecuteCommand(const QuestCommand& command, RE::TESQuest* quest, const std::string& context);

        // Command executors - Relationship & Affection
        bool ExecutePromoteToStatus(const std::string& npcSpecifier, const std::string& statusKeyword,
                                    RE::TESQuest* quest, const std::string& context);
        bool ExecuteSetAffection(const std::string& npcSpecifier, const std::string& value, RE::TESQuest* quest,
                                 const std::string& context);
        bool ExecuteChangeAffection(const std::string& npcSpecifier, const std::string& delta, RE::TESQuest* quest,
                                    const std::string& context);
        bool ExecuteAddDailyAffection(const std::string& npcSpecifier, const std::string& amount, RE::TESQuest* quest,
                                      const std::string& context);

        // Command executors - NPC Attributes
        bool ExecuteSetSocialClass(const std::string& npcSpecifier, const std::string& className, RE::TESQuest* quest,
                                   const std::string& context);
        bool ExecuteSetSkillType(const std::string& npcSpecifier, const std::string& skillName, RE::TESQuest* quest,
                                 const std::string& context);
        bool ExecuteSetTemperament(const std::string& npcSpecifier, const std::string& temperamentName,
                                   RE::TESQuest* quest, const std::string& context);

        // Helper to resolve NPC from specifier (alias name or form key)
        RE::Actor* ResolveNPC(const std::string& npcSpecifier, RE::TESQuest* quest) const;

        // Configuration storage: quest FormID -> config
        std::unordered_map<RE::FormID, QuestEventConfig> questConfigs_;
    };

    // Event sink for quest start/stop events
    class QuestStartStopEventSink : public RE::BSTEventSink<RE::TESQuestStartStopEvent> {
    public:
        static QuestStartStopEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStartStopEvent* event,
                                              RE::BSTEventSource<RE::TESQuestStartStopEvent>* source) override;

    private:
        QuestStartStopEventSink() = default;
        QuestStartStopEventSink(const QuestStartStopEventSink&) = delete;
        QuestStartStopEventSink(QuestStartStopEventSink&&) = delete;
        QuestStartStopEventSink& operator=(const QuestStartStopEventSink&) = delete;
        QuestStartStopEventSink& operator=(QuestStartStopEventSink&&) = delete;
    };

    // Event sink for quest stage change events
    class QuestStageEventSink : public RE::BSTEventSink<RE::TESQuestStageEvent> {
    public:
        static QuestStageEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* event,
                                              RE::BSTEventSource<RE::TESQuestStageEvent>* source) override;

    private:
        QuestStageEventSink() = default;
        QuestStageEventSink(const QuestStageEventSink&) = delete;
        QuestStageEventSink(QuestStageEventSink&&) = delete;
        QuestStageEventSink& operator=(const QuestStageEventSink&) = delete;
        QuestStageEventSink& operator=(QuestStageEventSink&&) = delete;
    };

}  // namespace MARAS
