#include "core/NPCRelationshipManager.h"

#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/Serialization.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"

namespace MARAS {

    // Save/Load support
    void NPCRelationshipManager::Clear() {
        allRegistered.clear();
        baseToRefIDMap.clear();
        npcProfiles.clear();
        relationshipPairs.clear();
        actorPairIndex_.clear();
        pendingRelationshipsByBaseID.clear();
        pendingChildrenByBaseID.clear();
        childrenByID.clear();
        nextSyntheticChildID_ = 0xFF700001;

        MARAS_LOG_INFO("Cleared all NPC relationship data");
    }

    bool NPCRelationshipManager::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) {
            MARAS_LOG_ERROR("Serialization interface is null");
            return false;
        }

        if (!serialization->WriteRecordData(Serialization::kMagicNumber)) {
            MARAS_LOG_ERROR("Failed to write magic number");
            return false;
        }

        // Save NPC Profiles
        std::uint32_t profileCount = static_cast<std::uint32_t>(npcProfiles.size());
        if (!serialization->WriteRecordData(profileCount)) {
            MARAS_LOG_ERROR("Failed to write profile count");
            return false;
        }

        for (const auto& [formID, profile] : npcProfiles) {
            if (!serialization->WriteRecordData(formID) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(profile.socialClass)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(profile.skillType)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(profile.temperament)) ||
                !serialization->WriteRecordData(profile.originalHome.has_value()) ||
                (profile.originalHome.has_value() && !serialization->WriteRecordData(profile.originalHome.value())) ||
                !serialization->WriteRecordData(profile.currentHome.has_value()) ||
                (profile.currentHome.has_value() && !serialization->WriteRecordData(profile.currentHome.value())) ||
                !serialization->WriteRecordData(profile.homeMarker.has_value()) ||
                (profile.homeMarker.has_value() && !serialization->WriteRecordData(profile.homeMarker.value()))) {
                MARAS_LOG_ERROR("Failed to write profile for NPC {:08X}", formID);
                return false;
            }

            auto kwCount = static_cast<std::uint32_t>(profile.addedKeywords.size());
            if (!serialization->WriteRecordData(kwCount)) {
                MARAS_LOG_ERROR("Failed to write keyword count for NPC {:08X}", formID);
                return false;
            }
            for (auto kwID : profile.addedKeywords) {
                if (!serialization->WriteRecordData(kwID)) {
                    MARAS_LOG_ERROR("Failed to write keyword {:08X} for NPC {:08X}", kwID, formID);
                    return false;
                }
            }
        }

        // Save Relationship Pairs
        std::uint32_t pairCount = static_cast<std::uint32_t>(relationshipPairs.size());
        if (!serialization->WriteRecordData(pairCount)) {
            MARAS_LOG_ERROR("Failed to write relationship pair count");
            return false;
        }

        for (const auto& [key, data] : relationshipPairs) {
            if (!serialization->WriteRecordData(key.actorA) ||
                !serialization->WriteRecordData(key.actorB) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.status)) ||
                !serialization->WriteRecordData(static_cast<std::uint8_t>(data.originalStatus)) ||
                !serialization->WriteRecordData(data.isOpen)) {
                MARAS_LOG_ERROR("Failed to write relationship pair {:08X} - {:08X}", key.actorA, key.actorB);
                return false;
            }

            auto historyCount = static_cast<std::uint32_t>(data.statusHistory.size());
            if (!serialization->WriteRecordData(historyCount)) {
                MARAS_LOG_ERROR("Failed to write status history count for pair {:08X} - {:08X}", key.actorA, key.actorB);
                return false;
            }
            for (const auto& entry : data.statusHistory) {
                if (!serialization->WriteRecordData(static_cast<std::uint8_t>(entry.fromStatus)) ||
                    !serialization->WriteRecordData(static_cast<std::uint8_t>(entry.toStatus)) ||
                    !serialization->WriteRecordData(entry.timestamp) ||
                    !serialization->WriteRecordData(entry.initiatorFormID)) {
                    MARAS_LOG_ERROR("Failed to write status history entry for pair {:08X} - {:08X}", key.actorA, key.actorB);
                    return false;
                }
            }

            auto cheatCount = static_cast<std::uint32_t>(data.cheatingRecords.size());
            if (!serialization->WriteRecordData(cheatCount)) {
                MARAS_LOG_ERROR("Failed to write cheating record count for pair {:08X} - {:08X}", key.actorA, key.actorB);
                return false;
            }
            for (const auto& record : data.cheatingRecords) {
                if (!serialization->WriteRecordData(record.cheaterID) ||
                    !serialization->WriteRecordData(record.victimID) ||
                    !serialization->WriteRecordData(record.affairPartnerID) ||
                    !serialization->WriteRecordData(record.count) ||
                    !serialization->WriteRecordData(record.lastTimestamp) ||
                    !serialization->WriteRecordData(record.isSecret) ||
                    !serialization->WriteRecordData(static_cast<std::uint8_t>(record.affairType)) ||
                    !serialization->WriteRecordData(static_cast<std::uint8_t>(record.affairStatus))) {
                    MARAS_LOG_ERROR("Failed to write cheating record for pair {:08X} - {:08X}", key.actorA, key.actorB);
                    return false;
                }
            }
        }

        // Save Pending Deferred Relationships
        std::uint32_t pendingMapCount = static_cast<std::uint32_t>(pendingRelationshipsByBaseID.size());
        if (!serialization->WriteRecordData(pendingMapCount)) {
            MARAS_LOG_ERROR("Failed to write pending map count");
            return false;
        }

        for (const auto& [baseID, pendingList] : pendingRelationshipsByBaseID) {
            if (!serialization->WriteRecordData(baseID)) return false;
            std::uint32_t listCount = static_cast<std::uint32_t>(pendingList.size());
            if (!serialization->WriteRecordData(listCount)) return false;
            for (const auto& p : pendingList) {
                if (!serialization->WriteRecordData(p.partnerBaseID) ||
                    !serialization->WriteRecordData(p.partnerRefID) ||
                    !serialization->WriteRecordData(static_cast<std::uint8_t>(p.targetStatus)) ||
                    !serialization->WriteRecordData(static_cast<std::uint8_t>(p.originalStatus))) {
                    MARAS_LOG_ERROR("Failed to write pending relationship record for Base {:08X}", baseID);
                    return false;
                }
            }
        }

        // Save Pending Deferred Children
        std::uint32_t pendingChildMapCount = static_cast<std::uint32_t>(pendingChildrenByBaseID.size());
        if (!serialization->WriteRecordData(pendingChildMapCount)) {
            MARAS_LOG_ERROR("Failed to write pending child map count");
            return false;
        }

        for (const auto& [cBase, pd] : pendingChildrenByBaseID) {
            if (!serialization->WriteRecordData(cBase) ||
                !serialization->WriteRecordData(pd.childRefID) ||
                !serialization->WriteRecordData(pd.parentABaseID) ||
                !serialization->WriteRecordData(pd.parentARefID) ||
                !serialization->WriteRecordData(pd.parentBBaseID) ||
                !serialization->WriteRecordData(pd.parentBRefID) ||
                !serialization->WriteRecordData(pd.isAdopted)) {
                MARAS_LOG_ERROR("Failed to write pending child record for Base {:08X}", cBase);
                return false;
            }
        }

        // Save Child Registry (version 11+)
        std::uint32_t childRegCount = static_cast<std::uint32_t>(childrenByID.size());
        if (!serialization->WriteRecordData(childRegCount)) {
            MARAS_LOG_ERROR("Failed to write child registry count");
            return false;
        }

        for (const auto& [cID, entry] : childrenByID) {
            if (!serialization->WriteRecordData(entry.childID) ||
                !serialization->WriteRecordData(entry.guardianAID) ||
                !serialization->WriteRecordData(entry.guardianBID) ||
                !serialization->WriteRecordData(entry.timestamp) ||
                !serialization->WriteRecordData(entry.isAdopted) ||
                !serialization->WriteRecordData(entry.bioMotherID) ||
                !serialization->WriteRecordData(entry.bioFatherID)) {
                MARAS_LOG_ERROR("Failed to write child registry entry {:08X}", cID);
                return false;
            }
            uint32_t nameLen = static_cast<uint32_t>(entry.name.size());
            if (!serialization->WriteRecordData(nameLen)) return false;
            if (nameLen > 0) {
                if (!serialization->WriteRecordData(entry.name.data(), nameLen)) return false;
            }

            // Version 15+ fertility fields
            if (!serialization->WriteRecordData(entry.isBiologicalChild) ||
                !serialization->WriteRecordData(entry.conceptionTimestamp) ||
                !serialization->WriteRecordData(entry.birthTimestamp) ||
                !serialization->WriteRecordData(entry.paternitySecret.isSecret) ||
                !serialization->WriteRecordData(entry.paternitySecret.discovered) ||
                !serialization->WriteRecordData(entry.paternitySecret.discoveredByPartner) ||
                !serialization->WriteRecordData(entry.paternitySecret.hasPhenotypeMismatch) ||
                !serialization->WriteRecordData(entry.paternitySecret.lastCheckTimestamp)) {
                MARAS_LOG_ERROR("Failed to write fertility fields for child {:08X}", cID);
                return false;
            }

            // Version 16+ lifecycle fields
            uint8_t uStage = static_cast<uint8_t>(entry.stage);
            if (!serialization->WriteRecordData(uStage)) return false;
            uint32_t locLen = static_cast<uint32_t>(entry.trainingLocation.size());
            if (!serialization->WriteRecordData(locLen)) return false;
            if (locLen > 0) {
                if (!serialization->WriteRecordData(entry.trainingLocation.data(), locLen)) return false;
            }

            // Version 17+ dual-edge secrecy fields
            if (!serialization->WriteRecordData(entry.paternitySecret.isSecretForBioFather) ||
                !serialization->WriteRecordData(entry.paternitySecret.isSecretForPartner) ||
                !serialization->WriteRecordData(entry.paternitySecret.discoveredByBioFather)) {
                MARAS_LOG_ERROR("Failed to write dual-edge secrecy fields for child {:08X}", cID);
                return false;
            }

            // Version 19+ child gender
            if (!serialization->WriteRecordData(entry.gender)) {
                MARAS_LOG_ERROR("Failed to write gender for child {:08X}", cID);
                return false;
            }
        }

        MARAS_LOG_INFO("Successfully saved NPCRelationshipManager data (Profiles: {}, Pairs: {}, Pending: {}, PendingChildren: {}, ChildRegistry: {})",
                       profileCount, pairCount, pendingMapCount, pendingChildMapCount, childRegCount);
        return true;
    }

    bool NPCRelationshipManager::Load(SKSE::SerializationInterface* serialization, std::uint32_t version, std::uint32_t length) {
        if (!serialization) {
            MARAS_LOG_ERROR("Serialization interface is null");
            return false;
        }

        Clear();

        std::uint32_t bytesRead = 0;
        const auto hasRemainingBytes = [&](std::uint32_t needed) -> bool {
            return length == 0 || (bytesRead <= length && needed <= (length - bytesRead));
        };
        const auto readValue = [&](auto& val) -> bool {
            constexpr std::uint32_t needed = static_cast<std::uint32_t>(sizeof(val));
            if (!hasRemainingBytes(needed)) {
                return false;
            }
            if (!serialization->ReadRecordData(val)) {
                return false;
            }
            bytesRead += needed;
            return true;
        };
        const auto readBuffer = [&](void* buf, std::uint32_t len) -> bool {
            if (len == 0) {
                return true;
            }
            if (!hasRemainingBytes(len)) {
                return false;
            }
            if (!serialization->ReadRecordData(buf, len)) {
                return false;
            }
            bytesRead += len;
            return true;
        };

        std::uint32_t magic = 0;
        if (!readValue(magic) || magic != Serialization::kMagicNumber) {
            MARAS_LOG_ERROR("Invalid magic number during load");
            return false;
        }

        if (version < 4) {
            // Version 1-3 Legacy Migration
            std::uint32_t npcCount = 0;
            if (!readValue(npcCount)) return false;

            MARAS_LOG_INFO("Migrating legacy {} NPC records (version {}) to version 4 pairs", npcCount, version);

            constexpr std::uint8_t kOldDeceasedValue = 5;

            for (std::uint32_t i = 0; i < npcCount; ++i) {
                RE::FormID oldFormID = 0, newFormID = 0;
                if (!readValue(oldFormID)) return false;
                bool formIDValid = serialization->ResolveFormID(oldFormID, newFormID);

                NPCProfileData profile;
                std::uint8_t enumValue = 0;

                if (!readValue(enumValue)) return false;
                profile.socialClass = static_cast<SocialClass>(enumValue);

                if (!readValue(enumValue)) return false;
                profile.skillType = static_cast<SkillType>(enumValue);

                if (!readValue(enumValue)) return false;
                profile.temperament = static_cast<Temperament>(enumValue);

                std::uint8_t statusValue = 0;
                if (!readValue(statusValue)) return false;

                bool hasOriginalHome = false;
                if (!readValue(hasOriginalHome)) return false;
                if (hasOriginalHome) {
                    RE::FormID oldHome = 0, resolvedHome = 0;
                    if (!readValue(oldHome)) return false;
                    if (oldHome != 0 && serialization->ResolveFormID(oldHome, resolvedHome)) {
                        profile.originalHome = resolvedHome;
                    }
                }

                bool hasCurrentHome = false;
                if (!readValue(hasCurrentHome)) return false;
                if (hasCurrentHome) {
                    RE::FormID oldHome = 0, resolvedHome = 0;
                    if (!readValue(oldHome)) return false;
                    if (oldHome != 0 && serialization->ResolveFormID(oldHome, resolvedHome)) {
                        profile.currentHome = resolvedHome;
                    }
                }

                uint32_t engDate = 0, marrDate = 0;
                if (!readValue(engDate) || !readValue(marrDate)) return false;

                if (version >= 2) {
                    bool hasHomeMarker = false;
                    if (!readValue(hasHomeMarker)) return false;
                    if (hasHomeMarker) {
                        RE::FormID oldMarker = 0, resolvedMarker = 0;
                        if (!readValue(oldMarker)) return false;
                        if (oldMarker != 0 && serialization->ResolveFormID(oldMarker, resolvedMarker)) {
                            profile.homeMarker = resolvedMarker;
                        }
                    }
                }

                if (version >= 3) {
                    std::uint32_t kwCount = 0;
                    if (!readValue(kwCount)) return false;
                    for (std::uint32_t k = 0; k < kwCount; ++k) {
                        RE::FormID oldKw = 0, resolvedKw = 0;
                        if (!readValue(oldKw)) return false;
                        if (oldKw != 0 && serialization->ResolveFormID(oldKw, resolvedKw)) {
                            profile.addedKeywords.insert(resolvedKw);
                        }
                    }
                }

                if (!formIDValid || statusValue == kOldDeceasedValue) continue;

                auto* form = RE::TESForm::LookupByID(newFormID);
                auto* actor = form ? form->As<RE::Actor>() : nullptr;
                if (!actor || actor->IsDead()) continue;

                profile.formID = newFormID;
                npcProfiles[newFormID] = profile;
                allRegistered.insert(newFormID);

                NPCRelationshipData pairData(newFormID, kPlayerFormID, static_cast<RelationshipStatus>(statusValue));
                if (engDate != 0) {
                    pairData.statusHistory.emplace_back(RelationshipStatus::Candidate, RelationshipStatus::Engaged, static_cast<float>(engDate), kPlayerFormID);
                }
                if (marrDate != 0) {
                    pairData.statusHistory.emplace_back(RelationshipStatus::Engaged, RelationshipStatus::Married, static_cast<float>(marrDate), kPlayerFormID);
                }
                GetOrCreatePairData(newFormID, kPlayerFormID) = std::move(pairData);

                if (profile.homeMarker.has_value()) {
                    SetLinkedRefForHomeMarker(newFormID, profile.homeMarker.value());
                }
            }
        } else {
            // Version 4+ Unified Format (note: version 4 is also the collapsed final schema version;
            // versions 5..20 were intermediate in-development versions)
            const bool hasChildRegistry = (version == 4 || version >= 11);
            const bool hasFertilityFields = (version == 4 || version >= 15);
            const bool hasLifecycleFields = (version == 4 || version >= 16);
            const bool hasDualSecrecyFields = (version == 4 || version >= 17);
            const bool hasChildGender = (version == 4 || version >= 19);
            constexpr std::uint32_t kMaxChildStringLen = 512;

            std::uint32_t profileCount = 0;
            if (!readValue(profileCount)) return false;

            for (std::uint32_t i = 0; i < profileCount; ++i) {
                RE::FormID oldFormID = 0, newFormID = 0;
                if (!readValue(oldFormID)) return false;
                bool valid = serialization->ResolveFormID(oldFormID, newFormID);

                NPCProfileData profile;
                std::uint8_t enumValue = 0;
                if (!readValue(enumValue)) return false;
                profile.socialClass = static_cast<SocialClass>(enumValue);
                if (!readValue(enumValue)) return false;
                profile.skillType = static_cast<SkillType>(enumValue);
                if (!readValue(enumValue)) return false;
                profile.temperament = static_cast<Temperament>(enumValue);

                bool hasOrigHome = false;
                if (!readValue(hasOrigHome)) return false;
                if (hasOrigHome) {
                    RE::FormID oldH = 0, newH = 0;
                    if (!readValue(oldH)) return false;
                    if (oldH != 0 && serialization->ResolveFormID(oldH, newH)) profile.originalHome = newH;
                }

                bool hasCurrHome = false;
                if (!readValue(hasCurrHome)) return false;
                if (hasCurrHome) {
                    RE::FormID oldH = 0, newH = 0;
                    if (!readValue(oldH)) return false;
                    if (oldH != 0 && serialization->ResolveFormID(oldH, newH)) profile.currentHome = newH;
                }

                bool hasMarker = false;
                if (!readValue(hasMarker)) return false;
                if (hasMarker) {
                    RE::FormID oldM = 0, newM = 0;
                    if (!readValue(oldM)) return false;
                    if (oldM != 0 && serialization->ResolveFormID(oldM, newM)) profile.homeMarker = newM;
                }

                std::uint32_t kwCount = 0;
                if (!readValue(kwCount)) return false;
                for (std::uint32_t k = 0; k < kwCount; ++k) {
                    RE::FormID oldKw = 0, newKw = 0;
                    if (!readValue(oldKw)) return false;
                    if (oldKw != 0 && serialization->ResolveFormID(oldKw, newKw)) profile.addedKeywords.insert(newKw);
                }

                if (valid) {
                    profile.formID = newFormID;
                    npcProfiles[newFormID] = profile;
                    allRegistered.insert(newFormID);
                    if (profile.homeMarker.has_value()) {
                        SetLinkedRefForHomeMarker(newFormID, profile.homeMarker.value());
                    }
                }
            }

            std::uint32_t pairCount = 0;
            if (!readValue(pairCount)) return false;

            for (std::uint32_t i = 0; i < pairCount; ++i) {
                RE::FormID oldA = 0, newA = 0, oldB = 0, newB = 0;
                if (!readValue(oldA) || !readValue(oldB)) return false;
                bool validA = (oldA == kPlayerFormID) ? (newA = kPlayerFormID, true) : serialization->ResolveFormID(oldA, newA);
                bool validB = (oldB == kPlayerFormID) ? (newB = kPlayerFormID, true) : serialization->ResolveFormID(oldB, newB);

                std::uint8_t stVal = 0;
                std::uint8_t origStVal = static_cast<std::uint8_t>(RelationshipStatus::Unknown);
                bool isOpenVal = false;
                if (!readValue(stVal) ||
                    !readValue(origStVal) ||
                    !readValue(isOpenVal)) return false;

                NPCRelationshipData data(newA, newB, static_cast<RelationshipStatus>(stVal));
                data.originalStatus = static_cast<RelationshipStatus>(origStVal);
                data.isOpen = isOpenVal;

                std::uint32_t historyCount = 0;
                if (!readValue(historyCount)) return false;
                for (std::uint32_t h = 0; h < historyCount; ++h) {
                    std::uint8_t fromVal = 0, toVal = 0;
                    float ts = 0.0f;
                    RE::FormID oldInit = 0, newInit = 0;
                    if (!readValue(fromVal) ||
                        !readValue(toVal) ||
                        !readValue(ts) ||
                        !readValue(oldInit)) break;

                    bool validInit = (oldInit == kPlayerFormID) ? (newInit = kPlayerFormID, true) : serialization->ResolveFormID(oldInit, newInit);
                    if (validInit) {
                        data.statusHistory.emplace_back(
                            static_cast<RelationshipStatus>(fromVal),
                            static_cast<RelationshipStatus>(toVal),
                            ts,
                            newInit);
                    }
                }

                std::uint32_t cheatCount = 0;
                if (!readValue(cheatCount)) return false;
                for (std::uint32_t c = 0; c < cheatCount; ++c) {
                    RE::FormID oldCheater = 0, newCheater = 0;
                    RE::FormID oldVictim = 0, newVictim = 0;
                    RE::FormID oldAffair = 0, newAffair = 0;
                    std::uint32_t recCount = 1, recTimestamp = 0;
                    bool isSecret = false;
                    std::uint8_t rawType = 0, rawStatus = 0;

                    if (!readValue(oldCheater) ||
                        !readValue(oldVictim) ||
                        !readValue(oldAffair) ||
                        !readValue(recCount) ||
                        !readValue(recTimestamp) ||
                        !readValue(isSecret) ||
                        !readValue(rawType) ||
                        !readValue(rawStatus)) {
                        break;
                    }

                    bool resCheater = (oldCheater == kPlayerFormID) ? (newCheater = kPlayerFormID, true) : serialization->ResolveFormID(oldCheater, newCheater);
                    bool resVictim = (oldVictim == kPlayerFormID) ? (newVictim = kPlayerFormID, true) : serialization->ResolveFormID(oldVictim, newVictim);
                    bool resAffair = (oldAffair == kPlayerFormID) ? (newAffair = kPlayerFormID, true) : serialization->ResolveFormID(oldAffair, newAffair);

                    if (resCheater && resVictim && resAffair) {
                        data.cheatingRecords.emplace_back(newCheater, newVictim, newAffair, recCount, recTimestamp, isSecret,
                                                          static_cast<AffairType>(rawType), static_cast<AffairStatus>(rawStatus));
                    }
                }

                if (validA && validB) {
                    GetOrCreatePairData(newA, newB) = std::move(data);
                }
            }

            std::uint32_t pendingMapCount = 0;
            if (readValue(pendingMapCount)) {
                for (std::uint32_t pIdx = 0; pIdx < pendingMapCount; ++pIdx) {
                    RE::FormID oldBaseID = 0, newBaseID = 0;
                    if (!readValue(oldBaseID)) break;
                    bool validBase = (oldBaseID == kPlayerFormID) ? (newBaseID = kPlayerFormID, true) : serialization->ResolveFormID(oldBaseID, newBaseID);

                    std::uint32_t listCount = 0;
                    if (!readValue(listCount)) break;

                    for (std::uint32_t j = 0; j < listCount; ++j) {
                        RE::FormID oldPBase = 0, newPBase = 0;
                        RE::FormID oldPRef = 0, newPRef = 0;
                        std::uint8_t targetSt = 0, origSt = 0;

                        if (!readValue(oldPBase) ||
                            !readValue(oldPRef) ||
                            !readValue(targetSt) ||
                            !readValue(origSt)) break;

                        bool validPBase = (oldPBase == kPlayerFormID) ? (newPBase = kPlayerFormID, true) : serialization->ResolveFormID(oldPBase, newPBase);
                        bool validPRef = (oldPRef == 0) ? (newPRef = 0, true) : ((oldPRef == kPlayerFormID) ? (newPRef = kPlayerFormID, true) : serialization->ResolveFormID(oldPRef, newPRef));

                        if (validBase && validPBase && validPRef) {
                            AddPendingRelationship(newBaseID, PendingRelationshipData(newPBase, newPRef, static_cast<RelationshipStatus>(targetSt), static_cast<RelationshipStatus>(origSt)));
                        }
                    }
                }
            }

            std::uint32_t pendingChildMapCount = 0;
            if (readValue(pendingChildMapCount)) {
                for (std::uint32_t cIdx = 0; cIdx < pendingChildMapCount; ++cIdx) {
                    RE::FormID oldCBase = 0, newCBase = 0;
                    RE::FormID oldCRef = 0, newCRef = 0;
                    RE::FormID oldPABase = 0, newPABase = 0;
                    RE::FormID oldPARef = 0, newPARef = 0;
                    RE::FormID oldPBBase = 0, newPBBase = 0;
                    RE::FormID oldPBRef = 0, newPBRef = 0;
                    bool isAdopted = true;

                    if (!readValue(oldCBase) ||
                        !readValue(oldCRef) ||
                        !readValue(oldPABase) ||
                        !readValue(oldPARef) ||
                        !readValue(oldPBBase) ||
                        !readValue(oldPBRef) ||
                        !readValue(isAdopted)) break;

                    bool validCBase = (oldCBase == kPlayerFormID) ? (newCBase = kPlayerFormID, true) : serialization->ResolveFormID(oldCBase, newCBase);
                    bool validCRef = (oldCRef == 0) ? (newCRef = 0, true) : ((oldCRef == kPlayerFormID) ? (newCRef = kPlayerFormID, true) : serialization->ResolveFormID(oldCRef, newCRef));
                    bool validPABase = (oldPABase == 0) ? (newPABase = 0, true) : ((oldPABase == kPlayerFormID) ? (newPABase = kPlayerFormID, true) : serialization->ResolveFormID(oldPABase, newPABase));
                    bool validPARef = (oldPARef == 0) ? (newPARef = 0, true) : ((oldPARef == kPlayerFormID) ? (newPARef = kPlayerFormID, true) : serialization->ResolveFormID(oldPARef, newPARef));
                    bool validPBBase = (oldPBBase == 0) ? (newPBBase = 0, true) : ((oldPBBase == kPlayerFormID) ? (newPBBase = kPlayerFormID, true) : serialization->ResolveFormID(oldPBBase, newPBBase));
                    bool validPBRef = (oldPBRef == 0) ? (newPBRef = 0, true) : ((oldPBRef == kPlayerFormID) ? (newPBRef = kPlayerFormID, true) : serialization->ResolveFormID(oldPBRef, newPBRef));

                    if (validCBase && validCRef && validPABase && validPARef && validPBBase && validPBRef) {
                        AddPendingChild(newCBase, PendingChildData(newCBase, newCRef, newPABase, newPARef, newPBBase, newPBRef, isAdopted));
                    }
                }
            }

            std::uint32_t childRegCount = 0;
            if (hasChildRegistry && readValue(childRegCount)) {
                for (std::uint32_t cIdx = 0; cIdx < childRegCount; ++cIdx) {
                    RE::FormID oldChild = 0, newChild = 0;
                    RE::FormID oldPA = 0, newPA = 0;
                    RE::FormID oldPB = 0, newPB = 0;
                    int32_t ts = 0;
                    bool isAdopted = true;
                    RE::FormID oldBioA = 0, newBioA = 0;
                    RE::FormID oldBioB = 0, newBioB = 0;

                    if (!readValue(oldChild) ||
                        !readValue(oldPA) ||
                        !readValue(oldPB) ||
                        !readValue(ts) ||
                        !readValue(isAdopted) ||
                        !readValue(oldBioA) ||
                        !readValue(oldBioB)) break;

                    uint32_t nameLen = 0;
                    if (!readValue(nameLen) || nameLen > kMaxChildStringLen || !hasRemainingBytes(nameLen)) {
                        MARAS_LOG_ERROR("Invalid or out-of-bounds child name length {} during load", nameLen);
                        break;
                    }
                    std::string childName;
                    if (nameLen > 0) {
                        childName.resize(nameLen);
                        if (!readBuffer(childName.data(), nameLen)) {
                            MARAS_LOG_ERROR("Failed to read child name of length {}", nameLen);
                            break;
                        }
                    }

                    bool isBioChild = !isAdopted;
                    float concTs = 0.0f;
                    float birthTs = 0.0f;
                    bool isSec = false, disc = false, byPartner = false, mismatch = false;
                    float lastCheck = 0.0f;

                    if (hasFertilityFields) {
                        if (!readValue(isBioChild) ||
                            !readValue(concTs) ||
                            !readValue(birthTs) ||
                            !readValue(isSec) ||
                            !readValue(disc) ||
                            !readValue(byPartner) ||
                            !readValue(mismatch) ||
                            !readValue(lastCheck)) break;
                    }

                    PaternitySecret pSec(isSec, disc, byPartner, mismatch, lastCheck);

                    ChildStage stage = ChildStage::Child;
                    std::string trainingLoc;
                    if (hasLifecycleFields) {
                        uint8_t uStage = 0;
                        if (!readValue(uStage)) break;
                        stage = static_cast<ChildStage>(uStage);

                        uint32_t locLen = 0;
                        if (!readValue(locLen) || locLen > kMaxChildStringLen || !hasRemainingBytes(locLen)) {
                            MARAS_LOG_ERROR("Invalid or out-of-bounds child training location length {} during load", locLen);
                            break;
                        }
                        if (locLen > 0) {
                            trainingLoc.resize(locLen);
                            if (!readBuffer(trainingLoc.data(), locLen)) {
                                MARAS_LOG_ERROR("Failed to read child training location of length {}", locLen);
                                break;
                            }
                        }
                    }

                    // Extra safety: if stage is Unborn, but birthTs > 0.0f, promote to Infant
                    if (stage == ChildStage::Unborn && birthTs > 0.0f) {
                        stage = ChildStage::Infant;
                    }

                    if (hasDualSecrecyFields) {
                        bool secBio = false, secPartner = false, byBio = false;
                        if (!readValue(secBio) ||
                            !readValue(secPartner) ||
                            !readValue(byBio)) break;
                        pSec.isSecretForBioFather = secBio;
                        pSec.isSecretForPartner = secPartner;
                        pSec.discoveredByBioFather = byBio;
                    } else {
                        pSec.isSecretForPartner = pSec.isSecret;
                        pSec.isSecretForBioFather = false;
                        pSec.discoveredByBioFather = false;
                    }

                    uint8_t childGender = 0;
                    if (hasChildGender && hasRemainingBytes(sizeof(childGender))) {
                        if (!readValue(childGender)) break;
                    }

                    bool validChild = false;
                    if (oldChild == kPlayerFormID) {
                        newChild = kPlayerFormID;
                        validChild = true;
                    } else if ((oldChild & 0xFF000000) == 0xFF000000) {
                        // Synthetic newborn ID preserves its ID across save/load
                        newChild = oldChild;
                        validChild = true;
                        nextSyntheticChildID_ = std::max(nextSyntheticChildID_, oldChild + 1);
                    } else {
                        validChild = serialization->ResolveFormID(oldChild, newChild);
                    }

                    if (validChild) {
                        if (oldPA != 0) (oldPA == kPlayerFormID) ? newPA = kPlayerFormID : serialization->ResolveFormID(oldPA, newPA);
                        if (oldPB != 0) (oldPB == kPlayerFormID) ? newPB = kPlayerFormID : serialization->ResolveFormID(oldPB, newPB);
                        if (oldBioA != 0) (oldBioA == kPlayerFormID) ? newBioA = kPlayerFormID : serialization->ResolveFormID(oldBioA, newBioA);
                        if (oldBioB != 0) (oldBioB == kPlayerFormID) ? newBioB = kPlayerFormID : ((oldBioB == kUnknownFatherFormID) ? (newBioB = kUnknownFatherFormID, true) : serialization->ResolveFormID(oldBioB, newBioB));

                        RE::FormID guardianA = newPA;
                        RE::FormID guardianB = newPB;
                        RE::FormID bioMother = newBioA;
                        RE::FormID bioFather = newBioB;

                        childrenByID.emplace(newChild, ChildRegistryEntry(newChild, guardianA, guardianB, childName, ts, isAdopted, bioMother, bioFather,
                                                                          isBioChild, concTs, birthTs, pSec, stage, trainingLoc, childGender));
                    }
                }
            }

            // Consume legacy dev pregnancy count if trailing bytes exist in record
            if (hasRemainingBytes(sizeof(std::uint32_t))) {
                std::uint32_t pregCount = 0;
                readValue(pregCount);
            }
        }

        MARAS_LOG_INFO("Successfully loaded {} profiles, {} relationship pairs, {} pending entries, {} pending children, and {} registered children",
                       npcProfiles.size(), relationshipPairs.size(), pendingRelationshipsByBaseID.size(), pendingChildrenByBaseID.size(), childrenByID.size());
        RecalculateAndUpdateGlobals();
        UpdateAllCanFormNonCheatingMainFactions();
        SyncAllMarriedCouplesChildCustody();
        ReevaluateAllNonPlayerChildrenPackages();
        return true;
    }

    void NPCRelationshipManager::Revert() {
        MARAS_LOG_INFO("Reverting NPC relationship data");
        Clear();
    }

    // Debug/logging
    void NPCRelationshipManager::LogStatistics() const {
        MARAS_LOG_INFO("MARAS NPC Statistics:");
        MARAS_LOG_INFO("  Total Registered: {}", GetTotalRegisteredCount());
        MARAS_LOG_INFO("  Candidates (Player): {}", GetCandidateCount(kPlayerFormID));
        MARAS_LOG_INFO("  Engaged (Player): {}", GetEngagedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Married (Player): {}", GetMarriedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Divorced (Player): {}", GetDivorcedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Jilted (Player): {}", GetJiltedCount(kPlayerFormID));
        MARAS_LOG_INFO("  ExDating (Player): {}", GetExDatingCount(kPlayerFormID));
        MARAS_LOG_INFO("  ExLover (Player): {}", GetExLoverCount(kPlayerFormID));
        MARAS_LOG_INFO("  Pending Deferred Base Entries: {}", pendingRelationshipsByBaseID.size());

        for (const auto& [baseID, pendingList] : pendingRelationshipsByBaseID) {
            for (const auto& pending : pendingList) {
                MARAS_LOG_INFO("    [Pending] Base {:08X} <-> Partner Base {:08X} (Ref {:08X}) | Target: {}, Original: {}",
                               baseID, pending.partnerBaseID, pending.partnerRefID,
                               Utils::RelationshipStatusToString(pending.targetStatus),
                               Utils::RelationshipStatusToString(pending.originalStatus));
            }
        }
    }

    void NPCRelationshipManager::RecalculateAndUpdateGlobals() {
        std::int32_t loveInterests = static_cast<std::int32_t>(GetAllEngaged(kPlayerFormID).size() + GetAllMarried(kPlayerFormID).size());
        std::int32_t spouses = static_cast<std::int32_t>(GetAllMarried(kPlayerFormID).size());

        auto& cache = FormCache::GetSingleton();

        if (auto globalLove = cache.GetLoveInterestsCount()) {
            globalLove->value = static_cast<float>(loveInterests);
            MARAS_LOG_DEBUG("Updated LoveInterestsCount global to {}", loveInterests);
        }

        if (auto globalSpouses = cache.GetSpousesCount()) {
            globalSpouses->value = static_cast<float>(spouses);
            MARAS_LOG_DEBUG("Updated SpousesCount global to {}", spouses);
        }

        UpdateAllSpousesChildrenFaction();
        UpdateAllSpousesCheatingAndOpenFactions();
        UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
    }

    void NPCRelationshipManager::LogNPCDetails(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (!profile) {
            MARAS_LOG_WARN("NPC {:08X} profile not found in MARAS data", npcFormID);
            return;
        }

        auto data = GetNPCData(npcFormID, kPlayerFormID);
        MARAS_LOG_INFO("NPC {:08X} Details:", npcFormID);
        MARAS_LOG_INFO("  Status with Player: {}", data ? static_cast<int>(data->status) : -1);
        MARAS_LOG_INFO("  Social Class: {}", static_cast<int>(profile->socialClass));
        MARAS_LOG_INFO("  Skill Type: {}", static_cast<int>(profile->skillType));
        MARAS_LOG_INFO("  Temperament: {}", static_cast<int>(profile->temperament));
    }

    nlohmann::json NPCRelationshipManager::DumpJson() const {
        nlohmann::json root;

        auto stageToString = [](ChildStage s) -> std::string {
            switch (s) {
                case ChildStage::Unborn:   return "unborn";
                case ChildStage::Infant:   return "infant";
                case ChildStage::Child:    return "child";
                case ChildStage::Training: return "training";
                case ChildStage::Adult:    return "adult";
            }
            return "child";
        };

        // 1. Registered NPCs & Profiles
        nlohmann::json profilesArray = nlohmann::json::array();
        std::vector<RE::FormID> actorsToExport;
        actorsToExport.push_back(kPlayerFormID);
        for (auto id : allRegistered) {
            if (id != kPlayerFormID) {
                actorsToExport.push_back(id);
            }
        }

        for (auto formID : actorsToExport) {
            nlohmann::json p;
            p["formId"] = std::format("{:08X}", formID);
            if (formID == kPlayerFormID) {
                auto player = RE::PlayerCharacter::GetSingleton();
                p["name"] = ActorSvc::GetName(kPlayerFormID);
                std::string pRace = ActorSvc::GetRaceName(kPlayerFormID);
                p["race"] = !pRace.empty() ? pRace : "Nord";
                p["gender"] = (player && player->GetActorBase()) ? (player->GetActorBase()->GetSex() == RE::SEXES::kFemale ? "female" : "male") : "male";
                p["isChild"] = false;
            } else {
                p["name"] = ActorSvc::GetName(formID);
                p["race"] = ActorSvc::GetRaceName(formID);
                p["isChild"] = IsChildNPC(formID);
                p["gender"] = (ActorSvc::GetSex(formID) == 1) ? "female" : "male";
            }

            auto itProf = npcProfiles.find(formID);
            if (itProf != npcProfiles.end()) {
                const auto& prof = itProf->second;
                p["socialClass"] = {
                    {"id", static_cast<uint8_t>(prof.socialClass)},
                    {"name", Utils::SocialClassToString(prof.socialClass)}
                };
                p["skillType"] = {
                    {"id", static_cast<uint8_t>(prof.skillType)},
                    {"name", Utils::SkillTypeToString(prof.skillType)}
                };
                p["temperament"] = {
                    {"id", static_cast<uint8_t>(prof.temperament)},
                    {"name", Utils::TemperamentToString(prof.temperament)}
                };

                if (prof.originalHome.has_value()) {
                    nlohmann::json h;
                    h["formId"] = std::format("{:08X}", *prof.originalHome);
                    std::string hName = "";
                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(*prof.originalHome)) {
                        if (cell->GetName()) hName = cell->GetName();
                    }
                    h["name"] = hName;
                    p["originalHome"] = h;
                }
                if (prof.currentHome.has_value()) {
                    nlohmann::json h;
                    h["formId"] = std::format("{:08X}", *prof.currentHome);
                    std::string hName = "";
                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(*prof.currentHome)) {
                        if (cell->GetName()) hName = cell->GetName();
                    }
                    h["name"] = hName;
                    p["currentHome"] = h;
                }
                if (prof.homeMarker.has_value()) {
                    p["homeMarker"] = std::format("{:08X}", *prof.homeMarker);
                }
                nlohmann::json kwArray = nlohmann::json::array();
                for (auto kwID : prof.addedKeywords) {
                    kwArray.push_back(std::format("{:08X}", kwID));
                }
                p["addedKeywords"] = kwArray;
            }

            profilesArray.push_back(p);
        }
        root["npcProfiles"] = profilesArray;

        // 2. Relationship Pairs
        nlohmann::json pairsArray = nlohmann::json::array();
        for (const auto& [key, data] : relationshipPairs) {
            nlohmann::json pairObj;
            pairObj["actorA"] = {
                {"formId", std::format("{:08X}", key.actorA)},
                {"name", ActorSvc::GetName(key.actorA)}
            };
            pairObj["actorB"] = {
                {"formId", std::format("{:08X}", key.actorB)},
                {"name", ActorSvc::GetName(key.actorB)}
            };
            pairObj["status"] = {
                {"id", static_cast<uint8_t>(data.status)},
                {"name", Utils::RelationshipStatusToString(data.status)}
            };
            pairObj["originalStatus"] = {
                {"id", static_cast<uint8_t>(data.originalStatus)},
                {"name", Utils::RelationshipStatusToString(data.originalStatus)}
            };
            pairObj["isOpen"] = data.isOpen;

            // Status history
            nlohmann::json histArray = nlohmann::json::array();
            for (const auto& h : data.statusHistory) {
                nlohmann::json hObj;
                RelationshipStatus fromSt = h.fromStatus;
                if (h.timestamp < 0 && fromSt == RelationshipStatus::Candidate) {
                    fromSt = RelationshipStatus::Unknown;
                }
                hObj["fromStatus"] = {
                    {"id", static_cast<uint8_t>(fromSt)},
                    {"name", Utils::RelationshipStatusToString(fromSt)}
                };
                hObj["toStatus"] = {
                    {"id", static_cast<uint8_t>(h.toStatus)},
                    {"name", Utils::RelationshipStatusToString(h.toStatus)}
                };
                if (std::isnan(h.timestamp) || h.timestamp < 0.0f) {
                    hObj["timestamp"] = -1;
                } else {
                    hObj["timestamp"] = h.timestamp;
                }

                RE::FormID initID = h.initiatorFormID;
                if (initID == 0 || (h.timestamp < 0 && (key.actorA != kPlayerFormID && key.actorB != kPlayerFormID) && initID == kPlayerFormID)) {
                    initID = DetermineDefaultInitiator(key.actorA, key.actorB);
                }
                std::string initName = ActorSvc::GetName(initID);
                if (initName.empty() && initID != 0) {
                    initName = std::format("Actor {:08X}", initID);
                }
                hObj["initiator"] = {
                    {"formId", std::format("{:08X}", initID)},
                    {"name", initName}
                };
                histArray.push_back(hObj);
            }
            pairObj["statusHistory"] = histArray;

            // Cheating records
            nlohmann::json cheatArray = nlohmann::json::array();
            for (const auto& c : data.cheatingRecords) {
                nlohmann::json cObj;
                cObj["cheater"] = {
                    {"formId", std::format("{:08X}", c.cheaterID)},
                    {"name", ActorSvc::GetName(c.cheaterID)}
                };
                cObj["victim"] = {
                    {"formId", std::format("{:08X}", c.victimID)},
                    {"name", ActorSvc::GetName(c.victimID)}
                };
                cObj["affairPartner"] = {
                    {"formId", std::format("{:08X}", c.affairPartnerID)},
                    {"name", ActorSvc::GetName(c.affairPartnerID)}
                };
                cObj["count"] = c.count;
                cObj["lastTimestamp"] = c.lastTimestamp;
                cObj["isSecret"] = c.isSecret;
                cObj["affairType"] = {
                    {"id", static_cast<uint8_t>(c.affairType)},
                    {"name", Utils::AffairTypeToString(c.affairType)}
                };
                cObj["affairStatus"] = {
                    {"id", static_cast<uint8_t>(c.affairStatus)},
                    {"name", Utils::AffairStatusToString(c.affairStatus)}
                };
                cheatArray.push_back(cObj);
            }
            pairObj["cheatingRecords"] = cheatArray;

            // Children associated with this pair
            nlohmann::json childrenArray = nlohmann::json::array();
            for (auto cID : GetChildren(key.actorA, key.actorB)) {
                auto entryOpt = GetChildEntry(cID);
                if (entryOpt.has_value()) {
                    nlohmann::json cObj;
                    cObj["childId"] = std::format("{:08X}", cID);
                    cObj["name"] = entryOpt->name;
                    cObj["timestamp"] = entryOpt->timestamp;
                    cObj["isAdopted"] = entryOpt->isAdopted;
                    cObj["isBiologicalChild"] = entryOpt->isBiologicalChild;
                    cObj["stage"] = {
                        {"id", static_cast<uint8_t>(entryOpt->stage)},
                        {"name", stageToString(entryOpt->stage)}
                    };
                    if (!entryOpt->trainingLocation.empty()) {
                        cObj["trainingLocation"] = entryOpt->trainingLocation;
                    }
                    auto* actor = HasPhysicalActor(cID)
                        ? NPCIndex::GetSingleton().GetActorFromFormId(cID)
                        : nullptr;
                    bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
                    cObj["hasRefID"] = hasRealActor;
                    cObj["isEphemeral"] = !hasRealActor || (entryOpt->stage == ChildStage::Infant) || (entryOpt->stage == ChildStage::Training);
                    childrenArray.push_back(cObj);
                }
            }
            pairObj["children"] = childrenArray;

            pairsArray.push_back(pairObj);
        }
        root["relationshipPairs"] = pairsArray;

        // 3. Child Registry
        nlohmann::json childrenRegArray = nlohmann::json::array();
        for (const auto& [childID, entry] : childrenByID) {
            nlohmann::json cObj;
            cObj["child"] = {
                {"formId", std::format("{:08X}", childID)},
                {"name", entry.name.empty() ? ActorSvc::GetName(childID) : entry.name}
            };
            cObj["guardianA"] = {
                {"formId", std::format("{:08X}", entry.guardianAID)},
                {"name", entry.guardianAID == 0 ? "" : ActorSvc::GetName(entry.guardianAID)}
            };
            cObj["guardianB"] = {
                {"formId", std::format("{:08X}", entry.guardianBID)},
                {"name", entry.guardianBID == 0 ? "" : ActorSvc::GetName(entry.guardianBID)}
            };
            cObj["timestamp"] = entry.timestamp;
            cObj["isAdopted"] = entry.isAdopted;
            cObj["bioMother"] = {
                {"formId", std::format("{:08X}", entry.bioMotherID)},
                {"name", entry.bioMotherID == 0 ? "" : ActorSvc::GetName(entry.bioMotherID)}
            };
            cObj["bioFather"] = {
                {"formId", std::format("{:08X}", entry.bioFatherID)},
                {"name", entry.bioFatherID == 0 ? "" : (entry.bioFatherID == kUnknownFatherFormID ? "Unknown" : ActorSvc::GetName(entry.bioFatherID))}
            };
            cObj["isBiologicalChild"] = entry.isBiologicalChild;
            cObj["conceptionTimestamp"] = entry.conceptionTimestamp;
            cObj["birthTimestamp"] = entry.birthTimestamp;
            cObj["stage"] = {
                {"id", static_cast<uint8_t>(entry.stage)},
                {"name", stageToString(entry.stage)}
            };
            if (!entry.trainingLocation.empty()) {
                cObj["trainingLocation"] = entry.trainingLocation;
            }
            auto* regActor = HasPhysicalActor(childID)
                ? NPCIndex::GetSingleton().GetActorFromFormId(childID)
                : nullptr;
            bool regHasRealActor = (regActor != nullptr && !regActor->IsDeleted());
            cObj["hasRefID"] = regHasRealActor;
            cObj["isEphemeral"] = !regHasRealActor || (entry.stage == ChildStage::Unborn) || (entry.stage == ChildStage::Infant) || (entry.stage == ChildStage::Training);
            cObj["paternitySecret"] = {
                {"isSecret", entry.paternitySecret.isSecret},
                {"isSecretForBioFather", entry.paternitySecret.isSecretForBioFather},
                {"isSecretForPartner", entry.paternitySecret.isSecretForPartner},
                {"discovered", entry.paternitySecret.discovered},
                {"discoveredByPartner", entry.paternitySecret.discoveredByPartner},
                {"discoveredByBioFather", entry.paternitySecret.discoveredByBioFather},
                {"hasPhenotypeMismatch", entry.paternitySecret.hasPhenotypeMismatch},
                {"lastCheckTimestamp", entry.paternitySecret.lastCheckTimestamp},
                {"bioFatherName", entry.paternitySecret.bioFatherName},
                {"bioFatherRaceFormId", entry.paternitySecret.bioFatherRaceID != 0 ? std::format("{:08X}", entry.paternitySecret.bioFatherRaceID) : ""}
            };
            childrenRegArray.push_back(cObj);
        }
        root["childrenRegistry"] = childrenRegArray;

        // 4. Pending Relationships
        nlohmann::json pendingRelsArray = nlohmann::json::array();
        for (const auto& [baseID, relsList] : pendingRelationshipsByBaseID) {
            for (const auto& r : relsList) {
                nlohmann::json rObj;
                rObj["baseFormId"] = std::format("{:08X}", baseID);
                rObj["partnerBaseId"] = std::format("{:08X}", r.partnerBaseID);
                rObj["partnerRefId"] = std::format("{:08X}", r.partnerRefID);
                rObj["targetStatus"] = {
                    {"id", static_cast<uint8_t>(r.targetStatus)},
                    {"name", Utils::RelationshipStatusToString(r.targetStatus)}
                };
                rObj["originalStatus"] = {
                    {"id", static_cast<uint8_t>(r.originalStatus)},
                    {"name", Utils::RelationshipStatusToString(r.originalStatus)}
                };
                pendingRelsArray.push_back(rObj);
            }
        }
        root["pendingRelationships"] = pendingRelsArray;

        // 5. Pending Children
        nlohmann::json pendingChildrenArray = nlohmann::json::array();
        for (const auto& [cBaseID, cData] : pendingChildrenByBaseID) {
            nlohmann::json cObj;
            cObj["childBaseId"] = std::format("{:08X}", cBaseID);
            cObj["childRefId"] = std::format("{:08X}", cData.childRefID);
            cObj["parentABaseId"] = std::format("{:08X}", cData.parentABaseID);
            cObj["parentARefId"] = std::format("{:08X}", cData.parentARefID);
            cObj["parentBBaseId"] = std::format("{:08X}", cData.parentBBaseID);
            cObj["parentBRefId"] = std::format("{:08X}", cData.parentBRefID);
            cObj["isAdopted"] = cData.isAdopted;
            pendingChildrenArray.push_back(cObj);
        }
        root["pendingChildren"] = pendingChildrenArray;

        // 6. Active Pregnancies
        nlohmann::json pregArray = nlohmann::json::array();
        for (const auto& [cID, entry] : childrenByID) {
            if (entry.stage != ChildStage::Unborn) continue;
            nlohmann::json pObj;
            pObj["mother"] = {
                {"formId", std::format("{:08X}", entry.bioMotherID)},
                {"name", ActorSvc::GetName(entry.bioMotherID)}
            };
            pObj["isPregnant"] = true;
            pObj["conceptionTimestamp"] = entry.conceptionTimestamp;
            RE::FormID presumedFatherID = GetMotherPartner(cID);
            pObj["presumedFather"] = {
                {"formId", presumedFatherID != 0 ? std::format("{:08X}", presumedFatherID) : ""},
                {"name", presumedFatherID != 0 ? ActorSvc::GetName(presumedFatherID) : ""}
            };
            pObj["bioFather"] = {
                {"formId", std::format("{:08X}", entry.bioFatherID)},
                {"name", (entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) ? ActorSvc::GetName(entry.bioFatherID) : (entry.bioFatherID == kUnknownFatherFormID ? "Unknown" : "")}
            };
            pregArray.push_back(pObj);
        }
        root["pregnancyState"] = pregArray;

        root["nextSyntheticChildId"] = std::format("{:08X}", nextSyntheticChildID_);

        return root;
    }

}  // namespace MARAS

