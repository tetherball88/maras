#include "core/NPCRelationshipManager.h"

#include <spdlog/spdlog.h>

#include <chrono>

#include "core/Serialization.h"
#include "core/SpouseHierarchyManager.h"
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
        // Get the tracked NPC faction (FormID 0x7 from TT_MARAS.esp)
        // Load FormLists from TT_MARAS.esp
        auto trackedFaction = Utils::LookupForm<RE::TESFaction>(0x7, "TT_MARAS.esp");
        if (!trackedFaction) {
            MARAS_LOG_WARN("Could not find tracked NPC faction (0x7) for NPC {:08X}", npcFormID);
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
        Temperament temperament = DetermineTemperament(npcFormID);

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

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Auto-registered NPC {} ({:08X}) as candidate in {} microseconds (SC: {}, ST: {}, T: {})",
                       Utils::GetNPCName(npcFormID), npcFormID, duration.count(),
                       Utils::SocialClassToString(socialClass), Utils::SkillTypeToString(skillType),
                       Utils::TemperamentToString(temperament));

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
        if (!IsCandidate(npcFormID)) {
            MARAS_LOG_WARN("Cannot promote NPC {:08X} to engaged - not a candidate", npcFormID);
            return false;
        }

        RemoveFromBucket(npcFormID, RelationshipStatus::Candidate);
        AddToBucket(npcFormID, RelationshipStatus::Engaged);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Engaged;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Engaged);

        MARAS_LOG_INFO("Promoted NPC {:08X} to engaged", npcFormID);
        return true;
    }

    bool NPCRelationshipManager::PromoteToMarried(RE::FormID npcFormID) {
        if (!IsEngaged(npcFormID)) {
            MARAS_LOG_WARN("Cannot promote NPC {:08X} to married - not engaged", npcFormID);
            return false;
        }

        RemoveFromBucket(npcFormID, RelationshipStatus::Engaged);
        AddToBucket(npcFormID, RelationshipStatus::Married);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Married;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Married);

        // Notify spouse hierarchy manager
        SpouseHierarchyManager::GetSingleton().OnSpouseAdded(npcFormID);

        MARAS_LOG_INFO("Promoted NPC {:08X} to married", npcFormID);
        return true;
    }

    bool NPCRelationshipManager::DemoteToCandidate(RE::FormID npcFormID) {
        if (!IsEngaged(npcFormID)) {
            MARAS_LOG_WARN("Cannot demote NPC {:08X} to candidate - not engaged", npcFormID);
            return false;
        }

        RemoveFromBucket(npcFormID, RelationshipStatus::Engaged);
        AddToBucket(npcFormID, RelationshipStatus::Candidate);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Candidate;
        data.engagementDate = 0;  // Reset engagement date

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Candidate);

        MARAS_LOG_INFO("Demoted NPC {:08X} to candidate", npcFormID);
        return true;
    }

    bool NPCRelationshipManager::PromoteToDivorced(RE::FormID npcFormID) {
        if (!IsMarried(npcFormID)) {
            MARAS_LOG_WARN("Cannot promote NPC {:08X} to divorced - not married", npcFormID);
            return false;
        }

        RemoveFromBucket(npcFormID, RelationshipStatus::Married);
        AddToBucket(npcFormID, RelationshipStatus::Divorced);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Divorced;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Divorced);

        // Remove from spouse hierarchy if present
        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);

        MARAS_LOG_INFO("Promoted NPC {:08X} to divorced", npcFormID);
        return true;
    }

    bool NPCRelationshipManager::PromoteToJilted(RE::FormID npcFormID) {
        if (!IsEngaged(npcFormID)) {
            MARAS_LOG_WARN("Cannot promote NPC {:08X} to jilted - not engaged", npcFormID);
            return false;
        }

        RemoveFromBucket(npcFormID, RelationshipStatus::Engaged);
        AddToBucket(npcFormID, RelationshipStatus::Jilted);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Jilted;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Jilted);

        MARAS_LOG_INFO("Promoted NPC {:08X} to jilted", npcFormID);
        return true;
    }

    bool NPCRelationshipManager::PromoteToDeceased(RE::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("Cannot promote NPC {:08X} to deceased - not registered", npcFormID);
            return false;
        }

        auto currentStatus = GetRelationshipStatus(npcFormID);
        RemoveFromBucket(npcFormID, currentStatus);
        AddToBucket(npcFormID, RelationshipStatus::Deceased);

        auto& data = npcData[npcFormID];
        data.status = RelationshipStatus::Deceased;

        // Update tracked faction rank
        UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Deceased);

        // Remove from spouse hierarchy if present
        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);

        MARAS_LOG_INFO("Promoted NPC {:08X} to deceased", npcFormID);
        return true;
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
        return data ? data->status : RelationshipStatus::Candidate;
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

    // Date tracking
    bool NPCRelationshipManager::SetEngagementDate(RE::FormID npcFormID, uint32_t gameDay) {
        if (!IsRegistered(npcFormID)) {
            return false;
        }
        npcData[npcFormID].engagementDate = gameDay;
        return true;
    }

    bool NPCRelationshipManager::SetMarriageDate(RE::FormID npcFormID, uint32_t gameDay) {
        if (!IsRegistered(npcFormID)) {
            return false;
        }
        npcData[npcFormID].marriageDate = gameDay;
        return true;
    }

    uint32_t NPCRelationshipManager::GetEngagementDate(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->engagementDate : 0;
    }

    uint32_t NPCRelationshipManager::GetMarriageDate(RE::FormID npcFormID) const {
        auto data = GetNPCData(npcFormID);
        return data ? data->marriageDate : 0;
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
            RE::FormID oldFormID, newFormID;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("Could not resolve FormID {:08X}, skipping NPC", oldFormID);
                continue;
            }

            NPCRelationshipData data;
            data.formID = newFormID;

            std::uint8_t enumValue;

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.socialClass = static_cast<SocialClass>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.skillType = static_cast<SkillType>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.temperament = static_cast<Temperament>(enumValue);

            if (!serialization->ReadRecordData(enumValue)) return false;
            data.status = static_cast<RelationshipStatus>(enumValue);

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

    void NPCRelationshipManager::ClearOverrides() {
        npcOverrides.clear();
        MARAS_LOG_DEBUG("Cleared all NPC type overrides");
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
        // Check override first
        auto override = GetSocialClassOverride(npcFormID);
        if (override.has_value()) {
            auto result = Utils::StringToSocialClass(override.value());
            MARAS_LOG_DEBUG("Using social class override for {:08X}: {}", npcFormID, override.value());
            return result;
        }

        // Fallback to faction-based determination
        return DetermineSocialClassByFaction(npcFormID);
    }

    SkillType NPCRelationshipManager::DetermineSkillType(RE::FormID npcFormID) {
        // Check override first
        auto override = GetSkillTypeOverride(npcFormID);
        if (override.has_value()) {
            auto result = Utils::StringToSkillType(override.value());
            MARAS_LOG_DEBUG("Using skill type override for {:08X}: {}", npcFormID, override.value());
            return result;
        }

        // Try class-based determination
        auto skillType = DetermineSkillTypeByClass(npcFormID);
        if (skillType != SkillType::Warrior) {  // Warrior is our default fallback
            return skillType;
        }

        // Fallback to skill-based determination
        return DetermineSkillTypeBySkills(npcFormID);
    }

    Temperament NPCRelationshipManager::DetermineTemperament(RE::FormID npcFormID) {
        // Check override first
        auto override = GetTemperamentOverride(npcFormID);
        if (override.has_value()) {
            auto result = Utils::StringToTemperament(override.value());
            MARAS_LOG_DEBUG("Using temperament override for {:08X}: {}", npcFormID, override.value());
            return result;
        }

        // Fallback to matrix-based determination
        return DetermineTemperamentByMatrix(npcFormID);
    }

    // ========================================
    // Type Determination Helper Methods (Stubs for now)
    // ========================================

    SocialClass NPCRelationshipManager::DetermineSocialClassByFaction(RE::FormID npcFormID) const {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_ERROR("Cannot find actor for FormID {:08X}", npcFormID);
            return SocialClass::Working;
        }

        // Load FormLists from TT_MARAS.esp
        // FormList references from Papyrus script - use string-based lookup
        auto rulerFactions = Utils::LookupForm<RE::BGSListForm>(0xd75, "TT_MARAS.esp");
        auto nobleFactions = Utils::LookupForm<RE::BGSListForm>(0xd76, "TT_MARAS.esp");
        auto religiousFactions = Utils::LookupForm<RE::BGSListForm>(0xd77, "TT_MARAS.esp");
        auto wealthyFactions = Utils::LookupForm<RE::BGSListForm>(0xd71, "TT_MARAS.esp");
        auto middleFactions = Utils::LookupForm<RE::BGSListForm>(0x4, "TT_MARAS.esp");
        auto povertyFactions = Utils::LookupForm<RE::BGSListForm>(0xd74, "TT_MARAS.esp");
        auto outcastFactions = Utils::LookupForm<RE::BGSListForm>(0xd70, "TT_MARAS.esp");

        // Get actor's factions
        if (!actor->GetActorBase() || !actor->GetActorBase()->factions.size()) {
            MARAS_LOG_DEBUG("No factions found for actor {:08X}, defaulting to Working class", npcFormID);
            return SocialClass::Working;
        }

        auto& actorFactions = actor->GetActorBase()->factions;
        int maxClassIndex = static_cast<int>(SocialClass::Working);  // Default

        for (const auto& factionInfo : actorFactions) {
            if (!factionInfo.faction) continue;

            auto faction = factionInfo.faction;

            // Check faction membership in FormLists (highest priority wins)
            if (rulerFactions && rulerFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Rulers));
            } else if (nobleFactions && nobleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Nobles));
            } else if (religiousFactions && religiousFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Religious));
            } else if (wealthyFactions && wealthyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Wealthy));
            } else if (middleFactions && middleFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Middle));
            } else if (povertyFactions && povertyFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Poverty));
            } else if (outcastFactions && outcastFactions->HasForm(faction)) {
                maxClassIndex = std::max(maxClassIndex, static_cast<int>(SocialClass::Outcast));
            }
        }

        // TODO: Check for rich clothing keyword (from Papyrus: TTM_JData.GetClothingRichKeyword())
        // This would boost to wealthy class if worn
        // Skipping for now as it requires keyword lookup implementation

        MARAS_LOG_DEBUG("Determined social class for {:08X}: {}", npcFormID,
                        Utils::SocialClassToString(static_cast<SocialClass>(maxClassIndex)));

        return static_cast<SocialClass>(maxClassIndex);
    }

    SkillType NPCRelationshipManager::DetermineSkillTypeByClass(RE::FormID npcFormID) const {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor || !actor->GetActorBase()) {
            MARAS_LOG_WARN("Cannot find actor or actor base for FormID {:08X}", npcFormID);
            return SkillType::Warrior;
        }

        auto spouseClass = actor->GetActorBase()->npcClass;
        if (!spouseClass) {
            MARAS_LOG_DEBUG("No class found for actor {:08X}", npcFormID);
            return SkillType::Warrior;
        }

        // Load FormLists from TT_MARAS.esp (from Papyrus checkByClass function)
        // FormList references from Papyrus script - use string-based lookup
        auto oratorClasses = Utils::LookupForm<RE::BGSListForm>(0x13, "TT_MARAS.esp");
        auto rangerClasses = Utils::LookupForm<RE::BGSListForm>(0x10, "TT_MARAS.esp");
        auto rogueClasses = Utils::LookupForm<RE::BGSListForm>(0x11, "TT_MARAS.esp");
        auto craftsmanClasses = Utils::LookupForm<RE::BGSListForm>(0x12, "TT_MARAS.esp");
        auto mageClasses = Utils::LookupForm<RE::BGSListForm>(0xf, "TT_MARAS.esp");
        auto warriorClasses = Utils::LookupForm<RE::BGSListForm>(0xe, "TT_MARAS.esp");

        // Check class membership (order matches Papyrus priority)
        if (oratorClasses && oratorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Orator", npcFormID);
            return SkillType::Orator;
        }
        if (rangerClasses && rangerClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Ranger", npcFormID);
            return SkillType::Ranger;
        }
        if (rogueClasses && rogueClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Rogue", npcFormID);
            return SkillType::Rogue;
        }
        if (craftsmanClasses && craftsmanClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Craftsman", npcFormID);
            return SkillType::Craftsman;
        }
        if (mageClasses && mageClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Mage", npcFormID);
            return SkillType::Mage;
        }
        if (warriorClasses && warriorClasses->HasForm(spouseClass)) {
            MARAS_LOG_DEBUG("Determined skill type by class for {:08X}: Warrior", npcFormID);
            return SkillType::Warrior;
        }

        MARAS_LOG_DEBUG("No class match found for {:08X}, defaulting to Warrior", npcFormID);
        return SkillType::Warrior;  // Default fallback
    }

    SkillType NPCRelationshipManager::DetermineSkillTypeBySkills(RE::FormID npcFormID) const {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!actor) {
            MARAS_LOG_WARN("Cannot find actor for FormID {:08X} in skill determination", npcFormID);
            return SkillType::Warrior;
        }

        // Get skill values (mirroring Papyrus DetermineSkillBased function)
        std::array<float, 18> skills;

        // Use CommonLibSSE API - access ActorValueOwner interface
        auto actorValueOwner = actor->As<RE::ActorValueOwner>();
        if (!actorValueOwner) {
            MARAS_LOG_WARN("Actor {:08X} does not implement ActorValueOwner interface", npcFormID);
            return SkillType::Warrior;
        }

        // Use ActorValue enum values - CommonLibSSE constants
        skills[0] = actorValueOwner->GetActorValue(RE::ActorValue::kOneHanded);
        skills[1] = actorValueOwner->GetActorValue(RE::ActorValue::kTwoHanded);
        skills[2] = actorValueOwner->GetActorValue(RE::ActorValue::kArchery);  // Marksman in Papyrus
        skills[3] = actorValueOwner->GetActorValue(RE::ActorValue::kBlock);
        skills[4] = actorValueOwner->GetActorValue(RE::ActorValue::kSmithing);
        skills[5] = actorValueOwner->GetActorValue(RE::ActorValue::kHeavyArmor);
        skills[6] = actorValueOwner->GetActorValue(RE::ActorValue::kLightArmor);
        skills[7] = actorValueOwner->GetActorValue(RE::ActorValue::kPickpocket);
        skills[8] = actorValueOwner->GetActorValue(RE::ActorValue::kLockpicking);
        skills[9] = actorValueOwner->GetActorValue(RE::ActorValue::kSneak);
        skills[10] = actorValueOwner->GetActorValue(RE::ActorValue::kAlchemy);
        skills[11] = actorValueOwner->GetActorValue(RE::ActorValue::kSpeech);  // Speechcraft in Papyrus
        skills[12] = actorValueOwner->GetActorValue(RE::ActorValue::kAlteration);
        skills[13] = actorValueOwner->GetActorValue(RE::ActorValue::kConjuration);
        skills[14] = actorValueOwner->GetActorValue(RE::ActorValue::kDestruction);
        skills[15] = actorValueOwner->GetActorValue(RE::ActorValue::kIllusion);
        skills[16] = actorValueOwner->GetActorValue(RE::ActorValue::kRestoration);
        skills[17] = actorValueOwner->GetActorValue(RE::ActorValue::kEnchanting);

        // Find max skill (copying Papyrus logic exactly)
        int maxIndex = 0;
        float maxVal = skills[0];

        for (int i = 1; i < 18; ++i) {
            if (skills[i] > maxVal) {
                maxIndex = i;
                maxVal = skills[i];
            }
        }

        // Determine skill type based on highest skill (matches Papyrus logic)
        if (maxIndex == 0 || maxIndex == 1 || maxIndex == 3 || maxIndex == 5) {
            // OneHanded, TwoHanded, Block, HeavyArmor
            return SkillType::Warrior;
        } else if (maxIndex == 2) {
            // Marksman
            return SkillType::Ranger;
        } else if (maxIndex == 7 || maxIndex == 8 || maxIndex == 9) {
            // Pickpocket, Lockpicking, Sneak
            return SkillType::Rogue;
        } else if (maxIndex == 12 || maxIndex == 13 || maxIndex == 14 || maxIndex == 15 || maxIndex == 16) {
            // Alteration, Conjuration, Destruction, Illusion, Restoration
            return SkillType::Mage;
        } else if (maxIndex == 11) {
            // Speech/Speechcraft
            return SkillType::Orator;
        } else if (maxIndex == 4 || maxIndex == 10 || maxIndex == 17) {
            // Smithing, Alchemy, Enchanting
            return SkillType::Craftsman;
        } else if (maxIndex == 6) {
            // LightArmor - special case from Papyrus
            // if marksman is higher than lockpick and pickpocket - archer (ranger)
            // otherwise rogue
            if (skills[2] > skills[7] && skills[2] > skills[8]) {
                return SkillType::Ranger;
            } else {
                return SkillType::Rogue;
            }
        }

        // Default fallback
        return SkillType::Warrior;
    }

    Temperament NPCRelationshipManager::DetermineTemperamentByMatrix(RE::FormID npcFormID) const {
        // Get the current social class and skill type for this NPC
        // These should be set before calling this method
        SocialClass socialClass = GetSocialClass(npcFormID);
        SkillType skillType = GetSkillType(npcFormID);

        int socialClassIndex = static_cast<int>(socialClass);
        int skillTypeIndex = static_cast<int>(skillType);

        // Temperament matrix from Papyrus CheckTemperament function
        // Matrix layout:
        //                Warrior(0)  Mage(1)     Rogue(2)    Craftsman(3) Ranger(4)   Orator(5)
        // Outcast(0)      Independent Jealous     Jealous     Humble       Independent Romantic
        // Poverty(1)      Humble      Romantic    Jealous     Independent  Proud       Romantic
        // Working(2)      Proud       Humble      Romantic    Independent  Independent Proud
        // Middle(3)       Proud       Romantic    Independent Romantic     Humble      Jealous
        // Wealthy(4)      Proud       Jealous     Romantic    Romantic     Independent Jealous
        // Religious(5)    Independent Humble      Romantic    Jealous      Humble      Proud
        // Nobles(6)       Jealous     Romantic    Independent Proud        Humble      Proud
        // Rulers(7)       Proud       Independent Humble      Romantic     Jealous     Independent

        // Group conditions by temperament for cleaner logic (copied from Papyrus)

        // Independent: Outcast(Warrior,Ranger), Poverty(Craftsman), Working(Craftsman,Ranger), Middle(Rogue),
        // Wealthy(Ranger), Religious(Warrior), Nobles(Rogue), Rulers(Mage,Orator)
        if ((socialClassIndex == 0 && (skillTypeIndex == 0 || skillTypeIndex == 4)) ||
            (socialClassIndex == 1 && skillTypeIndex == 3) ||
            (socialClassIndex == 2 && (skillTypeIndex == 3 || skillTypeIndex == 4)) ||
            (socialClassIndex == 3 && skillTypeIndex == 2) || (socialClassIndex == 4 && skillTypeIndex == 4) ||
            (socialClassIndex == 5 && skillTypeIndex == 0) || (socialClassIndex == 6 && skillTypeIndex == 2) ||
            (socialClassIndex == 7 && (skillTypeIndex == 1 || skillTypeIndex == 5))) {
            return Temperament::Independent;
        }

        // Jealous: Outcast(Mage,Rogue), Poverty(Rogue), Middle(Orator), Wealthy(Mage,Orator), Religious(Craftsman),
        // Nobles(Warrior), Rulers(Ranger)
        else if ((socialClassIndex == 0 && (skillTypeIndex == 1 || skillTypeIndex == 2)) ||
                 (socialClassIndex == 1 && skillTypeIndex == 2) || (socialClassIndex == 3 && skillTypeIndex == 5) ||
                 (socialClassIndex == 4 && (skillTypeIndex == 1 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 5 && skillTypeIndex == 3) || (socialClassIndex == 6 && skillTypeIndex == 0) ||
                 (socialClassIndex == 7 && skillTypeIndex == 4)) {
            return Temperament::Jealous;
        }

        // Humble: Outcast(Craftsman), Poverty(Warrior), Working(Mage), Middle(Ranger), Religious(Mage,Ranger),
        // Nobles(Ranger), Rulers(Rogue)
        else if ((socialClassIndex == 0 && skillTypeIndex == 3) || (socialClassIndex == 1 && skillTypeIndex == 0) ||
                 (socialClassIndex == 2 && skillTypeIndex == 1) || (socialClassIndex == 3 && skillTypeIndex == 4) ||
                 (socialClassIndex == 5 && (skillTypeIndex == 1 || skillTypeIndex == 4)) ||
                 (socialClassIndex == 6 && skillTypeIndex == 4) || (socialClassIndex == 7 && skillTypeIndex == 2)) {
            return Temperament::Humble;
        }

        // Proud: Poverty(Ranger), Working(Warrior,Orator), Middle(Warrior), Wealthy(Warrior), Religious(Orator),
        // Nobles(Craftsman,Orator), Rulers(Warrior)
        else if ((socialClassIndex == 1 && skillTypeIndex == 4) ||
                 (socialClassIndex == 2 && (skillTypeIndex == 0 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 3 && skillTypeIndex == 0) || (socialClassIndex == 4 && skillTypeIndex == 0) ||
                 (socialClassIndex == 5 && skillTypeIndex == 5) ||
                 (socialClassIndex == 6 && (skillTypeIndex == 3 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 7 && skillTypeIndex == 0)) {
            return Temperament::Proud;
        }

        // Romantic: Outcast(Orator), Poverty(Mage,Orator), Working(Rogue), Middle(Mage,Craftsman),
        // Wealthy(Rogue,Craftsman), Religious(Rogue), Nobles(Mage), Rulers(Craftsman)
        else if ((socialClassIndex == 0 && skillTypeIndex == 5) ||
                 (socialClassIndex == 1 && (skillTypeIndex == 1 || skillTypeIndex == 5)) ||
                 (socialClassIndex == 2 && skillTypeIndex == 2) ||
                 (socialClassIndex == 3 && (skillTypeIndex == 1 || skillTypeIndex == 3)) ||
                 (socialClassIndex == 4 && (skillTypeIndex == 2 || skillTypeIndex == 3)) ||
                 (socialClassIndex == 5 && skillTypeIndex == 2) || (socialClassIndex == 6 && skillTypeIndex == 1) ||
                 (socialClassIndex == 7 && skillTypeIndex == 3)) {
            return Temperament::Romantic;
        }

        // Default fallback (should not normally be reached)
        else {
            MARAS_LOG_DEBUG("Temperament matrix fallback for NPC {:08X} (SC:{}, ST:{}), using Independent", npcFormID,
                            socialClassIndex, skillTypeIndex);
            return Temperament::Independent;
        }
    }

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
        // Get the faction using the form ID from TTM_JData.psc: GetSpouseSocialClassFaction() -> 0x66
        auto faction = Utils::LookupForm<RE::TESFaction>(0x66, "TT_MARAS.esp");
        if (!faction) {
            MARAS_LOG_ERROR("Cannot find SpouseSocialClassFaction (0x66) in TT_MARAS.esp");
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank) {
        // Get the faction using the form ID from TTM_JData.psc: GetSpouseSkillTypeFaction() -> 0x4e
        auto faction = Utils::LookupForm<RE::TESFaction>(0x4e, "TT_MARAS.esp");
        if (!faction) {
            MARAS_LOG_ERROR("Cannot find SpouseSkillTypeFaction (0x4e) in TT_MARAS.esp");
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank) {
        // Get the faction using the form ID from TTM_JData.psc: GetSpouseTemperamentFaction() -> 0x118
        auto faction = Utils::LookupForm<RE::TESFaction>(0x118, "TT_MARAS.esp");
        if (!faction) {
            MARAS_LOG_ERROR("Cannot find SpouseTemperamentFaction (0x118) in TT_MARAS.esp");
            return false;
        }

        return AddToFaction(npcFormID, faction, rank);
    }

}  // namespace MARAS
