#include "core/SpouseHierarchyManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "core/Serialization.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {
    // Helper to get spouse hierarchy faction (centralized cached lookup)
    RE::TESFaction* GetHierarchyFaction() { return FormCache::GetSingleton().GetHierarchyFaction(); }

    // Helper to set or remove a faction rank for an actor by FormID (centralized)
    void SetActorHierarchyFactionRank(RE::FormID actorFormID, std::optional<std::int8_t> rank) {
        if (actorFormID == 0) return;
        auto faction = GetHierarchyFaction();
        if (!faction) return;
        auto actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
        if (!actor) return;

        if (rank.has_value()) {
            actor->AddToFaction(faction, *rank);
        } else {
            Utils::RemoveFromFaction(actor, faction);
        }
    }

    // Convenience wrappers to reduce repeated casting noise
    inline std::optional<std::int8_t> MakeRankOpt(int rank) {
        if (rank < INT8_MIN || rank > INT8_MAX) {
            return std::nullopt;
        }
        return static_cast<std::int8_t>(rank);
    }

    inline void ApplyFactionRank(RE::FormID id, int rank) { SetActorHierarchyFactionRank(id, MakeRankOpt(rank)); }
    inline void ClearFactionRank(RE::FormID id) { SetActorHierarchyFactionRank(id, std::nullopt); }

    // Helper to send hierarchy changed event to Papyrus
    void SendHierarchyChangedEvent(RE::FormID npcFormID, const std::string& action, float newRank) {
        if (npcFormID == 0) return;
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        if (!npcForm) {
            MARAS_LOG_WARN("Could not find form for FormID {:08X} to send hierarchy event", npcFormID);
            return;
        }

        auto eventSource = SKSE::GetModCallbackEventSource();
        if (eventSource) {
            SKSE::ModCallbackEvent modEvent("maras_hierarchy_changed", action, newRank, npcForm);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_hierarchy_changed event for NPC {:08X}: action={} rank={}", npcFormID, action,
                           newRank);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send hierarchy event");
        }
    }
}

namespace MARAS {

    SpouseHierarchyManager& SpouseHierarchyManager::GetSingleton() {
        static SpouseHierarchyManager instance;
        return instance;
    }

    SpouseHierarchyManager::SpouseHierarchyManager() { ranks_.fill(0); }

    int SpouseHierarchyManager::GetRank(RE::FormID npcFormID) const {
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] == npcFormID) {
                return static_cast<int>(i);
            }
        }
        return 4;  // 4+ default
    }

    bool SpouseHierarchyManager::SetRank(RE::FormID npcFormID, int rank) {
        // Normalize ranks: anything >=3 or negative -> remove (valid rank indices: 0,1,2)
        if (rank < 0 || rank >= static_cast<int>(ranks_.size())) {
            // remove if present
            bool changed = false;
            for (size_t i = 0; i < ranks_.size(); ++i) {
                if (ranks_[i] == npcFormID) {
                    ranks_[i] = 0;
                    changed = true;
                }
            }
            if (changed) FillGaps();
            return true;
        }

        // If requesting a top-3 rank
        if (rank >= 0 && rank < static_cast<int>(ranks_.size())) {
            // Find current rank
            int cur = GetRank(npcFormID);

            // If already in desired rank, nothing to do
            if (cur == rank) return true;

            RE::FormID occupant = ranks_[rank];

            if (occupant == 0) {
                // Empty spot: place the NPC there
                if (cur >= 0 && cur <= 2) {
                    ranks_[cur] = 0;
                }
                ranks_[rank] = npcFormID;
                // set faction rank on actor
                ApplyFactionRank(npcFormID, rank);
                // Notify Papyrus of promotion with rank difference (old - new)
                {
                    float diff = static_cast<float>(cur - rank);
                    SendHierarchyChangedEvent(npcFormID, std::string("promote"), diff);
                }
                FillGaps();
                return true;
            }

            // Occupied spot
            if (cur >= 0 && cur < static_cast<int>(ranks_.size())) {
                // NPC already had a top-3 spot: swap
                ranks_[cur] = occupant;
                ranks_[rank] = npcFormID;
                // update faction ranks for both
                ApplyFactionRank(occupant, cur);
                ApplyFactionRank(npcFormID, rank);
                // Notify Papyrus about both changes (send rank difference old-new)
                {
                    // npc: old=cur -> new=rank
                    const char* npcAction = (rank < cur) ? "promote" : "demote";
                    float npcDiff = static_cast<float>(cur - rank);
                    SendHierarchyChangedEvent(npcFormID, std::string(npcAction), npcDiff);
                    // occupant: old=rank -> new=cur
                    const char* occAction = (cur < rank) ? "promote" : "demote";
                    float occDiff = static_cast<float>(rank - cur);
                    SendHierarchyChangedEvent(occupant, std::string(occAction), occDiff);
                }
                return true;
            } else {
                // NPC was 4+ and tries to take an occupied spot -> evict current occupant to 4+
                ranks_[rank] = npcFormID;
                // the previous occupant is removed from top3
                for (size_t i = 0; i < ranks_.size(); ++i) {
                    if (ranks_[i] == occupant && static_cast<int>(i) != rank) {
                        ranks_[i] = 0;
                    }
                }
                // update faction ranks: set new npc, remove old occupant
                ApplyFactionRank(npcFormID, rank);
                ClearFactionRank(occupant);
                // Notify Papyrus: send rank differences (old - new)
                {
                    float npcDiff = static_cast<float>(cur - rank);
                    SendHierarchyChangedEvent(npcFormID, std::string("promote"), npcDiff);
                    // occupant demoted out of hierarchy (represented as rank size -> outside range)
                    float occDiff = static_cast<float>(rank - static_cast<int>(ranks_.size()));
                    SendHierarchyChangedEvent(occupant, std::string("demote"), occDiff);
                }
                // ensure no other gaps
                FillGaps();
                return true;
            }
        }

        return false;
    }

    void SpouseHierarchyManager::OnSpouseAdded(RE::FormID npcFormID) {
        // If already in top3, nothing to do
        if (GetRank(npcFormID) <= 2) return;

        // Find first empty slot
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] == 0) {
                ranks_[i] = npcFormID;
                // set faction rank
                ApplyFactionRank(npcFormID, static_cast<int>(i));
                MARAS_LOG_INFO("Assigned spouse {:08X} to hierarchy slot {}", npcFormID, i);
                return;
            }
        }
        // No empty slot: do nothing (4+)
    }

    void SpouseHierarchyManager::OnSpouseRemoved(RE::FormID npcFormID) {
        bool wasPresent = false;
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] == npcFormID) {
                // remove faction rank
                ClearFactionRank(npcFormID);
                ranks_[i] = 0;
                wasPresent = true;
            }
        }
        if (wasPresent) {
            MARAS_LOG_INFO("Removed spouse {:08X} from hierarchy and filling gaps", npcFormID);
            FillGaps();
        }
    }

    void SpouseHierarchyManager::FillGaps() {
        // Rebuild the top slots by keeping existing top-ranked spouses in order
        // and then appending other married NPCs. This ensures that when a
        // top slot is vacated (e.g. divorce at slot 0) the remaining top-ranked
        // spouses shift up (1->0, 2->1) and the 3rd slot is filled from the
        // remaining married NPCs, if any.
        auto& rel = NPCRelationshipManager::GetSingleton();
        auto married = rel.GetAllMarried();

        // Build candidates: existing top-ranked spouses first (preserve their relative order),
        // then other married NPCs not already present.
        std::vector<RE::FormID> candidates;
        candidates.reserve(ranks_.size() + married.size());
        std::unordered_set<RE::FormID> present;

        for (auto id : ranks_) {
            if (id != 0) {
                candidates.push_back(id);
                present.insert(id);
            }
        }

        for (auto id : married) {
            if (present.find(id) == present.end()) {
                candidates.push_back(id);
                present.insert(id);
            }
        }

        // Compose new ranks array from candidates, preserving order and shifting up as needed
        std::array<RE::FormID, 3> newRanks;
        newRanks.fill(0);
        for (size_t i = 0; i < ranks_.size() && i < candidates.size(); ++i) {
            newRanks[i] = candidates[i];
        }

        // Clear faction rank for any old occupant that no longer appears in newRanks
        for (auto old : ranks_) {
            if (old == 0) continue;
            bool stillPresent = false;
            for (auto nr : newRanks) {
                if (nr == old) {
                    stillPresent = true;
                    break;
                }
            }
            if (!stillPresent) {
                ClearFactionRank(old);
            }
        }

        // Apply faction ranks for new positions and log assignments
        for (size_t i = 0; i < newRanks.size(); ++i) {
            auto id = newRanks[i];
            if (id != 0) {
                ApplyFactionRank(id, static_cast<int>(i));
                MARAS_LOG_DEBUG("Filled spouse hierarchy slot {} with {:08X}", i, id);
            }
        }

        // Send hierarchy changed events for any spouse whose rank changed
        for (auto id : present) {
            if (id == 0) continue;
            int oldRank = 4;  // default 4+ for not present
            for (size_t i = 0; i < ranks_.size(); ++i) {
                if (ranks_[i] == id) {
                    oldRank = static_cast<int>(i);
                    break;
                }
            }

            int newRank = 4;
            for (size_t i = 0; i < newRanks.size(); ++i) {
                if (newRanks[i] == id) {
                    newRank = static_cast<int>(i);
                    break;
                }
            }

            if (oldRank != newRank) {
                const char* action = (newRank < oldRank) ? "promote" : "demote";
                float diff = static_cast<float>(oldRank - newRank);
                SendHierarchyChangedEvent(id, std::string(action), diff);
            }
        }

        // Commit new ranks
        ranks_ = newRanks;
    }

    bool SpouseHierarchyManager::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Write number of slots (always 3)
        std::uint32_t slotCount = static_cast<std::uint32_t>(ranks_.size());
        if (!serialization->WriteRecordData(slotCount)) return false;

        for (auto id : ranks_) {
            if (!serialization->WriteRecordData(id)) return false;
        }

        MARAS_LOG_INFO("Saved spouse hierarchy ({} slots)", ranks_.size());
        return true;
    }

    bool SpouseHierarchyManager::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        std::uint32_t slotCount = 0;
        if (!serialization->ReadRecordData(slotCount)) return false;
        ranks_.fill(0);

        for (std::uint32_t i = 0; i < slotCount && i < ranks_.size(); ++i) {
            RE::FormID savedId = 0;
            if (!serialization->ReadRecordData(savedId)) return false;
            // Resolve saved formIDs to current load-order IDs
            RE::FormID resolved = 0;
            if (savedId != 0 && serialization->ResolveFormID(savedId, resolved)) {
                // Skip dead or invalid actors
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(resolved);
                if (!actor || actor->IsDead()) {
                    MARAS_LOG_INFO("SpouseHierarchyManager::Load - skipping dead/invalid actor {:08X}", resolved);
                    ranks_[i] = 0;
                } else {
                    ranks_[i] = resolved;
                }
            } else {
                ranks_[i] = 0;  // unresolved or empty
            }
        }

        // Fill any gaps left by skipped dead actors
        FillGaps();

        // After loading ranks, apply faction ranks to actors
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] != 0) {
                ApplyFactionRank(ranks_[i], static_cast<int>(i));
            }
        }

        MARAS_LOG_INFO("Loaded spouse hierarchy");
        return true;
    }

    void SpouseHierarchyManager::Revert() {
        // remove faction rank from any assigned spouses
        for (auto id : ranks_) {
            if (id != 0) {
                ClearFactionRank(id);
            }
        }

        ranks_.fill(0);
    }

}  // namespace MARAS
