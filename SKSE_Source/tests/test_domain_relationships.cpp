#include "TestHarness.h"

#include "maras/domain/relationships/AffectionRules.h"
#include "maras/domain/relationships/BonusesDomain.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/domain/relationships/NpcTypeRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/domain/relationships/SpouseHierarchyRules.h"
#include "maras/foundation/IRng.h"
#include "maras/state/WorldState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
        using namespace MARAS::Domain::Relationships;

        struct StubMetadataProvider final : public IActorMetadataProvider {
            std::unordered_map<ActorId, std::string> names{};
            std::unordered_map<ActorId, bool> alive{};

            std::string GetActorName(ActorId actorId) const override {
                if (const auto it = names.find(actorId); it != names.end()) {
                    return it->second;
                }
                return IActorMetadataProvider::GetActorName(actorId);
            }

            bool IsAliveAndValid(ActorId actorId) const noexcept override {
                if (const auto it = alive.find(actorId); it != alive.end()) {
                    return it->second;
                }
                return true;
            }
        };

        void RegisterAdult(WorldState& ws, ActorId id, Temperament temp = Temperament::Proud) {
            ActorProfileRecord p{};
            p.refId = id.AsRefId();
            p.baseId = BaseId{ id.raw() };
            p.temperament = temp;
            p.isRegisteredAdult = true;
            ws.actors.Write().Upsert(std::move(p));
        }

    }  // namespace

    MARAS_TEST(DomainRelationships, FamilyGuardsAndRelationshipFormationRules) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId mother{ 0x00011001u };
        const ActorId father{ 0x00011002u };
        const ActorId thirdNpc{ 0x00011003u };
        const ChildId childA{ 0x00011010u };
        const ChildId childB{ 0x00011011u };

        meta.names[mother] = "Ysolda";
        meta.names[father] = "Mikael";
        meta.names[thirdNpc] = "Carlotta";
        meta.names[ActorId{ childA.raw() }] = "Lucia";
        meta.names[ActorId{ childB.raw() }] = "Sofie";

        RegisterAdult(ws, mother);
        RegisterAdult(ws, father);
        RegisterAdult(ws, thirdNpc);

        ChildRecord c1{};
        c1.childId = childA;
        c1.stage = ChildStage::Adult;
        c1.bioMother = mother;
        c1.bioFather = father;
        c1.guardians.Assign(mother, father);
        ws.lineage.Write().UpsertChild(c1);

        ChildRecord c2{};
        c2.childId = childB;
        c2.stage = ChildStage::Adult;
        c2.bioMother = mother;
        c2.bioFather = thirdNpc;
        c2.guardians.Assign(mother, ActorId{});
        ws.lineage.Write().UpsertChild(c2);

        const auto snap0 = ws.Snapshot();
        std::string reason;
        // Self-relationship blocked
        MARAS_CHECK(!CanFormRelationship(snap0, meta, mother, mother, RelationshipStatus::Dating, {}, &reason));
        // Child flag blocked
        RelationshipCheckContext childCtx{};
        childCtx.isChildA = true;
        MARAS_CHECK(!CanFormRelationship(snap0, meta, mother, thirdNpc, RelationshipStatus::Dating, childCtx, &reason));
        // Engine family blocked
        RelationshipCheckContext engineFamCtx{};
        engineFamCtx.areEngineFamily = true;
        engineFamCtx.engineFamilyReason = "Same clan.";
        MARAS_CHECK(!CanFormRelationship(snap0, meta, father, thirdNpc, RelationshipStatus::Married, engineFamCtx, &reason));
        MARAS_CHECK_EQ(reason, "Same clan.");

        // MARAS lineage parent-child and sibling checks
        MARAS_CHECK(AreMarasFamilyMembers(snap0, meta, mother, ActorId{ childA.raw() }, &reason));
        MARAS_CHECK(AreMarasFamilyMembers(snap0, meta, ActorId{ childA.raw() }, ActorId{ childB.raw() }, &reason));
        MARAS_CHECK(!CanFormRelationship(snap0, meta, ActorId{ childA.raw() }, ActorId{ childB.raw() }, RelationshipStatus::Lover, {}, &reason));
        MARAS_CHECK(CanFormRelationship(snap0, meta, ActorId::Player(), mother, RelationshipStatus::Dating, {}, &reason));

        // Monogamy block vs polygamy allowance for Player
        ws.settings.Write().enablePlayerPolygamy = false;
        const auto m1 = ApplyRelationshipTransition(
            ws, ActorId::Player(), mother, RelationshipStatus::Married, 10.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(m1.allowed);
        MARAS_CHECK_EQ(m1.offlineSyncedAffection, 100);
        MARAS_CHECK(!IsAvailableForStatus(ws.Snapshot(), ActorId::Player(), RelationshipStatus::Dating));
        MARAS_CHECK(!CanFormRelationship(ws.Snapshot(), meta, ActorId::Player(), thirdNpc, RelationshipStatus::Dating, {}, &reason));

        // Auto-open on new main partner when an existing open main partner exists
        ws.settings.Write().enablePlayerPolygamy = true;
        (void)ws.SetPairOpen(ActorId::Player(), mother, true, 11.0f);
        MARAS_CHECK(IsAvailableForStatus(ws.Snapshot(), ActorId::Player(), RelationshipStatus::Married));
        const auto m2 = ApplyRelationshipTransition(
            ws, ActorId::Player(), thirdNpc, RelationshipStatus::Married, 12.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(m2.allowed);
        MARAS_CHECK(m2.appliedAutoOpen);
        MARAS_CHECK(m2.reconcilePlayerBuffs);
        MARAS_CHECK(ws.relationships.Read().FindEdge(ActorId::Player(), thirdNpc)->isOpen);

        // Divorce resets isOpen & offline affection
        const auto div = ApplyRelationshipTransition(
            ws, ActorId::Player(), thirdNpc, RelationshipStatus::Divorced, 15.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(div.allowed);
        MARAS_CHECK(div.resetOpenOnBreakup);
        MARAS_CHECK_EQ(div.offlineSyncedAffection, 0);
        MARAS_CHECK(!ws.relationships.Read().FindEdge(ActorId::Player(), thirdNpc)->isOpen);
    }

    MARAS_TEST(DomainRelationships, AutoMarriageBackfillAndReadyCheck) {
        WorldState ws{};
        RelationshipEdgeRecord missingTsEdge{};
        const ActorId npcA{ 0x00012001u };
        const ActorId npcB{ 0x00012002u };
        missingTsEdge.pair = CanonicalPairKey(npcA, npcB);
        missingTsEdge.status = RelationshipStatus::Engaged;
        ws.relationships.Write().UpsertEdge(missingTsEdge);

        const auto pre = CollectAutoMarriageCandidates(ws.Snapshot(), 10.0f, 5.0f);
        MARAS_REQUIRE_EQ(pre.size(), std::size_t{ 1 });
        MARAS_CHECK(pre[0].needsTimestampBackfill);
        MARAS_CHECK(!pre[0].readyForAutoMarriage);

        BackfillMissingEngagementTimestamps(ws, std::array<CanonicalPairKey, 1>{ missingTsEdge.pair }, 10.0f);
        MARAS_CHECK(CollectAutoMarriageCandidates(ws.Snapshot(), 14.0f, 5.0f).empty());

        const auto ready = CollectAutoMarriageCandidates(ws.Snapshot(), 16.0f, 5.0f);
        MARAS_REQUIRE_EQ(ready.size(), std::size_t{ 1 });
        MARAS_CHECK(ready[0].readyForAutoMarriage);
    }

    MARAS_TEST(DomainRelationships, InfidelityDualStageAndOpenCloseResolution) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId spouse{ 0x00013001u };
        const ActorId paramour{ 0x00013002u };
        const ActorId npcSpouse{ 0x00013003u };
        const ActorId npcParamour{ 0x00013004u };
        RegisterAdult(ws, spouse);
        RegisterAdult(ws, paramour);
        RegisterAdult(ws, npcSpouse);
        RegisterAdult(ws, npcParamour);

        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), spouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, npcSpouse, npcParamour, RelationshipStatus::Married, 1.0f, npcSpouse, meta, false, false);

        // Set pending ConfessTalking intent before recording physical cheating to test upgrade
        ws.relationships.Write().SetPendingIntent(spouse, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessTalking,
            .createdGameDay = 2.0f,
            .relatedLover = paramour
        });

        // Player-victim affair retains Talking stage when passed Talking
        const auto talkIncident = ApplyRecordCheating(
            ws, spouse, ActorId::Player(), paramour, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 3.0f);
        MARAS_CHECK(talkIncident.recorded);
        MARAS_CHECK_EQ(talkIncident.recordedStage, AffairStage::Talking);
        MARAS_CHECK(IsActorCheating(ws.Snapshot(), spouse));
        MARAS_CHECK(IsRelationshipCheating(ws.Snapshot(), spouse, paramour));

        // Recording physical upgrades pending ConfessTalking intent to ConfessPhysical
        const auto physIncident = ApplyRecordCheating(
            ws, spouse, ActorId::Player(), paramour, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 4.0f);
        MARAS_CHECK(physIncident.recorded);
        MARAS_CHECK_EQ(physIncident.recordedStage, AffairStage::Physical);
        MARAS_CHECK(physIncident.intentUpgraded);

        // Non-player victim forces AffairStage::Physical even if Talking was passed
        const auto npcIncident = ApplyRecordCheating(
            ws, npcSpouse, npcParamour, paramour, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 4.0f);
        MARAS_CHECK(npcIncident.recorded);
        MARAS_CHECK_EQ(npcIncident.recordedStage, AffairStage::Physical);

        // Co-spouses of player are not flagged as cheating with one another when polygamy active
        const ActorId coSpouse{ 0x00013007u };
        RegisterAdult(ws, coSpouse);
        ws.settings.Write().enablePlayerPolygamy = true;
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), coSpouse, RelationshipStatus::Married, 5.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(!IsRelationshipCheating(ws.Snapshot(), spouse, coSpouse));

        // Discovery clears secret & marks victims, resolution breaks paramour on Confronted
        const auto disc = ApplyAffairDiscoveryOrResolution(ws, spouse, paramour, "Exposed", ActorId::Player());
        MARAS_CHECK(disc.updated);
        MARAS_CHECK(disc.isDiscovery);
        MARAS_CHECK(!disc.events.empty());

        const auto conf = ApplyAffairDiscoveryOrResolution(ws, spouse, paramour, "Confronted", ActorId::Player());
        MARAS_CHECK(conf.updated);
        MARAS_CHECK_EQ(conf.resolution, AffairResolution::Confronted);
        MARAS_CHECK(std::find(conf.partnersToBreak.begin(), conf.partnersToBreak.end(), paramour) != conf.partnersToBreak.end());

        // Set pair open collects allowedCheatersToRefresh for ongoing player affairs
        const ActorId openSpouse{ 0x00013005u };
        const ActorId openLover{ 0x00013006u };
        RegisterAdult(ws, openSpouse);
        RegisterAdult(ws, openLover);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), openSpouse, RelationshipStatus::Married, 6.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRecordCheating(
            ws, openSpouse, ActorId::Player(), openLover, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 7.0f);
        const auto openOut = ApplySetPairOpen(ws, ActorId::Player(), openSpouse, true, 8.0f);
        MARAS_CHECK(openOut.changed);
        MARAS_CHECK(!openOut.allowedCheatersToRefresh.empty());
    }

    MARAS_TEST(DomainRelationships, AffectionDecayAndDailyAccumulation) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId romanticSpouse{ 0x00014001u };
        const ActorId indepSpouse{ 0x00014002u };
        RegisterAdult(ws, romanticSpouse, Temperament::Romantic);
        RegisterAdult(ws, indepSpouse, Temperament::Independent);

        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), romanticSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), indepSpouse, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta, false, false);

        MARAS_CHECK_EQ(GetAffectionThreshold(80), "happy");
        MARAS_CHECK_EQ(GetAffectionThreshold(60), "content");
        MARAS_CHECK_EQ(GetAffectionThreshold(30), "troubled");
        MARAS_CHECK_EQ(GetAffectionThreshold(10), "estranged");

        const auto snap = ws.Snapshot();
        MARAS_CHECK(std::fabs(CalculateLonelinessDecay(snap, romanticSpouse, true, 2) - 6.0f) < 1e-4f);
        const float romanticDecay = CalculateLonelinessDecay(snap, romanticSpouse, false, 2);
        const float loverDecay = CalculateLonelinessDecay(snap, indepSpouse, false, 2);
        MARAS_CHECK(romanticDecay < 0.0f);
        MARAS_CHECK(std::fabs(romanticDecay) > std::fabs(loverDecay));

        ws.affection.Write().SetPermanent(romanticSpouse, 50);
        ws.affection.Write().SetMinMaxForType("gift", -10, 5);
        ws.affection.Write().SetDailyAccumulation(romanticSpouse, "gift", 20.0f);  // clamped to +5
        ws.affection.Write().SetDailyAccumulation(romanticSpouse, "talk", 3.0f);   // unclamped +3
        const auto changes = ApplyDailyAffections(ws, meta);
        MARAS_REQUIRE_EQ(changes.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(changes[0].oldScore, 50);
        MARAS_CHECK_EQ(changes[0].newScore, 58);
        MARAS_CHECK(changes[0].marriedSpouseChanged);
        MARAS_CHECK(ws.affection.Read().AllDaily().empty());
    }

    MARAS_TEST(DomainRelationships, HierarchyRankingDifficultyClassificationAndBonuses) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId s1{ 0x00015001u };
        const ActorId s2{ 0x00015002u };
        const ActorId s3{ 0x00015003u };
        ws.settings.Write().enablePlayerPolygamy = true;
        for (ActorId id : { s1, s2, s3 }) {
            RegisterAdult(ws, id);
            (void)ApplyRelationshipTransition(
                ws, ActorId::Player(), id, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        }

        const auto a1 = ApplyOnSpouseAdded(ws, s1);
        const auto a2 = ApplyOnSpouseAdded(ws, s2);
        const auto a3 = ApplyOnSpouseAdded(ws, s3);
        MARAS_CHECK_EQ(a1.newSlots[0], s1);
        MARAS_CHECK_EQ(a2.newSlots[1], s2);
        MARAS_CHECK_EQ(a3.newSlots[2], s3);

        // Promote s3 into slot 0 -> swaps with s1
        const auto promote = ApplySetHierarchyRank(ws, s3, 0);
        MARAS_CHECK(promote.validRequest);
        MARAS_CHECK(promote.slotsChanged);
        MARAS_CHECK_EQ(ws.hierarchy.Read().GetRank(s3), 0);
        MARAS_CHECK_EQ(ws.hierarchy.Read().GetRank(s1), 2);

        // Removing middle spouse compacts gap cleanly
        DeceasedActorRecord s1Dead{};
        s1Dead.refId = s1.AsRefId();
        s1Dead.baseId = BaseId{ s1.raw() };
        s1Dead.deathTimestamp = 5.0f;
        ws.deceased.Write().UpsertRecord(std::move(s1Dead));
        const auto removed = ApplyOnSpouseRemoved(ws, s1);
        MARAS_CHECK(removed.slotsChanged);
        MARAS_CHECK_EQ(ws.hierarchy.Read().GetRank(s2), 1);
        MARAS_CHECK_EQ(ws.hierarchy.Read().GetRank(s3), 0);

        // Marriage difficulty config & probability curve
        const auto diffCfg = MarriageDifficultyConfig::CreateDefault();
        MarriageDifficultyInputs highPrestige{};
        highPrestige.npcSocialClass = SocialClass::Rulers;
        highPrestige.isDragonborn = true;
        highPrestige.thaneHoldsCount = 5;
        highPrestige.isGuildLeader = true;
        highPrestige.isDating = true;
        highPrestige.permanentAffection = 90;
        const float prestige = CalculatePlayerPrestige(diffCfg, highPrestige);
        MARAS_CHECK(prestige > 40.0f);
        const auto resHigh = CalculateMarriageSuccessChance(diffCfg, highPrestige);

        MarriageDifficultyInputs lowInputs = highPrestige;
        lowInputs.isDragonborn = false;
        lowInputs.thaneHoldsCount = 0;
        lowInputs.isGuildLeader = false;
        lowInputs.isJilted = true;
        lowInputs.permanentAffection = 10;
        const auto resLow = CalculateMarriageSuccessChance(diffCfg, lowInputs);
        MARAS_CHECK(resHigh.chance > resLow.chance);
        MARAS_CHECK(resHigh.chance > 0.0f && resHigh.chance < 1.0f);

        // NpcTypeRules matrix & skill detection
        MARAS_CHECK_EQ(ComputeTemperament(SocialClass::Nobles, SkillType::Mage), Temperament::Romantic);
        std::array<float, 18> skills{};
        skills[14] = 85.0f;  // Destruction -> Mage
        MARAS_CHECK_EQ(ComputeSkillTypeFromSkills(skills), SkillType::Mage);
        skills[14] = 10.0f;
        skills[11] = 90.0f;  // Speech -> Orator
        MARAS_CHECK_EQ(ComputeSkillTypeFromSkills(skills), SkillType::Orator);

        // BonusCatalog
        BonusCatalog catalog{};
        catalog.SetCategories({
            { "warrior", { BonusEntry{ .name = "Brawn", .description = "+HP", .type = "spell", .unit = "%", .value = 5.0f } } }
        });
        const auto* entry = catalog.GetBonusEntry("warrior", 0);
        MARAS_REQUIRE(entry != nullptr);
        MARAS_CHECK_EQ(entry->name, "Brawn");
        MARAS_CHECK(catalog.GetBonusEntry("warrior", 9) == nullptr);
    }

    MARAS_TEST(DomainRelationships, InfidelityClosingOpenPairBreaksSideLoversAndClearsIntents) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId a{ 0x00016001u };
        const ActorId b{ 0x00016002u };
        const ActorId c{ 0x00016003u };
        const ActorId d{ 0x00016004u };
        const ActorId sideLover{ 0x00016005u };
        for (ActorId id : { a, b, c, d, sideLover }) {
            RegisterAdult(ws, id);
        }

        // Non-main Lover pair cannot be opened/closed
        (void)ApplyRelationshipTransition(ws, c, sideLover, RelationshipStatus::Lover, 1.0f, c, meta, false, false);
        MARAS_CHECK(!CanOpenRelationship(ws.Snapshot(), c, sideLover));
        MARAS_CHECK(!CanCloseRelationship(ws.Snapshot(), c, sideLover));

        // Open Married NPC pair (a, b) with side lovers (a, c) & (b, d)
        (void)ApplyRelationshipTransition(ws, a, b, RelationshipStatus::Married, 1.0f, a, meta, false, false);
        MARAS_CHECK(CanOpenRelationship(ws.Snapshot(), a, b));
        const auto opened = ApplySetPairOpen(ws, a, b, true, 2.0f);
        MARAS_CHECK(opened.changed);
        MARAS_CHECK(!opened.wasOpen);
        MARAS_CHECK(CanCloseRelationship(ws.Snapshot(), a, b));

        (void)ApplyRelationshipTransition(ws, a, c, RelationshipStatus::Lover, 3.0f, a, meta, false, false);
        (void)ApplyRelationshipTransition(ws, b, d, RelationshipStatus::Lover, 3.0f, b, meta, false, false);
        ws.relationships.Write().SetPendingIntent(c, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessTalking,
            .createdGameDay = 4.0f,
            .relatedLover = a
        });

        const auto closed = ApplySetPairOpen(ws, a, b, false, 5.0f);
        MARAS_CHECK(closed.changed);
        MARAS_CHECK(closed.wasOpen);
        MARAS_CHECK_EQ(closed.pairStatus, RelationshipStatus::Married);
        MARAS_CHECK_EQ(closed.brokenLovers.size(), std::size_t{ 2 });
        MARAS_CHECK_EQ(ws.relationships.Read().GetStatus(a, c), RelationshipStatus::ExLover);
        MARAS_CHECK_EQ(ws.relationships.Read().GetStatus(b, d), RelationshipStatus::ExLover);
        MARAS_CHECK_EQ(ws.relationships.Read().GetPendingIntent(c).type, PartnerIntentType::None);
        MARAS_CHECK_EQ(closed.clearedIntents.size(), std::size_t{ 1 });

        // Player-partner SetPairOpen clears Player-partner SuggestClose intent directly
        const ActorId pSpouse{ 0x00016006u };
        RegisterAdult(ws, pSpouse);
        const auto mPl = ApplyRelationshipTransition(ws, ActorId::Player(), pSpouse, RelationshipStatus::Married, 6.0f, ActorId::Player(), meta, false, false);
        MARAS_REQUIRE(mPl.allowed);
        (void)ApplySetPairOpen(ws, ActorId::Player(), pSpouse, true, 6.0f);
        ws.relationships.Write().SetPendingIntent(pSpouse, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestClose,
            .createdGameDay = 6.5f
        });
        const auto pClosed = ApplySetPairOpen(ws, ActorId::Player(), pSpouse, false, 7.0f);
        MARAS_CHECK(pClosed.changed);
        MARAS_CHECK_EQ(ws.relationships.Read().GetPendingIntent(pSpouse).type, PartnerIntentType::None);
        MARAS_CHECK_EQ(pClosed.clearedIntents.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(pClosed.clearedIntents[0], pSpouse);
    }

    MARAS_TEST(DomainRelationships, InfidelityAffairDiscoveryResolutionsAllowedAndBreakup) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId sp1{ 0x00016101u };
        const ActorId sp2{ 0x00016102u };
        const ActorId lover{ 0x00016103u };
        for (ActorId id : { sp1, sp2, lover }) {
            RegisterAdult(ws, id);
        }

        ws.settings.Write().enablePlayerPolygamy = true;
        (void)ApplyRelationshipTransition(ws, ActorId::Player(), sp1, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(ws, ActorId::Player(), sp2, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);

        // sp1 cheats secretly and holds ConfessTalking -> "Allowed" clears confess intent & queues victim to open
        ws.relationships.Write().SetPendingIntent(sp1, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessTalking,
            .createdGameDay = 2.0f,
            .relatedLover = lover
        });
        (void)ApplyRecordCheating(
            ws, sp1, ActorId::Player(), lover, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 3.0f);

        const auto allowedOut = ApplyAffairDiscoveryOrResolution(ws, sp1, lover, "Allowed", ActorId::Player());
        MARAS_CHECK(allowedOut.updated);
        MARAS_CHECK_EQ(allowedOut.resolution, AffairResolution::Allowed);
        MARAS_REQUIRE_EQ(allowedOut.victimsToOpen.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(allowedOut.victimsToOpen[0], ActorId::Player());
        MARAS_CHECK_EQ(ws.relationships.Read().GetPendingIntent(sp1).type, PartnerIntentType::None);
        MARAS_CHECK(std::find(allowedOut.clearedIntents.begin(), allowedOut.clearedIntents.end(), sp1) != allowedOut.clearedIntents.end());

        // sp2 cheats secretly and holds ConfessPhysical -> "Breakup" clears intent & populates victimsToSplit
        ws.relationships.Write().SetPendingIntent(sp2, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessPhysical,
            .createdGameDay = 4.0f,
            .relatedLover = lover
        });
        (void)ApplyRecordCheating(
            ws, sp2, ActorId::Player(), lover, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 5.0f);

        const auto splitOut = ApplyAffairDiscoveryOrResolution(ws, sp2, lover, "Breakup", ActorId::Player());
        MARAS_CHECK(splitOut.updated);
        MARAS_CHECK_EQ(splitOut.resolution, AffairResolution::Breakup);
        MARAS_REQUIRE_EQ(splitOut.victimsToSplit.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(splitOut.victimsToSplit[0], ActorId::Player());
        MARAS_CHECK_EQ(ws.relationships.Read().GetPendingIntent(sp2).type, PartnerIntentType::None);
    }

    MARAS_TEST(DomainRelationships, InfidelityCleanupActorIntentsAndPolygamyExemption) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId sp{ 0x00016201u };
        const ActorId paramour{ 0x00016202u };
        const ActorId otherSp{ 0x00016203u };
        for (ActorId id : { sp, paramour, otherSp }) {
            RegisterAdult(ws, id);
        }

        // Player has ongoing affair before enabling polygamy
        (void)ApplyRelationshipTransition(ws, ActorId::Player(), sp, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(ws, sp, paramour, RelationshipStatus::Lover, 2.0f, sp, meta, false, false);
        (void)ApplyRecordCheating(
            ws, sp, ActorId::Player(), paramour, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 3.0f);
        (void)ApplyRecordCheating(
            ws, ActorId::Player(), sp, otherSp, true, AffairType::Dating, AffairStatus::Ongoing, AffairStage::Physical, 3.5f);

        // MarkPlayerCheatingAllowedOnPolygamy transitions player-cheater Ongoing records to Allowed
        MARAS_CHECK_EQ(MarkPlayerCheatingAllowedOnPolygamy(ws), std::size_t{ 1 });
        ws.settings.Write().enablePlayerPolygamy = true;
        MARAS_CHECK(!IsActorCheating(ws.Snapshot(), ActorId::Player()));
        MARAS_CHECK_EQ(MarkCheatingAllowedForPair(ws, ActorId::Player(), sp).size(), std::size_t{ 1 });

        // EvaluateBreakupAffairsAndIntents closes remaining Ongoing affairs & clears player-partner intent
        ws.relationships.Write().SetPendingIntent(sp, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestOpen,
            .createdGameDay = 4.0f
        });
        (void)ApplyRecordCheating(
            ws, sp, ActorId::Player(), paramour, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 5.0f);
        const auto clearedBreak = EvaluateBreakupAffairsAndIntents(ws, ActorId::Player(), sp);
        MARAS_REQUIRE_EQ(clearedBreak.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(clearedBreak[0].partnerId, sp);

        // ApplyCleanupActorIntentsAndThirdPartyAffairs with eraseRecords=false (deceased retention) vs true (unregister)
        (void)ApplyRecordCheating(
            ws, sp, ActorId::Player(), otherSp, false, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 6.0f);
        ws.relationships.Write().SetPendingIntent(sp, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessPhysical,
            .createdGameDay = 6.0f,
            .relatedLover = otherSp
        });
        const auto deceasedClean = ApplyCleanupActorIntentsAndThirdPartyAffairs(ws, otherSp, false);
        MARAS_CHECK(!deceasedClean.clearedIntents.empty());
        MARAS_CHECK(!deceasedClean.affectedActors.empty());
        const auto* preservedRec = ws.affairs.Read().FindTriple(sp, ActorId::Player(), otherSp);
        MARAS_REQUIRE(preservedRec != nullptr);
        MARAS_CHECK_EQ(preservedRec->affairStatus, AffairStatus::EndedBreakup);

        const auto erasedClean = ApplyCleanupActorIntentsAndThirdPartyAffairs(ws, otherSp, true);
        MARAS_CHECK(!erasedClean.affectedActors.empty());
        MARAS_CHECK(ws.affairs.Read().FindTriple(sp, ActorId::Player(), otherSp) == nullptr);
    }

    MARAS_TEST(DomainRelationships, RelationshipRulesEngagementWhileDatingOtherAndAutoOpenPropagation) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId npcA{ 0x00016301u };
        const ActorId npcB{ 0x00016302u };
        const ActorId npcC{ 0x00016303u };
        for (ActorId id : { npcA, npcB, npcC }) {
            RegisterAdult(ws, id);
        }

        // Duplicate same-status transition reports skippedDuplicate == true
        const auto d1 = ApplyRelationshipTransition(ws, ActorId::Player(), npcA, RelationshipStatus::Dating, 1.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(d1.allowed);
        MARAS_CHECK_EQ(d1.offlineSyncedAffection, 80);
        const auto dup = ApplyRelationshipTransition(ws, ActorId::Player(), npcA, RelationshipStatus::Dating, 2.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(dup.skippedDuplicate);
        MARAS_CHECK(!dup.allowed);

        // Monogamous actor dating A cannot become Engaged to B
        std::string reason;
        ws.settings.Write().enablePlayerPolygamy = false;
        MARAS_CHECK(!CanFormRelationship(ws.Snapshot(), meta, ActorId::Player(), npcB, RelationshipStatus::Engaged, {}, &reason));
        MARAS_CHECK(reason.find("dating") != std::string::npos);
        (void)ApplyRelationshipTransition(ws, npcB, npcC, RelationshipStatus::Dating, 1.0f, npcB, meta, false, false);
        MARAS_CHECK(!CanFormRelationship(ws.Snapshot(), meta, npcB, ActorId::Player(), RelationshipStatus::Engaged, {}, &reason));

        // Offline affection sync rules for Lover (>=70, not lowering higher) and Candidate (50)
        ws.affection.Write().SetPermanent(npcB, 90);
        const auto lovHigh = ApplyRelationshipTransition(ws, ActorId::Player(), npcB, RelationshipStatus::Lover, 3.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(lovHigh.allowed);
        MARAS_CHECK(!lovHigh.offlineSyncedAffection.has_value());
        MARAS_CHECK_EQ(ws.affection.Read().GetPermanent(npcB, 0), 90);

        const auto candReset = ApplyRelationshipTransition(ws, ActorId::Player(), npcB, RelationshipStatus::Candidate, 4.0f, ActorId::Player(), meta, false, false);
        MARAS_CHECK(candReset.allowed);
        MARAS_CHECK_EQ(candReset.offlineSyncedAffection, 50);
    }

    MARAS_TEST(DomainRelationships, RelationshipRulesAreMarasFamilyMembersNormalization) {
        WorldState ws{};
        struct NormMeta final : public IActorMetadataProvider {
            BaseId GetActorBaseId(ActorId actorId) const noexcept override {
                if (actorId.raw() == 0x00016401u) return BaseId{ 0x00099401u };
                return BaseId{ actorId.raw() };
            }
        } meta{};

        const ActorId motherRef{ 0x00016401u };
        const BaseId motherBase{ 0x00099401u };
        const ChildId childFromPlayer{ 0x00016410u };
        const ChildId childPlayerHalfSib{ 0x00016411u };
        const ChildId unkChildA{ 0x00016412u };
        const ChildId unkChildB{ 0x00016413u };

        ChildRecord c1{};
        c1.childId = childFromPlayer;
        c1.bioMother = motherRef;
        c1.bioFather = ActorId{ kPlayerBaseRawId };  // BaseId form of Player
        ws.lineage.Write().UpsertChild(c1);

        ChildRecord c2{};
        c2.childId = childPlayerHalfSib;
        c2.bioMother = ActorId{ 0x00016402u };
        c2.bioFather = ActorId::Player();          // RefId form of Player
        ws.lineage.Write().UpsertChild(c2);

        // Unknown fathers with distinct mothers must NOT match as siblings
        ChildRecord u1{};
        u1.childId = unkChildA;
        u1.bioMother = ActorId{ 0x00016403u };
        u1.bioFather = ActorId::UnknownFather();
        ws.lineage.Write().UpsertChild(u1);

        ChildRecord u2{};
        u2.childId = unkChildB;
        u2.bioMother = ActorId{ 0x00016404u };
        u2.bioFather = ActorId::UnknownFather();
        ws.lineage.Write().UpsertChild(u2);

        const auto snap = ws.Snapshot();
        std::string reason;
        // Mother detected via BaseId against RefId child entry
        MARAS_CHECK(AreMarasFamilyMembers(snap, meta, ActorId{ motherBase.raw() }, ActorId{ childFromPlayer.raw() }, &reason));
        // Player RefId matches child stored with kPlayerBaseRawId
        MARAS_CHECK(AreMarasFamilyMembers(snap, meta, ActorId::Player(), ActorId{ childFromPlayer.raw() }, &reason));
        // Half-siblings via kPlayerBaseRawId vs kPlayerRefRawId normalization
        MARAS_CHECK(AreMarasFamilyMembers(snap, meta, ActorId{ childFromPlayer.raw() }, ActorId{ childPlayerHalfSib.raw() }, &reason));
        // UnknownFather children with different mothers are NOT siblings
        MARAS_CHECK(!AreMarasFamilyMembers(snap, meta, ActorId{ unkChildA.raw() }, ActorId{ unkChildB.raw() }, &reason));
    }

}  // namespace MARAS::Tests
