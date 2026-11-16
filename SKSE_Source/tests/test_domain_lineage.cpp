#include "TestHarness.h"

#include "maras/domain/lineage/ChildGrowthRules.h"
#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/domain/lineage/FertilityRules.h"
#include "maras/domain/lineage/LineageRules.h"
#include "maras/domain/lineage/NamePool.h"
#include "maras/domain/lineage/PaternityActionRules.h"
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

    MARAS_TEST(DomainLineage, FertilityUnbornTwinPromotionAndTargetedRemoval) {
        WorldState ws{};
        StubLineageMeta meta{};
        DeterministicRng rng{ 111u };

        const ActorId mother{ 0x00024001u };
        const ActorId sven{ 0x00024002u };
        const ActorId faendal{ 0x00024003u };
        meta.names[mother] = "Camilla";
        meta.names[sven] = "Sven";
        meta.names[faendal] = "Faendal";
        (void)ws.TransitionRelationship(mother, sven, RelationshipStatus::Married, 1.0f, mother, meta);

        // Twin 1 (legitimate with Sven) & Twin 2 (secret with Faendal + phenotype mismatch)
        const auto regSven = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother,
            .bioFather = sven,
            .bioFatherName = "Sven",
            .presumedFatherRaceEditorId = "NordRace",
            .bioFatherRaceEditorId = "NordRace",
            .conceptionTimestamp = 5.0f
        }, meta);
        MARAS_REQUIRE(!regSven.childId.IsNull());
        MARAS_CHECK(!regSven.hasSecret);

        const auto regFaendal = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother,
            .bioFather = faendal,
            .bioFatherName = "Faendal",
            .presumedFatherRaceEditorId = "NordRace",
            .bioFatherRaceEditorId = "WoodElfRace",
            .conceptionTimestamp = 6.0f
        }, meta);
        MARAS_REQUIRE(!regFaendal.childId.IsNull());
        MARAS_CHECK(regFaendal.hasSecret);
        MARAS_CHECK(regFaendal.hasPhenotypeMismatch);

        // Promote specifically Faendal's fetus while leaving Sven's twin Unborn
        ws.settings.Write().paternityPhenotypeBirthChance = 100;
        const auto promo = ApplyPromoteUnbornToInfant(
            ws, mother, 20.0f, "Elvenia", faendal, false, false, meta, rng);
        MARAS_REQUIRE(promo.promoted);
        MARAS_CHECK_EQ(promo.childId, regFaendal.childId);
        MARAS_CHECK(promo.evaluateAtBirth);
        MARAS_CHECK_EQ(ws.lineage.Read().GetUnbornOfMother(mother).size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(ws.lineage.Read().FindChild(regSven.childId)->stage, ChildStage::Unborn);
        MARAS_CHECK_EQ(ws.lineage.Read().FindChild(regFaendal.childId)->stage, ChildStage::Infant);

        // Register a third unborn child (from Faendal again) alongside Sven's remaining unborn child
        const auto regFaendal2 = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother,
            .bioFather = faendal,
            .bioFatherName = "Faendal",
            .conceptionTimestamp = 25.0f
        }, meta);
        MARAS_CHECK_EQ(ws.lineage.Read().GetUnbornOfMother(mother).size(), std::size_t{ 2 });

        // Targeted removal by bioFather removes only Faendal's unborn fetus and reports affected guardians
        std::vector<ActorId> affected;
        const auto removedFaendal = ApplyRemoveUnbornChild(ws, mother, faendal, &affected);
        MARAS_REQUIRE_EQ(removedFaendal.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(removedFaendal[0], regFaendal2.childId);
        MARAS_CHECK(std::find(affected.begin(), affected.end(), mother) != affected.end());
        MARAS_CHECK(std::find(affected.begin(), affected.end(), sven) != affected.end());
        MARAS_CHECK_EQ(ws.lineage.Read().GetUnbornOfMother(mother).size(), std::size_t{ 1 });

        // Wildcard removal (null bioFather) removes remaining unborn children
        const auto removedAll = ApplyRemoveUnbornChild(ws, mother, ActorId{});
        MARAS_REQUIRE_EQ(removedAll.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(removedAll[0], regSven.childId);
        MARAS_CHECK(ws.lineage.Read().GetUnbornOfMother(mother).empty());
    }

    MARAS_TEST(DomainLineage, FertilitySiblingSecretPropagationOnPartnerAndFullResolve) {
        WorldState ws{};
        StubLineageMeta meta{};
        const ActorId mother{ 0x00024101u };
        const ActorId sven{ 0x00024102u };
        const ActorId faendal{ 0x00024103u };
        const ActorId mikael{ 0x00024104u };
        (void)ws.TransitionRelationship(mother, sven, RelationshipStatus::Married, 1.0f, mother, meta);

        const auto fA = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother, .bioFather = faendal, .conceptionTimestamp = 5.0f
        }, meta);
        const auto fB = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother, .bioFather = faendal, .conceptionTimestamp = 5.0f
        }, meta);
        const auto fC = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother, .bioFather = mikael, .conceptionTimestamp = 6.0f
        }, meta);

        // Partner resolves fetus A via confession while still unborn & unannounced
        const auto partnerReveal = ApplyResolvePaternitySecretForPartner(ws, fA.childId, "confess");
        MARAS_CHECK(partnerReveal.updated);
        MARAS_CHECK(partnerReveal.learnedPartner);
        MARAS_CHECK(partnerReveal.wasPregnancySecretForPartner);
        MARAS_CHECK_EQ(partnerReveal.modEventOutcome, "mother_confessed_presumed_unannounced");
        MARAS_CHECK_EQ(partnerReveal.memoryOutcome, "mother_confessed_presumed");

        // All 3 unborn fetuses lose isPregnancySecretForPartner; A & B lose isSecretForPartner, while C keeps it!
        const auto* recA = ws.lineage.Read().FindChild(fA.childId);
        const auto* recB = ws.lineage.Read().FindChild(fB.childId);
        const auto* recC = ws.lineage.Read().FindChild(fC.childId);
        MARAS_REQUIRE(recA && recB && recC);
        MARAS_CHECK(!recA->paternitySecret.isPregnancySecretForPartner);
        MARAS_CHECK(!recB->paternitySecret.isPregnancySecretForPartner);
        MARAS_CHECK(!recC->paternitySecret.isPregnancySecretForPartner);
        MARAS_CHECK(!recA->paternitySecret.isSecretForPartner && recA->paternitySecret.discoveredByPartner);
        MARAS_CHECK(!recB->paternitySecret.isSecretForPartner && recB->paternitySecret.discoveredByPartner);
        MARAS_CHECK(recC->paternitySecret.isSecretForPartner && !recC->paternitySecret.discoveredByPartner);

        // Full resolve on fetus C flips all secret flags for Mikael's fetus
        const auto fullC = ApplyResolvePaternitySecret(ws, fC.childId, "discovered");
        MARAS_CHECK(fullC.updated);
        MARAS_CHECK(fullC.learnedPartner);
        MARAS_CHECK(fullC.learnedBioFather);
        MARAS_CHECK_EQ(fullC.modEventOutcome, "presumed_father_learnt");
        MARAS_CHECK(!ws.lineage.Read().FindChild(fC.childId)->paternitySecret.HasActiveSecret());
    }

    MARAS_TEST(DomainLineage, FertilityResolveForBioFatherAndSetSecretEdgesBypassCooldown) {
        WorldState ws{};
        StubLineageMeta meta{};
        const ActorId mother{ 0x00024201u };
        const ActorId sven{ 0x00024202u };
        const ActorId faendal{ 0x00024203u };
        (void)ws.TransitionRelationship(mother, sven, RelationshipStatus::Married, 1.0f, mother, meta);

        const auto fA = ApplyRegisterUnbornChild(ws, UnbornChildRegistrationInput{
            .bioMother = mother, .bioFather = faendal, .conceptionTimestamp = 12.0f
        }, meta);

        // Partial bio-father reveal clears only bioFather edge; partner & master isSecret remain true
        const auto bioOut = ApplyResolvePaternitySecretForBioFather(ws, fA.childId, "chat");
        MARAS_CHECK(bioOut.updated);
        MARAS_CHECK(bioOut.learnedBioFather);
        MARAS_CHECK(!bioOut.learnedPartner);
        MARAS_CHECK_EQ(bioOut.modEventOutcome, "bio_father_learnt");
        {
            const auto* rec = ws.lineage.Read().FindChild(fA.childId);
            MARAS_CHECK(!rec->paternitySecret.isSecretForBioFather);
            MARAS_CHECK(rec->paternitySecret.discoveredByBioFather);
            MARAS_CHECK(rec->paternitySecret.isSecretForPartner);
            MARAS_CHECK(rec->paternitySecret.isSecret);
        }

        // Resolving partner edge now transitions master isSecret -> false & discovered -> true
        const auto pOut = ApplySetPaternitySecretEdges(ws, fA.childId, std::nullopt, false, std::nullopt, true);
        MARAS_CHECK(pOut.updated);
        MARAS_CHECK(pOut.learnedPartner);
        MARAS_CHECK_EQ(pOut.modEventOutcome, "presumed_father_learnt_unannounced");
        {
            const auto* rec = ws.lineage.Read().FindChild(fA.childId);
            MARAS_CHECK(!rec->paternitySecret.HasActiveSecret());
            MARAS_CHECK(rec->paternitySecret.discovered);
            MARAS_CHECK_EQ(rec->paternitySecret.lastCheckTimestamp, 0.0f);
        }
    }

    MARAS_TEST(DomainLineage, FertilityBioFatherStepInCandidatesFilters) {
        WorldState ws{};
        StubLineageMeta meta{};

        const ActorId singleMother{ 0x00024301u };
        const ActorId partneredMother{ 0x00024302u };
        const ActorId spouse{ 0x00024303u };
        const ActorId eligibleBioFather{ 0x00024304u };
        const ActorId deadBioFather{ 0x00024305u };
        const ActorId partneredBioFather{ 0x00024306u };
        const ActorId bfSpouse{ 0x00024307u };
        const ActorId exBioFather{ 0x00024308u };
        const ActorId activeBioFather{ 0x00024309u };

        (void)ws.TransitionRelationship(partneredMother, spouse, RelationshipStatus::Married, 1.0f, partneredMother, meta);
        (void)ws.TransitionRelationship(partneredBioFather, bfSpouse, RelationshipStatus::Dating, 1.0f, partneredBioFather, meta);
        (void)ws.TransitionRelationship(singleMother, exBioFather, RelationshipStatus::ExLover, 1.0f, singleMother, meta);
        (void)ws.TransitionRelationship(singleMother, activeBioFather, RelationshipStatus::Lover, 1.0f, singleMother, meta);

        // Register candidates on singleMother
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = eligibleBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = ActorId::UnknownFather() }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = ActorId::Player() }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = deadBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = partneredBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = exBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = singleMother, .bioFather = activeBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = partneredMother, .bioFather = eligibleBioFather }, meta);
        (void)ApplyRegisterUnbornChild(ws, { .bioMother = ActorId::Player(), .bioFather = eligibleBioFather }, meta);

        struct DeadAwareMeta final : public IActorMetadataProvider {
            ActorId deadId{};
            bool IsAliveAndValid(ActorId actorId) const noexcept override { return actorId != deadId; }
        } deadMeta{};
        deadMeta.deadId = deadBioFather;

        // Player & partnered mothers always empty
        MARAS_CHECK(CollectBioFatherStepInCandidates(ws.Snapshot(), ActorId::Player(), deadMeta).empty());
        MARAS_CHECK(CollectBioFatherStepInCandidates(ws.Snapshot(), partneredMother, deadMeta).empty());

        // Single mother collects ONLY eligibleBioFather (filtering UnknownFather, Player, dead, partnered, Ex, Active)
        const auto cands = CollectBioFatherStepInCandidates(ws.Snapshot(), singleMother, deadMeta);
        MARAS_REQUIRE_EQ(cands.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(cands[0], eligibleBioFather);
    }

    MARAS_TEST(DomainLineage, FertilityPaternitySecretFallbackBreakup) {
        struct FixedZeroRng final : public IRng {
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        };

        StubLineageMeta meta{};
        FixedZeroRng alwaysRollRng{};
        const ActorId lover{ 0x00024499u };

        // Closed Married / Engaged / Dating -> Divorced / Jilted / ExDating on fallback breakup roll
        for (auto [startStatus, expectedBreakup] : std::array<std::pair<RelationshipStatus, RelationshipStatus>, 3>{ {
                 { RelationshipStatus::Married, RelationshipStatus::Divorced },
                 { RelationshipStatus::Engaged, RelationshipStatus::Jilted },
                 { RelationshipStatus::Dating, RelationshipStatus::ExDating } } }) {
            WorldState ws{};
            ws.settings.Write().paternityNonSkyrimNetRevealChance = 100;
            ws.settings.Write().paternityBreakupChance = 50;
            const ActorId mother{ 0x00024401u };
            const ActorId partner{ 0x00024402u };
            (void)ws.TransitionRelationship(mother, partner, startStatus, 1.0f, mother, meta);
            const auto reg = ApplyRegisterUnbornChild(ws, {
                .bioMother = mother, .bioFather = lover, .conceptionTimestamp = 5.0f
            }, meta);

            const auto out = ApplyPaternitySecretFallback(ws, reg.childId, 25.0f, meta, alwaysRollRng);
            MARAS_CHECK(out.evaluated);
            MARAS_CHECK(out.revealed);
            MARAS_CHECK(out.brokeUp);
            MARAS_CHECK_EQ(ws.relationships.Read().GetStatus(mother, partner), expectedBreakup);
        }

        // Open Married -> revealed, but never breaks up
        {
            WorldState ws{};
            ws.settings.Write().paternityNonSkyrimNetRevealChance = 100;
            ws.settings.Write().paternityBreakupChance = 100;
            const ActorId mother{ 0x00024411u };
            const ActorId partner{ 0x00024412u };
            (void)ws.TransitionRelationship(mother, partner, RelationshipStatus::Married, 1.0f, mother, meta);
            (void)ws.SetPairOpen(mother, partner, true, 2.0f);
            const auto reg = ApplyRegisterUnbornChild(ws, {
                .bioMother = mother, .bioFather = lover, .conceptionTimestamp = 5.0f
            }, meta);

            const auto out = ApplyPaternitySecretFallback(ws, reg.childId, 25.0f, meta, alwaysRollRng);
            MARAS_CHECK(out.evaluated);
            MARAS_CHECK(out.revealed);
            MARAS_CHECK(!out.brokeUp);
            MARAS_CHECK_EQ(ws.relationships.Read().GetStatus(mother, partner), RelationshipStatus::Married);
        }
    }

    MARAS_TEST(DomainLineage, FertilityVisualDiscrepancyDescriptionsAllTiers) {
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("ArgonianRace"), PhenotypeTier::Argonian);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("OrcRace"), PhenotypeTier::Orc);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("BretonRace"), PhenotypeTier::PaleHuman);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("ElderRace"), PhenotypeTier::PaleHuman);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("DarkElfRace"), PhenotypeTier::Mer);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID("CustomAlienRace"), PhenotypeTier::Unknown);
        MARAS_CHECK_EQ(ClassifyPhenotypeTierFromEditorID(""), PhenotypeTier::Unknown);

        MARAS_CHECK_EQ(GetVisualDiscrepancyDescription(PhenotypeTier::PaleHuman, PhenotypeTier::PaleHuman, "Elvenia"), "");
        MARAS_CHECK_EQ(GetVisualDiscrepancyDescription(PhenotypeTier::Unknown, PhenotypeTier::Argonian, "Elvenia"), "");
        MARAS_CHECK(GetVisualDiscrepancyDescription(PhenotypeTier::Argonian, PhenotypeTier::PaleHuman, "Keshu").find("scales") != std::string::npos);
        MARAS_CHECK(GetVisualDiscrepancyDescription(PhenotypeTier::Khajiit, PhenotypeTier::PaleHuman, "J'zargo").find("fur") != std::string::npos);
        MARAS_CHECK(GetVisualDiscrepancyDescription(PhenotypeTier::Orc, PhenotypeTier::PaleHuman, "Ghorbash").find("tusks") != std::string::npos);
        MARAS_CHECK(GetVisualDiscrepancyDescription(PhenotypeTier::Mer, PhenotypeTier::PaleHuman, "Elvenia").find("Elven") != std::string::npos);
        MARAS_CHECK(GetVisualDiscrepancyDescription(PhenotypeTier::Redguard, PhenotypeTier::PaleHuman, "Amren").find("complexion") != std::string::npos);
    }

    MARAS_TEST(DomainLineage, PaternityActionRules_RevealDirectiveAndTargetClassification) {
        using namespace MARAS::Domain::Lineage;
        using MARAS::Foundation::ActorId;

        const auto bothAll = ParsePaternityRevealDirective("  Both  ", "all", "true_paternity");
        MARAS_CHECK(bothAll.isBoth);
        MARAS_CHECK(!bothAll.isPregnancyOnly);
        MARAS_CHECK(bothAll.resolveAllMatches);
        MARAS_CHECK(bothAll.fatherLookupQuery.empty());

        const auto pregOnly = ParsePaternityRevealDirective("Marcurio", "all", "pregnancy_only");
        MARAS_CHECK(!pregOnly.isBoth);
        MARAS_CHECK(pregOnly.isPregnancyOnly);
        MARAS_CHECK(!pregOnly.resolveAllMatches);
        MARAS_CHECK_EQ(pregOnly.fatherLookupQuery, std::string("Marcurio"));

        const auto announcePreg = ParsePaternityRevealDirective("Husband", "Sven Jr", "announce_pregnancy");
        MARAS_CHECK(announcePreg.isPregnancyOnly);
        const auto hidePat = ParsePaternityRevealDirective("targetFather_pregnancy_only", "all", "hide_paternity");
        MARAS_CHECK(hidePat.isPregnancyOnly);
        MARAS_CHECK(!hidePat.resolveAllMatches);

        // Exact ActorId / display-name identity overrides substring matches ("Fabio" contains "bio")
        const auto namedDirective = ParsePaternityRevealDirective("Fabio", "Baby", "true_paternity");
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                namedDirective, ActorId{ 0x101u }, "Fabio",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::PresumedPartner);
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                namedDirective, ActorId{}, "Fabio",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::PresumedPartner);
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                namedDirective, ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::BioFather);

        // Role substring heuristics & unknown fallback
        const auto heuristicDirective = ParsePaternityRevealDirective("bio father", "Baby", "true_paternity");
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                heuristicDirective, ActorId{}, "the biological father",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::BioFather);
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                heuristicDirective, ActorId{}, "presumed husband",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::PresumedPartner);
        MARAS_CHECK_EQ(
            ClassifyPaternityRevealTarget(
                heuristicDirective, ActorId{}, "someone unknown",
                ActorId{ 0x202u }, "Vorstag",
                ActorId{ 0x101u }, "Fabio"),
            PaternityRevealTarget::Both);

        // Discovery target classification (BioFather must not be misclassified when partner edge is also secret)
        MARAS_CHECK_EQ(
            ClassifyPaternityDiscoveryTarget(ActorId{ 0x101u }, ActorId{ 0x101u }, ActorId{ 0x202u }, true),
            PaternityDiscoveryTarget::PresumedPartner);
        MARAS_CHECK_EQ(
            ClassifyPaternityDiscoveryTarget(ActorId{ 0x202u }, ActorId{ 0x101u }, ActorId{ 0x202u }, true),
            PaternityDiscoveryTarget::BioFather);
        MARAS_CHECK_EQ(
            ClassifyPaternityDiscoveryTarget(ActorId{ 0x303u }, ActorId{ 0x101u }, ActorId{ 0x202u }, true),
            PaternityDiscoveryTarget::PresumedPartner);
        MARAS_CHECK_EQ(
            ClassifyPaternityDiscoveryTarget(ActorId{ 0x303u }, ActorId{ 0x101u }, ActorId{ 0x202u }, false),
            PaternityDiscoveryTarget::BioFather);
    }

    MARAS_TEST(DomainLineage, FertilityLifecycleRules_ConceptionBatchLossAndSpawnPlans) {
        // 1. Provider parsing, FMR visible rank window, and BF cycle state actions
        MARAS_CHECK_EQ(ParseFertilityProvider("fmr"), FertilityProvider::Fmr);
        MARAS_CHECK_EQ(ParseFertilityProvider("FertilityMode"), FertilityProvider::Fmr);
        MARAS_CHECK_EQ(ParseFertilityProvider("BF"), FertilityProvider::BeeingFemale);
        MARAS_CHECK_EQ(ParseFertilityProvider("BeeingFemale"), FertilityProvider::BeeingFemale);
        MARAS_CHECK_EQ(ParseFertilityProvider("other"), FertilityProvider::Unknown);

        MARAS_CHECK(!IsFmrVisiblePregnancyRank(0));
        MARAS_CHECK(!IsFmrVisiblePregnancyRank(33));
        MARAS_CHECK(IsFmrVisiblePregnancyRank(34));
        MARAS_CHECK(IsFmrVisiblePregnancyRank(100));
        MARAS_CHECK(!IsFmrVisiblePregnancyRank(101));
        MARAS_CHECK(IsFmrVisiblePregnancyRank(120));

        MARAS_CHECK_EQ(ClassifyBfCycleState(4), BfCycleStateAction::None);
        MARAS_CHECK_EQ(ClassifyBfCycleState(5), BfCycleStateAction::VisibleTrimesterReveal);
        MARAS_CHECK_EQ(ClassifyBfCycleState(6), BfCycleStateAction::VisibleTrimesterReveal);
        MARAS_CHECK_EQ(ClassifyBfCycleState(7), BfCycleStateAction::VisibleTrimesterReveal);
        MARAS_CHECK_EQ(ClassifyBfCycleState(8), BfCycleStateAction::RecoveryDelivery);
        MARAS_CHECK_EQ(ClassifyBfCycleState(9), BfCycleStateAction::None);

        // 2. Conception gating: Unborn blocks both; FMR also blocks on "Newborn" infant whereas BF allows it
        const ActorId mother{ 0x00020100u };
        const ActorId bioFather1{ 0x00020200u };
        const ActorId bioFather2{ 0x00020300u };
        const ActorId bioFatherNtr{ 0x00020400u };
        const ActorId spouse{ 0x00020500u };
        const ChildId unbornConception{ 0xFF700009u };
        const ChildId infantNewborn{ 0xFF700010u };

        WorldState ws{};
        MARAS_CHECK(CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::Fmr));
        MARAS_CHECK(CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::BeeingFemale));

        {
            ChildRecord existingUnborn{};
            existingUnborn.childId = unbornConception;
            existingUnborn.bioMother = mother;
            existingUnborn.bioFather = bioFather1;
            existingUnborn.stage = ChildStage::Unborn;
            ws.lineage.Write().UpsertChild(existingUnborn);
        }
        MARAS_CHECK(!CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::Fmr));
        MARAS_CHECK(!CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::BeeingFemale));

        ws.lineage.Write().EraseChild(unbornConception);
        {
            ChildRecord newborn{};
            newborn.childId = infantNewborn;
            newborn.bioMother = mother;
            newborn.bioFather = bioFather1;
            newborn.name = "Newborn";
            newborn.stage = ChildStage::Infant;
            ws.lineage.Write().UpsertChild(newborn);
        }
        MARAS_CHECK(HasDuplicateFmrNewbornInfant(ws.lineage.Read(), mother));
        MARAS_CHECK(!CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::Fmr));
        MARAS_CHECK(CanRegisterFertilityConception(ws.Snapshot(), mother, FertilityProvider::BeeingFemale));

        // 3. Affection & SkyrimNet partner routing
        {
            const auto t1 = ResolveFertilityAffectionTarget(ActorId::Player(), spouse, bioFather1);
            MARAS_CHECK_EQ(t1.targetActor, spouse);
            MARAS_CHECK(t1.isPlayerPregnant);

            const auto t2 = ResolveFertilityAffectionTarget(ActorId::Player(), ActorId{}, bioFather1);
            MARAS_CHECK_EQ(t2.targetActor, bioFather1);
            MARAS_CHECK(t2.isPlayerPregnant);

            const auto t3 = ResolveFertilityAffectionTarget(mother, ActorId::Player(), bioFather1);
            MARAS_CHECK_EQ(t3.targetActor, mother);
            MARAS_CHECK(!t3.isPlayerPregnant);

            const auto t4 = ResolveFertilityAffectionTarget(mother, ActorId{}, ActorId::Player());
            MARAS_CHECK_EQ(t4.targetActor, mother);
            MARAS_CHECK(!t4.isPlayerPregnant);

            const auto t5 = ResolveFertilityAffectionTarget(mother, spouse, ActorId::Player());
            MARAS_CHECK(t5.targetActor.IsNull());

            MARAS_CHECK_EQ(ResolveFertilityEventPartner(spouse, bioFather1), spouse);
            MARAS_CHECK_EQ(ResolveFertilityEventPartner(ActorId{}, bioFather1), bioFather1);
            MARAS_CHECK(ResolveFertilityEventPartner(ActorId{}, ActorId::UnknownFather()).IsNull());
        }

        // 4. Multi-fetus labor session dedup, Baby NTR correction, delivery batch progress & stillbirth flush
        {
            LineageTable lineage{};
            const ChildId unbornA{ 0xFF700020u };
            const ChildId unbornB{ 0xFF700021u };
            const ChildId bornTwinA{ 0xFF700022u };

            ChildRecord u1{};
            u1.childId = unbornA;
            u1.bioMother = mother;
            u1.bioFather = bioFather1;
            u1.stage = ChildStage::Unborn;
            lineage.UpsertChild(u1);

            ChildRecord u2{};
            u2.childId = unbornB;
            u2.bioMother = mother;
            u2.bioFather = bioFather2;
            u2.stage = ChildStage::Unborn;
            lineage.UpsertChild(u2);

            const auto begin1 = ApplyBeginLaborBatch(lineage, mother, 1, 10.0f);
            MARAS_CHECK(begin1.startedNewBatch);
            MARAS_CHECK_EQ(begin1.totalCount, 2);

            const auto begin2 = ApplyBeginLaborBatch(lineage, mother, 2, 10.2f);
            MARAS_CHECK(!begin2.startedNewBatch);
            MARAS_CHECK_EQ(begin2.totalCount, 2);

            // BioFather matching unbornB requires no NTR correction; unmatched bioFatherNtr selects lowest unborn ID
            MARAS_CHECK(ResolveBabyNtrCorrectionTarget(lineage, mother, bioFather2).IsNull());
            MARAS_CHECK_EQ(ResolveBabyNtrCorrectionTarget(lineage, mother, bioFatherNtr), unbornA);

            // Twin A delivered -> 1 unborn remains, batch not yet completed
            lineage.EraseChild(unbornA);
            ChildRecord i1{};
            i1.childId = bornTwinA;
            i1.bioMother = mother;
            i1.bioFather = bioFather1;
            i1.name = "TwinA";
            i1.stage = ChildStage::Infant;
            lineage.UpsertChild(i1);

            const auto step1 = ApplyRecordDeliveredChild(lineage, mother, bornTwinA, true);
            MARAS_CHECK(!step1.batchCompleted);
            MARAS_CHECK_EQ(step1.bornSoFar, 1);
            MARAS_CHECK_EQ(step1.laborTotal, 2);
            MARAS_CHECK_EQ(FindSyntheticInfantForSpawn(lineage, mother, "twina"), bornTwinA);

            // Second fetus lost to stillbirth -> last unborn removed -> popping batch yields TwinA and clears batch
            lineage.EraseChild(unbornB);
            MARAS_CHECK(lineage.GetUnbornOfMother(mother).empty());
            const auto popped = ApplyPopDeliveryBatch(lineage, mother);
            MARAS_REQUIRE(popped.has_value());
            MARAS_REQUIRE_EQ(popped->bornChildIds.size(), std::size_t{ 1 });
            MARAS_CHECK_EQ(popped->bornChildIds[0], bornTwinA);
            MARAS_CHECK(lineage.FindActiveDeliveryBatch(mother) == nullptr);
        }

        // 5. Spawn lineage plan (guardian precedence, bioFather co-spouse fallback, NPC guardian pair)
        {
            MockFormIdResolver resolver{};
            resolver.sexes[ActorId::Player()] = ActorSex::Male;
            resolver.sexes[spouse] = ActorSex::Female;
            resolver.sexes[mother] = ActorSex::Female;
            resolver.sexes[bioFather1] = ActorSex::Male;

            WorldState planWs{};
            (void)planWs.PromoteToMarried(ActorId::Player(), spouse, 5.0f, ActorId::Player(), resolver);
            (void)planWs.PromoteToMarried(mother, bioFather1, 6.0f, mother, resolver);

            // Case A: bioMother == spouse (married to player), no preset guardians -> player child with coSpouse = spouse
            const ChildId playerTwin{ 0xFF700030u };
            ChildRecord recA{};
            recA.childId = playerTwin;
            recA.bioMother = spouse;
            recA.bioFather = ActorId::Player();
            recA.stage = ChildStage::Child;
            planWs.lineage.Write().UpsertChild(recA);
            const auto planA = PlanSpawnedChildLineage(planWs.Snapshot(), playerTwin, spouse, ActorId::Player());
            MARAS_CHECK(planA.isPlayerChild);
            MARAS_CHECK_EQ(planA.coSpouse, spouse);

            // Case B: guardians already cleared/unset on unborn/infant with bioFather == spouse married to player -> coSpouse = spouse
            const auto planB = PlanSpawnedChildLineage(planWs.Snapshot(), ChildId{}, mother, spouse);
            MARAS_CHECK(planB.isPlayerChild);
            MARAS_CHECK_EQ(planB.coSpouse, spouse);

            // Case C: NPC married couple without preset guardians -> non-player child with guardianA = mother, guardianB = bioFather1
            const ChildId npcChild{ 0xFF700031u };
            ChildRecord recC{};
            recC.childId = npcChild;
            recC.bioMother = mother;
            recC.bioFather = bioFather1;
            recC.stage = ChildStage::Child;
            planWs.lineage.Write().UpsertChild(recC);
            const auto planC = PlanSpawnedChildLineage(planWs.Snapshot(), npcChild, mother, bioFather1);
            MARAS_CHECK(!planC.isPlayerChild);
            MARAS_CHECK_EQ(planC.npcGuardianA, mother);
            MARAS_CHECK_EQ(planC.npcGuardianB, bioFather1);
            MARAS_CHECK_EQ(planC.tenantHouseLookupActor, mother);

            // Case D: Preset guardians on synthetic record override bio-parent inference
            ChildRecord recD{};
            recD.childId = ChildId{ 0xFF700032u };
            (void)recD.guardians.Assign(bioFather1, bioFather2);
            recD.bioMother = spouse;
            recD.bioFather = ActorId::Player();
            planWs.lineage.Write().UpsertChild(recD);
            const auto planD = PlanSpawnedChildLineage(planWs.Snapshot(), recD.childId, ActorId{}, ActorId{});
            MARAS_CHECK(!planD.isPlayerChild);
            MARAS_CHECK_EQ(planD.npcGuardianA, bioFather1);
            MARAS_CHECK_EQ(planD.npcGuardianB, bioFather2);
        }

        // 6. Adult graduation source resolution
        {
            LineageTable lineage{};
            const ChildId synKid{ 0xFF700040u };
            const ActorId oldChildActor{ 0x00080010u };
            const ActorId newAdultActor{ 0x00080020u };

            ChildRecord synRec{};
            synRec.childId = synKid;
            synRec.bioMother = mother;
            synRec.bioFather = bioFather1;
            synRec.name = "Alesan";
            synRec.stage = ChildStage::Child;
            lineage.UpsertChild(synRec);

            // Matches synthetic child by mother+name and does not overwrite with adult ID
            const auto g1 = ResolveAdultGraduationSource(
                lineage, newAdultActor, ActorId{}, mother, bioFather2, "Alesan", false);
            MARAS_CHECK_EQ(g1.previousId, synKid);
            MARAS_CHECK_EQ(g1.bioMother, mother);
            MARAS_CHECK_EQ(g1.bioFather, bioFather1);
            MARAS_CHECK(!g1.alreadyAdultSameActor);

            // Unmatched with allowFallbackToAdultId=false stays empty; true falls back to adultActor
            const auto gNoFallback = ResolveAdultGraduationSource(
                lineage, newAdultActor, ActorId{}, mother, bioFather2, "Unknown", false);
            MARAS_CHECK(gNoFallback.previousId.IsNull());

            const auto gFallback = ResolveAdultGraduationSource(
                lineage, newAdultActor, ActorId{}, mother, bioFather2, "Unknown", true);
            MARAS_CHECK_EQ(gFallback.previousId, ChildId{ newAdultActor.raw() });
            MARAS_CHECK(!gFallback.alreadyAdultSameActor);

            // Already-graduated same adult actor sets alreadyAdultSameActor
            ChildRecord adultRec{};
            adultRec.childId = ChildId{ newAdultActor.raw() };
            adultRec.stage = ChildStage::Adult;
            lineage.UpsertChild(adultRec);
            const auto gDup = ResolveAdultGraduationSource(
                lineage, newAdultActor, oldChildActor, mother, ActorId{}, "Alesan", true);
            MARAS_CHECK_EQ(gDup.previousId, ChildId{ newAdultActor.raw() });
            MARAS_CHECK(gDup.alreadyAdultSameActor);
        }
    }

    MARAS_TEST(DomainLineage, FertilityBatchWindowSpawnNullMotherAndPaternityDirectiveEdgeCases) {
        const ActorId mother{ 0x00029001u };
        const ActorId npcFather{ 0x00029002u };
        const ActorId datingPartner{ 0x00029003u };

        // 1. Aborted labor >= 0.5f days starts new batch, empty batch pop returns nullopt
        {
            LineageTable lineage{};
            const auto first = ApplyBeginLaborBatch(lineage, mother, 2, 10.0f);
            MARAS_CHECK(first.startedNewBatch);
            MARAS_CHECK_EQ(first.totalCount, 2);

            const auto sameWindow = ApplyBeginLaborBatch(lineage, mother, 1, 10.3f);
            MARAS_CHECK(!sameWindow.startedNewBatch);

            // Empty bornChildIds yields nullopt when popped
            MARAS_CHECK(!ApplyPopDeliveryBatch(lineage, mother).has_value());
            MARAS_CHECK(lineage.FindActiveDeliveryBatch(mother) == nullptr);

            const auto newAfterExpiry = ApplyBeginLaborBatch(lineage, mother, 3, 10.5f);
            MARAS_CHECK(newAfterExpiry.startedNewBatch);
            MARAS_CHECK_EQ(newAfterExpiry.totalCount, 3);
        }

        // 2. PlanSpawnedChildLineage: null mother + unmarried mother Child vs Adult marriage gate
        {
            WorldState ws{};
            DefaultActorMetadataProvider meta{};
            (void)ws.TransitionRelationship(
                mother, datingPartner, RelationshipStatus::Dating, 1.0f, mother, meta);

            const auto nullMotherPlan = PlanSpawnedChildLineage(
                ws.Snapshot(), ChildId{}, ActorId{}, npcFather, ChildStage::Child);
            MARAS_CHECK(!nullMotherPlan.isPlayerChild);
            MARAS_CHECK_EQ(nullMotherPlan.npcGuardianA, npcFather);
            MARAS_CHECK(nullMotherPlan.npcGuardianB.IsNull());
            MARAS_CHECK_EQ(nullMotherPlan.tenantHouseLookupActor, npcFather);

            const auto datingChildPlan = PlanSpawnedChildLineage(
                ws.Snapshot(), ChildId{}, mother, npcFather, ChildStage::Child);
            MARAS_CHECK_EQ(datingChildPlan.npcGuardianA, mother);
            MARAS_CHECK(datingChildPlan.npcGuardianB.IsNull());

            const auto datingAdultPlan = PlanSpawnedChildLineage(
                ws.Snapshot(), ChildId{}, mother, npcFather, ChildStage::Adult);
            MARAS_CHECK_EQ(datingAdultPlan.npcGuardianA, mother);
            MARAS_CHECK_EQ(datingAdultPlan.npcGuardianB, datingPartner);
        }

        // 3. PaternityActionRules::ParsePaternityRevealDirective role keywords & pregnancy_only guard
        {
            for (const char* kw : { "bio", "biological", "presumed", "assumed", "partner", "both" }) {
                const auto dir = ParsePaternityRevealDirective(kw, "all", "full");
                MARAS_CHECK(dir.fatherLookupQuery.empty());
            }
            const auto npcDir = ParsePaternityRevealDirective("  Brynjolf  ", "all", "full");
            MARAS_CHECK_EQ(npcDir.fatherLookupQuery, std::string("Brynjolf"));
            MARAS_CHECK(npcDir.resolveAllMatches);

            const auto pregOnlyDir = ParsePaternityRevealDirective("partner", "all", "pregnancy_only");
            MARAS_CHECK(pregOnlyDir.isPregnancyOnly);
            MARAS_CHECK(!pregOnlyDir.resolveAllMatches);
        }
    }

    MARAS_TEST(DomainLineage, GuardiansPairAndMarriedPlayerSingleParentGuard) {
        const ActorId spouseA{ 0x00029001u };
        const ActorId spouseB{ 0x00029002u };
        const HouseId breezehome{ 0x00029101u };
        const ChildId cid{ 0x00029201u };

        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        ws.settings.Write().enablePlayerPolygamy = true;
        ws.household.Write().RegisterHouse(breezehome, RefId{ 0x00029111u }, true);
        (void)ws.PromoteToMarried(ActorId::Player(), spouseA, 5.0f, ActorId::Player(), meta);
        (void)ws.PromoteToMarried(ActorId::Player(), spouseB, 8.0f, ActorId::Player(), meta);
        ws.household.Write().AssignTenant(spouseB.raw(), breezehome, true, false);

        ChildRecord rec{};
        rec.childId = cid;
        rec.stage = ChildStage::Child;
        rec.guardians.Assign(ActorId::Player(), spouseA);
        ws.lineage.Write().UpsertChild(rec);

        // Removing spouseA when spouseB is also married reassigns co-guardian to spouseB & inherits house
        const auto remA = ApplySetChildGuardian(ws, cid, spouseA, false, meta);
        MARAS_CHECK(remA.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.HasSameSet(ActorId::Player(), spouseB));
        MARAS_CHECK_EQ(ws.household.Read().GetTenantHouse(cid.raw()), breezehome);

        // Divorcing spouseA leaves spouseB as sole remaining living spouse -> cannot clear spouseB into sole Player
        (void)ws.PromoteToDivorced(ActorId::Player(), spouseA, 12.0f, ActorId::Player(), meta);
        const auto blockedClear = ApplySetChildGuardian(ws, cid, spouseB, false, meta);
        MARAS_CHECK(!blockedClear.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.HasSameSet(ActorId::Player(), spouseB));

        // Atomic pair setter passing (Player, 0) while married auto-fills surviving Lead Spouse (spouseB)
        const auto pairSet = ApplySetChildGuardiansPair(ws, cid, ActorId::Player(), ActorId{}, meta);
        MARAS_CHECK(!pairSet.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.HasSameSet(ActorId::Player(), spouseB));

        // Removing Player themselves still transitions cleanly to sole NPC guardian (spouseB)
        const auto remPlayer = ApplySetChildGuardian(ws, cid, ActorId::Player(), false, meta);
        MARAS_CHECK(remPlayer.changed);
        MARAS_CHECK(ws.lineage.Read().FindChild(cid)->guardians.IsSoleGuardian(spouseB));
        MARAS_CHECK(ws.household.Read().GetTenantHouse(cid.raw()).IsNull());
    }

}  // namespace MARAS::Tests
