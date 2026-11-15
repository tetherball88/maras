#include "TestHarness.h"

#include "maras/foundation/IRng.h"
#include "maras/state/CoSaveCodec.h"
#include "maras/state/CoSaveStream.h"
#include "maras/state/PerTableSnapshot.h"
#include "maras/state/WorldState.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Tests {

    namespace {

        using namespace MARAS::Foundation;
        using namespace MARAS::State;

        void WriteRawString(ICoSaveWriter& w, std::string_view str) {
            const auto len = static_cast<std::uint32_t>(str.size());
            (void)w.WritePod(len);
            if (len > 0) {
                (void)w.WriteBytes(str.data(), len);
            }
        }

        WorldState BuildRichV7WorldState(MockFormIdResolver& resolver) {
            WorldState state{};

            const ActorId spouseA{ 0x0001A694u };   // Ysolda
            const ActorId spouseB{ 0x00019DF7u };   // Camilla
            const ActorId paramourC{ 0x00020545u }; // Brynjolf
            const ActorId npcHusb{ 0x00030010u };
            const ActorId npcWife{ 0x00030020u };
            const ChildId unbornChild{ 0xFF70000Au };
            const ChildId youngChild{ 0x00050001u };
            const ChildId adultChild{ 0xFF800100u };

            resolver.baseIds[spouseA] = BaseId{ 0x00013BABu };
            resolver.baseIds[spouseB] = BaseId{ 0x0001347Bu };
            resolver.baseIds[paramourC] = BaseId{ 0x0001B07Du };
            resolver.baseIds[npcHusb] = BaseId{ 0x00030001u };
            resolver.baseIds[npcWife] = BaseId{ 0x00030002u };
            resolver.baseIds[youngChild.AsActorId()] = BaseId{ 0x00050000u };
            resolver.sexes[ActorId::Player()] = ActorSex::Male;
            resolver.sexes[spouseA] = ActorSex::Female;
            resolver.sexes[spouseB] = ActorSex::Female;
            resolver.sexes[paramourC] = ActorSex::Male;
            resolver.sexes[npcHusb] = ActorSex::Male;
            resolver.sexes[npcWife] = ActorSex::Female;
            resolver.names[spouseA] = "Ysolda";
            resolver.names[spouseB] = "Camilla";
            resolver.names[paramourC] = "Brynjolf";

            // 1. Actor profiles + Stage 4 Adult Child profile + Pending relationships
            {
                ActorProfileRecord profA{};
                profA.refId = spouseA.AsRefId();
                profA.baseId = resolver.baseIds[spouseA];
                profA.socialClass = SocialClass::Middle;
                profA.skillType = SkillType::Orator;
                profA.temperament = Temperament::Independent;
                profA.isRegisteredAdult = true;
                profA.legacyOriginalHome = FormRawId{ 0x000165A8u };
                profA.legacyCurrentHome = FormRawId{ 0x000165A2u };
                profA.legacyHomeMarker = RefId{ 0x000165B0u };
                profA.addedKeywords = { FormRawId{ 0x00099101u }, FormRawId{ 0x00099102u } };
                state.actors.Write().Upsert(profA);

                for (const ActorId id : { spouseB, paramourC, npcHusb, npcWife }) {
                    ActorProfileRecord p{};
                    p.refId = id.AsRefId();
                    p.baseId = resolver.GetActorBaseId(id);
                    p.socialClass = SocialClass::Working;
                    p.skillType = SkillType::Warrior;
                    p.temperament = Temperament::Romantic;
                    p.isRegisteredAdult = true;
                    state.actors.Write().Upsert(p);
                }

                ActorProfileRecord adultChildProf{};
                adultChildProf.refId = RefId{ adultChild.raw() };
                adultChildProf.baseId = BaseId{ adultChild.raw() };
                adultChildProf.socialClass = SocialClass::Wealthy;
                adultChildProf.skillType = SkillType::Ranger;
                adultChildProf.temperament = Temperament::Proud;
                adultChildProf.isRegisteredAdult = true;
                state.actors.Write().Upsert(adultChildProf);

                state.actors.Write().AddPendingRelationship(
                    BaseId{ 0x00077001u },
                    PendingRelationshipEntry{
                        .partnerBaseId = BaseId{ 0x00077002u },
                        .partnerRefId = RefId{ 0x00077012u },
                        .targetStatus = RelationshipStatus::Engaged,
                        .originalStatus = RelationshipStatus::Candidate,
                    });
            }

            // 2. Relationships + Hierarchy + Pending Intents
            (void)state.PromoteToMarried(ActorId::Player(), spouseA, 10.0f, ActorId::Player(), resolver);
            (void)state.PromoteToMarried(ActorId::Player(), spouseB, 14.0f, ActorId::Player(), resolver);
            (void)state.PromoteToMarried(npcHusb, npcWife, 8.0f, npcHusb, resolver);
            (void)state.SetPairOpen(npcHusb, npcWife, true, 9.0f);

            state.relationships.Write().SetPendingIntent(
                spouseB,
                PendingPartnerIntentRecord{
                    .type = PartnerIntentType::ConfessTalking,
                    .createdGameDay = 18.5f,
                    .relatedLover = paramourC,
                });

            // 3. Affairs ('AGN1' Talking stage on Player's spouse)
            (void)state.RecordCheatingIncident(
                spouseB,
                ActorId::Player(),
                paramourC,
                /*isSecret=*/true,
                AffairType::Dating,
                AffairStatus::Ongoing,
                AffairStage::Talking,
                18.75f);

            // 4. Household ('PHOU' + 'SPAS' + 'PLCH' + 'CFSN' + 'ACTR')
            const HouseId whiterunHouse{ 0x000165A2u };
            const HouseId riftenHouse{ 0x00016BFEu };
            state.household.Write().RegisterHouse(whiterunHouse, RefId{ 0x000165B0u }, true);
            state.household.Write().RegisterHouse(riftenHouse, RefId{ 0x00016BFFu }, false);
            state.household.Write().AssignTenant(spouseA.raw(), whiterunHouse, /*stayAtHome=*/true, false);
            state.household.Write().AssignTenant(spouseB.raw(), riftenHouse, /*stayAtHome=*/false, false);

            // 5. Lineage ('NPCR' children + 'PRG1' + 'CHD1' + 'LYC1' + 'CHD2' + pending child)
            {
                ChildRecord unborn{};
                unborn.childId = unbornChild;
                (void)unborn.guardians.Assign(ActorId::Player(), spouseB);
                unborn.name = "UnbornBaby";
                unborn.timestamp = 19;
                unborn.isAdopted = false;
                unborn.isBiologicalChild = true;
                unborn.bioMother = spouseB;
                unborn.bioFather = paramourC;
                unborn.presumedFather = ActorId::Player();
                unborn.conceptionTimestamp = 18.75f;
                unborn.stage = ChildStage::Unborn;
                unborn.paternitySecret.isSecret = true;
                unborn.paternitySecret.isSecretForPartner = true;
                unborn.paternitySecret.isSecretForBioFather = true;
                unborn.paternitySecret.isPregnancySecretForPartner = true;
                unborn.paternitySecret.hasPhenotypeMismatch = true;
                unborn.paternitySecret.lastCheckTimestamp = 19.0f;
                unborn.paternitySecret.bioFatherName = "Brynjolf";
                unborn.paternitySecret.bioFatherRaceId = FormRawId{ 0x00013746u };
                state.lineage.Write().UpsertChild(unborn);

                ChildRecord young{};
                young.childId = youngChild;
                (void)young.guardians.Assign(ActorId::Player(), spouseA);
                young.name = "Lucia";
                young.timestamp = 11;
                young.isAdopted = false;
                young.isBiologicalChild = true;
                young.bioMother = spouseA;
                young.bioFather = ActorId::Player();
                young.conceptionTimestamp = 2.0f;
                young.birthTimestamp = 11.0f;
                young.stage = ChildStage::Child;
                young.gender = 1;
                young.trainingClassIndex = 2;
                young.trainingStartGameDay = 12.5f;
                young.savedBioTemplateName = "NordChildF";
                young.savedRaceIndex = 0;
                young.hairColorFormId = FormRawId{ 0x000A0430u };
                young.eyesPartFormId = FormRawId{ 0x00051500u };
                young.hairPartFormId = FormRawId{ 0x00051501u };
                young.facialHairPartFormId = FormRawId{ 0x00000000u };
                young.heightScale = 1.06f;
                young.rpgApplied = true;
                young.hasBeastblood = true;
                young.beastbloodEvaluated = true;
                young.bioLineageEvaluated = true;
                young.guardianLineageEvaluated = true;
                young.bioSkillBonuses[0] = 5;
                young.bioSkillBonuses[4] = 10;
                young.guardianSkillBonuses[2] = 7;
                young.lastAppliedLevel = 14;
                young.daedricBlessingId = "Hircine";
                state.lineage.Write().UpsertChild(young);

                ChildRecord grad{};
                grad.childId = adultChild;
                (void)grad.guardians.Assign(ActorId::Player(), spouseA);
                grad.name = "ErikJr";
                grad.timestamp = 5;
                grad.isAdopted = false;
                grad.isBiologicalChild = true;
                grad.bioMother = spouseA;
                grad.bioFather = ActorId::Player();
                grad.birthTimestamp = 5.0f;
                grad.stage = ChildStage::Adult;
                grad.gender = 0;
                grad.rpgApplied = true;
                state.lineage.Write().UpsertChild(grad);

                state.lineage.Write().AddPendingChild(
                    BaseId{ 0x00088001u },
                    PendingChildEntry{
                        .childBaseId = BaseId{ 0x00088001u },
                        .childRefId = RefId{ 0x00088002u },
                        .parentABaseId = BaseId{ 0x00030001u },
                        .parentARefId = npcHusb.AsRefId(),
                        .parentBBaseId = BaseId{ 0x00030002u },
                        .parentBRefId = npcWife.AsRefId(),
                        .isAdopted = true,
                    });
            }

            // Sync young child household state + enrich PLCH, SPAS, CFSN, ACTR
            (void)state.SyncChildHouseholdState(youngChild, whiterunHouse);
            state.household.Write().SetChildChestIntroduced(true);
            if (auto* cState = state.household.Write().FindChildStateMutable(youngChild)) {
                cState->homeMarkerId = RefId{ 0x000165B0u };
                cState->petId = RefId{ 0x00090001u };
                cState->critterId = RefId{ 0x00090002u };
                cState->currentOrder = 2.0f;
                cState->orderExpiryGameDay = 25.0f;
                cState->forcegreetPending = true;
                cState->critterBanned = false;
                cState->petBanned = true;
                cState->isNewlyAdopted = false;
                cState->critterRetryTimestamp = 15.0f;
                cState->petRetryTimestamp = 16.0f;
                cState->playedScene1 = true;
                cState->playedScene2 = false;
                cState->lastInteractionTimestamp = 19.5f;
            }

            state.household.Write().UpsertSharedCell(
                CellId{ 0x000165A8u },
                SharedCellRecord{
                    .sharedWithPlayer = true,
                    .originalPublicRecorded = true,
                    .originalPublicState = false,
                    .sharingSpouses = { spouseA },
                });
            // Explicit 0 sentinel (originally unowned bed) + non-zero owner
            state.household.Write().SetOriginalBedOwner(RefId{ 0x000B0001u }, FormRawId{ 0u });
            state.household.Write().SetOriginalBedOwner(RefId{ 0x000B0002u }, FormRawId{ spouseA.raw() });

            state.household.Write().UpsertChildFactionSnapshot(
                youngChild,
                ChildFactionSnapshotRecord{
                    .crimeFactionId = FormRawId{ 0x000267EAu },
                    .factions = {
                        FactionRankSnapshotEntry{ FormRawId{ 0x00051596u }, 0 },
                        FactionRankSnapshotEntry{ FormRawId{ 0x0002816Cu }, -1 },
                    },
                });

            state.household.Write().UpsertAdultChildTravel(
                adultChild,
                AdultChildTravelRecord{
                    .targetMarkerId = RefId{ 0x000C0001u },
                    .targetLocationId = LocationId{ 0x00018A56u },
                    .targetCellId = CellId{ 0x000165A2u },
                    .destinationName = "Whiterun Bannered Mare",
                    .assignedGameDay = 17.0f,
                    .arrivedGameDay = 18.0f,
                    .stayDurationDays = 3.5f,
                    .isFamilyVisit = true,
                });

            // 6. Affection ('AFCT' permanent + lastDay + v7 daily accumulation)
            state.affection.Write().SetPermanent(spouseA, 92);
            state.affection.Write().SetPermanent(spouseB, 61);
            state.affection.Write().SetLastInteractionDay(spouseA, 19.25f);
            state.affection.Write().SetDailyAccumulation(spouseA, "Gift", 10.0f);
            state.affection.Write().SetDailyAccumulation(spouseA, "Intimacy", 15.5f);

            // 7. Deceased ('DCSD' both young child and graduated Stage 4 Adult Child with spouse + childInfo)
            {
                DeceasedActorRecord deadYoung{};
                deadYoung.baseId = BaseId{ 0xFF800201u };
                deadYoung.refId = RefId{ 0xFF800201u };
                deadYoung.name = "FallenChild";
                deadYoung.deathTimestamp = 12.0f;
                deadYoung.isChild = true;
                deadYoung.wasKilledByPlayer = false;
                deadYoung.childInfo = DeceasedChildMetadata{
                    .guardianABaseId = BaseId{ kPlayerRefRawId },
                    .guardianARefId = RefId::Player(),
                    .guardianAName = "Player",
                    .guardianBBaseId = resolver.baseIds[spouseA],
                    .guardianBRefId = spouseA.AsRefId(),
                    .guardianBName = "Ysolda",
                    .bioMotherBaseId = resolver.baseIds[spouseA],
                    .bioFatherBaseId = BaseId{ kPlayerRefRawId },
                    .isAdopted = false,
                    .houseId = whiterunHouse,
                };
                state.deceased.Write().UpsertRecord(deadYoung);

                DeceasedActorRecord deadGradAdult{};
                deadGradAdult.baseId = BaseId{ 0xFF800202u };
                deadGradAdult.refId = RefId{ 0xFF800202u };
                deadGradAdult.name = "FallenGradAdult";
                deadGradAdult.deathTimestamp = 16.0f;
                deadGradAdult.isChild = false;
                deadGradAdult.wasKilledByPlayer = true;
                deadGradAdult.killerBaseId = BaseId{ kPlayerRefRawId };
                deadGradAdult.killerRefId = RefId::Player();
                deadGradAdult.killerName = "Player";
                deadGradAdult.socialClass = SocialClass::Nobles;
                deadGradAdult.skillType = SkillType::Mage;
                deadGradAdult.temperament = Temperament::Proud;
                deadGradAdult.partnersAtDeath.push_back(DeceasedPartnerEntry{
                    .partnerBaseId = BaseId{ 0x00030099u },
                    .partnerRefId = RefId{ 0x0003009Au },
                    .partnerName = "WidowedSpouse",
                    .statusAtDeath = RelationshipStatus::Married,
                    .isPlayer = false,
                });
                deadGradAdult.childInfo = DeceasedChildMetadata{
                    .guardianABaseId = BaseId{ kPlayerRefRawId },
                    .guardianARefId = RefId::Player(),
                    .guardianAName = "Player",
                    .guardianBBaseId = resolver.baseIds[spouseA],
                    .guardianBRefId = spouseA.AsRefId(),
                    .guardianBName = "Ysolda",
                    .bioMotherBaseId = resolver.baseIds[spouseA],
                    .bioFatherBaseId = BaseId{ kPlayerRefRawId },
                    .isAdopted = false,
                    .houseId = whiterunHouse,
                };
                state.deceased.Write().UpsertRecord(deadGradAdult);
            }

            // 8. Cooldowns ('MKMK' + 'BNTR')
            {
                auto& cd = state.cooldowns.Write();
                cd.lastMatchmakingDay = 15.0f;
                cd.lastPlayerPartnerCheatingDay = 18.75f;
                cd.npcLastMatchmakingChangeDay[npcHusb] = 15.0f;
                cd.lastHomeArrivalEventDayByLoc[LocationId{ 0x00018A56u }] = 19.0f;
                cd.savedBanterCooldownHours = 36.0f;
                cd.lastBanterSceneDay = 19.125f;
                cd.banterCoupleCooldowns[CanonicalPairKey{ npcHusb, npcWife }] = 19.125f;
            }

            // 9. Settings ('CNFG' subVersion 4)
            {
                auto& s = state.settings.Write();
                s.skipWedding = true;
                s.affectionDecayMultiplier = 1.75f;
                s.childCustodyGenderPolicy = ChildCustodyGenderPolicy::StayWithFather;
                s.childCustodyPlayerPolicy = ChildCustodyPlayerPolicy::AlwaysWithPlayer;
                s.matchmakingOrientation = MatchmakingOrientation::Any;
                s.revealSecretsInUI = true;
                s.uiScale = 1.25f;
                s.childTrainingDurationDays = 21;
                s.childTrainingCost = 3500;
                s.childTrainingRequireFaction = false;
            }

            return state;
        }

    }  // namespace

    MARAS_TEST(CoSaveMigrations, LegacyNPCR_V1_V2_V3) {
        for (const std::uint32_t legacyVersion : { 1u, 2u, 3u }) {
            InMemoryCoSaveWriter writer{};
            MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkNPCR, legacyVersion));
            MARAS_REQUIRE(writer.WritePod(CoSaveConstants::kMagicNumber));
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(3u))); // 3 legacy entries

            auto writeLegacyEntry = [&](std::uint32_t formId, std::uint8_t statusByte,
                                        std::uint32_t engDate, std::uint32_t marrDate) {
                (void)writer.WritePod(formId);
                (void)writer.WritePod(static_cast<std::uint8_t>(SocialClass::Nobles));
                (void)writer.WritePod(static_cast<std::uint8_t>(SkillType::Orator));
                (void)writer.WritePod(static_cast<std::uint8_t>(Temperament::Jealous));
                (void)writer.WritePod(statusByte);

                const bool hasOrig = true;
                const bool hasCurr = true;
                (void)writer.WritePod(hasOrig);
                (void)writer.WritePod(static_cast<std::uint32_t>(0x00020001u));
                (void)writer.WritePod(hasCurr);
                (void)writer.WritePod(static_cast<std::uint32_t>(0x00020002u));
                (void)writer.WritePod(engDate);
                (void)writer.WritePod(marrDate);

                if (legacyVersion >= 2u) {
                    const bool hasMarker = true;
                    (void)writer.WritePod(hasMarker);
                    (void)writer.WritePod(static_cast<std::uint32_t>(0x00030001u));
                }
                if (legacyVersion >= 3u) {
                    (void)writer.WritePod(static_cast<std::uint32_t>(2u));
                    (void)writer.WritePod(static_cast<std::uint32_t>(0x00040001u));
                    (void)writer.WritePod(static_cast<std::uint32_t>(0x00040002u));
                }
            };

            // Entry 0: Valid Married spouse with load-order remapped FormID
            writeLegacyEntry(0x02001000u, static_cast<std::uint8_t>(RelationshipStatus::Married), 12u, 20u);
            // Entry 1: Legacy v1-v3 status 5 == Deceased (MUST NOT become Dating!)
            writeLegacyEntry(0x02002000u, 5u, 4u, 0u);
            // Entry 2: Player self-entry (MUST be ignored)
            writeLegacyEntry(kPlayerRefRawId, static_cast<std::uint8_t>(RelationshipStatus::Candidate), 0u, 0u);

            MockFormIdResolver resolver{};
            resolver.remapTable[0x02001000u] = 0x05001000u;
            resolver.baseIds[ActorId{ 0x05001000u }] = BaseId{ 0x05000FFFu };

            WorldState state{};
            InMemoryCoSaveReader reader(writer.Records());
            const CoSaveLoadReport report = LoadWorldState(state, reader, resolver);
            MARAS_REQUIRE(report.allChunksSucceeded);
            MARAS_CHECK_EQ(report.loadedChunkCount, 1u);

            const ActorId remappedSpouse{ 0x05001000u };
            MARAS_CHECK_EQ(state.actors->Size(), std::size_t{ 1 });
            const auto* prof = state.actors->Find(remappedSpouse.AsRefId());
            MARAS_REQUIRE(prof != nullptr);
            MARAS_CHECK_EQ(prof->socialClass, SocialClass::Nobles);
            MARAS_CHECK_EQ(prof->skillType, SkillType::Orator);
            MARAS_CHECK_EQ(prof->temperament, Temperament::Jealous);
            MARAS_CHECK_EQ(prof->baseId, BaseId{ 0x05000FFFu });
            MARAS_CHECK_EQ(prof->legacyOriginalHome.value_or(FormRawId{}), FormRawId{ 0x00020001u });
            MARAS_CHECK_EQ(prof->legacyCurrentHome.value_or(FormRawId{}), FormRawId{ 0x00020002u });

            if (legacyVersion >= 2u) {
                MARAS_CHECK_EQ(prof->legacyHomeMarker.value_or(RefId{}), RefId{ 0x00030001u });
            } else {
                MARAS_CHECK(!prof->legacyHomeMarker.has_value());
            }

            if (legacyVersion >= 3u) {
                MARAS_CHECK_EQ(prof->addedKeywords.size(), std::size_t{ 2 });
                MARAS_CHECK(prof->addedKeywords.contains(FormRawId{ 0x00040001u }));
            } else {
                MARAS_CHECK(prof->addedKeywords.empty());
            }

            MARAS_CHECK(state.IsMarried(ActorId::Player(), remappedSpouse));
            const auto* edge = state.relationships->FindEdge(ActorId::Player(), remappedSpouse);
            MARAS_REQUIRE(edge != nullptr);
            MARAS_CHECK_EQ(edge->originalStatus, RelationshipStatus::Unknown);
            MARAS_REQUIRE_EQ(edge->statusHistory.size(), std::size_t{ 2 });
            MARAS_CHECK_EQ(edge->statusHistory[0].toStatus, RelationshipStatus::Engaged);
            MARAS_CHECK_EQ(edge->statusHistory[0].timestamp, 12.0f);
            MARAS_CHECK_EQ(edge->statusHistory[1].toStatus, RelationshipStatus::Married);
            MARAS_CHECK_EQ(edge->statusHistory[1].timestamp, 20.0f);

            // Legacy status 5 (Deceased) entry must not exist
            MARAS_CHECK(state.relationships->FindEdge(ActorId::Player(), ActorId{ 0x02002000u }) == nullptr);
            // Post-load FillGaps places remapped spouse in hierarchy rank 0
            MARAS_CHECK_EQ(state.hierarchy->GetRank(remappedSpouse), 0);
        }
    }

    MARAS_TEST(CoSaveMigrations, PreV5_V4_PresumedFatherInferenceAndPreV4PHOU) {
        const ActorId mother{ 0x00010010u };
        const ActorId deceivedHusband{ 0x00010020u };
        const ActorId secretBioFather{ 0x00010030u };
        const ChildId synChild{ 0xFF700005u };
        const HouseId house{ 0x00060001u };

        InMemoryCoSaveWriter writer{};

        // 1. Write pre-v4 'PHOU' (version = 3: no initialMoveDone bool, no stayAtHome bool)
        MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkPHOU, 3u));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u))); // 1 house
        MARAS_REQUIRE(writer.WritePod(house.raw()));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0x00060002u))); // marker
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u))); // 1 tenant
        MARAS_REQUIRE(writer.WritePod(mother.raw()));

        // 2. Write v4 'NPCR' (version = 4: no presumedFather, no training fields, no bioFather meta)
        MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkNPCR, 4u));
        MARAS_REQUIRE(writer.WritePod(CoSaveConstants::kMagicNumber));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0u))); // 0 explicit profiles

        // 1 married pair: (mother, deceivedHusband)
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u)));
        MARAS_REQUIRE(writer.WritePod(mother.raw()));
        MARAS_REQUIRE(writer.WritePod(deceivedHusband.raw()));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(RelationshipStatus::Married)));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(RelationshipStatus::Candidate)));
        MARAS_REQUIRE(writer.WritePod(false)); // isOpen
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0u))); // historyCount
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0u))); // cheatCount

        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0u))); // pendingRelCount
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0u))); // pendingChildCount

        // 1 child in v4 schema (Unborn stage with birthTimestamp > 0 -> upgrades to Infant!)
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u)));
        MARAS_REQUIRE(writer.WritePod(synChild.raw()));
        MARAS_REQUIRE(writer.WritePod(mother.raw()));
        MARAS_REQUIRE(writer.WritePod(deceivedHusband.raw()));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::int32_t>(7)));
        MARAS_REQUIRE(writer.WritePod(false)); // isAdopted
        MARAS_REQUIRE(writer.WritePod(mother.raw()));
        MARAS_REQUIRE(writer.WritePod(secretBioFather.raw()));
        WriteRawString(writer, "BabySofie");
        MARAS_REQUIRE(writer.WritePod(true));  // isBioChild
        MARAS_REQUIRE(writer.WritePod(1.0f));  // conceptionTs
        MARAS_REQUIRE(writer.WritePod(8.5f));  // birthTs > 0 while stage == Unborn
        MARAS_REQUIRE(writer.WritePod(true));  // isSecret
        MARAS_REQUIRE(writer.WritePod(false)); // discovered
        MARAS_REQUIRE(writer.WritePod(false)); // discoveredByPartner
        MARAS_REQUIRE(writer.WritePod(true));  // hasPhenotypeMismatch
        MARAS_REQUIRE(writer.WritePod(8.5f));  // lastCheckTs
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(ChildStage::Unborn)));
        WriteRawString(writer, "");
        MARAS_REQUIRE(writer.WritePod(false)); // isSecretForBioFather
        MARAS_REQUIRE(writer.WritePod(true));  // isSecretForPartner
        MARAS_REQUIRE(writer.WritePod(false)); // discoveredByBioFather
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u))); // gender

        MockFormIdResolver resolver{};
        resolver.names[secretBioFather] = "Brynjolf";
        resolver.races[secretBioFather] = FormRawId{ 0x00013746u };

        WorldState state{};
        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport report = LoadWorldState(state, reader, resolver);
        MARAS_REQUIRE(report.allChunksSucceeded);

        const auto* child = state.lineage->FindChild(synChild);
        MARAS_REQUIRE(child != nullptr);
        MARAS_CHECK_EQ(child->presumedFather, deceivedHusband);
        MARAS_CHECK_EQ(child->stage, ChildStage::Infant);
        MARAS_CHECK_EQ(child->paternitySecret.bioFatherName, std::string_view("Brynjolf"));
        MARAS_CHECK_EQ(child->paternitySecret.bioFatherRaceId, FormRawId{ 0x00013746u });
        MARAS_CHECK_EQ(child->trainingClassIndex, -1);
        MARAS_CHECK_EQ(child->savedRaceIndex, static_cast<std::uint8_t>(255));
        MARAS_CHECK_EQ(state.lineage->NextSyntheticChildRaw(), 0xFF700006u);

        const auto* houseRec = state.household->FindHouse(house);
        MARAS_REQUIRE(houseRec != nullptr);
        MARAS_CHECK(!houseRec->initialMoveDone);
        MARAS_CHECK(state.household->GetTenantStayAtHome(mother.raw()));
    }

    MARAS_TEST(CoSaveMigrations, SettingsCNFG_LegacyHeaderlessAndSubVersions) {
        // 1. Headerless legacy 'CNFG' (first byte != 0xFF)
        {
            InMemoryCoSaveWriter writer{};
            MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkCNFG, 4u));
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u)));   // skipWedding = true
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u)));   // alwaysSuccessMarriage = true
            MARAS_REQUIRE(writer.WritePod(static_cast<std::int32_t>(2)));    // logDestination
            MARAS_REQUIRE(writer.WritePod(static_cast<std::int32_t>(3)));    // logLevel
            MARAS_REQUIRE(writer.WritePod(2.5f));                            // affectionDecayMultiplier
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(0u)));   // confirmStatusChange_Marry
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u)));   // confirmBreakup_Engagement
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(0u)));   // confirmDivorce
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u)));   // confirmDivorceDuringLowAffectionQuest
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint8_t>(1u)));   // migratedFromStorageUtil

            WorldState state{};
            InMemoryCoSaveReader reader(writer.Records());
            const CoSaveLoadReport report = LoadWorldState(state, reader);
            MARAS_REQUIRE(report.allChunksSucceeded);
            MARAS_CHECK(state.settings->skipWedding);
            MARAS_CHECK(state.settings->alwaysSuccessMarriage);
            MARAS_CHECK_EQ(state.settings->affectionDecayMultiplier, 2.5f);
            MARAS_CHECK(!state.settings->confirmDivorce);
            MARAS_CHECK(state.settings->childTrainingRequireFaction);
        }

        // 2. SubVersion 1 'CNFG': consumes legacyExcludePlayerActivePartners & legacyIntervalDays without shifting alignment
        {
            SettingsData src{};
            src.skipWedding = true;
            src.matchmakingUseLLM = false;
            src.playerPartnerCheatingChanceMultiplier = 2.25f;
            src.childTrainingDurationDays = 30;
            src.childTrainingCost = 5000;
            src.childTrainingRequireFaction = false; // subVersion 1 < 3 must force this back to true!

            InMemoryCoSaveWriter writer{};
            MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkCNFG, 7u));
            MARAS_REQUIRE(writer.WritePod(CoSaveConstants::kSettingsVersionMarker));
            MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u))); // subVersion 1

            ForEachPersistedSettingField([&](std::string_view key, auto memberPtr, bool) {
                if (key == "matchmakingUseLLM") {
                    // Legacy removed bool present in subVersion < 4
                    (void)writer.WritePod(static_cast<std::uint8_t>(1u));
                }
                if (key == "playerPartnerCheatingChanceMultiplier") {
                    // Legacy removed int32 interval present in subVersion < 2
                    (void)writer.WritePod(static_cast<std::int32_t>(99));
                }
                const auto& field = src.*memberPtr;
                using FieldT = std::decay_t<decltype(field)>;
                if constexpr (std::is_same_v<FieldT, bool>) {
                    (void)writer.WritePod(static_cast<std::uint8_t>(field ? 1u : 0u));
                } else if constexpr (std::is_enum_v<FieldT>) {
                    (void)writer.WritePod(static_cast<std::uint8_t>(field));
                } else {
                    (void)writer.WritePod(field);
                }
            });

            WorldState state{};
            InMemoryCoSaveReader reader(writer.Records());
            const CoSaveLoadReport report = LoadWorldState(state, reader);
            MARAS_REQUIRE(report.allChunksSucceeded);
            MARAS_CHECK(!state.settings->matchmakingUseLLM);
            MARAS_CHECK_EQ(state.settings->playerPartnerCheatingChanceMultiplier, 2.25f);
            MARAS_CHECK_EQ(state.settings->childTrainingDurationDays, 30);
            MARAS_CHECK_EQ(state.settings->childTrainingCost, 5000);
            // SubVersion < 3 migration forces childTrainingRequireFaction = true
            MARAS_CHECK(state.settings->childTrainingRequireFaction);
        }
    }

    MARAS_TEST(CoSaveMigrations, V7FullTwelveChunkLosslessRoundTripAndOrderIndependence) {
        MockFormIdResolver resolver{};
        const WorldState original = BuildRichV7WorldState(resolver);

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(original, writer));
        MARAS_REQUIRE_EQ(writer.Records().size(), std::size_t{ 12 });

        // 1. Canonical arrival order round-trip
        WorldState canonicalLoaded{};
        {
            InMemoryCoSaveReader reader(writer.Records());
            const CoSaveLoadReport report = LoadWorldState(canonicalLoaded, reader, resolver);
            MARAS_REQUIRE(report.allChunksSucceeded);
            MARAS_CHECK_EQ(report.loadedChunkCount, 12u);
            MARAS_CHECK_EQ(report.failedChunkCount, 0u);
        }

        MARAS_CHECK(original.actors == canonicalLoaded.actors);
        MARAS_CHECK(original.relationships == canonicalLoaded.relationships);
        MARAS_CHECK(original.affairs == canonicalLoaded.affairs);
        MARAS_CHECK(original.lineage == canonicalLoaded.lineage);
        MARAS_CHECK(original.hierarchy == canonicalLoaded.hierarchy);
        MARAS_CHECK(original.household == canonicalLoaded.household);
        MARAS_CHECK(original.affection == canonicalLoaded.affection);
        MARAS_CHECK(original.deceased == canonicalLoaded.deceased);
        MARAS_CHECK(original.cooldowns == canonicalLoaded.cooldowns);
        MARAS_CHECK(original.settings == canonicalLoaded.settings);
        MARAS_CHECK(original == canonicalLoaded);

        // Verify explicit 0 bed owner sentinel AND Stage 4 Adult Child registration survived
        MARAS_REQUIRE(canonicalLoaded.household->AllOriginalBedOwners().contains(RefId{ 0x000B0001u }));
        MARAS_CHECK_EQ(canonicalLoaded.household->AllOriginalBedOwners().at(RefId{ 0x000B0001u }), FormRawId{ 0u });
        MARAS_CHECK(canonicalLoaded.actors->IsRegisteredAdult(RefId{ 0xFF800100u }));
        MARAS_CHECK(!canonicalLoaded.actors->IsRegisteredAdult(RefId{ 0x00050001u }));

        // 2. Completely reversed chunk arrival order (CNFG -> ACTR -> ... -> PLCH before PHOU -> DCSD before NPCR)
        std::vector<CoSaveRecordChunk> reversedChunks = writer.Records();
        std::reverse(reversedChunks.begin(), reversedChunks.end());

        WorldState reversedLoaded{};
        {
            InMemoryCoSaveReader reader(std::move(reversedChunks));
            const CoSaveLoadReport report = LoadWorldState(reversedLoaded, reader, resolver);
            MARAS_REQUIRE(report.allChunksSucceeded);
            MARAS_CHECK_EQ(report.loadedChunkCount, 12u);
        }

        MARAS_CHECK(canonicalLoaded.actors == reversedLoaded.actors);
        MARAS_CHECK(canonicalLoaded.relationships == reversedLoaded.relationships);
        MARAS_CHECK(canonicalLoaded.affairs == reversedLoaded.affairs);
        MARAS_CHECK(canonicalLoaded.lineage == reversedLoaded.lineage);
        MARAS_CHECK(canonicalLoaded.hierarchy == reversedLoaded.hierarchy);
        MARAS_CHECK(canonicalLoaded.household == reversedLoaded.household);
        MARAS_CHECK(canonicalLoaded.affection == reversedLoaded.affection);
        MARAS_CHECK(canonicalLoaded.deceased == reversedLoaded.deceased);
        MARAS_CHECK(canonicalLoaded.cooldowns == reversedLoaded.cooldowns);
        MARAS_CHECK(canonicalLoaded.settings == reversedLoaded.settings);
        MARAS_CHECK(canonicalLoaded == reversedLoaded);
    }

    MARAS_TEST(CoSaveMigrations, SyntheticPoolBoundsAndUnloadedPlacedNpcPreservation) {
        MockFormIdResolver resolver{};
        WorldState source = BuildRichV7WorldState(resolver);

        const ChildId orphanedFmrChild{ 0xFF800999u };
        const ActorId orphanedDynamicAdult{ 0xFF900111u };

        // Add an orphaned 0xFF800999 FMR child and an orphaned 0xFF900111 dynamic adult before saving
        {
            ChildRecord dynChild{};
            dynChild.childId = orphanedFmrChild;
            (void)dynChild.guardians.Assign(ActorId::Player(), ActorId{ 0x0001A694u });
            dynChild.name = "OrphanedDynChild";
            dynChild.stage = ChildStage::Child;
            source.lineage.Write().UpsertChild(dynChild);
            (void)source.SyncChildHouseholdState(orphanedFmrChild, HouseId{ 0x000165A2u });

            ActorProfileRecord dynAdultProf{};
            dynAdultProf.refId = orphanedDynamicAdult.AsRefId();
            dynAdultProf.baseId = BaseId{ orphanedDynamicAdult.raw() };
            source.actors.Write().Upsert(dynAdultProf);
            (void)source.TransitionRelationship(
                orphanedDynamicAdult, ActorId{ 0x00030010u }, RelationshipStatus::Lover, 19.0f);
            source.affection.Write().SetPermanent(orphanedDynamicAdult, 77);
        }

        // Dynamic 0xFF800xxx / 0xFF900xxx IDs MUST NOT corrupt synthetic child pool [0xFF700000..0xFF7FFFFF]
        MARAS_CHECK_EQ(source.lineage->NextSyntheticChildRaw(), 0xFF70000Bu);

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(source, writer));

        // Simulate reload where 0xFF800999 & 0xFF900111 were deleted/unspawned, while 0xFF800100 & unloaded placed spouse 0x0001A694 remain valid
        resolver.missingDynamicRefs.insert(RefId{ orphanedFmrChild.raw() });
        resolver.missingDynamicRefs.insert(orphanedDynamicAdult.AsRefId());

        WorldState loaded{};
        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport report = LoadWorldState(loaded, reader, resolver);
        MARAS_REQUIRE(report.allChunksSucceeded);

        // 1. Synthetic pool stayed strictly inside [0xFF700001..0xFF7FFFFF]
        MARAS_CHECK_EQ(loaded.lineage->NextSyntheticChildRaw(), 0xFF70000Bu);
        const SyntheticChildId nextSyn = loaded.lineage.Write().AllocateSyntheticChildId();
        MARAS_CHECK_EQ(nextSyn, SyntheticChildId{ 0xFF70000Bu });

        // 2. Orphaned dynamic 0xFF refs were pruned cleanly across all tables
        MARAS_CHECK(loaded.lineage->FindChild(orphanedFmrChild) == nullptr);
        MARAS_CHECK(loaded.household->FindChildState(orphanedFmrChild) == nullptr);
        MARAS_CHECK(loaded.household->GetTenantHouse(orphanedFmrChild.raw()).IsNull());
        MARAS_CHECK(loaded.actors->Find(orphanedDynamicAdult.AsRefId()) == nullptr);
        MARAS_CHECK(!loaded.affection->HasPermanent(orphanedDynamicAdult));

        // 3. Present dynamic adult child 0xFF800100 and synthetic unborn child 0xFF70000A were preserved
        MARAS_CHECK(loaded.lineage->FindChild(ChildId{ 0xFF800100u }) != nullptr);
        MARAS_CHECK(loaded.lineage->FindChild(ChildId{ 0xFF70000Au }) != nullptr);

        // 4. Unloaded placed spouse 0x0001A694 preserved in Household, SPAS, Hierarchy, and Affection (Audit Bug #3)
        const ActorId unloadedSpouse{ 0x0001A694u };
        MARAS_CHECK_EQ(loaded.household->GetTenantHouse(unloadedSpouse.raw()), HouseId{ 0x000165A2u });
        MARAS_CHECK(loaded.household->IsCellSharedWithPlayer(CellId{ 0x000165A8u }));
        MARAS_CHECK_EQ(loaded.hierarchy->GetRank(unloadedSpouse), 0);
        MARAS_CHECK_EQ(loaded.affection->GetPermanent(unloadedSpouse), 92);
    }

    MARAS_TEST(CoSaveMigrations, TruncatedChunkAtomicRollbackAndRevertZeroBleed) {
        const ActorId sentinelActor{ 0x0001A694u };
        WorldState state{};
        ActorProfileRecord sentinelProf{};
        sentinelProf.refId = sentinelActor.AsRefId();
        state.actors.Write().Upsert(sentinelProf);
        state.household.Write().UpsertSharedCell(
            CellId{ 0x000165A8u },
            SharedCellRecord{
                .sharedWithPlayer = true,
                .originalPublicRecorded = true,
                .originalPublicState = false,
                .sharingSpouses = { sentinelActor },
            });
        (void)state.PromoteToMarried(ActorId::Player(), sentinelActor, 5.0f);

        InMemoryCoSaveWriter writer{};

        // Valid 'AFCT' chunk
        MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkAFCT, 7u));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u)));
        MARAS_REQUIRE(writer.WritePod(sentinelActor.raw()));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::int32_t>(84)));

        // Truncated 'NPCR' chunk: claims 5 profiles, ends immediately after magic + count
        MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkNPCR, 7u));
        MARAS_REQUIRE(writer.WritePod(CoSaveConstants::kMagicNumber));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(5u)));

        // Corrupt 'PHOU' chunk: tenantCount exceeds kMaxTenants sanity cap
        MARAS_REQUIRE(writer.OpenRecord(CoSaveConstants::kChunkPHOU, 7u));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(1u)));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0x000165A2u)));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(0x000165B0u)));
        MARAS_REQUIRE(writer.WritePod(true));
        MARAS_REQUIRE(writer.WritePod(static_cast<std::uint32_t>(999999u)));

        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport report = LoadWorldState(state, reader);
        MARAS_CHECK(!report.allChunksSucceeded);
        MARAS_CHECK_EQ(report.loadedChunkCount, 1u);
        MARAS_CHECK_EQ(report.failedChunkCount, 2u);

        // Valid AFCT loaded, while failed NPCR & PHOU rolled back without wiping pre-existing actors/SPAS!
        MARAS_CHECK_EQ(state.affection->GetPermanent(sentinelActor), 84);
        MARAS_CHECK(state.actors->Find(sentinelActor.AsRefId()) != nullptr);
        MARAS_CHECK(state.household->IsCellSharedWithPlayer(CellId{ 0x000165A8u }));

        // Audit Bug #1: WorldState::Revert() leaves zero cross-save bleed across all 10 tables
        state.Revert();
        MARAS_CHECK(state == WorldState{});
    }

    MARAS_TEST(CoSaveMigrations, CowTableStructuralSharingAndSnapshotIsolation) {
        MockFormIdResolver resolver{};
        WorldState state = BuildRichV7WorldState(resolver);
        const ActorId spouseA{ 0x0001A694u };

        const WorldStateSnapshot snap = state.Snapshot();
        MARAS_CHECK(state.actors.SharesAllocationWith(snap.actors));
        MARAS_CHECK(state.affection.SharesAllocationWith(snap.affection));
        MARAS_CHECK(state.household.SharesAllocationWith(snap.household));

        // Mutate ONLY AffectionTable
        state.affection.Write().SetPermanent(spouseA, 33);

        // Snapshot retains immutable point-in-time score (92), live state has 33
        MARAS_CHECK_EQ(snap.affection->GetPermanent(spouseA), 92);
        MARAS_CHECK_EQ(state.affection->GetPermanent(spouseA), 33);
        MARAS_CHECK(!state.affection.SharesAllocationWith(snap.affection));

        // All 9 untouched tables remain zero-copy shared with the snapshot!
        MARAS_CHECK(state.actors.SharesAllocationWith(snap.actors));
        MARAS_CHECK(state.relationships.SharesAllocationWith(snap.relationships));
        MARAS_CHECK(state.affairs.SharesAllocationWith(snap.affairs));
        MARAS_CHECK(state.lineage.SharesAllocationWith(snap.lineage));
        MARAS_CHECK(state.hierarchy.SharesAllocationWith(snap.hierarchy));
        MARAS_CHECK(state.household.SharesAllocationWith(snap.household));
        MARAS_CHECK(state.deceased.SharesAllocationWith(snap.deceased));
        MARAS_CHECK(state.cooldowns.SharesAllocationWith(snap.cooldowns));
        MARAS_CHECK(state.settings.SharesAllocationWith(snap.settings));
    }

    MARAS_TEST(CoSaveMigrations, LegacyV4PhysicalChildAndAdultChildSpouseTenantParity) {
        MockFormIdResolver resolver{};
        const HouseId breezehome{ 0x000165A2u };
        const ChildId legacyV4Child{ 0x0001B141u };   // Vanilla child in PLCH + PHOU prior to v5 LineageTable
        const ChildId gradSpouseChild{ 0xFF800555u }; // NPC couple's graduated Stage 4 Adult Child who married Player
        const ActorId npcParentA{ 0x00030010u };
        const ActorId npcParentB{ 0x00030020u };

        WorldState src{};
        src.household.Write().RegisterHouse(breezehome, RefId{ 0x000165B0u }, true);
        src.household.Write().AssignTenant(legacyV4Child.raw(), breezehome, true, true);
        src.household.Write().AssignTenant(gradSpouseChild.raw(), breezehome, /*stayAtHome=*/false, false);

        if (auto* v4State = src.household.Write().FindChildStateMutable(legacyV4Child)) {
            v4State->petId = RefId{ 0x00090011u };
            v4State->playedScene1 = true;
        }
        // Also simulate a stale PLCH entry for gradSpouseChild to verify pruning PLCH does NOT evict PHOU!
        src.household.Write().UpsertChildState(ChildHouseholdRecord{
            .childId = gradSpouseChild,
            .currentHouseId = breezehome,
        });

        ChildRecord gradRec{};
        gradRec.childId = gradSpouseChild;
        (void)gradRec.guardians.Assign(npcParentA, npcParentB);
        gradRec.bioMother = npcParentB;
        gradRec.bioFather = npcParentA;
        gradRec.name = "GrownNpcChild";
        gradRec.stage = ChildStage::Adult;
        src.lineage.Write().UpsertChild(gradRec);
        (void)src.PromoteToMarried(ActorId::Player(), gradSpouseChild.AsActorId(), 30.0f, ActorId::Player(), resolver);

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(src, writer));

        WorldState loaded{};
        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport report = LoadWorldState(loaded, reader, resolver);
        MARAS_REQUIRE(report.allChunksSucceeded);

        // 1. Legacy v4 physical child without LineageTable entry survives in both PLCH and PHOU
        const auto* loadedV4State = loaded.household->FindChildState(legacyV4Child);
        MARAS_REQUIRE(loadedV4State != nullptr);
        MARAS_CHECK_EQ(loadedV4State->currentHouseId, breezehome);
        MARAS_CHECK_EQ(loadedV4State->petId, RefId{ 0x00090011u });
        MARAS_CHECK(loadedV4State->playedScene1);
        MARAS_CHECK_EQ(loaded.household->GetTenantHouse(legacyV4Child.raw()), breezehome);

        // 2. Graduated Stage 4 Adult Child spouse is pruned from PLCH but NEVER evicted from PHOU
        MARAS_CHECK(loaded.household->FindChildState(gradSpouseChild) == nullptr);
        MARAS_CHECK_EQ(loaded.household->GetTenantHouse(gradSpouseChild.raw()), breezehome);
        MARAS_CHECK(!loaded.household->GetTenantStayAtHome(gradSpouseChild.raw()));
    }

    MARAS_TEST(CoSaveMigrations, DeceasedSecrecyExtensionRoundTripsAndOmitsWhenAbsent) {
        class ShiftResolver final : public IFormIdResolver {
        public:
            bool ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept override {
                if (oldRawId == 0) {
                    outNewRawId = 0;
                    return false;
                }
                if ((oldRawId & 0xFF000000u) == 0x05000000u) {
                    outNewRawId = (oldRawId & 0x00FFFFFFu) | 0x06000000u;
                    return true;
                }
                outNewRawId = oldRawId;
                return true;
            }
        };

        auto findDcsdPayload = [](const std::vector<CoSaveRecordChunk>& chunks) -> std::vector<std::uint8_t> {
            for (const auto& c : chunks) {
                if (c.type == CoSaveConstants::kChunkDCSD) {
                    return c.payload;
                }
            }
            return {};
        };

        // 1. Plain deceased record with NO secrecy flags omits 'DCS1' completely
        {
            WorldState plain{};
            DeceasedActorRecord rec{};
            rec.baseId = BaseId{ 0x05001000u };
            rec.refId = RefId{ 0x05001001u };
            rec.name = "PlainDeceased";
            rec.isChild = false;
            rec.partnersAtDeath.push_back(DeceasedPartnerEntry{
                .partnerBaseId = BaseId{ 0x05002000u },
                .partnerRefId = RefId{ 0x05002001u },
                .partnerName = "PublicSpouse",
                .statusAtDeath = RelationshipStatus::Married,
                .isPlayer = false,
            });
            plain.deceased.Write().UpsertRecord(rec);

            InMemoryCoSaveWriter wPlain{};
            MARAS_REQUIRE(SaveWorldState(plain, wPlain));
            const auto dcsdBytes = findDcsdPayload(wPlain.Records());
            MARAS_REQUIRE(!dcsdBytes.empty());
            std::uint32_t tailMagic = 0;
            if (dcsdBytes.size() >= sizeof(std::uint32_t)) {
                std::memcpy(&tailMagic, dcsdBytes.data() + dcsdBytes.size() - sizeof(std::uint32_t), sizeof(std::uint32_t));
            }
            MARAS_CHECK(tailMagic != CoSaveConstants::kExtDeceasedSecrecyDCS1);
        }

        // 2. Secret affair & secret paternity metadata round-trip cleanly with FormID remapping
        WorldState src{};
        {
            DeceasedActorRecord adult{};
            adult.baseId = BaseId{ 0x05001000u };
            adult.refId = RefId{ 0x05001001u };
            adult.name = "CheatingDeceased";
            adult.isChild = false;
            adult.partnersAtDeath.push_back(DeceasedPartnerEntry{
                .partnerBaseId = BaseId{ 0x05002000u },
                .partnerRefId = RefId{ 0x05002001u },
                .partnerName = "ExDivorcedAffair",
                .statusAtDeath = RelationshipStatus::Lover,
                .isPlayer = false,
                .isSecretAffair = true,
                .publicStatusAtDeath = RelationshipStatus::Divorced,
            });
            adult.partnersAtDeath.push_back(DeceasedPartnerEntry{
                .partnerBaseId = BaseId{ Foundation::kPlayerRefRawId },
                .partnerRefId = RefId::Player(),
                .partnerName = "PlayerParamour",
                .statusAtDeath = RelationshipStatus::Lover,
                .isPlayer = true,
                .isSecretAffair = true,
                .publicStatusAtDeath = RelationshipStatus::Unknown,
            });
            src.deceased.Write().UpsertRecord(adult);

            DeceasedActorRecord child{};
            child.baseId = BaseId{ 0x05003000u };
            child.refId = RefId{ 0x05003001u };
            child.name = "SecretFatherChild";
            child.isChild = true;
            child.childInfo = DeceasedChildMetadata{
                .guardianABaseId = BaseId{ 0x05001000u },
                .guardianARefId = RefId{ 0x05001001u },
                .guardianAName = "Mother",
                .guardianBBaseId = BaseId{ 0x05002000u },
                .guardianBRefId = RefId{ 0x05002001u },
                .guardianBName = "PresumedDad",
                .bioMotherBaseId = BaseId{ 0x05001000u },
                .bioFatherBaseId = BaseId{ 0x05004000u },
                .isAdopted = false,
                .houseId = HouseId{ 0x05005000u },
                .publicBioFatherBaseId = BaseId{ 0x05002000u },
                .hasUndiscoveredPaternitySecret = true,
            };
            src.deceased.Write().UpsertRecord(child);
        }

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(src, writer));

        WorldState loaded{};
        ShiftResolver shift{};
        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport report = LoadWorldState(loaded, reader, shift);
        MARAS_REQUIRE(report.allChunksSucceeded);

        const DeceasedActorRecord* loadedAdult = loaded.deceased->FindByActor(ActorId{ 0x06001001u }, BaseId{ 0x06001000u });
        MARAS_REQUIRE(loadedAdult != nullptr);
        MARAS_REQUIRE_EQ(loadedAdult->partnersAtDeath.size(), std::size_t{ 2 });
        MARAS_CHECK(loadedAdult->partnersAtDeath[0].isSecretAffair);
        MARAS_CHECK_EQ(loadedAdult->partnersAtDeath[0].publicStatusAtDeath, RelationshipStatus::Divorced);
        MARAS_CHECK(loadedAdult->partnersAtDeath[1].isSecretAffair);
        MARAS_CHECK_EQ(loadedAdult->partnersAtDeath[1].publicStatusAtDeath, RelationshipStatus::Unknown);

        const DeceasedActorRecord* loadedChild = loaded.deceased->FindByActor(ActorId{ 0x06003001u }, BaseId{ 0x06003000u });
        MARAS_REQUIRE(loadedChild != nullptr);
        MARAS_REQUIRE(loadedChild->childInfo.has_value());
        MARAS_CHECK(loadedChild->childInfo->hasUndiscoveredPaternitySecret);
        MARAS_CHECK_EQ(loadedChild->childInfo->bioFatherBaseId, BaseId{ 0x06004000u });
        MARAS_CHECK_EQ(loadedChild->childInfo->publicBioFatherBaseId, BaseId{ 0x06002000u });
    }

}  // namespace MARAS::Tests
