#include "core/AffectionService.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    // Constants
    namespace {
        constexpr int AFFECTION_MIN = 0;
        constexpr int AFFECTION_MAX = 100;
        constexpr int THRESHOLD_HAPPY = 75;
        constexpr int THRESHOLD_CONTENT = 50;
        constexpr int THRESHOLD_TROUBLED = 25;
    }  // namespace

    AffectionService& AffectionService::GetSingleton() {
        static AffectionService instance;
        return instance;
    }

    void AffectionService::AddAffection(FormID npcFormID, float amount, const std::string& type) {
        if (!ValidateActor(npcFormID, "AddAffection")) {
            return;
        }

        auto key = NormalizeType(type);
        dailyAffection_[npcFormID][key] += amount;

        // Record that this NPC received affection today
        RecordAffectionInteraction(npcFormID);

        MARAS_LOG_DEBUG("AddAffection: NPC {:08X} += {} ({}) (daily now {})", npcFormID, amount, key,
                        dailyAffection_[npcFormID][key]);
    }

    float AffectionService::GetDailyAffection(FormID npcFormID, const std::string& type) const {
        auto key = NormalizeType(type);
        auto it = dailyAffection_.find(npcFormID);
        if (it == dailyAffection_.end()) return 0.0f;
        auto it2 = it->second.find(key);
        return (it2 == it->second.end()) ? 0.0f : it2->second;
    }

    int AffectionService::GetPermanentAffection(FormID npcFormID) const {
        auto it = permanentAffection_.find(npcFormID);
        return (it == permanentAffection_.end()) ? 0 : it->second;
    }

    void AffectionService::SetPermanentAffection(FormID npcFormID, int amount) {
        auto actor = ValidateActor(npcFormID, "SetPermanentAffection");
        if (!actor) {
            return;
        }

        int clamped = ClampAffection(amount);
        int oldVal = GetPermanentAffection(npcFormID);

        std::string oldThreshold = GetAffectionThreshold(oldVal);
        std::string newThreshold = GetAffectionThreshold(clamped);

        // Store new value
        permanentAffection_[npcFormID] = clamped;
        MARAS_LOG_INFO("SetPermanentAffection: NPC {:08X} = {} (clamped from {})", npcFormID, clamped, amount);

        // Update faction rank
        UpdateAffectionFaction(actor, npcFormID, clamped);

        // Send event if threshold changed
        if (oldThreshold != newThreshold) {
            SendAffectionChangeEvent(npcFormID, newThreshold, clamped - oldVal);
        }
    }

    void AffectionService::SetAffectionMinMax(const std::string& type, int minVal, int maxVal) {
        auto key = NormalizeType(type);
        minMaxByType_[key] = {minVal, maxVal};
        MARAS_LOG_INFO("SetAffectionMinMax: {} -> [{}, {}]", key, minVal, maxVal);
    }

    bool AffectionService::HasMinMaxForType(const std::string& type) const {
        auto key = NormalizeType(type);
        return minMaxByType_.find(key) != minMaxByType_.end();
    }

    std::pair<int, int> AffectionService::GetMinMaxForType(const std::string& type) const {
        auto key = NormalizeType(type);
        auto it = minMaxByType_.find(key);
        if (it == minMaxByType_.end()) return {INT_MIN, INT_MAX};
        return it->second;
    }

    void AffectionService::ApplyDailyAffectionsForAll() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto all = manager.GetAllRegisteredNPCs();

        for (auto npcFormID : all) {
            auto it = dailyAffection_.find(npcFormID);
            if (it == dailyAffection_.end()) continue;

            int original = GetPermanentAffection(npcFormID);
            int deltaTotal = CalculateTotalDailyDelta(it->second);

            if (deltaTotal != 0) {
                int updated = ClampAffection(original + deltaTotal);
                SetPermanentAffection(npcFormID, updated);
                MARAS_LOG_INFO("Applied daily affection for NPC {:08X}: {} -> {} (delta {})", npcFormID, original,
                               updated, updated - original);
            }

            // Clear daily after applying
            it->second.clear();
        }
    }

    bool AffectionService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Write permanent affection count
        std::uint32_t count = static_cast<std::uint32_t>(permanentAffection_.size());
        if (!serialization->WriteRecordData(count)) return false;

        for (const auto& [formID, amount] : permanentAffection_) {
            if (!serialization->WriteRecordData(formID) || !serialization->WriteRecordData(amount)) return false;
        }

        MARAS_LOG_INFO("Saved {} permanent affection records", count);

        // Write last affection day count
        std::uint32_t dayCount = static_cast<std::uint32_t>(lastAffectionDay_.size());
        if (!serialization->WriteRecordData(dayCount)) return false;

        for (const auto& [formID, day] : lastAffectionDay_) {
            if (!serialization->WriteRecordData(formID) || !serialization->WriteRecordData(day)) return false;
        }

        MARAS_LOG_INFO("Saved {} last affection day records", dayCount);

        return true;
    }

    bool AffectionService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        Revert();

        // Load permanent affection
        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) return false;

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldFormID = 0, newFormID = 0;
            int amount = 0;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ReadRecordData(amount)) return false;
            if (!serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("AffectionService::Load - could not resolve FormID {:08X}, skipping", oldFormID);
                continue;
            }
            permanentAffection_[newFormID] = amount;
        }

        MARAS_LOG_INFO("Loaded {} permanent affection records", permanentAffection_.size());

        // Load last affection days
        std::uint32_t dayCount = 0;
        if (!serialization->ReadRecordData(dayCount)) {
            MARAS_LOG_WARN("Could not read last affection day count, continuing without it");
            return true;  // Not fatal, might be old save
        }

        for (std::uint32_t i = 0; i < dayCount; ++i) {
            RE::FormID oldFormID = 0, newFormID = 0;
            float day = 0.0f;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ReadRecordData(day)) {
                MARAS_LOG_WARN("Could not read last affection day record {}", i);
                continue;
            }
            if (!serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("AffectionService::Load - could not resolve FormID {:08X} for last affection day",
                               oldFormID);
                continue;
            }
            lastAffectionDay_[newFormID] = day;
        }

        MARAS_LOG_INFO("Loaded {} last affection day records", lastAffectionDay_.size());

        return true;
    }

    void AffectionService::Revert() {
        permanentAffection_.clear();
        dailyAffection_.clear();
        minMaxByType_.clear();
        lastAffectionDay_.clear();
        MARAS_LOG_INFO("Reverted affection service state");
    }

    float AffectionService::GetMultiplierForValue(int permanentAffection) const {
        if (permanentAffection >= 75) {
            return 1.25f;
        }
        if (permanentAffection >= 50) {
            return 1.0f;
        }
        if (permanentAffection >= 25) {
            return 0.25f;
        }
        return 0.0f;
    }

    float AffectionService::GetMultiplierForNPC(FormID npcFormID) const {
        int perm = GetPermanentAffection(npcFormID);
        return GetMultiplierForValue(perm);
    }

    void AffectionService::RecordAffectionInteraction(FormID npcFormID) {
        auto calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            lastAffectionDay_[npcFormID] = calendar->GetDaysPassed();
        }
    }

    float AffectionService::GetDaysSinceLastAffection(FormID npcFormID) const {
        auto calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;

        float currentDay = calendar->GetDaysPassed();
        auto it = lastAffectionDay_.find(npcFormID);
        if (it == lastAffectionDay_.end()) {
            // Never recorded, assume it's been a while
            return 999.0f;
        }

        return currentDay - it->second;
    }

    void AffectionService::OnDayChanged() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto allNPCs = manager.GetAllRegisteredNPCs();

        MARAS_LOG_INFO("Processing affection decay for {} NPCs on day change", allNPCs.size());

        for (auto npcFormID : allNPCs) {
            float daysSince = GetDaysSinceLastAffection(npcFormID);

            // Only apply decay if no affection received for more than 2 days
            if (daysSince <= 2.0f) {
                continue;
            }

            // Check if NPC is currently following (teammate)
            auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
            bool isFollowing = actor && actor->IsPlayerTeammate();

            float lonelinessAffection = CalculateLonelinessDecay(npcFormID, isFollowing);

            if (lonelinessAffection != 0.0f) {
                AddAffection(npcFormID, lonelinessAffection, "loneliness");
                MARAS_LOG_DEBUG("Applied loneliness affection {} to NPC {:08X} (days since: {}, following: {})",
                                lonelinessAffection, npcFormID, daysSince, isFollowing);
            }
        }
    }

    // Private helper methods

    RE::Actor* AffectionService::ValidateActor(FormID formID, const char* context) {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            MARAS_LOG_ERROR("{}: Cannot find actor for FormID {:08X}", context, formID);
        }
        return actor;
    }

    std::string AffectionService::NormalizeType(const std::string& type) { return Utils::ToLower(type); }

    std::string AffectionService::GetAffectionThreshold(int affectionValue) {
        if (affectionValue >= THRESHOLD_HAPPY) return "happy";
        if (affectionValue >= THRESHOLD_CONTENT) return "content";
        if (affectionValue >= THRESHOLD_TROUBLED) return "troubled";
        return "estranged";
    }

    int AffectionService::ClampAffection(int value) { return std::clamp(value, AFFECTION_MIN, AFFECTION_MAX); }

    void AffectionService::UpdateAffectionFaction(RE::Actor* actor, FormID npcFormID, int affectionValue) {
        auto faction = FormCache::GetSingleton().GetAffectionFaction();
        if (faction) {
            actor->AddToFaction(faction, static_cast<std::int8_t>(affectionValue));
            MARAS_LOG_DEBUG("UpdateAffectionFaction: set faction {:08X} rank {} for NPC {:08X}", faction->GetFormID(),
                            affectionValue, npcFormID);
        }
    }

    void AffectionService::SendAffectionChangeEvent(FormID npcFormID, const std::string& threshold, int delta) {
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        auto eventSource = SKSE::GetModCallbackEventSource();
        if (eventSource) {
            float diff = static_cast<float>(delta);
            SKSE::ModCallbackEvent modEvent("maras_change_affection", threshold, diff, npcForm);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_change_affection event for NPC {:08X}: threshold={} diff={}", npcFormID,
                           threshold, diff);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send affection change event");
        }
    }

    int AffectionService::CalculateTotalDailyDelta(const std::unordered_map<std::string, float>& dailyByType) const {
        int deltaTotal = 0;

        for (const auto& [type, value] : dailyByType) {
            int applied = static_cast<int>(std::lround(value));

            // Clamp if min/max configured for this type
            auto mmIt = minMaxByType_.find(type);
            if (mmIt != minMaxByType_.end()) {
                applied = std::clamp(applied, mmIt->second.first, mmIt->second.second);
            }

            deltaTotal += applied;
            MARAS_LOG_DEBUG("CalculateTotalDailyDelta: type {} daily {} -> applied {}", type, value, applied);
        }

        return deltaTotal;
    }

    float AffectionService::CalculateLonelinessDecay(FormID npcFormID, bool isFollowing) const {
        // If following player, they gain affection instead of losing it
        if (isFollowing) {
            SKSE::log::info("NPC {:08X} is following player, applying positive loneliness affection", npcFormID);
            return 6.0f;
        }

        // Get NPC's temperament
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto npcData = manager.GetNPCData(npcFormID);
        if (npcData->status != RelationshipStatus::Married && npcData->status != RelationshipStatus::Engaged) {
            // Only married and engaged NPCs experience loneliness decay
            return 0.0f;
        }
        if (!npcData) {
            MARAS_LOG_WARN("No NPC data found for {:08X}, using default decay", npcFormID);
            return -3.0f;  // Default decay
        }

        // Base decay per day based on temperament
        float perDay = 3.0f;  // Default
        switch (npcData->temperament) {
            case Temperament::Independent:
                perDay = 1.0f;
                break;
            case Temperament::Humble:
                perDay = 2.0f;
                break;
            case Temperament::Romantic:
                perDay = 4.0f;
                break;
            default:
                perDay = 3.0f;
                break;
        }

        // Apply multiplier based on spouse count
        int spouseCount = static_cast<int>(manager.GetAllMarried().size());
        float mult = 1.0f;
        if (spouseCount >= 7) {
            mult = 0.5f;
        } else if (spouseCount >= 5) {
            mult = 0.6f;
        } else if (spouseCount >= 3) {
            mult = 0.75f;
        }

        if (npcData->status == RelationshipStatus::Engaged) {
            mult *= 0.5f;  // Engaged NPCs have reduced loneliness decay
        }

        // Calculate final decay (negative value)
        float decay = -(perDay * mult);

        MARAS_LOG_DEBUG("Loneliness decay for NPC {:08X}: perDay={}, mult={}, final={}", npcFormID, perDay, mult,
                        decay);

        return decay;
    }

}  // namespace MARAS
