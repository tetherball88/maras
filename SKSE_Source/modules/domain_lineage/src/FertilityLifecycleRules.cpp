#include "maras/domain/lineage/FertilityLifecycleRules.h"

#include "maras/foundation/StringUtils.h"

#include <algorithm>

namespace MARAS::Domain::Lineage {

    namespace {
        [[nodiscard]] bool RecordMatchesMother(const State::ChildRecord& rec, ActorId motherId) noexcept {
            if (motherId.IsNull()) {
                return true;
            }
            return rec.guardians.HasGuardian(motherId) || rec.bioMother == motherId;
        }

        [[nodiscard]] bool IsRealFatherId(ActorId id) noexcept {
            return !id.IsNull() && !id.IsUnknownFather();
        }

        [[nodiscard]] bool AreMarried(
            const State::WorldStateSnapshot& snap,
            ActorId a,
            ActorId b) noexcept {
            return snap.relationships && !a.IsNull() && !b.IsNull() &&
                   snap.relationships->GetStatus(a, b) == Foundation::RelationshipStatus::Married;
        }

        [[nodiscard]] bool IsMarriedToPlayer(
            const State::WorldStateSnapshot& snap,
            ActorId npcId) noexcept {
            return !npcId.IsNull() && !npcId.IsPlayer() && AreMarried(snap, ActorId::Player(), npcId);
        }

        [[nodiscard]] bool ContainsChildId(const std::vector<ChildId>& ids, ChildId target) noexcept {
            return std::find(ids.begin(), ids.end(), target) != ids.end();
        }
    }  // namespace

    FertilityProvider ParseFertilityProvider(std::string_view raw) noexcept {
        if (Foundation::StringUtils::CaseInsensitiveEquals(raw, "FMR") ||
            Foundation::StringUtils::CaseInsensitiveEquals(raw, "FertilityMode")) {
            return FertilityProvider::Fmr;
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(raw, "BF") ||
            Foundation::StringUtils::CaseInsensitiveEquals(raw, "BeeingFemale")) {
            return FertilityProvider::BeeingFemale;
        }
        return FertilityProvider::Unknown;
    }

    BfCycleStateAction ClassifyBfCycleState(std::int32_t currentState) noexcept {
        if (currentState == 5 || currentState == 6 || currentState == 7) {
            return BfCycleStateAction::VisibleTrimesterReveal;
        }
        if (currentState == 8) {
            return BfCycleStateAction::RecoveryDelivery;
        }
        return BfCycleStateAction::None;
    }

    std::optional<BlockedFertilityMcmClick> ClassifyBlockedFertilityMcmClick(
        std::string_view configScriptName,
        std::string_view optionStateName) noexcept {
        if (configScriptName.empty() || optionStateName.empty()) {
            return std::nullopt;
        }

        using Foundation::StringUtils::CaseInsensitiveEquals;

        if (CaseInsensitiveEquals(configScriptName, "_JSW_BB_ConfigQuestScript")) {
            if (CaseInsensitiveEquals(optionStateName, "EXECUTE_ADOPTION")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::Fmr,
                    .action = BlockedFertilityMcmAction::SpawnYoungChild,
                };
            }
            if (CaseInsensitiveEquals(optionStateName, "SEND_FOR_TRAINING")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::Fmr,
                    .action = BlockedFertilityMcmAction::SendToTraining,
                };
            }
            if (CaseInsensitiveEquals(optionStateName, "SEND_TO_HONORHALL")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::Fmr,
                    .action = BlockedFertilityMcmAction::SendToOrphanage,
                };
            }
            if (CaseInsensitiveEquals(optionStateName, "SUMMON_SELECTED_UNSUMMONED") ||
                CaseInsensitiveEquals(optionStateName, "SUMMON_ALL_UNSUMMONED")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::Fmr,
                    .action = BlockedFertilityMcmAction::SpawnAdultChild,
                };
            }
            if (CaseInsensitiveEquals(optionStateName, "DISMISS_SELECTED_SUMMONED") ||
                CaseInsensitiveEquals(optionStateName, "DISMISS_ALL_SUMMONED")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::Fmr,
                    .action = BlockedFertilityMcmAction::DismissAdultChild,
                };
            }
            return std::nullopt;
        }

        if (CaseInsensitiveEquals(configScriptName, "FWSystemConfig")) {
            if (CaseInsensitiveEquals(optionStateName, "TextGrowSelected") ||
                CaseInsensitiveEquals(optionStateName, "TextGrowUpChildren")) {
                return BlockedFertilityMcmClick{
                    .provider = FertilityProvider::BeeingFemale,
                    .action = BlockedFertilityMcmAction::GrowOrHatchChild,
                };
            }
            return std::nullopt;
        }

        return std::nullopt;
    }

    ChildId FindSyntheticChildByName(
        const State::LineageTable& lineage,
        ActorId motherId,
        std::string_view name,
        std::optional<ChildStage> requiredStage) {
        std::vector<ChildId> candidates;
        candidates.reserve(lineage.Size());
        for (const auto& [cid, rec] : lineage.AllChildren()) {
            if (!cid.IsSynthetic()) {
                continue;
            }
            if (requiredStage.has_value() && rec.stage != *requiredStage) {
                continue;
            }
            if (!RecordMatchesMother(rec, motherId)) {
                continue;
            }
            candidates.push_back(cid);
        }
        std::sort(candidates.begin(), candidates.end());

        // Pass 1: exact (case-insensitive) name match
        if (!name.empty()) {
            for (const ChildId cid : candidates) {
                const auto* rec = lineage.FindChild(cid);
                if (rec && Foundation::StringUtils::CaseInsensitiveEquals(rec->name, name)) {
                    return cid;
                }
            }
            return ChildId{};
        }

        // Pass 2: empty name with mother provided -> first matching synthetic child
        if (!motherId.IsNull() && !candidates.empty()) {
            return candidates.front();
        }

        return ChildId{};
    }

    ChildId FindSyntheticInfantForSpawn(
        const State::LineageTable& lineage,
        ActorId motherId,
        std::string_view name) {
        if (!motherId.IsNull()) {
            if (const ChildId byMother =
                    FindSyntheticChildByName(lineage, motherId, name, ChildStage::Infant);
                !byMother.IsNull()) {
                return byMother;
            }
        }
        return FindSyntheticChildByName(lineage, ActorId{}, name, ChildStage::Infant);
    }

    bool HasDuplicateFmrNewbornInfant(const State::LineageTable& lineage, ActorId motherId) {
        if (motherId.IsNull()) {
            return false;
        }
        return !FindSyntheticChildByName(lineage, motherId, "Newborn", ChildStage::Infant).IsNull();
    }

    bool CanRegisterFertilityConception(
        const State::WorldStateSnapshot& snap,
        ActorId motherId,
        FertilityProvider provider) {
        if (motherId.IsNull() || !snap.lineage) {
            return false;
        }
        if (!snap.lineage->GetUnbornOfMother(motherId).empty()) {
            return false;
        }
        if (provider == FertilityProvider::Fmr) {
            if (!FindSyntheticChildByName(*snap.lineage, motherId, "Newborn").IsNull()) {
                return false;
            }
        }
        return true;
    }

    FertilityAffectionTarget ResolveFertilityAffectionTarget(
        ActorId motherId,
        ActorId presumedFatherId,
        ActorId bioFatherId) noexcept {
        FertilityAffectionTarget out{};
        if (motherId.IsNull()) {
            return out;
        }

        if (motherId.IsPlayer()) {
            out.isPlayerPregnant = true;
            if (IsRealFatherId(presumedFatherId)) {
                out.targetActor = presumedFatherId;
            } else if (IsRealFatherId(bioFatherId)) {
                out.targetActor = bioFatherId;
            }
            return out;
        }

        out.isPlayerPregnant = false;
        if (presumedFatherId.IsPlayer()) {
            out.targetActor = motherId;
        } else if (presumedFatherId.IsNull() && bioFatherId.IsPlayer()) {
            out.targetActor = motherId;
        }
        return out;
    }

    ActorId ResolveFertilityEventPartner(ActorId presumedFatherId, ActorId bioFatherId) noexcept {
        if (IsRealFatherId(presumedFatherId)) {
            return presumedFatherId;
        }
        if (IsRealFatherId(bioFatherId)) {
            return bioFatherId;
        }
        return ActorId{};
    }

    BeginLaborBatchOutcome ApplyBeginLaborBatch(
        State::LineageTable& lineage,
        ActorId motherId,
        std::int32_t reportedChildCount,
        float currentGameDay) {
        BeginLaborBatchOutcome out{};
        if (motherId.IsNull()) {
            return out;
        }

        if (const auto* existing = lineage.FindActiveDeliveryBatch(motherId);
            existing && existing->laborTotal > 0 && currentGameDay >= existing->laborStartGameDay &&
            (currentGameDay - existing->laborStartGameDay) < 0.5f) {
            out.startedNewBatch = false;
            out.totalCount = existing->laborTotal;
            return out;
        }

        const auto unbornCount = static_cast<std::int32_t>(lineage.GetUnbornOfMother(motherId).size());
        const std::int32_t total = std::max({ 1, reportedChildCount, unbornCount });

        State::ActiveDeliveryBatch batch{};
        batch.laborTotal = total;
        batch.laborStartGameDay = currentGameDay;
        lineage.UpsertActiveDeliveryBatch(motherId, std::move(batch));

        out.startedNewBatch = true;
        out.totalCount = total;
        return out;
    }

    ChildId ResolveBabyNtrCorrectionTarget(
        const State::LineageTable& lineage,
        ActorId motherId,
        ActorId bioFatherId) {
        if (motherId.IsNull() || !IsRealFatherId(bioFatherId)) {
            return ChildId{};
        }

        std::vector<ChildId> unbornIds = lineage.GetUnbornOfMother(motherId);
        if (unbornIds.empty()) {
            return ChildId{};
        }
        std::sort(unbornIds.begin(), unbornIds.end());

        for (const ChildId cid : unbornIds) {
            if (const auto* rec = lineage.FindChild(cid); rec && rec->bioFather == bioFatherId) {
                return ChildId{};
            }
        }
        return unbornIds.front();
    }

    bool ApplyAppendBornToDeliveryBatch(
        State::LineageTable& lineage,
        ActorId motherId,
        ChildId childId) {
        if (motherId.IsNull() || childId.IsNull()) {
            return false;
        }

        State::ActiveDeliveryBatch batch{};
        if (const auto* existing = lineage.FindActiveDeliveryBatch(motherId)) {
            batch = *existing;
        }
        if (!ContainsChildId(batch.bornChildIds, childId)) {
            batch.bornChildIds.push_back(childId);
        }
        lineage.UpsertActiveDeliveryBatch(motherId, std::move(batch));
        return true;
    }

    RecordDeliveredChildOutcome ApplyRecordDeliveredChild(
        State::LineageTable& lineage,
        ActorId motherId,
        ChildId childId,
        bool fromUnborn) {
        RecordDeliveredChildOutcome out{};
        if (motherId.IsNull() || childId.IsNull()) {
            return out;
        }

        (void)ApplyAppendBornToDeliveryBatch(lineage, motherId, childId);

        const auto remaining = static_cast<std::int32_t>(lineage.GetUnbornOfMother(motherId).size());
        if (!fromUnborn || remaining == 0) {
            if (auto popped = ApplyPopDeliveryBatch(lineage, motherId)) {
                out.batchCompleted = true;
                out.completedChildIds = std::move(popped->bornChildIds);
                out.bornSoFar = static_cast<std::int32_t>(out.completedChildIds.size());
                out.laborTotal = std::max(popped->laborTotal, out.bornSoFar);
            }
            return out;
        }

        if (auto* active = lineage.FindActiveDeliveryBatchMutable(motherId)) {
            out.bornSoFar = static_cast<std::int32_t>(active->bornChildIds.size());
            out.laborTotal = std::max(active->laborTotal, out.bornSoFar + remaining);
            active->laborTotal = out.laborTotal;
        }
        return out;
    }

    std::optional<State::ActiveDeliveryBatch> ApplyPopDeliveryBatch(
        State::LineageTable& lineage,
        ActorId motherId) {
        if (motherId.IsNull()) {
            return std::nullopt;
        }
        const auto* existing = lineage.FindActiveDeliveryBatch(motherId);
        if (!existing) {
            return std::nullopt;
        }
        State::ActiveDeliveryBatch copy = *existing;
        lineage.EraseActiveDeliveryBatch(motherId);
        if (copy.bornChildIds.empty()) {
            return std::nullopt;
        }
        return copy;
    }

    SpawnedChildLineagePlan PlanSpawnedChildLineage(
        const State::WorldStateSnapshot& snap,
        ChildId syntheticOrExistingId,
        ActorId bioMotherHint,
        ActorId bioFatherHint,
        ChildStage stage) noexcept {
        SpawnedChildLineagePlan plan{};
        ActorId guardianA{};
        ActorId guardianB{};
        ActorId bioMother = bioMotherHint;
        ActorId bioFather = bioFatherHint;
        ActorId motherPartner{};

        if (!syntheticOrExistingId.IsNull() && snap.lineage) {
            if (const auto* rec = snap.lineage->FindChild(syntheticOrExistingId)) {
                guardianA = rec->guardians.slotA;
                guardianB = rec->guardians.slotB;
                if (!rec->bioMother.IsNull()) {
                    bioMother = rec->bioMother;
                }
                if (!rec->bioFather.IsNull()) {
                    bioFather = rec->bioFather;
                }
                motherPartner = !rec->presumedFather.IsNull()
                    ? rec->presumedFather
                    : (snap.relationships ? snap.relationships->GetPrimaryPartner(rec->bioMother) : ActorId{});
            }
        }
        if (motherPartner.IsNull() && !bioMother.IsNull() && snap.relationships) {
            motherPartner = snap.relationships->GetPrimaryPartner(bioMother);
        }

        plan.bioMother = bioMother;
        plan.bioFather = bioFather;
        plan.presumedFather = motherPartner;

        bool isPlayerChild = false;
        if (!guardianA.IsNull() || !guardianB.IsNull()) {
            if (guardianA.IsPlayer() || guardianB.IsPlayer()) {
                isPlayerChild = true;
            } else if (IsMarriedToPlayer(snap, guardianA) || IsMarriedToPlayer(snap, guardianB)) {
                isPlayerChild = true;
            }
        } else {
            if (bioMother.IsPlayer() || bioFather.IsPlayer()) {
                isPlayerChild = true;
            } else if (IsMarriedToPlayer(snap, bioMother) || IsMarriedToPlayer(snap, bioFather)) {
                isPlayerChild = true;
            }
        }

        plan.isPlayerChild = isPlayerChild;
        if (isPlayerChild) {
            if (IsMarriedToPlayer(snap, guardianA)) {
                plan.coSpouse = guardianA;
            } else if (IsMarriedToPlayer(snap, guardianB)) {
                plan.coSpouse = guardianB;
            } else if (IsMarriedToPlayer(snap, bioMother)) {
                plan.coSpouse = bioMother;
            } else if (IsMarriedToPlayer(snap, bioFather)) {
                plan.coSpouse = bioFather;
            }
        } else {
            if (!guardianA.IsNull() || !guardianB.IsNull()) {
                plan.npcGuardianA = guardianA;
                plan.npcGuardianB = guardianB;
            } else {
                ActorId secondary = !motherPartner.IsNull()
                    ? motherPartner
                    : ((!bioFather.IsNull() && !bioFather.IsUnknownFather()) ? bioFather : ActorId{});
                if (secondary == bioMother) {
                    secondary = ActorId{};
                }
                if (!bioMother.IsNull()) {
                    plan.npcGuardianA = bioMother;
                    if (!secondary.IsNull() &&
                        (stage == ChildStage::Adult ||
                         (!motherPartner.IsNull() && AreMarried(snap, bioMother, motherPartner)))) {
                        plan.npcGuardianB = secondary;
                    }
                } else if (!secondary.IsNull()) {
                    plan.npcGuardianA = secondary;
                }
            }
            plan.tenantHouseLookupActor = !plan.npcGuardianA.IsNull() ? plan.npcGuardianA : bioMother;
        }

        return plan;
    }

    AdultGraduationPlan ResolveAdultGraduationSource(
        const State::LineageTable& lineage,
        ActorId adultActorId,
        ActorId oldChildActorId,
        ActorId motherHintId,
        ActorId fatherHintId,
        std::string_view childName,
        bool allowFallbackToAdultId) {
        AdultGraduationPlan plan{};

        if (!oldChildActorId.IsNull() && lineage.FindChild(ChildId{ oldChildActorId.raw() }) != nullptr) {
            plan.previousId = ChildId{ oldChildActorId.raw() };
        }
        if (plan.previousId.IsNull() && !adultActorId.IsNull() &&
            lineage.FindChild(ChildId{ adultActorId.raw() }) != nullptr) {
            plan.previousId = ChildId{ adultActorId.raw() };
        }
        if (plan.previousId.IsNull() && !childName.empty()) {
            if (!motherHintId.IsNull()) {
                plan.previousId = FindSyntheticChildByName(lineage, motherHintId, childName);
            }
            if (plan.previousId.IsNull()) {
                plan.previousId = FindSyntheticChildByName(lineage, ActorId{}, childName);
            }
        }
        if (plan.previousId.IsNull() && allowFallbackToAdultId && !adultActorId.IsNull()) {
            plan.previousId = ChildId{ adultActorId.raw() };
        }

        if (!plan.previousId.IsNull()) {
            if (const auto* rec = lineage.FindChild(plan.previousId)) {
                plan.previousStage = rec->stage;
                plan.bioMother = rec->bioMother;
                plan.bioFather = rec->bioFather;
            }
        }
        if (plan.bioMother.IsNull()) {
            plan.bioMother = motherHintId;
        }
        if (plan.bioFather.IsNull()) {
            plan.bioFather = fatherHintId;
        }

        plan.alreadyAdultSameActor = !adultActorId.IsNull() &&
            plan.previousId.raw() == adultActorId.raw() &&
            plan.previousStage == ChildStage::Adult;

        return plan;
    }

}  // namespace MARAS::Domain::Lineage
