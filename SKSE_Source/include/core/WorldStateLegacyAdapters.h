#pragma once

#include "core/AdultChildTravelService.h"
#include "core/DeceasedRegistry.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerChildrenService.h"
#include "maras/state/WorldTables.h"

namespace MARAS::LegacyAdapters {

    [[nodiscard]] inline NPCProfileData ToLegacyNPCProfile(const State::ActorProfileRecord& rec) {
        NPCProfileData out(
            rec.refId.raw(),
            static_cast<SocialClass>(rec.socialClass),
            static_cast<SkillType>(rec.skillType),
            static_cast<Temperament>(rec.temperament));
        if (rec.legacyOriginalHome.has_value()) {
            out.originalHome = rec.legacyOriginalHome->raw();
        }
        if (rec.legacyCurrentHome.has_value()) {
            out.currentHome = rec.legacyCurrentHome->raw();
        }
        if (rec.legacyHomeMarker.has_value()) {
            out.homeMarker = rec.legacyHomeMarker->raw();
        }
        for (const auto kw : rec.addedKeywords) {
            out.addedKeywords.insert(kw.raw());
        }
        return out;
    }

    [[nodiscard]] inline State::ActorProfileRecord ToActorProfileRecord(
        const NPCProfileData& profile,
        Foundation::BaseId baseId = Foundation::BaseId{},
        bool isRegisteredAdult = true) {
        State::ActorProfileRecord out{};
        out.refId = Foundation::RefId{ profile.formID };
        out.baseId = baseId;
        out.socialClass = static_cast<State::SocialClass>(profile.socialClass);
        out.skillType = static_cast<State::SkillType>(profile.skillType);
        out.temperament = static_cast<State::Temperament>(profile.temperament);
        out.isRegisteredAdult = isRegisteredAdult;
        if (profile.originalHome.has_value()) {
            out.legacyOriginalHome = Foundation::FormRawId{ *profile.originalHome };
        }
        if (profile.currentHome.has_value()) {
            out.legacyCurrentHome = Foundation::FormRawId{ *profile.currentHome };
        }
        if (profile.homeMarker.has_value()) {
            out.legacyHomeMarker = Foundation::RefId{ *profile.homeMarker };
        }
        for (const RE::FormID kw : profile.addedKeywords) {
            out.addedKeywords.insert(Foundation::FormRawId{ kw });
        }
        return out;
    }

    [[nodiscard]] inline StatusChangeRecord ToLegacyStatusChange(const State::StatusTransitionEntry& rec) {
        return StatusChangeRecord(
            static_cast<RelationshipStatus>(rec.fromStatus),
            static_cast<RelationshipStatus>(rec.toStatus),
            rec.timestamp,
            rec.initiator.raw());
    }

    [[nodiscard]] inline State::StatusTransitionEntry ToStatusTransition(const StatusChangeRecord& rec) {
        State::StatusTransitionEntry out{};
        out.fromStatus = static_cast<State::RelationshipStatus>(rec.fromStatus);
        out.toStatus = static_cast<State::RelationshipStatus>(rec.toStatus);
        out.timestamp = rec.timestamp;
        out.initiator = Foundation::ActorId{ rec.initiatorFormID };
        return out;
    }

    [[nodiscard]] inline CheatingRecord ToLegacyCheatingRecord(const State::AffairRecord& rec) {
        return CheatingRecord(
            rec.cheater.raw(),
            rec.deceivedPartner.raw(),
            rec.paramour.raw(),
            rec.incidentCount,
            rec.lastTimestamp,
            rec.isSecret,
            static_cast<AffairType>(rec.affairType),
            static_cast<AffairStatus>(rec.affairStatus),
            static_cast<AffairStage>(rec.affairStage),
            rec.stageStartDay);
    }

    [[nodiscard]] inline NPCRelationshipData ToLegacyRelationshipData(
        const State::RelationshipEdgeRecord& edge,
        const std::vector<State::AffairRecord>& coupleAffairs) {
        NPCRelationshipData out(
            edge.pair.first.raw(),
            edge.pair.second.raw(),
            static_cast<RelationshipStatus>(edge.status));
        out.originalStatus = static_cast<RelationshipStatus>(edge.originalStatus);
        out.isOpen = edge.isOpen;
        out.statusHistory.reserve(edge.statusHistory.size());
        for (const auto& entry : edge.statusHistory) {
            out.statusHistory.push_back(ToLegacyStatusChange(entry));
        }
        out.cheatingRecords.reserve(coupleAffairs.size());
        for (const auto& affair : coupleAffairs) {
            out.cheatingRecords.push_back(ToLegacyCheatingRecord(affair));
        }
        return out;
    }

    [[nodiscard]] inline PendingRelationshipData ToLegacyPendingRelationship(const State::PendingRelationshipEntry& rec) {
        return PendingRelationshipData(
            rec.partnerBaseId.raw(),
            rec.partnerRefId.raw(),
            static_cast<RelationshipStatus>(rec.targetStatus),
            static_cast<RelationshipStatus>(rec.originalStatus));
    }

    [[nodiscard]] inline State::PendingRelationshipEntry ToPendingRelationshipEntry(const PendingRelationshipData& rec) {
        State::PendingRelationshipEntry out{};
        out.partnerBaseId = Foundation::BaseId{ rec.partnerBaseID };
        out.partnerRefId = Foundation::RefId{ rec.partnerRefID };
        out.targetStatus = static_cast<State::RelationshipStatus>(rec.targetStatus);
        out.originalStatus = static_cast<State::RelationshipStatus>(rec.originalStatus);
        return out;
    }

    [[nodiscard]] inline PendingChildData ToLegacyPendingChild(const State::PendingChildEntry& rec) {
        return PendingChildData(
            rec.childBaseId.raw(),
            rec.childRefId.raw(),
            rec.parentABaseId.raw(),
            rec.parentARefId.raw(),
            rec.parentBBaseId.raw(),
            rec.parentBRefId.raw(),
            rec.isAdopted);
    }

    [[nodiscard]] inline State::PendingChildEntry ToPendingChildEntry(const PendingChildData& rec) {
        State::PendingChildEntry out{};
        out.childBaseId = Foundation::BaseId{ rec.childBaseID };
        out.childRefId = Foundation::RefId{ rec.childRefID };
        out.parentABaseId = Foundation::BaseId{ rec.parentABaseID };
        out.parentARefId = Foundation::RefId{ rec.parentARefID };
        out.parentBBaseId = Foundation::BaseId{ rec.parentBBaseID };
        out.parentBRefId = Foundation::RefId{ rec.parentBRefID };
        out.isAdopted = rec.isAdopted;
        return out;
    }

    [[nodiscard]] inline PendingPartnerIntent ToLegacyPendingPartnerIntent(const State::PendingPartnerIntentRecord& rec) {
        return PendingPartnerIntent(
            static_cast<PartnerIntentType>(rec.type),
            rec.createdGameDay,
            rec.relatedLover.raw());
    }

    [[nodiscard]] inline State::PendingPartnerIntentRecord ToPendingPartnerIntentRecord(const PendingPartnerIntent& rec) {
        State::PendingPartnerIntentRecord out{};
        out.type = static_cast<State::PartnerIntentType>(rec.type);
        out.createdGameDay = rec.createdGameDay;
        out.relatedLover = Foundation::ActorId{ rec.relatedLoverID };
        return out;
    }

    [[nodiscard]] inline PaternitySecret ToLegacyPaternitySecret(const State::PaternitySecretState& rec) {
        return PaternitySecret(
            rec.isSecret,
            rec.discovered,
            rec.discoveredByPartner,
            rec.hasPhenotypeMismatch,
            rec.lastCheckTimestamp,
            rec.bioFatherName,
            rec.bioFatherRaceId.raw(),
            rec.isSecretForBioFather,
            rec.isSecretForPartner,
            rec.discoveredByBioFather,
            rec.isPregnancySecretForPartner);
    }

    [[nodiscard]] inline State::PaternitySecretState ToPaternitySecretState(const PaternitySecret& rec) {
        State::PaternitySecretState out{};
        out.isSecret = rec.isSecret;
        out.isSecretForBioFather = rec.isSecretForBioFather;
        out.isSecretForPartner = rec.isSecretForPartner;
        out.isPregnancySecretForPartner = rec.isPregnancySecretForPartner;
        out.discovered = rec.discovered;
        out.discoveredByPartner = rec.discoveredByPartner;
        out.discoveredByBioFather = rec.discoveredByBioFather;
        out.hasPhenotypeMismatch = rec.hasPhenotypeMismatch;
        out.lastCheckTimestamp = rec.lastCheckTimestamp;
        out.bioFatherName = rec.bioFatherName;
        out.bioFatherRaceId = Foundation::FormRawId{ rec.bioFatherRaceID };
        return out;
    }

    [[nodiscard]] inline ChildRegistryEntry ToLegacyChildEntry(const State::ChildRecord& rec) {
        ChildRegistryEntry out(
            rec.childId.raw(),
            rec.guardians.slotA.raw(),
            rec.guardians.slotB.raw(),
            rec.name,
            rec.timestamp,
            rec.isAdopted,
            rec.bioMother.raw(),
            rec.bioFather.raw(),
            rec.isBiologicalChild,
            rec.conceptionTimestamp,
            rec.birthTimestamp,
            ToLegacyPaternitySecret(rec.paternitySecret),
            static_cast<ChildStage>(rec.stage),
            rec.trainingLocation,
            rec.gender,
            rec.presumedFather.raw(),
            rec.trainingClassIndex,
            rec.trainingStartGameDay,
            rec.savedBioTemplateName,
            rec.savedRaceIndex);
        out.hairColorFormID = rec.hairColorFormId.raw();
        out.eyesPartFormID = rec.eyesPartFormId.raw();
        out.hairPartFormID = rec.hairPartFormId.raw();
        out.facialHairPartFormID = rec.facialHairPartFormId.raw();
        out.heightScale = rec.heightScale;
        out.rpgApplied = rec.rpgApplied;
        out.hasBeastblood = rec.hasBeastblood;
        out.beastbloodEvaluated = rec.beastbloodEvaluated;
        out.bioLineageEvaluated = rec.bioLineageEvaluated;
        out.guardianLineageEvaluated = rec.guardianLineageEvaluated;
        out.bioSkillBonuses = rec.bioSkillBonuses;
        out.guardianSkillBonuses = rec.guardianSkillBonuses;
        out.daedricBlessingId = rec.daedricBlessingId;
        out.lastAppliedLevel = rec.lastAppliedLevel;
        return out;
    }

    [[nodiscard]] inline State::ChildRecord ToChildRecord(const ChildRegistryEntry& rec) {
        State::ChildRecord out{};
        out.childId = Foundation::ChildId{ rec.childID };
        out.guardians.slotA = Foundation::ActorId{ rec.guardianAID };
        out.guardians.slotB = Foundation::ActorId{ rec.guardianBID };
        out.name = rec.name;
        out.timestamp = rec.timestamp;
        out.isAdopted = rec.isAdopted;
        out.bioMother = Foundation::ActorId{ rec.bioMotherID };
        out.bioFather = Foundation::ActorId{ rec.bioFatherID };
        out.presumedFather = Foundation::ActorId{ rec.presumedFatherID };
        out.isBiologicalChild = rec.isBiologicalChild;
        out.conceptionTimestamp = rec.conceptionTimestamp;
        out.birthTimestamp = rec.birthTimestamp;
        out.paternitySecret = ToPaternitySecretState(rec.paternitySecret);
        out.stage = static_cast<State::ChildStage>(rec.stage);
        out.trainingLocation = rec.trainingLocation;
        out.gender = rec.gender;
        out.trainingClassIndex = rec.trainingClassIndex;
        out.trainingStartGameDay = rec.trainingStartGameDay;
        out.savedBioTemplateName = rec.savedBioTemplateName;
        out.savedRaceIndex = rec.savedRaceIndex;
        out.hairColorFormId = Foundation::FormRawId{ rec.hairColorFormID };
        out.eyesPartFormId = Foundation::FormRawId{ rec.eyesPartFormID };
        out.hairPartFormId = Foundation::FormRawId{ rec.hairPartFormID };
        out.facialHairPartFormId = Foundation::FormRawId{ rec.facialHairPartFormID };
        out.heightScale = rec.heightScale;
        out.rpgApplied = rec.rpgApplied;
        out.hasBeastblood = rec.hasBeastblood;
        out.beastbloodEvaluated = rec.beastbloodEvaluated;
        out.bioLineageEvaluated = rec.bioLineageEvaluated;
        out.guardianLineageEvaluated = rec.guardianLineageEvaluated;
        out.bioSkillBonuses = rec.bioSkillBonuses;
        out.guardianSkillBonuses = rec.guardianSkillBonuses;
        out.daedricBlessingId = rec.daedricBlessingId;
        out.lastAppliedLevel = rec.lastAppliedLevel;
        return out;
    }

    [[nodiscard]] inline ChildHouseholdState ToLegacyChildHouseholdState(const State::ChildHouseholdRecord& rec) {
        ChildHouseholdState out{};
        out.childID = rec.childId.raw();
        out.currentHouseFormID = rec.currentHouseId.raw();
        out.homeMarkerFormID = rec.homeMarkerId.raw();
        out.petFormID = rec.petId.raw();
        out.critterFormID = rec.critterId.raw();
        out.currentOrder = rec.currentOrder;
        out.orderExpiryGameDay = rec.orderExpiryGameDay;
        out.critterBanned = rec.critterBanned;
        out.petBanned = rec.petBanned;
        out.isNewlyAdopted = rec.isNewlyAdopted;
        out.forcegreetPending = rec.forcegreetPending;
        out.critterRetryTimestamp = rec.critterRetryTimestamp;
        out.petRetryTimestamp = rec.petRetryTimestamp;
        out.playedScene1 = rec.playedScene1;
        out.playedScene2 = rec.playedScene2;
        out.lastInteractionTimestamp = rec.lastInteractionTimestamp;
        return out;
    }

    [[nodiscard]] inline State::ChildHouseholdRecord ToChildHouseholdRecord(const ChildHouseholdState& rec) {
        State::ChildHouseholdRecord out{};
        out.childId = Foundation::ChildId{ rec.childID };
        out.currentHouseId = Foundation::HouseId{ rec.currentHouseFormID };
        out.homeMarkerId = Foundation::RefId{ rec.homeMarkerFormID };
        out.petId = Foundation::RefId{ rec.petFormID };
        out.critterId = Foundation::RefId{ rec.critterFormID };
        out.currentOrder = rec.currentOrder;
        out.orderExpiryGameDay = rec.orderExpiryGameDay;
        out.critterBanned = rec.critterBanned;
        out.petBanned = rec.petBanned;
        out.isNewlyAdopted = rec.isNewlyAdopted;
        out.forcegreetPending = rec.forcegreetPending;
        out.critterRetryTimestamp = rec.critterRetryTimestamp;
        out.petRetryTimestamp = rec.petRetryTimestamp;
        out.playedScene1 = rec.playedScene1;
        out.playedScene2 = rec.playedScene2;
        out.lastInteractionTimestamp = rec.lastInteractionTimestamp;
        return out;
    }

    [[nodiscard]] inline ChildFactionSnapshot ToLegacyChildFactionSnapshot(const State::ChildFactionSnapshotRecord& rec) {
        ChildFactionSnapshot out{};
        out.crimeFactionID = rec.crimeFactionId.raw();
        out.factions.reserve(rec.factions.size());
        for (const auto& entry : rec.factions) {
            out.factions.push_back(FactionRankEntry{ entry.factionId.raw(), entry.rank });
        }
        return out;
    }

    [[nodiscard]] inline State::ChildFactionSnapshotRecord ToChildFactionSnapshotRecord(const ChildFactionSnapshot& rec) {
        State::ChildFactionSnapshotRecord out{};
        out.crimeFactionId = Foundation::FormRawId{ rec.crimeFactionID };
        out.factions.reserve(rec.factions.size());
        for (const auto& entry : rec.factions) {
            out.factions.push_back(State::FactionRankSnapshotEntry{ Foundation::FormRawId{ entry.factionID }, entry.rank });
        }
        return out;
    }

    [[nodiscard]] inline AdultChildTravelState ToLegacyAdultChildTravelState(const State::AdultChildTravelRecord& rec) {
        AdultChildTravelState out{};
        out.targetMarkerID = rec.targetMarkerId.raw();
        out.targetLocationID = rec.targetLocationId.raw();
        out.targetCellID = rec.targetCellId.raw();
        out.destinationName = rec.destinationName;
        out.assignedGameDay = rec.assignedGameDay;
        out.arrivedGameDay = rec.arrivedGameDay;
        out.stayDurationDays = rec.stayDurationDays;
        out.isFamilyVisit = rec.isFamilyVisit;
        return out;
    }

    [[nodiscard]] inline State::AdultChildTravelRecord ToAdultChildTravelRecord(const AdultChildTravelState& rec) {
        State::AdultChildTravelRecord out{};
        out.targetMarkerId = Foundation::RefId{ rec.targetMarkerID };
        out.targetLocationId = Foundation::LocationId{ rec.targetLocationID };
        out.targetCellId = Foundation::CellId{ rec.targetCellID };
        out.destinationName = rec.destinationName;
        out.assignedGameDay = rec.assignedGameDay;
        out.arrivedGameDay = rec.arrivedGameDay;
        out.stayDurationDays = rec.stayDurationDays;
        out.isFamilyVisit = rec.isFamilyVisit;
        return out;
    }

    [[nodiscard]] inline DeceasedNPCRecord ToLegacyDeceasedRecord(const State::DeceasedActorRecord& rec) {
        DeceasedNPCRecord out{};
        out.baseFormID = rec.baseId.raw();
        out.refFormID = rec.refId.raw();
        out.name = rec.name;
        out.deathTimestamp = rec.deathTimestamp;
        out.isChild = rec.isChild;
        out.wasKilledByPlayer = rec.wasKilledByPlayer;
        out.killerBaseID = rec.killerBaseId.raw();
        out.killerRefID = rec.killerRefId.raw();
        out.killerName = rec.killerName;
        out.socialClass = static_cast<SocialClass>(rec.socialClass);
        out.skillType = static_cast<SkillType>(rec.skillType);
        out.temperament = static_cast<Temperament>(rec.temperament);
        out.partnersAtDeath.reserve(rec.partnersAtDeath.size());
        for (const auto& p : rec.partnersAtDeath) {
            out.partnersAtDeath.emplace_back(
                p.partnerBaseId.raw(),
                p.partnerRefId.raw(),
                p.partnerName,
                static_cast<RelationshipStatus>(p.statusAtDeath),
                p.isPlayer);
        }
        if (rec.childInfo.has_value()) {
            DeceasedChildInfo ci{};
            ci.guardianABaseID = rec.childInfo->guardianABaseId.raw();
            ci.guardianARefID = rec.childInfo->guardianARefId.raw();
            ci.guardianAName = rec.childInfo->guardianAName;
            ci.guardianBBaseID = rec.childInfo->guardianBBaseId.raw();
            ci.guardianBRefID = rec.childInfo->guardianBRefId.raw();
            ci.guardianBName = rec.childInfo->guardianBName;
            ci.bioMotherBaseID = rec.childInfo->bioMotherBaseId.raw();
            ci.bioFatherBaseID = rec.childInfo->bioFatherBaseId.raw();
            ci.isAdopted = rec.childInfo->isAdopted;
            ci.houseFormID = rec.childInfo->houseId.raw();
            out.childInfo = std::move(ci);
        }
        return out;
    }

    [[nodiscard]] inline State::DeceasedActorRecord ToDeceasedActorRecord(const DeceasedNPCRecord& rec) {
        State::DeceasedActorRecord out{};
        out.baseId = Foundation::BaseId{ rec.baseFormID };
        out.refId = Foundation::RefId{ rec.refFormID };
        out.name = rec.name;
        out.deathTimestamp = rec.deathTimestamp;
        out.isChild = rec.isChild;
        out.wasKilledByPlayer = rec.wasKilledByPlayer;
        out.killerBaseId = Foundation::BaseId{ rec.killerBaseID };
        out.killerRefId = Foundation::RefId{ rec.killerRefID };
        out.killerName = rec.killerName;
        out.socialClass = static_cast<State::SocialClass>(rec.socialClass);
        out.skillType = static_cast<State::SkillType>(rec.skillType);
        out.temperament = static_cast<State::Temperament>(rec.temperament);
        out.partnersAtDeath.reserve(rec.partnersAtDeath.size());
        for (const auto& p : rec.partnersAtDeath) {
            out.partnersAtDeath.push_back(State::DeceasedPartnerEntry{
                .partnerBaseId = Foundation::BaseId{ p.partnerBaseID },
                .partnerRefId = Foundation::RefId{ p.partnerRefID },
                .partnerName = p.partnerName,
                .statusAtDeath = static_cast<State::RelationshipStatus>(p.statusAtDeath),
                .isPlayer = p.isPlayer
            });
        }
        if (rec.childInfo.has_value()) {
            State::DeceasedChildMetadata ci{};
            ci.guardianABaseId = Foundation::BaseId{ rec.childInfo->guardianABaseID };
            ci.guardianARefId = Foundation::RefId{ rec.childInfo->guardianARefID };
            ci.guardianAName = rec.childInfo->guardianAName;
            ci.guardianBBaseId = Foundation::BaseId{ rec.childInfo->guardianBBaseID };
            ci.guardianBRefId = Foundation::RefId{ rec.childInfo->guardianBRefID };
            ci.guardianBName = rec.childInfo->guardianBName;
            ci.bioMotherBaseId = Foundation::BaseId{ rec.childInfo->bioMotherBaseID };
            ci.bioFatherBaseId = Foundation::BaseId{ rec.childInfo->bioFatherBaseID };
            ci.isAdopted = rec.childInfo->isAdopted;
            ci.houseId = Foundation::HouseId{ rec.childInfo->houseFormID };
            out.childInfo = std::move(ci);
        }
        return out;
    }

}  // namespace MARAS::LegacyAdapters
