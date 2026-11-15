#include "maras/projection/EventDispatchPlan.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace MARAS::Projection {

    namespace {

        [[nodiscard]] bool SafeCanTrigger(
            const NarrationProximityFn& canTrigger,
            State::ActorId speaker,
            State::ActorId target) {
            return static_cast<bool>(canTrigger) && canTrigger(speaker, target);
        }

        void AppendUniquePhysical(std::vector<State::ActorId>& out, State::ActorId id) {
            if (!id.HasPhysicalActor()) {
                return;
            }
            if (std::find(out.begin(), out.end(), id) == out.end()) {
                out.push_back(id);
            }
        }

        [[nodiscard]] bool ContainsActorId(
            const std::vector<State::ActorId>& list,
            State::ActorId id) noexcept {
            return std::find(list.begin(), list.end(), id) != list.end();
        }

        [[nodiscard]] std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            for (char& ch : out) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return out;
        }

        [[nodiscard]] ResolvedChildSecrecy ResolveFromPerceived(
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            const PerceivedChildLineage& perceived) {
            ResolvedChildSecrecy result{};
            result.found = true;
            result.stage = perceived.stage;
            result.mother = perceived.bioMother.HasPhysicalActor() ? perceived.bioMother : State::ActorId{};

            result.presumedFather = perceived.motherPartner;
            if (result.presumedFather == result.mother || !result.presumedFather.HasPhysicalActor()) {
                result.presumedFather = {};
            }

            const State::ActorId rawBio = perceived.perceivedBioFather;
            result.bioFather = rawBio.HasPhysicalActor() ? rawBio : State::ActorId{};

            if (!result.presumedFather.IsNull()) {
                result.isSecretForAssumed =
                    !PerspectiveView::ForActor(world, spatial, result.presumedFather)
                         .CanViewerKnowTrueBioFather(perceived.childId);
            }

            if (!result.bioFather.IsNull() && result.bioFather != result.presumedFather) {
                result.isSecretForBio =
                    !PerspectiveView::ForActor(world, spatial, result.bioFather)
                         .CanViewerKnowTrueBioFather(perceived.childId);
            } else if (result.bioFather.IsNull() && perceived.paternitySecret.has_value()) {
                const auto& sec = *perceived.paternitySecret;
                result.isSecretForBio =
                    !sec.discovered && sec.isSecretForBioFather && !sec.discoveredByBioFather;
            }

            if (perceived.stage == State::ChildStage::Unborn) {
                if (!result.presumedFather.IsNull()) {
                    result.isPregnancySecretForAssumed =
                        !PerspectiveView::ForActor(world, spatial, result.presumedFather)
                             .CanViewerKnowPregnancy(perceived.childId);
                } else if (perceived.paternitySecret.has_value()) {
                    const auto& sec = *perceived.paternitySecret;
                    result.isPregnancySecretForAssumed =
                        !sec.discovered && sec.isPregnancySecretForPartner && !sec.discoveredByPartner;
                }
            }

            return result;
        }

        [[nodiscard]] EventDispatchPlan MakeSingleSimpleLegPlan(
            SkyrimNetEventKind kind,
            State::ActorId originator,
            State::ActorId target,
            bool skipTrigger,
            float postSuppressSeconds = 0.0f) {
            EventDispatchPlan plan{};
            plan.kind = kind;
            plan.shouldEmit = true;
            plan.postSuppressNarrationSeconds = postSuppressSeconds;
            plan.legs.push_back(EventDeliveryLeg{
                .originator = originator,
                .target = target,
                .exactAudience = false,
                .audience = {},
                .skipTrigger = skipTrigger,
                .maskMode = DispatchMaskMode::Unmasked
            });
            return plan;
        }

        [[nodiscard]] EventDispatchPlan PlanChildMilestone(
            SkyrimNetEventKind kind,
            std::shared_ptr<const State::WorldStateSnapshot> world,
            std::shared_ptr<const State::SpatialActorSnapshot> spatial,
            State::ChildId childId,
            const NarrationProximityFn& canTrigger) {
            EventDispatchPlan plan{};
            plan.kind = kind;
            if (!world || !spatial) {
                return plan;
            }

            const ResolvedChildSecrecy sec = ResolveChildSecrecy(world, spatial, childId);
            if (!sec.found) {
                return plan;
            }

            const State::ActorId childActor =
                childId.HasPhysicalActor() ? State::ActorId{ childId.raw() } : State::ActorId{};
            const bool shouldTrigger =
                (!childActor.IsNull() && SafeCanTrigger(canTrigger, childActor, {})) ||
                (!sec.mother.IsNull() && SafeCanTrigger(canTrigger, sec.mother, {}));
            const bool unmaskedSkip = !shouldTrigger;

            // Never fall back to an unaware Player bioFather as event speaker.
            State::ActorId speaker = sec.mother;
            if (speaker.IsNull()) {
                speaker = sec.presumedFather;
            }
            if (speaker.IsNull()) {
                speaker = childActor;
            }
            if (speaker.IsNull() && !(sec.isSecretForBio && sec.bioFather.IsPlayer())) {
                speaker = State::ActorId::Player();
            }

            const State::ActorId awareBio = !sec.isSecretForBio ? sec.bioFather : State::ActorId{};

            if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
                if (!sec.mother.IsNull() || !awareBio.IsNull()) {
                    std::vector<State::ActorId> aud1{};
                    AppendUniquePhysical(aud1, sec.mother);
                    AppendUniquePhysical(aud1, awareBio);
                    const State::ActorId leg1Speaker =
                        !sec.mother.IsNull() ? sec.mother : awareBio;
                    plan.legs.push_back(EventDeliveryLeg{
                        .originator = leg1Speaker,
                        .target = {},
                        .exactAudience = true,
                        .audience = std::move(aud1),
                        .skipTrigger = unmaskedSkip,
                        .maskMode = DispatchMaskMode::Unmasked
                    });
                }

                std::vector<State::ActorId> aud2{};
                AppendUniquePhysical(aud2, sec.presumedFather);
                AppendUniquePhysical(aud2, childActor);
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = sec.presumedFather,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud2),
                    .skipTrigger = true,
                    .maskMode = DispatchMaskMode::MaskForPartner
                });
            } else if (sec.isSecretForBio) {
                if (!sec.mother.IsNull() || !sec.presumedFather.IsNull()) {
                    const State::ActorId secretBioSpeaker =
                        !sec.mother.IsNull() ? sec.mother : sec.presumedFather;
                    std::vector<State::ActorId> aud1{};
                    AppendUniquePhysical(aud1, sec.mother);
                    AppendUniquePhysical(aud1, sec.presumedFather);
                    plan.legs.push_back(EventDeliveryLeg{
                        .originator = secretBioSpeaker,
                        .target = {},
                        .exactAudience = true,
                        .audience = std::move(aud1),
                        .skipTrigger = unmaskedSkip,
                        .maskMode = DispatchMaskMode::Unmasked
                    });
                }

                if (!childActor.IsNull()) {
                    plan.legs.push_back(EventDeliveryLeg{
                        .originator = childActor,
                        .target = {},
                        .exactAudience = true,
                        .audience = { childActor },
                        .skipTrigger = true,
                        .maskMode = DispatchMaskMode::MaskForChildOnly
                    });
                }
            } else if (!speaker.IsNull()) {
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = speaker,
                    .target = childActor,
                    .exactAudience = false,
                    .audience = {},
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }

            plan.shouldEmit = !plan.legs.empty();
            return plan;
        }

    }  // namespace

    ResolvedChildSecrecy ResolveChildSecrecy(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId) {
        if (!world || !spatial) {
            return {};
        }
        const auto omniscient =
            PerspectiveView::ForDashboard(world, spatial, /*revealSecretsOverride=*/true);
        const auto perceived = omniscient.GetPerceivedChild(childId);
        if (!perceived.has_value()) {
            return {};
        }
        return ResolveFromPerceived(world, spatial, *perceived);
    }

    EventDispatchPlan PlanDirectNarration(
        State::ActorId originator,
        State::ActorId target,
        bool exactAudience,
        const std::vector<State::ActorId>& rawAudience,
        const NarrationProximityFn& canTrigger) {
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::DirectNarration;

        const State::ActorId cleanOriginator = originator.HasPhysicalActor() ? originator : State::ActorId{};
        const State::ActorId cleanTarget = target.HasPhysicalActor() ? target : State::ActorId{};
        if (!originator.IsNull() &&
            (cleanOriginator.IsNull() || !SafeCanTrigger(canTrigger, cleanOriginator, cleanTarget))) {
            return plan;
        }

        std::vector<State::ActorId> aud{};
        for (const auto id : rawAudience) {
            AppendUniquePhysical(aud, id);
        }

        plan.shouldEmit = true;
        plan.legs.push_back(EventDeliveryLeg{
            .originator = cleanOriginator,
            .target = cleanTarget,
            .exactAudience = exactAudience,
            .audience = std::move(aud),
            .skipTrigger = false,
            .maskMode = DispatchMaskMode::Unmasked
        });
        return plan;
    }

    EventDispatchPlan PlanPolygamyBlessing(
        State::ActorId player,
        State::ActorId spouse,
        const NarrationProximityFn& canTrigger) {
        if (!player.HasPhysicalActor() || !spouse.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::PolygamyBlessing };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::PolygamyBlessing,
            player,
            spouse,
            !SafeCanTrigger(canTrigger, player, spouse));
    }

    EventDispatchPlan PlanAffectionGift(
        State::ActorId player,
        State::ActorId receiver,
        const NarrationProximityFn& canTrigger) {
        if (!player.HasPhysicalActor() || !receiver.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::AffectionGift };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::AffectionGift,
            player,
            receiver,
            !SafeCanTrigger(canTrigger, player, receiver));
    }

    EventDispatchPlan PlanMaritalStatusChange(
        State::ActorId initiator,
        State::ActorId acceptor,
        std::string_view newStatus,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::MaritalStatusChange;

        const std::string lowerStatus = ToLowerAscii(newStatus);
        if (lowerStatus.empty() || lowerStatus == "candidate" ||
            !initiator.HasPhysicalActor() || !acceptor.HasPhysicalActor()) {
            return plan;
        }

        const bool skip =
            forceSkipTrigger || narrationSuppressed || !SafeCanTrigger(canTrigger, initiator, acceptor);
        const float suppressSeconds =
            (!skip && lowerStatus != "lover" && lowerStatus != "engaged") ? 3.0f : 0.0f;
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::MaritalStatusChange,
            initiator,
            acceptor,
            skip,
            suppressSeconds);
    }

    EventDispatchPlan PlanOpenRelationshipChange(
        State::ActorId initiator,
        State::ActorId partner,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!initiator.HasPhysicalActor() || !partner.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::OpenRelationshipChange };
        }
        const bool skip =
            forceSkipTrigger || narrationSuppressed || !SafeCanTrigger(canTrigger, initiator, partner);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::OpenRelationshipChange,
            initiator,
            partner,
            skip,
            skip ? 0.0f : 3.0f);
    }

    EventDispatchPlan PlanSpouseHierarchyChange(
        State::ActorId player,
        State::ActorId spouse,
        const NarrationProximityFn& canTrigger) {
        if (!player.HasPhysicalActor() || !spouse.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::SpouseHierarchyChange };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::SpouseHierarchyChange,
            player,
            spouse,
            !SafeCanTrigger(canTrigger, player, spouse));
    }

    EventDispatchPlan PlanSpouseAffectionChange(
        State::ActorId spouse,
        State::ActorId player,
        const NarrationProximityFn& canTrigger) {
        if (!spouse.HasPhysicalActor() || !player.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::SpouseAffectionChange };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::SpouseAffectionChange,
            spouse,
            player,
            !SafeCanTrigger(canTrigger, spouse, player));
    }

    EventDispatchPlan PlanProposalDecision(
        State::ActorId initiator,
        State::ActorId acceptor,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!initiator.HasPhysicalActor() || !acceptor.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::ProposalDecision };
        }
        const bool skip =
            forceSkipTrigger || narrationSuppressed || !SafeCanTrigger(canTrigger, initiator, acceptor);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::ProposalDecision,
            initiator,
            acceptor,
            skip);
    }

    EventDispatchPlan PlanActionDecline(
        State::ActorId speaker,
        State::ActorId target,
        bool skipTrigger,
        const NarrationProximityFn& canTrigger) {
        if (!speaker.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::ActionDecline };
        }
        const State::ActorId cleanTarget = target.HasPhysicalActor() ? target : State::ActorId{};
        const bool skip = skipTrigger || !SafeCanTrigger(canTrigger, speaker, cleanTarget);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::ActionDecline,
            speaker,
            cleanTarget,
            skip);
    }

    EventDispatchPlan PlanChildGuardianEvent(
        SkyrimNetEventKind kind,
        State::ActorId childActor,
        State::ActorId primaryGuardian,
        State::ActorId secondaryGuardian,
        std::string_view reason,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!childActor.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = kind };
        }

        const bool isCascade = (kind == SkyrimNetEventKind::ChildParentsChanged) &&
                               (reason == "marriage" || reason == "divorce" || narrationSuppressed);

        const bool pAPresent = primaryGuardian.HasPhysicalActor() &&
                               SafeCanTrigger(canTrigger, primaryGuardian, {});
        const bool pBPresent = secondaryGuardian.HasPhysicalActor() &&
                               secondaryGuardian != primaryGuardian &&
                               SafeCanTrigger(canTrigger, secondaryGuardian, {});

        State::ActorId speaker{};
        State::ActorId target{};
        bool skip = true;

        if (pAPresent && pBPresent) {
            speaker = primaryGuardian;
            target = secondaryGuardian;
            skip = isCascade;
        } else if (pAPresent) {
            speaker = primaryGuardian;
            target = SafeCanTrigger(canTrigger, primaryGuardian, childActor) ? childActor : State::ActorId{};
            skip = isCascade;
        } else if (pBPresent) {
            speaker = secondaryGuardian;
            target = SafeCanTrigger(canTrigger, secondaryGuardian, childActor) ? childActor : State::ActorId{};
            skip = isCascade;
        } else {
            speaker = primaryGuardian.HasPhysicalActor() ? primaryGuardian : secondaryGuardian;
            target = (secondaryGuardian.HasPhysicalActor() && secondaryGuardian != speaker)
                         ? secondaryGuardian
                         : childActor;
            skip = true;
        }

        return MakeSingleSimpleLegPlan(kind, speaker, target, skip);
    }

    EventDispatchPlan PlanPregnancyAnnounced(
        State::ActorId mother,
        const NarrationProximityFn& canTrigger) {
        if (!mother.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::PregnancyAnnounced };
        }
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::PregnancyAnnounced;
        plan.shouldEmit = true;
        plan.legs.push_back(EventDeliveryLeg{
            .originator = mother,
            .target = {},
            .exactAudience = true,
            .audience = { mother },
            .skipTrigger = !SafeCanTrigger(canTrigger, mother, {}),
            .maskMode = DispatchMaskMode::Unmasked
        });
        return plan;
    }

    EventDispatchPlan PlanPregnancyLost(
        State::ActorId mother,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger) {
        if (!mother.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::PregnancyLost };
        }
        const bool skip = !SafeCanTrigger(canTrigger, mother, {});
        const State::ActorId eventTarget =
            (partner.HasPhysicalActor() && SafeCanTrigger(canTrigger, mother, partner))
                ? partner
                : State::ActorId{};
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::PregnancyLost,
            mother,
            eventTarget,
            skip);
    }

    EventDispatchPlan PlanLaborStarted(
        State::ActorId mother,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger) {
        if (!mother.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::LaborStarted };
        }
        const bool skip = !SafeCanTrigger(canTrigger, mother, {});
        const State::ActorId eventTarget =
            (partner.HasPhysicalActor() && SafeCanTrigger(canTrigger, mother, partner))
                ? partner
                : State::ActorId{};
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::LaborStarted,
            mother,
            eventTarget,
            skip,
            skip ? 0.0f : 3.0f);
    }

    EventDispatchPlan PlanLaborProgress(State::ActorId mother) {
        if (!mother.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::LaborProgress };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::LaborProgress,
            mother,
            {},
            /*skipTrigger=*/true);
    }

    EventDispatchPlan PlanChildBirth(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ActorId mother,
        const std::vector<State::ChildId>& childIds,
        const NarrationProximityFn& canTrigger) {
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::ChildBirth;
        if (!world || !spatial) {
            return plan;
        }

        if (mother.HasPhysicalActor()) {
            const auto omniscient =
                PerspectiveView::ForDashboard(world, spatial, /*revealSecretsOverride=*/true);

            std::vector<ResolvedChildSecrecy> resolvedChildren{};
            if (!childIds.empty()) {
                for (const auto childId : childIds) {
                    if (const auto perceived = omniscient.GetPerceivedChild(childId); perceived.has_value()) {
                        resolvedChildren.push_back(ResolveFromPerceived(world, spatial, *perceived));
                    }
                }
            } else {
                const auto allVisible = omniscient.GetVisibleChildren();
                std::vector<PerceivedChildLineage> matching{};
                for (const auto& entry : allVisible) {
                    if ((entry.bioMother == mother || entry.guardians.HasGuardian(mother)) &&
                        (entry.stage == State::ChildStage::Infant || entry.stage == State::ChildStage::Child)) {
                        matching.push_back(entry);
                    }
                }
                std::sort(matching.begin(), matching.end(), [](const auto& a, const auto& b) {
                    return a.childId.raw() < b.childId.raw();
                });
                for (const auto& entry : matching) {
                    resolvedChildren.push_back(ResolveFromPerceived(world, spatial, entry));
                }
            }

            if (resolvedChildren.empty()) {
                return plan;
            }

            State::ActorId partner =
                world->relationships ? world->relationships->GetPrimaryPartner(mother) : State::ActorId{};
            if (partner.IsNull()) {
                for (const auto& sec : resolvedChildren) {
                    if (!sec.presumedFather.IsNull() && sec.presumedFather != mother) {
                        partner = sec.presumedFather;
                        break;
                    }
                }
            }
            if (partner == mother || !partner.HasPhysicalActor()) {
                partner = {};
            }

            State::ActorId firstBioFather{};
            bool anySecretForPartner = false;
            bool anySecretForBio = false;
            State::ActorId awareBioFather{};

            for (const auto& sec : resolvedChildren) {
                if (firstBioFather.IsNull() && !sec.bioFather.IsNull()) {
                    firstBioFather = sec.bioFather;
                }
                if (sec.isSecretForAssumed) {
                    anySecretForPartner = true;
                }
                if (sec.isSecretForBio) {
                    anySecretForBio = true;
                }
                if (!sec.isSecretForBio && !sec.bioFather.IsNull() && sec.bioFather != partner &&
                    awareBioFather.IsNull()) {
                    awareBioFather = sec.bioFather;
                }
            }

            const bool unmaskedSkip = !SafeCanTrigger(canTrigger, mother, {});
            if (!anySecretForPartner && !anySecretForBio) {
                const State::ActorId publicTarget = !partner.IsNull() ? partner : firstBioFather;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = mother,
                    .target = publicTarget,
                    .exactAudience = false,
                    .audience = {},
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            } else {
                const State::ActorId privyPartner = !anySecretForPartner ? partner : State::ActorId{};
                std::vector<State::ActorId> aud1{};
                AppendUniquePhysical(aud1, mother);
                AppendUniquePhysical(aud1, privyPartner);
                AppendUniquePhysical(aud1, awareBioFather);
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = mother,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud1),
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });

                if (anySecretForPartner && partner.HasPhysicalActor()) {
                    const bool maskedSkip =
                        mother.IsPlayer() ? !SafeCanTrigger(canTrigger, partner, mother) : true;
                    plan.legs.push_back(EventDeliveryLeg{
                        .originator = partner,
                        .target = {},
                        .exactAudience = true,
                        .audience = { partner },
                        .skipTrigger = maskedSkip,
                        .maskMode = DispatchMaskMode::MaskForPartner
                    });
                }
            }

            plan.shouldEmit = !plan.legs.empty();
            return plan;
        }

        // Case B: Motherless single-child fallback
        if (childIds.size() != 1) {
            return plan;
        }

        const ResolvedChildSecrecy sec = ResolveChildSecrecy(world, spatial, childIds[0]);
        if (!sec.found) {
            return plan;
        }

        State::ActorId speaker = sec.presumedFather;
        if (speaker.IsNull() && !(sec.isSecretForBio && sec.bioFather.IsPlayer())) {
            speaker = State::ActorId::Player();
        }
        constexpr bool unmaskedSkip = true;

        if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
            const State::ActorId awareBio = !sec.isSecretForBio ? sec.bioFather : State::ActorId{};
            if (!awareBio.IsNull()) {
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = awareBio,
                    .target = {},
                    .exactAudience = true,
                    .audience = { awareBio },
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }
            plan.legs.push_back(EventDeliveryLeg{
                .originator = sec.presumedFather,
                .target = {},
                .exactAudience = true,
                .audience = { sec.presumedFather },
                .skipTrigger = true,
                .maskMode = DispatchMaskMode::MaskForPartner
            });
        } else if (sec.isSecretForBio) {
            if (!sec.presumedFather.IsNull()) {
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = sec.presumedFather,
                    .target = {},
                    .exactAudience = true,
                    .audience = { sec.presumedFather },
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }
        } else if (!speaker.IsNull()) {
            plan.legs.push_back(EventDeliveryLeg{
                .originator = speaker,
                .target = sec.presumedFather,
                .exactAudience = false,
                .audience = {},
                .skipTrigger = unmaskedSkip,
                .maskMode = DispatchMaskMode::Unmasked
            });
        }

        plan.shouldEmit = !plan.legs.empty();
        return plan;
    }

    EventDispatchPlan PlanChildGrewUp(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger) {
        return PlanChildMilestone(SkyrimNetEventKind::ChildGrewUp, world, spatial, childId, canTrigger);
    }

    EventDispatchPlan PlanChildBecameAdult(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger) {
        return PlanChildMilestone(SkyrimNetEventKind::ChildBecameAdult, world, spatial, childId, canTrigger);
    }

    EventDispatchPlan PlanChildSentToTraining(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger) {
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::ChildSentToTraining;
        if (!world || !spatial) {
            return plan;
        }

        const ResolvedChildSecrecy sec = ResolveChildSecrecy(world, spatial, childId);
        if (!sec.found) {
            return plan;
        }

        const bool unmaskedSkip = sec.mother.IsNull() || !SafeCanTrigger(canTrigger, sec.mother, {});

        State::ActorId speaker = sec.mother;
        if (speaker.IsNull()) {
            speaker = sec.presumedFather;
        }
        if (speaker.IsNull() && !(sec.isSecretForBio && sec.bioFather.IsPlayer())) {
            speaker = State::ActorId::Player();
        }

        const State::ActorId awareBio = !sec.isSecretForBio ? sec.bioFather : State::ActorId{};

        if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
            if (!sec.mother.IsNull() || !awareBio.IsNull()) {
                std::vector<State::ActorId> aud1{};
                AppendUniquePhysical(aud1, sec.mother);
                AppendUniquePhysical(aud1, awareBio);
                const State::ActorId leg1Speaker =
                    !sec.mother.IsNull() ? sec.mother : awareBio;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = leg1Speaker,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud1),
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }
            plan.legs.push_back(EventDeliveryLeg{
                .originator = sec.presumedFather,
                .target = {},
                .exactAudience = true,
                .audience = { sec.presumedFather },
                .skipTrigger = true,
                .maskMode = DispatchMaskMode::MaskForPartner
            });
        } else if (sec.isSecretForBio) {
            if (!sec.mother.IsNull() || !sec.presumedFather.IsNull()) {
                std::vector<State::ActorId> aud1{};
                AppendUniquePhysical(aud1, sec.mother);
                AppendUniquePhysical(aud1, sec.presumedFather);
                const State::ActorId leg1Speaker = !sec.mother.IsNull() ? sec.mother : sec.presumedFather;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = leg1Speaker,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud1),
                    .skipTrigger = unmaskedSkip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }
        } else if (!speaker.IsNull()) {
            plan.legs.push_back(EventDeliveryLeg{
                .originator = speaker,
                .target = sec.presumedFather,
                .exactAudience = false,
                .audience = {},
                .skipTrigger = unmaskedSkip,
                .maskMode = DispatchMaskMode::Unmasked
            });
        }

        plan.shouldEmit = !plan.legs.empty();
        return plan;
    }

    EventDispatchPlan PlanPaternitySecretResolved(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        std::string_view rawCaseType,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        EventDispatchPlan plan{};
        plan.kind = SkyrimNetEventKind::PaternitySecretResolved;
        if (!world || !spatial) {
            return plan;
        }

        const ResolvedChildSecrecy sec = ResolveChildSecrecy(world, spatial, childId);
        if (!sec.found) {
            return plan;
        }

        if (rawCaseType == "mother_confessed_presumed_unannounced") {
            plan.canonicalOutcome = "mother_confessed_presumed";
            plan.wasPregnancyUnannounced = true;
        } else if (rawCaseType == "presumed_father_learnt_unannounced") {
            plan.canonicalOutcome = "presumed_father_learnt";
            plan.wasPregnancyUnannounced = true;
        } else if (rawCaseType == "mother_confessed_both_unannounced") {
            plan.canonicalOutcome = "mother_confessed_both";
            plan.wasPregnancyUnannounced = true;
        } else {
            plan.canonicalOutcome = std::string(rawCaseType);
            plan.wasPregnancyUnannounced = false;
        }

        const bool stillSecretAssumed = sec.isSecretForAssumed;
        const bool stillSecretBio = sec.isSecretForBio;

        if (plan.canonicalOutcome == "mother_shared_pregnancy_presumed") {
            const State::ActorId awareBio = !stillSecretBio ? sec.bioFather : State::ActorId{};
            const bool baseSkip =
                narrationSuppressed || !SafeCanTrigger(canTrigger, sec.mother, sec.presumedFather);
            const bool skipMother = sec.mother.IsPlayer() ? true : baseSkip;

            if (!sec.mother.IsNull() || !awareBio.IsNull()) {
                std::vector<State::ActorId> aud1{};
                AppendUniquePhysical(aud1, sec.mother);
                AppendUniquePhysical(aud1, awareBio);
                const State::ActorId leg1Speaker = !sec.mother.IsNull() ? sec.mother : awareBio;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = leg1Speaker,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud1),
                    .skipTrigger = skipMother,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }

            if (!sec.presumedFather.IsNull()) {
                const bool skipPartner = (sec.mother.IsPlayer() && !narrationSuppressed)
                                             ? !SafeCanTrigger(canTrigger, sec.presumedFather, sec.mother)
                                             : true;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = sec.presumedFather,
                    .target = {},
                    .exactAudience = true,
                    .audience = { sec.presumedFather },
                    .skipTrigger = skipPartner,
                    .maskMode = DispatchMaskMode::MaskForPartner
                });
            }
        } else if (plan.canonicalOutcome == "pregnancy_visible_presumed") {
            const State::ActorId awareBio = !stillSecretBio ? sec.bioFather : State::ActorId{};
            const State::ActorId pSpeaker =
                !sec.presumedFather.IsNull() ? sec.presumedFather : sec.mother;
            const bool partnerSkip =
                narrationSuppressed || !SafeCanTrigger(canTrigger, pSpeaker, sec.mother);

            if (!sec.mother.IsNull() || !awareBio.IsNull()) {
                std::vector<State::ActorId> aud1{};
                AppendUniquePhysical(aud1, sec.mother);
                AppendUniquePhysical(aud1, awareBio);
                const State::ActorId leg1Speaker = !sec.mother.IsNull() ? sec.mother : awareBio;
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = leg1Speaker,
                    .target = {},
                    .exactAudience = true,
                    .audience = std::move(aud1),
                    .skipTrigger = true,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }

            if (!sec.presumedFather.IsNull()) {
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = sec.presumedFather,
                    .target = {},
                    .exactAudience = true,
                    .audience = { sec.presumedFather },
                    .skipTrigger = partnerSkip,
                    .maskMode = DispatchMaskMode::MaskForPartner
                });
            }
        } else {
            State::ActorId speaker{};
            State::ActorId target{};
            bool skip = true;
            bool exact = false;
            std::vector<State::ActorId> aud{};

            if (plan.canonicalOutcome == "mother_confessed_bio") {
                speaker = sec.mother;
                target = sec.bioFather;
                skip = narrationSuppressed || !SafeCanTrigger(canTrigger, sec.mother, sec.bioFather);
                if (stillSecretAssumed) {
                    exact = true;
                    AppendUniquePhysical(aud, sec.mother);
                    AppendUniquePhysical(aud, sec.bioFather);
                }
            } else if (plan.canonicalOutcome == "mother_confessed_presumed") {
                speaker = sec.mother;
                target = sec.presumedFather;
                skip = narrationSuppressed || !SafeCanTrigger(canTrigger, sec.mother, sec.presumedFather);
                if (stillSecretBio) {
                    exact = true;
                    AppendUniquePhysical(aud, sec.mother);
                    AppendUniquePhysical(aud, sec.presumedFather);
                }
            } else if (plan.canonicalOutcome == "mother_confessed_both") {
                speaker = sec.mother;
                target = {};
                skip = narrationSuppressed ||
                       !(SafeCanTrigger(canTrigger, sec.mother, sec.presumedFather) ||
                         SafeCanTrigger(canTrigger, sec.mother, sec.bioFather));
                exact = false;
            } else if (plan.canonicalOutcome == "bio_father_learnt") {
                speaker = sec.bioFather;
                target = sec.mother;
                skip = narrationSuppressed || !SafeCanTrigger(canTrigger, sec.bioFather, {});
                if (!narrationSuppressed && !target.IsNull() &&
                    !SafeCanTrigger(canTrigger, sec.bioFather, target)) {
                    target = {};
                }
                if (stillSecretAssumed) {
                    exact = true;
                    AppendUniquePhysical(aud, sec.bioFather);
                    AppendUniquePhysical(aud, sec.mother);
                }
            } else if (plan.canonicalOutcome == "presumed_father_learnt") {
                speaker = sec.presumedFather;
                target = sec.mother;
                skip = narrationSuppressed || !SafeCanTrigger(canTrigger, sec.presumedFather, {});
                if (!narrationSuppressed && !target.IsNull() &&
                    !SafeCanTrigger(canTrigger, sec.presumedFather, target)) {
                    target = {};
                }
                if (stillSecretBio) {
                    exact = true;
                    AppendUniquePhysical(aud, sec.presumedFather);
                    AppendUniquePhysical(aud, sec.mother);
                }
            } else if (plan.canonicalOutcome == "divorce_revealed") {
                speaker = sec.mother;
                target = {};
                skip = true;
                exact = false;
            } else {
                const State::ActorId fSpeaker =
                    !sec.bioFather.IsNull() ? sec.bioFather : sec.presumedFather;
                speaker = !fSpeaker.IsNull() ? fSpeaker : sec.mother;
                target = sec.mother;
                skip = narrationSuppressed || !SafeCanTrigger(canTrigger, fSpeaker, sec.mother);
                exact = false;
            }

            if (speaker.IsNull()) {
                speaker = sec.mother;
            }
            if (speaker.IsNull() &&
                !(stillSecretBio && sec.bioFather.IsPlayer()) &&
                !(stillSecretAssumed && sec.presumedFather.IsPlayer())) {
                speaker = State::ActorId::Player();
            }

            if (exact) {
                if (!aud.empty()) {
                    if (!ContainsActorId(aud, speaker)) {
                        speaker = aud.front();
                    }
                    if (!target.IsNull() && !ContainsActorId(aud, target)) {
                        target = {};
                    }
                    plan.legs.push_back(EventDeliveryLeg{
                        .originator = speaker,
                        .target = target,
                        .exactAudience = true,
                        .audience = std::move(aud),
                        .skipTrigger = skip,
                        .maskMode = DispatchMaskMode::Unmasked
                    });
                }
            } else if (!speaker.IsNull()) {
                plan.legs.push_back(EventDeliveryLeg{
                    .originator = speaker,
                    .target = target,
                    .exactAudience = false,
                    .audience = {},
                    .skipTrigger = skip,
                    .maskMode = DispatchMaskMode::Unmasked
                });
            }
        }

        plan.shouldEmit = !plan.legs.empty();
        for (const auto& leg : plan.legs) {
            if (!leg.skipTrigger) {
                plan.postSuppressNarrationSeconds = 3.0f;
                break;
            }
        }
        return plan;
    }

    EventDispatchPlan PlanAffairConfessed(
        State::ActorId cheater,
        State::ActorId victim,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!cheater.HasPhysicalActor() || !victim.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::AffairConfessed };
        }
        const bool skip = narrationSuppressed || !SafeCanTrigger(canTrigger, cheater, victim);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::AffairConfessed,
            cheater,
            victim,
            skip,
            skip ? 0.0f : 3.0f);
    }

    EventDispatchPlan PlanAffairExposed(
        State::ActorId affairPartner,
        State::ActorId victim,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!affairPartner.HasPhysicalActor() || !victim.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::AffairExposed };
        }
        const bool skip = narrationSuppressed || !SafeCanTrigger(canTrigger, affairPartner, victim);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::AffairExposed,
            affairPartner,
            victim,
            skip,
            skip ? 0.0f : 3.0f);
    }

    EventDispatchPlan PlanAffairResolved(
        State::ActorId initiator,
        State::ActorId otherParty,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger) {
        if (!initiator.HasPhysicalActor() || !otherParty.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::AffairResolved };
        }
        const bool skip = narrationSuppressed || !SafeCanTrigger(canTrigger, initiator, otherParty);
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::AffairResolved,
            initiator,
            otherParty,
            skip,
            skip ? 0.0f : 3.0f);
    }

    EventDispatchPlan PlanCheatingCaught(
        State::ActorId victim,
        State::ActorId cheater,
        const NarrationProximityFn& canTrigger) {
        if (!victim.HasPhysicalActor() || !cheater.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::CheatingCaught };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::CheatingCaught,
            victim,
            cheater,
            !SafeCanTrigger(canTrigger, victim, cheater));
    }

    EventDispatchPlan PlanOpenRelationshipWalkIn(
        State::ActorId witness,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger) {
        if (!witness.HasPhysicalActor() || !partner.HasPhysicalActor()) {
            return EventDispatchPlan{ .kind = SkyrimNetEventKind::OpenRelationshipWalkIn };
        }
        return MakeSingleSimpleLegPlan(
            SkyrimNetEventKind::OpenRelationshipWalkIn,
            witness,
            partner,
            !SafeCanTrigger(canTrigger, witness, partner));
    }

}  // namespace MARAS::Projection
