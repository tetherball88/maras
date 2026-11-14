#include "maras/domain/agency/MatchmakingRules.h"

namespace MARAS::Domain::Agency {

    bool IsOrientationCompatible(
        MatchmakingOrientation orientation,
        ActorSex sexA,
        ActorSex sexB) noexcept {
        if (orientation == MatchmakingOrientation::Any) {
            return true;
        }
        if (sexA == ActorSex::Unknown || sexB == ActorSex::Unknown) {
            return false;
        }

        const bool sameSex = (sexA == sexB);
        const bool bothMale = (sexA == ActorSex::Male && sexB == ActorSex::Male);
        const bool bothFemale = (sexA == ActorSex::Female && sexB == ActorSex::Female);
        const bool hetero = !sameSex;

        switch (orientation) {
            case MatchmakingOrientation::Heterosexual:
                return hetero;
            case MatchmakingOrientation::Homosexual:
                return sameSex;
            case MatchmakingOrientation::Gay:
                return bothMale;
            case MatchmakingOrientation::Lesbian:
                return bothFemale;
            case MatchmakingOrientation::HeteroAndGay:
                return hetero || bothMale;
            case MatchmakingOrientation::HeteroAndLesbian:
                return hetero || bothFemale;
            case MatchmakingOrientation::Any:
            default:
                return true;
        }
    }

    bool IsNpcOnMatchmakingCooldown(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        float currentGameDay) noexcept {
        if (!snap.cooldowns || !snap.settings || actorId.IsNull()) {
            return false;
        }
        const auto cooldownDays = static_cast<float>(snap.settings->matchmakingNpcCooldownDays);
        if (cooldownDays <= 0.0f) {
            return false;
        }
        const auto it = snap.cooldowns->npcLastMatchmakingChangeDay.find(actorId);
        if (it == snap.cooldowns->npcLastMatchmakingChangeDay.end() || it->second < 0.0f) {
            return false;
        }
        return (currentGameDay - it->second) < cooldownDays;
    }

    bool IsEligibleForScheduledMatchmaking(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        float currentGameDay) noexcept {
        if (actorId.IsNull() || actorId.IsPlayer() || actorId.IsUnknownFather() || actorId.IsSyntheticChild()) {
            return false;
        }
        if (snap.deceased && snap.deceased->IsDeceased(actorId)) {
            return false;
        }
        if (snap.actors && !snap.actors->IsRegisteredAdult(actorId.AsRefId())) {
            return false;
        }
        if (snap.relationships) {
            const auto playerStatus = snap.relationships->GetStatus(ActorId::Player(), actorId);
            if (Foundation::IsMainTierStatus(playerStatus)) {
                return false;
            }
        }
        if (IsNpcOnMatchmakingCooldown(snap, actorId, currentGameDay)) {
            return false;
        }
        return true;
    }

    std::string_view GetEligibilityCategoryString(
        bool canFormMain,
        bool hasOpenMain) noexcept {
        if (canFormMain) {
            return "[eligible: main/secondary]";
        }
        if (hasOpenMain) {
            return "[open main: secondary allowed]";
        }
        return "[secondary only (cheating)]";
    }

    std::string_view GetOrientationRuleText(
        MatchmakingOrientation orientation) noexcept {
        switch (orientation) {
            case MatchmakingOrientation::Heterosexual:
                return "Heterosexual pairs only (male + female).";
            case MatchmakingOrientation::Homosexual:
                return "Same-sex pairs only (male + male or female + female).";
            case MatchmakingOrientation::Gay:
                return "Male + male pairs only.";
            case MatchmakingOrientation::Lesbian:
                return "Female + female pairs only.";
            case MatchmakingOrientation::HeteroAndGay:
                return "Heterosexual or male + male pairs.";
            case MatchmakingOrientation::HeteroAndLesbian:
                return "Heterosexual or female + female pairs.";
            case MatchmakingOrientation::Any:
            default:
                return "Any gender pairing is allowed.";
        }
    }

}  // namespace MARAS::Domain::Agency
