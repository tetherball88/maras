#include "core/SpouseAssetsService.h"

#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "services/ActorService.h"
#include "utils/Common.h"

namespace MARAS {

    namespace {
        // Iterate all bed references registered for an NPC (by FormID) and apply a function
        inline void ForEachNpcBed(RE::FormID npcFormID, const std::function<void(RE::TESObjectREFR*)>& fn) {
            if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return;
            auto beds = MARAS::HomeCellService::GetSingleton().GetNpcBeds(npcFormID);
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
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID) || !ActorSvc::IsValid(npcFormID)) return false;

        // Lookup npc home from HomeCellService by FormID (supports unloaded persistent spouses)
        RE::FormID homeCell = MARAS::HomeCellService::GetSingleton().GetNpcHome(npcFormID);
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
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return false;

        bool found = false;

        // Try to obtain the home directly by FormID from HomeCellService.
        RE::FormID home = MARAS::HomeCellService::GetSingleton().GetNpcHome(npcFormID);
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

        // Fallback: scan all registered homes if we couldn't resolve the home above
        if (!found) {
            for (auto it = sharedHomes_.begin(); it != sharedHomes_.end();) {
                auto& data = it->second;
                if (data.sharingSpouses.erase(npcFormID) > 0) {
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

        // Version 7+: Save original bed owners map so pre-share furniture ownership survives save/load
        std::uint32_t bedOwnerCount = static_cast<std::uint32_t>(originalBedOwners_.size());
        if (!serialization->WriteRecordData(bedOwnerCount)) return false;
        for (const auto& [bedID, ownerID] : originalBedOwners_) {
            if (!serialization->WriteRecordData(bedID) || !serialization->WriteRecordData(ownerID)) {
                return false;
            }
        }

        MARAS_LOG_INFO("SpouseAssetsService: saved {} shared homes and {} original bed owners",
                       cellCount, bedOwnerCount);
        return true;
    }

    bool SpouseAssetsService::Load(SKSE::SerializationInterface* serialization, std::uint32_t version) {
        if (!serialization) return false;
        Revert();

        auto failLoad = [this]() -> bool {
            Revert();
            return false;
        };

        constexpr std::uint32_t kMaxRecords = 10000;

        std::uint32_t cellCount = 0;
        if (!serialization->ReadRecordData(cellCount)) return failLoad();
        if (cellCount > kMaxRecords) {
            MARAS_LOG_ERROR("SpouseAssetsService::Load - corrupt cellCount {}", cellCount);
            return failLoad();
        }

        for (std::uint32_t i = 0; i < cellCount; ++i) {
            RE::FormID savedCell = 0;
            if (!serialization->ReadRecordData(savedCell)) return failLoad();

            // Resolve cell FormID
            RE::FormID cellId = 0;
            if (savedCell == 0 || !serialization->ResolveFormID(savedCell, cellId) || cellId == 0) {
                // Unresolvable cell; skip its data payload safely
                std::uint8_t sharedFlag = 0;
                if (!serialization->ReadRecordData(sharedFlag)) return failLoad();
                std::uint8_t recordedFlag = 0;
                if (!serialization->ReadRecordData(recordedFlag)) return failLoad();
                std::uint8_t origState = 0;
                if (!serialization->ReadRecordData(origState)) return failLoad();
                std::uint32_t sharingCount = 0;
                if (!serialization->ReadRecordData(sharingCount)) return failLoad();
                if (sharingCount > kMaxRecords) return failLoad();
                for (std::uint32_t j = 0; j < sharingCount; ++j) {
                    RE::FormID discard = 0;
                    if (!serialization->ReadRecordData(discard)) return failLoad();
                }
                continue;
            }

            RegisteredCellData data;

            // shared flag
            std::uint8_t sharedFlag = 0;
            if (!serialization->ReadRecordData(sharedFlag)) return failLoad();
            data.sharedWithPlayer = (sharedFlag != 0);

            // original public recorded/state
            std::uint8_t recordedFlag = 0;
            if (!serialization->ReadRecordData(recordedFlag)) return failLoad();
            data.originalPublicRecorded = (recordedFlag != 0);
            std::uint8_t origState = 0;
            if (!serialization->ReadRecordData(origState)) return failLoad();
            data.originalPublicState = (origState != 0);

            // sharing spouses (defer dead/unmarried reconciliation to FinalizeLoad after all chunks load)
            std::uint32_t sharingCount = 0;
            if (!serialization->ReadRecordData(sharingCount)) return failLoad();
            if (sharingCount > kMaxRecords) {
                MARAS_LOG_ERROR("SpouseAssetsService::Load - corrupt sharingCount {}", sharingCount);
                return failLoad();
            }
            for (std::uint32_t j = 0; j < sharingCount; ++j) {
                RE::FormID savedSid = 0;
                if (!serialization->ReadRecordData(savedSid)) return failLoad();
                RE::FormID resolvedSid = 0;
                if (savedSid != 0 && !IsSyntheticChildID(savedSid) &&
                    serialization->ResolveFormID(savedSid, resolvedSid) && resolvedSid != 0) {
                    data.sharingSpouses.insert(resolvedSid);
                }
            }

            sharedHomes_.emplace(cellId, std::move(data));
        }

        // Version 7+: Load original bed owners before FinalizeLoad restores or re-applies furniture sharing
        if (version >= 7) {
            std::uint32_t bedOwnerCount = 0;
            if (!serialization->ReadRecordData(bedOwnerCount)) {
                return failLoad();
            }
            if (bedOwnerCount > kMaxRecords) {
                MARAS_LOG_ERROR("SpouseAssetsService::Load - corrupt bedOwnerCount {}", bedOwnerCount);
                return failLoad();
            }
            for (std::uint32_t i = 0; i < bedOwnerCount; ++i) {
                RE::FormID savedBedID = 0, savedOwnerID = 0;
                if (!serialization->ReadRecordData(savedBedID) || !serialization->ReadRecordData(savedOwnerID)) {
                    return failLoad();
                }
                RE::FormID resolvedBedID = 0, resolvedOwnerID = 0;
                if (savedBedID != 0 && serialization->ResolveFormID(savedBedID, resolvedBedID) && resolvedBedID != 0) {
                    if (savedOwnerID == 0) {
                        originalBedOwners_[resolvedBedID] = 0;
                    } else if (serialization->ResolveFormID(savedOwnerID, resolvedOwnerID) && resolvedOwnerID != 0) {
                        originalBedOwners_[resolvedBedID] = resolvedOwnerID;
                    }
                }
            }
        }

        MARAS_LOG_INFO("SpouseAssetsService: loaded {} shared homes and {} original bed owners",
                       sharedHomes_.size(), originalBedOwners_.size());
        return true;
    }

    void SpouseAssetsService::FinalizeLoad() {
        auto& deceasedReg = DeceasedRegistry::GetSingleton();
        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();

        for (auto it = sharedHomes_.begin(); it != sharedHomes_.end();) {
            const RE::FormID cellId = it->first;
            auto& data = it->second;

            std::vector<RE::FormID> invalidSpouses;
            for (RE::FormID spouseId : data.sharingSpouses) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(spouseId);
                if (!actor) {
                    actor = idx.GetActorFromFormId(spouseId);
                }
                if (deceasedReg.IsDeceased(spouseId) ||
                    (actor && actor->IsDead()) ||
                    !ActorSvc::IsValid(spouseId) ||
                    !relMgr.IsMarried(spouseId)) {
                    invalidSpouses.push_back(spouseId);
                }
            }

            for (RE::FormID invalidId : invalidSpouses) {
                MARAS_LOG_INFO("SpouseAssetsService::FinalizeLoad - removing dead/unmarried spouse {:08X} from shared cell {:08X}",
                               invalidId, cellId);
                data.sharingSpouses.erase(invalidId);
                StopShareFurniture(invalidId);
            }

            if (data.sharingSpouses.empty()) {
                RestoreCellOriginalPublicState(cellId, data);
                it = sharedHomes_.erase(it);
                continue;
            }

            data.sharedWithPlayer = true;
            ApplyCellSharedPublicState(cellId, data);
            for (RE::FormID spouseId : data.sharingSpouses) {
                ShareFurniture(spouseId);
            }
            ++it;
        }
    }

    void SpouseAssetsService::ShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID) || !ActorSvc::IsValid(npcFormID)) return;

        auto playerFaction = MARAS::FormCache::GetSingleton().GetPlayerFaction();
        if (!playerFaction) {
            MARAS_LOG_WARN("ShareFurniture: player faction not available");
            return;
        }

        // Record original bed owner (including 0 when unowned) before transferring to player faction
        ForEachNpcBed(npcFormID, [this, playerFaction](RE::TESObjectREFR* ref) {
            const RE::FormID bedID = ref->GetFormID();
            if (!originalBedOwners_.contains(bedID)) {
                auto* origOwner = ref->extraList.GetOwner();
                if (origOwner != playerFaction) {
                    originalBedOwners_[bedID] = origOwner ? origOwner->GetFormID() : 0;
                }
            }
            ref->extraList.SetOwner(playerFaction);
        });
    }

    void SpouseAssetsService::StopShareFurniture(RE::FormID npcFormID) {
        if (npcFormID == 0 || IsSyntheticChildID(npcFormID)) return;

        auto actorBase = ActorSvc::GetActorBase(npcFormID);

        ForEachNpcBed(npcFormID, [this, actorBase](RE::TESObjectREFR* ref) {
            const RE::FormID bedID = ref->GetFormID();
            auto it = originalBedOwners_.find(bedID);
            if (it != originalBedOwners_.end()) {
                const RE::FormID savedOwnerID = it->second;
                originalBedOwners_.erase(it);
                if (savedOwnerID == 0) {
                    ref->extraList.SetOwner(nullptr);
                    return;
                }
                if (auto* restoredOwner = RE::TESForm::LookupByID(savedOwnerID)) {
                    ref->extraList.SetOwner(restoredOwner);
                    return;
                }
            }
            if (actorBase) {
                ref->extraList.SetOwner(actorBase);
            }
        });
    }

    void SpouseAssetsService::Revert() {
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
