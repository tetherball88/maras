#include "TestHarness.h"

#include "maras/domain/household/ChildForcegreetRules.h"
#include "maras/domain/household/DeceasedQueries.h"
#include "maras/domain/household/HouseholdRules.h"
#include "maras/domain/household/SpouseAssetRules.h"
#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/domain/household/TravelRules.h"
#include "maras/foundation/IRng.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <algorithm>
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

    MARAS_TEST(DomainHousehold, SleepPaidAffectionAndRestedBuffPlan) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        const HouseId breezehome{ 0x00039801u };
        const HouseId tavernHouse{ 0x00039899u };
        const ActorId spouse{ 0x00039901u };
        const ActorId lover{ 0x00039902u };
        const ActorId dating{ 0x00039903u };
        const ActorId candidate{ 0x00039904u };
        const ActorId exSpouse{ 0x00039905u };
        const ChildId child{ 0x00039A01u };

        ws.settings.Write().enablePlayerPolygamy = true;
        (void)ApplyRegisterHouse(ws, breezehome, RefId{ 0x00039802u }, true);
        (void)ws.TransitionRelationship(ActorId::Player(), spouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        (void)ws.TransitionRelationship(ActorId::Player(), lover, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta);
        (void)ws.TransitionRelationship(ActorId::Player(), dating, RelationshipStatus::Dating, 1.0f, ActorId::Player(), meta);
        (void)ws.TransitionRelationship(ActorId::Player(), candidate, RelationshipStatus::Candidate, 1.0f, ActorId::Player(), meta);
        (void)ws.TransitionRelationship(ActorId::Player(), exSpouse, RelationshipStatus::Divorced, 1.0f, ActorId::Player(), meta);

        (void)ApplyRegisterTenant(ws, spouse.raw(), breezehome, true, false);
        (void)ApplyRegisterTenant(ws, lover.raw(), breezehome, true, false);
        (void)ApplyRegisterTenant(ws, child.raw(), breezehome, true, true);

        ChildRecord crec{};
        crec.childId = child;
        crec.stage = ChildStage::Child;
        crec.guardians.Assign(ActorId::Player(), spouse);
        ws.lineage.Write().UpsertChild(std::move(crec));

        const auto snap = ws.Snapshot();

        // Same-cell affection eligibility: Married/Engaged/Dating/Lover only, deduplicated
        const std::vector<ActorId> sameCell{
            ActorId::Player(), spouse, spouse, dating, candidate, exSpouse
        };
        const auto paid = EvaluateSleepPaidAffectionPartners(snap, sameCell);
        MARAS_REQUIRE_EQ(paid.size(), std::size_t{ 2 });
        MARAS_CHECK_EQ(paid[0], spouse);
        MARAS_CHECK_EQ(paid[1], dating);

        // Spouse + Child present -> MarriageSleep + AdoptionMale/Female depending on player sex
        const std::vector<std::uint32_t> spouseAndChild{ spouse.raw(), child.raw() };
        const auto spouseMalePlan = EvaluateSleepRestedPlan(snap, breezehome, spouseAndChild, ActorSex::Male);
        MARAS_CHECK_EQ(spouseMalePlan.partnerBuff, PartnerSleepBuff::MarriageSleep);
        MARAS_CHECK_EQ(spouseMalePlan.childBuff, ChildSleepBuff::AdoptionMale);

        const auto spouseFemalePlan = EvaluateSleepRestedPlan(snap, breezehome, spouseAndChild, ActorSex::Female);
        MARAS_CHECK_EQ(spouseFemalePlan.partnerBuff, PartnerSleepBuff::MarriageSleep);
        MARAS_CHECK_EQ(spouseFemalePlan.childBuff, ChildSleepBuff::AdoptionFemale);

        // Lover overrides MarriageSleep to LoversRadiance when also present
        const std::vector<std::uint32_t> spouseLoverPresent{ spouse.raw(), lover.raw() };
        const auto loverPlan = EvaluateSleepRestedPlan(snap, breezehome, spouseLoverPresent, ActorSex::Male);
        MARAS_CHECK_EQ(loverPlan.partnerBuff, PartnerSleepBuff::LoversRadiance);
        MARAS_CHECK_EQ(loverPlan.childBuff, ChildSleepBuff::None);

        // Non-player-house yields None even when partner IDs are present
        const auto tavernPlan = EvaluateSleepRestedPlan(snap, tavernHouse, spouseLoverPresent, ActorSex::Male);
        MARAS_CHECK_EQ(tavernPlan, SleepRestedPlan{});
    }

    MARAS_TEST(DomainHousehold, ChildForcegreetPetAllowanceAndChestRules) {
        struct MinMaxRng final : public IRng {
            bool returnMaxInt{ false };
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) override {
                return returnMaxInt ? maxVal : minVal;
            }
        };

        WorldState ws{};
        const HouseId breezehome{ 0x0003B001u };
        const ChildId childA{ 0x0003B101u };
        const ChildId childB{ 0x0003B102u };
        const ChildId synChild{ 0xFF700010u };

        (void)ApplyRegisterHouse(ws, breezehome, RefId{ 0x0003B002u }, true);

        ChildHouseholdRecord stA{};
        stA.childId = childA;
        stA.currentHouseId = breezehome;
        stA.isNewlyAdopted = true;
        ChildHouseholdRecord stB{};
        stB.childId = childB;
        stB.currentHouseId = breezehome;
        ChildHouseholdRecord stSyn{};
        stSyn.childId = synChild;
        stSyn.currentHouseId = breezehome;
        stSyn.isNewlyAdopted = true;

        ws.household.Write().UpsertChildState(stA);
        ws.household.Write().UpsertChildState(stB);
        ws.household.Write().UpsertChildState(stSyn);

        MinMaxRng rng{};
        ForcegreetSelectionInput fgIn{};
        fgIn.houseId = breezehome;
        fgIn.currentGameDays = 50.0f;

        // Event 2: newly adopted + chest not yet introduced
        const auto ev2 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev2.eventNumber, 2);
        MARAS_CHECK_EQ(ev2.eventChild, childA);
        MARAS_CHECK(ev2.markChestIntroduced);
        MARAS_CHECK(ev2.clearNewlyAdoptedForHouse);

        // Event 1: newly adopted + chest already introduced
        ws.household.Write().SetChildChestIntroduced(true);
        const auto ev1 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev1.eventNumber, 1);
        MARAS_CHECK_EQ(ev1.eventChild, childA);
        MARAS_CHECK(!ev1.markChestIntroduced);

        // Event 3: no newly adopted children, chest not introduced -> weighted chest intro
        ws.household.Write().SetChildChestIntroduced(false);
        ws.household.Write().FindChildStateMutable(childA)->isNewlyAdopted = false;
        const auto ev3 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev3.eventNumber, 3);
        MARAS_CHECK(!ev3.eventChild.IsNull());
        MARAS_CHECK(ev3.eventChild != synChild);
        MARAS_CHECK(ev3.markChestIntroduced);

        // Event 4: pet adoption when animal companion present & childA not pet-banned
        ws.household.Write().SetChildChestIntroduced(true);
        fgIn.hasAnimalCompanion = true;
        const auto ev4 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev4.eventNumber, 4);
        MARAS_CHECK_EQ(ev4.eventChild, childA);

        // Event 5: critter adoption when childA pet-banned (e.g. retry cooldown) & minVal(1) < 10
        ws.household.Write().FindChildStateMutable(childA)->petBanned = true;
        ws.household.Write().FindChildStateMutable(childB)->petId = RefId{ 0x0003B901u };
        const auto ev5 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev5.eventNumber, 5);
        MARAS_CHECK_EQ(ev5.eventChild, childA);

        // Event 6 & 7: Name-calling 1 -> then after playedScene1 -> Name-calling 2 -> blocked when nameCallingUsedLast=true
        ws.household.Write().FindChildStateMutable(childA)->critterBanned = true;
        ws.household.Write().FindChildStateMutable(childB)->critterBanned = true;
        const auto ev6 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev6.eventNumber, 6);
        MARAS_CHECK_EQ(ev6.sceneChild1, childA);
        MARAS_CHECK_EQ(ev6.sceneChild2, childB);

        ws.household.Write().FindChildStateMutable(childA)->playedScene1 = true;
        const auto ev7 = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK_EQ(ev7.eventNumber, 7);
        MARAS_CHECK_EQ(ev7.sceneChild1, childA);
        MARAS_CHECK_EQ(ev7.sceneChild2, childB);

        // Name-calling suppressed when used last; clears flag on generic 10-13 selection
        ws.household.Write().SetNameCallingUsedLast(true);
        const auto evGeneric = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
        MARAS_CHECK(evGeneric.eventNumber >= 10 && evGeneric.eventNumber <= 13);
        MARAS_CHECK(evGeneric.clearNameCallingUsedLast);

        // Fallback helper always returns 10..13 + clears nameCallingUsedLast
        const auto fb = PlanGenericForcegreetFallback(ws.Snapshot(), breezehome, 50.0f, rng);
        MARAS_CHECK(fb.eventNumber >= 10 && fb.eventNumber <= 13);
        MARAS_CHECK(fb.clearNameCallingUsedLast);

        // Pet race suitability
        const std::vector<FormRawId> dogs{ FormRawId{ 0x000131F5u } };
        const std::vector<FormRawId> allowed{ FormRawId{ 0x000131F5u }, FormRawId{ 0x00013205u } };
        const auto dogSuit = EvaluatePetRaceSuitability(FormRawId{ 0x000131F5u }, dogs, allowed);
        MARAS_CHECK(dogSuit.isDog);
        MARAS_CHECK_EQ(dogSuit.suitabilityRank, 3);
        const auto wolfSuit = EvaluatePetRaceSuitability(FormRawId{ 0x0001320Au }, dogs, allowed);
        MARAS_CHECK(!wolfSuit.isDog);
        MARAS_CHECK_EQ(wolfSuit.suitabilityRank, 1);

        // PoorCounter deltas + floor clamping
        MARAS_CHECK_EQ(CalculateAllowancePoorDelta(0), 1);
        MARAS_CHECK_EQ(CalculateAllowancePoorDelta(25), -1);
        MARAS_CHECK_EQ(CalculateAllowancePoorDelta(100), -2);
        MARAS_CHECK_EQ(CalculateAllowancePoorDelta(500), -3);
        MARAS_CHECK_EQ(CalculateAllowancePoorDelta(1000), -5);
        MARAS_CHECK_EQ(ApplyPoorCounterDelta(2, -5), 0);
        MARAS_CHECK_EQ(ApplyPoorCounterDelta(1, 2), 3);

        // Gift tiers + regular gift deduction clamping + luck
        MARAS_CHECK_EQ(SelectChildGiftTier(24).tierIndex, 0);
        MARAS_CHECK_EQ(SelectChildGiftTier(25).tierIndex, 1);
        MARAS_CHECK_EQ(SelectChildGiftTier(50).tierIndex, 2);
        MARAS_CHECK_EQ(SelectChildGiftTier(100).tierIndex, 3);
        MARAS_CHECK_EQ(SelectChildGiftTier(250).tierIndex, 4);
        MARAS_CHECK_EQ(SelectChildGiftTier(500).tierIndex, 5);
        MARAS_CHECK_EQ(SelectChildGiftTier(1000).tierIndex, 6);

        rng.returnMaxInt = true;  // luck -> 2, max deduction clamped to childGold
        const auto regMax = PlanChildRegularGift(60, rng);
        MARAS_CHECK_EQ(regMax.tierIndex, 2);
        MARAS_CHECK_EQ(regMax.count, 2);
        MARAS_CHECK_EQ(regMax.goldDeduction, 60);

        // Chest refill: poorest/<=25 children yield no bonus, richest > 25 yields bonus tier & gold deduction
        rng.returnMaxInt = false;
        const std::vector<std::pair<ChildId, std::int32_t>> lowGold{ { childA, 25 } };
        const auto refillNoBonus = PlanChildChestRefill(lowGold, rng);
        MARAS_CHECK_EQ(refillNoBonus.junkItemCounts.size(), std::size_t{ 3 });
        MARAS_CHECK_EQ(refillNoBonus.tier0025RollCount, 1);
        MARAS_CHECK(!refillNoBonus.hasBonusGift);

        const std::vector<std::pair<ChildId, std::int32_t>> richGold{ { childA, 40 }, { childB, 600 } };
        const auto refillWithBonus = PlanChildChestRefill(richGold, rng);
        MARAS_CHECK(refillWithBonus.hasBonusGift);
        MARAS_CHECK_EQ(refillWithBonus.richestChild, childB);
        MARAS_CHECK_EQ(refillWithBonus.bonusGiftTierIndex, 5);
        MARAS_CHECK(refillWithBonus.bonusGoldDeduction >= 250 && refillWithBonus.bonusGoldDeduction <= 500);
    }

    MARAS_TEST(DomainHousehold, MultiSpouseSharedCellDeltasSleepAdultGuardAndForcegreetTenantFallback) {
        struct ZeroRng final : public IRng {
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        };

        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        const HouseId breezehome{ 0x0003C001u };
        const CellId sharedCell{ 0x0003C010u };
        const ActorId spouse1{ 0x0003C101u };
        const ActorId spouse2{ 0x0003C102u };
        const ChildId adultChild{ 0x0003C201u };
        const ChildId tenantChild{ 0x0003C202u };

        ws.settings.Write().enablePlayerPolygamy = true;
        for (ActorId id : { spouse1, spouse2 }) {
            ActorProfileRecord p{};
            p.refId = id.AsRefId();
            p.baseId = BaseId{ id.raw() + 0x100u };
            p.isRegisteredAdult = true;
            ws.actors.Write().Upsert(p);
            (void)ws.PromoteToMarried(ActorId::Player(), id, 1.0f, ActorId::Player(), meta);
        }

        // 1. StopSharingForSpouseWithDeltas ref-counts multiple spouses on the same shared cell
        {
            SharedCellRecord cellRec{};
            cellRec.sharedWithPlayer = true;
            cellRec.originalPublicRecorded = true;
            cellRec.originalPublicState = false;
            cellRec.sharingSpouses.insert(spouse1);
            cellRec.sharingSpouses.insert(spouse2);
            ws.household.Write().UpsertSharedCell(sharedCell, cellRec);

            const auto firstStop = ws.StopSharingForSpouseWithDeltas(spouse1);
            MARAS_CHECK(firstStop.changed);
            MARAS_REQUIRE_EQ(firstStop.stoppedSharingSpouses.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(firstStop.stoppedSharingSpouses[0], spouse1);
            MARAS_CHECK(firstStop.restoredSharedCells.empty());
            MARAS_CHECK(ws.household->IsCellSharedWithPlayer(sharedCell));

            const auto secondStop = ws.StopSharingForSpouseWithDeltas(spouse2);
            MARAS_CHECK(secondStop.changed);
            MARAS_REQUIRE_EQ(secondStop.stoppedSharingSpouses.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(secondStop.stoppedSharingSpouses[0], spouse2);
            MARAS_REQUIRE_EQ(secondStop.restoredSharedCells.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(secondStop.restoredSharedCells[0].cellId, sharedCell);
            MARAS_CHECK(!ws.household->IsCellSharedWithPlayer(sharedCell));
        }

        // 2. ComputeSpouseMultiplier excludes deceased spouse from livingMarriedCount
        {
            const float twoLivingMult = ComputeSpouseMultiplier(ws.Snapshot(), spouse1);
            (void)ws.RecordActorDeath(spouse2, 5.0f, false, ActorId{}, meta);
            const float oneLivingMult = ComputeSpouseMultiplier(ws.Snapshot(), spouse1);
            MARAS_CHECK(twoLivingMult > 0.0f);
            MARAS_CHECK(oneLivingMult > twoLivingMult);
        }

        // 3. EvaluateSleepRestedPlan ignores ChildStage::Adult without ChildHouseholdRecord & handles null house
        {
            (void)ApplyRegisterHouse(ws, breezehome, RefId{ 0x0003C002u }, true);
            ChildRecord adultRec{};
            adultRec.childId = adultChild;
            adultRec.stage = ChildStage::Adult;
            adultRec.guardians.Assign(ActorId::Player(), spouse1);
            ws.lineage.Write().UpsertChild(adultRec);
            (void)ApplyRegisterTenant(ws, adultChild.raw(), breezehome, true, false);

            const std::vector<std::uint32_t> onlyAdultChild{ adultChild.raw() };
            const auto adultPlan = EvaluateSleepRestedPlan(
                ws.Snapshot(), breezehome, onlyAdultChild, ActorSex::Male);
            MARAS_CHECK_EQ(adultPlan.partnerBuff, PartnerSleepBuff::None);
            MARAS_CHECK_EQ(adultPlan.childBuff, ChildSleepBuff::None);
            MARAS_CHECK_EQ(
                EvaluateSleepRestedPlan(ws.Snapshot(), HouseId{}, onlyAdultChild, ActorSex::Male),
                SleepRestedPlan{});
        }

        // 4. PlanReadyForcegreetEvent falls back to GetTenantHouse when currentHouseId is null
        {
            ChildHouseholdRecord chRec{};
            chRec.childId = tenantChild;
            chRec.isNewlyAdopted = true;
            ws.household.Write().UpsertChildState(chRec);
            (void)ApplyRegisterTenant(ws, tenantChild.raw(), breezehome, true, true);

            ZeroRng rng{};
            ForcegreetSelectionInput fgIn{};
            fgIn.houseId = breezehome;
            fgIn.currentGameDays = 20.0f;

            const auto plan = PlanReadyForcegreetEvent(ws.Snapshot(), fgIn, rng);
            MARAS_CHECK_EQ(plan.eventNumber, 2);
            MARAS_CHECK_EQ(plan.eventChild, tenantChild);
            MARAS_CHECK(plan.markChestIntroduced);
            MARAS_CHECK(plan.clearNewlyAdoptedForHouse);
        }
    }

    MARAS_TEST(DomainHousehold, ChildAnchoredRelocationAndMarriageSpouseStayAtHome) {
        const HouseId breezehome{ 0x00020098u };
        const HouseId proudspire{ 0x0003D002u };
        const RefId markerA{ 0x0003D010u };
        const RefId markerB{ 0x0003D020u };
        const ActorId serana{ 0x02002B74u };
        const ActorId unrelatedNpc{ 0x0003D100u };
        const ActorId deceasedSpouse{ 0x0003D101u };
        const ChildId newChild{ 0xFF000DEEu };
        const ChildId existingSibling{ 0xFF000DE0u };

        // 1. Child-anchored relocation (moveParents = true) houses unhoused co-parent with stayAtHome = false
        //    and populates changedHouseChildren only for children changing house or lacking homeMarkerId.
        {
            WorldState ws{};
            MARAS_CHECK(ApplyRegisterHouse(ws, breezehome, markerA, true));
            MARAS_CHECK(ApplyRegisterHouse(ws, proudspire, markerB, true));

            ws.relationships.Write().GetOrCreateEdge(serana, ActorId::Player()).status =
                RelationshipStatus::Married;

            ChildRecord newRec{};
            newRec.childId = newChild;
            newRec.stage = ChildStage::Child;
            newRec.guardians.Assign(ActorId::Player(), serana);
            ws.lineage.Write().UpsertChild(newRec);

            ChildRecord sibRec{};
            sibRec.childId = existingSibling;
            sibRec.stage = ChildStage::Child;
            sibRec.guardians.Assign(ActorId::Player(), serana);
            ws.lineage.Write().UpsertChild(sibRec);

            ChildHouseholdRecord sibHh{};
            sibHh.childId = existingSibling;
            sibHh.currentHouseId = breezehome;
            sibHh.homeMarkerId = RefId{ 0xFF000DF0u };
            ws.household.Write().UpsertChildState(sibHh);
            MARAS_CHECK(ApplyRegisterTenant(ws, existingSibling.raw(), breezehome, true, true));

            const auto reloc = ApplyRelocateFamilyUnit(ws, newChild.raw(), breezehome, true);
            MARAS_CHECK(reloc.targetHouseExists);
            MARAS_CHECK(!reloc.ineligibleAdultAnchor);
            MARAS_CHECK_EQ(reloc.movedAdults.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(reloc.movedAdults[0], serana);
            MARAS_CHECK_EQ(reloc.movedChildren.size(), std::size_t{ 2 });
            MARAS_CHECK_EQ(reloc.changedHouseChildren.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(reloc.changedHouseChildren[0], newChild);

            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(serana.raw()), breezehome);
            MARAS_CHECK(!ws.household.Read().GetTenantStayAtHome(serana.raw()));
            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(newChild.raw()), breezehome);
            MARAS_CHECK(ws.household.Read().GetTenantStayAtHome(newChild.raw()));
            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(existingSibling.raw()), breezehome);

            // 2. Child-anchored relocation to another house preserves already-housed spouse stayAtHome = true
            MARAS_CHECK(ApplySetTenantStayAtHome(ws, serana.raw(), true));
            const auto reloc2 = ApplyRelocateFamilyUnit(ws, newChild.raw(), proudspire, true);
            MARAS_CHECK(reloc2.targetHouseExists);
            MARAS_CHECK_EQ(reloc2.movedAdults.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(serana.raw()), proudspire);
            MARAS_CHECK(ws.household.Read().GetTenantStayAtHome(serana.raw()));
            MARAS_CHECK_EQ(reloc2.changedHouseChildren.size(), std::size_t{ 2 });
        }

        // 3. Child-anchored relocation with moveParents = false leaves unhoused co-parent unhoused
        {
            WorldState ws{};
            MARAS_CHECK(ApplyRegisterHouse(ws, breezehome, markerA, true));
            ws.relationships.Write().GetOrCreateEdge(serana, ActorId::Player()).status =
                RelationshipStatus::Married;

            ChildRecord newRec{};
            newRec.childId = newChild;
            newRec.stage = ChildStage::Child;
            newRec.guardians.Assign(ActorId::Player(), serana);
            ws.lineage.Write().UpsertChild(newRec);

            const auto reloc = ApplyRelocateFamilyUnit(ws, newChild.raw(), breezehome, false);
            MARAS_CHECK(reloc.targetHouseExists);
            MARAS_CHECK(reloc.movedAdults.empty());
            MARAS_CHECK_EQ(reloc.movedChildren.size(), std::size_t{ 1 });
            MARAS_CHECK(ws.household.Read().GetTenantHouse(serana.raw()).IsNull());
            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(newChild.raw()), breezehome);
        }

        // 4. Adult-anchored relocation eligibility & unhoused stayAtHome = true default
        {
            WorldState ws{};
            MARAS_CHECK(ApplyRegisterHouse(ws, breezehome, markerA, true));
            ws.relationships.Write().GetOrCreateEdge(serana, ActorId::Player()).status =
                RelationshipStatus::Married;

            DeceasedActorRecord deadRec{};
            deadRec.refId = deceasedSpouse.AsRefId();
            deadRec.baseId = BaseId{ 0x0003D901u };
            deadRec.deathTimestamp = 5.0f;
            ws.deceased.Write().UpsertRecord(std::move(deadRec));
            ws.relationships.Write().GetOrCreateEdge(deceasedSpouse, ActorId::Player()).status =
                RelationshipStatus::Married;

            const auto unrelatedReloc = ApplyRelocateFamilyUnit(ws, unrelatedNpc.raw(), breezehome, true);
            MARAS_CHECK(unrelatedReloc.targetHouseExists);
            MARAS_CHECK(unrelatedReloc.ineligibleAdultAnchor);
            MARAS_CHECK(unrelatedReloc.movedAdults.empty());
            MARAS_CHECK(ws.household.Read().GetTenantHouse(unrelatedNpc.raw()).IsNull());

            const auto deadReloc = ApplyRelocateFamilyUnit(ws, deceasedSpouse.raw(), breezehome, true);
            MARAS_CHECK(deadReloc.targetHouseExists);
            MARAS_CHECK(deadReloc.ineligibleAdultAnchor);
            MARAS_CHECK(deadReloc.movedAdults.empty());
            MARAS_CHECK(ws.household.Read().GetTenantHouse(deceasedSpouse.raw()).IsNull());

            const auto adultReloc = ApplyRelocateFamilyUnit(ws, serana.raw(), breezehome, true);
            MARAS_CHECK(adultReloc.targetHouseExists);
            MARAS_CHECK(!adultReloc.ineligibleAdultAnchor);
            MARAS_CHECK_EQ(adultReloc.movedAdults.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(serana.raw()), breezehome);
            MARAS_CHECK(ws.household.Read().GetTenantStayAtHome(serana.raw()));
        }

        // 5. EvaluateCustodyOnMarriage auto-houses unhoused new spouse into child's home with stayAtHome = false
        {
            WorldState ws{};
            MARAS_CHECK(ApplyRegisterHouse(ws, breezehome, markerA, true));

            ChildRecord soleChild{};
            soleChild.childId = newChild;
            soleChild.stage = ChildStage::Child;
            soleChild.guardians.Assign(ActorId::Player(), ActorId{});
            ws.lineage.Write().UpsertChild(soleChild);

            ChildHouseholdRecord chState{};
            chState.childId = newChild;
            chState.currentHouseId = breezehome;
            ws.household.Write().UpsertChildState(chState);
            MARAS_CHECK(ApplyRegisterTenant(ws, newChild.raw(), breezehome, true, true));

            ws.relationships.Write().GetOrCreateEdge(serana, ActorId::Player()).status =
                RelationshipStatus::Married;
            DefaultActorMetadataProvider meta{};
            (void)ws.EvaluateCustodyOnMarriage(ActorId::Player(), serana, meta);

            MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(serana.raw()), breezehome);
            MARAS_CHECK(!ws.household.Read().GetTenantStayAtHome(serana.raw()));
        }
    }

}  // namespace MARAS::Tests
