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
                young.skyrimNetUuid = 0x1122334455667788ULL;
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
                grad.skyrimNetUuid = 0x8877665544332211ULL;
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

    MARAS_TEST(CoSaveMigrations, Phase3Rel1ExtensionOmittedRemappedValidatedAndPruned) {
        auto containsRel1Tag = [](const std::vector<std::uint8_t>& bytes) {
            const std::uint32_t tag = CoSaveConstants::kExtRelationshipStateREL1;
            const std::uint8_t b0 = static_cast<std::uint8_t>(tag & 0xFFu);
            const std::uint8_t b1 = static_cast<std::uint8_t>((tag >> 8) & 0xFFu);
            const std::uint8_t b2 = static_cast<std::uint8_t>((tag >> 16) & 0xFFu);
            const std::uint8_t b3 = static_cast<std::uint8_t>((tag >> 24) & 0xFFu);
            if (bytes.size() < 4u) {
                return false;
            }
            for (std::size_t i = 0; i + 4u <= bytes.size(); ++i) {
                if (bytes[i] == b0 && bytes[i + 1] == b1 && bytes[i + 2] == b2 && bytes[i + 3] == b3) {
                    return true;
                }
            }
            return false;
        };

        const ActorId placedNpc{ 0x0001A694u };
        const ActorId oldPluginNpc{ 0x05000010u };
        const ActorId newPluginNpc{ 0x07000010u };
        const ActorId synChildActor{ 0xFF700001u };
        const ActorId deadNpc{ 0x00019DF7u };
        const ActorId orphanDynNpc{ 0xFF800123u };

        // (a) 'MKMK' without 'REL1' loads Phase 3 defaults and omits 'REL1' on re-save
        {
            WorldState srcNoRel1{};
            srcNoRel1.cooldowns.Write().npcLastMatchmakingChangeDay[placedNpc] = 12.5f;
            MARAS_REQUIRE(!srcNoRel1.cooldowns.Read().HasAnyPhase3RelState());

            InMemoryCoSaveWriter w1{};
            MARAS_REQUIRE(SaveWorldState(srcNoRel1, w1));
            const auto itMkmk = std::find_if(
                w1.Records().begin(),
                w1.Records().end(),
                [](const CoSaveRecordChunk& c) { return c.type == CoSaveConstants::kChunkMKMK; });
            MARAS_REQUIRE(itMkmk != w1.Records().end());
            MARAS_CHECK(!containsRel1Tag(itMkmk->payload));

            WorldState loadedNoRel1{};
            InMemoryCoSaveReader r1(w1.Records());
            const CoSaveLoadReport rep1 = LoadWorldState(loadedNoRel1, r1);
            MARAS_REQUIRE(rep1.allChunksSucceeded);
            MARAS_CHECK_EQ(loadedNoRel1.cooldowns->npcLastMatchmakingChangeDay.at(placedNpc), 12.5f);
            MARAS_CHECK_EQ(loadedNoRel1.cooldowns->lastTimePaidDivorceFee, -1.0f);
            MARAS_CHECK(!loadedNoRel1.cooldowns->playerKiller);
            MARAS_CHECK(!loadedNoRel1.cooldowns->playerHadWedding);
            MARAS_CHECK(!loadedNoRel1.cooldowns->dontShowSkipWeddingMessage);
            MARAS_CHECK_EQ(loadedNoRel1.cooldowns->GetBrokeupTime(placedNpc), -1.0f);
            MARAS_CHECK_EQ(loadedNoRel1.cooldowns->GetLastTimeSharedIncome(placedNpc), -1.0f);

            InMemoryCoSaveWriter w2{};
            MARAS_REQUIRE(SaveWorldState(loadedNoRel1, w2));
            const auto itResaved = std::find_if(
                w2.Records().begin(),
                w2.Records().end(),
                [](const CoSaveRecordChunk& c) { return c.type == CoSaveConstants::kChunkMKMK; });
            MARAS_REQUIRE(itResaved != w2.Records().end());
            MARAS_CHECK(!containsRel1Tag(itResaved->payload));
        }

        // (b) 'MKMK' + 'REL1' round-trip, FormID remapping, synthetic child ID rejection, & truncated 'REL1' rollback
        {
            WorldState srcRel1{};
            srcRel1.cooldowns.Write().npcLastMatchmakingChangeDay[oldPluginNpc] = 5.0f;
            srcRel1.cooldowns.Write().lastTimePaidDivorceFee = 22.5f;
            srcRel1.cooldowns.Write().playerKiller = true;
            srcRel1.cooldowns.Write().playerHadWedding = true;
            srcRel1.cooldowns.Write().dontShowSkipWeddingMessage = true;
            srcRel1.cooldowns.Write().SetBrokeupTime(oldPluginNpc, 30.0f);
            srcRel1.cooldowns.Write().SetLastTimeSharedIncome(oldPluginNpc, 31.0f);
            srcRel1.cooldowns.Write().SetBrokeupTime(synChildActor, 32.0f);
            srcRel1.cooldowns.Write().SetLastTimeSharedIncome(synChildActor, 33.0f);
            srcRel1.affection.Write().SetPermanent(newPluginNpc, 77);

            InMemoryCoSaveWriter writer{};
            MARAS_REQUIRE(SaveWorldState(srcRel1, writer));
            const auto itMkmk = std::find_if(
                writer.Records().begin(),
                writer.Records().end(),
                [](const CoSaveRecordChunk& c) { return c.type == CoSaveConstants::kChunkMKMK; });
            MARAS_REQUIRE(itMkmk != writer.Records().end());
            MARAS_CHECK(containsRel1Tag(itMkmk->payload));

            MockFormIdResolver resolver{};
            resolver.remapTable[oldPluginNpc.raw()] = newPluginNpc.raw();

            WorldState loadedRel1{};
            InMemoryCoSaveReader reader(writer.Records());
            const CoSaveLoadReport rep = LoadWorldState(loadedRel1, reader, resolver);
            MARAS_REQUIRE(rep.allChunksSucceeded);
            MARAS_CHECK_EQ(loadedRel1.cooldowns->lastTimePaidDivorceFee, 22.5f);
            MARAS_CHECK(loadedRel1.cooldowns->playerKiller);
            MARAS_CHECK(loadedRel1.cooldowns->playerHadWedding);
            MARAS_CHECK(loadedRel1.cooldowns->dontShowSkipWeddingMessage);
            MARAS_CHECK_EQ(loadedRel1.cooldowns->GetBrokeupTime(newPluginNpc), 30.0f);
            MARAS_CHECK_EQ(loadedRel1.cooldowns->GetLastTimeSharedIncome(newPluginNpc), 31.0f);
            MARAS_CHECK_EQ(loadedRel1.cooldowns->GetBrokeupTime(synChildActor), -1.0f);
            MARAS_CHECK_EQ(loadedRel1.cooldowns->GetLastTimeSharedIncome(synChildActor), -1.0f);

            // Truncate 'REL1' tail and verify atomic chunk rollback + Revert() zero-bleed
            for (auto& rec : writer.MutableRecords()) {
                if (rec.type == CoSaveConstants::kChunkMKMK) {
                    MARAS_REQUIRE(rec.payload.size() > 4u);
                    rec.payload.resize(rec.payload.size() - 4u);
                    break;
                }
            }
            WorldState partialState{};
            partialState.cooldowns.Write().SetBrokeupTime(placedNpc, 9.0f);
            InMemoryCoSaveReader corruptReader(writer.Records());
            const CoSaveLoadReport corruptRep = LoadWorldState(partialState, corruptReader, resolver);
            MARAS_CHECK(!corruptRep.allChunksSucceeded);
            MARAS_CHECK_EQ(corruptRep.failedChunkCount, 1u);
            MARAS_CHECK_EQ(partialState.cooldowns->GetBrokeupTime(placedNpc), 9.0f);
            MARAS_CHECK(!partialState.cooldowns->playerKiller);
            partialState.Revert();
            MARAS_CHECK(partialState == WorldState{});
        }

        // (c) FinalizeWorldStateAfterLoad prunes orphaned 0xFF and DeceasedTable entries, keeping unloaded placed NPC
        {
            WorldState ws{};
            MockFormIdResolver resolver{};
            resolver.missingDynamicRefs.insert(orphanDynNpc.AsRefId());

            DeceasedActorRecord deadRec{};
            deadRec.refId = deadNpc.AsRefId();
            deadRec.baseId = BaseId{ 0x0001347Bu };
            deadRec.name = "DeadNpc";
            deadRec.deathTimestamp = 4.0f;
            ws.deceased.Write().UpsertRecord(std::move(deadRec));

            ws.cooldowns.Write().SetBrokeupTime(placedNpc, 10.0f);
            ws.cooldowns.Write().SetLastTimeSharedIncome(placedNpc, 11.0f);
            ws.cooldowns.Write().SetBrokeupTime(deadNpc, 12.0f);
            ws.cooldowns.Write().SetLastTimeSharedIncome(deadNpc, 13.0f);
            ws.cooldowns.Write().SetBrokeupTime(orphanDynNpc, 14.0f);
            ws.cooldowns.Write().SetLastTimeSharedIncome(orphanDynNpc, 15.0f);

            (void)FinalizeWorldStateAfterLoad(ws, resolver);
            MARAS_CHECK_EQ(ws.cooldowns->GetBrokeupTime(placedNpc), 10.0f);
            MARAS_CHECK_EQ(ws.cooldowns->GetLastTimeSharedIncome(placedNpc), 11.0f);
            MARAS_CHECK_EQ(ws.cooldowns->GetBrokeupTime(deadNpc), -1.0f);
            MARAS_CHECK_EQ(ws.cooldowns->GetLastTimeSharedIncome(deadNpc), -1.0f);
            MARAS_CHECK_EQ(ws.cooldowns->GetBrokeupTime(orphanDynNpc), -1.0f);
            MARAS_CHECK_EQ(ws.cooldowns->GetLastTimeSharedIncome(orphanDynNpc), -1.0f);
        }

        // (d) RecordActorDeath sets playerKiller for active/historical Married/Engaged player partners only when killedByPlayer
        {
            MockFormIdResolver resolver{};
            resolver.baseIds[placedNpc] = BaseId{ 0x00013BABu };
            resolver.baseIds[deadNpc] = BaseId{ 0x0001347Bu };
            resolver.names[placedNpc] = "Ysolda";
            resolver.names[deadNpc] = "Camilla";

            // Non-player killer on Married partner leaves playerKiller false
            WorldState ws1{};
            ws1.settings.Write().enablePlayerPolygamy = true;
            MARAS_REQUIRE(ws1.PromoteToMarried(ActorId::Player(), placedNpc, 5.0f).changed);
            (void)ws1.RecordActorDeath(placedNpc, 10.0f, /*killedByPlayer=*/false, ActorId{}, resolver);
            MARAS_CHECK(!ws1.cooldowns->playerKiller);

            // Player killer on Dating/Lover leaves playerKiller false
            WorldState ws2{};
            (void)ws2.TransitionRelationship(ActorId::Player(), deadNpc, RelationshipStatus::Dating, 3.0f);
            (void)ws2.RecordActorDeath(deadNpc, 10.0f, /*killedByPlayer=*/true, ActorId::Player(), resolver);
            MARAS_CHECK(!ws2.cooldowns->playerKiller);

            // Player killer on active Engaged sets playerKiller = true
            WorldState ws3{};
            (void)ws3.TransitionRelationship(ActorId::Player(), placedNpc, RelationshipStatus::Engaged, 4.0f);
            (void)ws3.RecordActorDeath(placedNpc, 12.0f, /*killedByPlayer=*/true, ActorId::Player(), resolver);
            MARAS_CHECK(ws3.cooldowns->playerKiller);

            // Historical deceased Married partner re-processed with killedByPlayer sets playerKiller = true
            WorldState ws4{};
            DeceasedActorRecord priorDead{};
            priorDead.refId = placedNpc.AsRefId();
            priorDead.baseId = BaseId{ 0x00013BABu };
            priorDead.name = "Ysolda";
            priorDead.deathTimestamp = 8.0f;
            DeceasedPartnerEntry playerPartner{};
            playerPartner.isPlayer = true;
            playerPartner.partnerRefId = ActorId::Player().AsRefId();
            playerPartner.partnerBaseId = BaseId{ kPlayerBaseRawId };
            playerPartner.partnerName = "Player";
            playerPartner.statusAtDeath = RelationshipStatus::Married;
            priorDead.partnersAtDeath.push_back(std::move(playerPartner));
            ws4.deceased.Write().UpsertRecord(std::move(priorDead));
            (void)ws4.RecordActorDeath(placedNpc, 9.0f, /*killedByPlayer=*/true, ActorId::Player(), resolver);
            MARAS_CHECK(ws4.cooldowns->playerKiller);
        }
    }

    MARAS_TEST(CoSaveMigrations, FertilityDeliveryBatchFRT1_RoundTripOmissionAndOrphanPruning) {
        auto findNpcrPayload = [](const std::vector<CoSaveRecordChunk>& chunks) -> std::vector<std::uint8_t> {
            for (const auto& c : chunks) {
                if (c.type == CoSaveConstants::kChunkNPCR) {
                    return c.payload;
                }
            }
            return {};
        };

        auto payloadContainsTag = [](const std::vector<std::uint8_t>& bytes, std::uint32_t tag) {
            std::array<std::uint8_t, sizeof(std::uint32_t)> needle{};
            std::memcpy(needle.data(), &tag, sizeof(tag));
            return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
        };

        // 1. Omission: when ActiveDeliveryBatches is empty, 'FRT1' is omitted from 'NPCR'
        {
            WorldState emptyBatches{};
            InMemoryCoSaveWriter w{};
            MARAS_REQUIRE(SaveWorldState(emptyBatches, w));
            const auto npcr = findNpcrPayload(w.Records());
            MARAS_REQUIRE(!npcr.empty());
            MARAS_CHECK(!payloadContainsTag(npcr, CoSaveConstants::kExtFertilityBatchFRT1));
        }

        // 2. Round-trip with FormID remapping, live/missing born child pruning, and dead/orphaned mother pruning
        const ActorId savedLiveMother{ 0x02001010u };
        const ActorId remappedLiveMother{ 0x05001010u };
        const ActorId savedDeadMother{ 0x02001020u };
        const ActorId remappedDeadMother{ 0x05001020u };
        const ActorId orphanedDynMother{ 0xFF009911u };
        const ChildId liveInfant{ 0xFF700051u };
        const ChildId missingInfant{ 0xFF700052u };

        WorldState src{};
        {
            ActorProfileRecord p{};
            p.refId = savedLiveMother.AsRefId();
            p.baseId = BaseId{ 0x02001011u };
            p.socialClass = SocialClass::Working;
            p.isRegisteredAdult = true;
            src.actors.Write().Upsert(p);

            ChildRecord infant{};
            infant.childId = liveInfant;
            infant.bioMother = savedLiveMother;
            infant.bioFather = ActorId::Player();
            infant.name = "BornTwin";
            infant.stage = ChildStage::Infant;
            src.lineage.Write().UpsertChild(infant);

            ActiveDeliveryBatch liveBatch{};
            liveBatch.laborTotal = 3;
            liveBatch.laborStartGameDay = 42.5f;
            liveBatch.bornChildIds = { liveInfant, missingInfant };
            src.lineage.Write().UpsertActiveDeliveryBatch(savedLiveMother, liveBatch);

            DeceasedActorRecord deadRec{};
            deadRec.baseId = BaseId{ 0x02001021u };
            deadRec.refId = savedDeadMother.AsRefId();
            deadRec.name = "DeadMother";
            src.deceased.Write().UpsertRecord(deadRec);

            ActiveDeliveryBatch deadBatch{};
            deadBatch.laborTotal = 1;
            deadBatch.laborStartGameDay = 30.0f;
            deadBatch.bornChildIds = { liveInfant };
            src.lineage.Write().UpsertActiveDeliveryBatch(savedDeadMother, deadBatch);

            ActiveDeliveryBatch orphanBatch{};
            orphanBatch.laborTotal = 2;
            orphanBatch.laborStartGameDay = 35.0f;
            orphanBatch.bornChildIds = { liveInfant };
            src.lineage.Write().UpsertActiveDeliveryBatch(orphanedDynMother, orphanBatch);
        }

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(src, writer));
        MARAS_CHECK(payloadContainsTag(findNpcrPayload(writer.Records()), CoSaveConstants::kExtFertilityBatchFRT1));

        MockFormIdResolver resolver{};
        resolver.remapTable[savedLiveMother.raw()] = remappedLiveMother.raw();
        resolver.remapTable[savedDeadMother.raw()] = remappedDeadMother.raw();
        resolver.baseIds[remappedLiveMother] = BaseId{ 0x05001011u };
        resolver.deadOrInvalidActors.insert(remappedDeadMother);
        resolver.missingDynamicRefs.insert(orphanedDynMother.AsRefId());

        WorldState loaded{};
        InMemoryCoSaveReader reader(writer.Records());
        const CoSaveLoadReport rep = LoadWorldState(loaded, reader, resolver);
        MARAS_REQUIRE(rep.allChunksSucceeded);

        // Live mother remapped, missingInfant pruned, dead & orphaned mothers dropped
        MARAS_CHECK_EQ(loaded.lineage->AllActiveDeliveryBatches().size(), std::size_t{ 1 });
        MARAS_CHECK(loaded.lineage->FindActiveDeliveryBatch(remappedDeadMother) == nullptr);
        MARAS_CHECK(loaded.lineage->FindActiveDeliveryBatch(orphanedDynMother) == nullptr);

        const auto* liveLoaded = loaded.lineage->FindActiveDeliveryBatch(remappedLiveMother);
        MARAS_REQUIRE(liveLoaded != nullptr);
        MARAS_CHECK_EQ(liveLoaded->laborTotal, 3);
        MARAS_CHECK_EQ(liveLoaded->laborStartGameDay, 42.5f);
        MARAS_REQUIRE_EQ(liveLoaded->bornChildIds.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(liveLoaded->bornChildIds[0], liveInfant);

        // Erasing the last remaining child removes it from bornChildIds
        loaded.lineage.Write().EraseChild(liveInfant);
        const auto* afterErase = loaded.lineage->FindActiveDeliveryBatch(remappedLiveMother);
        MARAS_REQUIRE(afterErase != nullptr);
        MARAS_CHECK(afterErase->bornChildIds.empty());
    }

    MARAS_TEST(CoSaveMigrations, Phase7_PLC1AndBNT1_RoundTripOmissionOrderAndPruning) {
        auto findChunkPayload = [](const std::vector<CoSaveRecordChunk>& chunks, std::uint32_t type) -> std::vector<std::uint8_t> {
            for (const auto& c : chunks) {
                if (c.type == type) {
                    return c.payload;
                }
            }
            return {};
        };

        auto payloadContainsTag = [](const std::vector<std::uint8_t>& bytes, std::uint32_t tag) {
            std::array<std::uint8_t, sizeof(std::uint32_t)> needle{};
            std::memcpy(needle.data(), &tag, sizeof(tag));
            return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
        };

        // 1. Omission when empty/default
        {
            WorldState empty{};
            InMemoryCoSaveWriter w{};
            MARAS_REQUIRE(SaveWorldState(empty, w));
            const auto plch = findChunkPayload(w.Records(), CoSaveConstants::kChunkPLCH);
            const auto bntr = findChunkPayload(w.Records(), CoSaveConstants::kChunkBNTR);
            MARAS_REQUIRE(!plch.empty());
            MARAS_REQUIRE(!bntr.empty());
            MARAS_CHECK(!payloadContainsTag(plch, CoSaveConstants::kExtPlayerChildrenStatePLC1));
            MARAS_CHECK(!payloadContainsTag(bntr, CoSaveConstants::kExtBanterSceneStateBNT1));
        }

        // 2. Full v7 round-trip, chunk-order independence ('PLCH' before 'PHOU'/'SPAS'),
        //    dynamic XMarker preservation in movedSceneActors, and orphan/deceased pruning
        const ChildId childLive{ 0x00071001u };
        const ChildId childDeadCritter{ 0x00071002u };
        const RefId savedLiveCritter{ 0x02003001u };
        const RefId remappedLiveCritter{ 0x05003001u };
        const RefId orphanedDynCritter{ 0xFF009955u };

        const ActorId liveMovedNpc{ 0x02004001u };
        const ActorId remappedLiveMovedNpc{ 0x05004001u };
        const ActorId deadMutedNpc{ 0x02004002u };
        const ActorId remappedDeadMutedNpc{ 0x05004002u };
        const ActorId orphanedDynMutedNpc{ 0xFF00C777u };
        const RefId dynSceneMarker{ 0xFF00B123u };

        const HouseId savedHouse{ 0x00072001u };
        const RefId savedHouseMarker{ 0x00072002u };
        const CellId savedSharedCell{ 0x00073001u };

        WorldState src{};
        {
            ActorProfileRecord movedProfile{};
            movedProfile.refId = liveMovedNpc.AsRefId();
            movedProfile.baseId = BaseId{ 0x02004011u };
            movedProfile.socialClass = SocialClass::Working;
            movedProfile.isRegisteredAdult = true;
            src.actors.Write().Upsert(movedProfile);

            DeceasedActorRecord deadRec{};
            deadRec.baseId = BaseId{ 0x02004012u };
            deadRec.refId = deadMutedNpc.AsRefId();
            deadRec.name = "DeadBannerNpc";
            src.deceased.Write().UpsertRecord(deadRec);

            // Household state including houses, shared cells, and 'PLC1' state
            auto& hh = src.household.Write();
            hh.RegisterHouse(savedHouse, savedHouseMarker, true);
            SharedCellRecord sharedCell{};
            sharedCell.sharedWithPlayer = true;
            sharedCell.originalPublicRecorded = true;
            sharedCell.originalPublicState = false;
            sharedCell.sharingSpouses.insert(liveMovedNpc);
            hh.UpsertSharedCell(savedSharedCell, sharedCell);

            hh.SetChildChestIntroduced(true);
            hh.SetPoorCounter(2);
            hh.SetNameCallingUsedLast(true);

            ChildHouseholdRecord recLive{};
            recLive.childId = childLive;
            recLive.transientCritterId = savedLiveCritter;
            hh.UpsertChildState(recLive);

            ChildHouseholdRecord recOrphanCritter{};
            recOrphanCritter.childId = childDeadCritter;
            recOrphanCritter.transientCritterId = orphanedDynCritter;
            hh.UpsertChildState(recOrphanCritter);

            // Cooldown state including 'BNT1'
            auto& cd = src.cooldowns.Write();
            cd.savedBanterCooldownHours = 12.0f;
            cd.lastBanterSceneDay = 15.0f;
            cd.movedSceneActors[liveMovedNpc] = dynSceneMarker;
            cd.movedSceneActors[deadMutedNpc] = RefId{ 0x00074001u };
            cd.skyrimNetMutedActors = { liveMovedNpc, liveMovedNpc, deadMutedNpc, orphanedDynMutedNpc };
        }

        InMemoryCoSaveWriter writer{};
        MARAS_REQUIRE(SaveWorldState(src, writer));
        MARAS_CHECK(payloadContainsTag(findChunkPayload(writer.Records(), CoSaveConstants::kChunkPLCH),
                                       CoSaveConstants::kExtPlayerChildrenStatePLC1));
        MARAS_CHECK(payloadContainsTag(findChunkPayload(writer.Records(), CoSaveConstants::kChunkBNTR),
                                       CoSaveConstants::kExtBanterSceneStateBNT1));

        // 2a. Pre-Phase-7 backward compatibility: stripping 'PLC1' and 'BNT1' loads defaults cleanly
        {
            std::vector<CoSaveRecordChunk> legacyChunks = writer.Records();
            auto stripExtension = [&](std::uint32_t chunkType, std::uint32_t extTag) {
                for (auto& c : legacyChunks) {
                    if (c.type != chunkType) {
                        continue;
                    }
                    std::array<std::uint8_t, sizeof(std::uint32_t)> needle{};
                    std::memcpy(needle.data(), &extTag, sizeof(extTag));
                    const auto it = std::search(c.payload.begin(), c.payload.end(), needle.begin(), needle.end());
                    MARAS_REQUIRE(it != c.payload.end());
                    c.payload.erase(it, c.payload.end());
                }
            };
            stripExtension(CoSaveConstants::kChunkPLCH, CoSaveConstants::kExtPlayerChildrenStatePLC1);
            stripExtension(CoSaveConstants::kChunkBNTR, CoSaveConstants::kExtBanterSceneStateBNT1);

            MockFormIdResolver legacyResolver{};
            legacyResolver.remapTable[savedLiveCritter.raw()] = remappedLiveCritter.raw();
            legacyResolver.remapTable[liveMovedNpc.raw()] = remappedLiveMovedNpc.raw();
            legacyResolver.baseIds[remappedLiveMovedNpc] = BaseId{ 0x05004011u };

            WorldState legacyLoaded{};
            InMemoryCoSaveReader legacyReader(legacyChunks);
            const CoSaveLoadReport legacyRep = LoadWorldState(legacyLoaded, legacyReader, legacyResolver);
            MARAS_REQUIRE(legacyRep.allChunksSucceeded);
            MARAS_CHECK(legacyLoaded.household->IsChildChestIntroduced());
            MARAS_CHECK_EQ(legacyLoaded.household->GetPoorCounter(), 0);
            MARAS_CHECK(!legacyLoaded.household->IsNameCallingUsedLast());
            const auto* cs = legacyLoaded.household->FindChildState(childLive);
            MARAS_REQUIRE(cs != nullptr);
            MARAS_CHECK(cs->transientCritterId.IsNull());
            MARAS_CHECK(legacyLoaded.cooldowns->movedSceneActors.empty());
            MARAS_CHECK(legacyLoaded.cooldowns->skyrimNetMutedActors.empty());
        }

        // 2b. Reorder chunks so 'PLCH' precedes 'PHOU' and 'SPAS' to verify CloneHousehold* preservation
        std::vector<CoSaveRecordChunk> reordered = writer.Records();
        std::stable_sort(reordered.begin(), reordered.end(), [](const CoSaveRecordChunk& a, const CoSaveRecordChunk& b) {
            auto rank = [](std::uint32_t t) {
                if (t == CoSaveConstants::kChunkPLCH) return 0;
                if (t == CoSaveConstants::kChunkPHOU) return 1;
                if (t == CoSaveConstants::kChunkSPAS) return 2;
                return 3;
            };
            return rank(a.type) < rank(b.type);
        });

        MockFormIdResolver resolver{};
        resolver.remapTable[savedLiveCritter.raw()] = remappedLiveCritter.raw();
        resolver.remapTable[liveMovedNpc.raw()] = remappedLiveMovedNpc.raw();
        resolver.remapTable[deadMutedNpc.raw()] = remappedDeadMutedNpc.raw();
        resolver.baseIds[remappedLiveMovedNpc] = BaseId{ 0x05004011u };
        resolver.deadOrInvalidActors.insert(remappedDeadMutedNpc);
        resolver.missingDynamicRefs.insert(orphanedDynCritter);
        resolver.missingDynamicRefs.insert(orphanedDynMutedNpc.AsRefId());

        WorldState loaded{};
        InMemoryCoSaveReader reader(reordered);
        const CoSaveLoadReport rep = LoadWorldState(loaded, reader, resolver);
        MARAS_REQUIRE(rep.allChunksSucceeded);

        // 'PLC1' survived 'PHOU'/'SPAS' chunk ordering and remapped/pruned critters
        MARAS_CHECK(loaded.household->IsChildChestIntroduced());
        MARAS_CHECK_EQ(loaded.household->GetPoorCounter(), 2);
        MARAS_CHECK(loaded.household->IsNameCallingUsedLast());
        const auto* liveChildState = loaded.household->FindChildState(childLive);
        MARAS_REQUIRE(liveChildState != nullptr);
        MARAS_CHECK_EQ(liveChildState->transientCritterId, remappedLiveCritter);

        const auto* orphanChildState = loaded.household->FindChildState(childDeadCritter);
        MARAS_REQUIRE(orphanChildState != nullptr);
        MARAS_CHECK(orphanChildState->transientCritterId.IsNull());

        // 'BNT1' preserved live actor with dynamic 0xFF XMarker while pruning dead/unregistered actors
        MARAS_REQUIRE_EQ(loaded.cooldowns->movedSceneActors.size(), std::size_t{ 1 });
        const auto movedIt = loaded.cooldowns->movedSceneActors.find(remappedLiveMovedNpc);
        MARAS_REQUIRE(movedIt != loaded.cooldowns->movedSceneActors.end());
        MARAS_CHECK_EQ(movedIt->second, dynSceneMarker);

        MARAS_REQUIRE_EQ(loaded.cooldowns->skyrimNetMutedActors.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(loaded.cooldowns->skyrimNetMutedActors[0], remappedLiveMovedNpc);
    }

    MARAS_TEST(CoSaveMigrations, BoundarySafetyTruncationOverflowAndInvalidMagic) {
        auto writeLe32 = [](std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t val) {
            out[offset + 0] = static_cast<std::uint8_t>(val & 0xFFu);
            out[offset + 1] = static_cast<std::uint8_t>((val >> 8) & 0xFFu);
            out[offset + 2] = static_cast<std::uint8_t>((val >> 16) & 0xFFu);
            out[offset + 3] = static_cast<std::uint8_t>((val >> 24) & 0xFFu);
        };
        auto findTagOffset = [](const std::vector<std::uint8_t>& buf, std::uint32_t tag) -> std::optional<std::size_t> {
            std::array<std::uint8_t, 4> needle{
                static_cast<std::uint8_t>(tag & 0xFFu),
                static_cast<std::uint8_t>((tag >> 8) & 0xFFu),
                static_cast<std::uint8_t>((tag >> 16) & 0xFFu),
                static_cast<std::uint8_t>((tag >> 24) & 0xFFu),
            };
            const auto it = std::search(buf.begin(), buf.end(), needle.begin(), needle.end());
            if (it == buf.end()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(std::distance(buf.begin(), it));
        };
        auto seedTarget = []() {
            WorldState ws{};
            (void)ws.household.Write().RegisterHouse(HouseId{ 0x00090001u }, RefId{ 0x00090002u }, true);
            ws.household.Write().SetPoorCounter(7);
            ws.affection.Write().SetPermanent(ActorId{ 0x00090010u }, 88);
            ActorProfileRecord p{};
            p.refId = RefId{ 0x00090020u };
            p.baseId = BaseId{ 0x00090021u };
            p.isRegisteredAdult = true;
            ws.actors.Write().Upsert(p);
            DeceasedActorRecord d{};
            d.baseId = BaseId{ 0x00090031u };
            d.refId = RefId{ 0x00090032u };
            d.name = "KeepDead";
            ws.deceased.Write().UpsertRecord(std::move(d));
            ws.cooldowns.Write().playerKiller = true;
            ws.cooldowns.Write().savedBanterCooldownHours = 12.0f;
            ws.settings.Write().enablePlayerPolygamy = true;
            return ws;
        };
        auto verifyFailsAtomically = [&](const std::vector<CoSaveRecordChunk>& chunks) {
            WorldState target = seedTarget();
            const WorldState expected = target;
            IdentityFormIdResolver resolver{};
            InMemoryCoSaveReader reader(chunks);
            const CoSaveLoadReport rep = LoadWorldState(target, reader, resolver);
            MARAS_CHECK(!rep.allChunksSucceeded);
            MARAS_CHECK_EQ(rep.loadedChunkCount, 0u);
            MARAS_CHECK_EQ(rep.failedChunkCount, 1u);
            MARAS_CHECK(target == expected);
        };

        // 1. 0-byte empty payloads across mandatory/versioned chunks fail cleanly with rollback
        for (const std::uint32_t chunkType : {
                 CoSaveConstants::kChunkNPCR,
                 CoSaveConstants::kChunkAFCT,
                 CoSaveConstants::kChunkPLCH,
                 CoSaveConstants::kChunkMKMK,
                 CoSaveConstants::kChunkDCSD,
                 CoSaveConstants::kChunkBNTR,
                 CoSaveConstants::kChunkACTR,
                 CoSaveConstants::kChunkCNFG,
             }) {
            verifyFailsAtomically({ CoSaveRecordChunk{ chunkType, CoSaveConstants::kDataVersion, {} } });
        }

        // 2. Build valid WorldState containing all 5 optional extensions ('FRT1', 'DCS1', 'REL1', 'PLC1', 'BNT1')
        WorldState fullSrc{};
        const ActorId liveNpc{ 0x00081001u };
        const ActorId deadPartner{ 0x00081002u };
        const ChildId bornChild{ 0xFF700801u };
        {
            ActorProfileRecord p{};
            p.refId = liveNpc.AsRefId();
            p.baseId = BaseId{ 0x00081011u };
            p.socialClass = SocialClass::Working;
            p.isRegisteredAdult = true;
            fullSrc.actors.Write().Upsert(p);

            ChildRecord c{};
            c.childId = bornChild;
            c.bioMother = liveNpc;
            c.bioFather = ActorId::Player();
            c.stage = ChildStage::Infant;
            c.name = "BatchBaby";
            c.skyrimNetUuid = 0x0102030405060708ULL;
            fullSrc.lineage.Write().UpsertChild(c);

            ActiveDeliveryBatch batch{};
            batch.laborTotal = 2;
            batch.laborStartGameDay = 12.0f;
            batch.bornChildIds = { bornChild };
            fullSrc.lineage.Write().UpsertActiveDeliveryBatch(liveNpc, batch);

            DeceasedActorRecord dead{};
            dead.baseId = BaseId{ 0x00081021u };
            dead.refId = RefId{ 0x00081022u };
            dead.name = "DeadSecret";
            DeceasedPartnerEntry pe{};
            pe.partnerBaseId = BaseId{ 0x00081023u };
            pe.partnerRefId = deadPartner.AsRefId();
            pe.partnerName = "SecretLover";
            pe.statusAtDeath = RelationshipStatus::Lover;
            pe.isSecretAffair = true;
            dead.partnersAtDeath.push_back(std::move(pe));
            fullSrc.deceased.Write().UpsertRecord(std::move(dead));

            fullSrc.household.Write().SetChildChestIntroduced(true);
            fullSrc.household.Write().SetPoorCounter(3);
            fullSrc.household.Write().SetNameCallingUsedLast(true);
            fullSrc.cooldowns.Write().playerKiller = true;
            fullSrc.cooldowns.Write().skyrimNetMutedActors = { liveNpc };
        }
        InMemoryCoSaveWriter fullWriter{};
        MARAS_REQUIRE(SaveWorldState(fullSrc, fullWriter));

        // 3. Unknown 'XXXX' trailing extension magic and 1..3 byte extension truncations fail & roll back
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 6> extPairs{ {
            { CoSaveConstants::kChunkNPCR, CoSaveConstants::kExtFertilityBatchFRT1 },
            { CoSaveConstants::kChunkNPCR, CoSaveConstants::kExtChildSkyrimNetUuidSNU1 },
            { CoSaveConstants::kChunkDCSD, CoSaveConstants::kExtDeceasedSecrecyDCS1 },
            { CoSaveConstants::kChunkMKMK, CoSaveConstants::kExtRelationshipStateREL1 },
            { CoSaveConstants::kChunkPLCH, CoSaveConstants::kExtPlayerChildrenStatePLC1 },
            { CoSaveConstants::kChunkBNTR, CoSaveConstants::kExtBanterSceneStateBNT1 },
        } };
        const std::uint32_t badMagic = MakeFourCC('X', 'X', 'X', 'X');
        for (const auto& [chunkType, extTag] : extPairs) {
            bool tested = false;
            for (const auto& rec : fullWriter.Records()) {
                if (rec.type != chunkType) {
                    continue;
                }
                const auto off = findTagOffset(rec.payload, extTag);
                MARAS_REQUIRE(off.has_value());

                CoSaveRecordChunk corruptedChunk = rec;
                writeLe32(corruptedChunk.payload, *off, badMagic);
                verifyFailsAtomically({ corruptedChunk });

                for (const std::size_t extra : { std::size_t{ 2 }, std::size_t{ 6 } }) {
                    if (rec.payload.size() > *off + extra) {
                        CoSaveRecordChunk truncatedChunk = rec;
                        truncatedChunk.payload.resize(*off + extra);
                        verifyFailsAtomically({ truncatedChunk });
                    }
                }
                tested = true;
            }
            MARAS_CHECK(tested);
        }

        // 4. Bounds overflow rejection (kMaxSerializedEntries + 1 = 100001u, kMaxTenants + 1 = 10001u)
        {
            const std::uint32_t kOverEntries = 100001u;
            const std::uint32_t kOverTenants = 10001u;

            {
                InMemoryCoSaveWriter w{};
                MARAS_REQUIRE(w.OpenRecord(CoSaveConstants::kChunkAFCT, CoSaveConstants::kDataVersion));
                MARAS_REQUIRE(w.WritePod(kOverEntries));
                verifyFailsAtomically(w.Records());
            }
            {
                InMemoryCoSaveWriter w{};
                MARAS_REQUIRE(w.OpenRecord(CoSaveConstants::kChunkBNTR, CoSaveConstants::kDataVersion));
                const float cdHours = 24.0f;
                MARAS_REQUIRE(w.WritePod(cdHours));
                MARAS_REQUIRE(w.WritePod(kOverEntries));
                verifyFailsAtomically(w.Records());
            }
            {
                InMemoryCoSaveWriter w{};
                MARAS_REQUIRE(w.OpenRecord(CoSaveConstants::kChunkPLCH, CoSaveConstants::kDataVersion));
                const bool chestIntro = false;
                MARAS_REQUIRE(w.WritePod(chestIntro));
                MARAS_REQUIRE(w.WritePod(kOverTenants));
                verifyFailsAtomically(w.Records());
            }
            {
                InMemoryCoSaveWriter w{};
                MARAS_REQUIRE(w.OpenRecord(CoSaveConstants::kChunkCFSN, CoSaveConstants::kDataVersion));
                MARAS_REQUIRE(w.WritePod(kOverTenants));
                verifyFailsAtomically(w.Records());
            }
            {
                InMemoryCoSaveWriter w{};
                MARAS_REQUIRE(w.OpenRecord(CoSaveConstants::kChunkACTR, CoSaveConstants::kDataVersion));
                MARAS_REQUIRE(w.WritePod(CoSaveConstants::kMagicNumber));
                MARAS_REQUIRE(w.WritePod(kOverTenants));
                verifyFailsAtomically(w.Records());
            }
        }
    }

    MARAS_TEST(CoSaveMigrations, AffectionOrphanPruningAndWidowSceneMarkerReconciliation) {
        const ActorId savedLive{ 0x02006001u };
        const ActorId remappedLive{ 0x05006001u };
        const ActorId savedOrphan{ 0x02006002u };
        const ActorId deadSpouse{ 0x00082001u };
        const ActorId deadSceneNpc{ 0x00082002u };
        const CellId sharedCell{ 0x00083001u };
        const RefId dynMarker{ 0xFF00D111u };

        MockFormIdResolver resolver{};
        resolver.remapTable[savedLive.raw()] = remappedLive.raw();
        resolver.unresolvedRawIds.insert(savedOrphan.raw());
        resolver.baseIds[remappedLive] = BaseId{ 0x05006011u };
        resolver.baseIds[deadSpouse] = BaseId{ 0x00082011u };
        resolver.baseIds[deadSceneNpc] = BaseId{ 0x00082012u };
        resolver.names[deadSpouse] = "DeadSpouse";
        resolver.names[deadSceneNpc] = "DeadSceneNpc";
        resolver.deadOrInvalidActors.insert(deadSpouse);
        resolver.deadOrInvalidActors.insert(deadSceneNpc);

        WorldState src{};
        DefaultActorMetadataProvider saveMeta{};
        for (ActorId id : { savedLive, deadSpouse, deadSceneNpc }) {
            ActorProfileRecord p{};
            p.refId = id.AsRefId();
            p.baseId = BaseId{ id.raw() + 0x10u };
            p.isRegisteredAdult = true;
            src.actors.Write().Upsert(p);
        }
        (void)src.PromoteToMarried(ActorId::Player(), deadSpouse, 1.0f, ActorId::Player(), saveMeta);

        SharedCellRecord cellRec{};
        cellRec.sharedWithPlayer = true;
        cellRec.originalPublicRecorded = true;
        cellRec.originalPublicState = false;
        cellRec.sharingSpouses.insert(deadSpouse);
        src.household.Write().UpsertSharedCell(sharedCell, cellRec);

        src.cooldowns.Write().movedSceneActors[deadSceneNpc] = dynMarker;

        src.affection.Write().SetPermanent(savedLive, 77);
        src.affection.Write().SetLastInteractionDay(savedLive, 14.5f);
        src.affection.Write().SetDailyAccumulation(savedLive, "gift", 4.0f);

        src.affection.Write().SetPermanent(savedOrphan, 66);
        src.affection.Write().SetLastInteractionDay(savedOrphan, 9.0f);
        src.affection.Write().SetDailyAccumulation(savedOrphan, "gift", 3.0f);

        InMemoryCoSaveWriter w{};
        MARAS_REQUIRE(SaveWorldState(src, w));

        WorldState loaded{};
        InMemoryCoSaveReader reader(w.Records());
        const CoSaveLoadReport rep = LoadWorldState(loaded, reader, resolver);
        MARAS_REQUIRE(rep.allChunksSucceeded);

        // AffectionTable preserves live remapped actor and prunes unresolved/dead actors across all 3 maps
        MARAS_CHECK(loaded.affection->HasPermanent(remappedLive));
        MARAS_CHECK_EQ(loaded.affection->GetPermanent(remappedLive), 77);
        MARAS_REQUIRE(loaded.affection->GetLastInteractionDay(remappedLive).has_value());
        MARAS_CHECK_EQ(loaded.affection->AllDaily().size(), std::size_t{ 1 });
        MARAS_CHECK(loaded.affection->AllDaily().contains(remappedLive));
        MARAS_CHECK(!loaded.affection->HasPermanent(savedOrphan));
        MARAS_CHECK(!loaded.affection->GetLastInteractionDay(savedOrphan).has_value());

        // 'BNT1' dead actor returns retired scene marker; widowed spouse preserves SharedCellRecord
        MARAS_REQUIRE_EQ(rep.retiredSceneMarkers.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(rep.retiredSceneMarkers[0], dynMarker);
        MARAS_CHECK(loaded.cooldowns->movedSceneActors.empty());
        MARAS_CHECK(loaded.household->IsCellSharedWithPlayer(sharedCell));
    }

    MARAS_TEST(CoSaveMigrations, SNU1ChildSkyrimNetUuidExtensionRoundTripAndOmission) {
        auto findNpcrPayload = [](const std::vector<CoSaveRecordChunk>& recs) -> std::vector<std::uint8_t> {
            for (const auto& r : recs) {
                if (r.type == CoSaveConstants::kChunkNPCR) {
                    return r.payload;
                }
            }
            return {};
        };
        auto payloadContainsTag = [](const std::vector<std::uint8_t>& bytes, std::uint32_t tag) -> bool {
            std::array<std::uint8_t, sizeof(std::uint32_t)> needle{};
            std::memcpy(needle.data(), &tag, sizeof(tag));
            return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
        };

        const ActorId liveMother{ 0x02002010u };
        const ActorId remappedMother{ 0x05002010u };
        const ChildId trainingSynthChild{ 0xFF700101u };
        const ChildId liveDynamicChild{ 0xFF800101u };
        const ChildId savedPlacedChild{ 0x02001050u };
        const ChildId remappedPlacedChild{ 0x05001050u };
        const ChildId orphanedDynChild{ 0xFF800999u };

        // 1. Omission: when all children have skyrimNetUuid == 0, 'SNU1' is omitted from 'NPCR'
        {
            WorldState ws{};
            ChildRecord c{};
            c.childId = trainingSynthChild;
            c.stage = ChildStage::Training;
            c.name = "NoUuidChild";
            ws.lineage.Write().UpsertChild(c);

            InMemoryCoSaveWriter w{};
            MARAS_REQUIRE(SaveWorldState(ws, w));
            const auto npcr = findNpcrPayload(w.Records());
            MARAS_REQUIRE(!npcr.empty());
            MARAS_CHECK(!payloadContainsTag(npcr, CoSaveConstants::kExtChildSkyrimNetUuidSNU1));
        }

        auto runRoundTrip = [&](bool withFertilityBatch) {
            WorldState src{};
            ActorProfileRecord mom{};
            mom.refId = liveMother.AsRefId();
            mom.baseId = BaseId{ 0x02002011u };
            mom.isRegisteredAdult = true;
            src.actors.Write().Upsert(mom);

            ChildRecord synRec{};
            synRec.childId = trainingSynthChild;
            synRec.stage = ChildStage::Training;
            synRec.name = "SynthTrainee";
            synRec.bioMother = liveMother;
            synRec.bioFather = ActorId::Player();
            synRec.skyrimNetUuid = 0xAA00000000000001ULL;
            src.lineage.Write().UpsertChild(synRec);

            ChildRecord dynRec{};
            dynRec.childId = liveDynamicChild;
            dynRec.stage = ChildStage::Adult;
            dynRec.name = "LiveDynAdult";
            dynRec.bioMother = liveMother;
            dynRec.bioFather = ActorId::Player();
            dynRec.skyrimNetUuid = 0xBB00000000000002ULL;
            src.lineage.Write().UpsertChild(dynRec);

            ChildRecord placedRec{};
            placedRec.childId = savedPlacedChild;
            placedRec.stage = ChildStage::Child;
            placedRec.name = "PlacedChild";
            placedRec.guardians.Assign(ActorId::Player(), liveMother);
            placedRec.skyrimNetUuid = 0xCC00000000000003ULL;
            src.lineage.Write().UpsertChild(placedRec);

            ChildRecord orphanRec{};
            orphanRec.childId = orphanedDynChild;
            orphanRec.stage = ChildStage::Child;
            orphanRec.name = "OrphanDynChild";
            orphanRec.skyrimNetUuid = 0xDD00000000000004ULL;
            src.lineage.Write().UpsertChild(orphanRec);

            if (withFertilityBatch) {
                ActiveDeliveryBatch batch{};
                batch.laborTotal = 1;
                batch.laborStartGameDay = 8.0f;
                batch.bornChildIds = { trainingSynthChild };
                src.lineage.Write().UpsertActiveDeliveryBatch(liveMother, batch);
            }

            InMemoryCoSaveWriter w{};
            MARAS_REQUIRE(SaveWorldState(src, w));
            const auto npcr = findNpcrPayload(w.Records());
            MARAS_REQUIRE(!npcr.empty());
            MARAS_CHECK(payloadContainsTag(npcr, CoSaveConstants::kExtChildSkyrimNetUuidSNU1));
            MARAS_CHECK_EQ(payloadContainsTag(npcr, CoSaveConstants::kExtFertilityBatchFRT1), withFertilityBatch);

            MockFormIdResolver resolver{};
            resolver.remapTable[liveMother.raw()] = remappedMother.raw();
            resolver.remapTable[savedPlacedChild.raw()] = remappedPlacedChild.raw();
            resolver.baseIds[remappedMother] = BaseId{ 0x05002011u };
            resolver.missingDynamicRefs.insert(orphanedDynChild.AsActorId().AsRefId());

            WorldState loaded{};
            InMemoryCoSaveReader reader(w.Records());
            const CoSaveLoadReport rep = LoadWorldState(loaded, reader, resolver);
            MARAS_REQUIRE(rep.allChunksSucceeded);

            const auto* loadedSyn = loaded.lineage->FindChild(trainingSynthChild);
            MARAS_REQUIRE(loadedSyn != nullptr);
            MARAS_CHECK_EQ(loadedSyn->skyrimNetUuid, 0xAA00000000000001ULL);

            const auto* loadedDyn = loaded.lineage->FindChild(liveDynamicChild);
            MARAS_REQUIRE(loadedDyn != nullptr);
            MARAS_CHECK_EQ(loadedDyn->skyrimNetUuid, 0xBB00000000000002ULL);

            const auto* loadedPlaced = loaded.lineage->FindChild(remappedPlacedChild);
            MARAS_REQUIRE(loadedPlaced != nullptr);
            MARAS_CHECK_EQ(loadedPlaced->skyrimNetUuid, 0xCC00000000000003ULL);

            MARAS_CHECK(loaded.lineage->FindChild(orphanedDynChild) == nullptr);
        };

        // 2. Round-trip both WITHOUT 'FRT1' and WITH 'FRT1'
        runRoundTrip(false);
        runRoundTrip(true);
    }

}  // namespace MARAS::Tests
