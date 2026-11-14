#include "TestHarness.h"

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

    // Single living spouse -> 2.0x rank multiplier * 1.25x affection (>=75) = 2.5x
    store.Modify([&](WorldState& ws) {
        ws.actors.Write().Upsert(MakeAdultProfile(spouse1.raw(), SocialClass::Nobles, SkillType::Craftsman));
        AddRelationship(ws, spouse1, ActorId::Player(), RelationshipStatus::Married);
        ws.hierarchy.Write().OnSpouseAdded(spouse1);
        ws.affection.Write().SetPermanent(spouse1, 80);

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

    // Exact bonus.json negative-value parity without std::abs (TTM_ServiceBuff.psc match)
    const auto noblesSpell = ComputeBonusTargetValue("spell", -5.0f, 1.25f);
    MARAS_CHECK_EQ(noblesSpell.isActive, true);
    MARAS_CHECK(ApproxEq(noblesSpell.engineTargetValue, -6.25f));

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
