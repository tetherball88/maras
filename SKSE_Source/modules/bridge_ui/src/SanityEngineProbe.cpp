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
#include <RE/B/BGSRefAlias.h>
#include <RE/B/BGSRelationship.h>
#include <RE/E/ExtraFactionChanges.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESQuest.h>

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

        [[nodiscard]] std::int32_t GetVanillaRelationshipRankToPlayer(RE::Actor* actor) {
            if (!actor || actor->IsPlayerRef()) {
                return 0;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* actorBase = actor->GetActorBase();
            auto* playerBase = player ? player->GetActorBase() : nullptr;
            if (!actorBase || !playerBase) {
                return 0;
            }
            auto* rel = RE::BGSRelationship::GetRelationship(actorBase, playerBase);
            if (!rel) {
                rel = RE::BGSRelationship::GetRelationship(playerBase, actorBase);
            }
            if (!rel) {
                return 0;
            }
            return 4 - static_cast<std::int32_t>(rel->level.underlying());
        }

        [[nodiscard]] nlohmann::json BuildMarriageQuestProbe(RE::FormID questId) {
            nlohmann::json q = nlohmann::json::object();
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
            q["formPresent"] = (quest != nullptr);
            q["isRunning"] = quest ? quest->IsRunning() : false;
            q["stage"] = quest ? static_cast<std::int32_t>(quest->GetCurrentStageID()) : -1;

            std::string loveInterestHex;
            if (quest) {
                for (auto* alias : quest->aliases) {
                    if (alias && alias->aliasName == "LoveInterest") {
                        if (auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias)) {
                            if (auto* ref = refAlias->GetReference()) {
                                loveInterestHex = FormatHex(ref->GetFormID());
                            }
                        }
                        break;
                    }
                }
            }
            q["loveInterestHex"] = std::move(loveInterestHex);
            return q;
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

        [[nodiscard]] bool IsActorVisibleInProbe(
            RE::FormID rawId,
            const MARAS::Projection::PerspectiveView& view,
            const MARAS::State::WorldStateSnapshot& snap,
            const std::vector<MARAS::Projection::PerceivedChildLineage>& visibleChildren) {
            if (view.IsOmniscient()) {
                return true;
            }
            const MARAS::State::ActorId actorId{ rawId };
            if (!view.GetPerceivedRelationshipsOf(actorId).empty()) {
                return true;
            }
            if (!view.GetKnownAffairsInvolving(actorId).empty()) {
                return true;
            }
            if (view.Hierarchy().GetRank(actorId) >= 0 || !view.Household().GetTenantHouse(rawId).IsNull()) {
                return true;
            }
            for (const auto& child : visibleChildren) {
                if (child.childId.raw() == rawId ||
                    child.guardians.HasGuardian(actorId) ||
                    child.bioMother == actorId ||
                    child.perceivedBioFather == actorId ||
                    child.presumedFather == actorId) {
                    return true;
                }
            }

            // If every relationship, affair, or child record involving this actor was hidden by
            // PerspectiveView, their presence in MARAS is strictly secret — omit/mask in Intrigue Mode.
            const bool hasHiddenRelationships =
                snap.relationships && !snap.relationships->GetAdjacentPairs(actorId).empty();
            const bool hasHiddenAffairs =
                snap.affairs && !snap.affairs->GetAllInvolving(actorId).empty();
            bool hasHiddenLineage = false;
            if (snap.lineage) {
                if (snap.lineage->FindChild(MARAS::State::ChildId{ rawId }) != nullptr) {
                    hasHiddenLineage = true;
                } else {
                    for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                        (void)cid;
                        if (rec.guardians.HasGuardian(actorId) ||
                            rec.bioMother == actorId ||
                            rec.bioFather == actorId ||
                            rec.presumedFather == actorId) {
                            hasHiddenLineage = true;
                            break;
                        }
                    }
                }
            }
            return !(hasHiddenRelationships || hasHiddenAffairs || hasHiddenLineage);
        }

        [[nodiscard]] nlohmann::json BuildSingleActorProbe(
            RE::FormID rawId,
            const MARAS::State::WorldStateSnapshot& snap,
            const MARAS::Projection::PerspectiveView& view,
            const std::vector<MARAS::Projection::PerceivedChildLineage>& visibleChildren) {
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

            std::int32_t trackedRank = GetFactionRankSafe(actor, fc.GetTrackedFaction());
            std::int32_t spouseCheatingRank = GetFactionRankSafe(actor, fc.GetSpouseCheatingFaction());
            std::int32_t spouseChildrenRank = GetFactionRankSafe(actor, fc.GetSpouseChildrenFaction());
            std::int32_t canFormNonCheatingMainRank = GetFactionRankSafe(actor, fc.GetCanFormNonCheatingMainFaction());

            if (!view.IsOmniscient()) {
                const MARAS::State::ActorId actorId{ rawId };
                const bool actorVisible = IsActorVisibleInProbe(rawId, view, snap, visibleChildren);

                if (!actorVisible) {
                    trackedRank = -2;
                    spouseCheatingRank = -2;
                    spouseChildrenRank = -2;
                    canFormNonCheatingMainRank = -2;
                } else {
                    const auto playerEdge = view.GetPerceivedPair(MARAS::State::ActorId::Player(), actorId);
                    if (trackedRank >= 0) {
                        trackedRank = playerEdge.has_value()
                            ? static_cast<std::int32_t>(playerEdge->status)
                            : static_cast<std::int32_t>(MARAS::Foundation::RelationshipStatus::Candidate);
                    }

                    // Mask SpouseCheatingFaction: never expose rank 0 (undiscovered secret affair).
                    bool hasVisibleOngoingPlayerCheating = false;
                    for (const auto& affair : view.GetKnownAffairsInvolving(actorId)) {
                        if (affair.cheater == actorId &&
                            affair.deceivedPartner.IsPlayer() &&
                            affair.affairStatus == MARAS::State::AffairStatus::Ongoing &&
                            !affair.isSecret) {
                            hasVisibleOngoingPlayerCheating = true;
                            break;
                        }
                    }
                    spouseCheatingRank = hasVisibleOngoingPlayerCheating ? 1 : -2;

                    if (canFormNonCheatingMainRank >= 0) {
                        bool hasVisibleClosedMain = false;
                        for (const auto& edge : view.GetPerceivedRelationshipsOf(actorId)) {
                            if (MARAS::State::IsMainTierStatus(edge.status) && !edge.isOpen) {
                                hasVisibleClosedMain = true;
                                break;
                            }
                        }
                        canFormNonCheatingMainRank = hasVisibleClosedMain ? 0 : 1;
                    }

                    if (spouseChildrenRank >= 0) {
                        if (playerEdge.has_value() &&
                            playerEdge->status == MARAS::Foundation::RelationshipStatus::Married) {
                            std::size_t visibleSharedChildren = 0;
                            for (const auto& child : visibleChildren) {
                                if (child.guardians.HasGuardian(MARAS::State::ActorId::Player()) &&
                                    child.guardians.HasGuardian(actorId)) {
                                    ++visibleSharedChildren;
                                }
                            }
                            spouseChildrenRank = visibleSharedChildren > 0
                                ? static_cast<std::int32_t>(std::min<std::size_t>(visibleSharedChildren, 127u))
                                : -2;
                        } else {
                            spouseChildrenRank = -2;
                        }
                    }
                }
            }

            nlohmann::json j = nlohmann::json::object();
            j["formPresent"] = (form != nullptr);
            j["actorPresent"] = (actor != nullptr);
            j["isDeleted"] = form ? form->IsDeleted() : false;
            j["isDead"] = actor ? actor->IsDead() : false;
            j["is3DLoaded"] = actor ? actor->Is3DLoaded() : false;
            j["parentCellHex"] = parentCell ? FormatHex(parentCell->GetFormID()) : "";
            j["parentCellAttached"] = parentCell ? parentCell->IsAttached() : false;

            j["trackedRank"] = trackedRank;
            j["hierarchyRank"] = GetFactionRankSafe(actor, fc.GetHierarchyFaction());
            j["affectionRank"] = GetFactionRankSafe(actor, fc.GetAffectionFaction());
            j["housedRank"] = GetFactionRankSafe(actor, fc.GetSpouseHousedFaction());
            j["sharedHouseRank"] = GetFactionRankSafe(actor, fc.GetSpouseSharedHouseFaction());
            j["spouseCheatingRank"] = spouseCheatingRank;
            j["spouseChildrenRank"] = spouseChildrenRank;
            j["playerChildRank"] = GetFactionRankSafe(actor, fc.GetPlayerChildFaction());
            j["canFormNonCheatingMainRank"] = canFormNonCheatingMainRank;
            j["vanillaMarriedRank"] = GetFactionRankSafe(actor, fc.GetMarriedFaction());
            j["vanillaCourtingRank"] = GetFactionRankSafe(actor, fc.GetCourtingFaction());
            j["vanillaRelationshipRankToPlayer"] = GetVanillaRelationshipRankToPlayer(actor);
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

        const auto visibleChildren = view.GetVisibleChildren();

        // 1. Live TESGlobal mirrors
        nlohmann::json globals = nlohmann::json::object();
        globals["spousesCount"] = fc.GetSpousesCount() ? RoundProbeFloat(fc.GetSpousesCount()->value) : -1.0;
        if (!fc.GetLoveInterestsCount()) {
            globals["loveInterestsCount"] = -1.0;
        } else if (view.IsOmniscient()) {
            globals["loveInterestsCount"] = RoundProbeFloat(fc.GetLoveInterestsCount()->value);
        } else {
            std::int32_t visibleLoveInterests = 0;
            for (const auto& edge : view.GetPerceivedRelationshipsOf(MARAS::State::ActorId::Player())) {
                if (edge.status == MARAS::Foundation::RelationshipStatus::Married ||
                    edge.status == MARAS::Foundation::RelationshipStatus::Engaged) {
                    ++visibleLoveInterests;
                }
            }
            globals["loveInterestsCount"] = static_cast<double>(visibleLoveInterests);
        }
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
        playerObj["canFormNonCheatingMainRank"] = GetFactionRankSafe(player, fc.GetCanFormNonCheatingMainFaction());
        playerObj["activeBonusPerkCount"] = activePerks.size();
        playerObj["activeBonusPerks"] = std::move(activePerks);
        root["player"] = std::move(playerObj);

        // 2b. Live vanilla marriage quests (RelationshipMarriage 0x00074793, RelationshipMarriageBreakUp 0x0007431B)
        root["marriageQuests"] = {
            { "relationshipMarriage", BuildMarriageQuestProbe(0x00074793u) },
            { "relationshipMarriageBreakUp", BuildMarriageQuestProbe(0x0007431Bu) }
        };

        // 3. DeferredActorResolver runtime queue counts
        root["deferredResolver"] = {
            { "pendingCount", MARAS::DeferredActorResolver::GetSingleton().GetPendingCount() },
            { "unloadedCount", MARAS::DeferredActorResolver::GetSingleton().GetUnloadedCount() }
        };

        // 4. Live RE::Actor probes (visible children + visible registered profiles + sanity watch-list)
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
                    if (view.IsOmniscient() || IsActorVisibleInProbe(refId.raw(), view, *snap, visibleChildren)) {
                        actorIds.insert(refId.raw());
                    }
                }
            }
        }
        for (const auto& perceived : visibleChildren) {
            if (!perceived.childId.IsNull() && !perceived.childId.IsSynthetic()) {
                actorIds.insert(perceived.childId.raw());
            }
        }

        nlohmann::json actorsObj = nlohmann::json::object();
        if (snap) {
            for (const RE::FormID rawId : actorIds) {
                actorsObj[FormatHex(rawId)] = BuildSingleActorProbe(rawId, *snap, view, visibleChildren);
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
                if (view.IsOmniscient() || view.IsChildVisible(cid)) {
                    snapChildIds.push_back(cid.raw());
                }
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
