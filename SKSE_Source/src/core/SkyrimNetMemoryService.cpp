#include "core/SkyrimNetMemoryService.h"

#include "core/DeceasedRegistry.h"
#include "core/HomeCellService.h"
#include "core/LoggingService.h"
#include "core/NPCIndex.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerHouseService.h"
#include "core/SkyrimNetIntegration.h"
#include "services/ActorService.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/EnumUtils.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

namespace MARAS {

    namespace {

        std::string SafeJsonDump(const nlohmann::json& j) {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        std::string GetJsonString(const nlohmann::json& obj, std::string_view key, std::string_view defaultVal = "") {
            if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
                return obj[key].get<std::string>();
            }
            return std::string(defaultVal);
        }

        float GetJsonFloat(const nlohmann::json& obj, std::string_view key, float defaultVal = 0.0f) {
            if (obj.is_object() && obj.contains(key) && obj[key].is_number()) {
                return obj[key].get<float>();
            }
            return defaultVal;
        }

        const nlohmann::json* FindMemoriesJsonArray(const nlohmann::json& j) {
            if (j.is_array()) {
                return &j;
            }
            if (j.is_object() && j.contains("memories") && j["memories"].is_array()) {
                return &j["memories"];
            }
            return nullptr;
        }

        RE::FormID ParseHexFormID(std::string_view str) {
            if (str.empty()) return 0;
            if (str.starts_with("0x") || str.starts_with("0X")) {
                str.remove_prefix(2);
            }
            RE::FormID parsed = 0;
            const auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), parsed, 16);
            return (ec == std::errc{}) ? parsed : 0;
        }

        int GetCurrentGameDayInt() {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return static_cast<int>(calendar->GetDaysPassed());
            }
            return 0;
        }

        void ReplaceAllInPlace(std::string& str, std::string_view from, std::string_view to) {
            if (from.empty()) return;
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
        }

        std::string ConvertPerspectiveNoteToFirstPerson(std::string text) {
            if (text.empty()) return text;

            if (text.starts_with("You and ") || text.find(", you and ") != std::string::npos) {
                ReplaceAllInPlace(text, "open your relationship", "open our relationship");
                ReplaceAllInPlace(text, "closed your relationship", "closed our relationship");
                ReplaceAllInPlace(text, "into your family", "into our family");
            }

            static constexpr std::pair<std::string_view, std::string_view> kPhraseReplacements[] = {
                {"You were ", "I was "},
                {"you were ", "I was "},
                {"You are ", "I am "},
                {"you are ", "I am "},
                {"You and your partner ", "I and my partner "},
                {"You and ", "I and "},
                {", you and ", ", I and "},
                {"the two of you", "the two of us"},
                {"thought was yours", "thought was mine"},
                {"is yours", "is mine"},
                {"not you.", "not me."},
                {"not you,", "not me,"},
                {"not you)", "not me)"},
                {"with you", "with me"},
                {"to you", "to me"},
                {"on you", "on me"},
                {"caught you", "caught me"},
                {"confronted you", "confronted me"},
                {"divorced you", "divorced me"},
                {"leaving you", "leaving me"},
                {"raising you", "raising me"},
                {"; you ", "; I "},
                {", you ", ", I "},
                {"that you ", "that I "},
                {"while you ", "while I "},
                {"unless you ", "unless I "},
                {"and you ", "and I "},
                {"yet you ", "yet I "},
                {" you thought ", " I thought "},
                {"Your ", "My "},
                {"your ", "my "},
            };

            for (const auto& [from, to] : kPhraseReplacements) {
                ReplaceAllInPlace(text, from, to);
            }

            if (text.starts_with("You ")) {
                text.replace(0, 4, "I ");
            }

            return text;
        }

        std::string NormalizeTagToken(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            bool prevUnderscore = false;
            for (unsigned char ch : sv) {
                if (std::isalnum(ch)) {
                    out.push_back(static_cast<char>(std::tolower(ch)));
                    prevUnderscore = false;
                } else if (!out.empty() && !prevUnderscore) {
                    out.push_back('_');
                    prevUnderscore = true;
                }
            }
            if (!out.empty() && out.back() == '_') {
                out.pop_back();
            }
            return out;
        }

        void DeriveProgrammaticMetadata(
            const std::string& category,
            const std::string& eventType,
            const std::string& role,
            std::string& outType,
            std::string& outEmotion,
            float& outImportance)
        {
            if (category == "pregnancy_discovered" || category == "paternity_reveal" ||
                category == "child_birth" || category == "child_custody") {
                outType = "FAMILY";
            } else {
                outType = "RELATIONSHIP";
            }

            outEmotion = "content";
            outImportance = 0.70f;

            if (category == "relationship_change") {
                if (eventType == "married") {
                    outEmotion = "joyful";
                    outImportance = 0.85f;
                } else if (eventType == "engaged") {
                    outEmotion = "happy";
                    outImportance = 0.78f;
                } else if (eventType == "lover") {
                    if (role == "secret_lover" || role == "secret_affair_partner") {
                        outEmotion = "excited";
                        outImportance = 0.75f;
                    } else {
                        outEmotion = "loving";
                        outImportance = 0.72f;
                    }
                } else if (eventType == "dating") {
                    outEmotion = "happy";
                    outImportance = 0.68f;
                } else if (eventType == "open_relationship" || eventType == "close_relationship") {
                    outEmotion = "content";
                    outImportance = 0.68f;
                } else if (eventType == "divorced" || eventType == "jilted" ||
                           eventType == "exlover" || eventType == "exdating") {
                    outImportance = (eventType == "divorced" || eventType == "jilted") ? 0.85f : 0.72f;
                    outEmotion = (role == "breakup_initiator") ? "resolute" : "heartbroken";
                }
            } else if (category == "infidelity") {
                outImportance = (eventType == "caught" || eventType == "exposed" || eventType == "breakup") ? 0.85f : 0.78f;
                if (role == "betrayed_partner" || role == "betrayed_partner_leaving") {
                    outEmotion = "betrayed";
                } else if (role == "confronting_partner") {
                    outEmotion = "angry";
                } else if (role == "forgiving_partner" || role == "forgiven_cheater" || role == "accepted_lover") {
                    outEmotion = "relieved";
                } else if (role == "dumped_cheater" || role == "discarded_affair_partner") {
                    outEmotion = "heartbroken";
                } else if (role == "affair_exposer") {
                    outEmotion = "determined";
                } else {
                    outEmotion = "guilty";
                }
            } else if (category == "pregnancy_discovered") {
                outImportance = 0.78f;
                outEmotion = (role == "mother_discovering_secret_pregnancy") ? "anxious" : "joyful";
            } else if (category == "paternity_reveal") {
                outImportance = 0.85f;
                if (role == "expectant_father_unaware_of_secret") {
                    outEmotion = "joyful";
                } else if (role == "mother_hiding_paternity") {
                    outEmotion = "guilty";
                } else if (role == "deceived_partner") {
                    outEmotion = "betrayed";
                } else if (role == "biological_father") {
                    outEmotion = "shocked";
                } else {
                    outEmotion = "anxious";
                }
            } else if (category == "child_birth") {
                outImportance = 0.88f;
                if (role == "mother_with_secret") {
                    outEmotion = "anxious";
                } else if (role == "knowingly_accepting_partner") {
                    outEmotion = "accepting";
                } else if (role == "secret_biological_father") {
                    outEmotion = "bittersweet";
                } else {
                    outEmotion = "joyful";
                }
            } else if (category == "child_custody") {
                outImportance = (eventType.starts_with("adopted") || eventType.starts_with("inherited")) ? 0.85f : 0.75f;
                if (role == "bereaved_child" || role == "orphaned_child" || role == "sole_surviving_guardian" ||
                    role == "inheriting_guardian" || role == "surviving_guardian") {
                    outEmotion = "grieving";
                } else if (role == "former_guardian_lost_custody" || role == "former_guardian" || role == "child_in_divorce") {
                    outEmotion = "sad";
                } else {
                    outEmotion = "happy";
                }
            }
        }

    }  // namespace

    bool SkyrimNetMemoryService::CanActorGenerateMemory(RE::FormID formID) {
        if (formID == 0 || formID == kPlayerFormID || formID == 0x07 || formID == kUnknownFatherFormID) {
            return false;
        }
        if (IsSyntheticChildID(formID)) {
            return false;
        }
        if (DeceasedRegistry::GetSingleton().IsDeceased(formID)) {
            return false;
        }
        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            if (actor->IsDead() || actor->IsDeleted()) {
                return false;
            }
        }
        return true;
    }

    std::string SkyrimNetMemoryService::ResolveBestLocation(const std::vector<RE::FormID>& actorIDs) {
        auto tryResolveActorLocation = [](RE::FormID id) -> std::string {
            if (id == 0 || IsSyntheticChildID(id)) return "";
            RE::Actor* actor = (id == kPlayerFormID)
                                   ? RE::PlayerCharacter::GetSingleton()
                                   : RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                actor = NPCIndex::GetSingleton().GetActorFromFormId(id);
            }
            if (actor) {
                if (auto* cell = actor->GetParentCell()) {
                    const char* cellName = cell->GetName();
                    if (cellName && cellName[0] != '\0') {
                        if (auto* loc = actor->GetCurrentLocation()) {
                            const char* locName = loc->GetName();
                            if (locName && locName[0] != '\0' && std::string_view(locName) != std::string_view(cellName)) {
                                return std::format("{} ({})", cellName, locName);
                            }
                        }
                        return cellName;
                    }
                }
                if (auto* loc = actor->GetCurrentLocation()) {
                    const char* locName = loc->GetName();
                    if (locName && locName[0] != '\0') {
                        return locName;
                    }
                }
            }
            RE::FormID houseID = PlayerHouseService::GetSingleton().GetTenantHouseFormID(id);
            if (houseID != 0) {
                std::string hName = PlayerHouseService::GetHouseName(houseID);
                if (!hName.empty()) return hName;
            }
            if (const auto* prof = NPCRelationshipManager::GetSingleton().GetNPCProfile(id)) {
                if (prof->currentHome.has_value() && *prof->currentHome != 0) {
                    std::string hName = PlayerHouseService::GetHouseName(*prof->currentHome);
                    if (!hName.empty()) return hName;
                }
                if (prof->originalHome.has_value() && *prof->originalHome != 0) {
                    if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(*prof->originalHome)) {
                        if (loc->GetName() && loc->GetName()[0] != '\0') return loc->GetName();
                    }
                    if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(*prof->originalHome)) {
                        if (cell->GetName() && cell->GetName()[0] != '\0') return cell->GetName();
                    }
                }
            }
            return "";
        };

        for (RE::FormID id : actorIDs) {
            if (id != kPlayerFormID) {
                std::string loc = tryResolveActorLocation(id);
                if (!loc.empty()) return loc;
            }
        }
        for (RE::FormID id : actorIDs) {
            std::string loc = tryResolveActorLocation(id);
            if (!loc.empty()) return loc;
        }
        return "Skyrim";
    }

    MemoryParticipant SkyrimNetMemoryService::MakeParticipant(
        RE::FormID formID,
        const std::string& role,
        const std::string& perspectiveNote,
        bool allowGenerateMemory,
        const std::string& knowledgeRestriction,
        std::unordered_set<RE::FormID> hiddenActorFormIDs)
    {
        MemoryParticipant p;
        p.formID = formID;
        if (formID != 0 && IsSyntheticChildID(formID)) {
            if (auto childOpt = NPCRelationshipManager::GetSingleton().GetChildEntry(formID)) {
                if (!childOpt->name.empty() && !StringUtils::CaseInsensitiveEquals(childOpt->name, "Newborn")) {
                    p.name = childOpt->name;
                } else {
                    p.name = "child";
                }
            } else {
                p.name = "child";
            }
            p.generateMemory = false;
        } else if (formID != 0) {
            p.name = ActorSvc::GetName(formID);
            if (p.name.empty()) {
                if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(formID)) {
                    p.name = dec->name;
                }
            }
            if (p.name.empty()) {
                p.name = std::format("NPC (0x{:08X})", formID);
            }
            p.generateMemory = allowGenerateMemory && CanActorGenerateMemory(formID);
        }
        p.role = role;
        p.perspectiveNote = perspectiveNote;
        p.knowledgeRestriction = knowledgeRestriction;
        p.hiddenActorFormIDs = std::move(hiddenActorFormIDs);
        return p;
    }

    std::string SkyrimNetMemoryService::BuildActorRelationshipsSummary(RE::FormID actorFormID) const {
        if (actorFormID == 0 || IsSyntheticChildID(actorFormID)) {
            return "None";
        }
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        auto partners = mgr.GetAllPartners(actorFormID);

        bool hasOpenMainPartner = mgr.HasAnyOpenMainPair(actorFormID);
        std::ostringstream ss;
        bool first = true;

        for (RE::FormID partnerId : partners) {
            auto status = mgr.GetRelationshipStatus(actorFormID, partnerId);
            if (status == RelationshipStatus::Candidate || status == RelationshipStatus::Unknown) {
                continue;
            }

            std::string partnerName = ActorSvc::GetName(partnerId);
            const auto* pairData = mgr.GetNPCData(actorFormID, partnerId);
            auto children = mgr.GetChildren(actorFormID, partnerId);
            size_t childCount = children.size();
            std::string statusStr = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(status));
            bool isOpen = pairData ? pairData->isOpen : false;

            if (!first) ss << "\n";
            first = false;

            if (status == RelationshipStatus::Lover) {
                std::string loverPerm = hasOpenMainPartner ? "allowed by open main relationship" : "secret affair / lover";
                ss << std::format("- Lover with {} ({}, {} {})", partnerName, loverPerm, childCount, childCount == 1 ? "child" : "children");
            } else if (NPCRelationshipManager::IsMainTier(status)) {
                std::string perm = isOpen ? "open primary partner" : "exclusive primary partner";
                ss << std::format("- {} with {} ({}, {} {})", statusStr, partnerName, perm, childCount, childCount == 1 ? "child" : "children");
            } else {
                ss << std::format("- {} with {}", statusStr, partnerName);
            }
        }
        return ss.str().empty() ? "None (single / no active relationships)" : ss.str();
    }

    bool SkyrimNetMemoryService::CanMergeWithEpisode(const MemoryEpisode& episode, const MemoryEvent& event) const {
        for (const auto& p : event.participants) {
            if (p.formID != 0 && episode.participantFormIDs.contains(p.formID)) {
                return true;
            }
        }
        return false;
    }

    void SkyrimNetMemoryService::MergeEpisodes(MemoryEpisode& dst, MemoryEpisode&& src) {
        if (src.firstEventTime < dst.firstEventTime) {
            dst.firstEventTime = src.firstEventTime;
        }
        if (src.flushTime > dst.flushTime) {
            dst.flushTime = src.flushTime;
        }
        for (auto& ev : src.events) {
            dst.events.push_back(std::move(ev));
        }
        dst.participantFormIDs.insert(src.participantFormIDs.begin(), src.participantFormIDs.end());
        dst.targetFormIDs.insert(src.targetFormIDs.begin(), src.targetFormIDs.end());
        dst.isMatchmaking = dst.isMatchmaking && src.isMatchmaking;
    }

    void SkyrimNetMemoryService::RecordEvent(MemoryEvent event) {
        if (IsSuppressed() || !IsSkyrimNetAvailable()) {
            return;
        }

        bool hasAnyTarget = false;
        for (const auto& p : event.participants) {
            if (p.generateMemory && p.formID != 0) {
                hasAnyTarget = true;
                break;
            }
        }
        if (!hasAnyTarget) {
            MARAS_LOG_DEBUG("[MemoryService] Skipping event '{}' / '{}': no non-Player living NPC targets",
                            event.eventCategory, event.eventType);
            return;
        }

        if (IsMatchmakingApplyActive()) {
            event.isMatchmaking = true;
        }
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<size_t> matchingIndices;
        for (size_t i = 0; i < activeEpisodes_.size(); ++i) {
            if (CanMergeWithEpisode(activeEpisodes_[i], event)) {
                matchingIndices.push_back(i);
            }
        }

        if (matchingIndices.empty()) {
            MemoryEpisode ep;
            ep.episodeID = nextEpisodeID_++;
            ep.firstEventTime = now;
            ep.isMatchmaking = event.isMatchmaking;
            const auto& settings = Settings::GetSingleton();
            const bool useLLM = ep.isMatchmaking ? settings.matchmakingGenerateMemories : settings.skyrimnetUseLLMMemories;
            const auto graceWindow = useLLM ? kEpisodeGraceWindow : kProgrammaticGraceWindow;
            ep.flushTime = now + graceWindow;
            for (const auto& p : event.participants) {
                if (p.formID != 0) {
                    ep.participantFormIDs.insert(p.formID);
                    if (p.generateMemory) {
                        ep.targetFormIDs.insert(p.formID);
                    }
                }
            }
            MARAS_LOG_INFO("[MemoryService] Created Episode #{} for '{}' / '{}' ({} target(s), flushes in {}s)",
                           ep.episodeID, event.eventCategory, event.eventType, ep.targetFormIDs.size(), graceWindow.count());
            ep.events.push_back(std::move(event));
            activeEpisodes_.push_back(std::move(ep));
        } else {
            size_t primaryIdx = matchingIndices.front();
            auto& primaryEp = activeEpisodes_[primaryIdx];

            // Merge any additional bridged episodes in reverse index order
            for (size_t k = matchingIndices.size() - 1; k >= 1; --k) {
                size_t mergeIdx = matchingIndices[k];
                MergeEpisodes(primaryEp, std::move(activeEpisodes_[mergeIdx]));
                activeEpisodes_.erase(activeEpisodes_.begin() + static_cast<std::ptrdiff_t>(mergeIdx));
            }

            for (const auto& p : event.participants) {
                if (p.formID != 0) {
                    primaryEp.participantFormIDs.insert(p.formID);
                    if (p.generateMemory) {
                        primaryEp.targetFormIDs.insert(p.formID);
                    }
                }
            }

            MARAS_LOG_INFO("[MemoryService] Appended '{}' / '{}' to Episode #{} (now {} event(s), {} target(s))",
                           event.eventCategory, event.eventType, primaryEp.episodeID,
                           primaryEp.events.size() + 1, primaryEp.targetFormIDs.size());
            primaryEp.isMatchmaking = primaryEp.isMatchmaking && event.isMatchmaking;
            primaryEp.events.push_back(std::move(event));
        }
    }

    void SkyrimNetMemoryService::Tick() {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        std::vector<MemoryEpisode> readyToFlush;
        {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeEpisodes_.empty()) {
                return;
            }
            auto it = activeEpisodes_.begin();
            while (it != activeEpisodes_.end()) {
                const auto& settings = Settings::GetSingleton();
                const bool useLLM = it->isMatchmaking ? settings.matchmakingGenerateMemories : settings.skyrimnetUseLLMMemories;
                const auto effectiveFlushTime = useLLM
                                                    ? it->flushTime
                                                    : (std::min)(it->flushTime, it->firstEventTime + kProgrammaticGraceWindow);
                if (now >= effectiveFlushTime) {
                    readyToFlush.push_back(std::move(*it));
                    it = activeEpisodes_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (auto& ep : readyToFlush) {
            DispatchEpisodeToLLM(std::move(ep));
        }
    }

    void SkyrimNetMemoryService::Revert() {
        std::lock_guard<std::mutex> lock(mutex_);
        activeEpisodes_.clear();
        nextEpisodeID_ = 1;
        MARAS_LOG_INFO("[MemoryService] Cleared pending memory episodes on Revert");
    }

    nlohmann::json SkyrimNetMemoryService::BuildEpisodePromptContext(const MemoryEpisode& episode) const {
        nlohmann::json ctx;
        ctx["gameDay"] = GetCurrentGameDayInt();

        // Collect unique participants preserving order of appearance
        struct AggregatedParticipant {
            RE::FormID formID = 0;
            std::string name;
            bool generateMemory = false;
            std::vector<std::string> roles;
            std::vector<std::string> perspectiveNotes;
            std::vector<std::string> knowledgeRestrictions;
            std::unordered_set<size_t> participatedEventIndices;
        };

        std::vector<AggregatedParticipant> orderedParticipants;
        std::unordered_map<RE::FormID, size_t> formIdToIndex;
        std::unordered_map<std::string, size_t> syntheticNameToIndex;

        auto addOrUpdateParticipant = [&](const MemoryParticipant& mp, size_t eventNumber) {
            size_t idx = 0;
            if (mp.formID != 0) {
                auto it = formIdToIndex.find(mp.formID);
                if (it == formIdToIndex.end()) {
                    idx = orderedParticipants.size();
                    formIdToIndex[mp.formID] = idx;
                    AggregatedParticipant ap;
                    ap.formID = mp.formID;
                    ap.name = mp.name;
                    ap.generateMemory = mp.generateMemory;
                    orderedParticipants.push_back(std::move(ap));
                } else {
                    idx = it->second;
                    if (mp.generateMemory) orderedParticipants[idx].generateMemory = true;
                }
            } else {
                auto it = syntheticNameToIndex.find(mp.name);
                if (it == syntheticNameToIndex.end()) {
                    idx = orderedParticipants.size();
                    syntheticNameToIndex[mp.name] = idx;
                    AggregatedParticipant ap;
                    ap.formID = 0;
                    ap.name = mp.name;
                    ap.generateMemory = false;
                    orderedParticipants.push_back(std::move(ap));
                } else {
                    idx = it->second;
                }
            }

            auto& agg = orderedParticipants[idx];
            agg.participatedEventIndices.insert(eventNumber);

            if (!mp.role.empty() && std::find(agg.roles.begin(), agg.roles.end(), mp.role) == agg.roles.end()) {
                agg.roles.push_back(mp.role);
            }
            if (!mp.perspectiveNote.empty()) {
                std::string noteWithStep = (episode.events.size() > 1)
                                               ? std::format("[Event {}] {}", eventNumber, mp.perspectiveNote)
                                               : mp.perspectiveNote;
                agg.perspectiveNotes.push_back(std::move(noteWithStep));
            }
            if (!mp.knowledgeRestriction.empty() &&
                std::find(agg.knowledgeRestrictions.begin(), agg.knowledgeRestrictions.end(), mp.knowledgeRestriction) ==
                    agg.knowledgeRestrictions.end()) {
                agg.knowledgeRestrictions.push_back(mp.knowledgeRestriction);
            }
        };

        nlohmann::json eventsJson = nlohmann::json::array();
        for (size_t i = 0; i < episode.events.size(); ++i) {
            const auto& ev = episode.events[i];
            std::string involvedNames;
            for (size_t pIdx = 0; pIdx < ev.participants.size(); ++pIdx) {
                if (pIdx > 0) involvedNames += ", ";
                involvedNames += ev.participants[pIdx].name;
            }

            nlohmann::json evObj;
            evObj["index"] = i + 1;
            evObj["category"] = ev.eventCategory;
            evObj["type"] = ev.eventType;
            evObj["headline"] = ev.eventHeadline;
            evObj["location"] = ev.location.empty() ? "Skyrim" : ev.location;
            evObj["involvedNames"] = involvedNames;
            eventsJson.push_back(std::move(evObj));

            for (const auto& mp : ev.participants) {
                addOrUpdateParticipant(mp, i + 1);
            }
        }
        ctx["events"] = std::move(eventsJson);

        // When multiple events merge into one episode, explicitly restrict each target from knowing about
        // any event in the episode that they did not participate in.
        if (episode.events.size() > 1) {
            for (auto& ap : orderedParticipants) {
                if (!ap.generateMemory) continue;
                for (size_t i = 0; i < episode.events.size(); ++i) {
                    size_t evNum = i + 1;
                    if (!ap.participatedEventIndices.contains(evNum)) {
                        ap.knowledgeRestrictions.push_back(std::format(
                            "[Event {}] {} was NOT present for and DOES NOT KNOW about Event {} (\"{}\"). Do NOT mention, hint at, or leak anything from Event {} into {}'s memory or tags.",
                            evNum, ap.name, evNum, episode.events[i].eventHeadline, evNum, ap.name));
                    }
                }
            }
        }

        // Also automatically add knowledgeRestrictions for any target NPC who is the unaware partner
        // of an undiscovered infidelity or paternity secret involving another participant in this episode.
        const auto& relMgr = NPCRelationshipManager::GetSingleton();
        for (auto& ap : orderedParticipants) {
            if (!ap.generateMemory || ap.formID == 0) continue;
            for (const auto& otherP : orderedParticipants) {
                if (otherP.formID == 0 || otherP.formID == ap.formID) continue;
                for (const auto& rec : relMgr.GetCheatingRecordsForActor(otherP.formID)) {
                    if (rec.cheaterID == otherP.formID && rec.victimID == ap.formID && rec.isSecret && rec.affairStatus == AffairStatus::Ongoing) {
                        std::string affairName = rec.affairPartnerID != 0 ? ActorSvc::GetName(rec.affairPartnerID) : "a secret lover";
                        std::string restr = std::format(
                            "{} DOES NOT KNOW that {} is having a secret affair with {}. NEVER mention, hint at, or leak {}'s affair with {} into {}'s memory or tags.",
                            ap.name, otherP.name, affairName, otherP.name, affairName, ap.name);
                        if (std::find(ap.knowledgeRestrictions.begin(), ap.knowledgeRestrictions.end(), restr) ==
                            ap.knowledgeRestrictions.end()) {
                            ap.knowledgeRestrictions.push_back(std::move(restr));
                        }
                    }
                }
            }
        }

        nlohmann::json participantsJson = nlohmann::json::array();
        nlohmann::json recentEventsParticipantsJson = nlohmann::json::array();
        nlohmann::json targetsJson = nlohmann::json::array();

        for (const auto& ap : orderedParticipants) {
            nlohmann::json pObj;
            pObj["name"] = ap.name;

            if (ap.formID != 0 && !IsSyntheticChildID(ap.formID)) {
                auto entry = NPCIndex::GetSingleton().FindByFormID(ap.formID);
                int sexInt = entry ? entry->sex : ActorSvc::GetSex(ap.formID);
                std::string race = (entry && !entry->raceName.empty()) ? entry->raceName : ActorSvc::GetRaceName(ap.formID);
                if (race.empty()) race = "Nord";

                pObj["sex"] = (sexInt == 1) ? "female" : "male";
                pObj["race"] = race;
                const uint64_t actorUuid = GetActorUUID(ap.formID);
                pObj["uuid"] = (ap.formID == kPlayerFormID) ? 0ULL : actorUuid;
                pObj["relationships"] = BuildActorRelationshipsSummary(ap.formID);
                if (actorUuid != 0) {
                    recentEventsParticipantsJson.push_back(actorUuid);
                }
            } else {
                pObj["sex"] = "unknown";
                pObj["race"] = "Unknown";
                pObj["uuid"] = 0ULL;
                pObj["relationships"] = "None";
            }
            participantsJson.push_back(std::move(pObj));

            if (ap.generateMemory && ap.formID != 0) {
                nlohmann::json tObj;
                tObj["name"] = ap.name;
                tObj["formId"] = std::format("0x{:08X}", ap.formID);

                std::string roleJoined;
                for (size_t r = 0; r < ap.roles.size(); ++r) {
                    if (r > 0) roleJoined += " -> ";
                    roleJoined += ap.roles[r];
                }
                tObj["roleSummary"] = roleJoined.empty() ? "Participant" : roleJoined;
                tObj["perspectiveNotes"] = ap.perspectiveNotes.empty()
                                               ? nlohmann::json::array({ "Write a first-person memory of this event from your point of view." })
                                               : nlohmann::json(ap.perspectiveNotes);
                tObj["knowledgeRestrictions"] = ap.knowledgeRestrictions;
                targetsJson.push_back(std::move(tObj));
            }
        }

        ctx["participants"] = std::move(participantsJson);
        ctx["recent_events_participants"] = std::move(recentEventsParticipantsJson);
        ctx["targets"] = std::move(targetsJson);
        return ctx;
    }

    void SkyrimNetMemoryService::DispatchEpisodeToLLM(MemoryEpisode episode) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        // Collect ordered target FormIDs and per-target relatedActorFormIDs (excluding hiddenActorFormIDs)
        std::vector<RE::FormID> targetActors;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> relatedActorsByTarget;
        std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> hiddenActorsByTarget;

        for (const auto& ev : episode.events) {
            std::vector<RE::FormID> eventRealActors;
            for (const auto& p : ev.participants) {
                if (p.formID != 0 && !IsSyntheticChildID(p.formID)) {
                    eventRealActors.push_back(p.formID);
                }
                if (p.formID != 0) {
                    for (RE::FormID hiddenID : p.hiddenActorFormIDs) {
                        hiddenActorsByTarget[p.formID].insert(hiddenID);
                    }
                }
                if (p.generateMemory && p.formID != 0 && CanActorGenerateMemory(p.formID)) {
                    if (std::find(targetActors.begin(), targetActors.end(), p.formID) == targetActors.end()) {
                        targetActors.push_back(p.formID);
                    }
                }
            }
            for (const auto& p : ev.participants) {
                if (p.generateMemory && p.formID != 0) {
                    auto& relVec = relatedActorsByTarget[p.formID];
                    for (RE::FormID otherID : eventRealActors) {
                        if (otherID != p.formID && std::find(relVec.begin(), relVec.end(), otherID) == relVec.end()) {
                            relVec.push_back(otherID);
                        }
                    }
                }
            }
        }

        for (auto& [targetID, relVec] : relatedActorsByTarget) {
            auto hidIt = hiddenActorsByTarget.find(targetID);
            if (hidIt != hiddenActorsByTarget.end() && !hidIt->second.empty()) {
                relVec.erase(
                    std::remove_if(relVec.begin(), relVec.end(), [&](RE::FormID id) {
                        return hidIt->second.contains(id);
                    }),
                    relVec.end());
            }
        }

        if (targetActors.empty()) {
            return;
        }

        const auto& settings = Settings::GetSingleton();
        const bool useLLM = episode.isMatchmaking ? settings.matchmakingGenerateMemories : settings.skyrimnetUseLLMMemories;
        if (!useLLM) {
            uint64_t epID = episode.episodeID;
            size_t eventCount = episode.events.size();
            auto entries = BuildProgrammaticMemories(episode, targetActors, relatedActorsByTarget);
            if (entries.empty()) {
                return;
            }
            MARAS_LOG_INFO("[MemoryService] Built {} programmatic memory entry/entries for Episode #{} ({} event(s), LLM memories disabled)",
                           entries.size(), epID, eventCount);
            if (auto* taskIF = SKSE::GetTaskInterface()) {
                taskIF->AddTask([epID, entries = std::move(entries)]() {
                    try {
                        SkyrimNetMemoryService::GetSingleton().IngestMemories(entries);
                        MARAS_LOG_INFO("[MemoryService] Episode #{}: Ingested {} programmatic memory entry/entries into SkyrimNet",
                                       epID, entries.size());
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("[MemoryService] Exception ingesting programmatic Episode #{}: {}", epID, e.what());
                    }
                });
            }
            return;
        }

        nlohmann::json ctx = BuildEpisodePromptContext(episode);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_event_memories";

        uint64_t epID = episode.episodeID;
        size_t eventCount = episode.events.size();

        MARAS_LOG_INFO("[MemoryService] Dispatching Episode #{} ({} event(s), {} target NPC(s)) to LLM",
                       epID, eventCount, targetActors.size());

        SendCustomPromptToLLM(
            promptName,
            "memory",
            contextJson,
            [epID, targetActors = std::move(targetActors), relatedActorsByTarget = std::move(relatedActorsByTarget)](
                const char* response, int success) mutable {
                std::string respStr = (response && response[0] != '\0') ? std::string(response) : "";
                if (!success || respStr.empty()) {
                    MARAS_LOG_WARN("[MemoryService] Episode #{} LLM memory request failed or returned empty response", epID);
                    return;
                }
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([epID,
                                     respStr = std::move(respStr),
                                     targetActors = std::move(targetActors),
                                     relatedActorsByTarget = std::move(relatedActorsByTarget)]() {
                        try {
                            auto& svc = SkyrimNetMemoryService::GetSingleton();
                            auto entries = svc.ParseGeneratedMemories(respStr, targetActors, relatedActorsByTarget);
                            if (entries.empty()) {
                                MARAS_LOG_WARN("[MemoryService] Episode #{}: Failed to parse memories JSON from LLM response. Raw: {}",
                                               epID, respStr);
                                return;
                            }
                            svc.IngestMemories(entries);
                            MARAS_LOG_INFO("[MemoryService] Episode #{}: Ingested {} memory entry/entries into SkyrimNet",
                                           epID, entries.size());
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("[MemoryService] Exception processing Episode #{} callback: {}", epID, e.what());
                        }
                    });
                }
            });
    }

    std::vector<GeneratedMemoryEntry> SkyrimNetMemoryService::ParseGeneratedMemories(
        const std::string& response,
        const std::vector<RE::FormID>& targetActors,
        const std::unordered_map<RE::FormID, std::vector<RE::FormID>>& relatedActorsByTarget)
    {
        std::vector<GeneratedMemoryEntry> entries;
        if (response.empty() || targetActors.empty()) {
            return entries;
        }

        nlohmann::json resp = JsonService::ParseLLMJson(response);
        const auto* memsArray = FindMemoriesJsonArray(resp);
        if (!memsArray) {
            MARAS_LOG_WARN("[MemoryService] Failed to parse memories array from LLM response: {}", response);
            return entries;
        }

        struct TargetNameInfo {
            RE::FormID formID = 0;
            std::string name;
            std::string nameLower;
        };
        std::vector<TargetNameInfo> targetInfos;
        targetInfos.reserve(targetActors.size());
        for (RE::FormID id : targetActors) {
            TargetNameInfo info;
            info.formID = id;
            info.name = ActorSvc::GetName(id);
            info.nameLower = StringUtils::ToLowerStd(info.name);
            targetInfos.push_back(std::move(info));
        }

        size_t idx = 0;
        for (const auto& item : *memsArray) {
            if (!item.is_object()) continue;

            std::string actorName = GetJsonString(item, "actor");
            std::string actorFormIdStr = GetJsonString(item, "actorFormId");
            RE::FormID memActorId = ParseHexFormID(actorFormIdStr);

            if (memActorId != 0) {
                bool validTarget = std::find(targetActors.begin(), targetActors.end(), memActorId) != targetActors.end();
                if (!validTarget) {
                    memActorId = 0;
                }
            }

            if (memActorId == 0 && !actorName.empty()) {
                std::string actorNameLower = StringUtils::ToLowerStd(actorName);
                for (const auto& t : targetInfos) {
                    if (!t.nameLower.empty() &&
                        (actorNameLower == t.nameLower || actorNameLower.find(t.nameLower) != std::string::npos ||
                         t.nameLower.find(actorNameLower) != std::string::npos)) {
                        memActorId = t.formID;
                        break;
                    }
                }
            }

            if (memActorId == 0 && idx < targetActors.size()) {
                memActorId = targetActors[idx];
            }

            if (memActorId != 0) {
                if (auto* refr = ActorSvc::GetReference(memActorId)) {
                    memActorId = refr->GetFormID();
                } else {
                    RE::FormID ref = NPCIndex::GetSingleton().GetRefID(memActorId);
                    if (ref != 0) memActorId = ref;
                }
            }

            GeneratedMemoryEntry entry;
            entry.actorFormID = memActorId;
            entry.actorName = !actorName.empty() ? actorName : ActorSvc::GetName(memActorId);
            entry.content = GetJsonString(item, "content");
            entry.location = GetJsonString(item, "location", "Skyrim");
            entry.emotion = GetJsonString(item, "emotion", "content");
            entry.importanceScore = GetJsonFloat(item, "importance_score", 0.7f);
            entry.type = GetJsonString(item, "type", "RELATIONSHIP");

            if (item.contains("tags") && item["tags"].is_array()) {
                for (const auto& tag : item["tags"]) {
                    if (tag.is_string()) {
                        entry.tags.push_back(tag.get<std::string>());
                    }
                }
            }

            auto relIt = relatedActorsByTarget.find(memActorId);
            if (relIt != relatedActorsByTarget.end()) {
                entry.relatedActorFormIDs = relIt->second;
            } else {
                for (RE::FormID tID : targetActors) {
                    if (tID != memActorId && tID != 0) {
                        entry.relatedActorFormIDs.push_back(tID);
                    }
                }
            }

            if (entry.actorFormID != 0 && !entry.content.empty()) {
                entries.push_back(std::move(entry));
            }
            ++idx;
        }

        return entries;
    }

    std::vector<GeneratedMemoryEntry> SkyrimNetMemoryService::BuildProgrammaticMemories(
        const MemoryEpisode& episode,
        const std::vector<RE::FormID>& targetActors,
        const std::unordered_map<RE::FormID, std::vector<RE::FormID>>& relatedActorsByTarget) const
    {
        std::vector<GeneratedMemoryEntry> entries;
        if (episode.events.empty() || targetActors.empty()) {
            return entries;
        }

        auto addUniqueTag = [](std::vector<std::string>& tags, std::string_view raw) {
            if (tags.size() >= 6) return;
            std::string norm = NormalizeTagToken(raw);
            if (!norm.empty() && std::find(tags.begin(), tags.end(), norm) == tags.end()) {
                tags.push_back(std::move(norm));
            }
        };

        for (RE::FormID rawTargetID : targetActors) {
            if (rawTargetID == 0 || rawTargetID == kPlayerFormID) {
                continue;
            }

            std::string combinedContent;
            std::string bestLocation = "Skyrim";
            std::string bestType = "RELATIONSHIP";
            std::string bestEmotion = "content";
            float bestImportance = 0.0f;
            std::vector<std::string> tags;
            std::unordered_set<RE::FormID> hiddenForTarget;
            bool participated = false;

            for (const auto& ev : episode.events) {
                for (const auto& p : ev.participants) {
                    if (p.formID == rawTargetID && p.generateMemory) {
                        for (RE::FormID hid : p.hiddenActorFormIDs) {
                            hiddenForTarget.insert(hid);
                        }
                    }
                }
            }

            for (const auto& ev : episode.events) {
                const MemoryParticipant* selfParticipant = nullptr;
                for (const auto& p : ev.participants) {
                    if (p.formID == rawTargetID && p.generateMemory) {
                        selfParticipant = &p;
                        break;
                    }
                }
                if (!selfParticipant) {
                    continue;
                }

                participated = true;
                std::string sentence = ConvertPerspectiveNoteToFirstPerson(selfParticipant->perspectiveNote);
                if (!sentence.empty()) {
                    if (!combinedContent.empty()) {
                        combinedContent += " ";
                    }
                    combinedContent += sentence;
                }

                if (!ev.location.empty() && ev.location != "Skyrim") {
                    bestLocation = ev.location;
                }

                std::string evType;
                std::string evEmotion;
                float evImp = 0.70f;
                DeriveProgrammaticMetadata(ev.eventCategory, ev.eventType, selfParticipant->role,
                                           evType, evEmotion, evImp);
                if (evImp >= bestImportance) {
                    bestImportance = evImp;
                    bestType = std::move(evType);
                    bestEmotion = std::move(evEmotion);
                }

                addUniqueTag(tags, ev.eventCategory);
                addUniqueTag(tags, ev.eventType);
                addUniqueTag(tags, selfParticipant->role);
                for (const auto& otherP : ev.participants) {
                    if (otherP.formID != rawTargetID && !otherP.name.empty() &&
                        (otherP.formID == 0 || !hiddenForTarget.contains(otherP.formID))) {
                        addUniqueTag(tags, otherP.name);
                    }
                }
            }

            if (!participated || combinedContent.empty()) {
                continue;
            }

            RE::FormID memActorId = rawTargetID;
            if (auto* refr = ActorSvc::GetReference(memActorId)) {
                memActorId = refr->GetFormID();
            } else {
                RE::FormID ref = NPCIndex::GetSingleton().GetRefID(memActorId);
                if (ref != 0) memActorId = ref;
            }

            GeneratedMemoryEntry entry;
            entry.actorFormID = memActorId;
            entry.actorName = ActorSvc::GetName(rawTargetID);
            entry.content = std::move(combinedContent);
            entry.location = std::move(bestLocation);
            entry.emotion = std::move(bestEmotion);
            entry.importanceScore = (bestImportance > 0.0f) ? bestImportance : 0.70f;
            entry.type = std::move(bestType);
            entry.tags = std::move(tags);

            auto relIt = relatedActorsByTarget.find(rawTargetID);
            if (relIt == relatedActorsByTarget.end()) {
                relIt = relatedActorsByTarget.find(memActorId);
            }
            if (relIt != relatedActorsByTarget.end()) {
                entry.relatedActorFormIDs = relIt->second;
            } else {
                for (RE::FormID tID : targetActors) {
                    if (tID != rawTargetID && tID != memActorId && tID != 0 && !hiddenForTarget.contains(tID)) {
                        entry.relatedActorFormIDs.push_back(tID);
                    }
                }
            }

            entries.push_back(std::move(entry));
        }

        return entries;
    }

    void SkyrimNetMemoryService::IngestMemories(
        const std::vector<GeneratedMemoryEntry>& memories,
        const std::vector<RE::FormID>& fallbackRelatedActors)
    {
        for (const auto& mem : memories) {
            if (mem.actorFormID == 0 || mem.actorFormID == kPlayerFormID || mem.content.empty()) {
                continue;
            }
            std::vector<RE::FormID> related = mem.relatedActorFormIDs;
            for (RE::FormID fb : fallbackRelatedActors) {
                if (fb != 0 && fb != mem.actorFormID && std::find(related.begin(), related.end(), fb) == related.end()) {
                    related.push_back(fb);
                }
            }
            AddActorMemory(
                mem.actorFormID,
                mem.content,
                mem.importanceScore,
                mem.type,
                mem.emotion,
                mem.location,
                mem.tags,
                related);
        }
    }

    void SkyrimNetMemoryService::PreviewRelationshipMemories(
        RE::FormID actorA,
        RE::FormID actorB,
        const std::string& targetStatus,
        const std::string& action)
    {
        if (!IsSkyrimNetAvailable() || actorA == 0 || actorB == 0) {
            PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
            return;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);

        MemoryEvent ev;
        ev.eventCategory = "relationship_change";
        ev.eventType = targetStatus;
        ev.location = ResolveBestLocation({ actorA, actorB });
        ev.eventHeadline = std::format("{} and {} underwent a relationship transition (action: {}, status: {}).",
                                       nameA, nameB, action, targetStatus);
        ev.participants.push_back(MakeParticipant(
            actorA,
            "partner",
            std::format("You and {} entered '{}' status (action: {}).", nameB, targetStatus, action),
            true));
        ev.participants.push_back(MakeParticipant(
            actorB,
            "partner",
            std::format("You and {} entered '{}' status (action: {}).", nameA, targetStatus, action),
            true));

        MemoryEpisode previewEp;
        previewEp.episodeID = 0;
        previewEp.events.push_back(std::move(ev));

        nlohmann::json ctx = BuildEpisodePromptContext(previewEp);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_event_memories";

        std::vector<RE::FormID> targets;
        if (CanActorGenerateMemory(actorA)) targets.push_back(actorA);
        if (CanActorGenerateMemory(actorB)) targets.push_back(actorB);

        const auto& settings = Settings::GetSingleton();
        const bool useLLM = settings.matchmakingGenerateMemories;
        if (!useLLM) {
            std::unordered_map<RE::FormID, std::vector<RE::FormID>> relMap;
            relMap[actorA] = { actorB };
            relMap[actorB] = { actorA };
            auto entries = BuildProgrammaticMemories(previewEp, targets, relMap);
            nlohmann::json outputArray = nlohmann::json::array();
            for (const auto& entry : entries) {
                nlohmann::json m;
                m["actor"] = entry.actorName;
                m["actorFormId"] = PrismaUIDataBridge::FormIDToString(entry.actorFormID);
                m["content"] = entry.content;
                m["location"] = entry.location;
                m["emotion"] = entry.emotion;
                m["importance_score"] = entry.importanceScore;
                m["type"] = entry.type;
                m["tags"] = entry.tags;
                outputArray.push_back(std::move(m));
            }
            PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, outputArray);
            return;
        }

        SendCustomPromptToLLM(
            promptName,
            "memory",
            contextJson,
            [actorA, actorB, targetStatus, action, targets = std::move(targets)](const char* response, int success) mutable {
                std::string respStr = (response && response[0] != '\0') ? std::string(response) : "";
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([actorA, actorB, targetStatus, action, success, respStr = std::move(respStr), targets = std::move(targets)]() {
                        try {
                            auto& svc = SkyrimNetMemoryService::GetSingleton();
                            if (!success || respStr.empty()) {
                                ShowLLMRetryModal(
                                    "MARAS: Memory Generation Failed",
                                    std::format("{} & {}: LLM memory generation request failed or returned an empty response.",
                                                ActorSvc::GetName(actorA), ActorSvc::GetName(actorB)),
                                    LLMFailureType::RequestFailed,
                                    [actorA, actorB, targetStatus, action]() {
                                        SkyrimNetMemoryService::GetSingleton().PreviewRelationshipMemories(actorA, actorB, targetStatus, action);
                                    },
                                    [actorB]() {
                                        PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
                                    },
                                    "memory");
                                return;
                            }

                            std::unordered_map<RE::FormID, std::vector<RE::FormID>> relMap;
                            relMap[actorA] = { actorB };
                            relMap[actorB] = { actorA };
                            auto entries = svc.ParseGeneratedMemories(respStr, targets, relMap);
                            if (entries.empty()) {
                                ShowLLMRetryModal(
                                    "MARAS: Memory Generation Failed",
                                    std::format("{} & {}: Failed to parse memories array from LLM response.",
                                                ActorSvc::GetName(actorA), ActorSvc::GetName(actorB)),
                                    LLMFailureType::InvalidJson,
                                    [actorA, actorB, targetStatus, action]() {
                                        SkyrimNetMemoryService::GetSingleton().PreviewRelationshipMemories(actorA, actorB, targetStatus, action);
                                    },
                                    [actorB]() {
                                        PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
                                    },
                                    "memory");
                                return;
                            }

                            nlohmann::json outputArray = nlohmann::json::array();
                            for (const auto& entry : entries) {
                                nlohmann::json m;
                                m["actor"] = entry.actorName;
                                m["actorFormId"] = PrismaUIDataBridge::FormIDToString(entry.actorFormID);
                                m["content"] = entry.content;
                                m["location"] = entry.location;
                                m["emotion"] = entry.emotion;
                                m["importance_score"] = entry.importanceScore;
                                m["type"] = entry.type;
                                m["tags"] = entry.tags;
                                outputArray.push_back(std::move(m));
                            }
                            PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, outputArray);
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("[MemoryService] Exception in PreviewRelationshipMemories callback: {}", e.what());
                        }
                    });
                }
            });
    }

    // =========================================================================
    // Category 1: Relationship Status Transitions & Open/Close Toggles
    // =========================================================================

    void SkyrimNetMemoryService::RecordRelationshipStatusChange(
        RE::FormID initiatorID,
        RE::FormID partnerID,
        RelationshipStatus oldStatus,
        RelationshipStatus newStatus)
    {
        if (IsSuppressed() || !IsSkyrimNetAvailable()) return;
        if (newStatus == RelationshipStatus::Candidate || newStatus == RelationshipStatus::Unknown) return;

        std::string nameA = ActorSvc::GetName(initiatorID);
        std::string nameB = ActorSvc::GetName(partnerID);
        std::string oldStr = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(oldStatus));
        std::string newStr = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(newStatus));

        MemoryEvent ev;
        ev.eventCategory = "relationship_change";
        ev.eventType = newStr;
        ev.location = ResolveBestLocation({ initiatorID, partnerID });

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        bool isBreakup = NPCRelationshipManager::IsBreakupStatus(newStatus);
        bool isCheatingLover = (newStatus == RelationshipStatus::Lover) && mgr.IsRelationshipCheating(initiatorID, partnerID);

        if (isBreakup) {
            ev.eventHeadline = std::format("{} and {} ended their '{}' relationship and are now '{}' (initiated by {}).",
                                           nameA, nameB, oldStr, newStr, nameA);
            ev.participants.push_back(MakeParticipant(
                initiatorID,
                "breakup_initiator",
                std::format("You ended your '{}' relationship with {}, transitioning to '{}'.", oldStr, nameB, newStr),
                true));
            ev.participants.push_back(MakeParticipant(
                partnerID,
                "broken_up_partner",
                std::format("{} ended their '{}' relationship with you, leaving you '{}'.", nameA, oldStr, newStr),
                true));
        } else if (isCheatingLover) {
            bool isTalkingStage = false;
            for (const auto& rec : mgr.GetCheatingRecordsForActor(initiatorID)) {
                if (((rec.cheaterID == initiatorID && rec.affairPartnerID == partnerID) ||
                     (rec.cheaterID == partnerID && rec.affairPartnerID == initiatorID)) &&
                    rec.affairStatus == AffairStatus::Ongoing &&
                    rec.affairStage == AffairStage::Talking) {
                    isTalkingStage = true;
                    break;
                }
            }
            if (isTalkingStage) {
                ev.eventHeadline = std::format("{} and {} began a secret emotional and flirtatious affair behind their committed partner's back.",
                                               nameA, nameB);
                ev.participants.push_back(MakeParticipant(
                    initiatorID,
                    "secret_affair_partner",
                    std::format("You began a secret emotional/flirtatious affair with {}.", nameB),
                    true));
                ev.participants.push_back(MakeParticipant(
                    partnerID,
                    "secret_affair_partner",
                    std::format("You began a secret emotional/flirtatious affair with {}.", nameA),
                    true));
            } else {
                ev.eventHeadline = std::format("{} and {} began a secret affair as lovers behind their committed partner's back.",
                                               nameA, nameB);
                ev.participants.push_back(MakeParticipant(
                    initiatorID,
                    "secret_lover",
                    std::format("You entered a secret lover affair with {}.", nameB),
                    true));
                ev.participants.push_back(MakeParticipant(
                    partnerID,
                    "secret_lover",
                    std::format("You entered a secret lover affair with {}.", nameA),
                    true));
            }
        } else {
            ev.eventHeadline = std::format("{} and {} advanced their relationship from '{}' to '{}'.",
                                           nameA, nameB, oldStr, newStr);
            ev.participants.push_back(MakeParticipant(
                initiatorID,
                newStr,
                std::format("You and {} became '{}' (previously '{}').", nameB, newStr, oldStr),
                true));
            ev.participants.push_back(MakeParticipant(
                partnerID,
                newStr,
                std::format("You and {} became '{}' (previously '{}').", nameA, newStr, oldStr),
                true));
        }

        RecordEvent(std::move(ev));
    }

    void SkyrimNetMemoryService::RecordOpenRelationshipToggle(RE::FormID actorA, RE::FormID actorB, bool isOpen) {
        if (IsSuppressed() || !IsSkyrimNetAvailable()) return;

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);

        MemoryEvent ev;
        ev.eventCategory = "relationship_change";
        ev.eventType = isOpen ? "open_relationship" : "close_relationship";
        ev.location = ResolveBestLocation({ actorA, actorB });

        if (isOpen) {
            ev.eventHeadline = std::format("{} and {} agreed to open their relationship, allowing outside lovers.", nameA, nameB);
            ev.participants.push_back(MakeParticipant(
                actorA, "open_partner",
                std::format("You and {} agreed to open your relationship to outside partners.", nameB), true));
            ev.participants.push_back(MakeParticipant(
                actorB, "open_partner",
                std::format("You and {} agreed to open your relationship to outside partners.", nameA), true));
        } else {
            ev.eventHeadline = std::format("{} and {} agreed to close their relationship back to strict exclusivity.", nameA, nameB);
            ev.participants.push_back(MakeParticipant(
                actorA, "exclusive_partner",
                std::format("You and {} closed your relationship, recommitting to exclusivity.", nameB), true));
            ev.participants.push_back(MakeParticipant(
                actorB, "exclusive_partner",
                std::format("You and {} closed your relationship, recommitting to exclusivity.", nameA), true));
        }

        RecordEvent(std::move(ev));
    }

    // =========================================================================
    // Category 2: Infidelity Discovery, Confession, Exposure & Resolution (3 Participants)
    // =========================================================================

    void SkyrimNetMemoryService::RecordInfidelityEvent(
        RE::FormID cheaterID,
        RE::FormID victimID,
        RE::FormID affairPartnerID,
        const std::string& eventSubType,
        bool isWalkIn)
    {
        if (IsSuppressed() || !IsSkyrimNetAvailable()) return;
        if (cheaterID == 0 || victimID == 0) return;

        std::string cheaterName = ActorSvc::GetName(cheaterID);
        std::string victimName = ActorSvc::GetName(victimID);
        std::string affairName = affairPartnerID != 0 ? ActorSvc::GetName(affairPartnerID) : "a secret lover";

        MemoryEvent ev;
        ev.eventCategory = "infidelity";
        ev.eventType = eventSubType;
        ev.location = ResolveBestLocation({ victimID, cheaterID, affairPartnerID });

        if (eventSubType == "caught") {
            ev.eventHeadline = std::format("{} {} {} being intimate with {}.",
                                           victimName,
                                           isWalkIn ? "walked in on and caught" : "caught",
                                           cheaterName,
                                           affairName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "cheater_caught",
                std::format("Your partner {} caught you in an intimate encounter with {}.", victimName, affairName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "betrayed_partner",
                std::format("You caught your partner {} cheating on you with {}.", cheaterName, affairName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "affair_partner_caught",
                    std::format("{}'s partner {} caught the two of you together in the act.", cheaterName, victimName), true));
            }
        } else if (eventSubType == "confessed") {
            ev.eventHeadline = std::format("{} confessed their secret affair with {} to their partner {}.",
                                           cheaterName, affairName, victimName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "confessing_cheater",
                std::format("You confessed your secret affair with {} to your partner {}.", affairName, victimName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "betrayed_partner",
                std::format("Your partner {} confessed to you that they have been having a secret affair with {}.", cheaterName, affairName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "affair_partner",
                    std::format("{} confessed your secret affair to their partner {}, bringing it into the open.", cheaterName, victimName), true));
            }
        } else if (eventSubType == "exposed") {
            ev.eventHeadline = std::format("{} exposed their secret affair with {} directly to {}'s partner {}.",
                                           affairName, cheaterName, cheaterName, victimName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "exposed_cheater",
                std::format("Your affair partner {} exposed your secret infidelity to your partner {}.", affairName, victimName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "betrayed_partner",
                std::format("{} revealed to you that your partner {} has been cheating on you with them.", affairName, cheaterName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "affair_exposer",
                    std::format("You exposed your secret affair with {} directly to their partner {}.", cheaterName, victimName), true));
            }
        } else if (eventSubType == "confronted") {
            ev.eventHeadline = std::format("{} confronted {} over the affair with {}, demanding they end the affair while staying together.",
                                           victimName, cheaterName, affairName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "confronted_cheater",
                std::format("{} confronted you over your affair with {}; you ended things with {} to stay with {}.",
                            victimName, affairName, affairName, victimName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "confronting_partner",
                std::format("You confronted {} over their affair with {} and forced them to break off the affair.",
                            cheaterName, affairName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "discarded_affair_partner",
                    std::format("After {} confronted {}, {} broke off their affair with you to stay with {}.",
                                victimName, cheaterName, cheaterName, victimName), true));
            }
        } else if (eventSubType == "allowed") {
            ev.eventHeadline = std::format("{} forgave {}'s affair with {} and agreed to open their relationship.",
                                           victimName, cheaterName, affairName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "forgiven_cheater",
                std::format("{} accepted your involvement with {} and agreed to an open relationship.", victimName, affairName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "forgiving_partner",
                std::format("You chose to forgive {}'s affair with {} and opened your relationship.", cheaterName, affairName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "accepted_lover",
                    std::format("{}'s partner {} accepted your affair with {} and opened their relationship.",
                                cheaterName, victimName, cheaterName), true));
            }
        } else if (eventSubType == "breakup") {
            ev.eventHeadline = std::format("{} ended their relationship with {} over {}'s affair with {}.",
                                           victimName, cheaterName, cheaterName, affairName);
            ev.participants.push_back(MakeParticipant(
                cheaterID, "dumped_cheater",
                std::format("{} broke up with / divorced you over your affair with {}.", victimName, affairName), true));
            ev.participants.push_back(MakeParticipant(
                victimID, "betrayed_partner_leaving",
                std::format("Refusing to tolerate {}'s betrayal with {}, you ended your relationship with {}.",
                            cheaterName, affairName, cheaterName), true));
            if (affairPartnerID != 0) {
                ev.participants.push_back(MakeParticipant(
                    affairPartnerID, "affair_partner",
                    std::format("Your affair with {} led {} to break up with / divorce {}.",
                                cheaterName, victimName, cheaterName), true));
            }
        } else {
            return;
        }

        RecordEvent(std::move(ev));
    }

    // =========================================================================
    // Category 3: Paternity & Pregnancy Secret Reveals (1, 2, or 3 Participants)
    // =========================================================================

    void SkyrimNetMemoryService::RecordPregnancyDiscovered(
        RE::FormID motherID,
        RE::FormID partnerID,
        RE::FormID fallbackBioID,
        const std::string& fallbackBioName,
        int32_t childCount)
    {
        if (IsSuppressed() || !IsSkyrimNetAvailable() || motherID == 0) return;

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        if (partnerID == 0) {
            partnerID = mgr.GetPrimaryPartner(motherID);
        }

        std::string motherName = ActorSvc::GetName(motherID);
        std::string partnerName = partnerID != 0 ? ActorSvc::GetName(partnerID) : "";

        auto unbornIDs = mgr.GetUnbornChildren(motherID);
        if (static_cast<int32_t>(unbornIDs.size()) > childCount) {
            childCount = static_cast<int32_t>(unbornIDs.size());
        }
        if (childCount < 1) childCount = 1;
        std::string countDesc = (childCount > 1) ? std::format("{} children", childCount) : "a child";

        bool hasNonPartnerFetus = false;
        bool hasSecretPregnancyForPartner = false;
        RE::FormID primaryBioFatherID = (fallbackBioID != kUnknownFatherFormID) ? fallbackBioID : 0;
        std::string bioFatherNames;

        for (RE::FormID uID : unbornIDs) {
            auto childOpt = mgr.GetChildEntry(uID);
            if (!childOpt.has_value()) continue;

            RE::FormID bID = mgr.GetBioFather(uID);
            if (bID == 0) bID = childOpt->bioFatherID;
            if (primaryBioFatherID == 0 && bID != 0 && bID != kUnknownFatherFormID) {
                primaryBioFatherID = bID;
            }

            std::string fName = (bID != 0 && bID != kUnknownFatherFormID)
                                    ? ActorSvc::GetName(bID)
                                    : (!childOpt->paternitySecret.bioFatherName.empty()
                                           ? childOpt->paternitySecret.bioFatherName
                                           : fallbackBioName);
            if (fName.empty()) fName = "an unknown father";

            bool isPartnerFetus = (bID != 0 && partnerID != 0 && bID == partnerID) ||
                                  (!partnerName.empty() && fName == partnerName);
            if (!isPartnerFetus) {
                hasNonPartnerFetus = true;
                if (bioFatherNames.find(fName) == std::string::npos) {
                    if (!bioFatherNames.empty()) bioFatherNames += " and ";
                    bioFatherNames += fName;
                }
            }
            if (childOpt->paternitySecret.isPregnancySecretForPartner ||
                childOpt->paternitySecret.isSecretForPartner ||
                (partnerID != 0 && !isPartnerFetus)) {
                hasSecretPregnancyForPartner = true;
            }
        }

        if (unbornIDs.empty()) {
            std::string fName = (primaryBioFatherID != 0) ? ActorSvc::GetName(primaryBioFatherID) : fallbackBioName;
            if (fName.empty()) fName = !partnerName.empty() ? partnerName : "an unknown father";
            bool isPartnerFetus = (primaryBioFatherID != 0 && partnerID != 0 && primaryBioFatherID == partnerID) ||
                                  (!partnerName.empty() && fName == partnerName);
            if (!isPartnerFetus) {
                hasNonPartnerFetus = true;
                bioFatherNames = fName;
                if (partnerID != 0) hasSecretPregnancyForPartner = true;
            }
        }

        MemoryEvent ev;
        ev.eventCategory = "pregnancy_discovered";
        ev.location = ResolveBestLocation({ motherID, partnerID });

        // Case 1: Normal Pregnancy with Partner (partner is the biological father, no secret)
        if (partnerID != 0 && !hasNonPartnerFetus && !hasSecretPregnancyForPartner) {
            ev.eventType = "pregnancy_with_partner";
            ev.eventHeadline = std::format("{} discovered she is expecting {} with her partner {}.",
                                           motherName, countDesc, partnerName);
            ev.participants.push_back(MakeParticipant(
                motherID,
                "expectant_mother",
                std::format("You discovered that you are pregnant and expecting {} with your partner {}.",
                            countDesc, partnerName),
                true));
            ev.participants.push_back(MakeParticipant(
                partnerID,
                "expectant_father",
                std::format("You and your partner {} learned the joyful news that she is pregnant and expecting {} with you.",
                            motherName, countDesc),
                true));
            RecordEvent(std::move(ev));
            return;
        }

        // Case 2: Mother has a Partner, but Pregnancy is Fathered by Someone Else (Secret at Discovery)
        if (partnerID != 0 && hasNonPartnerFetus) {
            ev.eventType = "secret_pregnancy_discovered_by_mother";
            ev.eventHeadline = std::format("{} discovered that she is pregnant ({} fathered by {}), while in a relationship with {} (neither {} nor {} knows she is pregnant yet).",
                                           motherName, countDesc, bioFatherNames, partnerName, partnerName, bioFatherNames);
            ev.participants.push_back(MakeParticipant(
                motherID,
                "mother_discovering_secret_pregnancy",
                std::format("You discovered that you are pregnant with {} fathered by {} while in a relationship with {}. Neither {} nor {} knows you are pregnant yet unless you choose to tell them.",
                            countDesc, bioFatherNames, partnerName, partnerName, bioFatherNames),
                true));
            RecordEvent(std::move(ev));
            return;
        }

        // Case 3: Unpartnered Mother Discovers Pregnancy
        ev.eventType = "single_mother_pregnancy_discovered";
        std::string fatherDesc = !bioFatherNames.empty() ? bioFatherNames : "an unknown father";
        ev.eventHeadline = std::format("{} discovered that she is pregnant and expecting {} (fathered by {}).",
                                       motherName, countDesc, fatherDesc);
        ev.participants.push_back(MakeParticipant(
            motherID,
            "expectant_mother",
            std::format("You discovered that you are pregnant and expecting {}, knowing that {} is the biological father.",
                        countDesc, fatherDesc),
            true));
        RecordEvent(std::move(ev));
    }

    void SkyrimNetMemoryService::RecordPaternityReveal(
        RE::FormID childID,
        const std::string& outcome,
        const std::string& partnerReaction,
        bool wasPregnancySecretForPartner)
    {
        if (IsSuppressed() || !IsSkyrimNetAvailable() || childID == 0) return;

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        auto childOpt = mgr.GetChildEntry(childID);
        if (!childOpt.has_value()) return;

        const auto& entry = *childOpt;
        RE::FormID motherID = mgr.GetBioMother(childID);
        RE::FormID presumedID = mgr.GetMotherPartner(childID);
        RE::FormID bioFatherID = mgr.GetBioFather(childID);
        if (motherID == 0) return;

        std::string motherName = ActorSvc::GetName(motherID);
        std::string presumedName = presumedID != 0 ? ActorSvc::GetName(presumedID) : "her partner";
        std::string bioName = (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID)
                                  ? ActorSvc::GetName(bioFatherID)
                                  : (!entry.paternitySecret.bioFatherName.empty() ? entry.paternitySecret.bioFatherName : "another man");
        bool isUnborn = (entry.stage == ChildStage::Unborn);
        bool hasGivenName = !isUnborn && !entry.name.empty() && !StringUtils::CaseInsensitiveEquals(entry.name, "Newborn");
        std::string childDesc = isUnborn ? "unborn child" : (hasGivenName ? entry.name : "child");
        bool unannouncedPregForPartner = isUnborn && wasPregnancySecretForPartner;

        // Sub-case A: Pregnancy shared / visibly noticed by presumed father while true paternity remains secret
        if (outcome == "mother_shared_pregnancy_presumed" || outcome == "pregnancy_visible_presumed") {
            if (presumedID == 0) return;
            bool byBelly = (outcome == "pregnancy_visible_presumed");

            if (entry.paternitySecret.isSecretForPartner) {
                MemoryEvent ev;
                ev.eventCategory = "paternity_reveal";
                ev.eventType = outcome;
                ev.location = ResolveBestLocation({ motherID, presumedID });
                ev.eventHeadline = byBelly
                    ? std::format("{} noticed {}'s growing pregnant belly and believes the baby is his, while {} keeps secret that {} is the biological father.",
                                  presumedName, motherName, motherName, bioName)
                    : std::format("{} shared news of her pregnancy with {}, letting him believe the baby is his while keeping secret that {} is the biological father.",
                                  motherName, presumedName, bioName);

                ev.participants.push_back(MakeParticipant(
                    motherID,
                    "mother_hiding_paternity",
                    byBelly
                        ? std::format("{} noticed your growing pregnant belly and happily believes the baby is his, while you keep secret that {} is the true biological father.",
                                      presumedName, bioName)
                        : std::format("You shared news of your pregnancy with {} and let him believe the baby is his, while keeping secret that {} is the true biological father.",
                                      presumedName, bioName),
                    true));

                std::unordered_set<RE::FormID> hiddenFromPresumed;
                if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
                    hiddenFromPresumed.insert(bioFatherID);
                }

                ev.participants.push_back(MakeParticipant(
                    presumedID,
                    "expectant_father_unaware_of_secret",
                    byBelly
                        ? std::format("You noticed your partner {}'s growing pregnant belly and happily believe the baby is yours.", motherName)
                        : std::format("Your partner {} shared the joyful news that she is pregnant, and you happily believe the baby is yours.", motherName),
                    true,
                    std::format("{} DOES NOT KNOW that {} is the biological father or that {} had an affair. {} genuinely believes 100% that this baby is his own biological child with {}. NEVER mention {}, infidelity, deception, secrets, or doubt in {}'s memory or tags.",
                                presumedName, bioName, motherName, presumedName, motherName, bioName, presumedName),
                    std::move(hiddenFromPresumed)));

                RecordEvent(std::move(ev));
            }
            return;
        }

        // Sub-case B: Revealed / discovered ONLY for Bio Father (2 people: mother, bioFather)
        if (outcome == "mother_confessed_bio" || outcome == "bio_father_learnt") {
            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID) return;
            bool confessed = (outcome == "mother_confessed_bio");
            bool partnerUnawareOfPreg = isUnborn && entry.paternitySecret.isSecretForPartner && entry.paternitySecret.isPregnancySecretForPartner;
            std::string partnerClause = (presumedID != 0 && entry.paternitySecret.isSecretForPartner)
                                            ? (partnerUnawareOfPreg
                                                   ? std::format(" while your partner {} does not even know you are pregnant yet", presumedName)
                                                   : std::format(" while your partner {} remains unaware of the truth", presumedName))
                                            : "";
            std::string bioPartnerClause = (presumedID != 0 && entry.paternitySecret.isSecretForPartner)
                                               ? (partnerUnawareOfPreg
                                                      ? std::format(" while she is with {} (who does not even know she is pregnant yet)", presumedName)
                                                      : std::format(" while she is with {} (who does not know the truth)", presumedName))
                                               : (presumedID != 0 ? std::format(" while she is with {}", presumedName) : "");

            MemoryEvent ev;
            ev.eventCategory = "paternity_reveal";
            ev.eventType = outcome;
            ev.location = ResolveBestLocation({ motherID, bioFatherID });
            if (isUnborn) {
                ev.eventHeadline = confessed
                    ? std::format("{} confessed to {} that she is pregnant with his child (while {} is her partner).",
                                  motherName, bioName, presumedName)
                    : std::format("{} discovered that {} is pregnant with his child (while {} is her partner).",
                                  bioName, motherName, presumedName);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    confessed
                        ? std::format("You revealed to {} that you are pregnant with his child{}.", bioName, partnerClause)
                        : std::format("{} discovered that you are pregnant with his child{}.", bioName, partnerClause),
                    true));
                ev.participants.push_back(MakeParticipant(
                    bioFatherID, "biological_father",
                    confessed
                        ? std::format("{} confessed to you that she is pregnant with your child{}.", motherName, bioPartnerClause)
                        : std::format("You discovered that {} is pregnant with your child{}.", motherName, bioPartnerClause),
                    true));
            } else {
                ev.eventHeadline = confessed
                    ? std::format("{} confessed to {} that he is the true biological father of her {} (while {} is her partner).",
                                  motherName, bioName, childDesc, presumedName)
                    : std::format("{} discovered that he is the true biological father of {}'s {} (while {} is her partner).",
                                  bioName, motherName, childDesc, presumedName);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    confessed
                        ? std::format("You revealed to {} that he is the true biological father of your {}{}.", bioName, childDesc, partnerClause)
                        : std::format("{} discovered that he is the true biological father of your {}{}.", bioName, childDesc, partnerClause),
                    true));
                ev.participants.push_back(MakeParticipant(
                    bioFatherID, "biological_father",
                    confessed
                        ? std::format("{} confessed to you that you are the true biological father of her {}{}.", motherName, childDesc, bioPartnerClause)
                        : std::format("You discovered that you are the true biological father of {}'s {}{}.", motherName, childDesc, bioPartnerClause),
                    true));
            }
            RecordEvent(std::move(ev));
            return;
        }

        // Sub-case C: Revealed / discovered for Presumed Father (2 people: mother, presumedFather)
        if (outcome == "mother_confessed_presumed" || outcome == "presumed_father_learnt") {
            if (presumedID == 0) return;
            bool confessed = (outcome == "mother_confessed_presumed");
            std::string reactionDesc = (partnerReaction == "divorce")
                                           ? std::format(" and {} decided to divorce {}", presumedName, motherName)
                                           : (partnerReaction == "breakup"
                                                  ? std::format(" and {} decided to break up with {}", presumedName, motherName)
                                                  : "");

            MemoryEvent ev;
            ev.eventCategory = "paternity_reveal";
            ev.eventType = outcome;
            ev.location = ResolveBestLocation({ motherID, presumedID });
            if (unannouncedPregForPartner) {
                ev.eventHeadline = confessed
                    ? std::format("{} confessed to {} that she is pregnant with a child fathered by {}, not him{}.",
                                  motherName, presumedName, bioName, reactionDesc)
                    : std::format("{} discovered that {} is pregnant with a child fathered by {}, not him{}.",
                                  presumedName, motherName, bioName, reactionDesc);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    confessed
                        ? std::format("You confessed to {} that you are pregnant with a child fathered by {}, not him.", presumedName, bioName)
                        : std::format("{} discovered that you are pregnant with a child fathered by {}, not him.", presumedName, bioName),
                    true));
                ev.participants.push_back(MakeParticipant(
                    presumedID, "deceived_partner",
                    confessed
                        ? std::format("{} confessed to you that she is pregnant with a child fathered by {}, not you (you did not previously know she was pregnant).",
                                      motherName, bioName)
                        : std::format("You discovered that {} is pregnant with a child fathered by {}, not you (you did not previously know she was pregnant).",
                                      motherName, bioName),
                    true));
            } else {
                ev.eventHeadline = confessed
                    ? std::format("{} confessed to {} that her {} was actually fathered by {}, not him{}.",
                                  motherName, presumedName, childDesc, bioName, reactionDesc)
                    : std::format("{} discovered that {}'s {} was actually fathered by {}, not him{}.",
                                  presumedName, motherName, childDesc, bioName, reactionDesc);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    confessed
                        ? std::format("You confessed to {} that your {} was fathered by {}, not him.", presumedName, childDesc, bioName)
                        : std::format("{} discovered that your {} was fathered by {}, not him.", presumedName, childDesc, bioName),
                    true));
                ev.participants.push_back(MakeParticipant(
                    presumedID, "deceived_partner",
                    confessed
                        ? std::format("{} confessed to you that the {} you thought was yours was actually fathered by {}.",
                                      motherName, childDesc, bioName)
                        : std::format("You discovered that {}'s {} was actually fathered by {}, not you.",
                                      motherName, childDesc, bioName),
                    true));
            }
            RecordEvent(std::move(ev));
            return;
        }

        // Sub-case D: Revealed to BOTH at once (3 people: mother, presumedFather, bioFather)
        if (outcome == "mother_confessed_both" || outcome == "divorce_revealed") {
            MemoryEvent ev;
            ev.eventCategory = "paternity_reveal";
            ev.eventType = outcome;
            ev.location = ResolveBestLocation({ motherID, presumedID, bioFatherID });
            if (unannouncedPregForPartner && outcome == "mother_confessed_both") {
                ev.eventHeadline = std::format("{} revealed to both her partner {} and {} that she is pregnant with {}'s child.",
                                               motherName, presumedName, bioName, bioName);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    std::format("You revealed to both your partner {} and {} that you are pregnant with {}'s child.",
                                presumedName, bioName, bioName),
                    true));
                if (presumedID != 0) {
                    ev.participants.push_back(MakeParticipant(
                        presumedID, "deceived_partner",
                        std::format("You learned that {} is pregnant and that the baby was fathered by {}, not you (you did not previously know she was pregnant).",
                                    motherName, bioName),
                        true));
                }
                if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
                    ev.participants.push_back(MakeParticipant(
                        bioFatherID, "biological_father",
                        std::format("You learned that {} is pregnant with your biological child, and her partner {} now knows as well.",
                                    motherName, presumedName),
                        true));
                }
            } else {
                ev.eventHeadline = std::format("The paternity secret of {}'s {} came out to both {} (her partner) and {} (the biological father).",
                                               motherName, childDesc, presumedName, bioName);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    std::format("The truth that {} is the biological father of your {} (rather than {}) came out to both men.",
                                bioName, childDesc, presumedName),
                    true));
                if (presumedID != 0) {
                    ev.participants.push_back(MakeParticipant(
                        presumedID, "deceived_partner",
                        std::format("You learned that {}'s {} was actually fathered by {}, not you.",
                                    motherName, childDesc, bioName),
                        true));
                }
                if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
                    ev.participants.push_back(MakeParticipant(
                        bioFatherID, "biological_father",
                        std::format("You learned that you are the true biological father of {}'s {}, and her partner {} now knows as well.",
                                    motherName, childDesc, presumedName),
                        true));
                }
            }
            RecordEvent(std::move(ev));
        }
    }

    // =========================================================================
    // Category 4: Child Birth (1 to 3 Adult Participants — Never the Newborn)
    // =========================================================================

    void SkyrimNetMemoryService::RecordChildBirth(RE::FormID motherID, const std::vector<RE::FormID>& childIDs) {
        if (IsSuppressed() || !IsSkyrimNetAvailable() || motherID == 0) return;

        const auto& mgr = NPCRelationshipManager::GetSingleton();
        std::vector<RE::FormID> targetChildIDs = childIDs;
        if (targetChildIDs.empty()) {
            for (const auto& [cID, entry] : mgr.GetAllChildren()) {
                if (entry.stage == ChildStage::Infant &&
                    ( entry.bioMotherID == motherID || entry.guardianAID == motherID || entry.guardianBID == motherID)) {
                    targetChildIDs.push_back(cID);
                }
            }
        }
        if (targetChildIDs.empty()) return;

        std::string motherName = ActorSvc::GetName(motherID);

        struct FatherBirthBucket {
            RE::FormID bioFatherID = 0;
            RE::FormID presumedFatherID = 0;
            bool isSecretForPartner = false;
            bool isSecretForBioFather = false;
            std::vector<std::string> childDescriptions;
        };
        std::vector<FatherBirthBucket> buckets;

        for (RE::FormID cID : targetChildIDs) {
            auto cOpt = mgr.GetChildEntry(cID);
            if (!cOpt.has_value()) continue;

            RE::FormID bID = mgr.GetBioFather(cID);
            if (bID == 0) bID = cOpt->bioFatherID;
            RE::FormID pID = mgr.GetPresumedFather(cID);

            bool secPartner = cOpt->paternitySecret.isSecretForPartner;
            bool secBio = cOpt->paternitySecret.isSecretForBioFather;
            std::string rel = (cOpt->gender == 0) ? "son" : (cOpt->gender == 1 ? "daughter" : "child");
            bool hasGivenName = !cOpt->name.empty() && !StringUtils::CaseInsensitiveEquals(cOpt->name, "Newborn");
            std::string desc = hasGivenName ? std::format("{} ({})", cOpt->name, rel) : rel;

            auto it = std::find_if(buckets.begin(), buckets.end(), [&](const FatherBirthBucket& b) {
                return b.bioFatherID == bID && b.presumedFatherID == pID &&
                       b.isSecretForPartner == secPartner && b.isSecretForBioFather == secBio;
            });
            if (it != buckets.end()) {
                it->childDescriptions.push_back(std::move(desc));
            } else {
                FatherBirthBucket fb;
                fb.bioFatherID = bID;
                fb.presumedFatherID = pID;
                fb.isSecretForPartner = secPartner;
                fb.isSecretForBioFather = secBio;
                fb.childDescriptions.push_back(std::move(desc));
                buckets.push_back(std::move(fb));
            }
        }

        for (const auto& b : buckets) {
            std::string childrenList;
            for (size_t i = 0; i < b.childDescriptions.size(); ++i) {
                if (i > 0) childrenList += (i + 1 == b.childDescriptions.size()) ? " and " : ", ";
                childrenList += b.childDescriptions[i];
            }

            RE::FormID bioID = (b.bioFatherID != kUnknownFatherFormID) ? b.bioFatherID : 0;
            RE::FormID presID = b.presumedFatherID;
            std::string bioName = bioID != 0 ? ActorSvc::GetName(bioID) : "an unknown father";
            std::string presName = presID != 0 ? ActorSvc::GetName(presID) : "";

            bool sameOrNoPresumed = (presID == 0 || presID == bioID || presID == motherID);

            // Case 1: No paternity discrepancy (Mother + BioFather if known)
            if (sameOrNoPresumed) {
                MemoryEvent ev;
                ev.eventCategory = "child_birth";
                ev.eventType = "child_born";
                ev.location = ResolveBestLocation({ motherID, bioID });
                ev.eventHeadline = std::format("{} gave birth to {}, fathered by {}.", motherName, childrenList, bioName);
                ev.participants.push_back(MakeParticipant(
                    motherID, "mother",
                    std::format("You gave birth to your newborn {} with {}.", childrenList, bioName), true));
                if (bioID != 0 && !b.isSecretForBioFather) {
                    ev.participants.push_back(MakeParticipant(
                        bioID, "biological_father",
                        std::format("{} gave birth to your biological {}.", motherName, childrenList), true));
                }
                RecordEvent(std::move(ev));
                continue;
            }

            // Cases 2 & 3: Presumed Father is DUPED (isSecretForPartner == true)
            if (b.isSecretForPartner) {
                MemoryEvent ev;
                ev.eventCategory = "child_birth";
                ev.eventType = "child_born_secret";
                ev.location = ResolveBestLocation({ motherID, presID, bioID });
                ev.eventHeadline = b.isSecretForBioFather
                    ? std::format("{} gave birth to {} (fathered by {}), letting {} believe the baby is his while keeping the secret from both {} and {}.",
                                  motherName, childrenList, bioName, presName, presName, bioName)
                    : std::format("{} gave birth to {} (fathered by {}, who knows the truth), while letting her partner {} believe the baby is his.",
                                  motherName, childrenList, bioName, presName);

                ev.participants.push_back(MakeParticipant(
                    motherID, "mother_with_secret",
                    b.isSecretForBioFather
                        ? std::format("You gave birth to {} fathered by {}, letting {} believe the newborn is his while neither {} nor {} knows the truth.",
                                      childrenList, bioName, presName, presName, bioName)
                        : std::format("You gave birth to {} fathered by {} (who knows the child is his), while letting your partner {} believe the baby is his.",
                                      childrenList, bioName, presName),
                    true));

                std::unordered_set<RE::FormID> hiddenFromPresumed;
                if (bioID != 0) {
                    hiddenFromPresumed.insert(bioID);
                }

                ev.participants.push_back(MakeParticipant(
                    presID,
                    "proud_father_unaware_of_secret",
                    std::format("Your partner {} gave birth to your newborn {}, and you happily believe you are the biological father.",
                                motherName, childrenList),
                    true,
                    std::format("{} DOES NOT KNOW that {} is the biological father or that {} had an affair. {} genuinely believes 100% that the newborn {} is his own biological child with {}. NEVER mention {}, infidelity, deception, secrets, or doubt in {}'s memory or tags.",
                                presName, bioName, motherName, presName, childrenList, motherName, bioName, presName),
                    std::move(hiddenFromPresumed)));

                if (bioID != 0 && !b.isSecretForBioFather) {
                    ev.participants.push_back(MakeParticipant(
                        bioID, "secret_biological_father",
                        std::format("{} gave birth to your biological {}, though her partner {} still believes the newborn is his.",
                                    motherName, childrenList, presName),
                        true));
                }
                RecordEvent(std::move(ev));
                continue;
            }

            // Cases 4 & 5: Presumed Father KNOWS the child is not biologically his (!isSecretForPartner && presID != bioID)
            MemoryEvent awareEv;
            awareEv.eventCategory = "child_birth";
            awareEv.eventType = "child_born_non_bio_partner_aware";
            awareEv.location = ResolveBestLocation({ motherID, presID, bioID });
            awareEv.eventHeadline = std::format("{} gave birth to {} (biologically fathered by {}); her partner {} is aware the child is not biologically his and has stayed with {}.",
                                                motherName, childrenList, bioName, presName, motherName);
            awareEv.participants.push_back(MakeParticipant(
                motherID, "mother",
                std::format("You gave birth to {} (biologically fathered by {}), with your partner {} knowingly staying by your side despite knowing the child is not biologically his.",
                            childrenList, bioName, presName),
                true));
            awareEv.participants.push_back(MakeParticipant(
                presID, "knowingly_accepting_partner",
                std::format("Your partner {} gave birth to {}; you know the child was biologically fathered by {} (not you), yet you have chosen to stay with {} and accept the child.",
                            motherName, childrenList, bioName, motherName),
                true));
            if (bioID != 0 && !b.isSecretForBioFather) {
                awareEv.participants.push_back(MakeParticipant(
                    bioID, "biological_father",
                    std::format("{} gave birth to your biological {}, and both you and her partner {} know the truth of the child's parentage.",
                                motherName, childrenList, presName),
                    true));
            }
            RecordEvent(std::move(awareEv));
        }
    }

    // =========================================================================
    // Category 5: Child Adoption, Inheritance & Custody Changes
    // =========================================================================

    void SkyrimNetMemoryService::RecordChildCustodyEvent(
        const std::string& eventType,
        RE::FormID childID,
        RE::FormID guardianAID,
        RE::FormID guardianBID,
        RE::FormID otherID,
        const std::string& reason)
    {
        if (IsSuppressed() || !IsSkyrimNetAvailable() || childID == 0) return;

        auto childOpt = NPCRelationshipManager::GetSingleton().GetChildEntry(childID);
        if (childOpt && childOpt->stage == ChildStage::Unborn) return;
        std::string childName = (childOpt && !childOpt->name.empty() && !StringUtils::CaseInsensitiveEquals(childOpt->name, "Newborn"))
                                    ? childOpt->name
                                    : ActorSvc::GetName(childID);
        if (childName.empty() || StringUtils::CaseInsensitiveEquals(childName, "Newborn")) childName = "the child";

        // Only placed Stage 2 children (!IsSyntheticChildID && stage == Child) generate their own child memories
        bool childCanGenerate = (!IsSyntheticChildID(childID)) &&
                                (!childOpt.has_value() || childOpt->stage == ChildStage::Child) &&
                                CanActorGenerateMemory(childID);

        std::string nameGA = guardianAID != 0 ? ActorSvc::GetName(guardianAID) : "";
        std::string nameGB = guardianBID != 0 ? ActorSvc::GetName(guardianBID) : "";
        std::string nameOther = otherID != 0 ? ActorSvc::GetName(otherID) : "";
        if (nameOther.empty() && otherID != 0) {
            if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(otherID)) {
                nameOther = dec->name;
            }
        }

        MemoryEvent ev;
        ev.eventCategory = "child_custody";
        ev.eventType = eventType + (reason.empty() ? "" : ("_" + reason));
        ev.location = ResolveBestLocation({ childID, guardianAID, guardianBID, otherID });

        if (eventType == "adopted") {
            std::string guardiansStr = (!nameGA.empty() && !nameGB.empty())
                                           ? std::format("{} and {}", nameGA, nameGB)
                                           : (!nameGA.empty() ? nameGA : nameGB);
            ev.eventHeadline = std::format("{} was officially adopted into the family by {}.", childName, guardiansStr);

            ev.participants.push_back(MakeParticipant(
                childID, "adopted_child",
                std::format("You were adopted into a new home and family by {}.", guardiansStr),
                childCanGenerate));
            if (guardianAID != 0) {
                ev.participants.push_back(MakeParticipant(
                    guardianAID, "adoptive_guardian",
                    !nameGB.empty()
                        ? std::format("You and {} adopted {} into your family.", nameGB, childName)
                        : std::format("You adopted {} into your family.", childName),
                    true));
            }
            if (guardianBID != 0 && guardianBID != guardianAID) {
                ev.participants.push_back(MakeParticipant(
                    guardianBID, "adoptive_guardian",
                    !nameGA.empty()
                        ? std::format("You and {} adopted {} into your family.", nameGA, childName)
                        : std::format("You adopted {} into your family.", childName),
                    true));
            }
        } else if (eventType == "inherited") {
            std::string deceasedName = !nameOther.empty() ? nameOther : "their parent";
            if (guardianAID != 0 && guardianBID != 0) {
                // Polygamous spouse death: guardianAID is the inheriting surviving spouse, guardianBID is continuing parent (Player)
                ev.eventHeadline = std::format("Following {}'s death, {} came under the care and guardianship of {} and {}.",
                                               deceasedName, childName, nameGA, nameGB);
                ev.participants.push_back(MakeParticipant(
                    childID, "bereaved_child",
                    std::format("After losing {}, you moved into the care of {} and {}.", deceasedName, nameGA, nameGB),
                    childCanGenerate));
                ev.participants.push_back(MakeParticipant(
                    guardianAID, "inheriting_guardian",
                    std::format("Following {}'s death, you stepped in alongside {} to take guardianship of {}.",
                                deceasedName, nameGB, childName),
                    true));
                ev.participants.push_back(MakeParticipant(
                    guardianBID, "surviving_guardian",
                    std::format("Following {}'s death, you and {} now care for {} together.",
                                deceasedName, nameGA, childName),
                    true));
            } else if (guardianAID != 0) {
                ev.eventHeadline = std::format("Following {}'s death, {} remained in the sole guardianship of {}.",
                                               deceasedName, childName, nameGA);
                ev.participants.push_back(MakeParticipant(
                    childID, "bereaved_child",
                    std::format("After {} died, {} became your sole remaining guardian.", deceasedName, nameGA),
                    childCanGenerate));
                ev.participants.push_back(MakeParticipant(
                    guardianAID, "sole_surviving_guardian",
                    std::format("Following {}'s death, you became the sole surviving guardian caring for {}.",
                                deceasedName, childName),
                    true));
            } else {
                ev.eventHeadline = std::format("Following {}'s death, {} was left without any surviving guardian.",
                                               deceasedName, childName);
                ev.participants.push_back(MakeParticipant(
                    childID, "orphaned_child",
                    std::format("With {}'s death, you were left without a surviving parent or guardian.", deceasedName),
                    childCanGenerate));
            }
            if (otherID != 0) {
                ev.participants.push_back(MakeParticipant(otherID, "deceased_parent", "", /*allowGenerateMemory=*/false));
            }
        } else if (eventType == "guardians_changed") {
            if (reason == "marriage" && guardianAID != 0 && guardianBID != 0) {
                ev.eventHeadline = std::format("Through marriage, {} and {} now share joint guardianship of {}{}.",
                                               nameGA, nameGB, childName,
                                               !nameOther.empty() ? std::format(" (replacing former guardian {})", nameOther) : "");
                ev.participants.push_back(MakeParticipant(
                    childID, "child_gaining_step_guardian",
                    std::format("{} and {} are now married and raising you together as your guardians.", nameGA, nameGB),
                    childCanGenerate));
                ev.participants.push_back(MakeParticipant(
                    guardianAID, "married_guardian",
                    std::format("Through your marriage with {}, the two of you now share guardianship of {}.", nameGB, childName),
                    true));
                ev.participants.push_back(MakeParticipant(
                    guardianBID, "new_co_guardian",
                    std::format("Through your marriage with {}, you welcomed {} under your shared guardianship.", nameGA, childName),
                    true));
                if (otherID != 0 && otherID != guardianAID && otherID != guardianBID) {
                    ev.participants.push_back(MakeParticipant(
                        otherID, "former_guardian",
                        std::format("Custody and guardianship of {} transferred to {} and {}.", childName, nameGA, nameGB),
                        true));
                }
            } else if (reason == "divorce" && guardianAID != 0) {
                ev.eventHeadline = std::format("Following the breakup/divorce, {} retained sole custody of {}, while {} lost guardianship.",
                                               nameGA, childName, !nameOther.empty() ? nameOther : "the former partner");
                ev.participants.push_back(MakeParticipant(
                    childID, "child_in_divorce",
                    std::format("After {} and {} separated, you stayed in the custody of {} while {} lost guardianship.",
                                nameGA, nameOther, nameGA, nameOther),
                    childCanGenerate));
                ev.participants.push_back(MakeParticipant(
                    guardianAID, "custodial_guardian",
                    std::format("Following your separation from {}, you kept sole custody and guardianship of {}.",
                                nameOther, childName),
                    true));
                if (otherID != 0) {
                    ev.participants.push_back(MakeParticipant(
                        otherID, "former_guardian_lost_custody",
                        std::format("Following your separation from {}, you lost custody and guardianship of {} to {}.",
                                    nameGA, childName, nameGA),
                        true));
                }
            } else {
                std::string newGuardians = (!nameGA.empty() && !nameGB.empty())
                                               ? std::format("{} and {}", nameGA, nameGB)
                                               : (!nameGA.empty() ? nameGA : nameGB);
                ev.eventHeadline = std::format("Guardianship of {} changed to {}{}.",
                                               childName,
                                               newGuardians.empty() ? "none" : newGuardians,
                                               !nameOther.empty() ? std::format(" (former guardian: {})", nameOther) : "");
                ev.participants.push_back(MakeParticipant(
                    childID, "child",
                    std::format("Your guardianship changed; you are now in the care of {}.",
                                newGuardians.empty() ? "no one" : newGuardians),
                    childCanGenerate));
                if (guardianAID != 0) {
                    ev.participants.push_back(MakeParticipant(
                        guardianAID, "guardian",
                        std::format("You are now an official guardian of {}.", childName),
                        true));
                }
                if (guardianBID != 0 && guardianBID != guardianAID) {
                    ev.participants.push_back(MakeParticipant(
                        guardianBID, "guardian",
                        std::format("You are now an official guardian of {}.", childName),
                        true));
                }
                if (otherID != 0 && otherID != guardianAID && otherID != guardianBID) {
                    ev.participants.push_back(MakeParticipant(
                        otherID, "former_guardian",
                        std::format("You are no longer a guardian of {}.", childName),
                        true));
                }
            }
        } else {
            return;
        }

        RecordEvent(std::move(ev));
    }

}  // namespace MARAS
