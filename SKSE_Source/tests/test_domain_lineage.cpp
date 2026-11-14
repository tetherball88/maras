#include "TestHarness.h"

#include "maras/domain/lineage/ChildGrowthRules.h"
#include "maras/domain/lineage/FertilityRules.h"
#include "maras/domain/lineage/LineageRules.h"
#include "maras/domain/lineage/NamePool.h"
#include "maras/domain/lineage/TrainingCatalog.h"
#include "maras/foundation/IRng.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
        using namespace MARAS::Domain::Lineage;
        using MARAS::Projection::PerspectiveView;

        struct StubLineageMeta final : public IActorMetadataProvider {
            std::unordered_map<ActorId, std::string> names{};
            std::unordered_map<ActorId, BaseId> baseIds{};

            std::string GetActorName(ActorId actorId) const override {
                if (const auto it = names.find(actorId); it != names.end()) {
                    return it->second;
                }
                return IActorMetadataProvider::GetActorName(actorId);
            }

            BaseId GetActorBaseId(ActorId actorId) const noexcept override {
                if (const auto it = baseIds.find(actorId); it != baseIds.end()) {
                    return it->second;
                }
                return BaseId{ actorId.raw() };
            }
        };

        struct StubAffiliationPort final : public IParentAffiliationPort {
            std::unordered_map<ActorId, std::int32_t> ranks{};
            std::unordered_set<std::string> completedQuests{};
            std::unordered_set<std::string> matchedNpcs{};

            bool MatchesNpcKey(ActorId parentId, std::string_view npcKey) const override {
                return matchedNpcs.contains(std::format("{}:{}", parentId.raw(), npcKey));
            }
            std::int32_t GetFactionRank(ActorId parentId, std::string_view) const override {
                if (const auto it = ranks.find(parentId); it != ranks.end()) {
                    return it->second;
                }
                return -1;
            }
            bool IsQuestCompleted(std::string_view questKey) const override {
                return completedQuests.contains(std::string(questKey));
            }
            std::int32_t GetQuestStage(std::string_view) const override {
                return 0;
            }
            std::string GetParentDisplayName(ActorId parentId) const override {
                return parentId.IsPlayer() ? "Dragonborn" : "MentorParent";
            }
        };

    }  // namespace

    MARAS_TEST(DomainLineage, AdoptionBlockBeastbloodGuardiansAndOrders) {
        std::string reason;
        MARAS_CHECK(IsChildBlockedFromAdoption(
            RefId{ kAventusAretinoRefRawId }, BaseId{}, true, &reason));
        MARAS_CHECK(!reason.empty());
        MARAS_CHECK(!IsChildBlockedFromAdoption(
            RefId{ kAventusAretinoRefRawId }, BaseId{}, false, &reason));

        ChildRecord wwRec{};
        wwRec.childId = ChildId{ 0x00021001u };
        wwRec.name = "AelaJr";
        wwRec.bioMother = ActorId{ 0x00020001u };
        wwRec.bioFather = ActorId{ 0x00020002u };
        EvaluateChildBeastbloodInheritance(wwRec, true, true);
        MARAS_CHECK(wwRec.hasBeastblood);
        MARAS_CHECK(wwRec.beastbloodEvaluated);

        ChildRecord mortalRec{};
        mortalRec.childId = ChildId{ 0x00021002u };
        mortalRec.isAdopted = true;
        EvaluateChildBeastbloodInheritance(mortalRec, false, false);
        MARAS_CHECK(!mortalRec.hasBeastblood);
        MARAS_CHECK(mortalRec.beastbloodEvaluated);

        MARAS_CHECK(IsCompanionRetryCooldownElapsed(0.0f, 5.0f));
        MARAS_CHECK(!IsCompanionRetryCooldownElapsed(10.0f, 20.0f));
        MARAS_CHECK(IsCompanionRetryCooldownElapsed(10.0f, 26.0f));

        WorldState ws{};
        StubLineageMeta meta{};
        const ActorId gB{ 0x00020011u };
        const ChildId cid{ 0x00021010u };

        ChildRegistrationInput input{};
        input.childId = cid;
        input.guardianA = ActorId::Player();
        input.guardianB = gB;
        input.name = "Sissel";
        input.isAdopted = true;
        const auto regOut = ApplyRegisterChild(ws, input, meta);
        MARAS_CHECK(regOut.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.HasSameSet(gB, ActorId::Player()));

        // Orders & expiry (while in player household)
        const auto ord1 = ApplyOrderIssue(ws, cid, 2.0f, 3.0f, 100.0f);
        MARAS_CHECK(ord1.applied);
        MARAS_CHECK_EQ(*ord1.orderToApply, 2.0f);
        const auto expired = ApplyExpiredOrders(ws, 104.0f);
        MARAS_REQUIRE_EQ(expired.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(expired[0], cid);

        const auto remGuard = ApplySetChildGuardian(ws, cid, ActorId::Player(), false, meta);
        MARAS_CHECK(remGuard.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.IsSoleGuardian(gB));

        DeterministicRng rng{ 77u };
        MARAS_CHECK_EQ(PickWeightedChild(ws.Snapshot(), { cid }, 105.0f, rng), cid);
    }

    MARAS_TEST(DomainLineage, FertilityPhenotypeAndPaternitySecretLifecycle) {
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("NordRace"), PhenotypeTier::PaleHuman);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("RedguardRace"), PhenotypeTier::Redguard);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("KhajiitRace"), PhenotypeTier::Khajiit);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("HighElfRace"), PhenotypeTier::Mer);
        MARAS_CHECK(HasPhenotypeMismatch("NordRace", "KhajiitRace"));
        MARAS_CHECK(!HasPhenotypeMismatch("NordRace", "ImperialRace"));

        WorldState ws{};
        StubLineageMeta meta{};
        DeterministicRng rng{ 99u };
        const ActorId mother{ 0x00022001u };
        const ActorId spouse{ 0x00022002u };
        const ActorId lover{ 0x00022003u };
        meta.names[mother] = "Camilla";
        meta.names[spouse] = "Sven";
        meta.names[lover] = "Faendal";

        (void)ws.TransitionRelationship(mother, spouse, RelationshipStatus::Married, 1.0f, mother, meta);

        UnbornChildRegistrationInput unborn{};
        unborn.bioMother = mother;
        unborn.bioFather = lover;
        unborn.bioFatherName = "Faendal";
        unborn.presumedFatherRaceEditorId = "NordRace";
        unborn.bioFatherRaceEditorId = "WoodElfRace";
        unborn.conceptionTimestamp = 5.0f;

        const auto reg = ApplyRegisterUnbornChild(ws, unborn, meta);
        MARAS_REQUIRE(!reg.childId.IsNull());
        MARAS_CHECK_EQ(reg.presumedFather, spouse);
        MARAS_CHECK(reg.hasSecret);
        MARAS_CHECK(reg.hasPhenotypeMismatch);

        // Reveal pregnancy to partner while still unborn clears pregnancy-only secret flag first
        const auto pregReveal = ApplyRevealPregnancyToPartner(ws, reg.childId, "talk");
        MARAS_CHECK(pregReveal.updated);
        MARAS_CHECK(!ws.lineage.Read().FindChild(reg.childId)->paternitySecret.isPregnancySecretForPartner);
        MARAS_CHECK(ws.lineage.Read().FindChild(reg.childId)->paternitySecret.isSecretForPartner);

        // Promote unborn to infant
        const auto promo = ApplyPromoteUnbornToInfant(
            ws, mother, 20.0f, "ElvenBaby", lover, false, false, meta, rng);
        MARAS_CHECK(promo.promoted);
        MARAS_CHECK(promo.isSecretForPartner);
        MARAS_CHECK(promo.isSecretForBioFather);

        const auto* rec = ws.lineage.Read().FindChild(reg.childId);
        MARAS_REQUIRE(rec != nullptr);
        MARAS_CHECK_EQ(rec->stage, ChildStage::Infant);
        MARAS_CHECK_EQ(rec->name, "ElvenBaby");

        // Daily roll respects phenotype/growth settings and reveals cleanly via ApplyPaternitySecretFallback when triggered
        ws.settings.Write().paternityPhenotypeBaseChance = 100;
        ws.settings.Write().paternityPhenotypeMaxChance = 100;
        ws.settings.Write().paternityNonSkyrimNetRevealChance = 100;
        MARAS_CHECK(EvaluatePaternitySecretDailyRoll(*ws.lineage.Read().FindChild(reg.childId), ws.settings.Read(), 25.0f, rng));

        const auto fallback = ApplyPaternitySecretFallback(ws, reg.childId, 25.0f, meta, rng);
        MARAS_CHECK(fallback.evaluated);
        MARAS_CHECK(fallback.revealed);
        MARAS_CHECK(!ws.lineage.Read().FindChild(reg.childId)->paternitySecret.isSecret);
    }

    MARAS_TEST(DomainLineage, ChildGrowthTrainingCatalogAndNamePool) {
        MARAS_CHECK_EQ(*ParseSkillIndex("Marksman"), SkillIndex::Archery);
        MARAS_CHECK_EQ(*ParseSkillIndex("speechcraft"), SkillIndex::Speech);
        MARAS_CHECK_EQ(ComputeEffectiveChildLevel(5), std::uint16_t{ 10 });
        MARAS_CHECK_EQ(ComputeEffectiveChildLevel(99), std::uint16_t{ 50 });

        ChildRecord child{};
        child.childId = ChildId{ 0x00023001u };
        child.name = "Alessia";
        child.bioMother = ActorId::Player();
        child.guardians.Assign(ActorId::Player(), ActorId{ 0x00023002u });

        ParentLineageInput bioMom{ ActorId::Player(), 30, { SkillIndex::OneHanded, SkillIndex::Block, SkillIndex::HeavyArmor, SkillIndex::Archery } };
        ParentLineageInput guard{ ActorId{ 0x00023002u }, 25, { SkillIndex::Destruction, SkillIndex::Restoration, SkillIndex::Alteration, SkillIndex::Conjuration } };
        const std::array<ParentLineageInput, 2> guards{ bioMom, guard };
        const std::array<std::string, 2> blessings{ "mara_blessing", "azura_blessing" };

        EvaluateChildGuardianLineageAtGraduation(child, &bioMom, nullptr, guards, blessings);
        MARAS_CHECK(child.bioLineageEvaluated);
        MARAS_CHECK(child.guardianLineageEvaluated);
        MARAS_CHECK(child.bioSkillBonuses[static_cast<std::size_t>(SkillIndex::OneHanded)] > 0);
        MARAS_CHECK(child.guardianSkillBonuses[static_cast<std::size_t>(SkillIndex::Destruction)] > 0);
        MARAS_CHECK(!child.daedricBlessingId.empty() && child.daedricBlessingId != "none");

        // TrainingCatalog discounts & requirements
        TrainingCatalog catalog{};
        TrainingClassSpec spec{};
        spec.id = "companions";
        spec.className = "Shield-Sibling";
        spec.locationName = "Jorrvaskr";
        spec.costMultiplier = 1.5f;
        spec.requiresBeastblood = true;
        spec.leaderFactionKeys = { "CompanionsLeader" };
        spec.memberFactionKeys = { "CompanionsMember" };
        catalog.SetData({ spec }, {});

        MARAS_CHECK_EQ(catalog.FindClassByLocation("jorrvaskr"), 0);
        MARAS_CHECK_EQ(catalog.GetEffectiveTrainingCost(0, 1000), 1500);
        MARAS_CHECK_EQ(catalog.GetDiscountedTrainingCost(0, 1000, 25), 1125);

        WorldState ws{};
        ws.lineage.Write().UpsertChild(child);
        auto worldSnap = std::make_shared<const WorldStateSnapshot>(ws.Snapshot());
        auto view = PerspectiveView::ForActor(
            worldSnap, std::make_shared<const SpatialActorSnapshot>(), ActorId::Player());
        StubAffiliationPort port{};
        port.ranks[ActorId::Player()] = 0;

        const auto discount = catalog.EvaluateChildTrainingDiscount(child.childId, 0, view, port);
        MARAS_CHECK_EQ(discount.discountPercent, 25);
        MARAS_CHECK(catalog.EvaluatePlayerRequirements(0, true, port));

        // NamePool uniqueness & critter names
        NamePool pool{};
        pool.SetChildNames(
            { { "nord", { "Hakon", "Torvar" } } },
            { { "nord", { "Freya" } } });
        pool.SetCritterNames({ { "fox", { "Pumpkin", "Rusty" } } });
        DeterministicRng rng{ 123u };
        const std::unordered_set<std::string> taken{ "hakon" };
        MARAS_CHECK_EQ(pool.GetRandomChildName("NordRace", false, taken, true, rng), "Torvar");
        const std::string critter = pool.GetRandomCritterName("FoxRace", rng);
        MARAS_CHECK(critter == "Pumpkin" || critter == "Rusty");
    }

}  // namespace MARAS::Tests
