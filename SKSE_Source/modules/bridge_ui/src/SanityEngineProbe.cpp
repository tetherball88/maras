#include "maras/bridge_ui/SanityEngineProbe.h"

#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/BonusesConfigLoader.h"
#include "maras/engine/ChildVisualReconciler.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldStore.h"

#include <RE/B/BGSColorForm.h>
#include <RE/B/BGSPerk.h>
#include <RE/E/ExtraFactionChanges.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace MARAS::Bridge::Ui {

    namespace {

        [[nodiscard]] std::string FormatHex(RE::FormID id) {
            return id != 0 ? std::format("{:08X}", id) : std::string{};
        }

        [[nodiscard]] double RoundProbeFloat(float val) noexcept {
            return std::round(static_cast<double>(val) * 1000.0) / 1000.0;
        }

        [[nodiscard]] std::int32_t GetFactionRankSafe(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction || !actor->IsInFaction(faction)) {
                return -2;
            }
            return actor->GetFactionRank(faction, actor->IsPlayerRef());
        }

        [[nodiscard]] bool IsPlayerHouseholdChildFaction(RE::TESFaction* faction) {
            if (!faction) {
                return false;
            }
            auto& fc = MARAS::FormCache::GetSingleton();
            return faction == fc.GetPlayerChildFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptionChildOwnedFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptionFaction() ||
                   faction == fc.GetPlayerFaction() ||
                   faction == fc.GetBYOHRelationshipAdoptableFaction() ||
                   faction == fc.GetPlayerChildBothParentsFaction();
        }

        [[nodiscard]] std::size_t CountNonHouseholdFactions(RE::Actor* actor) {
            if (!actor) {
                return 0;
            }
            std::unordered_map<RE::TESFaction*, std::int8_t> effectiveFactions;
            if (auto* base = actor->GetActorBase()) {
                for (const auto& fData : base->factions) {
                    if (fData.faction && !IsPlayerHouseholdChildFaction(fData.faction)) {
                        effectiveFactions[fData.faction] = fData.rank;
                    }
                }
            }
            if (auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>()) {
                for (const auto& fData : extra->factionChanges) {
                    if (fData.faction && !IsPlayerHouseholdChildFaction(fData.faction)) {
                        if (fData.rank < 0) {
                            effectiveFactions.erase(fData.faction);
                        } else {
                            effectiveFactions[fData.faction] = fData.rank;
                        }
                    }
                }
            }

            std::size_t activeCount = 0;
            for (const auto& [faction, rank] : effectiveFactions) {
                if (faction && rank >= 0) {
                    ++activeCount;
                }
            }
            return activeCount;
        }

        [[nodiscard]] std::vector<std::string> CollectActivePlayerBonusPerks(RE::PlayerCharacter* player) {
            std::vector<std::string> activeHex;
            if (!player) {
                return activeHex;
            }

            constexpr std::array<const char*, 14> kAllBonusCategories{{
                "outcast", "poverty", "working", "middle", "wealthy", "religious", "nobles", "rulers",
                "warrior", "mage", "rogue", "craftsman", "ranger", "orator"
            }};

            const auto& bonuses = MARAS::Engine::BonusesConfigLoader::GetSingleton();
            std::set<RE::FormID> seenActivePerks;
            for (const char* cat : kAllBonusCategories) {
                const int count = bonuses.GetBonusCount(cat);
                for (int i = 0; i < count; ++i) {
                    const auto* entry = bonuses.GetBonusEntry(cat, i);
                    if (!entry || entry->perk.empty()) {
                        continue;
                    }
                    const auto resolvedId = MARAS::Utils::ParseAndResolveFormKey(entry->perk);
                    if (!resolvedId.has_value() || *resolvedId == 0) {
                        continue;
                    }
                    if (auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(*resolvedId)) {
                        if (player->HasPerk(perk)) {
                            seenActivePerks.insert(perk->GetFormID());
                        }
                    }
                }
            }

            activeHex.reserve(seenActivePerks.size());
            for (const RE::FormID perkId : seenActivePerks) {
                activeHex.push_back(FormatHex(perkId));
            }
            return activeHex;
        }

        [[nodiscard]] nlohmann::json BuildSingleActorProbe(
            RE::FormID rawId,
            const MARAS::State::WorldStateSnapshot& snap) {
            auto& fc = MARAS::FormCache::GetSingleton();
            auto* form = RE::TESForm::LookupByID(rawId);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            auto* base = actor ? actor->GetActorBase() : nullptr;
            auto* origAdultBase = MARAS::ChildVisualReconciler::GetSingleton().GetOriginalAdultBase(rawId);
            auto* parentCell = actor ? actor->GetParentCell() : nullptr;
            auto* avOwner = actor ? actor->AsActorValueOwner() : nullptr;

            RE::FormID storedMarkerId = 0;
            if (snap.household) {
                if (const auto* childSt = snap.household->FindChildState(MARAS::State::ChildId{ rawId });
                    childSt && !childSt->homeMarkerId.IsNull()) {
                    storedMarkerId = childSt->homeMarkerId.raw();
                }
            }
            if (storedMarkerId == 0 && snap.actors) {
                if (const auto* prof = snap.actors->Find(MARAS::State::RefId{ rawId });
                    prof && prof->legacyHomeMarker.has_value()) {
                    storedMarkerId = prof->legacyHomeMarker->raw();
                }
            }

            auto* markerRef = storedMarkerId != 0
                ? RE::TESForm::LookupByID<RE::TESObjectREFR>(storedMarkerId)
                : nullptr;
            auto* linkedSandboxRef = (actor && fc.GetHomeSandboxKeyword())
                ? actor->extraList.GetLinkedRef(fc.GetHomeSandboxKeyword())
                : nullptr;
            auto* crimeFaction = actor ? actor->GetCrimeFaction() : nullptr;
            auto* hairColor = (base && base->headRelatedData) ? base->headRelatedData->hairColor : nullptr;

            nlohmann::json j = nlohmann::json::object();
            j["formPresent"] = (form != nullptr);
            j["actorPresent"] = (actor != nullptr);
            j["isDeleted"] = form ? form->IsDeleted() : false;
            j["isDead"] = actor ? actor->IsDead() : false;
            j["is3DLoaded"] = actor ? actor->Is3DLoaded() : false;
            j["parentCellHex"] = parentCell ? FormatHex(parentCell->GetFormID()) : "";
            j["parentCellAttached"] = parentCell ? parentCell->IsAttached() : false;

            j["trackedRank"] = GetFactionRankSafe(actor, fc.GetTrackedFaction());
            j["hierarchyRank"] = GetFactionRankSafe(actor, fc.GetHierarchyFaction());
            j["affectionRank"] = GetFactionRankSafe(actor, fc.GetAffectionFaction());
            j["housedRank"] = GetFactionRankSafe(actor, fc.GetSpouseHousedFaction());
            j["sharedHouseRank"] = GetFactionRankSafe(actor, fc.GetSpouseSharedHouseFaction());
            j["spouseChildrenRank"] = GetFactionRankSafe(actor, fc.GetSpouseChildrenFaction());
            j["playerChildRank"] = GetFactionRankSafe(actor, fc.GetPlayerChildFaction());
            j["canFormNonCheatingMainRank"] = GetFactionRankSafe(actor, fc.GetCanFormNonCheatingMainFaction());
            j["vanillaMarriedRank"] = GetFactionRankSafe(actor, fc.GetMarriedFaction());
            j["playerFactionRank"] = GetFactionRankSafe(actor, fc.GetPlayerFaction());
            j["crimeFactionHex"] = crimeFaction ? FormatHex(crimeFaction->GetFormID()) : "";
            j["nonHouseholdFactionCount"] = CountNonHouseholdFactions(actor);

            j["variable06"] = avOwner ? RoundProbeFloat(avOwner->GetActorValue(RE::ActorValue::kVariable06)) : 0.0;
            j["variable07"] = avOwner ? RoundProbeFloat(avOwner->GetActorValue(RE::ActorValue::kVariable07)) : 0.0;
            j["scale"] = actor ? RoundProbeFloat(actor->GetScale()) : 1.0;

            j["homeMarkerHex"] = FormatHex(storedMarkerId);
            j["homeMarkerResolved"] = (markerRef != nullptr && !markerRef->IsDeleted());
            j["homeMarkerIsDynamic"] = ((storedMarkerId >> 24) == 0xFFu);
            j["linkedHomeSandboxRefHex"] = linkedSandboxRef ? FormatHex(linkedSandboxRef->GetFormID()) : "";

            j["actorBaseHex"] = base ? FormatHex(base->GetFormID()) : "";
            j["originalAdultBaseHex"] = origAdultBase ? FormatHex(origAdultBase->GetFormID()) : "";
            j["actorBasePtrNeOriginal"] = (base != nullptr && origAdultBase != nullptr && base != origAdultBase);
            j["hairColorHex"] = hairColor ? FormatHex(hairColor->GetFormID()) : "";
            return j;
        }

        [[nodiscard]] nlohmann::json BuildNpcHomeProbe(
            RE::FormID npcId,
            const MARAS::State::WorldStateSnapshot& snap) {
            const RE::FormID homeCellId = MARAS::BedOwnershipReconciler::GetSingleton().GetNpcHomeCell(npcId);
            auto* cell = homeCellId != 0 ? RE::TESForm::LookupByID<RE::TESObjectCELL>(homeCellId) : nullptr;
            const bool isPublic = cell != nullptr &&
                ((cell->cellFlags.underlying() &
                  static_cast<std::uint16_t>(RE::TESObjectCELL::Flag::kPublicArea)) != 0);

            bool sharedInDomain = false;
            if (homeCellId != 0 && snap.household) {
                const auto& cells = snap.household->AllSharedCells();
                if (const auto it = cells.find(MARAS::State::CellId{ homeCellId }); it != cells.end()) {
                    sharedInDomain = it->second.sharedWithPlayer;
                }
            }

            nlohmann::json bedsArr = nlohmann::json::array();
            for (const RE::FormID bedId : MARAS::SpatialWorldIndex::GetSingleton().GetNpcBeds(npcId)) {
                auto* bedRef = bedId != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(bedId) : nullptr;
                auto* owner = bedRef ? bedRef->extraList.GetOwner() : nullptr;
                bedsArr.push_back({
                    { "bedHex", FormatHex(bedId) },
                    { "liveOwnerHex", owner ? FormatHex(owner->GetFormID()) : "00000000" }
                });
            }

            return {
                { "homeCellHex", FormatHex(homeCellId) },
                { "cellResolved", cell != nullptr },
                { "cellIsPublic", isPublic },
                { "sharedInDomain", sharedInDomain },
                { "beds", std::move(bedsArr) }
            };
        }

    }  // namespace

    nlohmann::json BuildSanityEngineProbeJson(const MARAS::Projection::PerspectiveView& view) {
        const auto snap = MARAS::State::WorldStore::GetSingleton().Snapshot();
        auto& fc = MARAS::FormCache::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        nlohmann::json root = nlohmann::json::object();

        // 1. Live TESGlobal mirrors
        nlohmann::json globals = nlohmann::json::object();
        globals["spousesCount"] = fc.GetSpousesCount() ? RoundProbeFloat(fc.GetSpousesCount()->value) : -1.0;
        globals["loveInterestsCount"] = fc.GetLoveInterestsCount() ? RoundProbeFloat(fc.GetLoveInterestsCount()->value) : -1.0;
        globals["playerHousesCount"] = fc.GetPlayerHousesCount() ? RoundProbeFloat(fc.GetPlayerHousesCount()->value) : -1.0;
        globals["enablePolygamy"] = fc.GetEnablePolygamyGlobal() ? RoundProbeFloat(fc.GetEnablePolygamyGlobal()->value) : -1.0;
        globals["skyrimNetInstalled"] = fc.GetSkyrimNetInstalledGlobal() ? RoundProbeFloat(fc.GetSkyrimNetInstalledGlobal()->value) : -1.0;
        root["globals"] = std::move(globals);

        // 2. Live PlayerCharacter factions & spouse bonus perks
        auto activePerks = CollectActivePlayerBonusPerks(player);
        auto* vanillaMarriedFac = fc.GetMarriedFaction();
        nlohmann::json playerObj = nlohmann::json::object();
        playerObj["inVanillaMarriedFaction"] = (player != nullptr && vanillaMarriedFac != nullptr && player->IsInFaction(vanillaMarriedFac));
        playerObj["vanillaMarriedFactionRank"] = GetFactionRankSafe(player, vanillaMarriedFac);
        playerObj["activeBonusPerkCount"] = activePerks.size();
        playerObj["activeBonusPerks"] = std::move(activePerks);
        root["player"] = std::move(playerObj);

        // 3. DeferredActorResolver runtime queue counts
        root["deferredResolver"] = {
            { "pendingCount", MARAS::DeferredActorResolver::GetSingleton().GetPendingCount() },
            { "unloadedCount", MARAS::DeferredActorResolver::GetSingleton().GetUnloadedCount() }
        };

        // 4. Live RE::Actor probes (visible children + registered profiles + sanity watch-list)
        std::set<RE::FormID> actorIds{
            0x0001A69A,  // Ysolda
            0x000A2C94,  // Lydia
            0x0001A66B,  // Braith
            0x0001B11C,  // Niranye
            0x0001A675,  // Carlotta Valentia
            0x0001A671   // Mikael
        };
        if (snap && snap->actors) {
            for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
                (void)prof;
                if (!refId.IsNull() && !refId.IsPlayer() && !MARAS::Foundation::IsSyntheticChildRawId(refId.raw())) {
                    actorIds.insert(refId.raw());
                }
            }
        }
        for (const auto& perceived : view.GetVisibleChildren()) {
            if (!perceived.childId.IsNull() && !perceived.childId.IsSynthetic()) {
                actorIds.insert(perceived.childId.raw());
            }
        }

        nlohmann::json actorsObj = nlohmann::json::object();
        if (snap) {
            for (const RE::FormID rawId : actorIds) {
                actorsObj[FormatHex(rawId)] = BuildSingleActorProbe(rawId, *snap);
            }
        }
        root["actors"] = std::move(actorsObj);

        // 5. 'CFSN' Child Faction Snapshots
        nlohmann::json cfsnArr = nlohmann::json::array();
        if (snap && snap->household) {
            std::vector<RE::FormID> snapChildIds;
            snapChildIds.reserve(snap->household->AllChildFactionSnapshots().size());
            for (const auto& [cid, rec] : snap->household->AllChildFactionSnapshots()) {
                (void)rec;
                snapChildIds.push_back(cid.raw());
            }
            std::sort(snapChildIds.begin(), snapChildIds.end());
            for (const RE::FormID rawCid : snapChildIds) {
                const auto it = snap->household->AllChildFactionSnapshots().find(MARAS::State::ChildId{ rawCid });
                if (it == snap->household->AllChildFactionSnapshots().end()) {
                    continue;
                }
                cfsnArr.push_back({
                    { "childHex", FormatHex(rawCid) },
                    { "crimeFactionHex", it->second.crimeFactionId.IsNull() ? "" : FormatHex(it->second.crimeFactionId.raw()) },
                    { "factionCount", it->second.factions.size() }
                });
            }
        }
        root["childFactionSnapshots"] = std::move(cfsnArr);

        // 6. Live NPC Original Home Cells & Bed Ownership
        nlohmann::json npcHomes = nlohmann::json::object();
        if (snap) {
            for (const RE::FormID homeNpcId : { RE::FormID{ 0x0001A69A }, RE::FormID{ 0x0001A675 } }) {
                npcHomes[FormatHex(homeNpcId)] = BuildNpcHomeProbe(homeNpcId, *snap);
            }
        }
        root["npcHomes"] = std::move(npcHomes);

        return root;
    }

}  // namespace MARAS::Bridge::Ui
