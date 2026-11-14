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

}  // namespace MARAS::Tests
