#include "core/AffectionService.h"

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/Settings.h"

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

        // Partner count multipliers for decay (polyamory dampening)
        constexpr int kSpouseCountHigh = 7;
        constexpr int kSpouseCountMedium = 5;
        constexpr int kSpouseCountLow = 3;
        constexpr float kSpouseMultHigh = 0.5f;
        constexpr float kSpouseMultMedium = 0.6f;
        constexpr float kSpouseMultLow = 0.75f;

        // Relationship tier decay multipliers
        constexpr float kEngagedDecayMultiplier = 0.75f;
        constexpr float kDatingDecayMultiplier = 0.5f;
        constexpr float kLoverDecayMultiplier = 0.25f;

        // Open relationship decay multipliers by temperament
        constexpr float kOpenRelMultIndependent = 0.50f;
        constexpr float kOpenRelMultHumble = 0.75f;
        constexpr float kOpenRelMultProud = 0.90f;
        constexpr float kOpenRelMultRomantic = 1.15f;
        constexpr float kOpenRelMultJealous = 1.35f;

        // Known ongoing cheating decay multipliers by temperament
        constexpr float kCheatingMultIndependent = 1.25f;
        constexpr float kCheatingMultHumble = 1.35f;
        constexpr float kCheatingMultRomantic = 1.50f;
        constexpr float kCheatingMultProud = 1.75f;
        constexpr float kCheatingMultJealous = 2.00f;
    }  // namespace

    AffectionService& AffectionService::GetSingleton() {
        static AffectionService instance;
        return instance;
    }

    AffectionService::AffectionService() {
        // Default per-type daily clamp bounds
        minMaxByType_ = {
            {"intimacy", {-22, 22}},
            {"gift", {0, 15}},
            {"dialoguestarted", {0, 5}},
            {"slept", {0, 8}},
            {"promotion", {-24, 24}},
            {"loneliness", {-16, 6}},
        };
    }

    void AffectionService::AddAffection(FormID npcFormID, float amount, const std::string& type) {
        if (!ValidateActor(npcFormID, "AddAffection")) {
            return;
        }

        auto key = NormalizeType(type);
        dailyAffection_[npcFormID][key] += amount;

        // Only record positive, non-loneliness interactions as player attention
        if (amount > 0.0f && key != "loneliness") {
            RecordAffectionInteraction(npcFormID);
        }

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

    void AffectionService::SetPermanentAffection(FormID npcFormID, int amount, bool sendEvent) {
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

        // Send event if threshold changed and event sending requested
        if (sendEvent && oldThreshold != newThreshold) {
            SendAffectionChangeEvent(npcFormID, newThreshold, clamped - oldVal);
        }

        PrismaUIDataBridge::GetSingleton().OnAffectionChanged(npcFormID, clamped);
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
        for (auto& [npcFormID, typeMap] : dailyAffection_) {
            if (typeMap.empty()) continue;

            int original = GetPermanentAffection(npcFormID);
            int deltaTotal = CalculateTotalDailyDelta(typeMap);

            if (deltaTotal != 0) {
                int updated = ClampAffection(original + deltaTotal);
                SetPermanentAffection(npcFormID, updated);
                MARAS_LOG_INFO("Applied daily affection for NPC {:08X}: {} -> {} (delta {})", npcFormID, original,
                               updated, updated - original);
            }

            // Clear daily after applying
            typeMap.clear();
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

        constexpr std::uint32_t kMaxRecords = 100000;

        // Load permanent affection
        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) return false;
        if (count > kMaxRecords) {
            MARAS_LOG_ERROR("AffectionService::Load - corrupt permanent affection count {}", count);
            return false;
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldFormID = 0, newFormID = 0;
            int amount = 0;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ReadRecordData(amount)) return false;
            if (!serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("AffectionService::Load - could not resolve FormID {:08X}, skipping", oldFormID);
                continue;
            }

            // Skip dead or invalid actors
            auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(newFormID);
            if (!actor || actor->IsDead()) {
                MARAS_LOG_INFO("AffectionService::Load - skipping dead/invalid actor {:08X}", newFormID);
                continue;
            }

            int clamped = ClampAffection(amount);
            permanentAffection_[newFormID] = clamped;
            UpdateAffectionFaction(actor, newFormID, clamped);
        }

        MARAS_LOG_INFO("Loaded {} permanent affection records", permanentAffection_.size());

        // Load last affection days
        std::uint32_t dayCount = 0;
        if (!serialization->ReadRecordData(dayCount)) {
            MARAS_LOG_WARN("Could not read last affection day count, continuing without it");
            return true;  // Not fatal, might be old save
        }
        if (dayCount > kMaxRecords) {
            MARAS_LOG_ERROR("AffectionService::Load - corrupt last affection day count {}", dayCount);
            return false;
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

            // Skip dead or invalid actors
            auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(newFormID);
            if (!actor || actor->IsDead()) {
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
        lastAffectionDay_.clear();
        MARAS_LOG_INFO("Reverted affection service state");
    }

    void AffectionService::RemoveNPCData(FormID npcFormID) {
        permanentAffection_.erase(npcFormID);
        dailyAffection_.erase(npcFormID);
        lastAffectionDay_.erase(npcFormID);
        MARAS_LOG_DEBUG("Removed affection data for NPC {:08X}", npcFormID);
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

        // Married, engaged, dating, and lover partners of the player experience loneliness decay
        std::unordered_set<RE::FormID> uniquePartners;
        for (auto id : manager.GetAllMarried(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllEngaged(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllDating(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllLovers(kPlayerFormID)) uniquePartners.insert(id);

        const size_t activePartnerCount = uniquePartners.size();
        MARAS_LOG_INFO("Processing affection decay for {} player partner(s) on day change", activePartnerCount);

        for (auto npcFormID : uniquePartners) {
            float daysSince = GetDaysSinceLastAffection(npcFormID);

            // Only apply decay if no affection received for more than the threshold
            if (daysSince <= kDaysBeforeDecayStarts) {
                continue;
            }

            // Check if NPC is currently following (teammate/follower)
            auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
            bool isFollowing = actor && Utils::IsPlayerFollower(actor);

            float lonelinessAffection = CalculateLonelinessDecay(npcFormID, isFollowing, activePartnerCount);

            if (lonelinessAffection != 0.0f) {
                AddAffection(npcFormID, lonelinessAffection, "loneliness");
                MARAS_LOG_DEBUG("Applied loneliness affection {} to NPC {:08X} (days since: {}, following: {})",
                                lonelinessAffection, npcFormID, daysSince, isFollowing);
            }
        }

        // Commit all daily affection deltas (gifts, dialogue, intimacy, quests, loneliness) to permanent affection
        ApplyDailyAffectionsForAll();
    }

    void AffectionService::SetDecayMultiplier(float multiplier) {
        Settings::GetSingleton().affectionDecayMultiplier = std::clamp(multiplier, 0.0f, 2.0f);
        MARAS_LOG_INFO("Affection decay multiplier set to {}", Settings::GetSingleton().affectionDecayMultiplier);
    }

    float AffectionService::GetDecayMultiplier() const {
        return Settings::GetSingleton().affectionDecayMultiplier;
    }

    // Private helper methods

    RE::Actor* AffectionService::ValidateActor(FormID formID, const char* context) {
        auto actor = NPCIndex::GetSingleton().GetActorFromFormId(formID);
        if (!actor) {
            MARAS_LOG_ERROR("{}: Cannot find actor for FormID {:08X}", context, formID);
        }
        return actor;
    }

    std::string AffectionService::NormalizeType(const std::string& type) { return StringUtils::ToLowerStd(type); }

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
            Utils::AddToFaction(actor, faction, static_cast<std::int8_t>(affectionValue));
            MARAS_LOG_DEBUG("UpdateAffectionFaction: set faction {:08X} rank {} for NPC {:08X}", faction->GetFormID(),
                            affectionValue, npcFormID);
        }
    }

    void AffectionService::SendAffectionChangeEvent(FormID npcFormID, const std::string& threshold, int delta) {
        if (npcFormID == 0) return;
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        if (!npcForm) {
            MARAS_LOG_WARN("Could not find form for FormID {:08X} to send affection change event", npcFormID);
            return;
        }
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

    float AffectionService::CalculateLonelinessDecay(FormID npcFormID, bool isFollowing,
                                                     size_t activePartnerCount) const {
        float decayMult = Settings::GetSingleton().affectionDecayMultiplier;
        // If decay is disabled (multiplier = 0), return 0
        if (decayMult <= 0.0f) {
            return 0.0f;
        }

        // If following player, they gain affection instead of losing it
        if (isFollowing) {
            MARAS_LOG_INFO("NPC {:08X} is following player, applying positive loneliness affection", npcFormID);
            return kFollowingAffectionBonus;
        }

        // Get NPC's relationship data with the player
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto npcData = manager.GetNPCData(npcFormID, kPlayerFormID);
        if (!npcData) {
            return 0.0f;
        }

        float statusMult = 0.0f;
        switch (npcData->status) {
            case RelationshipStatus::Married:
                statusMult = 1.0f;
                break;
            case RelationshipStatus::Engaged:
                statusMult = kEngagedDecayMultiplier;
                break;
            case RelationshipStatus::Dating:
                statusMult = kDatingDecayMultiplier;
                break;
            case RelationshipStatus::Lover:
                statusMult = kLoverDecayMultiplier;
                break;
            default:
                // Only married, engaged, dating, and lover NPCs experience loneliness decay
                return 0.0f;
        }

        const Temperament temperament = manager.GetTemperament(npcFormID);

        // Base decay per day based on temperament
        float perDay = kDecayDefault;
        switch (temperament) {
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

        // Apply multiplier based on active partner count (all active partners: Married, Engaged, Dating, Lovers)
        float polyMult = 1.0f;
        if (activePartnerCount >= kSpouseCountHigh) {
            polyMult = kSpouseMultHigh;
        } else if (activePartnerCount >= kSpouseCountMedium) {
            polyMult = kSpouseMultMedium;
        } else if (activePartnerCount >= kSpouseCountLow) {
            polyMult = kSpouseMultLow;
        }

        // Open relationship multiplier (for main-tier relationships marked as open)
        float openRelMult = 1.0f;
        if (NPCRelationshipManager::IsMainTier(npcData->status) && npcData->isOpen) {
            switch (temperament) {
                case Temperament::Independent:
                    openRelMult = kOpenRelMultIndependent;
                    break;
                case Temperament::Humble:
                    openRelMult = kOpenRelMultHumble;
                    break;
                case Temperament::Proud:
                    openRelMult = kOpenRelMultProud;
                    break;
                case Temperament::Romantic:
                    openRelMult = kOpenRelMultRomantic;
                    break;
                case Temperament::Jealous:
                    openRelMult = kOpenRelMultJealous;
                    break;
                default:
                    openRelMult = 1.0f;
                    break;
            }
        }

        // Known ongoing cheating multiplier (either partner having an active discovered affair on this pair)
        bool hasKnownOngoingCheating = false;
        for (const auto& rec : npcData->cheatingRecords) {
            if (rec.affairStatus == AffairStatus::Ongoing && !rec.isSecret &&
                ((rec.cheaterID == npcFormID && rec.victimID == kPlayerFormID) ||
                 (rec.cheaterID == kPlayerFormID && rec.victimID == npcFormID))) {
                hasKnownOngoingCheating = true;
                break;
            }
        }

        float cheatingMult = 1.0f;
        if (hasKnownOngoingCheating) {
            switch (temperament) {
                case Temperament::Independent:
                    cheatingMult = kCheatingMultIndependent;
                    break;
                case Temperament::Humble:
                    cheatingMult = kCheatingMultHumble;
                    break;
                case Temperament::Romantic:
                    cheatingMult = kCheatingMultRomantic;
                    break;
                case Temperament::Proud:
                    cheatingMult = kCheatingMultProud;
                    break;
                case Temperament::Jealous:
                    cheatingMult = kCheatingMultJealous;
                    break;
                default:
                    cheatingMult = 1.0f;
                    break;
            }
        }

        float mult = polyMult * statusMult * openRelMult * cheatingMult * decayMult;

        // Calculate final decay (negative value)
        float decay = -(perDay * mult);

        MARAS_LOG_DEBUG(
            "Loneliness decay for NPC {:08X}: perDay={}, statusMult={}, polyMult={}, openRelMult={}, cheatingMult={}, "
            "decayMult={}, final={}",
            npcFormID, perDay, statusMult, polyMult, openRelMult, cheatingMult, decayMult, decay);

        return decay;
    }

    nlohmann::json AffectionService::DumpJson() const {
        nlohmann::json j;
        j["decayMultiplier"] = GetDecayMultiplier();

        nlohmann::json permArray = nlohmann::json::array();
        for (const auto& [formID, val] : permanentAffection_) {
            nlohmann::json a;
            a["formId"] = std::format("{:08X}", formID);
            a["name"] = ActorSvc::GetName(formID);
            a["value"] = val;
            auto dayIt = lastAffectionDay_.find(formID);
            a["lastAffectionDay"] = (dayIt != lastAffectionDay_.end()) ? dayIt->second : -1.0f;
            a["daysSinceLastAffection"] = GetDaysSinceLastAffection(formID);
            permArray.push_back(a);
        }
        j["permanentAffection"] = permArray;

        nlohmann::json dailyArray = nlohmann::json::array();
        for (const auto& [formID, typeMap] : dailyAffection_) {
            nlohmann::json d;
            d["formId"] = std::format("{:08X}", formID);
            d["name"] = ActorSvc::GetName(formID);
            nlohmann::json typesObj = nlohmann::json::object();
            for (const auto& [tName, amt] : typeMap) {
                typesObj[tName] = amt;
            }
            d["types"] = typesObj;
            dailyArray.push_back(d);
        }
        j["dailyAffection"] = dailyArray;

        nlohmann::json minMaxObj = nlohmann::json::object();
        for (const auto& [tName, range] : minMaxByType_) {
            minMaxObj[tName] = { {"min", range.first}, {"max", range.second} };
        }
        j["minMaxByType"] = minMaxObj;

        return j;
    }

}  // namespace MARAS
