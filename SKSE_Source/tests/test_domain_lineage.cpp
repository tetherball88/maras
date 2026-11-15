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

}  // namespace MARAS::Tests
