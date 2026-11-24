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

        [[nodiscard]] std::vector<ActorId> CollectActiveNonPlayerLovers(
            const State::WorldStateSnapshot& snap,
            ActorId partnerId) {
            std::vector<ActorId> result;
            if (!snap.relationships) {
                return result;
            }
            for (const auto& [otherId, st] : snap.relationships->GetActivePartners(partnerId)) {
                if (st == RelationshipStatus::Lover && !otherId.IsNull() && !otherId.IsPlayer()) {
                    if (!snap.deceased || !snap.deceased->IsDeceased(otherId)) {
                        result.push_back(otherId);
                    }
                }
            }
            return result;
        }

        [[nodiscard]] float GetLatestNonBreakupStartDay(
            const State::WorldStateSnapshot& snap,
            ActorId a,
            ActorId b) noexcept {
            if (!snap.relationships) {
                return -1.0f;
            }
            const auto* edge = snap.relationships->FindEdge(a, b);
            if (!edge) {
                return -1.0f;
            }
            for (auto it = edge->statusHistory.rbegin(); it != edge->statusHistory.rend(); ++it) {
                if (!Foundation::IsBreakupStatus(it->toStatus) && it->timestamp >= 0.0f) {
                    return it->timestamp;
                }
            }
            return -1.0f;
        }

        [[nodiscard]] bool HasDiscoveredOngoingPlayerAffairAgainst(
            const State::WorldStateSnapshot& snap,
            ActorId partnerId) noexcept {
            if (!snap.affairs) {
                return false;
            }
            for (const auto& rec : snap.affairs->GetByCheater(ActorId::Player())) {
                if (rec.deceivedPartner == partnerId &&
                    rec.affairStatus == Foundation::AffairStatus::Ongoing &&
                    !rec.isSecret) {
                    return true;
                }
            }
            return false;
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

        [[nodiscard]] bool ContainsActor(const std::vector<ActorId>& list, ActorId id) noexcept {
            return std::find(list.begin(), list.end(), id) != list.end();
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
        int totalPlayerMainPartners = 0;
        for (const auto& [pid, st] : snap.relationships->GetActivePartners(ActorId::Player())) {
            (void)pid;
            if (Foundation::IsMainTierStatus(st)) {
                ++totalPlayerMainPartners;
            }
        }

        const int maxSideLovers = std::clamp(settings.playerPartnerCheatingMaxSecretLovers, 1, 3);

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
            const auto openLovers = edge->isOpen ? CollectActiveNonPlayerLovers(snap, partnerId) : std::vector<ActorId>{};
            const int sideLoversCount = edge->isOpen
                                            ? static_cast<int>(openLovers.size())
                                            : static_cast<int>(ongoingAffairs.size());
            // M6: Only Married spouses participate in the spouse hierarchy; Dating/Engaged have rank -1.
            const int rank = (edge->status == RelationshipStatus::Married && snap.hierarchy)
                                 ? snap.hierarchy->GetRank(partnerId)
                                 : -1;
            // H1.2: Revenge triggers ONLY for ongoing, non-secret player affairs against THIS partner.
            const bool playerCheatedOnPartner =
                settings.playerPartnerCheatingEnableRevenge &&
                HasDiscoveredOngoingPlayerAffairAgainst(snap, partnerId);

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
                    // C6: Set flag so Action C emits exactly ONE ExpiredIntentForceSuggestOpenAffair (not two).
                    if (!edge->isOpen && settings.playerPartnerCheatingAgencyEnabled) {
                        forceCheatingFromExpiredSuggestOpen = true;
                    }
                } else if (pending.type == PartnerIntentType::SuggestClose) {
                    PartnerAgencyActionPlan plan{};
                    plan.type = PartnerAgencyActionType::ExpiredIntentSuggestClosePenalty;
                    plan.partnerId = partnerId;
                    plan.permanentAffectionDelta = -12;
                    actions.push_back(plan);
                }
            }

            // Step 2: Talking -> Physical escalation on ongoing affairs (M9: gated on cheating agency enabled)
            if (!edge->isOpen && settings.playerPartnerCheatingAgencyEnabled) {
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
            }

            // Immunity 3: Follower or physically near player skips active rolls
            if (input.isFollowerOrNearPlayer) {
                continue;
            }

            const float multBreakupSetting = std::clamp(settings.playerPartnerAgencyBreakupChanceMultiplier, 0.0f, 3.0f);

            // Action A (Closed Affair Breakup / Confess) & Action B (Open-Lover Breakup)
            int activeSideLoversCount = sideLoversCount;
            if (!edge->isOpen) {
                // M9: Gate Action A on playerPartnerCheatingAgencyEnabled
                if (settings.playerPartnerCheatingAgencyEnabled) {
                    bool brokeClosedAffair = false;
                    for (const auto& affRec : ongoingAffairs) {
                        const float startDay = GetLatestNonBreakupStartDay(snap, partnerId, affRec.paramour);
                        const float affairStart = (startDay >= 0.0f) ? startDay : affRec.stageStartDay;
                        if (multBreakupSetting <= 0.0f ||
                            (currentGameDay - affairStart) < settings.GetMinSideLoverAgeForBreakupDays()) {
                            continue;
                        }
                        const float baseA = (aff >= 60) ? 14.0f : 6.0f;
                        const float multTempA = (temp == Temperament::Humble || temp == Temperament::Romantic) ? 1.35f : 0.85f;
                        const float multStageA = (affRec.affairStage == AffairStage::Talking) ? 1.40f : 0.85f;
                        const float pBreakup = std::clamp(baseA * multTempA * multStageA * multBreakupSetting, 0.0f, 75.0f);
                        if (pBreakup > 0.0f && rng.RollPercent(pBreakup)) {
                            PartnerAgencyActionPlan p{};
                            p.type = PartnerAgencyActionType::BreakupClosedAffairLover;
                            p.partnerId = partnerId;
                            p.relatedLoverId = affRec.paramour;
                            p.rolledChance = pBreakup;
                            if (settings.playerPartnerAgencyEnableForceGreets &&
                                pending.type == PartnerIntentType::None &&
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
                }
            } else if (settings.playerPartnerOpenRelAgencyEnabled) {
                // H8: Action B — Open-relationship side-lover breakup roll (matching 0cebd663:1943-2023)
                for (const ActorId loverId : openLovers) {
                    const float startDay = GetLatestNonBreakupStartDay(snap, partnerId, loverId);
                    const float flingAgeDays = (startDay >= 0.0f) ? std::max(0.0f, currentGameDay - startDay) : 0.0f;
                    if (multBreakupSetting <= 0.0f || flingAgeDays < settings.GetMinSideLoverAgeForBreakupDays()) {
                        continue;
                    }
                    const float baseFlingAge =
                        (flingAgeDays <= settings.GetOpenFlingNoveltyDays())
                            ? 8.0f
                            : std::min(28.0f, 8.0f + (flingAgeDays - settings.GetOpenFlingNoveltyDays()) * 1.0f);
                    const float bonusMultiLovers =
                        (activeSideLoversCount >= 2) ? static_cast<float>(activeSideLoversCount - 1) * 8.0f : 0.0f;
                    const float bonusPlayerDevotion =
                        (aff >= 80 && daysSince <= settings.GetRecentEffortMaxDays()) ? 10.0f : 0.0f;
                    float multTempB = 1.00f;
                    switch (temp) {
                        case Temperament::Independent:
                            multTempB = 1.30f;
                            break;
                        case Temperament::Proud:
                            multTempB = 1.15f;
                            break;
                        case Temperament::Romantic:
                            multTempB = 0.75f;
                            break;
                        default:
                            break;
                    }
                    const float pOpenBreakup = std::clamp(
                        (baseFlingAge + bonusMultiLovers + bonusPlayerDevotion) * multTempB * multBreakupSetting,
                        0.0f, 75.0f);
                    if (pOpenBreakup > 0.0f && rng.RollPercent(pOpenBreakup)) {
                        PartnerAgencyActionPlan br{};
                        br.type = PartnerAgencyActionType::BreakupOpenLover;
                        br.partnerId = partnerId;
                        br.relatedLoverId = loverId;
                        br.rolledChance = pOpenBreakup;
                        actions.push_back(br);
                        if (activeSideLoversCount > 0) {
                            --activeSideLoversCount;
                        }
                    }
                }
            }

            // Action E: SuggestOpen / SuggestClose forcegreet intents
            if (settings.playerPartnerAgencyEnableForceGreets &&
                settings.playerPartnerOpenRelAgencyEnabled &&
                pending.type == PartnerIntentType::None &&
                !forceCheatingFromExpiredSuggestOpen &&
                !IsNpcOnMatchmakingCooldown(snap, partnerId, currentGameDay)) {
                if (!edge->isOpen && sideLoversCount == 0) {
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
                } else {
                    float baseTempE2 = 0.0f;
                    if (temp == Temperament::Jealous) baseTempE2 = 18.0f;
                    else if (temp == Temperament::Romantic) baseTempE2 = (aff >= 75) ? 12.0f : 5.0f;
                    else if (temp == Temperament::Humble) baseTempE2 = (aff >= 75) ? 8.0f : 2.0f;
                    else if (temp == Temperament::Proud) baseTempE2 = 4.0f;
                    if (baseTempE2 > 0.0f) {
                        const float bonusNoSideE2 = (sideLoversCount == 0) ? 8.0f : 0.0f;
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
                    sideLoversCount < maxSideLovers &&
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
                        (sideLoversCount <= 0) ? 1.00f : ((sideLoversCount == 1) ? 0.45f : 0.20f);
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
                if (sideLoversCount < maxSideLovers) {
                    PartnerAgencyActionPlan forced{};
                    forced.type = PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair;
                    forced.partnerId = partnerId;
                    forced.rolledChance = 100.0f;
                    forced.forceCheatingFromExpiredSuggestOpen = true;
                    actions.push_back(forced);
                }
                continue;
            }

            if (IsPartnerAgencyCheatingAllowedByImmunities(snap, input, currentGameDay, false)) {
                const float finalChance = ComputePartnerCheatingChance(
                    snap, input, currentGameDay, sideLoversCount, sharedChildren, playerCheatedOnPartner, rank);
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
        Foundation::IRng& rng,
        const std::vector<HomeArrivalPartnerCandidate>& partnerCandidates) {
        if (!snap.settings || !snap.relationships || !snap.actors) {
            return std::nullopt;
        }
        const auto& settings = *snap.settings;
        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            return std::nullopt;
        }
        if (!settings.playerPartnerAgencyEnableHomeArrivalEvents || playerLocationId.IsNull()) {
            return std::nullopt;
        }

        const auto* playerSample = spatial.Find(State::RefId::Player());

        struct ResidentEntry {
            ActorId partnerId{};
            bool partnerInCell{ false };
            bool isBusyInScene{ false };
            const std::vector<ActorId>* ineligibleSideLovers{ nullptr };
        };

        auto isSampleInPlayerCell = [&](ActorId partnerId) noexcept -> bool {
            const auto* ps = spatial.Find(partnerId.AsRefId());
            if (!ps || ps->IsDead() || ps->IsDisabled() || ps->IsDeleted()) {
                return false;
            }
            return playerSample && !playerSample->parentCellId.IsNull() &&
                   ps->parentCellId == playerSample->parentCellId;
        };

        auto checkResidesAtPlayerLocation = [&](ActorId partnerId,
                                                LocationId nativeHomeLocId,
                                                bool explicitResides) noexcept -> bool {
            const State::HouseId tenantHouse =
                snap.household ? snap.household->GetTenantHouse(partnerId.raw()) : State::HouseId{};
            if (!tenantHouse.IsNull()) {
                return (tenantHouse.raw() == playerLocationId.raw()) || explicitResides;
            }
            return (!nativeHomeLocId.IsNull() && nativeHomeLocId == playerLocationId) || explicitResides;
        };

        std::vector<ResidentEntry> residents;
        if (!partnerCandidates.empty()) {
            residents.reserve(partnerCandidates.size());
            for (const auto& cand : partnerCandidates) {
                const ActorId pid = cand.partnerId;
                if (pid.IsNull() || (snap.deceased && snap.deceased->IsDeceased(pid))) {
                    continue;
                }
                const auto* edge = snap.relationships->FindEdge(ActorId::Player(), pid);
                if (!edge || !Foundation::IsMainTierStatus(edge->status)) {
                    continue;
                }
                if (!checkResidesAtPlayerLocation(pid, cand.nativeHomeLocationId, cand.residesAtPlayerLocation)) {
                    continue;
                }
                residents.push_back(ResidentEntry{
                    .partnerId = pid,
                    .partnerInCell = cand.isPartnerInCurrentCell || isSampleInPlayerCell(pid),
                    .isBusyInScene = cand.isBusyInScene,
                    .ineligibleSideLovers = &cand.ineligibleSideLovers
                });
            }
        } else {
            for (const auto& [pid, st] : snap.relationships->GetActivePartners(ActorId::Player())) {
                if (!Foundation::IsMainTierStatus(st)) {
                    continue;
                }
                if (snap.deceased && snap.deceased->IsDeceased(pid)) {
                    continue;
                }
                if (!checkResidesAtPlayerLocation(pid, LocationId{}, false)) {
                    continue;
                }
                residents.push_back(ResidentEntry{
                    .partnerId = pid,
                    .partnerInCell = isSampleInPlayerCell(pid),
                    .isBusyInScene = false,
                    .ineligibleSideLovers = nullptr
                });
            }
        }

        if (residents.empty()) {
            return std::nullopt;
        }

        // Priority 1: Non-expired pending forcegreet intent for any resident partner present in the arrived cell
        if (settings.playerPartnerAgencyEnableForceGreets) {
            for (const auto& res : residents) {
                const auto intent = snap.relationships->GetPendingIntent(res.partnerId);
                if (intent.type == PartnerIntentType::None) {
                    continue;
                }
                if (intent.createdGameDay >= 0.0f &&
                    (currentGameDay - intent.createdGameDay) >= settings.GetPartnerIntentTTLDays()) {
                    continue;
                }
                const bool intentAllowed = [&]() noexcept {
                    switch (intent.type) {
                        case PartnerIntentType::SuggestOpen:
                        case PartnerIntentType::SuggestClose:
                            return settings.playerPartnerOpenRelAgencyEnabled;
                        case PartnerIntentType::ConfessTalking:
                        case PartnerIntentType::ConfessPhysical:
                            return settings.playerPartnerCheatingAgencyEnabled;
                        default:
                            return false;
                    }
                }();
                if (!intentAllowed || !res.partnerInCell) {
                    continue;
                }
                const auto* prof = snap.actors->Find(res.partnerId.AsRefId());
                return HomeArrivalPlan{
                    .kind = HomeArrivalKind::PendingIntentForcegreet,
                    .partnerId = res.partnerId,
                    .relatedLoverId = intent.relatedLover,
                    .intentType = intent.type,
                    .partnerTemperament = prof ? prof->temperament : Temperament::Proud
                };
            }
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
        for (const auto& res : residents) {
            if (res.isBusyInScene) {
                continue;
            }
            const ActorId partnerId = res.partnerId;
            const auto* edge = snap.relationships->FindEdge(ActorId::Player(), partnerId);
            if (!edge) {
                continue;
            }

            if (edge->isOpen && !settings.playerPartnerOpenRelAgencyEnabled) {
                continue;
            }
            if (!edge->isOpen && !settings.playerPartnerCheatingAgencyEnabled) {
                continue;
            }

            auto isEligibleLover = [&](ActorId lid) noexcept {
                if (lid.IsNull() || lid.IsPlayer()) {
                    return false;
                }
                if (snap.deceased && snap.deceased->IsDeceased(lid)) {
                    return false;
                }
                if (res.ineligibleSideLovers && ContainsActor(*res.ineligibleSideLovers, lid)) {
                    return false;
                }
                return true;
            };

            ActorId activeLoverId{};
            AffairStage activeStage = AffairStage::Physical;
            if (edge->isOpen) {
                for (const auto& [otherId, st] : snap.relationships->GetActivePartners(partnerId)) {
                    if (st == RelationshipStatus::Lover && isEligibleLover(otherId)) {
                        activeLoverId = otherId;
                        break;
                    }
                }
            } else {
                for (const auto& aff : CollectOngoingPlayerAffairsOf(snap, partnerId)) {
                    if (isEligibleLover(aff.paramour)) {
                        activeLoverId = aff.paramour;
                        activeStage = aff.affairStage;
                        break;
                    }
                }
            }

            if (activeLoverId.IsNull()) {
                continue;
            }

            const float daysSince = DaysSinceInteraction(snap, partnerId, currentGameDay);
            const int chance = ComputeHomeArrivalTriggerChance(daysSince, lonelinessStart, currentHourOfDay);
            if (!rng.RollPercent(static_cast<float>(chance))) {
                continue;
            }

            bool isLetterScenario = res.partnerInCell ? rng.RollPercent(50.0f) : true;
            if (isLetterScenario && !playerHomeHasTavernMarker) {
                if (!res.partnerInCell) {
                    continue;
                }
                isLetterScenario = false;
            }

            HomeArrivalKind kind = HomeArrivalKind::None;
            if (edge->isOpen) {
                kind = isLetterScenario ? HomeArrivalKind::OpenDateLetter : HomeArrivalKind::OpenDateInHouse;
            } else if (activeStage == AffairStage::Talking) {
                kind = isLetterScenario ? HomeArrivalKind::CheatingTalkingLetter : HomeArrivalKind::CheatingTalkingInHouse;
            } else {
                kind = isLetterScenario ? HomeArrivalKind::CheatingPhysicalLetter : HomeArrivalKind::CheatingPhysicalInHouse;
            }

            const auto* prof = snap.actors->Find(partnerId.AsRefId());
            const Temperament temp = prof ? prof->temperament : Temperament::Proud;
            return HomeArrivalPlan{
                .kind = kind,
                .partnerId = partnerId,
                .relatedLoverId = activeLoverId,
                .partnerTemperament = temp
            };
        }

        return std::nullopt;
    }

}  // namespace MARAS::Domain::Agency
