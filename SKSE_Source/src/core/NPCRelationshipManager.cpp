#include "core/NPCRelationshipManager.h"

#include <spdlog/spdlog.h>

#include <chrono>

#include "core/FormCache.h"
#include "core/NPCTypeDeterminer.h"
#include "core/Serialization.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/JsonOverrideLoader.h"

namespace MARAS {

    NPCRelationshipManager& NPCRelationshipManager::GetSingleton() {
        static NPCRelationshipManager instance;
        return instance;
    }

    // Private helper methods
    void NPCRelationshipManager::RemoveFromAllBuckets(RE::FormID npcFormID) {
        candidates.erase(npcFormID);
        engaged.erase(npcFormID);
        married.erase(npcFormID);
        divorced.erase(npcFormID);
        jilted.erase(npcFormID);
        deceased.erase(npcFormID);
    }

    void NPCRelationshipManager::AddToBucket(RE::FormID npcFormID, RelationshipStatus status) {
        switch (status) {
            case RelationshipStatus::Candidate:
                candidates.insert(npcFormID);
                break;
            case RelationshipStatus::Engaged:
                engaged.insert(npcFormID);
                break;
            case RelationshipStatus::Married:
                married.insert(npcFormID);
                break;
            case RelationshipStatus::Divorced:
                divorced.insert(npcFormID);
                break;
            case RelationshipStatus::Jilted:
                jilted.insert(npcFormID);
                break;
            case RelationshipStatus::Deceased:
                deceased.insert(npcFormID);
                break;
        }
    }

    void NPCRelationshipManager::RemoveFromBucket(RE::FormID npcFormID, RelationshipStatus status) {
        switch (status) {
            case RelationshipStatus::Candidate:
                candidates.erase(npcFormID);
                break;
            case RelationshipStatus::Engaged:
                engaged.erase(npcFormID);
                break;
            case RelationshipStatus::Married:
                married.erase(npcFormID);
                break;
            case RelationshipStatus::Divorced:
                divorced.erase(npcFormID);
                break;
            case RelationshipStatus::Jilted:
                jilted.erase(npcFormID);
                break;
            case RelationshipStatus::Deceased:
                deceased.erase(npcFormID);
                break;
        }
    }

    void NPCRelationshipManager::UpdateTrackedFactionRank(RE::FormID npcFormID, RelationshipStatus status) {
        // Get the tracked NPC faction from the centralized cache
        auto trackedFaction = FormCache::GetSingleton().GetTrackedFaction();
        if (!trackedFaction) {
            return;
        }

        std::int8_t rank = static_cast<std::int8_t>(status);
        AddToFaction(npcFormID, trackedFaction, rank);

        // Send status changed event to Papyrus
        SendStatusChangedEvent(npcFormID, status);

        MARAS_LOG_INFO("Set tracked faction rank {} for NPC {:08X} (status: {})", rank, npcFormID,
                       Utils::RelationshipStatusToString(status));
    }

    void NPCRelationshipManager::SendStatusChangedEvent(RE::FormID npcFormID, RelationshipStatus status) {
        // Get the actor reference
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        auto npcActor = npcForm ? npcForm->As<RE::Actor>() : nullptr;
        if (!npcActor) {
            MARAS_LOG_WARN("Could not find actor for FormID {:08X} to send status event", npcFormID);
            return;
        }

        // Prepare event parameters
        std::int32_t statusEnum = static_cast<std::int32_t>(status);
        std::string statusString(Utils::RelationshipStatusToString(status));

        // Convert to lowercase to match existing Papyrus conventions
        std::transform(statusString.begin(), statusString.end(), statusString.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Send mod event from SKSE to Papyrus using ModEvent
        auto eventSource = SKSE::GetModCallbackEventSource();
        if (eventSource) {
            SKSE::ModCallbackEvent modEvent("maras_status_changed", statusString, static_cast<float>(statusEnum),
                                            npcActor);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_status_changed event for NPC {:08X}: status={} ({})", npcFormID, statusEnum,
                           statusString);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send event");
        }
    }

    bool NPCRelationshipManager::ChangeStatusCommon(RE::FormID npcFormID, RelationshipStatus status,
                                                    const std::function<void(RE::FormID)>& postAction) {
        // Ensure NPC is registered and has storage/faction data
        if (!EnsureRegistered(npcFormID)) {
            MARAS_LOG_WARN("Cannot change status for unregistered NPC {:08X}", npcFormID);
            return false;
        }

        // Remove from any previous buckets and add to desired status bucket
        RemoveFromAllBuckets(npcFormID);
        AddToBucket(npcFormID, status);

        auto& data = npcData[npcFormID];
        data.status = status;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, status);

        // Manage faction membership based on relationship status
        ManageFactions(npcFormID, status);

        // Optional post-action (notify hierarchy, cleanup assets, etc.)
        if (postAction) {
            try {
                postAction(npcFormID);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("Exception in ChangeStatusCommon postAction for {:08X}: {}", npcFormID, e.what());
            }
        }

        MARAS_LOG_INFO("Set status for NPC {:08X} to {}", npcFormID, Utils::RelationshipStatusToString(status));
        // Recalculate and update TT_MARAS.esp globals that track love interests and spouses
        RecalculateAndUpdateGlobals();
        return true;
    }

    // Registration and unregistration
    bool NPCRelationshipManager::RegisterAsCandidate(RE::FormID npcFormID) {
        auto startTime = std::chrono::high_resolution_clock::now();

        if (IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("NPC {:08X} is already registered", npcFormID);
            return false;
        }

        // Automatically determine all attributes
        SocialClass socialClass = DetermineSocialClass(npcFormID);
        SkillType skillType = DetermineSkillType(npcFormID);
        // Temperament depends on SC and ST; prefer override if present
        Temperament temperament = [this, npcFormID, socialClass, skillType]() {
            if (auto ov = GetTemperamentOverride(npcFormID); ov.has_value()) {
                return Utils::StringToTemperament(ov.value());
            }
            return NPCTypeDeterminer::ComputeTemperament(socialClass, skillType);
        }();

        // Add to master set
        allRegistered.insert(npcFormID);

        // Add to candidate bucket
        candidates.insert(npcFormID);

        // Store NPC data
        npcData[npcFormID] = NPCRelationshipData(npcFormID, socialClass, skillType, temperament);

        // Add to appropriate factions with enum values as ranks
        AddToSocialClassFaction(npcFormID, static_cast<std::int8_t>(socialClass));
        AddToSkillTypeFaction(npcFormID, static_cast<std::int8_t>(skillType));
        AddToTemperamentFaction(npcFormID, static_cast<std::int8_t>(temperament));

        // Add to tracked faction with status as rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Candidate);

        // Manage status-based faction membership
        ManageFactions(npcFormID, RelationshipStatus::Candidate);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Auto-registered NPC {} ({:08X}) as candidate in {} microseconds (SC: {}, ST: {}, T: {})",
                       Utils::GetNPCName(npcFormID), npcFormID, duration.count(),
                       Utils::SocialClassToString(socialClass), Utils::SkillTypeToString(skillType),
                       Utils::TemperamentToString(temperament));

        // Update globals in case registration affects tracked counts
        RecalculateAndUpdateGlobals();

        return true;
    }

    bool NPCRelationshipManager::UnregisterNPC(MARAS::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("NPC {:08X} is not registered", npcFormID);
            return false;
        }

        // Remove from master set
        allRegistered.erase(npcFormID);

        // Remove from all buckets
        RemoveFromAllBuckets(npcFormID);

        // Remove data
        npcData.erase(npcFormID);

        // Ensure spouse hierarchy is updated if this NPC was present
        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);
        // Recalculate globals after removal
        RecalculateAndUpdateGlobals();
        MARAS_LOG_INFO("Unregistered NPC {} ({:08X})", Utils::GetNPCName(npcFormID), npcFormID);
        return true;
    }

    // Status checks
    bool NPCRelationshipManager::IsRegistered(RE::FormID npcFormID) const { return allRegistered.contains(npcFormID); }

    bool NPCRelationshipManager::IsCandidate(RE::FormID npcFormID) const { return candidates.contains(npcFormID); }

    bool NPCRelationshipManager::IsEngaged(RE::FormID npcFormID) const { return engaged.contains(npcFormID); }

    bool NPCRelationshipManager::IsMarried(RE::FormID npcFormID) const { return married.contains(npcFormID); }

    bool NPCRelationshipManager::IsDivorced(RE::FormID npcFormID) const { return divorced.contains(npcFormID); }

    bool NPCRelationshipManager::IsJilted(RE::FormID npcFormID) const { return jilted.contains(npcFormID); }

    bool NPCRelationshipManager::IsDeceased(RE::FormID npcFormID) const { return deceased.contains(npcFormID); }

    // Status transitions
    bool NPCRelationshipManager::PromoteToEngaged(RE::FormID npcFormID) {
        return ChangeStatusCommon(npcFormID, RelationshipStatus::Engaged, nullptr);
    }

    bool NPCRelationshipManager::PromoteToMarried(RE::FormID npcFormID) {
        return ChangeStatusCommon(npcFormID, RelationshipStatus::Married,
                                  [](RE::FormID id) { SpouseHierarchyManager::GetSingleton().OnSpouseAdded(id); });
    }

    bool NPCRelationshipManager::PromoteToDivorced(RE::FormID npcFormID) {
        return ChangeStatusCommon(npcFormID, RelationshipStatus::Divorced, [](RE::FormID id) {
            SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(id);
            MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(id);
        });
    }

    bool NPCRelationshipManager::PromoteToJilted(RE::FormID npcFormID) {
        return ChangeStatusCommon(npcFormID, RelationshipStatus::Jilted, nullptr);
    }

    bool NPCRelationshipManager::PromoteToDeceased(RE::FormID npcFormID) {
        return ChangeStatusCommon(npcFormID, RelationshipStatus::Deceased,
                                  [](RE::FormID id) { SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(id); });
    }

    // Bulk retrievals
    std::vector<RE::FormID> NPCRelationshipManager::GetAllRegisteredNPCs() const {
        return std::vector<RE::FormID>(allRegistered.begin(), allRegistered.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllCandidates() const {
        return std::vector<RE::FormID>(candidates.begin(), candidates.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllEngaged() const {
        return std::vector<RE::FormID>(engaged.begin(), engaged.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllMarried() const {
        return std::vector<RE::FormID>(married.begin(), married.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDivorced() const {
        return std::vector<RE::FormID>(divorced.begin(), divorced.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllJilted() const {
        return std::vector<RE::FormID>(jilted.begin(), jilted.end());
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDeceased() const {
        return std::vector<RE::FormID>(deceased.begin(), deceased.end());
    }

    // Data access
    const NPCRelationshipData* NPCRelationshipManager::GetNPCData(RE::FormID npcFormID) const {
        auto it = npcData.find(npcFormID);
        return (it != npcData.end()) ? &it->second : nullptr;
    }

    RelationshipStatus NPCRelationshipManager::GetRelationshipStatus(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->status : RelationshipStatus::Unknown;
    }

    SocialClass NPCRelationshipManager::GetSocialClass(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->socialClass : SocialClass::Outcast;
    }

    SkillType NPCRelationshipManager::GetSkillType(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->skillType : SkillType::Warrior;
    }

    Temperament NPCRelationshipManager::GetTemperament(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->temperament : Temperament::Proud;
    }

    // Statistics
    size_t NPCRelationshipManager::GetTotalRegisteredCount() const { return allRegistered.size(); }

    size_t NPCRelationshipManager::GetCandidateCount() const { return candidates.size(); }

    size_t NPCRelationshipManager::GetEngagedCount() const { return engaged.size(); }

    size_t NPCRelationshipManager::GetMarriedCount() const { return married.size(); }

    size_t NPCRelationshipManager::GetDivorcedCount() const { return divorced.size(); }

    size_t NPCRelationshipManager::GetJiltedCount() const { return jilted.size(); }

    size_t NPCRelationshipManager::GetDeceasedCount() const { return deceased.size(); }

    // Save/Load support
    void NPCRelationshipManager::Clear() {
        allRegistered.clear();
        candidates.clear();
        engaged.clear();
        married.clear();
        divorced.clear();
        jilted.clear();
        deceased.clear();
        npcData.clear();

        MARAS_LOG_INFO("Cleared all NPC relationship data");
    }

    bool NPCRelationshipManager::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) {
            MARAS_LOG_ERROR("Serialization interface is null");
            return false;
        }

        // Write magic number for validation
        if (!serialization->WriteRecordData(Serialization::kMagicNumber)) {
            MARAS_LOG_ERROR("Failed to write magic number");
            return false;
        }

        // Write the count of NPCs
        std::uint32_t npcCount = static_cast<std::uint32_t>(npcData.size());
        if (!serialization->WriteRecordData(npcCount)) {
            MARAS_LOG_ERROR("Failed to write NPC count");
            return false;
        }

        // Write each NPC's data
        for (const auto& [formID, data] : npcData) {
            if (!serialization->WriteRecordData(formID) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.socialClass)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.skillType)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.temperament)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.status)) ||
                !serialization->WriteRecordData(data.originalHome.has_value()) ||
                (data.originalHome.has_value() && !serialization->WriteRecordData(data.originalHome.value())) ||
                !serialization->WriteRecordData(data.currentHome.has_value()) ||
                (data.currentHome.has_value() && !serialization->WriteRecordData(data.currentHome.value())) ||
                !serialization->WriteRecordData(data.engagementDate) ||
                !serialization->WriteRecordData(data.marriageDate)) {
                MARAS_LOG_ERROR("Failed to write data for NPC {:08X}", formID);
                return false;
            }
        }

        MARAS_LOG_INFO("Successfully saved {} NPC relationship records", npcCount);
        return true;
    }

    bool NPCRelationshipManager::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("Serialization interface is null");
            return false;
        }

        Clear();

        std::uint32_t magic;
        if (!serialization->ReadRecordData(magic) || magic != Serialization::kMagicNumber) {
            MARAS_LOG_ERROR("Invalid magic number in save data");
            return false;
        }

        std::uint32_t npcCount;
        if (!serialization->ReadRecordData(npcCount)) {
            MARAS_LOG_ERROR("Failed to read NPC count");
            return false;
        }

        for (std::uint32_t i = 0; i < npcCount; ++i) {
            RE::FormID oldFormID = 0, newFormID = 0;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("Could not resolve FormID {:08X}, skipping NPC", oldFormID);
                // Consume the rest of this NPC's record to stay aligned
                std::uint8_t discard8 = 0;
                // socialClass, skillType, temperament, status
                for (int k = 0; k < 4; ++k) {
                    if (!serialization->ReadRecordData(discard8)) return false;
                }
                // originalHome flag and maybe value
                bool hasOrig = false;
                if (!serialization->ReadRecordData(hasOrig)) return false;
                if (hasOrig) {
                    RE::FormID discardForm = 0;
                    if (!serialization->ReadRecordData(discardForm)) return false;
                }
                // currentHome flag and maybe value
                bool hasCurr = false;
                if (!serialization->ReadRecordData(hasCurr)) return false;
                if (hasCurr) {
                    RE::FormID discardForm = 0;
                    if (!serialization->ReadRecordData(discardForm)) return false;
                }
                // engagementDate, marriageDate
                std::uint32_t discard32 = 0;
                if (!serialization->ReadRecordData(discard32) || !serialization->ReadRecordData(discard32))
                    return false;
                continue;
            }

            NPCRelationshipData data;
            data.formID = newFormID;

            std::uint8_t enumValue = 0;

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.socialClass = static_cast<SocialClass>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.skillType = static_cast<SkillType>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.temperament = static_cast<Temperament>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.status = static_cast<RelationshipStatus>(enumValue);

            // Read optional originalHome
            bool hasOriginalHome = false;
            if (!serialization->ReadRecordData(hasOriginalHome)) return false;
            if (hasOriginalHome) {
                RE::FormID oldHome = 0, resolvedHome = 0;
                if (!serialization->ReadRecordData(oldHome)) return false;
                if (oldHome != 0 && serialization->ResolveFormID(oldHome, resolvedHome)) {
                    data.originalHome = resolvedHome;
                }
            }

            // Read optional currentHome
            bool hasCurrentHome = false;
            if (!serialization->ReadRecordData(hasCurrentHome)) return false;
            if (hasCurrentHome) {
                RE::FormID oldHome = 0, resolvedHome = 0;
                if (!serialization->ReadRecordData(oldHome)) return false;
                if (oldHome != 0 && serialization->ResolveFormID(oldHome, resolvedHome)) {
                    data.currentHome = resolvedHome;
                }
            }

            if (!serialization->ReadRecordData(data.engagementDate) ||
                !serialization->ReadRecordData(data.marriageDate)) {
                return false;
            }

            npcData[newFormID] = data;
            allRegistered.insert(newFormID);
            AddToBucket(newFormID, data.status);
        }

        MARAS_LOG_INFO("Successfully loaded {} NPC relationship records", npcData.size());
        return true;
    }

    void NPCRelationshipManager::Revert() {
        MARAS_LOG_INFO("Reverting NPC relationship data");
        Clear();
    }

    // Debug/logging
    void NPCRelationshipManager::LogStatistics() const {
        MARAS_LOG_INFO("MARAS NPC Statistics:");
        MARAS_LOG_INFO("  Total Registered: {}", GetTotalRegisteredCount());
        MARAS_LOG_INFO("  Candidates: {}", GetCandidateCount());
        MARAS_LOG_INFO("  Engaged: {}", GetEngagedCount());
        MARAS_LOG_INFO("  Married: {}", GetMarriedCount());
        MARAS_LOG_INFO("  Divorced: {}", GetDivorcedCount());
        MARAS_LOG_INFO("  Jilted: {}", GetJiltedCount());
        MARAS_LOG_INFO("  Deceased: {}", GetDeceasedCount());
    }

    // Recalculate and update TT_MARAS.esp globals used by scripts (LoveInterestsCount and SpousesCount)
    void NPCRelationshipManager::RecalculateAndUpdateGlobals() {
        // Love interests = engaged + married (matches Papyrus behavior)
        std::int32_t loveInterests = static_cast<std::int32_t>(engaged.size() + married.size());
        std::int32_t spouses = static_cast<std::int32_t>(married.size());

        auto& cache = FormCache::GetSingleton();

        if (auto globalLove = cache.GetLoveInterestsCount()) {
            globalLove->value = static_cast<float>(loveInterests);
            MARAS_LOG_DEBUG("Updated LoveInterestsCount global to {}", loveInterests);
        } else {
            MARAS_LOG_WARN("Cannot update LoveInterestsCount global - form not found");
        }

        if (auto globalSpouses = cache.GetSpousesCount()) {
            globalSpouses->value = static_cast<float>(spouses);
            MARAS_LOG_DEBUG("Updated SpousesCount global to {}", spouses);
        } else {
            MARAS_LOG_WARN("Cannot update SpousesCount global - form not found");
        }
    }

    void NPCRelationshipManager::LogNPCDetails(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        if (!data) {
            MARAS_LOG_WARN("NPC {:08X} not found in MARAS data", npcFormID);
            return;
        }

        MARAS_LOG_INFO("NPC {:08X} Details:", npcFormID);
        MARAS_LOG_INFO("  Status: {}", static_cast<int>(data->status));
        MARAS_LOG_INFO("  Social Class: {}", static_cast<int>(data->socialClass));
        MARAS_LOG_INFO("  Skill Type: {}", static_cast<int>(data->skillType));
        MARAS_LOG_INFO("  Temperament: {}", static_cast<int>(data->temperament));
        MARAS_LOG_INFO("  Engagement Date: {}", data->engagementDate);
        MARAS_LOG_INFO("  Marriage Date: {}", data->marriageDate);
    }

    // ========================================
    // Override Management
    // ========================================

    bool NPCRelationshipManager::LoadOverridesFromFolder(const std::string& folderPath) {
        MARAS_LOG_INFO("Loading NPC type overrides from folder: {}", folderPath);

        npcOverrides.clear();
        bool success = Utils::JsonOverrideLoader::LoadOverridesFromFolder(folderPath, npcOverrides);

        if (success) {
            auto stats = Utils::JsonOverrideLoader::GetLastLoadStatistics();
            MARAS_LOG_INFO("Loaded {} override entries for NPCs", npcOverrides.size());
        }

        return success;
    }

    bool NPCRelationshipManager::HasSocialClassOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        return it != npcOverrides.end() && it->second.HasSocialClassOverride();
    }

    bool NPCRelationshipManager::HasSkillTypeOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        return it != npcOverrides.end() && it->second.HasSkillTypeOverride();
    }

    bool NPCRelationshipManager::HasTemperamentOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        return it != npcOverrides.end() && it->second.HasTemperamentOverride();
    }

    std::optional<std::string> NPCRelationshipManager::GetSocialClassOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        if (it != npcOverrides.end() && it->second.HasSocialClassOverride()) {
            return it->second.socialClass;
        }
        return std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetSkillTypeOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        if (it != npcOverrides.end() && it->second.HasSkillTypeOverride()) {
            return it->second.skillType;
        }
        return std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetTemperamentOverride(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        if (it != npcOverrides.end() && it->second.HasTemperamentOverride()) {
            return it->second.temperament;
        }
        return std::nullopt;
    }

    size_t NPCRelationshipManager::GetOverrideCount() const { return npcOverrides.size(); }

    Utils::JsonOverrideLoader::LoadStatistics NPCRelationshipManager::GetLastOverrideLoadStats() const {
        return Utils::JsonOverrideLoader::GetLastLoadStatistics();
    }

    // ========================================
    // Type Determination Methods
    // ========================================

    SocialClass NPCRelationshipManager::DetermineSocialClass(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSocialClass(npcFormID,
                                                       [this](RE::FormID id) { return GetSocialClassOverride(id); });
    }

    SkillType NPCRelationshipManager::DetermineSkillType(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSkillType(npcFormID,
                                                     [this](RE::FormID id) { return GetSkillTypeOverride(id); });
    }

    Temperament NPCRelationshipManager::DetermineTemperament(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineTemperament(
            npcFormID, [this](RE::FormID id) { return GetTemperamentOverride(id); },
            [this](RE::FormID id) { return GetSocialClass(id); }, [this](RE::FormID id) { return GetSkillType(id); });
    }

    // Removed determination helper implementations (moved to NPCTypeDeterminer.cpp)

    // ========================================
    // Faction Management Methods
    // ========================================

    bool NPCRelationshipManager::AddToFaction(RE::FormID npcFormID, RE::TESFaction* faction, std::int8_t rank) {
        if (!faction) {
            MARAS_LOG_ERROR("Faction is null for NPC {:08X}", npcFormID);
            return false;
        }

        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_ERROR("Cannot find actor for FormID {:08X}", npcFormID);
            return false;
        }

        // Add the NPC to the faction with specified rank
        actor->AddToFaction(faction, rank);

        MARAS_LOG_DEBUG("Added NPC {} ({:08X}) to faction {:08X} with rank {}", Utils::GetNPCName(npcFormID), npcFormID,
                        faction->GetFormID(), rank);

        return true;
    }

    bool NPCRelationshipManager::AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSocialClassFaction();
        if (!faction) {
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSkillTypeFaction();
        if (!faction) {
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseTemperamentFaction();
        if (!faction) {
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

    // ========================================
    // Status-Based Faction Management
    // ========================================

    void NPCRelationshipManager::ManageFactions(RE::FormID npcFormID, RelationshipStatus status) {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_WARN("Could not find actor {:08X} for faction management", npcFormID);
            return;
        }

        auto& cache = FormCache::GetSingleton();

        switch (status) {
            case RelationshipStatus::Candidate: {
                auto potentialFaction = cache.GetMarriagePotentialFaction();
                if (potentialFaction) {
                    Utils::AddToFaction(actor, potentialFaction, 0);
                }
                break;
            }

            case RelationshipStatus::Engaged: {
                auto askedFaction = cache.GetMarriageAskedFaction();
                auto courtingFaction = cache.GetCourtingFaction();
                if (askedFaction) Utils::AddToFaction(actor, askedFaction, 0);
                if (courtingFaction) Utils::AddToFaction(actor, courtingFaction, 0);
                break;
            }

            case RelationshipStatus::Married: {
                auto askedFaction = cache.GetMarriageAskedFaction();
                auto courtingFaction = cache.GetCourtingFaction();
                auto hirelingFaction = cache.GetPotentialHirelingFaction();
                auto marriedFaction = cache.GetMarriedFaction();
                auto playerFaction = cache.GetPlayerFaction();
                auto bedOwnershipFaction = cache.GetPlayerBedOwnershipFaction();

                // Remove from courting/engagement factions
                if (askedFaction) Utils::RemoveFromFaction(actor, askedFaction);
                if (courtingFaction) Utils::RemoveFromFaction(actor, courtingFaction);

                // Remove from hireling potential
                if (hirelingFaction) Utils::RemoveFromFaction(actor, hirelingFaction);

                // Add to marriage-related factions
                if (marriedFaction) Utils::AddToFaction(actor, marriedFaction, 0);
                if (playerFaction) Utils::AddToFaction(actor, playerFaction, 0);
                if (bedOwnershipFaction) Utils::AddToFaction(actor, bedOwnershipFaction, 0);

                MARAS_LOG_DEBUG("Added NPC {:08X} to married factions", npcFormID);
                break;
            }

            case RelationshipStatus::Divorced: {
                auto marriedFaction = cache.GetMarriedFaction();
                auto playerFaction = cache.GetPlayerFaction();
                auto bedOwnershipFaction = cache.GetPlayerBedOwnershipFaction();

                if (marriedFaction) Utils::RemoveFromFaction(actor, marriedFaction);
                if (playerFaction) Utils::RemoveFromFaction(actor, playerFaction);
                if (bedOwnershipFaction) Utils::RemoveFromFaction(actor, bedOwnershipFaction);

                MARAS_LOG_DEBUG("Removed NPC {:08X} from married factions", npcFormID);
                break;
            }

            case RelationshipStatus::Jilted: {
                auto askedFaction = cache.GetMarriageAskedFaction();
                auto courtingFaction = cache.GetCourtingFaction();

                if (askedFaction) Utils::RemoveFromFaction(actor, askedFaction);
                if (courtingFaction) Utils::RemoveFromFaction(actor, courtingFaction);

                MARAS_LOG_DEBUG("Removed NPC {:08X} from engagement factions", npcFormID);
                break;
            }

            case RelationshipStatus::Deceased: {
                // Deceased NPCs might need to remain in some factions, so no changes here
                // but could be extended if needed
                break;
            }
        }
    }

    // ========================================
    // Attribute setters
    // ========================================

    bool NPCRelationshipManager::SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass) {
        if (socialClass < 0 || socialClass > static_cast<std::int8_t>(SocialClass::Rulers)) {
            MARAS_LOG_WARN("SetSocialClass: invalid social class {} for {:08X}", socialClass, npcFormID);
            return false;
        }

        // Ensure NPC is registered so we have storage for it
        if (!EnsureRegistered(npcFormID)) return false;

        auto& data = npcData[npcFormID];
        data.socialClass = static_cast<SocialClass>(socialClass);

        // Update faction membership for social class
        AddToSocialClassFaction(npcFormID, socialClass);

        MARAS_LOG_INFO("Set social class for {:08X} to {}", npcFormID, Utils::SocialClassToString(data.socialClass));
        return true;
    }

    bool NPCRelationshipManager::SetSkillType(RE::FormID npcFormID, std::int8_t skillType) {
        if (skillType < 0 || skillType > static_cast<std::int8_t>(SkillType::Orator)) {
            MARAS_LOG_WARN("SetSkillType: invalid skill type {} for {:08X}", skillType, npcFormID);
            return false;
        }

        if (!EnsureRegistered(npcFormID)) return false;

        auto& data = npcData[npcFormID];
        data.skillType = static_cast<SkillType>(skillType);

        // Update faction membership for skill type
        AddToSkillTypeFaction(npcFormID, skillType);

        MARAS_LOG_INFO("Set skill type for {:08X} to {}", npcFormID, Utils::SkillTypeToString(data.skillType));
        return true;
    }

    bool NPCRelationshipManager::SetTemperament(RE::FormID npcFormID, std::int8_t temperament) {
        if (temperament < 0 || temperament > static_cast<std::int8_t>(Temperament::Independent)) {
            MARAS_LOG_WARN("SetTemperament: invalid temperament {} for {:08X}", temperament, npcFormID);
            return false;
        }

        if (!EnsureRegistered(npcFormID)) return false;

        auto& data = npcData[npcFormID];
        data.temperament = static_cast<Temperament>(temperament);

        // Update faction membership for temperament
        AddToTemperamentFaction(npcFormID, temperament);

        MARAS_LOG_INFO("Set temperament for {:08X} to {}", npcFormID, Utils::TemperamentToString(data.temperament));
        return true;
    }

    // Small DRY helper
    bool NPCRelationshipManager::EnsureRegistered(RE::FormID npcFormID) {
        if (IsRegistered(npcFormID)) {
            return true;
        }
        MARAS_LOG_WARN("NPC {:08X} is not registered - attempting to register as candidate", npcFormID);
        if (!RegisterAsCandidate(npcFormID)) {
            MARAS_LOG_WARN("Failed to auto-register NPC {:08X}", npcFormID);
            return false;
        }
        return true;
    }

}  // namespace MARAS
