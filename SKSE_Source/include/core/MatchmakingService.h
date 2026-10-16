#pragma once

#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"

namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    struct GeneratedMemoryEntry {
        std::string actorName;
        RE::FormID actorFormID = 0;
        std::string content;
        std::string location;
        std::string emotion;
        float importanceScore = 0.7f;
        std::string type = "RELATIONSHIP";
        std::vector<std::string> tags;
    };

    struct MatchmakingResult {
        std::string actorAName;
        std::string actorBName;
        RE::FormID actorAFormID = 0;
        RE::FormID actorBFormID = 0;
        std::string action;             // "new", "advance", "breakup", "open", "close"
        std::string targetStatus;       // "dating", "lover", "engaged", "married", "exdating", etc.
        std::string memory;             // Optional in-world narrative
        std::vector<GeneratedMemoryEntry> dualMemories; // Optional dual POV memories
        bool isCheating = false;        // True if suggested relationship involves infidelity / closed partner
        bool applied = false;
        bool skipped = false;
        std::string skipReason;
    };

    struct BatchTaskBase {
        MatchmakingResult result;
        bool completed = false;
        bool failed = false;
        std::string failureReason;
    };

    struct VerificationTask : BatchTaskBase {
        bool suitable = false;
        std::string verificationReason;
    };

    struct MemoryTask : BatchTaskBase {
        std::vector<GeneratedMemoryEntry> generatedMemories;
    };

    template <typename TaskType>
    struct BatchBase {
        std::vector<TaskType> tasks;
        size_t pendingCount = 0;
        bool isScheduledScan = true;
    };

    using VerificationBatch = BatchBase<VerificationTask>;
    using MemoryBatch = BatchBase<MemoryTask>;

    struct DiceRollActionSlot {
        std::string action;       // "new", "advance", "breakup", "open", "close", "open_or_close"
        std::string targetStatus; // "dating", "engaged", "lover", "married", etc.
        bool isCheating = false;
        std::string category;     // "New", "Advance", "Breakup", "OpenClose"
    };

    class MatchmakingService {
    public:
        static MatchmakingService& GetSingleton() {
            static MatchmakingService singleton;
            return singleton;
        }

        // Scheduled matchmaking hook from PollingService
        void TryScheduledMatchmaking(float currentGameDay);
        void RunLLMScheduledMatchmaking(float currentGameDay);
        void RunDiceRollMatchmaking(float currentGameDay);

        // Manual matchmaking triggered from Prisma UI for a specific target NPC
        void RunManualMatchmaking(RE::FormID targetNpcFormID);

        // Manual global matchmaking scan across all NPCs (without specific target)
        void RunGlobalMatchmaking(bool autoApply = false);
        void ProcessGlobalManualResults(const std::string& response);

        // Memory generation & ingestion
        void RunMemoryGeneration(RE::FormID actorA, RE::FormID actorB, const std::string& targetStatus, const std::string& action);
        void ProcessGeneratedMemories(const std::string& response, RE::FormID actorA, RE::FormID actorB);
        std::vector<GeneratedMemoryEntry> ParseGeneratedMemories(const std::string& response, RE::FormID actorA, RE::FormID actorB);
        void IngestMemories(const std::vector<GeneratedMemoryEntry>& memories, RE::FormID actorA, RE::FormID actorB);

        // Batched Verification & Memory Generation Pipelines
        void StartVerificationBatch(std::vector<MatchmakingResult> candidates, bool isScheduledScan = true);
        void RetryVerificationTasks(std::shared_ptr<VerificationBatch> batch, const std::vector<size_t>& indicesToRetry);
        void FinishVerificationBatch(std::shared_ptr<VerificationBatch> batch);

        void StartMemoryBatch(std::vector<MatchmakingResult> verifiedMatches, bool isScheduledScan = true);
        void RetryMemoryTasks(std::shared_ptr<MemoryBatch> batch, const std::vector<size_t>& indicesToRetry);
        void FinishMemoryBatch(std::shared_ptr<MemoryBatch> batch);

        void ShowInGameFailedLLMModal(const std::string& title,
                                      const std::vector<std::string>& failedDescriptions,
                                      std::function<void()> onRetry,
                                      std::function<void()> onCancel);

        // Context builders
        nlohmann::json BuildScheduledContext(float currentGameDay);
        nlohmann::json BuildManualContext(RE::FormID targetNpcFormID, float currentGameDay);
        nlohmann::json BuildMemoriesContext(RE::FormID actorA, RE::FormID actorB, const std::string& targetStatus, const std::string& action);
        nlohmann::json BuildVerifyContext(const MatchmakingResult& result);

        // Response parser & validation
        std::vector<MatchmakingResult> ParseLLMResponse(const std::string& response);
        void ResolveFormIDs(MatchmakingResult& result);
        bool ValidateCandidate(MatchmakingResult& result);
        bool ApplyMatch(MatchmakingResult& result);

        // NPC Relationship Change Cooldown
        void RecordNpcCooldown(RE::FormID actorID, float currentDay);
        bool IsNpcOnCooldown(RE::FormID actorID, float currentDay) const;
        float GetNpcCooldownRemaining(RE::FormID actorID, float currentDay) const;
        void ClearNpcCooldowns() { npcLastMatchmakingChangeDay_.clear(); }
        const std::unordered_map<RE::FormID, float>& GetNpcCooldowns() const { return npcLastMatchmakingChangeDay_; }

        // Serialization
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization, uint32_t version);
        void Revert();

        // Data dump
        [[nodiscard]] nlohmann::json DumpJson() const;

        float GetLastMatchmakingDay() const { return lastMatchmakingDay_; }
        void SetLastMatchmakingDay(float day) { lastMatchmakingDay_ = day; }
        float GetCurrentGameDay() const;

    private:
        MatchmakingService() = default;
        ~MatchmakingService() = default;
        MatchmakingService(const MatchmakingService&) = delete;
        MatchmakingService& operator=(const MatchmakingService&) = delete;

        void ProcessScheduledResults(const std::string& response, bool isScheduledScan = true);
        void ProcessManualResults(const std::string& response, RE::FormID targetNpcFormID);

        void RequestMemoriesAsync(RE::FormID actorA, RE::FormID actorB,
                                  const std::string& targetStatus, const std::string& action,
                                  std::function<void(const std::string&)> onSuccess,
                                  std::function<void()> onFailure = nullptr);

        std::string BuildExistingPairsString();
        std::string BuildTargetRelationshipsString(RE::FormID targetNpcFormID);
        std::string BuildActorDetailedRelationshipsString(RE::FormID actorFormID);

        // Dice Roll matchmaking helpers
        std::vector<DiceRollActionSlot> RollActionSlots(int maxSlots);
        std::optional<MatchmakingResult> FindEligiblePairForSlot(
            const DiceRollActionSlot& slot,
            const std::vector<NPCEntry>& eligibleNPCs,
            std::unordered_set<RE::FormID>& usedActors,
            float currentGameDay);

        float lastMatchmakingDay_ = -1.0f;
        std::unordered_map<RE::FormID, float> npcLastMatchmakingChangeDay_;
    };

}  // namespace MARAS
