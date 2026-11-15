#include "maras/domain/relationships/AffectionRules.h"

#include "maras/foundation/DomainEnums.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace MARAS::Domain::Relationships {

    namespace {
        constexpr int kThresholdHappy = 75;
        constexpr int kThresholdContent = 50;
        constexpr int kThresholdTroubled = 25;

        constexpr float kFollowingAffectionBonus = 6.0f;

        constexpr float kDecayIndependent = 1.0f;
        constexpr float kDecayHumble = 2.0f;
        constexpr float kDecayRomantic = 4.0f;
        constexpr float kDecayDefault = 3.0f;

        constexpr std::size_t kSpouseCountHigh = 7;
        constexpr std::size_t kSpouseCountMedium = 5;
        constexpr std::size_t kSpouseCountLow = 3;
        constexpr float kSpouseMultHigh = 0.5f;
        constexpr float kSpouseMultMedium = 0.6f;
        constexpr float kSpouseMultLow = 0.75f;

        constexpr float kEngagedDecayMultiplier = 0.75f;
        constexpr float kDatingDecayMultiplier = 0.5f;
        constexpr float kLoverDecayMultiplier = 0.25f;

        constexpr float kOpenRelMultIndependent = 0.50f;
        constexpr float kOpenRelMultHumble = 0.75f;
        constexpr float kOpenRelMultProud = 0.90f;
        constexpr float kOpenRelMultRomantic = 1.15f;
        constexpr float kOpenRelMultJealous = 1.35f;

        constexpr float kCheatingMultIndependent = 1.25f;
        constexpr float kCheatingMultHumble = 1.35f;
        constexpr float kCheatingMultRomantic = 1.50f;
        constexpr float kCheatingMultProud = 1.75f;
        constexpr float kCheatingMultJealous = 2.00f;
    }  // namespace

    std::string NormalizeAffectionType(std::string_view rawType) {
        return State::AffectionTable::NormalizeTypeKey(rawType);
    }

    std::string_view GetAffectionThreshold(int affectionValue) noexcept {
        if (affectionValue >= kThresholdHappy) {
            return "happy";
        }
        if (affectionValue >= kThresholdContent) {
            return "content";
        }
        if (affectionValue >= kThresholdTroubled) {
            return "troubled";
        }
        return "estranged";
    }

    int CalculateTotalDailyDelta(
        const State::AffectionTable& table,
        const std::unordered_map<std::string, float>& dailyByType) {
        int deltaTotal = 0;
        for (const auto& [type, value] : dailyByType) {
            int applied = static_cast<int>(std::lround(value));
            if (const auto range = table.GetMinMaxForType(type); range.has_value()) {
                applied = std::clamp(applied, range->first, range->second);
            }
            deltaTotal += applied;
        }
        return deltaTotal;
    }

    float CalculateLonelinessDecay(
        const State::WorldStateSnapshot& snap,
        ActorId npcId,
        bool isFollowing,
        std::size_t activePartnerCount) {
        const float decayMult = snap.settings ? snap.settings->affectionDecayMultiplier : 1.0f;
        if (decayMult <= 0.0f) {
            return 0.0f;
        }

        if (isFollowing) {
            return kFollowingAffectionBonus;
        }

        if (!snap.relationships || npcId.IsNull() || npcId.IsPlayer()) {
            return 0.0f;
        }

        const auto* edge = snap.relationships->FindEdge(npcId, ActorId::Player());
        if (!edge) {
            return 0.0f;
        }

        float statusMult = 0.0f;
        switch (edge->status) {
            case Foundation::RelationshipStatus::Married:
                statusMult = 1.0f;
                break;
            case Foundation::RelationshipStatus::Engaged:
                statusMult = kEngagedDecayMultiplier;
                break;
            case Foundation::RelationshipStatus::Dating:
                statusMult = kDatingDecayMultiplier;
                break;
            case Foundation::RelationshipStatus::Lover:
                statusMult = kLoverDecayMultiplier;
                break;
            default:
                return 0.0f;
        }

        const Foundation::Temperament temperament = [&]() noexcept {
            if (snap.actors) {
                if (const auto* profile = snap.actors->Find(Foundation::RefId{ npcId.raw() })) {
                    return profile->temperament;
                }
            }
            return Foundation::Temperament::Independent;
        }();

        float perDay = kDecayDefault;
        switch (temperament) {
            case Foundation::Temperament::Independent:
                perDay = kDecayIndependent;
                break;
            case Foundation::Temperament::Humble:
                perDay = kDecayHumble;
                break;
            case Foundation::Temperament::Romantic:
                perDay = kDecayRomantic;
                break;
            default:
                perDay = kDecayDefault;
                break;
        }

        float polyMult = 1.0f;
        if (activePartnerCount >= kSpouseCountHigh) {
            polyMult = kSpouseMultHigh;
        } else if (activePartnerCount >= kSpouseCountMedium) {
            polyMult = kSpouseMultMedium;
        } else if (activePartnerCount >= kSpouseCountLow) {
            polyMult = kSpouseMultLow;
        }

        float openRelMult = 1.0f;
        if (Foundation::IsMainTierStatus(edge->status) && edge->isOpen) {
            switch (temperament) {
                case Foundation::Temperament::Independent:
                    openRelMult = kOpenRelMultIndependent;
                    break;
                case Foundation::Temperament::Humble:
                    openRelMult = kOpenRelMultHumble;
                    break;
                case Foundation::Temperament::Proud:
                    openRelMult = kOpenRelMultProud;
                    break;
                case Foundation::Temperament::Romantic:
                    openRelMult = kOpenRelMultRomantic;
                    break;
                case Foundation::Temperament::Jealous:
                    openRelMult = kOpenRelMultJealous;
                    break;
                default:
                    openRelMult = 1.0f;
                    break;
            }
        }

        bool hasKnownOngoingCheating = false;
        if (snap.affairs) {
            for (const auto& rec : snap.affairs->GetForCouplePair(edge->pair)) {
                if (rec.affairStatus == Foundation::AffairStatus::Ongoing &&
                    !rec.isSecret &&
                    ((rec.cheater == npcId && rec.deceivedPartner.IsPlayer()) ||
                     (rec.cheater.IsPlayer() && rec.deceivedPartner == npcId))) {
                    hasKnownOngoingCheating = true;
                    break;
                }
            }
        }

        float cheatingMult = 1.0f;
        if (hasKnownOngoingCheating) {
            switch (temperament) {
                case Foundation::Temperament::Independent:
                    cheatingMult = kCheatingMultIndependent;
                    break;
                case Foundation::Temperament::Humble:
                    cheatingMult = kCheatingMultHumble;
                    break;
                case Foundation::Temperament::Romantic:
                    cheatingMult = kCheatingMultRomantic;
                    break;
                case Foundation::Temperament::Proud:
                    cheatingMult = kCheatingMultProud;
                    break;
                case Foundation::Temperament::Jealous:
                    cheatingMult = kCheatingMultJealous;
                    break;
                default:
                    cheatingMult = 1.0f;
                    break;
            }
        }

        const float mult = polyMult * statusMult * openRelMult * cheatingMult * decayMult;
        return -(perDay * mult);
    }

    std::vector<ActorId> CollectUniquePlayerActivePartners(const State::WorldStateSnapshot& snap) {
        std::vector<ActorId> result;
        if (!snap.relationships) {
            return result;
        }
        std::unordered_set<ActorId> seen;
        for (const auto& [partnerId, status] : snap.relationships->GetActivePartners(ActorId::Player())) {
            (void)status;
            if (!partnerId.IsNull() && !partnerId.IsPlayer() && seen.insert(partnerId).second) {
                result.push_back(partnerId);
            }
        }
        return result;
    }

    std::vector<DailyAffectionChange> ApplyDailyAffections(
        State::WorldState& ws,
        const State::IActorMetadataProvider& meta) {
        std::vector<DailyAffectionChange> changes;
        const auto& allDaily = ws.affection.Read().AllDaily();
        if (allDaily.empty()) {
            return changes;
        }

        struct PendingItem {
            ActorId actorId{};
            int deltaTotal{ 0 };
        };
        std::vector<PendingItem> pending;
        pending.reserve(allDaily.size());

        const auto& readAff = ws.affection.Read();
        for (const auto& [actorId, typeMap] : allDaily) {
            if (typeMap.empty() || actorId.IsNull() || actorId.IsPlayer() || actorId.IsSyntheticChild()) {
                continue;
            }
            const int deltaTotal = CalculateTotalDailyDelta(readAff, typeMap);
            if (deltaTotal != 0 && meta.IsAliveAndValid(actorId)) {
                pending.push_back(PendingItem{ actorId, deltaTotal });
            }
        }

        auto& aff = ws.affection.Write();
        for (const auto& item : pending) {
            const int original = aff.GetPermanent(item.actorId, 50);
            const int unclamped = original + item.deltaTotal;
            const int updated = State::AffectionTable::ClampScore(unclamped);
            aff.SetPermanent(item.actorId, updated);

            const bool isMarried = ws.relationships.Read().GetStatus(ActorId::Player(), item.actorId) ==
                                   Foundation::RelationshipStatus::Married;
            changes.push_back(DailyAffectionChange{
                .actorId = item.actorId,
                .oldScore = original,
                .newScore = updated,
                .unclampedScore = unclamped,
                .oldThreshold = GetAffectionThreshold(original),
                .newThreshold = GetAffectionThreshold(updated),
                .marriedSpouseChanged = (updated != original) && isMarried
            });
        }
        aff.ClearDailyAccumulation();

        return changes;
    }

}  // namespace MARAS::Domain::Relationships
