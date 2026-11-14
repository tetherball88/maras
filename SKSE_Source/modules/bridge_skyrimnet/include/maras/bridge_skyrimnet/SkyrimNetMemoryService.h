#pragma once

#include "maras/app/ModalTypes.h"
#include "maras/foundation/DomainEnums.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    struct MemoryParticipant {
        std::uint32_t formID = 0;
        std::string name;
        std::string role;
        std::string perspectiveNote;
        std::string knowledgeRestriction;
        bool generateMemory = false;
        std::unordered_set<std::uint32_t> hiddenActorFormIDs;
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
        std::uint64_t episodeID = 0;
        std::chrono::steady_clock::time_point firstEventTime{};
        std::chrono::steady_clock::time_point flushTime{};
        std::vector<MemoryEvent> events;
        std::unordered_set<std::uint32_t> participantFormIDs;
        std::unordered_set<std::uint32_t> targetFormIDs;
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

        class SuppressScope {
        public:
            SuppressScope() { PushSuppress(); }
            ~SuppressScope() { PopSuppress(); }
            SuppressScope(const SuppressScope&) = delete;
            SuppressScope& operator=(const SuppressScope&) = delete;
        };

        class MatchmakingApplyScope {
        public:
            MatchmakingApplyScope() { PushMatchmakingApply(); }
            ~MatchmakingApplyScope() { PopMatchmakingApply(); }
            MatchmakingApplyScope(const MatchmakingApplyScope&) = delete;
            MatchmakingApplyScope& operator=(const MatchmakingApplyScope&) = delete;
        };

        static void PushSuppress() noexcept { ++s_suppressDepth_; }
        static void PopSuppress() noexcept {
            if (s_suppressDepth_ > 0) {
                --s_suppressDepth_;
            }
        }
        static void PushMatchmakingApply() noexcept { ++s_matchmakingApplyDepth_; }
        static void PopMatchmakingApply() noexcept {
            if (s_matchmakingApplyDepth_ > 0) {
                --s_matchmakingApplyDepth_;
            }
        }

        [[nodiscard]] static bool IsSuppressed() noexcept { return s_suppressDepth_ > 0; }
        [[nodiscard]] static bool IsMatchmakingApplyActive() noexcept { return s_matchmakingApplyDepth_ > 0; }

        void RecordEvent(MemoryEvent event);
        void Tick();
        void Revert();

        void RecordRelationshipStatusChange(std::uint32_t initiatorID,
                                            std::uint32_t partnerID,
                                            MARAS::Foundation::RelationshipStatus oldStatus,
                                            MARAS::Foundation::RelationshipStatus newStatus);
        void RecordOpenRelationshipToggle(std::uint32_t actorA, std::uint32_t actorB, bool isOpen);
        void RecordInfidelityEvent(std::uint32_t cheaterID,
                                   std::uint32_t victimID,
                                   std::uint32_t affairPartnerID,
                                   const std::string& eventSubType,
                                   bool isWalkIn = false);
        void RecordPregnancyDiscovered(std::uint32_t motherID,
                                       std::uint32_t partnerID = 0,
                                       std::uint32_t fallbackBioID = 0,
                                       const std::string& fallbackBioName = "",
                                       std::int32_t childCount = 1);
        void RecordPaternityReveal(std::uint32_t childID,
                                   const std::string& outcome,
                                   const std::string& partnerReaction = "none",
                                   bool wasPregnancySecretForPartner = false);
        void RecordChildBirth(std::uint32_t motherID, const std::vector<std::uint32_t>& childIDs);
        void RecordChildCustodyEvent(const std::string& eventType,
                                     std::uint32_t childID,
                                     std::uint32_t guardianAID,
                                     std::uint32_t guardianBID,
                                     std::uint32_t otherID,
                                     const std::string& reason);

        void PreviewRelationshipMemories(std::uint32_t actorA,
                                         std::uint32_t actorB,
                                         const std::string& targetStatus,
                                         const std::string& action);
        std::vector<MARAS::App::GeneratedMemoryEntry> ParseGeneratedMemories(
            const std::string& response,
            const std::vector<std::uint32_t>& targetActors,
            const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& relatedActorsByTarget = {});
        std::vector<MARAS::App::GeneratedMemoryEntry> BuildProgrammaticMemories(
            const MemoryEpisode& episode,
            const std::vector<std::uint32_t>& targetActors,
            const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& relatedActorsByTarget = {}) const;
        void IngestMemories(const std::vector<MARAS::App::GeneratedMemoryEntry>& memories,
                            const std::vector<std::uint32_t>& fallbackRelatedActors = {});

        [[nodiscard]] std::string BuildActorRelationshipsSummary(std::uint32_t actorFormID) const;

    private:
        SkyrimNetMemoryService() = default;
        ~SkyrimNetMemoryService() = default;
        SkyrimNetMemoryService(const SkyrimNetMemoryService&) = delete;
        SkyrimNetMemoryService& operator=(const SkyrimNetMemoryService&) = delete;

        bool CanMergeWithEpisode(const MemoryEpisode& episode, const MemoryEvent& event) const;
        void MergeEpisodes(MemoryEpisode& dst, MemoryEpisode&& src);
        void DispatchEpisodeToLLM(MemoryEpisode episode);

        static bool CanActorGenerateMemory(std::uint32_t formID);
        static std::string ResolveBestLocation(const std::vector<std::uint32_t>& actorIDs);
        static MemoryParticipant MakeParticipant(std::uint32_t formID,
                                                 const std::string& role,
                                                 const std::string& perspectiveNote,
                                                 bool allowGenerateMemory = true,
                                                 const std::string& knowledgeRestriction = "",
                                                 std::unordered_set<std::uint32_t> hiddenActorFormIDs = {});

        mutable std::mutex mutex_;
        std::vector<MemoryEpisode> activeEpisodes_;
        std::uint64_t nextEpisodeID_ = 1;

        static inline thread_local int s_suppressDepth_ = 0;
        static inline thread_local int s_matchmakingApplyDepth_ = 0;
    };

}  // namespace MARAS::Bridge::SkyrimNet
