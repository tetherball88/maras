#pragma once

#include "PCH.h"
#include "core/RelationshipTypes.h"
#include "utils/Common.h"

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
        std::vector<RE::FormID> relatedActorFormIDs;
    };

    struct MemoryParticipant {
        RE::FormID formID = 0;
        std::string name;
        std::string role;
        std::string perspectiveNote;
        std::string knowledgeRestriction;  // Explicit instruction on what this NPC DOES NOT know (secrets to never bleed)
        bool generateMemory = false;
        std::unordered_set<RE::FormID> hiddenActorFormIDs;  // Secret actors not to link in AddActorMemory for this participant
    };

    struct MemoryEvent {
        std::string eventCategory;
        std::string eventType;
        std::string eventHeadline;
        std::string location;
        std::vector<MemoryParticipant> participants;
        bool isMatchmaking = false;
    };

    struct MemoryEpisode {
        uint64_t episodeID = 0;
        std::chrono::steady_clock::time_point firstEventTime{};
        std::chrono::steady_clock::time_point flushTime{};
        std::vector<MemoryEvent> events;
        std::unordered_set<RE::FormID> participantFormIDs;
        std::unordered_set<RE::FormID> targetFormIDs;
        bool isMatchmaking = false;
    };

    class SkyrimNetMemoryService {
    public:
        static constexpr std::chrono::seconds kEpisodeGraceWindow{ 90 };
        static constexpr std::chrono::seconds kProgrammaticGraceWindow{ 5 };

        static SkyrimNetMemoryService& GetSingleton() {
            static SkyrimNetMemoryService singleton;
            return singleton;
        }

        // RAII guard to temporarily suppress automatic event recording on the current thread
        class SuppressScope {
        public:
            SuppressScope() { ++s_suppressDepth_; }
            ~SuppressScope() { --s_suppressDepth_; }
            SuppressScope(const SuppressScope&) = delete;
            SuppressScope& operator=(const SuppressScope&) = delete;
        };

        // RAII guard indicating that background matchmaking is applying approved matches
        class MatchmakingApplyScope {
        public:
            MatchmakingApplyScope() { ++s_matchmakingApplyDepth_; }
            ~MatchmakingApplyScope() { --s_matchmakingApplyDepth_; }
            MatchmakingApplyScope(const MatchmakingApplyScope&) = delete;
            MatchmakingApplyScope& operator=(const MatchmakingApplyScope&) = delete;
        };

        static bool IsSuppressed() { return s_suppressDepth_ > 0; }
        static bool IsMatchmakingApplyActive() { return s_matchmakingApplyDepth_ > 0; }

        // Core episode buffer operations
        void RecordEvent(MemoryEvent event);
        void Tick();
        void Revert();

        // Domain event recorders
        void RecordRelationshipStatusChange(RE::FormID initiatorID, RE::FormID partnerID,
                                            RelationshipStatus oldStatus, RelationshipStatus newStatus);
        void RecordOpenRelationshipToggle(RE::FormID actorA, RE::FormID actorB, bool isOpen);
        void RecordInfidelityEvent(RE::FormID cheaterID, RE::FormID victimID, RE::FormID affairPartnerID,
                                   const std::string& eventSubType, bool isWalkIn = false);
        void RecordPregnancyDiscovered(RE::FormID motherID, RE::FormID partnerID = 0, RE::FormID fallbackBioID = 0, const std::string& fallbackBioName = "", int32_t childCount = 1);
        void RecordPaternityReveal(RE::FormID childID, const std::string& outcome,
                                   const std::string& partnerReaction = "none", bool wasPregnancySecretForPartner = false);
        void RecordChildBirth(RE::FormID motherID, const std::vector<RE::FormID>& childIDs);
        void RecordChildCustodyEvent(const std::string& eventType, RE::FormID childID,
                                     RE::FormID guardianAID, RE::FormID guardianBID,
                                     RE::FormID otherID, const std::string& reason);

        // Manual UI preview & direct ingestion helpers (shared with PrismaUIDataBridge)
        void PreviewRelationshipMemories(RE::FormID actorA, RE::FormID actorB,
                                         const std::string& targetStatus, const std::string& action);
        std::vector<GeneratedMemoryEntry> ParseGeneratedMemories(
            const std::string& response,
            const std::vector<RE::FormID>& targetActors,
            const std::unordered_map<RE::FormID, std::vector<RE::FormID>>& relatedActorsByTarget = {});
        std::vector<GeneratedMemoryEntry> BuildProgrammaticMemories(
            const MemoryEpisode& episode,
            const std::vector<RE::FormID>& targetActors,
            const std::unordered_map<RE::FormID, std::vector<RE::FormID>>& relatedActorsByTarget = {}) const;
        void IngestMemories(const std::vector<GeneratedMemoryEntry>& memories,
                            const std::vector<RE::FormID>& fallbackRelatedActors = {});

        // Builds a formatted summary of an actor's relationships
        std::string BuildActorRelationshipsSummary(RE::FormID actorFormID) const;

    private:
        SkyrimNetMemoryService() = default;
        ~SkyrimNetMemoryService() = default;
        SkyrimNetMemoryService(const SkyrimNetMemoryService&) = delete;
        SkyrimNetMemoryService& operator=(const SkyrimNetMemoryService&) = delete;

        bool CanMergeWithEpisode(const MemoryEpisode& episode, const MemoryEvent& event) const;
        void MergeEpisodes(MemoryEpisode& dst, MemoryEpisode&& src);
        void DispatchEpisodeToLLM(MemoryEpisode episode);
        nlohmann::json BuildEpisodePromptContext(const MemoryEpisode& episode) const;

        static bool CanActorGenerateMemory(RE::FormID formID);
        static std::string ResolveBestLocation(const std::vector<RE::FormID>& actorIDs);
        static MemoryParticipant MakeParticipant(RE::FormID formID, const std::string& role,
                                                 const std::string& perspectiveNote,
                                                 bool allowGenerateMemory = true,
                                                 const std::string& knowledgeRestriction = "",
                                                 std::unordered_set<RE::FormID> hiddenActorFormIDs = {});

        mutable std::mutex mutex_;
        std::vector<MemoryEpisode> activeEpisodes_;
        uint64_t nextEpisodeID_ = 1;

        static inline thread_local int s_suppressDepth_ = 0;
        static inline thread_local int s_matchmakingApplyDepth_ = 0;
    };

}  // namespace MARAS

