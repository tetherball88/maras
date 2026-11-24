#include "maras/app/MatchmakingContextBuilder.h"

#include "maras/app/AppContext.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/agency/MatchmakingValidationRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace MARAS::App {

    namespace {

        struct ActorProfileSnapshot {
            std::string name;
            std::string race;
            std::string sex;
            Foundation::ActorSex sexEnum{ Foundation::ActorSex::Unknown };
            std::uint64_t uuid{ 0 };
        };

        [[nodiscard]] std::string GetPlayerNameSafe() {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetName(); name && name[0] != '\0') {
                    return name;
                }
            }
            return "Dragonborn";
        }

        [[nodiscard]] ActorProfileSnapshot GetActorProfileSnapshot(RE::FormID actorID) {
            const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(actorID);
            ActorProfileSnapshot p;
            p.name = (entry && !entry->nameDisplay.empty()) ? entry->nameDisplay : ResolveActorNameForAgency(actorID);
            const std::uint8_t rawSex = entry ? entry->sex : ActorSvc::GetSex(actorID);
            p.sexEnum = (rawSex == 0) ? Foundation::ActorSex::Male
                                      : ((rawSex == 1) ? Foundation::ActorSex::Female : Foundation::ActorSex::Unknown);
            p.sex = (rawSex == 1) ? "female" : "male";
            p.race = (entry && !entry->raceName.empty()) ? entry->raceName : ActorSvc::GetRaceName(actorID);
            if (p.race.empty()) {
                p.race = "Nord";
            }
            p.uuid = GetSkyrimNetGateway().GetActorUUID(actorID);
            return p;
        }

        [[nodiscard]] std::string JoinStrings(const std::vector<std::string>& items, std::string_view delim) {
            std::string out;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out += delim;
                }
                out += items[i];
            }
            return out;
        }

        [[nodiscard]] std::string GetAffectionTierLabel(int affection) {
            if (affection >= 75) return "Happy";
            if (affection >= 50) return "Content";
            if (affection >= 25) return "Troubled";
            return "Estranged";
        }

        [[nodiscard]] std::string GetLocationDisplayName(RE::BGSLocation* loc) {
            if (!loc) return {};
            if (const char* name = loc->GetName(); name && name[0] != '\0') {
                return name;
            }
            if (const char* edid = loc->GetFormEditorID(); edid && edid[0] != '\0') {
                return edid;
            }
            return {};
        }

        [[nodiscard]] std::string GetCellDisplayName(RE::TESObjectCELL* cell) {
            if (!cell) return {};
            if (const char* name = cell->GetName(); name && name[0] != '\0') {
                return name;
            }
            if (const char* edid = cell->GetFormEditorID(); edid && edid[0] != '\0') {
                return edid;
            }
            return {};
        }

        [[nodiscard]] bool HasWhitelistedHabitatKeyword(RE::BGSLocation* loc) {
            if (!loc) return false;
            auto& fc = FormCache::GetSingleton();
            auto* locTypeDwelling = RE::TESForm::LookupByID<RE::BGSKeyword>(0x000130DCu);
            auto* locTypeCastle = RE::TESForm::LookupByID<RE::BGSKeyword>(0x0001CD57u);
            RE::BGSKeyword* const keywords[] = {
                fc.GetLocTypeTempleKeyword(),
                fc.GetLocTypeStoreKeyword(),
                fc.GetLocTypeStewardsDwellingKeyword(),
                fc.GetLocTypeInnKeyword(),
                fc.GetLocTypeHouseKeyword(),
                fc.GetLocTypeBarracksKeyword(),
                fc.GetLocTypeGuildKeyword(),
                fc.GetLocTypePlayerHouseKeyword(),
                fc.GetBYOHLocTypeHomesteadKeyword(),
                fc.GetLocTypeTownKeyword(),
                fc.GetLocTypeSettlementKeyword(),
                fc.GetLocTypeOrcStrongholdKeyword(),
                fc.GetLocTypeCityKeyword(),
                fc.GetLocTypeLumberMillKeyword(),
                fc.GetLocTypeFarmKeyword(),
                locTypeDwelling,
                locTypeCastle
            };
            for (auto* kw : keywords) {
                if (kw && loc->HasKeyword(kw)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] RE::BGSLocation* FindWhitelistedHabitatInChain(RE::BGSLocation* startLoc) {
            int depth = 0;
            for (auto* cur = startLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (HasWhitelistedHabitatKeyword(cur)) {
                    return cur;
                }
            }
            return nullptr;
        }

        [[nodiscard]] RE::BGSLocation* FindSettlementRootInChain(RE::BGSLocation* habitatLoc) {
            if (!habitatLoc) return nullptr;
            int depth = 0;
            for (auto* cur = habitatLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (IsSettlementLevelKeyword(cur)) {
                    return cur;
                }
            }
            return habitatLoc;
        }

        [[nodiscard]] RE::BGSLocation* FindHoldRootInChain(RE::BGSLocation* startLoc) {
            if (!startLoc) return nullptr;
            auto* locTypeHold = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00016771u);
            RE::BGSLocation* topMost = startLoc;
            int depth = 0;
            for (auto* cur = startLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (locTypeHold && cur->HasKeyword(locTypeHold)) {
                    return cur;
                }
                topMost = cur;
            }
            return topMost;
        }

        [[nodiscard]] std::size_t CountSharedChildrenForPair(const State::WorldStateSnapshot& snap,
                                                             Foundation::ActorId a,
                                                             Foundation::ActorId b) noexcept {
            if (!snap.lineage) {
                return 0;
            }
            std::size_t count = 0;
            for (const auto& [cid, rec] : snap.lineage->AllChildren()) {
                (void)cid;
                if (rec.stage == Foundation::ChildStage::Unborn) {
                    continue;
                }
                const bool hasA = rec.guardians.HasGuardian(a) || rec.bioMother == a || rec.bioFather == a;
                const bool hasB = rec.guardians.HasGuardian(b) || rec.bioMother == b || rec.bioFather == b;
                if (hasA && hasB) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] bool HasAnyMainPartner(const State::WorldStateSnapshot& snap,
                                             Foundation::ActorId actorId) noexcept {
            if (!snap.relationships) {
                return false;
            }
            return !snap.relationships->GetPrimaryPartner(actorId).IsNull();
        }

        [[nodiscard]] bool IsPlayerActiveMainPartnerSnap(const State::WorldStateSnapshot& snap,
                                                         RE::FormID id) noexcept {
            if (!snap.relationships || id == 0 || id == kPlayerFormID) {
                return false;
            }
            return Foundation::IsMainTierStatus(
                snap.relationships->GetStatus(Foundation::ActorId::Player(), Foundation::ActorId{ id }));
        }

        [[nodiscard]] bool IsActorInExclusiveMainRelationship(const State::WorldStateSnapshot& snap,
                                                              Foundation::ActorId actorId) noexcept {
            if (!snap.relationships || actorId.IsNull()) {
                return false;
            }
            return HasAnyMainPartner(snap, actorId) && !snap.relationships->HasAnyOpenMainPair(actorId);
        }

        [[nodiscard]] std::string FormatNpcRosterLine(const NPCEntry& entry,
                                                      const State::WorldStateSnapshot& snap) {
            const RE::FormID fid = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            const Foundation::ActorId aid{ fid };
            const bool canFormMain = !HasAnyMainPartner(snap, aid);
            const bool hasOpenMain = snap.relationships && snap.relationships->HasAnyOpenMainPair(aid);

            const std::string name = !entry.nameDisplay.empty() ? entry.nameDisplay : entry.nameLower;
            const char* sex = (entry.sex == 1) ? "female" : "male";
            const std::string race = !entry.raceName.empty() ? entry.raceName : "Nord";
            const auto elig = Domain::Agency::GetEligibilityCategoryString(canFormMain, hasOpenMain);
            return std::format("{} ({}, {}) - [{}]", name, sex, race, elig);
        }

        struct TargetAllowedOperations {
            std::string operationsText;
            std::string actions;
            std::string statuses;
        };

        [[nodiscard]] TargetAllowedOperations BuildTargetAllowedOperations(
            const State::WorldStateSnapshot& snap,
            RE::FormID targetNpcFormID,
            bool allowBreakups,
            bool allowOpenClose,
            bool allowCheating) {
            const Foundation::ActorId targetId{ targetNpcFormID };
            std::vector<RE::FormID> datingPartners;
            std::vector<RE::FormID> engagedPartners;
            std::vector<RE::FormID> marriedPartners;
            std::vector<RE::FormID> loverPartners;

            if (snap.relationships) {
                for (const auto& [pid, st] : snap.relationships->GetActivePartners(targetId)) {
                    switch (st) {
                        case Foundation::RelationshipStatus::Dating:
                            datingPartners.push_back(pid.raw());
                            break;
                        case Foundation::RelationshipStatus::Engaged:
                            engagedPartners.push_back(pid.raw());
                            break;
                        case Foundation::RelationshipStatus::Married:
                            marriedPartners.push_back(pid.raw());
                            break;
                        case Foundation::RelationshipStatus::Lover:
                            loverPartners.push_back(pid.raw());
                            break;
                        default:
                            break;
                    }
                }
            }

            const bool canFormMain = datingPartners.empty() && engagedPartners.empty() && marriedPartners.empty();
            const bool hasOpenMain = snap.relationships && snap.relationships->HasAnyOpenMainPair(targetId);

            std::vector<std::string> opLines;
            std::vector<std::string> actions;
            std::vector<std::string> statuses;

            auto addStatus = [&](const std::string& st) {
                if (std::find(statuses.begin(), statuses.end(), st) == statuses.end()) {
                    statuses.push_back(st);
                }
            };

            // 1. Create new relationships
            if (canFormMain) {
                opLines.push_back("- **Create new relationships** (dating, lover - allowed) with an eligible candidate");
                actions.push_back("new");
                addStatus("dating");
                addStatus("lover");
            } else if (hasOpenMain) {
                opLines.push_back("- **Create new relationships** (lover - allowed by open relationship) with an eligible candidate");
                actions.push_back("new");
                addStatus("lover");
            } else if (allowCheating) {
                opLines.push_back("- **Create new relationships** (lover - considered cheating/infidelity) with an eligible candidate");
                actions.push_back("new");
                addStatus("lover");
            }

            // 2. Advance existing relationships
            std::vector<std::string> advanceDetails;
            auto addAdvance = [&](const std::vector<RE::FormID>& partners, const char* from, const char* to) {
                for (const RE::FormID pid : partners) {
                    const std::string name = ResolveActorNameForAgency(pid);
                    advanceDetails.push_back(std::format("{}->{} with {}", from, to, name.empty() ? "partner" : name));
                    addStatus(to);
                }
            };
            addAdvance(datingPartners, "dating", "engaged");
            addAdvance(engagedPartners, "engaged", "married");

            if (canFormMain) {
                for (const RE::FormID pid : loverPartners) {
                    if (!HasAnyMainPartner(snap, Foundation::ActorId{ pid })) {
                        const std::string name = ResolveActorNameForAgency(pid);
                        advanceDetails.push_back(
                            "lover->dating or lover->engaged with " + (name.empty() ? std::string("lover") : name));
                        addStatus("dating");
                        addStatus("engaged");
                    }
                }
            }
            if (!advanceDetails.empty()) {
                actions.push_back("advance");
                opLines.push_back("- **Advance existing relationship** (" + JoinStrings(advanceDetails, ", ") + ")");
            }

            // 3. Break up existing relationships
            if (allowBreakups) {
                std::vector<std::string> breakupDetails;
                auto addBreakups = [&](const std::vector<RE::FormID>& partners, const char* from, const char* to) {
                    for (const RE::FormID pid : partners) {
                        const std::string name = ResolveActorNameForAgency(pid);
                        breakupDetails.push_back(std::format("{} to {} with {}", from, to, name.empty() ? from : name));
                        addStatus(to);
                    }
                };
                addBreakups(datingPartners, "dating", "exdating");
                addBreakups(engagedPartners, "engaged", "jilted");
                addBreakups(marriedPartners, "married", "divorced");
                addBreakups(loverPartners, "lover", "exlover");

                if (!breakupDetails.empty()) {
                    actions.push_back("breakup");
                    opLines.push_back("- **Break up existing relationship** (" + JoinStrings(breakupDetails, ", ") + ")");
                }
            }

            // 4. Open or close relationships
            if (allowOpenClose) {
                auto& relUseCases = RelationshipUseCases::GetSingleton();
                std::vector<std::string> openCloseDetails;
                std::vector<RE::FormID> allMain = datingPartners;
                allMain.insert(allMain.end(), engagedPartners.begin(), engagedPartners.end());
                allMain.insert(allMain.end(), marriedPartners.begin(), marriedPartners.end());

                bool hasOpen = false;
                bool hasClose = false;
                for (const RE::FormID pid : allMain) {
                    std::string pName = ResolveActorNameForAgency(pid);
                    if (pName.empty()) {
                        pName = "partner";
                    }
                    if (relUseCases.CanOpenRelationship(targetNpcFormID, pid)) {
                        openCloseDetails.push_back("open relationship with " + pName + " (switch to open)");
                        hasOpen = true;
                    } else if (relUseCases.CanCloseRelationship(targetNpcFormID, pid)) {
                        openCloseDetails.push_back("close relationship with " + pName + " (switch to exclusive)");
                        hasClose = true;
                    }
                }
                if (hasOpen) actions.push_back("open");
                if (hasClose) actions.push_back("close");
                if (!openCloseDetails.empty()) {
                    opLines.push_back("- **Open or close relationships** (" + JoinStrings(openCloseDetails, ", ") + ")");
                }
            }

            TargetAllowedOperations res;
            res.operationsText = opLines.empty()
                ? "No relationship modifications are currently available for this character based on their relationship status and mod settings."
                : JoinStrings(opLines, "\n");
            res.actions = actions.empty() ? "none" : JoinStrings(actions, "|");
            res.statuses = statuses.empty() ? "none" : JoinStrings(statuses, "|");
            return res;
        }

        [[nodiscard]] bool HasPastMarasRelationship(const State::WorldStateSnapshot& snap,
                                                    Foundation::ActorId a,
                                                    Foundation::ActorId b) noexcept {
            if (!snap.relationships) {
                return false;
            }
            const auto* edge = snap.relationships->FindEdge(a, b);
            if (!edge) {
                return false;
            }
            if (edge->status == Foundation::RelationshipStatus::ExLover ||
                edge->status == Foundation::RelationshipStatus::ExDating ||
                edge->status == Foundation::RelationshipStatus::Jilted ||
                edge->status == Foundation::RelationshipStatus::Divorced) {
                return true;
            }
            if (Foundation::IsActivePartnerStatus(edge->originalStatus) ||
                (Foundation::IsBreakupStatus(edge->originalStatus) &&
                 edge->originalStatus != Foundation::RelationshipStatus::Candidate)) {
                return true;
            }
            for (const auto& hist : edge->statusHistory) {
                if ((hist.fromStatus != Foundation::RelationshipStatus::Unknown &&
                     hist.fromStatus != Foundation::RelationshipStatus::Candidate) ||
                    (hist.toStatus != Foundation::RelationshipStatus::Unknown &&
                     hist.toStatus != Foundation::RelationshipStatus::Candidate)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] float GetLatestNonBreakupStartDay(const State::WorldStateSnapshot& snap,
                                                        Foundation::ActorId a,
                                                        Foundation::ActorId b) noexcept {
            if (!snap.relationships) {
                return -1.0f;
            }
            const auto* edge = snap.relationships->FindEdge(a, b);
            if (!edge) {
                return -1.0f;
            }
            for (auto it = edge->statusHistory.rbegin(); it != edge->statusHistory.rend(); ++it) {
                if (!Foundation::IsBreakupStatus(it->toStatus) && it->timestamp >= 0.0f) {
                    return it->timestamp;
                }
            }
            return -1.0f;
        }

        [[nodiscard]] std::string GetSharedWorkOrGuildFactionName(RE::FormID actorA,
                                                                  RE::FormID actorB,
                                                                  RE::FormID crimeFacA,
                                                                  RE::FormID crimeFacB) {
            auto* baseA = ActorSvc::GetActorBase(actorA);
            auto* baseB = ActorSvc::GetActorBase(actorB);
            if (!baseA || !baseB) {
                return {};
            }
            auto* trackedFac = FormCache::GetSingleton().GetTrackedFaction();
            const std::uint32_t marasModByte = trackedFac ? (trackedFac->GetFormID() >> 24) : 0xFEu;

            for (const auto& fA : baseA->factions) {
                if (!fA.faction || fA.rank < 0) continue;
                const RE::FormID fid = fA.faction->GetFormID();
                if (fid == 0 || fid == crimeFacA || fid == crimeFacB) continue;
                if ((fid >> 24) == marasModByte) continue;

                const char* edid = fA.faction->GetFormEditorID();
                if (edid && edid[0] != '\0') {
                    const std::string edidLower = Foundation::StringUtils::ToLowerStd(edid);
                    if (edidLower.find("crime") != std::string::npos ||
                        edidLower.find("town") != std::string::npos ||
                        edidLower.find("favor") != std::string::npos ||
                        edidLower.find("marriage") != std::string::npos ||
                        edidLower.find("potential") != std::string::npos ||
                        edidLower.find("services") != std::string::npos) {
                        continue;
                    }
                }
                for (const auto& fB : baseB->factions) {
                    if (fB.faction == fA.faction && fB.rank >= 0) {
                        if (const char* fName = fA.faction->GetName(); fName && fName[0] != '\0') {
                            return fName;
                        }
                        if (edid && edid[0] != '\0') {
                            return edid;
                        }
                        return "Guild/Faction";
                    }
                }
            }
            return {};
        }

    }  // namespace

    std::string ResolveActorNameForAgency(RE::FormID id) {
        if (id == 0) {
            return {};
        }
        if (id == kPlayerFormID) {
            return GetPlayerNameSafe();
        }
        if (auto name = SpatialWorldIndex::GetSingleton().GetName(id); !name.empty()) {
            return name;
        }
        return ActorSvc::GetName(id);
    }

    Foundation::ActorSex ResolveActorSexForAgency(RE::FormID id) noexcept {
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id)) {
            if (entry->sex == 0) return Foundation::ActorSex::Male;
            if (entry->sex == 1) return Foundation::ActorSex::Female;
        }
        const std::uint8_t rawSex = ActorSvc::GetSex(id);
        if (rawSex == 0) return Foundation::ActorSex::Male;
        if (rawSex == 1) return Foundation::ActorSex::Female;
        return Foundation::ActorSex::Unknown;
    }

    bool IsNpcEligibleWorldAdult(RE::FormID id, const State::WorldStateSnapshot& snap) {
        if (id == 0 || id == kPlayerFormID || id == Foundation::kUnknownFatherRawId || IsSyntheticChildID(id)) {
            return false;
        }
        Foundation::BaseId knownBase{};
        if (snap.actors) {
            if (const auto* prof = snap.actors->Find(Foundation::RefId{ id })) {
                knownBase = prof->baseId;
            }
        }
        if (knownBase.IsNull()) {
            const RE::FormID idxBase = SpatialWorldIndex::GetSingleton().GetBaseID(id);
            if (idxBase != 0) {
                knownBase = Foundation::BaseId{ idxBase };
            }
        }
        if (snap.deceased && snap.deceased->IsDeceased(Foundation::ActorId{ id }, knownBase)) {
            return false;
        }
        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
            if (actor->IsDead() || actor->IsDeleted() || actor->IsChild()) {
                return false;
            }
            return true;
        }
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(id)) {
            return !entry->isChild;
        }
        return ActorSvc::IsValid(id);
    }

    bool IsPairCheatingTransition(const State::WorldStateSnapshot& snap,
                                  RE::FormID actorA,
                                  RE::FormID actorB,
                                  std::string_view targetStatus) {
        return Domain::Agency::IsPairCheatingTransition(
            snap, Foundation::ActorId{ actorA }, Foundation::ActorId{ actorB }, targetStatus);
    }

    bool IsSettlementLevelKeyword(RE::BGSLocation* loc) {
        if (!loc) return false;
        auto& fc = FormCache::GetSingleton();
        RE::BGSKeyword* const settlementKws[] = {
            fc.GetLocTypeCityKeyword(),
            fc.GetLocTypeTownKeyword(),
            fc.GetLocTypeSettlementKeyword(),
            fc.GetLocTypeOrcStrongholdKeyword(),
            fc.GetLocTypeLumberMillKeyword(),
            fc.GetLocTypeFarmKeyword(),
            fc.GetLocTypePlayerHouseKeyword(),
            fc.GetBYOHLocTypeHomesteadKeyword()
        };
        for (auto* kw : settlementKws) {
            if (kw && loc->HasKeyword(kw)) {
                return true;
            }
        }
        return false;
    }

    bool IsLocationOrParentMatch(RE::BGSLocation* loc, RE::FormID targetLocID) {
        if (!loc || targetLocID == 0) return false;
        int depth = 0;
        for (auto* cur = loc; cur != nullptr && depth < 8; cur = cur->parentLoc, ++depth) {
            if (cur->GetFormID() == targetLocID) {
                return true;
            }
        }
        return false;
    }

    ActorLocationContext ResolveActorLocationContext(RE::FormID actorFormID,
                                                     const State::WorldStateSnapshot& snap) {
        ActorLocationContext ctx;
        if (actorFormID == 0 || actorFormID == kPlayerFormID) {
            return ctx;
        }

        auto& idx = SpatialWorldIndex::GetSingleton();
        const auto entryOpt = idx.FindByFormID(actorFormID);

        if (entryOpt.has_value() && entryOpt->crimeFactionID != 0) {
            ctx.crimeFactionID = entryOpt->crimeFactionID;
        } else if (auto* base = ActorSvc::GetActorBase(actorFormID)) {
            if (base->crimeFaction) {
                ctx.crimeFactionID = base->crimeFaction->GetFormID();
            }
        }

        RE::FormID tenantHouseID =
            snap.household ? snap.household->GetTenantHouse(actorFormID).raw() : 0;
        if (tenantHouseID == 0 && snap.actors) {
            if (const auto* prof = snap.actors->Find(Foundation::RefId{ actorFormID })) {
                if (prof->legacyCurrentHome.has_value() && !prof->legacyCurrentHome->IsNull()) {
                    tenantHouseID = prof->legacyCurrentHome->raw();
                } else if (prof->legacyOriginalHome.has_value() && !prof->legacyOriginalHome->IsNull()) {
                    tenantHouseID = prof->legacyOriginalHome->raw();
                }
            }
        }
        if (tenantHouseID != 0) {
            ctx.playerHouseID = tenantHouseID;
            ctx.activeCells.insert(tenantHouseID);
        }
        const RE::FormID homeCellID = idx.GetNpcHome(actorFormID);
        if (homeCellID != 0) {
            ctx.activeCells.insert(homeCellID);
        }

        auto applyHabitat = [&](RE::BGSLocation* rawLoc, RE::FormID cellID) -> bool {
            auto* habitat = FindWhitelistedHabitatInChain(rawLoc);
            if (!habitat) return false;
            auto* settlement = FindSettlementRootInChain(habitat);
            auto* holdLoc = FindHoldRootInChain(settlement ? settlement : habitat);
            if (cellID != 0) {
                ctx.activeCellID = cellID;
                ctx.activeCells.insert(cellID);
                if (auto* c = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID)) {
                    ctx.activeCellName = GetCellDisplayName(c);
                }
            }
            ctx.subLocationID = habitat->GetFormID();
            ctx.subLocationName = GetLocationDisplayName(habitat);
            ctx.settlementLocationID = settlement ? settlement->GetFormID() : habitat->GetFormID();
            ctx.settlementName = GetLocationDisplayName(settlement ? settlement : habitat);
            if (holdLoc) {
                ctx.holdLocationID = holdLoc->GetFormID();
                ctx.holdName = GetLocationDisplayName(holdLoc);
            }
            ctx.locationName = !ctx.settlementName.empty() ? ctx.settlementName : ctx.subLocationName;
            return true;
        };

        // 1. Live Runtime State
        if (RE::Actor* actor = idx.GetActorFromFormId(actorFormID);
            actor && !actor->IsDeleted() && !actor->IsDisabled()) {
            auto* parentCell = actor->GetParentCell();
            const RE::FormID cellID = parentCell ? parentCell->GetFormID() : 0;
            RE::BGSLocation* liveLoc = actor->GetCurrentLocation();
            if (!liveLoc && parentCell) {
                liveLoc = parentCell->GetLocation();
            }
            if (applyHabitat(liveLoc, cellID)) {
                return ctx;
            }
        }

        // 2. Household Assignment & HomeCell
        if (tenantHouseID != 0) {
            if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(tenantHouseID)) {
                if (!applyHabitat(loc, homeCellID)) {
                    ctx.subLocationID = tenantHouseID;
                    ctx.subLocationName = HouseholdEngineOps::GetHouseName(tenantHouseID);
                    ctx.settlementLocationID = tenantHouseID;
                    ctx.settlementName = ctx.subLocationName;
                    ctx.locationName = ctx.subLocationName;
                }
                return ctx;
            }
            if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(tenantHouseID)) {
                ctx.activeCellID = tenantHouseID;
                ctx.activeCellName = GetCellDisplayName(cell);
                if (!applyHabitat(cell->GetLocation(), tenantHouseID)) {
                    ctx.subLocationID = tenantHouseID;
                    ctx.subLocationName = HouseholdEngineOps::GetHouseName(tenantHouseID);
                    ctx.settlementLocationID = tenantHouseID;
                    ctx.settlementName = ctx.subLocationName;
                    ctx.locationName = ctx.subLocationName;
                }
                return ctx;
            }
        }

        if (homeCellID != 0) {
            if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(homeCellID)) {
                if (applyHabitat(cell->GetLocation(), homeCellID)) {
                    return ctx;
                }
            }
        }

        // 3. Static Fallback
        if (entryOpt.has_value()) {
            if (entryOpt->editorLocationID != 0) {
                if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(entryOpt->editorLocationID)) {
                    if (applyHabitat(loc, entryOpt->placementCellID)) {
                        return ctx;
                    }
                }
            }
            if (entryOpt->placementCellID != 0) {
                if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(entryOpt->placementCellID)) {
                    if (applyHabitat(cell->GetLocation(), entryOpt->placementCellID)) {
                        return ctx;
                    }
                }
            }
        }

        return ctx;
    }

    TavernRendezvousTarget FindNearbyTavernForRendezvous(RE::BGSLocation* homeLoc,
                                                         const ActorLocationContext& loverLocCtx) {
        TavernRendezvousTarget bestResult;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        auto& fc = FormCache::GetSingleton();
        auto* kwInn = fc.GetLocTypeInnKeyword();
        auto* centerLcrt = fc.GetLocationCenterMarkerRefType();
        if (!dataHandler || !kwInn) {
            return bestResult;
        }

        RE::BGSLocation* homeSettlement = FindSettlementRootInChain(homeLoc);
        RE::BGSLocation* homeHold = FindHoldRootInChain(homeLoc);
        const RE::FormID homeSettlementID = homeSettlement ? homeSettlement->GetFormID() : 0;
        const RE::FormID homeHoldID = homeHold ? homeHold->GetFormID() : 0;
        const RE::FormID loverSettlementID = loverLocCtx.settlementLocationID;
        const RE::FormID loverHoldID = loverLocCtx.holdLocationID;

        int bestScore = -1;
        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;
            RE::BGSLocation* cellLoc = cell->GetLocation();
            if (!cellLoc || !cellLoc->HasKeyword(kwInn)) continue;

            const char* rawInnName = cellLoc->GetFullName();
            if (!rawInnName || rawInnName[0] == '\0') continue;

            RE::FormID exactCenterMarker = 0;
            RE::FormID fallbackMarker = 0;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (centerLcrt) {
                    if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                        if (lrt->locRefType == centerLcrt) {
                            exactCenterMarker = ref->GetFormID();
                            return RE::BSContainer::ForEachResult::kStop;
                        }
                    }
                }
                if (fallbackMarker == 0 && ref->IsPersistent()) {
                    fallbackMarker = ref->GetFormID();
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            const RE::FormID chosenMarker = (exactCenterMarker != 0) ? exactCenterMarker : fallbackMarker;
            if (chosenMarker == 0) continue;

            RE::BGSLocation* innSettlement = FindSettlementRootInChain(cellLoc);
            RE::BGSLocation* innHold = FindHoldRootInChain(cellLoc);
            const RE::FormID innSettlementID = innSettlement ? innSettlement->GetFormID() : 0;
            const RE::FormID innHoldID = innHold ? innHold->GetFormID() : 0;

            int score = (exactCenterMarker != 0) ? 10 : 1;
            if (homeSettlementID != 0 && innSettlementID == homeSettlementID) {
                score += 1000;
            } else if (loverSettlementID != 0 && innSettlementID == loverSettlementID) {
                score += 800;
            } else if (homeHoldID != 0 && innHoldID == homeHoldID) {
                score += 500;
            } else if (loverHoldID != 0 && innHoldID == loverHoldID) {
                score += 300;
            } else {
                score += 50;
            }

            if (score > bestScore) {
                bestScore = score;
                bestResult.innLocationID = cellLoc->GetFormID();
                bestResult.centerMarkerID = chosenMarker;
                bestResult.found = true;
                const std::string innName(rawInnName);
                const std::string settlementName =
                    (innSettlement && innSettlement != cellLoc && innSettlement->GetFullName())
                        ? innSettlement->GetFullName()
                        : "";
                if (!settlementName.empty() && innName.find(settlementName) == std::string::npos) {
                    bestResult.displayName = std::format("{} in {}", innName, settlementName);
                } else {
                    bestResult.displayName = innName;
                }
            }
        }

        return bestResult;
    }

    std::vector<Domain::Agency::NpcMatchmakingProfile> CollectWorldAdultMatchmakingProfiles(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        bool requireEligibleForScheduled) {
        std::vector<Domain::Agency::NpcMatchmakingProfile> profiles;
        const auto allEntries = SpatialWorldIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);
        profiles.reserve(allEntries.size());

        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) {
                continue;
            }
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None" || entry.isChild) {
                continue;
            }
            const RE::FormID actorRaw = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            if (!IsNpcEligibleWorldAdult(actorRaw, snap)) {
                continue;
            }
            const Foundation::ActorId actorId{ actorRaw };
            if (requireEligibleForScheduled) {
                if (!Domain::Agency::IsEligibleForScheduledMatchmaking(snap, actorId, currentGameDay)) {
                    continue;
                }
                if (entry.baseFormID != 0 && IsPlayerActiveMainPartnerSnap(snap, entry.baseFormID)) {
                    continue;
                }
            }

            Domain::Agency::NpcMatchmakingProfile p{};
            p.actorId = actorId;
            p.name = entry.nameDisplay;
            p.sex = (entry.sex == 0) ? Foundation::ActorSex::Male
                                     : ((entry.sex == 1) ? Foundation::ActorSex::Female : Foundation::ActorSex::Unknown);
            if (snap.actors) {
                if (const auto* rec = snap.actors->Find(actorId.AsRefId())) {
                    p.socialClass = rec->socialClass;
                    p.skillType = rec->skillType;
                    p.temperament = rec->temperament;
                }
            }
            profiles.push_back(std::move(p));
        }
        return profiles;
    }

    std::vector<ScoredAffairCandidate> CollectShortlistedParamours(
        const State::WorldStateSnapshot& snap,
        Foundation::ActorId partnerId,
        bool isOpenRelationshipLover,
        const ActorLocationContext& partnerLocCtx,
        float currentGameDay,
        Foundation::IRng& rng) {
        std::vector<ScoredAffairCandidate> shortlist;
        if (!snap.settings || !snap.relationships || partnerId.IsNull()) {
            return shortlist;
        }

        auto& idx = SpatialWorldIndex::GetSingleton();
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        const auto allEntries = idx.GetAllEntries(NPCFilter::AdultOnly);

        const Foundation::ActorSex partnerSex = ResolveActorSexForAgency(partnerId.raw());
        const auto orientation = snap.settings->matchmakingOrientation;
        const Foundation::SocialClass partnerSC = relUseCases.GetSocialClass(partnerId.raw());
        const Foundation::Temperament partnerTemp = relUseCases.GetTemperament(partnerId.raw());

        const float playerRelStartDayRaw =
            GetLatestNonBreakupStartDay(snap, partnerId, Foundation::ActorId::Player());
        const float playerRelStartDay = (playerRelStartDayRaw >= 0.0f) ? playerRelStartDayRaw : 0.0f;

        std::vector<ScoredAffairCandidate> tierBuckets[5];
        std::vector<ScoredAffairCandidate> fallbackPool;

        for (const auto& entry : allEntries) {
            const RE::FormID candRaw = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            if (candRaw == 0 || candRaw == kPlayerFormID || candRaw == partnerId.raw()) continue;
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None" || entry.isChild) continue;
            if (!IsNpcEligibleWorldAdult(candRaw, snap)) continue;

            const Foundation::ActorId candId{ candRaw };
            if (RE::Actor* candActor = idx.GetActorFromFormId(candRaw)) {
                if (candActor->IsDead() || candActor->IsDeleted() || candActor->IsDisabled()) continue;
                if (Utils::IsPlayerFollower(candActor)) continue;
            }
            if (Domain::Agency::IsNpcOnMatchmakingCooldown(snap, candId, currentGameDay)) continue;
            if (relUseCases.AreFamilyMembers(partnerId.raw(), candRaw)) continue;

            const Foundation::ActorSex candSex = (entry.sex == 0) ? Foundation::ActorSex::Male : Foundation::ActorSex::Female;
            if (!Domain::Agency::IsOrientationCompatible(orientation, partnerSex, candSex)) continue;

            // Gatekeeper #8: Co-spouses never count as cheating with each other
            if (!isOpenRelationshipLover && IsPlayerActiveMainPartnerSnap(snap, candRaw)) {
                continue;
            }

            std::string canFormReason;
            if (!relUseCases.CanFormRelationship(partnerId.raw(), candRaw, Foundation::RelationshipStatus::Lover, &canFormReason)) {
                continue;
            }

            // Anti-yo-yo: EndedConfronted -> hard exclude; EndedBreakup / post-player-rel breakup -> penalty + no Tier 1
            bool endedConfronted = false;
            int pastSideBreakups = 0;
            if (snap.affairs) {
                for (const auto& rec : snap.affairs->GetAllInvolving(partnerId)) {
                    if ((rec.cheater == partnerId && rec.paramour == candId) ||
                        (rec.cheater == candId && rec.paramour == partnerId)) {
                        if (rec.affairStatus == Foundation::AffairStatus::EndedConfronted) {
                            endedConfronted = true;
                            break;
                        }
                        if (rec.affairStatus == Foundation::AffairStatus::EndedBreakup) {
                            ++pastSideBreakups;
                        }
                    }
                }
            }
            if (endedConfronted) {
                continue;
            }

            int histSideBreakups = 0;
            if (const auto* candEdge = snap.relationships->FindEdge(partnerId, candId)) {
                for (const auto& hist : candEdge->statusHistory) {
                    if (Foundation::IsBreakupStatus(hist.toStatus) && hist.timestamp >= playerRelStartDay) {
                        ++histSideBreakups;
                    }
                }
            }
            pastSideBreakups = std::max(pastSideBreakups, histSideBreakups);

            const ActorLocationContext candLoc = ResolveActorLocationContext(candRaw, snap);
            const bool isOldFlame = HasPastMarasRelationship(snap, partnerId, candId) && (pastSideBreakups == 0);
            const std::string sharedFaction = GetSharedWorkOrGuildFactionName(
                partnerId.raw(), candRaw, partnerLocCtx.crimeFactionID, candLoc.crimeFactionID);

            bool sharesActiveCell = false;
            for (const RE::FormID cCell : candLoc.activeCells) {
                if (cCell != 0 && partnerLocCtx.activeCells.contains(cCell)) {
                    sharesActiveCell = true;
                    break;
                }
            }
            const bool samePlayerHouse =
                (partnerLocCtx.playerHouseID != 0 && partnerLocCtx.playerHouseID == candLoc.playerHouseID);
            const bool sameSubLocation =
                (partnerLocCtx.subLocationID != 0 && partnerLocCtx.subLocationID == candLoc.subLocationID);
            const bool sameSettlement =
                (partnerLocCtx.settlementLocationID != 0 && partnerLocCtx.settlementLocationID == candLoc.settlementLocationID);
            const bool sameHold =
                (partnerLocCtx.holdLocationID != 0 && partnerLocCtx.holdLocationID == candLoc.holdLocationID) ||
                (partnerLocCtx.crimeFactionID != 0 && partnerLocCtx.crimeFactionID == candLoc.crimeFactionID);

            int tier = 0;
            int baseScore = 0;
            std::string connectionTag;

            if (isOldFlame) {
                tier = 1;
                baseScore = 100;
                connectionTag = "[Ex-Partner / Former Flame]";
            } else if (samePlayerHouse || sharesActiveCell ||
                       (sameSubLocation && partnerLocCtx.subLocationID != partnerLocCtx.settlementLocationID)) {
                tier = 2;
                baseScore = 85;
                const std::string roofName = !partnerLocCtx.subLocationName.empty()
                                                 ? partnerLocCtx.subLocationName
                                                 : (!partnerLocCtx.activeCellName.empty() ? partnerLocCtx.activeCellName : partnerLocCtx.locationName);
                connectionTag = std::format("[Currently in the same building/household ({})]",
                                            roofName.empty() ? "Shared Household" : roofName);
            } else if (sameSubLocation || (!sharedFaction.empty() && (sameSettlement || sameHold))) {
                tier = 3;
                baseScore = 70;
                const std::string hubOrFac = !sharedFaction.empty()
                                                 ? sharedFaction
                                                 : (!partnerLocCtx.subLocationName.empty() ? partnerLocCtx.subLocationName : partnerLocCtx.locationName);
                connectionTag = std::format("[Close Friend / Fellow {} member]",
                                            hubOrFac.empty() ? "Circle" : hubOrFac);
            } else if (sameSettlement) {
                tier = 4;
                baseScore = 50;
                connectionTag = std::format("[Neighbor in {}]",
                                            !partnerLocCtx.settlementName.empty() ? partnerLocCtx.settlementName : "the same town");
            } else if (sameHold) {
                tier = 5;
                baseScore = 25;
                const std::string regionName = !partnerLocCtx.holdName.empty()
                                                   ? partnerLocCtx.holdName
                                                   : (!partnerLocCtx.locationName.empty() ? partnerLocCtx.locationName : "the same Hold");
                connectionTag = std::format("[In the same region ({})]", regionName);
            }

            int tieBreaker = -40 * pastSideBreakups;
            const bool candCanFormMain = !HasAnyMainPartner(snap, candId);
            const bool candHasOpenMain = snap.relationships->HasAnyOpenMainPair(candId);
            tieBreaker += (candCanFormMain || candHasOpenMain) ? 10 : -10;

            const Foundation::SocialClass candSC = relUseCases.GetSocialClass(candRaw);
            const Foundation::Temperament candTemp = relUseCases.GetTemperament(candRaw);
            const int scDiff = std::abs(static_cast<int>(partnerSC) - static_cast<int>(candSC));
            if (scDiff <= 1 ||
                (partnerTemp == Foundation::Temperament::Proud &&
                 (candSC == Foundation::SocialClass::Wealthy ||
                  candSC == Foundation::SocialClass::Nobles ||
                  candSC == Foundation::SocialClass::Rulers))) {
                tieBreaker += 5;
            }

            const int jitter = rng.NextInt(0, 8);

            ScoredAffairCandidate sc{};
            sc.formID = candRaw;
            sc.uuid = GetSkyrimNetGateway().GetActorUUID(candRaw);
            sc.name = entry.nameDisplay;
            sc.race = entry.raceName.empty() ? "Nord" : entry.raceName;
            sc.sex = (entry.sex == 1) ? "female" : "male";
            sc.socialClass = std::string(Foundation::SocialClassToString(candSC));
            sc.temperament = std::string(Foundation::TemperamentToString(candTemp));
            sc.eligibility = std::string(Domain::Agency::GetEligibilityCategoryString(candCanFormMain, candHasOpenMain));
            sc.locationName = !candLoc.locationName.empty() ? candLoc.locationName : "Skyrim";
            sc.tier = (tier > 0) ? tier : 5;
            sc.score = std::max(1, baseScore + tieBreaker);
            sc.jitteredScore = sc.score + jitter;
            sc.connectionTag = !connectionTag.empty() ? connectionTag : std::format("[Resident of {}]", sc.locationName);
            sc.reasonsSummary = sc.connectionTag;

            if (tier >= 1 && tier <= 5) {
                tierBuckets[tier - 1].push_back(std::move(sc));
            } else if (!candLoc.locationName.empty()) {
                fallbackPool.push_back(std::move(sc));
            }
        }

        auto sortBucket = [](std::vector<ScoredAffairCandidate>& b) {
            std::sort(b.begin(), b.end(), [](const ScoredAffairCandidate& a, const ScoredAffairCandidate& c) {
                if (a.jitteredScore != c.jitteredScore) return a.jitteredScore > c.jitteredScore;
                return a.formID < c.formID;
            });
        };
        for (auto& b : tierBuckets) {
            sortBucket(b);
        }
        sortBucket(fallbackPool);

        std::vector<ScoredAffairCandidate> overflow;
        auto takeFromTier = [&](std::vector<ScoredAffairCandidate>& src, std::size_t cap) {
            for (std::size_t i = 0; i < src.size(); ++i) {
                if (i < cap && shortlist.size() < 8) {
                    shortlist.push_back(src[i]);
                } else {
                    overflow.push_back(src[i]);
                }
            }
        };
        takeFromTier(tierBuckets[0], 2);
        takeFromTier(tierBuckets[1], 3);
        takeFromTier(tierBuckets[2], 2);
        takeFromTier(tierBuckets[3], 8);
        takeFromTier(tierBuckets[4], 8);

        if (shortlist.size() < 8 && !overflow.empty()) {
            sortBucket(overflow);
            for (const auto& extra : overflow) {
                if (shortlist.size() >= 8) break;
                shortlist.push_back(extra);
            }
        }
        if (shortlist.size() < 4 && !fallbackPool.empty()) {
            for (const auto& fb : fallbackPool) {
                if (shortlist.size() >= 6) break;
                shortlist.push_back(fb);
            }
        }

        return shortlist;
    }

    std::string BuildExistingPairsString(const State::WorldStateSnapshot& snap) {
        if (!snap.relationships) {
            return "None";
        }
        std::ostringstream ss;
        bool first = true;
        for (const auto& [key, edge] : snap.relationships->AllEdges()) {
            if (key.first.IsPlayer() || key.second.IsPlayer() ||
                IsPlayerActiveMainPartnerSnap(snap, key.first.raw()) ||
                IsPlayerActiveMainPartnerSnap(snap, key.second.raw())) {
                continue;
            }
            if (!Foundation::IsActivePartnerStatus(edge.status)) {
                continue;
            }
            const std::string nameA = ResolveActorNameForAgency(key.first.raw());
            const std::string nameB = ResolveActorNameForAgency(key.second.raw());
            if (nameA.empty() || nameA == "None" || nameB.empty() || nameB == "None") {
                continue;
            }

            const std::size_t childCount = CountSharedChildrenForPair(snap, key.first, key.second);
            const std::string statusStr = Foundation::StringUtils::ToLowerStd(
                Foundation::RelationshipStatusToString(edge.status));
            const char* openStr = edge.isOpen ? "open" : "exclusive";

            if (!first) ss << "\n";
            first = false;

            if (edge.status == Foundation::RelationshipStatus::Lover) {
                ss << std::format("- {} & {}: lover ({} {})",
                                  nameA, nameB, childCount, childCount == 1 ? "child" : "children");
            } else {
                ss << std::format("- {} & {}: {} ({}, {} {})",
                                  nameA, nameB, statusStr, openStr, childCount, childCount == 1 ? "child" : "children");
            }
        }
        const std::string out = ss.str();
        return out.empty() ? std::string("None") : out;
    }

    std::string BuildActorDetailedRelationshipsString(const State::WorldStateSnapshot& snap,
                                                      RE::FormID actorFormID) {
        if (!snap.relationships || actorFormID == 0) {
            return "None (single / no active relationships)";
        }
        const Foundation::ActorId actorId{ actorFormID };
        const bool hasOpenMain = snap.relationships->HasAnyOpenMainPair(actorId);
        const auto active = snap.relationships->GetActivePartners(actorId);

        std::ostringstream ss;
        bool first = true;
        for (const auto& [partnerId, status] : active) {
            const std::string partnerName = ResolveActorNameForAgency(partnerId.raw());
            if (partnerName.empty() || partnerName == "None") continue;

            const auto* edge = snap.relationships->FindEdge(actorId, partnerId);
            const bool isOpen = edge ? edge->isOpen : false;
            const std::size_t childCount = CountSharedChildrenForPair(snap, actorId, partnerId);
            const std::string statusStr = Foundation::StringUtils::ToLowerStd(
                Foundation::RelationshipStatusToString(status));

            if (!first) ss << "\n";
            first = false;

            if (status == Foundation::RelationshipStatus::Lover) {
                const char* loverPerm = hasOpenMain ? "allowed by open main relationship" : "cheating / secret infidelity";
                ss << std::format("- Lover with {} ({}, {} {})",
                                  partnerName, loverPerm, childCount, childCount == 1 ? "child" : "children");
            } else {
                const char* perm = isOpen ? "open primary partner" : "exclusive / closed primary partner";
                ss << std::format("- {} with {} ({}, {} {})",
                                  statusStr, partnerName, perm, childCount, childCount == 1 ? "child" : "children");
            }
        }
        const std::string out = ss.str();
        return out.empty() ? std::string("None (single / no active relationships)") : out;
    }

    nlohmann::json BuildScheduledMatchmakingContextJson(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        std::optional<std::size_t> maxSuggestionsOverride) {
        const auto& settings = *snap.settings;
        const auto allEntries = SpatialWorldIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);

        std::ostringstream npcsStream;
        std::size_t count = 0;
        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) {
                continue;
            }
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None" || entry.isChild) {
                continue;
            }
            const RE::FormID actorRaw = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            if (!IsNpcEligibleWorldAdult(actorRaw, snap)) {
                continue;
            }
            if (IsPlayerActiveMainPartnerSnap(snap, actorRaw) ||
                (entry.baseFormID != 0 && IsPlayerActiveMainPartnerSnap(snap, entry.baseFormID))) {
                continue;
            }
            if (Domain::Agency::IsNpcOnMatchmakingCooldown(snap, Foundation::ActorId{ actorRaw }, currentGameDay)) {
                continue;
            }
            if (count > 0) npcsStream << "\n";
            npcsStream << FormatNpcRosterLine(entry, snap);
            ++count;
        }

        const std::size_t maxSuggestions = maxSuggestionsOverride.value_or(
            static_cast<std::size_t>(std::max(1, settings.matchmakingMaxSuggestions)));

        nlohmann::json ctx;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["maxSuggestions"] = maxSuggestions;
        ctx["allowCheating"] = settings.matchmakingAllowCheating;
        ctx["allowBreakups"] = settings.matchmakingAllowBreakups;
        ctx["allowOpenClose"] = settings.matchmakingAllowOpenClose;
        ctx["orientationRule"] = std::string(Domain::Agency::GetOrientationRuleText(settings.matchmakingOrientation));
        ctx["cheatingRule"] = std::string(Domain::Agency::BuildCheatingRuleText(settings.matchmakingAllowCheating));
        ctx["breakupRule"] = std::string(Domain::Agency::BuildBreakupRuleText(settings.matchmakingAllowBreakups));
        ctx["openCloseRule"] = std::string(Domain::Agency::BuildOpenCloseRuleText(settings.matchmakingAllowOpenClose));
        ctx["allowedOperations"] = Domain::Agency::BuildAllowedOperationsText(
            settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose);
        ctx["allowedActions"] = Domain::Agency::BuildAllowedActionsString(
            settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose);
        ctx["npcsCount"] = count;
        ctx["npcsList"] = npcsStream.str();
        ctx["existingPairsList"] = BuildExistingPairsString(snap);
        return ctx;
    }

    nlohmann::json BuildManualMatchmakingContextJson(
        const State::WorldStateSnapshot& snap,
        RE::FormID targetNpcFormID,
        float currentGameDay) {
        const auto& settings = *snap.settings;
        const auto allEntries = SpatialWorldIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);

        const auto target = GetActorProfileSnapshot(targetNpcFormID);
        const Foundation::ActorId targetId{ targetNpcFormID };
        const bool canFormMain = !HasAnyMainPartner(snap, targetId);
        const bool hasOpenMain = snap.relationships && snap.relationships->HasAnyOpenMainPair(targetId);
        const auto targetEligibility = Domain::Agency::GetEligibilityCategoryString(canFormMain, hasOpenMain);

        std::ostringstream candStream;
        std::size_t candCount = 0;
        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) continue;
            if (entry.refFormID == targetNpcFormID || entry.baseFormID == targetNpcFormID) continue;
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None" || entry.isChild) continue;
            const RE::FormID candRaw = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            if (!IsNpcEligibleWorldAdult(candRaw, snap)) continue;

            const Foundation::ActorSex candSex = (entry.sex == 0) ? Foundation::ActorSex::Male : Foundation::ActorSex::Female;
            if (!Domain::Agency::IsOrientationCompatible(settings.matchmakingOrientation, target.sexEnum, candSex)) {
                continue;
            }
            if (candCount > 0) candStream << "\n";
            candStream << FormatNpcRosterLine(entry, snap);
            ++candCount;
        }

        const auto targetOps = BuildTargetAllowedOperations(
            snap,
            targetNpcFormID,
            settings.matchmakingAllowBreakups,
            settings.matchmakingAllowOpenClose,
            settings.matchmakingAllowCheating);

        nlohmann::json ctx;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["maxSuggestions"] = std::max(1, settings.matchmakingMaxSuggestions);
        ctx["allowCheating"] = settings.matchmakingAllowCheating;
        ctx["allowBreakups"] = settings.matchmakingAllowBreakups;
        ctx["allowOpenClose"] = settings.matchmakingAllowOpenClose;
        ctx["targetName"] = target.name;
        ctx["targetSex"] = target.sex;
        ctx["targetRace"] = target.race;
        ctx["targetMatchUUID"] = target.uuid;
        ctx["targetEligibility"] = std::string(targetEligibility);
        ctx["targetRelationshipsList"] = BuildActorDetailedRelationshipsString(snap, targetNpcFormID);
        ctx["orientationRule"] = std::string(Domain::Agency::GetOrientationRuleText(settings.matchmakingOrientation));
        ctx["cheatingRule"] = std::string(Domain::Agency::BuildCheatingRuleText(settings.matchmakingAllowCheating));
        ctx["breakupRule"] = std::string(Domain::Agency::BuildBreakupRuleText(settings.matchmakingAllowBreakups));
        ctx["openCloseRule"] = std::string(Domain::Agency::BuildOpenCloseRuleText(settings.matchmakingAllowOpenClose));
        ctx["allowedOperations"] = targetOps.operationsText;
        ctx["allowedActions"] = targetOps.actions;
        ctx["allowedStatuses"] = targetOps.statuses;
        ctx["candidatesCount"] = candCount;
        ctx["candidatesList"] = candStream.str();
        ctx["existingPairsList"] = BuildExistingPairsString(snap);
        return ctx;
    }

    nlohmann::json BuildVerifyContextJson(
        const MatchmakingResult& cand,
        const State::WorldStateSnapshot& snap,
        float currentGameDay) {
        const auto pA = GetActorProfileSnapshot(cand.actorAFormID);
        const auto pB = GetActorProfileSnapshot(cand.actorBFormID);
        auto& skyrimNet = GetSkyrimNetGateway();

        // M8: Do NOT populate committedPartnerA/B when the primary partner IS the other actor in the pair.
        std::uint64_t committedAUuid = 0;
        std::string committedAName;
        if (snap.relationships) {
            const auto primaryA = snap.relationships->GetPrimaryPartner(Foundation::ActorId{ cand.actorAFormID });
            if (!primaryA.IsNull() && primaryA.raw() != cand.actorBFormID) {
                committedAUuid = skyrimNet.GetActorUUID(primaryA.raw());
                committedAName = ResolveActorNameForAgency(primaryA.raw());
            }
        }

        std::uint64_t committedBUuid = 0;
        std::string committedBName;
        if (snap.relationships) {
            const auto primaryB = snap.relationships->GetPrimaryPartner(Foundation::ActorId{ cand.actorBFormID });
            if (!primaryB.IsNull() && primaryB.raw() != cand.actorAFormID) {
                committedBUuid = skyrimNet.GetActorUUID(primaryB.raw());
                committedBName = ResolveActorNameForAgency(primaryB.raw());
            }
        }

        nlohmann::json ctx;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["actorAName"] = pA.name;
        ctx["actorAFormID"] = cand.actorAFormID;
        ctx["actorAUUID"] = pA.uuid;
        ctx["actorARace"] = pA.race;
        ctx["actorASex"] = pA.sex;
        ctx["actorARelationships"] = BuildActorDetailedRelationshipsString(snap, cand.actorAFormID);

        ctx["actorBName"] = pB.name;
        ctx["actorBFormID"] = cand.actorBFormID;
        ctx["actorBUUID"] = pB.uuid;
        ctx["actorBRace"] = pB.race;
        ctx["actorBSex"] = pB.sex;
        ctx["actorBRelationships"] = BuildActorDetailedRelationshipsString(snap, cand.actorBFormID);

        ctx["committedPartnerAName"] = committedAName;
        ctx["committedPartnerAUUID"] = committedAUuid;
        ctx["committedPartnerBName"] = committedBName;
        ctx["committedPartnerBUUID"] = committedBUuid;

        ctx["action"] = cand.action.empty() ? std::string("update relationship") : cand.action;
        ctx["targetStatus"] = cand.targetStatus.empty() ? std::string("dating") : cand.targetStatus;
        ctx["isCheating"] = cand.isCheating;

        nlohmann::json recentParticipants = nlohmann::json::array();
        auto appendUniqueUuid = [&](std::uint64_t uuid) {
            if (uuid == 0) return;
            for (const auto& existing : recentParticipants) {
                if (existing.is_number_unsigned() && existing.get<std::uint64_t>() == uuid) return;
            }
            recentParticipants.push_back(uuid);
        };
        appendUniqueUuid(pA.uuid);
        appendUniqueUuid(pB.uuid);
        appendUniqueUuid(committedAUuid);
        appendUniqueUuid(committedBUuid);
        ctx["recent_events_participants"] = std::move(recentParticipants);
        return ctx;
    }

    nlohmann::json BuildPartnerAgencyContextJson(
        const State::WorldStateSnapshot& snap,
        const PartnerAgencyContextInput& input,
        float currentGameDay) {
        auto& skyrimNet = GetSkyrimNetGateway();
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        const auto pSnap = GetActorProfileSnapshot(input.partnerID);
        const std::string playerName = GetPlayerNameSafe();
        const Foundation::ActorId partnerId{ input.partnerID };

        // H1.1: Compute playerCheatedOnPartner & playerAffairPartnerName ONLY from discovered ongoing affairs against partnerId!
        bool playerCheatedOnPartner = false;
        std::string playerAffairPartnerName;
        if (snap.affairs) {
            for (const auto& rec : snap.affairs->GetByCheater(Foundation::ActorId::Player())) {
                if (rec.deceivedPartner == partnerId &&
                    rec.affairStatus == Foundation::AffairStatus::Ongoing &&
                    !rec.isSecret) {
                    playerCheatedOnPartner = true;
                    if (!rec.paramour.IsNull()) {
                        playerAffairPartnerName = ResolveActorNameForAgency(rec.paramour.raw());
                    }
                    break;
                }
            }
        }

        // C1 + H6: Format shortlisted candidates with key ("c_1"...) & sanitized single-line criterion
        std::ostringstream candStream;
        nlohmann::json candidatesArr = nlohmann::json::array();
        for (std::size_t ci = 0; ci < input.shortlist.size(); ++ci) {
            const auto& c = input.shortlist[ci];
            if (ci > 0) candStream << "\n";
            candStream << std::format("{}. {} ({} {}) - {} | Location: {} | Status: [{}]",
                                      ci + 1, c.name, c.race, c.sex,
                                      c.connectionTag, c.locationName, c.eligibility);

            nlohmann::json cObj;
            cObj["key"] = std::format("c_{}", ci + 1);
            cObj["criterion"] = skyrimNet.SanitizeSingleLine(
                std::format("{} chooses {} ({} {}, {}, Location: {}, Status: {})",
                            input.partnerName, c.name, c.race, c.sex,
                            c.connectionTag, c.locationName, c.eligibility));
            cObj["name"] = c.name;
            cObj["uuid"] = c.uuid;
            cObj["race"] = c.race;
            cObj["sex"] = c.sex;
            cObj["socialClass"] = c.socialClass;
            cObj["temperament"] = c.temperament;
            cObj["location"] = c.locationName;
            cObj["connectionTag"] = c.connectionTag;
            cObj["eligibility"] = c.eligibility;
            cObj["score"] = c.score;
            candidatesArr.push_back(std::move(cObj));
        }

        const std::string hierDesc = (input.hierarchyRank >= 0)
                                         ? std::format("Rank {} Spouse", input.hierarchyRank + 1)
                                         : "Unranked / Non-Polygamous";
        const std::string relStatusLower = Foundation::StringUtils::ToLowerStd(
            Foundation::RelationshipStatusToString(input.statusWithPlayer));
        const std::string locStr = input.locCtx.locationName.empty() ? "Skyrim" : input.locCtx.locationName;
        const int daysSinceInt = static_cast<int>(std::round(input.daysSinceInteraction));
        const std::string affTier = GetAffectionTierLabel(input.affection);

        const float baseL = std::max(1.0f, snap.settings ? snap.settings->GetLonelinessDecayStartDays() : 2.0f);
        const float absenceRatio = input.daysSinceInteraction / baseL;
        std::string playerPresenceHint;
        if (absenceRatio <= 0.25f) {
            playerPresenceHint = std::format("{} spent time with {} just now; they were together very recently.", playerName, input.partnerName);
        } else if (absenceRatio <= 0.85f) {
            playerPresenceHint = std::format("{} spent time with {} recently and has been attentive.", playerName, input.partnerName);
        } else if (absenceRatio <= 1.50f) {
            playerPresenceHint = std::format("{} left recently and has been away for a little while.", playerName);
        } else if (absenceRatio <= 2.50f) {
            playerPresenceHint = std::format("{} has been gone for a while and hasn't come back to spend time with {}.", playerName, input.partnerName);
        } else if (absenceRatio <= 4.00f) {
            playerPresenceHint = std::format("{} has not returned or spoken to {} for a long time.", playerName, input.partnerName);
        } else if (absenceRatio <= 6.50f) {
            playerPresenceHint = std::format("{} left a very long time ago and has barely been present in {}'s life.", playerName, input.partnerName);
        } else {
            playerPresenceHint = std::format("It feels like forever since {} last came back or spoke to {}.", playerName, input.partnerName);
        }

        const float flingRatio = input.sideLoverAgeDays / baseL;
        const std::string sideLoverDurationHint =
            (flingRatio <= 2.0f)
                ? "started recently"
                : ((flingRatio <= 4.5f) ? "has been going on for some time" : "has been going on for a long time");

        nlohmann::json ctx;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["playerName"] = playerName;
        ctx["partnerUUID"] = pSnap.uuid;
        ctx["partnerName"] = input.partnerName;
        ctx["partnerRace"] = pSnap.race;
        ctx["partnerSex"] = pSnap.sex;
        ctx["partnerStatusWithPlayer"] = relStatusLower;
        ctx["playerRelationshipStatus"] = relStatusLower;
        ctx["partnerTemperament"] = std::string(Foundation::TemperamentToString(relUseCases.GetTemperament(input.partnerID)));
        ctx["partnerSocialClass"] = std::string(Foundation::SocialClassToString(relUseCases.GetSocialClass(input.partnerID)));
        ctx["partnerLocation"] = locStr;
        ctx["partnerCurrentLocation"] = locStr;
        ctx["partnerAffection"] = input.affection;
        ctx["affectionScore"] = input.affection;
        ctx["partnerAffectionTier"] = affTier;
        ctx["affectionTier"] = affTier;
        ctx["daysSincePlayerInteraction"] = daysSinceInt;
        ctx["daysSinceLastVisit"] = daysSinceInt;
        ctx["playerPresenceHint"] = playerPresenceHint;
        ctx["sideLoverDurationHint"] = sideLoverDurationHint;
        ctx["hierarchyRank"] = hierDesc;
        ctx["hierarchyRankDescription"] = hierDesc;
        ctx["sharedChildrenCount"] = input.sharedChildrenCount;
        ctx["playerCheatedOnPartner"] = playerCheatedOnPartner;
        ctx["playerAffairPartnerName"] = playerAffairPartnerName;
        ctx["temptationChance"] = static_cast<int>(std::round(input.finalChance));
        ctx["temptationDrivers"] = input.driversSummary;
        ctx["isOpenRelationship"] = input.isOpenRelationshipLover;
        ctx["evalMode"] = input.evalMode;
        ctx["canConfess"] = input.canConfess;

        std::uint64_t sideLoverUuid = 0;
        if (input.sideLoverID != 0) {
            const auto sSnap = GetActorProfileSnapshot(input.sideLoverID);
            const auto sLoc = ResolveActorLocationContext(input.sideLoverID, snap);
            sideLoverUuid = sSnap.uuid;
            ctx["sideLoverUUID"] = sSnap.uuid;
            ctx["sideLoverName"] = input.sideLoverName;
            ctx["sideLoverRace"] = sSnap.race;
            ctx["sideLoverSex"] = sSnap.sex;
            ctx["sideLoverSocialClass"] = std::string(Foundation::SocialClassToString(relUseCases.GetSocialClass(input.sideLoverID)));
            ctx["sideLoverTemperament"] = std::string(Foundation::TemperamentToString(relUseCases.GetTemperament(input.sideLoverID)));
            ctx["sideLoverLocation"] = sLoc.locationName.empty() ? std::string("Skyrim") : sLoc.locationName;
            ctx["sideLoverAgeDays"] = static_cast<int>(std::round(input.sideLoverAgeDays));
            ctx["affairStage"] = (input.sideAffairStage == Foundation::AffairStage::Talking) ? "talking" : "physical";
        } else {
            ctx["sideLoverUUID"] = std::uint64_t{ 0 };
            ctx["sideLoverName"] = "";
            ctx["sideLoverRace"] = "";
            ctx["sideLoverSex"] = "";
            ctx["sideLoverLocation"] = "Skyrim";
            ctx["affairStage"] = input.isOpenRelationshipLover ? "open_lover" : "talking";
        }

        ctx["partnerRelationships"] = BuildActorDetailedRelationshipsString(snap, input.partnerID);
        ctx["candidatesCount"] = input.shortlist.size();
        ctx["candidatesList"] = candStream.str();
        ctx["candidates"] = std::move(candidatesArr);

        nlohmann::json recentParticipants = nlohmann::json::array();
        auto appendUniqueUuid = [&](std::uint64_t uuid) {
            if (uuid == 0) return;
            for (const auto& existing : recentParticipants) {
                if (existing.is_number_unsigned() && existing.get<std::uint64_t>() == uuid) return;
            }
            recentParticipants.push_back(uuid);
        };
        appendUniqueUuid(pSnap.uuid);
        appendUniqueUuid(skyrimNet.GetActorUUID(kPlayerFormID));
        if (input.sideLoverID != 0) {
            appendUniqueUuid(sideLoverUuid);
        } else {
            for (const auto& c : input.shortlist) {
                appendUniqueUuid(c.uuid);
            }
        }
        ctx["recent_events_participants"] = std::move(recentParticipants);
        return ctx;
    }

}  // namespace MARAS::App
