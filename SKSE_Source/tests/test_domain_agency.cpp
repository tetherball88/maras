#include "TestHarness.h"

#include "maras/domain/agency/BanterPlanner.h"
#include "maras/domain/agency/EncounterTemplate.h"
#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/domain/agency/NpcMatchmaker.h"
#include "maras/domain/agency/PartnerAgencyEvaluator.h"
#include "maras/foundation/IRng.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <array>
#include <vector>

namespace MARAS::Tests {

    namespace {
        using namespace MARAS::Foundation;
        using namespace MARAS::State;
        using namespace MARAS::Domain::Agency;

        void AddAdult(WorldState& ws, ActorId id, Temperament temp = Temperament::Independent) {
            ActorProfileRecord rec{};
            rec.refId = id.AsRefId();
            rec.baseId = BaseId{ id.raw() };
            rec.temperament = temp;
            rec.isRegisteredAdult = true;
            ws.actors.Write().Upsert(std::move(rec));
        }

    }  // namespace

    MARAS_TEST(DomainAgency, MatchmakingRulesAndDeterministicDiceRollProposals) {
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Heterosexual, ActorSex::Male, ActorSex::Female));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Heterosexual, ActorSex::Male, ActorSex::Male));
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Homosexual, ActorSex::Female, ActorSex::Female));
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Any, ActorSex::Male, ActorSex::Female));

        WorldState ws{};
        ws.settings.Write().matchmakingEnabled = true;
        ws.settings.Write().matchmakingOrientation = MatchmakingOrientation::Heterosexual;
        ws.settings.Write().matchmakingMaxSuggestions = 1;
        ws.settings.Write().diceRollChanceNewDating = 100;
        ws.settings.Write().diceRollChanceNewEngaged = 0;
        ws.settings.Write().diceRollChanceNewLoverAllowed = 0;
        ws.settings.Write().diceRollChanceNewLoverCheating = 0;
        ws.settings.Write().diceRollChanceAdvance = 0;
        ws.settings.Write().diceRollChanceBreakup = 0;
        ws.settings.Write().diceRollChanceOpenClose = 0;

        const ActorId maleA{ 0x00040001u };
        const ActorId femaleB{ 0x00040002u };
        AddAdult(ws, maleA);
        AddAdult(ws, femaleB);

        const std::vector<NpcMatchmakingProfile> npcs{
            NpcMatchmakingProfile{ maleA, "Farkas", ActorSex::Male, SocialClass::Working, SkillType::Warrior, Temperament::Proud },
            NpcMatchmakingProfile{ femaleB, "Ria", ActorSex::Female, SocialClass::Working, SkillType::Warrior, Temperament::Humble }
        };

        MARAS_CHECK(IsEligibleForScheduledMatchmaking(ws.Snapshot(), maleA, 10.0f));
        DeterministicRng rng{ 2026u };
        const auto proposals = GenerateDiceRollProposals(ws.Snapshot(), npcs, 10.0f, rng);
        MARAS_REQUIRE_EQ(proposals.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(proposals[0].slot.action, MatchmakingActionKind::New);
        MARAS_CHECK_EQ(proposals[0].slot.targetStatus, RelationshipStatus::Dating);

        // Cooldown puts actor on cooldown cleanly
        ws.cooldowns.Write().npcLastMatchmakingChangeDay[maleA] = 9.0f;
        MARAS_CHECK(IsNpcOnMatchmakingCooldown(ws.Snapshot(), maleA, 10.0f));
        MARAS_CHECK(!IsEligibleForScheduledMatchmaking(ws.Snapshot(), maleA, 10.0f));
    }

    MARAS_TEST(DomainAgency, PartnerAgencyEvaluationAndHomeArrivalPriority) {
        WorldState ws{};
        const ActorId partner{ 0x00041001u };
        AddAdult(ws, partner, Temperament::Independent);
        DefaultActorMetadataProvider meta{};
        (void)ws.TransitionRelationship(ActorId::Player(), partner, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        ws.affection.Write().SetPermanent(partner, 20);
        ws.affection.Write().SetLastInteractionDay(partner, 1.0f);

        PartnerAgencyInput input{};
        input.partnerId = partner;
        input.partnerName = "Jenassa";
        input.partnerSex = ActorSex::Female;

        ws.settings.Write().playerPartnerCheatingAgencyEnabled = true;
        input.isPregnantWithPlayerChild = true;
        MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), input, 30.0f, false));
        MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), input, 30.0f, true));

        input.isPregnantWithPlayerChild = false;
        MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), input, 30.0f, false));
        const float chance = ComputePartnerCheatingChance(ws.Snapshot(), input, 30.0f, 0, 0, true, 2);
        MARAS_CHECK(chance > 0.0f);

        // Expired SuggestOpen pending intent forces SeekSecretAffair action
        ws.relationships.Write().SetPendingIntent(partner, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestOpen,
            .createdGameDay = 10.0f
        });
        DeterministicRng rng{ 55u };
        const auto actions = EvaluatePartnerAgencyActions(ws.Snapshot(), { input }, 30.0f, rng);
        MARAS_CHECK(actions.size() >= 2);
        MARAS_CHECK_EQ(actions[0].type, PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair);

        // Home arrival prioritizes active pending forcegreet in same cell over letter roll
        ws.relationships.Write().SetPendingIntent(partner, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestClose,
            .createdGameDay = 29.5f
        });
        SpatialActorSnapshot spatial{};
        LoadedActorSample pSample{};
        pSample.refId = RefId::Player();
        pSample.parentCellId = CellId{ 0x00042001u };
        LoadedActorSample npcSample{};
        npcSample.refId = partner.AsRefId();
        npcSample.parentCellId = CellId{ 0x00042001u };
        spatial.Clear();
        spatial.SetMetadata(30.0f, 1u);
        spatial.AddSample(pSample);
        spatial.AddSample(npcSample);
        spatial.FinalizeSortAndDedupe();

        const auto arrival = EvaluatePartnerHomeArrival(
            ws.Snapshot(), spatial, LocationId{ 0x00042010u }, false, 19, 30.0f, rng);
        MARAS_REQUIRE(arrival.has_value());
        MARAS_CHECK_EQ(arrival->kind, HomeArrivalKind::PendingIntentForcegreet);
        MARAS_CHECK_EQ(arrival->intentType, PartnerIntentType::SuggestClose);
    }

    MARAS_TEST(DomainAgency, EncounterTemplateRenderingAndBanterPlanner) {
        EncounterTemplateSpec tmpl{};
        tmpl.id = "cheating_talking_letter";
        tmpl.modalTone = "danger";
        tmpl.allowLoverNameToken = false;
        tmpl.modalTitle = "Note from {partner}";
        tmpl.defaults = { "Dear {lover}", "Met at {location}.", "See you, {player}.", "- {partner}" };
        tmpl.variants[Temperament::Romantic].salutation = "My dearest {lover}";

        EncounterTemplateCatalog catalog{};
        catalog.SetTemplate(tmpl);
        const auto* found = catalog.FindTemplate("CHEATING_TALKING_LETTER");
        MARAS_REQUIRE(found != nullptr);

        const auto rendered = RenderEncounterTemplate(
            *found, Temperament::Romantic, "Aela", "Hero", "Whiterun", "Farkas");
        // allowLoverNameToken == false masks {lover} as "someone"
        MARAS_CHECK(rendered.letterBody.find("My dearest someone") != std::string::npos);
        MARAS_CHECK(rendered.letterBody.find("Whiterun") != std::string::npos);
        MARAS_CHECK(rendered.letterBody.find("Farkas") == std::string::npos);

        // BanterPlanner filtering & tier priority
        WorldState ws{};
        ws.settings.Write().enableScenes = true;
        ws.settings.Write().enableScenes_LocTypeInn = true;
        ws.settings.Write().enableScenes_LocTypeTemple = false;

        MARAS_CHECK(IsBanterQualifyingStatus(RelationshipStatus::Married));
        MARAS_CHECK(!IsBanterQualifyingStatus(RelationshipStatus::Divorced));
        MARAS_CHECK(AreAllLocationKeywordsAllowedForScenes(
            ws.Snapshot().settings.operator*(), std::array<std::string_view, 1>{ "LocTypeInn" }));
        MARAS_CHECK(!AreAllLocationKeywordsAllowedForScenes(
            ws.Snapshot().settings.operator*(), std::array<std::string_view, 1>{ "LocTypeTemple" }));

        const ActorId a1{ 0x00043001u };
        const ActorId b1{ 0x00043002u };
        const ActorId ghostPartner{ 0x00043003u };
        AddAdult(ws, a1);
        AddAdult(ws, b1);
        AddAdult(ws, ghostPartner);
        DefaultActorMetadataProvider meta{};
        (void)ws.TransitionRelationship(a1, b1, RelationshipStatus::Married, 1.0f, a1, meta);
        (void)ws.TransitionRelationship(a1, ghostPartner, RelationshipStatus::Lover, 1.0f, a1, meta);

        SpatialActorSnapshot spatial{};
        LoadedActorSample player{};
        player.refId = RefId::Player();
        player.flags = LoadedActorFlags::Is3DLoaded | LoadedActorFlags::HasCurrentProcess;
        LoadedActorSample sA{};
        sA.refId = a1.AsRefId();
        sA.flags = LoadedActorFlags::Is3DLoaded | LoadedActorFlags::HasCurrentProcess | LoadedActorFlags::HasActorTypeNpc;
        LoadedActorSample sB{};
        sB.refId = b1.AsRefId();
        sB.flags = LoadedActorFlags::Is3DLoaded | LoadedActorFlags::HasCurrentProcess | LoadedActorFlags::HasActorTypeNpc;
        LoadedActorSample sGhost{};
        sGhost.refId = ghostPartner.AsRefId();
        sGhost.flags = LoadedActorFlags::Is3DLoaded | LoadedActorFlags::HasCurrentProcess | LoadedActorFlags::HasActorTypeNpc | LoadedActorFlags::IsGhost;
        spatial.Clear();
        spatial.SetMetadata(10.0f, 1u);
        spatial.AddSample(player);
        spatial.AddSample(sA);
        spatial.AddSample(sB);
        spatial.AddSample(sGhost);
        spatial.FinalizeSortAndDedupe();

        const auto cands = FindBanterCoupleCandidates(ws.Snapshot(), spatial, 3000.0f, 10.0f);
        MARAS_REQUIRE_EQ(cands.size(), std::size_t{ 2 });  // (a1,b1)=tier 1; (a1,ghostPartner)=tier 2 (ghost excluded from nearby)
        DeterministicRng rng{ 1u };
        const auto picked = SelectPrioritizedCouple(cands, rng);
        MARAS_REQUIRE(picked.has_value());
        MARAS_CHECK_EQ(picked->tier, 1);
    }

    MARAS_TEST(DomainAgency, AllEncounterTemplatesAndLoverMaskingVariants) {
        EncounterTemplateCatalog catalog{};

        // 1. Open-date away letter permits explicit {lover} token and merges variant overrides cleanly.
        EncounterTemplateSpec openDate{};
        openDate.id = "open_date_away";
        openDate.modalTone = "ai";
        openDate.modalSize = "md";
        openDate.allowLoverNameToken = true;
        openDate.modalTitle = "Note from {partner}";
        openDate.modalSubtitle = "Addressed to {player}";
        openDate.envelopeMessage = "Signed by {partner} before heading to {location}.";
        openDate.defaults = {
            "Dear {player},",
            "Stepped out to meet {lover}.",
            "Find us at {location}.",
            "- {partner}"
        };
        openDate.variants[Temperament::Proud].salutation = "{player},";
        openDate.variants[Temperament::Humble].premise = "Hoping you don't mind my time with {lover}.";

        // 2. Cheating talking & physical letters must ALWAYS mask {lover} across every rendered field.
        EncounterTemplateSpec cheatingTalking{};
        cheatingTalking.id = "cheating_talking_letter";
        cheatingTalking.modalTone = "warning";
        cheatingTalking.allowLoverNameToken = false;
        cheatingTalking.modalTitle = "Half-Burnt Note for {partner} (from {lover})";
        cheatingTalking.modalSubtitle = "Hearth near {location}";
        cheatingTalking.defaults = { "Dear {partner}", "Spoke with {lover}", "Near {location}", "Burned" };
        cheatingTalking.variants[Temperament::Jealous].salutation = "{partner} & {lover},";

        EncounterTemplateSpec cheatingPhysical{};
        cheatingPhysical.id = "cheating_physical_letter";
        cheatingPhysical.modalTone = "danger";
        cheatingPhysical.allowLoverNameToken = false;
        cheatingPhysical.modalTitle = "Charred Letter";
        cheatingPhysical.defaults = { "My {partner}", "Tonight with {lover}", "At {location}", "Charred" };
        cheatingPhysical.variants[Temperament::Independent].clue = "Slip toward {location} away from {player}.";

        catalog.SetTemplate(std::move(openDate));
        catalog.SetTemplate(std::move(cheatingTalking));
        catalog.SetTemplate(std::move(cheatingPhysical));

        const auto* tOpen = catalog.FindTemplate("open_date_away");
        const auto* tTalk = catalog.FindTemplate("CHEATING_TALKING_LETTER");
        const auto* tPhys = catalog.FindTemplate("cheating_physical_letter");
        MARAS_REQUIRE(tOpen != nullptr);
        MARAS_REQUIRE(tTalk != nullptr);
        MARAS_REQUIRE(tPhys != nullptr);

        // Open-date Proud preserves default premise/clue/signOff while replacing salutation
        const auto rOpenProud = RenderEncounterTemplate(
            *tOpen, Temperament::Proud, "Aela", "Dragonborn", "The Bannered Mare", "Farkas");
        MARAS_CHECK(rOpenProud.letterBody.find("Dragonborn,\n\nStepped out to meet Farkas.") != std::string::npos);
        MARAS_CHECK(rOpenProud.letterBody.find("Find us at The Bannered Mare.") != std::string::npos);
        MARAS_CHECK(rOpenProud.letterBody.find("- Aela") != std::string::npos);

        // Open-date Humble overrides premise while falling back to default salutation
        const auto rOpenHumble = RenderEncounterTemplate(
            *tOpen, Temperament::Humble, "Aela", "Dragonborn", "The Bannered Mare", "Farkas");
        MARAS_CHECK(rOpenHumble.letterBody.find("Dear Dragonborn,") != std::string::npos);
        MARAS_CHECK(rOpenHumble.letterBody.find("Hoping you don't mind my time with Farkas.") != std::string::npos);

        // Cheating talking letter never reveals lover name even if {lover} appears in variant or modalTitle
        const auto rTalkJealous = RenderEncounterTemplate(
            *tTalk, Temperament::Jealous, "Camilla", "Hero", "Riverwood Trader", "Sven");
        MARAS_CHECK(rTalkJealous.modalTitle.find("Sven") == std::string::npos);
        MARAS_CHECK(rTalkJealous.modalTitle.find("someone") != std::string::npos);
        MARAS_CHECK(rTalkJealous.letterBody.find("Sven") == std::string::npos);
        MARAS_CHECK(rTalkJealous.letterBody.find("Camilla & someone,") != std::string::npos);

        // Cheating physical letter never reveals lover name and overrides clue cleanly
        const auto rPhysIndep = RenderEncounterTemplate(
            *tPhys, Temperament::Independent, "Mjoll", "Hero", "Riften", "Aerin");
        MARAS_CHECK(rPhysIndep.modalTone == "danger");
        MARAS_CHECK(rPhysIndep.letterBody.find("Aerin") == std::string::npos);
        MARAS_CHECK(rPhysIndep.letterBody.find("Tonight with someone") != std::string::npos);
        MARAS_CHECK(rPhysIndep.letterBody.find("Slip toward Riften away from Hero.") != std::string::npos);
    }

    MARAS_TEST(DomainAgency, PartnerAgencyCheatingImmunitiesMatrix) {
        DefaultActorMetadataProvider meta{};
        auto makeBaseState = [&](ActorId partner) {
            WorldState ws{};
            AddAdult(ws, partner, Temperament::Proud);
            (void)ws.TransitionRelationship(ActorId::Player(), partner, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
            ws.settings.Write().playerPartnerCheatingAgencyEnabled = true;
            ws.settings.Write().playerPartnerCheatingAllowWhenHappy = false;
            ws.settings.Write().playerPartnerCheatingMaxSecretLovers = 1;
            ws.settings.Write().lonelinessThresholdDays = 2;
            ws.settings.Write().multContentNeglectStart = 2.0f;  // content neglect threshold = 4.0 days
            ws.affection.Write().SetPermanent(partner, 40);
            ws.affection.Write().SetLastInteractionDay(partner, 1.0f);
            return ws;
        };

        const ActorId partner{ 0x00044001u };
        const PartnerAgencyInput baseInput{
            .partnerId = partner,
            .partnerName = "Ysolda",
            .partnerSex = ActorSex::Female,
            .isFollowerOrNearPlayer = false,
            .isPregnantWithPlayerChild = false
        };

        // 1. Feature disabled -> blocked
        {
            auto ws = makeBaseState(partner);
            ws.settings.Write().playerPartnerCheatingAgencyEnabled = false;
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
        }

        // 2. Deceased partner -> blocked
        {
            auto ws = makeBaseState(partner);
            DeceasedActorRecord dead{};
            dead.refId = partner.AsRefId();
            dead.baseId = BaseId{ partner.raw() };
            dead.deathTimestamp = 5.0f;
            ws.deceased.Write().UpsertRecord(std::move(dead));
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
        }

        // 3. Non-main partner (Lover / Candidate) -> blocked
        {
            WorldState ws{};
            AddAdult(ws, partner);
            ws.settings.Write().playerPartnerCheatingAgencyEnabled = true;
            ws.affection.Write().SetPermanent(partner, 20);
            (void)ws.TransitionRelationship(ActorId::Player(), partner, RelationshipStatus::Lover, 1.0f, ActorId::Player(), meta);
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
        }

        // 4. Follower or physically near player -> blocked (even with forced bypass)
        {
            auto ws = makeBaseState(partner);
            auto nearInput = baseInput;
            nearInput.isFollowerOrNearPlayer = true;
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), nearInput, 20.0f, false));
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), nearInput, 20.0f, true));
        }

        // 5. Matchmaking cooldown -> blocked normally, bypassed by expired SuggestOpen
        {
            auto ws = makeBaseState(partner);
            ws.settings.Write().matchmakingNpcCooldownDays = 14;
            ws.cooldowns.Write().npcLastMatchmakingChangeDay[partner] = 15.0f;
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f, false));
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f, true));
        }

        // 6. Ongoing secret lovers cap (clamped 1..3)
        {
            auto ws = makeBaseState(partner);
            ws.settings.Write().playerPartnerCheatingMaxSecretLovers = 0;  // clamps to 1
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
            (void)ws.RecordCheatingIncident(
                partner, ActorId::Player(), ActorId{ 0x00044011u }, true,
                AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 10.0f);
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));

            // Raise to 99 -> clamps to 3
            ws.settings.Write().playerPartnerCheatingMaxSecretLovers = 99;
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
            (void)ws.RecordCheatingIncident(
                partner, ActorId::Player(), ActorId{ 0x00044012u }, true,
                AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 11.0f);
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
            (void)ws.RecordCheatingIncident(
                partner, ActorId::Player(), ActorId{ 0x00044013u }, true,
                AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 12.0f);
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f));
        }

        // 7. Pregnant with player child -> blocked even when forced
        {
            auto ws = makeBaseState(partner);
            auto pregInput = baseInput;
            pregInput.isPregnantWithPlayerChild = true;
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), pregInput, 20.0f, false));
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), pregInput, 20.0f, true));
        }

        // 8. Happy tier (>=75) & Content tier (50..74) neglect boundaries
        {
            auto ws = makeBaseState(partner);
            ws.affection.Write().SetPermanent(partner, 80);
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f, false));
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f, true));
            ws.settings.Write().playerPartnerCheatingAllowWhenHappy = true;
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 20.0f, false));

            ws.settings.Write().playerPartnerCheatingAllowWhenHappy = false;
            ws.affection.Write().SetPermanent(partner, 60);
            ws.affection.Write().SetLastInteractionDay(partner, 10.0f);
            // Content neglect start = 4.0 days; 14.0 - 10.0 == 4.0 -> still <= 4.0 -> blocked
            MARAS_CHECK(!IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 14.0f, false));
            // 15.0 - 10.0 == 5.0 > 4.0 -> allowed
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 15.0f, false));
            // Or bypassed even before 4 days
            MARAS_CHECK(IsPartnerAgencyCheatingAllowedByImmunities(ws.Snapshot(), baseInput, 12.0f, true));
        }
    }

    MARAS_TEST(DomainAgency, PartnerAgencyCheatingChanceTemperamentsStagesAndCaps) {
        DefaultActorMetadataProvider meta{};
        const ActorId partner{ 0x00044101u };
        PartnerAgencyInput input{ .partnerId = partner, .partnerName = "Camilla", .partnerSex = ActorSex::Female };

        auto compute = [&](Temperament temp, RelationshipStatus status, int aff, float daysSince,
                           int secretCount, int childrenCount, bool playerCheating, int rank,
                           bool allowHappy = false, int maxCap = 65) {
            WorldState ws{};
            AddAdult(ws, partner, temp);
            (void)ws.TransitionRelationship(ActorId::Player(), partner, status, 1.0f, ActorId::Player(), meta);
            auto& s = ws.settings.Write();
            s.lonelinessThresholdDays = 2;
            s.multNeglectGrowthStart = 1.0f;   // troubled neglect start = 2.0
            s.multContentNeglectStart = 2.0f;  // content neglect start = 4.0
            s.playerPartnerCheatingAllowWhenHappy = allowHappy;
            s.playerPartnerCheatingEnableNeglect = true;
            s.playerPartnerCheatingEnableRevenge = true;
            s.playerPartnerCheatingEnableHierarchy = true;
            s.playerPartnerCheatingEnableChildrenAnchor = true;
            s.playerPartnerCheatingChanceMultiplier = 1.0f;
            s.playerPartnerCheatingMaxRollCap = maxCap;
            ws.affection.Write().SetPermanent(partner, aff);
            ws.affection.Write().SetLastInteractionDay(partner, 10.0f);
            return ComputePartnerCheatingChance(
                ws.Snapshot(), input, 10.0f + daysSince,
                secretCount, childrenCount, playerCheating, rank);
        };

        auto approxEq = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

        // Dating stage (1.0x), aff=30 (baseAff=18), daysSince=0, rank=0, no children/cheating
        // Humble = 18 * 0.50 = 9.0
        MARAS_CHECK(approxEq(compute(Temperament::Humble, RelationshipStatus::Dating, 30, 0.0f, 0, 0, false, 0), 9.0f));
        // Proud = 18 * 1.20 = 21.6
        MARAS_CHECK(approxEq(compute(Temperament::Proud, RelationshipStatus::Dating, 30, 0.0f, 0, 0, false, 0), 21.6f));
        // Romantic: aff < 50 -> 1.30x => 23.4; aff=60 & daysSince=1 (<=4) -> 6 * 0.75 = 4.5
        MARAS_CHECK(approxEq(compute(Temperament::Romantic, RelationshipStatus::Dating, 30, 0.0f, 0, 0, false, 0), 23.4f));
        MARAS_CHECK(approxEq(compute(Temperament::Romantic, RelationshipStatus::Dating, 60, 1.0f, 0, 0, false, 0), 4.5f));
        // Independent: daysSince=0 -> 18 * 1.0 = 18.0; daysSince=6 (>2 -> bonusNeglect=(6-2)*2.5=10 -> core=28, 1.35x = 37.8)
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 0.0f, 0, 0, false, 0), 18.0f));
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 6.0f, 0, 0, false, 0), 37.8f));
        // Jealous: playerCheating=false -> 18 * 0.70 = 12.6; playerCheating=true -> (18+15) * 1.75 = 57.75
        MARAS_CHECK(approxEq(compute(Temperament::Jealous, RelationshipStatus::Dating, 30, 0.0f, 0, 0, false, 0), 12.6f));
        MARAS_CHECK(approxEq(compute(Temperament::Jealous, RelationshipStatus::Dating, 30, 0.0f, 0, 0, true, 0), 57.75f));

        // Stage multipliers on Engaged / Married (0.80x) and Lover (1.25x):
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Married, 30, 0.0f, 0, 0, false, 0), 14.4f));
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Engaged, 30, 0.0f, 0, 0, false, 0), 14.4f));
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Lover, 30, 0.0f, 0, 0, false, 0), 22.5f));

        // Secret lovers decay (1 -> 0.45x, >=2 -> 0.20x):
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 0.0f, 1, 0, false, 0), 8.1f));
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 0.0f, 2, 0, false, 0), 3.6f));

        // Hierarchy rank (+4 for rank=1, +8 for rank>=2) & children mitigation (min(12, count*4)):
        // core = 18 + 4 (rank 1) - 8 (2 children) = 14.0
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 0.0f, 0, 2, false, 1), 14.0f));
        // core = 18 + 8 (rank 2) - 12 (5 children capped at 12) = 14.0
        MARAS_CHECK(approxEq(compute(Temperament::Independent, RelationshipStatus::Dating, 30, 0.0f, 0, 5, false, 2), 14.0f));

        // Max cap clamping & floor at 0 (children mitigation > baseAff)
        MARAS_CHECK(approxEq(compute(Temperament::Jealous, RelationshipStatus::Dating, 10, 10.0f, 0, 0, true, 2, false, 50), 50.0f));
        MARAS_CHECK(approxEq(compute(Temperament::Humble, RelationshipStatus::Dating, 80, 0.0f, 0, 3, false, 0, true), 0.0f));
    }

    MARAS_TEST(DomainAgency, PartnerAgencyExpiredIntentsAndAffairLifecycle) {
        struct FixedZeroRng final : public IRng {
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        };

        DefaultActorMetadataProvider meta{};
        FixedZeroRng alwaysRollRng{};

        const ActorId p1{ 0x00044201u };
        const ActorId p2{ 0x00044202u };
        const ActorId p3{ 0x00044203u };
        const ActorId p4{ 0x00044204u };
        const ActorId p5{ 0x00044205u };
        const ActorId lover1{ 0x00044211u };
        const ActorId lover3{ 0x00044213u };

        WorldState ws{};
        ws.settings.Write().enablePlayerPolygamy = true;
        ws.settings.Write().playerPartnerCheatingAgencyEnabled = true;
        ws.settings.Write().playerPartnerOpenRelAgencyEnabled = true;
        ws.settings.Write().playerPartnerAgencyEnableForceGreets = true;
        ws.settings.Write().lonelinessThresholdDays = 2;  // TTL = 4, Escalation = 4, MinBreakupAge = 3

        for (auto [pid, temp] : std::array<std::pair<ActorId, Temperament>, 5>{ {
                 { p1, Temperament::Humble },
                 { p2, Temperament::Jealous },
                 { p3, Temperament::Romantic },
                 { p4, Temperament::Independent },
                 { p5, Temperament::Proud } } }) {
            AddAdult(ws, pid, temp);
            (void)ws.TransitionRelationship(ActorId::Player(), pid, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
            ws.affection.Write().SetLastInteractionDay(pid, 28.0f);
        }

        // p1: Expired ConfessTalking intent -> ExpiredIntentConfessEscalatePhysical
        ws.affection.Write().SetPermanent(p1, 40);
        ws.relationships.Write().SetPendingIntent(p1, PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessTalking,
            .createdGameDay = 10.0f,
            .relatedLover = lover1
        });

        // p2: Expired SuggestClose intent -> ExpiredIntentSuggestClosePenalty (-12 affection)
        ws.affection.Write().SetPermanent(p2, 40);
        ws.relationships.Write().SetPendingIntent(p2, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestClose,
            .createdGameDay = 10.0f
        });

        // p3: Talking affair past escalation age (>= 4 days) + high affection (75) with Romantic triggers
        // EscalateTalkingToPhysical AND BreakupClosedAffairLover with triggerConfessIntent == true
        ws.affection.Write().SetPermanent(p3, 75);
        (void)ws.RecordCheatingIncident(
            p3, ActorId::Player(), lover3, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 5.0f);

        // p4: Closed pair, neglected, Independent -> SuggestOpenIntent + SeekSecretAffair
        ws.affection.Write().SetPermanent(p4, 20);
        ws.affection.Write().SetLastInteractionDay(p4, 1.0f);

        // p5: Open pair, Jealous is SuggestClose; Proud can emit SeekOpenLover & SuggestCloseIntent
        (void)ws.SetPairOpen(ActorId::Player(), p5, true, 5.0f);
        ws.affection.Write().SetPermanent(p5, 80);

        const std::vector<PartnerAgencyInput> inputs{
            { p1, "HumbleSpouse", ActorSex::Female, false, false },
            { p2, "JealousSpouse", ActorSex::Female, false, false },
            { p3, "RomanticSpouse", ActorSex::Female, false, false },
            { p4, "IndepSpouse", ActorSex::Female, false, false },
            { p5, "ProudOpenSpouse", ActorSex::Female, false, false }
        };

        const auto plans = EvaluatePartnerAgencyActions(ws.Snapshot(), inputs, 30.0f, alwaysRollRng);

        auto findPlan = [&](ActorId pid, PartnerAgencyActionType t) -> const PartnerAgencyActionPlan* {
            for (const auto& plan : plans) {
                if (plan.partnerId == pid && plan.type == t) {
                    return &plan;
                }
            }
            return nullptr;
        };

        const auto* p1Exp = findPlan(p1, PartnerAgencyActionType::ExpiredIntentConfessEscalatePhysical);
        MARAS_REQUIRE(p1Exp != nullptr);
        MARAS_CHECK(p1Exp->escalateTalkingToPhysical);
        MARAS_CHECK_EQ(p1Exp->relatedLoverId, lover1);

        const auto* p2Exp = findPlan(p2, PartnerAgencyActionType::ExpiredIntentSuggestClosePenalty);
        MARAS_REQUIRE(p2Exp != nullptr);
        MARAS_CHECK_EQ(p2Exp->permanentAffectionDelta, -12);

        const auto* p3Esc = findPlan(p3, PartnerAgencyActionType::EscalateTalkingToPhysical);
        MARAS_REQUIRE(p3Esc != nullptr);
        MARAS_CHECK_EQ(p3Esc->relatedLoverId, lover3);

        const auto* p3Break = findPlan(p3, PartnerAgencyActionType::BreakupClosedAffairLover);
        MARAS_REQUIRE(p3Break != nullptr);
        MARAS_CHECK(p3Break->triggerConfessIntent);

        MARAS_CHECK(findPlan(p4, PartnerAgencyActionType::SuggestOpenIntent) != nullptr);
        MARAS_CHECK(findPlan(p4, PartnerAgencyActionType::SeekSecretAffair) != nullptr);
        MARAS_CHECK(findPlan(p5, PartnerAgencyActionType::SuggestCloseIntent) != nullptr);
        MARAS_CHECK(findPlan(p5, PartnerAgencyActionType::SeekOpenLover) != nullptr);
        MARAS_CHECK(findPlan(p5, PartnerAgencyActionType::SeekSecretAffair) == nullptr);

        // ConfessAffairIntent when ongoing affair is younger than MinSideLoverAgeForBreakupDays (3.0)
        WorldState wsConf{};
        AddAdult(wsConf, p1, Temperament::Humble);
        (void)wsConf.TransitionRelationship(ActorId::Player(), p1, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        wsConf.settings.Write().playerPartnerAgencyEnableForceGreets = true;
        wsConf.settings.Write().lonelinessThresholdDays = 2;
        wsConf.affection.Write().SetPermanent(p1, 65);
        wsConf.affection.Write().SetLastInteractionDay(p1, 29.0f);
        (void)wsConf.RecordCheatingIncident(
            p1, ActorId::Player(), lover1, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 29.5f);
        const auto confPlans = EvaluatePartnerAgencyActions(
            wsConf.Snapshot(), { { p1, "HumbleSpouse", ActorSex::Female, false, false } }, 30.0f, alwaysRollRng);
        bool sawConfessIntent = false;
        for (const auto& plan : confPlans) {
            if (plan.partnerId == p1 && plan.type == PartnerAgencyActionType::ConfessAffairIntent) {
                sawConfessIntent = true;
                MARAS_CHECK(plan.triggerConfessIntent);
            }
        }
        MARAS_CHECK(sawConfessIntent);
    }

    MARAS_TEST(DomainAgency, PartnerAgencyHomeArrivalCooldownAndEventBranching) {
        struct FixedZeroRng final : public IRng {
            float NextFloat(float minVal = 0.0f, float = 1.0f) override { return minVal; }
            std::int32_t NextInt(std::int32_t minVal, std::int32_t) override { return minVal; }
        };

        DefaultActorMetadataProvider meta{};
        FixedZeroRng alwaysRollRng{};
        const ActorId spouse{ 0x00044301u };
        const ActorId lover{ 0x00044302u };
        const LocationId homeA{ 0x00044310u };
        const LocationId homeB{ 0x00044311u };
        const CellId homeCell{ 0x00044320u };
        const CellId otherCell{ 0x00044321u };

        WorldState ws{};
        AddAdult(ws, spouse, Temperament::Romantic);
        (void)ws.TransitionRelationship(ActorId::Player(), spouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);
        ws.settings.Write().playerPartnerAgencyEnableHomeArrivalEvents = true;
        ws.settings.Write().lonelinessThresholdDays = 2;  // TTL = 4.0, HomeArrivalCooldown = 2.0

        SpatialActorSnapshot spatial{};
        auto populateSpatial = [&](CellId spouseCell) {
            spatial.Clear();
            spatial.SetMetadata(30.0f, 1u);
            LoadedActorSample pSample{};
            pSample.refId = RefId::Player();
            pSample.parentCellId = homeCell;
            LoadedActorSample sSample{};
            sSample.refId = spouse.AsRefId();
            sSample.parentCellId = spouseCell;
            spatial.AddSample(pSample);
            spatial.AddSample(sSample);
            spatial.FinalizeSortAndDedupe();
        };

        // 1. Expired pending intent is skipped for PendingIntentForcegreet, falling through to event roll
        populateSpatial(homeCell);
        ws.relationships.Write().SetPendingIntent(spouse, PendingPartnerIntentRecord{
            .type = PartnerIntentType::SuggestOpen,
            .createdGameDay = 10.0f  // age 20.0 >= TTL 4.0 -> expired
        });
        (void)ws.SetPairOpen(ActorId::Player(), spouse, true, 5.0f);
        {
            const auto plan = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeA, false, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(plan.has_value());
            MARAS_CHECK_EQ(plan->kind, HomeArrivalKind::OpenDateInHouse);
        }

        // 2. Same Open pair with tavern marker present (50% roll succeeds under FixedZeroRng) OR partner not in cell -> OpenDateLetter
        {
            const auto planTavern = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeA, true, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(planTavern.has_value());
            MARAS_CHECK_EQ(planTavern->kind, HomeArrivalKind::OpenDateLetter);

            populateSpatial(otherCell);
            const auto planAway = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeA, false, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(planAway.has_value());
            MARAS_CHECK_EQ(planAway->kind, HomeArrivalKind::OpenDateLetter);
        }

        // 3. Per-location cooldown blocks homeA while allowing homeB
        ws.cooldowns.Write().lastHomeArrivalEventDayByLoc[homeA] = 29.5f;  // elapsed 0.5 < 2.0
        {
            const auto blocked = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeA, false, 20, 30.0f, alwaysRollRng);
            MARAS_CHECK(!blocked.has_value());
            const auto allowedOtherHome = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeB, false, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(allowedOtherHome.has_value());
            MARAS_CHECK_EQ(allowedOtherHome->kind, HomeArrivalKind::OpenDateLetter);
        }

        // 4. Ongoing Talking vs Physical secret affairs -> CheatingTalkingLetter / CheatingPhysicalLetter
        (void)ws.SetPairOpen(ActorId::Player(), spouse, false, 20.0f);
        (void)ws.RecordCheatingIncident(
            spouse, ActorId::Player(), lover, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 25.0f);
        {
            const auto talkLetter = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeB, false, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(talkLetter.has_value());
            MARAS_CHECK_EQ(talkLetter->kind, HomeArrivalKind::CheatingTalkingLetter);
            MARAS_CHECK_EQ(talkLetter->relatedLoverId, lover);
        }

        (void)ws.RecordCheatingIncident(
            spouse, ActorId::Player(), lover, true,
            AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 29.0f);
        {
            const auto physLetter = EvaluatePartnerHomeArrival(ws.Snapshot(), spatial, homeB, false, 20, 30.0f, alwaysRollRng);
            MARAS_REQUIRE(physLetter.has_value());
            MARAS_CHECK_EQ(physLetter->kind, HomeArrivalKind::CheatingPhysicalLetter);
        }
    }

    MARAS_TEST(DomainAgency, MatchmakingOrientationMatrix) {
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Any, ActorSex::Unknown, ActorSex::Unknown));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Heterosexual, ActorSex::Unknown, ActorSex::Female));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Gay, ActorSex::Unknown, ActorSex::Male));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Lesbian, ActorSex::Female, ActorSex::Unknown));

        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Gay, ActorSex::Male, ActorSex::Male));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Gay, ActorSex::Female, ActorSex::Female));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Gay, ActorSex::Male, ActorSex::Female));

        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::Lesbian, ActorSex::Female, ActorSex::Female));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::Lesbian, ActorSex::Male, ActorSex::Male));

        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::HeteroAndGay, ActorSex::Male, ActorSex::Female));
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::HeteroAndGay, ActorSex::Male, ActorSex::Male));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::HeteroAndGay, ActorSex::Female, ActorSex::Female));

        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::HeteroAndLesbian, ActorSex::Male, ActorSex::Female));
        MARAS_CHECK(IsOrientationCompatible(MatchmakingOrientation::HeteroAndLesbian, ActorSex::Female, ActorSex::Female));
        MARAS_CHECK(!IsOrientationCompatible(MatchmakingOrientation::HeteroAndLesbian, ActorSex::Male, ActorSex::Male));
    }

    MARAS_TEST(DomainAgency, MatchmakingRollActionSlotsCapsAndSettingsGates) {
        SettingsData settings{};
        settings.diceRollChanceNewDating = 0;
        settings.diceRollChanceNewEngaged = 0;
        settings.diceRollChanceNewLoverAllowed = 0;
        settings.diceRollChanceNewLoverCheating = 100;
        settings.diceRollChanceAdvance = 0;
        settings.diceRollChanceBreakup = 100;
        settings.diceRollChanceOpenClose = 100;

        // With all three boolean gates disabled, total positive weight is 0 -> empty slots
        settings.matchmakingAllowCheating = false;
        settings.matchmakingAllowBreakups = false;
        settings.matchmakingAllowOpenClose = false;
        DeterministicRng rng{ 101u };
        MARAS_CHECK(RollActionSlots(settings, 5, rng).empty());

        // Enable only Advance with 100 weight and request 6 slots -> capped at 3 per MatchmakingActionKind
        settings.diceRollChanceNewDating = 0;
        settings.diceRollChanceNewLoverCheating = 0;
        settings.diceRollChanceBreakup = 0;
        settings.diceRollChanceOpenClose = 0;
        settings.diceRollChanceAdvance = 100;
        const auto advOnly = RollActionSlots(settings, 6, rng);
        MARAS_REQUIRE_EQ(advOnly.size(), std::size_t{ 3 });
        for (const auto& s : advOnly) {
            MARAS_CHECK_EQ(s.action, MatchmakingActionKind::Advance);
        }

        // Enable NewDating + Advance with maxSlots = 5 and check maxSlots respected
        settings.diceRollChanceNewDating = 50;
        settings.diceRollChanceAdvance = 50;
        const auto mixed = RollActionSlots(settings, 5, rng);
        MARAS_REQUIRE_EQ(mixed.size(), std::size_t{ 5 });
        int newCount = 0;
        int advCount = 0;
        for (const auto& s : mixed) {
            if (s.action == MatchmakingActionKind::New) ++newCount;
            if (s.action == MatchmakingActionKind::Advance) ++advCount;
        }
        MARAS_CHECK(newCount <= 3 && advCount <= 3);
    }

    MARAS_TEST(DomainAgency, MatchmakingFindEligiblePairForSlotAllActionKinds) {
        DefaultActorMetadataProvider meta{};
        DeterministicRng rng{ 303u };

        const ActorId mSingle{ 0x00044401u };
        const ActorId fSingle{ 0x00044402u };
        const ActorId mMarried{ 0x00044403u };
        const ActorId fMarried{ 0x00044404u };
        const std::vector<NpcMatchmakingProfile> npcs{
            { mSingle, "A", ActorSex::Male, SocialClass::Working, SkillType::Warrior, Temperament::Proud },
            { fSingle, "B", ActorSex::Female, SocialClass::Working, SkillType::Mage, Temperament::Humble },
            { mMarried, "C", ActorSex::Male, SocialClass::Working, SkillType::Rogue, Temperament::Independent },
            { fMarried, "D", ActorSex::Female, SocialClass::Working, SkillType::Orator, Temperament::Romantic }
        };

        WorldState ws{};
        for (const auto& p : npcs) {
            AddAdult(ws, p.actorId, p.temperament);
        }
        ws.settings.Write().matchmakingEnabled = true;
        ws.settings.Write().matchmakingOrientation = MatchmakingOrientation::Heterosexual;
        ws.settings.Write().matchmakingAllowCheating = true;
        ws.settings.Write().matchmakingAllowBreakups = true;
        ws.settings.Write().matchmakingAllowOpenClose = true;
        (void)ws.TransitionRelationship(mMarried, fMarried, RelationshipStatus::Married, 1.0f, mMarried, meta);

        // New Lover (cheating=true): only pairs where >=1 side has an exclusive main partner
        {
            const auto prop = FindEligiblePairForSlot(
                ws.Snapshot(), npcs,
                MatchmakingActionSlot{ MatchmakingActionKind::New, RelationshipStatus::Lover, true, false },
                20.0f, rng);
            MARAS_REQUIRE(prop.has_value());
            MARAS_CHECK(
                prop->actorA == mMarried || prop->actorA == fMarried ||
                prop->actorB == mMarried || prop->actorB == fMarried);
        }

        // New Lover (cheating=false): excludes exclusive main partners (mSingle + fSingle only)
        {
            const auto prop = FindEligiblePairForSlot(
                ws.Snapshot(), npcs,
                MatchmakingActionSlot{ MatchmakingActionKind::New, RelationshipStatus::Lover, false, false },
                20.0f, rng);
            MARAS_REQUIRE(prop.has_value());
            MARAS_CHECK(
                (prop->actorA == mSingle && prop->actorB == fSingle) ||
                (prop->actorA == fSingle && prop->actorB == mSingle));
        }

        // OpenClose toggles isOpen false -> true on NPC Married pair, never touches Player pair
        (void)ws.TransitionRelationship(ActorId::Player(), mSingle, RelationshipStatus::Dating, 1.0f, ActorId::Player(), meta);
        {
            const auto propOpen = FindEligiblePairForSlot(
                ws.Snapshot(), npcs,
                MatchmakingActionSlot{ MatchmakingActionKind::OpenClose, RelationshipStatus::Unknown, false, false },
                20.0f, rng);
            MARAS_REQUIRE(propOpen.has_value());
            MARAS_CHECK(!propOpen->actorA.IsPlayer() && !propOpen->actorB.IsPlayer());
            MARAS_CHECK(propOpen->slot.openTarget);
        }

        // Breakup maps Married -> Divorced
        {
            const auto propBreak = FindEligiblePairForSlot(
                ws.Snapshot(), npcs,
                MatchmakingActionSlot{ MatchmakingActionKind::Breakup, RelationshipStatus::Unknown, false, false },
                20.0f, rng);
            MARAS_REQUIRE(propBreak.has_value());
            MARAS_CHECK_EQ(propBreak->slot.targetStatus, RelationshipStatus::Divorced);
        }

        // Advance transitions: Lover without main -> Dating; Dating -> Engaged; Engaged -> Married
        WorldState wsAdv{};
        AddAdult(wsAdv, mSingle);
        AddAdult(wsAdv, fSingle);
        wsAdv.settings.Write().matchmakingEnabled = true;

        (void)wsAdv.TransitionRelationship(mSingle, fSingle, RelationshipStatus::Lover, 1.0f, mSingle, meta);
        auto p1 = FindEligiblePairForSlot(
            wsAdv.Snapshot(), npcs,
            MatchmakingActionSlot{ MatchmakingActionKind::Advance, RelationshipStatus::Unknown, false, false },
            20.0f, rng);
        MARAS_REQUIRE(p1.has_value());
        MARAS_CHECK_EQ(p1->slot.targetStatus, RelationshipStatus::Dating);

        (void)wsAdv.TransitionRelationship(mSingle, fSingle, RelationshipStatus::Dating, 2.0f, mSingle, meta);
        auto p2 = FindEligiblePairForSlot(
            wsAdv.Snapshot(), npcs,
            MatchmakingActionSlot{ MatchmakingActionKind::Advance, RelationshipStatus::Unknown, false, false },
            20.0f, rng);
        MARAS_REQUIRE(p2.has_value());
        MARAS_CHECK_EQ(p2->slot.targetStatus, RelationshipStatus::Engaged);

        (void)wsAdv.TransitionRelationship(mSingle, fSingle, RelationshipStatus::Engaged, 3.0f, mSingle, meta);
        auto p3 = FindEligiblePairForSlot(
            wsAdv.Snapshot(), npcs,
            MatchmakingActionSlot{ MatchmakingActionKind::Advance, RelationshipStatus::Unknown, false, false },
            20.0f, rng);
        MARAS_REQUIRE(p3.has_value());
        MARAS_CHECK_EQ(p3->slot.targetStatus, RelationshipStatus::Married);
    }

    MARAS_TEST(DomainAgency, NpcMatchmakerSkipsDeceasedCooldownsAndPlayerMainPartners) {
        WorldState ws{};
        DefaultActorMetadataProvider meta{};
        DeterministicRng rng{ 404u };

        ws.settings.Write().matchmakingEnabled = true;
        ws.settings.Write().matchmakingOrientation = MatchmakingOrientation::Heterosexual;
        ws.settings.Write().matchmakingMaxSuggestions = 2;
        ws.settings.Write().matchmakingNpcCooldownDays = 14;
        ws.settings.Write().diceRollChanceNewDating = 100;
        ws.settings.Write().diceRollChanceNewEngaged = 0;
        ws.settings.Write().diceRollChanceNewLoverAllowed = 0;
        ws.settings.Write().diceRollChanceNewLoverCheating = 0;
        ws.settings.Write().diceRollChanceAdvance = 0;
        ws.settings.Write().diceRollChanceBreakup = 0;
        ws.settings.Write().diceRollChanceOpenClose = 0;

        const ActorId deadMale{ 0x00044501u };
        const ActorId cooldownFemale{ 0x00044502u };
        const ActorId playerSpouse{ 0x00044503u };
        const ActorId freeMale{ 0x00044504u };
        const ActorId freeFemale{ 0x00044505u };
        for (ActorId id : { deadMale, cooldownFemale, playerSpouse, freeMale, freeFemale }) {
            AddAdult(ws, id);
        }

        DeceasedActorRecord deadRec{};
        deadRec.refId = deadMale.AsRefId();
        deadRec.baseId = BaseId{ deadMale.raw() };
        deadRec.deathTimestamp = 2.0f;
        ws.deceased.Write().UpsertRecord(std::move(deadRec));

        ws.cooldowns.Write().npcLastMatchmakingChangeDay[cooldownFemale] = 19.0f;
        (void)ws.TransitionRelationship(ActorId::Player(), playerSpouse, RelationshipStatus::Married, 1.0f, ActorId::Player(), meta);

        const std::vector<NpcMatchmakingProfile> npcs{
            { deadMale, "DeadMale", ActorSex::Male, SocialClass::Working, SkillType::Warrior, Temperament::Proud },
            { cooldownFemale, "CooldownFem", ActorSex::Female, SocialClass::Working, SkillType::Mage, Temperament::Humble },
            { playerSpouse, "PlayerSpouse", ActorSex::Female, SocialClass::Working, SkillType::Rogue, Temperament::Romantic },
            { freeMale, "FreeMale", ActorSex::Male, SocialClass::Working, SkillType::Warrior, Temperament::Proud },
            { freeFemale, "FreeFemale", ActorSex::Female, SocialClass::Working, SkillType::Orator, Temperament::Independent }
        };

        const auto proposals = GenerateDiceRollProposals(ws.Snapshot(), npcs, 20.0f, rng);
        MARAS_REQUIRE(!proposals.empty());
        for (const auto& prop : proposals) {
            MARAS_CHECK(
                (prop.actorA == freeMale && prop.actorB == freeFemale) ||
                (prop.actorA == freeFemale && prop.actorB == freeMale));
        }

        // Same-sex-only pool under Heterosexual setting -> 0 proposals
        const std::vector<NpcMatchmakingProfile> twoMales{ npcs[3], npcs[0] };
        MARAS_CHECK(GenerateDiceRollProposals(ws.Snapshot(), twoMales, 20.0f, rng).empty());
    }

}  // namespace MARAS::Tests
