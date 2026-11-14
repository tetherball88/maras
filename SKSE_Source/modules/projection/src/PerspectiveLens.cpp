#include "maras/projection/PerspectiveView.h"

#include <algorithm>
#include <unordered_map>

namespace MARAS::Projection {

    namespace {

        [[nodiscard]] std::shared_ptr<const State::WorldStateSnapshot> EmptyWorldSnapshot() {
            static const std::shared_ptr<const State::WorldStateSnapshot> kEmpty =
                State::WorldState{}.Snapshot()
                    .actors
                    ? std::make_shared<const State::WorldStateSnapshot>(State::WorldState{}.Snapshot())
                    : std::make_shared<const State::WorldStateSnapshot>();
            return kEmpty;
        }

        [[nodiscard]] std::shared_ptr<const State::SpatialActorSnapshot> EmptySpatialSnapshot() {
            static const std::shared_ptr<const State::SpatialActorSnapshot> kEmpty =
                std::make_shared<const State::SpatialActorSnapshot>();
            return kEmpty;
        }

        [[nodiscard]] constexpr bool IsValidBiologicalParentId(State::ActorId actor) noexcept {
            return !actor.IsNull() && !actor.IsUnknownFather();
        }

        [[nodiscard]] constexpr bool IsSecretAffairRecord(const State::AffairRecord& rec) noexcept {
            return rec.isSecret &&
                   rec.affairStatus != State::AffairStatus::Allowed &&
                   rec.affairStatus != State::AffairStatus::EndedConfronted;
        }

        [[nodiscard]] constexpr bool IsAffairMaskEligibleStatus(State::RelationshipStatus st) noexcept {
            return st == State::RelationshipStatus::Lover ||
                   st == State::RelationshipStatus::Dating ||
                   st == State::RelationshipStatus::ExLover ||
                   st == State::RelationshipStatus::ExDating ||
                   st == State::RelationshipStatus::Candidate;
        }

        [[nodiscard]] PerceivedAffair ToPerceivedAffair(const State::AffairRecord& rec) noexcept {
            return PerceivedAffair{
                .id = rec.id,
                .cheater = rec.cheater,
                .deceivedPartner = rec.deceivedPartner,
                .paramour = rec.paramour,
                .incidentCount = rec.incidentCount,
                .lastTimestamp = rec.lastTimestamp,
                .isSecret = rec.isSecret,
                .affairType = rec.affairType,
                .affairStatus = rec.affairStatus,
                .effectiveAffairStage = rec.deceivedPartner.IsPlayer()
                                            ? rec.affairStage
                                            : State::AffairStage::Physical,
                .stageStartDay = rec.stageStartDay,
            };
        }

        [[nodiscard]] std::optional<State::RelationshipStatus> FindMostRecentPriorMainOrBreakupStatus(
            const std::vector<State::StatusTransitionEntry>& history) noexcept {
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                if (it->toStatus == State::RelationshipStatus::Divorced ||
                    it->toStatus == State::RelationshipStatus::Jilted) {
                    return it->toStatus;
                }
            }
            return std::nullopt;
        }

        void StripAffairTransitions(
            const std::vector<State::StatusTransitionEntry>& src,
            std::vector<State::StatusTransitionEntry>& dst) {
            dst.clear();
            dst.reserve(src.size());
            for (const auto& entry : src) {
                if (entry.toStatus != State::RelationshipStatus::Lover &&
                    entry.toStatus != State::RelationshipStatus::Dating) {
                    dst.push_back(entry);
                }
            }
        }

        void PushUniqueValidParent(std::vector<State::ActorId>& dst, State::ActorId id, State::ActorId selfChild) {
            if (!IsValidBiologicalParentId(id) || id == selfChild) {
                return;
            }
            if (std::find(dst.begin(), dst.end(), id) == dst.end()) {
                dst.push_back(id);
            }
        }

    }  // namespace

    // =========================================================================
    // PerceivedChildLineage Helpers
    // =========================================================================

    bool PerceivedChildLineage::IsActorPerceivedBioParent(State::ActorId actor) const noexcept {
        if (!IsValidBiologicalParentId(actor)) {
            return false;
        }
        return actor == bioMother || actor == perceivedBioFather;
    }

    bool PerceivedChildLineage::IsActorPerceivedStepParent(State::ActorId actor) const noexcept {
        if (!IsValidBiologicalParentId(actor)) {
            return false;
        }
        return std::find(stepParents.begin(), stepParents.end(), actor) != stepParents.end();
    }

    State::ActorId PerceivedChildLineage::PerceivedOtherParent(State::ActorId parent) const noexcept {
        if (!IsValidBiologicalParentId(parent)) {
            return State::ActorId{};
        }
        if (parent == bioMother) {
            if (IsValidBiologicalParentId(perceivedBioFather) && perceivedBioFather != parent) {
                return perceivedBioFather;
            }
            if (const State::ActorId otherGuard = guardians.OtherGuardian(parent);
                IsValidBiologicalParentId(otherGuard)) {
                return otherGuard;
            }
            return State::ActorId{};
        }
        if (parent == perceivedBioFather) {
            if (IsValidBiologicalParentId(bioMother) && bioMother != parent) {
                return bioMother;
            }
            if (const State::ActorId otherGuard = guardians.OtherGuardian(parent);
                IsValidBiologicalParentId(otherGuard)) {
                return otherGuard;
            }
            return State::ActorId{};
        }
        if (guardians.HasGuardian(parent)) {
            return guardians.OtherGuardian(parent);
        }
        return State::ActorId{};
    }

    // =========================================================================
    // PerspectiveView Factories & Construction
    // =========================================================================

    PerspectiveView::PerspectiveView(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        ViewerMode mode,
        State::ActorId viewer,
        const IWerewolfStateProvider* werewolfProvider)
        : world_(world ? std::move(world) : EmptyWorldSnapshot()),
          spatial_(spatial ? std::move(spatial) : EmptySpatialSnapshot()),
          mode_(mode),
          viewer_(viewer),
          werewolfProvider_(werewolfProvider) {
        // Guard against partially populated snapshots from caller tests.
        if (!world_->actors || !world_->relationships || !world_->affairs ||
            !world_->lineage || !world_->hierarchy || !world_->household ||
            !world_->affection || !world_->deceased || !world_->cooldowns || !world_->settings) {
            const auto empty = EmptyWorldSnapshot();
            auto normalized = std::make_shared<State::WorldStateSnapshot>(*world_);
            if (!normalized->actors) normalized->actors = empty->actors;
            if (!normalized->relationships) normalized->relationships = empty->relationships;
            if (!normalized->affairs) normalized->affairs = empty->affairs;
            if (!normalized->lineage) normalized->lineage = empty->lineage;
            if (!normalized->hierarchy) normalized->hierarchy = empty->hierarchy;
            if (!normalized->household) normalized->household = empty->household;
            if (!normalized->affection) normalized->affection = empty->affection;
            if (!normalized->deceased) normalized->deceased = empty->deceased;
            if (!normalized->cooldowns) normalized->cooldowns = empty->cooldowns;
            if (!normalized->settings) normalized->settings = empty->settings;
            world_ = std::move(normalized);
        }
    }

    PerspectiveView PerspectiveView::ForActor(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ActorId viewer,
        const IWerewolfStateProvider* werewolfProvider) {
        return PerspectiveView(
            std::move(world),
            std::move(spatial),
            ViewerMode::ActorSelf,
            viewer,
            werewolfProvider);
    }

    PerspectiveView PerspectiveView::ForDialogueTarget(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        const IWerewolfStateProvider* werewolfProvider) {
        return PerspectiveView(
            std::move(world),
            std::move(spatial),
            ViewerMode::DialogueTargetPublic,
            State::ActorId{},
            werewolfProvider);
    }

    PerspectiveView PerspectiveView::ForDashboard(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        const IWerewolfStateProvider* werewolfProvider) {
        const bool reveal = world && world->settings && world->settings->revealSecretsInUI;
        return ForDashboard(std::move(world), std::move(spatial), reveal, werewolfProvider);
    }

    PerspectiveView PerspectiveView::ForDashboard(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        bool revealSecretsOverride,
        const IWerewolfStateProvider* werewolfProvider) {
        return PerspectiveView(
            std::move(world),
            std::move(spatial),
            revealSecretsOverride ? ViewerMode::Omniscient : ViewerMode::DashboardIntrigue,
            State::ActorId{},
            werewolfProvider);
    }

    // =========================================================================
    // Actor Status Queries
    // =========================================================================

    bool PerspectiveView::IsActorKnownWerewolf(State::ActorId actor) const noexcept {
        if (!IsValidBiologicalParentId(actor)) {
            return false;
        }
        if (const auto* childRec = world_->lineage->FindChild(State::ChildId{ actor.raw() });
            childRec && childRec->hasBeastblood) {
            return true;
        }
        if (werewolfProvider_ != nullptr) {
            if (const auto provided = werewolfProvider_->IsAdultWerewolf(actor); provided.has_value()) {
                return *provided;
            }
        }
        if (const State::RefId ref = actor.AsRefId(); !ref.IsNull()) {
            if (const auto* sample = spatial_->Find(ref); sample != nullptr) {
                return sample->IsWerewolf();
            }
        }
        return false;
    }

    bool PerspectiveView::IsActorAlive(State::ActorId actor) const noexcept {
        if (!IsValidBiologicalParentId(actor)) {
            return false;
        }
        if (world_->deceased->IsDeceased(actor)) {
            return false;
        }
        if (const State::RefId ref = actor.AsRefId(); !ref.IsNull()) {
            if (const auto* sample = spatial_->Find(ref); sample != nullptr) {
                return !sample->IsDead() && !sample->IsDeleted();
            }
        }
        return true;
    }

    // =========================================================================
    // Paternity & Pregnancy Secret Resolution
    // =========================================================================

    bool PerspectiveView::CanViewerKnowPregnancyImpl(const State::ChildRecord& child) const noexcept {
        if (child.stage != State::ChildStage::Unborn) {
            return true;
        }
        if (mode_ == ViewerMode::Omniscient) {
            return true;
        }

        const auto& sec = child.paternitySecret;
        const bool pregnancyHiddenFromPartner =
            !sec.discovered && !sec.discoveredByPartner && sec.isPregnancySecretForPartner;

        switch (mode_) {
            case ViewerMode::Omniscient:
                return true;
            case ViewerMode::DashboardIntrigue:
                // Parity with IsUnannouncedSecretPregnancyHiddenInUI:
                // Player mother's own unborn record stays listed (paternity still masked); NPC secret pregnancies are hidden.
                if (child.bioMother.IsPlayer()) {
                    return true;
                }
                return !pregnancyHiddenFromPartner;
            case ViewerMode::DialogueTargetPublic:
                return !pregnancyHiddenFromPartner;
            case ViewerMode::ActorSelf:
                if (sec.discovered) {
                    return true;
                }
                if (!pregnancyHiddenFromPartner) {
                    // Notice even after partner knows pregnancy, if pregnancy was never secret to partner
                    // OR was discoveredByPartner, mother/partner/public can know she is pregnant.
                    return true;
                }
                return sec.CanObserverKnowPregnancy(child.stage, child.bioMother, child.bioFather, viewer_);
        }
        return false;
    }

    bool PerspectiveView::CanViewerKnowTrueBioFatherImpl(
        const State::ChildRecord& child,
        State::ActorId effectivePresumedFather,
        bool isBioSameAsPresumed) const noexcept {
        if (mode_ == ViewerMode::Omniscient) {
            return true;
        }

        const auto& sec = child.paternitySecret;
        if (isBioSameAsPresumed || !sec.HasActiveSecret() || sec.discovered) {
            return true;
        }

        switch (mode_) {
            case ViewerMode::Omniscient:
                return true;
            case ViewerMode::DialogueTargetPublic:
            case ViewerMode::DashboardIntrigue:
                return false;
            case ViewerMode::ActorSelf:
                return sec.CanObserverKnowTrueBioFather(
                    child.childId,
                    child.bioMother,
                    child.bioFather,
                    effectivePresumedFather,
                    child.presumedFather,
                    viewer_);
        }
        return false;
    }

    PerspectiveView::ResolvedPaternityContext PerspectiveView::AnalyzePaternity(
        const State::ChildRecord& child) const noexcept {
        ResolvedPaternityContext ctx{};
        const auto& sec = child.paternitySecret;

        State::ActorId motherPartner = child.presumedFather;
        if (motherPartner.IsNull()) {
            motherPartner = child.guardians.OtherGuardian(child.bioMother);
        }
        if (motherPartner.IsNull() && !child.bioMother.IsNull()) {
            motherPartner = world_->relationships->GetPrimaryPartner(child.bioMother);
        }

        if (!motherPartner.IsNull() &&
            !child.bioMother.IsNull() &&
            child.presumedFather.IsNull() &&
            !sec.isSecretForPartner &&
            !child.guardians.HasGuardian(motherPartner) &&
            State::IsBreakupStatus(world_->relationships->GetStatus(child.bioMother, motherPartner))) {
            motherPartner = State::ActorId{};
        }

        ctx.cleanMotherPartner = motherPartner;
        ctx.effectivePresumedFather = !child.presumedFather.IsNull()
                                          ? child.presumedFather
                                          : motherPartner;
        ctx.isBioSameAsPresumed = (!ctx.effectivePresumedFather.IsNull() &&
                                   !child.bioFather.IsNull() &&
                                   ctx.effectivePresumedFather == child.bioFather);
        ctx.hasPresumedFather = (!ctx.effectivePresumedFather.IsNull() &&
                                 !ctx.isBioSameAsPresumed &&
                                 (child.bioMother.IsNull() ||
                                  ctx.effectivePresumedFather != child.bioMother));

        ctx.pregnancySecretActive =
            !sec.discovered && sec.isPregnancySecretForPartner && !sec.discoveredByPartner;
        ctx.secretFromPartnerActive =
            !sec.discovered &&
            (sec.isSecretForPartner || (child.stage == State::ChildStage::Unborn && sec.isPregnancySecretForPartner)) &&
            !sec.discoveredByPartner;
        ctx.secretFromBioFatherActive =
            !sec.discovered && sec.isSecretForBioFather && !sec.discoveredByBioFather;
        ctx.rawHasUndiscoveredSecret =
            sec.HasActiveSecret() && (ctx.secretFromPartnerActive || ctx.secretFromBioFatherActive);

        ctx.canViewerKnow = CanViewerKnowTrueBioFatherImpl(
            child,
            ctx.effectivePresumedFather,
            ctx.isBioSameAsPresumed);
        ctx.hideSecretFromViewer =
            (!ctx.isBioSameAsPresumed && ctx.rawHasUndiscoveredSecret && !ctx.canViewerKnow);

        return ctx;
    }

    bool PerspectiveView::IsChildVisible(State::ChildId childId) const noexcept {
        const auto* child = world_->lineage->FindChild(childId);
        if (!child) {
            return false;
        }
        return CanViewerKnowPregnancyImpl(*child);
    }

    bool PerspectiveView::CanViewerKnowPregnancy(State::ChildId childId) const noexcept {
        const auto* child = world_->lineage->FindChild(childId);
        if (!child) {
            return false;
        }
        return CanViewerKnowPregnancyImpl(*child);
    }

    bool PerspectiveView::CanViewerKnowTrueBioFather(State::ChildId childId) const noexcept {
        const auto* child = world_->lineage->FindChild(childId);
        if (!child) {
            return false;
        }
        const auto ctx = AnalyzePaternity(*child);
        return ctx.canViewerKnow;
    }

    PerceivedChildLineage PerspectiveView::ProjectChildRecord(const State::ChildRecord& child) const {
        const auto& sec = child.paternitySecret;
        const auto ctx = AnalyzePaternity(child);

        PerceivedChildLineage out{};
        out.childId = child.childId;
        out.name = child.name;
        out.stage = child.stage;
        out.gender = child.gender;
        out.isAdopted = child.isAdopted;
        out.isBiologicalChild = child.isBiologicalChild;
        out.conceptionTimestamp = child.conceptionTimestamp;
        out.birthTimestamp = child.birthTimestamp;
        out.guardians = child.guardians;
        out.bioMother = child.bioMother;
        out.motherPartner = ctx.cleanMotherPartner;
        out.trainingLocation = child.trainingLocation;
        out.trainingClassIndex = child.trainingClassIndex;
        out.trainingStartGameDay = child.trainingStartGameDay;
        out.hideSecretFromViewer = ctx.hideSecretFromViewer;
        out.hasUndiscoveredPaternitySecret =
            (!ctx.isBioSameAsPresumed && !ctx.hideSecretFromViewer && ctx.rawHasUndiscoveredSecret);

        out.perceivedBioFather =
            ctx.hideSecretFromViewer
                ? ((!child.isAdopted && ctx.secretFromPartnerActive && ctx.hasPresumedFather)
                       ? ctx.effectivePresumedFather
                       : State::ActorId{})
                : child.bioFather;

        if (!ctx.hideSecretFromViewer &&
            !IsValidBiologicalParentId(out.perceivedBioFather) &&
            !sec.bioFatherName.empty() &&
            sec.bioFatherName != "Unknown") {
            out.perceivedBioFatherFallbackName = sec.bioFatherName;
        }

        out.publicBioFather =
            (!ctx.isBioSameAsPresumed && ctx.rawHasUndiscoveredSecret)
                ? ((!child.isAdopted && ctx.secretFromPartnerActive && ctx.hasPresumedFather)
                       ? ctx.effectivePresumedFather
                       : State::ActorId{})
                : child.bioFather;

        if (!ctx.hideSecretFromViewer && ctx.hasPresumedFather) {
            out.presumedFather = ctx.effectivePresumedFather;
        }

        if (!child.bioMother.IsNull()) {
            out.biologicalParents.push_back(child.bioMother);
        }
        if (IsValidBiologicalParentId(out.perceivedBioFather) && out.perceivedBioFather != child.bioMother) {
            out.biologicalParents.push_back(out.perceivedBioFather);
        }

        if (!out.biologicalParents.empty()) {
            for (const State::ActorId guard : child.guardians.AsArray()) {
                if (guard.IsNull()) {
                    continue;
                }
                if (std::find(out.biologicalParents.begin(), out.biologicalParents.end(), guard) ==
                        out.biologicalParents.end() &&
                    std::find(out.stepParents.begin(), out.stepParents.end(), guard) == out.stepParents.end()) {
                    out.stepParents.push_back(guard);
                }
            }
        }

        // Beastblood: NEVER exposed in public dialogue_target mode.
        if (mode_ == ViewerMode::DialogueTargetPublic) {
            out.hasBeastblood = false;
        } else if (mode_ == ViewerMode::Omniscient) {
            out.hasBeastblood = child.hasBeastblood;
        } else if (child.hasBeastblood) {
            const bool visibleParentHasBeastblood =
                IsActorKnownWerewolf(child.bioMother) || IsActorKnownWerewolf(out.perceivedBioFather);
            out.hasBeastblood = !ctx.hideSecretFromViewer || visibleParentHasBeastblood;
        }

        // Detailed paternity secret metadata only exposed when viewer knows truth and intrigue context exists.
        const bool hasIntrigueContext =
            (!ctx.effectivePresumedFather.IsNull() && !ctx.isBioSameAsPresumed) ||
            sec.isSecretForBioFather;
        if (!ctx.hideSecretFromViewer && ctx.canViewerKnow && hasIntrigueContext &&
            (sec.isSecret || sec.discovered || !child.presumedFather.IsNull())) {
            PerceivedPaternitySecret pSec{};
            pSec.isSecret = !sec.discovered && (sec.isSecretForPartner || sec.isSecretForBioFather);
            pSec.isSecretForPartner = sec.isSecretForPartner;
            pSec.isSecretForBioFather = sec.isSecretForBioFather;
            pSec.isPregnancySecretForPartner = sec.isPregnancySecretForPartner;
            pSec.discovered = sec.discovered;
            pSec.discoveredByPartner = sec.discoveredByPartner;
            pSec.discoveredByBioFather = sec.discoveredByBioFather;
            pSec.hasPhenotypeMismatch = sec.hasPhenotypeMismatch;
            pSec.motherPartner = ctx.effectivePresumedFather;
            pSec.presumedFather = ctx.effectivePresumedFather;
            pSec.presumedFatherIsGuardian =
                !ctx.effectivePresumedFather.IsNull() && child.guardians.HasGuardian(ctx.effectivePresumedFather);
            pSec.trueBioFather = child.bioFather;
            if (!sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                pSec.trueBioFatherFallbackName = sec.bioFatherName;
            }
            pSec.trueBioFatherRaceId = sec.bioFatherRaceId;
            out.paternitySecret = std::move(pSec);
        }

        return out;
    }

    std::optional<PerceivedChildLineage> PerspectiveView::GetPerceivedChild(State::ChildId childId) const {
        const auto* child = world_->lineage->FindChild(childId);
        if (!child || !CanViewerKnowPregnancyImpl(*child)) {
            return std::nullopt;
        }
        return ProjectChildRecord(*child);
    }

    std::vector<PerceivedChildLineage> PerspectiveView::GetVisibleChildren() const {
        std::vector<PerceivedChildLineage> out{};
        out.reserve(world_->lineage->Size());
        for (const auto& [childId, child] : world_->lineage->AllChildren()) {
            (void)childId;
            if (!CanViewerKnowPregnancyImpl(child)) {
                continue;
            }
            out.push_back(ProjectChildRecord(child));
        }
        std::sort(out.begin(), out.end(), [](const PerceivedChildLineage& lhs, const PerceivedChildLineage& rhs) {
            return lhs.childId < rhs.childId;
        });
        return out;
    }

    std::vector<PerceivedChildLineage> PerspectiveView::GetCustodialChildrenOf(State::ActorId guardian) const {
        std::vector<PerceivedChildLineage> out{};
        if (guardian.IsNull()) {
            return out;
        }
        for (const auto& [childId, child] : world_->lineage->AllChildren()) {
            (void)childId;
            if (child.stage == State::ChildStage::Unborn) {
                continue;
            }
            if (child.guardians.HasGuardian(guardian)) {
                out.push_back(ProjectChildRecord(child));
            }
        }
        std::sort(out.begin(), out.end(), [](const PerceivedChildLineage& lhs, const PerceivedChildLineage& rhs) {
            return lhs.childId < rhs.childId;
        });
        return out;
    }

    std::vector<PerceivedChildLineage> PerspectiveView::GetNonCustodialChildrenOf(State::ActorId parent) const {
        std::vector<PerceivedChildLineage> out{};
        if (!IsValidBiologicalParentId(parent)) {
            return out;
        }
        for (const auto& [childId, child] : world_->lineage->AllChildren()) {
            (void)childId;
            if (child.stage == State::ChildStage::Unborn) {
                continue;
            }
            if (child.guardians.HasGuardian(parent)) {
                continue;
            }
            PerceivedChildLineage projected = ProjectChildRecord(child);
            if (projected.IsActorPerceivedBioParent(parent)) {
                out.push_back(std::move(projected));
            }
        }
        std::sort(out.begin(), out.end(), [](const PerceivedChildLineage& lhs, const PerceivedChildLineage& rhs) {
            return lhs.childId < rhs.childId;
        });
        return out;
    }

    std::vector<PerceivedSiblingEntry> PerspectiveView::GetPerceivedSiblingsOf(
        State::ChildId childId,
        bool forcePublicOnly) const {
        std::vector<PerceivedSiblingEntry> out{};
        const auto subjectOpt = GetPerceivedChild(childId);
        if (!subjectOpt.has_value() || subjectOpt->stage == State::ChildStage::Unborn) {
            return out;
        }

        const bool usePublicParents =
            forcePublicOnly ||
            mode_ == ViewerMode::DialogueTargetPublic ||
            mode_ == ViewerMode::DashboardIntrigue;

        const PerceivedChildLineage& subject = *subjectOpt;
        const State::ActorId subMother = subject.bioMother;
        const State::ActorId subFather = usePublicParents
                                             ? subject.publicBioFather
                                             : subject.perceivedBioFather;

        for (const auto& [otherId, otherRec] : world_->lineage->AllChildren()) {
            if (otherId == childId || otherRec.stage == State::ChildStage::Unborn) {
                continue;
            }
            const PerceivedChildLineage other = ProjectChildRecord(otherRec);
            const State::ActorId otherFather = usePublicParents
                                                   ? other.publicBioFather
                                                   : other.perceivedBioFather;

            const bool sameBioMother =
                !subject.isAdopted &&
                !other.isAdopted &&
                IsValidBiologicalParentId(subMother) &&
                subMother == other.bioMother;
            const bool sameBioFather =
                !subject.isAdopted &&
                !other.isAdopted &&
                IsValidBiologicalParentId(subFather) &&
                IsValidBiologicalParentId(otherFather) &&
                subFather == otherFather;
            const bool sharesGuardian =
                (!subject.guardians.slotA.IsNull() && other.guardians.HasGuardian(subject.guardians.slotA)) ||
                (!subject.guardians.slotB.IsNull() && other.guardians.HasGuardian(subject.guardians.slotB));

            std::optional<SiblingRelationKind> relKind{};
            if (sameBioMother && sameBioFather) {
                relKind = SiblingRelationKind::Full;
            } else if (sameBioMother || sameBioFather) {
                relKind = SiblingRelationKind::Half;
            } else if (sharesGuardian) {
                relKind = SiblingRelationKind::AdoptiveOrStep;
            }

            if (relKind.has_value()) {
                out.push_back(PerceivedSiblingEntry{
                    .childId = other.childId,
                    .name = other.name,
                    .stage = other.stage,
                    .gender = other.gender,
                    .relation = *relKind,
                    .isAdopted = other.isAdopted,
                });
            }
        }

        std::sort(out.begin(), out.end(), [](const PerceivedSiblingEntry& lhs, const PerceivedSiblingEntry& rhs) {
            return lhs.childId < rhs.childId;
        });
        return out;
    }

    std::optional<PerceivedSelfPregnancy> PerspectiveView::GetSelfPregnancy(State::ActorId mother) const {
        if (!IsValidBiologicalParentId(mother)) {
            return std::nullopt;
        }

        std::vector<const State::ChildRecord*> visibleFetuses{};
        for (const auto& [childId, child] : world_->lineage->AllChildren()) {
            (void)childId;
            if (child.stage != State::ChildStage::Unborn || child.bioMother != mother) {
                continue;
            }
            if (!CanViewerKnowPregnancyImpl(child)) {
                continue;
            }
            visibleFetuses.push_back(&child);
        }

        if (visibleFetuses.empty()) {
            return std::nullopt;
        }

        std::sort(visibleFetuses.begin(), visibleFetuses.end(), [](const State::ChildRecord* a, const State::ChildRecord* b) {
            return a->childId < b->childId;
        });

        PerceivedSelfPregnancy result{};
        result.mother = mother;
        result.motherPartner = world_->relationships->GetPrimaryPartner(mother);
        result.totalFetusCount = static_cast<std::uint32_t>(visibleFetuses.size());

        for (const State::ChildRecord* fetus : visibleFetuses) {
            const PerceivedChildLineage projected = ProjectChildRecord(*fetus);
            const auto& sec = fetus->paternitySecret;

            if (result.motherPartner.IsNull() && !projected.motherPartner.IsNull()) {
                result.motherPartner = projected.motherPartner;
            }

            const bool isMotherPartner =
                (!result.motherPartner.IsNull() &&
                 !projected.perceivedBioFather.IsNull() &&
                 projected.perceivedBioFather == result.motherPartner);

            const bool groupSecret = !projected.hideSecretFromViewer &&
                                     !isMotherPartner &&
                                     ((sec.HasActiveSecret() && !sec.discovered) ||
                                      (!projected.motherPartner.IsNull() &&
                                       IsValidBiologicalParentId(projected.perceivedBioFather) &&
                                       !sec.discovered));
            const bool grpSecPartner = !projected.hideSecretFromViewer && sec.isSecretForPartner && !sec.discoveredByPartner && !sec.discovered;
            const bool grpSecBio = !projected.hideSecretFromViewer && sec.isSecretForBioFather && !sec.discoveredByBioFather && !sec.discovered;
            const bool grpPregSecPartner = !projected.hideSecretFromViewer && grpSecPartner && sec.isPregnancySecretForPartner;

            const State::ActorId groupFather =
                IsValidBiologicalParentId(projected.perceivedBioFather)
                    ? projected.perceivedBioFather
                    : State::ActorId{};
            const std::string fallbackName =
                groupFather.IsNull() ? projected.perceivedBioFatherFallbackName : std::string{};

            auto it = std::find_if(result.groups.begin(), result.groups.end(), [&](const PerceivedPregnancyGroup& g) {
                if (!groupFather.IsNull() || !g.perceivedFather.IsNull()) {
                    return g.perceivedFather == groupFather;
                }
                return g.fatherFallbackName == fallbackName;
            });

            if (it != result.groups.end()) {
                it->fetusCount += 1;
                it->earliestConceptionDay = std::min(it->earliestConceptionDay, fetus->conceptionTimestamp);
                it->isSecret = it->isSecret || groupSecret;
                it->isSecretForPartner = it->isSecretForPartner || grpSecPartner;
                it->isSecretForBioFather = it->isSecretForBioFather || grpSecBio;
                it->isPregnancySecretForPartner = it->isPregnancySecretForPartner || grpPregSecPartner;
                it->fetusIds.push_back(fetus->childId);
            } else {
                PerceivedPregnancyGroup grp{};
                grp.perceivedFather = groupFather;
                grp.fatherFallbackName = fallbackName;
                grp.fetusCount = 1;
                grp.earliestConceptionDay = fetus->conceptionTimestamp;
                grp.isMotherPartner = isMotherPartner;
                grp.isSecret = groupSecret;
                grp.isSecretForPartner = grpSecPartner;
                grp.isSecretForBioFather = grpSecBio;
                grp.isPregnancySecretForPartner = grpPregSecPartner;
                grp.fetusIds.push_back(fetus->childId);
                result.groups.push_back(std::move(grp));
            }

            result.anySecret = result.anySecret || groupSecret;
            result.anySecretForPartner = result.anySecretForPartner || grpSecPartner;
            result.anySecretForBioFather = result.anySecretForBioFather || grpSecBio;
            result.anyPregnancySecretForPartner = result.anyPregnancySecretForPartner || grpPregSecPartner;
            if (groupSecret || (!projected.hideSecretFromViewer && projected.hasUndiscoveredPaternitySecret)) {
                result.hasPaternitySecret = true;
            }
        }

        return result;
    }

    std::vector<PerceivedExpectingPregnancy> PerspectiveView::GetExpectingPregnanciesFor(
        State::ActorId prospectiveParent) const {
        std::vector<PerceivedExpectingPregnancy> out{};
        if (!IsValidBiologicalParentId(prospectiveParent)) {
            return out;
        }

        struct Accum {
            PerceivedExpectingPregnancy entry{};
        };
        std::unordered_map<State::ActorId, Accum> byMother{};

        for (const auto& [childId, child] : world_->lineage->AllChildren()) {
            (void)childId;
            if (child.stage != State::ChildStage::Unborn) {
                continue;
            }
            const State::ActorId mother = child.bioMother;
            if (!IsValidBiologicalParentId(mother) || mother == prospectiveParent) {
                continue;
            }
            if (!CanViewerKnowPregnancyImpl(child)) {
                continue;
            }

            const ResolvedPaternityContext ctx = AnalyzePaternity(child);
            const State::ActorId motherPartner = ctx.cleanMotherPartner;
            const bool isPartner = (!motherPartner.IsNull() && motherPartner == prospectiveParent);
            const bool isBioFather = (IsValidBiologicalParentId(child.bioFather) && child.bioFather == prospectiveParent);

            if (!isPartner && !isBioFather) {
                continue;
            }

            std::optional<ExpectingParentRole> role{};
            State::ActorId perceivedFather{};
            std::string fallbackName{};

            if (isPartner) {
                if (ctx.isBioSameAsPresumed || child.bioFather == prospectiveParent) {
                    if (ctx.pregnancySecretActive && !IsOmniscient()) {
                        continue;
                    }
                    role = ExpectingParentRole::PartnerBioFather;
                    perceivedFather = prospectiveParent;
                } else if (ctx.pregnancySecretActive && !IsOmniscient()) {
                    // Partner does not even know the mother is pregnant yet.
                    continue;
                } else if (ctx.secretFromPartnerActive) {
                    // Announced pregnancy while paternity remains secret from partner.
                    role = ExpectingParentRole::PresumedFatherUnaware;
                    perceivedFather = prospectiveParent;
                } else {
                    role = ExpectingParentRole::PartnerKnowsTruth;
                    if (ctx.canViewerKnow) {
                        perceivedFather = IsValidBiologicalParentId(child.bioFather) ? child.bioFather : State::ActorId{};
                        if (perceivedFather.IsNull() &&
                            !child.paternitySecret.bioFatherName.empty() &&
                            child.paternitySecret.bioFatherName != "Unknown") {
                            fallbackName = child.paternitySecret.bioFatherName;
                        }
                    }
                }
            } else {
                // Non-partner biological father
                if (!ctx.canViewerKnow || (ctx.secretFromBioFatherActive && !IsOmniscient())) {
                    continue;
                }
                role = ExpectingParentRole::BioFatherKnows;
                perceivedFather = prospectiveParent;
            }

            if (!role.has_value()) {
                continue;
            }

            auto [it, inserted] = byMother.try_emplace(mother);
            if (inserted) {
                it->second.entry = PerceivedExpectingPregnancy{
                    .mother = mother,
                    .fetusCount = 1,
                    .earliestConceptionDay = child.conceptionTimestamp,
                    .role = *role,
                    .perceivedBioFather = perceivedFather,
                    .perceivedBioFatherFallbackName = std::move(fallbackName),
                    .motherPartner = motherPartner,
                    .isSecretForPartner = !ctx.hideSecretFromViewer && ctx.secretFromPartnerActive,
                    .isPregnancySecretForPartner = !ctx.hideSecretFromViewer && ctx.pregnancySecretActive,
                    .isSecretForBioFather = !ctx.hideSecretFromViewer && ctx.secretFromBioFatherActive,
                };
            } else {
                auto& e = it->second.entry;
                e.fetusCount += 1;
                e.earliestConceptionDay = std::min(e.earliestConceptionDay, child.conceptionTimestamp);
                e.isSecretForPartner = e.isSecretForPartner || (!ctx.hideSecretFromViewer && ctx.secretFromPartnerActive);
                e.isPregnancySecretForPartner = e.isPregnancySecretForPartner || (!ctx.hideSecretFromViewer && ctx.pregnancySecretActive);
                e.isSecretForBioFather = e.isSecretForBioFather || (!ctx.hideSecretFromViewer && ctx.secretFromBioFatherActive);
            }
        }

        out.reserve(byMother.size());
        for (auto& [motherId, acc] : byMother) {
            (void)motherId;
            out.push_back(std::move(acc.entry));
        }
        std::sort(out.begin(), out.end(), [](const PerceivedExpectingPregnancy& a, const PerceivedExpectingPregnancy& b) {
            return a.mother < b.mother;
        });
        return out;
    }

    std::vector<State::ActorId> PerspectiveView::GetVisibleParentIdsForTrainingDiscount(
        State::ChildId childId) const {
        std::vector<State::ActorId> out{};
        const auto perceivedOpt = GetPerceivedChild(childId);
        if (!perceivedOpt.has_value()) {
            return out;
        }
        const PerceivedChildLineage& child = *perceivedOpt;
        const State::ActorId selfActor = childId.AsActorId();

        PushUniqueValidParent(out, child.guardians.slotA, selfActor);
        PushUniqueValidParent(out, child.guardians.slotB, selfActor);
        PushUniqueValidParent(out, child.bioMother, selfActor);
        PushUniqueValidParent(out, child.perceivedBioFather, selfActor);
        if (!child.hideSecretFromViewer && !child.presumedFather.IsNull()) {
            PushUniqueValidParent(out, child.presumedFather, selfActor);
        }
        return out;
    }

    std::vector<State::ActorId> PerspectiveView::GetVisitableFamilyParentIds(
        State::ChildId childId) const {
        std::vector<State::ActorId> out{};
        const auto perceivedOpt = GetPerceivedChild(childId);
        if (!perceivedOpt.has_value() || perceivedOpt->stage == State::ChildStage::Unborn) {
            return out;
        }

        const PerceivedChildLineage& child = *perceivedOpt;
        const State::ActorId selfActor = childId.AsActorId();

        auto pushIfLiving = [&](State::ActorId pid) {
            if (!IsValidBiologicalParentId(pid) || pid == selfActor || !IsActorAlive(pid)) {
                return;
            }
            PushUniqueValidParent(out, pid, selfActor);
        };

        pushIfLiving(child.guardians.slotA);
        pushIfLiving(child.guardians.slotB);
        pushIfLiving(child.bioMother);
        pushIfLiving(child.perceivedBioFather);

        if (!child.hideSecretFromViewer && !child.presumedFather.IsNull()) {
            const bool isPresumedWithMother =
                child.guardians.HasGuardian(child.presumedFather) ||
                (!child.bioMother.IsNull() &&
                 State::IsActivePartnerStatus(
                     world_->relationships->GetStatus(child.bioMother, child.presumedFather)));
            if (isPresumedWithMother) {
                pushIfLiving(child.presumedFather);
            }
        }
        return out;
    }

    // =========================================================================
    // Relationship & Affair Secret Projection
    // =========================================================================

    bool PerspectiveView::CanViewerKnowAffair(const State::AffairRecord& record) const noexcept {
        if (mode_ == ViewerMode::Omniscient) {
            return true;
        }
        if (!IsSecretAffairRecord(record)) {
            return true;
        }
        switch (mode_) {
            case ViewerMode::Omniscient:
                return true;
            case ViewerMode::DialogueTargetPublic:
            case ViewerMode::DashboardIntrigue:
                return false;
            case ViewerMode::ActorSelf:
                return record.CanObserverKnow(viewer_);
        }
        return false;
    }

    bool PerspectiveView::IsSecretAffairPair(State::ActorId a, State::ActorId b) const {
        if (a.IsNull() || b.IsNull() || a == b) {
            return false;
        }
        std::uint32_t secretCount = 0;
        std::uint32_t publicCount = 0;

        auto tally = [&](const std::vector<State::AffairRecord>& records, State::ActorId expectedParamour) {
            for (const auto& rec : records) {
                if (rec.paramour != expectedParamour) {
                    continue;
                }
                if (IsSecretAffairRecord(rec)) {
                    ++secretCount;
                } else {
                    ++publicCount;
                }
            }
        };

        tally(world_->affairs->GetByCheater(a), b);
        tally(world_->affairs->GetByCheater(b), a);
        return secretCount > 0 && publicCount == 0;
    }

    bool PerspectiveView::HasVisibleOngoingAffairBetween(State::ActorId a, State::ActorId b) const {
        auto checkDirection = [&](State::ActorId cheater, State::ActorId paramour) {
            for (const auto& rec : world_->affairs->GetByCheater(cheater)) {
                if (rec.paramour == paramour &&
                    rec.affairStatus == State::AffairStatus::Ongoing &&
                    CanViewerKnowAffair(rec)) {
                    return true;
                }
            }
            return false;
        };
        return checkDirection(a, b) || checkDirection(b, a);
    }

    bool PerspectiveView::IsVisiblePairCheating(const PerceivedRelationshipEdge& perceived) const {
        const State::ActorId a = perceived.pair.first;
        const State::ActorId b = perceived.pair.second;
        if (HasVisibleOngoingAffairBetween(a, b)) {
            return true;
        }

        // Only evaluate closed-main-tier conflict across edges visible to this viewer (never leak hidden affairs).
        auto isPlayerActiveMain = [&](State::ActorId npc) {
            if (npc.IsNull() || npc.IsPlayer()) {
                return false;
            }
            return State::IsMainTierStatus(world_->relationships->GetStatus(State::ActorId::Player(), npc));
        };

        const bool bothCoSpouses = isPlayerActiveMain(a) && isPlayerActiveMain(b);
        auto canKnowPairSecret = [&](State::ActorId x, State::ActorId y) {
            if (mode_ == ViewerMode::Omniscient) {
                return true;
            }
            if (!IsSecretAffairPair(x, y)) {
                return true;
            }
            return mode_ == ViewerMode::ActorSelf && !viewer_.IsNull() && (viewer_ == x || viewer_ == y);
        };

        auto hasOtherClosedMain = [&](State::ActorId self, State::ActorId partner) {
            if (self.IsPlayer() && world_->settings->enablePlayerPolygamy) {
                return false;
            }
            for (const State::CanonicalPairKey& key : world_->relationships->GetAdjacentPairs(self)) {
                if (key.Contains(partner)) {
                    continue;
                }
                if (!canKnowPairSecret(key.first, key.second)) {
                    continue;
                }
                const auto* edge = world_->relationships->FindEdge(key);
                if (!edge || !State::IsMainTierStatus(edge->status) || edge->isOpen) {
                    continue;
                }
                if (bothCoSpouses && key.Other(self).IsPlayer()) {
                    continue;
                }
                return true;
            }
            return false;
        };

        if (State::IsActivePartnerStatus(perceived.status) &&
            (hasOtherClosedMain(a, b) || hasOtherClosedMain(b, a))) {
            return true;
        }
        return false;
    }

    std::optional<PerceivedRelationshipEdge> PerspectiveView::GetPerceivedPair(
        State::ActorId a,
        State::ActorId b) const {
        const auto* raw = world_->relationships->FindEdge(a, b);
        if (!raw) {
            return std::nullopt;
        }

        PerceivedRelationshipEdge out{};
        out.pair = raw->pair;
        out.status = raw->status;
        out.originalStatus = raw->originalStatus;
        out.isOpen = raw->isOpen;
        out.statusHistory = raw->statusHistory;

        const bool isSecretPair = IsSecretAffairPair(raw->pair.first, raw->pair.second);
        const bool isPrivyToSecretPair =
            (mode_ == ViewerMode::Omniscient) ||
            (mode_ == ViewerMode::ActorSelf &&
             !viewer_.IsNull() &&
             (viewer_ == raw->pair.first || viewer_ == raw->pair.second));

        if (!isSecretPair) {
            out.isSecretAffair = false;
            out.isCheatingPair = IsVisiblePairCheating(out);
            return out;
        }

        if (isPrivyToSecretPair) {
            out.isSecretAffair = true;
            out.isCheatingPair = IsVisiblePairCheating(out);
            return out;
        }

        if (!IsAffairMaskEligibleStatus(raw->status)) {
            // Non-affair status (e.g., Married) keeps visible bond without flagging hidden affair.
            out.isSecretAffair = false;
            out.isCheatingPair = false;
            return out;
        }

        if (const auto prior = FindMostRecentPriorMainOrBreakupStatus(raw->statusHistory); prior.has_value()) {
            out.status = *prior;
            out.isOpen = false;
            out.isSecretAffair = false;
            out.isCheatingPair = false;
            StripAffairTransitions(raw->statusHistory, out.statusHistory);
            return out;
        }

        if (mode_ == ViewerMode::DashboardIntrigue &&
            (raw->pair.first.IsPlayer() || raw->pair.second.IsPlayer())) {
            out.status = State::RelationshipStatus::Candidate;
            out.isOpen = false;
            out.isSecretAffair = false;
            out.isCheatingPair = false;
            out.statusHistory.clear();
            return out;
        }

        return std::nullopt;
    }

    std::vector<PerceivedRelationshipEdge> PerspectiveView::GetPerceivedRelationshipsOf(
        State::ActorId subject) const {
        std::vector<PerceivedRelationshipEdge> out{};
        if (subject.IsNull()) {
            return out;
        }

        const auto& adj = world_->relationships->GetAdjacentPairs(subject);
        out.reserve(adj.size());
        for (const State::CanonicalPairKey& key : adj) {
            if (auto projected = GetPerceivedPair(key.first, key.second); projected.has_value()) {
                out.push_back(std::move(*projected));
            }
        }
        std::sort(out.begin(), out.end(), [subject](const PerceivedRelationshipEdge& lhs, const PerceivedRelationshipEdge& rhs) {
            return lhs.Other(subject) < rhs.Other(subject);
        });
        return out;
    }

    std::vector<PerceivedRelationshipEdge> PerspectiveView::GetVisibleRelationshipEdges() const {
        std::vector<PerceivedRelationshipEdge> out{};
        const auto& edges = world_->relationships->AllEdges();
        out.reserve(edges.size());
        for (const auto& [key, edge] : edges) {
            (void)edge;
            if (auto projected = GetPerceivedPair(key.first, key.second); projected.has_value()) {
                out.push_back(std::move(*projected));
            }
        }
        std::sort(out.begin(), out.end(), [](const PerceivedRelationshipEdge& lhs, const PerceivedRelationshipEdge& rhs) {
            return lhs.pair < rhs.pair;
        });
        return out;
    }

    std::vector<PerceivedAffair> PerspectiveView::GetKnownAffairsInvolving(State::ActorId actor) const {
        std::vector<PerceivedAffair> out{};
        if (actor.IsNull()) {
            return out;
        }
        for (const State::AffairRecord& rec : world_->affairs->GetAllInvolving(actor)) {
            if (CanViewerKnowAffair(rec)) {
                out.push_back(ToPerceivedAffair(rec));
            }
        }
        std::sort(out.begin(), out.end(), [](const PerceivedAffair& lhs, const PerceivedAffair& rhs) {
            return lhs.id < rhs.id;
        });
        return out;
    }

    std::vector<PerceivedAffair> PerspectiveView::GetAllVisibleAffairs() const {
        std::vector<PerceivedAffair> out{};
        for (const auto& [id, rec] : world_->affairs->AllRecords()) {
            (void)id;
            if (CanViewerKnowAffair(rec)) {
                out.push_back(ToPerceivedAffair(rec));
            }
        }
        std::sort(out.begin(), out.end(), [](const PerceivedAffair& lhs, const PerceivedAffair& rhs) {
            return lhs.id < rhs.id;
        });
        return out;
    }

    State::PendingPartnerIntentRecord PerspectiveView::GetPerceivedPendingIntent(
        State::ActorId partner) const noexcept {
        if (partner.IsNull()) {
            return State::PendingPartnerIntentRecord{};
        }
        const State::PendingPartnerIntentRecord intent = world_->relationships->GetPendingIntent(partner);
        const bool isSecretConfessIntent =
            intent.type == State::PartnerIntentType::ConfessTalking ||
            intent.type == State::PartnerIntentType::ConfessPhysical;
        if (!isSecretConfessIntent) {
            return intent;
        }

        if (mode_ == ViewerMode::Omniscient ||
            (mode_ == ViewerMode::ActorSelf && viewer_ == partner)) {
            return intent;
        }

        return State::PendingPartnerIntentRecord{};
    }

}  // namespace MARAS::Projection
