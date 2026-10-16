#include "core/SpouseAssetsService.h"

#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "core/NPCIndex.h"
#include "services/ActorService.h"
#include "utils/Common.h"

namespace MARAS {

    namespace {
        // Helper to lookup an actor by formID via NPCIndex
        inline RE::Actor* LookupActor(RE::FormID id) {
            if (id == 0) {
                return nullptr;
            }
            return NPCIndex::GetSingleton().GetActorFromFormId(id);
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

        // Captures original cell public flag if not yet recorded and ensures cell is set public
        void ApplyCellSharedPublicState(RE::FormID cellFormID, RegisteredCellData& data) {
            auto* form = RE::TESForm::LookupByID(cellFormID);
            if (!form) {
                MARAS_LOG_WARN("SpouseAssetsService: could not lookup cell {:08X} to apply public state", cellFormID);
                return;
            }
            auto* cell = form->As<RE::TESObjectCELL>();
            if (!cell) {
                MARAS_LOG_WARN("SpouseAssetsService: cell {:08X} form is not a TESObjectCELL", cellFormID);
                return;
            }

            std::uint16_t flags = cell->cellFlags.underlying();
            bool isPublic = (flags & static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0;
            if (!data.originalPublicRecorded) {
                data.originalPublicRecorded = true;
                data.originalPublicState = isPublic;
            }
            if (!isPublic) {
                cell->SetPublic(true);
                MARAS_LOG_INFO("SpouseAssetsService: set cell {:08X} public=true (was private)", cellFormID);
            }
        }

        // Restores original cell public flag if it was originally private
        void RestoreCellOriginalPublicState(RE::FormID cellFormID, const RegisteredCellData& data) {
            if (!data.originalPublicRecorded || data.originalPublicState) {
                return;
            }
            if (auto* form = RE::TESForm::LookupByID(cellFormID)) {
                if (auto* cell = form->As<RE::TESObjectCELL>()) {
                    cell->SetPublic(false);
                    MARAS_LOG_INFO("SpouseAssetsService: restored cell {:08X} public=false", cellFormID);
                }
            }
        }
    }  // namespace

    SpouseAssetsService& SpouseAssetsService::GetSingleton() {
        static SpouseAssetsService instance;
        return instance;
    }

    SpouseAssetsService::SpouseAssetsService() {
        sharedHomes_.clear();
        originalBedOwners_.clear();
    }

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

        auto& data = sharedHomes_[homeCell];
        ApplyCellSharedPublicState(homeCell, data);
        data.sharingSpouses.insert(npcFormID);
        data.sharedWithPlayer = true;

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
                        RestoreCellOriginalPublicState(home, data);
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
                        RestoreCellOriginalPublicState(it->first, data);
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
        Revert();

        constexpr std::uint32_t kMaxRecords = 10000;

        std::uint32_t cellCount = 0;
        if (!serialization->ReadRecordData(cellCount)) return false;
        if (cellCount > kMaxRecords) {
            MARAS_LOG_ERROR("SpouseAssetsService::Load - corrupt cellCount {}", cellCount);
            return false;
        }

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
                if (sharingCount > kMaxRecords) return false;
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
            if (sharingCount > kMaxRecords) {
                MARAS_LOG_ERROR("SpouseAssetsService::Load - corrupt sharingCount {}", sharingCount);
                return false;
            }
            for (std::uint32_t j = 0; j < sharingCount; ++j) {
                RE::FormID savedSid = 0;
                if (!serialization->ReadRecordData(savedSid)) return false;
                RE::FormID resolvedSid = 0;
                if (savedSid != 0 && serialization->ResolveFormID(savedSid, resolvedSid)) {
                    // Skip dead or invalid actors
                    auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(resolvedSid);
                    if (!actor || actor->IsDead()) {
                        MARAS_LOG_INFO("SpouseAssetsService::Load - skipping dead/invalid actor {:08X}", resolvedSid);
                        continue;
                    }
                    data.sharingSpouses.insert(resolvedSid);
                }
            }

            // Only store and re-apply if there are still valid sharing spouses
            if (!data.sharingSpouses.empty()) {
                data.sharedWithPlayer = true;
                ApplyCellSharedPublicState(cellId, data);
                for (auto spouseId : data.sharingSpouses) {
                    ShareFurniture(spouseId);
                }
                sharedHomes_.emplace(cellId, std::move(data));
            } else {
                // Restore original public state if all spouses were dead
                RestoreCellOriginalPublicState(cellId, data);
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

        // Record original bed owner before transferring to player faction
        ForEachNpcBed(actor, [this, playerFaction](RE::TESObjectREFR* ref) {
            const RE::FormID bedID = ref->GetFormID();
            if (!originalBedOwners_.contains(bedID)) {
                if (auto* origOwner = ref->extraList.GetOwner()) {
                    if (origOwner != playerFaction) {
                        originalBedOwners_[bedID] = origOwner->GetFormID();
                    }
                }
            }
            ref->extraList.SetOwner(playerFaction);
        });
    }

    void SpouseAssetsService::StopShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0) return;

        auto actor = LookupActor(npcFormID);
        if (!actor) return;

        auto actorBase = ActorSvc::GetActorBase(npcFormID);

        ForEachNpcBed(actor, [this, actorBase](RE::TESObjectREFR* ref) {
            const RE::FormID bedID = ref->GetFormID();
            RE::TESForm* restoredOwner = nullptr;
            auto it = originalBedOwners_.find(bedID);
            if (it != originalBedOwners_.end()) {
                restoredOwner = RE::TESForm::LookupByID(it->second);
                originalBedOwners_.erase(it);
            }
            if (!restoredOwner) {
                restoredOwner = actorBase;
            }
            if (restoredOwner) {
                ref->extraList.SetOwner(restoredOwner);
            }
        });
    }

    void SpouseAssetsService::Revert() {
        for (const auto& [cellID, data] : sharedHomes_) {
            for (auto spouseID : data.sharingSpouses) {
                StopShareFurniture(spouseID);
            }
            RestoreCellOriginalPublicState(cellID, data);
        }
        sharedHomes_.clear();
        originalBedOwners_.clear();
        MARAS_LOG_INFO("SpouseAssetsService: reverted data");
    }

    nlohmann::json SpouseAssetsService::DumpJson() const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [cellID, data] : sharedHomes_) {
            nlohmann::json h;
            h["cellFormId"] = std::format("{:08X}", cellID);
            std::string cellName = "";
            if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID)) {
                if (cell->GetName()) cellName = cell->GetName();
            }
            h["cellName"] = cellName;
            h["sharedWithPlayer"] = data.sharedWithPlayer;
            h["originalPublicRecorded"] = data.originalPublicRecorded;
            h["originalPublicState"] = data.originalPublicState;

            nlohmann::json spousesArray = nlohmann::json::array();
            for (auto spouseID : data.sharingSpouses) {
                nlohmann::json s;
                s["formId"] = std::format("{:08X}", spouseID);
                s["name"] = ActorSvc::GetName(spouseID);
                spousesArray.push_back(s);
            }
            h["sharingSpouses"] = spousesArray;
            j.push_back(h);
        }
        return j;
    }

}  // namespace MARAS
