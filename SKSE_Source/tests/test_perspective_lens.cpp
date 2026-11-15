#include "TestHarness.h"

#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

    using MARAS::Foundation::ActorId;
    using MARAS::Foundation::AffairId;
    using MARAS::Foundation::BaseId;
    using MARAS::Foundation::CanonicalPairKey;
    using MARAS::Foundation::CellId;
    using MARAS::Foundation::ChildId;
    using MARAS::Foundation::FormRawId;
    using MARAS::Foundation::LocationId;
    using MARAS::Foundation::RefId;
    using MARAS::Foundation::SyntheticChildId;

    using MARAS::State::ActorProfileRecord;
    using MARAS::State::ActorSex;
    using MARAS::State::AffairStage;
    using MARAS::State::AffairStatus;
    using MARAS::State::AffairType;
    using MARAS::State::ChildRecord;
    using MARAS::State::ChildStage;
    using MARAS::State::DeceasedActorRecord;
    using MARAS::State::LoadedActorFlags;
    using MARAS::State::LoadedActorSample;
    using MARAS::State::PartnerIntentType;
    using MARAS::State::PaternitySecretState;
    using MARAS::State::PendingPartnerIntentRecord;
    using MARAS::State::RelationshipEdgeRecord;
    using MARAS::State::RelationshipStatus;
    using MARAS::State::SpatialActorSnapshot;
    using MARAS::State::SpatialSnapshotStore;
    using MARAS::State::StatusTransitionEntry;
    using MARAS::State::WorldState;

    using MARAS::Projection::ExpectingParentRole;
    using MARAS::Projection::IWerewolfStateProvider;
    using MARAS::Projection::PerceivedChildLineage;
    using MARAS::Projection::PerceivedExpectingPregnancy;
    using MARAS::Projection::PerceivedRelationshipEdge;
    using MARAS::Projection::PerceivedSiblingEntry;
    using MARAS::Projection::PerspectiveView;
    using MARAS::Projection::SiblingRelationKind;
    using MARAS::Projection::ViewerMode;

    [[nodiscard]] bool ContainsActor(const std::vector<ActorId>& list, ActorId id) {
        return std::find(list.begin(), list.end(), id) != list.end();
    }

    [[nodiscard]] bool ContainsRef(const std::vector<RefId>& list, RefId id) {
        return std::find(list.begin(), list.end(), id) != list.end();
    }

    [[nodiscard]] bool ContainsChild(const std::vector<PerceivedChildLineage>& list, ChildId id) {
        return std::any_of(list.begin(), list.end(), [id](const PerceivedChildLineage& c) {
            return c.childId == id;
        });
    }

    [[nodiscard]] const PerceivedSiblingEntry* FindSibling(
        const std::vector<PerceivedSiblingEntry>& list,
        ChildId id) {
        for (const auto& entry : list) {
            if (entry.childId == id) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool IsValidUtf8(std::string_view s) noexcept {
        std::size_t i = 0;
        while (i < s.size()) {
            const auto b0 = static_cast<unsigned char>(s[i]);
            if ((b0 & 0x80u) == 0u) {
                ++i;
            } else if ((b0 & 0xE0u) == 0xC0u) {
                if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0u) != 0x80u) {
                    return false;
                }
                i += 2;
            } else if ((b0 & 0xF0u) == 0xE0u) {
                if (i + 2 >= s.size() ||
                    (static_cast<unsigned char>(s[i + 1]) & 0xC0u) != 0x80u ||
                    (static_cast<unsigned char>(s[i + 2]) & 0xC0u) != 0x80u) {
                    return false;
                }
                i += 3;
            } else if ((b0 & 0xF8u) == 0xF0u) {
                if (i + 3 >= s.size() ||
                    (static_cast<unsigned char>(s[i + 1]) & 0xC0u) != 0x80u ||
                    (static_cast<unsigned char>(s[i + 2]) & 0xC0u) != 0x80u ||
                    (static_cast<unsigned char>(s[i + 3]) & 0xC0u) != 0x80u) {
                    return false;
                }
                i += 4;
            } else {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] LoadedActorSample MakeBasicSample(
        RefId refId,
        float x,
        float y,
        float z,
        std::uint16_t extraFlags = LoadedActorFlags::None,
        std::string_view name = "NPC") {
        LoadedActorSample s{};
        s.refId = refId;
        s.baseId = BaseId{ refId.raw() };
        s.x = x;
        s.y = y;
        s.z = z;
        s.sex = ActorSex::Female;
        s.flags = static_cast<std::uint16_t>(
            LoadedActorFlags::HasCurrentProcess |
            LoadedActorFlags::Is3DLoaded |
            LoadedActorFlags::HasActorTypeNpc |
            extraFlags);
        s.SetDisplayName(name);
        return s;
    }

}  // namespace

// =============================================================================
// SpatialActorSnapshot & SpatialSnapshotStore Tests
// =============================================================================

MARAS_TEST(SpatialSnapshot, SortedLookupDedupeAndUtf8Truncation) {
    SpatialActorSnapshot snap{};
    snap.Reserve(8);

    const RefId idA{ 0x00010001u };
    const RefId idB{ 0x00020002u };
    const RefId idC{ 0x00030003u };

    // Insert out of order + duplicate idB (second entry must not break sorted search or dedupe)
    snap.AddSample(MakeBasicSample(idC, 10.0f, 0.0f, 0.0f, LoadedActorFlags::None, "Gamma"));
    snap.AddSample(MakeBasicSample(idB, 20.0f, 0.0f, 0.0f, LoadedActorFlags::None, "BetaFirst"));
    snap.AddSample(MakeBasicSample(RefId{}, 0.0f, 0.0f, 0.0f, LoadedActorFlags::None, "NullRef"));
    snap.AddSample(MakeBasicSample(idA, 30.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer, "Alpha"));
    snap.AddSample(MakeBasicSample(idB, 99.0f, 0.0f, 0.0f, LoadedActorFlags::None, "BetaDuplicate"));

    snap.FinalizeSortAndDedupe();
    snap.SetMetadata(12.5f, 7u);

    MARAS_REQUIRE_EQ(snap.Size(), std::size_t{ 3 });
    MARAS_CHECK_EQ(snap.CurrentGameDay(), 12.5f);
    MARAS_CHECK_EQ(snap.Sequence(), std::uint64_t{ 7 });

    const auto& all = snap.AllSamples();
    MARAS_CHECK_EQ(all[0].refId, idA);
    MARAS_CHECK_EQ(all[1].refId, idB);
    MARAS_CHECK_EQ(all[2].refId, idC);

    const auto* foundA = snap.Find(idA);
    const auto* foundB = snap.Find(idB);
    const auto* foundC = snap.Find(idC);
    MARAS_REQUIRE(foundA != nullptr && foundB != nullptr && foundC != nullptr);
    MARAS_CHECK(foundA->IsPlayer());
    MARAS_CHECK_EQ(foundB->DisplayNameView(), std::string_view{ "BetaFirst" });
    MARAS_CHECK_EQ(foundC->DisplayNameView(), std::string_view{ "Gamma" });
    MARAS_CHECK(snap.Find(RefId{ 0x00099999u }) == nullptr);

    // UTF-8 safe bounded truncation (buffer holds max 63 payload bytes + NUL):
    // 1) ASCII > 63 bytes
    LoadedActorSample asciiTrunc{};
    asciiTrunc.SetDisplayName(std::string(80, 'A'));
    MARAS_CHECK_EQ(asciiTrunc.DisplayNameView().size(), std::size_t{ 63 });
    MARAS_CHECK_EQ(asciiTrunc.displayName[63], '\0');

    // 2) 2-byte UTF-8 ('é' = C3 A9) straddling byte 63 (62 'a' + 2-byte codepoint -> 62 bytes)
    LoadedActorSample twoByteSplit{};
    std::string s2 = std::string(62, 'a') + "\xC3\xA9";
    twoByteSplit.SetDisplayName(s2);
    MARAS_CHECK_EQ(twoByteSplit.DisplayNameView().size(), std::size_t{ 62 });
    MARAS_CHECK(IsValidUtf8(twoByteSplit.DisplayNameView()));
    MARAS_CHECK_EQ(twoByteSplit.displayName[62], '\0');

    // 3) 3-byte UTF-8 ('—' = E2 80 94) straddling byte 63 (61 'a' + 3-byte codepoint -> 61 bytes)
    LoadedActorSample threeByteSplit{};
    std::string s3 = std::string(61, 'b') + "\xE2\x80\x94" + "tail";
    threeByteSplit.SetDisplayName(s3);
    MARAS_CHECK_EQ(threeByteSplit.DisplayNameView().size(), std::size_t{ 61 });
    MARAS_CHECK(IsValidUtf8(threeByteSplit.DisplayNameView()));

    // 4) 4-byte UTF-8 emoji straddling byte 63 (60 'c' + 4-byte emoji -> 60 bytes)
    LoadedActorSample fourByteSplit{};
    std::string s4 = std::string(60, 'c') + "\xF0\x9F\x98\x80" + "end";
    fourByteSplit.SetDisplayName(s4);
    MARAS_CHECK_EQ(fourByteSplit.DisplayNameView().size(), std::size_t{ 60 });
    MARAS_CHECK(IsValidUtf8(fourByteSplit.DisplayNameView()));

    // 5) 4-byte UTF-8 fitting cleanly right up to byte 63 (59 'd' + 4-byte emoji = 63 bytes -> kept)
    LoadedActorSample fourByteFit{};
    std::string s5 = std::string(59, 'd') + "\xF0\x9F\x98\x80" + "overflow";
    fourByteFit.SetDisplayName(s5);
    MARAS_CHECK_EQ(fourByteFit.DisplayNameView().size(), std::size_t{ 63 });
    MARAS_CHECK(IsValidUtf8(fourByteFit.DisplayNameView()));
}

MARAS_TEST(SpatialSnapshot, NearbyCandidatesAndTeammatesParity) {
    SpatialActorSnapshot snap{};
    const RefId center = RefId::Player();
    const RefId validNear{ 0x00010001u };
    const RefId exactBoundary{ 0x00010002u };  // distance == radius (2000): excluded by strict `< radiusSq`
    const RefId deadActor{ 0x00010003u };
    const RefId combatActor{ 0x00010004u };
    const RefId sceneActor{ 0x00010005u };
    const RefId childActor{ 0x00010006u };
    const RefId mannequinActor{ 0x00010007u };
    const RefId creatureActor{ 0x00010008u };
    const RefId deletedActor{ 0x00010009u };
    const RefId followerReg{ 0x0001000Au };
    const RefId followerUnreg{ 0x0001000Bu };

    snap.AddSample(MakeBasicSample(center, 0.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer, "Player"));
    snap.AddSample(MakeBasicSample(validNear, 500.0f, 0.0f, 0.0f, LoadedActorFlags::IsRegisteredAdult, "Lydia"));
    snap.AddSample(MakeBasicSample(exactBoundary, 2000.0f, 0.0f, 0.0f));
    snap.AddSample(MakeBasicSample(deadActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsDead));
    snap.AddSample(MakeBasicSample(combatActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsInCombat));
    snap.AddSample(MakeBasicSample(sceneActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsInScene));
    snap.AddSample(MakeBasicSample(childActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsChild));
    snap.AddSample(MakeBasicSample(mannequinActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsMannequin));

    auto creature = MakeBasicSample(creatureActor, 100.0f, 0.0f, 0.0f);
    creature.SetFlag(LoadedActorFlags::HasActorTypeNpc, false);
    snap.AddSample(creature);

    snap.AddSample(MakeBasicSample(deletedActor, 100.0f, 0.0f, 0.0f, LoadedActorFlags::IsDeleted | LoadedActorFlags::IsPlayerTeammate | LoadedActorFlags::IsRegisteredAdult));
    snap.AddSample(MakeBasicSample(followerReg, 250.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayerTeammate | LoadedActorFlags::IsRegisteredAdult, "Serana"));
    snap.AddSample(MakeBasicSample(followerUnreg, 300.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayerTeammate, "TempFollower"));
    snap.FinalizeSortAndDedupe();

    const auto nearby = snap.GetNearbyCandidates(center, 2000.0f);
    MARAS_CHECK(ContainsRef(nearby, validNear));
    MARAS_CHECK(ContainsRef(nearby, followerReg));
    MARAS_CHECK(ContainsRef(nearby, followerUnreg));
    MARAS_CHECK(!ContainsRef(nearby, center));
    MARAS_CHECK(!ContainsRef(nearby, exactBoundary));
    MARAS_CHECK(!ContainsRef(nearby, deadActor));
    MARAS_CHECK(!ContainsRef(nearby, combatActor));
    MARAS_CHECK(!ContainsRef(nearby, sceneActor));
    MARAS_CHECK(!ContainsRef(nearby, childActor));
    MARAS_CHECK(!ContainsRef(nearby, mannequinActor));
    MARAS_CHECK(!ContainsRef(nearby, creatureActor));
    MARAS_CHECK(!ContainsRef(nearby, deletedActor));

    MARAS_CHECK(snap.IsWithinRadius(center, exactBoundary, 2000.0f));  // IsWithinRadius is inclusive `<= radius`
    MARAS_CHECK(!snap.IsWithinRadius(center, deletedActor, 2000.0f));

    const auto teammatesFromFlag = snap.GetPlayerTeammates();
    MARAS_CHECK_EQ(teammatesFromFlag.size(), std::size_t{ 1 });
    MARAS_CHECK(ContainsRef(teammatesFromFlag, followerReg));
    MARAS_CHECK(!ContainsRef(teammatesFromFlag, followerUnreg));

    // Also test filtering against an authoritative ActorProfileTable
    MARAS::State::ActorProfileTable regTable{};
    ActorProfileRecord rec{};
    rec.refId = followerUnreg;
    rec.isRegisteredAdult = true;
    regTable.Upsert(rec);
    const auto teammatesFromTable = snap.GetPlayerTeammates(&regTable);
    MARAS_CHECK_EQ(teammatesFromTable.size(), std::size_t{ 1 });
    MARAS_CHECK(ContainsRef(teammatesFromTable, followerUnreg));
}

MARAS_TEST(SpatialSnapshot, ZeroAllocBufferRecyclingAndConcurrency) {
    SpatialSnapshotStore store{};
    MARAS_REQUIRE_EQ(store.PoolSize(), SpatialSnapshotStore::kInitialPoolSlots);
    MARAS_CHECK(store.Snapshot()->Empty());

    const RefId npcA{ 0x0001A001u };
    const RefId npcB{ 0x0001A002u };

    auto snap1 = store.Publish(1.0f, [&](SpatialActorSnapshot& out) {
        out.AddSample(MakeBasicSample(RefId::Player(), 0.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer));
        out.AddSample(MakeBasicSample(npcA, 10.0f, 0.0f, 0.0f));
    });
    const LoadedActorSample* ptr1 = snap1->AllSamples().data();
    const std::size_t cap1 = snap1->Capacity();
    MARAS_CHECK(cap1 >= SpatialSnapshotStore::kDefaultSampleReserve);
    snap1.reset();

    auto snap2 = store.Publish(2.0f, [&](SpatialActorSnapshot& out) {
        out.AddSample(MakeBasicSample(RefId::Player(), 1.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer));
        out.AddSample(MakeBasicSample(npcB, 20.0f, 0.0f, 0.0f));
    });
    const LoadedActorSample* ptr2 = snap2->AllSamples().data();
    snap2.reset();

    // Tick 3 recycles pool[0] in place with identical heap backing pointer and reserved capacity
    auto snap3 = store.Publish(3.0f, [&](SpatialActorSnapshot& out) {
        out.AddSample(MakeBasicSample(RefId::Player(), 2.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer));
    });
    MARAS_CHECK_EQ(snap3->Sequence(), std::uint64_t{ 3 });
    MARAS_CHECK(snap3->AllSamples().data() == ptr1);
    MARAS_CHECK_EQ(snap3->Capacity(), cap1);

    // Pinning snap3 forces tick 4 onto another slot so snap3 remains immutable while snap4 recycles ptr2
    auto snap4 = store.Publish(4.0f, [&](SpatialActorSnapshot& out) {
        out.AddSample(MakeBasicSample(RefId::Player(), 3.0f, 0.0f, 0.0f, LoadedActorFlags::IsPlayer));
        out.AddSample(MakeBasicSample(npcA, 30.0f, 0.0f, 0.0f));
    });
    MARAS_CHECK(snap4->AllSamples().data() == ptr2);
    MARAS_CHECK_EQ(snap3->Size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(snap3->Sequence(), std::uint64_t{ 3 });
    MARAS_CHECK_EQ(snap4->Size(), std::size_t{ 2 });
    MARAS_CHECK_EQ(snap4->Sequence(), std::uint64_t{ 4 });
    MARAS_CHECK_EQ(store.PoolSize(), SpatialSnapshotStore::kInitialPoolSlots);

    snap3.reset();
    snap4.reset();

    // Concurrent 1-writer / 4-reader stress test
    std::atomic<bool> stop{ false };
    std::atomic<bool> sawBad{ false };
    std::atomic<std::size_t> reads{ 0 };
    std::vector<std::thread> readers{};
    readers.reserve(4);
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            std::uint64_t lastSeq = 0;
            while (!stop.load(std::memory_order_acquire)) {
                const auto s = store.Snapshot();
                if (!s) {
                    sawBad.store(true, std::memory_order_release);
                    break;
                }
                if (s->Sequence() < lastSeq) {
                    sawBad.store(true, std::memory_order_release);
                    break;
                }
                lastSeq = s->Sequence();
                if (!s->Empty() && s->Find(RefId::Player()) == nullptr) {
                    sawBad.store(true, std::memory_order_release);
                    break;
                }
                (void)s->GetNearbyCandidates(RefId::Player(), 500.0f);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (std::uint32_t i = 0; i < 200; ++i) {
        store.Publish(static_cast<float>(i), [&](SpatialActorSnapshot& out) {
            out.AddSample(MakeBasicSample(RefId::Player(), static_cast<float>(i), 0.0f, 0.0f, LoadedActorFlags::IsPlayer));
            out.AddSample(MakeBasicSample(npcA, static_cast<float>(i) + 50.0f, 0.0f, 0.0f));
        });
    }

    stop.store(true, std::memory_order_release);
    for (auto& t : readers) {
        t.join();
    }

    MARAS_CHECK(!sawBad.load(std::memory_order_acquire));
    MARAS_CHECK(reads.load(std::memory_order_relaxed) > 0);
    MARAS_CHECK_EQ(store.PoolSize(), SpatialSnapshotStore::kInitialPoolSlots);

    store.Reset();
    MARAS_CHECK(store.Snapshot()->Empty());
}

// =============================================================================
// PerspectiveView Paternity Matrix Tests (Cases A, B, C, D)
// =============================================================================

MARAS_TEST(PerspectiveLens, MarriedMotherAffairPregnancyAndBornChildMatrix) {
    const ActorId mother{ 0x00010010u };
    const ActorId husband{ 0x00010020u };
    const ActorId lover{ 0x00010030u };
    const ActorId bystander{ 0x00010040u };
    const ChildId unbornId{ SyntheticChildId{ 0xFF700001u } };
    const ChildId playerUnbornId{ SyntheticChildId{ 0xFF700002u } };
    const ChildId bornChildId{ 0xFF800100u };

    WorldState ws{};
    (void)ws.PromoteToMarried(mother, husband, 1.0f);

    // Case A: NPC married mother secretly pregnant by lover (pregnancy + paternity both secret)
    ChildRecord npcUnborn{};
    npcUnborn.childId = unbornId;
    npcUnborn.name = "SecretFetus";
    npcUnborn.stage = ChildStage::Unborn;
    npcUnborn.isAdopted = false;
    npcUnborn.isBiologicalChild = true;
    npcUnborn.bioMother = mother;
    npcUnborn.bioFather = lover;
    npcUnborn.presumedFather = husband;
    npcUnborn.conceptionTimestamp = 5.0f;
    npcUnborn.paternitySecret.isSecret = true;
    npcUnborn.paternitySecret.isPregnancySecretForPartner = true;
    npcUnborn.paternitySecret.isSecretForPartner = true;
    npcUnborn.paternitySecret.isSecretForBioFather = true;
    ws.lineage.Write().UpsertChild(npcUnborn);

    // Also test Player married mother with unannounced affair pregnancy for DashboardIntrigue parity
    (void)ws.PromoteToMarried(ActorId::Player(), bystander, 1.0f);
    ChildRecord playerUnborn{};
    playerUnborn.childId = playerUnbornId;
    playerUnborn.name = "PlayerFetus";
    playerUnborn.stage = ChildStage::Unborn;
    playerUnborn.isAdopted = false;
    playerUnborn.isBiologicalChild = true;
    playerUnborn.bioMother = ActorId::Player();
    playerUnborn.bioFather = lover;
    playerUnborn.presumedFather = bystander;
    playerUnborn.conceptionTimestamp = 5.0f;
    playerUnborn.paternitySecret.isSecret = true;
    playerUnborn.paternitySecret.isPregnancySecretForPartner = true;
    playerUnborn.paternitySecret.isSecretForPartner = true;
    playerUnborn.paternitySecret.isSecretForBioFather = true;
    ws.lineage.Write().UpsertChild(playerUnborn);

    auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    auto spatialSnap = std::make_shared<SpatialActorSnapshot>();

    // Phase A1: Unborn pregnancy secret -> only mother and Omniscient see npcUnborn
    const auto vMother = PerspectiveView::ForActor(worldSnap, spatialSnap, mother);
    const auto vHusband = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    const auto vLover = PerspectiveView::ForActor(worldSnap, spatialSnap, lover);
    const auto vBystander = PerspectiveView::ForActor(worldSnap, spatialSnap, bystander);
    const auto vTarget = PerspectiveView::ForDialogueTarget(worldSnap, spatialSnap);
    const auto vUiHidden = PerspectiveView::ForDashboard(worldSnap, spatialSnap, false);
    const auto vUiReveal = PerspectiveView::ForDashboard(worldSnap, spatialSnap, true);

    MARAS_CHECK(vMother.IsChildVisible(unbornId));
    MARAS_CHECK(vUiReveal.IsChildVisible(unbornId));
    MARAS_CHECK(!vHusband.IsChildVisible(unbornId));
    MARAS_CHECK(!vLover.IsChildVisible(unbornId));
    MARAS_CHECK(!vBystander.IsChildVisible(unbornId));
    MARAS_CHECK(!vTarget.IsChildVisible(unbornId));
    MARAS_CHECK(!vUiHidden.IsChildVisible(unbornId));

    // Player-mother unborn record is visible in Dashboard(hidden) BUT secret bioFather/paternitySecret are masked!
    MARAS_CHECK(vUiHidden.IsChildVisible(playerUnbornId));
    const auto uiPlayerPreg = vUiHidden.GetPerceivedChild(playerUnbornId);
    MARAS_REQUIRE(uiPlayerPreg.has_value());
    MARAS_CHECK_EQ(uiPlayerPreg->perceivedBioFather, bystander);
    MARAS_CHECK(!uiPlayerPreg->paternitySecret.has_value());
    MARAS_CHECK(!uiPlayerPreg->hasUndiscoveredPaternitySecret);

    // Expecting pregnancies while pregnancy is unannounced: neither Husband nor Lover sees expecting entry
    MARAS_CHECK(vHusband.GetExpectingPregnanciesFor(husband).empty());
    MARAS_CHECK(vLover.GetExpectingPregnanciesFor(lover).empty());

    // Phase A2: Pregnancy announced to Husband (isPregnancySecretForPartner = false), paternity still secret
    ws.lineage.Write().FindChildMutable(unbornId)->paternitySecret.isPregnancySecretForPartner = false;
    worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());

    const auto vHusbandAnnounced = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    const auto vLoverAnnounced = PerspectiveView::ForActor(worldSnap, spatialSnap, lover);
    const auto expHusband = vHusbandAnnounced.GetExpectingPregnanciesFor(husband);
    MARAS_REQUIRE_EQ(expHusband.size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(expHusband[0].mother, mother);
    MARAS_CHECK_EQ(expHusband[0].role, ExpectingParentRole::PresumedFatherUnaware);
    MARAS_CHECK_EQ(expHusband[0].perceivedBioFather, husband);
    // Unaware bio-father still sees ZERO expecting entries
    MARAS_CHECK(vLoverAnnounced.GetExpectingPregnanciesFor(lover).empty());

    // Case B: Child is born while both isSecretForPartner and isSecretForBioFather remain true
    ChildRecord born{};
    born.childId = bornChildId;
    born.name = "AelaChild";
    born.stage = ChildStage::Child;
    born.isAdopted = false;
    born.isBiologicalChild = true;
    born.guardians.Assign(mother, husband);
    born.bioMother = mother;
    born.bioFather = lover;
    born.presumedFather = husband;
    born.paternitySecret.isSecret = true;
    born.paternitySecret.isSecretForPartner = true;
    born.paternitySecret.isSecretForBioFather = true;
    born.paternitySecret.bioFatherName = "Lover";
    ws.lineage.Write().UpsertChild(born);

    worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());

    const auto bMother = PerspectiveView::ForActor(worldSnap, spatialSnap, mother);
    const auto bHusband = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    const auto bLover = PerspectiveView::ForActor(worldSnap, spatialSnap, lover);
    const auto bChild = PerspectiveView::ForActor(worldSnap, spatialSnap, bornChildId.AsActorId());
    const auto bBystander = PerspectiveView::ForActor(worldSnap, spatialSnap, bystander);
    const auto bTarget = PerspectiveView::ForDialogueTarget(worldSnap, spatialSnap);
    const auto bUiHidden = PerspectiveView::ForDashboard(worldSnap, spatialSnap, false);
    const auto bUiReveal = PerspectiveView::ForDashboard(worldSnap, spatialSnap, true);

    // Unmasked truth for mother and Omniscient UI
    for (const PerspectiveView* truthView : { &bMother, &bUiReveal }) {
        const auto c = truthView->GetPerceivedChild(bornChildId);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK_EQ(c->perceivedBioFather, lover);
        MARAS_CHECK_EQ(c->presumedFather, husband);
        MARAS_CHECK(c->hasUndiscoveredPaternitySecret);
        MARAS_CHECK(!c->hideSecretFromViewer);
        MARAS_REQUIRE(c->paternitySecret.has_value());
        MARAS_CHECK_EQ(c->paternitySecret->trueBioFather, lover);
        MARAS_CHECK(c->IsActorPerceivedBioParent(lover));
        MARAS_CHECK(c->IsActorPerceivedStepParent(husband));
        MARAS_CHECK(!c->IsActorPerceivedBioParent(husband));
    }

    // Masked view for deceived husband, child self-blindness, bystander, dialogue target, and Intrigue UI
    for (const PerspectiveView* maskedView : { &bHusband, &bChild, &bBystander, &bTarget, &bUiHidden }) {
        const auto c = maskedView->GetPerceivedChild(bornChildId);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK_EQ(c->perceivedBioFather, husband);
        MARAS_CHECK(c->presumedFather.IsNull());
        MARAS_CHECK(!c->hasUndiscoveredPaternitySecret);
        MARAS_CHECK(c->hideSecretFromViewer);
        MARAS_CHECK(!c->paternitySecret.has_value());
        MARAS_CHECK(c->stepParents.empty());
        MARAS_CHECK(c->IsActorPerceivedBioParent(husband));
        MARAS_CHECK(!c->IsActorPerceivedStepParent(husband));
        MARAS_CHECK(!c->IsActorPerceivedBioParent(lover));
    }

    // Unaware bioFather never claims the secret child as his non-custodial child
    const auto loverNonCustodial = bLover.GetNonCustodialChildrenOf(lover);
    MARAS_CHECK(!ContainsChild(loverNonCustodial, bornChildId));
    const auto loverChildView = bLover.GetPerceivedChild(bornChildId);
    MARAS_REQUIRE(loverChildView.has_value());
    MARAS_CHECK(loverChildView->perceivedBioFather != lover);
}

MARAS_TEST(PerspectiveLens, SingleMotherSecretFromBioFatherAndPartialDiscovery) {
    const ActorId mother{ 0x00020010u };
    const ActorId husband{ 0x00020020u };
    const ActorId bioFather{ 0x00020030u };
    const ActorId bystander{ 0x00020040u };
    const ChildId singleChildId{ 0xFF800200u };
    const ChildId partialChildId{ 0xFF800201u };

    WorldState ws{};

    // Case C: Single mother (motherPartner == 0) hiding paternity from bioFather (Audit Leak #2)
    ChildRecord singleChild{};
    singleChild.childId = singleChildId;
    singleChild.name = "SingleSecret";
    singleChild.stage = ChildStage::Child;
    singleChild.isAdopted = false;
    singleChild.isBiologicalChild = true;
    singleChild.guardians.Assign(mother, ActorId{});
    singleChild.bioMother = mother;
    singleChild.bioFather = bioFather;
    singleChild.paternitySecret.isSecret = true;
    singleChild.paternitySecret.isSecretForPartner = false;
    singleChild.paternitySecret.isSecretForBioFather = true;
    singleChild.paternitySecret.bioFatherName = "BioDad";
    ws.lineage.Write().UpsertChild(singleChild);

    // Case D: Married mother where partner discovered/knows truth, but bioFather still unaware
    (void)ws.PromoteToMarried(mother, husband, 1.0f);
    ChildRecord partialChild{};
    partialChild.childId = partialChildId;
    partialChild.name = "HalfSecret";
    partialChild.stage = ChildStage::Child;
    partialChild.isAdopted = false;
    partialChild.isBiologicalChild = true;
    partialChild.guardians.Assign(mother, husband);
    partialChild.bioMother = mother;
    partialChild.bioFather = bioFather;
    partialChild.presumedFather = husband;
    partialChild.paternitySecret.isSecret = true;
    partialChild.paternitySecret.isSecretForPartner = true;
    partialChild.paternitySecret.discoveredByPartner = true;
    partialChild.paternitySecret.isSecretForBioFather = true;
    partialChild.paternitySecret.discoveredByBioFather = false;
    ws.lineage.Write().UpsertChild(partialChild);

    const auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    const auto spatialSnap = std::make_shared<SpatialActorSnapshot>();

    const auto vMother = PerspectiveView::ForActor(worldSnap, spatialSnap, mother);
    const auto vHusband = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    const auto vBioFather = PerspectiveView::ForActor(worldSnap, spatialSnap, bioFather);
    const auto vSingleChild = PerspectiveView::ForActor(worldSnap, spatialSnap, singleChildId.AsActorId());
    const auto vPartialChild = PerspectiveView::ForActor(worldSnap, spatialSnap, partialChildId.AsActorId());
    const auto vBystander = PerspectiveView::ForActor(worldSnap, spatialSnap, bystander);
    const auto vTarget = PerspectiveView::ForDialogueTarget(worldSnap, spatialSnap);
    const auto vUiHidden = PerspectiveView::ForDashboard(worldSnap, spatialSnap, false);
    const auto vUiReveal = PerspectiveView::ForDashboard(worldSnap, spatialSnap, true);

    // Case C checks
    MARAS_CHECK_EQ(vMother.GetPerceivedChild(singleChildId)->perceivedBioFather, bioFather);
    MARAS_CHECK_EQ(vUiReveal.GetPerceivedChild(singleChildId)->perceivedBioFather, bioFather);
    for (const PerspectiveView* hiddenView : { &vBioFather, &vSingleChild, &vBystander, &vTarget, &vUiHidden }) {
        const auto c = hiddenView->GetPerceivedChild(singleChildId);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK(c->perceivedBioFather.IsNull());
        MARAS_CHECK(!c->paternitySecret.has_value());
        MARAS_CHECK(!c->hasUndiscoveredPaternitySecret);
        MARAS_CHECK(!c->IsActorPerceivedBioParent(bioFather));
    }

    // Case D checks: Mother and Husband know truth; bioFather, child, bystander, DialogueTarget, Intrigue UI mask it
    MARAS_CHECK_EQ(vMother.GetPerceivedChild(partialChildId)->perceivedBioFather, bioFather);
    MARAS_CHECK_EQ(vHusband.GetPerceivedChild(partialChildId)->perceivedBioFather, bioFather);
    MARAS_CHECK_EQ(vUiReveal.GetPerceivedChild(partialChildId)->perceivedBioFather, bioFather);
    for (const PerspectiveView* hiddenView : { &vBioFather, &vPartialChild, &vBystander, &vTarget, &vUiHidden }) {
        const auto c = hiddenView->GetPerceivedChild(partialChildId);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK(c->perceivedBioFather != bioFather);
        MARAS_CHECK(c->perceivedBioFather.IsNull());
        MARAS_CHECK(!c->paternitySecret.has_value());
        MARAS_CHECK(!c->hasUndiscoveredPaternitySecret);
    }
    MARAS_CHECK(vBioFather.GetNonCustodialChildrenOf(bioFather).empty());
}

MARAS_TEST(PerspectiveLens, BeastbloodAndSiblingSecretMasking) {
    const ActorId mother{ 0x00030010u };
    const ActorId husband{ 0x00030020u };
    const ActorId werewolfLover{ 0x00030030u };
    const ActorId woman2{ 0x00030040u };
    const ActorId woman3{ 0x00030050u };
    const ActorId bystander{ 0x00030060u };

    const ChildId secretChild{ 0xFF800300u };
    const ChildId husbandHalfSib{ 0xFF800301u };
    const ChildId secretBioHalfSib{ 0xFF800302u };
    const ChildId unknownFatherChildA{ 0xFF800303u };
    const ChildId unknownFatherChildB{ 0xFF800304u };

    WorldState ws{};
    (void)ws.PromoteToMarried(mother, husband, 1.0f);

    ChildRecord sc{};
    sc.childId = secretChild;
    sc.name = "SecretPup";
    sc.stage = ChildStage::Child;
    sc.isAdopted = false;
    sc.isBiologicalChild = true;
    sc.guardians.Assign(mother, husband);
    sc.bioMother = mother;
    sc.bioFather = werewolfLover;
    sc.presumedFather = husband;
    sc.hasBeastblood = true;
    sc.paternitySecret.isSecret = true;
    sc.paternitySecret.isSecretForPartner = true;
    sc.paternitySecret.isSecretForBioFather = true;
    ws.lineage.Write().UpsertChild(sc);

    ChildRecord hSib{};
    hSib.childId = husbandHalfSib;
    hSib.name = "HusbandsKid";
    hSib.stage = ChildStage::Child;
    hSib.isAdopted = false;
    hSib.isBiologicalChild = true;
    hSib.guardians.Assign(husband, woman2);
    hSib.bioMother = woman2;
    hSib.bioFather = husband;
    ws.lineage.Write().UpsertChild(hSib);

    ChildRecord lSib{};
    lSib.childId = secretBioHalfSib;
    lSib.name = "LoversKid";
    lSib.stage = ChildStage::Child;
    lSib.isAdopted = false;
    lSib.isBiologicalChild = true;
    lSib.guardians.Assign(woman3, werewolfLover);
    lSib.bioMother = woman3;
    lSib.bioFather = werewolfLover;
    ws.lineage.Write().UpsertChild(lSib);

    // UnknownFather sentinel children must NEVER match as siblings
    ChildRecord unkA{};
    unkA.childId = unknownFatherChildA;
    unkA.name = "UnkA";
    unkA.stage = ChildStage::Child;
    unkA.isAdopted = false;
    unkA.bioMother = woman2;
    unkA.bioFather = ActorId::UnknownFather();
    ws.lineage.Write().UpsertChild(unkA);

    ChildRecord unkB{};
    unkB.childId = unknownFatherChildB;
    unkB.name = "UnkB";
    unkB.stage = ChildStage::Child;
    unkB.isAdopted = false;
    unkB.bioMother = woman3;
    unkB.bioFather = ActorId::UnknownFather();
    ws.lineage.Write().UpsertChild(unkB);

    auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());

    auto buildSpatial = [&](bool husbandIsWerewolf) {
        auto s = std::make_shared<SpatialActorSnapshot>();
        s->AddSample(MakeBasicSample(RefId{ mother.raw() }, 0, 0, 0));
        s->AddSample(MakeBasicSample(
            RefId{ husband.raw() }, 10, 0, 0,
            husbandIsWerewolf ? LoadedActorFlags::IsWerewolf : LoadedActorFlags::None));
        s->AddSample(MakeBasicSample(RefId{ werewolfLover.raw() }, 20, 0, 0, LoadedActorFlags::IsWerewolf));
        s->FinalizeSortAndDedupe();
        return s;
    };

    const auto spatialHumanHusband = buildSpatial(false);

    // Mother & Omniscient see beastblood AND true half-sibling (secretBioHalfSib)
    for (const auto& truthView : {
             PerspectiveView::ForActor(worldSnap, spatialHumanHusband, mother),
             PerspectiveView::ForDashboard(worldSnap, spatialHumanHusband, true) }) {
        const auto c = truthView.GetPerceivedChild(secretChild);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK(c->hasBeastblood);

        const auto sibs = truthView.GetPerceivedSiblingsOf(secretChild);
        const auto* bioSib = FindSibling(sibs, secretBioHalfSib);
        MARAS_REQUIRE(bioSib != nullptr);
        MARAS_CHECK_EQ(bioSib->relation, SiblingRelationKind::Half);
        const auto* stepSib = FindSibling(sibs, husbandHalfSib);
        MARAS_REQUIRE(stepSib != nullptr);
        MARAS_CHECK_EQ(stepSib->relation, SiblingRelationKind::AdoptiveOrStep);
    }

    // Deceived husband, child, bystander, and Dashboard(hidden) mask beastblood when visible parents are human,
    // hide secretBioHalfSib, and mistakenly see husbandHalfSib as Half sibling!
    for (const auto& maskedView : {
             PerspectiveView::ForActor(worldSnap, spatialHumanHusband, husband),
             PerspectiveView::ForActor(worldSnap, spatialHumanHusband, secretChild.AsActorId()),
             PerspectiveView::ForActor(worldSnap, spatialHumanHusband, bystander),
             PerspectiveView::ForDashboard(worldSnap, spatialHumanHusband, false) }) {
        const auto c = maskedView.GetPerceivedChild(secretChild);
        MARAS_REQUIRE(c.has_value());
        MARAS_CHECK(!c->hasBeastblood);

        const auto sibs = maskedView.GetPerceivedSiblingsOf(secretChild);
        MARAS_CHECK(FindSibling(sibs, secretBioHalfSib) == nullptr);
        const auto* presumedSib = FindSibling(sibs, husbandHalfSib);
        MARAS_REQUIRE(presumedSib != nullptr);
        MARAS_CHECK_EQ(presumedSib->relation, SiblingRelationKind::Half);
    }

    // DialogueTargetPublic NEVER exposes beastblood even if parent is a werewolf or paternity is discovered!
    const auto spatialWolfHusband = buildSpatial(true);
    const auto vWolfHusbandChild = PerspectiveView::ForActor(worldSnap, spatialWolfHusband, secretChild.AsActorId());
    MARAS_CHECK(vWolfHusbandChild.GetPerceivedChild(secretChild)->hasBeastblood);

    const auto vTargetPublic = PerspectiveView::ForDialogueTarget(worldSnap, spatialWolfHusband);
    MARAS_CHECK(!vTargetPublic.GetPerceivedChild(secretChild)->hasBeastblood);
    MARAS_CHECK(FindSibling(vTargetPublic.GetPerceivedSiblingsOf(secretChild), secretBioHalfSib) == nullptr);

    // Even after paternity discovery, DialogueTargetPublic must NEVER expose hasBeastblood
    ws.lineage.Write().FindChildMutable(secretChild)->paternitySecret.discovered = true;
    worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    MARAS_CHECK(!PerspectiveView::ForDialogueTarget(worldSnap, spatialWolfHusband)
                     .GetPerceivedChild(secretChild)
                     ->hasBeastblood);

    // UnknownFather sentinel children are NOT treated as siblings
    const auto unkSibs = vTargetPublic.GetPerceivedSiblingsOf(unknownFatherChildA);
    MARAS_CHECK(FindSibling(unkSibs, unknownFatherChildB) == nullptr);
}

MARAS_TEST(PerspectiveLens, AffairLedgerPairMaskingAndZeroPlayerExemptionInUI) {
    const ActorId paramourB{ 0x00040020u };
    const ActorId spouseC{ 0x00040030u };
    const ActorId paramourD{ 0x00040040u };
    const ActorId npc1{ 0x00040050u };
    const ActorId npc2{ 0x00040060u };
    const ActorId exSpouseNpc3{ 0x00040070u };
    const ActorId bystander{ 0x00040080u };

    WorldState ws{};
    ws.settings.Write().enablePlayerPolygamy = false;

    // Marriages
    (void)ws.PromoteToMarried(ActorId::Player(), spouseC, 1.0f);
    (void)ws.PromoteToMarried(npc1, npc2, 1.0f);

    // 1. Player secretly cheats on spouseC with paramourB (Lover)
    (void)ws.TransitionRelationship(ActorId::Player(), paramourB, RelationshipStatus::Lover, 5.0f, ActorId::Player());
    MARAS_REQUIRE(ws.RecordCheatingIncident(
        ActorId::Player(), spouseC, paramourB, /*isSecret=*/true,
        AffairType::Lover, AffairStatus::Ongoing, AffairStage::Physical, 5.0f));

    // 2. spouseC secretly cheats on Player with paramourD (Dating + ConfessTalking intent)
    (void)ws.TransitionRelationship(spouseC, paramourD, RelationshipStatus::Dating, 6.0f, spouseC);
    MARAS_REQUIRE(ws.RecordCheatingIncident(
        spouseC, ActorId::Player(), paramourD, /*isSecret=*/true,
        AffairType::Dating, AffairStatus::Ongoing, AffairStage::Talking, 6.0f));
    ws.relationships.Write().SetPendingIntent(
        spouseC,
        PendingPartnerIntentRecord{
            .type = PartnerIntentType::ConfessTalking,
            .createdGameDay = 6.5f,
            .relatedLover = paramourD,
        });

    // 3. npc1 had prior Divorced relationship with exSpouseNpc3, now secretly Lover behind npc2's back
    (void)ws.PromoteToMarried(npc1, exSpouseNpc3, 1.0f);
    (void)ws.PromoteToDivorced(npc1, exSpouseNpc3, 2.0f);
    (void)ws.TransitionRelationship(npc1, exSpouseNpc3, RelationshipStatus::Lover, 7.0f, npc1);
    MARAS_REQUIRE(ws.RecordCheatingIncident(
        npc1, npc2, exSpouseNpc3, /*isSecret=*/true,
        AffairType::Lover, AffairStatus::Ongoing, AffairStage::Talking, 7.0f));

    const auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    const auto spatialSnap = std::make_shared<SpatialActorSnapshot>();

    // A) Dashboard Intrigue Mode (revealSecretsInUI == false): ZERO Player exemption!
    const auto uiHidden = PerspectiveView::ForDashboard(worldSnap, spatialSnap, false);
    MARAS_CHECK(uiHidden.GetAllVisibleAffairs().empty());
    MARAS_CHECK(uiHidden.GetKnownAffairsInvolving(ActorId::Player()).empty());

    const auto uiPlayerParamour = uiHidden.GetPerceivedPair(ActorId::Player(), paramourB);
    MARAS_REQUIRE(uiPlayerParamour.has_value());
    MARAS_CHECK_EQ(uiPlayerParamour->status, RelationshipStatus::Candidate);
    MARAS_CHECK(!uiPlayerParamour->isOpen);
    MARAS_CHECK(!uiPlayerParamour->isSecretAffair);
    MARAS_CHECK(uiPlayerParamour->statusHistory.empty());

    MARAS_CHECK(!uiHidden.GetPerceivedPair(spouseC, paramourD).has_value());
    const auto uiSpouseCPlayer = uiHidden.GetPerceivedPair(spouseC, ActorId::Player());
    MARAS_REQUIRE(uiSpouseCPlayer.has_value());
    MARAS_CHECK_EQ(uiSpouseCPlayer->status, RelationshipStatus::Married);
    MARAS_CHECK(!uiSpouseCPlayer->isCheatingPair);  // Secret Dating edge must NOT leak via main marriage!

    const auto uiNpcEx = uiHidden.GetPerceivedPair(npc1, exSpouseNpc3);
    MARAS_REQUIRE(uiNpcEx.has_value());
    MARAS_CHECK_EQ(uiNpcEx->status, RelationshipStatus::Divorced);
    MARAS_CHECK(!uiNpcEx->isSecretAffair);
    MARAS_CHECK(std::none_of(
        uiNpcEx->statusHistory.begin(),
        uiNpcEx->statusHistory.end(),
        [](const StatusTransitionEntry& h) { return h.toStatus == RelationshipStatus::Lover; }));

    MARAS_CHECK_EQ(uiHidden.GetPerceivedPendingIntent(spouseC).type, PartnerIntentType::None);

    // B) Dashboard Omniscient Mode (revealSecretsInUI == true)
    const auto uiReveal = PerspectiveView::ForDashboard(worldSnap, spatialSnap, true);
    MARAS_CHECK_EQ(uiReveal.GetAllVisibleAffairs().size(), std::size_t{ 3 });
    MARAS_CHECK_EQ(uiReveal.GetPerceivedPair(ActorId::Player(), paramourB)->status, RelationshipStatus::Lover);
    MARAS_CHECK(uiReveal.GetPerceivedPair(spouseC, paramourD).has_value());
    MARAS_CHECK_EQ(uiReveal.GetPerceivedPair(npc1, exSpouseNpc3)->status, RelationshipStatus::Lover);
    MARAS_CHECK_EQ(uiReveal.GetPerceivedPendingIntent(spouseC).type, PartnerIntentType::ConfessTalking);

    // C) Self-perspectives: Player sees own secret affair with paramourB, NOT spouseC's affair on Player!
    const auto vPlayer = PerspectiveView::ForActor(worldSnap, spatialSnap, ActorId::Player());
    const auto playerKnownAffairs = vPlayer.GetKnownAffairsInvolving(ActorId::Player());
    MARAS_REQUIRE_EQ(playerKnownAffairs.size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(playerKnownAffairs[0].paramour, paramourB);
    MARAS_CHECK_EQ(vPlayer.GetPerceivedPendingIntent(spouseC).type, PartnerIntentType::None);
    MARAS_CHECK(!vPlayer.GetPerceivedPair(spouseC, paramourD).has_value());
    MARAS_CHECK(!vPlayer.GetPerceivedPair(spouseC, ActorId::Player())->isCheatingPair);

    // spouseC sees her own Talking affair & ConfessTalking intent
    const auto vSpouseC = PerspectiveView::ForActor(worldSnap, spatialSnap, spouseC);
    const auto spouseCAffairs = vSpouseC.GetKnownAffairsInvolving(spouseC);
    MARAS_REQUIRE_EQ(spouseCAffairs.size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(spouseCAffairs[0].effectiveAffairStage, AffairStage::Talking);
    MARAS_CHECK_EQ(vSpouseC.GetPerceivedPendingIntent(spouseC).type, PartnerIntentType::ConfessTalking);
    MARAS_CHECK(vSpouseC.GetPerceivedPair(spouseC, ActorId::Player())->isCheatingPair);

    // NPC-victim affair always reports effectiveAffairStage == Physical
    const auto vNpc1 = PerspectiveView::ForActor(worldSnap, spatialSnap, npc1);
    const auto npc1Affairs = vNpc1.GetKnownAffairsInvolving(npc1);
    MARAS_REQUIRE_EQ(npc1Affairs.size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(npc1Affairs[0].effectiveAffairStage, AffairStage::Physical);

    // DialogueTargetPublic and bystander hide all secret affairs & secret Confess intents
    for (const auto& pubView : {
             PerspectiveView::ForDialogueTarget(worldSnap, spatialSnap),
             PerspectiveView::ForActor(worldSnap, spatialSnap, bystander) }) {
        MARAS_CHECK(pubView.GetAllVisibleAffairs().empty());
        MARAS_CHECK(!pubView.GetPerceivedPair(ActorId::Player(), paramourB).has_value());
        MARAS_CHECK(!pubView.GetPerceivedPair(spouseC, paramourD).has_value());
        MARAS_CHECK_EQ(pubView.GetPerceivedPendingIntent(spouseC).type, PartnerIntentType::None);
    }
}

MARAS_TEST(PerspectiveLens, TrainingDiscountAndFamilyVisitParentHelpers) {
    const ActorId mother{ 0x00050010u };
    const ActorId husband{ 0x00050020u };
    const ActorId trueBioFather{ 0x00050030u };
    const ChildId adultChildId{ 0xFF800500u };

    WorldState ws{};
    (void)ws.PromoteToMarried(mother, husband, 1.0f);

    ChildRecord child{};
    child.childId = adultChildId;
    child.name = "Valen";
    child.stage = ChildStage::Adult;
    child.isAdopted = false;
    child.isBiologicalChild = true;
    child.guardians.Assign(mother, husband);
    child.bioMother = mother;
    child.bioFather = trueBioFather;
    child.presumedFather = husband;
    child.paternitySecret.isSecret = true;
    child.paternitySecret.isSecretForPartner = true;
    child.paternitySecret.isSecretForBioFather = true;
    ws.lineage.Write().UpsertChild(child);

    auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    auto spatialSnap = std::make_shared<SpatialActorSnapshot>();

    const auto childViewSecret = PerspectiveView::ForActor(worldSnap, spatialSnap, adultChildId.AsActorId());
    const auto discountSecret = childViewSecret.GetVisibleParentIdsForTrainingDiscount(adultChildId);
    MARAS_CHECK(ContainsActor(discountSecret, mother));
    MARAS_CHECK(ContainsActor(discountSecret, husband));
    MARAS_CHECK(!ContainsActor(discountSecret, trueBioFather));

    const auto visitSecret = childViewSecret.GetVisitableFamilyParentIds(adultChildId);
    MARAS_CHECK(ContainsActor(visitSecret, mother));
    MARAS_CHECK(ContainsActor(visitSecret, husband));
    MARAS_CHECK(!ContainsActor(visitSecret, trueBioFather));

    // Mark mother deceased -> excluded from visitable living family parents, while husband remains
    DeceasedActorRecord deadMother{};
    deadMother.baseId = BaseId{ mother.raw() };
    deadMother.refId = RefId{ mother.raw() };
    deadMother.name = "Mother";
    deadMother.deathTimestamp = 10.0f;
    ws.deceased.Write().UpsertRecord(deadMother);

    // Mark husband dead in live SpatialActorSnapshot as well
    spatialSnap->AddSample(MakeBasicSample(RefId{ husband.raw() }, 0, 0, 0, LoadedActorFlags::IsDead));
    spatialSnap->FinalizeSortAndDedupe();

    // Expose paternity secret -> trueBioFather becomes visible for both training discount and living family visit
    ws.lineage.Write().FindChildMutable(adultChildId)->paternitySecret.discovered = true;
    worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());

    const auto childViewOpen = PerspectiveView::ForActor(worldSnap, spatialSnap, adultChildId.AsActorId());
    const auto discountOpen = childViewOpen.GetVisibleParentIdsForTrainingDiscount(adultChildId);
    MARAS_CHECK(ContainsActor(discountOpen, trueBioFather));

    const auto visitOpen = childViewOpen.GetVisitableFamilyParentIds(adultChildId);
    MARAS_CHECK(!ContainsActor(visitOpen, mother));
    MARAS_CHECK(!ContainsActor(visitOpen, husband));
    MARAS_CHECK(ContainsActor(visitOpen, trueBioFather));
}

MARAS_TEST(PerspectiveLens, P2_2_RefinementsAndUnloadedWerewolfProvider) {
    class StubWerewolfProvider final : public IWerewolfStateProvider {
    public:
        ActorId unloadedWolf{};
        bool spatialPrecedenceViolated{ false };

        [[nodiscard]] std::optional<bool> IsAdultWerewolf(ActorId actor) const noexcept override {
            if (actor == unloadedWolf) {
                return true;
            }
            return std::nullopt;
        }
    };

    const ActorId mother{ 0x00060010u };
    const ActorId husband{ 0x00060020u };
    const ActorId bioFather{ 0x00060030u };
    const ActorId bystander{ 0x00060040u };
    const ChildId resolvedEdgesChild{ 0xFF800600u };
    const ChildId wolfAdultChild{ 0xFF800601u };
    const ChildId turnedAdultChild{ 0xFF800602u };

    WorldState ws{};
    (void)ws.PromoteToMarried(mother, husband, 1.0f);

    // 1. Secret edges individually resolved (discoveredByPartner = true, isSecretForBioFather = false, discovered = false)
    ChildRecord c1{};
    c1.childId = resolvedEdgesChild;
    c1.name = "EdgeResolved";
    c1.timestamp = 42;
    c1.savedBioTemplateName = "ImperialTemplate";
    c1.stage = ChildStage::Child;
    c1.isAdopted = false;
    c1.isBiologicalChild = true;
    c1.guardians.Assign(mother, husband);
    c1.bioMother = mother;
    c1.bioFather = bioFather;
    c1.presumedFather = husband;
    c1.paternitySecret.isSecret = true;
    c1.paternitySecret.isSecretForPartner = true;
    c1.paternitySecret.discoveredByPartner = true;
    c1.paternitySecret.isSecretForBioFather = false;
    c1.paternitySecret.discovered = false;
    ws.lineage.Write().UpsertChild(c1);

    // 2. Stage 4 Adult child with inherited hasBeastblood = true from unloaded werewolf bioFather
    ChildRecord c2{};
    c2.childId = wolfAdultChild;
    c2.name = "WolfAdult";
    c2.timestamp = 77;
    c2.savedBioTemplateName = "NordWolf";
    c2.stage = ChildStage::Adult;
    c2.isAdopted = false;
    c2.isBiologicalChild = true;
    c2.guardians.Assign(mother, husband);
    c2.bioMother = mother;
    c2.bioFather = bioFather;
    c2.presumedFather = husband;
    c2.hasBeastblood = true;
    c2.paternitySecret.isSecret = true;
    c2.paternitySecret.isSecretForPartner = true;
    c2.paternitySecret.isSecretForBioFather = true;
    ws.lineage.Write().UpsertChild(c2);

    // 3. Stage 4 Adult child with hasBeastblood = false who later became a werewolf in-world
    ChildRecord c3{};
    c3.childId = turnedAdultChild;
    c3.name = "TurnedAdult";
    c3.stage = ChildStage::Adult;
    c3.isAdopted = true;
    c3.guardians.Assign(mother, husband);
    c3.hasBeastblood = false;
    ws.lineage.Write().UpsertChild(c3);

    // Confronted affair record whose raw isSecret stayed true -> ToPerceivedAffair normalizes isSecret = false
    (void)ws.TransitionRelationship(mother, bioFather, RelationshipStatus::Lover, 2.0f, mother);
    MARAS_REQUIRE(ws.RecordCheatingIncident(
        mother, husband, bioFather, /*isSecret=*/true,
        AffairType::Lover, AffairStatus::EndedConfronted, AffairStage::Physical, 2.5f));

    auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    auto spatialSnap = std::make_shared<SpatialActorSnapshot>();
    spatialSnap->AddSample(MakeBasicSample(RefId{ mother.raw() }, 0, 0, 0));
    spatialSnap->AddSample(MakeBasicSample(RefId{ husband.raw() }, 5, 0, 0));
    spatialSnap->AddSample(MakeBasicSample(RefId{ turnedAdultChild.raw() }, 8, 0, 0, LoadedActorFlags::IsWerewolf));
    spatialSnap->FinalizeSortAndDedupe();

    StubWerewolfProvider wolfProvider{};
    wolfProvider.unloadedWolf = bioFather;

    // Child self-view sees true bioFather when all secret edges are resolved, plus timestamp & savedBioTemplateName
    const auto vChild = PerspectiveView::ForActor(worldSnap, spatialSnap, resolvedEdgesChild.AsActorId(), &wolfProvider);
    const auto p1 = vChild.GetPerceivedChild(resolvedEdgesChild);
    MARAS_REQUIRE(p1.has_value());
    MARAS_CHECK_EQ(p1->perceivedBioFather, bioFather);
    MARAS_CHECK_EQ(p1->timestamp, 42);
    MARAS_CHECK_EQ(p1->savedBioTemplateName, std::string("ImperialTemplate"));
    MARAS_REQUIRE(p1->paternitySecret.has_value());
    MARAS_CHECK(p1->paternitySecret->discoveredByPartner);

    // IsActorKnownWerewolf gating for Stage 4 child with secret inherited beastblood
    const auto vUiHidden = PerspectiveView::ForDashboard(worldSnap, spatialSnap, false, &wolfProvider);
    const auto vUiReveal = PerspectiveView::ForDashboard(worldSnap, spatialSnap, true, &wolfProvider);
    const auto vTarget = PerspectiveView::ForDialogueTarget(worldSnap, spatialSnap, &wolfProvider);
    const auto vBystander = PerspectiveView::ForActor(worldSnap, spatialSnap, bystander, &wolfProvider);
    const auto vMother = PerspectiveView::ForActor(worldSnap, spatialSnap, mother, &wolfProvider);
    const auto vWolfChild = PerspectiveView::ForActor(worldSnap, spatialSnap, wolfAdultChild.AsActorId(), &wolfProvider);

    MARAS_CHECK(!vUiHidden.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));
    MARAS_CHECK(!vTarget.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));
    MARAS_CHECK(!vBystander.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));
    MARAS_CHECK(!vWolfChild.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));
    MARAS_CHECK(vUiReveal.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));
    MARAS_CHECK(vMother.IsActorKnownWerewolf(wolfAdultChild.AsActorId()));

    // Unloaded werewolf bioFather resolved via IWerewolfStateProvider when viewer can know truth
    MARAS_CHECK(vMother.IsActorKnownWerewolf(bioFather));
    MARAS_CHECK(!vTarget.IsActorKnownWerewolf(bioFather));

    // Stage 4 adult child turned werewolf in spatial snapshot is recognized everywhere except DialogueTargetPublic
    MARAS_CHECK(vUiHidden.IsActorKnownWerewolf(turnedAdultChild.AsActorId()));
    MARAS_CHECK(vMother.IsActorKnownWerewolf(turnedAdultChild.AsActorId()));
    MARAS_CHECK(!vTarget.IsActorKnownWerewolf(turnedAdultChild.AsActorId()));

    // ToPerceivedAffair normalizes isSecret = false for EndedConfronted
    const auto motherAffairs = vMother.GetKnownAffairsInvolving(mother);
    MARAS_REQUIRE_EQ(motherAffairs.size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(motherAffairs[0].affairStatus, AffairStatus::EndedConfronted);
    MARAS_CHECK(!motherAffairs[0].isSecret);
}

MARAS_TEST(PerspectiveLens, PartnerBioFatherSecretPregnancyHiddenFromPartner) {
    const ActorId wife{ 0x00021010u };
    const ActorId husband{ 0x00021020u };
    const ChildId unbornId{ 0xFF801001u };

    WorldState ws{};
    MARAS::Tests::MockFormIdResolver meta{};
    (void)ws.PromoteToMarried(wife, husband, 1.0f, wife, meta);

    ChildRecord unborn{};
    unborn.childId = unbornId;
    unborn.name = "SecretFromHusbandFather";
    unborn.stage = ChildStage::Unborn;
    unborn.isBiologicalChild = true;
    unborn.isAdopted = false;
    unborn.bioMother = wife;
    unborn.bioFather = husband;
    unborn.presumedFather = husband;
    unborn.paternitySecret.isPregnancySecretForPartner = true;
    ws.lineage.Write().UpsertChild(unborn);

    auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    auto spatialSnap = std::make_shared<SpatialActorSnapshot>();

    const auto vWife = PerspectiveView::ForActor(worldSnap, spatialSnap, wife);
    const auto vHusband = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    MARAS_CHECK(vWife.IsChildVisible(unbornId));
    MARAS_CHECK(!vHusband.IsChildVisible(unbornId));
    MARAS_CHECK(!vHusband.CanViewerKnowPregnancy(unbornId));
    MARAS_CHECK(vHusband.GetExpectingPregnanciesFor(husband).empty());

    // Once announced/discovered by partner, husband sees both child and expecting pregnancy
    ws.lineage.Write().FindChildMutable(unbornId)->paternitySecret.discoveredByPartner = true;
    worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    const auto vHusbandAfter = PerspectiveView::ForActor(worldSnap, spatialSnap, husband);
    MARAS_CHECK(vHusbandAfter.IsChildVisible(unbornId));
    MARAS_CHECK(vHusbandAfter.CanViewerKnowPregnancy(unbornId));
    MARAS_REQUIRE_EQ(vHusbandAfter.GetExpectingPregnanciesFor(husband).size(), std::size_t{ 1 });
}

MARAS_TEST(PerspectiveLens, AdoptedOrphanStepParentsAndBioSiblingsSemantics) {
    const ActorId bioMom{ 0x00022010u };
    const ActorId bioDad{ 0x00022020u };
    const ActorId adoptA{ 0x00022030u };
    const ActorId adoptB{ 0x00022040u };
    const ChildId adoptedOrphanA{ 0xFF702001u };
    const ChildId adoptedOrphanB{ 0xFF702002u };
    const ChildId adoptedNonBio{ 0xFF702003u };

    WorldState ws{};

    ChildRecord cA{};
    cA.childId = adoptedOrphanA;
    cA.name = "OrphanA";
    cA.stage = ChildStage::Child;
    cA.isAdopted = true;
    cA.isBiologicalChild = true;
    cA.bioMother = bioMom;
    cA.bioFather = bioDad;
    (void)cA.guardians.Assign(adoptA, adoptB);
    ws.lineage.Write().UpsertChild(cA);

    ChildRecord cB{};
    cB.childId = adoptedOrphanB;
    cB.name = "OrphanB";
    cB.stage = ChildStage::Child;
    cB.isAdopted = true;
    cB.isBiologicalChild = true;
    cB.bioMother = bioMom;
    cB.bioFather = bioDad;
    ws.lineage.Write().UpsertChild(cB);

    ChildRecord cNonBio{};
    cNonBio.childId = adoptedNonBio;
    cNonBio.name = "AdoptedNonBio";
    cNonBio.stage = ChildStage::Child;
    cNonBio.isAdopted = true;
    cNonBio.isBiologicalChild = false;
    (void)cNonBio.guardians.Assign(adoptA, adoptB);
    ws.lineage.Write().UpsertChild(cNonBio);

    const auto worldSnap = std::make_shared<MARAS::State::WorldStateSnapshot>(ws.Snapshot());
    const auto view = PerspectiveView::ForDashboard(worldSnap, std::make_shared<SpatialActorSnapshot>(), false);

    const auto pA = view.GetPerceivedChild(adoptedOrphanA);
    MARAS_REQUIRE(pA.has_value());
    MARAS_CHECK(pA->stepParents.empty());
    MARAS_CHECK(!pA->IsActorPerceivedStepParent(adoptA));
    MARAS_CHECK(!pA->IsActorPerceivedStepParent(adoptB));

    const auto sibs = view.GetPerceivedSiblingsOf(adoptedOrphanA);
    const auto* fullSib = FindSibling(sibs, adoptedOrphanB);
    MARAS_REQUIRE(fullSib != nullptr);
    MARAS_CHECK_EQ(fullSib->relation, SiblingRelationKind::Full);
    const auto* adoptSib = FindSibling(sibs, adoptedNonBio);
    MARAS_REQUIRE(adoptSib != nullptr);
    MARAS_CHECK_EQ(adoptSib->relation, SiblingRelationKind::AdoptiveOrStep);
}
