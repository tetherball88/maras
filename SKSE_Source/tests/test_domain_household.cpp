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

    MARAS_TEST(DomainHousehold, TravelSelectNextDestinationAvoidsRepeatAndHandlesEmptyPools) {
        struct FixedZeroRng final : public IRng {
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        } rng{};

        // Empty both -> std::nullopt
        MARAS_CHECK(!SelectNextTravelDestination({}, {}, RefId{}, rng).has_value());

        const RefId innA{ 0x00037001u };
        const RefId innB{ 0x00037002u };
        const TravelDestinationCandidate dInnA{ innA, LocationId{ 0x00037101u }, CellId{ 0x00037201u }, "Bannered Mare", false };
        const TravelDestinationCandidate dInnB{ innB, LocationId{ 0x00037102u }, CellId{ 0x00037202u }, "Sleeping Giant", false };

        // Empty family pool falls back cleanly to inn pool and filters out previousMarkerId
        const auto picked = SelectNextTravelDestination({}, { dInnA, dInnB }, innA, rng);
        MARAS_REQUIRE(picked.has_value());
        MARAS_CHECK_EQ(picked->targetMarkerId, innB);

        // Sole destination still selected even when it equals previousMarkerId
        const auto solePicked = SelectNextTravelDestination({}, { dInnA }, innA, rng);
        MARAS_REQUIRE(solePicked.has_value());
        MARAS_CHECK_EQ(solePicked->targetMarkerId, innA);

        // Empty inn pool falls back to family pool (even if rng roll > kFamilyVisitChancePercent)
        struct HighFloatRng final : public IRng {
            float NextFloat(float, float maxVal = 1.0f) override { return maxVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        } highRng{};
        const TravelDestinationCandidate dFam{ RefId{ 0x00037003u }, LocationId{}, CellId{}, "Ysolda", true };
        const auto famFallback = SelectNextTravelDestination({ dFam }, {}, RefId{}, highRng);
        MARAS_REQUIRE(famFallback.has_value());
        MARAS_CHECK(famFallback->isFamilyVisit);
    }

    MARAS_TEST(DomainHousehold, TravelEvaluateTickUnregistersInvalidAdultsAndHandlesArrivalTimeout) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};

        const ChildId invalidNonAdult{ 0x00038001u };
        const ChildId invalidSynthetic{ 0xFF700001u };
        const ChildId invalidHoused{ 0x00038002u };
        const ChildId invalidMarriedPlayer{ 0x00038003u };
        const ChildId validLocArrival{ 0x00038004u };
        const ChildId validPosArrival{ 0x00038005u };
        const ChildId validTimeout{ 0x00038006u };

        auto addChild = [&](ChildId cid, ChildStage st) {
            ChildRecord r{};
            r.childId = cid;
            r.stage = st;
            ws.lineage.Write().UpsertChild(std::move(r));
        };
        addChild(invalidNonAdult, ChildStage::Child);
        addChild(invalidSynthetic, ChildStage::Adult);
        addChild(invalidHoused, ChildStage::Adult);
        addChild(invalidMarriedPlayer, ChildStage::Adult);
        addChild(validLocArrival, ChildStage::Adult);
        addChild(validPosArrival, ChildStage::Adult);
        addChild(validTimeout, ChildStage::Adult);

        const HouseId house{ 0x00038100u };
        (void)ApplyRegisterHouse(ws, house, RefId{ 0x00038101u }, true);
        (void)ApplyRegisterTenant(ws, invalidHoused.raw(), house, true, true);
        (void)ws.TransitionRelationship(ActorId{ invalidMarriedPlayer.raw() }, ActorId::Player(), RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);

        for (ChildId cid : { invalidNonAdult, invalidSynthetic, invalidHoused, invalidMarriedPlayer }) {
            ws.household.Write().UpsertAdultChildTravel(cid, AdultChildTravelRecord{
                .targetMarkerId = RefId{ 0x00038201u },
                .assignedGameDay = 1.0f,
                .arrivedGameDay = -1.0f,
                .stayDurationDays = 3.0f
            });
        }

        // validLocArrival has assignedGameDay=0 (timeout elapsed), but arrives NOW -> must NOT rotate on arrival tick
        ws.household.Write().UpsertAdultChildTravel(validLocArrival, AdultChildTravelRecord{
            .targetMarkerId = RefId{ 0x00038202u },
            .targetLocationId = LocationId{ 0x00038302u },
            .assignedGameDay = 0.0f,
            .arrivedGameDay = -1.0f,
            .stayDurationDays = 3.0f
        });
        // validPosArrival arrives via distance <= 300 units in same cell
        const RefId markerPos{ 0x00038203u };
        ws.household.Write().UpsertAdultChildTravel(validPosArrival, AdultChildTravelRecord{
            .targetMarkerId = markerPos,
            .assignedGameDay = 15.0f,
            .arrivedGameDay = -1.0f,
            .stayDurationDays = 3.0f
        });
        // validTimeout has never arrived for >= 10 days -> needs rotation
        ws.household.Write().UpsertAdultChildTravel(validTimeout, AdultChildTravelRecord{
            .targetMarkerId = RefId{ 0x00038204u },
            .assignedGameDay = 5.0f,
            .arrivedGameDay = -1.0f,
            .stayDurationDays = 3.0f
        });

        SpatialActorSnapshot spatial{};
        spatial.Clear();
        spatial.SetMetadata(20.0f, 1u);
        spatial.AddSample(LoadedActorSample{
            .refId = RefId{ validLocArrival.raw() },
            .currentLocationId = LocationId{ 0x00038302u }
        });
        spatial.AddSample(LoadedActorSample{
            .refId = RefId{ validPosArrival.raw() },
            .parentCellId = CellId{ 0x00038400u },
            .x = 100.0f, .y = 100.0f, .z = 0.0f
        });
        spatial.AddSample(LoadedActorSample{
            .refId = markerPos,
            .parentCellId = CellId{ 0x00038400u },
            .x = 250.0f, .y = 100.0f, .z = 0.0f  // dist = 150 <= 300
        });
        spatial.FinalizeSortAndDedupe();

        const auto tick1 = EvaluateTravelTick(ws, 20.0f, spatial);
        MARAS_CHECK_EQ(tick1.unregisteredInvalidAdults.size(), std::size_t{ 4 });
        MARAS_CHECK_EQ(tick1.arrivedChildren.size(), std::size_t{ 2 });
        MARAS_REQUIRE_EQ(tick1.childrenNeedingRotation.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(tick1.childrenNeedingRotation[0], validTimeout);

        // Next tick after stayDurationDays (3.0) elapses for validLocArrival & validPosArrival -> both rotate
        const auto tick2 = EvaluateTravelTick(ws, 24.0f, spatial);
        MARAS_CHECK_EQ(tick2.childrenNeedingRotation.size(), std::size_t{ 3 });
    }

    MARAS_TEST(DomainHousehold, DeceasedQueriesMaskSecretAffairsAndUndiscoveredPaternity) {
        WorldState ws{};
        const ActorId camilla{ 0x00039001u };
        const BaseId camillaBase{ 0x00039101u };
        const ActorId sven{ 0x00039002u };
        const BaseId svenBase{ 0x00039102u };
        const ActorId faendal{ 0x00039003u };
        const BaseId faendalBase{ 0x00039103u };

        DeceasedActorRecord sp{};
        sp.refId = camilla.AsRefId();
        sp.baseId = camillaBase;
        sp.deathTimestamp = 10.0f;
        sp.partnersAtDeath.push_back(DeceasedPartnerEntry{
            .partnerBaseId = svenBase,
            .partnerRefId = sven.AsRefId(),
            .partnerName = "Sven",
            .statusAtDeath = RelationshipStatus::Married
        });
        // Undiscovered secret affair with Faendal: publicStatusAtDeath = Unknown
        sp.partnersAtDeath.push_back(DeceasedPartnerEntry{
            .partnerBaseId = faendalBase,
            .partnerRefId = faendal.AsRefId(),
            .partnerName = "Faendal",
            .statusAtDeath = RelationshipStatus::Lover,
            .isSecretAffair = true,
            .publicStatusAtDeath = RelationshipStatus::Unknown
        });
        // Secret affair with former ex where publicStatusAtDeath = ExDating
        const ActorId exSp{ 0x00039004u };
        const BaseId exBase{ 0x00039104u };
        sp.partnersAtDeath.push_back(DeceasedPartnerEntry{
            .partnerBaseId = exBase,
            .partnerRefId = exSp.AsRefId(),
            .partnerName = "Ex",
            .statusAtDeath = RelationshipStatus::Lover,
            .isSecretAffair = true,
            .publicStatusAtDeath = RelationshipStatus::ExDating
        });
        ws.deceased.Write().UpsertRecord(std::move(sp));

        // Deceased child with undiscovered paternity secret pointing publicly to Player
        const ChildId deadChild{ 0x00039201u };
        DeceasedActorRecord dc{};
        dc.refId = RefId{ deadChild.raw() };
        dc.baseId = BaseId{ deadChild.raw() };
        dc.isChild = true;
        dc.deathTimestamp = 12.0f;
        dc.childInfo = DeceasedChildMetadata{
            .bioMotherBaseId = camillaBase,
            .bioFatherBaseId = faendalBase,
            .publicBioFatherBaseId = BaseId{ kPlayerBaseRawId },
            .hasUndiscoveredPaternitySecret = true
        };
        ws.deceased.Write().UpsertRecord(std::move(dc));

        const auto snap = ws.Snapshot();
        MARAS_CHECK(WasMarriedTo(snap, camilla, sven, camillaBase, svenBase));
        // Faendal is masked completely (Unknown public status)
        MARAS_CHECK(GetDeceasedPartnersOf(snap, faendal, faendalBase).empty());
        // Ex is seen only as Ex (WasExOf true), not active partner
        MARAS_CHECK(WasExOf(snap, camilla, exSp, camillaBase, exBase));
        MARAS_CHECK(GetDeceasedPartnersOf(snap, exSp, exBase).empty());

        // Child paternity masks true bioFather (faendal) and exposes publicBioFather (Player)
        MARAS_CHECK(GetDeceasedChildrenOf(snap, faendal, faendalBase).empty());
        const auto playerDeadChildren = GetDeceasedChildrenOf(snap, ActorId::Player());
        MARAS_REQUIRE_EQ(playerDeadChildren.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(playerDeadChildren[0], deadChild);
        // UnknownFather must never match
        MARAS_CHECK(GetDeceasedChildrenOf(snap, ActorId::UnknownFather()).empty());
    }

    MARAS_TEST(DomainHousehold, HouseholdRelocationAndDivorceTenantEviction) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        const HouseId breezehome{ 0x00039501u };
        const ActorId spouseA{ 0x00039601u };
        const ActorId spouseB{ 0x00039602u };
        const ChildId child{ 0x00039701u };

        ws.settings.Write().enablePlayerPolygamy = true;
        ws.settings.Write().childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::AlwaysWithPlayer;
        (void)ApplyRegisterHouse(ws, breezehome, RefId{ 0x00039502u }, true);
        (void)ws.TransitionRelationship(ActorId::Player(), spouseA, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        (void)ws.TransitionRelationship(ActorId::Player(), spouseB, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        (void)ApplyRegisterTenant(ws, spouseA.raw(), breezehome, true, false);
        (void)ApplyRegisterTenant(ws, spouseB.raw(), breezehome, true, false);
        (void)ApplyRegisterTenant(ws, child.raw(), breezehome, true, true);

        ChildRecord crec{};
        crec.childId = child;
        crec.stage = ChildStage::Child;
        crec.guardians.Assign(ActorId::Player(), spouseA);
        ws.lineage.Write().UpsertChild(std::move(crec));

        // Divorcing spouseA removes spouseA from tenantOrder & clears child guardian if policy demands,
        // while keeping Player-custodied child in breezehome
        (void)ws.BreakupPair(ActorId::Player(), spouseA, RelationshipStatus::Divorced, 10.0f, ActorId::Player(), meta);
        (void)ApplyRemoveTenant(ws, spouseA.raw());
        MARAS_CHECK(ws.household.Read().GetTenantHouse(spouseA.raw()).IsNull());
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(spouseB.raw()), breezehome);
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(child.raw()), breezehome);
    }

}  // namespace MARAS::Tests
