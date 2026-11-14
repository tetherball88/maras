#include "maras/app/AgencyWorkflowCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/app/AsyncLlmBatchRunner.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/agency/BanterPlanner.h"
#include "maras/domain/agency/EncounterTemplate.h"
#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/domain/agency/NpcMatchmaker.h"
#include "maras/domain/agency/PartnerAgencyEvaluator.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BanterSceneStarter.h"
#include "maras/engine/EncounterConfigLoader.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/JsonService.h"
#include "maras/engine/SkseSerializationCallbacks.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace MARAS::App {

    namespace {

        constexpr float kBanterCoupleCooldownMultiplier = 2.0f;

        [[nodiscard]] float GetCurrentGameDaysSafe() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

        [[nodiscard]] int GetCurrentHourOfDaySafe() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return static_cast<int>(std::floor(calendar->GetHour()));
            }
            return 12;
        }

        [[nodiscard]] std::string FormatFormId8Hex(RE::FormID id) {
            return std::format("{:08X}", id);
        }

        [[nodiscard]] std::string GetPlayerNameSafe() {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetName(); name && name[0] != '\0') {
                    return name;
                }
            }
            return "Dragonborn";
        }

        [[nodiscard]] std::string ResolveActorName(RE::FormID id) {
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

        [[nodiscard]] std::string ResolveActorRaceName(RE::FormID id) {
            if (auto* race = ActorSvc::GetRace(id)) {
                if (const char* fullName = race->GetFullName(); fullName && fullName[0] != '\0') {
                    return fullName;
                }
                if (const char* name = race->GetName(); name && name[0] != '\0') {
                    return name;
                }
            }
            return "Unknown";
        }

        [[nodiscard]] std::string SexToDisplayString(Foundation::ActorSex sex) noexcept {
            switch (sex) {
                case Foundation::ActorSex::Male:
                    return "Male";
                case Foundation::ActorSex::Female:
                    return "Female";
                default:
                    return "Unknown";
            }
        }

        [[nodiscard]] Foundation::ActorSex ResolveActorSexEnum(RE::FormID id) noexcept {
            const std::uint8_t rawSex = ActorSvc::GetSex(id);
            if (rawSex == 0) {
                return Foundation::ActorSex::Male;
            }
            if (rawSex == 1) {
                return Foundation::ActorSex::Female;
            }
            return Foundation::ActorSex::Unknown;
        }

        [[nodiscard]] std::string FormatRelationshipsSummary(const State::WorldStateSnapshot& snap,
                                                             Foundation::ActorId actorId) {
            if (!snap.relationships || !snap.affairs) {
                return "None";
            }
            std::string summary;
            const auto active = snap.relationships->GetActivePartners(actorId);
            for (const auto& [partnerId, status] : active) {
                if (!summary.empty()) {
                    summary += "\n";
                }
                const bool isOpen = snap.relationships->IsPairOpen(actorId, partnerId);
                summary += std::format("- {} ({}{})",
                                       ResolveActorName(partnerId.raw()),
                                       Foundation::RelationshipStatusToString(status),
                                       isOpen ? ", Open" : "");
            }
            for (const auto& rec : snap.affairs->GetByCheater(actorId)) {
                if (rec.affairStatus != Foundation::AffairStatus::Ongoing) {
                    continue;
                }
                if (!summary.empty()) {
                    summary += "\n";
                }
                summary += std::format("- {} (Affair with, stage={}, secret={})",
                                       ResolveActorName(rec.paramour.raw()),
                                       Foundation::AffairStageToString(rec.affairStage),
                                       rec.isSecret ? "yes" : "no");
            }
            return summary.empty() ? std::string("Single / None") : summary;
        }

        [[nodiscard]] bool IsNpcEligibleBasicEngineCheck(RE::FormID id,
                                                         const State::WorldStateSnapshot& snap) {
            if (id == 0 || id == kPlayerFormID || IsSyntheticChildID(id)) {
                return false;
            }
            const auto* profile = snap.actors ? snap.actors->Find(Foundation::RefId{ id }) : nullptr;
            if (!profile || !profile->isRegisteredAdult) {
                return false;
            }
            if (snap.deceased && snap.deceased->IsDeceased(Foundation::ActorId{ id }, profile->baseId)) {
                return false;
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                if (actor->IsDead() || actor->IsDeleted() || actor->IsChild()) {
                    return false;
                }
            } else if (!ActorSvc::IsValid(id)) {
                return false;
            }
            return true;
        }

        [[nodiscard]] std::vector<Domain::Agency::NpcMatchmakingProfile> CollectEligibleMatchmakingProfiles(
            const State::WorldStateSnapshot& snap,
            float currentGameDay,
            bool requireEligibleForScheduled) {
            std::vector<Domain::Agency::NpcMatchmakingProfile> profiles;
            if (!snap.actors) {
                return profiles;
            }

            const auto& allProfiles = snap.actors->AllProfiles();
            profiles.reserve(allProfiles.size());
            for (const auto& [refId, rec] : allProfiles) {
                const Foundation::ActorId actorId{ refId.raw() };
                if (!IsNpcEligibleBasicEngineCheck(actorId.raw(), snap)) {
                    continue;
                }
                if (requireEligibleForScheduled &&
                    !Domain::Agency::IsEligibleForScheduledMatchmaking(snap, actorId, currentGameDay)) {
                    continue;
                }
                Domain::Agency::NpcMatchmakingProfile p{};
                p.actorId = actorId;
                p.name = ResolveActorName(actorId.raw());
                if (p.name.empty()) {
                    continue;
                }
                p.sex = ResolveActorSexEnum(actorId.raw());
                p.socialClass = rec.socialClass;
                p.skillType = rec.skillType;
                p.temperament = rec.temperament;
                profiles.push_back(std::move(p));
            }
            return profiles;
        }

        [[nodiscard]] std::string ResolvePrimaryPartnerName(const State::WorldStateSnapshot& snap,
                                                            Foundation::ActorId actorId,
                                                            Foundation::ActorId& outId) {
            outId = snap.relationships ? snap.relationships->GetPrimaryPartner(actorId) : Foundation::ActorId{};
            if (outId.IsNull()) {
                return {};
            }
            return ResolveActorName(outId.raw());
        }

        [[nodiscard]] std::string ActionKindToString(Domain::Agency::MatchmakingActionKind kind) noexcept {
            switch (kind) {
                case Domain::Agency::MatchmakingActionKind::New:
                    return "new";
                case Domain::Agency::MatchmakingActionKind::Advance:
                    return "advance";
                case Domain::Agency::MatchmakingActionKind::Breakup:
                    return "breakup";
                case Domain::Agency::MatchmakingActionKind::OpenClose:
                    return "open_close";
            }
            return "new";
        }

        [[nodiscard]] std::string NormalizeActionString(std::string_view raw) {
            const std::string lower = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(raw));
            if (lower == "openclose" || lower == "open_close" || lower == "open/close" || lower == "toggle_open") {
                return "open_close";
            }
            if (lower == "new" || lower == "advance" || lower == "breakup") {
                return lower;
            }
            return lower;
        }

        [[nodiscard]] std::string BuildAllowedActionsText(const State::SettingsData& settings) {
            std::string out = "new|advance";
            if (settings.matchmakingAllowBreakups) {
                out += "|breakup";
            }
            if (settings.matchmakingAllowOpenClose) {
                out += "|open_close";
            }
            return out;
        }

        [[nodiscard]] std::string BuildAllowedStatusesText(const State::SettingsData& settings) {
            std::string out = "dating|lover|engaged|married";
            if (settings.matchmakingAllowBreakups) {
                out += "|exdating|exlover|divorced|jilted";
            }
            return out;
        }

        [[nodiscard]] nlohmann::json BuildRosterJsonArray(
            const std::vector<Domain::Agency::NpcMatchmakingProfile>& pool,
            const State::WorldStateSnapshot& snap,
            Foundation::ActorId excludeId = Foundation::ActorId{}) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : pool) {
                if (!excludeId.IsNull() && p.actorId == excludeId) {
                    continue;
                }
                const auto active = snap.relationships ? snap.relationships->GetActivePartners(p.actorId)
                                                       : std::vector<std::pair<Foundation::ActorId, Foundation::RelationshipStatus>>{};
                const bool hasMainPartner = !snap.relationships->GetPrimaryPartner(p.actorId).IsNull();
                const bool hasOpenMain = snap.relationships->HasAnyOpenMainPair(p.actorId);
                const bool canFormMain = !hasMainPartner;

                nlohmann::json entry;
                entry["name"] = p.name;
                entry["formId"] = FormatFormId8Hex(p.actorId.raw());
                entry["sex"] = SexToDisplayString(p.sex);
                entry["race"] = ResolveActorRaceName(p.actorId.raw());
                entry["socialClass"] = std::string(Foundation::SocialClassToString(p.socialClass));
                entry["skillType"] = std::string(Foundation::SkillTypeToString(p.skillType));
                entry["temperament"] = std::string(Foundation::TemperamentToString(p.temperament));
                entry["eligibility"] = std::string(Domain::Agency::GetEligibilityCategoryString(canFormMain, hasOpenMain));
                entry["existingPartnersCount"] = active.size();
                arr.push_back(std::move(entry));
            }
            return arr;
        }

        [[nodiscard]] nlohmann::json BuildVerifyContextJson(const MatchmakingResult& cand,
                                                            const State::WorldStateSnapshot& snap,
                                                            float currentGameDay) {
            const Foundation::ActorId idA{ cand.actorAFormID };
            const Foundation::ActorId idB{ cand.actorBFormID };
            Foundation::ActorId committedAId{};
            Foundation::ActorId committedBId{};
            const std::string committedAName = ResolvePrimaryPartnerName(snap, idA, committedAId);
            const std::string committedBName = ResolvePrimaryPartnerName(snap, idB, committedBId);

            auto& skyrimNet = GetSkyrimNetGateway();
            auto appendUniqueUuid = [](nlohmann::json& arr, std::uint64_t uuid) {
                if (uuid == 0) {
                    return;
                }
                for (const auto& existing : arr) {
                    if (existing.is_number_unsigned() && existing.get<std::uint64_t>() == uuid) {
                        return;
                    }
                }
                arr.push_back(uuid);
            };

            const std::uint64_t uuidA = skyrimNet.GetActorUUID(cand.actorAFormID);
            const std::uint64_t uuidB = skyrimNet.GetActorUUID(cand.actorBFormID);
            const std::uint64_t committedAUuid = committedAId.IsNull() ? 0 : skyrimNet.GetActorUUID(committedAId.raw());
            const std::uint64_t committedBUuid = committedBId.IsNull() ? 0 : skyrimNet.GetActorUUID(committedBId.raw());

            nlohmann::json ctx;
            ctx["gameDay"] = currentGameDay;
            ctx["actorAName"] = cand.actorAName;
            ctx["actorAFormID"] = cand.actorAFormID;
            ctx["actorAUUID"] = uuidA;
            ctx["actorARace"] = ResolveActorRaceName(cand.actorAFormID);
            ctx["actorASex"] = SexToDisplayString(ResolveActorSexEnum(cand.actorAFormID));
            ctx["actorARelationships"] = FormatRelationshipsSummary(snap, idA);

            ctx["actorBName"] = cand.actorBName;
            ctx["actorBFormID"] = cand.actorBFormID;
            ctx["actorBUUID"] = uuidB;
            ctx["actorBRace"] = ResolveActorRaceName(cand.actorBFormID);
            ctx["actorBSex"] = SexToDisplayString(ResolveActorSexEnum(cand.actorBFormID));
            ctx["actorBRelationships"] = FormatRelationshipsSummary(snap, idB);

            ctx["committedPartnerAName"] = committedAName;
            ctx["committedPartnerAUUID"] = committedAUuid;
            ctx["committedPartnerBName"] = committedBName;
            ctx["committedPartnerBUUID"] = committedBUuid;

            ctx["action"] = cand.action;
            ctx["targetStatus"] = cand.targetStatus;
            ctx["isCheating"] = cand.isCheating;
            nlohmann::json recentParticipants = nlohmann::json::array();
            appendUniqueUuid(recentParticipants, uuidA);
            appendUniqueUuid(recentParticipants, uuidB);
            appendUniqueUuid(recentParticipants, committedAUuid);
            appendUniqueUuid(recentParticipants, committedBUuid);
            ctx["recent_events_participants"] = std::move(recentParticipants);
            return ctx;
        }

        [[nodiscard]] nlohmann::json BuildMemoryBatchContextJson(const std::vector<MatchmakingResult>& matches,
                                                                 const State::WorldStateSnapshot& snap,
                                                                 float currentGameDay) {
            auto& skyrimNet = GetSkyrimNetGateway();
            nlohmann::json ctx;
            ctx["gameDay"] = currentGameDay;
            ctx["participants"] = nlohmann::json::array();
            ctx["events"] = nlohmann::json::array();
            ctx["targets"] = nlohmann::json::array();
            ctx["recent_events_participants"] = nlohmann::json::array();

            std::unordered_set<RE::FormID> seenParticipants;
            std::unordered_set<std::uint64_t> seenUuids;
            std::size_t evIndex = 1;

            auto ensureParticipant = [&](RE::FormID formId, const std::string& displayName) {
                if (formId == 0 || !seenParticipants.insert(formId).second) {
                    return;
                }
                const std::uint64_t uuid = skyrimNet.GetActorUUID(formId);
                if (uuid != 0 && seenUuids.insert(uuid).second) {
                    ctx["recent_events_participants"].push_back(uuid);
                }
                nlohmann::json p;
                p["name"] = displayName.empty() ? ResolveActorName(formId) : displayName;
                p["uuid"] = uuid;
                p["race"] = ResolveActorRaceName(formId);
                p["sex"] = SexToDisplayString(ResolveActorSexEnum(formId));
                p["relationships"] = FormatRelationshipsSummary(snap, Foundation::ActorId{ formId });
                ctx["participants"].push_back(std::move(p));

                nlohmann::json target;
                target["name"] = displayName.empty() ? ResolveActorName(formId) : displayName;
                target["formId"] = FormatFormId8Hex(formId);
                target["roleSummary"] = "Participant in relationship update";
                target["perspectiveNotes"] = nlohmann::json::array({ "Directly experienced the relationship event." });
                target["knowledgeRestrictions"] = nlohmann::json::array();
                ctx["targets"].push_back(std::move(target));
            };

            for (const auto& m : matches) {
                if (m.skipped) {
                    continue;
                }
                ensureParticipant(m.actorAFormID, m.actorAName);
                ensureParticipant(m.actorBFormID, m.actorBName);

                nlohmann::json ev;
                ev["index"] = evIndex++;
                ev["category"] = "Relationship";
                ev["type"] = m.action;
                ev["location"] = "Skyrim";
                ev["involvedNames"] = std::format("{}, {}", m.actorAName, m.actorBName);
                ev["headline"] = std::format("{} and {} {} -> {}{}",
                                             m.actorAName,
                                             m.actorBName,
                                             m.action,
                                             m.targetStatus,
                                             m.isCheating ? " (secret infidelity)" : "");
                ctx["events"].push_back(std::move(ev));
            }

            return ctx;
        }

        [[nodiscard]] ModalTone ParseModalTone(std::string_view tone) noexcept {
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "warning")) {
                return ModalTone::Warning;
            }
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "danger")) {
                return ModalTone::Danger;
            }
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "success")) {
                return ModalTone::Success;
            }
            if (Foundation::StringUtils::CaseInsensitiveEquals(tone, "ai")) {
                return ModalTone::Ai;
            }
            return ModalTone::Default;
        }

        [[nodiscard]] ModalSize ParseModalSize(std::string_view size) noexcept {
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "sm")) {
                return ModalSize::Sm;
            }
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "lg")) {
                return ModalSize::Lg;
            }
            if (Foundation::StringUtils::CaseInsensitiveEquals(size, "xl")) {
                return ModalSize::Xl;
            }
            return ModalSize::Md;
        }

        [[nodiscard]] std::string ResolveHouseDisplayName(RE::FormID houseId) {
            if (houseId == 0) {
                return "Home";
            }
            auto name = HouseholdEngineOps::GetSingleton().GetHouseName(houseId);
            if (!name.empty()) {
                return name;
            }
            if (auto* form = RE::TESForm::LookupByID(houseId)) {
                if (const char* n = form->GetName(); n && n[0] != '\0') {
                    return n;
                }
            }
            return "Home";
        }

        struct TavernRendezvousTarget {
            RE::FormID innLocationID{ 0 };
            RE::FormID centerMarkerID{ 0 };
            std::string displayName;
            bool found{ false };
        };

        [[nodiscard]] RE::BGSLocation* FindHoldRootInChain(RE::BGSLocation* startLoc) {
            if (!startLoc) {
                return nullptr;
            }
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

        [[nodiscard]] TavernRendezvousTarget FindNearbyTavernForRendezvous(RE::BGSLocation* referenceLoc) {
            TavernRendezvousTarget target;
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            auto& fc = FormCache::GetSingleton();
            auto* kwInn = fc.GetLocTypeInnKeyword();
            auto* centerLcrt = fc.GetLocationCenterMarkerRefType();
            if (!dataHandler || !kwInn) {
                return target;
            }

            RE::BGSLocation* targetHold = FindHoldRootInChain(referenceLoc);
            std::vector<TavernRendezvousTarget> sameHoldTaverns;
            std::vector<TavernRendezvousTarget> anyTaverns;
            std::unordered_set<RE::FormID> seenLocs;

            for (auto* cell : dataHandler->interiorCells) {
                if (!cell) {
                    continue;
                }
                auto* loc = cell->GetLocation();
                if (!loc || !loc->HasKeyword(kwInn)) {
                    continue;
                }
                const RE::FormID locId = loc->GetFormID();
                if (locId == 0 || !seenLocs.insert(locId).second) {
                    continue;
                }

                RE::FormID centerMarkerId = 0;
                RE::FormID fallbackMarkerId = 0;
                cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                    if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    if (centerLcrt) {
                        if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                            if (lrt->locRefType == centerLcrt) {
                                centerMarkerId = ref->GetFormID();
                                return RE::BSContainer::ForEachResult::kStop;
                            }
                        }
                    }
                    if (fallbackMarkerId == 0 && ref->IsPersistent()) {
                        fallbackMarkerId = ref->GetFormID();
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });

                const RE::FormID chosenMarker = centerMarkerId != 0 ? centerMarkerId : fallbackMarkerId;
                if (chosenMarker == 0) {
                    continue;
                }

                TavernRendezvousTarget cand;
                cand.innLocationID = locId;
                cand.centerMarkerID = chosenMarker;
                if (const char* locName = loc->GetName(); locName && locName[0] != '\0') {
                    cand.displayName = locName;
                } else if (const char* cellName = cell->GetName(); cellName && cellName[0] != '\0') {
                    cand.displayName = cellName;
                } else {
                    cand.displayName = "Local Inn";
                }
                cand.found = true;

                if (targetHold && FindHoldRootInChain(loc) == targetHold) {
                    sameHoldTaverns.push_back(cand);
                }
                anyTaverns.push_back(std::move(cand));
            }

            const auto& pool = !sameHoldTaverns.empty() ? sameHoldTaverns : anyTaverns;
            if (pool.empty()) {
                return target;
            }
            Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));
            const auto idx = static_cast<std::size_t>(rng.NextInt(0, static_cast<std::int32_t>(pool.size() - 1)));
            return pool[idx];
        }

        [[nodiscard]] std::string ResolveHomeArrivalScenarioId(const Domain::Agency::HomeArrivalPlan& plan,
                                                               bool isLetterScenario) {
            using Domain::Agency::HomeArrivalKind;
            using Foundation::PartnerIntentType;
            switch (plan.kind) {
                case HomeArrivalKind::PendingIntentForcegreet:
                    switch (plan.intentType) {
                        case PartnerIntentType::SuggestOpen:
                            return "intent_suggest_open";
                        case PartnerIntentType::SuggestClose:
                            return "intent_suggest_close";
                        case PartnerIntentType::ConfessTalking:
                            return "intent_confess_talking";
                        case PartnerIntentType::ConfessPhysical:
                            return "intent_confess_physical";
                        default:
                            return "intent_suggest_open";
                    }
                case HomeArrivalKind::OpenDateInHouse:
                    return isLetterScenario ? "open_date_away" : "open_in_house";
                case HomeArrivalKind::OpenDateLetter:
                    return isLetterScenario ? "open_date_away" : "open_in_house";
                case HomeArrivalKind::CheatingTalkingLetter:
                    return isLetterScenario ? "cheating_talking_letter" : "cheating_talking_in_house";
                case HomeArrivalKind::CheatingPhysicalLetter:
                    return isLetterScenario ? "cheating_physical_letter" : "cheating_physical_in_house";
                default:
                    return {};
            }
        }

        [[nodiscard]] Domain::Agency::RenderedEncounterModal SynthesizeMinimalEncounterFallback(
            std::string_view templateId,
            std::string_view partnerName,
            std::string_view playerName,
            std::string_view locationName) {
            Domain::Agency::RenderedEncounterModal modal{};
            modal.modalTone = (templateId == "open_date_away") ? "ai" : "danger";
            modal.modalSize = "md";
            modal.defaultCancelButtonIndex = 0;
            modal.modalTitle = std::format("A Note from {}", partnerName);
            modal.modalSubtitle = std::string(locationName);
            modal.envelopeMessage = std::format("You find an unsigned note left behind for {}, mentioning {}.",
                                                playerName, locationName);
            modal.infoBoxId = "partner_note";
            modal.infoBoxTitle = "Parchment Contents";
            modal.letterBody = std::format("{} stepped out to meet someone at {}. - {}",
                                           partnerName, locationName, partnerName);
            modal.buttonId = "close";
            modal.buttonLabel = "Put Note Away";
            return modal;
        }

        [[nodiscard]] std::optional<MatchmakingResult> JsonEntryToCandidate(
            const nlohmann::json& entry,
            const std::unordered_map<std::string, RE::FormID>& nameLookup,
            RE::FormID forcedActorAId = 0) {
            if (!entry.is_object()) {
                return std::nullopt;
            }

            RE::FormID idA = forcedActorAId;
            std::string nameA;
            if (idA == 0 && entry.contains("actorA") && entry["actorA"].is_string()) {
                nameA = entry["actorA"].get<std::string>();
                const auto it = nameLookup.find(Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(nameA)));
                if (it != nameLookup.end()) {
                    idA = it->second;
                }
            } else if (idA != 0) {
                nameA = ResolveActorName(idA);
            }

            RE::FormID idB = 0;
            std::string nameB;
            if (entry.contains("actorB") && entry["actorB"].is_string()) {
                nameB = entry["actorB"].get<std::string>();
            } else if (entry.contains("targetMatch") && entry["targetMatch"].is_string()) {
                nameB = entry["targetMatch"].get<std::string>();
            }
            if (!nameB.empty()) {
                const auto it = nameLookup.find(Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(nameB)));
                if (it != nameLookup.end()) {
                    idB = it->second;
                }
            }

            if (idA == 0 || idB == 0 || idA == idB) {
                return std::nullopt;
            }

            MatchmakingResult res{};
            res.actorAFormID = idA;
            res.actorBFormID = idB;
            res.actorAName = nameA.empty() ? ResolveActorName(idA) : nameA;
            res.actorBName = nameB.empty() ? ResolveActorName(idB) : nameB;
            if (entry.contains("action") && entry["action"].is_string()) {
                res.action = NormalizeActionString(entry["action"].get<std::string>());
            } else {
                res.action = "new";
            }
            if (entry.contains("targetStatus") && entry["targetStatus"].is_string()) {
                res.targetStatus = Foundation::StringUtils::ToLowerStd(
                    Foundation::StringUtils::TrimView(entry["targetStatus"].get<std::string>()));
            }
            if (entry.contains("cheating") && entry["cheating"].is_boolean()) {
                res.isCheating = entry["cheating"].get<bool>();
            }
            if (res.action == "open_close" && res.targetStatus.empty()) {
                if (entry.contains("openTarget") && entry["openTarget"].is_boolean()) {
                    res.targetStatus = entry["openTarget"].get<bool>() ? "open" : "closed";
                } else {
                    res.targetStatus = "open";
                }
            }
            return res;
        }

        [[nodiscard]] std::unordered_map<std::string, RE::FormID> BuildLowerNameMap(
            const std::vector<Domain::Agency::NpcMatchmakingProfile>& profiles) {
            std::unordered_map<std::string, RE::FormID> out;
            out.reserve(profiles.size());
            for (const auto& p : profiles) {
                if (!p.name.empty()) {
                    out.emplace(Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(p.name)),
                                p.actorId.raw());
                }
            }
            return out;
        }

        [[nodiscard]] std::vector<Foundation::ActorId> CollectShortlistedParamours(
            const State::WorldStateSnapshot& snap,
            Foundation::ActorId partnerId,
            std::size_t maxCandidates) {
            std::vector<Foundation::ActorId> out;
            if (!snap.actors || !snap.relationships || !snap.settings) {
                return out;
            }
            const Foundation::ActorSex partnerSex = ResolveActorSexEnum(partnerId.raw());
            const auto orientation = snap.settings->matchmakingOrientation;

            for (const auto& [refId, rec] : snap.actors->AllProfiles()) {
                const Foundation::ActorId candId{ refId.raw() };
                if (candId == partnerId || candId.IsPlayer() || !rec.isRegisteredAdult) {
                    continue;
                }
                if (!IsNpcEligibleBasicEngineCheck(candId.raw(), snap)) {
                    continue;
                }
                if (!Domain::Agency::IsOrientationCompatible(orientation, partnerSex, ResolveActorSexEnum(candId.raw()))) {
                    continue;
                }
                const auto existingToPartner = snap.relationships->GetStatus(partnerId, candId);
                if (Foundation::IsActivePartnerStatus(existingToPartner)) {
                    continue;
                }
                if (RelationshipUseCases::GetSingleton().AreFamilyMembers(partnerId.raw(), candId.raw())) {
                    continue;
                }
                out.push_back(candId);
                if (out.size() >= maxCandidates) {
                    break;
                }
            }
            return out;
        }

        struct PartnerAgencyBatchItem {
            Domain::Agency::PartnerAgencyActionPlan plan{};
            std::string partnerName{};
            std::string sideLoverName{};
            std::string evalMode{};
            bool canConfess{ false };
            std::string affairStageStr{ "talking" };
            nlohmann::json contextJson{};
            std::vector<Foundation::ActorId> shortlistedIds{};
            PartnerAgencyEvalResult result{};
        };

        void ApplySinglePartnerAgencyOutcome(const PartnerAgencyBatchItem& item, float currentGameDay) {
            using Domain::Agency::PartnerAgencyActionType;
            const auto& plan = item.plan;
            const RE::FormID partnerRaw = plan.partnerId.raw();
            if (partnerRaw == 0) {
                return;
            }

            if (plan.permanentAffectionDelta != 0) {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                const int curAff = snap->affection ? snap->affection->GetPermanent(plan.partnerId, 50) : 50;
                RelationshipUseCases::GetSingleton().SetPermanentAffection(
                    partnerRaw, std::clamp(curAff + plan.permanentAffectionDelta, 0, 100));
            }

            switch (plan.type) {
                case PartnerAgencyActionType::BreakupClosedAffairLover:
                case PartnerAgencyActionType::BreakupOpenLover: {
                    if (!item.result.willBreakup) {
                        return;
                    }
                    const RE::FormID loverRaw = plan.relatedLoverId.raw();
                    if (loverRaw != 0) {
                        RelationshipUseCases::GetSingleton().PromoteToStatus(
                            partnerRaw, loverRaw, Foundation::RelationshipStatus::ExLover,
                            partnerRaw, /*bypassDifficulty=*/true, /*suppressModal=*/true);
                    }
                    if (plan.type == PartnerAgencyActionType::BreakupClosedAffairLover &&
                        item.canConfess && item.result.willConfess) {
                        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                            const auto type = (item.affairStageStr == "talking")
                                                  ? Foundation::PartnerIntentType::ConfessTalking
                                                  : Foundation::PartnerIntentType::ConfessPhysical;
                            ws.relationships.Write().SetPendingIntent(
                                plan.partnerId,
                                State::PendingPartnerIntentRecord{ type, currentGameDay, plan.relatedLoverId });
                        });
                    }
                    break;
                }
                case PartnerAgencyActionType::SuggestOpenIntent:
                case PartnerAgencyActionType::SuggestCloseIntent: {
                    if (!item.result.willSuggest) {
                        return;
                    }
                    const auto intentType = (plan.type == PartnerAgencyActionType::SuggestOpenIntent)
                                                ? Foundation::PartnerIntentType::SuggestOpen
                                                : Foundation::PartnerIntentType::SuggestClose;
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.relationships.Write().SetPendingIntent(
                            plan.partnerId,
                            State::PendingPartnerIntentRecord{ intentType, currentGameDay, Foundation::ActorId{} });
                    });
                    break;
                }
                case PartnerAgencyActionType::ConfessAffairIntent: {
                    const auto type = (item.affairStageStr == "talking")
                                          ? Foundation::PartnerIntentType::ConfessTalking
                                          : Foundation::PartnerIntentType::ConfessPhysical;
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        ws.relationships.Write().SetPendingIntent(
                            plan.partnerId,
                            State::PendingPartnerIntentRecord{ type, currentGameDay, plan.relatedLoverId });
                    });
                    break;
                }
                case PartnerAgencyActionType::SeekOpenLover:
                case PartnerAgencyActionType::SeekSecretAffair:
                case PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair: {
                    if (!item.result.willCheat) {
                        return;
                    }
                    Foundation::ActorId chosenId = plan.relatedLoverId;
                    if (!item.result.chosenCandidateName.empty()) {
                        const std::string chosenLower = Foundation::StringUtils::ToLowerStd(
                            Foundation::StringUtils::TrimView(item.result.chosenCandidateName));
                        for (const auto candId : item.shortlistedIds) {
                            if (Foundation::StringUtils::ToLowerStd(ResolveActorName(candId.raw())) == chosenLower) {
                                chosenId = candId;
                                break;
                            }
                        }
                    }
                    if (chosenId.IsNull() && !item.shortlistedIds.empty()) {
                        chosenId = item.shortlistedIds.front();
                    }
                    if (chosenId.IsNull()) {
                        return;
                    }

                    const bool isOpenPartner = (plan.type == PartnerAgencyActionType::SeekOpenLover);
                    if (isOpenPartner) {
                        ScopedMatchmakingApply applyScope;
                        RelationshipUseCases::GetSingleton().PromoteToStatus(
                            partnerRaw, chosenId.raw(), Foundation::RelationshipStatus::Lover,
                            partnerRaw, /*bypassDifficulty=*/true, /*suppressModal=*/true);
                    } else {
                        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                            ws.RecordCheatingIncident(
                                plan.partnerId,
                                Foundation::ActorId::Player(),
                                chosenId,
                                /*isSecret=*/true,
                                Foundation::AffairType::Casual,
                                Foundation::AffairStatus::Ongoing,
                                Foundation::AffairStage::Talking,
                                currentGameDay);
                        });
                    }
                    MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Partner agency applied: partner={:08X}, target={:08X}, open={}, reason='{}'",
                                   partnerRaw, chosenId.raw(), isOpenPartner, item.result.reasoning);
                    break;
                }
                default:
                    break;
            }
        }

        [[nodiscard]] bool IsSceneSettingEnabledForPair(const State::SettingsData& s,
                                                        const State::WorldStateSnapshot& snap,
                                                        RE::FormID idA,
                                                        RE::FormID idB) {
            if (!snap.relationships || !snap.affairs) {
                return false;
            }
            const Foundation::ActorId a{ idA };
            const Foundation::ActorId b{ idB };
            const auto status = snap.relationships->GetStatus(a, b);
            const bool isOpen = snap.relationships->IsPairOpen(a, b);
            const bool isCheating = snap.relationships && snap.affairs && snap.affairs->HasCheatedOn(a, b);

            if (isCheating) {
                return s.enableScenes_Cheating || s.enableScenes_CheatingDiscovered || s.enableScenes_CheatingCaught;
            }
            if (isOpen && s.enableScenes_OpenRelationship) {
                return true;
            }
            switch (status) {
                case Foundation::RelationshipStatus::Lover:
                    return s.enableScenes_Lover;
                case Foundation::RelationshipStatus::Dating:
                    return s.enableScenes_Dating;
                case Foundation::RelationshipStatus::Engaged:
                    return s.enableScenes_Engaged;
                case Foundation::RelationshipStatus::Married:
                    return s.enableScenes_Married;
                default:
                    return false;
            }
        }

    }  // namespace

    AgencyWorkflowCoordinator& AgencyWorkflowCoordinator::GetSingleton() {
        static AgencyWorkflowCoordinator instance;
        return instance;
    }

    bool AgencyWorkflowCoordinator::IsMatchmakingRunning() const noexcept {
        return runningMatchmaking_.load(std::memory_order_acquire);
    }

    bool AgencyWorkflowCoordinator::IsPartnerAgencyRunning() const noexcept {
        return runningPartnerAgency_.load(std::memory_order_acquire);
    }

    void AgencyWorkflowCoordinator::ClearRunningBatchState() noexcept {
        runningMatchmaking_.store(false, std::memory_order_release);
        runningPartnerAgency_.store(false, std::memory_order_release);
    }

    // =========================================================================
    // Cooldown & Persistent Day Accessors
    // =========================================================================

    void AgencyWorkflowCoordinator::RecordNpcCooldown(RE::FormID npcFormID, float currentGameDay) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([npcFormID, currentGameDay](State::WorldState& ws) {
            ws.cooldowns.Write().npcLastMatchmakingChangeDay[Foundation::ActorId{ npcFormID }] = currentGameDay;
        });
    }

    bool AgencyWorkflowCoordinator::IsNpcOnCooldown(RE::FormID npcFormID, float currentGameDay) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Agency::IsNpcOnMatchmakingCooldown(*snap, Foundation::ActorId{ npcFormID }, currentGameDay);
    }

    int AgencyWorkflowCoordinator::GetNpcCooldownRemaining(RE::FormID npcFormID, float currentGameDay) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->cooldowns || snap->settings->matchmakingNpcCooldownDays <= 0) {
            return 0;
        }
        const auto it = snap->cooldowns->npcLastMatchmakingChangeDay.find(Foundation::ActorId{ npcFormID });
        if (it == snap->cooldowns->npcLastMatchmakingChangeDay.end()) {
            return 0;
        }
        const float elapsed = currentGameDay - it->second;
        if (elapsed < 0.0f) {
            return 0;
        }
        const int remaining = static_cast<int>(
            std::ceil(static_cast<float>(snap->settings->matchmakingNpcCooldownDays) - elapsed));
        return std::max(0, remaining);
    }

    void AgencyWorkflowCoordinator::ClearNpcCooldowns() {
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.cooldowns.Write().npcLastMatchmakingChangeDay.clear();
        });
    }

    float AgencyWorkflowCoordinator::GetLastMatchmakingDay() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->cooldowns ? snap->cooldowns->lastMatchmakingDay : -1.0f;
    }

    void AgencyWorkflowCoordinator::SetLastMatchmakingDay(float day) {
        State::WorldStore::GetSingleton().Modify([day](State::WorldState& ws) {
            ws.cooldowns.Write().lastMatchmakingDay = day;
        });
    }

    float AgencyWorkflowCoordinator::GetLastPlayerPartnerCheatingDay() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->cooldowns ? snap->cooldowns->lastPlayerPartnerCheatingDay : -1.0f;
    }

    void AgencyWorkflowCoordinator::SetLastPlayerPartnerCheatingDay(float day) {
        State::WorldStore::GetSingleton().Modify([day](State::WorldState& ws) {
            ws.cooldowns.Write().lastPlayerPartnerCheatingDay = day;
        });
    }

    // =========================================================================
    // Matchmaking Workflows
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledMatchmaking(float currentGameDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->matchmakingEnabled) {
            return;
        }
        if (IsMatchmakingRunning()) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] TryScheduledMatchmaking skipped: batch already in progress");
            return;
        }

        const float lastDay = GetLastMatchmakingDay();
        const int intervalDays = std::max(1, snap->settings->matchmakingIntervalDays);
        if (lastDay >= 0.0f && currentGameDay >= lastDay &&
            (currentGameDay - lastDay) < static_cast<float>(intervalDays)) {
            return;
        }

        // Caller-level SkyrimNet runtime gate: evaluate availability before rolling or gathering LLM context.
        if (GetSkyrimNetGateway().IsAvailable() && snap->settings->matchmakingUseLLM) {
            RunLLMScheduledMatchmaking(currentGameDay);
        } else {
            RunDiceRollMatchmaking(currentGameDay);
        }
    }

    void AgencyWorkflowCoordinator::RunLLMScheduledMatchmaking(float currentGameDay) {
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            runningMatchmaking_.store(false, std::memory_order_release);
            return;
        }

        const auto eligibleNpcs = CollectEligibleMatchmakingProfiles(*snap, currentGameDay, true);
        if (eligibleNpcs.size() < 2) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] RunLLMScheduledMatchmaking: fewer than 2 eligible NPCs ({})",
                            eligibleNpcs.size());
            SetLastMatchmakingDay(currentGameDay);
            runningMatchmaking_.store(false, std::memory_order_release);
            return;
        }

        nlohmann::json ctx;
        ctx["gameDay"] = currentGameDay;
        ctx["maxSuggestions"] = std::max(1, snap->settings->matchmakingMaxSuggestions);
        ctx["orientationRules"] = std::string(Domain::Agency::GetOrientationRuleText(snap->settings->matchmakingOrientation));
        ctx["allowBreakups"] = snap->settings->matchmakingAllowBreakups;
        ctx["allowOpenClose"] = snap->settings->matchmakingAllowOpenClose;
        ctx["allowCheating"] = snap->settings->matchmakingAllowCheating;
        ctx["allowedActions"] = BuildAllowedActionsText(*snap->settings);
        ctx["allowedStatuses"] = BuildAllowedStatusesText(*snap->settings);
        ctx["npcs"] = BuildRosterJsonArray(eligibleNpcs, *snap);

        struct ScheduledPayload {
            nlohmann::json contextJson;
            nlohmann::json parsedResponse;
        };

        auto nameLookup = BuildLowerNameMap(eligibleNpcs);
        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = false,
            .skipConfirmationModal = false
        };

        LlmBatchCallbacks<ScheduledPayload> callbacks;
        callbacks.submitTask = [](ScheduledPayload& payload, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations\\maras_matchmaking_scheduled",
                "gamemaster_evaluation",
                payload.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, "LLM request failed for scheduled matchmaking", nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, "Invalid JSON matches payload from scheduled matchmaking", nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](ScheduledPayload& p) {
                        p.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const ScheduledPayload&, const std::string& reason) {
            return std::format("Scheduled Matchmaking: {}", reason);
        };
        callbacks.onAllResolved = [this, opts, nameLookup = std::move(nameLookup)](
                                      const std::shared_ptr<LlmBatchState<ScheduledPayload>>& state) {
            SetLastMatchmakingDay(opts.currentGameDay);

            std::vector<MatchmakingResult> candidates;
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                for (const auto& m : item.payload.parsedResponse["matches"]) {
                    if (auto cand = JsonEntryToCandidate(m, nameLookup)) {
                        (void)ValidateCandidate(*cand, *snapNow, opts.currentGameDay);
                        candidates.push_back(std::move(*cand));
                    }
                }
            }

            if (candidates.empty()) {
                MARAS_LOG_INFO("[AgencyWorkflowCoordinator] LLM scheduled matchmaking returned 0 valid proposals");
                ClearRunningBatchState();
                return;
            }
            StartVerificationBatch(std::move(candidates), opts);
        };

        RunLlmBatch<ScheduledPayload>(
            "Scheduled NPC Matchmaking",
            "gamemaster_evaluation",
            { ScheduledPayload{ std::move(ctx), {} } },
            std::move(callbacks),
            GetSessionEpoch());
    }

    void AgencyWorkflowCoordinator::RunDiceRollMatchmaking(float currentGameDay) {
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings) {
            runningMatchmaking_.store(false, std::memory_order_release);
            return;
        }

        const auto eligibleNpcs = CollectEligibleMatchmakingProfiles(*snap, currentGameDay, true);
        SetLastMatchmakingDay(currentGameDay);

        if (eligibleNpcs.size() < 2) {
            MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] RunDiceRollMatchmaking: fewer than 2 eligible NPCs ({})",
                            eligibleNpcs.size());
            runningMatchmaking_.store(false, std::memory_order_release);
            return;
        }

        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));
        const auto proposals = Domain::Agency::GenerateDiceRollProposals(*snap, eligibleNpcs, currentGameDay, rng);

        std::vector<MatchmakingResult> candidates;
        candidates.reserve(proposals.size());
        for (const auto& p : proposals) {
            MatchmakingResult res{};
            res.actorAFormID = p.actorA.raw();
            res.actorBFormID = p.actorB.raw();
            res.actorAName = ResolveActorName(p.actorA.raw());
            res.actorBName = ResolveActorName(p.actorB.raw());
            res.action = ActionKindToString(p.slot.action);
            res.targetStatus = (p.slot.action == Domain::Agency::MatchmakingActionKind::OpenClose)
                                   ? (p.slot.openTarget ? std::string("open") : std::string("closed"))
                                   : Foundation::StringUtils::ToLowerStd(
                                         Foundation::RelationshipStatusToString(p.slot.targetStatus));
            res.isCheating = p.slot.isCheating;
            (void)ValidateCandidate(res, *snap, currentGameDay);
            if (!res.skipped) {
                candidates.push_back(std::move(res));
            }
        }

        if (candidates.empty()) {
            MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Dice-roll scheduled matchmaking generated 0 valid matches");
            runningMatchmaking_.store(false, std::memory_order_release);
            return;
        }

        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = false,
            .skipConfirmationModal = !snap->settings->matchmakingConfirmResults
        };
        StartMemoryBatchOrApply(std::move(candidates), opts);
    }

    bool AgencyWorkflowCoordinator::RunManualMatchmaking(const std::vector<RE::FormID>& npcFormIDs) {
        if (npcFormIDs.empty()) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunManualMatchmaking requires active SkyrimNet availability");
            return false;
        }
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunManualMatchmaking skipped: batch already active");
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto roster = CollectEligibleMatchmakingProfiles(*snap, currentGameDay, false);
        auto nameLookup = BuildLowerNameMap(roster);

        struct ManualTask {
            RE::FormID targetId{ 0 };
            std::string targetName{};
            nlohmann::json contextJson{};
            nlohmann::json parsedResponse{};
        };

        std::vector<ManualTask> payloads;
        payloads.reserve(npcFormIDs.size());

        for (const RE::FormID targetId : npcFormIDs) {
            if (!IsNpcEligibleBasicEngineCheck(targetId, *snap)) {
                continue;
            }
            nlohmann::json ctx;
            ctx["gameDay"] = currentGameDay;
            ctx["targetName"] = ResolveActorName(targetId);
            ctx["targetMatchUUID"] = GetSkyrimNetGateway().GetActorUUID(targetId);
            ctx["targetRace"] = ResolveActorRaceName(targetId);
            ctx["targetSex"] = SexToDisplayString(ResolveActorSexEnum(targetId));
            ctx["maxSuggestions"] = std::max(1, snap->settings->matchmakingMaxSuggestions);
            ctx["allowedActions"] = BuildAllowedActionsText(*snap->settings);
            ctx["allowedStatuses"] = BuildAllowedStatusesText(*snap->settings);
            ctx["candidates"] = BuildRosterJsonArray(roster, *snap, Foundation::ActorId{ targetId });

            payloads.push_back(ManualTask{
                .targetId = targetId,
                .targetName = ResolveActorName(targetId),
                .contextJson = std::move(ctx),
                .parsedResponse = {}
            });
        }

        if (payloads.empty()) {
            runningMatchmaking_.store(false, std::memory_order_release);
            return false;
        }

        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = false,
            .isManual = true,
            .skipConfirmationModal = false
        };

        LlmBatchCallbacks<ManualTask> callbacks;
        callbacks.submitTask = [](ManualTask& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations\\maras_matchmaking_manual",
                "gamemaster_evaluation",
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [targetName = task.targetName, reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, std::format("Request failed for {}", targetName), nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, std::format("Invalid JSON response for {}", targetName), nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](ManualTask& t) {
                        t.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const ManualTask& t, const std::string& reason) {
            return std::format("{} ({:08X}): {}", t.targetName, t.targetId, reason);
        };
        callbacks.onAllResolved = [this, opts, nameLookup = std::move(nameLookup)](
                                      const std::shared_ptr<LlmBatchState<ManualTask>>& state) {
            std::vector<MatchmakingResult> candidates;
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                for (const auto& m : item.payload.parsedResponse["matches"]) {
                    if (auto cand = JsonEntryToCandidate(m, nameLookup, item.payload.targetId)) {
                        (void)ValidateCandidate(*cand, *snapNow, opts.currentGameDay);
                        candidates.push_back(std::move(*cand));
                    }
                }
            }

            if (candidates.empty()) {
                GetUiBridgeGateway().SendMatchmakingResults({}, opts.isGlobal);
                ClearRunningBatchState();
                return;
            }
            StartVerificationBatch(std::move(candidates), opts);
        };

        RunLlmBatch<ManualTask>(
            "Manual NPC Matchmaking",
            "gamemaster_evaluation",
            std::move(payloads),
            std::move(callbacks),
            GetSessionEpoch());
        return true;
    }

    bool AgencyWorkflowCoordinator::RunGlobalMatchmaking(std::optional<std::size_t> maxResultsOverride) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable()) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunGlobalMatchmaking requires active SkyrimNet availability");
            return false;
        }
        if (runningMatchmaking_.exchange(true, std::memory_order_acq_rel)) {
            MARAS_LOG_WARN("[AgencyWorkflowCoordinator] RunGlobalMatchmaking skipped: batch already active");
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto eligible = CollectEligibleMatchmakingProfiles(*snap, currentGameDay, false);
        if (eligible.size() < 2) {
            runningMatchmaking_.store(false, std::memory_order_release);
            return false;
        }

        const std::size_t maxCount = maxResultsOverride.value_or(
            static_cast<std::size_t>(std::max(1, snap->settings->matchmakingMaxSuggestions)));

        nlohmann::json ctx;
        ctx["gameDay"] = currentGameDay;
        ctx["maxSuggestions"] = maxCount;
        ctx["orientationRules"] = std::string(Domain::Agency::GetOrientationRuleText(snap->settings->matchmakingOrientation));
        ctx["allowBreakups"] = snap->settings->matchmakingAllowBreakups;
        ctx["allowOpenClose"] = snap->settings->matchmakingAllowOpenClose;
        ctx["allowCheating"] = snap->settings->matchmakingAllowCheating;
        ctx["allowedActions"] = BuildAllowedActionsText(*snap->settings);
        ctx["allowedStatuses"] = BuildAllowedStatusesText(*snap->settings);
        ctx["npcs"] = BuildRosterJsonArray(eligible, *snap);

        struct GlobalTask {
            nlohmann::json contextJson;
            nlohmann::json parsedResponse;
        };

        auto nameLookup = BuildLowerNameMap(eligible);
        const MatchBatchOptions opts{
            .currentGameDay = currentGameDay,
            .isGlobal = true,
            .isManual = true,
            .skipConfirmationModal = false
        };

        LlmBatchCallbacks<GlobalTask> callbacks;
        callbacks.submitTask = [](GlobalTask& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations\\maras_matchmaking_scheduled",
                "gamemaster_evaluation",
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, "LLM request failed for global matchmaking", nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("matches") || !parsed["matches"].is_array()) {
                        reportDone(false, true, "Invalid JSON matches payload for global matchmaking", nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [parsed](GlobalTask& t) {
                        t.parsedResponse = parsed;
                    });
                });
        };
        callbacks.formatFailureLine = [](const GlobalTask&, const std::string& reason) {
            return std::format("Global Matchmaking: {}", reason);
        };
        callbacks.onAllResolved = [this, opts, nameLookup = std::move(nameLookup)](
                                      const std::shared_ptr<LlmBatchState<GlobalTask>>& state) {
            std::vector<MatchmakingResult> candidates;
            const auto snapNow = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& item : state->tasks) {
                if (!item.success || !item.payload.parsedResponse.contains("matches")) {
                    continue;
                }
                for (const auto& m : item.payload.parsedResponse["matches"]) {
                    if (auto cand = JsonEntryToCandidate(m, nameLookup)) {
                        (void)ValidateCandidate(*cand, *snapNow, opts.currentGameDay);
                        candidates.push_back(std::move(*cand));
                    }
                }
            }
            if (candidates.empty()) {
                GetUiBridgeGateway().SendMatchmakingResults({}, true);
                ClearRunningBatchState();
                return;
            }
            StartVerificationBatch(std::move(candidates), opts);
        };

        RunLlmBatch<GlobalTask>(
            "Global NPC Matchmaking",
            "gamemaster_evaluation",
            { GlobalTask{ std::move(ctx), {} } },
            std::move(callbacks),
            GetSessionEpoch());
        return true;
    }

    void AgencyWorkflowCoordinator::ProcessManualResults(RE::FormID targetNpcId, const nlohmann::json& matchesArray) {
        if (!matchesArray.is_array()) {
            return;
        }
        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto nameLookup = BuildLowerNameMap(CollectEligibleMatchmakingProfiles(*snap, currentGameDay, false));

        std::vector<MatchmakingResult> candidates;
        for (const auto& m : matchesArray) {
            if (auto cand = JsonEntryToCandidate(m, nameLookup, targetNpcId)) {
                (void)ValidateCandidate(*cand, *snap, currentGameDay);
                candidates.push_back(std::move(*cand));
            }
        }
        ApplyMatchmakingResults(
            std::move(candidates),
            MatchBatchOptions{ .currentGameDay = currentGameDay, .isGlobal = false, .isManual = true });
    }

    void AgencyWorkflowCoordinator::ProcessGlobalManualResults(const nlohmann::json& matchesArray) {
        if (!matchesArray.is_array()) {
            return;
        }
        const float currentGameDay = GetCurrentGameDaysSafe();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto nameLookup = BuildLowerNameMap(CollectEligibleMatchmakingProfiles(*snap, currentGameDay, false));

        std::vector<MatchmakingResult> candidates;
        for (const auto& m : matchesArray) {
            if (auto cand = JsonEntryToCandidate(m, nameLookup)) {
                (void)ValidateCandidate(*cand, *snap, currentGameDay);
                candidates.push_back(std::move(*cand));
            }
        }
        ApplyMatchmakingResults(
            std::move(candidates),
            MatchBatchOptions{ .currentGameDay = currentGameDay, .isGlobal = true, .isManual = true });
    }

    bool AgencyWorkflowCoordinator::ValidateCandidate(MatchmakingResult& result,
                                                    const State::WorldStateSnapshot& snap,
                                                    float currentGameDay) const {
        auto reject = [&](std::string reason) {
            result.skipped = true;
            result.skipReason = std::move(reason);
            return false;
        };

        if (!snap.settings || !snap.relationships) {
            return reject("World state unavailable");
        }
        if (result.actorAFormID == 0 || result.actorBFormID == 0 || result.actorAFormID == result.actorBFormID) {
            return reject("Invalid actor pair");
        }
        if (!IsNpcEligibleBasicEngineCheck(result.actorAFormID, snap) ||
            !IsNpcEligibleBasicEngineCheck(result.actorBFormID, snap)) {
            return reject("One or both actors are dead, children, or unregistered");
        }
        if (RelationshipUseCases::GetSingleton().AreFamilyMembers(result.actorAFormID, result.actorBFormID)) {
            return reject("Actors are family members");
        }

        const Foundation::ActorId idA{ result.actorAFormID };
        const Foundation::ActorId idB{ result.actorBFormID };
        if (!Domain::Agency::IsOrientationCompatible(
                snap.settings->matchmakingOrientation,
                ResolveActorSexEnum(idA.raw()),
                ResolveActorSexEnum(idB.raw()))) {
            return reject("Orientation incompatibility");
        }

        if (Domain::Agency::IsNpcOnMatchmakingCooldown(snap, idA, currentGameDay) ||
            Domain::Agency::IsNpcOnMatchmakingCooldown(snap, idB, currentGameDay)) {
            return reject("One or both actors are on matchmaking cooldown");
        }

        const auto curStatus = snap.relationships->GetStatus(idA, idB);
        const std::string action = NormalizeActionString(result.action);
        result.action = action;

        if (action == "breakup") {
            if (!snap.settings->matchmakingAllowBreakups) {
                return reject("Breakups disabled in settings");
            }
            if (!Foundation::IsActivePartnerStatus(curStatus)) {
                return reject("Actors are not active partners");
            }
            return true;
        }

        if (action == "open_close") {
            if (!snap.settings->matchmakingAllowOpenClose) {
                return reject("Open/close toggles disabled in settings");
            }
            if (!Foundation::IsMainTierStatus(curStatus)) {
                return reject("Only main-tier couples can toggle open/closed");
            }
            return true;
        }

        const auto targetStatus = Foundation::StringToRelationshipStatus(result.targetStatus);
        if (targetStatus == Foundation::RelationshipStatus::Unknown) {
            return reject("Unknown target relationship status");
        }

        if (action == "advance" && !Foundation::IsActivePartnerStatus(curStatus)) {
            return reject("Cannot advance non-active pair");
        }
        if (action == "new" && Foundation::IsActivePartnerStatus(curStatus)) {
            return reject("Pair already has an active relationship");
        }

        const bool hasClosedMainA = !snap.relationships->GetPrimaryPartner(idA).IsNull() &&
                                    !snap.relationships->HasAnyOpenMainPair(idA);
        const bool hasClosedMainB = !snap.relationships->GetPrimaryPartner(idB).IsNull() &&
                                    !snap.relationships->HasAnyOpenMainPair(idB);
        const bool wouldCheat =
            (hasClosedMainA && snap.relationships->GetPrimaryPartner(idA) != idB) ||
            (hasClosedMainB && snap.relationships->GetPrimaryPartner(idB) != idA);

        if (wouldCheat && (!snap.settings->matchmakingAllowCheating || targetStatus != Foundation::RelationshipStatus::Lover)) {
            return reject("Cheating disallowed or invalid status for affair");
        }
        if (wouldCheat) {
            result.isCheating = true;
        }
        return true;
    }

    void AgencyWorkflowCoordinator::StartVerificationBatch(std::vector<MatchmakingResult> candidates,
                                                           const MatchBatchOptions& opts) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !GetSkyrimNetGateway().IsAvailable() || !snap->settings->matchmakingEvaluationStep) {
            ProceedAfterVerification(std::move(candidates), opts);
            return;
        }

        struct VerifyTask {
            MatchmakingResult candidate;
            nlohmann::json contextJson;
            bool skipVerify{ false };
            MatchmakingVerifyEvalResult evalResult;
        };

        std::vector<VerifyTask> tasks;
        tasks.reserve(candidates.size());
        bool anyToVerify = false;

        for (auto& cand : candidates) {
            VerifyTask task{};
            task.skipVerify = cand.skipped;
            if (!cand.skipped) {
                task.contextJson = BuildVerifyContextJson(cand, *snap, opts.currentGameDay);
                anyToVerify = true;
            }
            task.candidate = std::move(cand);
            tasks.push_back(std::move(task));
        }

        if (!anyToVerify) {
            ProceedAfterVerification(std::move(candidates), opts);
            return;
        }

        LlmBatchCallbacks<VerifyTask> callbacks;
        callbacks.submitTask = [](VerifyTask& task, auto reportDone) -> bool {
            if (task.skipVerify) {
                reportDone(true, false, {}, nullptr);
                return true;
            }
            return GetSkyrimNetGateway().EvaluateMatchmakingVerify(
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                task.candidate.action,
                task.candidate.targetStatus,
                task.candidate.actorAName,
                task.candidate.actorBName,
                [reportDone = std::move(reportDone)](const MatchmakingVerifyEvalResult& eval) {
                    if (!eval.success) {
                        const bool isInvalidJson = (eval.failureType == LLMFailureType::InvalidJson);
                        const std::string err = !eval.failureDetail.empty() ? eval.failureDetail : eval.reason;
                        reportDone(false, isInvalidJson, err, nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [eval](VerifyTask& t) {
                        t.evalResult = eval;
                    });
                });
        };
        callbacks.formatFailureLine = [](const VerifyTask& t, const std::string& reason) {
            return std::format("{} & {}: {}", t.candidate.actorAName, t.candidate.actorBName, reason);
        };
        callbacks.onAllResolved = [this, opts](const std::shared_ptr<LlmBatchState<VerifyTask>>& state) {
            std::vector<MatchmakingResult> verified;
            verified.reserve(state->tasks.size());
            for (auto& item : state->tasks) {
                MatchmakingResult res = item.payload.candidate;
                if (!item.payload.skipVerify && item.success && !item.payload.evalResult.suitable) {
                    res.skipped = true;
                    res.skipReason = item.payload.evalResult.reason.empty()
                                         ? std::string("Rejected during narrative verification")
                                         : item.payload.evalResult.reason;
                } else if (!item.payload.skipVerify && item.success && !item.payload.evalResult.reason.empty()) {
                    res.memory = item.payload.evalResult.reason;
                }
                verified.push_back(std::move(res));
            }
            ProceedAfterVerification(std::move(verified), opts);
        };

        RunLlmBatch<VerifyTask>(
            "Matchmaking Verification",
            "gamemaster_evaluation",
            std::move(tasks),
            std::move(callbacks),
            GetSessionEpoch());
    }

    void AgencyWorkflowCoordinator::ProceedAfterVerification(std::vector<MatchmakingResult> results,
                                                             const MatchBatchOptions& opts) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const bool requireModal =
            !opts.skipConfirmationModal &&
            snap->settings &&
            snap->settings->matchmakingConfirmResults &&
            !opts.isManual;
        if (requireModal) {
            ShowMatchmakingReviewModal(std::move(results), opts);
            return;
        }
        StartMemoryBatchOrApply(std::move(results), opts);
    }

    void AgencyWorkflowCoordinator::ShowMatchmakingReviewModal(std::vector<MatchmakingResult> results,
                                                               const MatchBatchOptions& opts) {
        std::vector<ModalChecklistItem> items;
        items.reserve(results.size());
        bool anyEligible = false;

        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            ModalChecklistItem item{};
            item.id = std::format("pair_{}", i);
            item.label = std::format("{} & {}", r.actorAName, r.actorBName);
            item.subtitle = std::format("{} -> {}", r.action, r.targetStatus);
            item.description = r.skipped ? r.skipReason : r.memory;
            item.checked = !r.skipped;
            item.checkboxDisabled = r.skipped;
            item.badgeText = r.skipped ? "Skipped" : (r.isCheating ? "Cheating" : "Approved");
            item.badgeTone = r.skipped ? "default" : (r.isCheating ? "warning" : "success");
            if (!r.skipped) {
                anyEligible = true;
            }
            items.push_back(std::move(item));
        }

        if (!anyEligible) {
            ApplyMatchmakingResults(std::move(results), opts);
            return;
        }

        ModalRequest req{};
        req.modalType = "generic";
        req.title = "Review NPC Matchmaking Results";
        req.subtitle = opts.isGlobal ? "Scheduled / Global Matchmaking" : "Manual Matchmaking";
        req.message = "Select which proposed relationship changes to apply.";
        req.tone = ModalTone::Ai;
        req.size = ModalSize::Lg;
        req.defaultCancelButtonIndex = 1;
        req.controls.push_back(ModalControl::MakeChecklist(
            "selected_matches",
            "Proposed Changes",
            std::move(items),
            /*showSelectAllToggle=*/true));

        ModalButton applyBtn = ModalButton::Make("apply", "Apply Selected", ModalButtonVariant::Primary);
        applyBtn.requireChecklistId = "selected_matches";
        applyBtn.minCheckedCount = 1;
        req.buttons.push_back(std::move(applyBtn));
        req.buttons.push_back(ModalButton::Make("discard", "Discard All", ModalButtonVariant::Secondary));

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        auto sharedResults = std::make_shared<std::vector<MatchmakingResult>>(std::move(results));

        GetModalGateway().ShowModal(
            std::move(req),
            [this, expectedEpoch, sharedResults, opts](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(GetPapyrusMutex());
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (resp.aborted || resp.buttonId != "apply") {
                    for (auto& r : *sharedResults) {
                        r.skipped = true;
                        r.skipReason = "Discarded by user";
                    }
                    GetUiBridgeGateway().SendMatchmakingResults(*sharedResults, opts.isGlobal);
                    ClearRunningBatchState();
                    return;
                }

                for (std::size_t i = 0; i < sharedResults->size(); ++i) {
                    const std::string itemId = std::format("pair_{}", i);
                    if (!resp.IsItemChecked("selected_matches", itemId, false)) {
                        (*sharedResults)[i].skipped = true;
                        (*sharedResults)[i].skipReason = "Unchecked in review";
                    }
                }
                StartMemoryBatchOrApply(std::move(*sharedResults), opts);
            });
    }

    void AgencyWorkflowCoordinator::StartMemoryBatchOrApply(std::vector<MatchmakingResult> results,
                                                            const MatchBatchOptions& opts) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const bool hasActive = std::any_of(results.begin(), results.end(), [](const MatchmakingResult& r) {
            return !r.skipped;
        });
        if (!hasActive || !snap->settings || !snap->settings->matchmakingGenerateMemories ||
            !GetSkyrimNetGateway().IsAvailable()) {
            ApplyMatchmakingResults(std::move(results), opts);
            return;
        }

        struct MemoryBatchTask {
            nlohmann::json contextJson;
            std::vector<GeneratedMemoryEntry> generatedEntries;
        };

        auto sharedResults = std::make_shared<std::vector<MatchmakingResult>>(std::move(results));

        LlmBatchCallbacks<MemoryBatchTask> callbacks;
        callbacks.submitTask = [](MemoryBatchTask& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().SendCustomPromptToLLM(
                "maras_evaluations\\maras_event_memories",
                "gamemaster_evaluation",
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                [reportDone = std::move(reportDone)](const char* rawResp, int success) {
                    if (!success || !rawResp) {
                        reportDone(false, false, "LLM memory generation request failed", nullptr);
                        return;
                    }
                    const nlohmann::json parsed = JsonService::ParseLLMJson(rawResp);
                    if (parsed.is_discarded() || !parsed.contains("memories") || !parsed["memories"].is_array()) {
                        reportDone(false, true, "Invalid JSON memories array", nullptr);
                        return;
                    }

                    std::vector<GeneratedMemoryEntry> entries;
                    for (const auto& m : parsed["memories"]) {
                        if (!m.is_object()) {
                            continue;
                        }
                        GeneratedMemoryEntry e{};
                        if (m.contains("actor") && m["actor"].is_string()) {
                            e.actorName = m["actor"].get<std::string>();
                        }
                        if (m.contains("actorFormId") && m["actorFormId"].is_string()) {
                            if (auto parsedId = Utils::HexStringToFormID(m["actorFormId"].get<std::string>())) {
                                e.actorFormID = *parsedId;
                            }
                        }
                        if (m.contains("content") && m["content"].is_string()) {
                            e.content = m["content"].get<std::string>();
                        }
                        if (m.contains("location") && m["location"].is_string()) {
                            e.location = m["location"].get<std::string>();
                        }
                        if (m.contains("emotion") && m["emotion"].is_string()) {
                            e.emotion = m["emotion"].get<std::string>();
                        }
                        if (m.contains("importance_score") && m["importance_score"].is_number()) {
                            e.importanceScore = m["importance_score"].get<float>();
                        }
                        if (m.contains("type") && m["type"].is_string()) {
                            e.type = m["type"].get<std::string>();
                        }
                        if (m.contains("tags") && m["tags"].is_array()) {
                            for (const auto& t : m["tags"]) {
                                if (t.is_string()) {
                                    e.tags.push_back(t.get<std::string>());
                                }
                            }
                        }
                        if (!e.content.empty()) {
                            entries.push_back(std::move(e));
                        }
                    }

                    reportDone(true, false, {}, [entries = std::move(entries)](MemoryBatchTask& t) mutable {
                        t.generatedEntries = std::move(entries);
                    });
                });
        };
        callbacks.formatFailureLine = [](const MemoryBatchTask&, const std::string& reason) {
            return std::format("Memory Generation: {}", reason);
        };
        callbacks.onAllResolved = [this, sharedResults, opts](
                                      const std::shared_ptr<LlmBatchState<MemoryBatchTask>>& state) {
            if (!state->tasks.empty() && state->tasks.front().success) {
                const auto& generated = state->tasks.front().payload.generatedEntries;
                for (auto& match : *sharedResults) {
                    if (match.skipped) {
                        continue;
                    }
                    for (const auto& entry : generated) {
                        const bool matchesActor =
                            (entry.actorFormID != 0 &&
                             (entry.actorFormID == match.actorAFormID || entry.actorFormID == match.actorBFormID)) ||
                            Foundation::StringUtils::CaseInsensitiveEquals(entry.actorName, match.actorAName) ||
                            Foundation::StringUtils::CaseInsensitiveEquals(entry.actorName, match.actorBName);
                        if (matchesActor) {
                            GeneratedMemoryEntry copy = entry;
                            copy.relatedActorFormIDs = { match.actorAFormID, match.actorBFormID };
                            match.dualMemories.push_back(std::move(copy));
                        }
                    }
                }
            }
            ApplyMatchmakingResults(std::move(*sharedResults), opts);
        };

        MemoryBatchTask task{};
        task.contextJson = BuildMemoryBatchContextJson(*sharedResults, *snap, opts.currentGameDay);

        RunLlmBatch<MemoryBatchTask>(
            "Matchmaking Memory Generation",
            "gamemaster_evaluation",
            { std::move(task) },
            std::move(callbacks),
            GetSessionEpoch());
    }

    bool AgencyWorkflowCoordinator::ApplyMatch(MatchmakingResult& result, float currentGameDay) {
        if (result.skipped) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!ValidateCandidate(result, *snap, currentGameDay)) {
            return false;
        }

        const RE::FormID idA = result.actorAFormID;
        const RE::FormID idB = result.actorBFormID;
        bool applied = false;

        ScopedMatchmakingApply applyScope;
        const bool hasPreGeneratedMemories = !result.dualMemories.empty();
        std::optional<ScopedMemorySuppression> memorySuppression;
        if (hasPreGeneratedMemories) {
            memorySuppression.emplace();
        }

        if (result.action == "open_close") {
            const bool targetOpen = Foundation::StringUtils::CaseInsensitiveEquals(result.targetStatus, "open");
            applied = RelationshipUseCases::GetSingleton().SetPairIsOpen(idA, idB, targetOpen);
            if (applied && !hasPreGeneratedMemories && GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().RecordOpenRelationshipToggle(idA, idB, targetOpen);
            }
        } else {
            const auto targetStatus = Foundation::StringToRelationshipStatus(result.targetStatus);
            if (targetStatus == Foundation::RelationshipStatus::Unknown) {
                result.skipped = true;
                result.skipReason = "Invalid target status";
                return false;
            }
            applied = RelationshipUseCases::GetSingleton().PromoteToStatus(
                idA, idB, targetStatus, idA, /*bypassDifficulty=*/true, /*suppressModal=*/true);
        }

        memorySuppression.reset();

        if (!applied) {
            result.skipped = true;
            result.skipReason = "Relationship transition rejected";
            return false;
        }

        result.applied = true;
        RecordNpcCooldown(idA, currentGameDay);
        RecordNpcCooldown(idB, currentGameDay);

        if (hasPreGeneratedMemories && GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().IngestMemories(result.dualMemories, { idA, idB });
        }

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Applied matchmaking: {:08X} ('{}') & {:08X} ('{}') action='{}' status='{}'",
                       idA, result.actorAName, idB, result.actorBName, result.action, result.targetStatus);
        return true;
    }

    void AgencyWorkflowCoordinator::ApplyMatchmakingResults(std::vector<MatchmakingResult> results,
                                                            const MatchBatchOptions& opts) {
        std::size_t appliedCount = 0;
        for (auto& r : results) {
            if (!r.skipped && ApplyMatch(r, opts.currentGameDay)) {
                ++appliedCount;
            }
        }

        GetUiBridgeGateway().SendMatchmakingResults(results, opts.isGlobal);
        if (appliedCount > 0 && GetUiBridgeGateway().IsDashboardOpen()) {
            GetUiBridgeGateway().SendOverviewData();
            GetUiBridgeGateway().SendNPCList();
        }

        ClearRunningBatchState();
    }

    // =========================================================================
    // Player-Partner Agency (Infidelity / Open-Close Intents)
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledPlayerPartnerInfidelity(float currentGameDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->relationships || !snap->affairs) {
            return;
        }
        const auto& settings = *snap->settings;
        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            return;
        }
        if (IsPartnerAgencyRunning()) {
            return;
        }

        const float lastDay = GetLastPlayerPartnerCheatingDay();
        const float scanInterval = settings.GetPartnerAgencyScanIntervalDays();
        if (lastDay >= 0.0f && currentGameDay >= lastDay && (currentGameDay - lastDay) < scanInterval) {
            return;
        }

        runningPartnerAgency_.store(true, std::memory_order_release);

        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        std::vector<Domain::Agency::PartnerAgencyInput> inputs;

        for (const auto& [partnerId, status] : snap->relationships->GetActivePartners(Foundation::ActorId::Player())) {
            if (!Foundation::IsMainTierStatus(status)) {
                continue;
            }
            if (!IsNpcEligibleBasicEngineCheck(partnerId.raw(), *snap)) {
                continue;
            }
            bool nearOrFollower = false;
            if (spatialSnap) {
                if (const auto* sample = spatialSnap->Find(partnerId.AsRefId())) {
                    nearOrFollower = sample->IsPlayerTeammate() ||
                                     spatialSnap->IsWithinRadius(Foundation::RefId::Player(), partnerId.AsRefId(), 1500.0f);
                }
            }
            bool pregnantWithPlayer = false;
            if (snap->lineage) {
                for (const auto cid : snap->lineage->GetUnbornOfMother(partnerId)) {
                    if (const auto* child = snap->lineage->FindChild(cid)) {
                        if (child->bioFather.IsPlayer() || child->presumedFather.IsPlayer()) {
                            pregnantWithPlayer = true;
                            break;
                        }
                    }
                }
            }

            Domain::Agency::PartnerAgencyInput input{};
            input.partnerId = partnerId;
            input.partnerName = ResolveActorName(partnerId.raw());
            input.partnerSex = ResolveActorSexEnum(partnerId.raw());
            input.isFollowerOrNearPlayer = nearOrFollower;
            input.isPregnantWithPlayerChild = pregnantWithPlayer;
            inputs.push_back(std::move(input));
        }

        if (inputs.empty()) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            runningPartnerAgency_.store(false, std::memory_order_release);
            return;
        }

        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));
        const auto plans = Domain::Agency::EvaluatePartnerAgencyActions(*snap, inputs, currentGameDay, rng);

        std::vector<PartnerAgencyBatchItem> llmTasks;
        const bool useLlm = GetSkyrimNetGateway().IsAvailable() && settings.playerPartnerCheatingUseLLM;

        for (const auto& plan : plans) {
            using Domain::Agency::PartnerAgencyActionType;

            if (plan.type == PartnerAgencyActionType::EscalateTalkingToPhysical) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.affairs.Write().SetStage(
                        plan.partnerId,
                        Foundation::ActorId::Player(),
                        plan.relatedLoverId,
                        Foundation::AffairStage::Physical,
                        currentGameDay);
                });
                continue;
            }
            if (plan.type == PartnerAgencyActionType::ExpiredIntentSuggestClosePenalty) {
                PartnerAgencyBatchItem item{};
                item.plan = plan;
                ApplySinglePartnerAgencyOutcome(item, currentGameDay);
                continue;
            }
            if (plan.type == PartnerAgencyActionType::ExpiredIntentConfessEscalatePhysical) {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.affairs.Write().SetStage(
                        plan.partnerId,
                        Foundation::ActorId::Player(),
                        plan.relatedLoverId,
                        Foundation::AffairStage::Physical,
                        currentGameDay);
                    ws.relationships.Write().ClearPendingIntent(plan.partnerId);
                });
                PartnerAgencyBatchItem item{};
                item.plan = plan;
                ApplySinglePartnerAgencyOutcome(item, currentGameDay);
                continue;
            }

            PartnerAgencyBatchItem item{};
            item.plan = plan;
            item.partnerName = ResolveActorName(plan.partnerId.raw());
            item.sideLoverName = ResolveActorName(plan.relatedLoverId.raw());
            item.affairStageStr = "talking";
            if (!plan.relatedLoverId.IsNull()) {
                if (const auto* affair = snap->affairs->FindTriple(
                        plan.partnerId, Foundation::ActorId::Player(), plan.relatedLoverId)) {
                    item.affairStageStr = std::string(Foundation::AffairStageToString(affair->affairStage));
                }
            }

            switch (plan.type) {
                case PartnerAgencyActionType::BreakupClosedAffairLover:
                    item.evalMode = "breakup_affair";
                    item.canConfess = true;
                    break;
                case PartnerAgencyActionType::BreakupOpenLover:
                    item.evalMode = "breakup_open_lover";
                    break;
                case PartnerAgencyActionType::SuggestOpenIntent:
                    item.evalMode = "suggest_open";
                    break;
                case PartnerAgencyActionType::SuggestCloseIntent:
                    item.evalMode = "suggest_close";
                    break;
                case PartnerAgencyActionType::ConfessAffairIntent:
                    ApplySinglePartnerAgencyOutcome(item, currentGameDay);
                    continue;
                case PartnerAgencyActionType::SeekOpenLover:
                case PartnerAgencyActionType::SeekSecretAffair:
                case PartnerAgencyActionType::ExpiredIntentForceSuggestOpenAffair:
                    item.shortlistedIds = CollectShortlistedParamours(*snap, plan.partnerId, 6);
                    if (item.shortlistedIds.empty()) {
                        continue;
                    }
                    break;
                default:
                    continue;
            }

            if (!useLlm) {
                item.result.willBreakup = true;
                item.result.willConfess = plan.triggerConfessIntent;
                item.result.willSuggest = true;
                item.result.willCheat = true;
                if (!item.shortlistedIds.empty()) {
                    const auto idx = static_cast<std::size_t>(
                        rng.NextInt(0, static_cast<std::int32_t>(item.shortlistedIds.size() - 1)));
                    item.result.chosenCandidateName = ResolveActorName(item.shortlistedIds[idx].raw());
                }
                ApplySinglePartnerAgencyOutcome(item, currentGameDay);
                continue;
            }

            const bool isOpen = snap->relationships->IsPairOpen(Foundation::ActorId::Player(), plan.partnerId);
            const auto relStatus = snap->relationships->GetStatus(Foundation::ActorId::Player(), plan.partnerId);

            auto& skyrimNet = GetSkyrimNetGateway();
            const std::uint64_t partnerUuid = skyrimNet.GetActorUUID(plan.partnerId.raw());
            const std::uint64_t sideLoverUuid =
                plan.relatedLoverId.IsNull() ? 0 : skyrimNet.GetActorUUID(plan.relatedLoverId.raw());
            const std::uint64_t playerUuid = skyrimNet.GetActorUUID(kPlayerFormID);

            nlohmann::json ctx;
            ctx["partnerName"] = item.partnerName;
            ctx["partnerUUID"] = partnerUuid;
            ctx["partnerRace"] = ResolveActorRaceName(plan.partnerId.raw());
            ctx["partnerSex"] = SexToDisplayString(ResolveActorSexEnum(plan.partnerId.raw()));
            ctx["partnerRelationships"] = FormatRelationshipsSummary(*snap, plan.partnerId);
            ctx["playerName"] = GetPlayerNameSafe();
            ctx["playerRelationshipStatus"] = std::string(Foundation::RelationshipStatusToString(relStatus));
            ctx["isOpenRelationship"] = isOpen;
            ctx["evalMode"] = item.evalMode;
            ctx["affairStage"] = item.affairStageStr;
            ctx["sideLoverName"] = item.sideLoverName;
            ctx["sideLoverUUID"] = sideLoverUuid;
            ctx["sideLoverRace"] = plan.relatedLoverId.IsNull() ? std::string() : ResolveActorRaceName(plan.relatedLoverId.raw());
            ctx["sideLoverSex"] = plan.relatedLoverId.IsNull()
                                      ? std::string()
                                      : SexToDisplayString(ResolveActorSexEnum(plan.relatedLoverId.raw()));
            ctx["partnerCurrentLocation"] = "Home";
            ctx["playerPresenceHint"] = "Away from home";
            ctx["sharedChildrenCount"] = 0;
            ctx["playerCheatedOnPartner"] = snap->affairs->HasCheatedOn(Foundation::ActorId::Player(), plan.partnerId);
            ctx["playerAffairPartnerName"] = "";
            ctx["sideLoverLocation"] = "Skyrim";
            ctx["sideLoverDurationHint"] = "has been ongoing";
            ctx["canConfess"] = item.canConfess;

            nlohmann::json recentParticipants = nlohmann::json::array();
            auto appendUniqueUuid = [&](std::uint64_t uuid) {
                if (uuid == 0) {
                    return;
                }
                for (const auto& existing : recentParticipants) {
                    if (existing.is_number_unsigned() && existing.get<std::uint64_t>() == uuid) {
                        return;
                    }
                }
                recentParticipants.push_back(uuid);
            };
            appendUniqueUuid(partnerUuid);
            appendUniqueUuid(playerUuid);
            if (!plan.relatedLoverId.IsNull()) {
                appendUniqueUuid(sideLoverUuid);
            }

            nlohmann::json candArr = nlohmann::json::array();
            std::string candText;
            for (const auto candId : item.shortlistedIds) {
                const std::string cName = ResolveActorName(candId.raw());
                const std::uint64_t cUuid = skyrimNet.GetActorUUID(candId.raw());
                if (plan.relatedLoverId.IsNull()) {
                    appendUniqueUuid(cUuid);
                }
                nlohmann::json ce;
                ce["name"] = cName;
                ce["formId"] = FormatFormId8Hex(candId.raw());
                ce["uuid"] = cUuid;
                ce["connectionTag"] = "Local acquaintance";
                ce["eligibility"] = "Available";
                candArr.push_back(std::move(ce));
                candText += std::format("- {}\n", cName);
            }
            ctx["recent_events_participants"] = std::move(recentParticipants);
            ctx["candidatesCount"] = candArr.size();
            ctx["candidatesList"] = candText;
            ctx["candidates"] = std::move(candArr);

            item.contextJson = std::move(ctx);
            llmTasks.push_back(std::move(item));
        }

        if (llmTasks.empty()) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            runningPartnerAgency_.store(false, std::memory_order_release);
            return;
        }

        LlmBatchCallbacks<PartnerAgencyBatchItem> callbacks;
        callbacks.submitTask = [](PartnerAgencyBatchItem& task, auto reportDone) -> bool {
            return GetSkyrimNetGateway().EvaluatePartnerAgency(
                task.contextJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                task.evalMode,
                task.canConfess,
                task.partnerName,
                task.sideLoverName,
                task.affairStageStr,
                [reportDone = std::move(reportDone)](const PartnerAgencyEvalResult& eval) {
                    if (!eval.success) {
                        const bool isInvalidJson = (eval.failureType == LLMFailureType::InvalidJson);
                        const std::string err = !eval.failureDetail.empty() ? eval.failureDetail : eval.reasoning;
                        reportDone(false, isInvalidJson, err, nullptr);
                        return;
                    }
                    reportDone(true, false, {}, [eval](PartnerAgencyBatchItem& item) {
                        item.result = eval;
                    });
                });
        };
        callbacks.formatFailureLine = [](const PartnerAgencyBatchItem& t, const std::string& reason) {
            return std::format("{} ({}): {}", t.partnerName, t.evalMode.empty() ? "agency" : t.evalMode, reason);
        };
        callbacks.onAllResolved = [this, currentGameDay](
                                      const std::shared_ptr<LlmBatchState<PartnerAgencyBatchItem>>& state) {
            SetLastPlayerPartnerCheatingDay(currentGameDay);
            for (const auto& item : state->tasks) {
                if (item.success) {
                    ApplySinglePartnerAgencyOutcome(item.payload, currentGameDay);
                }
            }
            runningPartnerAgency_.store(false, std::memory_order_release);
        };

        RunLlmBatch<PartnerAgencyBatchItem>(
            "Player-Partner Agency",
            "gamemaster_evaluation",
            std::move(llmTasks),
            std::move(callbacks),
            GetSessionEpoch());
    }

    // =========================================================================
    // Partner Home Arrival Event + Encounter Template Modal
    // =========================================================================

    bool AgencyWorkflowCoordinator::EvaluatePartnerHomeArrival(RE::BGSLocation* newLocation) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->playerPartnerAgencyEnableHomeArrivalEvents) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerLoc = newLocation ? newLocation : (player ? player->GetCurrentLocation() : nullptr);
        if (!playerLoc) {
            return false;
        }

        const float currentGameDay = GetCurrentGameDaysSafe();
        const float intentTTLDays = snap->settings->GetPartnerIntentTTLDays();
        if (snap->relationships && !snap->relationships->AllPendingIntents().empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                std::vector<Foundation::ActorId> expired;
                for (const auto& [pid, rec] : ws.relationships->AllPendingIntents()) {
                    if (rec.createdGameDay >= 0.0f && (currentGameDay - rec.createdGameDay) > intentTTLDays) {
                        expired.push_back(pid);
                    }
                }
                if (!expired.empty()) {
                    auto& rels = ws.relationships.Write();
                    for (const auto pid : expired) {
                        rels.ClearPendingIntent(pid);
                    }
                }
            });
        }

        const auto refreshedSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        if (!refreshedSnap || !spatialSnap) {
            return false;
        }

        const TavernRendezvousTarget tavern = FindNearbyTavernForRendezvous(playerLoc);
        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(std::random_device{}()));

        const auto planOpt = Domain::Agency::EvaluatePartnerHomeArrival(
            *refreshedSnap,
            *spatialSnap,
            Foundation::LocationId{ playerLoc->GetFormID() },
            tavern.found,
            GetCurrentHourOfDaySafe(),
            currentGameDay,
            rng);
        if (!planOpt.has_value()) {
            return false;
        }

        auto plan = *planOpt;
        const RE::FormID partnerId = plan.partnerId.raw();
        const RE::FormID loverId = plan.relatedLoverId.raw();
        const bool isPriorityIntent = (plan.kind == Domain::Agency::HomeArrivalKind::PendingIntentForcegreet);

        bool isLetterScenario =
            (plan.kind == Domain::Agency::HomeArrivalKind::OpenDateLetter ||
             plan.kind == Domain::Agency::HomeArrivalKind::CheatingTalkingLetter ||
             plan.kind == Domain::Agency::HomeArrivalKind::CheatingPhysicalLetter);
        if (isLetterScenario && !tavern.found) {
            // Downgrade cleanly if tavern rendezvous target could not be resolved.
            isLetterScenario = false;
        }

        const std::string scenario = ResolveHomeArrivalScenarioId(plan, isLetterScenario);
        if (scenario.empty()) {
            return false;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.cooldowns.Write().lastHomeArrivalEventDayByLoc[Foundation::LocationId{ playerLoc->GetFormID() }] =
                currentGameDay;
        });

        nlohmann::json payload;
        payload["scenario"] = scenario;
        payload["partnerFormId"] = FormatFormId8Hex(partnerId);
        payload["loverFormId"] = loverId != 0 ? FormatFormId8Hex(loverId) : std::string();
        payload["targetLocationFormId"] = isLetterScenario ? FormatFormId8Hex(tavern.innLocationID) : std::string();
        payload["targetMarkerFormId"] = isLetterScenario ? FormatFormId8Hex(tavern.centerMarkerID) : std::string();
        payload["targetLocationName"] = isLetterScenario
                                            ? tavern.displayName
                                            : ResolveHouseDisplayName(refreshedSnap->household
                                                                          ? refreshedSnap->household->GetTenantHouse(partnerId).raw()
                                                                          : 0);

        if (isLetterScenario) {
            const auto& catalog = Engine::EncounterConfigLoader::GetSingleton().GetCatalog();
            const Domain::Agency::EncounterTemplateSpec* tmpl = catalog.FindTemplate(scenario);
            const std::string partnerName = ResolveActorName(partnerId);
            const std::string playerName = GetPlayerNameSafe();
            const std::string loverName = loverId != 0 ? ResolveActorName(loverId) : std::string();

            Domain::Agency::RenderedEncounterModal rendered;
            if (tmpl) {
                rendered = Domain::Agency::RenderEncounterTemplate(
                    *tmpl, plan.partnerTemperament, partnerName, playerName, tavern.displayName, loverName);
            } else {
                MARAS_LOG_WARN("[AgencyWorkflowCoordinator] Encounter template '{}' missing from catalog; synthesizing minimal fallback modal",
                               scenario);
                rendered = SynthesizeMinimalEncounterFallback(scenario, partnerName, playerName, tavern.displayName);
            }

            ModalRequest req{};
            req.modalType = "generic";
            req.title = rendered.modalTitle;
            req.subtitle = rendered.modalSubtitle;
            req.message = rendered.envelopeMessage;
            req.tone = ParseModalTone(rendered.modalTone);
            req.size = ParseModalSize(rendered.modalSize);
            req.defaultCancelButtonIndex = rendered.defaultCancelButtonIndex;
            req.controls.push_back(ModalControl::MakeInfoBox(
                rendered.infoBoxId,
                rendered.infoBoxTitle,
                rendered.letterBody,
                req.tone));
            req.buttons.push_back(ModalButton::Make(
                rendered.buttonId.empty() ? std::string("close") : rendered.buttonId,
                rendered.buttonLabel.empty() ? std::string("Put Note Away") : rendered.buttonLabel,
                ModalButtonVariant::Primary));

            GetModalGateway().ShowModal(std::move(req), nullptr);
        }

        EmitModCallbackEvent(
            "maras_partner_home_arrival_event",
            payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
            isPriorityIntent ? 1.0f : 0.0f,
            partnerId);

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Dispatched maras_partner_home_arrival_event: scenario='{}', partner={:08X}, lover={:08X}, loc={:08X}",
                       scenario, partnerId, loverId, playerLoc->GetFormID());
        return true;
    }

    // =========================================================================
    // NPC-NPC Banter Coordination
    // =========================================================================

    void AgencyWorkflowCoordinator::TryScheduledBanter(float currentGameDay) {
        if (!GetSkyrimNetGateway().IsAvailable()) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->enableScenes || IsBanterSceneOnCooldown(currentGameDay)) {
            return;
        }
        (void)TryTriggerBanter();
    }

    bool AgencyWorkflowCoordinator::TryTriggerBanter() {
        if (!GetSkyrimNetGateway().IsAvailable()) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->settings || !snap->settings->enableScenes) {
            return false;
        }
        const float currentDay = GetCurrentGameDaysSafe();
        if (IsBanterSceneOnCooldown(currentDay)) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerLoc = player ? player->GetCurrentLocation() : nullptr;
        if (!BanterSceneStarter::GetSingleton().IsLocationAllowedForScenes(playerLoc)) {
            return false;
        }

        const auto couple = FindBanterCoupleCandidates(snap->settings->skyrimnetActionRadius);
        if (couple.size() < 2 || !couple[0] || !couple[1]) {
            return false;
        }

        const RE::FormID idA = couple[0]->GetFormID();
        const RE::FormID idB = couple[1]->GetFormID();
        if (!IsSceneSettingEnabledForPair(*snap->settings, *snap, idA, idB)) {
            return false;
        }

        const bool partnerPresent = BanterSceneStarter::GetSingleton().IsBanterPartnerPresent(
            couple[1], snap->settings->skyrimnetActionRadius);
        nlohmann::json payload;
        payload["actorA"] = FormatFormId8Hex(idA);
        payload["actorB"] = FormatFormId8Hex(idB);
        payload["partnerPresent"] = partnerPresent;

        EmitModCallbackEvent(
            "maras_banter_scene_trigger",
            payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
            partnerPresent ? 1.0f : 0.0f,
            idA);

        RecordScenePlayed(couple[0], couple[1]);
        return true;
    }

    std::vector<RE::Actor*> AgencyWorkflowCoordinator::FindBanterCoupleCandidates(float scanRadius) const {
        return BanterSceneStarter::GetSingleton().FindBanterCoupleCandidates(scanRadius);
    }

    void AgencyWorkflowCoordinator::RecordScenePlayed(RE::Actor* actorA, RE::Actor* actorB) {
        const float currentDay = GetCurrentGameDaysSafe();
        const RE::FormID idA = actorA ? actorA->GetFormID() : 0;
        const RE::FormID idB = actorB ? actorB->GetFormID() : 0;

        State::WorldStore::GetSingleton().Modify([currentDay, idA, idB](State::WorldState& ws) {
            auto& cd = ws.cooldowns.Write();
            cd.lastBanterSceneDay = currentDay;
            if (idA != 0 && idB != 0) {
                cd.banterCoupleCooldowns[State::CanonicalPairKey(
                    Foundation::ActorId{ idA }, Foundation::ActorId{ idB })] = currentDay;
            }
        });

        MARAS_LOG_INFO("[AgencyWorkflowCoordinator] Recorded banter scene played ({:08X}, {:08X}) on day {:.3f}",
                       idA, idB, currentDay);
    }

    bool AgencyWorkflowCoordinator::IsBanterSceneOnCooldown(float currentDay) const {
        const float cooldownHours = GetBanterCooldownHours();
        if (cooldownHours <= 0.0f) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float lastSceneDay = snap->cooldowns ? snap->cooldowns->lastBanterSceneDay : -1.0f;
        if (lastSceneDay < 0.0f || currentDay < lastSceneDay) {
            return false;
        }
        return (currentDay - lastSceneDay) < (cooldownHours / 24.0f);
    }

    bool AgencyWorkflowCoordinator::IsCoupleOnBanterCooldown(RE::FormID actorA,
                                                             RE::FormID actorB,
                                                             float currentDay) const {
        const float cooldownHours = GetBanterCooldownHours() * kBanterCoupleCooldownMultiplier;
        if (cooldownHours <= 0.0f) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->cooldowns) {
            return false;
        }
        const State::CanonicalPairKey key(Foundation::ActorId{ actorA }, Foundation::ActorId{ actorB });
        const auto it = snap->cooldowns->banterCoupleCooldowns.find(key);
        if (it == snap->cooldowns->banterCoupleCooldowns.end()) {
            return false;
        }
        if (currentDay < it->second) {
            return false;
        }
        return (currentDay - it->second) < (cooldownHours / 24.0f);
    }

    void AgencyWorkflowCoordinator::SetBanterCooldownHours(float hours) {
        const float clamped = std::max(0.0f, hours);
        State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
            ws.settings.Write().scenesCooldownHours = clamped;
            ws.cooldowns.Write().savedBanterCooldownHours = clamped;
        });
    }

    float AgencyWorkflowCoordinator::GetBanterCooldownHours() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->settings ? std::max(0.0f, snap->settings->scenesCooldownHours) : 24.0f;
    }

    void AgencyWorkflowCoordinator::Revert() {
        ClearRunningBatchState();
        MARAS_LOG_DEBUG("[AgencyWorkflowCoordinator] Reverted transient agency workflow state");
    }

}  // namespace MARAS::App
