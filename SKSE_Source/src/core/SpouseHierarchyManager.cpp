#include "core/SpouseHierarchyManager.h"

#include <spdlog/spdlog.h>

#include <optional>
#include <string>

#include "core/NPCRelationshipManager.h"
#include "core/Serialization.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {
    // Helper to get spouse hierarchy faction
    RE::TESFaction* GetHierarchyFaction() { return Utils::LookupForm<RE::TESFaction>(0x111, "TT_MARAS.esp"); }

    // Helper to set or remove a faction rank for an actor by FormID
    void SetActorHierarchyFactionRank(RE::FormID actorFormID, std::optional<std::int8_t> rank) {
        if (actorFormID == 0) return;
        auto faction = GetHierarchyFaction();
        if (!faction) return;
        if (auto actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID)) {
            if (rank.has_value()) {
                // Add or update faction rank
                actor->AddToFaction(faction, rank.value());
            } else {
                // Try engine removal; the util will call the relocated engine function
                // when available and fall back to the -1 rank behavior otherwise.
                Utils::RemoveFromFaction(actor, faction);
            }
        }
    }

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
        // Normalize ranks: anything >=4 or negative -> remove
        if (rank < 0 || rank >= 4) {
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
        if (rank >= 0 && rank <= 2) {
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
                SetActorHierarchyFactionRank(npcFormID,
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(rank)));
                // Notify Papyrus of promotion with rank difference (old - new)
                {
                    float diff = static_cast<float>(cur - rank);
                    SendHierarchyChangedEvent(npcFormID, std::string("promote"), diff);
                }
                FillGaps();
                return true;
            }

            // Occupied spot
            if (cur >= 0 && cur <= 2) {
                // NPC already had a top-3 spot: swap
                ranks_[cur] = occupant;
                ranks_[rank] = npcFormID;
                // update faction ranks for both
                SetActorHierarchyFactionRank(occupant,
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(cur)));
                SetActorHierarchyFactionRank(npcFormID,
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(rank)));
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
                SetActorHierarchyFactionRank(npcFormID,
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(rank)));
                SetActorHierarchyFactionRank(occupant, std::nullopt);
                // Notify Papyrus: send rank differences (old - new)
                {
                    float npcDiff = static_cast<float>(cur - rank);
                    SendHierarchyChangedEvent(npcFormID, std::string("promote"), npcDiff);
                    float occDiff = static_cast<float>(rank - 4);
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
                SetActorHierarchyFactionRank(npcFormID,
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(i)));
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
                SetActorHierarchyFactionRank(npcFormID, std::nullopt);
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
        // Pull all married NPCs and ensure top slots are filled without duplicates
        auto& rel = NPCRelationshipManager::GetSingleton();
        auto married = rel.GetAllMarried();

        // Build a set of already assigned
        std::unordered_set<RE::FormID> assigned;
        for (auto id : ranks_) {
            if (id != 0) assigned.insert(id);
        }

        // For each slot, if empty, find first married not assigned
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] == 0) {
                for (auto id : married) {
                    if (assigned.contains(id)) continue;
                    // assign
                    ranks_[i] = id;
                    assigned.insert(id);
                    // set faction rank for this assigned spouse
                    SetActorHierarchyFactionRank(id,
                                                 static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(i)));
                    MARAS_LOG_DEBUG("Filled spouse hierarchy slot {} with {:08X}", i, id);
                    break;
                }
            }
        }
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
            RE::FormID id = 0;
            if (!serialization->ReadRecordData(id)) return false;
            ranks_[i] = id;
        }
        // After loading ranks, apply faction ranks to actors
        for (size_t i = 0; i < ranks_.size(); ++i) {
            if (ranks_[i] != 0) {
                SetActorHierarchyFactionRank(ranks_[i],
                                             static_cast<std::optional<std::int8_t>>(static_cast<std::int8_t>(i)));
            }
        }

        MARAS_LOG_INFO("Loaded spouse hierarchy");
        return true;
    }

    void SpouseHierarchyManager::Revert() {
        // remove faction rank from any assigned spouses
        for (auto id : ranks_) {
            if (id != 0) {
                SetActorHierarchyFactionRank(id, std::nullopt);
            }
        }

        ranks_.fill(0);
    }

}  // namespace MARAS
