#include "maras/bridge_skyrimnet/SkyrimNetDecorators.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"
#include "maras/bridge_skyrimnet/SkyrimNetDecisions.h"
#include "maras/bridge_skyrimnet/SkyrimNetEventService.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/domain/lineage/ChildProfileGuidance.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"

#include <nlohmann/json.hpp>
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        float GetNearbyActorsRadius() {
            const float settingRadius =
                MARAS::State::WorldStore::GetSingleton().Snapshot()->settings->skyrimnetActionRadius;
            if (settingRadius > 0.0f) {
                return settingRadius;
            }
            const std::string val = GetPluginConfigValueExport("maras", "nearbyActors.radius", "3000");
            if (!val.empty()) {
                try {
                    return std::stof(val);
                } catch (...) {
                }
            }
            return 3000.0f;
        }

        // In-house relationship lookup — no external DLL dependency.
        // Returns the first NPC base related to actor matching the given filters:
        //   associationFormID — Skyrim.esm FormID of the association type (0 = skip this filter).
        //   exactRank         — exact relationship rank (INT32_MIN = skip; use association filter only).
        // Rank mapping: kLover(0) -> +4, kArchnemesis(8) -> -4.
        RE::TESNPC* FindRelatedNpcBase(RE::Actor* actor,
                                       RE::FormID associationFormID,
                                       std::int32_t exactRank) noexcept {
            if (!actor) return nullptr;
            auto* npcBase = MARAS::ActorSvc::GetActorBase(actor->GetFormID());
            if (!npcBase || !npcBase->relationships) return nullptr;

            for (const auto* rel : *npcBase->relationships) {
                if (!rel) continue;

                if (exactRank != INT32_MIN) {
                    const int rank = 4 - static_cast<int>(rel->level.underlying());
                    if (rank != exactRank) continue;
                } else if (associationFormID != 0) {
                    if (!rel->assocType || rel->assocType->GetFormID() != associationFormID) continue;
                }

                RE::TESNPC* other = (rel->npc1 == npcBase) ? rel->npc2 : rel->npc1;
                if (other) return other;
            }
            return nullptr;
        }

        std::string CleanAnimalRaceName(std::string raceName) {
            if (raceName.empty()) return "animal";
            std::string s = std::move(raceName);

            auto endsWithCaseInsensitive = [](const std::string& str, const std::string& suffix) {
                if (str.length() < suffix.length()) return false;
                return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });
            };

            if (endsWithCaseInsensitive(s, "Race")) {
                s = s.substr(0, s.length() - 4);
            }
            if (endsWithCaseInsensitive(s, "Companion")) {
                s = s.substr(0, s.length() - 9);
            }

            while (!s.empty() && (s.back() == ' ' || s.back() == '_')) {
                s.pop_back();
            }

            return MARAS::Foundation::StringUtils::ToLowerStd(s);
        }

        std::string FormatNamesSummary(const std::vector<std::string>& names) {
            if (names.empty()) return "";
            if (names.size() == 1) return names[0];
            if (names.size() == 2) return names[0] + " and " + names[1];

            std::string result;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) result += ", ";
                if (i == names.size() - 1) result += "and ";
                result += names[i];
            }
            return result;
        }

        nlohmann::json MakeNpcRef(RE::FormID formID,
                                  const MARAS::State::WorldStateSnapshot& world,
                                  const MARAS::State::SpatialActorSnapshot& spatial) {
            nlohmann::json j;
            j["uuid"] = GetActorUUID(formID);
            j["name"] = MARAS::ResolveActorNameSafe(formID, world, spatial);
            return j;
        }

        nlohmann::json MakeNpcListRef(const std::vector<RE::FormID>& formIDs,
                                      const MARAS::State::WorldStateSnapshot& world,
                                      const MARAS::State::SpatialActorSnapshot& spatial) {
            nlohmann::json j;
            std::vector<std::uint64_t> uuids;
            std::vector<std::string> names;
            uuids.reserve(formIDs.size());
            names.reserve(formIDs.size());
            for (auto fid : formIDs) {
                uuids.push_back(GetActorUUID(fid));
                names.push_back(MARAS::ResolveActorNameSafe(fid, world, spatial));
            }
            j["uuids"] = std::move(uuids);
            j["names"] = FormatNamesSummary(names);
            return j;
        }

        std::string BuildRelationshipProfileJSON(RE::Actor* actor) {
            if (!actor) return "{}";
            const RE::FormID actorFormID = actor->GetFormID();
            const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                worldSnap,
                spatialSnap,
                MARAS::State::ActorId{ actorFormID },
                &MARAS::GetEngineWerewolfStateProvider());
            const bool playerPolygamyEnabled = worldSnap->settings->enablePlayerPolygamy;
            const float currentDay = (spatialSnap->CurrentGameDay() >= 0.0f)
                ? spatialSnap->CurrentGameDay()
                : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);

            auto makeNpcRef = [&](RE::FormID id) -> nlohmann::json {
                return MakeNpcRef(id, *worldSnap, *spatialSnap);
            };
            auto makeNpcListRef = [&](const std::vector<RE::FormID>& ids) -> nlohmann::json {
                return MakeNpcListRef(ids, *worldSnap, *spatialSnap);
            };

            auto statusToKey = [](MARAS::State::RelationshipStatus status) -> std::string {
                switch (status) {
                    case MARAS::State::RelationshipStatus::Married:  return "married";
                    case MARAS::State::RelationshipStatus::Engaged:  return "engaged";
                    case MARAS::State::RelationshipStatus::Dating:   return "dating";
                    case MARAS::State::RelationshipStatus::Lover:    return "lover";
                    case MARAS::State::RelationshipStatus::Divorced: return "divorced";
                    case MARAS::State::RelationshipStatus::Jilted:   return "jilted";
                    case MARAS::State::RelationshipStatus::ExDating: return "exdating";
                    case MARAS::State::RelationshipStatus::ExLover:  return "exlover";
                    default: return "";
                }
            };
            auto isCurrentStatus = [](MARAS::State::RelationshipStatus status) {
                return status == MARAS::State::RelationshipStatus::Married ||
                       status == MARAS::State::RelationshipStatus::Engaged ||
                       status == MARAS::State::RelationshipStatus::Dating ||
                       status == MARAS::State::RelationshipStatus::Lover;
            };
            auto isMainTierStatus = [](MARAS::State::RelationshipStatus status) {
                return status == MARAS::State::RelationshipStatus::Dating ||
                       status == MARAS::State::RelationshipStatus::Engaged ||
                       status == MARAS::State::RelationshipStatus::Married;
            };
            auto isPastStatus = [](MARAS::State::RelationshipStatus status) {
                return status == MARAS::State::RelationshipStatus::Divorced ||
                       status == MARAS::State::RelationshipStatus::Jilted ||
                       status == MARAS::State::RelationshipStatus::ExDating ||
                       status == MARAS::State::RelationshipStatus::ExLover;
            };
            auto affairStatusToKey = [](MARAS::State::AffairStatus status) -> std::string {
                switch (status) {
                    case MARAS::State::AffairStatus::Ongoing:          return "ongoing";
                    case MARAS::State::AffairStatus::EndedBreakup:     return "ended_breakup";
                    case MARAS::State::AffairStatus::EndedVictimSplit: return "ended_victim_split";
                    case MARAS::State::AffairStatus::Allowed:          return "allowed";
                    case MARAS::State::AffairStatus::EndedConfronted:  return "ended_confronted";
                    default: return "unknown";
                }
            };

            nlohmann::json j;
            j["schema_version"] = 2;
            j["actor"] = makeNpcRef(actorFormID);
            j["family"] = nlohmann::json::array();
            const auto selfPregOpt = selfView.GetSelfPregnancy(MARAS::State::ActorId{ actorFormID });
            const bool isPregnant = selfPregOpt.has_value();
            j["is_pregnant"] = isPregnant;
            if (selfPregOpt.has_value()) {
                const auto& selfPreg = *selfPregOpt;
                RE::FormID motherPartnerID = selfPreg.motherPartner.raw();
                const auto motherPartnerPair = selfView.GetPerceivedPair(
                    MARAS::State::ActorId{ actorFormID }, selfPreg.motherPartner);
                if (motherPartnerID != 0 &&
                    (MARAS::MatchesActor(motherPartnerID, actorFormID) ||
                     MARAS::ResolveActorSexSafe(motherPartnerID, *worldSnap, *spatialSnap) == MARAS::State::ActorSex::Female ||
                     (motherPartnerPair && motherPartnerPair->status == MARAS::State::RelationshipStatus::Divorced))) {
                    motherPartnerID = 0;
                }

                nlohmann::json pregObj;
                pregObj["is_pregnant"] = true;
                pregObj["child_count"] = static_cast<int>(selfPreg.totalFetusCount > 0 ? selfPreg.totalFetusCount : 1);
                if (motherPartnerID != 0) {
                    pregObj["mother_partner"] = makeNpcRef(motherPartnerID);
                }

                nlohmann::json groupsArr = nlohmann::json::array();
                for (const auto& grp : selfPreg.groups) {
                    nlohmann::json gObj;
                    if (!grp.perceivedFather.IsNull() && grp.perceivedFather.raw() != MARAS::kUnknownFatherFormID) {
                        gObj["bio_father"] = makeNpcRef(grp.perceivedFather.raw());
                    } else {
                        nlohmann::json bioRef;
                        bioRef["uuid"] = 0;
                        bioRef["name"] = grp.fatherFallbackName.empty() ? "Unknown" : grp.fatherFallbackName;
                        gObj["bio_father"] = bioRef;
                    }
                    gObj["count"] = static_cast<int>(grp.fetusCount);
                    gObj["is_mother_partner"] = grp.isMotherPartner;
                    gObj["is_secret"] = grp.isSecret;
                    gObj["is_secret_for_partner"] = grp.isSecretForPartner;
                    gObj["is_secret_for_bio_father"] = grp.isSecretForBioFather;
                    gObj["is_pregnancy_secret_for_partner"] = grp.isPregnancySecretForPartner;
                    groupsArr.push_back(std::move(gObj));
                }
                pregObj["groups"] = std::move(groupsArr);

                if (!selfPreg.groups.empty()) {
                    const auto& firstGrp = selfPreg.groups.front();
                    if (!firstGrp.perceivedFather.IsNull() && firstGrp.perceivedFather.raw() != MARAS::kUnknownFatherFormID) {
                        pregObj["bio_father"] = makeNpcRef(firstGrp.perceivedFather.raw());
                    } else {
                        nlohmann::json bioRef;
                        bioRef["uuid"] = 0;
                        bioRef["name"] = firstGrp.fatherFallbackName.empty() ? "Unknown" : firstGrp.fatherFallbackName;
                        pregObj["bio_father"] = bioRef;
                    }
                    if (!firstGrp.fetusIds.empty()) {
                        const MARAS::State::ChildId frontId = firstGrp.fetusIds.front();
                        pregObj["unborn_child_id"] = frontId.raw();
                        if (const auto perceivedUnborn = selfView.GetPerceivedChild(frontId); perceivedUnborn.has_value()) {
                            pregObj["unborn_child_name"] = perceivedUnborn->name;
                        }
                    }
                }
                pregObj["has_paternity_secret"] = selfPreg.hasPaternitySecret;
                pregObj["is_secret"] = selfPreg.anySecret;
                pregObj["is_secret_for_partner"] = selfPreg.anySecretForPartner;
                pregObj["is_secret_for_bio_father"] = selfPreg.anySecretForBioFather;
                pregObj["is_pregnancy_secret_for_partner"] = selfPreg.anyPregnancySecretForPartner;

                j["pregnancy"] = std::move(pregObj);
            }

            j["expecting_pregnancies"] = nlohmann::json::array();
            if (!isPregnant) {
                for (const auto& exp : selfView.GetExpectingPregnanciesFor(MARAS::State::ActorId{ actorFormID })) {
                    nlohmann::json expObj;
                    expObj["mother"] = makeNpcRef(exp.mother.raw());
                    expObj["child_count"] = static_cast<int>(exp.fetusCount > 0 ? exp.fetusCount : 1);
                    switch (exp.role) {
                        case MARAS::Projection::ExpectingParentRole::PartnerBioFather:
                            expObj["role"] = "partner_bio_father";
                            break;
                        case MARAS::Projection::ExpectingParentRole::PresumedFatherUnaware:
                            expObj["role"] = "presumed_father_unaware";
                            break;
                        case MARAS::Projection::ExpectingParentRole::PartnerKnowsTruth:
                            expObj["role"] = "partner_knows_truth";
                            if (!exp.perceivedBioFather.IsNull() &&
                                exp.perceivedBioFather.raw() != MARAS::kUnknownFatherFormID) {
                                expObj["bio_father"] = makeNpcRef(exp.perceivedBioFather.raw());
                            } else {
                                nlohmann::json bioRef;
                                bioRef["uuid"] = 0;
                                bioRef["name"] = exp.perceivedBioFatherFallbackName.empty()
                                    ? "Unknown"
                                    : exp.perceivedBioFatherFallbackName;
                                expObj["bio_father"] = bioRef;
                            }
                            expObj["is_secret_for_bio_father"] = exp.isSecretForBioFather;
                            break;
                        case MARAS::Projection::ExpectingParentRole::BioFatherKnows:
                            expObj["role"] = "bio_father_knows";
                            if (!exp.motherPartner.IsNull()) {
                                expObj["mother_partner"] = makeNpcRef(exp.motherPartner.raw());
                                expObj["is_pregnancy_secret_for_partner"] = exp.isPregnancySecretForPartner;
                                expObj["is_secret_for_partner"] = exp.isSecretForPartner;
                            }
                            break;
                    }
                    j["expecting_pregnancies"].push_back(std::move(expObj));
                }
            }
            j["current_relationships"] = nlohmann::json::array();
            j["player_co_partners"] = nlohmann::json::array();
            j["past_relationships"] = nlohmann::json::array();
            j["infidelity"] = nlohmann::json::array();

            const MARAS::State::ActorId selfActorId{ actorFormID };
            const auto perceivedEdges = selfView.GetPerceivedRelationshipsOf(selfActorId);
            const auto knownAffairs = selfView.GetKnownAffairsInvolving(selfActorId);

            auto isActorCheatingWith = [&](RE::FormID partnerID) {
                return std::ranges::any_of(knownAffairs, [&](const MARAS::Projection::PerceivedAffair& record) {
                    return record.affairStatus == MARAS::State::AffairStatus::Ongoing &&
                           record.cheater.raw() == actorFormID &&
                           record.paramour.raw() == partnerID;
                });
            };
            auto isSecretAffairPair = [&](RE::FormID partnerID, bool ongoingOnly) {
                return std::ranges::any_of(knownAffairs, [&](const MARAS::Projection::PerceivedAffair& record) {
                    if (record.affairStatus == MARAS::State::AffairStatus::Allowed || !record.isSecret) return false;
                    if (ongoingOnly && record.affairStatus != MARAS::State::AffairStatus::Ongoing) return false;
                    return (record.cheater.raw() == actorFormID && record.paramour.raw() == partnerID) ||
                           (record.paramour.raw() == actorFormID && record.cheater.raw() == partnerID);
                });
            };
            auto findOpenPrimary = [&](RE::FormID excludedPartnerID) -> RE::FormID {
                for (const auto& edge : perceivedEdges) {
                    const RE::FormID partnerID = edge.Other(selfActorId).raw();
                    if (partnerID == excludedPartnerID) continue;
                    if (isMainTierStatus(edge.status) && edge.isOpen) {
                        return partnerID;
                    }
                }
                return 0;
            };
            auto hasOtherMainPair = [&](RE::FormID excludedPartnerID) {
                for (const auto& edge : perceivedEdges) {
                    const RE::FormID partnerID = edge.Other(selfActorId).raw();
                    if (partnerID == excludedPartnerID) continue;
                    if (isMainTierStatus(edge.status)) return true;
                }
                return false;
            };
            auto isPlayerActiveMain = [&](RE::FormID id) {
                if (id == 0 || id == MARAS::kPlayerFormID) return false;
                const auto edge = selfView.GetPerceivedPair(
                    MARAS::State::ActorId::Player(), MARAS::State::ActorId{ id });
                return edge.has_value() && isMainTierStatus(edge->status);
            };

            for (const auto& edge : perceivedEdges) {
                const RE::FormID partnerID = edge.Other(selfActorId).raw();
                if (isCurrentStatus(edge.status) && !isActorCheatingWith(partnerID)) {
                    nlohmann::json relationship;
                    relationship["partner"] = makeNpcRef(partnerID);
                    relationship["status"] = statusToKey(edge.status);
                    if (isMainTierStatus(edge.status)) {
                        relationship["is_open"] = edge.isOpen;
                    }
                    if (isSecretAffairPair(partnerID, /*ongoingOnly=*/true)) {
                        relationship["is_secret_affair"] = true;
                    }

                    const RE::FormID openPrimaryID = findOpenPrimary(partnerID);
                    if ((actorFormID == MARAS::kPlayerFormID && playerPolygamyEnabled) ||
                        (isPlayerActiveMain(actorFormID) && isPlayerActiveMain(partnerID))) {
                        relationship["permission"] = "player_polygamy";
                    } else if (openPrimaryID != 0) {
                        relationship["permission"] = "open_primary";
                        relationship["permission_source"] = makeNpcRef(openPrimaryID);
                    } else if (!hasOtherMainPair(partnerID)) {
                        relationship["permission"] = isMainTierStatus(edge.status) ? "primary" : "unrestricted";
                    }
                    j["current_relationships"].push_back(std::move(relationship));
                } else if (isPastStatus(edge.status)) {
                    nlohmann::json relationship;
                    relationship["partner"] = makeNpcRef(partnerID);
                    relationship["status"] = statusToKey(edge.status);
                    if (isSecretAffairPair(partnerID, /*ongoingOnly=*/false)) {
                        relationship["is_secret_affair"] = true;
                    }
                    j["past_relationships"].push_back(std::move(relationship));
                }
            }

            const auto playerRelationship = selfView.GetPerceivedPair(selfActorId, MARAS::State::ActorId::Player());
            if (actorFormID != MARAS::kPlayerFormID && playerRelationship && isCurrentStatus(playerRelationship->status)) {
                j["player_relationship_status"] = statusToKey(playerRelationship->status);
                const int affScore = selfView.Affection().GetPermanent(selfActorId);
                const auto lastDayOpt = selfView.Affection().GetLastInteractionDay(selfActorId);
                const int daysSinceInt = lastDayOpt.has_value()
                    ? static_cast<int>(std::floor(std::max(0.0f, currentDay - *lastDayOpt)))
                    : 999;
                std::string affTier = "estranged";
                if (affScore >= 75) affTier = "happy";
                else if (affScore >= 50) affTier = "content";
                else if (affScore >= 25) affTier = "troubled";
                j["player_affection_score"] = affScore;
                j["player_affection_tier"] = affTier;
                j["days_since_player_interaction"] = daysSinceInt;
                const auto pendingIntent = selfView.GetPerceivedPendingIntent(selfActorId);
                j["pending_intent"] = std::string(MARAS::Foundation::PartnerIntentTypeToString(pendingIntent.type));
            }
            if (playerRelationship && isCurrentStatus(playerRelationship->status) && playerPolygamyEnabled) {
                j["player_relationship_status"] = statusToKey(playerRelationship->status);
                struct PlayerPartner {
                    RE::FormID formID;
                    MARAS::State::RelationshipStatus status;
                };
                std::vector<PlayerPartner> playerPartners;
                for (const auto& pEdge : selfView.GetPerceivedRelationshipsOf(MARAS::State::ActorId::Player())) {
                    const RE::FormID partnerID = pEdge.Other(MARAS::State::ActorId::Player()).raw();
                    if (partnerID == actorFormID) continue;
                    if (isCurrentStatus(pEdge.status)) {
                        playerPartners.push_back({ partnerID, pEdge.status });
                    }
                }
                auto statusPriority = [](MARAS::State::RelationshipStatus status) {
                    switch (status) {
                        case MARAS::State::RelationshipStatus::Married: return 0;
                        case MARAS::State::RelationshipStatus::Engaged: return 1;
                        case MARAS::State::RelationshipStatus::Dating:  return 2;
                        case MARAS::State::RelationshipStatus::Lover:   return 3;
                        default: return 4;
                    }
                };
                std::ranges::sort(playerPartners, [&](const PlayerPartner& left, const PlayerPartner& right) {
                    return statusPriority(left.status) < statusPriority(right.status);
                });
                for (const auto& playerPartner : playerPartners) {
                    nlohmann::json coPartner;
                    coPartner["person"] = makeNpcRef(playerPartner.formID);
                    coPartner["role"] = playerRelationship->status == MARAS::State::RelationshipStatus::Married &&
                                                playerPartner.status == MARAS::State::RelationshipStatus::Married
                                            ? "co_spouse"
                                            : "co_partner";
                    coPartner["player_relationship_status"] = statusToKey(playerPartner.status);
                    j["player_co_partners"].push_back(std::move(coPartner));
                }
            }

            auto buildChildObj = [&](const MARAS::Projection::PerceivedChildLineage& child) -> std::optional<nlohmann::json> {
                if (child.stage == MARAS::State::ChildStage::Unborn) return std::nullopt;
                const RE::FormID childID = child.childId.raw();

                nlohmann::json childObj;
                childObj["uuid"] = (IsSkyrimNetAvailable() && child.skyrimNetUuid != 0)
                    ? child.skyrimNetUuid
                    : GetActorUUID(childID);
                const std::string childName =
                    (child.name.empty() || MARAS::Foundation::StringUtils::CaseInsensitiveEquals(child.name, "Newborn"))
                        ? "child"
                        : child.name;
                childObj["name"] = childName;
                childObj["is_adopted"] = child.isAdopted;
                childObj["is_biological"] = child.isBiologicalChild;
                childObj["age_days"] = std::max(0.0f, currentDay - child.birthTimestamp);

                std::string stageStr = "child";
                switch (child.stage) {
                    case MARAS::State::ChildStage::Unborn:   stageStr = "unborn"; break;
                    case MARAS::State::ChildStage::Infant:   stageStr = "infant"; break;
                    case MARAS::State::ChildStage::Child:    stageStr = "child"; break;
                    case MARAS::State::ChildStage::Training: stageStr = "training"; break;
                    case MARAS::State::ChildStage::Adult:    stageStr = "adult"; break;
                }
                childObj["stage"] = stageStr;

                const auto* childSample = (!MARAS::IsSyntheticChildID(childID))
                    ? spatialSnap->Find(MARAS::State::RefId{ childID })
                    : nullptr;
                bool hasRealActor = false;
                if (childSample) {
                    hasRealActor = !childSample->IsDeleted();
                } else if (MARAS::HasPhysicalActor(childID)) {
                    auto* childAct = MARAS::SpatialWorldIndex::GetSingleton().GetActorFromFormId(childID);
                    hasRealActor = (childAct != nullptr && !childAct->IsDeleted());
                }
                const bool isEphemeral = !hasRealActor ||
                    (child.stage == MARAS::State::ChildStage::Unborn) ||
                    (child.stage == MARAS::State::ChildStage::Infant) ||
                    (child.stage == MARAS::State::ChildStage::Training);
                childObj["has_ref_id"] = hasRealActor;
                childObj["is_ephemeral"] = isEphemeral;
                if (!child.trainingLocation.empty()) {
                    childObj["training_location"] = child.trainingLocation;
                }

                const MARAS::State::ActorSex resolvedSex = MARAS::ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
                int childSex = child.gender;
                if (resolvedSex == MARAS::State::ActorSex::Female) {
                    childSex = 1;
                } else if (resolvedSex == MARAS::State::ActorSex::Male) {
                    childSex = 0;
                }
                if (child.stage == MARAS::State::ChildStage::Unborn || childSex == 2) {
                    childObj["gender"] = "unknown";
                    childObj["relation"] = (child.stage == MARAS::State::ChildStage::Unborn) ? "unborn child" : "child";
                } else {
                    const std::string childGenderStr = (childSex == 1) ? "female" : "male";
                    childObj["gender"] = childGenderStr;
                    childObj["relation"] = (childSex == 1) ? "daughter" : "son";
                }

                if (child.hasUndiscoveredPaternitySecret) {
                    childObj["has_undiscovered_paternity_secret"] = true;
                }
                if (!child.presumedFather.IsNull()) {
                    childObj["presumed_father"] = makeNpcRef(child.presumedFather.raw());
                }
                if (child.paternitySecret.has_value() &&
                    !child.paternitySecret->presumedFather.IsNull() &&
                    child.paternitySecret->presumedFather != child.perceivedBioFather &&
                    (child.paternitySecret->isSecret || child.paternitySecret->discovered ||
                     child.paternitySecret->isSecretForPartner || child.paternitySecret->isSecretForBioFather)) {
                    const auto& sec = *child.paternitySecret;
                    nlohmann::json pSec;
                    pSec["is_secret"] = sec.isSecret;
                    pSec["is_secret_for_partner"] = sec.isSecretForPartner;
                    pSec["is_secret_for_bio_father"] = sec.isSecretForBioFather;
                    pSec["mother_partner"] = makeNpcRef(sec.motherPartner.raw());
                    pSec["presumed_father"] = makeNpcRef(sec.presumedFather.raw());
                    pSec["presumed_father_is_guardian"] = sec.presumedFatherIsGuardian;
                    if (!sec.trueBioFather.IsNull() && sec.trueBioFather.raw() != MARAS::kUnknownFatherFormID) {
                        pSec["biological_father"] = makeNpcRef(sec.trueBioFather.raw());
                    } else if (!sec.trueBioFatherFallbackName.empty() && sec.trueBioFatherFallbackName != "Unknown") {
                        nlohmann::json bioFatherRef;
                        bioFatherRef["uuid"] = 0;
                        bioFatherRef["name"] = sec.trueBioFatherFallbackName;
                        pSec["biological_father"] = bioFatherRef;
                    } else {
                        pSec["biological_father"] = makeNpcRef(0);
                    }
                    pSec["discovered_by_partner"] = sec.discoveredByPartner;
                    pSec["discovered_by_bio_father"] = sec.discoveredByBioFather;
                    pSec["has_phenotype_mismatch"] = sec.hasPhenotypeMismatch;
                    childObj["paternity_secret"] = std::move(pSec);
                }

                std::vector<RE::FormID> currentGuardians;
                if (!child.guardians.slotA.IsNull()) currentGuardians.push_back(child.guardians.slotA.raw());
                if (!child.guardians.slotB.IsNull() && child.guardians.slotB != child.guardians.slotA) {
                    currentGuardians.push_back(child.guardians.slotB.raw());
                }
                if (!currentGuardians.empty()) {
                    childObj["guardians"] = makeNpcListRef(currentGuardians);
                    childObj["current_parents"] = makeNpcListRef(currentGuardians);
                }

                std::vector<RE::FormID> bioParents;
                bioParents.reserve(child.biologicalParents.size());
                for (const auto bp : child.biologicalParents) {
                    bioParents.push_back(bp.raw());
                }
                if (!bioParents.empty()) {
                    childObj["biological_parents"] = makeNpcListRef(bioParents);

                    std::vector<RE::FormID> stepParents;
                    stepParents.reserve(child.stepParents.size());
                    for (const auto sp : child.stepParents) {
                        stepParents.push_back(sp.raw());
                    }
                    if (!stepParents.empty()) {
                        childObj["step_parents"] = makeNpcListRef(stepParents);
                    }

                    if (!child.bioMother.IsNull()) {
                        childObj["bio_mother"] = makeNpcRef(child.bioMother.raw());
                        childObj["biological_mother"] = childObj["bio_mother"];
                    }
                    if (!child.perceivedBioFather.IsNull() &&
                        child.perceivedBioFather.raw() != MARAS::kUnknownFatherFormID) {
                        childObj["bio_father"] = makeNpcRef(child.perceivedBioFather.raw());
                        childObj["biological_father"] = childObj["bio_father"];
                    } else if (!child.perceivedBioFatherFallbackName.empty() &&
                               child.perceivedBioFatherFallbackName != "Unknown") {
                        nlohmann::json bioFatherRef;
                        bioFatherRef["uuid"] = 0;
                        bioFatherRef["name"] = child.perceivedBioFatherFallbackName;
                        childObj["bio_father"] = bioFatherRef;
                        childObj["biological_father"] = bioFatherRef;
                    }

                    const bool isActorStep = child.IsActorPerceivedStepParent(selfActorId);
                    const bool isActorBio = child.IsActorPerceivedBioParent(selfActorId);
                    childObj["is_actor_step_parent"] = isActorStep;
                    childObj["is_actor_bio_parent"] = isActorBio;

                    if (isActorStep) {
                        childObj["relation"] = (childSex == 1) ? "stepdaughter" : "stepson";
                    }
                }

                const auto* hhChild = worldSnap->household->FindChildState(child.childId);
                const RE::FormID critterID = hhChild ? hhChild->critterId.raw() : 0;
                if (critterID != 0) {
                    nlohmann::json critterObj;
                    critterObj["uuid"] = GetActorUUID(critterID);
                    critterObj["name"] = MARAS::ResolveActorNameSafe(critterID, *worldSnap, *spatialSnap);
                    critterObj["type"] = CleanAnimalRaceName(MARAS::ResolveActorRaceSafe(critterID, *worldSnap, *spatialSnap));
                    childObj["critter"] = std::move(critterObj);
                }

                const RE::FormID petID = hhChild ? hhChild->petId.raw() : 0;
                if (petID != 0) {
                    nlohmann::json petObj;
                    petObj["uuid"] = GetActorUUID(petID);
                    petObj["name"] = MARAS::ResolveActorNameSafe(petID, *worldSnap, *spatialSnap);
                    petObj["type"] = CleanAnimalRaceName(MARAS::ResolveActorRaceSafe(petID, *worldSnap, *spatialSnap));
                    childObj["pet"] = std::move(petObj);
                }
                childObj["has_beastblood"] = child.hasBeastblood;

                return childObj;
            };

            std::map<RE::FormID, std::vector<nlohmann::json>> childrenByOtherParent;
            std::map<RE::FormID, std::vector<std::string>> childNamesByOtherParent;
            std::vector<RE::FormID> otherParentOrder;
            for (const auto& child : selfView.GetCustodialChildrenOf(selfActorId)) {
                const RE::FormID otherPID =
                    (child.guardians.slotA == selfActorId) ? child.guardians.slotB.raw() : child.guardians.slotA.raw();
                if (auto cObjOpt = buildChildObj(child)) {
                    if (childrenByOtherParent.find(otherPID) == childrenByOtherParent.end()) {
                        otherParentOrder.push_back(otherPID);
                    }
                    childNamesByOtherParent[otherPID].push_back((*cObjOpt)["name"].get<std::string>());
                    childrenByOtherParent[otherPID].push_back(std::move(*cObjOpt));
                }
            }

            std::vector<nlohmann::json> nonCustodialChildren;
            for (const auto& child : selfView.GetNonCustodialChildrenOf(selfActorId)) {
                const RE::FormID childID = child.childId.raw();
                nlohmann::json ncObj;
                ncObj["uuid"] = (IsSkyrimNetAvailable() && child.skyrimNetUuid != 0)
                    ? child.skyrimNetUuid
                    : GetActorUUID(childID);
                const std::string childName =
                    (child.name.empty() || MARAS::Foundation::StringUtils::CaseInsensitiveEquals(child.name, "Newborn"))
                        ? "child"
                        : child.name;
                ncObj["name"] = childName;
                const MARAS::State::ActorSex resolvedSex = MARAS::ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
                int childSex = child.gender;
                if (resolvedSex == MARAS::State::ActorSex::Female) {
                    childSex = 1;
                } else if (resolvedSex == MARAS::State::ActorSex::Male) {
                    childSex = 0;
                }
                ncObj["gender"] = (childSex == 1) ? "female" : "male";
                ncObj["relation"] = (childSex == 1) ? "daughter" : "son";

                std::vector<RE::FormID> guardians;
                if (!child.guardians.slotA.IsNull()) guardians.push_back(child.guardians.slotA.raw());
                if (!child.guardians.slotB.IsNull() && child.guardians.slotB != child.guardians.slotA) {
                    guardians.push_back(child.guardians.slotB.raw());
                }
                if (!guardians.empty()) {
                    ncObj["guardians"] = makeNpcListRef(guardians);
                }

                const RE::FormID otherBioID = child.PerceivedOtherParent(selfActorId).raw();
                if (otherBioID != 0 && otherBioID != MARAS::kUnknownFatherFormID) {
                    ncObj["other_bio_parent"] = makeNpcRef(otherBioID);
                }
                if (child.hasUndiscoveredPaternitySecret && child.paternitySecret.has_value()) {
                    if (child.perceivedBioFather == selfActorId) {
                        ncObj["is_paternity_secret"] = true;
                    }
                    if (child.bioMother == selfActorId) {
                        ncObj["is_other_parent_secret"] = true;
                        if (child.paternitySecret->isSecretForPartner && !child.presumedFather.IsNull()) {
                            ncObj["public_other_parent"] = makeNpcRef(child.presumedFather.raw());
                        }
                    }
                }
                nonCustodialChildren.push_back(std::move(ncObj));
            }

            nlohmann::json childrenGroupsArray = nlohmann::json::array();
            for (auto otherPID : otherParentOrder) {
                nlohmann::json grpObj;
                if (otherPID != 0) {
                    grpObj["other_parent"] = makeNpcRef(otherPID);
                } else {
                    grpObj["single_parent"] = true;
                }
                grpObj["children"] = childrenByOtherParent[otherPID];
                grpObj["children_names_summary"] = FormatNamesSummary(childNamesByOtherParent[otherPID]);
                childrenGroupsArray.push_back(std::move(grpObj));
            }

            j["family"] = std::move(childrenGroupsArray);
            j["non_custodial_children"] = std::move(nonCustodialChildren);

            if (const auto asChildOpt = selfView.GetPerceivedChild(MARAS::State::ChildId{ actorFormID }); asChildOpt.has_value()) {
                const auto& asChild = *asChildOpt;
                nlohmann::json parentage;
                parentage["is_adopted"] = asChild.isAdopted;
                parentage["is_biological"] = asChild.isBiologicalChild;

                if (asChild.hasUndiscoveredPaternitySecret) {
                    parentage["has_undiscovered_paternity_secret"] = true;
                }

                const RE::FormID asChildMotherID = asChild.bioMother.raw();
                const RE::FormID asChildFatherID = asChild.motherPartner.raw();
                const RE::FormID presFatherID = asChild.presumedFather.raw();
                const RE::FormID effectiveChildBioFatherID = asChild.perceivedBioFather.raw();
                const bool hasAnyGuardian = asChild.guardians.HasAnyGuardian();

                if (asChildMotherID != 0 && (asChild.guardians.HasGuardian(asChild.bioMother) || !hasAnyGuardian)) {
                    parentage["mother"] = makeNpcRef(asChildMotherID);
                }
                if (asChildFatherID != 0) {
                    parentage["mother_partner"] = makeNpcRef(asChildFatherID);
                    if (asChild.guardians.HasGuardian(asChild.motherPartner) || !hasAnyGuardian) {
                        parentage["father"] = makeNpcRef(asChildFatherID);
                    }
                }
                if (presFatherID != 0) {
                    parentage["presumed_father"] = makeNpcRef(presFatherID);
                }

                parentage["has_beastblood"] = asChild.hasBeastblood;

                if (asChildMotherID != 0) {
                    parentage["bio_mother"] = makeNpcRef(asChildMotherID);
                    parentage["biological_mother"] = parentage["bio_mother"];
                }
                if (effectiveChildBioFatherID != 0 &&
                    effectiveChildBioFatherID != MARAS::kUnknownFatherFormID) {
                    parentage["bio_father"] = makeNpcRef(effectiveChildBioFatherID);
                    parentage["biological_father"] = parentage["bio_father"];
                } else if (!asChild.perceivedBioFatherFallbackName.empty() &&
                           asChild.perceivedBioFatherFallbackName != "Unknown") {
                    nlohmann::json bioFatherRef;
                    bioFatherRef["uuid"] = 0;
                    bioFatherRef["name"] = asChild.perceivedBioFatherFallbackName;
                    parentage["bio_father"] = bioFatherRef;
                    parentage["biological_father"] = bioFatherRef;
                }

                std::vector<RE::FormID> currentGuardians;
                if (!asChild.guardians.slotA.IsNull()) currentGuardians.push_back(asChild.guardians.slotA.raw());
                if (!asChild.guardians.slotB.IsNull() && asChild.guardians.slotB != asChild.guardians.slotA) {
                    currentGuardians.push_back(asChild.guardians.slotB.raw());
                }
                if (!currentGuardians.empty()) {
                    parentage["guardians"] = makeNpcListRef(currentGuardians);
                    parentage["current_parents"] = makeNpcListRef(currentGuardians);
                }

                std::vector<RE::FormID> bioParents;
                bioParents.reserve(asChild.biologicalParents.size());
                for (const auto bp : asChild.biologicalParents) {
                    bioParents.push_back(bp.raw());
                }
                if (!bioParents.empty()) {
                    parentage["biological_parents"] = makeNpcListRef(bioParents);

                    std::vector<RE::FormID> stepParents;
                    stepParents.reserve(asChild.stepParents.size());
                    for (const auto sp : asChild.stepParents) {
                        stepParents.push_back(sp.raw());
                    }
                    if (!stepParents.empty()) {
                        parentage["step_parents"] = makeNpcListRef(stepParents);
                    }
                }

                std::string stageStr = "child";
                switch (asChild.stage) {
                    case MARAS::State::ChildStage::Unborn:   stageStr = "unborn"; break;
                    case MARAS::State::ChildStage::Infant:   stageStr = "infant"; break;
                    case MARAS::State::ChildStage::Child:    stageStr = "child"; break;
                    case MARAS::State::ChildStage::Training: stageStr = "training"; break;
                    case MARAS::State::ChildStage::Adult:    stageStr = "adult"; break;
                }
                parentage["stage"] = stageStr;
                if (!asChild.trainingLocation.empty()) {
                    parentage["training_location"] = asChild.trainingLocation;
                }

                const MARAS::State::ActorSex resolvedAsChildSex =
                    MARAS::ResolveActorSexSafe(actorFormID, *worldSnap, *spatialSnap);
                int asChildSex = asChild.gender;
                if (resolvedAsChildSex == MARAS::State::ActorSex::Female) {
                    asChildSex = 1;
                } else if (resolvedAsChildSex == MARAS::State::ActorSex::Male) {
                    asChildSex = 0;
                }
                parentage["gender"] = (asChildSex == 1) ? "female" : "male";
                parentage["relation"] = (asChildSex == 1) ? "daughter" : "son";

                std::vector<RE::FormID> siblingIDs;
                for (const auto& sib : selfView.GetPerceivedSiblingsOf(asChild.childId, /*forcePublicOnly=*/false)) {
                    siblingIDs.push_back(sib.childId.raw());
                }
                if (!siblingIDs.empty()) {
                    parentage["siblings"] = makeNpcListRef(siblingIDs);
                }

                std::vector<RE::FormID> publicSiblingIDs;
                for (const auto& sib : selfView.GetPerceivedSiblingsOf(asChild.childId, /*forcePublicOnly=*/true)) {
                    publicSiblingIDs.push_back(sib.childId.raw());
                }
                if (!publicSiblingIDs.empty()) {
                    parentage["siblings_public"] = makeNpcListRef(publicSiblingIDs);
                }

                const auto* hhChild = worldSnap->household->FindChildState(asChild.childId);
                const RE::FormID critterID = hhChild ? hhChild->critterId.raw() : 0;
                if (critterID != 0) {
                    nlohmann::json critterObj;
                    critterObj["uuid"] = GetActorUUID(critterID);
                    critterObj["name"] = MARAS::ResolveActorNameSafe(critterID, *worldSnap, *spatialSnap);
                    critterObj["type"] = CleanAnimalRaceName(MARAS::ResolveActorRaceSafe(critterID, *worldSnap, *spatialSnap));
                    parentage["critter"] = std::move(critterObj);
                }

                const RE::FormID petID = hhChild ? hhChild->petId.raw() : 0;
                if (petID != 0) {
                    nlohmann::json petObj;
                    petObj["uuid"] = GetActorUUID(petID);
                    petObj["name"] = MARAS::ResolveActorNameSafe(petID, *worldSnap, *spatialSnap);
                    petObj["type"] = CleanAnimalRaceName(MARAS::ResolveActorRaceSafe(petID, *worldSnap, *spatialSnap));
                    parentage["pet"] = std::move(petObj);
                }

                if (asChild.paternitySecret.has_value() &&
                    presFatherID != 0 &&
                    (asChild.paternitySecret->isSecret || asChild.paternitySecret->discovered)) {
                    const auto& sec = *asChild.paternitySecret;
                    nlohmann::json pIntrigue;
                    pIntrigue["is_secret"] = sec.isSecret;
                    pIntrigue["is_secret_for_partner"] = sec.isSecretForPartner;
                    pIntrigue["is_secret_for_bio_father"] = sec.isSecretForBioFather;
                    pIntrigue["mother_partner"] = makeNpcRef(presFatherID);
                    pIntrigue["presumed_father"] = makeNpcRef(presFatherID);
                    pIntrigue["presumed_father_is_guardian"] = sec.presumedFatherIsGuardian;
                    if (!sec.trueBioFather.IsNull()) {
                        pIntrigue["biological_father"] = makeNpcRef(sec.trueBioFather.raw());
                    } else if (!sec.trueBioFatherFallbackName.empty() &&
                               sec.trueBioFatherFallbackName != "Unknown") {
                        nlohmann::json bioFatherRef;
                        bioFatherRef["uuid"] = 0;
                        bioFatherRef["name"] = sec.trueBioFatherFallbackName;
                        pIntrigue["biological_father"] = bioFatherRef;
                    }
                    pIntrigue["discovered_by_partner"] = sec.discoveredByPartner;
                    pIntrigue["discovered_by_bio_father"] = sec.discoveredByBioFather;
                    parentage["paternity_secret"] = std::move(pIntrigue);
                }

                nlohmann::json involvedAdults = nlohmann::json::array();
                nlohmann::json recentEventsParticipants = nlohmann::json::array();
                std::vector<RE::FormID> seenAdultIDs;
                auto appendEventUUID = [&](std::uint64_t uuid) {
                    if (uuid == 0) return;
                    for (const auto& existing : recentEventsParticipants) {
                        if (existing.is_number_unsigned() && existing.get<std::uint64_t>() == uuid) return;
                    }
                    recentEventsParticipants.push_back(uuid);
                };
                appendEventUUID(GetActorUUID(actorFormID));

                auto addInvolvedAdult = [&](RE::FormID adultID, const std::string& roleLabel) {
                    if (adultID == 0 || adultID == MARAS::kUnknownFatherFormID || adultID == actorFormID) return;
                    for (RE::FormID seen : seenAdultIDs) {
                        if (MARAS::MatchesActor(seen, adultID)) return;
                    }
                    seenAdultIDs.push_back(adultID);
                    nlohmann::json ref = makeNpcRef(adultID);
                    ref["role"] = roleLabel;
                    const std::uint64_t adultUUID = ref.value("uuid", std::uint64_t{ 0 });
                    if (adultUUID != 0) {
                        appendEventUUID(adultUUID);
                    }
                    involvedAdults.push_back(std::move(ref));
                };

                if (asChildMotherID != 0) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.bioMother);
                    addInvolvedAdult(asChildMotherID, isGuard ? "Biological Mother & Guardian" : "Biological Mother");
                }
                if (effectiveChildBioFatherID != 0 &&
                    effectiveChildBioFatherID != MARAS::kUnknownFatherFormID) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.perceivedBioFather);
                    addInvolvedAdult(effectiveChildBioFatherID, isGuard ? "Father & Guardian" : "Father");
                }
                if (presFatherID != 0) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.presumedFather);
                    addInvolvedAdult(presFatherID, isGuard ? "Guardian / Raising Father" : "Presumed Father");
                }
                for (RE::FormID gID : currentGuardians) {
                    addInvolvedAdult(gID, asChild.isAdopted ? "Adoptive Parent & Guardian" : "Step-Parent & Guardian");
                }
                parentage["involved_adults"] = std::move(involvedAdults);

                // Perspective-masked involved adults & paternity state on the child's own relationship profile.
                // Never leak the secret true biological father or secret paternity labels when hideSecretFromViewer is true.
                nlohmann::json bioGenAdults = nlohmann::json::array();
                std::vector<RE::FormID> seenBioGenAdultIDs;
                auto addBioGenAdult = [&](RE::FormID adultID,
                                          const std::string& roleKey,
                                          const std::string& roleLabel,
                                          bool isGuardian,
                                          std::optional<bool> knowsPaternity = std::nullopt) {
                    if (adultID == 0 || adultID == MARAS::kUnknownFatherFormID || adultID == actorFormID) return;
                    for (RE::FormID seen : seenBioGenAdultIDs) {
                        if (MARAS::MatchesActor(seen, adultID)) return;
                    }
                    seenBioGenAdultIDs.push_back(adultID);
                    nlohmann::json ref = makeNpcRef(adultID);
                    ref["role_key"] = roleKey;
                    ref["role"] = roleLabel;
                    ref["is_guardian"] = isGuardian;
                    if (knowsPaternity.has_value()) {
                        ref["knows_paternity"] = *knowsPaternity;
                    }
                    const std::uint64_t adultUUID = ref.value("uuid", std::uint64_t{ 0 });
                    if (adultUUID != 0) {
                        appendEventUUID(adultUUID);
                    }
                    bioGenAdults.push_back(std::move(ref));
                };

                if (asChildMotherID != 0) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.bioMother);
                    addBioGenAdult(
                        asChildMotherID,
                        "bio_mother",
                        isGuard ? "Biological Mother & Household Guardian" : "Biological Mother (Non-Custodial)",
                        isGuard,
                        true);
                }
                if (asChild.hideSecretFromViewer) {
                    if (effectiveChildBioFatherID != 0 &&
                        effectiveChildBioFatherID != MARAS::kUnknownFatherFormID) {
                        const bool isGuard = asChild.guardians.HasGuardian(asChild.perceivedBioFather);
                        addBioGenAdult(
                            effectiveChildBioFatherID,
                            "bio_father",
                            isGuard ? "Biological Father & Household Guardian" : "Biological Father (Non-Custodial)",
                            isGuard,
                            true);
                    }
                } else {
                    const auto& pSecOpt = asChild.paternitySecret;
                    const bool hasPresumedFather = (presFatherID != 0);
                    const bool hasPaternitySecret = hasPresumedFather && pSecOpt.has_value() &&
                        (pSecOpt->isSecret || pSecOpt->discovered ||
                         pSecOpt->discoveredByPartner || pSecOpt->discoveredByBioFather);
                    const bool partnerKnowsPaternity = !hasPaternitySecret ||
                        (pSecOpt.has_value() &&
                         (pSecOpt->discovered || pSecOpt->discoveredByPartner || !pSecOpt->isSecretForPartner));
                    const bool bioFatherKnowsPaternity = !pSecOpt.has_value() ||
                        pSecOpt->discovered || pSecOpt->discoveredByBioFather || !pSecOpt->isSecretForBioFather;
                    const bool isSingleMotherSecretFromBioFather = (!hasPresumedFather &&
                        effectiveChildBioFatherID != 0 &&
                        pSecOpt.has_value() &&
                        pSecOpt->isSecret &&
                        pSecOpt->isSecretForBioFather &&
                        !pSecOpt->discoveredByBioFather &&
                        !pSecOpt->discovered);

                    if (effectiveChildBioFatherID != 0 &&
                        effectiveChildBioFatherID != MARAS::kUnknownFatherFormID) {
                        const bool isGuard = asChild.guardians.HasGuardian(asChild.perceivedBioFather);
                        const std::string bioFatherRole = MARAS::Domain::Lineage::ComputeBioFatherRoleLabel(
                            isGuard,
                            hasPaternitySecret,
                            isSingleMotherSecretFromBioFather,
                            partnerKnowsPaternity,
                            bioFatherKnowsPaternity);
                        addBioGenAdult(
                            effectiveChildBioFatherID, "bio_father", bioFatherRole, isGuard, bioFatherKnowsPaternity);
                    }
                    if (hasPresumedFather) {
                        const bool isGuard = asChild.guardians.HasGuardian(asChild.presumedFather);
                        const std::string presFatherRole = MARAS::Domain::Lineage::ComputePresumedFatherRoleLabel(
                            isGuard,
                            partnerKnowsPaternity,
                            bioFatherKnowsPaternity);
                        addBioGenAdult(
                            presFatherID, "presumed_father", presFatherRole, isGuard, partnerKnowsPaternity);
                    }
                }
                for (RE::FormID gID : currentGuardians) {
                    addBioGenAdult(
                        gID,
                        "guardian",
                        asChild.isAdopted ? "Adoptive Parent & Household Guardian" : "Step-Parent & Household Guardian",
                        true);
                }
                parentage["bio_generation_adults"] = std::move(bioGenAdults);
                parentage["recent_events_participants"] = std::move(recentEventsParticipants);
                const std::int64_t maxEventAgeSec = MARAS::Domain::Lineage::ComputeChildRecentEventsMaxAgeSeconds(
                    asChild.isBiologicalChild,
                    asChild.isAdopted,
                    asChild.birthTimestamp,
                    asChild.timestamp,
                    currentDay);
                if (maxEventAgeSec > 0) {
                    parentage["recent_events_max_age_seconds"] = maxEventAgeSec;
                }

                j["parents"] = std::move(parentage);
            }

            for (const auto& record : knownAffairs) {
                if (record.affairStatus == MARAS::State::AffairStatus::Allowed) continue;
                if (record.deceivedPartner == selfActorId && record.isSecret) continue;

                nlohmann::json infidelity;
                if (record.cheater == selfActorId) {
                    infidelity["actor_role"] = "cheater";
                } else if (record.paramour == selfActorId) {
                    infidelity["actor_role"] = "affair_partner";
                } else {
                    infidelity["actor_role"] = "victim";
                }
                infidelity["cheater"] = makeNpcRef(record.cheater.raw());
                infidelity["affair_partner"] = makeNpcRef(record.paramour.raw());
                infidelity["victim"] = makeNpcRef(record.deceivedPartner.raw());
                infidelity["status"] = affairStatusToKey(record.affairStatus);
                infidelity["type"] = MARAS::Foundation::StringUtils::ToLowerStd(
                    MARAS::Foundation::AffairTypeToString(record.affairType));
                infidelity["affair_stage"] = MARAS::Foundation::StringUtils::ToLowerStd(
                    MARAS::Foundation::AffairStageToString(record.effectiveAffairStage));
                infidelity["incident_count"] = record.incidentCount;
                infidelity["last_timestamp"] = record.lastTimestamp;
                infidelity["is_secret"] = record.isSecret;
                j["infidelity"].push_back(std::move(infidelity));
            }

            try {
                return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNet] BuildRelationshipProfileJSON dump error: {}", e.what());
                return "{}";
            }
        }

        std::string GetCandidateDisplayName(RE::FormID partnerFid,
                                            const MARAS::State::WorldStateSnapshot& world,
                                            const MARAS::State::SpatialActorSnapshot& spatial) {
            std::string name = MARAS::ResolveActorNameSafe(partnerFid, world, spatial);
            if (name.empty() || name == "None") {
                if (partnerFid == MARAS::kPlayerFormID) {
                    name = "Player";
                } else {
                    name = fmt::format("0x{:08X}", partnerFid);
                }
            }
            return name;
        }

        std::string GetNearbyCandidatesByEligibility(RE::Actor* actor,
                                                     std::function<bool(RE::FormID)> soloCheck,
                                                     std::function<bool(RE::FormID, RE::FormID)> pairCheck,
                                                     std::function<bool(RE::FormID, RE::FormID)> extraFilter = nullptr) {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            if (!soloCheck(fid)) return "";

            const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const float radius = GetNearbyActorsRadius();
            std::string result;
            spatialSnap->ForEachNearbyCandidate(MARAS::State::RefId{ fid }, radius,
                [&](const MARAS::State::LoadedActorSample& sample) {
                    const auto targetFid = sample.refId.raw();
                    if (pairCheck(fid, targetFid) && (!extraFilter || extraFilter(fid, targetFid))) {
                        const std::string name = GetCandidateDisplayName(targetFid, *worldSnap, *spatialSnap);
                        if (!name.empty()) {
                            const MARAS::State::ActorSex sex =
                                MARAS::ResolveActorSexSafe(targetFid, *worldSnap, *spatialSnap);
                            const char* gender = (sex == MARAS::State::ActorSex::Female) ? "Female" : "Male";
                            if (!result.empty()) result += ", ";
                            result += fmt::format("{} ({})", name, gender);
                        }
                    }
                });
            return result;
        }

        std::string GetPartnersByStatus(RE::Actor* actor,
                                        MARAS::Foundation::RelationshipStatus status,
                                        std::function<bool(RE::FormID, RE::FormID)> pairCheck) {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto selfActorId = MARAS::State::ActorId{ fid };
            const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());

            std::string result;
            for (const auto& edge : selfView.GetPerceivedRelationshipsOf(selfActorId)) {
                if (edge.status != status) continue;
                const RE::FormID pFid = edge.Other(selfActorId).raw();
                if (!pairCheck(fid, pFid)) continue;
                const std::string name = GetCandidateDisplayName(pFid, *worldSnap, *spatialSnap);
                if (!name.empty() && name != "None") {
                    if (!result.empty()) result += ", ";
                    result += name;
                }
            }
            return result;
        }

        struct PaternityEvalState {
            MARAS::SessionEpoch epoch{ 0 };
            RE::FormID childID{ 0 };
            RE::FormID motherID{ 0 };
            RE::FormID presumedFatherID{ 0 };
            RE::FormID bioFatherID{ 0 };
            std::string childName;
            std::string motherName;
            bool isSecretForPresumedFather{ false };
            std::string contextJson;
        };

        bool DispatchPaternityEval(std::shared_ptr<const PaternityEvalState> state) {
            if (!state || !MARAS::IsSessionEpochCurrent(state->epoch)) {
                return false;
            }

            return Decisions::EvaluatePaternity(
                state->contextJson,
                state->isSecretForPresumedFather,
                [state](const MARAS::App::PaternityEvalResult& res) {
                    if (!MARAS::IsSessionEpochCurrent(state->epoch)) {
                        return;
                    }
                    MARAS::AddEpochTask(state->epoch, "EvaluatePaternitySecret::OnResult", [state, res]() {
                        std::lock_guard lock(MARAS::GetPapyrusMutex());
                        auto& relMgr = MARAS::App::RelationshipUseCases::GetSingleton();
                        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
                        auto& paternity = MARAS::App::PaternityWorkflowCoordinator::GetSingleton();
                        auto& uiBridge = MARAS::App::GetUiBridgeGateway();

                        RE::FormID childID = state->childID;
                        if (!lineage.GetChildEntry(childID).has_value()) {
                            const RE::FormID promotedID = paternity.FindChildPaternitySecret(
                                state->motherID, state->bioFatherID, state->childName);
                            if (promotedID != 0 && lineage.GetChildEntry(promotedID).has_value()) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: child {:08X} resolved to promoted child {:08X}.",
                                               state->childID, promotedID);
                                childID = promotedID;
                            }
                        }
                        if (childID == 0 || !lineage.GetChildEntry(childID).has_value()) {
                            MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: child {:08X} ('{}') no longer present in ChildRegistry; ignoring async result.",
                                           state->childID, state->childName);
                            uiBridge.SendOverviewData();
                            return;
                        }
                        if (state->motherID != 0) {
                            const bool motherDeadInEngine = [motherID = state->motherID]() {
                                if (motherID == MARAS::kPlayerFormID) {
                                    auto* player = RE::PlayerCharacter::GetSingleton();
                                    return player && player->IsDead();
                                }
                                auto* actor = RE::TESForm::LookupByID<RE::Actor>(motherID);
                                return actor && (actor->IsDeleted() || actor->IsDead());
                            }();
                            if (lineage.IsDeceased(state->motherID) || motherDeadInEngine) {
                                MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: mother {:08X} ('{}') is deceased/dead; ignoring async result for child {:08X}.",
                                               state->motherID, state->motherName, childID);
                                uiBridge.SendOverviewData();
                                return;
                            }
                        }

                        if (!res.success) {
                            MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: evaluation failed for child {:08X} (usedDecisions={}, detail='{}').",
                                           childID, res.usedDecisions, res.failureDetail);

                            auto retryAction = [state]() {
                                std::lock_guard retryLock(MARAS::GetPapyrusMutex());
                                if (!MARAS::IsSessionEpochCurrent(state->epoch)) {
                                    return;
                                }
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: retrying evaluation for child {:08X}.", state->childID);
                                if (!DispatchPaternityEval(state)) {
                                    auto& bridge = MARAS::App::GetUiBridgeGateway();
                                    bridge.SendOverviewData();
                                    bridge.SendNPCDetail(state->childID);
                                }
                            };

                            auto ignoreAction = [childID, expectedEpoch = state->epoch]() {
                                std::lock_guard ignoreLock(MARAS::GetPapyrusMutex());
                                if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                                    return;
                                }
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: player chose to ignore failed evaluation for child {:08X}.", childID);
                                auto& bridge = MARAS::App::GetUiBridgeGateway();
                                bridge.SendOverviewData();
                                bridge.SendNPCDetail(childID);
                            };

                            const std::string fullDescription = fmt::format(
                                "Child: {} (Mother: {})\n{}",
                                state->childName, state->motherName, res.failureDetail);

                            MARAS::App::ShowLLMRetryModal(
                                MARAS::Tr("modal.llm_retry.eval_paternity_secret"),
                                fullDescription,
                                res.failureType,
                                std::move(retryAction),
                                std::move(ignoreAction),
                                res.usedDecisions ? "decisions" : "gamemaster_evaluation");
                            return;
                        }

                        const std::string& outcome = res.outcome;
                        const std::string& partnerReaction = res.partnerReaction;
                        const std::string& summary = res.summary;

                        MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret result for child {:08X} (usedDecisions={}): outcome='{}', reaction='{}', summary='{}'",
                                       childID, res.usedDecisions, outcome, partnerReaction, summary);

                        const bool wasPregSecretPartner = paternity.IsPregnancySecretForPartner(childID);
                        bool anySecretResolved = false;

                        auto tryBreakupIfActive = [&]() {
                            const RE::FormID motherID = state->motherID;
                            const RE::FormID presumedFatherID = state->presumedFatherID;
                            if (motherID == 0 || presumedFatherID == 0) {
                                return;
                            }
                            if (lineage.IsDeceased(motherID) || lineage.IsDeceased(presumedFatherID)) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: skipping breakup for {:08X} & {:08X} (deceased in registry).",
                                               motherID, presumedFatherID);
                                return;
                            }
                            auto isDeadInEngine = [](RE::FormID id) -> bool {
                                if (id == MARAS::kPlayerFormID) {
                                    auto* player = RE::PlayerCharacter::GetSingleton();
                                    return player && player->IsDead();
                                }
                                auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
                                return actor && actor->IsDead();
                            };
                            if (isDeadInEngine(motherID) || isDeadInEngine(presumedFatherID)) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: skipping breakup for {:08X} & {:08X} (actor dead in engine).",
                                               motherID, presumedFatherID);
                                return;
                            }
                            const auto curStatus = relMgr.GetRelationshipStatus(motherID, presumedFatherID);
                            if (curStatus != MARAS::Foundation::RelationshipStatus::Dating &&
                                curStatus != MARAS::Foundation::RelationshipStatus::Lover &&
                                curStatus != MARAS::Foundation::RelationshipStatus::Engaged &&
                                curStatus != MARAS::Foundation::RelationshipStatus::Married) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: skipping breakup for {:08X} & {:08X} (status {} no longer active).",
                                               motherID, presumedFatherID, static_cast<int>(curStatus));
                                return;
                            }
                            relMgr.Breakup(motherID, presumedFatherID);
                            if (motherID == MARAS::kPlayerFormID || presumedFatherID == MARAS::kPlayerFormID) {
                                uiBridge.SendHouseholdBuffs();
                            }
                        };

                        // 1. Resolve state based on the specific canonical outcome
                        bool shouldTryBreakup = false;
                        if (outcome == "mother_confessed_presumed" || outcome == "presumed_father_learnt") {
                            anySecretResolved |= paternity.ResolvePaternitySecretForPartner(
                                childID, outcome == "presumed_father_learnt", /*sendEvent=*/false);
                            if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                shouldTryBreakup = true;
                            }
                        } else if (outcome == "mother_confessed_bio" || outcome == "bio_father_learnt") {
                            anySecretResolved |= paternity.ResolvePaternitySecretForBioFather(
                                childID, outcome == "bio_father_learnt", /*sendEvent=*/false);
                        } else if (outcome == "mother_confessed_both") {
                            anySecretResolved |= paternity.ResolvePaternitySecretForPartner(
                                childID, false, /*sendEvent=*/false);
                            anySecretResolved |= paternity.ResolvePaternitySecretForBioFather(
                                childID, false, /*sendEvent=*/false);
                            if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                shouldTryBreakup = true;
                            }
                        }

                        // 2. Dispatch SkyrimNet event & mod event before cascading breakup so 3s suppression applies
                        if (anySecretResolved && outcome != "none" && outcome != "remains_secret" && !outcome.empty()) {
                            const std::string eventOutcome =
                                (wasPregSecretPartner &&
                                 (outcome == "mother_confessed_presumed" ||
                                  outcome == "presumed_father_learnt" ||
                                  outcome == "mother_confessed_both"))
                                    ? (outcome + "_unannounced")
                                    : outcome;
                            App::AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, eventOutcome);
                            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
                            MARAS::EmitModCallbackEvent("maras_paternity_secret_resolved", eventOutcome, childIdFloat, RE::FormID{ 0 });
                            if (IsSkyrimNetAvailable()) {
                                SkyrimNetEventService::GetSingleton().EmitPaternitySecretResolved(childID, eventOutcome);
                                SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(
                                    childID, outcome, partnerReaction, wasPregSecretPartner);
                            }
                        }

                        if (shouldTryBreakup) {
                            tryBreakupIfActive();
                        }

                        // 3. Notify UI dashboard
                        uiBridge.SendOverviewData();
                        uiBridge.SendNPCDetail(childID);
                    });
                });
        }

    }  // namespace

    void RegisterSkyrimNetDecorators() {
        MARAS_LOG_INFO("[SkyrimNet] RegisterSkyrimNetDecorators called.");

        HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] SkyrimNet.dll not found in process - decorator registration skipped.");
            return;
        }
        if (!::GetProcAddress(hMod, "PublicRegisterDecorator")) {
            MARAS_LOG_WARN("[SkyrimNet] PublicRegisterDecorator export not found in SkyrimNet.dll.");
            return;
        }

        // 1. ttm_get_ex_partner
        RegisterDecoratorExport(
            "ttm_get_ex_partner",
            "Returns a sentence describing this actor's connection to a player household member via a prior in-game relationship. Covers both the NPC who left someone to join the player, and the NPC who was left behind.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const std::uint32_t actorFormId = actor->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap,
                    spatialSnap,
                    MARAS::State::ActorId{ actorFormId },
                    &MARAS::GetEngineWerewolfStateProvider());
                const auto* player = RE::PlayerCharacter::GetSingleton();
                const std::string playerName = player ? player->GetName() : "the Dragonborn";

                auto getPlayerRel = [&selfView](RE::FormID fid) -> std::string {
                    const auto pair = selfView.GetPerceivedPair(
                        MARAS::State::ActorId{ fid }, MARAS::State::ActorId::Player());
                    switch (pair ? pair->status : MARAS::State::RelationshipStatus::Unknown) {
                        case MARAS::State::RelationshipStatus::Engaged:  return "became engaged to";
                        case MARAS::State::RelationshipStatus::Married:  return "married";
                        case MARAS::State::RelationshipStatus::Jilted:   return "had a brief engagement with";
                        case MARAS::State::RelationshipStatus::Divorced: return "married and later divorced";
                        case MARAS::State::RelationshipStatus::ExDating: return "briefly dated";
                        case MARAS::State::RelationshipStatus::ExLover:  return "were lovers with";
                        default: return "";
                    }
                };

                // Perspective 1: MARAS-tracked actor who left a prior in-game relationship for the player.
                const std::string playerRel1 = getPlayerRel(actorFormId);
                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 playerRel='{}'.", playerRel1);

                if (!playerRel1.empty()) {
                    RE::TESNPC* exPartnerBase = nullptr;
                    std::string relationType;

                    exPartnerBase = FindRelatedNpcBase(actor, 0x000142CA, INT32_MIN);  // Spouse
                    if (exPartnerBase) {
                        relationType = "married";
                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 found Spouse '{}'.", exPartnerBase->GetName());
                    } else {
                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 no Spouse found, checking Courting.");
                        exPartnerBase = FindRelatedNpcBase(actor, 0x0001EE23, INT32_MIN);  // Courting
                        if (exPartnerBase) {
                            relationType = "courting";
                            MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 found Courting '{}'.", exPartnerBase->GetName());
                        } else {
                            MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 no Courting found, checking Lover.");
                            exPartnerBase = FindRelatedNpcBase(actor, 0, 4);  // rank 4 = kLover
                            if (exPartnerBase) {
                                relationType = "lover";
                                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 found Lover '{}'.", exPartnerBase->GetName());
                            } else {
                                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 no prior relationship found.");
                            }
                        }
                    }

                    if (exPartnerBase) {
                        const std::string actorName = actor->GetName();
                        const std::string exName = exPartnerBase->GetName();

                        std::string sentence;
                        if (relationType == "married")
                            sentence = actorName + " divorced " + exName + " when they " + playerRel1 + " " + playerName + ".";
                        else if (relationType == "courting")
                            sentence = actorName + " ended their courtship with " + exName + " when they " + playerRel1 + " " + playerName + ".";
                        else
                            sentence = actorName + " left their romance with " + exName + " when they " + playerRel1 + " " + playerName + ".";

                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P1 result='{}'.", sentence);
                        return sentence;
                    }
                }

                // Perspective 2: Actor NOT tracked by MARAS whose prior partner is now tracked with the player.
                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: not MARAS-tracked, checking P2.");

                RE::TESNPC* exPartner2Base = nullptr;
                std::string relationType2;

                exPartner2Base = FindRelatedNpcBase(actor, 0x000142CA, INT32_MIN);  // Spouse
                if (exPartner2Base) {
                    relationType2 = "married";
                    MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 found Spouse '{}'.", exPartner2Base->GetName());
                } else {
                    MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 no Spouse found, checking Courting.");
                    exPartner2Base = FindRelatedNpcBase(actor, 0x0001EE23, INT32_MIN);  // Courting
                    if (exPartner2Base) {
                        relationType2 = "courting";
                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 found Courting '{}'.", exPartner2Base->GetName());
                    } else {
                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 no Courting found, checking Lover.");
                        exPartner2Base = FindRelatedNpcBase(actor, 0, 4);  // rank 4 = kLover
                        if (exPartner2Base) {
                            relationType2 = "lover";
                            MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 found Lover '{}'.", exPartner2Base->GetName());
                        } else {
                            MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 no prior relationship found.");
                        }
                    }
                }

                if (exPartner2Base) {
                    const RE::FormID targetBaseId = exPartner2Base->GetFormID();
                    const auto playerRelationships =
                        selfView.GetPerceivedRelationshipsOf(MARAS::State::ActorId::Player());
                    RE::FormID matchedFormID = 0;
                    auto findByStatus = [&](MARAS::State::RelationshipStatus status) {
                        if (matchedFormID != 0) return;
                        for (const auto& edge : playerRelationships) {
                            if (edge.status != status) continue;
                            const RE::FormID fid = edge.Other(MARAS::State::ActorId::Player()).raw();
                            if (fid == 0) continue;
                            RE::FormID candidateBaseId = MARAS::ResolveBaseFormIDSafe(fid, *spatialSnap);
                            if (candidateBaseId == 0) {
                                if (const auto* base = MARAS::ActorSvc::GetActorBase(fid)) {
                                    candidateBaseId = base->GetFormID();
                                }
                            }
                            if (candidateBaseId == targetBaseId) {
                                matchedFormID = fid;
                                break;
                            }
                        }
                    };
                    findByStatus(MARAS::State::RelationshipStatus::Married);
                    findByStatus(MARAS::State::RelationshipStatus::Engaged);
                    findByStatus(MARAS::State::RelationshipStatus::Jilted);
                    findByStatus(MARAS::State::RelationshipStatus::Divorced);
                    findByStatus(MARAS::State::RelationshipStatus::ExDating);
                    findByStatus(MARAS::State::RelationshipStatus::ExLover);

                    MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 partner='{}' matchedFormID=0x{:08X}.",
                                    exPartner2Base->GetName(), matchedFormID);

                    if (matchedFormID) {
                        const std::string playerRel2 = getPlayerRel(matchedFormID);
                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 playerRel='{}'.", playerRel2);

                        if (!playerRel2.empty()) {
                            const std::string actorName = actor->GetName();
                            const std::string partnerName = exPartner2Base->GetName();

                            std::string sentence;
                            if (relationType2 == "married")
                                sentence = partnerName + " divorced " + actorName + " when " + partnerName + " " + playerRel2 + " " + playerName + ".";
                            else if (relationType2 == "courting")
                                sentence = actorName + " and " + partnerName + " were courting until " + partnerName + " " + playerRel2 + " " + playerName + ".";
                            else
                                sentence = actorName + " and " + partnerName + " were lovers until " + partnerName + " " + playerRel2 + " " + playerName + ".";

                            MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 result='{}'.", sentence);
                            return sentence;
                        }

                        MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 partner '{}' is not MARAS-tracked, no result.",
                                        exPartner2Base->GetName());
                    }
                }

                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: no result found.");
                return "";
            });

        // 2. ttm_get_propose_dating_candidates
        RegisterDecoratorExport(
            "ttm_get_propose_dating_candidates",
            "Returns a comma-separated list of names of all nearby eligible candidates for starting a dating relationship with this actor, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetNearbyCandidatesByEligibility(
                    a,
                    [&](auto fid) { return m.IsAvailableForDating(fid); },
                    [&](auto fid, auto tid) { return m.IsAvailableForDating(fid, tid); });
            });

        // 3. ttm_get_propose_lover_candidates
        RegisterDecoratorExport(
            "ttm_get_propose_lover_candidates",
            "Returns a comma-separated list of names of all nearby eligible candidates for becoming lovers with this actor, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetNearbyCandidatesByEligibility(
                    a,
                    [&](auto fid) { return m.IsAvailableForLover(fid); },
                    [&](auto fid, auto tid) { return m.IsAvailableForLover(fid, tid); },
                    [&](auto fid, auto tid) {
                        return m.GetRelationshipStatus(fid, tid) != MARAS::Foundation::RelationshipStatus::Lover;
                    });
            });

        // 4. ttm_get_propose_engagement_candidates
        RegisterDecoratorExport(
            "ttm_get_propose_engagement_candidates",
            "Returns the name of the nearby dating partner (if dating) or a comma-separated list of nearby eligible candidates (if single) for proposing engagement, or empty string if none.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const auto fid = actor->GetFormID();
                const auto& mgr = MARAS::App::RelationshipUseCases::GetSingleton();
                if (!mgr.IsAvailableForEngaged(fid)) {
                    return "";
                }

                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const float radius = GetNearbyActorsRadius();
                const auto selfActorId = MARAS::State::ActorId{ fid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());

                // If the actor is currently dating someone, that specific partner is the only valid target
                std::vector<RE::FormID> datingPartners;
                for (const auto& edge : selfView.GetPerceivedRelationshipsOf(selfActorId)) {
                    if (edge.status == MARAS::State::RelationshipStatus::Dating) {
                        datingPartners.push_back(edge.Other(selfActorId).raw());
                    }
                }
                if (!datingPartners.empty()) {
                    const auto datingPartnerFid = datingPartners[0];
                    std::string matchResult;
                    spatialSnap->ForEachNearbyCandidate(MARAS::State::RefId{ fid }, radius,
                        [&](const MARAS::State::LoadedActorSample& sample) {
                            if (!matchResult.empty()) return;
                            if (sample.refId.raw() == datingPartnerFid &&
                                mgr.IsAvailableForEngaged(fid, datingPartnerFid)) {
                                matchResult = GetCandidateDisplayName(datingPartnerFid, *worldSnap, *spatialSnap);
                            }
                        });
                    return matchResult;
                }

                // If single, find all nearby eligible candidates without leaking third-party secret Dating edges
                std::string result;
                spatialSnap->ForEachNearbyCandidate(MARAS::State::RefId{ fid }, radius,
                    [&](const MARAS::State::LoadedActorSample& sample) {
                        const auto targetFid = sample.refId.raw();
                        const MARAS::State::ActorId targetActorId{ targetFid };
                        for (const auto& edge : selfView.GetPerceivedRelationshipsOf(targetActorId)) {
                            if (edge.status == MARAS::State::RelationshipStatus::Dating &&
                                edge.Other(targetActorId).raw() != fid) {
                                return;
                            }
                        }
                        if (mgr.IsAvailableForEngaged(fid, targetFid)) {
                            const std::string name = GetCandidateDisplayName(targetFid, *worldSnap, *spatialSnap);
                            if (!name.empty()) {
                                if (!result.empty()) result += ", ";
                                result += name;
                            }
                        }
                    });
                return result;
            });

        // 5. ttm_get_breakup_dating_candidates
        RegisterDecoratorExport(
            "ttm_get_breakup_dating_candidates",
            "Returns a comma-separated list of names of all partners currently dating this actor that can be broken up with, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetPartnersByStatus(
                    a,
                    MARAS::Foundation::RelationshipStatus::Dating,
                    [&](auto fid, auto pid) { return m.IsAvailableForBreakupDating(fid, pid); });
            });

        // 6. ttm_get_breakup_lover_candidates
        RegisterDecoratorExport(
            "ttm_get_breakup_lover_candidates",
            "Returns a comma-separated list of names of all partners currently in a lover relationship with this actor that can be broken up with, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetPartnersByStatus(
                    a,
                    MARAS::Foundation::RelationshipStatus::Lover,
                    [&](auto fid, auto pid) { return m.IsAvailableForBreakupLover(fid, pid); });
            });

        // 7. ttm_get_breakup_engagement_candidates
        RegisterDecoratorExport(
            "ttm_get_breakup_engagement_candidates",
            "Returns a comma-separated list of names of all partners currently engaged to this actor that can be broken up with (called off), or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetPartnersByStatus(
                    a,
                    MARAS::Foundation::RelationshipStatus::Engaged,
                    [&](auto fid, auto pid) { return m.IsAvailableForJilted(fid, pid); });
            });

        // 8. ttm_get_divorce_candidates
        RegisterDecoratorExport(
            "ttm_get_divorce_candidates",
            "Returns a comma-separated list of names of all spouses currently married to this actor that can be divorced, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetPartnersByStatus(
                    a,
                    MARAS::Foundation::RelationshipStatus::Married,
                    [&](auto fid, auto pid) { return m.IsAvailableForDivorced(fid, pid); });
            });

        // 9. ttm_get_open_relationship_candidates
        RegisterDecoratorExport(
            "ttm_get_open_relationship_candidates",
            "Returns a comma-separated list of names of all nearby closed main-tier partners for this actor that can be opened, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetNearbyCandidatesByEligibility(
                    a,
                    [&](auto fid) { return m.IsAvailableForOpenRelationship(fid); },
                    [&](auto fid, auto tid) { return m.IsAvailableForOpenRelationship(fid, tid); });
            });

        // 10. ttm_get_close_relationship_candidates
        RegisterDecoratorExport(
            "ttm_get_close_relationship_candidates",
            "Returns a comma-separated list of names of all nearby open main-tier partners for this actor that can be closed, or empty string if none.",
            [](RE::Actor* a) {
                const auto& m = MARAS::App::RelationshipUseCases::GetSingleton();
                return GetNearbyCandidatesByEligibility(
                    a,
                    [&](auto fid) { return m.IsAvailableForCloseRelationship(fid); },
                    [&](auto fid, auto tid) { return m.IsAvailableForCloseRelationship(fid, tid); });
            });

        // 11. ttm_get_exclusive_main_partners
        RegisterDecoratorExport(
            "ttm_get_exclusive_main_partners",
            "Returns a comma-separated list of names of all exclusive (non-open) main-tier partners (dating, engaged, married) for this actor across all MARAS relationships that would be cheated on, or empty string if none.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const auto fid = actor->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                if (fid == MARAS::kPlayerFormID && worldSnap->settings->enablePlayerPolygamy) {
                    return "";
                }

                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ fid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto perceived = selfView.GetPerceivedRelationshipsOf(selfActorId);
                std::string result;
                auto appendExclusivePartners = [&](MARAS::State::RelationshipStatus status) {
                    for (const auto& edge : perceived) {
                        if (edge.status != status || edge.isOpen) {
                            continue;
                        }
                        const auto partnerActorId = edge.Other(selfActorId);
                        const std::string name = MARAS::ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                        if (!name.empty() && name != "None") {
                            if (!result.empty()) result += ", ";
                            result += name;
                        }
                    }
                };

                appendExclusivePartners(MARAS::State::RelationshipStatus::Married);
                appendExclusivePartners(MARAS::State::RelationshipStatus::Engaged);
                appendExclusivePartners(MARAS::State::RelationshipStatus::Dating);

                return result;
            });

        // 12. ttm_get_lovers_becoming_ex
        RegisterDecoratorExport(
            "ttm_get_lovers_becoming_ex",
            "Returns a comma-separated list of names of all lover-status partners for this actor that will become ex-lovers upon entering dating or engagement, or empty string if none.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const auto fid = actor->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                if (fid == MARAS::kPlayerFormID && worldSnap->settings->enablePlayerPolygamy) {
                    return "";
                }

                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ fid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto perceived = selfView.GetPerceivedRelationshipsOf(selfActorId);
                for (const auto& edge : perceived) {
                    if (MARAS::State::IsMainTierStatus(edge.status) && edge.isOpen) {
                        return "";
                    }
                }

                std::string result;
                for (const auto& edge : perceived) {
                    if (edge.status != MARAS::State::RelationshipStatus::Lover) {
                        continue;
                    }
                    const auto partnerActorId = edge.Other(selfActorId);
                    const std::string name = MARAS::ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                    if (!name.empty() && name != "None") {
                        if (!result.empty()) result += ", ";
                        result += name;
                    }
                }
                return result;
            });

        // 13. ttm_get_dating_partner
        RegisterDecoratorExport(
            "ttm_get_dating_partner",
            "Returns the name of the dating partner for this actor, or empty string if not currently in a dating relationship.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const auto fid = actor->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ fid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                for (const auto& edge : selfView.GetPerceivedRelationshipsOf(selfActorId)) {
                    if (edge.status == MARAS::State::RelationshipStatus::Dating) {
                        const std::string name = MARAS::ResolveActorNameSafe(
                            edge.Other(selfActorId).raw(), *worldSnap, *spatialSnap);
                        if (!name.empty() && name != "None") return name;
                        break;
                    }
                }
                return "";
            });

        // 14. ttm_get_open_main_partners
        RegisterDecoratorExport(
            "ttm_get_open_main_partners",
            "Returns a comma-separated list of names of all open main-tier partners (dating, engaged, married) for this actor, or empty string if none.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const auto fid = actor->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ fid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto perceived = selfView.GetPerceivedRelationshipsOf(selfActorId);
                std::string result;
                auto appendOpenPartners = [&](MARAS::State::RelationshipStatus status) {
                    for (const auto& edge : perceived) {
                        if (edge.status == status && edge.isOpen) {
                            const auto partnerActorId = edge.Other(selfActorId);
                            const std::string name = MARAS::ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                            if (!name.empty() && name != "None") {
                                if (!result.empty()) result += ", ";
                                result += name;
                            }
                        }
                    }
                };

                appendOpenPartners(MARAS::State::RelationshipStatus::Married);
                appendOpenPartners(MARAS::State::RelationshipStatus::Engaged);
                appendOpenPartners(MARAS::State::RelationshipStatus::Dating);

                return result;
            });

        // 15. ttm_is_registered
        RegisterDecoratorExport(
            "ttm_is_registered",
            "Returns JSON boolean true if the actor is registered in MARAS (player, tracked NPC, registered child, or pregnant), or false otherwise.",
            [](RE::Actor* actor) -> std::string {
                if (!actor) {
                    return "false";
                }
                const auto fid = actor->GetFormID();
                if (fid == 0) {
                    return "false";
                }

                if (fid == MARAS::kPlayerFormID) {
                    return "true";
                }

                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ fid };
                const MARAS::State::RefId selfRefId{ fid };

                if (worldSnap->actors->IsRegisteredAdult(selfRefId) || worldSnap->actors->Find(selfRefId) != nullptr) {
                    return "true";
                }

                const auto omniscientView = MARAS::Projection::PerspectiveView::ForDashboard(
                    worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &MARAS::GetEngineWerewolfStateProvider());
                if (omniscientView.GetPerceivedChild(MARAS::State::ChildId{ fid }).has_value()) {
                    return "true";
                }
                if (omniscientView.GetSelfPregnancy(selfActorId).has_value()) {
                    return "true";
                }

                for (const auto& edge : omniscientView.GetPerceivedRelationshipsOf(selfActorId)) {
                    if (edge.status != MARAS::State::RelationshipStatus::Unknown &&
                        edge.status != MARAS::State::RelationshipStatus::Candidate) {
                        return "true";
                    }
                }

                if (const auto* sample = spatialSnap->Find(selfRefId); sample && sample->IsTrackedFaction()) {
                    return "true";
                }

                return "false";
            });

        // 16. ttm_npc_relationship_profile
        RegisterDecoratorExport(
            "ttm_npc_relationship_profile",
            "Returns schema-v2 JSON containing actor-relative family, current and past relationships, Player co-partners, and knowledge-filtered infidelity records.",
            [](RE::Actor* actor) -> std::string {
                try {
                    return BuildRelationshipProfileJSON(actor);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[SkyrimNet] Exception in ttm_npc_relationship_profile: {}", e.what());
                    return "{}";
                } catch (...) {
                    MARAS_LOG_ERROR("[SkyrimNet] Unknown exception in ttm_npc_relationship_profile");
                    return "{}";
                }
            });

        // 17. ttm_get_nearby_actors
        RegisterDecoratorExport(
            "ttm_get_nearby_actors",
            "JSON object listing all nearby valid actors (including the player, within nearbyActors.radius). Fields: actorIds (array of SkyrimNet UUIDs).",
            [](RE::Actor* actor) -> std::string {
                nlohmann::json j;
                j["actorIds"] = nlohmann::json::array();
                if (!actor) {
                    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                }

                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const float radius = GetNearbyActorsRadius();
                spatialSnap->ForEachNearbyCandidate(MARAS::State::RefId{ actor->GetFormID() }, radius,
                    [&](const MARAS::State::LoadedActorSample& sample) {
                        const std::uint64_t uuid = GetActorUUID(sample.refId.raw());
                        if (uuid != 0) {
                            j["actorIds"].push_back(uuid);
                        }
                    });

                return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            });

        auto collectNearbyIds = [](RE::FormID centerFid,
                                   const MARAS::State::SpatialActorSnapshot& spatial,
                                   float radius) {
            std::vector<RE::FormID> ids;
            spatial.ForEachNearbyCandidate(MARAS::State::RefId{ centerFid }, radius,
                [&](const MARAS::State::LoadedActorSample& sample) {
                    ids.push_back(sample.refId.raw());
                });
            return ids;
        };

        // 18. ttm_get_available_actions
        RegisterDecoratorExport(
            "ttm_get_available_actions",
            "Returns a string with all available relationship actions for this actor listed with short explanations.",
            [collectNearbyIds](RE::Actor* actor) -> std::string {
                if (!actor) return "";
                const std::uint32_t actorFormId = actor->GetFormID();

                const auto& mgr = MARAS::App::RelationshipUseCases::GetSingleton();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const auto nearbyIds = collectNearbyIds(actorFormId, *spatialSnap, GetNearbyActorsRadius());

                std::vector<std::string> actions;

                // Dating
                if (mgr.IsAvailableForDating(actorFormId)) {
                    bool hasDatingTarget = false;
                    for (const RE::FormID targetFid : nearbyIds) {
                        if (mgr.IsAvailableForDating(actorFormId, targetFid)) {
                            hasDatingTarget = true;
                            break;
                        }
                    }
                    if (hasDatingTarget) {
                        actions.push_back("ProposeDating");
                        actions.push_back("ConsiderDatingProposal");
                    }
                }

                // Lover
                bool hasLoverTarget = false;
                if (mgr.IsAvailableForLover(actorFormId)) {
                    for (const RE::FormID targetFid : nearbyIds) {
                        if (mgr.IsAvailableForLover(actorFormId, targetFid)) {
                            hasLoverTarget = true;
                            break;
                        }
                    }
                }
                if (hasLoverTarget) {
                    actions.push_back("ProposeLover");
                    actions.push_back("ConsiderLoverProposal");
                }

                // Engagement
                if (mgr.IsAvailableForEngaged(actorFormId)) {
                    bool hasEngagedTarget = false;
                    const MARAS::State::ActorId selfActorId{ actorFormId };
                    const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                        worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                    RE::FormID datingPartnerFid = 0;
                    for (const auto& edge : selfView.GetPerceivedRelationshipsOf(selfActorId)) {
                        if (edge.status == MARAS::State::RelationshipStatus::Dating) {
                            datingPartnerFid = edge.Other(selfActorId).raw();
                            break;
                        }
                    }
                    if (datingPartnerFid != 0) {
                        for (const RE::FormID targetFid : nearbyIds) {
                            if (MARAS::MatchesActor(targetFid, datingPartnerFid) &&
                                mgr.IsAvailableForEngaged(actorFormId, datingPartnerFid)) {
                                hasEngagedTarget = true;
                                break;
                            }
                        }
                    } else {
                        for (const RE::FormID targetFid : nearbyIds) {
                            if (mgr.IsAvailableForEngaged(actorFormId, targetFid)) {
                                hasEngagedTarget = true;
                                break;
                            }
                        }
                    }

                    if (hasEngagedTarget) {
                        actions.push_back("ProposeEngagement");
                        actions.push_back("ConsiderEngagementProposal");
                    }
                }

                if (mgr.IsAvailableForBreakupDating(actorFormId)) {
                    actions.push_back("BreakupDating");
                }
                if (mgr.IsAvailableForBreakupLover(actorFormId)) {
                    actions.push_back("BreakupLover");
                }
                if (mgr.IsAvailableForJilted(actorFormId)) {
                    actions.push_back("CallOffEngagement");
                }
                if (mgr.IsAvailableForDivorced(actorFormId)) {
                    actions.push_back("Divorce");
                }
                if (mgr.IsAvailableForOpenRelationship(actorFormId)) {
                    bool hasOpenTarget = false;
                    for (const RE::FormID targetFid : nearbyIds) {
                        if (mgr.IsAvailableForOpenRelationship(actorFormId, targetFid)) {
                            hasOpenTarget = true;
                            break;
                        }
                    }
                    if (hasOpenTarget) {
                        actions.push_back("OpenRelationship");
                    }
                }
                if (mgr.IsAvailableForCloseRelationship(actorFormId)) {
                    bool hasCloseTarget = false;
                    for (const RE::FormID targetFid : nearbyIds) {
                        if (mgr.IsAvailableForCloseRelationship(actorFormId, targetFid)) {
                            hasCloseTarget = true;
                            break;
                        }
                    }
                    if (hasCloseTarget) {
                        actions.push_back("CloseRelationship");
                    }
                }

                if (actions.empty()) {
                    return "None";
                }

                std::string result;
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += actions[i];
                }
                return result;
            });

        // 19. ttm_get_reveal_child_secret_candidates
        RegisterDecoratorExport(
            "ttm_get_reveal_child_secret_candidates",
            "Returns a comma-separated list of candidate fathers (presumed, bio, or both) nearby for whom the mother has an active paternity secret to reveal, or empty string if none.",
            [collectNearbyIds](RE::Actor* a) -> std::string {
                if (!a) return "";
                const auto aFid = a->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap,
                    spatialSnap,
                    MARAS::State::ActorId{ aFid },
                    &MARAS::GetEngineWerewolfStateProvider());
                const auto visibleChildren = selfView.GetVisibleChildren();
                if (visibleChildren.empty()) return "";

                const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
                if (nearbyIds.empty()) return "";

                auto isNear = [&](RE::FormID targetFid) -> bool {
                    if (targetFid == 0) return false;
                    for (const RE::FormID nearbyFid : nearbyIds) {
                        if (MARAS::MatchesActor(targetFid, nearbyFid)) return true;
                    }
                    return false;
                };

                std::string res;
                std::set<std::pair<RE::FormID, RE::FormID>> seenUnbornFathers;
                for (const auto& entry : visibleChildren) {
                    const RE::FormID motherID = entry.bioMother.raw();
                    if (!MARAS::MatchesActor(motherID, aFid)) continue;
                    if (!entry.paternitySecret.has_value()) continue;
                    const auto& sec = *entry.paternitySecret;
                    if (sec.discovered || (!sec.isSecret && !sec.isPregnancySecretForPartner)) continue;

                    const RE::FormID motherPartnerID = entry.motherPartner.raw();
                    const RE::FormID bioFatherID = sec.trueBioFather.raw();
                    if (motherPartnerID != 0 && bioFatherID != 0 && MARAS::MatchesActor(bioFatherID, motherPartnerID)) continue;

                    if (entry.stage == MARAS::State::ChildStage::Unborn) {
                        if (!seenUnbornFathers.insert({ motherPartnerID, bioFatherID }).second) {
                            continue;
                        }
                    }

                    const bool pNear = (sec.isSecretForPartner || sec.isPregnancySecretForPartner) &&
                                       motherPartnerID != 0 && isNear(motherPartnerID);
                    const bool bNear = sec.isSecretForBioFather && bioFatherID != 0 && isNear(bioFatherID);

                    std::string childDesc =
                        (entry.name.empty() || MARAS::Foundation::StringUtils::CaseInsensitiveEquals(entry.name, "Newborn"))
                            ? "child"
                            : entry.name;
                    std::string partnerRoleLabel = "presumed father of ";
                    if (entry.stage == MARAS::State::ChildStage::Unborn) {
                        const std::string bioName = !sec.trueBioFatherFallbackName.empty()
                            ? sec.trueBioFatherFallbackName
                            : MARAS::ResolveActorNameSafe(bioFatherID, *worldSnap, *spatialSnap);
                        childDesc = "unborn pregnancy" + (!bioName.empty() ? " fathered by " + bioName : "");
                        if (sec.isPregnancySecretForPartner) {
                            partnerRoleLabel = "partner unaware of pregnancy (use revealMode 'pregnancy_only' to announce pregnancy so he believes it is his, or 'true_paternity' to confess true father) for ";
                        } else {
                            partnerRoleLabel = "partner / presumed father (use revealMode 'true_paternity' to confess true father) of ";
                        }
                    }

                    if (pNear && bNear) {
                        const std::string pName = GetCandidateDisplayName(motherPartnerID, *worldSnap, *spatialSnap);
                        const std::string bName = GetCandidateDisplayName(bioFatherID, *worldSnap, *spatialSnap);
                        if (!res.empty()) res += ", ";
                        res += pName + " (" + partnerRoleLabel + childDesc + ")";
                        res += ", " + bName + " (biological father of " + childDesc + ")";
                        res += ", or Both [" + pName + " and " + bName + "] (regarding " + childDesc + ")";
                    } else if (pNear) {
                        const std::string pName = GetCandidateDisplayName(motherPartnerID, *worldSnap, *spatialSnap);
                        if (!res.empty()) res += ", ";
                        res += pName + " (" + partnerRoleLabel + childDesc + ")";
                    } else if (bNear) {
                        const std::string bName = GetCandidateDisplayName(bioFatherID, *worldSnap, *spatialSnap);
                        if (!res.empty()) res += ", ";
                        res += bName + " (biological father of " + childDesc + ")";
                    }
                }
                return res;
            });

        // 20. ttm_get_discover_child_paternity_candidates
        RegisterDecoratorExport(
            "ttm_get_discover_child_paternity_candidates",
            "Returns a comma-separated list of children (and mothers) nearby about whom this father has discovered / found out an active paternity secret, or empty string if none.",
            [collectNearbyIds](RE::Actor* a) -> std::string {
                if (!a) return "";
                const auto aFid = a->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const auto omniscientView = MARAS::Projection::PerspectiveView::ForDashboard(
                    worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &MARAS::GetEngineWerewolfStateProvider());
                const auto allChildren = omniscientView.GetVisibleChildren();
                if (allChildren.empty()) return "";

                const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
                if (nearbyIds.empty()) return "";

                auto isNear = [&](RE::FormID targetFid) -> bool {
                    if (targetFid == 0) return false;
                    for (const RE::FormID nearbyFid : nearbyIds) {
                        if (MARAS::MatchesActor(targetFid, nearbyFid)) return true;
                    }
                    return false;
                };

                std::string res;
                std::set<std::pair<RE::FormID, RE::FormID>> seenUnbornCandidates;
                for (const auto& entry : allChildren) {
                    if (!entry.paternitySecret.has_value()) continue;
                    const auto& sec = *entry.paternitySecret;
                    if (sec.discovered || (!sec.isSecret && !sec.isPregnancySecretForPartner)) continue;

                    const RE::FormID cID = entry.childId.raw();
                    const RE::FormID motherID = entry.bioMother.raw();
                    const bool motherNear = (motherID != 0 && isNear(motherID));
                    const bool childNear = (cID != 0 && !MARAS::IsSyntheticChildID(cID) && isNear(cID));

                    // Father can discover/learn the truth if either the mother OR the child is nearby
                    if (!motherNear && !childNear) continue;

                    const RE::FormID motherPartnerID = entry.motherPartner.raw();
                    const RE::FormID bioFatherID = sec.trueBioFather.raw();
                    if (motherPartnerID != 0 && bioFatherID != 0 && MARAS::MatchesActor(bioFatherID, motherPartnerID)) continue;

                    bool isUnannouncedPlayerPregnancy = false;
                    if (entry.stage == MARAS::State::ChildStage::Unborn) {
                        if (motherID != MARAS::kPlayerFormID) {
                            // Never leak an unannounced / non-showing NPC pregnancy into an unaware partner's or biological father's action candidates.
                            if (MARAS::MatchesActor(motherPartnerID, aFid) && sec.isPregnancySecretForPartner) {
                                continue;
                            }
                            if (MARAS::MatchesActor(bioFatherID, aFid) && sec.isSecretForBioFather) {
                                continue;
                            }
                        } else {
                            if (!motherNear) {
                                continue;
                            }
                            const float currentDay = (spatialSnap->CurrentGameDay() >= 0.0f)
                                ? spatialSnap->CurrentGameDay()
                                : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);
                            isUnannouncedPlayerPregnancy = sec.isPregnancySecretForPartner ||
                                (motherPartnerID == 0 && entry.conceptionTimestamp > 0.0f &&
                                 (currentDay - entry.conceptionTimestamp) < 3.0f);
                        }
                        if (!seenUnbornCandidates.insert({ motherID, bioFatherID }).second) {
                            continue;
                        }
                    }

                    const std::string motherName = (motherID != 0)
                        ? GetCandidateDisplayName(motherID, *worldSnap, *spatialSnap)
                        : "Unknown Mother";
                    // IMPORTANT: Never include the secret biological father's name in childDesc for an unaware father!
                    const std::string childDesc = (entry.stage == MARAS::State::ChildStage::Unborn)
                        ? "unborn pregnancy"
                        : ((entry.name.empty() || MARAS::Foundation::StringUtils::CaseInsensitiveEquals(entry.name, "Newborn"))
                               ? "child"
                               : entry.name);
                    const std::string unannouncedTag = isUnannouncedPlayerPregnancy
                        ? "; UNANNOUNCED — you DO NOT know she is pregnant unless she explicitly reveals/confesses it in dialogue right now"
                        : "";

                    if (MARAS::MatchesActor(motherPartnerID, aFid) && sec.isSecretForPartner) {
                        if (!res.empty()) res += ", ";
                        res += childDesc + " (child with " + motherName +
                               (entry.stage == MARAS::State::ChildStage::Unborn
                                    ? "; partner / presumed father"
                                    : "; presumed father") +
                               unannouncedTag;
                        if (entry.stage != MARAS::State::ChildStage::Unborn && sec.hasPhenotypeMismatch) {
                            res += ", noticeable physical difference";
                        }
                        res += ")";
                    } else if (MARAS::MatchesActor(bioFatherID, aFid) && sec.isSecretForBioFather) {
                        if (!res.empty()) res += ", ";
                        res += childDesc + " (child with " + motherName + "; biological father" + unannouncedTag + ")";
                    }
                }
                return res;
            });

        // 21. ttm_get_confess_affair_candidates
        RegisterDecoratorExport(
            "ttm_get_confess_affair_candidates",
            "Returns a comma-separated list of partners nearby to whom this actor can confess an active secret affair, or empty string if none.",
            [collectNearbyIds](RE::Actor* a) -> std::string {
                if (!a) return "";
                const auto aFid = a->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ aFid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
                if (records.empty()) return "";

                const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
                if (nearbyIds.empty()) return "";

                auto isNear = [&](RE::FormID targetFid) -> bool {
                    if (targetFid == 0) return false;
                    for (const RE::FormID nearbyFid : nearbyIds) {
                        if (MARAS::MatchesActor(targetFid, nearbyFid)) return true;
                    }
                    return false;
                };

                std::string res;
                std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
                for (const auto& rec : records) {
                    if (rec.cheater != selfActorId) continue;
                    if (!rec.isSecret || rec.affairStatus != MARAS::State::AffairStatus::Ongoing) continue;
                    const RE::FormID victimID = rec.deceivedPartner.raw();
                    const RE::FormID partnerID = rec.paramour.raw();
                    if (!isNear(victimID)) continue;
                    if (!seenPairs.insert({ victimID, partnerID }).second) continue;

                    const std::string victimName = GetCandidateDisplayName(victimID, *worldSnap, *spatialSnap);
                    const std::string partnerName = GetCandidateDisplayName(partnerID, *worldSnap, *spatialSnap);

                    if (!res.empty()) res += ", ";
                    res += victimName + " (partner; secret affair with " + partnerName + ")";
                }
                return res;
            });

        // 22. ttm_get_expose_affair_candidates
        RegisterDecoratorExport(
            "ttm_get_expose_affair_candidates",
            "Returns a comma-separated list of victims nearby to whom this affair partner can expose a secret affair, or empty string if none.",
            [collectNearbyIds](RE::Actor* a) -> std::string {
                if (!a) return "";
                const auto aFid = a->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ aFid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
                if (records.empty()) return "";

                const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
                if (nearbyIds.empty()) return "";

                auto isNear = [&](RE::FormID targetFid) -> bool {
                    if (targetFid == 0) return false;
                    for (const RE::FormID nearbyFid : nearbyIds) {
                        if (MARAS::MatchesActor(targetFid, nearbyFid)) return true;
                    }
                    return false;
                };

                std::string res;
                std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
                for (const auto& rec : records) {
                    if (rec.paramour != selfActorId) continue;
                    if (!rec.isSecret || rec.affairStatus != MARAS::State::AffairStatus::Ongoing) continue;
                    const RE::FormID victimID = rec.deceivedPartner.raw();
                    const RE::FormID cheaterID = rec.cheater.raw();
                    if (!isNear(victimID)) continue;
                    if (!seenPairs.insert({ victimID, cheaterID }).second) continue;

                    const std::string victimName = GetCandidateDisplayName(victimID, *worldSnap, *spatialSnap);
                    const std::string cheaterName = GetCandidateDisplayName(cheaterID, *worldSnap, *spatialSnap);

                    if (!res.empty()) res += ", ";
                    res += victimName + " (victim; secret affair with " + cheaterName + ")";
                }
                return res;
            });

        // 23. ttm_get_resolve_affair_candidates
        RegisterDecoratorExport(
            "ttm_get_resolve_affair_candidates",
            "Returns a comma-separated list of partners nearby with whom this actor has an exposed ongoing affair to resolve, or empty string if none.",
            [collectNearbyIds](RE::Actor* a) -> std::string {
                if (!a) return "";
                const auto aFid = a->GetFormID();
                const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
                const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
                const MARAS::State::ActorId selfActorId{ aFid };
                const auto selfView = MARAS::Projection::PerspectiveView::ForActor(
                    worldSnap, spatialSnap, selfActorId, &MARAS::GetEngineWerewolfStateProvider());
                const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
                if (records.empty()) return "";

                const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
                if (nearbyIds.empty()) return "";

                auto isNear = [&](RE::FormID targetFid) -> bool {
                    if (targetFid == 0) return false;
                    for (const RE::FormID nearbyFid : nearbyIds) {
                        if (MARAS::MatchesActor(targetFid, nearbyFid)) return true;
                    }
                    return false;
                };

                std::string res;
                std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
                for (const auto& rec : records) {
                    if (rec.isSecret || rec.affairStatus != MARAS::State::AffairStatus::Ongoing) continue;

                    RE::FormID otherParty = 0;
                    if (rec.cheater == selfActorId) {
                        otherParty = rec.deceivedPartner.raw();
                    } else if (rec.deceivedPartner == selfActorId) {
                        otherParty = rec.cheater.raw();
                    } else {
                        continue;
                    }

                    if (!isNear(otherParty)) continue;
                    const RE::FormID partnerID = rec.paramour.raw();
                    if (!seenPairs.insert({ otherParty, partnerID }).second) continue;

                    const std::string otherName = GetCandidateDisplayName(otherParty, *worldSnap, *spatialSnap);
                    const std::string partnerName = GetCandidateDisplayName(partnerID, *worldSnap, *spatialSnap);

                    if (!res.empty()) res += ", ";
                    res += otherName + " (regarding exposed affair with " + partnerName + ")";
                }
                return res;
            });

        MARAS_LOG_INFO("[SkyrimNet] Decorator registration complete.");
    }

    bool EvaluatePaternitySecret(std::uint32_t childID) {
        if (!IsSkyrimNetAvailable()) return false;

        const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto omniscientView = MARAS::Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &MARAS::GetEngineWerewolfStateProvider());
        const auto entryOpt = omniscientView.GetPerceivedChild(MARAS::State::ChildId{ childID });
        if (!entryOpt.has_value()) return false;

        const auto& entry = *entryOpt;
        const RE::FormID motherID = entry.bioMother.raw();
        const RE::FormID presumedFatherID = entry.motherPartner.raw();
        const RE::FormID bioFatherID = entry.perceivedBioFather.raw();

        if (motherID == 0 || presumedFatherID == 0 || bioFatherID == 0) return false;
        if (MARAS::MatchesActor(presumedFatherID, bioFatherID)) return false;

        const float currentDay = (spatialSnap->CurrentGameDay() >= 0.0f)
            ? spatialSnap->CurrentGameDay()
            : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);
        const float childAgeDays = currentDay - entry.birthTimestamp;
        const bool isAtBirth = (childAgeDays < 0.5f);

        const auto* secPtr = entry.paternitySecret ? &(*entry.paternitySecret) : nullptr;
        const bool hasVisualDiscrepancy = secPtr ? secPtr->hasPhenotypeMismatch : false;
        const std::string visualDesc = hasVisualDiscrepancy
            ? MARAS::App::PaternityWorkflowCoordinator::GetSingleton().GetVisualDiscrepancyDescription(
                  motherID, presumedFatherID, bioFatherID)
            : "";

        const std::uint64_t motherUUID = GetActorUUID(motherID);
        const std::uint64_t presumedFatherUUID = GetActorUUID(presumedFatherID);
        const std::uint64_t bioFatherUUID = GetActorUUID(bioFatherID);

        nlohmann::json ctx;
        ctx["childName"] = (entry.name.empty() || MARAS::Foundation::StringUtils::CaseInsensitiveEquals(entry.name, "Newborn"))
            ? "child"
            : entry.name;
        const MARAS::State::ActorSex resolvedChildSex = MARAS::ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
        int childGenderVal = entry.gender;
        if (resolvedChildSex == MARAS::State::ActorSex::Female) {
            childGenderVal = 1;
        } else if (resolvedChildSex == MARAS::State::ActorSex::Male) {
            childGenderVal = 0;
        }
        const std::string childGenderStr = (childGenderVal == 2) ? "unknown" : ((childGenderVal == 1) ? "female" : "male");
        const std::string childRelationStr = (childGenderVal == 2) ? "child" : ((childGenderVal == 1) ? "daughter" : "son");
        ctx["gender"] = childGenderStr;
        ctx["childGender"] = childGenderStr;
        ctx["childRelation"] = childRelationStr;
        ctx["childAgeDays"] = static_cast<int>(std::round(std::max(0.0f, childAgeDays)));
        ctx["isAtBirth"] = isAtBirth;

        ctx["motherName"] = MARAS::ResolveActorNameSafe(motherID, *worldSnap, *spatialSnap);
        ctx["motherRace"] = MARAS::ResolveActorRaceSafe(motherID, *worldSnap, *spatialSnap);
        ctx["motherUUID"] = motherUUID;

        ctx["presumedFatherName"] = MARAS::ResolveActorNameSafe(presumedFatherID, *worldSnap, *spatialSnap);
        ctx["presumedFatherRace"] = MARAS::ResolveActorRaceSafe(presumedFatherID, *worldSnap, *spatialSnap);
        ctx["presumedFatherUUID"] = presumedFatherUUID;
        const auto partnerPair = omniscientView.GetPerceivedPair(
            MARAS::State::ActorId{ motherID }, MARAS::State::ActorId{ presumedFatherID });
        const auto relStatus = partnerPair
            ? partnerPair->status
            : MARAS::Foundation::RelationshipStatus::Unknown;
        ctx["maritalStatus"] = std::string(MARAS::Foundation::RelationshipStatusToString(relStatus));

        ctx["bioFatherName"] = MARAS::ResolveActorNameSafe(bioFatherID, *worldSnap, *spatialSnap);
        ctx["bioFatherRace"] = MARAS::ResolveActorRaceSafe(bioFatherID, *worldSnap, *spatialSnap);
        ctx["bioFatherUUID"] = bioFatherUUID;

        MARAS::State::AffairStatus resolvedAffairStatus = MARAS::State::AffairStatus::Ongoing;
        for (const auto& rec : omniscientView.GetKnownAffairsInvolving(MARAS::State::ActorId{ motherID })) {
            if (rec.cheater.raw() == motherID &&
                rec.deceivedPartner.raw() == presumedFatherID &&
                rec.paramour.raw() == bioFatherID) {
                resolvedAffairStatus = rec.affairStatus;
                break;
            }
        }
        ctx["affairStatus"] = std::string(MARAS::Foundation::AffairStatusToString(resolvedAffairStatus));

        const bool secretForPartner = secPtr ? secPtr->isSecretForPartner : false;
        const bool secretForBioFather = secPtr ? secPtr->isSecretForBioFather : false;
        ctx["isSecretForPresumedFather"] = secretForPartner;
        ctx["isSecretForPartner"] = secretForPartner;
        ctx["isSecretForBioFather"] = secretForBioFather;

        ctx["hasVisualDiscrepancy"] = hasVisualDiscrepancy;
        ctx["visualDiscrepancyDescription"] = visualDesc;

        nlohmann::json recentEventsParticipants = nlohmann::json::array();
        if (motherUUID != 0) recentEventsParticipants.push_back(motherUUID);
        if (presumedFatherUUID != 0 && presumedFatherUUID != motherUUID) {
            recentEventsParticipants.push_back(presumedFatherUUID);
        }
        if (bioFatherUUID != 0 && bioFatherUUID != motherUUID && bioFatherUUID != presumedFatherUUID) {
            recentEventsParticipants.push_back(bioFatherUUID);
        }
        ctx["recent_events_participants"] = std::move(recentEventsParticipants);

        auto state = std::make_shared<PaternityEvalState>();
        state->epoch = MARAS::GetSessionEpoch();
        state->childID = childID;
        state->motherID = motherID;
        state->presumedFatherID = presumedFatherID;
        state->bioFatherID = bioFatherID;
        state->childName = ctx["childName"].get<std::string>();
        state->motherName = ctx["motherName"].get<std::string>();
        state->isSecretForPresumedFather = secretForPartner;
        state->contextJson = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        return DispatchPaternityEval(std::move(state));
    }

}  // namespace MARAS::Bridge::SkyrimNet
