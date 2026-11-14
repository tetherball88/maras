#include "TestHarness.h"

#include "maras/state/WorldStore.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

    using MARAS::Foundation::ActorId;
    using MARAS::Foundation::BaseId;
    using MARAS::Foundation::HouseId;
    using MARAS::Foundation::RefId;
    using MARAS::State::ActorProfileRecord;
    using MARAS::State::DeceasedActorRecord;
    using MARAS::State::RelationshipEdgeRecord;
    using MARAS::State::RelationshipStatus;
    using MARAS::State::SettingsData;
    using MARAS::State::SocialClass;
    using MARAS::State::WorldState;
    using MARAS::State::WorldStore;

}  // namespace

MARAS_TEST(WorldStore, SnapshotImmutabilityAndStructuralSharing) {
    WorldStore store{};

    const auto snap0 = store.Snapshot();
    MARAS_REQUIRE(snap0 != nullptr);
    MARAS_CHECK_EQ(snap0->actors->AllProfiles().size(), std::size_t{ 0 });

    constexpr RefId npc{ 0x0001A692 };
    store.Modify([&](WorldState& ws) {
        ActorProfileRecord profile{};
        profile.refId = npc;
        profile.socialClass = SocialClass::Nobles;
        profile.isRegisteredAdult = true;
        ws.actors.Write().Upsert(profile);
    });

    const auto snap1 = store.Snapshot();
    // snap0 remains an immutable pre-mutation view
    MARAS_CHECK_EQ(snap0->actors->AllProfiles().size(), std::size_t{ 0 });
    MARAS_CHECK_EQ(snap1->actors->AllProfiles().size(), std::size_t{ 1 });
    MARAS_CHECK(snap0->actors.get() != snap1->actors.get());

    // Untouched tables remain structurally shared at zero copy cost
    MARAS_CHECK(snap0->household.get() == snap1->household.get());
    MARAS_CHECK(snap0->lineage.get() == snap1->lineage.get());
    MARAS_CHECK(snap0->settings.get() == snap1->settings.get());
}

MARAS_TEST(WorldStore, NonVoidAndReentrantModify) {
    WorldStore store{};
    constexpr ActorId npc{ 0x0001B001 };

    const bool registered = store.Modify([&](WorldState& ws) -> bool {
        ActorProfileRecord profile{};
        profile.refId = RefId{ npc.raw() };
        profile.isRegisteredAdult = true;
        ws.actors.Write().Upsert(profile);

        // Nested Modify under std::recursive_mutex must succeed cleanly
        const auto edgeStatus = store.Modify([&](WorldState& inner) {
            RelationshipEdgeRecord edge{};
            edge.pair = MARAS::Foundation::CanonicalPairKey(ActorId::Player(), npc);
            edge.status = RelationshipStatus::Dating;
            inner.relationships.Write().UpsertEdge(edge);
            return edge.status;
        });

        MARAS_CHECK_EQ(edgeStatus, RelationshipStatus::Dating);
        return ws.actors.Read().IsRegisteredAdult(RefId{ npc.raw() });
    });

    MARAS_CHECK(registered);
    const auto snap = store.Snapshot();
    MARAS_CHECK(snap->actors->IsRegisteredAdult(RefId{ npc.raw() }));
    const auto* edge = snap->relationships->FindEdge(ActorId::Player(), npc);
    MARAS_REQUIRE(edge != nullptr);
    MARAS_CHECK_EQ(edge->status, RelationshipStatus::Dating);
}

MARAS_TEST(WorldStore, RevertRestoresIniBaselineAndWipesGameplayTables) {
    WorldStore store{};

    SettingsData iniBaseline{};
    iniBaseline.toggleUIKey = 42;
    iniBaseline.toggleUIModCtrl = true;
    iniBaseline.defaultLogLevel = 2;
    iniBaseline.logLevel = 2;
    iniBaseline.enablePlayerPolygamy = false;
    iniBaseline.uiScale = 1.25f;
    store.SetIniBaselineSettings(iniBaseline);

    const auto snapAfterIni = store.Snapshot();
    MARAS_CHECK_EQ(snapAfterIni->settings->toggleUIKey, std::uint32_t{ 42 });
    MARAS_CHECK_EQ(snapAfterIni->settings->logLevel, 2);
    MARAS_CHECK(!snapAfterIni->settings->enablePlayerPolygamy);

    constexpr ActorId npc{ 0x00020001 };
    constexpr HouseId house{ 0x00030001 };

    store.Modify([&](WorldState& ws) {
        ActorProfileRecord profile{};
        profile.refId = RefId{ npc.raw() };
        profile.isRegisteredAdult = true;
        ws.actors.Write().Upsert(profile);

        ws.affection.Write().SetPermanent(npc, 75);
        ws.affection.Write().SetLastInteractionDay(npc, 12.0f);
        ws.household.Write().RegisterHouse(house, RefId{ 0x00030002 });
        ws.hierarchy.Write().SetRawSlot(0, npc);

        DeceasedActorRecord deadRec{};
        deadRec.baseId = BaseId{ npc.raw() };
        deadRec.deathTimestamp = 14.0f;
        deadRec.wasKilledByPlayer = true;
        ws.deceased.Write().UpsertRecord(deadRec);

        ws.cooldowns.Write().lastMatchmakingDay = 9.0f;
        ws.settings.Write().logLevel = 0;
        ws.settings.Write().revealSecretsInUI = true;
        ws.settings.Write().uiScale = 1.5f;
    });

    MARAS_REQUIRE_EQ(store.Snapshot()->actors->AllProfiles().size(), std::size_t{ 1 });
    MARAS_CHECK_EQ(store.Snapshot()->settings->logLevel, 0);
    MARAS_CHECK(store.Snapshot()->settings->revealSecretsInUI);

    store.Revert();

    const auto snapAfterRevert = store.Snapshot();
    MARAS_CHECK_EQ(snapAfterRevert->actors->AllProfiles().size(), std::size_t{ 0 });
    MARAS_CHECK_EQ(snapAfterRevert->affection->AllPermanent().size(), std::size_t{ 0 });
    MARAS_CHECK_EQ(snapAfterRevert->household->AllHouses().size(), std::size_t{ 0 });
    MARAS_CHECK(snapAfterRevert->hierarchy->GetSpouseAtSlot(0).IsNull());
    MARAS_CHECK_EQ(snapAfterRevert->deceased->AllRecords().size(), std::size_t{ 0 });
    MARAS_CHECK_EQ(snapAfterRevert->cooldowns->lastMatchmakingDay, -1.0f);

    // INI baseline preserved, runtime overrides reverted
    MARAS_CHECK_EQ(snapAfterRevert->settings->toggleUIKey, std::uint32_t{ 42 });
    MARAS_CHECK(snapAfterRevert->settings->toggleUIModCtrl);
    MARAS_CHECK_EQ(snapAfterRevert->settings->logLevel, 2);
    MARAS_CHECK(!snapAfterRevert->settings->enablePlayerPolygamy);
    MARAS_CHECK_EQ(snapAfterRevert->settings->uiScale, 1.25f);
    MARAS_CHECK(!snapAfterRevert->settings->revealSecretsInUI);

    // ReplaceState swaps in staged WorldState atomically
    WorldState staged{};
    ActorProfileRecord stagedProfile{};
    stagedProfile.refId = RefId{ npc.raw() };
    stagedProfile.isRegisteredAdult = true;
    staged.actors.Write().Upsert(stagedProfile);
    staged.settings.Write() = store.GetIniBaselineSettings();
    staged.settings.Write().revealSecretsInUI = true;
    store.ReplaceState(std::move(staged));

    MARAS_CHECK(store.Snapshot()->actors->IsRegisteredAdult(RefId{ npc.raw() }));
    MARAS_CHECK_EQ(store.Snapshot()->settings->toggleUIKey, std::uint32_t{ 42 });
    MARAS_CHECK(store.Snapshot()->settings->revealSecretsInUI);
}

MARAS_TEST(WorldStore, ConcurrentReadersStayConsistentDuringWrites) {
    WorldStore store{};
    std::atomic<bool> stop{ false };
    std::atomic<std::size_t> snapshotsRead{ 0 };
    std::atomic<bool> sawInvalidSnapshot{ false };

    std::vector<std::thread> readers{};
    readers.reserve(4);
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_acquire)) {
                const auto snap = store.Snapshot();
                if (!snap || !snap->actors || !snap->relationships || !snap->affairs ||
                    !snap->lineage || !snap->hierarchy || !snap->household ||
                    !snap->affection || !snap->deceased || !snap->cooldowns || !snap->settings) {
                    sawInvalidSnapshot.store(true, std::memory_order_release);
                    break;
                }
                const auto regCount = snap->actors->Size();
                const auto permCount = snap->affection->AllPermanent().size();
                // Each writer iteration inserts into actors and affection together
                if (regCount != permCount) {
                    sawInvalidSnapshot.store(true, std::memory_order_release);
                    break;
                }
                snapshotsRead.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (std::uint32_t i = 1; i <= 500; ++i) {
        const ActorId npc{ 0x00010000U + i };
        store.Modify([&](WorldState& ws) {
            ActorProfileRecord profile{};
            profile.refId = RefId{ npc.raw() };
            profile.isRegisteredAdult = true;
            ws.actors.Write().Upsert(profile);
            ws.affection.Write().SetPermanent(npc, 50);
            ws.affection.Write().SetLastInteractionDay(npc, static_cast<float>(i));
        });
        if (i % 100 == 0) {
            store.Revert();
        }
    }

    stop.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }

    MARAS_CHECK(!sawInvalidSnapshot.load(std::memory_order_acquire));
    MARAS_CHECK(snapshotsRead.load(std::memory_order_relaxed) > 0);
}
