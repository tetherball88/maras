#include "core/DeceasedRegistry.h"
#include "core/NPCIndex.h"
#include "core/PlayerChildrenService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"

namespace MARAS {

    DeceasedRegistry& DeceasedRegistry::GetSingleton() {
        static DeceasedRegistry instance;
        return instance;
    }

    namespace {
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
    }  // namespace

    void DeceasedRegistry::ResolveFormIDs(RE::FormID inputFormID, RE::FormID& outBaseID, RE::FormID& outRefID) const {
        if (inputFormID == 0 || inputFormID == kPlayerFormID || inputFormID == kPlayerBaseID ||
            inputFormID == kUnknownFatherFormID || IsSyntheticChildID(inputFormID)) {
            outBaseID = (inputFormID == kPlayerBaseID) ? kPlayerFormID : inputFormID;
            outRefID = outBaseID;
            return;
        }

        outRefID = inputFormID;
        outBaseID = NPCIndex::GetSingleton().GetBaseID(inputFormID);
        if (outBaseID == 0) {
            outBaseID = inputFormID;
            outRefID = NPCIndex::GetSingleton().GetRefID(inputFormID);
        }
    }

    bool DeceasedRegistry::RecordDeceasedAdult(RE::Actor* victim, bool killedByPlayer, RE::Actor* killer) {
        if (!victim) return false;

        std::unique_lock lock(mutex_);

        RE::FormID refID = victim->GetFormID();
        RE::FormID baseID = 0;
        if (auto* actorBase = victim->GetActorBase()) {
            baseID = actorBase->GetFormID();
        } else {
            baseID = NPCIndex::GetSingleton().GetBaseID(refID);
        }
        if (baseID == 0) baseID = refID;

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        if (!relMgr.IsRegistered(refID) && (baseID == 0 || !relMgr.IsRegistered(baseID))) {
            MARAS_LOG_DEBUG("RecordDeceasedAdult: Actor {:08X} (base {:08X}) is not registered in MARAS, skipping", refID, baseID);
            return false;
        }

        if (recordsByBaseID_.contains(baseID)) {
            MARAS_LOG_WARN("RecordDeceasedAdult: Actor {:08X} (base {:08X}) already in DeceasedRegistry", refID, baseID);
            return false;
        }

        DeceasedNPCRecord record;
        record.baseFormID = baseID;
        record.refFormID = refID;
        record.name = victim->GetDisplayFullName();
        if (record.name.empty()) record.name = victim->GetName();

        if (auto* calendar = RE::Calendar::GetSingleton()) {
            record.deathTimestamp = calendar->GetDaysPassed();
        }
        record.isChild = false;
        record.wasKilledByPlayer = killedByPlayer;
        if (killer) {
            record.killerRefID = killer->GetFormID();
            if (auto* kBase = killer->GetActorBase()) {
                record.killerBaseID = kBase->GetFormID();
            }
            record.killerName = killer->GetDisplayFullName();
            if (record.killerName.empty()) record.killerName = killer->GetName();
        }

        if (auto* profile = relMgr.GetNPCProfile(refID)) {
            record.socialClass = profile->socialClass;
            record.skillType = profile->skillType;
            record.temperament = profile->temperament;
        } else if (auto* profileBase = relMgr.GetNPCProfile(baseID)) {
            record.socialClass = profileBase->socialClass;
            record.skillType = profileBase->skillType;
            record.temperament = profileBase->temperament;
        }

        // Capture snapshot of all romantic and ex-partner relationships before unregistering
        std::vector<std::pair<RE::FormID, RelationshipStatus>> relationshipsToSnapshot;
        std::unordered_set<RE::FormID> seenPartners;
        for (const auto& [key, data] : relMgr.GetAllRelationshipPairs()) {
            RE::FormID partnerID = 0;
            if (key.Contains(refID)) {
                partnerID = key.GetOther(refID);
            } else if (baseID != 0 && baseID != refID && key.Contains(baseID)) {
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
                partnerBaseID = NPCIndex::GetSingleton().GetBaseID(partnerID);
                partnerName = ActorSvc::GetName(partnerID);
            }

            record.partnersAtDeath.emplace_back(partnerBaseID, partnerRefID, partnerName, status, isPlayer);
            MARAS_LOG_INFO("RecordDeceasedAdult: Snapshot partner {:08X} ('{}') with status {}",
                           partnerRefID, partnerName, static_cast<int>(status));
        }

        recordsByBaseID_[baseID] = record;
        refToBaseMap_[refID] = baseID;

        MARAS_LOG_INFO("RecordDeceasedAdult: Archived deceased adult {:08X} ('{}', base {:08X}) with {} partner bonds",
                       refID, record.name, baseID, record.partnersAtDeath.size());
        return true;
    }

    bool DeceasedRegistry::RecordDeceasedChild(RE::Actor* child, const ChildRegistryEntry& entry, bool killedByPlayer, RE::Actor* killer) {
        if (!child) return false;

        std::unique_lock lock(mutex_);

        RE::FormID refID = child->GetFormID();
        RE::FormID baseID = 0;
        if (auto* actorBase = child->GetActorBase()) {
            baseID = actorBase->GetFormID();
        } else {
            baseID = NPCIndex::GetSingleton().GetBaseID(refID);
        }
        if (baseID == 0) baseID = refID;

        if (recordsByBaseID_.contains(baseID)) {
            MARAS_LOG_WARN("RecordDeceasedChild: Child {:08X} (base {:08X}) already in DeceasedRegistry", refID, baseID);
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
            if (auto* kBase = killer->GetActorBase()) {
                record.killerBaseID = kBase->GetFormID();
            }
            record.killerName = killer->GetDisplayFullName();
            if (record.killerName.empty()) record.killerName = killer->GetName();
        }

        DeceasedChildInfo cInfo;
        cInfo.guardianARefID = entry.guardianAID;
        cInfo.guardianABaseID = (entry.guardianAID == kPlayerFormID) ? kPlayerFormID : NPCIndex::GetSingleton().GetBaseID(entry.guardianAID);
        cInfo.guardianAName = (entry.guardianAID == kPlayerFormID) ? "Player" : ActorSvc::GetName(entry.guardianAID);

        cInfo.guardianBRefID = entry.guardianBID;
        cInfo.guardianBBaseID = (entry.guardianBID == kPlayerFormID) ? kPlayerFormID : NPCIndex::GetSingleton().GetBaseID(entry.guardianBID);
        cInfo.guardianBName = (entry.guardianBID == kPlayerFormID) ? "Player" : ActorSvc::GetName(entry.guardianBID);

        auto resolveBioParentBaseID = [](RE::FormID id) -> RE::FormID {
            if (id == 0 || id == kPlayerFormID || id == kUnknownFatherFormID) return id;
            if (id == kPlayerBaseID) return kPlayerFormID;
            RE::FormID bID = NPCIndex::GetSingleton().GetBaseID(id);
            return bID != 0 ? bID : id;
        };
        cInfo.bioMotherBaseID = resolveBioParentBaseID(entry.bioMotherID);
        cInfo.bioFatherBaseID = resolveBioParentBaseID(entry.bioFatherID);
        cInfo.isAdopted = entry.isAdopted;

        if (auto state = PlayerChildrenService::GetSingleton().GetState(refID)) {
            cInfo.houseFormID = state->currentHouseFormID;
        }

        record.childInfo = cInfo;
        recordsByBaseID_[baseID] = record;
        refToBaseMap_[refID] = baseID;

        MARAS_LOG_INFO("RecordDeceasedChild: Archived deceased child {:08X} ('{}', base {:08X})",
                       refID, record.name, baseID);
        return true;
    }

    bool DeceasedRegistry::IsDeceased(RE::FormID formID) const {
        std::shared_lock lock(mutex_);
        if (recordsByBaseID_.contains(formID)) return true;
        auto it = refToBaseMap_.find(formID);
        if (it != refToBaseMap_.end()) return true;

        RE::FormID baseID = NPCIndex::GetSingleton().GetBaseID(formID);
        return (baseID != 0 && recordsByBaseID_.contains(baseID));
    }

    bool DeceasedRegistry::IsWidowOrWidower(RE::FormID actorFormID) const {
        std::shared_lock lock(mutex_);
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        for (const auto& [dBaseID, record] : recordsByBaseID_) {
            if (record.isChild) continue;
            for (const auto& partner : record.partnersAtDeath) {
                if (partner.statusAtDeath == RelationshipStatus::Married) {
                    if (partner.isPlayer && (actorFormID == kPlayerFormID || baseID == kPlayerFormID)) {
                        return true;
                    }
                    if (partner.partnerRefID != 0 && (partner.partnerRefID == refID || partner.partnerRefID == actorFormID)) {
                        return true;
                    }
                    if (partner.partnerBaseID != 0 && (partner.partnerBaseID == baseID || partner.partnerBaseID == actorFormID)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool DeceasedRegistry::WasMarriedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        std::shared_lock lock(mutex_);
        const auto* rec = FindRecordLocked(deceasedFormID);
        if (!rec || rec->isChild) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);

        for (const auto& p : rec->partnersAtDeath) {
            if (p.statusAtDeath == RelationshipStatus::Married) {
                if (p.isPlayer && (partnerFormID == kPlayerFormID || pBaseID == kPlayerFormID)) return true;
                if (p.partnerRefID != 0 && (p.partnerRefID == pRefID || p.partnerRefID == partnerFormID)) return true;
                if (p.partnerBaseID != 0 && (p.partnerBaseID == pBaseID || p.partnerBaseID == partnerFormID)) return true;
            }
        }
        return false;
    }

    bool DeceasedRegistry::WasEngagedTo(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        std::shared_lock lock(mutex_);
        const auto* rec = FindRecordLocked(deceasedFormID);
        if (!rec || rec->isChild) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);

        for (const auto& p : rec->partnersAtDeath) {
            if (p.statusAtDeath == RelationshipStatus::Engaged) {
                if (p.isPlayer && (partnerFormID == kPlayerFormID || pBaseID == kPlayerFormID)) return true;
                if (p.partnerRefID != 0 && (p.partnerRefID == pRefID || p.partnerRefID == partnerFormID)) return true;
                if (p.partnerBaseID != 0 && (p.partnerBaseID == pBaseID || p.partnerBaseID == partnerFormID)) return true;
            }
        }
        return false;
    }

    bool DeceasedRegistry::WasExOf(RE::FormID deceasedFormID, RE::FormID partnerFormID) const {
        std::shared_lock lock(mutex_);
        const auto* rec = FindRecordLocked(deceasedFormID);
        if (!rec || rec->isChild) return false;

        RE::FormID pBaseID = 0, pRefID = 0;
        ResolveFormIDs(partnerFormID, pBaseID, pRefID);

        for (const auto& p : rec->partnersAtDeath) {
            if (IsExPartnerStatus(p.statusAtDeath)) {
                if (p.isPlayer && (partnerFormID == kPlayerFormID || pBaseID == kPlayerFormID)) return true;
                if (p.partnerRefID != 0 && (p.partnerRefID == pRefID || p.partnerRefID == partnerFormID)) return true;
                if (p.partnerBaseID != 0 && (p.partnerBaseID == pBaseID || p.partnerBaseID == partnerFormID)) return true;
            }
        }
        return false;
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedSpousesOf(RE::FormID actorFormID) const {
        std::shared_lock lock(mutex_);
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        for (const auto& [dBaseID, record] : recordsByBaseID_) {
            if (record.isChild) continue;
            for (const auto& partner : record.partnersAtDeath) {
                if (partner.statusAtDeath == RelationshipStatus::Married) {
                    bool matches = false;
                    if (partner.isPlayer && (actorFormID == kPlayerFormID || baseID == kPlayerFormID)) matches = true;
                    else if (partner.partnerRefID != 0 && (partner.partnerRefID == refID || partner.partnerRefID == actorFormID)) matches = true;
                    else if (partner.partnerBaseID != 0 && (partner.partnerBaseID == baseID || partner.partnerBaseID == actorFormID)) matches = true;

                    if (matches) {
                        result.push_back(record);
                        break;
                    }
                }
            }
        }
        return result;
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedPartnersOf(RE::FormID actorFormID) const {
        std::shared_lock lock(mutex_);
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(actorFormID, baseID, refID);

        for (const auto& [dBaseID, record] : recordsByBaseID_) {
            if (record.isChild) continue;
            for (const auto& partner : record.partnersAtDeath) {
                if (!IsRomanticOrExStatus(partner.statusAtDeath)) continue;
                bool matches = false;
                if (partner.isPlayer && (actorFormID == kPlayerFormID || baseID == kPlayerFormID)) matches = true;
                else if (partner.partnerRefID != 0 && (partner.partnerRefID == refID || partner.partnerRefID == actorFormID)) matches = true;
                else if (partner.partnerBaseID != 0 && (partner.partnerBaseID == baseID || partner.partnerBaseID == actorFormID)) matches = true;

                if (matches) {
                    result.push_back(record);
                    break;
                }
            }
        }
        return result;
    }

    std::vector<DeceasedNPCRecord> DeceasedRegistry::GetDeceasedChildrenOf(RE::FormID parentFormID) const {
        std::shared_lock lock(mutex_);
        std::vector<DeceasedNPCRecord> result;
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(parentFormID, baseID, refID);

        for (const auto& [dBaseID, record] : recordsByBaseID_) {
            if (!record.isChild || !record.childInfo.has_value()) continue;
            if (MatchesChildParentOrGuardian(record.childInfo.value(), parentFormID, baseID, refID)) {
                result.push_back(record);
            }
        }
        return result;
    }

    bool DeceasedRegistry::HasDeceasedChildren(RE::FormID parentFormID) const {
        std::shared_lock lock(mutex_);
        RE::FormID baseID = 0, refID = 0;
        ResolveFormIDs(parentFormID, baseID, refID);

        for (const auto& [dBaseID, record] : recordsByBaseID_) {
            if (!record.isChild || !record.childInfo.has_value()) continue;
            if (MatchesChildParentOrGuardian(record.childInfo.value(), parentFormID, baseID, refID)) {
                return true;
            }
        }
        return false;
    }

    const DeceasedNPCRecord* DeceasedRegistry::FindRecordLocked(RE::FormID formID) const {
        auto it = recordsByBaseID_.find(formID);
        if (it != recordsByBaseID_.end()) return &it->second;

        auto itRef = refToBaseMap_.find(formID);
        if (itRef != refToBaseMap_.end()) {
            auto itBase = recordsByBaseID_.find(itRef->second);
            if (itBase != recordsByBaseID_.end()) return &itBase->second;
        }

        RE::FormID baseID = NPCIndex::GetSingleton().GetBaseID(formID);
        if (baseID != 0) {
            auto itBase = recordsByBaseID_.find(baseID);
            if (itBase != recordsByBaseID_.end()) return &itBase->second;
        }

        return nullptr;
    }

    std::optional<DeceasedNPCRecord> DeceasedRegistry::FindRecord(RE::FormID formID) const {
        std::shared_lock lock(mutex_);
        if (const auto* rec = FindRecordLocked(formID)) {
            return *rec;
        }
        return std::nullopt;
    }

    std::unordered_map<RE::FormID, DeceasedNPCRecord> DeceasedRegistry::GetAllRecords() const {
        std::shared_lock lock(mutex_);
        return recordsByBaseID_;
    }

    std::size_t DeceasedRegistry::GetDeceasedCount() const {
        std::shared_lock lock(mutex_);
        return recordsByBaseID_.size();
    }

    void DeceasedRegistry::Revert() {
        std::unique_lock lock(mutex_);
        recordsByBaseID_.clear();
        refToBaseMap_.clear();
        MARAS_LOG_INFO("DeceasedRegistry reverted to clean state");
    }

    bool DeceasedRegistry::Save(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;
        std::shared_lock lock(mutex_);

        std::uint32_t count = static_cast<std::uint32_t>(recordsByBaseID_.size());
        if (!serialization->WriteRecordData(count)) return false;

        auto writeString = [serialization](const std::string& str) -> bool {
            std::uint32_t len = static_cast<std::uint32_t>(str.size());
            if (!serialization->WriteRecordData(len)) return false;
            if (len > 0) {
                if (!serialization->WriteRecordData(str.data(), len)) return false;
            }
            return true;
        };

        for (const auto& [baseID, rec] : recordsByBaseID_) {
            if (!serialization->WriteRecordData(rec.baseFormID) ||
                !serialization->WriteRecordData(rec.refFormID) ||
                !writeString(rec.name) ||
                !serialization->WriteRecordData(rec.deathTimestamp) ||
                !serialization->WriteRecordData(rec.isChild) ||
                !serialization->WriteRecordData(rec.wasKilledByPlayer) ||
                !serialization->WriteRecordData(rec.killerBaseID) ||
                !serialization->WriteRecordData(rec.killerRefID) ||
                !writeString(rec.killerName)) {
                MARAS_LOG_ERROR("DeceasedRegistry::Save: Failed to write base header for {:08X}", baseID);
                return false;
            }

            if (!rec.isChild) {
                std::uint8_t sc = static_cast<std::uint8_t>(rec.socialClass);
                std::uint8_t st = static_cast<std::uint8_t>(rec.skillType);
                std::uint8_t tp = static_cast<std::uint8_t>(rec.temperament);
                if (!serialization->WriteRecordData(sc) ||
                    !serialization->WriteRecordData(st) ||
                    !serialization->WriteRecordData(tp)) {
                    return false;
                }

                std::uint32_t partnerCount = static_cast<std::uint32_t>(rec.partnersAtDeath.size());
                if (!serialization->WriteRecordData(partnerCount)) return false;

                for (const auto& p : rec.partnersAtDeath) {
                    std::uint8_t relSt = static_cast<std::uint8_t>(p.statusAtDeath);
                    if (!serialization->WriteRecordData(p.partnerBaseID) ||
                        !serialization->WriteRecordData(p.partnerRefID) ||
                        !writeString(p.partnerName) ||
                        !serialization->WriteRecordData(relSt) ||
                        !serialization->WriteRecordData(p.isPlayer)) {
                        return false;
                    }
                }
            } else {
                bool hasChildInfo = rec.childInfo.has_value();
                if (!serialization->WriteRecordData(hasChildInfo)) return false;

                if (hasChildInfo) {
                    const auto& ci = rec.childInfo.value();
                    if (!serialization->WriteRecordData(ci.guardianABaseID) ||
                        !serialization->WriteRecordData(ci.guardianARefID) ||
                        !writeString(ci.guardianAName) ||
                        !serialization->WriteRecordData(ci.guardianBBaseID) ||
                        !serialization->WriteRecordData(ci.guardianBRefID) ||
                        !writeString(ci.guardianBName) ||
                        !serialization->WriteRecordData(ci.bioMotherBaseID) ||
                        !serialization->WriteRecordData(ci.bioFatherBaseID) ||
                        !serialization->WriteRecordData(ci.isAdopted) ||
                        !serialization->WriteRecordData(ci.houseFormID)) {
                        return false;
                    }
                }
            }
        }

        MARAS_LOG_INFO("DeceasedRegistry::Save: Successfully saved {} deceased records", count);
        return true;
    }

    bool DeceasedRegistry::Load(SKSE::SerializationInterface* serialization, std::uint32_t /*version*/) {
        if (!serialization) return false;
        std::unique_lock lock(mutex_);

        recordsByBaseID_.clear();
        refToBaseMap_.clear();

        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) {
            MARAS_LOG_ERROR("DeceasedRegistry::Load: Failed to read record count");
            return false;
        }

        auto readString = [serialization](std::string& outStr) -> bool {
            std::uint32_t len = 0;
            if (!serialization->ReadRecordData(len)) return false;
            if (len > 0) {
                outStr.resize(len);
                if (!serialization->ReadRecordData(outStr.data(), len)) return false;
            } else {
                outStr.clear();
            }
            return true;
        };

        auto resolveOrKeep = [serialization](RE::FormID oldID) -> RE::FormID {
            if (oldID == 0 || oldID == kPlayerFormID || oldID == kPlayerBaseID ||
                oldID == kUnknownFatherFormID || IsSyntheticChildID(oldID)) {
                return oldID;
            }
            RE::FormID newID = 0;
            if (serialization->ResolveFormID(oldID, newID)) return newID;
            return oldID;
        };

        for (std::uint32_t i = 0; i < count; ++i) {
            DeceasedNPCRecord rec;
            RE::FormID oldBaseID = 0, oldRefID = 0;
            if (!serialization->ReadRecordData(oldBaseID) ||
                !serialization->ReadRecordData(oldRefID) ||
                !readString(rec.name) ||
                !serialization->ReadRecordData(rec.deathTimestamp) ||
                !serialization->ReadRecordData(rec.isChild) ||
                !serialization->ReadRecordData(rec.wasKilledByPlayer) ||
                !serialization->ReadRecordData(rec.killerBaseID) ||
                !serialization->ReadRecordData(rec.killerRefID) ||
                !readString(rec.killerName)) {
                MARAS_LOG_ERROR("DeceasedRegistry::Load: Failed to read header for record {}", i);
                return false;
            }

            rec.baseFormID = resolveOrKeep(oldBaseID);
            rec.refFormID = resolveOrKeep(oldRefID);
            rec.killerBaseID = resolveOrKeep(rec.killerBaseID);
            rec.killerRefID = resolveOrKeep(rec.killerRefID);

            if (!rec.isChild) {
                std::uint8_t sc = 0, st = 0, tp = 0;
                if (!serialization->ReadRecordData(sc) ||
                    !serialization->ReadRecordData(st) ||
                    !serialization->ReadRecordData(tp)) {
                    return false;
                }
                rec.socialClass = static_cast<SocialClass>(sc);
                rec.skillType = static_cast<SkillType>(st);
                rec.temperament = static_cast<Temperament>(tp);

                std::uint32_t partnerCount = 0;
                if (!serialization->ReadRecordData(partnerCount)) return false;

                for (std::uint32_t pIdx = 0; pIdx < partnerCount; ++pIdx) {
                    DeceasedPartnerSnapshot p;
                    std::uint8_t relSt = 0;
                    if (!serialization->ReadRecordData(p.partnerBaseID) ||
                        !serialization->ReadRecordData(p.partnerRefID) ||
                        !readString(p.partnerName) ||
                        !serialization->ReadRecordData(relSt) ||
                        !serialization->ReadRecordData(p.isPlayer)) {
                        return false;
                    }
                    p.partnerBaseID = resolveOrKeep(p.partnerBaseID);
                    p.partnerRefID = resolveOrKeep(p.partnerRefID);
                    p.statusAtDeath = static_cast<RelationshipStatus>(relSt);
                    rec.partnersAtDeath.push_back(p);
                }
            } else {
                bool hasChildInfo = false;
                if (!serialization->ReadRecordData(hasChildInfo)) return false;

                if (hasChildInfo) {
                    DeceasedChildInfo ci;
                    if (!serialization->ReadRecordData(ci.guardianABaseID) ||
                        !serialization->ReadRecordData(ci.guardianARefID) ||
                        !readString(ci.guardianAName) ||
                        !serialization->ReadRecordData(ci.guardianBBaseID) ||
                        !serialization->ReadRecordData(ci.guardianBRefID) ||
                        !readString(ci.guardianBName) ||
                        !serialization->ReadRecordData(ci.bioMotherBaseID) ||
                        !serialization->ReadRecordData(ci.bioFatherBaseID) ||
                        !serialization->ReadRecordData(ci.isAdopted) ||
                        !serialization->ReadRecordData(ci.houseFormID)) {
                        return false;
                    }
                    ci.guardianABaseID = resolveOrKeep(ci.guardianABaseID);
                    ci.guardianARefID = resolveOrKeep(ci.guardianARefID);
                    ci.guardianBBaseID = resolveOrKeep(ci.guardianBBaseID);
                    ci.guardianBRefID = resolveOrKeep(ci.guardianBRefID);
                    ci.bioMotherBaseID = resolveOrKeep(ci.bioMotherBaseID);
                    ci.bioFatherBaseID = resolveOrKeep(ci.bioFatherBaseID);
                    ci.houseFormID = resolveOrKeep(ci.houseFormID);
                    rec.childInfo = ci;
                }
            }

            recordsByBaseID_[rec.baseFormID] = rec;
            if (rec.refFormID != 0) {
                refToBaseMap_[rec.refFormID] = rec.baseFormID;
            }
        }

        MARAS_LOG_INFO("DeceasedRegistry::Load: Successfully loaded {} deceased records", recordsByBaseID_.size());
        return true;
    }

    nlohmann::json DeceasedRegistry::DumpJson() const {
        std::shared_lock lock(mutex_);
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [baseID, rec] : recordsByBaseID_) {
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
            } else if (rec.childInfo.has_value()) {
                const auto& ci = rec.childInfo.value();
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
                cInfo["bioFatherBaseId"] = std::format("{:08X}", ci.bioFatherBaseID);
                cInfo["isAdopted"] = ci.isAdopted;
                cInfo["houseFormId"] = ci.houseFormID != 0 ? std::format("{:08X}", ci.houseFormID) : "";
                r["childInfo"] = cInfo;
            }

            j.push_back(r);
        }
        return j;
    }

}  // namespace MARAS
