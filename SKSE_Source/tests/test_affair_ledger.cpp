#include "TestHarness.h"

#include "maras/state/WorldState.h"

#include <cstdint>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
    }

    MARAS_TEST(AffairLedger, DirectionalInfidelityAsymmetry) {
        const ActorId partnerA{ 0x00010010u };
        const ActorId partnerB{ 0x00010020u };
        const ActorId paramourC{ 0x00010030u };

        WorldState state{};
        (void)state.PromoteToMarried(partnerA, partnerB, 5.0f);

        // partnerA cheats on partnerB with paramourC; partnerB remains completely faithful
        MARAS_REQUIRE(state.RecordCheatingIncident(
            partnerA,
            partnerB,
            paramourC,
            /*isSecret=*/true,
            AffairType::Lover,
            AffairStatus::Ongoing,
            AffairStage::Physical,
            10.0f));

        MARAS_CHECK(state.affairs->HasCheatedOn(partnerA, partnerB));
        MARAS_CHECK(!state.affairs->HasCheatedOn(partnerB, partnerA));

        MARAS_CHECK(state.IsActorCheating(partnerA));
        MARAS_CHECK(!state.IsActorCheating(partnerB));

        // The affair pair (partnerA, paramourC) is cheating; the main marriage (partnerA, partnerB) is not
        MARAS_CHECK(state.IsRelationshipCheating(partnerA, paramourC));
        MARAS_CHECK(state.IsRelationshipCheating(paramourC, partnerA));
        MARAS_CHECK(!state.IsRelationshipCheating(partnerA, partnerB));
    }

    MARAS_TEST(AffairLedger, TalkingToPhysicalEscalationAndPartnerIntentAutoUpgrade) {
        const ActorId playerSpouse{ 0x0001A694u };
        const ActorId paramour{ 0x00020545u };
        const ActorId npcHusb{ 0x00030001u };
        const ActorId npcWife{ 0x00030002u };

        WorldState state{};
        (void)state.PromoteToMarried(ActorId::Player(), playerSpouse, 3.0f);
        (void)state.PromoteToMarried(npcHusb, npcWife, 3.0f);

        // 1. Player's spouse starts at Talking stage with a pending ConfessTalking intent
        MARAS_REQUIRE(state.RecordCheatingIncident(
            playerSpouse,
            ActorId::Player(),
            paramour,
            /*isSecret=*/true,
            AffairType::Dating,
            AffairStatus::Ongoing,
            AffairStage::Talking,
            10.0f));

        state.relationships.Write().SetPendingIntent(
            playerSpouse,
            PendingPartnerIntentRecord{
                .type = PartnerIntentType::ConfessTalking,
                .createdGameDay = 10.5f,
                .relatedLover = paramour,
            });

        const auto* initialRec = state.affairs->FindTriple(playerSpouse, ActorId::Player(), paramour);
        MARAS_REQUIRE(initialRec != nullptr);
        MARAS_CHECK_EQ(initialRec->affairStage, AffairStage::Talking);
        MARAS_CHECK_EQ(initialRec->incidentCount, 1u);

        // 2. Escalate to Physical -> AffairStage becomes Physical AND ConfessTalking auto-upgrades to ConfessPhysical!
        MARAS_REQUIRE(state.RecordCheatingIncident(
            playerSpouse,
            ActorId::Player(),
            paramour,
            /*isSecret=*/true,
            AffairType::Lover,
            AffairStatus::Ongoing,
            AffairStage::Physical,
            15.25f));

        const auto* escalatedRec = state.affairs->FindTriple(playerSpouse, ActorId::Player(), paramour);
        MARAS_REQUIRE(escalatedRec != nullptr);
        MARAS_CHECK_EQ(escalatedRec->affairStage, AffairStage::Physical);
        MARAS_CHECK_EQ(escalatedRec->stageStartDay, 15.25f);
        MARAS_CHECK_EQ(escalatedRec->incidentCount, 2u);
        MARAS_CHECK_EQ(escalatedRec->affairType, AffairType::Lover);

        const PendingPartnerIntentRecord upgradedIntent = state.relationships->GetPendingIntent(playerSpouse);
        MARAS_CHECK_EQ(upgradedIntent.type, PartnerIntentType::ConfessPhysical);
        MARAS_CHECK_EQ(upgradedIntent.createdGameDay, 15.25f);
        MARAS_CHECK_EQ(upgradedIntent.relatedLover, paramour);

        // 3. Non-Player victim: Talking stage is clamped immediately to Physical
        MARAS_REQUIRE(state.RecordCheatingIncident(
            npcWife,
            npcHusb,
            paramour,
            /*isSecret=*/true,
            AffairType::Casual,
            AffairStatus::Ongoing,
            AffairStage::Talking,
            16.0f));

        const auto* npcRec = state.affairs->FindTriple(npcWife, npcHusb, paramour);
        MARAS_REQUIRE(npcRec != nullptr);
        MARAS_CHECK_EQ(npcRec->affairStage, AffairStage::Physical);
    }

    MARAS_TEST(AffairLedger, OpenRelationshipPolygamyAndCoSpouseExemptions) {
        const ActorId spouseA{ 0x0001A694u };
        const ActorId spouseB{ 0x00019DF7u };
        const ActorId paramour{ 0x00020545u };
        const ActorId npcA{ 0x00030001u };
        const ActorId npcB{ 0x00030002u };

        WorldState state{};
        (void)state.PromoteToMarried(ActorId::Player(), spouseA, 2.0f);
        (void)state.PromoteToMarried(ActorId::Player(), spouseB, 4.0f);
        (void)state.PromoteToMarried(npcA, npcB, 5.0f);

        // 1. Player polygamy enabled -> Player cannot be flagged as cheating
        state.settings.Write().enablePlayerPolygamy = true;
        MARAS_CHECK(!state.RecordCheatingIncident(
            ActorId::Player(), spouseA, paramour, true, AffairType::Casual, AffairStatus::Ongoing, AffairStage::Physical, 6.0f));
        MARAS_CHECK(!state.IsActorCheating(ActorId::Player()));

        // 2. Player co-spouses being intimate with each other is NOT cheating on the Player
        MARAS_CHECK(!state.RecordCheatingIncident(
            spouseA, ActorId::Player(), spouseB, false, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 7.0f));
        (void)state.TransitionRelationship(spouseA, spouseB, RelationshipStatus::Lover, 7.0f);
        MARAS_CHECK(!state.IsRelationshipCheating(spouseA, spouseB));
        MARAS_CHECK(!state.IsActorCheating(spouseA));

        // 3. Disabling Player polygamy makes Player subject to standard closed-pair fidelity
        state.settings.Write().enablePlayerPolygamy = false;
        MARAS_CHECK(state.RecordCheatingIncident(
            ActorId::Player(), spouseA, paramour, true, AffairType::Casual, AffairStatus::Ongoing, AffairStage::Physical, 8.0f));
        MARAS_CHECK(state.IsActorCheating(ActorId::Player()));

        // 4. Opening a marriage marks ongoing affairs Allowed & blocks new cheating records; closing breaks side Lovers
        MARAS_REQUIRE(state.RecordCheatingIncident(
            npcA, npcB, paramour, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 9.0f));
        (void)state.TransitionRelationship(npcA, paramour, RelationshipStatus::Lover, 9.0f);
        MARAS_CHECK(state.IsActorCheating(npcA));

        (void)state.SetPairOpen(npcA, npcB, /*isOpen=*/true, 10.0f);
        const auto* allowedRec = state.affairs->FindTriple(npcA, npcB, paramour);
        MARAS_REQUIRE(allowedRec != nullptr);
        MARAS_CHECK_EQ(allowedRec->affairStatus, AffairStatus::Allowed);
        MARAS_CHECK(!allowedRec->isSecret);
        MARAS_CHECK(!state.IsActorCheating(npcA));
        MARAS_CHECK(!state.RecordCheatingIncident(
            npcA, npcB, paramour, true, AffairType::Casual, AffairStatus::Ongoing, AffairStage::Physical, 11.0f));

        // Closing (npcA, npcB) breaks side Lover bond (npcA, paramour) -> ExLover!
        (void)state.SetPairOpen(npcA, npcB, /*isOpen=*/false, 12.0f);
        MARAS_CHECK_EQ(state.relationships->GetStatus(npcA, paramour), RelationshipStatus::ExLover);
    }

    MARAS_TEST(AffairLedger, PerspectiveBoundSecretVisibilityAndChildSelfBlindness) {
        const ActorId spouseA{ 0x0001A694u };
        const ActorId paramour{ 0x00020545u };
        const ActorId bystander{ 0x00040099u };
        const ChildId secretChildId{ 0x00050077u };

        WorldState state{};
        (void)state.PromoteToMarried(ActorId::Player(), spouseA, 2.0f);
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouseA, ActorId::Player(), paramour, /*isSecret=*/true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 6.0f));

        // 1. Secret affair observer checks
        const auto* secretAffair = state.affairs->FindTriple(spouseA, ActorId::Player(), paramour);
        MARAS_REQUIRE(secretAffair != nullptr);
        MARAS_CHECK(secretAffair->CanObserverKnow(spouseA));
        MARAS_CHECK(secretAffair->CanObserverKnow(paramour));
        MARAS_CHECK(!secretAffair->CanObserverKnow(ActorId::Player()));
        MARAS_CHECK(!secretAffair->CanObserverKnow(bystander));

        // Expose & resolve -> public knowledge
        MARAS_CHECK_EQ(
            state.affairs.Write().ExposeAndResolve(spouseA, paramour, ActorId::Player(), AffairResolution::Confronted),
            std::size_t{ 1 });
        const auto* exposedAffair = state.affairs->FindTriple(spouseA, ActorId::Player(), paramour);
        MARAS_REQUIRE(exposedAffair != nullptr);
        MARAS_CHECK_EQ(exposedAffair->affairStatus, AffairStatus::EndedConfronted);
        MARAS_CHECK(exposedAffair->CanObserverKnow(ActorId::Player()));
        MARAS_CHECK(exposedAffair->CanObserverKnow(bystander));

        // 2. Paternity & pregnancy secret observer checks (including child self-blindness!)
        PaternitySecretState pat{};
        pat.isSecret = true;
        pat.isSecretForPartner = true;
        pat.isSecretForBioFather = true;
        pat.isPregnancySecretForPartner = true;

        // Unborn secret pregnancy: ONLY bioMother knows while hidden from both partner and bioFather
        MARAS_CHECK(pat.CanObserverKnowPregnancy(ChildStage::Unborn, spouseA, paramour, spouseA));
        MARAS_CHECK(!pat.CanObserverKnowPregnancy(ChildStage::Unborn, spouseA, paramour, ActorId::Player()));
        MARAS_CHECK(!pat.CanObserverKnowPregnancy(ChildStage::Unborn, spouseA, paramour, paramour));
        MARAS_CHECK(!pat.CanObserverKnowPregnancy(ChildStage::Unborn, spouseA, paramour, bystander));

        // BioFather discovers, child is born, deceived partner (Player) still unaware
        pat.discoveredByBioFather = true;
        pat.isPregnancySecretForPartner = false;

        MARAS_CHECK(pat.CanObserverKnowTrueBioFather(
            secretChildId, spouseA, paramour, ActorId::Player(), ActorId::Player(), spouseA));
        MARAS_CHECK(pat.CanObserverKnowTrueBioFather(
            secretChildId, spouseA, paramour, ActorId::Player(), ActorId::Player(), paramour));
        MARAS_CHECK(!pat.CanObserverKnowTrueBioFather(
            secretChildId, spouseA, paramour, ActorId::Player(), ActorId::Player(), ActorId::Player()));
        // Child itself NEVER magically knows its own hidden paternity secret!
        MARAS_CHECK(!pat.CanObserverKnowTrueBioFather(
            secretChildId, spouseA, paramour, ActorId::Player(), ActorId::Player(), secretChildId.AsActorId()));
    }

    MARAS_TEST(AffairLedger, ThreeParticipantLifecycleClosureInConstantTime) {
        const ActorId spouseA{ 0x0001A694u };
        const ActorId paramour1{ 0x00020545u };
        const ActorId paramour2{ 0x00020546u };
        const ActorId cheaterNpc{ 0x00030001u };
        const ActorId victimNpc{ 0x00030002u };
        const ActorId paramour3{ 0x00030003u };

        WorldState state{};
        (void)state.PromoteToMarried(ActorId::Player(), spouseA, 1.0f);
        (void)state.PromoteToMarried(cheaterNpc, victimNpc, 1.0f);

        // Audit Bug #6 Case A: Paramour dies -> ongoing affair closes in O(1) via byParamour_ index!
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouseA, ActorId::Player(), paramour1, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 5.0f));
        MARAS_CHECK(state.IsActorCheating(spouseA));

        (void)state.RecordActorDeath(paramour1, 6.0f);
        const auto* afterParamourDeath = state.affairs->FindTriple(spouseA, ActorId::Player(), paramour1);
        MARAS_REQUIRE(afterParamourDeath != nullptr);
        MARAS_CHECK_EQ(afterParamourDeath->affairStatus, AffairStatus::EndedBreakup);
        MARAS_CHECK(!state.IsActorCheating(spouseA));

        // Case B: Cheater and paramour break up (ExLover) -> closes ongoing affair & clears Confess intent
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouseA, ActorId::Player(), paramour2, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 8.0f));
        (void)state.TransitionRelationship(spouseA, paramour2, RelationshipStatus::Lover, 8.0f);
        state.relationships.Write().SetPendingIntent(
            spouseA,
            PendingPartnerIntentRecord{
                .type = PartnerIntentType::ConfessPhysical,
                .createdGameDay = 8.5f,
                .relatedLover = paramour2,
            });

        (void)state.BreakupPair(spouseA, paramour2, RelationshipStatus::ExLover, 9.0f, spouseA);
        const auto* afterParamourBreakup = state.affairs->FindTriple(spouseA, ActorId::Player(), paramour2);
        MARAS_REQUIRE(afterParamourBreakup != nullptr);
        MARAS_CHECK_EQ(afterParamourBreakup->affairStatus, AffairStatus::EndedBreakup);
        MARAS_CHECK_EQ(state.relationships->GetPendingIntent(spouseA).type, PartnerIntentType::None);

        // Case C: Deceived partner dies -> ongoing affair closes as EndedVictimSplit
        MARAS_REQUIRE(state.RecordCheatingIncident(
            cheaterNpc, victimNpc, paramour3, true, AffairType::Casual, AffairStatus::Ongoing, AffairStage::Physical, 10.0f));
        (void)state.RecordActorDeath(victimNpc, 11.0f);
        const auto* afterVictimDeath = state.affairs->FindTriple(cheaterNpc, victimNpc, paramour3);
        MARAS_REQUIRE(afterVictimDeath != nullptr);
        MARAS_CHECK_EQ(afterVictimDeath->affairStatus, AffairStatus::EndedVictimSplit);
        MARAS_CHECK(!state.IsActorCheating(cheaterNpc));
    }

}  // namespace MARAS::Tests
