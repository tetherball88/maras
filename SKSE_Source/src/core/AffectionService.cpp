#include "core/AffectionService.h"

#include "core/BuffReconciler.h"
#include "core/FactionReconciler.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/SessionEpoch.h"
#include "maras/state/WorldStore.h"
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
        // kDaysBeforeDecayStarts replaced by Settings::GetLonelinessDecayStartDays()
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
        ResetDefaultMinMax();
    }

    void AffectionService::ResetDefaultMinMax() {
        // Default per-type daily clamp bounds
        std::unique_lock lock(minMaxMutex_);
        minMaxByType_ = {
            {"intimacy", {-22, 22}},
            {"gift", {0, 15}},
            {"dialoguestarted", {0, 5}},
            {"slept", {0, 8}},
            {"promotion", {-24, 24}},
            {"loneliness", {-16, 6}},
            {"family", {0, 6}},
        };
    }

    void AffectionService::AddAffection(FormID npcFormID, float amount, const std::string& type) {
        if (!ValidateActor(npcFormID, "AddAffection")) {
            return;
        }

        const auto key = NormalizeType(type);
        std::optional<float> currentDay;
        if (amount > 0.0f) {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                currentDay = calendar->GetDaysPassed();
            }
        }

        float newDaily = 0.0f;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& aff = ws.affection.Write();
            const State::ActorId id{ npcFormID };
            newDaily = aff.GetDailyAccumulation(id, key) + amount;
            aff.SetDailyAccumulation(id, key, newDaily);
            if (currentDay.has_value()) {
                aff.SetLastInteractionDay(id, *currentDay);
            }
        });

        MARAS_LOG_DEBUG("AddAffection: NPC {:08X} += {} ({}) (daily now {})", npcFormID, amount, key, newDaily);
    }

    float AffectionService::GetDailyAffection(FormID npcFormID, const std::string& type) const {
        const auto key = NormalizeType(type);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->affection->GetDailyAccumulation(State::ActorId{ npcFormID }, key);
    }

    int AffectionService::GetPermanentAffection(FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->affection->GetPermanent(State::ActorId{ npcFormID }, 0);
    }

    void AffectionService::SetPermanentAffection(FormID npcFormID, int amount, bool sendEvent) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID || IsSyntheticChildID(npcFormID)) {
            return;
        }
        auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        if (!actor && !ActorSvc::IsValid(npcFormID)) {
            MARAS_LOG_ERROR("SetPermanentAffection: Cannot find valid actor/base for FormID {:08X}", npcFormID);
            return;
        }

        const int clamped = ClampAffection(amount);
        int oldVal = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ActorId id{ npcFormID };
            oldVal = ws.affection.Read().GetPermanent(id, 0);
            ws.affection.Write().SetPermanent(id, clamped);
        });

        const std::string oldThreshold = GetAffectionThreshold(oldVal);
        const std::string newThreshold = GetAffectionThreshold(clamped);

        MARAS_LOG_INFO("SetPermanentAffection: NPC {:08X} = {} (clamped from {})", npcFormID, clamped, amount);

        UpdateAffectionFaction(actor, npcFormID, clamped);
        if (clamped != oldVal &&
            State::WorldStore::GetSingleton().Snapshot()->relationships->GetStatus(
                State::ActorId{ kPlayerFormID }, State::ActorId{ npcFormID }) == State::RelationshipStatus::Married) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }

        // Send event if threshold changed and event sending requested
        if (sendEvent && oldThreshold != newThreshold) {
            SendAffectionChangeEvent(npcFormID, newThreshold, clamped - oldVal);
        }

        PrismaUIDataBridge::GetSingleton().OnAffectionChanged(npcFormID, clamped);
    }

    void AffectionService::SetAffectionMinMax(const std::string& type, int minVal, int maxVal) {
        auto key = NormalizeType(type);
        {
            std::unique_lock lock(minMaxMutex_);
            minMaxByType_[key] = {minVal, maxVal};
        }
        MARAS_LOG_INFO("SetAffectionMinMax: {} -> [{}, {}]", key, minVal, maxVal);
    }

    bool AffectionService::HasMinMaxForType(const std::string& type) const {
        auto key = NormalizeType(type);
        std::shared_lock lock(minMaxMutex_);
        return minMaxByType_.find(key) != minMaxByType_.end();
    }

    std::pair<int, int> AffectionService::GetMinMaxForType(const std::string& type) const {
        auto key = NormalizeType(type);
        std::shared_lock lock(minMaxMutex_);
        auto it = minMaxByType_.find(key);
        if (it == minMaxByType_.end()) return {INT_MIN, INT_MAX};
        return it->second;
    }

    void AffectionService::ApplyDailyAffectionsForAll() {
        struct PendingDaily {
            FormID npcFormID{ 0 };
            RE::Actor* actor{ nullptr };
            int deltaTotal{ 0 };
            bool valid{ false };
        };

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& allDaily = snap->affection->AllDaily();
        if (allDaily.empty()) {
            return;
        }

        std::vector<PendingDaily> pending;
        pending.reserve(allDaily.size());
        for (const auto& [actorId, typeMap] : allDaily) {
            if (typeMap.empty()) continue;
            const FormID npcFormID = actorId.raw();
            const int deltaTotal = CalculateTotalDailyDelta(typeMap);
            RE::Actor* actor = nullptr;
            bool valid = false;
            if (deltaTotal != 0 && npcFormID != 0 && npcFormID != kPlayerFormID && !IsSyntheticChildID(npcFormID)) {
                actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
                valid = actor != nullptr || ActorSvc::IsValid(npcFormID);
                if (!valid) {
                    MARAS_LOG_ERROR("SetPermanentAffection: Cannot find valid actor/base for FormID {:08X}", npcFormID);
                }
            }
            pending.push_back(PendingDaily{ npcFormID, actor, deltaTotal, valid });
        }

        struct AppliedChange {
            FormID npcFormID{ 0 };
            RE::Actor* actor{ nullptr };
            int original{ 0 };
            int unclamped{ 0 };
            int updated{ 0 };
        };
        std::vector<AppliedChange> applied;
        applied.reserve(pending.size());

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& aff = ws.affection.Write();
            for (const auto& item : pending) {
                if (item.valid && item.deltaTotal != 0) {
                    const State::ActorId id{ item.npcFormID };
                    const int original = aff.GetPermanent(id, 0);
                    const int unclamped = original + item.deltaTotal;
                    const int updated = ClampAffection(unclamped);
                    aff.SetPermanent(id, updated);
                    applied.push_back(AppliedChange{ item.npcFormID, item.actor, original, unclamped, updated });
                }
            }
            aff.ClearDailyAccumulation();
        });

        const auto updatedSnap = State::WorldStore::GetSingleton().Snapshot();
        bool anyMarriedSpouseChanged = false;
        for (const auto& chg : applied) {
            const std::string oldThreshold = GetAffectionThreshold(chg.original);
            const std::string newThreshold = GetAffectionThreshold(chg.updated);
            MARAS_LOG_INFO("SetPermanentAffection: NPC {:08X} = {} (clamped from {})", chg.npcFormID, chg.updated, chg.unclamped);

            UpdateAffectionFaction(chg.actor, chg.npcFormID, chg.updated);
            if (chg.updated != chg.original &&
                updatedSnap->relationships->GetStatus(State::ActorId{ kPlayerFormID }, State::ActorId{ chg.npcFormID }) ==
                    State::RelationshipStatus::Married) {
                anyMarriedSpouseChanged = true;
            }
            if (oldThreshold != newThreshold) {
                SendAffectionChangeEvent(chg.npcFormID, newThreshold, chg.updated - chg.original);
            }
            PrismaUIDataBridge::GetSingleton().OnAffectionChanged(chg.npcFormID, chg.updated);

            MARAS_LOG_INFO("Applied daily affection for NPC {:08X}: {} -> {} (delta {})", chg.npcFormID, chg.original,
                           chg.updated, chg.updated - chg.original);
        }
        if (anyMarriedSpouseChanged) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
    }

    void AffectionService::FinalizeLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<FormID> loadedActors;
        loadedActors.reserve(snap->affection->AllPermanent().size());
        for (const auto& [actorId, val] : snap->affection->AllPermanent()) {
            (void)val;
            const FormID id = actorId.raw();
            if (id == 0 || IsSyntheticChildID(id)) {
                continue;
            }
            auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(id);
            if (!actor) {
                actor = RE::TESForm::LookupByID<RE::Actor>(id);
            }
            if (actor && !actor->IsDead()) {
                loadedActors.push_back(id);
            }
        }
        if (!loadedActors.empty()) {
            FactionReconciler::GetSingleton().ReconcileActors(loadedActors);
        }
        MARAS_LOG_INFO("AffectionService::FinalizeLoad - synchronized affection faction rank for {} loaded actor(s)",
                       loadedActors.size());
    }

    void AffectionService::Revert() {
        ResetDefaultMinMax();
        MARAS_LOG_INFO("Reverted affection service state");
    }

    void AffectionService::RemoveNPCData(FormID npcFormID) {
        if (npcFormID == 0) return;
        State::WorldStore::GetSingleton().Modify([npcFormID](State::WorldState& ws) {
            ws.affection.Write().EraseActor(State::ActorId{ npcFormID });
        });
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
        if (npcFormID == 0) return;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            const float currentDay = calendar->GetDaysPassed();
            State::WorldStore::GetSingleton().Modify([npcFormID, currentDay](State::WorldState& ws) {
                ws.affection.Write().SetLastInteractionDay(State::ActorId{ npcFormID }, currentDay);
            });
        }
    }

    float AffectionService::GetDaysSinceLastAffection(FormID npcFormID) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return 0.0f;

        const float currentDay = calendar->GetDaysPassed();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto dayOpt = snap->affection->GetLastInteractionDay(State::ActorId{ npcFormID });
        if (!dayOpt.has_value()) {
            // Never recorded, assume it's been a while
            return kNeverRecordedDaysSince;
        }

        return currentDay - *dayOpt;
    }

    void AffectionService::OnDayChanged() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        const float lonelinessStartDays = Settings::GetSingleton().Snapshot()->GetLonelinessDecayStartDays();

        // Married, engaged, dating, and lover partners of the player experience loneliness decay
        std::unordered_set<RE::FormID> uniquePartners;
        for (auto id : manager.GetAllMarried(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllEngaged(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllDating(kPlayerFormID)) uniquePartners.insert(id);
        for (auto id : manager.GetAllLovers(kPlayerFormID)) uniquePartners.insert(id);

        const size_t activePartnerCount = uniquePartners.size();
        MARAS_LOG_INFO("Processing affection decay for {} player partner(s) on day change", activePartnerCount);

        for (auto npcFormID : uniquePartners) {
            // Check if NPC is currently following (teammate/follower)
            auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
            bool isFollowing = actor && Utils::IsPlayerFollower(actor);

            float daysSince = GetDaysSinceLastAffection(npcFormID);

            // Only apply decay if not following and no affection received for more than the threshold
            if (!isFollowing && daysSince <= lonelinessStartDays) {
                continue;
            }

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
        const float clamped = std::clamp(multiplier, 0.0f, 2.0f);
        State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
            ws.settings.Write().affectionDecayMultiplier = clamped;
        });
        MARAS_LOG_INFO("Affection decay multiplier set to {}", clamped);
    }

    float AffectionService::GetDecayMultiplier() const {
        return Settings::GetSingleton().Snapshot()->affectionDecayMultiplier;
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

    void AffectionService::UpdateAffectionFaction(RE::Actor* actor, FormID npcFormID, int /*affectionValue*/) {
        if (actor) {
            FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
        } else if (npcFormID != 0) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        }
    }

    void AffectionService::SendAffectionChangeEvent(FormID npcFormID, const std::string& threshold, int delta) {
        if (npcFormID == 0) return;
        auto npcForm = RE::TESForm::LookupByID(npcFormID);
        if (!npcForm) {
            MARAS_LOG_WARN("Could not find form for FormID {:08X} to send affection change event", npcFormID);
            return;
        }
        float diff = static_cast<float>(delta);
        EmitModCallbackEvent("maras_change_affection", threshold, diff, npcFormID);
        MARAS_LOG_INFO("Sent maras_change_affection event for NPC {:08X}: threshold={} diff={}", npcFormID,
                       threshold, diff);
    }

    int AffectionService::CalculateTotalDailyDelta(const std::unordered_map<std::string, float>& dailyByType) const {
        int deltaTotal = 0;
        std::unordered_map<std::string, std::pair<int, int>> minMaxCopy;
        {
            std::shared_lock lock(minMaxMutex_);
            minMaxCopy = minMaxByType_;
        }

        for (const auto& [type, value] : dailyByType) {
            int applied = static_cast<int>(std::lround(value));

            // Clamp if min/max configured for this type
            auto mmIt = minMaxCopy.find(type);
            if (mmIt != minMaxCopy.end()) {
                applied = std::clamp(applied, mmIt->second.first, mmIt->second.second);
            }

            deltaTotal += applied;
            MARAS_LOG_DEBUG("CalculateTotalDailyDelta: type {} daily {} -> applied {}", type, value, applied);
        }

        return deltaTotal;
    }

    float AffectionService::CalculateLonelinessDecay(FormID npcFormID, bool isFollowing,
                                                     size_t activePartnerCount) const {
        const float decayMult = Settings::GetSingleton().Snapshot()->affectionDecayMultiplier;
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
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& permMap = snap->affection->AllPermanent();
        const auto& dayMap = snap->affection->AllLastDays();
        const auto& dailyMap = snap->affection->AllDaily();

        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        nlohmann::json j;
        j["decayMultiplier"] = snap->settings->affectionDecayMultiplier;

        nlohmann::json permArray = nlohmann::json::array();
        for (const auto& [actorId, val] : permMap) {
            const FormID formID = actorId.raw();
            nlohmann::json a;
            a["formId"] = std::format("{:08X}", formID);
            a["name"] = ActorSvc::GetName(formID);
            a["value"] = val;
            const auto dayIt = dayMap.find(actorId);
            a["lastAffectionDay"] = (dayIt != dayMap.end()) ? dayIt->second : -1.0f;
            a["daysSinceLastAffection"] = !calendar
                                              ? 0.0f
                                              : (dayIt != dayMap.end() ? currentDay - dayIt->second : kNeverRecordedDaysSince);
            permArray.push_back(a);
        }
        j["permanentAffection"] = permArray;

        nlohmann::json dailyArray = nlohmann::json::array();
        for (const auto& [actorId, typeMap] : dailyMap) {
            const FormID formID = actorId.raw();
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
        {
            std::shared_lock lock(minMaxMutex_);
            for (const auto& [tName, range] : minMaxByType_) {
                minMaxObj[tName] = { {"min", range.first}, {"max", range.second} };
            }
        }
        j["minMaxByType"] = minMaxObj;

        return j;
    }

}  // namespace MARAS
