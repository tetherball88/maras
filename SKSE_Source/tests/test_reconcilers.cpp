#include "TestHarness.h"

#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldStore.h"

#include <cmath>
#include <vector>

namespace {

    using MARAS::Foundation::ActorId;
    using MARAS::Foundation::BaseId;
    using MARAS::Foundation::ChildId;
    using MARAS::Foundation::HouseId;
    using MARAS::Foundation::LocationId;
    using MARAS::Foundation::RefId;
    using MARAS::State::ActorProfileRecord;
    using MARAS::State::ActorSex;
    using MARAS::State::AdultChildTravelRecord;
    using MARAS::State::AffairStatus;
    using MARAS::State::AffairType;
    using MARAS::State::ChildHouseholdRecord;
    using MARAS::State::ChildRecord;
    using MARAS::State::ChildStage;
    using MARAS::State::ComputeActorPackagePlan;
    using MARAS::State::ComputeAdultActorFactionPlan;
    using MARAS::State::ComputeBonusTargetValue;
    using MARAS::State::ComputeBuffMultipliers;
    using MARAS::State::ComputeChildFactionPlan;
    using MARAS::State::ComputeCompanionFactionPlan;
    using MARAS::State::ComputeDesiredGlobals;
    using MARAS::State::ComputePlayerFactionPlan;
    using MARAS::State::DeceasedActorRecord;
    using MARAS::State::FactionMembershipOp;
    using MARAS::State::PackageRole;
    using MARAS::State::RelationshipEdgeRecord;
    using MARAS::State::RelationshipStatus;
    using MARAS::State::SkillType;
    using MARAS::State::SocialClass;
    using MARAS::State::Temperament;
    using MARAS::State::WorldState;
    using MARAS::State::WorldStore;

    [[nodiscard]] bool ApproxEq(float a, float b, float eps = 1e-4f) noexcept {
        return std::fabs(a - b) <= eps;
    }

    ActorProfileRecord MakeAdultProfile(
        std::uint32_t id,
        SocialClass sc = SocialClass::Middle,
        SkillType st = SkillType::Warrior,
        Temperament temp = Temperament::Romantic,
        std::uint32_t homeMarker = 0) {
        ActorProfileRecord rec{};
        rec.refId = RefId{ id };
        rec.baseId = BaseId{ id };
        rec.socialClass = sc;
        rec.skillType = st;
        rec.temperament = temp;
        rec.isRegisteredAdult = true;
        if (homeMarker != 0) {
            rec.legacyHomeMarker = RefId{ homeMarker };
        }
        return rec;
    }

    void AddRelationship(
        WorldState& ws,
        ActorId a,
        ActorId b,
        RelationshipStatus status,
        bool isOpen = false) {
        auto& edge = ws.relationships.Write().GetOrCreateEdge(a, b);
        edge.status = status;
        edge.isOpen = isOpen;
    }

}  // namespace

MARAS_TEST(Reconcilers, DesiredGlobals_PolygamyAndSkyrimNetGating) {
    WorldStore store{};
    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = true;
        ws.settings.Write().simulateNoSkyrimNet = false;
        AddRelationship(ws, ActorId::Player(), ActorId{ 0x00010001u }, RelationshipStatus::Married);
        AddRelationship(ws, ActorId::Player(), ActorId{ 0x00010002u }, RelationshipStatus::Married);
        AddRelationship(ws, ActorId::Player(), ActorId{ 0x00010003u }, RelationshipStatus::Engaged);
        AddRelationship(ws, ActorId::Player(), ActorId{ 0x00010004u }, RelationshipStatus::Dating);
        ws.household.Write().RegisterHouse(HouseId{ 0x00020001u }, RefId{ 0x00020011u });
        ws.household.Write().RegisterHouse(HouseId{ 0x00020002u }, RefId{ 0x00020012u });
    });

    const auto snap = store.Snapshot();
    const auto planLoaded = ComputeDesiredGlobals(*snap, true);
    MARAS_CHECK_EQ(planLoaded.spousesCount, 2);
    MARAS_CHECK_EQ(planLoaded.loveInterestsCount, 3);
    MARAS_CHECK_EQ(planLoaded.playerHousesCount, 2);
    MARAS_CHECK_EQ(planLoaded.enablePolygamy, true);
    MARAS_CHECK_EQ(planLoaded.hasSkyrimNet, true);

    const auto planUnloaded = ComputeDesiredGlobals(*snap, false);
    MARAS_CHECK_EQ(planUnloaded.hasSkyrimNet, false);

    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        ws.settings.Write().simulateNoSkyrimNet = true;
    });
    const auto planSimulated = ComputeDesiredGlobals(*store.Snapshot(), true);
    MARAS_CHECK_EQ(planSimulated.enablePolygamy, false);
    MARAS_CHECK_EQ(planSimulated.hasSkyrimNet, false);
}

MARAS_TEST(Reconcilers, PlayerFactions_PolygamyVsMonogamyAndOpenMain) {
    WorldStore store{};
    constexpr ActorId spouse1{ 0x00010010u };

    // Single closed marriage in monogamy -> PlayerMarriedFaction present, canFormNonCheating = 0
    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        AddRelationship(ws, ActorId::Player(), spouse1, RelationshipStatus::Married, false);
    });
    auto p1 = ComputePlayerFactionPlan(*store.Snapshot());
    MARAS_CHECK_EQ(p1.hasMarriedFaction, true);
    MARAS_CHECK_EQ(p1.canFormNonCheatingMainRank, std::int8_t{ 0 });

    // Enabling polygamy strips PlayerMarriedFaction & restores canFormNonCheating = 1
    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = true;
    });
    auto p2 = ComputePlayerFactionPlan(*store.Snapshot());
    MARAS_CHECK_EQ(p2.hasMarriedFaction, false);
    MARAS_CHECK_EQ(p2.canFormNonCheatingMainRank, std::int8_t{ 1 });

    // Monogamy with ONLY open relationships -> hasMarriedFaction true, canFormNonCheating = 1
    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        ws.relationships.Write().GetOrCreateEdge(ActorId::Player(), spouse1).isOpen = true;
    });
    auto p3 = ComputePlayerFactionPlan(*store.Snapshot());
    MARAS_CHECK_EQ(p3.hasMarriedFaction, true);
    MARAS_CHECK_EQ(p3.canFormNonCheatingMainRank, std::int8_t{ 1 });

    // Unmarried monogamous player -> hasMarriedFaction false
    store.Modify([&](WorldState& ws) {
        ws.relationships.Write().GetOrCreateEdge(ActorId::Player(), spouse1).status = RelationshipStatus::Divorced;
    });
    auto p4 = ComputePlayerFactionPlan(*store.Snapshot());
    MARAS_CHECK_EQ(p4.hasMarriedFaction, false);
    MARAS_CHECK_EQ(p4.canFormNonCheatingMainRank, std::int8_t{ 1 });
}

MARAS_TEST(Reconcilers, AdultActorFactions_MarriedCheatingOpenHierarchyAndChildren) {
    WorldStore store{};
    constexpr ActorId spouse{ 0x0001A001u };
    constexpr ActorId paramour{ 0x0001A002u };
    constexpr ChildId child1{ 0xFF000101u };
    constexpr HouseId house{ 0x00050001u };

    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(spouse.raw(), SocialClass::Nobles, SkillType::Mage, Temperament::Proud));
        ws.actors.Write().Upsert(MakeAdultProfile(paramour.raw(), SocialClass::Working, SkillType::Rogue, Temperament::Humble));
        AddRelationship(ws, spouse, ActorId::Player(), RelationshipStatus::Married, true);
        ws.hierarchy.Write().OnSpouseAdded(spouse);
        ws.affection.Write().SetPermanent(spouse, 82);
        ws.household.Write().RegisterHouse(house, RefId{ 0x00050002u });
        ws.household.Write().AssignTenant(spouse.raw(), house, true, false);

        ChildRecord ch{};
        ch.childId = child1;
        ch.guardians.Assign(ActorId::Player(), spouse);
        ch.stage = ChildStage::Child;
        ws.lineage.Write().UpsertChild(ch);

        // Ongoing secret affair
        ws.affairs.Write().RecordIncident(
            spouse, ActorId::Player(), paramour, true, AffairType::Lover, AffairStatus::Ongoing,
            MARAS::State::AffairStage::Physical, 10.0f);
    });

    auto planSecret = ComputeAdultActorFactionPlan(*store.Snapshot(), spouse);
    MARAS_CHECK_EQ(planSecret.isRelevantAdult, true);
    MARAS_CHECK_EQ(planSecret.trackedRank.value(), std::int8_t{ 2 });
    MARAS_CHECK_EQ(planSecret.socialClassRank.value(), static_cast<std::int8_t>(SocialClass::Nobles));
    MARAS_CHECK_EQ(planSecret.skillTypeRank.value(), static_cast<std::int8_t>(SkillType::Mage));
    MARAS_CHECK_EQ(planSecret.temperamentRank.value(), static_cast<std::int8_t>(Temperament::Proud));
    MARAS_CHECK_EQ(planSecret.hierarchyRank.value(), std::int8_t{ 0 });
    MARAS_CHECK_EQ(planSecret.affectionRank.value(), std::int8_t{ 82 });
    MARAS_CHECK_EQ(planSecret.spouseChildrenRank.value(), std::int8_t{ 1 });
    MARAS_CHECK_EQ(planSecret.spouseCheatingRank.value(), std::int8_t{ 0 });  // ongoing secret = 0
    MARAS_CHECK_EQ(planSecret.spouseOpenRelationshipRank.value(), std::int8_t{ 0 });
    MARAS_CHECK_EQ(planSecret.canFormNonCheatingMainRank.value(), std::int8_t{ 1 });
    MARAS_CHECK_EQ(planSecret.housedRank.value(), std::int8_t{ 0 });
    MARAS_CHECK_EQ(planSecret.marriedFactionOp, FactionMembershipOp::EnsurePresent);
    MARAS_CHECK_EQ(planSecret.potentialHirelingOp, FactionMembershipOp::EnsureAbsent);

    // Known ongoing affair escalates cheating rank to 1
    store.Modify([&](WorldState& ws) {
        ws.affairs.Write().RecordIncident(
            spouse, ActorId::Player(), paramour, false, AffairType::Lover, AffairStatus::Ongoing,
            MARAS::State::AffairStage::Physical, 12.0f);
    });
    auto planKnown = ComputeAdultActorFactionPlan(*store.Snapshot(), spouse);
    MARAS_CHECK_EQ(planKnown.spouseCheatingRank.value(), std::int8_t{ 1 });

    // Divorce + eviction cleans spouse ranks & marriage factions
    store.Modify([&](WorldState& ws) {
        ws.relationships.Write().GetOrCreateEdge(spouse, ActorId::Player()).status = RelationshipStatus::Divorced;
        ws.relationships.Write().GetOrCreateEdge(spouse, ActorId::Player()).isOpen = false;
        ws.hierarchy.Write().OnSpouseRemoved(spouse, {});
        ws.household.Write().EvictTenant(spouse.raw());
    });
    auto planDivorced = ComputeAdultActorFactionPlan(*store.Snapshot(), spouse);
    MARAS_CHECK_EQ(planDivorced.trackedRank.value(), std::int8_t{ 3 });
    MARAS_CHECK(!planDivorced.hierarchyRank.has_value());
    MARAS_CHECK(!planDivorced.spouseChildrenRank.has_value());
    MARAS_CHECK(!planDivorced.spouseCheatingRank.has_value());
    MARAS_CHECK(!planDivorced.housedRank.has_value());
    MARAS_CHECK_EQ(planDivorced.marriedFactionOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(planDivorced.marriagePotentialOp, FactionMembershipOp::EnsurePresent);
}

MARAS_TEST(Reconcilers, Stage4AdultChildFactions_BeastbloodAndYoungChildCleanup) {
    WorldStore store{};
    constexpr ChildId adultBerserker{ 0xFF000201u };
    constexpr ChildId adultHunter{ 0xFF000202u };

    store.Modify([&](WorldState& ws) {
        ChildRecord b{};
        b.childId = adultBerserker;
        b.guardians.Assign(ActorId::Player(), ActorId{ 0x00010001u });
        b.stage = ChildStage::Adult;
        b.hasBeastblood = true;
        ws.lineage.Write().UpsertChild(b);

        ChildRecord h{};
        h.childId = adultHunter;
        h.guardians.Assign(ActorId::Player(), ActorId{ 0x00010001u });
        h.stage = ChildStage::Adult;
        h.hasBeastblood = true;
        ws.lineage.Write().UpsertChild(h);
    });

    const auto snap = store.Snapshot();
    const auto bPlan = ComputeAdultActorFactionPlan(*snap, adultBerserker.AsActorId(), "pack_berserker");
    MARAS_CHECK_EQ(bPlan.isRelevantAdult, true);
    MARAS_CHECK_EQ(bPlan.isGraduatedAdultChild, true);
    MARAS_CHECK_EQ(bPlan.clearYoungChildFactions, true);
    MARAS_CHECK_EQ(bPlan.ensurePotentialFollower, true);
    MARAS_CHECK_EQ(bPlan.ensurePlayerWerewolf, true);
    MARAS_CHECK_EQ(bPlan.ensureCompanionsCircle, true);
    MARAS_CHECK_EQ(bPlan.ensureFrostmoonWerewolves, false);

    const auto hPlan = ComputeAdultActorFactionPlan(*snap, adultHunter.AsActorId(), "pack_hunter");
    MARAS_CHECK_EQ(hPlan.ensurePlayerWerewolf, true);
    MARAS_CHECK_EQ(hPlan.ensureCompanionsCircle, false);
    MARAS_CHECK_EQ(hPlan.ensureFrostmoonWerewolves, true);
}

MARAS_TEST(Reconcilers, ChildFactions_UnhousedPlayerChildPreservesAdoptable25AndOrphanage0) {
    WorldStore store{};
    constexpr ActorId spouseMale{ 0x0001C001u };
    constexpr ChildId unhousedPlayerChild{ 0xFF000301u };
    constexpr ChildId orphanChild{ 0xFF000302u };
    constexpr ChildId parentedNpcChild{ 0xFF000303u };
    constexpr RefId honorhall{ 0x00090001u };
    constexpr RefId npcMom{ 0x0001C002u };

    store.Modify([&](WorldState& ws) {
        AddRelationship(ws, spouseMale, ActorId::Player(), RelationshipStatus::Married);

        ChildRecord c1{};
        c1.childId = unhousedPlayerChild;
        c1.guardians.Assign(ActorId::Player(), spouseMale);
        c1.stage = ChildStage::Child;
        ws.lineage.Write().UpsertChild(c1);

        // Unhoused player child household record (houseId == 0)
        ChildHouseholdRecord hh1{};
        hh1.childId = unhousedPlayerChild;
        hh1.currentHouseId = HouseId{};
        ws.household.Write().UpsertChildState(hh1);

        ChildRecord c2{};
        c2.childId = orphanChild;
        c2.stage = ChildStage::Child;
        ws.lineage.Write().UpsertChild(c2);

        ChildRecord c3{};
        c3.childId = parentedNpcChild;
        c3.guardians.Assign(ActorId{ npcMom.raw() }, ActorId{});
        c3.stage = ChildStage::Child;
        ws.lineage.Write().UpsertChild(c3);
    });

    const auto snap = store.Snapshot();
    const auto p1 = ComputeChildFactionPlan(*snap, unhousedPlayerChild, ActorSex::Male, RefId{ spouseMale.raw() }, honorhall);
    MARAS_CHECK_EQ(p1.isYoungChild, true);
    MARAS_CHECK_EQ(p1.inPlayerHousehold, true);
    MARAS_CHECK_EQ(p1.playerChildRank.value(), std::int8_t{ 0 });
    MARAS_CHECK_EQ(p1.adoptableOp, FactionMembershipOp::EnsurePresent);
    MARAS_CHECK_EQ(p1.adoptableRank, std::int8_t{ 25 });
    MARAS_CHECK_EQ(p1.bothParentsRank.value(), std::int8_t{ 2 });  // Male co-guardian -> rank 2

    const auto p1Female = ComputeChildFactionPlan(*snap, unhousedPlayerChild, ActorSex::Female, RefId{ spouseMale.raw() }, honorhall);
    MARAS_CHECK_EQ(p1Female.bothParentsRank.value(), std::int8_t{ 1 });

    const auto p2 = ComputeChildFactionPlan(*snap, orphanChild, ActorSex::Unknown, honorhall, honorhall);
    MARAS_CHECK_EQ(p2.inPlayerHousehold, false);
    MARAS_CHECK(!p2.playerChildRank.has_value());
    MARAS_CHECK_EQ(p2.adoptableOp, FactionMembershipOp::EnsurePresent);
    MARAS_CHECK_EQ(p2.adoptableRank, std::int8_t{ 0 });

    const auto p3 = ComputeChildFactionPlan(*snap, parentedNpcChild, ActorSex::Unknown, npcMom, honorhall);
    MARAS_CHECK_EQ(p3.adoptableOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(p3.isPhysicalInfant, false);
}

MARAS_TEST(Reconcilers, PhysicalInfant_MutedFactionPlanAndHomeSandboxOrFollowPackages) {
    WorldStore store{};
    constexpr HouseId house{ 0x00060010u };
    constexpr RefId childHomeMarker{ 0x00060011u };
    constexpr ChildId housedInfant{ 0xFF000601u };
    constexpr ChildId npcInfant{ 0xFF000602u };
    constexpr RefId npcMom{ 0x0002B099u };
    constexpr RefId honorhall{ 0x00090001u };

    store.Modify([&](WorldState& ws) {
        ws.household.Write().RegisterHouse(house, RefId{ 0x00060012u });

        ChildRecord c1{};
        c1.childId = housedInfant;
        c1.stage = ChildStage::Infant;
        c1.guardians.Assign(ActorId::Player(), ActorId{});
        ws.lineage.Write().UpsertChild(c1);

        ChildHouseholdRecord hh{};
        hh.childId = housedInfant;
        hh.currentHouseId = house;
        hh.homeMarkerId = childHomeMarker;
        ws.household.Write().UpsertChildState(hh);
        ws.household.Write().AssignTenant(housedInfant.raw(), house, true);

        ChildRecord c2{};
        c2.childId = npcInfant;
        c2.stage = ChildStage::Infant;
        c2.guardians.Assign(ActorId{ npcMom.raw() }, ActorId{});
        ws.lineage.Write().UpsertChild(c2);
    });

    const auto snap = store.Snapshot();

    const auto fp1 = ComputeChildFactionPlan(*snap, housedInfant, ActorSex::Unknown, RefId{}, honorhall);
    MARAS_CHECK_EQ(fp1.isYoungChild, true);
    MARAS_CHECK_EQ(fp1.isPhysicalInfant, true);
    MARAS_CHECK_EQ(fp1.inPlayerHousehold, true);

    const auto pp1 = ComputeActorPackagePlan(*snap, RefId{ housedInfant.raw() }, RefId{});
    MARAS_CHECK_EQ(pp1.role, PackageRole::HousedTenant);
    MARAS_CHECK_EQ(pp1.homeSandboxTargetId, childHomeMarker);

    const auto fp2 = ComputeChildFactionPlan(*snap, npcInfant, ActorSex::Unknown, npcMom, honorhall);
    MARAS_CHECK_EQ(fp2.isPhysicalInfant, true);
    MARAS_CHECK_EQ(fp2.adoptableOp, FactionMembershipOp::EnsureAbsent);

    const auto pp2 = ComputeActorPackagePlan(*snap, RefId{ npcInfant.raw() }, npcMom);
    MARAS_CHECK_EQ(pp2.role, PackageRole::FollowParentChild);
    MARAS_CHECK_EQ(pp2.childFollowTargetId, npcMom);
}

MARAS_TEST(Reconcilers, CompanionFactions_AssignedAndRemovedPetCritter) {
    WorldStore store{};
    constexpr ChildId child{ 0xFF000401u };
    constexpr RefId pet{ 0x00080001u };
    constexpr RefId critter{ 0x00080002u };

    store.Modify([&](WorldState& ws) {
        ChildHouseholdRecord hh{};
        hh.childId = child;
        hh.petId = pet;
        hh.critterId = critter;
        ws.household.Write().UpsertChildState(hh);
    });

    auto petPlan = ComputeCompanionFactionPlan(*store.Snapshot(), pet);
    MARAS_CHECK_EQ(petPlan.isActiveCompanion, true);
    MARAS_CHECK_EQ(petPlan.ownerChildId, child);
    MARAS_CHECK_EQ(petPlan.playerChildPetRank.value(), std::int8_t{ 0 });
    MARAS_CHECK_EQ(petPlan.playerFactionRank.value(), std::int8_t{ 0 });

    store.Modify([&](WorldState& ws) {
        ws.household.Write().FindChildStateMutable(child)->petId = RefId{};
    });
    auto removedPlan = ComputeCompanionFactionPlan(*store.Snapshot(), pet);
    MARAS_CHECK_EQ(removedPlan.isActiveCompanion, false);
    MARAS_CHECK(!removedPlan.playerChildPetRank.has_value());
}

MARAS_TEST(Reconcilers, PackagePrecedence_TenantOverPetOverChildOverAdultTravelAndEvictionClearsLinkedRefs) {
    WorldStore store{};
    constexpr HouseId house{ 0x00050010u };
    constexpr ActorId spouse{ 0x0001D001u };
    constexpr RefId spouseHomeMarker{ 0x00090010u };
    constexpr ChildId unhousedChild{ 0xFF000501u };
    constexpr RefId pet{ 0x00080010u };
    constexpr ChildId adultChild{ 0xFF000502u };
    constexpr RefId travelMarker{ 0x00090020u };

    store.Modify([&](WorldState& ws) {
        ws.household.Write().RegisterHouse(house, RefId{ 0x00050011u });

        ws.actors.Write().Upsert(MakeAdultProfile(spouse.raw(), SocialClass::Middle, SkillType::Warrior, Temperament::Humble, spouseHomeMarker.raw()));
        AddRelationship(ws, spouse, ActorId::Player(), RelationshipStatus::Married);
        ws.household.Write().AssignTenant(spouse.raw(), house, true, false);

        ChildRecord c1{};
        c1.childId = unhousedChild;
        c1.guardians.Assign(ActorId::Player(), spouse);
        c1.stage = ChildStage::Child;
        ws.lineage.Write().UpsertChild(c1);

        ChildHouseholdRecord hh1{};
        hh1.childId = unhousedChild;
        hh1.currentHouseId = HouseId{};
        hh1.petId = pet;
        ws.household.Write().UpsertChildState(hh1);

        ChildRecord c4{};
        c4.childId = adultChild;
        c4.guardians.Assign(ActorId::Player(), spouse);
        c4.stage = ChildStage::Adult;
        ws.lineage.Write().UpsertChild(c4);

        AdultChildTravelRecord tr{};
        tr.targetMarkerId = travelMarker;
        ws.household.Write().UpsertAdultChildTravel(adultChild, tr);
    });

    const auto snap1 = store.Snapshot();
    const auto tPlan = ComputeActorPackagePlan(*snap1, RefId{ spouse.raw() });
    MARAS_CHECK_EQ(tPlan.role, PackageRole::HousedTenant);
    MARAS_CHECK_EQ(tPlan.isTenant, true);
    MARAS_CHECK_EQ(tPlan.homeSandboxTargetId, spouseHomeMarker);
    MARAS_CHECK(tPlan.childFollowTargetId.IsNull());

    const auto pPlan = ComputeActorPackagePlan(*snap1, pet);
    MARAS_CHECK_EQ(pPlan.role, PackageRole::PetOrCritter);
    MARAS_CHECK_EQ(pPlan.homeSandboxTargetId, RefId{ unhousedChild.raw() });

    const auto chPlan = ComputeActorPackagePlan(*snap1, RefId{ unhousedChild.raw() }, RefId{ spouse.raw() });
    MARAS_CHECK_EQ(chPlan.role, PackageRole::FollowParentChild);
    MARAS_CHECK(chPlan.homeSandboxTargetId.IsNull());
    MARAS_CHECK_EQ(chPlan.childFollowTargetId, RefId{ spouse.raw() });

    const auto a4Plan = ComputeActorPackagePlan(*snap1, RefId{ adultChild.raw() });
    MARAS_CHECK_EQ(a4Plan.role, PackageRole::AdultChildTravel);
    MARAS_CHECK_EQ(a4Plan.childFollowTargetId, travelMarker);

    // StayAtHome=false keeps 0x6B target while clearing package override role to None
    store.Modify([&](WorldState& ws) {
        ws.household.Write().SetTenantStayAtHome(spouse.raw(), false);
    });
    const auto noStayPlan = ComputeActorPackagePlan(*store.Snapshot(), RefId{ spouse.raw() });
    MARAS_CHECK_EQ(noStayPlan.role, PackageRole::None);
    MARAS_CHECK_EQ(noStayPlan.homeSandboxTargetId, spouseHomeMarker);

    // Evicting spouse clears both role AND homeSandboxTargetId (0x6B)
    store.Modify([&](WorldState& ws) {
        ws.household.Write().EvictTenant(spouse.raw());
    });
    const auto evictedPlan = ComputeActorPackagePlan(*store.Snapshot(), RefId{ spouse.raw() });
    MARAS_CHECK_EQ(evictedPlan.role, PackageRole::None);
    MARAS_CHECK_EQ(evictedPlan.isTenant, false);
    MARAS_CHECK(evictedPlan.homeSandboxTargetId.IsNull());

    // Marrying Stage 4 Adult Child to Player suppresses AdultChildTravel
    store.Modify([&](WorldState& ws) {
        AddRelationship(ws, adultChild.AsActorId(), ActorId::Player(), RelationshipStatus::Married);
    });
    const auto marriedAdultChildPlan = ComputeActorPackagePlan(*store.Snapshot(), RefId{ adultChild.raw() });
    MARAS_CHECK_EQ(marriedAdultChildPlan.role, PackageRole::None);
    MARAS_CHECK(marriedAdultChildPlan.childFollowTargetId.IsNull());
}

MARAS_TEST(Reconcilers, BuffMultipliersAndFormulas_SingleVsRankedSpousesAndNegativeValues) {
    WorldStore store{};
    constexpr ActorId spouse1{ 0x0001E001u };
    constexpr ActorId spouse2{ 0x0001E002u };
    constexpr ActorId deadSpouse{ 0x0001E003u };
    constexpr ActorId profilelessSpouse{ 0x0001E004u };

    // Single living spouse -> 2.0x rank multiplier * 1.25x affection (>=75) = 2.5x
    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(spouse1.raw(), SocialClass::Nobles, SkillType::Craftsman));
        AddRelationship(ws, spouse1, ActorId::Player(), RelationshipStatus::Married);
        ws.hierarchy.Write().OnSpouseAdded(spouse1);
        ws.affection.Write().SetPermanent(spouse1, 80);

        // Married spouse without an ActorProfileRecord must not count or dilute rank multipliers
        AddRelationship(ws, profilelessSpouse, ActorId::Player(), RelationshipStatus::Married);
        ws.affection.Write().SetPermanent(profilelessSpouse, 80);

        // Deceased spouse must not dilute spouseCount or contribute buffs
        DeceasedActorRecord dec{};
        dec.baseId = BaseId{ deadSpouse.raw() };
        dec.refId = RefId{ deadSpouse.raw() };
        ws.deceased.Write().UpsertRecord(dec);
    });

    const std::vector<RefId> teammates{ RefId{ spouse1.raw() } };
    const auto b1 = ComputeBuffMultipliers(*store.Snapshot(), teammates);
    MARAS_CHECK_EQ(b1.livingMarriedSpouseCount, 1);
    MARAS_CHECK(ApproxEq(b1.permanentSocialClassMultipliers[static_cast<std::size_t>(SocialClass::Nobles)], 2.5f));
    MARAS_CHECK(ApproxEq(b1.followerSkillTypeMultipliers[static_cast<std::size_t>(SkillType::Craftsman)], 1.25f));

    // Add second living spouse (rank 1, affection 55 -> 1.0x):
    // spouse1 (rank 0): 1.0 * 1.25 = 1.25; spouse2 (rank 1): 0.5 * 1.0 = 0.5
    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(spouse2.raw(), SocialClass::Religious, SkillType::Warrior));
        AddRelationship(ws, spouse2, ActorId::Player(), RelationshipStatus::Married);
        ws.hierarchy.Write().OnSpouseAdded(spouse2);
        ws.affection.Write().SetPermanent(spouse2, 55);
    });
    const auto b2 = ComputeBuffMultipliers(*store.Snapshot(), teammates);
    MARAS_CHECK_EQ(b2.livingMarriedSpouseCount, 2);
    MARAS_CHECK(ApproxEq(b2.permanentSocialClassMultipliers[static_cast<std::size_t>(SocialClass::Nobles)], 1.25f));
    MARAS_CHECK(ApproxEq(b2.permanentSocialClassMultipliers[static_cast<std::size_t>(SocialClass::Religious)], 0.5f));

    // "spell" takes positive magnitude via std::abs (Skyrim Effect::SetMagnitude clamps negatives);
    // "add" preserves signed scaling.
    const auto noblesSpell = ComputeBonusTargetValue("spell", -5.0f, 1.25f);
    MARAS_CHECK_EQ(noblesSpell.isActive, true);
    MARAS_CHECK(ApproxEq(noblesSpell.engineTargetValue, 6.25f));

    const auto signedAdd = ComputeBonusTargetValue("add", -5.0f, 1.25f);
    MARAS_CHECK_EQ(signedAdd.isActive, true);
    MARAS_CHECK(ApproxEq(signedAdd.engineTargetValue, -6.25f));

    const auto craftsmanMult = ComputeBonusTargetValue("multiply", -5.0f, 1.25f);
    MARAS_CHECK_EQ(craftsmanMult.isActive, true);
    MARAS_CHECK(ApproxEq(craftsmanMult.engineTargetValue, 1.0f + (-0.05f * 1.25f)));

    const auto rulersMult = ComputeBonusTargetValue("multiply", 1.0f, 2.0f);
    MARAS_CHECK_EQ(rulersMult.isActive, true);
    MARAS_CHECK(ApproxEq(rulersMult.engineTargetValue, 1.02f));

    const auto inactiveMult = ComputeBonusTargetValue("multiply", 5.0f, 0.0f);
    MARAS_CHECK_EQ(inactiveMult.isActive, false);
    MARAS_CHECK(ApproxEq(inactiveMult.engineTargetValue, 1.0f));
}

MARAS_TEST(Reconcilers, FreshAndRevertedWorldStoreDefaultsPolygamyFalse) {
    WorldStore store{};
    MARAS_CHECK_EQ(store.Snapshot()->settings->enablePlayerPolygamy, false);
    MARAS_CHECK_EQ(ComputeDesiredGlobals(*store.Snapshot(), true).enablePolygamy, false);

    store.Modify([](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = true;
    });
    MARAS_CHECK_EQ(store.Snapshot()->settings->enablePlayerPolygamy, true);
    MARAS_CHECK_EQ(ComputeDesiredGlobals(*store.Snapshot(), true).enablePolygamy, true);

    store.Revert();
    MARAS_CHECK_EQ(store.Snapshot()->settings->enablePlayerPolygamy, false);
    MARAS_CHECK_EQ(ComputeDesiredGlobals(*store.Snapshot(), true).enablePolygamy, false);
}

MARAS_TEST(Reconcilers, ClosedMainPairRankComposesWithNoAiDialogueChances) {
    WorldStore store{};
    constexpr ActorId spouseA{ 0x0001A101u };
    constexpr ActorId unattachedB{ 0x0001A102u };
    constexpr ActorId openSpouseC{ 0x0001A103u };
    constexpr ActorId npcHusbD{ 0x0001A104u };
    constexpr ActorId npcWifeE{ 0x0001A105u };

    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        for (ActorId id : { spouseA, unattachedB, openSpouseC, npcHusbD, npcWifeE }) {
            ws.actors.Write().Upsert(MakeAdultProfile(id.raw(), SocialClass::Working, SkillType::Warrior, Temperament::Humble));
        }
        AddRelationship(ws, ActorId::Player(), spouseA, RelationshipStatus::Married, /*isOpen=*/false);
        AddRelationship(ws, npcHusbD, npcWifeE, RelationshipStatus::Married, /*isOpen=*/false);
    });

    {
        const auto snap = store.Snapshot();
        const bool playerHasClosed = (ComputePlayerFactionPlan(*snap).canFormNonCheatingMainRank == std::int8_t{ 0 });
        const bool unattachedClosed = (ComputeAdultActorFactionPlan(*snap, unattachedB).canFormNonCheatingMainRank == std::int8_t{ 0 });
        const bool npcMarriedClosed = (ComputeAdultActorFactionPlan(*snap, npcHusbD).canFormNonCheatingMainRank == std::int8_t{ 0 });
        MARAS_CHECK(playerHasClosed);
        MARAS_CHECK(!unattachedClosed);
        MARAS_CHECK(npcMarriedClosed);

        const auto vsUnattached = MARAS::Domain::Relationships::ComputeNoAiDialogueChances(
            0.5f, false, RelationshipStatus::Candidate, unattachedClosed, playerHasClosed);
        MARAS_CHECK(std::fabs(vsUnattached.marriageChance - 30.0f) < 1e-4f);
    }

    // With polygamy enabled and open relationship on openSpouseC, both Player and openSpouseC/unattachedB can form main rank == 1
    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = true;
        ws.relationships.Write().GetOrCreateEdge(ActorId::Player(), spouseA).isOpen = true;
        AddRelationship(ws, ActorId::Player(), openSpouseC, RelationshipStatus::Dating, /*isOpen=*/true);
    });

    {
        const auto snap = store.Snapshot();
        const bool playerHasClosed = (ComputePlayerFactionPlan(*snap).canFormNonCheatingMainRank == std::int8_t{ 0 });
        const bool openCClosed = (ComputeAdultActorFactionPlan(*snap, openSpouseC).canFormNonCheatingMainRank == std::int8_t{ 0 });
        MARAS_CHECK(!playerHasClosed);
        MARAS_CHECK(!openCClosed);

        const auto vsOpenDating = MARAS::Domain::Relationships::ComputeNoAiDialogueChances(
            0.5f, false, RelationshipStatus::Dating, openCClosed, playerHasClosed);
        MARAS_CHECK(std::fabs(vsOpenDating.marriageChance - 75.0f) < 1e-4f);
    }
}

MARAS_TEST(Reconcilers, SpouseSharedHouseFactionPlanSync) {
    WorldStore store{};
    constexpr ActorId spouseSharing{ 0x0001A201u };
    constexpr ActorId spouseNotSharing{ 0x0001A202u };
    constexpr MARAS::State::CellId sharedCell{ 0x0003A001u };

    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(spouseSharing.raw()));
        ws.actors.Write().Upsert(MakeAdultProfile(spouseNotSharing.raw()));
        AddRelationship(ws, ActorId::Player(), spouseSharing, RelationshipStatus::Married);
        AddRelationship(ws, ActorId::Player(), spouseNotSharing, RelationshipStatus::Married);

        MARAS::State::SharedCellRecord rec{};
        rec.sharedWithPlayer = true;
        rec.sharingSpouses.insert(spouseSharing);
        ws.household.Write().UpsertSharedCell(sharedCell, std::move(rec));
    });

    {
        const auto snap = store.Snapshot();
        MARAS_CHECK_EQ(ComputeAdultActorFactionPlan(*snap, spouseSharing).spouseSharedHouseRank,
                       std::optional<std::int8_t>{ std::int8_t{ 0 } });
        MARAS_CHECK(!ComputeAdultActorFactionPlan(*snap, spouseNotSharing).spouseSharedHouseRank.has_value());
    }

    store.Modify([&](WorldState& ws) {
        MARAS_CHECK(ws.household.Write().StopSharingForSpouse(spouseSharing));
    });

    {
        const auto snap = store.Snapshot();
        MARAS_CHECK(!ComputeAdultActorFactionPlan(*snap, spouseSharing).spouseSharedHouseRank.has_value());
    }
}

MARAS_TEST(Reconcilers, PlayerFamilyBlockedFactions_SuppressRomanceAndStripPotentialMarriage) {
    WorldStore store{};
    constexpr ChildId playerUnregChild{ 0xFF000601u };
    constexpr ChildId playerRegChild{ 0xFF000602u };
    constexpr ChildId npcOnlyChild{ 0xFF000603u };
    constexpr ActorId npcSpouseA{ 0x0001D001u };
    constexpr ActorId npcSpouseB{ 0x0001D002u };
    constexpr ActorId npcPartner{ 0x0001D003u };
    constexpr ActorId vanillaSibling{ 0x0001D004u };

    store.Modify([&](WorldState& ws) {
        ChildRecord c1{};
        c1.childId = playerUnregChild;
        c1.guardians.Assign(ActorId::Player(), npcSpouseA);
        c1.stage = ChildStage::Adult;
        ws.lineage.Write().UpsertChild(c1);

        ChildRecord c2{};
        c2.childId = playerRegChild;
        c2.bioMother = ActorId::Player();
        c2.stage = ChildStage::Adult;
        ws.lineage.Write().UpsertChild(c2);
        ws.actors.Write().Upsert(MakeAdultProfile(playerRegChild.raw(), SocialClass::Wealthy, SkillType::Mage, Temperament::Proud));
        ws.affection.Write().SetPermanent(playerRegChild.AsActorId(), 90);
        AddRelationship(ws, playerRegChild.AsActorId(), npcPartner, RelationshipStatus::Dating);

        ChildRecord c3{};
        c3.childId = npcOnlyChild;
        c3.guardians.Assign(npcSpouseA, npcSpouseB);
        c3.bioMother = npcSpouseA;
        c3.bioFather = npcSpouseB;
        c3.stage = ChildStage::Adult;
        ws.lineage.Write().UpsertChild(c3);
    });

    const auto snap = store.Snapshot();

    const auto unregPlan = ComputeAdultActorFactionPlan(*snap, playerUnregChild.AsActorId());
    MARAS_CHECK_EQ(unregPlan.isRelevantAdult, true);
    MARAS_CHECK_EQ(unregPlan.isBlockedPlayerFamily, true);
    MARAS_CHECK_EQ(unregPlan.playerFamilyBlockedRank, std::optional<std::int8_t>{ std::int8_t{ 0 } });
    MARAS_CHECK(!unregPlan.trackedRank.has_value());
    MARAS_CHECK_EQ(unregPlan.marriagePotentialOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(unregPlan.forceStripMarriagePotential, true);

    const auto regPlan = ComputeAdultActorFactionPlan(*snap, playerRegChild.AsActorId());
    MARAS_CHECK_EQ(regPlan.isBlockedPlayerFamily, true);
    MARAS_CHECK_EQ(regPlan.playerFamilyBlockedRank, std::optional<std::int8_t>{ std::int8_t{ 0 } });
    MARAS_CHECK_EQ(regPlan.socialClassRank, std::optional<std::int8_t>{ static_cast<std::int8_t>(SocialClass::Wealthy) });
    MARAS_CHECK_EQ(regPlan.skillTypeRank, std::optional<std::int8_t>{ static_cast<std::int8_t>(SkillType::Mage) });
    MARAS_CHECK_EQ(regPlan.temperamentRank, std::optional<std::int8_t>{ static_cast<std::int8_t>(Temperament::Proud) });
    MARAS_CHECK_EQ(regPlan.canFormNonCheatingMainRank, std::optional<std::int8_t>{ std::int8_t{ 0 } });
    MARAS_CHECK(!regPlan.trackedRank.has_value());
    MARAS_CHECK(!regPlan.affectionRank.has_value());
    MARAS_CHECK_EQ(regPlan.marriagePotentialOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(regPlan.forceStripMarriagePotential, true);
    MARAS_CHECK_EQ(regPlan.clearIgnoreProposeKeyword, false);

    const auto npcChildPlan = ComputeAdultActorFactionPlan(*snap, npcOnlyChild.AsActorId());
    MARAS_CHECK_EQ(npcChildPlan.isBlockedPlayerFamily, false);
    MARAS_CHECK(!npcChildPlan.playerFamilyBlockedRank.has_value());

    const auto engineFamilyPlan = ComputeAdultActorFactionPlan(*snap, vanillaSibling, {}, /*isBlockedPlayerFamily=*/true);
    MARAS_CHECK_EQ(engineFamilyPlan.isRelevantAdult, true);
    MARAS_CHECK_EQ(engineFamilyPlan.isBlockedPlayerFamily, true);
    MARAS_CHECK_EQ(engineFamilyPlan.playerFamilyBlockedRank, std::optional<std::int8_t>{ std::int8_t{ 0 } });
    MARAS_CHECK_EQ(engineFamilyPlan.marriagePotentialOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(engineFamilyPlan.forceStripMarriagePotential, true);
}

MARAS_TEST(Reconcilers, ExternalFactionWrite_PapyrusMarriagePolygamyAndFamilyGuards) {
    using MARAS::State::ClampPapyrusFactionRank;
    using MARAS::State::ComputeModdedFactionRank;
    using MARAS::State::EvaluateExternalFactionWrite;
    using MARAS::State::ExternalFactionWriteAction;
    using MARAS::State::kVanillaCourtingFactionRawId;
    using MARAS::State::kVanillaMarriageAskedFactionRawId;
    using MARAS::State::kVanillaPlayerMarriedFactionRawId;
    using MARAS::State::kVanillaPotentialMarriageFactionRawId;

    MARAS_CHECK_EQ(ClampPapyrusFactionRank(0), std::int8_t{ 0 });
    MARAS_CHECK_EQ(ClampPapyrusFactionRank(-1), std::int8_t{ -1 });
    MARAS_CHECK_EQ(ClampPapyrusFactionRank(200), std::int8_t{ 127 });
    MARAS_CHECK_EQ(ClampPapyrusFactionRank(-200), std::int8_t{ -128 });
    MARAS_CHECK_EQ(ComputeModdedFactionRank(true, 2, -5), std::int8_t{ -3 });
    MARAS_CHECK_EQ(ComputeModdedFactionRank(false, 0, 1), std::int8_t{ 1 });

    WorldStore store{};
    constexpr ActorId bride{ 0x0001A675u };
    constexpr ChildId playerChild{ 0xFF000701u };
    constexpr ActorId sibling{ 0x0001A68Bu };

    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        AddRelationship(ws, bride, ActorId::Player(), RelationshipStatus::Engaged);
        ChildRecord ch{};
        ch.childId = playerChild;
        ch.guardians.Assign(ActorId::Player(), bride);
        ch.stage = ChildStage::Adult;
        ws.lineage.Write().UpsertChild(ch);
    });

    auto snap = store.Snapshot();
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, bride, kVanillaPlayerMarriedFactionRawId, 0),
        ExternalFactionWriteAction::PromoteNpcToPlayerMarried);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, ActorId::Player(), kVanillaPlayerMarriedFactionRawId, 0),
        ExternalFactionWriteAction::Passthrough);

    store.Modify([&](WorldState& ws) {
        ws.relationships.Write().GetOrCreateEdge(bride, ActorId::Player()).status = RelationshipStatus::Married;
        ws.settings.Write().enablePlayerPolygamy = true;
    });
    snap = store.Snapshot();
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, bride, kVanillaPlayerMarriedFactionRawId, 0),
        ExternalFactionWriteAction::Passthrough);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, bride, kVanillaPlayerMarriedFactionRawId, -1),
        ExternalFactionWriteAction::DemoteNpcToPlayerDivorced);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, ActorId::Player(), kVanillaPlayerMarriedFactionRawId, 0),
        ExternalFactionWriteAction::BlockPolygamyPlayerMarried);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, ActorId::Player(), kVanillaPlayerMarriedFactionRawId, -1),
        ExternalFactionWriteAction::Passthrough);

    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, playerChild.AsActorId(), kVanillaPotentialMarriageFactionRawId, 0),
        ExternalFactionWriteAction::BlockPlayerFamilyCourtship);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, playerChild.AsActorId(), kVanillaMarriageAskedFactionRawId, 0),
        ExternalFactionWriteAction::BlockPlayerFamilyCourtship);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, sibling, kVanillaCourtingFactionRawId, 0, /*isBlockedEngineFamily=*/true),
        ExternalFactionWriteAction::BlockPlayerFamilyCourtship);
    MARAS_CHECK_EQ(
        EvaluateExternalFactionWrite(*snap, bride, 0x00012345u, 0),
        ExternalFactionWriteAction::Passthrough);
}

MARAS_TEST(Reconcilers, AdultActorFactions_PotentialMarriageAndIgnoreProposalLifecycle) {
    WorldStore store{};
    constexpr ActorId npcA{ 0x0001E001u };
    constexpr ActorId npcB{ 0x0001E002u };
    constexpr ActorId npcC{ 0x0001E003u };
    constexpr ActorId directFiance{ 0x0001E004u };

    store.Modify([&](WorldState& ws) {
        ws.settings.Write().enablePlayerPolygamy = false;
        ws.actors.Write().Upsert(MakeAdultProfile(npcA.raw(), SocialClass::Working, SkillType::Warrior, Temperament::Proud));
        ws.actors.Write().Upsert(MakeAdultProfile(npcB.raw(), SocialClass::Working, SkillType::Warrior, Temperament::Proud));
        ws.actors.Write().Upsert(MakeAdultProfile(npcC.raw(), SocialClass::Working, SkillType::Warrior, Temperament::Proud));
        ws.actors.Write().Upsert(MakeAdultProfile(directFiance.raw(), SocialClass::Middle, SkillType::Orator, Temperament::Jealous));
        AddRelationship(ws, npcA, npcB, RelationshipStatus::Dating);
        AddRelationship(ws, npcB, npcC, RelationshipStatus::Married);
        AddRelationship(ws, npcB, ActorId::Player(), RelationshipStatus::Dating);
        AddRelationship(ws, directFiance, ActorId::Player(), RelationshipStatus::Engaged);
    });

    const auto npcOnlyPlan = ComputeAdultActorFactionPlan(*store.Snapshot(), npcA);
    MARAS_CHECK_EQ(npcOnlyPlan.marriagePotentialOp, FactionMembershipOp::LeaveUntouched);
    MARAS_CHECK_EQ(npcOnlyPlan.clearIgnoreProposeKeyword, false);

    const auto marriedNpcPlan = ComputeAdultActorFactionPlan(*store.Snapshot(), npcB);
    MARAS_CHECK_EQ(marriedNpcPlan.marriagePotentialOp, FactionMembershipOp::EnsureAbsent);
    MARAS_CHECK_EQ(marriedNpcPlan.clearIgnoreProposeKeyword, false);

    const auto directPlan = ComputeAdultActorFactionPlan(*store.Snapshot(), directFiance);
    MARAS_CHECK_EQ(directPlan.marriagePotentialOp, FactionMembershipOp::EnsurePresent);
    MARAS_CHECK_EQ(directPlan.clearIgnoreProposeKeyword, true);

    for (const auto st : { RelationshipStatus::Candidate, RelationshipStatus::Dating, RelationshipStatus::Lover,
                           RelationshipStatus::ExDating, RelationshipStatus::ExLover }) {
        store.Modify([&](WorldState& ws) {
            ws.relationships.Write().GetOrCreateEdge(npcA, ActorId::Player()).status = st;
        });
        const auto plan = ComputeAdultActorFactionPlan(*store.Snapshot(), npcA);
        MARAS_CHECK_EQ(plan.marriagePotentialOp, FactionMembershipOp::EnsurePresent);
        MARAS_CHECK_EQ(plan.clearIgnoreProposeKeyword, false);
    }

    for (const auto st : { RelationshipStatus::Engaged, RelationshipStatus::Married,
                           RelationshipStatus::Divorced, RelationshipStatus::Jilted }) {
        store.Modify([&](WorldState& ws) {
            ws.relationships.Write().GetOrCreateEdge(npcA, ActorId::Player()).status = st;
        });
        const auto plan = ComputeAdultActorFactionPlan(*store.Snapshot(), npcA);
        MARAS_CHECK_EQ(plan.marriagePotentialOp, FactionMembershipOp::EnsurePresent);
        MARAS_CHECK_EQ(plan.clearIgnoreProposeKeyword, true);
    }
}

MARAS_TEST(Reconcilers, AdultActorFactions_CourtingFactionLeftUntouchedWhenEngagedAndClearedOtherwise) {
    WorldStore store{};
    constexpr ActorId npc{ 0x0001E101u };

    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(npc.raw()));
        AddRelationship(ws, npc, ActorId::Player(), RelationshipStatus::Engaged);
    });

    const auto engagedPlan = ComputeAdultActorFactionPlan(*store.Snapshot(), npc);
    MARAS_CHECK_EQ(engagedPlan.marriageAskedOp, FactionMembershipOp::EnsurePresent);
    MARAS_CHECK_EQ(engagedPlan.courtingOp, FactionMembershipOp::LeaveUntouched);

    for (const auto st : { RelationshipStatus::Candidate, RelationshipStatus::Dating,
                           RelationshipStatus::Lover, RelationshipStatus::Married,
                           RelationshipStatus::Divorced, RelationshipStatus::Jilted,
                           RelationshipStatus::ExDating, RelationshipStatus::ExLover }) {
        store.Modify([&](WorldState& ws) {
            ws.relationships.Write().GetOrCreateEdge(npc, ActorId::Player()).status = st;
        });
        const auto plan = ComputeAdultActorFactionPlan(*store.Snapshot(), npc);
        MARAS_CHECK_EQ(plan.courtingOp, FactionMembershipOp::EnsureAbsent);
    }
}
