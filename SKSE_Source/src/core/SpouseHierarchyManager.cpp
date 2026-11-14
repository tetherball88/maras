#include "core/SpouseHierarchyManager.h"

#include <algorithm>
#include <unordered_set>

#include "core/BuffReconciler.h"
#include "core/FactionReconciler.h"
#include "core/FormCache.h"
#include "core/GlobalVarReconciler.h"
#include "core/NPCIndex.h"
#include "core/SessionEpoch.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"

namespace MARAS {
    namespace {
        // Reconcile spouse hierarchy faction via FactionReconciler
        inline void ApplyFactionRank(RE::FormID id, int /*rank*/) {
            if (id != 0) {
                FactionReconciler::GetSingleton().ReconcileActor(id);
            }
        }

        inline void ClearFactionRank(RE::FormID id) {
            if (id != 0) {
                FactionReconciler::GetSingleton().ReconcileActor(id);
            }
        }

        // Helper to send hierarchy changed event to Papyrus
        void SendHierarchyChangedEvent(RE::FormID npcFormID, const std::string& action, float newRank) {
            if (npcFormID == 0) return;
            if (!RE::TESForm::LookupByID(npcFormID)) {
                MARAS_LOG_WARN("Could not find form for FormID {:08X} to send hierarchy event", npcFormID);
                return;
            }

            EmitModCallbackEvent("maras_hierarchy_changed", action, newRank, npcFormID);
            MARAS_LOG_INFO("Sent maras_hierarchy_changed event for NPC {:08X}: action={} rank={}", npcFormID,
                           action, newRank);
        }
    }  // namespace

    SpouseHierarchyManager& SpouseHierarchyManager::GetSingleton() {
        static SpouseHierarchyManager instance;
        return instance;
    }

    SpouseHierarchyManager::SpouseHierarchyManager() = default;

    int SpouseHierarchyManager::GetRank(RE::FormID npcFormID) const {
        if (npcFormID == 0) return 4;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->hierarchy->GetRank(State::ActorId{ npcFormID });
    }

    RE::FormID SpouseHierarchyManager::GetSpouseByRank(int rank) const {
        if (rank < 0 || rank >= static_cast<int>(State::SpouseHierarchyTable::kSlotCount)) {
            return 0;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->hierarchy->GetSpouseAtSlot(static_cast<std::size_t>(rank)).raw();
    }

    bool SpouseHierarchyManager::SetRank(RE::FormID npcFormID, int rank) {
        if (npcFormID == 0) return false;

        constexpr int kSlots = static_cast<int>(State::SpouseHierarchyTable::kSlotCount);
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> oldSlots{};
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> newSlots{};
        int oldRank = 4;
        RE::FormID occupant = 0;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            oldSlots = ws.hierarchy.Read().Slots();
            oldRank = ws.hierarchy.Read().GetRank(State::ActorId{ npcFormID });

            if (rank < 0 || rank >= kSlots) {
                if (oldRank >= 0 && oldRank < kSlots) {
                    const auto married = ws.GetPlayerSpousesInMarriageOrder();
                    std::vector<State::ActorId> liveMarried;
                    liveMarried.reserve(married.size());
                    const auto& dec = ws.deceased.Read();
                    for (const auto id : married) {
                        if (!id.IsNull() && !dec.IsDeceased(id)) {
                            liveMarried.push_back(id);
                        }
                    }
                    ws.hierarchy.Write().SetRank(State::ActorId{ npcFormID }, rank, liveMarried);
                }
            } else if (oldRank != rank) {
                occupant = oldSlots[static_cast<std::size_t>(rank)].raw();
                ws.hierarchy.Write().SetRank(State::ActorId{ npcFormID }, rank, {});
            }

            newSlots = ws.hierarchy.Read().Slots();
        });

        // Apply engine side effects and notifications strictly after releasing WorldStore mutex
        if (rank < 0 || rank >= kSlots) {
            if (oldRank >= 0 && oldRank < kSlots) {
                ClearFactionRank(npcFormID);
                const float diff = static_cast<float>(oldRank - kSlots);
                SendHierarchyChangedEvent(npcFormID, "demote", diff);

                for (const auto oldId : oldSlots) {
                    if (oldId.IsNull() || oldId.raw() == npcFormID) continue;
                    const bool stillPresent = std::any_of(newSlots.begin(), newSlots.end(), [&](State::ActorId s) {
                        return s == oldId;
                    });
                    if (!stillPresent) {
                        ClearFactionRank(oldId.raw());
                    }
                }
                for (std::size_t i = 0; i < newSlots.size(); ++i) {
                    const RE::FormID id = newSlots[i].raw();
                    if (id != 0) {
                        ApplyFactionRank(id, static_cast<int>(i));
                        MARAS_LOG_DEBUG("Filled spouse hierarchy slot {} with {:08X}", i, id);
                    }
                }
                for (std::size_t newIdx = 0; newIdx < newSlots.size(); ++newIdx) {
                    const RE::FormID id = newSlots[newIdx].raw();
                    if (id == 0 || id == npcFormID) continue;
                    int prevIdx = 4;
                    for (std::size_t j = 0; j < oldSlots.size(); ++j) {
                        if (oldSlots[j].raw() == id) {
                            prevIdx = static_cast<int>(j);
                            break;
                        }
                    }
                    const int curIdx = static_cast<int>(newIdx);
                    if (prevIdx != curIdx) {
                        const char* action = (curIdx < prevIdx) ? "promote" : "demote";
                        const float shiftDiff = static_cast<float>(prevIdx - curIdx);
                        SendHierarchyChangedEvent(id, action, shiftDiff);
                    }
                }
                BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            }
            PrismaUIDataBridge::GetSingleton().OnHierarchyChanged();
            return true;
        }

        if (oldRank == rank) {
            return true;
        }

        if (occupant == 0) {
            ApplyFactionRank(npcFormID, rank);
            const char* action = (rank < oldRank) ? "promote" : "demote";
            const float diff = static_cast<float>(oldRank - rank);
            SendHierarchyChangedEvent(npcFormID, action, diff);
        } else if (oldRank >= 0 && oldRank < kSlots) {
            ApplyFactionRank(occupant, oldRank);
            ApplyFactionRank(npcFormID, rank);
            const char* npcAction = (rank < oldRank) ? "promote" : "demote";
            SendHierarchyChangedEvent(npcFormID, npcAction, static_cast<float>(oldRank - rank));
            const char* occAction = (oldRank < rank) ? "promote" : "demote";
            SendHierarchyChangedEvent(occupant, occAction, static_cast<float>(rank - oldRank));
        } else {
            ApplyFactionRank(npcFormID, rank);
            ClearFactionRank(occupant);
            SendHierarchyChangedEvent(npcFormID, "promote", static_cast<float>(oldRank - rank));
            SendHierarchyChangedEvent(occupant, "demote", static_cast<float>(rank - kSlots));
        }

        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        PrismaUIDataBridge::GetSingleton().OnHierarchyChanged();
        return true;
    }

    void SpouseHierarchyManager::OnSpouseAdded(RE::FormID npcFormID) {
        if (npcFormID == 0) return;
        int assignedSlot = -1;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const int before = ws.hierarchy.Read().GetRank(State::ActorId{ npcFormID });
            if (before <= 2) {
                return;
            }
            ws.hierarchy.Write().OnSpouseAdded(State::ActorId{ npcFormID });
            const int after = ws.hierarchy.Read().GetRank(State::ActorId{ npcFormID });
            if (after <= 2) {
                assignedSlot = after;
            }
        });

        if (assignedSlot >= 0) {
            ApplyFactionRank(npcFormID, assignedSlot);
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            MARAS_LOG_INFO("Assigned spouse {:08X} to hierarchy slot {}", npcFormID, assignedSlot);
        }
    }

    void SpouseHierarchyManager::OnSpouseRemoved(RE::FormID npcFormID) {
        if (npcFormID == 0) return;

        bool wasPresent = false;
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> oldSlots{};
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> newSlots{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            oldSlots = ws.hierarchy.Read().Slots();
            for (const auto id : oldSlots) {
                if (id.raw() == npcFormID) {
                    wasPresent = true;
                    break;
                }
            }
            if (!wasPresent) {
                return;
            }

            const auto married = ws.GetPlayerSpousesInMarriageOrder();
            const auto& dec = ws.deceased.Read();
            auto& hier = ws.hierarchy.Write();
            for (std::size_t i = 0; i < State::SpouseHierarchyTable::kSlotCount; ++i) {
                if (hier.GetSpouseAtSlot(i).raw() == npcFormID) {
                    hier.SetRawSlot(i, State::ActorId{});
                }
            }
            hier.FillGaps(married, State::ActorId{ npcFormID }, [&](State::ActorId id) {
                return dec.IsDeceased(id);
            });
            newSlots = hier.Slots();
        });

        if (!wasPresent) {
            return;
        }

        ClearFactionRank(npcFormID);
        MARAS_LOG_INFO("Removed spouse {:08X} from hierarchy and filling gaps", npcFormID);

        for (const auto oldId : oldSlots) {
            if (oldId.IsNull() || oldId.raw() == npcFormID) continue;
            const bool stillPresent = std::any_of(newSlots.begin(), newSlots.end(), [&](State::ActorId s) {
                return s == oldId;
            });
            if (!stillPresent) {
                ClearFactionRank(oldId.raw());
            }
        }

        for (std::size_t i = 0; i < newSlots.size(); ++i) {
            const RE::FormID id = newSlots[i].raw();
            if (id != 0) {
                ApplyFactionRank(id, static_cast<int>(i));
                MARAS_LOG_DEBUG("Filled spouse hierarchy slot {} with {:08X}", i, id);
            }
        }

        for (std::size_t newIdx = 0; newIdx < newSlots.size(); ++newIdx) {
            const RE::FormID id = newSlots[newIdx].raw();
            if (id == 0) continue;
            int prevIdx = 4;
            for (std::size_t j = 0; j < oldSlots.size(); ++j) {
                if (oldSlots[j].raw() == id) {
                    prevIdx = static_cast<int>(j);
                    break;
                }
            }
            const int curIdx = static_cast<int>(newIdx);
            if (prevIdx != curIdx) {
                const char* action = (curIdx < prevIdx) ? "promote" : "demote";
                const float diff = static_cast<float>(prevIdx - curIdx);
                SendHierarchyChangedEvent(id, action, diff);
            }
        }
        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
    }

    void SpouseHierarchyManager::FillGaps(RE::FormID excludeFormID, bool sendEvents) {
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> oldSlots{};
        std::array<State::ActorId, State::SpouseHierarchyTable::kSlotCount> newSlots{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            oldSlots = ws.hierarchy.Read().Slots();
            const auto married = ws.GetPlayerSpousesInMarriageOrder();
            const auto& dec = ws.deceased.Read();
            ws.hierarchy.Write().FillGaps(married, State::ActorId{ excludeFormID }, [&](State::ActorId id) {
                return dec.IsDeceased(id);
            });
            newSlots = ws.hierarchy.Read().Slots();
        });

        for (const auto oldId : oldSlots) {
            if (oldId.IsNull()) continue;
            const bool stillPresent = std::any_of(newSlots.begin(), newSlots.end(), [&](State::ActorId s) {
                return s == oldId;
            });
            if (!stillPresent) {
                ClearFactionRank(oldId.raw());
            }
        }

        for (std::size_t i = 0; i < newSlots.size(); ++i) {
            const RE::FormID id = newSlots[i].raw();
            if (id != 0) {
                ApplyFactionRank(id, static_cast<int>(i));
                MARAS_LOG_DEBUG("Filled spouse hierarchy slot {} with {:08X}", i, id);
            }
        }

        if (sendEvents) {
            for (std::size_t newIdx = 0; newIdx < newSlots.size(); ++newIdx) {
                const RE::FormID id = newSlots[newIdx].raw();
                if (id == 0) continue;
                int prevIdx = 4;
                for (std::size_t j = 0; j < oldSlots.size(); ++j) {
                    if (oldSlots[j].raw() == id) {
                        prevIdx = static_cast<int>(j);
                        break;
                    }
                }
                const int curIdx = static_cast<int>(newIdx);
                if (prevIdx != curIdx) {
                    const char* action = (curIdx < prevIdx) ? "promote" : "demote";
                    const float diff = static_cast<float>(prevIdx - curIdx);
                    SendHierarchyChangedEvent(id, action, diff);
                }
            }
        }
        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
    }

    void SpouseHierarchyManager::SyncAllFactionsAndGlobals() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& slots = snap->hierarchy->Slots();

        std::unordered_set<RE::FormID> actorIdSet;
        actorIdSet.reserve(snap->actors->AllProfiles().size() + slots.size());
        for (const auto& [actorId, profile] : snap->actors->AllProfiles()) {
            (void)profile;
            if (!actorId.IsNull()) {
                actorIdSet.insert(actorId.raw());
            }
        }
        for (const auto id : slots) {
            if (!id.IsNull()) {
                actorIdSet.insert(id.raw());
            }
        }

        std::vector<RE::FormID> actorIds(actorIdSet.begin(), actorIdSet.end());
        if (!actorIds.empty()) {
            FactionReconciler::GetSingleton().ReconcileActors(actorIds);
        }
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();

        MARAS_LOG_INFO("Synchronized spouse hierarchy factions for top {} slots", slots.size());
    }

    nlohmann::json SpouseHierarchyManager::DumpJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& slots = snap->hierarchy->Slots();
        nlohmann::json j = nlohmann::json::array();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const RE::FormID id = slots[i].raw();
            nlohmann::json r;
            r["rank"] = static_cast<int>(i);
            r["formId"] = id != 0 ? std::format("{:08X}", id) : "";
            r["name"] = id != 0 ? ActorSvc::GetName(id) : "";
            j.push_back(r);
        }
        return j;
    }

}  // namespace MARAS
