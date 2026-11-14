#include "TestHarness.h"

#include "maras/domain/household/DeceasedQueries.h"
#include "maras/domain/household/HouseholdRules.h"
#include "maras/domain/household/SpouseAssetRules.h"
#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/domain/household/TravelRules.h"
#include "maras/foundation/IRng.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <cmath>
#include <vector>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
        using namespace MARAS::Domain::Household;
        using MARAS::Domain::Relationships::BonusCatalog;
        using MARAS::Domain::Relationships::BonusEntry;
        using MARAS::Projection::PerspectiveView;
    }  // namespace

    MARAS_TEST(DomainHousehold, HousesTenantsRelocationAndSharedAssets) {
        WorldState ws{};
        const HouseId breezehome{ 0x00030001u };
        const HouseId proudspire{ 0x00030002u };
        const RefId markerA{ 0x00030010u };
        const RefId markerB{ 0x00030020u };
        const ActorId spouse{ 0x00031001u };
        const ChildId sharedChild{ 0x00032001u };
        const ChildId otherChild{ 0x00032002u };

        MARAS_CHECK(ApplyRegisterHouse(ws, breezehome, markerA, true));
        MARAS_CHECK(ApplyRegisterHouse(ws, proudspire, markerB, true));
        MARAS_CHECK(ApplyRegisterTenant(ws, spouse.raw(), breezehome, true, false));
        MARAS_CHECK(ApplyRegisterTenant(ws, sharedChild.raw(), breezehome, true, true));
        MARAS_CHECK(ApplyRegisterTenant(ws, otherChild.raw(), breezehome, true, true));

        ChildRecord scRec{};
        scRec.childId = sharedChild;
        scRec.stage = ChildStage::Child;
        scRec.guardians.Assign(spouse, ActorId::Player());
        ws.lineage.Write().UpsertChild(scRec);

        ChildRecord ocRec{};
        ocRec.childId = otherChild;
        ocRec.stage = ChildStage::Child;
        ocRec.guardians.Assign(ActorId::Player(), ActorId{ 0x00031002u });
        ws.lineage.Write().UpsertChild(ocRec);

        // Player-guarded child cannot be evicted as a standalone tenant
        const auto eligChild = CanRemoveTenant(ws.Snapshot(), sharedChild.raw());
        MARAS_CHECK(eligChild.isChild);
        MARAS_CHECK(!eligChild.eligible);

        const auto eligSpouse = CanRemoveTenant(ws.Snapshot(), spouse.raw());
        MARAS_CHECK(!eligSpouse.isChild);
        MARAS_CHECK(eligSpouse.eligible);

        // Relocating spouse moves sharedChild (guarded by spouse) and skips otherChild
        const auto reloc = ApplyRelocateFamilyUnit(ws, spouse.raw(), proudspire, true);
        MARAS_CHECK(reloc.targetHouseExists);
        MARAS_CHECK_EQ(reloc.movedAdults.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(reloc.movedChildren.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(reloc.movedChildren[0], sharedChild);
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(spouse.raw()), proudspire);
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(sharedChild.raw()), proudspire);
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(otherChild.raw()), breezehome);

        // Shared cells
        const CellId cellA{ 0x00033001u };
        const CellId cellB{ 0x00033002u };
        const auto sharedOut = ApplyShareCellsWithPlayer(ws, spouse, { cellA, cellB });
        MARAS_CHECK(sharedOut.shared);
        MARAS_CHECK_EQ(sharedOut.newlySharedCells.size(), std::size_t{ 2 });
        MARAS_CHECK(IsCellSharedWithPlayer(ws.Snapshot(), cellA));

        const auto stopOut = ApplyStopSharingForSpouse(ws, spouse);
        MARAS_CHECK(stopOut.stopped);
        MARAS_CHECK(!IsCellSharedWithPlayer(ws.Snapshot(), cellA));
    }

    MARAS_TEST(DomainHousehold, DeceasedQueriesAndDynamicActorIsolation) {
        WorldState ws{};
        const ActorId spouse{ 0x00034001u };
        const ActorId dynChildDead{ 0xFF000101u };
        const ActorId dynChildSibling{ 0xFF000102u };
        constexpr BaseId sharedCloneBase{ 0x00039999u };

        DeceasedActorRecord spouseRec{};
        spouseRec.refId = spouse.AsRefId();
        spouseRec.baseId = BaseId{ 0x00034999u };
        spouseRec.deathTimestamp = 12.0f;
        spouseRec.partnersAtDeath.push_back(DeceasedPartnerEntry{
            .partnerBaseId = BaseId{ kPlayerBaseRawId },
            .partnerRefId = ActorId::Player().AsRefId(),
            .partnerName = "Player",
            .statusAtDeath = RelationshipStatus::Married,
            .isPlayer = true
        });
        ws.deceased.Write().UpsertRecord(std::move(spouseRec));

        DeceasedActorRecord deadCloneRec{};
        deadCloneRec.refId = dynChildDead.AsRefId();
        deadCloneRec.baseId = sharedCloneBase;
        deadCloneRec.isChild = true;
        deadCloneRec.deathTimestamp = 15.0f;
        deadCloneRec.childInfo = DeceasedChildMetadata{ .bioMotherBaseId = BaseId{ kPlayerBaseRawId } };
        ws.deceased.Write().UpsertRecord(std::move(deadCloneRec));

        const auto snap = ws.Snapshot();
        MARAS_CHECK(IsDeceased(snap, spouse));
        MARAS_CHECK(IsWidowOrWidower(snap, ActorId::Player()));
        MARAS_CHECK(WasMarriedTo(snap, spouse, ActorId::Player()));
        MARAS_CHECK_EQ(GetDeceasedSpousesOf(snap, ActorId::Player()).size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(GetDeceasedChildrenOf(snap, ActorId::Player()).size(), std::size_t{ 1 });

        // Dynamic 0xFF sibling sharing BaseId must NOT be treated as deceased
        MARAS_CHECK(IsDeceased(snap, dynChildDead, sharedCloneBase));
        MARAS_CHECK(!IsDeceased(snap, dynChildSibling, sharedCloneBase));
    }

    MARAS_TEST(DomainHousehold, AdultChildTravelAndHouseholdBuffEvaluation) {
        WorldState ws{};
        const ActorId mother{ 0x00035001u };
        const ActorId secretBioFather{ 0x00035002u };
        const ChildId adultChild{ 0x00035010u };

        ChildRecord rec{};
        rec.childId = adultChild;
        rec.stage = ChildStage::Adult;
        rec.bioMother = mother;
        rec.bioFather = secretBioFather;
        rec.guardians.Assign(mother, ActorId{});
        rec.paternitySecret.isSecret = true;
        rec.paternitySecret.isSecretForPartner = true;
        rec.paternitySecret.isSecretForBioFather = true;
        ws.lineage.Write().UpsertChild(rec);

        SpatialActorSnapshot spatial{};
        LoadedActorSample momSample{};
        momSample.refId = mother.AsRefId();
        momSample.SetDisplayName("Aela");
        momSample.parentCellId = CellId{ 0x00036001u };
        momSample.currentLocationId = LocationId{ 0x00036101u };

        LoadedActorSample bfSample{};
        bfSample.refId = secretBioFather.AsRefId();
        bfSample.SetDisplayName("Skjor");
        bfSample.parentCellId = CellId{ 0x00036002u };
        bfSample.currentLocationId = LocationId{ 0x00036102u };

        LoadedActorSample childSample{};
        childSample.refId = RefId{ adultChild.raw() };
        childSample.parentCellId = momSample.parentCellId;
        childSample.currentLocationId = momSample.currentLocationId;

        spatial.Clear();
        spatial.SetMetadata(10.0f, 1u);
        spatial.AddSample(momSample);
        spatial.AddSample(bfSample);
        spatial.AddSample(childSample);
        spatial.FinalizeSortAndDedupe();
        const auto spatialPtr = std::make_shared<const SpatialActorSnapshot>(spatial);
        const auto worldSnap = std::make_shared<const WorldStateSnapshot>(ws.Snapshot());
        const auto view = PerspectiveView::ForActor(worldSnap, spatialPtr, ActorId{ adultChild.raw() });

        // Child does not know secret bio father -> only mother is a visit candidate
        const auto familyCands = BuildFamilyVisitCandidates(view, adultChild);
        MARAS_REQUIRE_EQ(familyCands.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(familyCands[0].targetMarkerId, mother.AsRefId());

        DeterministicRng rng{ 42u };
        const auto travelRec = CreateTravelRecord(familyCands[0], 10.0f, false, rng);
        ws.household.Write().UpsertAdultChildTravel(adultChild, travelRec);
        const auto tickOut = EvaluateTravelTick(ws, 10.0f, spatial);
        MARAS_REQUIRE_EQ(tickOut.arrivedChildren.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(tickOut.arrivedChildren[0], adultChild);

        // SpouseBuffRules evaluation
        ActorProfileRecord spouseProf{};
        spouseProf.refId = mother.AsRefId();
        spouseProf.baseId = BaseId{ mother.raw() };
        spouseProf.socialClass = SocialClass::Nobles;
        spouseProf.skillType = SkillType::Ranger;
        spouseProf.isRegisteredAdult = true;
        ws.actors.Write().Upsert(spouseProf);

        RelationshipEdgeRecord edge{};
        edge.pair = CanonicalPairKey(ActorId::Player(), mother);
        edge.status = RelationshipStatus::Married;
        ws.relationships.Write().UpsertEdge(edge);
        ws.affection.Write().SetPermanent(mother, 100);
        ws.hierarchy.Write().SetRank(mother, 0, { mother });

        const float mult = ComputeSpouseMultiplier(ws.Snapshot(), mother);
        MARAS_CHECK(mult > 0.0f);

        BonusCatalog catalog{};
        catalog.SetCategories({
            { "nobles", { BonusEntry{ .name = "SpeechBoost", .description = "", .type = "add", .unit = "%", .value = 10.0f } } },
            { "ranger", { BonusEntry{ .name = "BowBoost", .description = "", .type = "add", .unit = "%", .value = 8.0f } } }
        });
        const auto report = EvaluateHouseholdBuffs(
            ws.Snapshot(), std::array<RefId, 1>{ mother.AsRefId() }, catalog);
        MARAS_CHECK(report.activePermanentCount >= 1);
        MARAS_CHECK(report.activeFollowerCount >= 1);
    }

}  // namespace MARAS::Tests
