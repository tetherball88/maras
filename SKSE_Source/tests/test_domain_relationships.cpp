#include "TestHarness.h"

#include "maras/domain/relationships/AffectionEventRules.h"
#include "maras/domain/relationships/AffectionRules.h"
#include "maras/domain/relationships/BonusesDomain.h"
#include "maras/domain/relationships/DialogueAndActionRules.h"
#include "maras/domain/relationships/GiftAndIntimacyRules.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/domain/relationships/NpcTypeRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/domain/relationships/RelationshipSettlementRules.h"
#include "maras/domain/relationships/SpouseHierarchyRules.h"
#include "maras/foundation/IRng.h"
#include "maras/state/SessionEpoch.h"
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

    MARAS_TEST(DomainRelationships, AffectionTableDefaultsAndEpochReset) {
        AffectionTable table{};
        table.ResetDefaultMinMax();

        const auto checkBounds = [&](std::string_view key, int expectedMin, int expectedMax) {
            const auto bounds = table.GetMinMaxForType(key);
            MARAS_REQUIRE(bounds.has_value());
            MARAS_CHECK_EQ(bounds->first, expectedMin);
            MARAS_CHECK_EQ(bounds->second, expectedMax);
        };
        checkBounds("intimacy", -22, 22);
        checkBounds("gift", 0, 15);
        checkBounds("dialoguestarted", 0, 5);
        checkBounds("slept", 0, 8);
        checkBounds("promotion", -24, 24);
        checkBounds("loneliness", -16, 6);
        checkBounds("family", 0, 6);

        MARAS_CHECK_EQ(AffectionTable::ClampScore(-5), 0);
        MARAS_CHECK_EQ(AffectionTable::ClampScore(125), 100);
        MARAS_CHECK_EQ(table.GetPermanent(ActorId{ 0x00017001u }, kDefaultAffectionScore), 50);

        SetLegacyMigrationActive(true);
        MARAS_CHECK(IsLegacyMigrationActive());
        (void)BumpSessionEpoch("test_reset");
        MARAS_CHECK(!IsLegacyMigrationActive());
    }

    MARAS_TEST(DomainRelationships, DailyAffectionEventCalculators) {
        const auto nearEq = [](float a, float b) { return std::abs(a - b) < 0.001f; };

        MARAS_CHECK(nearEq(CalculateIntimacyDailyDelta(Temperament::Romantic, 4.0f), 6.0f));
        MARAS_CHECK(nearEq(CalculateIntimacyDailyDelta(Temperament::Jealous, 4.0f), 6.0f));
        MARAS_CHECK(nearEq(CalculateIntimacyDailyDelta(Temperament::Independent, 4.0f), 2.0f));
        MARAS_CHECK(nearEq(CalculateIntimacyDailyDelta(Temperament::Proud, 4.0f), 4.0f));

        // Outcast = 0 => factor = 0.667; Proud non-Romantic keeps 1.0x; Romantic multiplies 1.5x
        MARAS_CHECK(nearEq(
            CalculateGiftDailyDelta(SocialClass::Outcast, Temperament::Proud, 667.0f), 10.0f));
        MARAS_CHECK(nearEq(
            CalculateGiftDailyDelta(SocialClass::Outcast, Temperament::Romantic, 667.0f), 15.0f));

        MARAS_CHECK(nearEq(CalculateDialogueStartedDailyDelta(), 1.0f));

        MARAS_CHECK(nearEq(CalculatePromotionDailyDelta(Temperament::Proud, 2.0f), 24.0f));
        MARAS_CHECK(nearEq(CalculatePromotionDailyDelta(Temperament::Humble, -2.0f), -8.0f));
        MARAS_CHECK(nearEq(CalculatePromotionDailyDelta(Temperament::Romantic, 1.0f), 8.0f));

        MARAS_CHECK(nearEq(CalculateSleptDailyDelta(Temperament::Romantic), 5.0f));
        MARAS_CHECK(nearEq(CalculateSleptDailyDelta(Temperament::Jealous), 4.0f));
        MARAS_CHECK(nearEq(CalculateSleptDailyDelta(Temperament::Independent), 1.0f));
        MARAS_CHECK(nearEq(CalculateSleptDailyDelta(Temperament::Humble), 3.0f));
    }

    MARAS_TEST(DomainRelationships, LifeEventAffectionScalingEligibilityAndClamping) {
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Romantic, 15.0f), 20);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Proud, 15.0f), 18);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Independent, 15.0f), 12);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Humble, 15.0f), 15);

        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Jealous, -15.0f), -22);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Romantic, -15.0f), -18);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Independent, -15.0f), -10);
        MARAS_CHECK_EQ(ScalePlayerPartnerLifeEventDelta(Temperament::Humble, -15.0f), -12);

        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId spouse{ 0x00017101u };
        const ActorId exLover{ 0x00017102u };
        const ActorId candidateOnly{ 0x00017103u };
        const ActorId unregistered{ 0x00017104u };
        RegisterAdult(ws, spouse, Temperament::Proud);
        RegisterAdult(ws, exLover, Temperament::Proud);
        RegisterAdult(ws, candidateOnly, Temperament::Humble);

        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), spouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), exLover, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), exLover, RelationshipStatus::ExLover, 2.0f, ActorId::Player(), meta, false, false);
        ws.affection.Write().SetPermanent(spouse, 95);

        const auto snap = ws.Snapshot();
        MARAS_CHECK(!PlanPlayerPartnerAffectionAdjustment(snap, ActorId{}, 10.0f, "null").has_value());
        MARAS_CHECK(!PlanPlayerPartnerAffectionAdjustment(snap, ActorId::Player(), 10.0f, "player").has_value());
        MARAS_CHECK(!PlanPlayerPartnerAffectionAdjustment(snap, ActorId::UnknownFather(), 10.0f, "unk").has_value());
        MARAS_CHECK(!PlanPlayerPartnerAffectionAdjustment(snap, unregistered, 10.0f, "unreg").has_value());
        MARAS_CHECK(!PlanPlayerPartnerAffectionAdjustment(snap, exLover, 10.0f, "ex").has_value());

        const auto candPlan = PlanPlayerPartnerAffectionAdjustment(snap, candidateOnly, -60.0f, "cand");
        MARAS_REQUIRE(candPlan.has_value());
        MARAS_CHECK_EQ(candPlan->oldScore, 50);
        MARAS_CHECK_EQ(candPlan->appliedDelta, -51);
        MARAS_CHECK_EQ(candPlan->newScore, 0);

        const auto spousePlan = PlanPlayerPartnerAffectionAdjustment(snap, spouse, 20.0f, "clamped");
        MARAS_REQUIRE(spousePlan.has_value());
        MARAS_CHECK_EQ(spousePlan->oldScore, 95);
        MARAS_CHECK_EQ(spousePlan->appliedDelta, 24);
        MARAS_CHECK_EQ(spousePlan->newScore, 100);
    }

    MARAS_TEST(DomainRelationships, AffectionLifeEventPlannersMatrix) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId indepSpouse{ 0x00017201u };
        const ActorId jealousSpouse{ 0x00017202u };
        const ActorId paramour{ 0x00017203u };
        RegisterAdult(ws, indepSpouse, Temperament::Independent);
        RegisterAdult(ws, jealousSpouse, Temperament::Jealous);
        RegisterAdult(ws, paramour, Temperament::Proud);
        ws.settings.Write().enablePlayerPolygamy = true;
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), indepSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), jealousSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), paramour, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta, false, false);

        const auto snap = ws.Snapshot();

        // Open relationship toggle differs by temperament and direction
        const auto openIndep = PlanOpenRelationshipToggleAffection(snap, ActorId::Player(), indepSpouse, true);
        MARAS_REQUIRE_EQ(openIndep.size(), 1u);
        MARAS_CHECK_EQ(openIndep[0].appliedDelta, 12);
        const auto closeJealous = PlanOpenRelationshipToggleAffection(snap, jealousSpouse, ActorId::Player(), false);
        MARAS_REQUIRE_EQ(closeJealous.size(), 1u);
        MARAS_CHECK_EQ(closeJealous[0].appliedDelta, 20);
        MARAS_CHECK(PlanOpenRelationshipToggleAffection(snap, indepSpouse, jealousSpouse, true).empty());

        // Walk-in affects only jealous player partner
        const auto walkInJealous = PlanOpenRelationshipWalkInAffection(snap, ActorId::Player(), jealousSpouse);
        MARAS_REQUIRE_EQ(walkInJealous.size(), 1u);
        MARAS_CHECK_EQ(walkInJealous[0].appliedDelta, -15);
        MARAS_CHECK(PlanOpenRelationshipWalkInAffection(snap, ActorId::Player(), indepSpouse).empty());
        MARAS_CHECK(PlanOpenRelationshipWalkInAffection(snap, indepSpouse, jealousSpouse).empty());

        // Cheating caught vs affair exposed vs affair resolved
        const auto caughtCheater = PlanCheatingCaughtAffection(snap, indepSpouse, ActorId::Player());
        MARAS_REQUIRE_EQ(caughtCheater.size(), 1u);
        MARAS_CHECK_EQ(caughtCheater[0].partner, indepSpouse);
        MARAS_CHECK_EQ(caughtCheater[0].appliedDelta, -21);
        MARAS_CHECK(PlanCheatingCaughtAffection(snap, indepSpouse, paramour).empty());

        const auto exposedPlayerVictim = PlanAffairExposedAffection(snap, jealousSpouse, ActorId::Player(), paramour);
        MARAS_REQUIRE_EQ(exposedPlayerVictim.size(), 2u);
        MARAS_CHECK_EQ(exposedPlayerVictim[0].partner, jealousSpouse);
        MARAS_CHECK_EQ(exposedPlayerVictim[0].appliedDelta, -75);
        MARAS_CHECK_EQ(exposedPlayerVictim[1].partner, paramour);
        MARAS_CHECK_EQ(exposedPlayerVictim[1].appliedDelta, -30);

        const auto resolvedNpcAllowed = PlanAffairResolvedAffection(snap, indepSpouse, jealousSpouse, "allowed");
        MARAS_CHECK(resolvedNpcAllowed.empty());
        const auto resolvedNpcConfronted = PlanAffairResolvedAffection(snap, indepSpouse, jealousSpouse, "confronted");
        MARAS_CHECK_EQ(resolvedNpcConfronted.size(), 2u);
    }

    MARAS_TEST(DomainRelationships, AffectionPregnancyPaternityAndChildPlanners) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId motherSpouse{ 0x00017301u };
        const ActorId bioFatherLover{ 0x00017302u };
        RegisterAdult(ws, motherSpouse, Temperament::Proud);
        RegisterAdult(ws, bioFatherLover, Temperament::Humble);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), motherSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), bioFatherLover, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta, false, false);

        // Secret unborn pregnancy suppresses conceived affection until partner knows
        const ChildId unbornId{ 0xFF001234u };
        ChildRecord unborn{};
        unborn.childId = unbornId;
        unborn.stage = ChildStage::Unborn;
        unborn.bioMother = motherSpouse;
        unborn.bioFather = bioFatherLover;
        unborn.presumedFather = ActorId::Player();
        unborn.paternitySecret.isSecret = true;
        unborn.paternitySecret.isSecretForPartner = true;
        unborn.paternitySecret.isPregnancySecretForPartner = true;
        ws.lineage.Write().UpsertChild(unborn);

        MARAS_CHECK(PlanPregnancyConceivedAffection(ws.Snapshot(), motherSpouse, false).empty());

        unborn.paternitySecret.discoveredByPartner = true;
        unborn.paternitySecret.isPregnancySecretForPartner = false;
        ws.lineage.Write().UpsertChild(unborn);
        const auto conceivedAfterReveal = PlanPregnancyConceivedAffection(ws.Snapshot(), motherSpouse, false);
        MARAS_REQUIRE_EQ(conceivedAfterReveal.size(), 1u);
        MARAS_CHECK_EQ(conceivedAfterReveal[0].appliedDelta, 24);  // 20 * 1.2 (Proud)

        const auto twinsBirth = PlanChildBirthAffection(ws.Snapshot(), motherSpouse, 2);
        MARAS_REQUIRE_EQ(twinsBirth.size(), 1u);
        MARAS_CHECK_EQ(twinsBirth[0].appliedDelta, 42);  // (25 + 10) * 1.2

        const auto adoptedPlan = PlanChildAdoptedAffection(ws.Snapshot(), motherSpouse);
        MARAS_REQUIRE_EQ(adoptedPlan.size(), 1u);
        MARAS_CHECK_EQ(adoptedPlan[0].appliedDelta, 24);  // 20 * 1.2

        // Paternity secret reveal variants: normalized _unannounced + UnknownFather safety + mother_confessed_both
        MARAS_CHECK(PlanPaternitySecretAffection(ws.Snapshot(), unbornId, "remains_secret").empty());
        const auto presumedLearnt = PlanPaternitySecretAffection(
            ws.Snapshot(), unbornId, "presumed_father_learnt_unannounced");
        MARAS_REQUIRE_EQ(presumedLearnt.size(), 1u);
        MARAS_CHECK_EQ(presumedLearnt[0].partner, motherSpouse);
        MARAS_CHECK_EQ(presumedLearnt[0].appliedDelta, -40);

        const ChildId playerMomChild{ 0x00017350u };
        ChildRecord pmChild{};
        pmChild.childId = playerMomChild;
        pmChild.stage = ChildStage::Child;
        pmChild.bioMother = ActorId::Player();
        pmChild.bioFather = bioFatherLover;
        pmChild.presumedFather = motherSpouse;
        pmChild.paternitySecret.isSecret = true;
        pmChild.paternitySecret.isSecretForBioFather = true;
        ws.lineage.Write().UpsertChild(pmChild);

        const auto bothConfessed = PlanPaternitySecretAffection(
            ws.Snapshot(), playerMomChild, "mother_confessed_both_unannounced");
        MARAS_REQUIRE_EQ(bothConfessed.size(), 2u);
        MARAS_CHECK_EQ(bothConfessed[0].partner, motherSpouse);
        MARAS_CHECK_EQ(bothConfessed[0].appliedDelta, -30);
        MARAS_CHECK_EQ(bothConfessed[1].partner, bioFatherLover);
        MARAS_CHECK_EQ(bothConfessed[1].appliedDelta, 15);

        // Child milestone routing respects secret-for-bio-father when mother has no presumed partner
        WorldState wsUnpartneredMom{};
        const ActorId secretDad{ 0x00017381u };
        RegisterAdult(wsUnpartneredMom, secretDad, Temperament::Humble);

        ChildRecord secretBioOnly{};
        const ChildId secretBioChild{ 0x00017360u };
        secretBioOnly.childId = secretBioChild;
        secretBioOnly.stage = ChildStage::Adult;
        secretBioOnly.bioMother = ActorId::Player();
        secretBioOnly.bioFather = secretDad;
        secretBioOnly.paternitySecret.isSecret = true;
        secretBioOnly.paternitySecret.isSecretForBioFather = true;
        wsUnpartneredMom.lineage.Write().UpsertChild(secretBioOnly);
        MARAS_CHECK(PlanChildMilestoneForParents(wsUnpartneredMom.Snapshot(), secretBioChild, "adult").empty());

        secretBioOnly.paternitySecret.discoveredByBioFather = true;
        wsUnpartneredMom.lineage.Write().UpsertChild(secretBioOnly);
        const auto adultMilestone = PlanChildMilestoneForParents(wsUnpartneredMom.Snapshot(), secretBioChild, "adult");
        MARAS_REQUIRE_EQ(adultMilestone.size(), 1u);
        MARAS_CHECK_EQ(adultMilestone[0].partner, secretDad);
        MARAS_CHECK_EQ(adultMilestone[0].appliedDelta, 15);

        // UnknownFather bioFather never causes bogus adjustment even on mother_confessed_both
        ChildRecord unkDadChild{};
        const ChildId unkDadId{ 0x00017370u };
        unkDadChild.childId = unkDadId;
        unkDadChild.stage = ChildStage::Child;
        unkDadChild.bioMother = ActorId::Player();
        unkDadChild.bioFather = ActorId::UnknownFather();
        unkDadChild.presumedFather = motherSpouse;
        ws.lineage.Write().UpsertChild(unkDadChild);
        const auto unkDadPlan = PlanPaternitySecretAffection(ws.Snapshot(), unkDadId, "mother_confessed_both");
        MARAS_REQUIRE_EQ(unkDadPlan.size(), 1u);
        MARAS_CHECK_EQ(unkDadPlan[0].partner, motherSpouse);
    }

    MARAS_TEST(DomainRelationships, Phase3MarriageDifficultySettlementAndTransitionRules) {
        auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

        // 1. ComputeLoversLedgerIntimacyBonus (rank 0 gives +6 bugfix)
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 0, -1), 0.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(1, 1, -1), 10.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 1, -1), 6.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 0, 0), 6.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 0, 1), 4.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 0, 2), 2.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(0, 0, 3), 0.0f));
        MARAS_CHECK(approx(ComputeLoversLedgerIntimacyBonus(1, 0, 0), 16.0f));

        // 2. ComputeIntimacyAdjustment
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(true, false, false, false, false, 0.0f), 25.0f));
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(true, true, false, false, false, 0.0f), 30.0f));
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(false, false, true, false, false, 0.0f), 15.0f));
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(false, false, true, true, false, 0.0f), 20.0f));
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(false, false, false, false, true, 0.0f), 10.0f));
        MARAS_CHECK(approx(ComputeIntimacyAdjustment(true, true, false, false, false, 16.0f), 14.0f));

        // 3. ComputeNoAiDialogueChances
        {
            const auto allMax = ComputeNoAiDialogueChances(0.1f, true, RelationshipStatus::Candidate, true, true);
            MARAS_CHECK(approx(allMax.datingChance, 100.0f));
            MARAS_CHECK(approx(allMax.loverChance, 100.0f));
            MARAS_CHECK(approx(allMax.marriageChance, 100.0f));

            const auto baseCand = ComputeNoAiDialogueChances(0.5f, false, RelationshipStatus::Candidate, false, false);
            MARAS_CHECK(approx(baseCand.datingChance, 57.5f));
            MARAS_CHECK(approx(baseCand.loverChance, 65.0f));
            MARAS_CHECK(approx(baseCand.marriageChance, 50.0f));

            const auto closedBreakup = ComputeNoAiDialogueChances(0.5f, false, RelationshipStatus::ExDating, true, false);
            MARAS_CHECK(approx(closedBreakup.datingChance, 22.5f * 1.15f));
            MARAS_CHECK(approx(closedBreakup.loverChance, 22.5f * 1.3f));
            MARAS_CHECK(approx(closedBreakup.marriageChance, 22.5f));

            const auto loverBoost = ComputeNoAiDialogueChances(0.5f, false, RelationshipStatus::Lover, false, false);
            MARAS_CHECK(approx(loverBoost.datingChance, 86.25f));
            MARAS_CHECK(approx(loverBoost.loverChance, 65.0f));
            MARAS_CHECK(approx(loverBoost.marriageChance, 60.0f));

            const auto datingBoost = ComputeNoAiDialogueChances(0.5f, false, RelationshipStatus::Dating, false, false);
            MARAS_CHECK(approx(datingBoost.datingChance, 57.5f));
            MARAS_CHECK(approx(datingBoost.loverChance, 65.0f));
            MARAS_CHECK(approx(datingBoost.marriageChance, 75.0f));

            const auto clamped = ComputeNoAiDialogueChances(0.95f, false, RelationshipStatus::Lover, false, false);
            MARAS_CHECK(approx(clamped.datingChance, 100.0f));
            MARAS_CHECK(approx(clamped.loverChance, 100.0f));
            MARAS_CHECK(approx(clamped.marriageChance, 100.0f));
        }

        // 4. ComputeInitialCandidateAffection
        MARAS_CHECK_EQ(ComputeInitialCandidateAffection(-2), 30);
        MARAS_CHECK_EQ(ComputeInitialCandidateAffection(0), 50);
        MARAS_CHECK_EQ(ComputeInitialCandidateAffection(3), 80);
        MARAS_CHECK_EQ(ComputeInitialCandidateAffection(4), 90);
        MARAS_CHECK_EQ(ComputeInitialCandidateAffection(6), 100);

        // 5. ComputeSpouseIncomeShare
        {
            const auto firstShare = ComputeSpouseIncomeShare(0.5f, -1.0f);
            MARAS_CHECK(firstShare.awarded);
            MARAS_CHECK_EQ(firstShare.goldAmount, 100);
            MARAS_CHECK(approx(firstShare.updatedLastSharedDay, 0.5f));

            const auto tooSoon = ComputeSpouseIncomeShare(10.5f, 10.0f);
            MARAS_CHECK(!tooSoon.awarded);
            MARAS_CHECK_EQ(tooSoon.goldAmount, 0);
            MARAS_CHECK(approx(tooSoon.updatedLastSharedDay, 10.0f));

            const auto threeDays = ComputeSpouseIncomeShare(13.8f, 10.0f);
            MARAS_CHECK(threeDays.awarded);
            MARAS_CHECK_EQ(threeDays.goldAmount, 300);

            const auto capped = ComputeSpouseIncomeShare(25.0f, 10.0f);
            MARAS_CHECK(capped.awarded);
            MARAS_CHECK_EQ(capped.goldAmount, 500);
        }

        // 6. ComputeDivorceSettlement
        {
            const auto cooldown = ComputeDivorceSettlement(11.9f, 5.0f, 5000, 1, 1, 5, 5);
            MARAS_CHECK(cooldown.onCooldown);

            const auto due = ComputeDivorceSettlement(12.0f, 5.0f, 5000, 2, 1, 15, 3);
            MARAS_CHECK(!due.onCooldown);
            MARAS_CHECK_EQ(due.playerWealthShare, 1000);
            MARAS_CHECK_EQ(due.assetsShare, 3200);
            MARAS_CHECK_EQ(due.totalFee, 4300);
            MARAS_CHECK(due.canPayDirectly);

            const auto cannotPay = ComputeDivorceSettlement(20.0f, -1.0f, 200, 1, 0, 0, 0);
            MARAS_CHECK(!cannotPay.onCooldown);
            MARAS_CHECK_EQ(cannotPay.playerWealthShare, 60);
            MARAS_CHECK_EQ(cannotPay.assetsShare, 700);
            MARAS_CHECK_EQ(cannotPay.totalFee, 860);
            MARAS_CHECK(!cannotPay.canPayDirectly);
        }

        // 7. ApplyRelationshipTransition: Candidate affection, permanent floors, and breakup cooldowns
        {
            WorldState ws{};
            StubMetadataProvider meta{};
            const ActorId cand{ 0x00019001u };
            const ActorId cleanEx{ 0x00019002u };
            const ActorId cheatEx{ 0x00019003u };
            const ActorId paramour{ 0x00019004u };
            const ActorId jiltedNpc{ 0x00019005u };
            for (ActorId id : { cand, cleanEx, cheatEx, paramour, jiltedNpc }) {
                RegisterAdult(ws, id);
            }
            ws.settings.Write().enablePlayerPolygamy = true;

            RelationshipCheckContext rank3Ctx{};
            rank3Ctx.playerRelationshipRank = 3;
            const auto candOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), cand, RelationshipStatus::Candidate, 1.0f, ActorId::Player(), meta, false, false, rank3Ctx);
            MARAS_CHECK(candOut.allowed);
            MARAS_CHECK_EQ(candOut.offlineSyncedAffection, 80);
            MARAS_CHECK_EQ(ws.affection->GetPermanent(cand), 80);

            ws.affection.Write().SetPermanent(cleanEx, 15);
            const auto dateOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), cleanEx, RelationshipStatus::Dating, 2.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK(dateOut.allowed);
            MARAS_CHECK_EQ(dateOut.offlineSyncedAffection, 80);
            MARAS_CHECK_EQ(ws.affection->GetPermanent(cleanEx), 80);

            // Clean ExDating does NOT apply breakup cooldown
            const auto cleanExOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), cleanEx, RelationshipStatus::ExDating, 3.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK(cleanExOut.allowed);
            MARAS_CHECK(!cleanExOut.appliedBreakupCooldown);
            MARAS_CHECK_EQ(ws.cooldowns->GetBrokeupTime(cleanEx), -1.0f);

            // Cheating Dating -> ExDating DOES apply breakup cooldown
            (void)ApplyRelationshipTransition(
                ws, ActorId::Player(), cheatEx, RelationshipStatus::Dating, 4.0f, ActorId::Player(), meta, false, false);
            (void)ApplyRecordCheating(
                ws, cheatEx, ActorId::Player(), paramour, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 4.5f);
            const auto cheatExOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), cheatEx, RelationshipStatus::ExDating, 5.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK(cheatExOut.allowed);
            MARAS_CHECK(cheatExOut.appliedBreakupCooldown);
            MARAS_CHECK(approx(ws.cooldowns->GetBrokeupTime(cheatEx), 5.0f));

            // Engaged sets permanent 100 and Jilted applies breakup cooldown
            ws.affection.Write().SetPermanent(jiltedNpc, 20);
            const auto engOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), jiltedNpc, RelationshipStatus::Engaged, 6.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK_EQ(engOut.offlineSyncedAffection, 100);
            const auto jiltOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), jiltedNpc, RelationshipStatus::Jilted, 7.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK(jiltOut.appliedBreakupCooldown);
            MARAS_CHECK(approx(ws.cooldowns->GetBrokeupTime(jiltedNpc), 7.0f));

            // Lover floor >= 70
            const ActorId loverNpc{ 0x00019006u };
            RegisterAdult(ws, loverNpc);
            ws.affection.Write().SetPermanent(loverNpc, 25);
            const auto loverOut = ApplyRelationshipTransition(
                ws, ActorId::Player(), loverNpc, RelationshipStatus::Lover, 8.0f, ActorId::Player(), meta, false, false);
            MARAS_CHECK(loverOut.allowed);
            MARAS_CHECK_EQ(loverOut.offlineSyncedAffection, 70);
            MARAS_CHECK_EQ(ws.affection->GetPermanent(loverNpc), 70);
        }
    }

    MARAS_TEST(DomainRelationships, GiftSessionEvaluationFormatsAndValidates) {
        WorldState ws{};
        StubMetadataProvider meta{};
        const ActorId ysolda{ 0x00019101u };
        const ActorId stranger{ 0x00019102u };
        RegisterAdult(ws, ysolda);

        const std::vector<GiftedItemEntry> items{
            { "Gold Necklace", 1, 200 },
            { "Sweetroll", 3, 10 },
            { "IgnoredEmpty", 0, 999 },
            { "", 2, 50 },
        };

        const auto ok = EvaluateGiftSession(ws.Snapshot(), ysolda, "Dragonborn", "Ysolda", items);
        MARAS_CHECK(ok.valid);
        MARAS_CHECK_EQ(ok.totalGoldValue, 230);
        MARAS_CHECK_EQ(ok.formattedItems, std::string("Gold Necklace,Sweetroll(3)"));
        MARAS_CHECK_EQ(ok.skyrimNetMessage,
                       std::string("Dragonborn gifted Gold Necklace,Sweetroll(3) to Ysolda of total value 230."));

        const auto untracked = EvaluateGiftSession(ws.Snapshot(), stranger, "Dragonborn", "Stranger", items);
        MARAS_CHECK(!untracked.valid);

        const std::vector<GiftedItemEntry> invalidItems{ { "Broken", -1, 50 }, { "", 1, 10 } };
        const auto emptyOut = EvaluateGiftSession(ws.Snapshot(), ysolda, "Dragonborn", "Ysolda", invalidItems);
        MARAS_CHECK(!emptyOut.valid);
    }

    MARAS_TEST(DomainRelationships, IntimateThreadParticipantsAndCompletionAffection) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        const ActorId cheaterSpouse{ 0x00019201u };
        const ActorId paramour{ 0x00019202u };
        const ActorId faithfulDating{ 0x00019203u };
        const ActorId untrackedNpc{ 0x00019204u };

        RegisterAdult(ws, cheaterSpouse);
        RegisterAdult(ws, paramour);
        RegisterAdult(ws, faithfulDating);

        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), cheaterSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRelationshipTransition(
            ws, ActorId::Player(), faithfulDating, RelationshipStatus::Dating, 1.0f, ActorId::Player(), meta, false, false);
        (void)ApplyRecordCheating(
            ws, cheaterSpouse, ActorId::Player(), paramour, true, AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 2.0f);

        const auto snap = ws.Snapshot();

        // Non-player thread with cheating potential catches cheaterSpouse for 0x5C4 mid-act faction
        const std::vector<ActorId> npcThread{ cheaterSpouse, paramour };
        const auto cheaterEval = EvaluateIntimateThreadParticipants(snap, npcThread, true);
        MARAS_CHECK(cheaterEval.playerIntimacyPartners.empty());
        MARAS_REQUIRE_EQ(cheaterEval.cheatersCaughtMidAct.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(cheaterEval.cheatersCaughtMidAct[0], cheaterSpouse);

        // Without cheating/open potential flag, mid-act cheater list stays empty
        const auto noFlagEval = EvaluateIntimateThreadParticipants(snap, npcThread, false);
        MARAS_CHECK(noFlagEval.cheatersCaughtMidAct.empty());

        // Player thread extracts tracked non-player participants (deduped) and skips untracked NPC
        const std::vector<ActorId> playerThread{
            ActorId::Player(), faithfulDating, faithfulDating, untrackedNpc
        };
        const auto playerEval = EvaluateIntimateThreadParticipants(snap, playerThread, false);
        MARAS_REQUIRE_EQ(playerEval.playerIntimacyPartners.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(playerEval.playerIntimacyPartners[0], faithfulDating);

        // Completion awards 8.0f foreplay or 15.0f sexual affection to deduplicated tracked partners
        const std::vector<ActorId> recorded{ faithfulDating, untrackedNpc, faithfulDating };
        const auto foreplay = EvaluateIntimateThreadCompletion(snap, recorded, false);
        MARAS_CHECK(std::fabs(foreplay.baseAffection - 8.0f) < 1e-4f);
        MARAS_REQUIRE_EQ(foreplay.eligiblePartners.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(foreplay.eligiblePartners[0], faithfulDating);

        const auto sexOutcome = EvaluateIntimateThreadCompletion(snap, recorded, true);
        MARAS_CHECK(std::fabs(sexOutcome.baseAffection - 15.0f) < 1e-4f);
        MARAS_REQUIRE_EQ(sexOutcome.eligiblePartners.size(), std::size_t{ 1 });
    }

    MARAS_TEST(DomainRelationships, DialogueAndActionRules_AliasHelpersAndTopicOutcomes) {
        using namespace MARAS::Domain::Relationships;
        using MARAS::Foundation::RelationshipStatus;

        MARAS_CHECK_EQ(ClassifyActorNameAlias("", "Dovah"), ActorNameAliasKind::NoneSentinel);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("  None  ", "Dovah"), ActorNameAliasKind::NoneSentinel);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("player", "Dovah"), ActorNameAliasKind::Player);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("Dragonborn", "Dovah"), ActorNameAliasKind::Player);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("the dragonborn", "Dovah"), ActorNameAliasKind::Player);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("dovah", "Dovah"), ActorNameAliasKind::Player);
        MARAS_CHECK_EQ(ClassifyActorNameAlias("Lydia", "Dovah"), ActorNameAliasKind::NamedNpc);

        MARAS_CHECK_EQ(*MapBreakupTargetToRequiredCurrentStatus("ExDating"), RelationshipStatus::Dating);
        MARAS_CHECK_EQ(*MapBreakupTargetToRequiredCurrentStatus("jilted"), RelationshipStatus::Engaged);
        MARAS_CHECK_EQ(*MapBreakupTargetToRequiredCurrentStatus("exlover"), RelationshipStatus::Lover);
        MARAS_CHECK_EQ(*MapBreakupTargetToRequiredCurrentStatus("DIVORCED"), RelationshipStatus::Married);
        MARAS_CHECK(!MapBreakupTargetToRequiredCurrentStatus("married").has_value());

        MARAS_CHECK_EQ(
            BuildNpcProposalNarration("Sven", "Camilla", "engaged"),
            std::string("Sven proposes an engagement relationship to Camilla. Camilla must answer and accept or reject the proposal."));
        MARAS_CHECK_EQ(
            BuildNpcProposalNarration("Vilkas", "Aela", "dating"),
            std::string("Vilkas proposes a dating relationship to Aela. Aela must answer and accept or reject the proposal."));

        MARAS_CHECK_EQ(ParseDivorceResolutionQuestStage("reconcile"), 100);
        MARAS_CHECK_EQ(ParseDivorceResolutionQuestStage(" DIVORCE "), 200);
        MARAS_CHECK_EQ(ParseDivorceResolutionQuestStage("wait"), 0);

        const auto endBreakup = EvaluateDialogueTopicEndOutcome("maras_sex_walkin_cheating_sorry_no_persuade_failure");
        MARAS_CHECK(endBreakup.breakupWithSpeaker);
        MARAS_CHECK_EQ(endBreakup.papyrusSideEffect, DialogueEndSideEffect::None);

        const auto endJoin = EvaluateDialogueTopicEndOutcome("maras_player_caught_cheater_join__persuade_success");
        MARAS_CHECK(!endJoin.breakupWithSpeaker);
        MARAS_CHECK_EQ(endJoin.papyrusSideEffect, DialogueEndSideEffect::AddParticipantSpeakerPlayer);

        const auto endThread = EvaluateDialogueTopicEndOutcome("maras_no_ai_cheating_demand_giveup_stop");
        MARAS_CHECK(endThread.requiresCaughtFactionForSideEffect);
        MARAS_CHECK_EQ(endThread.papyrusSideEffect, DialogueEndSideEffect::EndCaughtCheaterThread);

        const auto candBegin = EvaluateDialogueTopicBeginOutcome("maras_enable_candidate", RelationshipStatus::Unknown);
        MARAS_CHECK(candBegin.registerCandidate);
        MARAS_CHECK_EQ(candBegin.PackPapyrusEffectCode(), 0);

        MARAS_CHECK_EQ(
            *EvaluateDialogueTopicBeginOutcome("maras_breakup", RelationshipStatus::Engaged).promoteSpeakerStatus,
            RelationshipStatus::Jilted);
        MARAS_CHECK_EQ(
            *EvaluateDialogueTopicBeginOutcome("maras_no_ai_cheating_breakup", RelationshipStatus::Married).promoteSpeakerStatus,
            RelationshipStatus::Divorced);
        MARAS_CHECK(!EvaluateDialogueTopicBeginOutcome("maras_breakup", RelationshipStatus::Candidate).promoteSpeakerStatus.has_value());

        const auto intimDemand = EvaluateDialogueTopicBeginOutcome("maras_no_ai_cheating_demand_intimidate_success", RelationshipStatus::Married);
        MARAS_CHECK(std::fabs(intimDemand.affectionDelta - (-20.0f)) < 1e-4f);
        MARAS_CHECK_EQ(intimDemand.cheatingTarget, DialogueCheatingTarget::SpeakerIsCheater);
        MARAS_CHECK_EQ(intimDemand.cheatingStatusOrResolution, std::string_view("confronted"));
        MARAS_CHECK_EQ(intimDemand.PackPapyrusEffectCode(), 200);

        const auto openAgree = EvaluateDialogueTopicBeginOutcome("maras_partner_suggest_open_agree", RelationshipStatus::Married);
        MARAS_CHECK(openAgree.clearPartnerIntent);
        MARAS_CHECK(openAgree.setOpenBeforeAffection);
        MARAS_CHECK(openAgree.setOpenRelationship == true);
        MARAS_CHECK(std::fabs(openAgree.affectionDelta - 10.0f) < 1e-4f);
        MARAS_CHECK_EQ(openAgree.PackPapyrusEffectCode(), 3);

        const auto closePersuade = EvaluateDialogueTopicBeginOutcome("maras_partner_suggest_close_refuse_persuade_success", RelationshipStatus::Married);
        MARAS_CHECK(closePersuade.clearPartnerIntent);
        MARAS_CHECK(!closePersuade.setOpenRelationship.has_value());
        MARAS_CHECK_EQ(closePersuade.PackPapyrusEffectCode(), 104);

        const auto walkinSorry = EvaluateDialogueTopicBeginOutcome("maras_sex_walkin_cheating_sorry_yes", RelationshipStatus::Married);
        MARAS_CHECK_EQ(walkinSorry.cheatingTarget, DialogueCheatingTarget::PlayerIsCheater);
        MARAS_CHECK_EQ(walkinSorry.cheatingStatusOrResolution, std::string_view("confronted"));
        MARAS_CHECK_EQ(walkinSorry.PackPapyrusEffectCode(), 7);

        const auto walkinContinue = EvaluateDialogueTopicBeginOutcome("maras_sex_walkin_cheating_continue", RelationshipStatus::Dating);
        MARAS_CHECK(walkinContinue.breakupWithSpeaker);
        MARAS_CHECK_EQ(walkinContinue.PackPapyrusEffectCode(), 1);

        const auto quickSex = EvaluateDialogueTopicBeginOutcome("maras_no_ai_initiate_sex", RelationshipStatus::Dating);
        MARAS_CHECK_EQ(quickSex.PackPapyrusEffectCode(), 8);
    }

}  // namespace MARAS::Tests
