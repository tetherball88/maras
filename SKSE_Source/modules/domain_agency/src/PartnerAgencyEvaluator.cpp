#include "maras/domain/agency/PartnerAgencyEvaluator.h"

#include "maras/domain/agency/MatchmakingRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace MARAS::Domain::Agency {

    namespace {
        [[nodiscard]] int CountSharedChildren(
            const State::WorldStateSnapshot& snap,
            ActorId partnerId) noexcept {
            if (!snap.lineage) {
                return 0;
            }
            int count = 0;
            for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                (void)cid;
                if (rec.stage == Foundation::ChildStage::Unborn) {
                    continue;
                }
                const bool hasPlayer =
                    rec.guardians.HasGuardian(ActorId::Player()) ||
                    rec.bioMother.IsPlayer() ||
                    rec.bioFather.IsPlayer();
                const bool hasPartner =
                    rec.guardians.HasGuardian(partnerId) ||
                    rec.bioMother == partnerId ||
                    rec.bioFather == partnerId;
                if (hasPlayer && hasPartner) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] std::vector<State::AffairRecord> CollectOngoingPlayerAffairsOf(
            const State::WorldStateSnapshot& snap,
            ActorId partnerId) {
            std::vector<State::AffairRecord> result;
            if (!snap.affairs) {
                return result;
            }
            for (const auto& rec : snap.affairs->GetByCheater(partnerId)) {
                if (rec.deceivedPartner.IsPlayer() && rec.affairStatus == Foundation::AffairStatus::Ongoing) {
                    result.push_back(rec);
                }
            }
            return result;
        }

        [[nodiscard]] float DaysSinceInteraction(
            const State::WorldStateSnapshot& snap,
            ActorId partnerId,
            float currentGameDay) noexcept {
            if (!snap.affection) {
                return 0.0f;
            }
            const auto lastOpt = snap.affection->GetLastInteractionDay(partnerId);
            if (!lastOpt.has_value() || *lastOpt < 0.0f) {
                return 0.0f;
            }
            return std::max(0.0f, currentGameDay - *lastOpt);
        }

        [[nodiscard]] int ComputeHomeArrivalTriggerChance(
            float daysSinceLastVisit,
            float lonelinessStartDays,
            int currentHourOfDay) noexcept {
            int chance = 25;
            if (daysSinceLastVisit > lonelinessStartDays) {
                chance += 15;
            }
            if (currentHourOfDay >= 17 && currentHourOfDay < 22) {
                chance += 10;
            } else if (currentHourOfDay >= 22 || currentHourOfDay < 5) {
                chance += 20;
            }
            return std::clamp(chance, 0, 100);
        }
    }  // namespace

    bool IsPartnerAgencyCheatingAllowedByImmunities(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyInput& input,
        float currentGameDay,
        bool forceBypassFromExpiredSuggestOpen) noexcept {
        if (!snap.settings || !snap.relationships || !snap.affection || input.partnerId.IsNull()) {
            return false;
        }
        const auto& settings = *snap.settings;
        if (!settings.playerPartnerCheatingAgencyEnabled) {
            return false;
        }
        // Immunity 1: Deceased
        if (snap.deceased && snap.deceased->IsDeceased(input.partnerId)) {
            return false;
        }
        // Immunity 2: Must be active main partner of player (closed)
        const auto status = snap.relationships->GetStatus(ActorId::Player(), input.partnerId);
        if (!Foundation::IsMainTierStatus(status)) {
            return false;
        }
        // Immunity 3: Active follower or physically near player
        if (input.isFollowerOrNearPlayer) {
            return false;
        }
        // Immunity 4: NPC matchmaking cooldown (unless bypassed by expired SuggestOpen)
        if (!forceBypassFromExpiredSuggestOpen && IsNpcOnMatchmakingCooldown(snap, input.partnerId, currentGameDay)) {
            return false;
        }
        // Immunity 5: Max secret lovers reached
        const int ongoingCount = static_cast<int>(CollectOngoingPlayerAffairsOf(snap, input.partnerId).size());
        const int maxSecretLovers = std::clamp(settings.playerPartnerCheatingMaxSecretLovers, 1, 3);
        if (ongoingCount >= maxSecretLovers) {
            return false;
        }
        // Immunity 6: Pregnant with player's child
        if (input.isPregnantWithPlayerChild) {
            return false;
        }
        const int aff = snap.affection->GetPermanent(input.partnerId, 50);
        // Immunity 7: Happy tier (>= 75)
        if (aff >= 75 && !settings.playerPartnerCheatingAllowWhenHappy && !forceBypassFromExpiredSuggestOpen) {
            return false;
        }
        // Immunity 8: Content (50..74) & not neglected (daysSince <= contentNeglectStart)
        const float daysSince = DaysSinceInteraction(snap, input.partnerId, currentGameDay);
        if (aff >= 50 && aff < 75 &&
            daysSince <= settings.GetNeglectContentStartDays() &&
            !forceBypassFromExpiredSuggestOpen) {
            return false;
        }
        return true;
    }

    float ComputePartnerCheatingChance(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyInput& input,
        float currentGameDay,
        int secretLoversCount,
        int sharedChildrenCount,
        bool playerWasCheatingRecently,
        int partnerHierarchyRank) noexcept {
        if (!snap.settings || !snap.relationships || !snap.affection || !snap.actors) {
            return 0.0f;
        }
        const auto& settings = *snap.settings;
        const int aff = snap.affection->GetPermanent(input.partnerId, 50);
        const float daysSince = DaysSinceInteraction(snap, input.partnerId, currentGameDay);
        const auto* profile = snap.actors->Find(input.partnerId.AsRefId());
        const Temperament temp = profile ? profile->temperament : Temperament::Proud;
        const auto status = snap.relationships->GetStatus(ActorId::Player(), input.partnerId);

        float baseAff = 0.0f;
        if (aff < 25) {
            baseAff = 30.0f;
        } else if (aff < 50) {
            baseAff = 18.0f;
        } else if (aff < 75) {
            baseAff = 6.0f;
        } else {
            baseAff = settings.playerPartnerCheatingAllowWhenHappy ? 4.0f : 0.0f;
        }

        float bonusNeglect = 0.0f;
        if (settings.playerPartnerCheatingEnableNeglect && daysSince > settings.GetNeglectTroubledStartDays()) {
            bonusNeglect = std::min(15.0f, (daysSince - settings.GetNeglectTroubledStartDays()) * 2.5f);
        }

        const float bonusRevenge =
            (settings.playerPartnerCheatingEnableRevenge && playerWasCheatingRecently) ? 15.0f : 0.0f;

        float bonusHierarchy = 0.0f;
        if (settings.playerPartnerCheatingEnableHierarchy && partnerHierarchyRank >= 1) {
            bonusHierarchy = (partnerHierarchyRank == 1) ? 4.0f : 8.0f;
        }

        const float mitigationChildren =
            (settings.playerPartnerCheatingEnableChildrenAnchor && sharedChildrenCount > 0)
                ? std::min(12.0f, static_cast<float>(sharedChildrenCount) * 4.0f)
                : 0.0f;

        float multTemperament = 1.0f;
        switch (temp) {
            case Temperament::Humble:
                multTemperament = 0.50f;
                break;
            case Temperament::Romantic:
                multTemperament = (aff < 50 || daysSince > settings.GetNeglectContentStartDays()) ? 1.30f : 0.75f;
                break;
            case Temperament::Proud:
                multTemperament = 1.20f;
                break;
            case Temperament::Independent:
                multTemperament = (daysSince > settings.GetNeglectTroubledStartDays()) ? 1.35f : 1.00f;
                break;
            case Temperament::Jealous:
                multTemperament = playerWasCheatingRecently ? 1.75f : 0.70f;
                break;
            default:
                break;
        }

        float multStage = 1.0f;
        if (status == RelationshipStatus::Lover) {
            multStage = 1.25f;
        } else if (status == RelationshipStatus::Dating) {
            multStage = 1.00f;
        } else if (status == RelationshipStatus::Engaged || status == RelationshipStatus::Married) {
            multStage = 0.80f;
        }

        const float multExistingLovers =
            (secretLoversCount <= 0) ? 1.00f : ((secretLoversCount == 1) ? 0.45f : 0.20f);
        const float multSetting = std::max(0.0f, settings.playerPartnerCheatingChanceMultiplier);
        const float maxCap = static_cast<float>(std::clamp(settings.playerPartnerCheatingMaxRollCap, 0, 100));

        const float additiveCore = std::max(0.0f, baseAff + bonusNeglect + bonusRevenge + bonusHierarchy - mitigationChildren);
        return std::clamp(additiveCore * multTemperament * multStage * multExistingLovers * multSetting, 0.0f, maxCap);
    }

    std::vector<PartnerAgencyActionPlan> EvaluatePartnerAgencyActions(
        const State::WorldStateSnapshot& snap,
        const std::vector<PartnerAgencyInput>& partners,
        float currentGameDay,
        Foundation::IRng& rng) {
        std::vector<PartnerAgencyActionPlan> actions;
        if (!snap.settings || !snap.relationships || !snap.affection || !snap.actors) {
            return actions;
        }

        const auto& settings = *snap.settings;
        const bool playerCheatingRecently =
            snap.affairs && !snap.affairs->GetByCheater(ActorId::Player()).empty();
        int totalPlayerMainPartners = 0;
        for (const auto& [pid, st] : snap.relationships->GetActivePartners(ActorId::Player())) {
            (void)pid;
            if (Foundation::IsMainTierStatus(st)) {
                ++totalPlayerMainPartners;
            }
        }

        for (const auto& input : partners) {
            const ActorId partnerId = input.partnerId;
            if (partnerId.IsNull() || (snap.deceased && snap.deceased->IsDeceased(partnerId))) {
                continue;
            }

            const auto* edge = snap.relationships->FindEdge(ActorId::Player(), partnerId);
            if (!edge || !Foundation::IsMainTierStatus(edge->status)) {
                continue;
            }

            const auto* profile = snap.actors->Find(partnerId.AsRefId());
            const Temperament temp = profile ? profile->temperament : Temperament::Proud;
            const int aff = snap.affection->GetPermanent(partnerId, 50);
            const float daysSince = DaysSinceInteraction(snap, partnerId, currentGameDay);
            const int sharedChildren = CountSharedChildren(snap, partnerId);
            const auto ongoingAffairs = CollectOngoingPlayerAffairsOf(snap, partnerId);
            const int secretLoversCount = static_cast<int>(ongoingAffairs.size());
            const int rank = snap.hierarchy ? snap.hierarchy->GetRank(partnerId) : 4;

            // Step 1: Check expired pending partner intent
            const auto pending = snap.relationships->GetPendingIntent(partnerId);
            bool forceCheatingFromExpiredSuggestOpen = false;
            if (pending.type != PartnerIntentType::None &&
                pending.createdGameDay >= 0.0f &&
                (currentGameDay - pending.createdGameDay) >= settings.GetPartnerIntentTTLDays()) {
                if (pending.type == PartnerIntentType::ConfessTalking && !pending.relatedLover.IsNull()) {
                    PartnerAgencyActionPlan plan{};
                    plan.type = PartnerAgencyActionType::ExpiredIntentConfessEscalatePhysical;
                    plan.partnerId = partnerId;
                    plan.relatedLoverId = pending.relatedLover;
                    plan.escalateTalkingToPhysical = true;
                    actions.push_back(plan);
                } else if (pending.type == PartnerIntentType::SuggestOpen) {
                    PartnerAgencyActionPlan plan{};
                    plan.type = PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair;
                    plan.partnerId = partnerId;
                    plan.forceCheatingFromExpiredSuggestOpen = true;
                    actions.push_back(plan);
                    forceCheatingFromExpiredSuggestOpen = true;
                } else if (pending.type == PartnerIntentType::SuggestClose) {
                    PartnerAgencyActionPlan plan{};
                    plan.type = PartnerAgencyActionType::ExpiredIntentSuggestClosePenalty;
                    plan.partnerId = partnerId;
                    plan.permanentAffectionDelta = -12;
                    actions.push_back(plan);
                }
            }

            // Step 2: Talking -> Physical escalation on ongoing affairs
            for (const auto& affRec : ongoingAffairs) {
                if (affRec.affairStage == AffairStage::Talking &&
                    (currentGameDay - affRec.stageStartDay) >= settings.GetAffairTalkingEscalationDays()) {
                    PartnerAgencyActionPlan esc{};
                    esc.type = PartnerAgencyActionType::EscalateTalkingToPhysical;
                    esc.partnerId = partnerId;
                    esc.relatedLoverId = affRec.paramour;
                    esc.escalateTalkingToPhysical = true;
                    actions.push_back(esc);
                }
            }

            // Immunity 3: Follower or physically near player skips active rolls
            if (input.isFollowerOrNearPlayer) {
                continue;
            }

            // Action A: Closed affair breakup / 20% confess sub-roll
            const float multBreakupSetting = std::clamp(settings.playerPartnerAgencyBreakupChanceMultiplier, 0.0f, 3.0f);
            bool brokeClosedAffair = false;
            for (const auto& affRec : ongoingAffairs) {
                if ((currentGameDay - affRec.stageStartDay) < settings.GetMinSideLoverAgeForBreakupDays()) {
                    continue;
                }
                const float baseA = (aff >= 60) ? 14.0f : 6.0f;
                const float multTempA = (temp == Temperament::Humble || temp == Temperament::Romantic) ? 1.35f : 0.85f;
                const float multStageA = (affRec.affairStage == AffairStage::Talking) ? 1.40f : 0.85f;
                const float pBreakup = std::clamp(baseA * multTempA * multStageA * multBreakupSetting, 0.0f, 75.0f);
                if (rng.RollPercent(pBreakup)) {
                    PartnerAgencyActionPlan p{};
                    p.type = PartnerAgencyActionType::BreakupClosedAffairLover;
                    p.partnerId = partnerId;
                    p.relatedLoverId = affRec.paramour;
                    p.rolledChance = pBreakup;
                    if (settings.playerPartnerAgencyEnableForceGreets &&
                        (temp == Temperament::Humble || temp == Temperament::Romantic) &&
                        aff >= 60 &&
                        rng.RollPercent(20.0f)) {
                        p.triggerConfessIntent = true;
                    }
                    actions.push_back(p);
                    brokeClosedAffair = true;
                    break;
                }
            }

            if (!brokeClosedAffair && !ongoingAffairs.empty() &&
                settings.playerPartnerAgencyEnableForceGreets &&
                pending.type == PartnerIntentType::None &&
                (temp == Temperament::Humble || temp == Temperament::Romantic) &&
                aff >= 60 &&
                daysSince <= settings.GetRecentEffortMaxDays() &&
                rng.RollPercent(20.0f)) {
                PartnerAgencyActionPlan conf{};
                conf.type = PartnerAgencyActionType::ConfessAffairIntent;
                conf.partnerId = partnerId;
                conf.relatedLoverId = ongoingAffairs.front().paramour;
                conf.rolledChance = 20.0f;
                conf.triggerConfessIntent = true;
                actions.push_back(conf);
            }

            // Action E: SuggestOpen / SuggestClose forcegreet intents
            if (settings.playerPartnerAgencyEnableForceGreets &&
                pending.type == PartnerIntentType::None &&
                !forceCheatingFromExpiredSuggestOpen &&
                !IsNpcOnMatchmakingCooldown(snap, partnerId, currentGameDay)) {
                if (!edge->isOpen && settings.playerPartnerOpenRelAgencyEnabled) {
                    float baseTempE1 = 0.0f;
                    if (temp == Temperament::Independent) baseTempE1 = 12.0f;
                    else if (temp == Temperament::Proud) baseTempE1 = (rank >= 1) ? 8.0f : 3.0f;
                    else if (temp == Temperament::Romantic) {
                        baseTempE1 = (daysSince > settings.GetSuggestOpenNeglectMinDays()) ? 8.0f : 0.0f;
                    }
                    if (baseTempE1 > 0.0f) {
                        const float bonusPolyE1 = (totalPlayerMainPartners > 1) ? 8.0f : 0.0f;
                        const float bonusNeglectE1 = (daysSince > settings.GetSuggestOpenNeglectMinDays())
                                                         ? std::min(10.0f, (daysSince - settings.GetSuggestOpenNeglectMinDays()) * 2.0f)
                                                         : 0.0f;
                        const float pSuggestOpen = std::clamp(baseTempE1 + bonusPolyE1 + bonusNeglectE1, 0.0f, 60.0f);
                        if (rng.RollPercent(pSuggestOpen)) {
                            PartnerAgencyActionPlan e1{};
                            e1.type = PartnerAgencyActionType::SuggestOpenIntent;
                            e1.partnerId = partnerId;
                            e1.rolledChance = pSuggestOpen;
                            actions.push_back(e1);
                        }
                    }
                } else if (edge->isOpen) {
                    float baseTempE2 = 0.0f;
                    if (temp == Temperament::Jealous) baseTempE2 = 18.0f;
                    else if (temp == Temperament::Romantic) baseTempE2 = (aff >= 75) ? 12.0f : 5.0f;
                    else if (temp == Temperament::Humble) baseTempE2 = (aff >= 75) ? 8.0f : 2.0f;
                    else if (temp == Temperament::Proud) baseTempE2 = 4.0f;
                    if (baseTempE2 > 0.0f) {
                        const float bonusNoSideE2 = (secretLoversCount == 0) ? 8.0f : 0.0f;
                        const float bonusChildrenE2 = (sharedChildren > 0) ? 6.0f : 0.0f;
                        const float pSuggestClose = std::clamp(baseTempE2 + bonusNoSideE2 + bonusChildrenE2, 0.0f, 65.0f);
                        if (rng.RollPercent(pSuggestClose)) {
                            PartnerAgencyActionPlan e2{};
                            e2.type = PartnerAgencyActionType::SuggestCloseIntent;
                            e2.partnerId = partnerId;
                            e2.rolledChance = pSuggestClose;
                            actions.push_back(e2);
                        }
                    }
                }
            }

            if (input.isPregnantWithPlayerChild) {
                continue;
            }

            // Action D: Open-relationship partner seeks an open lover
            if (edge->isOpen) {
                if (settings.playerPartnerOpenRelAgencyEnabled &&
                    !IsNpcOnMatchmakingCooldown(snap, partnerId, currentGameDay)) {
                    float baseTempD = 8.0f;
                    switch (temp) {
                        case Temperament::Independent: baseTempD = 22.0f; break;
                        case Temperament::Romantic:    baseTempD = 16.0f; break;
                        case Temperament::Proud:       baseTempD = 14.0f; break;
                        case Temperament::Humble:      baseTempD = 8.0f;  break;
                        case Temperament::Jealous:     baseTempD = 5.0f;  break;
                        default: break;
                    }
                    const float bonusLonelinessD =
                        (daysSince > settings.GetLonelinessDecayStartDays())
                            ? std::min(15.0f, (daysSince - settings.GetLonelinessDecayStartDays()) * 2.5f)
                            : 0.0f;
                    const float bonusPolyParityD = (totalPlayerMainPartners > 1) ? 8.0f : 0.0f;
                    const float mitChildrenD =
                        (settings.playerPartnerCheatingEnableChildrenAnchor && sharedChildren > 0) ? 5.0f : 0.0f;
                    const float mitRecentVisitD =
                        (daysSince <= settings.GetOpenRecentVisitMitigationMaxDays()) ? 8.0f : 0.0f;
                    const float multExistingLovers =
                        (secretLoversCount <= 0) ? 1.00f : ((secretLoversCount == 1) ? 0.45f : 0.20f);
                    const float multOpenSetting = std::clamp(settings.playerPartnerAgencyOpenLoverChanceMultiplier, 0.0f, 3.0f);
                    const float additiveOpen =
                        std::max(0.0f, baseTempD + bonusLonelinessD + bonusPolyParityD - mitChildrenD - mitRecentVisitD);
                    const float finalOpenChance =
                        std::clamp(additiveOpen * multExistingLovers * multOpenSetting, 0.0f, 65.0f);
                    if (finalOpenChance > 0.0f && rng.RollPercent(finalOpenChance)) {
                        PartnerAgencyActionPlan seekOpen{};
                        seekOpen.type = PartnerAgencyActionType::SeekOpenLover;
                        seekOpen.partnerId = partnerId;
                        seekOpen.rolledChance = finalOpenChance;
                        actions.push_back(seekOpen);
                    }
                }
                continue;
            }

            // Action C: Secret cheating temptation on exclusive main pair
            if (forceCheatingFromExpiredSuggestOpen) {
                PartnerAgencyActionPlan forced{};
                forced.type = PartnerAgencyActionType::SeekSecretAffair;
                forced.partnerId = partnerId;
                forced.rolledChance = 100.0f;
                forced.forceCheatingFromExpiredSuggestOpen = true;
                actions.push_back(forced);
                continue;
            }

            if (IsPartnerAgencyCheatingAllowedByImmunities(snap, input, currentGameDay, false)) {
                const float finalChance = ComputePartnerCheatingChance(
                    snap, input, currentGameDay, secretLoversCount, sharedChildren, playerCheatingRecently, rank);
                if (finalChance > 0.0f && rng.RollPercent(finalChance)) {
                    PartnerAgencyActionPlan cheatPlan{};
                    cheatPlan.type = PartnerAgencyActionType::SeekSecretAffair;
                    cheatPlan.partnerId = partnerId;
                    cheatPlan.rolledChance = finalChance;
                    actions.push_back(cheatPlan);
                }
            }
        }

        return actions;
    }

    std::optional<HomeArrivalPlan> EvaluatePartnerHomeArrival(
        const State::WorldStateSnapshot& snap,
        const State::SpatialActorSnapshot& spatial,
        LocationId playerLocationId,
        bool playerHomeHasTavernMarker,
        int currentHourOfDay,
        float currentGameDay,
        Foundation::IRng& rng) {
        if (!snap.settings || !snap.relationships || !snap.actors) {
            return std::nullopt;
        }
        const auto& settings = *snap.settings;
        const auto* playerSample = spatial.Find(State::RefId::Player());

        // Priority 1: Non-expired pending forcegreet intent for any loaded nearby partner
        for (const auto& [partnerId, intent] : snap.relationships->AllPendingIntents()) {
            if (intent.type == PartnerIntentType::None) {
                continue;
            }
            if (intent.createdGameDay >= 0.0f &&
                (currentGameDay - intent.createdGameDay) >= settings.GetPartnerIntentTTLDays()) {
                continue;
            }
            const auto* sample = spatial.Find(partnerId.AsRefId());
            if (!sample || sample->IsDead() || sample->IsDisabled() || sample->IsDeleted()) {
                continue;
            }
            if (playerSample && !playerSample->parentCellId.IsNull() &&
                sample->parentCellId == playerSample->parentCellId) {
                const auto* prof = snap.actors->Find(partnerId.AsRefId());
                return HomeArrivalPlan{
                    .kind = HomeArrivalKind::PendingIntentForcegreet,
                    .partnerId = partnerId,
                    .relatedLoverId = intent.relatedLover,
                    .intentType = intent.type,
                    .partnerTemperament = prof ? prof->temperament : Temperament::Proud
                };
            }
        }

        if (!settings.playerPartnerAgencyEnableHomeArrivalEvents || playerLocationId.IsNull()) {
            return std::nullopt;
        }

        // Priority 2: Home arrival cooldown check per LocationId
        if (snap.cooldowns) {
            const auto itCool = snap.cooldowns->lastHomeArrivalEventDayByLoc.find(playerLocationId);
            if (itCool != snap.cooldowns->lastHomeArrivalEventDayByLoc.end() &&
                itCool->second >= 0.0f &&
                (currentGameDay - itCool->second) < settings.GetHomeArrivalCooldownDays()) {
                return std::nullopt;
            }
        }

        const float lonelinessStart = settings.GetLonelinessDecayStartDays();
        for (const auto& [partnerId, st] : snap.relationships->GetActivePartners(ActorId::Player())) {
            if (!Foundation::IsMainTierStatus(st)) {
                continue;
            }
            if (snap.deceased && snap.deceased->IsDeceased(partnerId)) {
                continue;
            }

            const float daysSince = DaysSinceInteraction(snap, partnerId, currentGameDay);
            const int chance = ComputeHomeArrivalTriggerChance(daysSince, lonelinessStart, currentHourOfDay);
            if (!rng.RollPercent(static_cast<float>(chance))) {
                continue;
            }

            const auto* prof = snap.actors->Find(partnerId.AsRefId());
            const Temperament temp = prof ? prof->temperament : Temperament::Proud;
            const bool partnerInCell = [&]() {
                const auto* ps = spatial.Find(partnerId.AsRefId());
                return ps && playerSample && ps->parentCellId == playerSample->parentCellId && !ps->IsDead();
            }();

            const auto* edge = snap.relationships->FindEdge(ActorId::Player(), partnerId);
            if (edge && edge->isOpen) {
                const bool preferLetter = !partnerInCell || (playerHomeHasTavernMarker && rng.RollPercent(50.0f));
                return HomeArrivalPlan{
                    .kind = preferLetter ? HomeArrivalKind::OpenDateLetter : HomeArrivalKind::OpenDateInHouse,
                    .partnerId = partnerId,
                    .partnerTemperament = temp
                };
            }

            const auto affairs = CollectOngoingPlayerAffairsOf(snap, partnerId);
            if (!affairs.empty()) {
                const auto& aff = affairs.front();
                return HomeArrivalPlan{
                    .kind = (aff.affairStage == AffairStage::Talking)
                                ? HomeArrivalKind::CheatingTalkingLetter
                                : HomeArrivalKind::CheatingPhysicalLetter,
                    .partnerId = partnerId,
                    .relatedLoverId = aff.paramour,
                    .partnerTemperament = temp
                };
            }
        }

        return std::nullopt;
    }

}  // namespace MARAS::Domain::Agency
