#include "maras/app/FertilityLifecycleCoordinator.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldStore.h"

#include <algorithm>

namespace MARAS::App {

    namespace {
        [[nodiscard]] RE::Actor* ResolveActorForm(RE::FormID id) {
            if (!HasPhysicalActor(id)) {
                return nullptr;
            }
            if (id == kPlayerFormID) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (auto* indexed = SpatialWorldIndex::GetSingleton().GetActorFromFormId(id)) {
                return indexed;
            }
            return RE::TESForm::LookupByID<RE::Actor>(id);
        }

        [[nodiscard]] bool IsFemaleActor(RE::Actor* actor) noexcept {
            const auto* base = actor ? actor->GetActorBase() : nullptr;
            return base && (base->GetSex() == RE::SEX::kFemale);
        }
    }  // namespace

    FertilityLifecycleCoordinator& FertilityLifecycleCoordinator::GetSingleton() {
        static FertilityLifecycleCoordinator instance;
        return instance;
    }

    std::string FertilityLifecycleCoordinator::PickNpcChildNameForParents(
        RE::Actor* mother,
        RE::Actor* father,
        bool isFemale) const {
        auto& pool = Engine::NamePoolLoader::GetSingleton();
        if (!pool.IsChildLoaded()) {
            (void)pool.LoadChildNamesFromFile();
        }
        RE::Actor* raceSource = mother;
        if (father && Utils::RandomFloat(0.0f, 1.0f) < 0.5f) {
            raceSource = father;
        }
        std::string picked = raceSource ? pool.GetRandomChildName(raceSource, isFemale, true) : "";
        if (picked.empty() && mother && raceSource != mother) {
            picked = pool.GetRandomChildName(mother, isFemale, true);
        }
        return picked;
    }

    void FertilityLifecycleCoordinator::FinalizeCompletedDeliveryBatch(
        RE::FormID motherId,
        const std::vector<Foundation::ChildId>& bornChildIds) {
        if (motherId == 0 || bornChildIds.empty()) {
            return;
        }

        const RE::FormID firstChildId = bornChildIds.front().raw();
        const auto& lineage = LineageUseCases::GetSingleton();
        const RE::FormID bioFatherId = lineage.GetBioFather(firstChildId);
        const RE::FormID presumedFatherId = lineage.GetMotherPartner(firstChildId);

        const auto affTarget = Domain::Lineage::ResolveFertilityAffectionTarget(
            State::ActorId{ motherId },
            State::ActorId{ presumedFatherId },
            State::ActorId{ bioFatherId });
        if (!affTarget.targetActor.IsNull()) {
            AffectionEventCoordinator::GetSingleton().ApplyChildBirthAffection(
                affTarget.targetActor.raw(),
                static_cast<std::int32_t>(bornChildIds.size()));
        }

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            if (bornChildIds.size() > 1) {
                std::vector<std::uint32_t> rawIds;
                rawIds.reserve(bornChildIds.size());
                for (const auto cid : bornChildIds) {
                    rawIds.push_back(cid.raw());
                }
                sn.EmitChildBirthBatch(motherId, rawIds);
            } else {
                const std::string childName = lineage.GetChildName(firstChildId);
                sn.EmitChildBirth(firstChildId, childName);
            }
        }
    }

    void FertilityLifecycleCoordinator::ApplyGrowUpTransition(
        RE::FormID childId,
        const std::string& childName,
        std::int32_t gender) {
        if (childId == 0) {
            return;
        }

        auto& lineage = LineageUseCases::GetSingleton();
        if (gender >= 0 && gender <= 255) {
            lineage.SetChildGender(childId, static_cast<std::uint8_t>(gender));
        }
        if (!childName.empty()) {
            lineage.UpdateChildName(childId, childName);
        }
        lineage.SetChildStage(childId, Foundation::ChildStage::Child);

        MARAS_LOG_DEBUG(
            "[Fertility] Child grew up from Newborn to '{}' (ID {:08X}, gender={})",
            childName, childId, gender);

        AffectionEventCoordinator::GetSingleton().ApplyChildMilestoneForParents(childId, "child");

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            sn.EmitChildGrewUp(childId, childName);
        }
    }

    void FertilityLifecycleCoordinator::OnConception(
        std::string_view provider,
        RE::Actor* mother,
        const std::vector<RE::Actor*>& bioFathers,
        std::int32_t trackingOrChildCount,
        float currentGameDay) {
        const auto prov = Domain::Lineage::ParseFertilityProvider(provider);
        if (prov == Domain::Lineage::FertilityProvider::Unknown || !mother) {
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        const State::ActorId motherActorId{ motherId };
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->actors || !snap->relationships) {
            return;
        }

        if (!Domain::Lineage::CanRegisterFertilityConception(*snap, motherActorId, prov)) {
            MARAS_LOG_DEBUG(
                "[Fertility] Mother {:08X} ('{}') already pregnant/carrying newborn for {}, skipping conception.",
                motherId, ActorSvc::GetName(motherId), provider);
            return;
        }

        const RE::FormID presumedFatherId = snap->relationships->GetPrimaryPartner(motherActorId).raw();
        auto& pw = PaternityWorkflowCoordinator::GetSingleton();

        if (prov == Domain::Lineage::FertilityProvider::Fmr) {
            RE::Actor* bioFather = bioFathers.empty() ? nullptr : bioFathers.front();
            const RE::FormID bioFatherId = bioFather ? bioFather->GetFormID() : 0;
            const RE::FormID unbornId = pw.RegisterUnbornChild(motherId, bioFatherId, currentGameDay, true);
            const std::string bioFatherName = bioFather ? ActorSvc::GetName(bioFatherId) : "Unknown";

            MARAS_LOG_INFO(
                "[Fertility][FMR] {} ({:08X}) conceived unborn child {:08X} (index={}, bioFather={}, presumedFather={})",
                ActorSvc::GetName(motherId),
                motherId,
                unbornId,
                trackingOrChildCount,
                bioFatherName,
                presumedFatherId != 0 ? ActorSvc::GetName(presumedFatherId) : "None");

            const auto affTarget = Domain::Lineage::ResolveFertilityAffectionTarget(
                motherActorId,
                State::ActorId{ presumedFatherId },
                State::ActorId{ bioFatherId });
            if (!affTarget.targetActor.IsNull()) {
                AffectionEventCoordinator::GetSingleton().ApplyPregnancyConceivedAffection(
                    affTarget.targetActor.raw(), affTarget.isPlayerPregnant);
            }

            auto& sn = GetSkyrimNetGateway();
            if (sn.IsAvailable()) {
                sn.EmitPregnancyAnnounced(
                    motherId,
                    presumedFatherId,
                    bioFatherId,
                    bioFatherName,
                    1);
            }
            return;
        }

        // Beeing Female NG: multi-fetus per-child father list
        std::int32_t nonNullFatherCount = 0;
        for (RE::Actor* candidate : bioFathers) {
            if (candidate) {
                ++nonNullFatherCount;
            }
        }
        const std::int32_t childCount =
            (trackingOrChildCount > 0)
                ? trackingOrChildCount
                : std::max<std::int32_t>(1, nonNullFatherCount);

        RE::FormID fallbackFatherId = 0;
        for (RE::Actor* candidate : bioFathers) {
            if (candidate) {
                fallbackFatherId = candidate->GetFormID();
                break;
            }
        }

        for (std::int32_t i = 0; i < childCount; ++i) {
            RE::FormID fetusFatherId = 0;
            if (static_cast<std::size_t>(i) < bioFathers.size() && bioFathers[static_cast<std::size_t>(i)] != nullptr) {
                fetusFatherId = bioFathers[static_cast<std::size_t>(i)]->GetFormID();
            } else {
                fetusFatherId = fallbackFatherId;
            }

            const bool evaluateStepIn = (i == childCount - 1);
            const RE::FormID unbornId = pw.RegisterUnbornChild(motherId, fetusFatherId, currentGameDay, evaluateStepIn);
            MARAS_LOG_DEBUG(
                "[Fertility][BF] {} ({:08X}) conceived fetus #{}/{} (unborn={:08X}, bioFather={:08X}, presumed={:08X})",
                ActorSvc::GetName(motherId),
                motherId,
                i + 1,
                childCount,
                unbornId,
                fetusFatherId,
                presumedFatherId);
        }

        const RE::FormID primaryBioFatherId =
            (!bioFathers.empty() && bioFathers.front() != nullptr)
                ? bioFathers.front()->GetFormID()
                : fallbackFatherId;

        const auto affTarget = Domain::Lineage::ResolveFertilityAffectionTarget(
            motherActorId,
            State::ActorId{ presumedFatherId },
            State::ActorId{ primaryBioFatherId });
        if (!affTarget.targetActor.IsNull()) {
            AffectionEventCoordinator::GetSingleton().ApplyPregnancyConceivedAffection(
                affTarget.targetActor.raw(), affTarget.isPlayerPregnant);
        }

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            const std::string bioFatherName =
                primaryBioFatherId != 0 ? ActorSvc::GetName(primaryBioFatherId) : "";
            sn.EmitPregnancyAnnounced(
                motherId,
                presumedFatherId,
                primaryBioFatherId,
                bioFatherName,
                childCount);
        }
    }

    void FertilityLifecycleCoordinator::OnProgressStatus(
        std::string_view provider,
        RE::Actor* mother,
        std::int32_t stageOrRank,
        float currentGameDay) {
        const auto prov = Domain::Lineage::ParseFertilityProvider(provider);
        if (prov == Domain::Lineage::FertilityProvider::Unknown || !mother) {
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        auto& pw = PaternityWorkflowCoordinator::GetSingleton();

        if (prov == Domain::Lineage::FertilityProvider::Fmr) {
            if (Domain::Lineage::IsFmrVisiblePregnancyRank(stageOrRank)) {
                const RE::FormID unbornId = pw.FindUnbornChild(motherId);
                if (unbornId != 0 && pw.IsPregnancySecretForPartner(unbornId)) {
                    MARAS_LOG_INFO(
                        "[Fertility][FMR] {} ({:08X}) reached visible pregnancy stage (rank={}), revealing pregnancy to partner.",
                        ActorSvc::GetName(motherId), motherId, stageOrRank);
                    pw.RevealPregnancyToPartner(unbornId, true);
                }
            }
            return;
        }

        switch (Domain::Lineage::ClassifyBfCycleState(stageOrRank)) {
            case Domain::Lineage::BfCycleStateAction::VisibleTrimesterReveal: {
                const RE::FormID unbornId = pw.FindUnbornChild(motherId);
                if (unbornId != 0 && pw.IsPregnancySecretForPartner(unbornId)) {
                    MARAS_LOG_INFO(
                        "[Fertility][BF] {} ({:08X}) reached visible pregnancy state ({}), revealing pregnancy to partner.",
                        ActorSvc::GetName(motherId), motherId, stageOrRank);
                    pw.RevealPregnancyToPartner(unbornId, true);
                }
                break;
            }
            case Domain::Lineage::BfCycleStateAction::RecoveryDelivery: {
                if (!pw.GetUnbornChildren(motherId).empty()) {
                    auto& pool = Engine::NamePoolLoader::GetSingleton();
                    if (!pool.IsChildLoaded()) {
                        (void)pool.LoadChildNamesFromFile();
                    }
                    const bool femaleMother = IsFemaleActor(mother);

                    auto& lineage = LineageUseCases::GetSingleton();
                    while (!pw.GetUnbornChildren(motherId).empty()) {
                        std::string childName = pool.GetRandomChildName(mother, femaleMother, true);
                        if (childName.empty()) {
                            childName = "Newborn";
                        }
                        const RE::FormID childId = pw.PromoteUnbornToInfant(motherId, currentGameDay, childName);
                        if (childId == 0) {
                            break;
                        }
                        lineage.SetChildGender(childId, 2);
                        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                            (void)Domain::Lineage::ApplyAppendBornToDeliveryBatch(
                                ws.lineage.Write(), State::ActorId{ motherId }, State::ChildId{ childId });
                        });
                        MARAS_LOG_INFO(
                            "[Fertility][BF] {} ({:08X}) gave birth without spawned baby item (ID {:08X}, name='{}')",
                            ActorSvc::GetName(motherId), motherId, childId, childName);
                    }
                }

                std::optional<State::ActiveDeliveryBatch> completed{};
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    completed = Domain::Lineage::ApplyPopDeliveryBatch(
                        ws.lineage.Write(), State::ActorId{ motherId });
                });
                if (completed && !completed->bornChildIds.empty()) {
                    FinalizeCompletedDeliveryBatch(motherId, completed->bornChildIds);
                }
                break;
            }
            case Domain::Lineage::BfCycleStateAction::None:
            default:
                break;
        }
    }

    void FertilityLifecycleCoordinator::OnLaborStarted(
        RE::Actor* mother,
        std::int32_t childCount,
        RE::Actor* fallbackFather,
        float currentGameDay) {
        if (!mother) {
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        Domain::Lineage::BeginLaborBatchOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyBeginLaborBatch(
                ws.lineage.Write(),
                State::ActorId{ motherId },
                childCount,
                currentGameDay);
        });

        if (!outcome.startedNewBatch) {
            MARAS_LOG_INFO(
                "[Fertility][BF] Mother {} ({:08X}) labor event continues ongoing labor ({} children).",
                ActorSvc::GetName(motherId), motherId, outcome.totalCount);
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        RE::FormID presumedFatherId =
            (snap && snap->relationships)
                ? snap->relationships->GetPrimaryPartner(State::ActorId{ motherId }).raw()
                : 0;
        if (presumedFatherId == 0 && fallbackFather) {
            presumedFatherId = fallbackFather->GetFormID();
        }

        MARAS_LOG_INFO(
            "[Fertility][BF] Mother {} ({:08X}) entered labor (count={}).",
            ActorSvc::GetName(motherId), motherId, outcome.totalCount);

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            sn.EmitLaborStarted(motherId, presumedFatherId, outcome.totalCount);
        }
    }

    void FertilityLifecycleCoordinator::OnBirth(
        std::string_view provider,
        RE::Actor* mother,
        RE::Actor* bioFather,
        const std::string& childName,
        std::int32_t babySex,
        float currentGameDay) {
        const auto prov = Domain::Lineage::ParseFertilityProvider(provider);
        if (prov == Domain::Lineage::FertilityProvider::Unknown || !mother) {
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        const RE::FormID initialBioFatherId = bioFather ? bioFather->GetFormID() : 0;
        const std::string effectiveName = childName.empty() ? "Newborn" : childName;
        auto& pw = PaternityWorkflowCoordinator::GetSingleton();
        auto& lineage = LineageUseCases::GetSingleton();

        if (prov == Domain::Lineage::FertilityProvider::Fmr) {
            const RE::FormID unbornId = pw.FindUnbornChild(motherId);
            RE::FormID childId = 0;
            if (unbornId != 0) {
                childId = pw.PromoteUnbornToInfant(motherId, currentGameDay, effectiveName, initialBioFatherId);
            } else {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                if (snap && snap->lineage &&
                    Domain::Lineage::HasDuplicateFmrNewbornInfant(*snap->lineage, State::ActorId{ motherId })) {
                    MARAS_LOG_WARN(
                        "[Fertility][FMR] Mother {} ({:08X}) already has an active infant. Skipping duplicate birth registration.",
                        ActorSvc::GetName(motherId), motherId);
                    return;
                }
                const RE::FormID presumedFatherId =
                    (snap && snap->relationships)
                        ? snap->relationships->GetPrimaryPartner(State::ActorId{ motherId }).raw()
                        : 0;
                childId = pw.RegisterFertilityChild(
                    0, motherId, presumedFatherId, initialBioFatherId, 0.0f, currentGameDay, effectiveName);
            }

            if (childId == 0) {
                return;
            }

            const RE::FormID effectiveBioFatherId =
                initialBioFatherId != 0 ? initialBioFatherId : lineage.GetBioFather(childId);
            const RE::FormID presumedFatherId = lineage.GetMotherPartner(childId);

            MARAS_LOG_INFO(
                "[Fertility][FMR] Promoted/registered newborn {:08X} for mother {} ({:08X}) (bioFather={:08X}, presumedFather={:08X}).",
                childId, ActorSvc::GetName(motherId), motherId, effectiveBioFatherId, presumedFatherId);

            const auto affTarget = Domain::Lineage::ResolveFertilityAffectionTarget(
                State::ActorId{ motherId },
                State::ActorId{ presumedFatherId },
                State::ActorId{ effectiveBioFatherId });
            if (!affTarget.targetActor.IsNull()) {
                AffectionEventCoordinator::GetSingleton().ApplyChildBirthAffection(affTarget.targetActor.raw(), 1);
            }

            auto& sn = GetSkyrimNetGateway();
            if (sn.IsAvailable()) {
                sn.EmitChildBirth(childId, effectiveName);
            }
            return;
        }

        // Beeing Female NG: match unborn by father (correcting Baby NTR if needed), promote, batch
        const std::int32_t normalizedSex = (babySex == 0 || babySex == 1) ? babySex : 2;
        RE::FormID childId = 0;
        bool fromUnborn = false;

        if (const auto snap = State::WorldStore::GetSingleton().Snapshot(); snap && snap->lineage) {
            if (!snap->lineage->GetUnbornOfMother(State::ActorId{ motherId }).empty()) {
                const auto correctionTarget = Domain::Lineage::ResolveBabyNtrCorrectionTarget(
                    *snap->lineage, State::ActorId{ motherId }, State::ActorId{ initialBioFatherId });
                if (!correctionTarget.IsNull() && initialBioFatherId != 0) {
                    lineage.SetBioFather(correctionTarget.raw(), initialBioFatherId);
                }
                childId = pw.PromoteUnbornToInfant(motherId, currentGameDay, effectiveName, initialBioFatherId);
                fromUnborn = (childId != 0);
            }
        }

        if (childId == 0) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const RE::FormID presumedFatherId =
                (snap && snap->relationships)
                    ? snap->relationships->GetPrimaryPartner(State::ActorId{ motherId }).raw()
                    : 0;
            childId = pw.RegisterFertilityChild(
                0, motherId, presumedFatherId, initialBioFatherId, 0.0f, currentGameDay, effectiveName);
        }

        if (childId == 0) {
            return;
        }

        lineage.SetChildGender(childId, static_cast<std::uint8_t>(normalizedSex));

        const RE::FormID effectiveBioFatherId =
            initialBioFatherId != 0 ? initialBioFatherId : lineage.GetBioFather(childId);

        MARAS_LOG_INFO(
            "[Fertility][BF] Born newborn {:08X} ('{}', sex={}) for mother {} ({:08X}) (bioFather={:08X}).",
            childId, effectiveName, normalizedSex, ActorSvc::GetName(motherId), motherId, effectiveBioFatherId);

        Domain::Lineage::RecordDeliveredChildOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyRecordDeliveredChild(
                ws.lineage.Write(),
                State::ActorId{ motherId },
                State::ChildId{ childId },
                fromUnborn);
        });

        if (outcome.batchCompleted) {
            FinalizeCompletedDeliveryBatch(motherId, outcome.completedChildIds);
        } else if (auto& sn = GetSkyrimNetGateway(); sn.IsAvailable()) {
            sn.EmitLaborProgress(motherId, outcome.bornSoFar, outcome.laborTotal);
        }
    }

    void FertilityLifecycleCoordinator::OnPregnancyLoss(
        std::string_view provider,
        RE::Actor* mother,
        RE::Actor* bioFather,
        const std::string& reason,
        bool requireRemovedRecord) {
        const auto prov = Domain::Lineage::ParseFertilityProvider(provider);
        if (prov == Domain::Lineage::FertilityProvider::Unknown || !mother) {
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        RE::FormID bioFatherId = bioFather ? bioFather->GetFormID() : 0;
        const std::string lossReason = reason.empty() ? "miscarriage" : reason;

        auto& pw = PaternityWorkflowCoordinator::GetSingleton();
        auto& lineage = LineageUseCases::GetSingleton();

        if (bioFatherId == 0) {
            if (const RE::FormID unbornId = pw.FindUnbornChild(motherId); unbornId != 0) {
                bioFatherId = lineage.GetBioFather(unbornId);
            }
        }

        const bool filterByFather =
            (prov == Domain::Lineage::FertilityProvider::BeeingFemale && lossReason == "stillbirth" && bioFatherId != 0);
        const RE::FormID filterFatherArg = filterByFather ? bioFatherId : 0;

        bool removed = false;
        if (requireRemovedRecord || pw.IsNPCPregnant(motherId) || pw.FindUnbornChild(motherId) != 0) {
            removed = pw.RemoveUnbornChild(motherId, filterFatherArg);
        }

        if (!requireRemovedRecord || removed) {
            MARAS_LOG_INFO(
                "[Fertility] Pregnancy lost for {} ({:08X}, reason='{}').",
                ActorSvc::GetName(motherId), motherId, lossReason);

            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const RE::FormID presumedFatherId =
                (snap && snap->relationships)
                    ? snap->relationships->GetPrimaryPartner(State::ActorId{ motherId }).raw()
                    : 0;

            const auto affTarget = Domain::Lineage::ResolveFertilityAffectionTarget(
                State::ActorId{ motherId },
                State::ActorId{ presumedFatherId },
                State::ActorId{ bioFatherId });
            if (!affTarget.targetActor.IsNull()) {
                AffectionEventCoordinator::GetSingleton().ApplyPregnancyLostAffection(
                    affTarget.targetActor.raw(), lossReason);
            }

            auto& sn = GetSkyrimNetGateway();
            if (sn.IsAvailable()) {
                const auto eventPartner = Domain::Lineage::ResolveFertilityEventPartner(
                    State::ActorId{ presumedFatherId }, State::ActorId{ bioFatherId });
                sn.EmitPregnancyLost(motherId, eventPartner.raw(), lossReason);
            }
        }

        if (prov == Domain::Lineage::FertilityProvider::BeeingFemale &&
            pw.GetUnbornChildren(motherId).empty()) {
            std::optional<State::ActiveDeliveryBatch> completed{};
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                completed = Domain::Lineage::ApplyPopDeliveryBatch(
                    ws.lineage.Write(), State::ActorId{ motherId });
            });
            if (completed && !completed->bornChildIds.empty()) {
                FinalizeCompletedDeliveryBatch(motherId, completed->bornChildIds);
            }
        }
    }

    void FertilityLifecycleCoordinator::OnChildGrewUp(
        RE::Actor* mother,
        const std::string& childName,
        std::int32_t gender,
        bool isNpcNpc,
        RE::Actor* fatherHint) {
        if (!mother) {
            MARAS_LOG_WARN("[Fertility][FMR] OnChildGrewUp called with no mother.");
            return;
        }

        const RE::FormID motherId = mother->GetFormID();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return;
        }

        const auto childCid = Domain::Lineage::FindSyntheticChildByName(
            *snap->lineage, State::ActorId{ motherId }, "Newborn");
        if (childCid.IsNull()) {
            MARAS_LOG_WARN(
                "[Fertility][FMR] Newborn synthetic child for mother {} ({:08X}) ('{}') not found.",
                ActorSvc::GetName(motherId), motherId, childName);
            return;
        }

        std::int32_t resolvedGender = gender;
        std::string resolvedName = childName;
        if (isNpcNpc) {
            if (resolvedGender < 0 || resolvedGender > 1) {
                resolvedGender = (Utils::RandomFloat(0.0f, 1.0f) < 0.5f) ? 0 : 1;
            }
            if (resolvedName.empty()) {
                RE::Actor* effectiveFather = fatherHint;
                if (!effectiveFather) {
                    const RE::FormID bioFatherId = LineageUseCases::GetSingleton().GetBioFather(childCid.raw());
                    effectiveFather = ResolveActorForm(bioFatherId);
                }
                resolvedName = PickNpcChildNameForParents(mother, effectiveFather, resolvedGender == 1);
                if (resolvedName.empty()) {
                    resolvedName = (resolvedGender == 1) ? "Daughter" : "Son";
                }
            }
        }

        ApplyGrowUpTransition(childCid.raw(), resolvedName, resolvedGender);
    }

    void FertilityLifecycleCoordinator::OnChildGrewUpActor(
        RE::Actor* childActor,
        const std::string& childName) {
        if (!childActor) {
            return;
        }

        const RE::FormID childId = childActor->GetFormID();
        const std::string resolvedName = childName.empty() ? ActorSvc::GetName(childId) : childName;
        AffectionEventCoordinator::GetSingleton().ApplyChildMilestoneForParents(childId, "child");

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            sn.EmitChildGrewUp(childId, resolvedName);
        }
    }

    void FertilityLifecycleCoordinator::OnChildSentToTraining(
        const std::string& childName,
        const std::string& trainingLocation,
        const std::string& className) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return;
        }

        const auto childCid = Domain::Lineage::FindSyntheticChildByName(*snap->lineage, State::ActorId{}, childName);
        if (childCid.IsNull()) {
            return;
        }

        const RE::FormID childId = childCid.raw();
        auto& lineage = LineageUseCases::GetSingleton();
        lineage.SetChildTrainingLocation(childId, trainingLocation);
        const auto entry = lineage.GetChildEntry(childId);
        if (entry && entry->stage != Foundation::ChildStage::Training) {
            lineage.SetChildStage(childId, Foundation::ChildStage::Training);
        }

        MARAS_LOG_INFO(
            "[Fertility][FMR] Child '{}' ({:08X}) sent to training at '{}' ({}).",
            childName, childId, trainingLocation, className);

        AffectionEventCoordinator::GetSingleton().ApplyChildMilestoneForParents(childId, "training");

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            sn.EmitChildSentToTraining(childId, childName, trainingLocation);
        }
    }

    void FertilityLifecycleCoordinator::OnChildFinishedTraining(const std::string& childName) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return;
        }

        const auto childCid = Domain::Lineage::FindSyntheticChildByName(*snap->lineage, State::ActorId{}, childName);
        if (childCid.IsNull()) {
            return;
        }

        const RE::FormID childId = childCid.raw();
        auto& lineage = LineageUseCases::GetSingleton();
        const auto entry = lineage.GetChildEntry(childId);
        if (!entry || entry->stage == Foundation::ChildStage::Adult) {
            return;
        }

        lineage.SetChildStage(childId, Foundation::ChildStage::Adult);
        MARAS_LOG_INFO(
            "[Fertility][FMR] Child '{}' ({:08X}) finished training, became adult.",
            childName, childId);

        AffectionEventCoordinator::GetSingleton().ApplyChildMilestoneForParents(childId, "adult");

        auto& sn = GetSkyrimNetGateway();
        if (sn.IsAvailable()) {
            sn.EmitChildBecameAdult(childId, childName);
        }
    }

    void FertilityLifecycleCoordinator::OnAdultChildSpawned(
        std::string_view provider,
        RE::Actor* adultActor,
        RE::Actor* oldChildActor,
        RE::Actor* motherHint,
        RE::Actor* fatherHint,
        const std::string& childName,
        bool allowFallbackToAdultId,
        bool fireEvent) {
        const auto prov = Domain::Lineage::ParseFertilityProvider(provider);
        if (prov == Domain::Lineage::FertilityProvider::Unknown || !adultActor) {
            return;
        }

        const RE::FormID adultId = adultActor->GetFormID();
        const std::string resolvedName = childName.empty() ? ActorSvc::GetName(adultId) : childName;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return;
        }

        const auto plan = Domain::Lineage::ResolveAdultGraduationSource(
            *snap->lineage,
            State::ActorId{ adultId },
            State::ActorId{ oldChildActor ? oldChildActor->GetFormID() : 0 },
            State::ActorId{ motherHint ? motherHint->GetFormID() : 0 },
            State::ActorId{ fatherHint ? fatherHint->GetFormID() : 0 },
            resolvedName,
            allowFallbackToAdultId);

        if (plan.previousId.IsNull() || plan.alreadyAdultSameActor) {
            return;
        }

        MARAS_LOG_INFO(
            "[Fertility] Promoting child record {:08X} -> adult actor '{}' ({:08X})",
            plan.previousId.raw(), resolvedName, adultId);

        auto& lineage = LineageUseCases::GetSingleton();
        lineage.RegisterChildActor(
            adultId,
            0,
            0,
            0,
            plan.bioMother.raw(),
            plan.bioFather.raw(),
            static_cast<std::int32_t>(plan.previousId.raw()),
            static_cast<std::uint8_t>(Foundation::ChildStage::Adult),
            std::nullopt,
            true);

        if (fireEvent && plan.previousStage != Foundation::ChildStage::Adult) {
            AffectionEventCoordinator::GetSingleton().ApplyChildMilestoneForParents(adultId, "adult");
            auto& sn = GetSkyrimNetGateway();
            if (sn.IsAvailable()) {
                sn.EmitChildBecameAdult(adultId, resolvedName);
            }
        }
    }

    std::int32_t FertilityLifecycleCoordinator::FindInfantForSpawn(
        RE::Actor* mother,
        const std::string& childName) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return 0;
        }
        const auto cid = Domain::Lineage::FindSyntheticInfantForSpawn(
            *snap->lineage,
            State::ActorId{ mother ? mother->GetFormID() : 0 },
            childName);
        return static_cast<std::int32_t>(cid.raw());
    }

    std::int32_t FertilityLifecycleCoordinator::ResolveFertilitySpawnSyntheticId(
        RE::Actor* childActor,
        RE::Actor* bioMother,
        RE::Actor* bioFather,
        std::int32_t existingSyntheticId,
        float dob,
        const std::string& childName) {
        if (!childActor) {
            return 0;
        }
        if (existingSyntheticId != 0) {
            return existingSyntheticId;
        }

        const RE::FormID bioMotherId = bioMother ? bioMother->GetFormID() : 0;
        const RE::FormID bioFatherId = bioFather ? bioFather->GetFormID() : 0;
        if (bioMotherId == 0) {
            return 0;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const RE::FormID presumedFatherId =
            (snap && snap->relationships)
                ? snap->relationships->GetPrimaryPartner(State::ActorId{ bioMotherId }).raw()
                : 0;

        const std::string resolvedName =
            childName.empty() ? ActorSvc::GetName(childActor->GetFormID()) : childName;

        const RE::FormID createdId = PaternityWorkflowCoordinator::GetSingleton().RegisterFertilityChild(
            0, bioMotherId, presumedFatherId, bioFatherId, dob, dob, resolvedName);
        return static_cast<std::int32_t>(createdId);
    }

    bool FertilityLifecycleCoordinator::RegisterNpcFertilityChildSpawn(
        RE::Actor* childActor,
        std::int32_t childId,
        RE::Actor* bioMother,
        RE::Actor* bioFather,
        std::int32_t stage,
        bool isMigrating) {
        if (!childActor) {
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->relationships) {
            return false;
        }

        const auto clampedStageVal = std::clamp<std::int32_t>(
            stage,
            static_cast<std::int32_t>(Foundation::ChildStage::Infant),
            static_cast<std::int32_t>(Foundation::ChildStage::Adult));
        const auto childStage = static_cast<Foundation::ChildStage>(clampedStageVal);

        const auto plan = Domain::Lineage::PlanSpawnedChildLineage(
            *snap,
            State::ChildId{ static_cast<RE::FormID>(childId) },
            State::ActorId{ bioMother ? bioMother->GetFormID() : 0 },
            State::ActorId{ bioFather ? bioFather->GetFormID() : 0 },
            childStage);

        if (plan.isPlayerChild) {
            return false;
        }

        const RE::FormID houseLoc = (stage < 4 && !plan.tenantHouseLookupActor.IsNull())
            ? HouseholdUseCases::GetSingleton().GetTenantHouseFormID(plan.tenantHouseLookupActor.raw())
            : 0;
        const auto clampedStage = static_cast<std::uint8_t>(clampedStageVal);

        LineageUseCases::GetSingleton().RegisterChildActor(
            childActor->GetFormID(),
            plan.npcGuardianA.raw(),
            plan.npcGuardianB.raw(),
            houseLoc,
            plan.bioMother.raw(),
            plan.bioFather.raw(),
            childId,
            clampedStage,
            std::nullopt,
            !isMigrating);

        childActor->EvaluatePackage();
        MARAS_LOG_INFO(
            "[Fertility] Registered NPC spawned child '{}' ({:08X}) from ID {:08X} (guardians={:08X},{:08X}).",
            ActorSvc::GetName(childActor->GetFormID()),
            childActor->GetFormID(),
            static_cast<RE::FormID>(childId),
            plan.npcGuardianA.raw(),
            plan.npcGuardianB.raw());
        return true;
    }

    RE::Actor* FertilityLifecycleCoordinator::GetFertilitySpawnCoSpouse(
        std::int32_t childId,
        RE::Actor* bioMother,
        RE::Actor* bioFather) const {
        if (childId == 0 && !bioMother && !bioFather) {
            return nullptr;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->relationships) {
            return nullptr;
        }

        const auto plan = Domain::Lineage::PlanSpawnedChildLineage(
            *snap,
            State::ChildId{ static_cast<RE::FormID>(childId) },
            State::ActorId{ bioMother ? bioMother->GetFormID() : 0 },
            State::ActorId{ bioFather ? bioFather->GetFormID() : 0 });
        if (plan.coSpouse.IsNull()) {
            return nullptr;
        }
        return ResolveActorForm(plan.coSpouse.raw());
    }

    std::int32_t FertilityLifecycleCoordinator::MigrateCompletedFmrNpcChild(
        RE::Actor* mother,
        RE::Actor* father,
        float conceptionTime,
        float birthTime) {
        if (!mother) {
            return 0;
        }

        const RE::FormID motherId = mother->GetFormID();
        const RE::FormID fatherId = father ? father->GetFormID() : 0;
        const std::string placeholderName = ActorSvc::GetName(motherId) + "'s Child";

        const RE::FormID childId = PaternityWorkflowCoordinator::GetSingleton().RegisterFertilityChild(
            0, motherId, 0, fatherId, conceptionTime, birthTime, placeholderName);
        if (childId == 0) {
            return 0;
        }

        const std::int32_t gender = (Utils::RandomFloat(0.0f, 1.0f) < 0.5f) ? 0 : 1;
        std::string childName = PickNpcChildNameForParents(mother, father, gender == 1);
        if (childName.empty()) {
            childName = placeholderName;
        }

        auto& lineage = LineageUseCases::GetSingleton();
        lineage.SetChildStage(childId, Foundation::ChildStage::Child);
        lineage.SetChildGender(childId, static_cast<std::uint8_t>(gender));
        lineage.UpdateChildName(childId, childName);

        MARAS_LOG_INFO(
            "[Fertility][FMR] Migrated completed NPC-NPC birth: mother={} ({:08X}), father={:08X}, child='{}' ({:08X}).",
            ActorSvc::GetName(motherId), motherId, fatherId, childName, childId);

        return static_cast<std::int32_t>(childId);
    }

}  // namespace MARAS::App
