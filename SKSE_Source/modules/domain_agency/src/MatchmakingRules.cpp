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
            return "can form main";
        }
        if (hasOpenMain) {
            return "secondary only (allowed)";
        }
        return "secondary only (cheating)";
    }

    std::string_view GetOrientationRuleText(
        MatchmakingOrientation orientation) noexcept {
        switch (orientation) {
            case MatchmakingOrientation::Heterosexual:
                return "Heterosexual only: ONLY pair different-sex NPCs (male + female). Do NOT pair same-sex NPCs.";
            case MatchmakingOrientation::Homosexual:
                return "Homosexual only: ONLY pair same-sex NPCs (male + male OR female + female). Do NOT pair different-sex NPCs.";
            case MatchmakingOrientation::Gay:
                return "Gay (male-male) only: ONLY pair male NPCs with other male NPCs. Do NOT pair female NPCs.";
            case MatchmakingOrientation::Lesbian:
                return "Lesbian (female-female) only: ONLY pair female NPCs with other female NPCs. Do NOT pair male NPCs.";
            case MatchmakingOrientation::HeteroAndGay:
                return "Heterosexual and Gay: ONLY pair different-sex NPCs (male + female) OR male same-sex NPCs (male + male). Do NOT pair female same-sex NPCs.";
            case MatchmakingOrientation::HeteroAndLesbian:
                return "Heterosexual and Lesbian: ONLY pair different-sex NPCs (male + female) OR female same-sex NPCs (female + female). Do NOT pair male same-sex NPCs.";
            case MatchmakingOrientation::Any:
            default:
                return "Any orientation: Pairing any combination of sexes (heterosexual or homosexual) is allowed.";
        }
    }

    std::string BuildAllowedOperationsText(
        bool allowBreakups,
        bool allowOpenClose) {
        std::string out =
            "- **Create new relationships** (dating, lover — mark whether lover is allowed or considered cheating)\n"
            "- **Advance existing relationships** (dating->engaged, engaged->married, lover->dating, lover->engaged)";
        if (allowBreakups) {
            out += "\n- **Break up existing relationships** (to exdating, exlover, divorced, jilted)";
        }
        if (allowOpenClose) {
            out += "\n- **Open or close relationships** (switch between open and exclusive)";
        }
        return out;
    }

    std::string BuildAllowedActionsString(
        bool allowBreakups,
        bool allowOpenClose) {
        std::string out = "new|advance";
        if (allowBreakups) {
            out += "|breakup";
        }
        if (allowOpenClose) {
            out += "|open|close";
        }
        return out;
    }

    std::string_view BuildCheatingRuleText(bool allowCheating) noexcept {
        if (!allowCheating) {
            return "Cheating/infidelity is DISABLED by player settings. NPCs marked '[secondary only (cheating)]' (who have exclusive/closed main partners) MUST NOT be paired with any secondary partners (lover).";
        }
        return "Cheating/infidelity is ALLOWED. You may suggest secondary lover relationships for NPCs marked '[secondary only (cheating)]' if dramatically interesting (this creates infidelity drama).";
    }

    std::string_view BuildBreakupRuleText(bool allowBreakups) noexcept {
        if (!allowBreakups) {
            return "Breakups are DISABLED by mod settings. Do NOT suggest breaking up any relationships and do NOT use action 'breakup'.";
        }
        return "Breakups are ALLOWED. You may suggest breaking up existing relationships (action 'breakup') when appropriate.";
    }

    std::string_view BuildOpenCloseRuleText(bool allowOpenClose) noexcept {
        if (!allowOpenClose) {
            return "Open/close pair modifications are DISABLED by mod settings. Do NOT output actions 'open' or 'close'.";
        }
        return "Open/close modifications are ALLOWED. You may suggest opening (action 'open') or closing (action 'close') existing pairs.";
    }

    std::pair<std::string_view, RelationshipStatus> GetBreakupTargetStatus(
        RelationshipStatus currentStatus) noexcept {
        switch (currentStatus) {
            case RelationshipStatus::Married:
                return { "divorced", RelationshipStatus::Divorced };
            case RelationshipStatus::Engaged:
                return { "jilted", RelationshipStatus::Jilted };
            case RelationshipStatus::Dating:
                return { "exdating", RelationshipStatus::ExDating };
            case RelationshipStatus::Lover:
                return { "exlover", RelationshipStatus::ExLover };
            default:
                return { "", RelationshipStatus::Unknown };
        }
    }

}  // namespace MARAS::Domain::Agency
