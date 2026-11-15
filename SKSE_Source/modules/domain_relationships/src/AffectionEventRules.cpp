#include "maras/domain/relationships/AffectionEventRules.h"

#include "maras/foundation/StringUtils.h"

#include <unordered_map>
#include <utility>

namespace MARAS::Domain::Relationships {

    namespace {

        [[nodiscard]] constexpr State::ActorId SanitizeActorId(State::ActorId id) noexcept {
            if (id.IsNull() || id.IsUnknownFather() || id.IsSyntheticChild()) {
                return State::ActorId{};
            }
            return id;
        }

        void AppendPlannedAdjustment(
            const State::WorldStateSnapshot& snap,
            std::unordered_map<State::ActorId, int>& sequentialScores,
            std::vector<PartnerAffectionAdjustment>& out,
            State::ActorId partner,
            float baseDelta,
            std::string_view reason) {
            auto planned = PlanPlayerPartnerAffectionAdjustment(snap, partner, baseDelta, reason);
            if (!planned.has_value()) {
                return;
            }
            if (const auto it = sequentialScores.find(partner); it != sequentialScores.end()) {
                planned->oldScore = it->second;
                planned->newScore = State::AffectionTable::ClampScore(it->second + planned->appliedDelta);
            }
            sequentialScores[partner] = planned->newScore;
            out.push_back(std::move(*planned));
        }

        void AppendSingleIfPresent(
            std::vector<PartnerAffectionAdjustment>& out,
            std::optional<PartnerAffectionAdjustment> planned) {
            if (planned.has_value()) {
                out.push_back(std::move(*planned));
            }
        }

        [[nodiscard]] std::string NormalizePaternityOutcome(std::string_view rawOutcome) {
            std::string normalized = Foundation::StringUtils::ToLowerStd(rawOutcome);
            if (normalized == "mother_confessed_presumed_unannounced") {
                return "mother_confessed_presumed";
            }
            if (normalized == "presumed_father_learnt_unannounced") {
                return "presumed_father_learnt";
            }
            if (normalized == "mother_confessed_both_unannounced") {
                return "mother_confessed_both";
            }
            return normalized;
        }

    }  // namespace

    // =========================================================================
    // Pure Daily Affection Calculators
    // =========================================================================

    float CalculateIntimacyDailyDelta(
        Foundation::Temperament temperament,
        float baseAffection) noexcept {
        float mlt = 1.0f;
        if (temperament == Foundation::Temperament::Romantic ||
            temperament == Foundation::Temperament::Jealous) {
            mlt = 1.5f;
        } else if (temperament == Foundation::Temperament::Independent) {
            mlt = 0.5f;
        }
        return baseAffection * mlt;
    }

    float CalculateGiftDailyDelta(
        Foundation::SocialClass socialClass,
        Foundation::Temperament temperament,
        float goldValue) noexcept {
        const float factor = 0.667f + (static_cast<float>(static_cast<int>(socialClass)) / 7.0f);
        float giftAffection = goldValue / (factor * 100.0f);
        if (temperament == Foundation::Temperament::Romantic) {
            giftAffection *= 1.5f;
        }
        return giftAffection;
    }

    float CalculatePromotionDailyDelta(
        Foundation::Temperament temperament,
        float rankDiff) noexcept {
        float promoAffection = 8.0f;
        if (temperament == Foundation::Temperament::Proud) {
            promoAffection = 12.0f;
        } else if (temperament == Foundation::Temperament::Humble) {
            promoAffection = 4.0f;
        }
        return promoAffection * rankDiff;
    }

    float CalculateSleptDailyDelta(
        Foundation::Temperament temperament) noexcept {
        if (temperament == Foundation::Temperament::Romantic) {
            return 5.0f;
        }
        if (temperament == Foundation::Temperament::Jealous) {
            return 4.0f;
        }
        if (temperament == Foundation::Temperament::Independent) {
            return 1.0f;
        }
        return 3.0f;
    }

    // =========================================================================
    // Pure Snapshot Lookup & Life-Event Scaling Helpers
    // =========================================================================

    Foundation::RelationshipStatus ResolvePlayerRelativeStatus(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept {
        if (npc.IsNull() || npc.IsPlayer() || npc.IsUnknownFather() || npc.IsSyntheticChild()) {
            return Foundation::RelationshipStatus::Unknown;
        }
        if (snap.relationships) {
            if (const auto* edge = snap.relationships->FindEdge(npc, State::ActorId::Player())) {
                return edge->status;
            }
        }
        if (snap.actors && snap.actors->Find(State::RefId{ npc.raw() }) != nullptr) {
            return Foundation::RelationshipStatus::Candidate;
        }
        return Foundation::RelationshipStatus::Unknown;
    }

    Foundation::Temperament ResolveActorTemperament(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept {
        if (snap.actors && !npc.IsNull()) {
            if (const auto* prof = snap.actors->Find(State::RefId{ npc.raw() })) {
                return prof->temperament;
            }
        }
        return Foundation::Temperament::Proud;
    }

    Foundation::SocialClass ResolveActorSocialClass(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept {
        if (snap.actors && !npc.IsNull()) {
            if (const auto* prof = snap.actors->Find(State::RefId{ npc.raw() })) {
                return prof->socialClass;
            }
        }
        return Foundation::SocialClass::Outcast;
    }

    int ScalePlayerPartnerLifeEventDelta(
        Foundation::Temperament temperament,
        float baseDelta) noexcept {
        float multiplier = 1.0f;
        if (baseDelta > 0.0f) {
            if (temperament == Foundation::Temperament::Romantic) {
                multiplier = 1.35f;
            } else if (temperament == Foundation::Temperament::Proud) {
                multiplier = 1.20f;
            } else if (temperament == Foundation::Temperament::Independent) {
                multiplier = 0.80f;
            }
        } else {
            if (temperament == Foundation::Temperament::Jealous) {
                multiplier = 1.50f;
            } else if (temperament == Foundation::Temperament::Romantic) {
                multiplier = 1.25f;
            } else if (temperament == Foundation::Temperament::Independent) {
                multiplier = 0.70f;
            } else if (temperament == Foundation::Temperament::Humble) {
                multiplier = 0.85f;
            }
        }
        return static_cast<int>(baseDelta * multiplier);
    }

    std::optional<PartnerAffectionAdjustment> PlanPlayerPartnerAffectionAdjustment(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        float baseDelta,
        std::string_view reason) {
        if (partner.IsNull() || partner.IsPlayer() || partner.IsUnknownFather() || partner.IsSyntheticChild()) {
            return std::nullopt;
        }

        const auto status = ResolvePlayerRelativeStatus(snap, partner);
        if (!IsEligiblePlayerAffectionStatus(status)) {
            return std::nullopt;
        }

        const Foundation::Temperament temperament = ResolveActorTemperament(snap, partner);
        const int appliedDelta = ScalePlayerPartnerLifeEventDelta(temperament, baseDelta);
        const int oldScore = snap.affection
                                 ? snap.affection->GetPermanent(partner, kDefaultAffectionScore)
                                 : kDefaultAffectionScore;
        const int newScore = State::AffectionTable::ClampScore(oldScore + appliedDelta);

        PartnerAffectionAdjustment adjustment;
        adjustment.partner = partner;
        adjustment.temperament = temperament;
        adjustment.baseDelta = baseDelta;
        adjustment.appliedDelta = appliedDelta;
        adjustment.oldScore = oldScore;
        adjustment.newScore = newScore;
        adjustment.reason = std::string(reason);
        return adjustment;
    }

    State::ActorId ResolveChildMotherPartner(
        const State::WorldStateSnapshot& snap,
        const State::ChildRecord& rec) noexcept {
        if (!rec.presumedFather.IsNull()) {
            return rec.presumedFather;
        }
        const State::ActorId mother = rec.bioMother;
        if (mother.IsNull()) {
            return State::ActorId{};
        }
        if (snap.relationships) {
            const State::ActorId primary = snap.relationships->GetPrimaryPartner(mother);
            if (!primary.IsNull() && primary != mother) {
                return primary;
            }
        }
        const State::ActorId coGuardian = rec.guardians.OtherGuardian(mother);
        if (!coGuardian.IsNull() && coGuardian != mother) {
            return coGuardian;
        }
        return State::ActorId{};
    }

    bool IsMotherPregnancySecretForPartner(
        const State::WorldStateSnapshot& snap,
        State::ActorId mother) noexcept {
        if (mother.IsNull() || !snap.lineage) {
            return false;
        }
        const auto unborn = snap.lineage->GetUnbornOfMother(mother);
        if (unborn.empty()) {
            return false;
        }
        const auto* rec = snap.lineage->FindChild(unborn.front());
        if (!rec || rec->stage != Foundation::ChildStage::Unborn || !rec->paternitySecret.isSecretForPartner) {
            return false;
        }
        const State::ActorId partner = ResolveChildMotherPartner(snap, *rec);
        if (partner.IsNull()) {
            return false;
        }
        return !rec->paternitySecret.CanObserverKnowPregnancy(
            rec->stage, rec->bioMother, rec->bioFather, partner, partner);
    }

    bool IsChildSecretForBioFather(
        const State::WorldStateSnapshot& snap,
        const State::ChildRecord& rec) noexcept {
        if (!rec.paternitySecret.isSecretForBioFather) {
            return false;
        }
        if (rec.bioFather.IsNull() || rec.bioFather.IsUnknownFather()) {
            return !rec.paternitySecret.discovered && !rec.paternitySecret.discoveredByBioFather;
        }
        const State::ActorId motherPartner = ResolveChildMotherPartner(snap, rec);
        return !rec.paternitySecret.CanObserverKnowTrueBioFather(
            rec.childId, rec.bioMother, rec.bioFather, motherPartner, rec.presumedFather, rec.bioFather);
    }

    // =========================================================================
    // Pure Life-Event Planners
    // =========================================================================

    std::vector<PartnerAffectionAdjustment> PlanPregnancyConceivedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        bool isPlayerPregnant) {
        std::vector<PartnerAffectionAdjustment> out;
        const State::ActorId mother = isPlayerPregnant ? State::ActorId::Player() : SanitizeActorId(partner);
        if (!mother.IsNull() && IsMotherPregnancySecretForPartner(snap, mother)) {
            return out;
        }
        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(
                snap, partner, 20.0f, isPlayerPregnant ? "player_pregnant" : "pregnancy_conceived"));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanPregnancyLostAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        std::string_view reason) {
        std::vector<PartnerAffectionAdjustment> out;
        const std::string suffix = reason.empty() ? "miscarriage" : std::string(reason);
        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(
                snap, partner, -15.0f, "pregnancy_lost_" + suffix));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanChildBirthAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        int childCount) {
        std::vector<PartnerAffectionAdjustment> out;
        float baseDelta = 25.0f;
        if (childCount > 1) {
            baseDelta += static_cast<float>((childCount - 1) * 10);
        }
        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(snap, partner, baseDelta, "child_birth"));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanChildAdoptedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner) {
        std::vector<PartnerAffectionAdjustment> out;
        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(snap, partner, 20.0f, "child_adopted"));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanChildMilestoneAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        std::string_view milestone) {
        std::vector<PartnerAffectionAdjustment> out;
        const std::string milestoneStr = milestone.empty() ? "child" : std::string(milestone);
        const float baseDelta = Foundation::StringUtils::CaseInsensitiveEquals(milestoneStr, "adult") ? 15.0f : 10.0f;
        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(
                snap, partner, baseDelta, "child_milestone_" + milestoneStr));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanChildMilestoneForParents(
        const State::WorldStateSnapshot& snap,
        State::ChildId childId,
        std::string_view milestone) {
        std::vector<PartnerAffectionAdjustment> out;
        if (childId.IsNull() || !snap.lineage) {
            return out;
        }
        const auto* rec = snap.lineage->FindChild(childId);
        if (!rec) {
            return out;
        }

        const State::ActorId bioMother = SanitizeActorId(rec->bioMother);
        const State::ActorId bioFather = SanitizeActorId(rec->bioFather);
        const State::ActorId presumedFather = SanitizeActorId(ResolveChildMotherPartner(snap, *rec));
        const bool isSecretForBio = IsChildSecretForBioFather(snap, *rec);

        if (!bioMother.IsNull() && bioMother.IsPlayer()) {
            if (!presumedFather.IsNull()) {
                return PlanChildMilestoneAffection(snap, presumedFather, milestone);
            }
            if (!bioFather.IsNull() && !isSecretForBio) {
                return PlanChildMilestoneAffection(snap, bioFather, milestone);
            }
            return out;
        }

        if (!presumedFather.IsNull() && presumedFather.IsPlayer()) {
            return PlanChildMilestoneAffection(snap, bioMother, milestone);
        }
        if (presumedFather.IsNull() && !bioFather.IsNull() && bioFather.IsPlayer() && !isSecretForBio) {
            return PlanChildMilestoneAffection(snap, bioMother, milestone);
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanCheatingCaughtAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim) {
        std::vector<PartnerAffectionAdjustment> out;
        if (victim.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, cheater, -30.0f, "cheating_caught"));
        } else if (cheater.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, victim, -35.0f, "cheating_caught"));
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanOpenRelationshipToggleAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId actorA,
        State::ActorId actorB,
        bool isOpen) {
        std::vector<PartnerAffectionAdjustment> out;
        const State::ActorId playerPartner = actorA.IsPlayer()
                                                 ? actorB
                                                 : (actorB.IsPlayer() ? actorA : State::ActorId{});
        if (playerPartner.IsNull() || playerPartner.IsPlayer()) {
            return out;
        }

        const Foundation::Temperament temperament = ResolveActorTemperament(snap, playerPartner);
        float baseDelta = 0.0f;
        if (isOpen) {
            if (temperament == Foundation::Temperament::Independent) {
                baseDelta = 15.0f;
            } else if (temperament == Foundation::Temperament::Jealous) {
                baseDelta = -35.0f;
            } else if (temperament == Foundation::Temperament::Romantic) {
                baseDelta = -20.0f;
            } else {
                baseDelta = -10.0f;
            }
        } else {
            if (temperament == Foundation::Temperament::Jealous) {
                baseDelta = 20.0f;
            } else if (temperament == Foundation::Temperament::Romantic) {
                baseDelta = 15.0f;
            } else if (temperament == Foundation::Temperament::Independent) {
                baseDelta = -10.0f;
            } else {
                baseDelta = 10.0f;
            }
        }

        AppendSingleIfPresent(
            out,
            PlanPlayerPartnerAffectionAdjustment(snap, playerPartner, baseDelta, "open_relationship_toggle"));
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanOpenRelationshipWalkInAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        State::ActorId witness) {
        std::vector<PartnerAffectionAdjustment> out;
        if (!partner.IsPlayer() || witness.IsNull() || witness.IsPlayer()) {
            return out;
        }
        if (ResolveActorTemperament(snap, witness) == Foundation::Temperament::Jealous) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, witness, -10.0f, "open_relationship_walkin"));
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanAffairConfessedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim) {
        std::vector<PartnerAffectionAdjustment> out;
        if (cheater.IsPlayer() && !victim.IsNull() && !victim.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, victim, -30.0f, "affair_confessed_by_player"));
        } else if (victim.IsPlayer() && !cheater.IsNull() && !cheater.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, cheater, -20.0f, "affair_confessed_to_player"));
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanAffairExposedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim,
        State::ActorId affairPartner) {
        std::vector<PartnerAffectionAdjustment> out;
        if (cheater.IsPlayer() && !victim.IsNull() && !victim.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, victim, -50.0f, "affair_exposed_player_cheated"));
        } else if (victim.IsPlayer()) {
            std::unordered_map<State::ActorId, int> sequentialScores;
            if (!cheater.IsNull() && !cheater.IsPlayer()) {
                AppendPlannedAdjustment(
                    snap, sequentialScores, out, cheater, -50.0f, "affair_exposed_partner_cheated");
            }
            if (!affairPartner.IsNull() && !affairPartner.IsPlayer()) {
                AppendPlannedAdjustment(
                    snap, sequentialScores, out, affairPartner, -30.0f, "affair_exposed_affair_partner");
            }
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanAffairResolvedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim,
        std::string_view resolution) {
        std::vector<PartnerAffectionAdjustment> out;
        float delta = 0.0f;
        std::string normRes;
        if (Foundation::StringUtils::CaseInsensitiveEquals(resolution, "confronted")) {
            delta = 10.0f;
            normRes = "confronted";
        } else if (Foundation::StringUtils::CaseInsensitiveEquals(resolution, "breakup")) {
            delta = -20.0f;
            normRes = "breakup";
        } else {
            return out;
        }

        const std::string reason = "affair_resolved_" + normRes;
        if (cheater.IsPlayer() && !victim.IsNull() && !victim.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, victim, delta, reason));
        } else if (victim.IsPlayer() && !cheater.IsNull() && !cheater.IsPlayer()) {
            AppendSingleIfPresent(
                out,
                PlanPlayerPartnerAffectionAdjustment(snap, cheater, delta, reason));
        } else {
            std::unordered_map<State::ActorId, int> sequentialScores;
            AppendPlannedAdjustment(snap, sequentialScores, out, cheater, delta, reason);
            AppendPlannedAdjustment(snap, sequentialScores, out, victim, delta, reason);
        }
        return out;
    }

    std::vector<PartnerAffectionAdjustment> PlanPaternitySecretAffection(
        const State::WorldStateSnapshot& snap,
        State::ChildId childId,
        std::string_view outcomeType) {
        std::vector<PartnerAffectionAdjustment> out;
        if (childId.IsNull() || !snap.lineage) {
            return out;
        }
        const std::string normalized = NormalizePaternityOutcome(outcomeType);
        if (normalized.empty() || normalized == "none" || normalized == "remains_secret") {
            return out;
        }

        const auto* rec = snap.lineage->FindChild(childId);
        if (!rec) {
            return out;
        }

        const State::ActorId mother = SanitizeActorId(rec->bioMother);
        const State::ActorId presumedFather = SanitizeActorId(ResolveChildMotherPartner(snap, *rec));
        const State::ActorId bioFather = SanitizeActorId(rec->bioFather);

        if (normalized == "mother_confessed_bio" || normalized == "bio_father_learnt") {
            const std::string reason = "paternity_secret_" + normalized;
            if (!bioFather.IsNull() && bioFather.IsPlayer() && !mother.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, mother, 15.0f, reason));
            } else if (!mother.IsNull() && mother.IsPlayer() && !bioFather.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, bioFather, 15.0f, reason));
            }
            return out;
        }

        if (normalized == "mother_confessed_presumed") {
            const std::string reason = "paternity_secret_" + normalized;
            if (!presumedFather.IsNull() && presumedFather.IsPlayer() && !mother.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, mother, -30.0f, reason));
            } else if (!mother.IsNull() && mother.IsPlayer() && !presumedFather.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, presumedFather, -30.0f, reason));
            }
            return out;
        }

        if (normalized == "presumed_father_learnt") {
            const std::string reason = "paternity_secret_" + normalized;
            if (!presumedFather.IsNull() && presumedFather.IsPlayer() && !mother.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, mother, -40.0f, reason));
            } else if (!mother.IsNull() && mother.IsPlayer() && !presumedFather.IsNull()) {
                AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, presumedFather, -40.0f, reason));
            }
            return out;
        }

        if (normalized == "mother_shared_pregnancy_presumed" || normalized == "pregnancy_visible_presumed") {
            if (!mother.IsNull() && mother.IsPlayer() && !presumedFather.IsNull()) {
                return PlanPregnancyConceivedAffection(snap, presumedFather, /*isPlayerPregnant=*/true);
            }
            if (!presumedFather.IsNull() && presumedFather.IsPlayer() && !mother.IsNull()) {
                return PlanPregnancyConceivedAffection(snap, mother, /*isPlayerPregnant=*/false);
            }
            return out;
        }

        if (normalized == "mother_confessed_both") {
            std::unordered_map<State::ActorId, int> sequentialScores;
            if (!presumedFather.IsNull() && presumedFather.IsPlayer() && !mother.IsNull()) {
                AppendPlannedAdjustment(
                    snap, sequentialScores, out, mother, -30.0f, "paternity_secret_mother_confessed_presumed");
            } else if (!bioFather.IsNull() && bioFather.IsPlayer() && !mother.IsNull()) {
                AppendPlannedAdjustment(
                    snap, sequentialScores, out, mother, 15.0f, "paternity_secret_mother_confessed_bio");
            } else if (!mother.IsNull() && mother.IsPlayer()) {
                if (!presumedFather.IsNull()) {
                    AppendPlannedAdjustment(
                        snap, sequentialScores, out, presumedFather, -30.0f, "paternity_secret_mother_confessed_presumed");
                }
                if (!bioFather.IsNull()) {
                    AppendPlannedAdjustment(
                        snap, sequentialScores, out, bioFather, 15.0f, "paternity_secret_mother_confessed_bio");
                }
            }
            return out;
        }

        const std::string reason = "paternity_secret_" + normalized;
        constexpr float kFallbackDelta = -35.0f;
        if (!presumedFather.IsNull() && presumedFather.IsPlayer() && !mother.IsNull()) {
            AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, mother, kFallbackDelta, reason));
        } else if (!mother.IsNull() && mother.IsPlayer() && !presumedFather.IsNull()) {
            AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, presumedFather, kFallbackDelta, reason));
        } else if (!bioFather.IsNull() && bioFather.IsPlayer() && !mother.IsNull()) {
            AppendSingleIfPresent(out, PlanPlayerPartnerAffectionAdjustment(snap, mother, kFallbackDelta, reason));
        }
        return out;
    }

}  // namespace MARAS::Domain::Relationships
