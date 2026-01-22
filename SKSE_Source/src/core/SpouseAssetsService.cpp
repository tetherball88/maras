#include "core/SpouseAssetsService.h"

#include <cstdint>
#include <functional>

#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "utils/Common.h"

namespace MARAS {

    namespace {
        // Helper to lookup an actor by formID
        inline RE::Actor* LookupActor(RE::FormID id) {
            if (id == 0) {
                return nullptr;
            }
            return RE::TESForm::LookupByID<RE::Actor>(id);
        }

        // Iterate all bed references registered for an NPC and apply a function
        inline void ForEachNpcBed(RE::Actor* actor, const std::function<void(RE::TESObjectREFR*)>& fn) {
            if (!actor) return;
            auto beds = MARAS::HomeCellService::GetSingleton().GetNpcBeds(actor);
            for (auto bedId : beds) {
                if (auto form = RE::TESForm::LookupByID(bedId)) {
                    if (auto ref = form->As<RE::TESObjectREFR>()) {
                        fn(ref);
                    }
                }
            }
        }
    }  // namespace

    SpouseAssetsService& SpouseAssetsService::GetSingleton() {
        static SpouseAssetsService instance;
        return instance;
    }

    SpouseAssetsService::SpouseAssetsService() { sharedHomes_.clear(); }

    // Note: door queries are provided by HomeCellService; this service doesn't store door lists.

    bool SpouseAssetsService::ShareHouseWithPlayer(RE::FormID npcFormID) {
        if (npcFormID == 0) return false;

        auto actor = LookupActor(npcFormID);
        if (!actor) return false;

        // Lookup npc home from HomeCellService
        RE::FormID homeCell = MARAS::HomeCellService::GetSingleton().GetNpcHome(actor);
        if (homeCell == 0) {
            MARAS_LOG_WARN("ShareHouseWithPlayer: NPC {:08X} has no known home", npcFormID);
            return false;
        }

        auto it = sharedHomes_.find(homeCell);
        if (it == sharedHomes_.end()) {
            RegisteredCellData data;
            data.sharingSpouses.insert(npcFormID);
            data.sharedWithPlayer = true;

            // Record original public state for the cell (if available) and set public if originally private
            if (auto form = RE::TESForm::LookupByID(homeCell)) {
                if (auto cell = form->As<RE::TESObjectCELL>()) {
                    std::uint16_t flags = cell->cellFlags.underlying();
                    bool isPublic = (flags & static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0;
                    data.originalPublicRecorded = true;
                    data.originalPublicState = isPublic;
                    if (!isPublic) {
                        cell->SetPublic(true);
                        MARAS_LOG_INFO("ShareHouseWithPlayer: set cell {:08X} public=true (was private)", homeCell);
                    }
                } else {
                    MARAS_LOG_WARN("ShareHouseWithPlayer: cell {:08X} form is not a TESObjectCELL", homeCell);
                }
            } else {
                MARAS_LOG_WARN("ShareHouseWithPlayer: could not lookup cell {:08X} to record public state", homeCell);
            }

            sharedHomes_.emplace(homeCell, std::move(data));
        } else {
            // Ensure original public state is recorded for existing entry
            if (!it->second.originalPublicRecorded) {
                if (auto form = RE::TESForm::LookupByID(homeCell)) {
                    if (auto cell = form->As<RE::TESObjectCELL>()) {
                        std::uint16_t flags = cell->cellFlags.underlying();
                        bool isPublic = (flags & static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0;
                        it->second.originalPublicRecorded = true;
                        it->second.originalPublicState = isPublic;
                        if (!isPublic) {
                            cell->SetPublic(true);
                            MARAS_LOG_INFO("ShareHouseWithPlayer: set cell {:08X} public=true (was private)", homeCell);
                        }
                    }
                }
            }

            it->second.sharingSpouses.insert(npcFormID);
            it->second.sharedWithPlayer = true;
        }

        // Transfer owned beds to player faction using HomeCellService data
        ShareFurniture(npcFormID);

        MARAS_LOG_INFO("ShareHouseWithPlayer: NPC {:08X} shared home {:08X}", npcFormID, homeCell);
        return true;
    }

    bool SpouseAssetsService::StopShareHouseWithPlayer(RE::FormID npcFormID) {
        if (npcFormID == 0) return false;

        bool found = false;

        // Try to obtain the actor and home from HomeCellService. If we can find the home,
        // only operate on that home entry. Otherwise fall back to scanning all shared homes.
        auto actor = LookupActor(npcFormID);
        if (actor) {
            RE::FormID home = MARAS::HomeCellService::GetSingleton().GetNpcHome(actor);
            if (home != 0) {
                auto it = sharedHomes_.find(home);
                if (it != sharedHomes_.end()) {
                    auto& data = it->second;

                    if (data.sharingSpouses.contains(npcFormID)) {
                        data.sharingSpouses.erase(npcFormID);
                        StopShareFurniture(npcFormID);
                        found = true;
                    }

                    data.sharedWithPlayer = !data.sharingSpouses.empty();
                    // If no more spouses sharing this home, restore original public state if needed
                    if (!data.sharedWithPlayer) {
                        if (data.originalPublicRecorded && !data.originalPublicState) {
                            if (auto form = RE::TESForm::LookupByID(home)) {
                                if (auto cell = form->As<RE::TESObjectCELL>()) {
                                    cell->SetPublic(false);
                                    MARAS_LOG_INFO("StopShareHouseWithPlayer: restored cell {:08X} public=false", home);
                                }
                            }
                        }
                        sharedHomes_.erase(it);
                    }
                }
            }
        }

        // Fallback: scan all registered homes if we couldn't resolve the actor/home above
        if (!found) {
            for (auto it = sharedHomes_.begin(); it != sharedHomes_.end();) {
                auto& data = it->second;
                if (data.sharingSpouses.erase(npcFormID) > 0) {
                    // Attempt to restore furniture ownership (may no-op if actor can't be looked up)
                    StopShareFurniture(npcFormID);
                    found = true;

                    data.sharedWithPlayer = !data.sharingSpouses.empty();
                    if (!data.sharedWithPlayer) {
                        // Restore original public state if we recorded it and it was originally private
                        if (data.originalPublicRecorded && !data.originalPublicState) {
                            if (auto form = RE::TESForm::LookupByID(it->first)) {
                                if (auto cell = form->As<RE::TESObjectCELL>()) {
                                    cell->SetPublic(false);
                                    MARAS_LOG_INFO("StopShareHouseWithPlayer: restored cell {:08X} public=false",
                                                   it->first);
                                }
                            }
                        }

                        it = sharedHomes_.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }

        MARAS_LOG_INFO("StopShareHouseWithPlayer: stopped sharing for NPC {:08X} (found: {})", npcFormID, found);
        return found;
    }

    bool SpouseAssetsService::IsHouseSharedWithPlayer(RE::FormID cellFormID) const {
        if (cellFormID == 0) return false;
        auto it = sharedHomes_.find(cellFormID);
        if (it == sharedHomes_.end()) return false;
        return it->second.sharedWithPlayer;
    }

    bool SpouseAssetsService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;
        // Save number of shared homes
        std::uint32_t cellCount = static_cast<std::uint32_t>(sharedHomes_.size());
        if (!serialization->WriteRecordData(cellCount)) return false;

        for (const auto& kv : sharedHomes_) {
            RE::FormID cellId = kv.first;
            if (!serialization->WriteRecordData(cellId)) return false;

            // shared flag
            if (!serialization->WriteRecordData(static_cast<std::uint8_t>(kv.second.sharedWithPlayer ? 1 : 0)))
                return false;

            // original public recorded/state
            if (!serialization->WriteRecordData(static_cast<std::uint8_t>(kv.second.originalPublicRecorded ? 1 : 0)))
                return false;
            if (!serialization->WriteRecordData(static_cast<std::uint8_t>(kv.second.originalPublicState ? 1 : 0)))
                return false;

            // sharing spouses set
            std::uint32_t sharingCount = static_cast<std::uint32_t>(kv.second.sharingSpouses.size());
            if (!serialization->WriteRecordData(sharingCount)) return false;
            for (auto s : kv.second.sharingSpouses)
                if (!serialization->WriteRecordData(s)) return false;
        }

        MARAS_LOG_INFO("SpouseAssetsService: saved {} shared homes", cellCount);
        return true;
    }

    bool SpouseAssetsService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;
        sharedHomes_.clear();

        std::uint32_t cellCount = 0;
        if (!serialization->ReadRecordData(cellCount)) return false;
        for (std::uint32_t i = 0; i < cellCount; ++i) {
            RE::FormID savedCell = 0;
            if (!serialization->ReadRecordData(savedCell)) return false;

            // Resolve cell FormID
            RE::FormID cellId = 0;
            if (savedCell != 0 && !serialization->ResolveFormID(savedCell, cellId)) {
                // Unresolvable cell; skip its data payload safely
                std::uint8_t sharedFlag = 0;
                if (!serialization->ReadRecordData(sharedFlag)) return false;
                std::uint8_t recordedFlag = 0;
                if (!serialization->ReadRecordData(recordedFlag)) return false;
                std::uint8_t origState = 0;
                if (!serialization->ReadRecordData(origState)) return false;
                std::uint32_t sharingCount = 0;
                if (!serialization->ReadRecordData(sharingCount)) return false;
                for (std::uint32_t j = 0; j < sharingCount; ++j) {
                    RE::FormID discard = 0;
                    if (!serialization->ReadRecordData(discard)) return false;
                }
                continue;
            }

            RegisteredCellData data;

            // shared flag
            std::uint8_t sharedFlag = 0;
            if (!serialization->ReadRecordData(sharedFlag)) return false;
            data.sharedWithPlayer = (sharedFlag != 0);

            // original public recorded/state
            std::uint8_t recordedFlag = 0;
            if (!serialization->ReadRecordData(recordedFlag)) return false;
            data.originalPublicRecorded = (recordedFlag != 0);
            std::uint8_t origState = 0;
            if (!serialization->ReadRecordData(origState)) return false;
            data.originalPublicState = (origState != 0);

            // sharing spouses
            std::uint32_t sharingCount = 0;
            if (!serialization->ReadRecordData(sharingCount)) return false;
            for (std::uint32_t j = 0; j < sharingCount; ++j) {
                RE::FormID savedSid = 0;
                if (!serialization->ReadRecordData(savedSid)) return false;
                RE::FormID resolvedSid = 0;
                if (savedSid != 0 && serialization->ResolveFormID(savedSid, resolvedSid)) {
                    // Skip dead or invalid actors
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(resolvedSid);
                    if (!actor || actor->IsDead()) {
                        MARAS_LOG_INFO("SpouseAssetsService::Load - skipping dead/invalid actor {:08X}", resolvedSid);
                        continue;
                    }
                    data.sharingSpouses.insert(resolvedSid);
                }
            }

            // Only store if there are still valid sharing spouses
            if (!data.sharingSpouses.empty()) {
                sharedHomes_.emplace(cellId, std::move(data));
            } else {
                // Restore original public state if all spouses were dead
                if (data.originalPublicRecorded && !data.originalPublicState) {
                    if (auto form = RE::TESForm::LookupByID(cellId)) {
                        if (auto cell = form->As<RE::TESObjectCELL>()) {
                            cell->SetPublic(false);
                            MARAS_LOG_INFO("SpouseAssetsService::Load - restored cell {:08X} public=false (all spouses dead)", cellId);
                        }
                    }
                }
            }
        }

        MARAS_LOG_INFO("SpouseAssetsService: loaded {} shared homes", sharedHomes_.size());
        return true;
    }

    void SpouseAssetsService::ShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0) return;
        auto actor = LookupActor(npcFormID);
        if (!actor) return;

        auto playerFaction = MARAS::FormCache::GetSingleton().GetPlayerFaction();
        if (!playerFaction) {
            MARAS_LOG_WARN("ShareFurniture: player faction not available");
            return;
        }

        // Use HomeCellService recorded beds for this NPC
        ForEachNpcBed(actor, [playerFaction](RE::TESObjectREFR* ref) { ref->extraList.SetOwner(playerFaction); });
    }

    void SpouseAssetsService::StopShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0) return;

        auto actor = LookupActor(npcFormID);
        if (!actor) return;

        // Restore those bed refs back to the actor
        ForEachNpcBed(actor, [actor](RE::TESObjectREFR* ref) { ref->extraList.SetOwner(actor->GetActorBase()); });
    }

    void SpouseAssetsService::Revert() {
        sharedHomes_.clear();
        MARAS_LOG_INFO("SpouseAssetsService: reverted data");
    }

}  // namespace MARAS
