#include "core/SkyrimNetIntegration.h"
#include "core/NPCRelationshipManager.h"
#include "core/AffectionService.h"
#include "core/PlayerChildrenService.h"
#include "core/HomeCellService.h"
#include "core/FormCache.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/Settings.h"
#include "utils/JsonService.h"


#include <Windows.h>

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


namespace MARAS {

    bool IsSkyrimNetAvailable()
    {
        if (Settings::GetSingleton().simulateNoSkyrimNet) {
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
            spdlog::warn("[SkyrimNet] SkyrimNet.dll not found in process - integration skipped.");
            return nullptr;
        }

        auto* fn = reinterpret_cast<PublicRegisterDecorator_t>(
            GetProcAddress(hMod, "PublicRegisterDecorator"));
        if (!fn) {
            spdlog::warn("[SkyrimNet] PublicRegisterDecorator export not found in SkyrimNet.dll.");
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

    static uint64_t ActorToUUID(RE::Actor* actor)
    {
        if (!actor) return 0;
        return FormIDToUUID(actor->GetFormID());
    }

    static float GetNearbyActorsRadius()
    {
        float settingRadius = Settings::GetSingleton().skyrimnetActionRadius;
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
            std::string result = cb(actor);
            MARAS_LOG_TRACE("[SkyrimNet] Decorator '{}' returned for actor formId 0x{:08X}: '{}'.", decName, actorFormId, result);
            return result;
        };

        if (fn(name, description, std::move(wrappedCallback))) {
            spdlog::info("[SkyrimNet] Registered decorator '{}'", name);
        } else {
            spdlog::warn("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
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

    static nlohmann::json MakeNpcRef(RE::FormID formID) {
        nlohmann::json j;
        j["uuid"] = FormIDToUUID(formID);
        j["name"] = ActorSvc::GetName(formID);
        return j;
    }

    static nlohmann::json MakeNpcListRef(const std::vector<RE::FormID>& formIDs) {
        nlohmann::json j;
        std::vector<uint64_t> uuids;
        std::vector<std::string> names;
        for (auto fid : formIDs) {
            uuids.push_back(FormIDToUUID(fid));
            names.push_back(ActorSvc::GetName(fid));
        }
        j["uuids"] = uuids;
        j["names"] = FormatNamesSummary(names);
        return j;
    }

    static std::string BuildRelationshipProfileJSON(RE::Actor* actor) {
        if (!actor) return "{}";
        const auto actorFormID = actor->GetFormID();
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto& childSvc = PlayerChildrenService::GetSingleton();
        const bool playerPolygamyEnabled = Settings::GetSingleton().GetEnablePolygamy();

        auto statusToKey = [](RelationshipStatus status) -> std::string {
            switch (status) {
                case RelationshipStatus::Married:  return "married";
                case RelationshipStatus::Engaged:  return "engaged";
                case RelationshipStatus::Dating:   return "dating";
                case RelationshipStatus::Lover:    return "lover";
                case RelationshipStatus::Divorced: return "divorced";
                case RelationshipStatus::Jilted:   return "jilted";
                case RelationshipStatus::ExDating: return "exdating";
                case RelationshipStatus::ExLover:  return "exlover";
                default: return "";
            }
        };
        auto isCurrentStatus = [](RelationshipStatus status) {
            return status == RelationshipStatus::Married ||
                   status == RelationshipStatus::Engaged ||
                   status == RelationshipStatus::Dating ||
                   status == RelationshipStatus::Lover;
        };
        auto isMainTierStatus = [](RelationshipStatus status) {
            return status == RelationshipStatus::Dating ||
                   status == RelationshipStatus::Engaged ||
                   status == RelationshipStatus::Married;
        };
        auto isPastStatus = [](RelationshipStatus status) {
            return status == RelationshipStatus::Divorced ||
                   status == RelationshipStatus::Jilted ||
                   status == RelationshipStatus::ExDating ||
                   status == RelationshipStatus::ExLover;
        };
        auto affairStatusToKey = [](AffairStatus status) -> std::string {
            switch (status) {
                case AffairStatus::Ongoing:          return "ongoing";
                case AffairStatus::EndedBreakup:     return "ended_breakup";
                case AffairStatus::EndedVictimSplit: return "ended_victim_split";
                case AffairStatus::Allowed:          return "allowed";
                case AffairStatus::EndedConfronted:  return "ended_confronted";
                default: return "unknown";
            }
        };

        nlohmann::json j;
        j["schema_version"] = 2;
        j["actor"] = MakeNpcRef(actorFormID);
        j["family"] = nlohmann::json::array();
        const bool isPregnant = mgr.IsNPCPregnant(actorFormID);
        j["is_pregnant"] = isPregnant;
        if (isPregnant) {
            auto unbornIDs = mgr.GetUnbornChildren(actorFormID);
            size_t childCount = unbornIDs.size();

            RE::FormID motherPartnerID = 0;
            if (!unbornIDs.empty()) {
                motherPartnerID = mgr.GetMotherPartner(unbornIDs.front());
            }
            if (motherPartnerID == 0) {
                RE::FormID partnerID = mgr.GetPrimaryPartner(actorFormID);
                if (partnerID != 0 && !MatchesActor(partnerID, actorFormID) && ActorSvc::GetSex(partnerID) == 0) {
                    motherPartnerID = partnerID;
                }
            }
            if (motherPartnerID != 0 && mgr.GetRelationshipStatus(actorFormID, motherPartnerID) == RelationshipStatus::Divorced) {
                motherPartnerID = 0;
            }

            struct PregnancyFatherGroup {
                RE::FormID bioFatherID{ 0 };
                std::string bioFatherName;
                int count{ 0 };
                bool isMotherPartner{ false };
                bool isSecret{ false };
                bool isSecretForPartner{ false };
                bool isSecretForBioFather{ false };
            };

            std::vector<PregnancyFatherGroup> groups;

            for (RE::FormID uID : unbornIDs) {
                auto childOpt = mgr.GetChildEntry(uID);
                if (!childOpt.has_value()) continue;

                RE::FormID bioID = mgr.GetBioFather(uID);
                if (bioID == 0) {
                    bioID = childOpt->bioFatherID;
                }
                std::string bioName = (bioID != 0 && bioID != kUnknownFatherFormID) ? ActorSvc::GetName(bioID) : childOpt->paternitySecret.bioFatherName;
                if (bioName.empty()) {
                    bioName = "Unknown";
                }

                bool isBioSameAsPartner = (motherPartnerID != 0 && bioID != 0 && MatchesActor(bioID, motherPartnerID));
                bool secPartner = isBioSameAsPartner ? false : childOpt->paternitySecret.isSecretForPartner;
                bool secBio = isBioSameAsPartner ? false : childOpt->paternitySecret.isSecretForBioFather;
                bool isSec = isBioSameAsPartner ? false : (childOpt->paternitySecret.HasActiveSecret() && !childOpt->paternitySecret.discovered);
                if (!isBioSameAsPartner && !childOpt->paternitySecret.discovered && !isSec && motherPartnerID != 0 && bioID != 0 && !MatchesActor(bioID, motherPartnerID)) {
                    isSec = true;
                    secPartner = true;
                    secBio = true;
                }

                auto it = std::find_if(groups.begin(), groups.end(), [&](const PregnancyFatherGroup& g) {
                    if (bioID != 0 && bioID != kUnknownFatherFormID) return g.bioFatherID == bioID;
                    return g.bioFatherName == bioName;
                });

                if (it != groups.end()) {
                    it->count++;
                    if (isSec) it->isSecret = true;
                    if (secPartner) it->isSecretForPartner = true;
                    if (secBio) it->isSecretForBioFather = true;
                } else {
                    PregnancyFatherGroup grp;
                    grp.bioFatherID = bioID;
                    grp.bioFatherName = bioName;
                    grp.count = 1;
                    grp.isMotherPartner = isBioSameAsPartner;
                    grp.isSecret = isSec;
                    grp.isSecretForPartner = secPartner;
                    grp.isSecretForBioFather = secBio;
                    groups.push_back(grp);
                }
            }

            if (groups.empty()) {
                RE::FormID bioID = 0;
                std::string bioName;
                bool isSecret = false;
                bool secPartner = false;
                bool secBio = false;

                if (!unbornIDs.empty()) {
                    bioID = mgr.GetBioFather(unbornIDs.front());
                    if (auto childOpt = mgr.GetChildEntry(unbornIDs.front())) {
                        if (bioID == 0) {
                            bioID = childOpt->bioFatherID;
                        }
                        bioName = childOpt->paternitySecret.bioFatherName;
                        isSecret = childOpt->paternitySecret.HasActiveSecret() && !childOpt->paternitySecret.discovered;
                        secPartner = childOpt->paternitySecret.isSecretForPartner;
                        secBio = childOpt->paternitySecret.isSecretForBioFather;
                    }
                }

                if (bioID == 0 && motherPartnerID != 0) {
                    bioID = motherPartnerID;
                }

                if (bioName.empty() && bioID != 0 && bioID != kUnknownFatherFormID) {
                    bioName = ActorSvc::GetName(bioID);
                }
                if (bioName.empty()) {
                    bioName = "Unknown";
                }

                bool isBioSameAsPartner = (motherPartnerID != 0 && bioID != 0 && MatchesActor(bioID, motherPartnerID));
                if (isBioSameAsPartner) {
                    isSecret = false;
                    secPartner = false;
                    secBio = false;
                } else if (!isSecret && motherPartnerID != 0 && bioID != 0 && bioID != kUnknownFatherFormID) {
                    isSecret = true;
                    secPartner = true;
                    secBio = true;
                }

                PregnancyFatherGroup grp;
                grp.bioFatherID = bioID;
                grp.bioFatherName = bioName;
                grp.count = 1;
                grp.isMotherPartner = isBioSameAsPartner;
                grp.isSecret = isSecret;
                grp.isSecretForPartner = secPartner;
                grp.isSecretForBioFather = secBio;
                groups.push_back(grp);
                childCount = 1;
            }

            nlohmann::json pregObj;
            pregObj["is_pregnant"] = true;
            pregObj["child_count"] = static_cast<int>(childCount > 0 ? childCount : 1);
            if (motherPartnerID != 0) {
                pregObj["mother_partner"] = MakeNpcRef(motherPartnerID);
            }

            nlohmann::json groupsArr = nlohmann::json::array();
            bool anySecret = false;
            bool anySecretPartner = false;
            bool anySecretBio = false;

            for (const auto& grp : groups) {
                nlohmann::json gObj;
                if (grp.bioFatherID != 0 && grp.bioFatherID != kUnknownFatherFormID) {
                    gObj["bio_father"] = MakeNpcRef(grp.bioFatherID);
                } else {
                    nlohmann::json bioRef;
                    bioRef["uuid"] = 0;
                    bioRef["name"] = grp.bioFatherName.empty() ? "Unknown" : grp.bioFatherName;
                    gObj["bio_father"] = bioRef;
                }
                gObj["count"] = grp.count;
                gObj["is_mother_partner"] = grp.isMotherPartner;
                gObj["is_secret"] = grp.isSecret;
                gObj["is_secret_for_partner"] = grp.isSecretForPartner;
                gObj["is_secret_for_bio_father"] = grp.isSecretForBioFather;
                groupsArr.push_back(gObj);

                if (grp.isSecret) anySecret = true;
                if (grp.isSecretForPartner) anySecretPartner = true;
                if (grp.isSecretForBioFather) anySecretBio = true;
            }
            pregObj["groups"] = groupsArr;

            if (!groups.empty()) {
                if (groups[0].bioFatherID != 0 && groups[0].bioFatherID != kUnknownFatherFormID) {
                    pregObj["bio_father"] = MakeNpcRef(groups[0].bioFatherID);
                } else {
                    nlohmann::json bioRef;
                    bioRef["uuid"] = 0;
                    bioRef["name"] = groups[0].bioFatherName.empty() ? "Unknown" : groups[0].bioFatherName;
                    pregObj["bio_father"] = bioRef;
                }
            }
            if (!unbornIDs.empty()) {
                pregObj["unborn_child_id"] = unbornIDs.front();
                auto opt = mgr.GetChildEntry(unbornIDs.front());
                if (opt.has_value()) {
                    pregObj["unborn_child_name"] = opt->name;
                }
            }
            bool hasPaternitySecret = anySecret;
            if (!hasPaternitySecret && motherPartnerID != 0) {
                for (const auto& grp : groups) {
                    if (!grp.isMotherPartner && grp.bioFatherID != 0 && !MatchesActor(grp.bioFatherID, motherPartnerID)) {
                        hasPaternitySecret = true;
                        break;
                    }
                }
            }
            pregObj["has_paternity_secret"] = hasPaternitySecret;
            pregObj["is_secret"] = anySecret;
            pregObj["is_secret_for_partner"] = anySecretPartner;
            pregObj["is_secret_for_bio_father"] = anySecretBio;

            j["pregnancy"] = pregObj;
        }
        j["current_relationships"] = nlohmann::json::array();
        j["player_co_partners"] = nlohmann::json::array();
        j["past_relationships"] = nlohmann::json::array();
        j["infidelity"] = nlohmann::json::array();

        const auto partnerIDs = mgr.GetAllPartners(actorFormID);
        const auto cheatingRecords = mgr.GetCheatingRecordsForActor(actorFormID);

        auto isActorCheatingWith = [&](RE::FormID partnerID) {
            return std::ranges::any_of(cheatingRecords, [&](const CheatingRecord& record) {
                return record.affairStatus == AffairStatus::Ongoing &&
                       record.cheaterID == actorFormID &&
                       record.affairPartnerID == partnerID;
            });
        };
        auto findOpenPrimary = [&](RE::FormID excludedPartnerID) -> RE::FormID {
            for (auto partnerID : partnerIDs) {
                if (partnerID == excludedPartnerID) continue;
                const auto* data = mgr.GetNPCData(actorFormID, partnerID);
                if (data && isMainTierStatus(data->status) && data->isOpen) {
                    return partnerID;
                }
            }
            return 0;
        };
        auto hasOtherMainPair = [&](RE::FormID excludedPartnerID) {
            for (auto partnerID : partnerIDs) {
                if (partnerID == excludedPartnerID) continue;
                const auto* data = mgr.GetNPCData(actorFormID, partnerID);
                if (data && isMainTierStatus(data->status)) return true;
            }
            return false;
        };

        for (auto partnerID : partnerIDs) {
            const auto* data = mgr.GetNPCData(actorFormID, partnerID);
            if (!data) continue;

            if (isCurrentStatus(data->status) && !isActorCheatingWith(partnerID)) {
                nlohmann::json relationship;
                relationship["partner"] = MakeNpcRef(partnerID);
                relationship["status"] = statusToKey(data->status);
                if (isMainTierStatus(data->status)) {
                    relationship["is_open"] = data->isOpen;
                }

                const RE::FormID openPrimaryID = findOpenPrimary(partnerID);
                if ((actorFormID == kPlayerFormID && playerPolygamyEnabled) ||
                    (mgr.IsPlayerActiveMainPartner(actorFormID) && mgr.IsPlayerActiveMainPartner(partnerID))) {
                    relationship["permission"] = "player_polygamy";
                } else if (openPrimaryID != 0) {
                    relationship["permission"] = "open_primary";
                    relationship["permission_source"] = MakeNpcRef(openPrimaryID);
                } else if (!hasOtherMainPair(partnerID)) {
                    relationship["permission"] = isMainTierStatus(data->status) ? "primary" : "unrestricted";
                }
                j["current_relationships"].push_back(std::move(relationship));
            } else if (isPastStatus(data->status)) {
                nlohmann::json relationship;
                relationship["partner"] = MakeNpcRef(partnerID);
                relationship["status"] = statusToKey(data->status);
                j["past_relationships"].push_back(std::move(relationship));
            }
        }

        const auto* playerRelationship = mgr.GetNPCData(actorFormID, kPlayerFormID);
        if (actorFormID != kPlayerFormID && playerRelationship && isCurrentStatus(playerRelationship->status)) {
            j["player_relationship_status"] = statusToKey(playerRelationship->status);
            const auto& affSvc = AffectionService::GetSingleton();
            const int affScore = affSvc.GetPermanentAffection(actorFormID);
            const float daysSince = affSvc.GetDaysSinceLastAffection(actorFormID);
            std::string affTier = "estranged";
            if (affScore >= 75) affTier = "happy";
            else if (affScore >= 50) affTier = "content";
            else if (affScore >= 25) affTier = "troubled";
            j["player_affection_score"] = affScore;
            j["player_affection_tier"] = affTier;
            j["days_since_player_interaction"] = static_cast<int>(std::floor(daysSince));
            j["pending_intent"] = std::string(Utils::PartnerIntentTypeToString(mgr.GetPartnerPendingIntent(actorFormID).type));
        }
        if (playerRelationship && isCurrentStatus(playerRelationship->status) && playerPolygamyEnabled) {
            j["player_relationship_status"] = statusToKey(playerRelationship->status);
            struct PlayerPartner {
                RE::FormID formID;
                RelationshipStatus status;
            };
            std::vector<PlayerPartner> playerPartners;
            for (auto partnerID : mgr.GetAllPartners(kPlayerFormID)) {
                if (partnerID == actorFormID) continue;
                const auto* data = mgr.GetNPCData(kPlayerFormID, partnerID);
                if (data && isCurrentStatus(data->status)) {
                    playerPartners.push_back({ partnerID, data->status });
                }
            }
            auto statusPriority = [](RelationshipStatus status) {
                switch (status) {
                    case RelationshipStatus::Married: return 0;
                    case RelationshipStatus::Engaged: return 1;
                    case RelationshipStatus::Dating:  return 2;
                    case RelationshipStatus::Lover:   return 3;
                    default: return 4;
                }
            };
            std::ranges::sort(playerPartners, [&](const PlayerPartner& left, const PlayerPartner& right) {
                return statusPriority(left.status) < statusPriority(right.status);
            });
            for (const auto& playerPartner : playerPartners) {
                nlohmann::json coPartner;
                coPartner["person"] = MakeNpcRef(playerPartner.formID);
                coPartner["role"] = playerRelationship->status == RelationshipStatus::Married &&
                                            playerPartner.status == RelationshipStatus::Married
                                        ? "co_spouse"
                                        : "co_partner";
                coPartner["player_relationship_status"] = statusToKey(playerPartner.status);
                j["player_co_partners"].push_back(std::move(coPartner));
            }
        }

        auto canActorKnowPaternitySecret = [&](RE::FormID perspectiveActorID, RE::FormID motherID, RE::FormID motherPartnerID, RE::FormID bioFatherID, const PaternitySecret& sec) -> bool {
            if (sec.discovered) return true;
            if (motherID != 0 && MatchesActor(perspectiveActorID, motherID)) return true;
            if (motherPartnerID != 0 && MatchesActor(perspectiveActorID, motherPartnerID)) {
                return !sec.isSecretForPartner || sec.discoveredByPartner;
            }
            if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID && MatchesActor(perspectiveActorID, bioFatherID)) {
                return !sec.isSecretForBioFather || sec.discoveredByBioFather;
            }
            return !sec.isSecretForPartner || sec.discoveredByPartner;
        };

        auto buildChildObj = [&](RE::FormID childID) -> std::optional<nlohmann::json> {
            auto entryOpt = mgr.GetChildEntry(childID);
            if (!entryOpt.has_value()) return std::nullopt;
            const auto& childEntry = *entryOpt;
            if (childEntry.stage == ChildStage::Unborn) return std::nullopt;

            nlohmann::json childObj;
            childObj["uuid"] = FormIDToUUID(childID);
            std::string childName = childEntry.name.empty() ? ActorSvc::GetName(childID) : childEntry.name;
            childObj["name"] = childName;
            childObj["is_adopted"] = childEntry.isAdopted;
            childObj["is_biological"] = childEntry.isBiologicalChild;
            auto calendar = RE::Calendar::GetSingleton();
            float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;
            childObj["age_days"] = std::max(0.0f, currentDay - childEntry.birthTimestamp);

            std::string stageStr = "child";
            switch (childEntry.stage) {
                case ChildStage::Unborn:   stageStr = "unborn"; break;
                case ChildStage::Infant:   stageStr = "infant"; break;
                case ChildStage::Child:    stageStr = "child"; break;
                case ChildStage::Training: stageStr = "training"; break;
                case ChildStage::Adult:    stageStr = "adult"; break;
            }
            childObj["stage"] = stageStr;
            auto* actor = HasPhysicalActor(childID)
                ? NPCIndex::GetSingleton().GetActorFromFormId(childID)
                : nullptr;
            bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
            bool isEphemeral = !hasRealActor || (childEntry.stage == ChildStage::Unborn) || (childEntry.stage == ChildStage::Infant) || (childEntry.stage == ChildStage::Training);
            childObj["has_ref_id"] = hasRealActor;
            childObj["is_ephemeral"] = isEphemeral;
            if (!childEntry.trainingLocation.empty()) {
                childObj["training_location"] = childEntry.trainingLocation;
            }

            int childSex = 0;
            if (hasRealActor && actor->GetActorBase()) {
                childSex = (actor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
            } else {
                childSex = childEntry.gender;
            }
            if (childEntry.stage == ChildStage::Unborn || childSex == 2) {
                childObj["gender"] = "unknown";
                childObj["relation"] = "unborn child";
            } else {
                std::string childGenderStr = (childSex == 1) ? "female" : "male";
                childObj["gender"] = childGenderStr;
                childObj["relation"] = (childSex == 1) ? "daughter" : "son";
            }

            const auto& sec = childEntry.paternitySecret;
            RE::FormID motherPartnerID = (childEntry.presumedFatherID != 0) ? childEntry.presumedFatherID : mgr.GetMotherPartner(childID);
            RE::FormID motherID = mgr.GetBioMother(childID);
            RE::FormID bioFatherID = mgr.GetBioFather(childID);

            if (motherPartnerID != 0 && motherID != 0 && childEntry.presumedFatherID == 0 && !sec.isSecretForPartner && !mgr.IsGuardian(childID, motherPartnerID) && isPastStatus(mgr.GetRelationshipStatus(motherID, motherPartnerID))) {
                motherPartnerID = 0;
            }

            bool isBioSameAsPartner = (motherPartnerID != 0 && bioFatherID != 0 && MatchesActor(bioFatherID, motherPartnerID));
            bool canActorKnow = canActorKnowPaternitySecret(actorFormID, motherID, motherPartnerID, bioFatherID, sec);
            bool hideSecretFromActor = (motherPartnerID != 0 && !isBioSameAsPartner && sec.HasActiveSecret() && !canActorKnow);

            if (motherPartnerID != 0 && !isBioSameAsPartner) {
                childObj["presumed_father"] = MakeNpcRef(motherPartnerID);
            }

            if (motherPartnerID != 0 && !isBioSameAsPartner && (sec.isSecret || sec.discovered) && canActorKnow) {
                nlohmann::json pSec;
                bool secPartner = sec.isSecretForPartner;
                pSec["is_secret"] = !sec.discovered && (secPartner || sec.isSecretForBioFather);
                pSec["is_secret_for_partner"] = secPartner;
                pSec["is_secret_for_bio_father"] = sec.isSecretForBioFather;
                pSec["mother_partner"] = MakeNpcRef(motherPartnerID);
                pSec["presumed_father"] = MakeNpcRef(motherPartnerID);
                pSec["presumed_father_is_guardian"] = mgr.IsGuardian(childID, motherPartnerID);
                if (bioFatherID != 0) {
                    pSec["biological_father"] = MakeNpcRef(bioFatherID);
                } else if (!sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                    nlohmann::json bioFatherRef;
                    bioFatherRef["uuid"] = 0;
                    bioFatherRef["name"] = sec.bioFatherName;
                    pSec["biological_father"] = bioFatherRef;
                } else {
                    pSec["biological_father"] = MakeNpcRef(0);
                }
                pSec["discovered_by_partner"] = sec.discoveredByPartner;
                pSec["discovered_by_bio_father"] = sec.discoveredByBioFather;
                pSec["has_phenotype_mismatch"] = sec.hasPhenotypeMismatch;
                childObj["paternity_secret"] = pSec;
            }

            std::vector<RE::FormID> currentGuardians;
            if (childEntry.guardianAID != 0) currentGuardians.push_back(childEntry.guardianAID);
            if (childEntry.guardianBID != 0 && childEntry.guardianBID != childEntry.guardianAID) currentGuardians.push_back(childEntry.guardianBID);
            if (!currentGuardians.empty()) {
                childObj["guardians"] = MakeNpcListRef(currentGuardians);
                childObj["current_parents"] = MakeNpcListRef(currentGuardians);
            }

            RE::FormID childBioMotherID = childEntry.bioMotherID;
            RE::FormID effectiveBioFatherID = hideSecretFromActor ? motherPartnerID : childEntry.bioFatherID;

            std::vector<RE::FormID> bioParents;
            if (childBioMotherID != 0) bioParents.push_back(childBioMotherID);
            if (effectiveBioFatherID != 0 && effectiveBioFatherID != kUnknownFatherFormID && effectiveBioFatherID != childBioMotherID) {
                bioParents.push_back(effectiveBioFatherID);
            }
            if (!bioParents.empty()) {
                childObj["biological_parents"] = MakeNpcListRef(bioParents);
            }

            if (!bioParents.empty()) {
                std::vector<RE::FormID> stepParents;
                for (auto cp : currentGuardians) {
                    bool isBio = false;
                    for (auto bp : bioParents) {
                        if (MatchesActor(cp, bp)) { isBio = true; break; }
                    }
                    if (!isBio) {
                        stepParents.push_back(cp);
                    }
                }
                if (!stepParents.empty()) {
                    childObj["step_parents"] = MakeNpcListRef(stepParents);
                }

                if (childBioMotherID != 0) {
                    childObj["bio_mother"] = MakeNpcRef(childBioMotherID);
                    childObj["biological_mother"] = childObj["bio_mother"];
                }
                if (effectiveBioFatherID != 0 && effectiveBioFatherID != kUnknownFatherFormID) {
                    childObj["bio_father"] = MakeNpcRef(effectiveBioFatherID);
                    childObj["biological_father"] = childObj["bio_father"];
                } else if (!hideSecretFromActor && !sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                    nlohmann::json bioFatherRef;
                    bioFatherRef["uuid"] = 0;
                    bioFatherRef["name"] = sec.bioFatherName;
                    childObj["bio_father"] = bioFatherRef;
                    childObj["biological_father"] = bioFatherRef;
                }

                bool isActorStep = false;
                for (auto sp : stepParents) {
                    if (MatchesActor(actorFormID, sp)) { isActorStep = true; break; }
                }
                bool isActorBio = false;
                for (auto bp : bioParents) {
                    if (MatchesActor(actorFormID, bp)) { isActorBio = true; break; }
                }
                childObj["is_actor_step_parent"] = isActorStep;
                childObj["is_actor_bio_parent"] = isActorBio;

                if (isActorStep) {
                    childObj["relation"] = (childSex == 1) ? "stepdaughter" : "stepson";
                }
            }

            RE::FormID critterID = childSvc.GetCritter(childID);
            if (critterID != 0) {
                nlohmann::json critterObj;
                critterObj["uuid"] = FormIDToUUID(critterID);
                critterObj["name"] = ActorSvc::GetName(critterID);
                critterObj["type"] = CleanAnimalRaceName(ActorSvc::GetRaceName(critterID));
                childObj["critter"] = critterObj;
            }

            RE::FormID petID = childSvc.GetPet(childID);
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["uuid"] = FormIDToUUID(petID);
                petObj["name"] = ActorSvc::GetName(petID);
                petObj["type"] = CleanAnimalRaceName(ActorSvc::GetRaceName(petID));
                childObj["pet"] = petObj;
            }

            return childObj;
        };

        auto allChildIDs = mgr.GetChildrenOfActor(actorFormID);
        std::map<RE::FormID, std::vector<nlohmann::json>> childrenByOtherParent;
        std::map<RE::FormID, std::vector<std::string>> childNamesByOtherParent;
        std::vector<RE::FormID> otherParentOrder;
        std::vector<nlohmann::json> nonCustodialChildren;

        for (auto childID : allChildIDs) {
            auto entryOpt = mgr.GetChildEntry(childID);
            if (!entryOpt.has_value()) continue;
            const auto& childEntry = *entryOpt;
            if (childEntry.stage == ChildStage::Unborn) continue;

            if (mgr.IsGuardian(childID, actorFormID)) {
                RE::FormID otherPID = mgr.GetCoGuardian(childID, actorFormID);

                if (auto cObjOpt = buildChildObj(childID)) {
                    if (childrenByOtherParent.find(otherPID) == childrenByOtherParent.end()) {
                        otherParentOrder.push_back(otherPID);
                    }
                    childrenByOtherParent[otherPID].push_back(*cObjOpt);
                    childNamesByOtherParent[otherPID].push_back((*cObjOpt)["name"].get<std::string>());
                }
            } else {
                RE::FormID motherPartnerID = (childEntry.presumedFatherID != 0) ? childEntry.presumedFatherID : mgr.GetMotherPartner(childID);
                bool isBioParent = MatchesActor(childEntry.bioMotherID, actorFormID) || MatchesActor(childEntry.bioFatherID, actorFormID);
                bool isSecretPresumedFather = !isBioParent && motherPartnerID != 0 &&
                                              MatchesActor(motherPartnerID, actorFormID) &&
                                              childEntry.paternitySecret.isSecretForPartner;
                if (isBioParent || isSecretPresumedFather) {
                    const auto& sec = childEntry.paternitySecret;
                    bool isHiddenSecret = false;
                    if (isBioParent && MatchesActor(childEntry.bioFatherID, actorFormID)) {
                        if (sec.HasActiveSecret() && !sec.discovered && sec.isSecretForBioFather && !sec.discoveredByBioFather) {
                            isHiddenSecret = true;
                        }
                    }
                    if (!isHiddenSecret) {
                        nlohmann::json ncObj;
                        ncObj["uuid"] = FormIDToUUID(childID);
                        std::string childName = childEntry.name.empty() ? ActorSvc::GetName(childID) : childEntry.name;
                        ncObj["name"] = childName;
                        auto* childActor = HasPhysicalActor(childID)
                            ? NPCIndex::GetSingleton().GetActorFromFormId(childID)
                            : nullptr;
                        int childSex = 0;
                        if (childActor && childActor->GetActorBase()) {
                            childSex = (childActor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
                        } else {
                            childSex = childEntry.gender;
                        }
                        ncObj["gender"] = (childSex == 1) ? "female" : "male";
                        ncObj["relation"] = (childSex == 1) ? "daughter" : "son";

                        std::vector<RE::FormID> guardians;
                        if (childEntry.guardianAID != 0) guardians.push_back(childEntry.guardianAID);
                        if (childEntry.guardianBID != 0 && childEntry.guardianBID != childEntry.guardianAID) guardians.push_back(childEntry.guardianBID);
                        if (!guardians.empty()) {
                            ncObj["guardians"] = MakeNpcListRef(guardians);
                        }

                        RE::FormID otherBioID = (isSecretPresumedFather || !MatchesActor(childEntry.bioMotherID, actorFormID))
                                                    ? childEntry.bioMotherID
                                                    : childEntry.bioFatherID;
                        if (otherBioID != 0 && otherBioID != kUnknownFatherFormID) {
                            ncObj["other_bio_parent"] = MakeNpcRef(otherBioID);
                        }
                        if (isSecretPresumedFather) {
                            ncObj["is_presumed_father"] = true;
                        }
                        nonCustodialChildren.push_back(std::move(ncObj));
                    }
                }
            }
        }

        nlohmann::json childrenGroupsArray = nlohmann::json::array();
        for (auto otherPID : otherParentOrder) {
            nlohmann::json grpObj;
            if (otherPID != 0) {
                grpObj["other_parent"] = MakeNpcRef(otherPID);
            } else {
                grpObj["single_parent"] = true;
            }
            grpObj["children"] = childrenByOtherParent[otherPID];
            grpObj["children_names_summary"] = FormatNamesSummary(childNamesByOtherParent[otherPID]);
            childrenGroupsArray.push_back(grpObj);
        }

        j["family"] = std::move(childrenGroupsArray);
        j["non_custodial_children"] = std::move(nonCustodialChildren);

        if (auto asChildOpt = mgr.GetChildEntry(actorFormID); asChildOpt.has_value()) {
            const auto& asChild = *asChildOpt;
            nlohmann::json parentage;
            parentage["is_adopted"] = asChild.isAdopted;
            parentage["is_biological"] = asChild.isBiologicalChild;

            const auto& sec = asChild.paternitySecret;
            RE::FormID asChildMotherID = mgr.GetBioMother(actorFormID);
            RE::FormID asChildFatherID = (asChild.presumedFatherID != 0) ? asChild.presumedFatherID : mgr.GetMotherPartner(actorFormID);
            RE::FormID asChildBioFatherID = mgr.GetBioFather(actorFormID);

            RE::FormID presFatherID = asChildFatherID;
            if (presFatherID != 0 && asChildMotherID != 0 && asChild.presumedFatherID == 0 && !sec.isSecretForPartner && !mgr.IsGuardian(actorFormID, presFatherID) && isPastStatus(mgr.GetRelationshipStatus(asChildMotherID, presFatherID))) {
                presFatherID = 0;
            }

            bool isBioSameAsPresumed = (presFatherID != 0 && asChildBioFatherID != 0 && MatchesActor(asChildBioFatherID, presFatherID));
            bool canChildKnow = canActorKnowPaternitySecret(actorFormID, asChildMotherID, presFatherID, asChildBioFatherID, sec);
            bool hideSecretFromChild = (presFatherID != 0 && !isBioSameAsPresumed && sec.HasActiveSecret() && !canChildKnow);
            RE::FormID effectiveChildBioFatherID = hideSecretFromChild ? presFatherID : asChildBioFatherID;

            if (asChildMotherID != 0 && (mgr.IsGuardian(actorFormID, asChildMotherID) || (asChild.guardianAID == 0 && asChild.guardianBID == 0))) {
                parentage["mother"] = MakeNpcRef(asChildMotherID);
            }
            if (asChildFatherID != 0) {
                parentage["mother_partner"] = MakeNpcRef(asChildFatherID);
                if (mgr.IsGuardian(actorFormID, asChildFatherID) || (asChild.guardianAID == 0 && asChild.guardianBID == 0)) {
                    parentage["father"] = MakeNpcRef(asChildFatherID);
                }
            }
            if (presFatherID != 0 && !isBioSameAsPresumed) {
                parentage["presumed_father"] = MakeNpcRef(presFatherID);
            }

            RE::FormID asChildBioMotherID = asChild.bioMotherID;

            if (asChildBioMotherID != 0) {
                parentage["bio_mother"] = MakeNpcRef(asChildBioMotherID);
                parentage["biological_mother"] = MakeNpcRef(asChildBioMotherID);
            }
            if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID) {
                parentage["bio_father"] = MakeNpcRef(effectiveChildBioFatherID);
                parentage["biological_father"] = MakeNpcRef(effectiveChildBioFatherID);
            } else if (!hideSecretFromChild && !sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                nlohmann::json bioFatherRef;
                bioFatherRef["uuid"] = 0;
                bioFatherRef["name"] = sec.bioFatherName;
                parentage["bio_father"] = bioFatherRef;
                parentage["biological_father"] = bioFatherRef;
            }

            std::vector<RE::FormID> currentGuardians;
            if (asChild.guardianAID != 0) currentGuardians.push_back(asChild.guardianAID);
            if (asChild.guardianBID != 0 && asChild.guardianBID != asChild.guardianAID) currentGuardians.push_back(asChild.guardianBID);
            if (!currentGuardians.empty()) {
                parentage["guardians"] = MakeNpcListRef(currentGuardians);
                parentage["current_parents"] = MakeNpcListRef(currentGuardians);
            }

            std::vector<RE::FormID> bioParents;
            if (asChildBioMotherID != 0) bioParents.push_back(asChildBioMotherID);
            if (effectiveChildBioFatherID != 0 && effectiveChildBioFatherID != kUnknownFatherFormID && effectiveChildBioFatherID != asChildBioMotherID) {
                bioParents.push_back(effectiveChildBioFatherID);
            }
            if (!bioParents.empty()) {
                parentage["biological_parents"] = MakeNpcListRef(bioParents);
            }

            if (!bioParents.empty()) {
                std::vector<RE::FormID> stepParents;
                for (auto cp : currentGuardians) {
                    bool isBio = false;
                    for (auto bp : bioParents) {
                        if (MatchesActor(cp, bp)) { isBio = true; break; }
                    }
                    if (!isBio) {
                        stepParents.push_back(cp);
                    }
                }
                if (!stepParents.empty()) {
                    parentage["step_parents"] = MakeNpcListRef(stepParents);
                }
            }
            std::string stageStr = "child";
            switch (asChild.stage) {
                case ChildStage::Unborn:   stageStr = "unborn"; break;
                case ChildStage::Infant:   stageStr = "infant"; break;
                case ChildStage::Child:    stageStr = "child"; break;
                case ChildStage::Training: stageStr = "training"; break;
                case ChildStage::Adult:    stageStr = "adult"; break;
            }
            parentage["stage"] = stageStr;
            if (!asChild.trainingLocation.empty()) {
                parentage["training_location"] = asChild.trainingLocation;
            }

            int asChildSex = 0;
            if (HasPhysicalActor(asChild.childID)) {
                auto* childAct = NPCIndex::GetSingleton().GetActorFromFormId(asChild.childID);
                if (childAct && childAct->GetActorBase()) {
                    asChildSex = (childAct->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
                }
            } else {
                asChildSex = asChild.gender;
            }
            parentage["gender"] = (asChildSex == 1) ? "female" : "male";
            parentage["relation"] = (asChildSex == 1) ? "daughter" : "son";

            std::vector<RE::FormID> siblingIDs;
            auto addSiblings = [&](RE::FormID parentID) {
                if (parentID == 0) return;
                for (auto sibID : mgr.GetChildrenOfActor(parentID)) {
                    if (sibID != actorFormID && std::find(siblingIDs.begin(), siblingIDs.end(), sibID) == siblingIDs.end()) {
                        auto sibOpt = mgr.GetChildEntry(sibID);
                        if (sibOpt && sibOpt->stage == ChildStage::Unborn) continue;
                        siblingIDs.push_back(sibID);
                    }
                }
            };
            addSiblings(asChild.guardianAID);
            addSiblings(asChild.guardianBID);
            addSiblings(asChildBioMotherID);
            addSiblings(effectiveChildBioFatherID);
            if (!siblingIDs.empty()) {
                parentage["siblings"] = MakeNpcListRef(siblingIDs);
            }

            RE::FormID critterID = childSvc.GetCritter(actorFormID);
            if (critterID != 0) {
                nlohmann::json critterObj;
                critterObj["uuid"] = FormIDToUUID(critterID);
                critterObj["name"] = ActorSvc::GetName(critterID);
                critterObj["type"] = CleanAnimalRaceName(ActorSvc::GetRaceName(critterID));
                parentage["critter"] = critterObj;
            }

            RE::FormID petID = childSvc.GetPet(actorFormID);
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["uuid"] = FormIDToUUID(petID);
                petObj["name"] = ActorSvc::GetName(petID);
                petObj["type"] = CleanAnimalRaceName(ActorSvc::GetRaceName(petID));
                parentage["pet"] = petObj;
            }

            if (presFatherID != 0 && !isBioSameAsPresumed && (sec.isSecret || sec.discovered) && canChildKnow) {
                nlohmann::json pIntrigue;
                bool secAssumed = sec.isSecretForPartner;
                pIntrigue["is_secret"] = !sec.discovered && (secAssumed || sec.isSecretForBioFather);
                pIntrigue["is_secret_for_partner"] = secAssumed;
                pIntrigue["is_secret_for_bio_father"] = sec.isSecretForBioFather;
                pIntrigue["mother_partner"] = MakeNpcRef(presFatherID);
                pIntrigue["presumed_father"] = MakeNpcRef(presFatherID);
                pIntrigue["presumed_father_is_guardian"] = mgr.IsGuardian(actorFormID, presFatherID);
                if (asChildBioFatherID != 0) {
                    pIntrigue["biological_father"] = MakeNpcRef(asChildBioFatherID);
                } else if (!sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                    nlohmann::json bioFatherRef;
                    bioFatherRef["uuid"] = 0;
                    bioFatherRef["name"] = sec.bioFatherName;
                    pIntrigue["biological_father"] = bioFatherRef;
                }
                pIntrigue["discovered_by_partner"] = sec.discoveredByPartner;
                pIntrigue["discovered_by_bio_father"] = sec.discoveredByBioFather;
                parentage["paternity_secret"] = pIntrigue;
            }
            j["parents"] = std::move(parentage);
        }

        for (const auto& record : cheatingRecords) {
            if (record.affairStatus == AffairStatus::Allowed) continue;
            if (record.victimID == actorFormID && record.isSecret) continue;

            nlohmann::json infidelity;
            if (record.cheaterID == actorFormID) {
                infidelity["actor_role"] = "cheater";
            } else if (record.affairPartnerID == actorFormID) {
                infidelity["actor_role"] = "affair_partner";
            } else {
                infidelity["actor_role"] = "victim";
            }
            infidelity["cheater"] = MakeNpcRef(record.cheaterID);
            infidelity["affair_partner"] = MakeNpcRef(record.affairPartnerID);
            infidelity["victim"] = MakeNpcRef(record.victimID);
            infidelity["status"] = affairStatusToKey(record.affairStatus);
            infidelity["type"] = StringUtils::ToLowerStd(Utils::AffairTypeToString(record.affairType));
            infidelity["affair_stage"] = StringUtils::ToLowerStd(std::string(Utils::AffairStageToString(record.affairStage)));
            infidelity["incident_count"] = record.count;
            infidelity["last_timestamp"] = record.lastTimestamp;
            infidelity["is_secret"] = record.isSecret;
            j["infidelity"].push_back(std::move(infidelity));
        }

        try {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (const std::exception& e) {
            spdlog::error("[SkyrimNet] BuildRelationshipProfileJSON dump error: {}", e.what());
            return "{}";
        }
    }

    static std::string GetCandidateDisplayName(RE::FormID partnerFid, RE::Actor* targetActor = nullptr) {
        std::string name = ActorSvc::GetName(partnerFid);
        if (name.empty() || name == "None") {
            if (!targetActor) {
                if (auto* form = RE::TESForm::LookupByID(partnerFid)) {
                    targetActor = form->As<RE::Actor>();
                }
            }
            if (targetActor && targetActor->GetDisplayFullName() && *(targetActor->GetDisplayFullName())) {
                name = targetActor->GetDisplayFullName();
            } else if (partnerFid == kPlayerFormID) {
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

        const float radius = GetNearbyActorsRadius();
        const auto nearby = Utils::GetNearbyActors(actor, radius);
        std::string result;
        for (RE::Actor* target : nearby) {
            if (!target) continue;
            const auto targetFid = target->GetFormID();
            if (pairCheck(fid, targetFid) && (!extraFilter || extraFilter(fid, targetFid))) {
                std::string name = GetCandidateDisplayName(targetFid, target);
                if (!name.empty()) {
                    auto* base = target->GetActorBase();
                    const char* gender = (base && base->GetSex() == RE::SEX::kFemale) ? "Female" : "Male";
                    if (!result.empty()) result += ", ";
                    result += fmt::format("{} ({})", name, gender);
                }
            }
        }
        return result;
    }

    static std::string GetPartnersByStatus(
        RE::Actor* actor,
        RelationshipStatus status,
        std::function<bool(RE::FormID, RE::FormID)> pairCheck)
    {
        if (!actor) return "";
        const auto fid = actor->GetFormID();
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto partners = mgr.GetPartners(fid, status);
        if (partners.empty()) return "";

        std::string result;
        for (auto pFid : partners) {
            if (!pairCheck(fid, pFid)) continue;
            std::string name = GetCandidateDisplayName(pFid);
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
    spdlog::info("[SkyrimNet] RegisterSkyrimNetDecorators called.");

    auto* fn = FindRegisterDecorator();
    if (!fn) return;

    RegisterDecorator(fn,
        "ttm_get_ex_partner",
        "Returns a sentence describing this actor's connection to a player household member via a prior in-game relationship. Covers both the NPC who left someone to join the player, and the NPC who was left behind.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const uint32_t actorFormId = actor->GetFormID();
            const auto& mgr      = NPCRelationshipManager::GetSingleton();
            const auto* player   = RE::PlayerCharacter::GetSingleton();
            const std::string playerName = player ? player->GetName() : "the Dragonborn";

            // Helper: map MARAS status to a player-relationship phrase.
            auto getPlayerRel = [&mgr](RE::FormID fid) -> std::string {
                if (mgr.IsEngaged(fid))  return "became engaged to";
                if (mgr.IsMarried(fid))  return "married";
                if (mgr.IsJilted(fid))   return "had a brief engagement with";
                if (mgr.IsDivorced(fid)) return "married and later divorced";
                if (mgr.IsExDating(fid)) return "briefly dated";
                if (mgr.IsExLover(fid))  return "were lovers with";
                return "";
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
                // via exact base pointer comparison — more reliable than name matching.
                RE::FormID matchedFormID = 0;
                auto findByBase = [&](const std::vector<RE::FormID>& ids) {
                    for (auto fid : ids) {
                        if (matchedFormID) break;
                        const auto* a = ActorSvc::GetActorBase(fid);
                        if (a && a == exPartner2Base)
                            matchedFormID = fid;
                    }
                };
                findByBase(mgr.GetAllMarried());
                if (!matchedFormID) findByBase(mgr.GetAllEngaged());
                if (!matchedFormID) findByBase(mgr.GetAllJilted());
                if (!matchedFormID) findByBase(mgr.GetAllDivorced());
                if (!matchedFormID) findByBase(mgr.GetAllExDating());
                if (!matchedFormID) findByBase(mgr.GetAllExLovers());

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

        const float radius = GetNearbyActorsRadius();
        const auto nearby = Utils::GetNearbyActors(actor, radius);

        // If the actor is currently dating someone, that specific partner is the only valid target
        const auto datingPartners = mgr.GetPartners(fid, RelationshipStatus::Dating);
        if (!datingPartners.empty()) {
            const auto datingPartnerFid = datingPartners[0];
            for (RE::Actor* target : nearby) {
                if (target && target->GetFormID() == datingPartnerFid && mgr.IsAvailableForEngaged(fid, datingPartnerFid)) {
                    return GetCandidateDisplayName(datingPartnerFid, target);
                }
            }
            return "";
        }

        // If single, find all nearby eligible candidates
        std::string result;
        for (RE::Actor* target : nearby) {
            if (!target) continue;
            const auto targetFid = target->GetFormID();
            const auto targetDating = mgr.GetPartners(targetFid, RelationshipStatus::Dating);
            if (!targetDating.empty() && targetDating[0] != fid) {
                continue;
            }
            if (mgr.IsAvailableForEngaged(fid, targetFid)) {
                std::string name = GetCandidateDisplayName(targetFid, target);
                if (!name.empty()) {
                    if (!result.empty()) result += ", ";
                    result += name;
                }
            }
        }
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
            if (fid == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                return "";
            }

            const auto& mgr = NPCRelationshipManager::GetSingleton();
            std::string result;
            auto appendExclusivePartners = [&](const std::vector<RE::FormID>& partners) {
                for (auto partnerId : partners) {
                    const auto* rData = mgr.GetNPCData(fid, partnerId);
                    if (rData && rData->isOpen) {
                        continue;
                    }
                    std::string name = ActorSvc::GetName(partnerId);
                    if (!name.empty() && name != "None") {
                        if (!result.empty()) result += ", ";
                        result += name;
                    }
                }
            };

            appendExclusivePartners(mgr.GetPartners(fid, RelationshipStatus::Married));
            appendExclusivePartners(mgr.GetPartners(fid, RelationshipStatus::Engaged));
            appendExclusivePartners(mgr.GetPartners(fid, RelationshipStatus::Dating));

            return result;
        });

    RegisterDecorator(fn,
        "ttm_get_lovers_becoming_ex",
        "Returns a comma-separated list of names of all lover-status partners for this actor that will become ex-lovers upon entering dating or engagement, or empty string if none.",
        [](RE::Actor* actor) -> std::string {
            if (!actor) return "";
            const auto fid = actor->GetFormID();
            if (fid == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) {
                return "";
            }

            const auto& mgr = NPCRelationshipManager::GetSingleton();
            if (mgr.HasAnyOpenMainPair(fid)) {
                return "";
            }

            const auto lovers = mgr.GetPartners(fid, RelationshipStatus::Lover);
            std::string result;
            for (auto partnerId : lovers) {
                std::string name = ActorSvc::GetName(partnerId);
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
            const auto& mgr = NPCRelationshipManager::GetSingleton();
            const auto dating = mgr.GetPartners(fid, RelationshipStatus::Dating);
            if (!dating.empty()) {
                std::string name = ActorSvc::GetName(dating[0]);
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
            const auto& mgr = NPCRelationshipManager::GetSingleton();
            std::string result;
            auto appendOpenPartners = [&](const std::vector<RE::FormID>& partners) {
                for (auto partnerId : partners) {
                    const auto* rData = mgr.GetNPCData(fid, partnerId);
                    if (rData && rData->isOpen) {
                        std::string name = ActorSvc::GetName(partnerId);
                        if (!name.empty() && name != "None") {
                            if (!result.empty()) result += ", ";
                            result += name;
                        }
                    }
                }
            };

            appendOpenPartners(mgr.GetPartners(fid, RelationshipStatus::Married));
            appendOpenPartners(mgr.GetPartners(fid, RelationshipStatus::Engaged));
            appendOpenPartners(mgr.GetPartners(fid, RelationshipStatus::Dating));

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

        std::string actorName = ActorSvc::GetName(fid);

        if (fid == kPlayerFormID || actor->IsPlayerRef()) {
            return "true";
        }

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        if (mgr.IsRegistered(fid)) {
            return "true";
        }
        if (auto childOpt = mgr.GetChildEntry(fid); childOpt.has_value()) {
            return "true";
        }
        if (mgr.IsNPCPregnant(fid)) {
            return "true";
        }
        if (!mgr.GetAllPartners(fid).empty()) {
            return "true";
        }
        if (mgr.GetNPCProfile(fid) != nullptr) {
            return "true";
        }

        auto* trackedFaction = FormCache::GetSingleton().GetTrackedFaction();
        if (trackedFaction && actor->IsInFaction(trackedFaction)) {
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
                spdlog::error("[SkyrimNet] Exception in ttm_npc_relationship_profile: {}", e.what());
                return "{}";
            } catch (...) {
                spdlog::error("[SkyrimNet] Unknown exception in ttm_npc_relationship_profile");
                return "{}";
            }
        });

    auto makeNearbyActorsDecorator = []() {
        return [](RE::Actor* actor) -> std::string {
            nlohmann::json j;
            j["actorIds"] = nlohmann::json::array();
            if (!actor) {
                return j.dump();
            }

            const float radius = GetNearbyActorsRadius();
            const auto nearby = Utils::GetNearbyActors(actor, radius);

            for (RE::Actor* a : nearby) {
                uint64_t uuid = ActorToUUID(a);
                if (uuid != 0) {
                    j["actorIds"].push_back(uuid);
                }
            }
            std::string result = j.dump();
            return result;
        };
    };

    RegisterDecorator(fn,
        "ttm_get_nearby_actors",
        "JSON object listing all nearby valid actors (including the player, within nearbyActors.radius). Fields: actorIds (array of SkyrimNet UUIDs).",
        makeNearbyActorsDecorator());

    auto getConfessAffairCandidates = [](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto& m = NPCRelationshipManager::GetSingleton();
        const auto records = m.GetCheatingRecordsForActor(aFid);
        if (records.empty()) return "";

        const float r = GetNearbyActorsRadius();
        const auto nearbyActors = Utils::GetNearbyActors(a, r);
        if (nearbyActors.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (RE::Actor* target : nearbyActors) {
                if (target && Utils::MatchesActor(targetFid, target->GetFormID())) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.cheaterID != aFid) continue;
            if (!rec.isSecret || rec.affairStatus != AffairStatus::Ongoing) continue;
            if (!isNear(rec.victimID)) continue;

            if (!seenPairs.insert({ rec.victimID, rec.affairPartnerID }).second) continue;

            std::string victimName = GetCandidateDisplayName(rec.victimID);
            std::string partnerName = GetCandidateDisplayName(rec.affairPartnerID);

            if (!res.empty()) res += ", ";
            res += victimName + " (partner; secret affair with " + partnerName + ")";
        }
        return res;
    };

    auto getExposeAffairCandidates = [](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto& m = NPCRelationshipManager::GetSingleton();
        const auto records = m.GetCheatingRecordsForActor(aFid);
        if (records.empty()) return "";

        const float r = GetNearbyActorsRadius();
        const auto nearbyActors = Utils::GetNearbyActors(a, r);
        if (nearbyActors.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (RE::Actor* target : nearbyActors) {
                if (target && Utils::MatchesActor(targetFid, target->GetFormID())) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.affairPartnerID != aFid) continue;
            if (!rec.isSecret || rec.affairStatus != AffairStatus::Ongoing) continue;
            if (!isNear(rec.victimID)) continue;

            if (!seenPairs.insert({ rec.victimID, rec.cheaterID }).second) continue;

            std::string victimName = GetCandidateDisplayName(rec.victimID);
            std::string cheaterName = GetCandidateDisplayName(rec.cheaterID);

            if (!res.empty()) res += ", ";
            res += victimName + " (victim; secret affair with " + cheaterName + ")";
        }
        return res;
    };

    auto getResolveAffairCandidates = [](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto& m = NPCRelationshipManager::GetSingleton();
        const auto records = m.GetCheatingRecordsForActor(aFid);
        if (records.empty()) return "";

        const float r = GetNearbyActorsRadius();
        const auto nearbyActors = Utils::GetNearbyActors(a, r);
        if (nearbyActors.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (RE::Actor* target : nearbyActors) {
                if (target && Utils::MatchesActor(targetFid, target->GetFormID())) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenPairs;
        for (const auto& rec : records) {
            if (rec.isSecret || rec.affairStatus != AffairStatus::Ongoing) continue;

            RE::FormID otherParty = 0;
            if (rec.cheaterID == aFid) {
                otherParty = rec.victimID;
            } else if (rec.victimID == aFid) {
                otherParty = rec.cheaterID;
            } else {
                continue;
            }

            if (!isNear(otherParty)) continue;
            if (!seenPairs.insert({ otherParty, rec.affairPartnerID }).second) continue;

            std::string otherName = GetCandidateDisplayName(otherParty);
            std::string partnerName = GetCandidateDisplayName(rec.affairPartnerID);

            if (!res.empty()) res += ", ";
            res += otherName + " (regarding exposed affair with " + partnerName + ")";
        }
        return res;
    };

    auto getRevealChildSecretCandidates = [](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto& m = NPCRelationshipManager::GetSingleton();
        const auto& allChildren = m.GetAllChildren();
        if (allChildren.empty()) return "";

        const float r = GetNearbyActorsRadius();
        const auto nearbyActors = Utils::GetNearbyActors(a, r);
        if (nearbyActors.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (RE::Actor* target : nearbyActors) {
                if (target && Utils::MatchesActor(targetFid, target->GetFormID())) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenUnbornFathers;
        for (const auto& [cID, entry] : allChildren) {
            RE::FormID motherID = m.GetBioMother(cID);
            if (!MatchesActor(motherID, aFid)) continue;
            if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;

            RE::FormID motherPartnerID = m.GetMotherPartner(cID);
            RE::FormID bioFatherID = m.GetBioFather(cID);
            if (motherPartnerID != 0 && bioFatherID != 0 && MatchesActor(bioFatherID, motherPartnerID)) continue;

            if (entry.stage == ChildStage::Unborn) {
                if (!seenUnbornFathers.insert({ motherPartnerID, bioFatherID }).second) {
                    continue;
                }
            }

            bool pNear = entry.paternitySecret.isSecretForPartner && motherPartnerID != 0 && isNear(motherPartnerID);
            bool bNear = entry.paternitySecret.isSecretForBioFather && bioFatherID != 0 && isNear(bioFatherID);

            std::string childDesc = entry.name;
            std::string partnerRoleLabel = "presumed father of ";
            if (entry.stage == ChildStage::Unborn) {
                std::string bioName = !entry.paternitySecret.bioFatherName.empty() ? entry.paternitySecret.bioFatherName : ActorSvc::GetName(bioFatherID);
                childDesc = "unborn pregnancy" + (!bioName.empty() ? " fathered by " + bioName : "");
                partnerRoleLabel = "partner / presumed father of ";
            }

            if (pNear && bNear) {
                std::string pName = GetCandidateDisplayName(motherPartnerID);
                std::string bName = GetCandidateDisplayName(bioFatherID);
                if (!res.empty()) res += ", ";
                res += pName + " (" + partnerRoleLabel + childDesc + ")";
                res += ", " + bName + " (biological father of " + childDesc + ")";
                res += ", or Both [" + pName + " and " + bName + "] (regarding " + childDesc + ")";
            } else if (pNear) {
                std::string pName = GetCandidateDisplayName(motherPartnerID);
                if (!res.empty()) res += ", ";
                res += pName + " (" + partnerRoleLabel + childDesc + ")";
            } else if (bNear) {
                std::string bName = GetCandidateDisplayName(bioFatherID);
                if (!res.empty()) res += ", ";
                res += bName + " (biological father of " + childDesc + ")";
            }
        }
        return res;
    };

    auto getDiscoverChildPaternityCandidates = [](RE::Actor* a) -> std::string {
        if (!a) return "";
        const auto aFid = a->GetFormID();
        const auto& m = NPCRelationshipManager::GetSingleton();
        const auto& allChildren = m.GetAllChildren();
        if (allChildren.empty()) return "";

        const float r = GetNearbyActorsRadius();
        const auto nearbyActors = Utils::GetNearbyActors(a, r);
        if (nearbyActors.empty()) return "";

        auto isNear = [&](RE::FormID targetFid) -> bool {
            if (targetFid == 0) return false;
            for (RE::Actor* target : nearbyActors) {
                if (target && Utils::MatchesActor(targetFid, target->GetFormID())) return true;
            }
            return false;
        };

        std::string res;
        std::set<std::pair<RE::FormID, RE::FormID>> seenUnbornCandidates;
        for (const auto& [cID, entry] : allChildren) {
            if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;

            RE::FormID motherID = m.GetBioMother(cID);
            bool motherNear = (motherID != 0 && isNear(motherID));
            bool childNear = (cID != 0 && isNear(cID));

            // Father can discover/learn the truth if either the mother OR the child is nearby
            if (!motherNear && !childNear) continue;

            RE::FormID motherPartnerID = m.GetMotherPartner(cID);
            RE::FormID bioFatherID = m.GetBioFather(cID);
            if (motherPartnerID != 0 && bioFatherID != 0 && MatchesActor(bioFatherID, motherPartnerID)) continue;

            if (entry.stage == ChildStage::Unborn) {
                if (!seenUnbornCandidates.insert({ motherID, bioFatherID }).second) {
                    continue;
                }
            }

            std::string motherName = (motherID != 0) ? GetCandidateDisplayName(motherID) : "Unknown Mother";
            std::string childDesc = entry.name;
            if (entry.stage == ChildStage::Unborn) {
                std::string bioName = !entry.paternitySecret.bioFatherName.empty() ? entry.paternitySecret.bioFatherName : ActorSvc::GetName(bioFatherID);
                childDesc = "unborn pregnancy" + (!bioName.empty() ? " fathered by " + bioName : "");
            }

            if (MatchesActor(motherPartnerID, aFid) && entry.paternitySecret.isSecretForPartner) {
                if (!res.empty()) res += ", ";
                res += childDesc + " (child with " + motherName + (entry.stage == ChildStage::Unborn ? "; partner / presumed father" : "; presumed father");
                if (entry.stage != ChildStage::Unborn && entry.paternitySecret.hasPhenotypeMismatch) {
                    res += ", noticeable physical difference";
                }
                res += ")";
            } else if (MatchesActor(bioFatherID, aFid) && entry.paternitySecret.isSecretForBioFather) {
                if (!res.empty()) res += ", ";
                res += childDesc + " (child with " + motherName + "; biological father)";
            }
        }
        return res;
    };

    auto getAvailableRelationshipActions = [=](RE::Actor* actor) -> std::string {
        if (!actor) return "";
        const uint32_t actorFormId = actor->GetFormID();

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const float radius = GetNearbyActorsRadius();
        const auto nearby = Utils::GetNearbyActors(actor, radius);

        std::vector<std::string> actions;

        // Dating: Actor must be eligible and at least one nearby actor must be eligible to date
        if (mgr.IsAvailableForDating(actorFormId)) {
            bool hasDatingTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForDating(actorFormId, target->GetFormID())) {
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
        if (mgr.IsAvailableForLover(actorFormId) && !nearby.empty()) {
            actions.push_back("ProposeLover");
            actions.push_back("ConsiderLoverProposal");
        }

        // Engagement: Actor must be eligible and valid target nearby
        if (mgr.IsAvailableForEngaged(actorFormId)) {
            bool hasEngagedTarget = false;
            const auto datingPartners = mgr.GetPartners(actorFormId, RelationshipStatus::Dating);
            if (!datingPartners.empty()) {
                const auto datingPartnerFid = datingPartners[0];
                for (RE::Actor* target : nearby) {
                    if (target && Utils::MatchesActor(target->GetFormID(), datingPartnerFid) && mgr.IsAvailableForEngaged(actorFormId, datingPartnerFid)) {
                        hasEngagedTarget = true;
                        break;
                    }
                }
            } else {
                for (RE::Actor* target : nearby) {
                    if (target && mgr.IsAvailableForEngaged(actorFormId, target->GetFormID())) {
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
            bool hasBreakupDatingTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForBreakupDating(actorFormId, target->GetFormID())) {
                    hasBreakupDatingTarget = true;
                    break;
                }
            }
            if (hasBreakupDatingTarget) {
                actions.push_back("BreakupDating");
            }
        }
        if (mgr.IsAvailableForBreakupLover(actorFormId)) {
            bool hasBreakupLoverTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForBreakupLover(actorFormId, target->GetFormID())) {
                    hasBreakupLoverTarget = true;
                    break;
                }
            }
            if (hasBreakupLoverTarget) {
                actions.push_back("BreakupLover");
            }
        }
        if (mgr.IsAvailableForJilted(actorFormId)) {
            bool hasJiltedTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForJilted(actorFormId, target->GetFormID())) {
                    hasJiltedTarget = true;
                    break;
                }
            }
            if (hasJiltedTarget) {
                actions.push_back("CallOffEngagement");
            }
        }
        if (mgr.IsAvailableForDivorced(actorFormId)) {
            bool hasDivorceTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForDivorced(actorFormId, target->GetFormID())) {
                    hasDivorceTarget = true;
                    break;
                }
            }
            if (hasDivorceTarget) {
                actions.push_back("Divorce");
            }
        }
        if (mgr.IsAvailableForOpenRelationship(actorFormId)) {
            bool hasOpenTarget = false;
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForOpenRelationship(actorFormId, target->GetFormID())) {
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
            for (RE::Actor* target : nearby) {
                if (target && mgr.IsAvailableForCloseRelationship(actorFormId, target->GetFormID())) {
                    hasCloseTarget = true;
                    break;
                }
            }
            if (hasCloseTarget) {
                actions.push_back("CloseRelationship");
            }
        }

        // if (!getRevealChildSecretCandidates(actor).empty()) {
        //     actions.push_back("RevealChildSecret");
        // }
        // if (!getDiscoverChildPaternityCandidates(actor).empty()) {
        //     actions.push_back("DiscoverChildPaternity");
        // }
        // if (!getConfessAffairCandidates(actor).empty()) {
        //     actions.push_back("ConfessAffair");
        // }
        // if (!getExposeAffairCandidates(actor).empty()) {
        //     actions.push_back("ExposeAffair");
        // }
        // if (!getResolveAffairCandidates(actor).empty()) {
        //     actions.push_back("ResolveAffair");
        // }

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

namespace {
    std::string GetModelSlotDisplayName(std::string_view llmVariant) {
        if (llmVariant == "memory") {
            return "Memory";
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

    PrismaModalManager::GetSingleton().ShowModal(
        std::move(req),
        [onRetry = std::move(onRetry), onIgnore = std::move(onIgnore)](const ModalResponse& resp) mutable {
            if (!resp.aborted && (resp.buttonIndex == 0 || resp.buttonId == "retry")) {
                if (onRetry) onRetry();
            } else {
                if (onIgnore) onIgnore();
            }
        });
}

bool EvaluateRelationshipStatusChange(RE::FormID actorFormID, RE::FormID partnerFormID, const std::string& targetStatus, const std::string& evalId) {
    if (!IsSkyrimNetAvailable()) {
        MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: SkyrimNet is not available or simulated as disabled");
        return false;
    }

    HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
    if (!hMod) {
        MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: SkyrimNet.dll not found in process");
        return false;
    }

    auto fnSendLLM = reinterpret_cast<PublicSendCustomPromptToLLM_t>(GetProcAddress(hMod, "PublicSendCustomPromptToLLM"));
    if (!fnSendLLM) {
        MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: PublicSendCustomPromptToLLM export not found in SkyrimNet.dll");
        return false;
    }

    if (!ActorSvc::IsValid(actorFormID) || !ActorSvc::IsValid(partnerFormID)) {
        MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: invalid actor/partner FormIDs {:08X} / {:08X}", actorFormID, partnerFormID);
        return false;
    }

    std::string currentStatusStr(Utils::RelationshipStatusToString(NPCRelationshipManager::GetSingleton().GetRelationshipStatus(actorFormID, partnerFormID)));
    std::string actorName = ActorSvc::GetName(actorFormID);
    std::string partnerName = ActorSvc::GetName(partnerFormID);

    nlohmann::json ctx;
    ctx["actorName"] = actorName;
    ctx["actorFormId"] = fmt::format("0x{:08X}", actorFormID);
    ctx["partnerName"] = partnerName;
    ctx["partnerFormId"] = fmt::format("0x{:08X}", partnerFormID);
    ctx["currentStatus"] = currentStatusStr;
    ctx["targetStatus"] = targetStatus;
    if (!evalId.empty()) {
        ctx["evalId"] = evalId;
    }

    std::string contextJson = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::string promptName = "maras_evaluations/maras_evaluate_relationship_change";
    std::string llmVariant = "gamemaster_evaluation";

    auto callbackHolder = std::make_shared<std::function<void(const char*, int)>>();

    *callbackHolder = [callbackHolder, actorFormID, partnerFormID, actorName, partnerName, currentStatusStr, targetStatus, evalId, contextJson, promptName, llmVariant, fnSendLLM](const char* response, int success) {
        try {
            MARAS_LOG_INFO("[SkyrimNet] EvaluateRelationshipStatusChange callback: success={}", success);

            auto queueRetryModal = [&](const std::string& errorDetail, LLMFailureType failureType) {
                std::function<void(const char*, int)> savedCb = std::move(*callbackHolder);
                *callbackHolder = nullptr;

                auto retryAction = [callbackHolder, savedCb = std::move(savedCb), promptName, llmVariant, contextJson, fnSendLLM]() mutable {
                    MARAS_LOG_INFO("[SkyrimNet] EvaluateRelationshipStatusChange: retrying LLM request.");
                    *callbackHolder = std::move(savedCb);
                    if (*callbackHolder) {
                        if (!fnSendLLM(promptName.c_str(), llmVariant.c_str(), contextJson.c_str(), *callbackHolder)) {
                            *callbackHolder = nullptr;
                        }
                    }
                };

                auto ignoreAction = [callbackHolder]() {
                    MARAS_LOG_INFO("[SkyrimNet] EvaluateRelationshipStatusChange: player chose to ignore failed evaluation.");
                    *callbackHolder = nullptr;
                };

                std::string fullDescription = fmt::format(
                    "{} & {} ({} -> {})\n{}",
                    actorName, partnerName, currentStatusStr, targetStatus, errorDetail);

                ShowLLMRetryModal(
                    "Relationship Status Change",
                    fullDescription,
                    failureType,
                    std::move(retryAction),
                    std::move(ignoreAction),
                    llmVariant);
            };

            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: LLM returned failure or empty response.");
                queueRetryModal("HTTP / connection request failed or returned an empty response.", LLMFailureType::RequestFailed);
                return;
            }

            nlohmann::json resp = JsonService::ParseLLMJson(response);

            if (resp.is_discarded() || !resp.is_object()) {
                MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: LLM response was not valid JSON (even after sanitization and repair). Raw: {}", response);
                queueRetryModal("AI model returned broken or unreadable JSON.", LLMFailureType::InvalidJson);
                return;
            }

            bool allow = false;
            if (resp.contains("allow") && resp["allow"].is_boolean()) {
                allow = resp["allow"].get<bool>();
            } else if (resp.contains("start") && resp["start"].is_boolean()) {
                allow = resp["start"].get<bool>();
            }

            std::string reason = (resp.contains("reason") && resp["reason"].is_string())
                ? resp["reason"].get<std::string>()
                : "";

            *callbackHolder = nullptr;

            MARAS_LOG_INFO("[SkyrimNet] EvaluateRelationshipStatusChange result: allow={}, reason='{}'", allow, reason);

            if (allow) {
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([actorFormID, partnerFormID, targetStatus]() {
                        try {
                            NPCRelationshipManager::GetSingleton().PromoteToStatusWith(actorFormID, partnerFormID, Utils::StringToRelationshipStatus(targetStatus));
                            MARAS_LOG_INFO("[SkyrimNet] Promoted relationship between {:08X} and {:08X} to '{}' after LLM evaluation", actorFormID, partnerFormID, targetStatus);
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("[SkyrimNet] Exception in EvaluateRelationshipStatusChange game-thread task: {}", e.what());
                        }
                    });
                }
            } else {
                MARAS_LOG_INFO("[SkyrimNet] LLM evaluation declined status change to '{}' between {:08X} and {:08X}", targetStatus, actorFormID, partnerFormID);
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[SkyrimNet] Exception in EvaluateRelationshipStatusChange callback: {}", e.what());
            *callbackHolder = nullptr;
        } catch (...) {
            MARAS_LOG_ERROR("[SkyrimNet] Unknown exception in EvaluateRelationshipStatusChange callback");
            *callbackHolder = nullptr;
        }
    };

    bool queued = fnSendLLM(promptName.c_str(), llmVariant.c_str(), contextJson.c_str(), *callbackHolder);
    if (!queued) {
        *callbackHolder = nullptr;
        MARAS_LOG_WARN("[SkyrimNet] EvaluateRelationshipStatusChange: fnSendLLM returned false (immediate failure)");
        return false;
    }

    return true;
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

using PublicAddMemory_t = int (*)(
    uint32_t formId, const char* contentText, float importance,
    const char* memoryType, const char* emotion, const char* location,
    const char* tagsJSON, const char* relatedActorsJSON);

bool AddActorMemory(RE::FormID actorFormID, const std::string& content, float importance,
                    const std::string& memoryType, const std::string& emotion, const std::string& location,
                    const std::vector<std::string>& tags, RE::FormID relatedActorFormID) {
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
    if (relatedActorFormID != 0) {
        relatedJson.push_back(relatedActorFormID);
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

    bool EvaluatePaternitySecret(RE::FormID childID) {
        if (!IsSkyrimNetAvailable()) return false;

        auto& mgr = NPCRelationshipManager::GetSingleton();
        auto entryOpt = mgr.GetChildEntry(childID);
        if (!entryOpt.has_value()) return false;

        const auto& entry = *entryOpt;
        RE::FormID motherID = mgr.GetBioMother(childID);
        RE::FormID presumedFatherID = mgr.GetMotherPartner(childID);
        RE::FormID bioFatherID = mgr.GetBioFather(childID);

        if (motherID == 0 || presumedFatherID == 0 || bioFatherID == 0) return false;
        if (MatchesActor(presumedFatherID, bioFatherID)) return false;

        auto calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;
        float childAgeDays = currentDay - entry.birthTimestamp;
        bool isAtBirth = (childAgeDays < 0.5f);

        bool hasVisualDiscrepancy = entry.paternitySecret.hasPhenotypeMismatch;
        std::string visualDesc = hasVisualDiscrepancy ? mgr.GetVisualDiscrepancyDescription(motherID, presumedFatherID, bioFatherID) : "";

        uint64_t motherUUID = GetActorUUID(motherID);
        uint64_t presumedFatherUUID = GetActorUUID(presumedFatherID);
        uint64_t bioFatherUUID = GetActorUUID(bioFatherID);

        nlohmann::json ctx;
        ctx["childName"] = entry.name.empty() ? ActorSvc::GetName(childID) : entry.name;
        int childGenderVal = mgr.GetChildGender(childID);
        std::string childGenderStr = (childGenderVal == 2) ? "unknown" : ((childGenderVal == 1) ? "female" : "male");
        std::string childRelationStr = (childGenderVal == 2) ? "child" : ((childGenderVal == 1) ? "daughter" : "son");
        ctx["gender"] = childGenderStr;
        ctx["childGender"] = childGenderStr;
        ctx["childRelation"] = childRelationStr;
        ctx["childAgeDays"] = static_cast<int>(std::round(std::max(0.0f, childAgeDays)));
        ctx["isAtBirth"] = isAtBirth;

        ctx["motherName"] = ActorSvc::GetName(motherID);
        ctx["motherRace"] = ActorSvc::GetRaceName(motherID);
        ctx["motherUUID"] = motherUUID;

        ctx["presumedFatherName"] = ActorSvc::GetName(presumedFatherID);
        ctx["presumedFatherRace"] = ActorSvc::GetRaceName(presumedFatherID);
        ctx["presumedFatherUUID"] = presumedFatherUUID;
        ctx["maritalStatus"] = std::string(Utils::RelationshipStatusToString(mgr.GetRelationshipStatus(motherID, presumedFatherID)));

        ctx["bioFatherName"] = ActorSvc::GetName(bioFatherID);
        ctx["bioFatherRace"] = ActorSvc::GetRaceName(bioFatherID);
        ctx["bioFatherUUID"] = bioFatherUUID;
        ctx["affairStatus"] = std::string(Utils::AffairStatusToString(mgr.GetCheatingAffairStatus(motherID, presumedFatherID, bioFatherID)));
        ctx["isSecretForPresumedFather"] = entry.paternitySecret.isSecretForPartner;
        ctx["isSecretForPartner"] = entry.paternitySecret.isSecretForPartner;
        ctx["isSecretForBioFather"] = entry.paternitySecret.isSecretForBioFather;

        ctx["hasVisualDiscrepancy"] = hasVisualDiscrepancy;
        ctx["visualDiscrepancyDescription"] = visualDesc;

        std::string childName = ctx["childName"].get<std::string>();
        std::string motherName = ctx["motherName"].get<std::string>();
        std::string contextJson = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::string promptName = "maras_evaluations/maras_paternity_evaluation";

        auto callbackHolder = std::make_shared<std::function<void(const char*, int)>>();

        *callbackHolder = [callbackHolder, childID, motherID, presumedFatherID, bioFatherID, childName, motherName, promptName, contextJson](const char* response, int success) {
            try {
                auto queueRetryModal = [&](const std::string& errorDetail, LLMFailureType failureType) {
                    std::function<void(const char*, int)> savedCb = std::move(*callbackHolder);
                    *callbackHolder = nullptr;

                    auto retryAction = [callbackHolder, savedCb = std::move(savedCb), promptName, contextJson, childID]() mutable {
                        MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: retrying LLM request for child {:08X}.", childID);
                        *callbackHolder = std::move(savedCb);
                        if (*callbackHolder) {
                            if (!SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, *callbackHolder)) {
                                *callbackHolder = nullptr;
                                PrismaUIDataBridge::GetSingleton().SendOverviewData();
                                PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
                            }
                        }
                    };

                    auto ignoreAction = [callbackHolder, childID]() {
                        MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret: player chose to ignore failed evaluation for child {:08X}.", childID);
                        *callbackHolder = nullptr;
                        PrismaUIDataBridge::GetSingleton().SendOverviewData();
                        PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
                    };

                    std::string fullDescription = fmt::format(
                        "Child: {} (Mother: {})\n{}",
                        childName, motherName, errorDetail);

                    ShowLLMRetryModal(
                        "Paternity Secret Evaluation",
                        fullDescription,
                        failureType,
                        std::move(retryAction),
                        std::move(ignoreAction),
                        "gamemaster_evaluation");
                };

                if (!success || !response || response[0] == '\0') {
                    MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: LLM request failed or returned empty response.");
                    queueRetryModal("HTTP / connection request failed or returned an empty response.", LLMFailureType::RequestFailed);
                    return;
                }

                nlohmann::json resp = JsonService::ParseLLMJson(response);
                if (resp.is_discarded() || !resp.is_object()) {
                    MARAS_LOG_WARN("[SkyrimNet] EvaluatePaternitySecret: Failed to parse LLM response: {}", response);
                    queueRetryModal("AI model returned broken or unreadable JSON.", LLMFailureType::InvalidJson);
                    return;
                }

                *callbackHolder = nullptr;

                auto getStr = [&](const char* key, const char* defaultVal) -> std::string {
                    return (resp.contains(key) && resp[key].is_string()) ? resp[key].get<std::string>() : defaultVal;
                };
                auto getBool = [&](const char* key, bool defaultVal) -> bool {
                    return (resp.contains(key) && resp[key].is_boolean()) ? resp[key].get<bool>() : defaultVal;
                };

                std::string outcome = getStr("outcome", "");
                if (outcome.empty()) {
                    outcome = getStr("event", "remains_secret");
                }

                // Normalize legacy prompt values if returned
                if (outcome == "confession") {
                    outcome = "mother_confessed_presumed";
                } else if (outcome == "discovery") {
                    outcome = "presumed_father_learnt";
                } else if (outcome == "remains_secret" || outcome == "none") {
                    bool bioInformed = getBool("bio_father_informed", false) || getBool("bio_father_aware", false);
                    outcome = bioInformed ? "bio_father_learnt" : "none";
                }

                std::string partnerReaction = getStr("partner_reaction", "none");
                std::string summary = getStr("summary", "");

                MARAS_LOG_INFO("[SkyrimNet] EvaluatePaternitySecret result: outcome='{}', reaction='{}'",
                             outcome, partnerReaction);

                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([childID, motherID, presumedFatherID, bioFatherID, outcome, partnerReaction, summary]() {
                        try {
                            auto& mgr = NPCRelationshipManager::GetSingleton();

                            // 1. Resolve state based on the specific canonical outcome
                            if (outcome == "mother_confessed_presumed" || outcome == "presumed_father_learnt") {
                                mgr.ResolvePaternitySecretForPartner(childID, outcome == "presumed_father_learnt", /*sendEvent=*/false);
                                if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                    mgr.Breakup(motherID, presumedFatherID);
                                }
                            } else if (outcome == "mother_confessed_bio" || outcome == "bio_father_learnt") {
                                mgr.ResolvePaternitySecretForBioFather(childID, outcome == "bio_father_learnt", /*sendEvent=*/false);
                            } else if (outcome == "mother_confessed_both") {
                                mgr.ResolvePaternitySecretForPartner(childID, false, /*sendEvent=*/false);
                                mgr.ResolvePaternitySecretForBioFather(childID, false, /*sendEvent=*/false);
                                if (partnerReaction == "divorce" || partnerReaction == "breakup") {
                                    mgr.Breakup(motherID, presumedFatherID);
                                }
                            }

                            // 2. Dispatch SkyrimNet mod event for the exact outcome if one occurred
                            if (outcome != "none" && outcome != "remains_secret" && !outcome.empty()) {
                                if (auto* source = SKSE::GetModCallbackEventSource()) {
                                    float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", outcome, childIdFloat, nullptr);
                                    source->SendEvent(&modEvent);
                                }
                            }

                            // 3. Notify Prisma UI
                            PrismaUIDataBridge::GetSingleton().SendOverviewData();
                            PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("[SkyrimNet] Exception in EvaluatePaternitySecret game-thread task: {}", e.what());
                        }
                    });
                }
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNet] Exception in EvaluatePaternitySecret callback: {}", e.what());
                *callbackHolder = nullptr;
            } catch (...) {
                MARAS_LOG_ERROR("[SkyrimNet] Unknown exception in EvaluatePaternitySecret callback");
                *callbackHolder = nullptr;
            }
        };

        bool queued = SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, *callbackHolder);
        if (!queued) {
            *callbackHolder = nullptr;
            return false;
        }
        return true;
    }

    uint64_t GetActorUUID(RE::FormID formID) {
        return FormIDToUUID(formID);
    }

}  // namespace MARAS
