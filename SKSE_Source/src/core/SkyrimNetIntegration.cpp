#include "core/SkyrimNetIntegration.h"
#include "core/DeceasedRegistry.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetDecisions.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/NPCRelationshipManager.h"
#include "core/AffectionService.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PlayerChildrenService.h"
#include "core/HomeCellService.h"
#include "core/FormCache.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/Settings.h"
#include "utils/JsonService.h"


#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/ActorUtils.h"


// The target export signature as published by SkyrimNet's public API.
// std::function is passed by value intentionally – SkyrimNet was designed this way.
using PublicRegisterDecorator_t = bool (*)(
    const char* name,
    const char* description,
    std::function<std::string(RE::Actor*)> callback);

using PublicGetActorNameByUUID_t    = std::string (*)(uint64_t uuid);
using PublicFormIDToUUID_t          = uint64_t (*)(uint32_t formId);
using PublicGetPluginConfigValue_t  = std::string (*)(const char* plugin, const char* path, const char* defaultValue);
using PublicRegisterEvent_t         = bool (*)(const char* eventType, const char* content, uint32_t originatorFormId, uint32_t targetFormId, const uint32_t* audienceFormIds, int audienceCount);


namespace MARAS {

    bool IsSkyrimNetAvailable()
    {
        if (State::WorldStore::GetSingleton().Snapshot()->settings->simulateNoSkyrimNet) {
            return false;
        }
        return GetModuleHandleW(L"SkyrimNet.dll") != nullptr;
    }

namespace {

    // Helper: look up the export from any already-loaded DLL whose base name
    // matches (case-insensitive, without ".dll").
    static PublicRegisterDecorator_t FindRegisterDecorator()
    {
        // SkyrimNet ships its DLL as "SkyrimNet.dll" inside SKSE/Plugins/.
        // GetModuleHandleW only searches modules already mapped into this process.
        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] SkyrimNet.dll not found in process - integration skipped.");
            return nullptr;
        }

        auto* fn = reinterpret_cast<PublicRegisterDecorator_t>(
            GetProcAddress(hMod, "PublicRegisterDecorator"));
        if (!fn) {
            MARAS_LOG_WARN("[SkyrimNet] PublicRegisterDecorator export not found in SkyrimNet.dll.");
            return nullptr;
        }
        return fn;
    }

    static PublicFormIDToUUID_t FindFormIDToUUID()
    {
        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) return nullptr;
        return reinterpret_cast<PublicFormIDToUUID_t>(GetProcAddress(hMod, "PublicFormIDToUUID"));
    }

    static PublicGetPluginConfigValue_t FindGetPluginConfigValue()
    {
        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) return nullptr;
        return reinterpret_cast<PublicGetPluginConfigValue_t>(GetProcAddress(hMod, "PublicGetPluginConfigValue"));
    }

    static uint64_t FormIDToUUID(RE::FormID formID)
    {
        if (formID == 0) return 0;
        uint64_t uuid = 0;
        if (auto fn = FindFormIDToUUID()) {
            if (auto* base = ActorSvc::GetActorBase(formID)) {
                uuid = fn(base->GetFormID());
            }
            if (uuid == 0) {
                uuid = fn(formID);
            }
        }
        return uuid;
    }

    static float GetNearbyActorsRadius()
    {
        const float settingRadius = State::WorldStore::GetSingleton().Snapshot()->settings->skyrimnetActionRadius;
        if (settingRadius > 0.0f) {
            return settingRadius;
        }
        auto fn = FindGetPluginConfigValue();
        if (fn) {
            try {
                std::string val = fn("maras", "nearbyActors.radius", "3000");
                return std::stof(val);
            } catch (...) {}
        }
        return 3000.0f;
    }


    // -----------------------------------------------------------------------
    // Individual decorator registrations
    // Each lambda receives an RE::Actor* and returns a std::string that
    // SkyrimNet injects into its prompt templates as {{ decoratorName(uuid) }}.
    // -----------------------------------------------------------------------

    static void RegisterDecorator(PublicRegisterDecorator_t fn,
        const char* name, const char* description,
        std::function<std::string(RE::Actor*)> callback)
    {
        std::string decName = name ? name : "";
        auto wrappedCallback = [decName, cb = std::move(callback)](RE::Actor* actor) -> std::string {
            const uint32_t actorFormId = actor ? actor->GetFormID() : 0u;
            MARAS_LOG_TRACE("[SkyrimNet] Invoking decorator '{}' for actor formId 0x{:08X}.", decName, actorFormId);
            try {
                std::string result = cb(actor);
                MARAS_LOG_TRACE("[SkyrimNet] Decorator '{}' returned for actor formId 0x{:08X}: '{}'.", decName, actorFormId, result);
                return result;
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNet] Exception in decorator '{}' for actor 0x{:08X}: {}", decName, actorFormId, e.what());
                return "";
            } catch (...) {
                MARAS_LOG_ERROR("[SkyrimNet] Unknown exception in decorator '{}' for actor 0x{:08X}", decName, actorFormId);
                return "";
            }
        };

        if (fn(name, description, std::move(wrappedCallback))) {
            MARAS_LOG_INFO("[SkyrimNet] Registered decorator '{}'", name);
        } else {
            MARAS_LOG_WARN("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
        }
    }

    // In-house relationship lookup — no external DLL dependency.
    // Returns the first NPC base related to actor matching the given filters:
    //   associationFormID — Skyrim.esm FormID of the association type (0 = skip this filter).
    //   exactRank         — exact relationship rank (INT32_MIN = skip; use association filter only).
    // Rank mapping: kLover(0) → +4, kArchnemesis(8) → -4.
    static RE::TESNPC* FindRelatedNpcBase(RE::Actor* actor,
        RE::FormID associationFormID,
        int32_t exactRank) noexcept
    {
        if (!actor) return nullptr;
        auto* npcBase = ActorSvc::GetActorBase(actor->GetFormID());
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





    static std::string CleanAnimalRaceName(std::string raceName) {
        if (raceName.empty()) return "animal";
        std::string s = raceName;

        auto endsWithCaseInsensitive = [](const std::string& str, const std::string& suffix) {
            if (str.length() < suffix.length()) return false;
            return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
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

        return StringUtils::ToLowerStd(s);
    }

    static std::string FormatNamesSummary(const std::vector<std::string>& names) {
        if (names.empty()) return "";
        if (names.size() == 1) return names[0];
        if (names.size() == 2) return names[0] + " and " + names[1];

        std::string result;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) result += ", ";
            if (i == names.size() - 1) result += "and ";
            result += names[i];
        }
        return result;
    }

    static nlohmann::json MakeNpcRef(
        RE::FormID formID,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial)
    {
        nlohmann::json j;
        j["uuid"] = FormIDToUUID(formID);
        j["name"] = ResolveActorNameSafe(formID, world, spatial);
        return j;
    }

    static nlohmann::json MakeNpcListRef(
        const std::vector<RE::FormID>& formIDs,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial)
    {
        nlohmann::json j;
        std::vector<uint64_t> uuids;
        std::vector<std::string> names;
        uuids.reserve(formIDs.size());
        names.reserve(formIDs.size());
        for (auto fid : formIDs) {
            uuids.push_back(FormIDToUUID(fid));
            names.push_back(ResolveActorNameSafe(fid, world, spatial));
        }
        j["uuids"] = std::move(uuids);
        j["names"] = FormatNamesSummary(names);
        return j;
    }

    static std::string BuildRelationshipProfileJSON(RE::Actor* actor) {
        if (!actor) return "{}";
        const auto actorFormID = actor->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto selfView = Projection::PerspectiveView::ForActor(
            worldSnap,
            spatialSnap,
            State::ActorId{ actorFormID },
            &GetEngineWerewolfStateProvider());
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

        auto statusToKey = [](State::RelationshipStatus status) -> std::string {
            switch (status) {
                case State::RelationshipStatus::Married:  return "married";
                case State::RelationshipStatus::Engaged:  return "engaged";
                case State::RelationshipStatus::Dating:   return "dating";
                case State::RelationshipStatus::Lover:    return "lover";
                case State::RelationshipStatus::Divorced: return "divorced";
                case State::RelationshipStatus::Jilted:   return "jilted";
                case State::RelationshipStatus::ExDating: return "exdating";
                case State::RelationshipStatus::ExLover:  return "exlover";
                default: return "";
            }
        };
        auto isCurrentStatus = [](State::RelationshipStatus status) {
            return status == State::RelationshipStatus::Married ||
                   status == State::RelationshipStatus::Engaged ||
                   status == State::RelationshipStatus::Dating ||
                   status == State::RelationshipStatus::Lover;
        };
        auto isMainTierStatus = [](State::RelationshipStatus status) {
            return status == State::RelationshipStatus::Dating ||
                   status == State::RelationshipStatus::Engaged ||
                   status == State::RelationshipStatus::Married;
        };
        auto isPastStatus = [](State::RelationshipStatus status) {
            return status == State::RelationshipStatus::Divorced ||
                   status == State::RelationshipStatus::Jilted ||
                   status == State::RelationshipStatus::ExDating ||
                   status == State::RelationshipStatus::ExLover;
        };
        auto affairStatusToKey = [](State::AffairStatus status) -> std::string {
            switch (status) {
                case State::AffairStatus::Ongoing:          return "ongoing";
                case State::AffairStatus::EndedBreakup:     return "ended_breakup";
                case State::AffairStatus::EndedVictimSplit: return "ended_victim_split";
                case State::AffairStatus::Allowed:          return "allowed";
                case State::AffairStatus::EndedConfronted:  return "ended_confronted";
                default: return "unknown";
            }
        };

        nlohmann::json j;
        j["schema_version"] = 2;
        j["actor"] = makeNpcRef(actorFormID);
        j["family"] = nlohmann::json::array();
        const auto selfPregOpt = selfView.GetSelfPregnancy(State::ActorId{ actorFormID });
        const bool isPregnant = selfPregOpt.has_value();
        j["is_pregnant"] = isPregnant;
        if (selfPregOpt.has_value()) {
            const auto& selfPreg = *selfPregOpt;
            RE::FormID motherPartnerID = selfPreg.motherPartner.raw();
            if (motherPartnerID != 0 &&
                (MatchesActor(motherPartnerID, actorFormID) ||
                 ResolveActorSexSafe(motherPartnerID, *worldSnap, *spatialSnap) == State::ActorSex::Female ||
                 worldSnap->relationships->GetStatus(State::ActorId{ actorFormID }, selfPreg.motherPartner) == State::RelationshipStatus::Divorced)) {
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
                if (!grp.perceivedFather.IsNull() && grp.perceivedFather.raw() != kUnknownFatherFormID) {
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
                if (!firstGrp.perceivedFather.IsNull() && firstGrp.perceivedFather.raw() != kUnknownFatherFormID) {
                    pregObj["bio_father"] = makeNpcRef(firstGrp.perceivedFather.raw());
                } else {
                    nlohmann::json bioRef;
                    bioRef["uuid"] = 0;
                    bioRef["name"] = firstGrp.fatherFallbackName.empty() ? "Unknown" : firstGrp.fatherFallbackName;
                    pregObj["bio_father"] = bioRef;
                }
                if (!firstGrp.fetusIds.empty()) {
                    const State::ChildId frontId = firstGrp.fetusIds.front();
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
            for (const auto& exp : selfView.GetExpectingPregnanciesFor(State::ActorId{ actorFormID })) {
                nlohmann::json expObj;
                expObj["mother"] = makeNpcRef(exp.mother.raw());
                expObj["child_count"] = static_cast<int>(exp.fetusCount > 0 ? exp.fetusCount : 1);
                switch (exp.role) {
                    case Projection::ExpectingParentRole::PartnerBioFather:
                        expObj["role"] = "partner_bio_father";
                        break;
                    case Projection::ExpectingParentRole::PresumedFatherUnaware:
                        expObj["role"] = "presumed_father_unaware";
                        break;
                    case Projection::ExpectingParentRole::PartnerKnowsTruth:
                        expObj["role"] = "partner_knows_truth";
                        if (!exp.perceivedBioFather.IsNull() && exp.perceivedBioFather.raw() != kUnknownFatherFormID) {
                            expObj["bio_father"] = makeNpcRef(exp.perceivedBioFather.raw());
                        } else {
                            nlohmann::json bioRef;
                            bioRef["uuid"] = 0;
                            bioRef["name"] = exp.perceivedBioFatherFallbackName.empty() ? "Unknown" : exp.perceivedBioFatherFallbackName;
                            expObj["bio_father"] = bioRef;
                        }
                        expObj["is_secret_for_bio_father"] = exp.isSecretForBioFather;
                        break;
                    case Projection::ExpectingParentRole::BioFatherKnows:
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

        const State::ActorId selfActorId{ actorFormID };
        const auto perceivedEdges = selfView.GetPerceivedRelationshipsOf(selfActorId);
        const auto knownAffairs = selfView.GetKnownAffairsInvolving(selfActorId);

        auto isActorCheatingWith = [&](RE::FormID partnerID) {
            return std::ranges::any_of(knownAffairs, [&](const Projection::PerceivedAffair& record) {
                return record.affairStatus == State::AffairStatus::Ongoing &&
                       record.cheater.raw() == actorFormID &&
                       record.paramour.raw() == partnerID;
            });
        };
        auto isSecretAffairPair = [&](RE::FormID partnerID, bool ongoingOnly) {
            return std::ranges::any_of(knownAffairs, [&](const Projection::PerceivedAffair& record) {
                if (record.affairStatus == State::AffairStatus::Allowed || !record.isSecret) return false;
                if (ongoingOnly && record.affairStatus != State::AffairStatus::Ongoing) return false;
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
            if (id == 0 || id == kPlayerFormID) return false;
            const auto edge = selfView.GetPerceivedPair(State::ActorId::Player(), State::ActorId{ id });
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
                if ((actorFormID == kPlayerFormID && playerPolygamyEnabled) ||
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

        const auto playerRelationship = selfView.GetPerceivedPair(selfActorId, State::ActorId::Player());
        if (actorFormID != kPlayerFormID && playerRelationship && isCurrentStatus(playerRelationship->status)) {
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
            j["pending_intent"] = std::string(Utils::PartnerIntentTypeToString(
                static_cast<PartnerIntentType>(static_cast<uint8_t>(pendingIntent.type))));
        }
        if (playerRelationship && isCurrentStatus(playerRelationship->status) && playerPolygamyEnabled) {
            j["player_relationship_status"] = statusToKey(playerRelationship->status);
            struct PlayerPartner {
                RE::FormID formID;
                State::RelationshipStatus status;
            };
            std::vector<PlayerPartner> playerPartners;
            for (const auto& pEdge : selfView.GetPerceivedRelationshipsOf(State::ActorId::Player())) {
                const RE::FormID partnerID = pEdge.Other(State::ActorId::Player()).raw();
                if (partnerID == actorFormID) continue;
                if (isCurrentStatus(pEdge.status)) {
                    playerPartners.push_back({ partnerID, pEdge.status });
                }
            }
            auto statusPriority = [](State::RelationshipStatus status) {
                switch (status) {
                    case State::RelationshipStatus::Married: return 0;
                    case State::RelationshipStatus::Engaged: return 1;
                    case State::RelationshipStatus::Dating:  return 2;
                    case State::RelationshipStatus::Lover:   return 3;
                    default: return 4;
                }
            };
            std::ranges::sort(playerPartners, [&](const PlayerPartner& left, const PlayerPartner& right) {
                return statusPriority(left.status) < statusPriority(right.status);
            });
            for (const auto& playerPartner : playerPartners) {
                nlohmann::json coPartner;
                coPartner["person"] = makeNpcRef(playerPartner.formID);
                coPartner["role"] = playerRelationship->status == State::RelationshipStatus::Married &&
                                            playerPartner.status == State::RelationshipStatus::Married
                                        ? "co_spouse"
                                        : "co_partner";
                coPartner["player_relationship_status"] = statusToKey(playerPartner.status);
                j["player_co_partners"].push_back(std::move(coPartner));
            }
        }

        auto buildChildObj = [&](const Projection::PerceivedChildLineage& child) -> std::optional<nlohmann::json> {
            if (child.stage == State::ChildStage::Unborn) return std::nullopt;
            const RE::FormID childID = child.childId.raw();

            nlohmann::json childObj;
            childObj["uuid"] = FormIDToUUID(childID);
            const std::string childName =
                (child.name.empty() || StringUtils::CaseInsensitiveEquals(child.name, "Newborn")) ? "child" : child.name;
            childObj["name"] = childName;
            childObj["is_adopted"] = child.isAdopted;
            childObj["is_biological"] = child.isBiologicalChild;
            childObj["age_days"] = std::max(0.0f, currentDay - child.birthTimestamp);

            std::string stageStr = "child";
            switch (child.stage) {
                case State::ChildStage::Unborn:   stageStr = "unborn"; break;
                case State::ChildStage::Infant:   stageStr = "infant"; break;
                case State::ChildStage::Child:    stageStr = "child"; break;
                case State::ChildStage::Training: stageStr = "training"; break;
                case State::ChildStage::Adult:    stageStr = "adult"; break;
            }
            childObj["stage"] = stageStr;

            const auto* childSample = (!IsSyntheticChildID(childID)) ? spatialSnap->Find(State::RefId{ childID }) : nullptr;
            bool hasRealActor = false;
            if (childSample) {
                hasRealActor = !childSample->IsDeleted();
            } else if (HasPhysicalActor(childID)) {
                auto* childAct = NPCIndex::GetSingleton().GetActorFromFormId(childID);
                hasRealActor = (childAct != nullptr && !childAct->IsDeleted());
            }
            const bool isEphemeral = !hasRealActor ||
                (child.stage == State::ChildStage::Unborn) ||
                (child.stage == State::ChildStage::Infant) ||
                (child.stage == State::ChildStage::Training);
            childObj["has_ref_id"] = hasRealActor;
            childObj["is_ephemeral"] = isEphemeral;
            if (!child.trainingLocation.empty()) {
                childObj["training_location"] = child.trainingLocation;
            }

            const State::ActorSex resolvedSex = ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
            int childSex = child.gender;
            if (resolvedSex == State::ActorSex::Female) {
                childSex = 1;
            } else if (resolvedSex == State::ActorSex::Male) {
                childSex = 0;
            }
            if (child.stage == State::ChildStage::Unborn || childSex == 2) {
                childObj["gender"] = "unknown";
                childObj["relation"] = (child.stage == State::ChildStage::Unborn) ? "unborn child" : "child";
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
                if (!sec.trueBioFather.IsNull() && sec.trueBioFather.raw() != kUnknownFatherFormID) {
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
                if (!child.perceivedBioFather.IsNull() && child.perceivedBioFather.raw() != kUnknownFatherFormID) {
                    childObj["bio_father"] = makeNpcRef(child.perceivedBioFather.raw());
                    childObj["biological_father"] = childObj["bio_father"];
                } else if (!child.perceivedBioFatherFallbackName.empty() && child.perceivedBioFatherFallbackName != "Unknown") {
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
                critterObj["uuid"] = FormIDToUUID(critterID);
                critterObj["name"] = ResolveActorNameSafe(critterID, *worldSnap, *spatialSnap);
                critterObj["type"] = CleanAnimalRaceName(ResolveActorRaceSafe(critterID, *worldSnap, *spatialSnap));
                childObj["critter"] = std::move(critterObj);
            }

            const RE::FormID petID = hhChild ? hhChild->petId.raw() : 0;
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["uuid"] = FormIDToUUID(petID);
                petObj["name"] = ResolveActorNameSafe(petID, *worldSnap, *spatialSnap);
                petObj["type"] = CleanAnimalRaceName(ResolveActorRaceSafe(petID, *worldSnap, *spatialSnap));
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
            ncObj["uuid"] = FormIDToUUID(childID);
            const std::string childName =
                (child.name.empty() || StringUtils::CaseInsensitiveEquals(child.name, "Newborn")) ? "child" : child.name;
            ncObj["name"] = childName;
            const State::ActorSex resolvedSex = ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
            int childSex = child.gender;
            if (resolvedSex == State::ActorSex::Female) {
                childSex = 1;
            } else if (resolvedSex == State::ActorSex::Male) {
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
            if (otherBioID != 0 && otherBioID != kUnknownFatherFormID) {
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

        if (const auto asChildOpt = selfView.GetPerceivedChild(State::ChildId{ actorFormID }); asChildOpt.has_value()) {
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
            if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID) {
                parentage["bio_father"] = makeNpcRef(effectiveChildBioFatherID);
                parentage["biological_father"] = parentage["bio_father"];
            } else if (!asChild.perceivedBioFatherFallbackName.empty() && asChild.perceivedBioFatherFallbackName != "Unknown") {
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
                case State::ChildStage::Unborn:   stageStr = "unborn"; break;
                case State::ChildStage::Infant:   stageStr = "infant"; break;
                case State::ChildStage::Child:    stageStr = "child"; break;
                case State::ChildStage::Training: stageStr = "training"; break;
                case State::ChildStage::Adult:    stageStr = "adult"; break;
            }
            parentage["stage"] = stageStr;
            if (!asChild.trainingLocation.empty()) {
                parentage["training_location"] = asChild.trainingLocation;
            }

            const State::ActorSex resolvedAsChildSex = ResolveActorSexSafe(actorFormID, *worldSnap, *spatialSnap);
            int asChildSex = asChild.gender;
            if (resolvedAsChildSex == State::ActorSex::Female) {
                asChildSex = 1;
            } else if (resolvedAsChildSex == State::ActorSex::Male) {
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
                critterObj["uuid"] = FormIDToUUID(critterID);
                critterObj["name"] = ResolveActorNameSafe(critterID, *worldSnap, *spatialSnap);
                critterObj["type"] = CleanAnimalRaceName(ResolveActorRaceSafe(critterID, *worldSnap, *spatialSnap));
                parentage["critter"] = std::move(critterObj);
            }

            const RE::FormID petID = hhChild ? hhChild->petId.raw() : 0;
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["uuid"] = FormIDToUUID(petID);
                petObj["name"] = ResolveActorNameSafe(petID, *worldSnap, *spatialSnap);
                petObj["type"] = CleanAnimalRaceName(ResolveActorRaceSafe(petID, *worldSnap, *spatialSnap));
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
                } else if (!sec.trueBioFatherFallbackName.empty() && sec.trueBioFatherFallbackName != "Unknown") {
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
            auto appendEventUUID = [&](uint64_t uuid) {
                if (uuid == 0) return;
                for (const auto& existing : recentEventsParticipants) {
                    if (existing.is_number_unsigned() && existing.get<uint64_t>() == uuid) return;
                }
                recentEventsParticipants.push_back(uuid);
            };
            appendEventUUID(FormIDToUUID(actorFormID));

            auto addInvolvedAdult = [&](RE::FormID adultID, const std::string& roleLabel) {
                if (adultID == 0 || adultID == kUnknownFatherFormID || adultID == actorFormID) return;
                for (RE::FormID seen : seenAdultIDs) {
                    if (MatchesActor(seen, adultID)) return;
                }
                seenAdultIDs.push_back(adultID);
                nlohmann::json ref = makeNpcRef(adultID);
                ref["role"] = roleLabel;
                const uint64_t adultUUID = ref.value("uuid", uint64_t{ 0 });
                if (adultUUID != 0) {
                    appendEventUUID(adultUUID);
                }
                involvedAdults.push_back(std::move(ref));
            };

            if (asChildMotherID != 0) {
                const bool isGuard = asChild.guardians.HasGuardian(asChild.bioMother);
                addInvolvedAdult(asChildMotherID, isGuard ? "Biological Mother & Guardian" : "Biological Mother");
            }
            if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID) {
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
            auto addBioGenAdult = [&](RE::FormID adultID, const std::string& roleKey, const std::string& roleLabel, bool isGuardian, std::optional<bool> knowsPaternity = std::nullopt) {
                if (adultID == 0 || adultID == kUnknownFatherFormID || adultID == actorFormID) return;
                for (RE::FormID seen : seenBioGenAdultIDs) {
                    if (MatchesActor(seen, adultID)) return;
                }
                seenBioGenAdultIDs.push_back(adultID);
                nlohmann::json ref = makeNpcRef(adultID);
                ref["role_key"] = roleKey;
                ref["role"] = roleLabel;
                ref["is_guardian"] = isGuardian;
                if (knowsPaternity.has_value()) {
                    ref["knows_paternity"] = *knowsPaternity;
                }
                const uint64_t adultUUID = ref.value("uuid", uint64_t{ 0 });
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
                if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID) {
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
                    (pSecOpt->isSecret || pSecOpt->discovered || pSecOpt->discoveredByPartner || pSecOpt->discoveredByBioFather);
                const bool partnerKnowsPaternity = !hasPaternitySecret ||
                    (pSecOpt.has_value() && (pSecOpt->discovered || pSecOpt->discoveredByPartner || !pSecOpt->isSecretForPartner));
                const bool bioFatherKnowsPaternity = !pSecOpt.has_value() ||
                    pSecOpt->discovered || pSecOpt->discoveredByBioFather || !pSecOpt->isSecretForBioFather;
                const bool isSingleMotherSecretFromBioFather = (!hasPresumedFather &&
                    effectiveChildBioFatherID != 0 &&
                    pSecOpt.has_value() &&
                    pSecOpt->isSecret &&
                    pSecOpt->isSecretForBioFather &&
                    !pSecOpt->discoveredByBioFather &&
                    !pSecOpt->discovered);

                if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.perceivedBioFather);
                    std::string bioFatherRole;
                    if (hasPaternitySecret) {
                        if (!partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Also Unaware)";
                        } else if (!partnerKnowsPaternity && bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Secretly Knows Truth; Presumed Father Unaware)";
                        } else if (partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Knows)";
                        } else {
                            bioFatherRole = isGuard
                                ? "True Biological Father & Household Guardian (Paternity Known to Both Fathers)"
                                : "True Biological Father (Paternity Known to Both Fathers)";
                        }
                    } else if (isSingleMotherSecretFromBioFather) {
                        bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity)";
                    } else {
                        bioFatherRole = isGuard ? "Biological Father & Household Guardian" : "Biological Father (Non-Custodial)";
                    }
                    addBioGenAdult(effectiveChildBioFatherID, "bio_father", bioFatherRole, isGuard, bioFatherKnowsPaternity);
                }
                if (hasPresumedFather) {
                    const bool isGuard = asChild.guardians.HasGuardian(asChild.presumedFather);
                    std::string presFatherRole;
                    if (!partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                        presFatherRole = isGuard
                            ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Also Unaware)"
                            : "Presumed Father (Believes Child Is Biologically His; Bio Father Also Unaware)";
                    } else if (!partnerKnowsPaternity && bioFatherKnowsPaternity) {
                        presFatherRole = isGuard
                            ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Secretly Knows)"
                            : "Presumed Father (Believes Child Is Biologically His; Bio Father Secretly Knows)";
                    } else if (partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                        presFatherRole = isGuard
                            ? "Presumed Father & Household Guardian (Knows Child Was Fathered by Bio Father; Bio Father Unaware)"
                            : "Former Presumed Father / Mother's Partner (Knows Child Was Fathered by Bio Father; Non-Custodial; Bio Father Unaware)";
                    } else {
                        presFatherRole = isGuard
                            ? "Presumed Father & Household Guardian / Step-Father Figure (Both Fathers Know True Paternity)"
                            : "Former Presumed Father / Mother's Partner (Both Fathers Know True Paternity; Non-Custodial)";
                    }
                    addBioGenAdult(presFatherID, "presumed_father", presFatherRole, isGuard, partnerKnowsPaternity);
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

            j["parents"] = std::move(parentage);
        }

        for (const auto& record : knownAffairs) {
            if (record.affairStatus == State::AffairStatus::Allowed) continue;
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
            infidelity["type"] = StringUtils::ToLowerStd(Utils::AffairTypeToString(
                static_cast<AffairType>(static_cast<uint8_t>(record.affairType))));
            infidelity["affair_stage"] = StringUtils::ToLowerStd(std::string(Utils::AffairStageToString(
                static_cast<AffairStage>(static_cast<uint8_t>(record.effectiveAffairStage)))));
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

    static std::string GetCandidateDisplayName(
        RE::FormID partnerFid,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial)
    {
        std::string name = ResolveActorNameSafe(partnerFid, world, spatial);
        if (name.empty() || name == "None") {
            if (partnerFid == kPlayerFormID) {
                name = "Player";
            } else {
                name = fmt::format("0x{:08X}", partnerFid);
            }
        }
        return name;
    }

    static std::string GetNearbyCandidatesByEligibility(
        RE::Actor* actor,
        std::function<bool(RE::FormID)> soloCheck,
        std::function<bool(RE::FormID, RE::FormID)> pairCheck,
        std::function<bool(RE::FormID, RE::FormID)> extraFilter = nullptr)
    {
        if (!actor) return "";
        const auto fid = actor->GetFormID();
        if (!soloCheck(fid)) return "";

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const float radius = GetNearbyActorsRadius();
        std::string result;
        spatialSnap->ForEachNearbyCandidate(State::RefId{ fid }, radius, [&](const State::LoadedActorSample& sample) {
            const auto targetFid = sample.refId.raw();
            if (pairCheck(fid, targetFid) && (!extraFilter || extraFilter(fid, targetFid))) {
                const std::string name = GetCandidateDisplayName(targetFid, *worldSnap, *spatialSnap);
                if (!name.empty()) {
                    const State::ActorSex sex = ResolveActorSexSafe(targetFid, *worldSnap, *spatialSnap);
                    const char* gender = (sex == State::ActorSex::Female) ? "Female" : "Male";
                    if (!result.empty()) result += ", ";
                    result += fmt::format("{} ({})", name, gender);
                }
            }
        });
        return result;
    }

    static std::string GetPartnersByStatus(
        RE::Actor* actor,
        RelationshipStatus status,
        std::function<bool(RE::FormID, RE::FormID)> pairCheck)
    {
        if (!actor) return "";
        const auto fid = actor->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto partners = worldSnap->relationships->GetPartnersWithStatus(
            State::ActorId{ fid },
            static_cast<State::RelationshipStatus>(static_cast<uint8_t>(status)));
        if (partners.empty()) return "";

        std::string result;
        for (const auto pActorId : partners) {
            const RE::FormID pFid = pActorId.raw();
            if (!pairCheck(fid, pFid)) continue;
            const std::string name = GetCandidateDisplayName(pFid, *worldSnap, *spatialSnap);
            if (!name.empty() && name != "None") {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
        return result;
    }

}  // anonymous namespace

void RegisterSkyrimNetDecorators()
{
    MARAS_LOG_INFO("[SkyrimNet] RegisterSkyrimNetDecorators called.");

    auto* fn = FindRegisterDecorator();
    if (!fn) return;

    RegisterDecorator(fn,
        "ttm_get_ex_partner",
        "Returns a sentence describing this actor's connection to a player household member via a prior in-game relationship. Covers both the NPC who left someone to join the player, and the NPC who was left behind.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const uint32_t actorFormId = actor->GetFormID();
            const auto worldSnap       = State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap     = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto* player         = RE::PlayerCharacter::GetSingleton();
            const std::string playerName = player ? player->GetName() : "the Dragonborn";

            // Helper: map MARAS status to a player-relationship phrase.
            auto getPlayerRel = [&worldSnap](RE::FormID fid) -> std::string {
                switch (worldSnap->relationships->GetStatus(State::ActorId{ fid }, State::ActorId::Player())) {
                    case State::RelationshipStatus::Engaged:  return "became engaged to";
                    case State::RelationshipStatus::Married:  return "married";
                    case State::RelationshipStatus::Jilted:   return "had a brief engagement with";
                    case State::RelationshipStatus::Divorced: return "married and later divorced";
                    case State::RelationshipStatus::ExDating: return "briefly dated";
                    case State::RelationshipStatus::ExLover:  return "were lovers with";
                    default: return "";
                }
            };

            // ---------------------------------------------------------------
            // Perspective 1: This actor is tracked by MARAS — they left a
            // prior in-game relationship to be with the player, and may have
            // subsequently resumed it as a secret or open affair.
            // ---------------------------------------------------------------
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
                    const std::string exName    = exPartnerBase->GetName();

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

            // ---------------------------------------------------------------
            // Perspective 2: This actor is NOT tracked by MARAS — check if
            // their own in-game ex-partner is now with the player, and may
            // have subsequently resumed involvement with this actor.
            // actor → RelationsFinder → ex-partner → is ex-partner in MARAS?
            // ---------------------------------------------------------------
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
                // Match the ex-partner's NPC base against live MARAS-tracked actors
                // via snapshot base FormID comparison first, then ActorSvc::GetActorBase fallback.
                const RE::FormID targetBaseId = exPartner2Base->GetFormID();
                const auto adjPairs = worldSnap->relationships->GetAdjacentPairs(State::ActorId::Player());
                RE::FormID matchedFormID = 0;
                auto findByStatus = [&](State::RelationshipStatus status) {
                    if (matchedFormID != 0) return;
                    for (const auto& key : adjPairs) {
                        const auto* edge = worldSnap->relationships->FindEdge(key);
                        if (!edge || edge->status != status) continue;
                        const RE::FormID fid = key.Other(State::ActorId::Player()).raw();
                        if (fid == 0) continue;
                        RE::FormID candidateBaseId = ResolveBaseFormIDSafe(fid, *spatialSnap);
                        if (candidateBaseId == 0) {
                            if (const auto* base = ActorSvc::GetActorBase(fid)) {
                                candidateBaseId = base->GetFormID();
                            }
                        }
                        if (candidateBaseId == targetBaseId) {
                            matchedFormID = fid;
                            break;
                        }
                    }
                };
                findByStatus(State::RelationshipStatus::Married);
                findByStatus(State::RelationshipStatus::Engaged);
                findByStatus(State::RelationshipStatus::Jilted);
                findByStatus(State::RelationshipStatus::Divorced);
                findByStatus(State::RelationshipStatus::ExDating);
                findByStatus(State::RelationshipStatus::ExLover);

                MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 partner='{}' matchedFormID=0x{:08X}.",
                    exPartner2Base->GetName(), matchedFormID);

                if (matchedFormID) {
                    const std::string playerRel2 = getPlayerRel(matchedFormID);
                    MARAS_LOG_TRACE("[SkyrimNet] ttm_get_ex_partner: P2 playerRel='{}'.", playerRel2);

                    if (!playerRel2.empty()) {
                        const std::string actorName   = actor->GetName();
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

    RegisterDecorator(fn,
        "ttm_get_propose_dating_candidates",
        "Returns a comma-separated list of names of all nearby eligible candidates for starting a dating relationship with this actor, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetNearbyCandidatesByEligibility(a,
                [&](auto fid) { return m.IsAvailableForDating(fid); },
                [&](auto fid, auto tid) { return m.IsAvailableForDating(fid, tid); });
        });

    RegisterDecorator(fn,
        "ttm_get_propose_lover_candidates",
        "Returns a comma-separated list of names of all nearby eligible candidates for becoming lovers with this actor, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetNearbyCandidatesByEligibility(a,
                [&](auto fid) { return m.IsAvailableForLover(fid); },
                [&](auto fid, auto tid) { return m.IsAvailableForLover(fid, tid); },
                [&](auto fid, auto tid) { return m.GetRelationshipStatus(fid, tid) != RelationshipStatus::Lover; });
        });

    auto getProposeEngagementCandidates = [](RE::Actor* actor) -> std::string {
        if (!actor) return "";
        const auto fid = actor->GetFormID();
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        if (!mgr.IsAvailableForEngaged(fid)) {
            return "";
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const float radius = GetNearbyActorsRadius();

        // If the actor is currently dating someone, that specific partner is the only valid target
        const auto datingPartners = worldSnap->relationships->GetPartnersWithStatus(
            State::ActorId{ fid }, State::RelationshipStatus::Dating);
        if (!datingPartners.empty()) {
            const auto datingPartnerFid = datingPartners[0].raw();
            std::string matchResult;
            spatialSnap->ForEachNearbyCandidate(State::RefId{ fid }, radius, [&](const State::LoadedActorSample& sample) {
                if (!matchResult.empty()) return;
                if (sample.refId.raw() == datingPartnerFid && mgr.IsAvailableForEngaged(fid, datingPartnerFid)) {
                    matchResult = GetCandidateDisplayName(datingPartnerFid, *worldSnap, *spatialSnap);
                }
            });
            return matchResult;
        }

        // If single, find all nearby eligible candidates
        std::string result;
        spatialSnap->ForEachNearbyCandidate(State::RefId{ fid }, radius, [&](const State::LoadedActorSample& sample) {
            const auto targetFid = sample.refId.raw();
            const auto targetDating = worldSnap->relationships->GetPartnersWithStatus(
                State::ActorId{ targetFid }, State::RelationshipStatus::Dating);
            if (!targetDating.empty() && targetDating[0].raw() != fid) {
                return;
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
    };

    RegisterDecorator(fn,
        "ttm_get_propose_engagement_candidates",
        "Returns the name of the nearby dating partner (if dating) or a comma-separated list of nearby eligible candidates (if single) for proposing engagement, or empty string if none.",
        getProposeEngagementCandidates);

    RegisterDecorator(fn,
        "ttm_get_breakup_dating_candidates",
        "Returns a comma-separated list of names of all partners currently dating this actor that can be broken up with, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetPartnersByStatus(a, RelationshipStatus::Dating,
                [&](auto fid, auto pid) { return m.IsAvailableForBreakupDating(fid, pid); });
        });

    RegisterDecorator(fn,
        "ttm_get_breakup_lover_candidates",
        "Returns a comma-separated list of names of all partners currently in a lover relationship with this actor that can be broken up with, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetPartnersByStatus(a, RelationshipStatus::Lover,
                [&](auto fid, auto pid) { return m.IsAvailableForBreakupLover(fid, pid); });
        });

    RegisterDecorator(fn,
        "ttm_get_breakup_engagement_candidates",
        "Returns a comma-separated list of names of all partners currently engaged to this actor that can be broken up with (called off), or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetPartnersByStatus(a, RelationshipStatus::Engaged,
                [&](auto fid, auto pid) { return m.IsAvailableForJilted(fid, pid); });
        });

    RegisterDecorator(fn,
        "ttm_get_divorce_candidates",
        "Returns a comma-separated list of names of all spouses currently married to this actor that can be divorced, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetPartnersByStatus(a, RelationshipStatus::Married,
                [&](auto fid, auto pid) { return m.IsAvailableForDivorced(fid, pid); });
        });

    RegisterDecorator(fn,
        "ttm_get_open_relationship_candidates",
        "Returns a comma-separated list of names of all nearby closed main-tier partners for this actor that can be opened, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetNearbyCandidatesByEligibility(a,
                [&](auto fid) { return m.IsAvailableForOpenRelationship(fid); },
                [&](auto fid, auto tid) { return m.IsAvailableForOpenRelationship(fid, tid); });
        });

    RegisterDecorator(fn,
        "ttm_get_close_relationship_candidates",
        "Returns a comma-separated list of names of all nearby open main-tier partners for this actor that can be closed, or empty string if none.",
        [](RE::Actor* a) {
            const auto& m = NPCRelationshipManager::GetSingleton();
            return GetNearbyCandidatesByEligibility(a,
                [&](auto fid) { return m.IsAvailableForCloseRelationship(fid); },
                [&](auto fid, auto tid) { return m.IsAvailableForCloseRelationship(fid, tid); });
        });

    RegisterDecorator(fn,
        "ttm_get_exclusive_main_partners",
        "Returns a comma-separated list of names of all exclusive (non-open) main-tier partners (dating, engaged, married) for this actor across all MARAS relationships that would be cheated on, or empty string if none.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            if (fid == kPlayerFormID && worldSnap->settings->enablePlayerPolygamy) {
                return "";
            }

            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const State::ActorId selfActorId{ fid };
            std::string result;
            auto appendExclusivePartners = [&](State::RelationshipStatus status) {
                for (const auto partnerActorId : worldSnap->relationships->GetPartnersWithStatus(selfActorId, status)) {
                    const auto* edge = worldSnap->relationships->FindEdge(State::CanonicalPairKey(selfActorId, partnerActorId));
                    if (edge && edge->isOpen) {
                        continue;
                    }
                    const std::string name = ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                    if (!name.empty() && name != "None") {
                        if (!result.empty()) result += ", ";
                        result += name;
                    }
                }
            };

            appendExclusivePartners(State::RelationshipStatus::Married);
            appendExclusivePartners(State::RelationshipStatus::Engaged);
            appendExclusivePartners(State::RelationshipStatus::Dating);

            return result;
        });

    RegisterDecorator(fn,
        "ttm_get_lovers_becoming_ex",
        "Returns a comma-separated list of names of all lover-status partners for this actor that will become ex-lovers upon entering dating or engagement, or empty string if none.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            if (fid == kPlayerFormID && worldSnap->settings->enablePlayerPolygamy) {
                return "";
            }

            const State::ActorId selfActorId{ fid };
            if (worldSnap->relationships->HasAnyOpenMainPair(selfActorId)) {
                return "";
            }

            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto lovers = worldSnap->relationships->GetPartnersWithStatus(selfActorId, State::RelationshipStatus::Lover);
            std::string result;
            for (const auto partnerActorId : lovers) {
                const std::string name = ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                if (!name.empty() && name != "None") {
                    if (!result.empty()) result += ", ";
                    result += name;
                }
            }
            return result;
        });

    RegisterDecorator(fn,
        "ttm_get_dating_partner",
        "Returns the name of the dating partner for this actor, or empty string if not currently in a dating relationship.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto dating = worldSnap->relationships->GetPartnersWithStatus(State::ActorId{ fid }, State::RelationshipStatus::Dating);
            if (!dating.empty()) {
                const std::string name = ResolveActorNameSafe(dating[0].raw(), *worldSnap, *spatialSnap);
                if (!name.empty() && name != "None") return name;
            }
            return "";
        });

    RegisterDecorator(fn,
        "ttm_get_open_main_partners",
        "Returns a comma-separated list of names of all open main-tier partners (dating, engaged, married) for this actor, or empty string if none.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const State::ActorId selfActorId{ fid };
            std::string result;
            auto appendOpenPartners = [&](State::RelationshipStatus status) {
                for (const auto partnerActorId : worldSnap->relationships->GetPartnersWithStatus(selfActorId, status)) {
                    const auto* edge = worldSnap->relationships->FindEdge(State::CanonicalPairKey(selfActorId, partnerActorId));
                    if (edge && edge->isOpen) {
                        const std::string name = ResolveActorNameSafe(partnerActorId.raw(), *worldSnap, *spatialSnap);
                        if (!name.empty() && name != "None") {
                            if (!result.empty()) result += ", ";
                            result += name;
                        }
                    }
                }
            };

            appendOpenPartners(State::RelationshipStatus::Married);
            appendOpenPartners(State::RelationshipStatus::Engaged);
            appendOpenPartners(State::RelationshipStatus::Dating);

            return result;
        });

    auto isActorRegisteredCallback = [](RE::Actor* actor) -> std::string {
        if (!actor) {
            return "false";
        }
        const auto fid = actor->GetFormID();
        if (fid == 0) {
            return "false";
        }

        if (fid == kPlayerFormID) {
            return "true";
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ActorId selfActorId{ fid };
        const State::RefId selfRefId{ fid };

        if (worldSnap->actors->IsRegisteredAdult(selfRefId) || worldSnap->actors->Find(selfRefId) != nullptr) {
            return "true";
        }

        const auto omniscientView = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        if (omniscientView.GetPerceivedChild(State::ChildId{ fid }).has_value()) {
            return "true";
        }
        if (omniscientView.GetSelfPregnancy(selfActorId).has_value()) {
            return "true";
        }

        for (const auto& key : worldSnap->relationships->GetAdjacentPairs(selfActorId)) {
            const auto* edge = worldSnap->relationships->FindEdge(key);
            if (edge &&
                edge->status != State::RelationshipStatus::Unknown &&
                edge->status != State::RelationshipStatus::Candidate) {
                return "true";
            }
        }

        if (const auto* sample = spatialSnap->Find(selfRefId); sample && sample->IsTrackedFaction()) {
            return "true";
        }

        return "false";
    };

    RegisterDecorator(fn,
        "ttm_is_registered",
        "Returns JSON boolean true if the actor is registered in MARAS (player, tracked NPC, registered child, or pregnant), or false otherwise.",
        isActorRegisteredCallback);

    RegisterDecorator(fn,
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

    auto makeNearbyActorsDecorator = []() {
        return [](RE::Actor* actor) -> std::string {
            nlohmann::json j;
            j["actorIds"] = nlohmann::json::array();
            if (!actor) {
                return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }

            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const float radius = GetNearbyActorsRadius();
            spatialSnap->ForEachNearbyCandidate(State::RefId{ actor->GetFormID() }, radius, [&](const State::LoadedActorSample& sample) {
                const uint64_t uuid = FormIDToUUID(sample.refId.raw());
                if (uuid != 0) {
                    j["actorIds"].push_back(uuid);
                }
            });

            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        };
    };

    RegisterDecorator(fn,
        "ttm_get_nearby_actors",
        "JSON object listing all nearby valid actors (including the player, within nearbyActors.radius). Fields: actorIds (array of SkyrimNet UUIDs).",
        makeNearbyActorsDecorator());

    auto collectNearbyIds = [](RE::FormID centerFid, const State::SpatialActorSnapshot& spatial, float radius) {
        std::vector<RE::FormID> ids;
        spatial.ForEachNearbyCandidate(State::RefId{ centerFid }, radius, [&](const State::LoadedActorSample& sample) {
            ids.push_back(sample.refId.raw());
        });
        return ids;
    };

    auto getConfessAffairCandidates = [collectNearbyIds](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ActorId selfActorId{ aFid };
        const auto selfView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, selfActorId, &GetEngineWerewolfStateProvider());
        const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
        if (records.empty()) return "";

        const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
        if (nearbyIds.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (const RE::FormID nearbyFid : nearbyIds) {
                if (Utils::MatchesActor(targetFid, nearbyFid)) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.cheater != selfActorId) continue;
            if (!rec.isSecret || rec.affairStatus != State::AffairStatus::Ongoing) continue;
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
    };

    auto getExposeAffairCandidates = [collectNearbyIds](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ActorId selfActorId{ aFid };
        const auto selfView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, selfActorId, &GetEngineWerewolfStateProvider());
        const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
        if (records.empty()) return "";

        const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
        if (nearbyIds.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (const RE::FormID nearbyFid : nearbyIds) {
                if (Utils::MatchesActor(targetFid, nearbyFid)) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.paramour != selfActorId) continue;
            if (!rec.isSecret || rec.affairStatus != State::AffairStatus::Ongoing) continue;
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
    };

    auto getResolveAffairCandidates = [collectNearbyIds](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ActorId selfActorId{ aFid };
        const auto selfView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, selfActorId, &GetEngineWerewolfStateProvider());
        const auto records = selfView.GetKnownAffairsInvolving(selfActorId);
        if (records.empty()) return "";

        const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
        if (nearbyIds.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (const RE::FormID nearbyFid : nearbyIds) {
                if (Utils::MatchesActor(targetFid, nearbyFid)) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.isSecret || rec.affairStatus != State::AffairStatus::Ongoing) continue;

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
    };

    auto getRevealChildSecretCandidates = [collectNearbyIds](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto selfView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ aFid }, &GetEngineWerewolfStateProvider());
        const auto visibleChildren = selfView.GetVisibleChildren();
        if (visibleChildren.empty()) return "";

        const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
        if (nearbyIds.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (const RE::FormID nearbyFid : nearbyIds) {
                if (Utils::MatchesActor(targetFid, nearbyFid)) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenUnbornFathers;
        for (const auto& entry : visibleChildren) {
            const RE::FormID motherID = entry.bioMother.raw();
            if (!MatchesActor(motherID, aFid)) continue;
            if (!entry.paternitySecret.has_value()) continue;
            const auto& sec = *entry.paternitySecret;
            if (sec.discovered || (!sec.isSecret && !sec.isPregnancySecretForPartner)) continue;

            const RE::FormID motherPartnerID = entry.motherPartner.raw();
            const RE::FormID bioFatherID = sec.trueBioFather.raw();
            if (motherPartnerID != 0 && bioFatherID != 0 && MatchesActor(bioFatherID, motherPartnerID)) continue;

            if (entry.stage == State::ChildStage::Unborn) {
                if (!seenUnbornFathers.insert({ motherPartnerID, bioFatherID }).second) {
                    continue;
                }
            }

            const bool pNear = (sec.isSecretForPartner || sec.isPregnancySecretForPartner) &&
                               motherPartnerID != 0 && isNear(motherPartnerID);
            const bool bNear = sec.isSecretForBioFather && bioFatherID != 0 && isNear(bioFatherID);

            std::string childDesc =
                (entry.name.empty() || StringUtils::CaseInsensitiveEquals(entry.name, "Newborn")) ? "child" : entry.name;
            std::string partnerRoleLabel = "presumed father of ";
            if (entry.stage == State::ChildStage::Unborn) {
                const std::string bioName = !sec.trueBioFatherFallbackName.empty()
                    ? sec.trueBioFatherFallbackName
                    : ResolveActorNameSafe(bioFatherID, *worldSnap, *spatialSnap);
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
    };

    auto getDiscoverChildPaternityCandidates = [collectNearbyIds](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto omniscientView = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto allChildren = omniscientView.GetVisibleChildren();
        if (allChildren.empty()) return "";

        const auto nearbyIds = collectNearbyIds(aFid, *spatialSnap, GetNearbyActorsRadius());
        if (nearbyIds.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (const RE::FormID nearbyFid : nearbyIds) {
                if (Utils::MatchesActor(targetFid, nearbyFid)) return true;
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
            const bool childNear = (cID != 0 && !IsSyntheticChildID(cID) && isNear(cID));

            // Father can discover/learn the truth if either the mother OR the child is nearby
            if (!motherNear && !childNear) continue;

            const RE::FormID motherPartnerID = entry.motherPartner.raw();
            const RE::FormID bioFatherID = sec.trueBioFather.raw();
            if (motherPartnerID != 0 && bioFatherID != 0 && MatchesActor(bioFatherID, motherPartnerID)) continue;

            bool isUnannouncedPlayerPregnancy = false;
            if (entry.stage == State::ChildStage::Unborn) {
                if (motherID != kPlayerFormID) {
                    // Never leak an unannounced / non-showing NPC pregnancy into an unaware partner's or biological father's action candidates.
                    if (MatchesActor(motherPartnerID, aFid) && sec.isPregnancySecretForPartner) {
                        continue;
                    }
                    if (MatchesActor(bioFatherID, aFid) && sec.isSecretForBioFather) {
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
            const std::string childDesc = (entry.stage == State::ChildStage::Unborn)
                ? "unborn pregnancy"
                : ((entry.name.empty() || StringUtils::CaseInsensitiveEquals(entry.name, "Newborn")) ? "child" : entry.name);
            const std::string unannouncedTag = isUnannouncedPlayerPregnancy
                ? "; UNANNOUNCED — you DO NOT know she is pregnant unless she explicitly reveals/confesses it in dialogue right now"
                : "";

            if (MatchesActor(motherPartnerID, aFid) && sec.isSecretForPartner) {
                if (!res.empty()) res += ", ";
                res += childDesc + " (child with " + motherName +
                       (entry.stage == State::ChildStage::Unborn ? "; partner / presumed father" : "; presumed father") +
                       unannouncedTag;
                if (entry.stage != State::ChildStage::Unborn && sec.hasPhenotypeMismatch) {
                    res += ", noticeable physical difference";
                }
                res += ")";
            } else if (MatchesActor(bioFatherID, aFid) && sec.isSecretForBioFather) {
                if (!res.empty()) res += ", ";
                res += childDesc + " (child with " + motherName + "; biological father" + unannouncedTag + ")";
            }
        }
        return res;
    };

    auto getAvailableRelationshipActions = [collectNearbyIds](RE::Actor* actor) -> std::string {
        if (!actor) return "";
        const uint32_t actorFormId = actor->GetFormID();

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto nearbyIds = collectNearbyIds(actorFormId, *spatialSnap, GetNearbyActorsRadius());

        std::vector<std::string> actions;

        // Dating: Actor must be eligible and at least one nearby actor must be eligible to date
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

        // Lover: Actor must be eligible and at least one actor nearby
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

        // Engagement: Actor must be eligible and valid target nearby
        if (mgr.IsAvailableForEngaged(actorFormId)) {
            bool hasEngagedTarget = false;
            const auto datingPartners = worldSnap->relationships->GetPartnersWithStatus(
                State::ActorId{ actorFormId }, State::RelationshipStatus::Dating);
            if (!datingPartners.empty()) {
                const auto datingPartnerFid = datingPartners[0].raw();
                for (const RE::FormID targetFid : nearbyIds) {
                    if (Utils::MatchesActor(targetFid, datingPartnerFid) &&
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
        for (size_t i = 0; i < actions.size(); ++i) {
            if (i > 0) result += ", ";
            result += actions[i];
        }

        return result;
    };

    RegisterDecorator(fn,
        "ttm_get_available_actions",
        "Returns a string with all available relationship actions for this actor listed with short explanations.",
        getAvailableRelationshipActions);

    RegisterDecorator(fn,
        "ttm_get_reveal_child_secret_candidates",
        "Returns a comma-separated list of candidate fathers (presumed, bio, or both) nearby for whom the mother has an active paternity secret to reveal, or empty string if none.",
        getRevealChildSecretCandidates);

    RegisterDecorator(fn,
        "ttm_get_discover_child_paternity_candidates",
        "Returns a comma-separated list of children (and mothers) nearby about whom this father has discovered / found out an active paternity secret, or empty string if none.",
        getDiscoverChildPaternityCandidates);

    RegisterDecorator(fn,
        "ttm_get_confess_affair_candidates",
        "Returns a comma-separated list of partners nearby to whom this actor can confess an active secret affair, or empty string if none.",
        getConfessAffairCandidates);

    RegisterDecorator(fn,
        "ttm_get_expose_affair_candidates",
        "Returns a comma-separated list of victims nearby to whom this affair partner can expose a secret affair, or empty string if none.",
        getExposeAffairCandidates);

    RegisterDecorator(fn,
        "ttm_get_resolve_affair_candidates",
        "Returns a comma-separated list of partners nearby with whom this actor has an exposed ongoing affair to resolve, or empty string if none.",
        getResolveAffairCandidates);

    MARAS_LOG_INFO("[SkyrimNet] Decorator registration complete.");

}

using PublicSendCustomPromptToLLM_t = bool (*)(const char* promptName, const char* variant, const char* contextJson, std::function<void(const char* response, int success)> callback);
using PublicSendCustomDecisionToLLM_t = bool (*)(const char* templateName, const char* contextJson, int maxProbabilities, std::function<void(const char* answer, const char* resultJson, int success)> callback);

namespace {
    std::string GetModelSlotDisplayName(std::string_view llmVariant) {
        if (llmVariant == "memory") {
            return "Memory";
        }
        if (llmVariant == "decisions") {
            return "Decisions";
        }
        return "GameMaster";
    }
}

std::string GetLLMFailureGuidanceTitle(LLMFailureType failureType) {
    switch (failureType) {
        case LLMFailureType::InvalidJson:
            return "What Does This Mean? (Broken JSON Response)";
        case LLMFailureType::RequestFailed:
        default:
            return "What Does This Mean? (HTTP / Connection Failure)";
    }
}

std::string GetLLMFailureGuidance(LLMFailureType failureType, std::string_view llmVariant) {
    const std::string slotName = GetModelSlotDisplayName(llmVariant);
    if (failureType == LLMFailureType::InvalidJson) {
        return fmt::format(
            "Your AI model replied, but its response was formatted incorrectly (broken JSON) and could not be read.\n\n"
            "• Why this happens: M.A.R.A.S requires the AI to reply in a strict structured data format. Even good models occasionally make a formatting mistake, and clicking Retry usually fixes it.\n"
            "• Seeing this error too often? If this happens frequently, the AI model assigned to your '{}' slot in SkyrimNet is incapable of returning valid JSON reliably most of the time. Consider switching that slot in SkyrimNet to a more capable model.",
            slotName);
    }

    return fmt::format(
        "SkyrimNet could not get a response from your AI provider for the '{}' model slot (HTTP / network error or empty reply).\n\n"
        "• Cloud AI (OpenRouter, OpenAI, etc.): The service may have timed out, hit a rate limit, run out of API credits, or your API key / model name in SkyrimNet may be invalid.\n"
        "• Local AI (Ollama, KoboldCPP, LM Studio): Your local AI server may be offline, still loading a model, or overloaded.\n"
        "• What to do: Click Retry to send the request again. If it keeps failing, check your internet connection or SkyrimNet provider settings.",
        slotName);
}

std::string GetLLMBatchFailureGuidance(bool hasRequestFailure, bool hasInvalidJson, std::string_view llmVariant) {
    if (hasRequestFailure && hasInvalidJson) {
        const std::string slotName = GetModelSlotDisplayName(llmVariant);
        return fmt::format(
            "Some AI requests failed to connect, while others returned unreadable data:\n\n"
            "• HTTP / Connection Failed: SkyrimNet could not reach your AI provider or received an empty reply (timeout, rate limit, out of API credits, invalid API key, or local AI server offline).\n"
            "• Broken JSON Response: The AI model in your SkyrimNet '{}' slot replied with broken formatting. If you see broken JSON errors too often, your '{}' model is incapable of returning valid JSON reliably and should be changed to a stronger model.",
            slotName, slotName);
    }
    if (hasInvalidJson) {
        return GetLLMFailureGuidance(LLMFailureType::InvalidJson, llmVariant);
    }
    return GetLLMFailureGuidance(LLMFailureType::RequestFailed, llmVariant);
}

void ShowLLMRetryModal(const std::string& evaluationType,
                       const std::string& description,
                       LLMFailureType failureType,
                       std::function<void()> onRetry,
                       std::function<void()> onIgnore,
                       std::string_view llmVariant)
{
    if (!SKSE::GetTaskInterface()) {
        MARAS_LOG_ERROR("ShowLLMRetryModal: no task interface, falling back to ignore");
        if (onIgnore) onIgnore();
        return;
    }

    ModalRequest req;
    req.title = "LLM Evaluation Failed";
    req.subtitle = evaluationType;
    req.message = "Choose Retry to send the request again, or Ignore to skip this evaluation.";
    req.tone = ModalTone::Warning;
    req.size = ModalSize::Md;
    req.defaultCancelButtonIndex = 1;

    if (!description.empty()) {
        req.controls.push_back(ModalControl::MakeInfoBox("error_details", "Failure Details", description, ModalTone::Warning));
    }
    req.controls.push_back(ModalControl::MakeInfoBox(
        "error_guidance",
        GetLLMFailureGuidanceTitle(failureType),
        GetLLMFailureGuidance(failureType, llmVariant),
        ModalTone::Default));

    req.buttons.push_back(ModalButton::Make("retry", "Retry Request", ModalButtonVariant::Primary));
    req.buttons.push_back(ModalButton::Make("ignore", "Ignore", ModalButtonVariant::Secondary));

    const SessionEpoch expectedEpoch = GetSessionEpoch();
    PrismaModalManager::GetSingleton().ShowModal(
        std::move(req),
        [expectedEpoch, onRetry = std::move(onRetry), onIgnore = std::move(onIgnore)](const ModalResponse& resp) mutable {
            if (!IsSessionEpochCurrent(expectedEpoch) || resp.aborted) {
                return;
            }
            if (resp.buttonIndex == 0 || resp.buttonId == "retry") {
                if (onRetry) onRetry();
            } else {
                if (onIgnore) onIgnore();
            }
        });
}

bool SendCustomPromptToLLM(const std::string& promptName, const std::string& variant, const std::string& contextJson, std::function<void(const char* response, int success)> callback) {
    if (!IsSkyrimNetAvailable()) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet is not available or simulated as disabled");
        return false;
    }

    HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
    if (!hMod) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet.dll not found in process");
        return false;
    }

    auto fnSendLLM = reinterpret_cast<PublicSendCustomPromptToLLM_t>(GetProcAddress(hMod, "PublicSendCustomPromptToLLM"));
    if (!fnSendLLM) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: PublicSendCustomPromptToLLM export not found in SkyrimNet.dll");
        return false;
    }

    return fnSendLLM(promptName.c_str(), variant.c_str(), contextJson.c_str(), std::move(callback));
}

bool SendCustomDecisionToLLM(const std::string& templateName, const std::string& contextJson, int maxProbabilities,
                             std::function<void(const char* answer, const char* resultJson, int success)> callback) {
    if (!IsSkyrimNetAvailable()) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet is not available or simulated as disabled");
        return false;
    }

    HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
    if (!hMod) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet.dll not found in process");
        return false;
    }

    auto fnSendDecision = reinterpret_cast<PublicSendCustomDecisionToLLM_t>(GetProcAddress(hMod, "PublicSendCustomDecisionToLLM"));
    if (!fnSendDecision) {
        MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: PublicSendCustomDecisionToLLM export not found in SkyrimNet.dll");
        return false;
    }

    return fnSendDecision(templateName.c_str(), contextJson.c_str(), maxProbabilities, std::move(callback));
}

using PublicAddMemory_t = int (*)(
    uint32_t formId, const char* contentText, float importance,
    const char* memoryType, const char* emotion, const char* location,
    const char* tagsJSON, const char* relatedActorsJSON);

bool AddActorMemory(RE::FormID actorFormID, const std::string& content, float importance,
                    const std::string& memoryType, const std::string& emotion, const std::string& location,
                    const std::vector<std::string>& tags, const std::vector<RE::FormID>& relatedActorFormIDs) {
    if (!IsSkyrimNetAvailable()) return false;
    if (actorFormID == 0 || content.empty()) return false;
    HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
    if (!hMod) return false;

    auto fnAddMemory = reinterpret_cast<PublicAddMemory_t>(GetProcAddress(hMod, "PublicAddMemory"));
    if (!fnAddMemory) {
        MARAS_LOG_WARN("[SkyrimNet] AddActorMemory: PublicAddMemory export not found in SkyrimNet.dll");
        return false;
    }

    nlohmann::json tagsJson = tags.empty() ? nlohmann::json::array() : nlohmann::json(tags);
    std::string tagsStr = tagsJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    nlohmann::json relatedJson = nlohmann::json::array();
    for (RE::FormID relID : relatedActorFormIDs) {
        if (relID != 0 && relID != actorFormID && !MARAS::IsSyntheticChildID(relID)) {
            relatedJson.push_back(relID);
        }
    }
    std::string relatedStr = relatedJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    int memId = fnAddMemory(
        actorFormID,
        content.c_str(),
        importance,
        memoryType.empty() ? "RELATIONSHIP" : memoryType.c_str(),
        emotion.empty() ? "content" : emotion.c_str(),
        location.empty() ? "Skyrim" : location.c_str(),
        tagsStr.c_str(),
        relatedStr.c_str()
    );

    if (memId > 0) {
        MARAS_LOG_INFO("[SkyrimNet] Successfully added memory ID {} for actor 0x{:08X}", memId, actorFormID);
        return true;
    } else {
        MARAS_LOG_WARN("[SkyrimNet] Failed to add memory for actor 0x{:08X}", actorFormID);
        return false;
    }
}

    namespace {

        struct PaternityEvalState {
            SessionEpoch epoch{ 0 };
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
            if (!state || !IsSessionEpochCurrent(state->epoch)) {
                return false;
            }

            return SkyrimNetDecisions::EvaluatePaternity(
                state->contextJson,
                state->isSecretForPresumedFather,
                [state](const SkyrimNetDecisions::PaternityEvalResult& res) {
                    if (!IsSessionEpochCurrent(state->epoch)) {
                        return;
                    }
                    AddEpochTask(state->epoch, "EvaluatePaternitySecret::OnResult", [state, res]() {
                        std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                        auto& mgr = NPCRelationshipManager::GetSingleton();
                        RE::FormID childID = state->childID;
                        if (!mgr.GetChildEntry(childID).has_value()) {
                            const RE::FormID promotedID = mgr.FindChildPaternitySecret(state->motherID, state->bioFatherID, state->childName);
                            if (promotedID != 0 && mgr.GetChildEntry(promotedID).has_value()) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: child {:08X} resolved to promoted child {:08X}.",
                                               state->childID, promotedID);
                                childID = promotedID;
                            }
                        }
                        if (childID == 0 || !mgr.GetChildEntry(childID).has_value()) {
                            MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: child {:08X} ('{}') no longer present in ChildRegistry; ignoring async result.",
                                           state->childID, state->childName);
                            PrismaUIDataBridge::GetSingleton().SendOverviewData();
                            return;
                        }
                        if (state->motherID != 0) {
                            const bool motherDeadInEngine = [motherID = state->motherID]() {
                                if (motherID == kPlayerFormID) {
                                    auto* player = RE::PlayerCharacter::GetSingleton();
                                    return player && player->IsDead();
                                }
                                auto* actor = RE::TESForm::LookupByID<RE::Actor>(motherID);
                                return actor && (actor->IsDeleted() || actor->IsDead());
                            }();
                            if (DeceasedRegistry::GetSingleton().IsDeceased(state->motherID) || motherDeadInEngine) {
                                MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: mother {:08X} ('{}') is deceased/dead; ignoring async result for child {:08X}.",
                                               state->motherID, state->motherName, childID);
                                PrismaUIDataBridge::GetSingleton().SendOverviewData();
                                return;
                            }
                        }

                        if (!res.success) {
                            MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: evaluation failed for child {:08X} (usedDecisions={}, detail='{}').",
                                           childID, res.usedDecisions, res.failureDetail);

                            auto retryAction = [state]() {
                                std::lock_guard retryLock(PapyrusInterface::GetPapyrusMutex());
                                if (!IsSessionEpochCurrent(state->epoch)) {
                                    return;
                                }
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: retrying evaluation for child {:08X}.", state->childID);
                                if (!DispatchPaternityEval(state)) {
                                    PrismaUIDataBridge::GetSingleton().SendOverviewData();
                                    PrismaUIDataBridge::GetSingleton().SendNPCDetail(state->childID);
                                }
                            };

                            auto ignoreAction = [childID, expectedEpoch = state->epoch]() {
                                std::lock_guard ignoreLock(PapyrusInterface::GetPapyrusMutex());
                                if (!IsSessionEpochCurrent(expectedEpoch)) {
                                    return;
                                }
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: player chose to ignore failed evaluation for child {:08X}.", childID);
                                PrismaUIDataBridge::GetSingleton().SendOverviewData();
                                PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
                            };

                            std::string fullDescription = fmt::format(
                                "Child: {} (Mother: {})\n{}",
                                state->childName, state->motherName, res.failureDetail);

                            ShowLLMRetryModal(
                                "Paternity Secret Evaluation",
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

                        bool wasPregSecretPartner = mgr.IsPregnancySecretForPartner(childID);
                        bool anySecretResolved = false;

                        auto tryBreakupIfActive = [&]() {
                            const RE::FormID motherID = state->motherID;
                            const RE::FormID presumedFatherID = state->presumedFatherID;
                            if (motherID == 0 || presumedFatherID == 0) {
                                return;
                            }
                            auto& deceased = DeceasedRegistry::GetSingleton();
                            if (deceased.IsDeceased(motherID) || deceased.IsDeceased(presumedFatherID)) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: skipping breakup for {:08X} & {:08X} (deceased in registry).",
                                               motherID, presumedFatherID);
                                return;
                            }
                            auto isDeadInEngine = [](RE::FormID id) -> bool {
                                if (id == 0x14) {
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
                            const auto curStatus = mgr.GetRelationshipStatus(motherID, presumedFatherID);
                            if (curStatus != RelationshipStatus::Dating &&
                                curStatus != RelationshipStatus::Lover &&
                                curStatus != RelationshipStatus::Engaged &&
                                curStatus != RelationshipStatus::Married) {
                                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: skipping breakup for {:08X} & {:08X} (status {} no longer active).",
                                               motherID, presumedFatherID, static_cast<int>(curStatus));
                                return;
                            }
                            mgr.Breakup(motherID, presumedFatherID);
                            if (motherID == 0x14 || presumedFatherID == 0x14) {
                                PrismaUIDataBridge::GetSingleton().SendHouseholdBuffs();
                            }
                        };

                        // 1. Resolve state based on the specific canonical outcome
                        if (outcome == "mother_confessed_presumed" || outcome == "presumed_father_learnt") {
                            anySecretResolved |= mgr.ResolvePaternitySecretForPartner(childID, outcome == "presumed_father_learnt", /*sendEvent=*/false);
                            if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                tryBreakupIfActive();
                            }
                        } else if (outcome == "mother_confessed_bio" || outcome == "bio_father_learnt") {
                            anySecretResolved |= mgr.ResolvePaternitySecretForBioFather(childID, outcome == "bio_father_learnt", /*sendEvent=*/false);
                        } else if (outcome == "mother_confessed_both") {
                            anySecretResolved |= mgr.ResolvePaternitySecretForPartner(childID, false, /*sendEvent=*/false);
                            anySecretResolved |= mgr.ResolvePaternitySecretForBioFather(childID, false, /*sendEvent=*/false);
                            if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                tryBreakupIfActive();
                            }
                        }

                        // 2. Dispatch SkyrimNet mod event for the exact outcome if one occurred
                        if (anySecretResolved && outcome != "none" && outcome != "remains_secret" && !outcome.empty()) {
                            std::string eventOutcome = (wasPregSecretPartner && (outcome == "mother_confessed_presumed" || outcome == "presumed_father_learnt" || outcome == "mother_confessed_both"))
                                                           ? (outcome + "_unannounced")
                                                           : outcome;
                            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                            EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
                            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, outcome, partnerReaction, wasPregSecretPartner);
                        }

                        // 3. Notify Prisma UI
                        PrismaUIDataBridge::GetSingleton().SendOverviewData();
                        PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
                    });
                });
        }

    }  // namespace

    bool EvaluatePaternitySecret(RE::FormID childID) {
        if (!IsSkyrimNetAvailable()) return false;

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto omniscientView = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto entryOpt = omniscientView.GetPerceivedChild(State::ChildId{ childID });
        if (!entryOpt.has_value()) return false;

        const auto& entry = *entryOpt;
        const RE::FormID motherID = entry.bioMother.raw();
        const RE::FormID presumedFatherID = entry.motherPartner.raw();
        const RE::FormID bioFatherID = entry.perceivedBioFather.raw();

        if (motherID == 0 || presumedFatherID == 0 || bioFatherID == 0) return false;
        if (MatchesActor(presumedFatherID, bioFatherID)) return false;

        const float currentDay = (spatialSnap->CurrentGameDay() >= 0.0f)
            ? spatialSnap->CurrentGameDay()
            : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);
        const float childAgeDays = currentDay - entry.birthTimestamp;
        const bool isAtBirth = (childAgeDays < 0.5f);

        const auto* secPtr = entry.paternitySecret ? &(*entry.paternitySecret) : nullptr;
        const bool hasVisualDiscrepancy = secPtr ? secPtr->hasPhenotypeMismatch : false;
        const std::string visualDesc = hasVisualDiscrepancy
            ? NPCRelationshipManager::GetSingleton().GetVisualDiscrepancyDescription(motherID, presumedFatherID, bioFatherID)
            : "";

        const uint64_t motherUUID = GetActorUUID(motherID);
        const uint64_t presumedFatherUUID = GetActorUUID(presumedFatherID);
        const uint64_t bioFatherUUID = GetActorUUID(bioFatherID);

        nlohmann::json ctx;
        ctx["childName"] = (entry.name.empty() || StringUtils::CaseInsensitiveEquals(entry.name, "Newborn")) ? "child" : entry.name;
        const State::ActorSex resolvedChildSex = ResolveActorSexSafe(childID, *worldSnap, *spatialSnap);
        int childGenderVal = entry.gender;
        if (resolvedChildSex == State::ActorSex::Female) {
            childGenderVal = 1;
        } else if (resolvedChildSex == State::ActorSex::Male) {
            childGenderVal = 0;
        }
        const std::string childGenderStr = (childGenderVal == 2) ? "unknown" : ((childGenderVal == 1) ? "female" : "male");
        const std::string childRelationStr = (childGenderVal == 2) ? "child" : ((childGenderVal == 1) ? "daughter" : "son");
        ctx["gender"] = childGenderStr;
        ctx["childGender"] = childGenderStr;
        ctx["childRelation"] = childRelationStr;
        ctx["childAgeDays"] = static_cast<int>(std::round(std::max(0.0f, childAgeDays)));
        ctx["isAtBirth"] = isAtBirth;

        ctx["motherName"] = ResolveActorNameSafe(motherID, *worldSnap, *spatialSnap);
        ctx["motherRace"] = ResolveActorRaceSafe(motherID, *worldSnap, *spatialSnap);
        ctx["motherUUID"] = motherUUID;

        ctx["presumedFatherName"] = ResolveActorNameSafe(presumedFatherID, *worldSnap, *spatialSnap);
        ctx["presumedFatherRace"] = ResolveActorRaceSafe(presumedFatherID, *worldSnap, *spatialSnap);
        ctx["presumedFatherUUID"] = presumedFatherUUID;
        const auto relStatus = worldSnap->relationships->GetStatus(
            State::ActorId{ motherID }, State::ActorId{ presumedFatherID });
        ctx["maritalStatus"] = std::string(Utils::RelationshipStatusToString(
            static_cast<RelationshipStatus>(static_cast<uint8_t>(relStatus))));

        ctx["bioFatherName"] = ResolveActorNameSafe(bioFatherID, *worldSnap, *spatialSnap);
        ctx["bioFatherRace"] = ResolveActorRaceSafe(bioFatherID, *worldSnap, *spatialSnap);
        ctx["bioFatherUUID"] = bioFatherUUID;

        State::AffairStatus resolvedAffairStatus = State::AffairStatus::Ongoing;
        for (const auto& rec : omniscientView.GetKnownAffairsInvolving(State::ActorId{ motherID })) {
            if (rec.cheater.raw() == motherID &&
                rec.deceivedPartner.raw() == presumedFatherID &&
                rec.paramour.raw() == bioFatherID) {
                resolvedAffairStatus = rec.affairStatus;
                break;
            }
        }
        ctx["affairStatus"] = std::string(Utils::AffairStatusToString(
            static_cast<AffairStatus>(static_cast<uint8_t>(resolvedAffairStatus))));

        const bool secretForPartner = secPtr ? secPtr->isSecretForPartner : false;
        const bool secretForBioFather = secPtr ? secPtr->isSecretForBioFather : false;
        ctx["isSecretForPresumedFather"] = secretForPartner;
        ctx["isSecretForPartner"] = secretForPartner;
        ctx["isSecretForBioFather"] = secretForBioFather;

        ctx["hasVisualDiscrepancy"] = hasVisualDiscrepancy;
        ctx["visualDiscrepancyDescription"] = visualDesc;

        nlohmann::json recentEventsParticipants = nlohmann::json::array();
        if (motherUUID != 0) recentEventsParticipants.push_back(motherUUID);
        if (presumedFatherUUID != 0 && presumedFatherUUID != motherUUID) recentEventsParticipants.push_back(presumedFatherUUID);
        if (bioFatherUUID != 0 && bioFatherUUID != motherUUID && bioFatherUUID != presumedFatherUUID) recentEventsParticipants.push_back(bioFatherUUID);
        ctx["recent_events_participants"] = std::move(recentEventsParticipants);

        auto state = std::make_shared<PaternityEvalState>();
        state->epoch = GetSessionEpoch();
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

    uint64_t GetActorUUID(RE::FormID formID) {
        return FormIDToUUID(formID);
    }

    bool ShouldTriggerSkyrimNetNarration(RE::Actor* speaker, RE::Actor* target) {
        if (!IsSkyrimNetAvailable() || !speaker) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        float radius = State::WorldStore::GetSingleton().Snapshot()->settings->skyrimnetActionRadius;
        if (radius <= 0.0f) {
            radius = 2000.0f;
        }
        const float radiusSq = radius * radius;
        const auto playerPos = player->GetPosition();
        auto* playerCell = player->GetParentCell();
        if (!playerCell) {
            return false;
        }

        auto isActorNearPlayer = [&](RE::Actor* actor) -> bool {
            if (!actor) return false;
            if (actor == player) return true;
            if (!actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled() || actor->IsDeleted()) {
                return false;
            }
            auto* actorCell = actor->GetParentCell();
            if (!actorCell) return false;
            if (playerCell->IsInteriorCell() || actorCell->IsInteriorCell()) {
                if (playerCell != actorCell) return false;
            } else {
                if (player->GetWorldspace() != actor->GetWorldspace()) return false;
            }
            if (playerPos.GetSquaredDistance(actor->GetPosition()) > radiusSq) {
                return false;
            }
            return true;
        };

        if (!isActorNearPlayer(speaker)) {
            return false;
        }

        if (target && target != speaker) {
            if (!isActorNearPlayer(target)) {
                return false;
            }
        }

        // If only the Player was passed as speaker/target, require at least one nearby NPC to be present
        if (speaker == player && (!target || target == player)) {
            bool hasNearbyNpc = false;
            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            spatialSnap->ForEachNearbyCandidate(State::RefId{ kPlayerFormID }, radius, [&](const State::LoadedActorSample&) {
                hasNearbyNpc = true;
            });
            if (!hasNearbyNpc) {
                return false;
            }
        }

        return true;
    }

    bool RegisterSkyrimNetEvent(const std::string& eventType,
                                const std::string& content,
                                RE::FormID originatorFormID,
                                RE::FormID targetFormID,
                                bool exactAudience,
                                const std::vector<RE::FormID>& audienceFormIDs) {
        if (!IsSkyrimNetAvailable()) {
            return false;
        }
        if (eventType.empty() || content.empty()) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: empty eventType or content");
            return false;
        }

        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: SkyrimNet.dll not found in process");
            return false;
        }

        auto fnRegisterEvent = reinterpret_cast<PublicRegisterEvent_t>(GetProcAddress(hMod, "PublicRegisterEvent"));
        if (!fnRegisterEvent) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: PublicRegisterEvent export not found in SkyrimNet.dll");
            return false;
        }

        if (IsSyntheticChildID(originatorFormID)) {
            originatorFormID = 0;
        }
        if (IsSyntheticChildID(targetFormID)) {
            targetFormID = 0;
        }

        std::string payloadToSend = content;
        if (eventType == "ttm_event") {
            nlohmann::json parsed = nlohmann::json::parse(content, nullptr, /*allow_exceptions=*/false);
            if (parsed.is_object() && parsed.contains("_marasMemory")) {
                const nlohmann::json memMeta = parsed["_marasMemory"];
                std::string ttmType;
                if (auto itType = parsed.find("type"); itType != parsed.end() && itType->is_string()) {
                    ttmType = itType->get<std::string>();
                }
                parsed.erase("_marasMemory");
                payloadToSend = parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

                if (memMeta.is_object()) {
                    auto readFormId = [&](std::string_view key) -> RE::FormID {
                        auto it = memMeta.find(key);
                        if (it != memMeta.end() && (it->is_number_unsigned() || it->is_number_integer())) {
                            return static_cast<RE::FormID>(it->get<uint64_t>());
                        }
                        return 0;
                    };

                    if (ttmType == "ttm_pregnancy_announced") {
                        const RE::FormID motherID = readFormId("motherId");
                        const RE::FormID partnerID = readFormId("partnerId");
                        const RE::FormID fallbackBioID = readFormId("fallbackBioId");
                        std::string fallbackBioName;
                        if (auto itName = memMeta.find("fallbackBioName"); itName != memMeta.end() && itName->is_string()) {
                            fallbackBioName = itName->get<std::string>();
                        }
                        int32_t childCount = 1;
                        if (auto itCnt = memMeta.find("childCount"); itCnt != memMeta.end() && (itCnt->is_number_integer() || itCnt->is_number_unsigned())) {
                            childCount = itCnt->get<int32_t>();
                        }
                        if (motherID != 0) {
                            std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                            SkyrimNetMemoryService::GetSingleton().RecordPregnancyDiscovered(
                                motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
                        }
                    } else if (ttmType == "ttm_child_birth") {
                        const RE::FormID motherID = readFormId("motherId");
                        std::vector<RE::FormID> childIDs;
                        if (auto itIds = memMeta.find("childIds"); itIds != memMeta.end() && itIds->is_array()) {
                            childIDs.reserve(itIds->size());
                            for (const auto& elem : *itIds) {
                                if (elem.is_number_unsigned() || elem.is_number_integer()) {
                                    childIDs.push_back(static_cast<RE::FormID>(elem.get<uint64_t>()));
                                }
                            }
                        }
                        if (motherID != 0) {
                            std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                            SkyrimNetMemoryService::GetSingleton().RecordChildBirth(motherID, childIDs);
                        }
                    }
                }
            }
        }

        std::vector<uint32_t> exactAudienceIds;
        auto addUniqueValidActor = [&](RE::FormID id) {
            if (id == 0 || IsSyntheticChildID(id)) return;
            const uint32_t uId = static_cast<uint32_t>(id);
            if (std::find(exactAudienceIds.begin(), exactAudienceIds.end(), uId) == exactAudienceIds.end()) {
                exactAudienceIds.push_back(uId);
            }
        };

        for (RE::FormID id : audienceFormIDs) {
            addUniqueValidActor(id);
        }

        if (exactAudience || !exactAudienceIds.empty()) {
            addUniqueValidActor(originatorFormID);
            addUniqueValidActor(targetFormID);

            if (exactAudienceIds.empty()) {
                MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent('{}'): exactAudience requested but no valid actors provided; skipping to avoid nearby broadcast", eventType);
                return false;
            }
        }

        const uint32_t* audiencePtr = exactAudienceIds.empty() ? nullptr : exactAudienceIds.data();
        const int audienceCount = static_cast<int>(exactAudienceIds.size());

        bool ok = fnRegisterEvent(
            eventType.c_str(),
            payloadToSend.c_str(),
            static_cast<uint32_t>(originatorFormID),
            static_cast<uint32_t>(targetFormID),
            audiencePtr,
            audienceCount);

        MARAS_LOG_DEBUG("[SkyrimNet] RegisterSkyrimNetEvent('{}', orig=0x{:08X}, target=0x{:08X}, exactAudience={}, audienceCount={}) -> {}",
                        eventType, originatorFormID, targetFormID, exactAudience || audienceCount > 0, audienceCount, ok);
        return ok;
    }

}  // namespace MARAS
