#include "core/DeceasedRegistry.h"
#include "core/AdultChildProgression.h"
#include "core/NPCIndex.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PlayerChildrenService.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"

namespace MARAS {

    DeceasedRegistry& DeceasedRegistry::GetSingleton() {
        static DeceasedRegistry instance;
        return instance;
    }

    namespace {
        bool IsDynamicActorRefID(RE::FormID id) noexcept {
            return id != 0 && !IsSyntheticChildID(id) && id != kUnknownFatherFormID &&
                   ((id & 0xFF000000) == 0xFF000000);
        }

        RE::FormID ResolveStableStorageID(RE::FormID id) {
            if (id == 0 || id == kPlayerFormID || id == kUnknownFatherFormID || IsSyntheticChildID(id)) {
                return id;
            }
            if (id == kPlayerBaseID) {
                return kPlayerFormID;
            }
            // Dynamically spawned 0xFF actors (FMR/BF children, graduated Stage 4 adult children)
            // share template TESNPC forms; their 0xFF Reference FormID is their unique identity.
            if (IsDynamicActorRefID(id) || AdultChildProgression::GetSingleton().GetOriginalAdultBase(id) != nullptr) {
                return id;
            }
            RE::FormID baseID = NPCIndex::GetSingleton().GetBaseID(id);
            return baseID != 0 ? baseID : id;
        }

        DeceasedChildInfo BuildDeceasedChildInfo(const ChildRegistryEntry& entry, RE::FormID refID) {
            DeceasedChildInfo cInfo;
            cInfo.guardianARefID = entry.guardianAID;
            cInfo.guardianABaseID = ResolveStableStorageID(entry.guardianAID);
            cInfo.guardianAName = (entry.guardianAID == kPlayerFormID) ? "Player" : ActorSvc::GetName(entry.guardianAID);

            cInfo.guardianBRefID = entry.guardianBID;
            cInfo.guardianBBaseID = ResolveStableStorageID(entry.guardianBID);
            cInfo.guardianBName = (entry.guardianBID == kPlayerFormID) ? "Player" : ActorSvc::GetName(entry.guardianBID);

            cInfo.bioMotherBaseID = ResolveStableStorageID(entry.bioMotherID);
            cInfo.bioFatherBaseID = ResolveStableStorageID(entry.bioFatherID);
            cInfo.isAdopted = entry.isAdopted;

            if (auto state = PlayerChildrenService::GetSingleton().GetState(refID)) {
                cInfo.houseFormID = state->currentHouseFormID;
            }
            return cInfo;
        }

        bool IsRomanticOrExStatus(RelationshipStatus st) {
            return st == RelationshipStatus::Married ||
                   st == RelationshipStatus::Engaged ||
                   st == RelationshipStatus::Dating ||
                   st == RelationshipStatus::Lover ||
                   st == RelationshipStatus::Divorced ||
                   st == RelationshipStatus::Jilted ||
                   st == RelationshipStatus::ExDating ||
                   st == RelationshipStatus::ExLover;
        }

        bool IsExPartnerStatus(RelationshipStatus st) {
            return st == RelationshipStatus::Divorced ||
                   st == RelationshipStatus::Jilted ||
                   st == RelationshipStatus::ExDating ||
                   st == RelationshipStatus::ExLover;
        }

        bool MatchesChildParentOrGuardian(const DeceasedChildInfo& cInfo, RE::FormID parentFormID,
                                          RE::FormID baseID, RE::FormID refID) {
            if (parentFormID == 0 || parentFormID == kUnknownFatherFormID) return false;

            if (parentFormID == kPlayerFormID || parentFormID == kPlayerBaseID || baseID == kPlayerFormID) {
                return cInfo.guardianARefID == kPlayerFormID || cInfo.guardianBRefID == kPlayerFormID ||
                       cInfo.guardianABaseID == kPlayerFormID || cInfo.guardianBBaseID == kPlayerFormID ||
                       cInfo.bioMotherBaseID == kPlayerFormID || cInfo.bioFatherBaseID == kPlayerFormID ||
                       cInfo.bioMotherBaseID == kPlayerBaseID || cInfo.bioFatherBaseID == kPlayerBaseID;
            }

            auto matchesID = [&](RE::FormID storedID) {
                if (storedID == 0 || storedID == kUnknownFatherFormID) return false;
                if (storedID == parentFormID) return true;
                if (refID != 0 && storedID == refID) return true;
                if (baseID != 0 && storedID == baseID) return true;
                return false;
            };

            return matchesID(cInfo.guardianARefID) || matchesID(cInfo.guardianBRefID) ||
                   matchesID(cInfo.guardianABaseID) || matchesID(cInfo.guardianBBaseID) ||
                   matchesID(cInfo.bioMotherBaseID) || matchesID(cInfo.bioFatherBaseID);
        }

        const State::DeceasedActorRecord* FindDeceasedStateRecord(
            const State::DeceasedTable& table,
            RE::FormID formID) {
            if (formID == 0) return nullptr;
            RE::FormID baseID = 0;
            if (!IsDynamicActorRefID(formID) &&
                AdultChildProgression::GetSingleton().GetOriginalAdultBase(formID) == nullptr) {
                baseID = NPCIndex::GetSingleton().GetBaseID(formID);
            }
            return table.FindByActor(State::ActorId{ formID }, State::BaseId{ baseID });
        }

        bool MatchesPartnerStatusInDeceasedRecord(
            const DeceasedNPCRecord& rec,
            RE::FormID partnerFormID,
            RE::FormID pBaseID,
            RE::FormID pRefID,
            bool (*statusPred)(RelationshipStatus)) {
            if (rec.isChild) return false;
            for (const auto& p : rec.partnersAtDeath) {
                if (!statusPred(p.statusAtDeath)) continue;
                if (p.isPlayer && (partnerFormID == kPlayerFormID || pBaseID == kPlayerFormID)) return true;
                if (p.partnerRefID != 0 && (p.partnerRefID == pRefID || p.partnerRefID == partnerFormID)) return true;
                if (p.partnerBaseID != 0 && (p.partnerBaseID == pBaseID || p.partnerBaseID == partnerFormID)) return true;
            }
            return false;
        }
    }  // namespace

    void DeceasedRegistry::ResolveFormIDs(RE::FormID inputFormID, RE::FormID& outBaseID, RE::FormID& outRefID) const {
        if (inputFormID == 0 || inputFormID == kPlayerFormID || inputFormID == kPlayerBaseID ||
            inputFormID == kUnknownFatherFormID || IsSyntheticChildID(inputFormID)) {
            outBaseID = (inputFormID == kPlayerBaseID) ? kPlayerFormID : inputFormID;
            outRefID = outBaseID;
            return;
        }

        if (IsDynamicActorRefID(inputFormID) ||
            AdultChildProgression::GetSingleton().GetOriginalAdultBase(inputFormID) != nullptr) {
            outBaseID = inputFormID;
            outRefID = inputFormID;
            return;
        }

        outRefID = inputFormID;
        outBaseID = NPCIndex::GetSingleton().GetBaseID(inputFormID);
        if (outBaseID == 0) {
            outBaseID = inputFormID;
            outRefID = NPCIndex::GetSingleton().GetRefID(inputFormID);
        }
    }

    bool DeceasedRegistry::RecordDeceasedAdult(RE::Actor* victim, bool killedByPlayer, RE::Actor* killer,
                                               const ChildRegistryEntry* adultChildEntry) {
        if (!victim) return false;

        RE::FormID refID = victim->GetFormID();
        const bool isDynamicActor =
            IsDynamicActorRefID(refID) || AdultChildProgression::GetSingleton().GetOriginalAdultBase(refID) != nullptr;

        RE::FormID baseID = 0;
        if (isDynamicActor) {
            baseID = refID;
        } else if (auto* actorBase = victim->GetActorBase()) {
            baseID = actorBase->GetFormID();
        } else {
            baseID = NPCIndex::GetSingleton().GetBaseID(refID);
        }
        if (baseID == 0) baseID = refID;

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const bool isTrackedAdult =
            relMgr.IsRegistered(refID) || (!isDynamicActor && baseID != 0 && relMgr.IsRegistered(baseID));
        if (!isTrackedAdult && !adultChildEntry) {
            MARAS_LOG_DEBUG("RecordDeceasedAdult: Actor {:08X} (base {:08X}) is not registered in MARAS, skipping", refID, baseID);
            return false;
        }

        if (State::WorldStore::GetSingleton().Snapshot()->deceased->FindByActor(
                State::ActorId{ refID }, State::BaseId{ baseID }) != nullptr) {
            MARAS_LOG_WARN("RecordDeceasedAdult: Actor {:08X} (key {:08X}) already in DeceasedRegistry", refID, baseID);
            return false;
        }

        DeceasedNPCRecord record;
        record.baseFormID = baseID;
        record.refFormID = refID;
        record.name = (adultChildEntry && !adultChildEntry->name.empty()) ? adultChildEntry->name : victim->GetDisplayFullName();
        if (record.name.empty()) record.name = victim->GetName();

        if (auto* calendar = RE::Calendar::GetSingleton()) {
            record.deathTimestamp = calendar->GetDaysPassed();
        }
        record.isChild = false;
        record.wasKilledByPlayer = killedByPlayer;
        if (killer) {
            record.killerRefID = killer->GetFormID();
            if (IsDynamicActorRefID(record.killerRefID)) {
                record.killerBaseID = record.killerRefID;
            } else if (auto* kBase = killer->GetActorBase()) {
                record.killerBaseID = kBase->GetFormID();
            }
            record.killerName = killer->GetDisplayFullName();
            if (record.killerName.empty()) record.killerName = killer->GetName();
        }

        if (auto profile = relMgr.GetNPCProfile(refID)) {
            record.socialClass = profile->socialClass;
            record.skillType = profile->skillType;
            record.temperament = profile->temperament;
        } else if (!isDynamicActor && baseID != refID) {
            if (auto profileBase = relMgr.GetNPCProfile(baseID)) {
                record.socialClass = profileBase->socialClass;
                record.skillType = profileBase->skillType;
                record.temperament = profileBase->temperament;
            }
        }

        // Capture snapshot of all romantic and ex-partner relationships before unregistering
        std::vector<std::pair<RE::FormID, RelationshipStatus>> relationshipsToSnapshot;
        std::unordered_set<RE::FormID> seenPartners;
        for (const auto& [key, data] : relMgr.GetAllRelationshipPairs()) {
            RE::FormID partnerID = 0;
            if (key.Contains(refID)) {
                partnerID = key.GetOther(refID);
            } else if (!isDynamicActor && baseID != 0 && baseID != refID && key.Contains(baseID)) {
                partnerID = key.GetOther(baseID);
            } else {
                continue;
            }
            if (partnerID == 0 || !IsRomanticOrExStatus(data.status)) continue;
            if (seenPartners.insert(partnerID).second) {
                relationshipsToSnapshot.emplace_back(partnerID, data.status);
            }
        }

        for (const auto& [partnerID, status] : relationshipsToSnapshot) {
            bool isPlayer = (partnerID == kPlayerFormID);
            RE::FormID partnerRefID = isPlayer ? kPlayerFormID : partnerID;
            RE::FormID partnerBaseID = isPlayer ? kPlayerFormID : 0;
            std::string partnerName;

            if (isPlayer) {
                partnerName = "Player";
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    partnerName = player->GetName();
                }
            } else {
                partnerBaseID = ResolveStableStorageID(partnerID);
                partnerName = ActorSvc::GetName(partnerID);
            }

            record.partnersAtDeath.emplace_back(partnerBaseID, partnerRefID, partnerName, status, isPlayer);
            MARAS_LOG_INFO("RecordDeceasedAdult: Snapshot partner {:08X} ('{}') with status {}",
                           partnerRefID, partnerName, static_cast<int>(status));
        }

        if (adultChildEntry) {
            record.childInfo = BuildDeceasedChildInfo(*adultChildEntry, refID);
        }

        const State::DeceasedActorRecord stateRec = LegacyAdapters::ToDeceasedActorRecord(record);
        bool inserted = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (ws.deceased.Read().FindByActor(State::ActorId{ refID }, State::BaseId{ baseID }) == nullptr) {
                ws.deceased.Write().UpsertRecord(stateRec);
                inserted = true;
            }
        });
        if (!inserted) {
            MARAS_LOG_WARN("RecordDeceasedAdult: Actor {:08X} (key {:08X}) already in DeceasedRegistry", refID, baseID);
            return false;
        }

        MARAS_LOG_INFO("RecordDeceasedAdult: Archived deceased adult {:08X} ('{}', key {:08X}) with {} partner bonds (hasChildLineage={})",
                       refID, record.name, baseID, record.partnersAtDeath.size(), record.childInfo.has_value());
        return true;
    }

    bool DeceasedRegistry::RecordDeceasedChild(RE::Actor* child, const ChildRegistryEntry& entry, bool killedByPlayer, RE::Actor* killer) {
        if (!child) return false;

        RE::FormID refID = child->GetFormID();
        const bool isDynamicActor =
            IsDynamicActorRefID(refID) || AdultChildProgression::GetSingleton().GetOriginalAdultBase(refID) != nullptr;

        RE::FormID baseID = 0;
        if (isDynamicActor) {
            baseID = refID;
        } else if (auto* actorBase = child->GetActorBase()) {
            baseID = actorBase->GetFormID();
        } else {
            baseID = NPCIndex::GetSingleton().GetBaseID(refID);
        }
        if (baseID == 0) baseID = refID;

        if (State::WorldStore::GetSingleton().Snapshot()->deceased->FindByActor(
                State::ActorId{ refID }, State::BaseId{ baseID }) != nullptr) {
            MARAS_LOG_WARN("RecordDeceasedChild: Child {:08X} (key {:08X}) already in DeceasedRegistry", refID, baseID);
            return false;
        }

        DeceasedNPCRecord record;
        record.baseFormID = baseID;
        record.refFormID = refID;
        record.name = entry.name.empty() ? child->GetDisplayFullName() : entry.name;
        if (record.name.empty()) record.name = child->GetName();

        if (auto* calendar = RE::Calendar::GetSingleton()) {
            record.deathTimestamp = calendar->GetDaysPassed();
        }
        record.isChild = true;
        record.wasKilledByPlayer = killedByPlayer;
        if (killer) {
            record.killerRefID = killer->GetFormID();
            if (IsDynamicActorRefID(record.killerRefID)) {
                record.killerBaseID = record.killerRefID;
            } else if (auto* kBase = killer->GetActorBase()) {
                record.killerBaseID = kBase->GetFormID();
            }
            record.killerName = killer->GetDisplayFullName();
            if (record.killerName.empty()) record.killerName = killer->GetName();
        }

        record.childInfo = BuildDeceasedChildInfo(entry, refID);
        const State::DeceasedActorRecord stateRec = LegacyAdapters::ToDeceasedActorRecord(record);
        bool inserted = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (ws.deceased.Read().FindByActor(State::ActorId{ refID }, State::BaseId{ baseID }) == nullptr) {
                ws.deceased.Write().UpsertRecord(stateRec);
                inserted = true;
            }
        });
        if (!inserted) {
            MARAS_LOG_WARN("RecordDeceasedChild: Child {:08X} (key {:08X}) already in DeceasedRegistry", refID, baseID);
            return false;
        }

        MARAS_LOG_INFO("RecordDeceasedChild: Archived deceased child {:08X} ('{}', key {:08X})",
                       refID, record.name, baseID);
        return true;
    }

    bool DeceasedRegistry::IsDeceased(RE::FormID formID) const {
        if (formID == 0) return false;
        RE::FormID baseID = 0;
        if (!IsDynamicActorRefID(formID) &&
            AdultChildProgression::GetSingleton().GetOriginalAdultBase(formID) == nullptr) {
            baseID = NPCIndex::GetSingleton().GetBaseID(formID);
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->deceased->IsDeceased(State::ActorId{ formID }, State::BaseId{ baseID });
    }

    bool DeceasedRegistry::IsWidowOrWidower(RE::FormID actorFormID) const {
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            const DeceasedNPCRecord record = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (MatchesPartnerStatusInDeceasedRecord(
                    record, actorFormID, baseID, refID,
                    [](RelationshipStatus st) { return st == RelationshipStatus::Married; })) {
                return true;
            }
        }
        return false;
    }

    bool DeceasedRegistry::WasMarriedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        const auto recOpt = FindRecord(deceasedFormID);
        if (!recOpt.has_value()) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);
        return MatchesPartnerStatusInDeceasedRecord(
            *recOpt, partnerFormID, pBaseID, pRefID,
            [](RelationshipStatus st) { return st == RelationshipStatus::Married; });
    }

    bool DeceasedRegistry::WasEngagedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        const auto recOpt = FindRecord(deceasedFormID);
        if (!recOpt.has_value()) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);
        return MatchesPartnerStatusInDeceasedRecord(
            *recOpt, partnerFormID, pBaseID, pRefID,
            [](RelationshipStatus st) { return st == RelationshipStatus::Engaged; });
    }

    bool DeceasedRegistry::WasExOf(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        const auto recOpt = FindRecord(deceasedFormID);
        if (!recOpt.has_value()) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);
        return MatchesPartnerStatusInDeceasedRecord(
            *recOpt, partnerFormID, pBaseID, pRefID, IsExPartnerStatus);
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedSpousesOf(RE::FormID actorFormID) const {
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            DeceasedNPCRecord record = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (MatchesPartnerStatusInDeceasedRecord(
                    record, actorFormID, baseID, refID,
                    [](RelationshipStatus st) { return st == RelationshipStatus::Married; })) {
                result.push_back(std::move(record));
            }
        }
        return result;
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedPartnersOf(RE::FormID actorFormID) const {
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            DeceasedNPCRecord record = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (MatchesPartnerStatusInDeceasedRecord(
                    record, actorFormID, baseID, refID, IsRomanticOrExStatus)) {
                result.push_back(std::move(record));
            }
        }
        return result;
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedChildrenOf(RE::FormID parentFormID) const {
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(parentFormID, baseID, refID);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            DeceasedNPCRecord record = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (!record.childInfo.has_value()) continue;
            if (MatchesChildParentOrGuardian(record.childInfo.value(), parentFormID, baseID, refID)) {
                result.push_back(std::move(record));
            }
        }
        return result;
    }

    bool DeceasedRegistry::HasDeceasedChildren(RE::FormID parentFormID) const {
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(parentFormID, baseID, refID);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            const DeceasedNPCRecord record = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (!record.childInfo.has_value()) continue;
            if (MatchesChildParentOrGuardian(record.childInfo.value(), parentFormID, baseID, refID)) {
                return true;
            }
        }
        return false;
    }

    std::optional<DeceasedNPCRecord> DeceasedRegistry::FindRecord(RE::FormID formID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = FindDeceasedStateRecord(*snap->deceased, formID)) {
            return LegacyAdapters::ToLegacyDeceasedRecord(*rec);
        }
        return std::nullopt;
    }

    std::unordered_map<RE::FormID, DeceasedNPCRecord> DeceasedRegistry::GetAllRecords() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<RE::FormID, DeceasedNPCRecord> result;
        for (const auto& [key, stateRec] : snap->deceased->AllRecords()) {
            DeceasedNPCRecord rec = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            if (rec.baseFormID != 0) {
                result[rec.baseFormID] = std::move(rec);
            }
        }
        return result;
    }

    std::size_t DeceasedRegistry::GetDeceasedCount() const {
        return State::WorldStore::GetSingleton().Snapshot()->deceased->Size();
    }

    void DeceasedRegistry::Revert() {
        MARAS_LOG_INFO("DeceasedRegistry reverted to clean state");
    }

    nlohmann::json DeceasedRegistry::DumpJson(const Projection::PerspectiveView& view) const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [key, stateRec] : view.Deceased().AllRecords()) {
            const DeceasedNPCRecord rec = LegacyAdapters::ToLegacyDeceasedRecord(stateRec);
            nlohmann::json r;
            r["baseFormId"] = std::format("{:08X}", rec.baseFormID);
            r["refFormId"] = std::format("{:08X}", rec.refFormID);
            r["name"] = rec.name;
            r["deathTimestamp"] = rec.deathTimestamp;
            r["isChild"] = rec.isChild;
            r["wasKilledByPlayer"] = rec.wasKilledByPlayer;
            r["killer"] = {
                {"baseFormId", rec.killerBaseID != 0 ? std::format("{:08X}", rec.killerBaseID) : ""},
                {"refFormId", rec.killerRefID != 0 ? std::format("{:08X}", rec.killerRefID) : ""},
                {"name", rec.killerName}
            };

            if (!rec.isChild) {
                r["socialClass"] = {
                    {"id", static_cast<uint8_t>(rec.socialClass)},
                    {"name", Utils::SocialClassToString(rec.socialClass)}
                };
                r["skillType"] = {
                    {"id", static_cast<uint8_t>(rec.skillType)},
                    {"name", Utils::SkillTypeToString(rec.skillType)}
                };
                r["temperament"] = {
                    {"id", static_cast<uint8_t>(rec.temperament)},
                    {"name", Utils::TemperamentToString(rec.temperament)}
                };

                nlohmann::json partnersArr = nlohmann::json::array();
                for (const auto& p : rec.partnersAtDeath) {
                    nlohmann::json pObj;
                    pObj["partnerBaseId"] = std::format("{:08X}", p.partnerBaseID);
                    pObj["partnerRefId"] = std::format("{:08X}", p.partnerRefID);
                    pObj["name"] = p.partnerName;
                    pObj["statusAtDeath"] = {
                        {"id", static_cast<uint8_t>(p.statusAtDeath)},
                        {"name", Utils::RelationshipStatusToString(p.statusAtDeath)}
                    };
                    pObj["isPlayer"] = p.isPlayer;
                    partnersArr.push_back(pObj);
                }
                r["partnersAtDeath"] = partnersArr;
            }
            if (rec.childInfo.has_value()) {
                const auto& ci = rec.childInfo.value();
                RE::FormID visibleBioFatherBaseId = ci.bioFatherBaseID;
                if (!view.IsOmniscient()) {
                    const RE::FormID lookupId = rec.refFormID != 0 ? rec.refFormID : rec.baseFormID;
                    if (const auto perceivedChild = view.GetPerceivedChild(State::ChildId{ lookupId })) {
                        const RE::FormID perceivedFatherRaw = perceivedChild->perceivedBioFather.raw();
                        visibleBioFatherBaseId = (perceivedFatherRaw != 0 && perceivedFatherRaw != kUnknownFatherFormID)
                            ? ResolveBaseFormIDSafe(perceivedFatherRaw, view.Spatial())
                            : 0;
                    }
                }
                nlohmann::json cInfo;
                cInfo["guardianA"] = {
                    {"baseFormId", std::format("{:08X}", ci.guardianABaseID)},
                    {"refFormId", std::format("{:08X}", ci.guardianARefID)},
                    {"name", ci.guardianAName}
                };
                cInfo["guardianB"] = {
                    {"baseFormId", std::format("{:08X}", ci.guardianBBaseID)},
                    {"refFormId", std::format("{:08X}", ci.guardianBRefID)},
                    {"name", ci.guardianBName}
                };
                cInfo["bioMotherBaseId"] = std::format("{:08X}", ci.bioMotherBaseID);
                cInfo["bioFatherBaseId"] = std::format("{:08X}", visibleBioFatherBaseId);
                cInfo["isAdopted"] = ci.isAdopted;
                cInfo["houseFormId"] = ci.houseFormID != 0 ? std::format("{:08X}", ci.houseFormID) : "";
                r["childInfo"] = cInfo;
            }

            j.push_back(r);
        }
        return j;
    }

    nlohmann::json DeceasedRegistry::DumpJson() const {
        return DumpJson(Projection::PerspectiveView::ForDashboard(
            State::WorldStore::GetSingleton().Snapshot(),
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider()));
    }

}  // namespace MARAS
