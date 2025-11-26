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
        // Affection value bounds
        constexpr int kAffectionMin = 0;
        constexpr int kAffectionMax = 100;

        // Threshold values for affection states
        constexpr int kThresholdHappy = 75;
        constexpr int kThresholdContent = 50;
        constexpr int kThresholdTroubled = 25;

        // Multiplier values for affection levels
        constexpr float kMultiplierHappy = 1.25f;
        constexpr float kMultiplierContent = 1.0f;
        constexpr float kMultiplierTroubled = 0.25f;
        constexpr float kMultiplierEstranged = 0.0f;

        // Loneliness decay configuration
        constexpr float kDaysBeforeDecayStarts = 2.0f;
        constexpr float kFollowingAffectionBonus = 6.0f;
        constexpr float kDefaultDecayPerDay = 3.0f;
        constexpr float kNeverRecordedDaysSince = 999.0f;

        // Temperament-based decay rates
        constexpr float kDecayIndependent = 1.0f;
        constexpr float kDecayHumble = 2.0f;
        constexpr float kDecayRomantic = 4.0f;
        constexpr float kDecayDefault = 3.0f;

        // Spouse count multipliers for decay
        constexpr int kSpouseCountHigh = 7;
        constexpr int kSpouseCountMedium = 5;
        constexpr int kSpouseCountLow = 3;
        constexpr float kSpouseMultHigh = 0.5f;
        constexpr float kSpouseMultMedium = 0.6f;
        constexpr float kSpouseMultLow = 0.75f;
        constexpr float kEngagedDecayMultiplier = 0.5f;
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
        if (permanentAffection >= kThresholdHappy) {
            return kMultiplierHappy;
        }
        if (permanentAffection >= kThresholdContent) {
            return kMultiplierContent;
        }
        if (permanentAffection >= kThresholdTroubled) {
            return kMultiplierTroubled;
        }
        return kMultiplierEstranged;
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
            return kNeverRecordedDaysSince;
        }

        return currentDay - it->second;
    }

    void AffectionService::OnDayChanged() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto allNPCs = manager.GetAllRegisteredNPCs();

        MARAS_LOG_INFO("Processing affection decay for {} NPCs on day change", allNPCs.size());

        for (auto npcFormID : allNPCs) {
            float daysSince = GetDaysSinceLastAffection(npcFormID);

            // Only apply decay if no affection received for more than the threshold
            if (daysSince <= kDaysBeforeDecayStarts) {
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
        if (affectionValue >= kThresholdHappy) return "happy";
        if (affectionValue >= kThresholdContent) return "content";
        if (affectionValue >= kThresholdTroubled) return "troubled";
        return "estranged";
    }

    int AffectionService::ClampAffection(int value) { return std::clamp(value, kAffectionMin, kAffectionMax); }

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
            MARAS_LOG_INFO("NPC {:08X} is following player, applying positive loneliness affection", npcFormID);
            return kFollowingAffectionBonus;
        }

        // Get NPC's temperament
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto npcData = manager.GetNPCData(npcFormID);
        if (!npcData) {
            MARAS_LOG_WARN("No NPC data found for {:08X}, using default decay", npcFormID);
            return -kDefaultDecayPerDay;
        }
        if (npcData->status != RelationshipStatus::Married && npcData->status != RelationshipStatus::Engaged) {
            // Only married and engaged NPCs experience loneliness decay
            return 0.0f;
        }

        // Base decay per day based on temperament
        float perDay = kDecayDefault;
        switch (npcData->temperament) {
            case Temperament::Independent:
                perDay = kDecayIndependent;
                break;
            case Temperament::Humble:
                perDay = kDecayHumble;
                break;
            case Temperament::Romantic:
                perDay = kDecayRomantic;
                break;
            default:
                perDay = kDecayDefault;
                break;
        }

        // Apply multiplier based on spouse count
        int spouseCount = static_cast<int>(manager.GetAllMarried().size());
        float mult = 1.0f;
        if (spouseCount >= kSpouseCountHigh) {
            mult = kSpouseMultHigh;
        } else if (spouseCount >= kSpouseCountMedium) {
            mult = kSpouseMultMedium;
        } else if (spouseCount >= kSpouseCountLow) {
            mult = kSpouseMultLow;
        }

        if (npcData->status == RelationshipStatus::Engaged) {
            mult *= kEngagedDecayMultiplier;
        }

        // Calculate final decay (negative value)
        float decay = -(perDay * mult);

        MARAS_LOG_DEBUG("Loneliness decay for NPC {:08X}: perDay={}, mult={}, final={}", npcFormID, perDay, mult,
                        decay);

        return decay;
    }

}  // namespace MARAS
