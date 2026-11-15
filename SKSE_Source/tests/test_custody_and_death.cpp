#include "TestHarness.h"

#include "maras/domain/household/DeceasedQueries.h"
#include "maras/foundation/IRng.h"
#include "maras/state/WorldState.h"

#include <cstdint>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
    }

    MARAS_TEST(CustodyAndDeath, GuardianSlotsNeutralityAndSlotPreservation) {
        const ActorId parentA{ 0x00010001u };
        const ActorId parentB{ 0x00010002u };
        const ActorId parentC{ 0x00010003u };

        GuardianSlots slotsAB{};
        const GuardianSetDiff initDiff = slotsAB.Assign(parentA, parentB);
        MARAS_CHECK(initDiff.changed);
        MARAS_CHECK_EQ(slotsAB.Count(), 2);
        MARAS_CHECK(slotsAB.HasGuardian(parentA));
        MARAS_CHECK(slotsAB.HasGuardian(parentB));

        GuardianSlots slotsBA{};
        (void)slotsBA.Assign(parentB, parentA);

        // Strict slot neutrality: (A, B) == (B, A)
        MARAS_CHECK(slotsAB == slotsBA);
        MARAS_CHECK(slotsAB.HasSameSet(parentB, parentA));
        MARAS_CHECK_EQ(slotsAB.OtherGuardian(parentA), parentB);
        MARAS_CHECK_EQ(slotsAB.OtherGuardian(parentB), parentA);

        // Re-assigning the same guardian set in reversed argument order is a no-op and preserves existing slots
        const GuardianSetDiff reversedNoOp = slotsAB.Assign(parentB, parentA);
        MARAS_CHECK(!reversedNoOp.changed);
        MARAS_CHECK_EQ(slotsAB.slotA, parentA);
        MARAS_CHECK_EQ(slotsAB.slotB, parentB);

        // Replacing parentA with parentC preserves surviving parentB in slotB
        const GuardianSetDiff replaceDiff = slotsAB.Assign(parentC, parentB);
        MARAS_CHECK(replaceDiff.changed);
        MARAS_CHECK_EQ(replaceDiff.PrimaryRemoved(), parentA);
        MARAS_CHECK_EQ(replaceDiff.PrimaryAdded(), parentC);
        MARAS_CHECK_EQ(slotsAB.slotA, parentC);
        MARAS_CHECK_EQ(slotsAB.slotB, parentB);

        // Removing slotA leaves surviving parentB in slotB; (0, B) == (B, 0)
        const GuardianSetDiff removeDiff = slotsAB.Remove(parentC);
        MARAS_CHECK(removeDiff.changed);
        MARAS_CHECK_EQ(slotsAB.slotA, ActorId{});
        MARAS_CHECK_EQ(slotsAB.slotB, parentB);
        MARAS_CHECK(slotsAB.IsSingleGuardian());
        MARAS_CHECK(slotsAB.IsSoleGuardian(parentB));
        MARAS_CHECK_EQ(slotsAB.SingleGuardian(), parentB);

        GuardianSlots singleSlotA{ .slotA = parentB, .slotB = ActorId{} };
        MARAS_CHECK(slotsAB == singleSlotA);

        // Duplicate actor in both arguments collapses cleanly to single guardian
        GuardianSlots dupSlots{};
        (void)dupSlots.Assign(parentA, parentA);
        MARAS_CHECK_EQ(dupSlots.Count(), 1);
        MARAS_CHECK(dupSlots.IsSoleGuardian(parentA));
    }

    MARAS_TEST(CustodyAndDeath, ResolveSingleCustodianPoliciesAndSlotNeutrality) {
        const ActorId motherNpc{ 0x00020001u };
        const ActorId fatherNpc{ 0x00020002u };
        const ActorId stepParent{ 0x00020003u };
        const ActorId bioFatherParamour{ 0x00020004u };
        const ChildId childId{ 0x00050010u };

        MockFormIdResolver meta{};
        meta.sexes[ActorId::Player()] = ActorSex::Male;
        meta.sexes[motherNpc] = ActorSex::Female;
        meta.sexes[fatherNpc] = ActorSex::Male;
        meta.sexes[stepParent] = ActorSex::Female;
        DeterministicRng rng{ 12345u };

        WorldState state{};

        // 1. Biological parent priority beats adoptive/step parent regardless of parameter order
        {
            ChildRecord child{};
            child.childId = childId;
            child.isAdopted = false;
            child.bioFather = fatherNpc;
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, child, fatherNpc, stepParent, meta, rng), fatherNpc);
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, child, stepParent, fatherNpc, meta, rng), fatherNpc);
        }

        // 2. Active secret presumedFather is treated as bio-parent so split custody never leaks secret infidelity
        {
            ChildRecord secretChild{};
            secretChild.childId = childId;
            secretChild.isAdopted = false;
            secretChild.bioMother = motherNpc;
            secretChild.bioFather = bioFatherParamour;
            secretChild.presumedFather = ActorId::Player();
            secretChild.paternitySecret.isSecret = true;
            secretChild.paternitySecret.isSecretForPartner = true;

            state.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::AlwaysWithPlayer;
            MARAS_CHECK_EQ(
                state.ResolveSingleCustodian(childId, secretChild, motherNpc, ActorId::Player(), meta, rng),
                ActorId::Player());

            // Once secret is exposed, motherNpc is sole biological parent in the pair -> wins bio-parent priority
            secretChild.paternitySecret.isSecret = false;
            secretChild.paternitySecret.isSecretForPartner = false;
            secretChild.paternitySecret.discovered = true;
            MARAS_CHECK_EQ(
                state.ResolveSingleCustodian(childId, secretChild, motherNpc, ActorId::Player(), meta, rng),
                motherNpc);
        }

        // 3. Player custody overrides (AlwaysWithPlayer / NeverWithPlayer)
        {
            ChildRecord sharedBio{};
            sharedBio.childId = childId;
            sharedBio.isAdopted = false;
            sharedBio.bioMother = motherNpc;
            sharedBio.bioFather = ActorId::Player();

            state.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::AlwaysWithPlayer;
            MARAS_CHECK_EQ(
                state.ResolveSingleCustodian(childId, sharedBio, motherNpc, ActorId::Player(), meta, rng),
                ActorId::Player());

            state.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::NeverWithPlayer;
            MARAS_CHECK_EQ(
                state.ResolveSingleCustodian(childId, sharedBio, ActorId::Player(), motherNpc, meta, rng),
                motherNpc);
        }

        // 4. Gender policy inspects ActorSex via metadata port — NEVER guardian slot A vs. B!
        {
            ChildRecord adopted{};
            adopted.childId = childId;
            adopted.isAdopted = true;
            state.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::UseGenderCustody;

            state.settings.Write().childCustodyGenderPolicy = ChildCustodyGenderPolicy::StayWithMother;
            // Pass Male first, Female second -> still selects Female motherNpc!
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, adopted, fatherNpc, motherNpc, meta, rng), motherNpc);
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, adopted, motherNpc, fatherNpc, meta, rng), motherNpc);

            state.settings.Write().childCustodyGenderPolicy = ChildCustodyGenderPolicy::StayWithFather;
            // Pass Female first, Male second -> still selects Male fatherNpc!
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, adopted, motherNpc, fatherNpc, meta, rng), fatherNpc);
            MARAS_CHECK_EQ(state.ResolveSingleCustodian(childId, adopted, fatherNpc, motherNpc, meta, rng), fatherNpc);
        }

        // 5. Same-sex couple deterministic coin flip via injected IRng
        {
            ChildRecord adoptedSameSex{};
            adoptedSameSex.childId = childId;
            adoptedSameSex.isAdopted = true;

            DeterministicRng rngA{ 0xCAFEBABEu };
            DeterministicRng rngB{ 0xCAFEBABEu };
            const ActorId pickA = state.ResolveSingleCustodian(childId, adoptedSameSex, motherNpc, stepParent, meta, rngA);
            const ActorId pickB = state.ResolveSingleCustodian(childId, adoptedSameSex, motherNpc, stepParent, meta, rngB);
            MARAS_CHECK_EQ(pickA, pickB);
            MARAS_CHECK(pickA == motherNpc || pickA == stepParent);
        }
    }

    MARAS_TEST(CustodyAndDeath, MarriageStepChildLinkingSecretResolutionAndDivorceCleanup) {
        const ActorId spouseA{ 0x0001A694u };
        const ActorId spouseB{ 0x00019DF7u };
        const ChildId stepChild{ 0x00050011u };
        const HouseId whiterunHouse{ 0x000165A2u };
        const CellId whiterunCell{ 0x000165A8u };

        MockFormIdResolver meta{};
        meta.sexes[ActorId::Player()] = ActorSex::Male;
        meta.sexes[spouseA] = ActorSex::Female;
        meta.sexes[spouseB] = ActorSex::Female;
        DeterministicRng rng{ 42u };

        WorldState state{};
        state.household.Write().RegisterHouse(whiterunHouse, RefId{ 0x000165B0u }, true);

        // Child born before marriage with hidden paternity where Player is actually the bioFather
        {
            ChildRecord c{};
            c.childId = stepChild;
            (void)c.guardians.Assign(spouseA, ActorId{});
            c.isAdopted = false;
            c.isBiologicalChild = true;
            c.bioMother = spouseA;
            c.bioFather = ActorId::Player();
            c.presumedFather = ActorId{ 0x00099999u };
            c.stage = ChildStage::Child;
            c.paternitySecret.isSecret = true;
            c.paternitySecret.isSecretForBioFather = true;
            state.lineage.Write().UpsertChild(c);
        }

        state.household.Write().AssignTenant(spouseA.raw(), whiterunHouse, true, false);
        state.household.Write().UpsertSharedCell(
            whiterunCell,
            SharedCellRecord{
                .sharedWithPlayer = true,
                .originalPublicRecorded = true,
                .originalPublicState = false,
                .sharingSpouses = { spouseA },
            });

        // Marry spouseA (rank 0) and spouseB (rank 1)
        (void)state.PromoteToMarried(ActorId::Player(), spouseA, 10.0f, ActorId::Player(), meta);
        (void)state.PromoteToMarried(ActorId::Player(), spouseB, 12.0f, ActorId::Player(), meta);

        // Bio-parents marrying resolves paternitySecret and links (Player, spouseA) into whiterunHouse
        const auto* afterMarry = state.lineage->FindChild(stepChild);
        MARAS_REQUIRE(afterMarry != nullptr);
        MARAS_CHECK(!afterMarry->paternitySecret.HasActiveSecret());
        MARAS_CHECK(afterMarry->presumedFather.IsNull());
        MARAS_CHECK(afterMarry->guardians.HasSameSet(ActorId::Player(), spouseA));
        MARAS_CHECK_EQ(state.household->GetTenantHouse(stepChild.raw()), whiterunHouse);
        MARAS_CHECK_EQ(state.hierarchy->GetRank(spouseA), 0);
        MARAS_CHECK_EQ(state.hierarchy->GetRank(spouseB), 1);

        // Set pending intent on spouseA, then divorce spouseA (StayWithMother -> child leaves Player's house)
        state.relationships.Write().SetPendingIntent(
            spouseA,
            PendingPartnerIntentRecord{ .type = PartnerIntentType::SuggestOpen, .createdGameDay = 14.0f });
        state.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::UseGenderCustody;
        state.settings.Write().childCustodyGenderPolicy = ChildCustodyGenderPolicy::StayWithMother;

        (void)state.PromoteToDivorced(ActorId::Player(), spouseA, 15.0f, ActorId::Player(), meta, &rng);

        // Audit Bug #4 verified: divorced spouseA evicted from house, cell unshared, hierarchy compacted!
        MARAS_CHECK(state.IsDivorced(ActorId::Player(), spouseA));
        MARAS_CHECK(state.household->GetTenantHouse(spouseA.raw()).IsNull());
        MARAS_CHECK(!state.household->IsCellSharedWithPlayer(whiterunCell));
        MARAS_CHECK_EQ(state.relationships->GetPendingIntent(spouseA).type, PartnerIntentType::None);
        MARAS_CHECK_EQ(state.hierarchy->GetRank(spouseA), 4);
        MARAS_CHECK_EQ(state.hierarchy->GetRank(spouseB), 0);

        const auto* afterDivorce = state.lineage->FindChild(stepChild);
        MARAS_REQUIRE(afterDivorce != nullptr);
        MARAS_CHECK(afterDivorce->guardians.IsSoleGuardian(spouseA));
        MARAS_CHECK(state.household->GetTenantHouse(stepChild.raw()).IsNull());
        MARAS_CHECK(state.household->FindChildState(stepChild) == nullptr);
    }

    MARAS_TEST(CustodyAndDeath, SpouseDeathReassignsChildToHighestSurvivingSpouseAndRemovesUnborn) {
        const ActorId spouseA{ 0x0001A694u };
        const ActorId spouseB{ 0x00019DF7u };
        const ChildId unbornChild{ 0xFF700020u };
        const ChildId livingChild{ 0x00050021u };
        const HouseId houseA{ 0x000165A2u };
        const HouseId houseB{ 0x00016BFEu };

        MockFormIdResolver meta{};
        meta.sexes[ActorId::Player()] = ActorSex::Male;
        meta.sexes[spouseA] = ActorSex::Female;
        meta.sexes[spouseB] = ActorSex::Female;
        meta.names[spouseA] = "Ysolda";
        meta.names[spouseB] = "Camilla";

        WorldState state{};
        state.household.Write().RegisterHouse(houseA, RefId{ 0x000165B0u }, true);
        state.household.Write().RegisterHouse(houseB, RefId{ 0x00016BFFu }, true);

        (void)state.PromoteToMarried(ActorId::Player(), spouseA, 5.0f, ActorId::Player(), meta);
        (void)state.PromoteToMarried(ActorId::Player(), spouseB, 8.0f, ActorId::Player(), meta);
        state.household.Write().AssignTenant(spouseA.raw(), houseA, true, false);
        state.household.Write().AssignTenant(spouseB.raw(), houseB, true, false);

        {
            ChildRecord fetus{};
            fetus.childId = unbornChild;
            (void)fetus.guardians.Assign(ActorId::Player(), spouseA);
            fetus.bioMother = spouseA;
            fetus.bioFather = ActorId::Player();
            fetus.stage = ChildStage::Unborn;
            state.lineage.Write().UpsertChild(fetus);

            ChildRecord kid{};
            kid.childId = livingChild;
            (void)kid.guardians.Assign(ActorId::Player(), spouseA);
            kid.bioMother = spouseA;
            kid.bioFather = ActorId::Player();
            kid.stage = ChildStage::Child;
            state.lineage.Write().UpsertChild(kid);
            (void)state.SyncChildHouseholdState(livingChild, houseA);
        }

        MARAS_CHECK_EQ(state.household->GetTenantHouse(livingChild.raw()), houseA);

        // SpouseA dies: unborn fetus dies with mother; livingChild transfers to highest-ranking surviving spouseB & houseB!
        (void)state.RecordActorDeath(spouseA, 22.0f, /*killedByPlayer=*/false, ActorId{}, meta);

        MARAS_CHECK(state.deceased->IsDeceased(spouseA));
        MARAS_CHECK(state.deceased->IsWidowOrWidower(ActorId::Player()));
        MARAS_CHECK(state.lineage->FindChild(unbornChild) == nullptr);

        const auto* reassigned = state.lineage->FindChild(livingChild);
        MARAS_REQUIRE(reassigned != nullptr);
        MARAS_CHECK(reassigned->guardians.HasSameSet(ActorId::Player(), spouseB));
        MARAS_CHECK_EQ(state.household->GetTenantHouse(livingChild.raw()), houseB);
        MARAS_REQUIRE(state.household->FindChildState(livingChild) != nullptr);
        MARAS_CHECK_EQ(state.household->FindChildState(livingChild)->currentHouseId, houseB);
        MARAS_CHECK_EQ(state.hierarchy->GetRank(spouseB), 0);
    }

    MARAS_TEST(CustodyAndDeath, FmrDynamicSiblingIsolationAndStage4AdultChildDeath) {
        const BaseId sharedFmrTemplateBase{ 0x04001111u };
        const ChildId fmrSiblingA{ 0xFF800501u };
        const ChildId fmrSiblingB{ 0xFF800502u };
        const ChildId gradAdultChild{ 0xFF800600u };
        const ActorId gradSpouse{ 0x00040055u };
        const ActorId motherSpouse{ 0x0001A694u };

        MockFormIdResolver meta{};
        meta.baseIds[fmrSiblingA.AsActorId()] = sharedFmrTemplateBase;
        meta.baseIds[fmrSiblingB.AsActorId()] = sharedFmrTemplateBase;
        meta.baseIds[gradSpouse] = BaseId{ 0x00040050u };
        meta.baseIds[motherSpouse] = BaseId{ 0x00013BABu };
        meta.names[fmrSiblingA.AsActorId()] = "FmrTwinA";
        meta.names[fmrSiblingB.AsActorId()] = "FmrTwinB";
        meta.names[gradAdultChild.AsActorId()] = "GraduatedSon";
        meta.names[gradSpouse] = "GradSpouse";
        meta.names[motherSpouse] = "Ysolda";

        WorldState state{};
        (void)state.PromoteToMarried(ActorId::Player(), motherSpouse, 4.0f, ActorId::Player(), meta);

        // 1. Register two FMR dynamic 0xFF siblings sharing the same template BaseId (Audit Bug #5)
        for (const ChildId sibId : { fmrSiblingA, fmrSiblingB }) {
            ChildRecord sib{};
            sib.childId = sibId;
            (void)sib.guardians.Assign(ActorId::Player(), motherSpouse);
            sib.name = meta.GetActorName(sibId.AsActorId());
            sib.isAdopted = false;
            sib.isBiologicalChild = true;
            sib.bioMother = motherSpouse;
            sib.bioFather = ActorId::Player();
            sib.stage = ChildStage::Child;
            state.lineage.Write().UpsertChild(sib);
        }

        // Kill ONLY fmrSiblingA -> fmrSiblingB MUST NOT be marked deceased via shared template BaseId!
        (void)state.RecordActorDeath(fmrSiblingA.AsActorId(), 18.0f, false, ActorId{}, meta);
        MARAS_CHECK(state.deceased->IsDeceased(fmrSiblingA.AsActorId(), sharedFmrTemplateBase));
        MARAS_CHECK(!state.deceased->IsDeceased(fmrSiblingB.AsActorId(), sharedFmrTemplateBase));
        MARAS_CHECK(state.lineage->FindChild(fmrSiblingB) != nullptr);

        // 2. Register a graduated Stage 4 Adult Child who is ALSO married to gradSpouse (Audit Bug #2)
        {
            ChildRecord grad{};
            grad.childId = gradAdultChild;
            (void)grad.guardians.Assign(ActorId::Player(), motherSpouse);
            grad.name = "GraduatedSon";
            grad.isAdopted = false;
            grad.isBiologicalChild = true;
            grad.bioMother = motherSpouse;
            grad.bioFather = ActorId::Player();
            grad.stage = ChildStage::Adult;
            state.lineage.Write().UpsertChild(grad);

            ActorProfileRecord prof{};
            prof.refId = RefId{ gradAdultChild.raw() };
            prof.baseId = BaseId{ gradAdultChild.raw() };
            prof.socialClass = SocialClass::Wealthy;
            prof.skillType = SkillType::Warrior;
            prof.temperament = Temperament::Proud;
            prof.isRegisteredAdult = true;
            state.actors.Write().Upsert(prof);

            state.household.Write().UpsertAdultChildTravel(
                gradAdultChild,
                AdultChildTravelRecord{
                    .targetMarkerId = RefId{ 0x000C0001u },
                    .destinationName = "Solitude",
                    .assignedGameDay = 20.0f,
                });

            (void)state.PromoteToMarried(gradAdultChild.AsActorId(), gradSpouse, 21.0f, gradAdultChild.AsActorId(), meta);
        }

        // Kill Stage 4 Adult Child -> archives BOTH childInfo lineage AND adult partnersAtDeath!
        (void)state.RecordActorDeath(gradAdultChild.AsActorId(), 30.0f, true, ActorId::Player(), meta);

        const DeceasedActorRecord* deadRec = state.deceased->FindByActor(gradAdultChild.AsActorId());
        MARAS_REQUIRE(deadRec != nullptr);
        MARAS_CHECK(!deadRec->isChild);
        MARAS_CHECK(deadRec->wasKilledByPlayer);
        MARAS_REQUIRE(deadRec->childInfo.has_value());
        MARAS_CHECK_EQ(deadRec->childInfo->guardianARefId, RefId::Player());
        MARAS_CHECK_EQ(deadRec->childInfo->guardianBRefId, motherSpouse.AsRefId());
        MARAS_REQUIRE_EQ(deadRec->partnersAtDeath.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(deadRec->partnersAtDeath[0].partnerRefId, gradSpouse.AsRefId());
        MARAS_CHECK_EQ(deadRec->partnersAtDeath[0].statusAtDeath, RelationshipStatus::Married);

        // Surviving spouse of graduated Stage 4 Adult Child is properly recognized as a widow/widower!
        MARAS_CHECK(state.deceased->IsWidowOrWidower(gradSpouse));
        MARAS_CHECK(state.lineage->FindChild(gradAdultChild) == nullptr);
        MARAS_CHECK(state.household->FindAdultChildTravel(gradAdultChild) == nullptr);
        MARAS_CHECK(state.relationships->FindEdge(gradAdultChild.AsActorId(), gradSpouse) == nullptr);
    }

    MARAS_TEST(CustodyAndDeath, DeceasedSecretAffairsAndPaternityPreserved) {
        const ActorId spouse{ 0x00060010u };
        const ActorId publicHusband{ 0x00060020u };
        const ActorId secretEx{ 0x00060030u };
        const ActorId secretParamour{ 0x00060040u };
        const ActorId secretExParamour{ 0x00060050u };
        const ChildId secretChild{ 0x00070010u };

        WorldState state{};
        MockFormIdResolver meta{};
        meta.names[spouse] = "CheatingSpouse";
        meta.names[publicHusband] = "PublicHusband";
        meta.names[secretEx] = "DivorcedExParamour";
        meta.names[secretParamour] = "SecretParamour";
        meta.names[secretExParamour] = "PureSecretExParamour";
        meta.names[secretChild.AsActorId()] = "SecretChild";
        meta.baseIds[spouse] = BaseId{ 0x00060011u };
        meta.baseIds[publicHusband] = BaseId{ 0x00060021u };
        meta.baseIds[secretEx] = BaseId{ 0x00060031u };
        meta.baseIds[secretParamour] = BaseId{ 0x00060041u };
        meta.baseIds[secretExParamour] = BaseId{ 0x00060051u };
        meta.baseIds[secretChild.AsActorId()] = BaseId{ 0x00070011u };

        (void)state.PromoteToMarried(spouse, publicHusband, 1.0f, spouse, meta);
        (void)state.TransitionRelationship(spouse, secretEx, RelationshipStatus::Divorced, 2.0f, spouse);
        (void)state.TransitionRelationship(spouse, secretEx, RelationshipStatus::Lover, 3.0f, spouse);
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouse, publicHusband, secretEx, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 3.0f));
        (void)state.TransitionRelationship(spouse, secretParamour, RelationshipStatus::Lover, 4.0f, spouse);
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouse, publicHusband, secretParamour, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 4.0f));
        (void)state.TransitionRelationship(spouse, secretExParamour, RelationshipStatus::Lover, 4.2f, spouse);
        MARAS_REQUIRE(state.RecordCheatingIncident(
            spouse, publicHusband, secretExParamour, true,
            AffairType::Lover, AffairStatus::EndedBreakup, AffairStage::Physical, 4.4f));
        (void)state.TransitionRelationship(spouse, secretExParamour, RelationshipStatus::ExLover, 4.5f, spouse);

        ChildRecord child{};
        child.childId = secretChild;
        child.name = "SecretChild";
        child.stage = ChildStage::Child;
        child.isBiologicalChild = true;
        child.isAdopted = false;
        child.bioMother = spouse;
        child.bioFather = secretParamour;
        child.presumedFather = publicHusband;
        (void)child.guardians.Assign(spouse, publicHusband);
        child.paternitySecret.isSecret = true;
        child.paternitySecret.isSecretForPartner = true;
        state.lineage.Write().UpsertChild(child);

        (void)state.RecordActorDeath(secretChild.AsActorId(), 5.0f, false, ActorId{}, meta);
        const DeceasedActorRecord* deadChild = state.deceased->FindByActor(secretChild.AsActorId());
        MARAS_REQUIRE(deadChild != nullptr);
        MARAS_REQUIRE(deadChild->childInfo.has_value());
        MARAS_CHECK(deadChild->childInfo->hasUndiscoveredPaternitySecret);
        MARAS_CHECK_EQ(deadChild->childInfo->bioFatherBaseId, BaseId{ 0x00060041u });
        MARAS_CHECK_EQ(deadChild->childInfo->publicBioFatherBaseId, BaseId{ 0x00060021u });

        (void)state.RecordActorDeath(spouse, 6.0f, false, ActorId{}, meta);
        const DeceasedActorRecord* deadSpouse = state.deceased->FindByActor(spouse);
        MARAS_REQUIRE(deadSpouse != nullptr);
        MARAS_REQUIRE_EQ(deadSpouse->partnersAtDeath.size(), std::size_t{ 4 });
        for (const auto& pe : deadSpouse->partnersAtDeath) {
            if (pe.partnerRefId == publicHusband.AsRefId()) {
                MARAS_CHECK(!pe.isSecretAffair);
                MARAS_CHECK_EQ(pe.statusAtDeath, RelationshipStatus::Married);
            } else if (pe.partnerRefId == secretEx.AsRefId()) {
                MARAS_CHECK(pe.isSecretAffair);
                MARAS_CHECK_EQ(pe.statusAtDeath, RelationshipStatus::Lover);
                MARAS_CHECK_EQ(pe.publicStatusAtDeath, RelationshipStatus::Divorced);
            } else if (pe.partnerRefId == secretParamour.AsRefId()) {
                MARAS_CHECK(pe.isSecretAffair);
                MARAS_CHECK_EQ(pe.statusAtDeath, RelationshipStatus::Lover);
                MARAS_CHECK_EQ(pe.publicStatusAtDeath, RelationshipStatus::Unknown);
            } else if (pe.partnerRefId == secretExParamour.AsRefId()) {
                MARAS_CHECK(pe.isSecretAffair);
                MARAS_CHECK_EQ(pe.statusAtDeath, RelationshipStatus::ExLover);
                MARAS_CHECK_EQ(pe.publicStatusAtDeath, RelationshipStatus::Unknown);
            }
        }

        // Domain::Household::DeceasedQueries public queries respect deceased secrecy metadata
        const WorldStateSnapshot snap = state.Snapshot();
        MARAS_CHECK(Domain::Household::WasExOf(
            snap, spouse, secretEx, meta.baseIds[spouse], meta.baseIds[secretEx], secretEx.AsRefId()));
        MARAS_CHECK(!Domain::Household::WasExOf(
            snap, spouse, secretExParamour, meta.baseIds[spouse], meta.baseIds[secretExParamour], secretExParamour.AsRefId()));
        MARAS_CHECK(!Domain::Household::WasExOf(
            snap, spouse, secretParamour, meta.baseIds[spouse], meta.baseIds[secretParamour], secretParamour.AsRefId()));
        MARAS_CHECK(Domain::Household::GetDeceasedPartnersOf(
            snap, secretParamour, meta.baseIds[secretParamour], secretParamour.AsRefId()).empty());
        MARAS_CHECK_EQ(
            Domain::Household::GetDeceasedPartnersOf(
                snap, publicHusband, meta.baseIds[publicHusband], publicHusband.AsRefId()).size(),
            std::size_t{ 1 });
        MARAS_CHECK(Domain::Household::GetDeceasedChildrenOf(
            snap, secretParamour, meta.baseIds[secretParamour], secretParamour.AsRefId()).empty());
        MARAS_CHECK_EQ(
            Domain::Household::GetDeceasedChildrenOf(
                snap, publicHusband, meta.baseIds[publicHusband], publicHusband.AsRefId()).size(),
            std::size_t{ 1 });
    }

    MARAS_TEST(CustodyAndDeath, DeathAndUnregisterSharedCellSceneMarkersAndBatchCleanup) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};

        const ActorId widowSpouse{ 0x000A1001u };
        const ActorId killedSpouse{ 0x000A1002u };
        const ActorId unregNpc{ 0x000A1003u };
        const CellId widowCell{ 0x000A2001u };
        const CellId killedCell{ 0x000A2002u };
        const CellId unregCell{ 0x000A2003u };
        const RefId marker1{ 0xFF00E001u };
        const RefId marker2{ 0xFF00E002u };
        const RefId marker3{ 0xFF00E003u };

        ws.settings.Write().enablePlayerPolygamy = true;
        for (ActorId id : { widowSpouse, killedSpouse, unregNpc }) {
            ActorProfileRecord rec{};
            rec.refId = id.AsRefId();
            rec.baseId = BaseId{ id.raw() + 0x100u };
            rec.isRegisteredAdult = true;
            ws.actors.Write().Upsert(rec);
        }
        (void)ws.PromoteToMarried(ActorId::Player(), widowSpouse, 1.0f, ActorId::Player(), meta);
        (void)ws.PromoteToMarried(ActorId::Player(), killedSpouse, 1.0f, ActorId::Player(), meta);

        auto addSharedCell = [&](CellId cell, ActorId owner) {
            SharedCellRecord cr{};
            cr.sharedWithPlayer = true;
            cr.originalPublicRecorded = true;
            cr.originalPublicState = false;
            cr.sharingSpouses.insert(owner);
            ws.household.Write().UpsertSharedCell(cell, cr);
        };
        addSharedCell(widowCell, widowSpouse);
        addSharedCell(killedCell, killedSpouse);
        addSharedCell(unregCell, unregNpc);

        {
            auto& cd = ws.cooldowns.Write();
            cd.movedSceneActors[widowSpouse] = marker1;
            cd.movedSceneActors[killedSpouse] = marker2;
            cd.movedSceneActors[unregNpc] = marker3;
            cd.skyrimNetMutedActors = { widowSpouse, killedSpouse, unregNpc };
        }
        {
            auto& lin = ws.lineage.Write();
            lin.UpsertActiveDeliveryBatch(widowSpouse, ActiveDeliveryBatch{ 1, 5.0f, {} });
            lin.UpsertActiveDeliveryBatch(killedSpouse, ActiveDeliveryBatch{ 1, 5.0f, {} });
            lin.UpsertActiveDeliveryBatch(unregNpc, ActiveDeliveryBatch{ 1, 5.0f, {} });
        }

        // 1. Widow spouse death (killedByPlayer=false) preserves shared cell, retires scene marker & batch
        const auto dWidow = ws.RecordActorDeath(widowSpouse, 10.0f, /*killedByPlayer=*/false, ActorId{}, meta);
        MARAS_CHECK(dWidow.stoppedSharingSpouses.empty());
        MARAS_CHECK(ws.household->IsCellSharedWithPlayer(widowCell));
        MARAS_REQUIRE_EQ(dWidow.retiredSceneMarkers.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(dWidow.retiredSceneMarkers[0], marker1);
        MARAS_CHECK(ws.lineage->FindActiveDeliveryBatch(widowSpouse) == nullptr);
        MARAS_CHECK(!ws.cooldowns->movedSceneActors.contains(widowSpouse));

        // 2. Player-killed spouse unshares shared cell + emits cell/spouse deltas & retires scene marker
        const auto dKilled = ws.RecordActorDeath(
            killedSpouse, 11.0f, /*killedByPlayer=*/true, ActorId::Player(), meta);
        MARAS_REQUIRE_EQ(dKilled.stoppedSharingSpouses.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(dKilled.stoppedSharingSpouses[0], killedSpouse);
        MARAS_REQUIRE_EQ(dKilled.restoredSharedCells.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(dKilled.restoredSharedCells[0].cellId, killedCell);
        MARAS_CHECK(!ws.household->IsCellSharedWithPlayer(killedCell));
        MARAS_REQUIRE_EQ(dKilled.retiredSceneMarkers.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(dKilled.retiredSceneMarkers[0], marker2);
        MARAS_CHECK(ws.lineage->FindActiveDeliveryBatch(killedSpouse) == nullptr);

        // 3. UnregisterActor unshares cell, retires marker, clears cooldowns & delivery batch
        const auto uRes = ws.UnregisterActor(unregNpc);
        MARAS_REQUIRE_EQ(uRes.stoppedSharingSpouses.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(uRes.stoppedSharingSpouses[0], unregNpc);
        MARAS_REQUIRE_EQ(uRes.restoredSharedCells.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(uRes.restoredSharedCells[0].cellId, unregCell);
        MARAS_CHECK(!ws.household->IsCellSharedWithPlayer(unregCell));
        MARAS_REQUIRE_EQ(uRes.retiredSceneMarkers.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(uRes.retiredSceneMarkers[0], marker3);
        MARAS_CHECK(ws.cooldowns->movedSceneActors.empty());
        MARAS_CHECK(ws.cooldowns->skyrimNetMutedActors.empty());
        MARAS_CHECK(ws.lineage->FindActiveDeliveryBatch(unregNpc) == nullptr);
    }

}  // namespace MARAS::Tests
