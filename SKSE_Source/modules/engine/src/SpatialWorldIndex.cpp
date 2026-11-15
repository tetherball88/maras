#include "maras/engine/SpatialWorldIndex.h"

#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/NPCIndexReportWriter.h"
#include "maras/engine/ProcessUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace MARAS {

    namespace {
        constexpr RE::FormID kKeywordDwelling = 0x000130DC;
        constexpr RE::FormID kKeywordHouse = 0x0001CB85;
        constexpr RE::FormID kLocationCenterMarker = 0x0001BDF1;
        constexpr std::uint32_t kFurnitureCanSleepMask =
            static_cast<std::uint32_t>(RE::TESFurniture::ActiveMarker::kCanSleep);
        constexpr const char* kDefaultRaceName = "Unknown";

        RE::Actor* ValidateCachedActor(RE::Actor* cached) {
            if (!cached) return nullptr;
            const auto formId = cached->GetFormID();
            if (formId == 0 || IsSyntheticChildID(formId)) return nullptr;
            auto* form = RE::TESForm::LookupByID(formId);
            if (form != cached || form->IsDeleted() || cached->IsDead()) return nullptr;
            return cached;
        }

        bool IsHumanoidActor(RE::TESNPC* base) {
            if (!base) return false;
            if (base->HasKeywordString("ActorTypeNPC")) {
                return true;
            }
            if (auto* race = base->GetRace(); race && race->HasKeywordString("ActorTypeNPC")) {
                return true;
            }
            return false;
        }

        bool IsHomeLocation(
            RE::BGSLocation* location,
            const RE::BGSKeyword* kwDwelling,
            const RE::BGSKeyword* kwHouse) {
            if (!location) return false;
            if (kwDwelling && location->HasKeyword(kwDwelling)) return true;
            if (kwHouse && location->HasKeyword(kwHouse)) return true;
            return false;
        }

        bool IsShareableHouseDoor(RE::TESObjectREFR& doorRef) {
            const auto lock = doorRef.GetLock();
            if (!lock || !lock->key) {
                return false;
            }
            const auto lvl = lock->GetLockLevel(&doorRef);
            return lvl != RE::LOCK_LEVEL::kRequiresKey;
        }

        bool IsBed(RE::TESFurniture* furniture) {
            if (!furniture) return false;
            const auto flags = furniture->furnFlags.underlying();
            return (flags & kFurnitureCanSleepMask) != 0;
        }

        std::string GetFormDisplayName(const RE::TESForm* form) {
            if (!form) return {};
            if (const auto* full = form->As<RE::TESFullName>()) {
                const char* n = full->GetFullName();
                if (n && n[0] != '\0') {
                    return n;
                }
            }
            const char* edid = form->GetFormEditorID();
            if (edid && edid[0] != '\0') {
                return edid;
            }
            return {};
        }

        std::string GetFormNameForLog(const RE::TESForm* form) {
            if (!form) return "(null)";
            if (auto name = GetFormDisplayName(form); !name.empty()) {
                return name;
            }
            return std::to_string(form->GetFormID());
        }

        std::string FormatFormID(RE::FormID formID) {
            return fmt::format("0x{:08X}", formID);
        }

        std::string FormatFormIDList(const std::vector<RE::FormID>& formIDs) {
            std::ostringstream ss;
            for (std::size_t i = 0; i < formIDs.size(); ++i) {
                if (i) ss << ", ";
                ss << FormatFormID(formIDs[i]);
            }
            return ss.str();
        }
    }  // namespace

    SpatialWorldIndex& SpatialWorldIndex::GetSingleton() {
        static SpatialWorldIndex instance;
        return instance;
    }

    bool SpatialWorldIndex::MatchesExclusionRule(
        std::string_view value,
        const std::unordered_set<std::string>& exactSet,
        const std::vector<std::string>& patterns) {
        if (value.empty()) return false;
        if (exactSet.contains(std::string(value))) {
            return true;
        }
        for (const auto& pattern : patterns) {
            if (Foundation::StringUtils::MatchesPattern(value, pattern)) {
                return true;
            }
        }
        return false;
    }

    bool SpatialWorldIndex::IsExcludedName(std::string_view lowerName) const {
        return MatchesExclusionRule(lowerName, m_excludeExactNames, m_excludeNamePatterns);
    }

    std::string SpatialWorldIndex::GetEditorIDExclusionReason(std::string_view lowerEdid) const {
        if (lowerEdid.empty()) return "";
        if (lowerEdid.find("clone") != std::string_view::npos) return "CloneEditorID";
        if (lowerEdid.find("dead") != std::string_view::npos ||
            lowerEdid.find("corpse") != std::string_view::npos) {
            return "DeadEditorID";
        }
        if (m_excludeExactEditorIDs.contains(std::string(lowerEdid))) return "INI_ExcludeExactEditorID";
        for (const auto& pattern : m_excludeEditorIDPatterns) {
            if (Foundation::StringUtils::MatchesPattern(lowerEdid, pattern)) {
                return "INI_ExcludeEditorIDPattern";
            }
        }
        return "";
    }

    bool SpatialWorldIndex::IsExcludedEditorID(std::string_view lowerEdid) const {
        return !GetEditorIDExclusionReason(lowerEdid).empty();
    }

    bool SpatialWorldIndex::IsExcludedOriginPlugin(RE::TESForm* form) const {
        if (!form || (m_excludeExactPlugins.empty() && m_excludePluginPatterns.empty())) return false;
        const auto* file = form->GetFile(0);
        if (!file || !file->fileName || *file->fileName == '\0') return false;
        const std::string fileNameLower = Foundation::StringUtils::ToLowerStd(file->fileName);
        return MatchesExclusionRule(fileNameLower, m_excludeExactPlugins, m_excludePluginPatterns);
    }

    bool SpatialWorldIndex::IsValidNPCCandidate(
        RE::TESNPC* base,
        RE::TESObjectREFR* refr,
        std::string* outReason) const {
        auto setReason = [&](std::string_view r) {
            if (outReason) *outReason = std::string(r);
        };

        if (!base) {
            setReason("NullBase");
            return false;
        }

        const RE::FormID baseID = base->GetFormID();
        const RE::FormID refID = refr ? refr->GetFormID() : 0;

        if (baseID == 0 || baseID == kPlayerBaseID || baseID == kPlayerRefID) {
            setReason("PlayerOrNullFormID");
            return false;
        }

        if (refID != 0 && (refID == kPlayerRefID || refID == kPlayerBaseID || (refID >> 24) == 0xFF)) {
            setReason("PlayerOrDynamicRefID");
            return false;
        }

        if (refr) {
            if (refr->IsDeleted()) {
                setReason("DeletedReference");
                return false;
            }
            if (auto* actor = refr->As<RE::Actor>(); actor && actor->IsDead()) {
                setReason("DeadActor");
                return false;
            }
            if (refr->extraList.HasType(RE::ExtraDataType::kLeveledCreature)) {
                setReason("LeveledSpawn");
                return false;
            }
            if (auto* baseObj = refr->GetBaseObject(); baseObj && baseObj->GetFormType() == RE::FormType::LeveledNPC) {
                setReason("LeveledPlacedReference");
                return false;
            }
        }

        const bool isExplicitlyExcluded =
            m_iniExcludeForms.contains(baseID) || (refID != 0 && m_iniExcludeForms.contains(refID));
        if (isExplicitlyExcluded) {
            setReason("INI_ExcludeFormID");
            return false;
        }

        if (base->IsGhost()) {
            setReason("Ghost");
            return false;
        }
        if (base->IsPreset()) {
            setReason("ChargenPreset");
            return false;
        }

        if (!IsHumanoidActor(base)) {
            setReason("NonHumanoid");
            return false;
        }

        const char* nameStr = base->GetFullName();
        if (!nameStr || nameStr[0] == '\0' || std::strcmp(nameStr, "None") == 0) {
            nameStr = base->GetName();
        }
        if (!nameStr || nameStr[0] == '\0' || std::strcmp(nameStr, "None") == 0) {
            setReason("NoName");
            return false;
        }

        const std::string nameLower = Foundation::StringUtils::ToLowerStd(nameStr);
        if (IsExcludedName(nameLower)) {
            const std::string reason =
                m_excludeExactNames.contains(nameLower) ? "INI_ExcludeExactName" : "INI_ExcludeNamePattern";
            setReason(reason);
            return false;
        }

        const char* bEdid = base->GetFormEditorID();
        const std::string bEdidStr = (bEdid && bEdid[0] != '\0') ? bEdid : "";
        if (!bEdidStr.empty()) {
            const std::string bEdidReason = GetEditorIDExclusionReason(Foundation::StringUtils::ToLowerStd(bEdidStr));
            if (!bEdidReason.empty()) {
                setReason(bEdidReason);
                return false;
            }
        }

        std::string rEdidStr;
        if (refr) {
            const char* rEdid = refr->GetFormEditorID();
            if (rEdid && rEdid[0] != '\0') {
                rEdidStr = rEdid;
                const std::string rEdidReason = GetEditorIDExclusionReason(Foundation::StringUtils::ToLowerStd(rEdidStr));
                if (!rEdidReason.empty()) {
                    setReason(rEdidReason);
                    return false;
                }
            }
        }

        if (IsExcludedOriginPlugin(base)) {
            const auto* file = base->GetFile(0);
            const std::string pluginName = (file && file->fileName) ? file->fileName : "Plugin";
            setReason("INI_ExcludePlugin(" + pluginName + ")");
            return false;
        }
        if (refr && IsExcludedOriginPlugin(refr)) {
            const auto* file = refr->GetFile(0);
            const std::string pluginName = (file && file->fileName) ? file->fileName : "Plugin";
            setReason("INI_ExcludePlugin(" + pluginName + ")");
            return false;
        }

        const bool isExplicitlyIncluded =
            m_iniIncludeForms.contains(baseID) || (refID != 0 && m_iniIncludeForms.contains(refID));
        if (isExplicitlyIncluded) {
            return true;
        }

        if (base->IsUnique()) {
            return true;
        }

        if (refr && refr->IsPersistent()) {
            if (bEdidStr.empty() && rEdidStr.empty()) {
                setReason("NonUniquePersistentNoEditorID");
                return false;
            }
            return true;
        }

        setReason("NonUniqueNonPersistent");
        return false;
    }

    void SpatialWorldIndex::LoadOverridesLocked() {
        const std::string iniPath = "Data/SKSE/Plugins/MARAS/npc_index_overrides.ini";
        std::ifstream file(iniPath);
        if (!file.is_open()) {
            MARAS_LOG_INFO("SpatialWorldIndex: No overrides INI found at '{}'", iniPath);
            m_excludeExactNames.clear();
            m_excludeNamePatterns.clear();
            m_excludeExactEditorIDs.clear();
            m_excludeEditorIDPatterns.clear();
            m_excludeExactPlugins.clear();
            m_excludePluginPatterns.clear();
            m_iniIncludeForms.clear();
            m_iniExcludeForms.clear();
            return;
        }

        std::unordered_set<std::string> excludeExactNames;
        std::vector<std::string> excludeNamePatterns;
        std::unordered_set<std::string> excludeExactEditorIDs;
        std::vector<std::string> excludeEditorIDPatterns;
        std::unordered_set<std::string> excludeExactPlugins;
        std::vector<std::string> excludePluginPatterns;
        std::unordered_set<RE::FormID> iniIncludeForms;
        std::unordered_set<RE::FormID> iniExcludeForms;

        std::string currentSection;
        std::string line;

        while (std::getline(file, line)) {
            std::string trimmed = Foundation::StringUtils::TrimStd(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                continue;
            }

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = Foundation::StringUtils::ToLowerStd(trimmed.substr(1, trimmed.length() - 2));
                if (currentSection != "include" && currentSection != "exclude" &&
                    currentSection != "excludenames" && currentSection != "excludeeditorids" &&
                    currentSection != "excludeplugins") {
                    MARAS_LOG_WARN("SpatialWorldIndex: Unrecognized section [{}] in overrides INI", currentSection);
                }
                continue;
            }

            const auto commentPos = trimmed.find_first_of(";#");
            if (commentPos != std::string::npos) {
                trimmed = Foundation::StringUtils::TrimStd(trimmed.substr(0, commentPos));
                if (trimmed.empty()) continue;
            }

            if (trimmed.length() >= 2 &&
                ((trimmed.front() == '"' && trimmed.back() == '"') ||
                 (trimmed.front() == '\'' && trimmed.back() == '\''))) {
                trimmed = trimmed.substr(1, trimmed.length() - 2);
            }

            if (currentSection == "excludenames") {
                const std::string lowerStr = Foundation::StringUtils::ToLowerStd(trimmed);
                if (lowerStr.find('*') != std::string::npos || lowerStr.find('?') != std::string::npos) {
                    excludeNamePatterns.push_back(lowerStr);
                } else {
                    excludeExactNames.insert(lowerStr);
                }
                continue;
            }

            if (currentSection == "excludeeditorids") {
                const std::string lowerStr = Foundation::StringUtils::ToLowerStd(trimmed);
                if (lowerStr.find('*') != std::string::npos || lowerStr.find('?') != std::string::npos) {
                    excludeEditorIDPatterns.push_back(lowerStr);
                } else {
                    excludeExactEditorIDs.insert(lowerStr);
                }
                continue;
            }

            if (currentSection == "excludeplugins") {
                const std::string lowerStr = Foundation::StringUtils::ToLowerStd(trimmed);
                if (lowerStr.find('*') != std::string::npos || lowerStr.find('?') != std::string::npos) {
                    excludePluginPatterns.push_back(lowerStr);
                } else {
                    excludeExactPlugins.insert(lowerStr);
                    if (!lowerStr.ends_with(".esp") && !lowerStr.ends_with(".esm") && !lowerStr.ends_with(".esl")) {
                        excludeExactPlugins.insert(lowerStr + ".esp");
                        excludeExactPlugins.insert(lowerStr + ".esm");
                        excludeExactPlugins.insert(lowerStr + ".esl");
                    }
                }
                continue;
            }

            RE::FormID resolvedFormID = 0;

            if (trimmed.starts_with("__formData|")) {
                if (const auto opt = Utils::ParseAndResolveFormKey(trimmed)) {
                    resolvedFormID = *opt;
                }
            } else if (trimmed.find('|') != std::string::npos) {
                const auto pipePos = trimmed.find('|');
                const std::string pluginName = trimmed.substr(0, pipePos);
                const std::string hexID = trimmed.substr(pipePos + 1);
                if (const auto optHex = Utils::HexStringToFormID(hexID)) {
                    if (auto* form = Utils::LookupForm<RE::TESForm>(*optHex, pluginName, "SpatialWorldIndex")) {
                        resolvedFormID = form->GetFormID();
                    }
                }
            } else if (trimmed.starts_with("0x") || trimmed.starts_with("0X")) {
                if (const auto opt = Utils::HexStringToFormID(trimmed)) {
                    resolvedFormID = *opt;
                }
            } else {
                if (auto* form = RE::TESForm::LookupByEditorID(trimmed)) {
                    resolvedFormID = form->GetFormID();
                }
            }

            if (resolvedFormID != 0) {
                if (currentSection == "include") {
                    iniIncludeForms.insert(resolvedFormID);
                    MARAS_LOG_DEBUG("SpatialWorldIndex: INI include rule added for FormID 0x{:08X}", resolvedFormID);
                } else if (currentSection == "exclude") {
                    iniExcludeForms.insert(resolvedFormID);
                    MARAS_LOG_DEBUG("SpatialWorldIndex: INI exclude rule added for FormID 0x{:08X}", resolvedFormID);
                }
            } else {
                MARAS_LOG_WARN("SpatialWorldIndex: Failed to resolve INI entry '{}' in section [{}]",
                               trimmed, currentSection);
            }
        }

        m_excludeExactNames = std::move(excludeExactNames);
        m_excludeNamePatterns = std::move(excludeNamePatterns);
        m_excludeExactEditorIDs = std::move(excludeExactEditorIDs);
        m_excludeEditorIDPatterns = std::move(excludeEditorIDPatterns);
        m_excludeExactPlugins = std::move(excludeExactPlugins);
        m_excludePluginPatterns = std::move(excludePluginPatterns);
        m_iniIncludeForms = std::move(iniIncludeForms);
        m_iniExcludeForms = std::move(iniExcludeForms);

        MARAS_LOG_INFO("SpatialWorldIndex: Loaded INI overrides: {} includes, {} excludes, {} exact name filters, {} name patterns, {} exact editorID filters, {} editorID patterns",
                       m_iniIncludeForms.size(), m_iniExcludeForms.size(), m_excludeExactNames.size(),
                       m_excludeNamePatterns.size(), m_excludeExactEditorIDs.size(), m_excludeEditorIDPatterns.size());
    }

    void SpatialWorldIndex::ClearNpcIndexLocked() {
        m_entriesByRef.clear();
        m_baseToRefMap.clear();
        m_nameToRefMap.clear();
        m_loadedActors.clear();
        m_allNames.clear();
        m_excludeExactNames.clear();
        m_excludeNamePatterns.clear();
        m_excludeExactEditorIDs.clear();
        m_excludeEditorIDPatterns.clear();
        m_excludeExactPlugins.clear();
        m_excludePluginPatterns.clear();
        m_iniIncludeForms.clear();
        m_iniExcludeForms.clear();
        m_indexBuilt.store(false, std::memory_order_release);
    }

    void SpatialWorldIndex::BuildIndex() {
        BuildNpcIndex();
        std::unique_lock lock(m_mutex);
        BuildHomeCellIndexLocked();
        BuildInnDestinationIndexLocked();
    }

    void SpatialWorldIndex::BuildNpcIndex() {
        bool shouldDumpReport = false;
        if (const auto worldSnap = State::WorldStore::GetSingleton().Snapshot(); worldSnap && worldSnap->settings) {
            shouldDumpReport = (worldSnap->settings->logLevel <= 1);
        }

        std::vector<UniqueNPCInfo> uniqueBaseNPCs;
        std::vector<UpgradedRefInfo> upgradedUniqueRefs;
        std::vector<ExcludedNPCInfo> excludedUnique;
        std::vector<ExcludedNPCInfo> excludedNonUnique;

        {
            std::unique_lock lock(m_mutex);

            const auto startTime = std::chrono::steady_clock::now();
            MARAS_LOG_INFO("SpatialWorldIndex: Building centralized NPC index...");

            m_entriesByRef.clear();
            m_baseToRefMap.clear();
            m_nameToRefMap.clear();
            m_loadedActors.clear();
            m_allNames.clear();

            LoadOverridesLocked();

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                MARAS_LOG_ERROR("SpatialWorldIndex: TESDataHandler is null, aborting NPC index build");
                return;
            }

            const auto& npcArray = dataHandler->GetFormArray<RE::TESNPC>();
            for (auto* npc : npcArray) {
                if (!npc) continue;

                const RE::FormID baseID = npc->GetFormID();
                const char* edid = npc->GetFormEditorID();
                const std::string edidStr = (edid && edid[0] != '\0') ? edid : "";
                const char* nameStr = npc->GetFullName();
                if (!nameStr || nameStr[0] == '\0' || std::strcmp(nameStr, "None") == 0) {
                    nameStr = npc->GetName();
                }
                const std::string nameDisplay =
                    (nameStr && nameStr[0] != '\0' && std::strcmp(nameStr, "None") != 0) ? nameStr : "(No Name)";
                const bool isChild = npc->race ? npc->race->data.flags.any(RE::RACE_DATA::Flag::kChild) : false;
                const bool isUnique = npc->IsUnique();

                std::string reason;
                if (!IsValidNPCCandidate(npc, nullptr, shouldDumpReport ? &reason : nullptr)) {
                    if (shouldDumpReport) {
                        if (isUnique) {
                            excludedUnique.push_back({ nameDisplay, edidStr, baseID, reason, isChild });
                        } else {
                            excludedNonUnique.push_back({ nameDisplay, edidStr, baseID, reason, isChild });
                        }
                    }
                    continue;
                }

                const std::string nameLower = Foundation::StringUtils::ToLowerStd(nameDisplay);
                const std::string raceName =
                    (npc->race && npc->race->GetFullName()) ? npc->race->GetFullName() : kDefaultRaceName;

                NPCEntry entry;
                entry.baseFormID = baseID;
                entry.refFormID = baseID;  // Will be upgraded to placed RefID below
                entry.editorID = edidStr;
                entry.nameDisplay = nameDisplay;
                entry.nameLower = nameLower;
                entry.isChild = isChild;
                entry.isUnique = isUnique;
                entry.sex = static_cast<std::uint8_t>(npc->GetSex());
                entry.raceName = raceName;
                entry.crimeFactionID = npc->crimeFaction ? npc->crimeFaction->GetFormID() : 0;

                const auto* originFile = npc->GetFile(0);
                entry.pluginName = (originFile && originFile->fileName) ? originFile->fileName : "";

                m_entriesByRef[baseID] = entry;
                m_baseToRefMap[baseID] = baseID;
                if (shouldDumpReport) {
                    uniqueBaseNPCs.push_back({
                        entry.nameDisplay,
                        entry.editorID,
                        baseID,
                        entry.raceName,
                        entry.sex == 1 ? "Female" : "Male",
                        entry.isChild
                    });
                }
            }

            MARAS_LOG_INFO("SpatialWorldIndex: Phase 1 complete: {} unique/included base forms indexed",
                           m_entriesByRef.size());

            int persistentResolvedCount = 0;
            auto resolveBaseFormIDs = [&](RE::TESObjectCELL* cell) {
                if (!cell) return;
                cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                    if (!ref) return RE::BSContainer::ForEachResult::kContinue;

                    RE::TESNPC* base = nullptr;
                    if (auto* actor = ref->As<RE::Actor>()) {
                        base = actor->GetActorBase();
                    } else if (auto* baseObj = ref->GetBaseObject()) {
                        base = baseObj->As<RE::TESNPC>();
                    }
                    if (!base) return RE::BSContainer::ForEachResult::kContinue;

                    const RE::FormID baseID = base->GetFormID();
                    const RE::FormID refID = ref->GetFormID();

                    if (!IsValidNPCCandidate(base, ref)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    auto it = m_entriesByRef.find(baseID);
                    if (it != m_entriesByRef.end()) {
                        bool shouldUpgrade = false;
                        if (it->second.refFormID == baseID) {
                            shouldUpgrade = true;
                        } else if (ref->IsPersistent()) {
                            auto* prevRefr = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->second.refFormID);
                            if (!prevRefr || !prevRefr->IsPersistent()) {
                                shouldUpgrade = true;
                            }
                        }

                        if (shouldUpgrade) {
                            it->second.refFormID = refID;
                            it->second.placementCellID = cell->GetFormID();
                            RE::BGSLocation* loc = ref->GetEditorLocation();
                            if (!loc) loc = cell->GetLocation();
                            if (loc) it->second.editorLocationID = loc->GetFormID();
                            m_baseToRefMap[baseID] = refID;
                            ++persistentResolvedCount;
                            if (shouldDumpReport) {
                                const char* cellName = cell->GetName();
                                upgradedUniqueRefs.push_back({
                                    it->second.nameDisplay,
                                    it->second.editorID,
                                    baseID,
                                    refID,
                                    (cellName && cellName[0]) ? cellName : "Interior / Worldspace Persistent"
                                });
                            }
                        }
                    } else {
                        const char* nameStr = base->GetFullName();
                        if (!nameStr || nameStr[0] == '\0' || std::strcmp(nameStr, "None") == 0) {
                            nameStr = base->GetName();
                        }
                        const std::string nameDisplay =
                            (nameStr && nameStr[0] != '\0' && std::strcmp(nameStr, "None") != 0) ? nameStr : "(No Name)";
                        const std::string nameLower = Foundation::StringUtils::ToLowerStd(nameDisplay);
                        const char* edid = base->GetFormEditorID();
                        const std::string edidStr = (edid && edid[0] != '\0') ? edid : "";
                        const bool isChild = (base->race && base->race->data.flags.any(RE::RACE_DATA::Flag::kChild));
                        const std::string raceName =
                            (base->race && base->race->GetFullName()) ? base->race->GetFullName() : kDefaultRaceName;

                        NPCEntry entry;
                        entry.baseFormID = baseID;
                        entry.refFormID = refID;
                        entry.editorID = edidStr;
                        entry.nameDisplay = nameDisplay;
                        entry.nameLower = nameLower;
                        entry.isChild = isChild;
                        entry.isUnique = false;
                        entry.placementCellID = cell->GetFormID();
                        RE::BGSLocation* loc = ref->GetEditorLocation();
                        if (!loc) loc = cell->GetLocation();
                        entry.editorLocationID = loc ? loc->GetFormID() : 0;
                        entry.crimeFactionID = base->crimeFaction ? base->crimeFaction->GetFormID() : 0;
                        entry.sex = static_cast<std::uint8_t>(base->GetSex());
                        entry.raceName = raceName;

                        const auto* originFile = base->GetFile(0);
                        entry.pluginName = (originFile && originFile->fileName) ? originFile->fileName : "";

                        m_entriesByRef[baseID] = entry;
                        m_baseToRefMap[baseID] = refID;
                        ++persistentResolvedCount;
                        if (shouldDumpReport) {
                            const char* cellName = cell->GetName();
                            upgradedUniqueRefs.push_back({
                                entry.nameDisplay,
                                entry.editorID,
                                baseID,
                                refID,
                                (cellName && cellName[0]) ? cellName : "Interior / Worldspace Persistent (Non-Unique)"
                            });
                        }
                    }

                    return RE::BSContainer::ForEachResult::kContinue;
                });
            };

            const auto& worldspaces = dataHandler->GetFormArray<RE::TESWorldSpace>();
            for (auto* worldspace : worldspaces) {
                if (!worldspace || !worldspace->persistentCell) continue;
                resolveBaseFormIDs(worldspace->persistentCell);
            }

            for (auto* cell : dataHandler->interiorCells) {
                if (!cell) continue;
                resolveBaseFormIDs(cell);
            }

            MARAS_LOG_INFO("SpatialWorldIndex: Phase 1.5 & 1.6 complete: {} base FormIDs upgraded to placed references",
                           persistentResolvedCount);

            std::unordered_map<RE::FormID, NPCEntry> finalEntriesByRef;
            std::unordered_map<RE::FormID, RE::FormID> finalBaseToRefMap;
            finalEntriesByRef.reserve(m_entriesByRef.size());
            finalBaseToRefMap.reserve(m_entriesByRef.size());
            m_nameToRefMap.reserve(m_entriesByRef.size());
            m_allNames.reserve(m_entriesByRef.size());

            std::unordered_set<std::string> uniqueNames;
            for (const auto& [origKey, entry] : m_entriesByRef) {
                (void)origKey;
                if (entry.refFormID == 0 || entry.refFormID == entry.baseFormID) {
                    auto* form = RE::TESForm::LookupByID(entry.refFormID);
                    if (!form ||
                        (form->GetFormType() != RE::FormType::ActorCharacter &&
                         form->GetFormType() != RE::FormType::Reference)) {
                        continue;
                    }
                }

                const RE::FormID primaryKey = entry.refFormID;
                finalEntriesByRef[primaryKey] = entry;
                if (entry.baseFormID != 0) {
                    finalBaseToRefMap[entry.baseFormID] = primaryKey;
                }
                m_nameToRefMap[entry.nameLower].push_back(primaryKey);
                if (uniqueNames.insert(entry.nameLower).second) {
                    m_allNames.push_back(entry.nameLower);
                }
            }

            m_entriesByRef = std::move(finalEntriesByRef);
            m_baseToRefMap = std::move(finalBaseToRefMap);

            ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
                IndexLoadedActorLocked(actor);
                return false;
            });

            m_indexBuilt.store(true, std::memory_order_release);
            const std::size_t childCount = GetChildCountLocked();
            const std::size_t adultCount = m_entriesByRef.size() >= childCount ? (m_entriesByRef.size() - childCount) : 0;
            const auto endTime = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            MARAS_LOG_INFO("SpatialWorldIndex: NPC build complete in {} ms — {} total indexed NPCs ({} children, {} adults, {} currently loaded)",
                           elapsedMs, m_entriesByRef.size(), childCount, adultCount, m_loadedActors.size());
        }

        // Write promotions report outside m_mutex so readers/writers are never blocked on file I/O.
        if (shouldDumpReport) {
            NPCIndexReportWriter::WriteReport(
                std::move(uniqueBaseNPCs),
                std::move(upgradedUniqueRefs),
                std::move(excludedUnique),
                std::move(excludedNonUnique));
        }
    }

    void SpatialWorldIndex::BuildHomeCellIndex() {
        std::unique_lock lock(m_mutex);
        BuildHomeCellIndexLocked();
    }

    void SpatialWorldIndex::BuildHomeCellIndexLocked() {
        m_homeCells.clear();
        m_actorsWithHome.clear();
        m_bedsWithOwners.clear();

        const auto start = std::chrono::high_resolution_clock::now();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_WARN("SpatialWorldIndex: TESDataHandler not available for HomeCellIndex");
            return;
        }

        const auto* kwDwelling = RE::TESForm::LookupByID<RE::BGSKeyword>(kKeywordDwelling);
        const auto* kwHouse = RE::TESForm::LookupByID<RE::BGSKeyword>(kKeywordHouse);

        std::size_t scanned = 0;

        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;

            auto* location = cell->GetLocation();
            if (!IsHomeLocation(location, kwDwelling, kwHouse)) continue;

            HomeCellData hdata;
            if (!ScanHomeCellLocked(cell, hdata)) continue;

            m_homeCells[cell->GetFormID()] = std::move(hdata);
            ++scanned;
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        MARAS_LOG_INFO("SpatialWorldIndex: built home cell index for {} cells in {} ms", scanned, dur);
    }

    bool SpatialWorldIndex::ScanHomeCellLocked(RE::TESObjectCELL* cell, HomeCellData& outData) {
        if (!cell) return false;
        const RE::FormID cellId = cell->GetFormID();

        bool hasPersistentActor = false;
        static RE::BGSLocationRefType* kLocationCenterLCRT =
            RE::TESForm::LookupByID<RE::BGSLocationRefType>(kLocationCenterMarker);

        bool foundCenterMarker = false;
        std::vector<RE::TESObjectREFR*> candidateBeds;

        cell->ForEachReference([&](RE::TESObjectREFR* ref) {
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;
            auto* base = ref->GetBaseObject();
            if (!base) return RE::BSContainer::ForEachResult::kContinue;

            if (base->formType == RE::FormType::Door) {
                if (IsShareableHouseDoor(*ref)) {
                    outData.doors.push_back(ref->GetFormID());
                }
            }

            if (base->formType == RE::FormType::Furniture) {
                if (auto* furn = base->As<RE::TESFurniture>(); furn && IsBed(furn)) {
                    candidateBeds.push_back(ref);
                }
            }

            if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                if (lrt->locRefType == kLocationCenterLCRT) {
                    outData.centerMarker = ref->GetFormID();
                    foundCenterMarker = true;
                }
            }

            if (!foundCenterMarker && !outData.centerMarker) {
                outData.centerMarker = ref->GetFormID();
            }

            if (ref->IsPersistent() && base->formType == RE::FormType::NPC) {
                hasPersistentActor = true;
                ProcessActorHomeLocked(ref->GetFormID(), cellId);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (hasPersistentActor) {
            for (auto* bedRef : candidateBeds) {
                ProcessBedOwnershipLocked(*bedRef);
            }
            if (!outData.centerMarker) {
                MARAS_LOG_WARN("SpatialWorldIndex: Cell {:08X} '{}' has persistent actors but NO center marker found",
                               cellId, GetFormNameForLog(cell));
            }
        }

        return hasPersistentActor;
    }

    void SpatialWorldIndex::ProcessBedOwnershipLocked(RE::TESObjectREFR& bedRef) {
        RE::TESForm* ownerForm = bedRef.extraList.GetOwner();
        if (!ownerForm) return;
        m_bedsWithOwners[ownerForm->GetFormID()].push_back(bedRef.GetFormID());
    }

    void SpatialWorldIndex::ProcessActorHomeLocked(RE::FormID actorFormId, RE::FormID cellId) {
        if (!m_actorsWithHome.contains(actorFormId)) {
            ActorHomeData adata;
            adata.homeCell = cellId;
            m_actorsWithHome.emplace(actorFormId, std::move(adata));
        }
    }

    void SpatialWorldIndex::BuildInnDestinationIndex() {
        std::unique_lock lock(m_mutex);
        BuildInnDestinationIndexLocked();
    }

    void SpatialWorldIndex::BuildInnDestinationIndexLocked() {
        m_innDestinations.clear();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_WARN("SpatialWorldIndex: TESDataHandler unavailable during BuildInnDestinationIndex");
            return;
        }

        auto* kwInn = FormCache::GetSingleton().GetLocTypeInnKeyword();
        if (!kwInn) {
            MARAS_LOG_WARN("SpatialWorldIndex: LocTypeInn keyword unavailable");
            return;
        }

        auto* centerLCRT = FormCache::GetSingleton().GetLocationCenterMarkerRefType();
        std::unordered_set<RE::FormID> seenLocations;

        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;

            auto* location = cell->GetLocation();
            if (!location || !location->HasKeyword(kwInn)) continue;

            const RE::FormID locID = location->GetFormID();
            if (locID == 0 || seenLocations.contains(locID)) continue;

            RE::FormID centerMarkerID = 0;
            RE::FormID fallbackMarkerID = 0;

            cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (centerLCRT) {
                    if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                        if (lrt->locRefType == centerLCRT) {
                            centerMarkerID = ref->GetFormID();
                            return RE::BSContainer::ForEachResult::kStop;
                        }
                    }
                }
                if (fallbackMarkerID == 0 && ref->IsPersistent()) {
                    fallbackMarkerID = ref->GetFormID();
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            const RE::FormID chosenMarkerID = (centerMarkerID != 0) ? centerMarkerID : fallbackMarkerID;
            if (chosenMarkerID == 0) continue;

            std::string innName = GetFormDisplayName(location);
            if (innName.empty()) {
                innName = GetFormDisplayName(cell);
            }
            if (innName.empty()) {
                innName = "Local Inn";
            }

            seenLocations.insert(locID);
            m_innDestinations.push_back(Domain::Household::TravelDestinationCandidate{
                Domain::Household::RefId{ chosenMarkerID },
                Domain::Household::LocationId{ locID },
                Domain::Household::CellId{ cell->GetFormID() },
                std::move(innName),
                false
            });
        }

        MARAS_LOG_INFO("SpatialWorldIndex: Indexed {} Inn/Tavern destinations across Skyrim",
                       m_innDestinations.size());
    }

    void SpatialWorldIndex::IndexLoadedActorLocked(RE::Actor* actor) {
        if (!actor) return;
        auto* base = actor->GetActorBase();
        if (!base) return;

        if (!IsValidNPCCandidate(base, actor)) {
            return;
        }

        const RE::FormID refID = actor->GetFormID();
        const RE::FormID baseID = base->GetFormID();
        const char* displayName = actor->GetDisplayFullName();
        if (!displayName || displayName[0] == '\0') return;

        const std::string lowerName = Foundation::StringUtils::ToLowerStd(displayName);

        m_loadedActors[refID] = actor;
        m_baseToRefMap[baseID] = refID;

        auto it = m_entriesByRef.find(refID);
        if (it != m_entriesByRef.end()) {
            if (auto* cell = actor->GetParentCell()) {
                const char* cname = cell->GetName();
                it->second.currentCell = (cname && cname[0]) ? cname : "";
                if (it->second.placementCellID == 0) {
                    it->second.placementCellID = cell->GetFormID();
                }
                if (it->second.editorLocationID == 0) {
                    RE::BGSLocation* loc = actor->GetEditorLocation();
                    if (!loc) loc = cell->GetLocation();
                    if (loc) it->second.editorLocationID = loc->GetFormID();
                }
                if (it->second.crimeFactionID == 0 && base->crimeFaction) {
                    it->second.crimeFactionID = base->crimeFaction->GetFormID();
                }
            }
        } else {
            NPCEntry entry;
            entry.refFormID = refID;
            entry.baseFormID = baseID;
            const char* edid = base->GetFormEditorID();
            entry.editorID = (edid && edid[0] != '\0') ? edid : "";
            entry.nameDisplay = displayName;
            entry.nameLower = lowerName;
            entry.isChild = actor->IsChild();
            entry.isUnique = base->IsUnique();
            entry.sex = static_cast<std::uint8_t>(base->GetSex());
            const auto* race = actor->GetRace();
            entry.raceName = (race && race->GetFullName()) ? race->GetFullName() : kDefaultRaceName;

            const auto* originFile = base->GetFile(0);
            entry.pluginName = (originFile && originFile->fileName) ? originFile->fileName : "";

            if (auto* cell = actor->GetParentCell()) {
                const char* cname = cell->GetName();
                entry.currentCell = (cname && cname[0]) ? cname : "";
                entry.placementCellID = cell->GetFormID();
                RE::BGSLocation* loc = actor->GetEditorLocation();
                if (!loc) loc = cell->GetLocation();
                entry.editorLocationID = loc ? loc->GetFormID() : 0;
            }
            if (entry.editorLocationID == 0) {
                if (auto* loc = actor->GetEditorLocation()) {
                    entry.editorLocationID = loc->GetFormID();
                }
            }
            if (base->crimeFaction) {
                entry.crimeFactionID = base->crimeFaction->GetFormID();
            }

            m_entriesByRef[refID] = entry;

            auto& refList = m_nameToRefMap[lowerName];
            if (refList.empty()) {
                m_allNames.push_back(lowerName);
            }
            if (std::find(refList.begin(), refList.end(), refID) == refList.end()) {
                refList.push_back(refID);
            }

            MARAS_LOG_INFO("SpatialWorldIndex: Dynamically indexed actor '{}' (Base ID 0x{:08X} -> Ref ID 0x{:08X}) in cell '{}'",
                           entry.nameDisplay, baseID, refID, entry.currentCell);
        }
    }

    void SpatialWorldIndex::RegisterLoadedActor(RE::Actor* actor) {
        if (!actor) return;
        std::unique_lock lock(m_mutex);
        IndexLoadedActorLocked(actor);
    }

    void SpatialWorldIndex::RefreshIndex() {
        if (!m_indexBuilt.load(std::memory_order_acquire)) {
            BuildNpcIndex();
            return;
        }

        std::unique_lock lock(m_mutex);

        std::size_t evicted = 0;
        for (auto it = m_loadedActors.begin(); it != m_loadedActors.end();) {
            if (!ValidateCachedActor(it->second)) {
                it = m_loadedActors.erase(it);
                ++evicted;
            } else {
                ++it;
            }
        }

        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedActorLocked(actor);
            return false;
        });

        MARAS_LOG_INFO("SpatialWorldIndex: Refreshed index ({} loaded actors, {} stale evicted)",
                       m_loadedActors.size(), evicted);
    }

    void SpatialWorldIndex::ClearLoadedActors() {
        std::unique_lock lock(m_mutex);
        m_loadedActors.clear();
        MARAS_LOG_INFO("SpatialWorldIndex: Cleared cached loaded actor pointers on pre-load");
    }

    void SpatialWorldIndex::ClearIndex() {
        std::unique_lock lock(m_mutex);
        ClearNpcIndexLocked();
        MARAS_LOG_INFO("SpatialWorldIndex: NPC index cleared");
    }

    void SpatialWorldIndex::Revert() {
        std::unique_lock lock(m_mutex);
        ClearNpcIndexLocked();
        m_homeCells.clear();
        m_actorsWithHome.clear();
        m_bedsWithOwners.clear();
        m_innDestinations.clear();
        MARAS_LOG_INFO("SpatialWorldIndex: Reverted all index data");
    }

    const NPCEntry* SpatialWorldIndex::FindByNameLocked(std::string_view searchTerm, NPCFilter filter) const {
        if (searchTerm.empty()) return nullptr;

        const std::string lowerSearch = Foundation::StringUtils::ToLowerStd(searchTerm);

        // 1. Exact match in name map
        if (const auto exactIt = m_nameToRefMap.find(lowerSearch); exactIt != m_nameToRefMap.end()) {
            for (const RE::FormID refID : exactIt->second) {
                const auto entryIt = m_entriesByRef.find(refID);
                if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                    return &entryIt->second;
                }
            }
        }

        // 2. Fuzzy match with Levenshtein distance
        constexpr int kMaxFuzzyDist = 3;
        if (const auto fuzzy = Foundation::StringUtils::FuzzyFind(lowerSearch, m_allNames, kMaxFuzzyDist)) {
            if (const auto nameIt = m_nameToRefMap.find(fuzzy.match); nameIt != m_nameToRefMap.end()) {
                for (const RE::FormID refID : nameIt->second) {
                    const auto entryIt = m_entriesByRef.find(refID);
                    if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                        return &entryIt->second;
                    }
                }
            }
        }

        // 3. Deterministic forward-substring match
        const NPCEntry* bestSubstringEntry = nullptr;
        for (const auto& [refID, entry] : m_entriesByRef) {
            (void)refID;
            if (!PassesFilter(entry, filter)) continue;
            if (entry.nameLower.find(lowerSearch) != std::string::npos) {
                if (!bestSubstringEntry) {
                    bestSubstringEntry = &entry;
                } else {
                    const bool candPrefix = entry.nameLower.starts_with(lowerSearch);
                    const bool bestPrefix = bestSubstringEntry->nameLower.starts_with(lowerSearch);
                    if (candPrefix != bestPrefix) {
                        if (candPrefix) bestSubstringEntry = &entry;
                    } else if (entry.nameLower.length() != bestSubstringEntry->nameLower.length()) {
                        if (entry.nameLower.length() < bestSubstringEntry->nameLower.length()) {
                            bestSubstringEntry = &entry;
                        }
                    } else if (entry.nameLower != bestSubstringEntry->nameLower) {
                        if (entry.nameLower < bestSubstringEntry->nameLower) bestSubstringEntry = &entry;
                    } else if (entry.refFormID < bestSubstringEntry->refFormID) {
                        bestSubstringEntry = &entry;
                    }
                }
            }
        }
        if (bestSubstringEntry) {
            return bestSubstringEntry;
        }

        // 4. Snapshot scan fallback across latest published 2 Hz SpatialActorSnapshot
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        RE::FormID bestLiveRefID = 0;
        int bestLiveDist = kMaxFuzzyDist + 1;
        const int searchLen = static_cast<int>(lowerSearch.length());

        if (spatialSnap) {
            for (const auto& sample : spatialSnap->AllSamples()) {
                if (sample.refId.IsNull() || sample.IsDeleted() || sample.IsDead()) continue;
                const auto displayName = sample.DisplayNameView();
                if (displayName.empty()) continue;

                const std::string lowerName = Foundation::StringUtils::ToLowerStd(displayName);

                if (lowerName == lowerSearch) {
                    bestLiveRefID = sample.refId.raw();
                    bestLiveDist = 0;
                    break;
                }

                if (lowerName.find(lowerSearch) != std::string::npos) {
                    if (1 < bestLiveDist) {
                        bestLiveDist = 1;
                        bestLiveRefID = sample.refId.raw();
                    }
                    continue;
                }

                const int nameLen = static_cast<int>(lowerName.length());
                const int lenDiff = std::abs(searchLen - nameLen);
                if (lenDiff >= bestLiveDist) {
                    continue;
                }

                const int dist = Foundation::StringUtils::LevenshteinDistance(lowerSearch, lowerName);
                if (dist < bestLiveDist) {
                    bestLiveDist = dist;
                    bestLiveRefID = sample.refId.raw();
                }
            }
        }

        if (bestLiveRefID != 0 && bestLiveDist <= kMaxFuzzyDist) {
            const auto it = m_entriesByRef.find(bestLiveRefID);
            if (it != m_entriesByRef.end() && PassesFilter(it->second, filter)) {
                return &it->second;
            }
        }

        return nullptr;
    }

    std::optional<NPCEntry> SpatialWorldIndex::FindByName(std::string_view searchTerm, NPCFilter filter) const {
        std::shared_lock lock(m_mutex);
        if (const auto* entry = FindByNameLocked(searchTerm, filter)) {
            return *entry;
        }
        return std::nullopt;
    }

    RE::Actor* SpatialWorldIndex::FindActorByName(std::string_view searchTerm, NPCFilter filter) const {
        if (searchTerm.empty()) return nullptr;

        const std::string lowerSearch = Foundation::StringUtils::ToLowerStd(searchTerm);
        std::vector<RE::FormID> loadedExactCandidates;
        RE::FormID fallbackRefID = 0;

        {
            std::shared_lock lock(m_mutex);
            if (const auto exactIt = m_nameToRefMap.find(lowerSearch); exactIt != m_nameToRefMap.end()) {
                for (const RE::FormID refID : exactIt->second) {
                    if (!m_loadedActors.contains(refID)) {
                        continue;
                    }
                    const auto entryIt = m_entriesByRef.find(refID);
                    if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                        loadedExactCandidates.push_back(refID);
                    }
                }
            }

            if (const auto* entry = FindByNameLocked(searchTerm, filter)) {
                fallbackRefID = entry->refFormID;
            }
        }

        for (const RE::FormID refID : loadedExactCandidates) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(refID);
                actor && !actor->IsDeleted() && !actor->IsDead()) {
                return actor;
            }
        }

        if (fallbackRefID != 0) {
            return GetActorFromFormId(fallbackRefID);
        }

        return nullptr;
    }

    RE::Actor* SpatialWorldIndex::FindActorByNameNear(
        std::string_view searchTerm,
        RE::Actor* nearActor,
        bool allowSelf,
        NPCFilter filter) const {
        if (!nearActor) {
            return FindActorByName(searchTerm, filter);
        }

        const RE::FormID nearRefId = nearActor->GetFormID();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::LoadedActorSample* nearSample =
            (spatialSnap && nearRefId != 0) ? spatialSnap->Find(State::RefId{ nearRefId }) : nullptr;

        if (!nearSample && !nearActor->GetParentCell()) {
            return FindActorByName(searchTerm, filter);
        }

        if (searchTerm.empty() || !spatialSnap) return nullptr;

        const std::string lowerSearch = Foundation::StringUtils::ToLowerStd(searchTerm);
        constexpr int kMaxDist = 3;
        float nearX = 0.0f;
        float nearY = 0.0f;
        float nearZ = 0.0f;
        if (nearSample) {
            nearX = nearSample->x;
            nearY = nearSample->y;
            nearZ = nearSample->z;
        } else {
            const auto nearPos = nearActor->GetPosition();
            nearX = nearPos.x;
            nearY = nearPos.y;
            nearZ = nearPos.z;
        }

        struct Candidate {
            RE::FormID refID;
            int nameScore;
            float distSq;
        };

        std::vector<Candidate> candidates;

        for (const auto& sample : spatialSnap->AllSamples()) {
            if (sample.refId.IsNull() || sample.IsDeleted() || sample.IsDead()) continue;
            if (!allowSelf && sample.refId.raw() == nearRefId) continue;

            const auto displayName = sample.DisplayNameView();
            if (displayName.empty()) continue;

            if (filter == NPCFilter::AdultOnly && sample.IsChild()) continue;
            if (filter == NPCFilter::ChildOnly && !sample.IsChild()) continue;

            const std::string lowerName = Foundation::StringUtils::ToLowerStd(displayName);
            int nameScore = -1;

            if (lowerName == lowerSearch) {
                nameScore = 0;
            } else if (lowerName.find(lowerSearch) != std::string::npos ||
                       (lowerName.length() >= 4 && lowerSearch.find(lowerName) != std::string::npos)) {
                nameScore = 1;
            } else {
                const int dist = Foundation::StringUtils::LevenshteinDistance(lowerSearch, lowerName);
                if (dist <= kMaxDist) {
                    nameScore = 2 + dist;
                }
            }

            if (nameScore >= 0) {
                const float dx = sample.x - nearX;
                const float dy = sample.y - nearY;
                const float dz = sample.z - nearZ;
                candidates.push_back({ sample.refId.raw(), nameScore, dx * dx + dy * dy + dz * dz });
            }
        }

        if (candidates.empty()) return nullptr;

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.nameScore != b.nameScore) return a.nameScore < b.nameScore;
            if (a.distSq != b.distSq) return a.distSq < b.distSq;
            return a.refID < b.refID;
        });

        for (const auto& c : candidates) {
            if (auto* actor = GetActorFromFormId(c.refID)) {
                return actor;
            }
        }
        return nullptr;
    }

    std::optional<NPCEntry> SpatialWorldIndex::FindByFormID(RE::FormID formID) const {
        if (formID == 0 || IsSyntheticChildID(formID)) return std::nullopt;
        std::shared_lock lock(m_mutex);

        if (const auto it = m_entriesByRef.find(formID); it != m_entriesByRef.end()) {
            return it->second;
        }

        if (const auto mapIt = m_baseToRefMap.find(formID); mapIt != m_baseToRefMap.end()) {
            if (const auto refIt = m_entriesByRef.find(mapIt->second); refIt != m_entriesByRef.end()) {
                return refIt->second;
            }
        }

        return std::nullopt;
    }

    RE::FormID SpatialWorldIndex::GetRefIDLocked(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerBaseID || formID == kPlayerRefID) return kPlayerRefID;
        if (IsSyntheticChildID(formID)) return formID;

        if (const auto it = m_entriesByRef.find(formID); it != m_entriesByRef.end() && it->second.refFormID != 0) {
            return it->second.refFormID;
        }

        if (const auto mapIt = m_baseToRefMap.find(formID); mapIt != m_baseToRefMap.end() && mapIt->second != 0) {
            return mapIt->second;
        }

        return 0;
    }

    RE::FormID SpatialWorldIndex::GetRefID(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerBaseID || formID == kPlayerRefID) return kPlayerRefID;
        if (IsSyntheticChildID(formID)) return formID;

        {
            std::shared_lock lock(m_mutex);
            if (const RE::FormID mapped = GetRefIDLocked(formID); mapped != 0) {
                return mapped;
            }
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (form->As<RE::TESObjectREFR>()) {
                return formID;
            }
            if (form->As<RE::TESNPC>()) {
                return 0;
            }
        }

        return 0;
    }

    RE::FormID SpatialWorldIndex::GetBaseIDLocked(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerRefID || formID == kPlayerBaseID) return kPlayerBaseID;
        if (IsSyntheticChildID(formID)) return 0;

        if (const auto it = m_entriesByRef.find(formID); it != m_entriesByRef.end() && it->second.baseFormID != 0) {
            return it->second.baseFormID;
        }

        if (m_baseToRefMap.contains(formID)) {
            return formID;
        }

        return 0;
    }

    RE::FormID SpatialWorldIndex::GetBaseID(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerRefID || formID == kPlayerBaseID) return kPlayerBaseID;
        if (IsSyntheticChildID(formID)) return 0;

        {
            std::shared_lock lock(m_mutex);
            if (const RE::FormID mapped = GetBaseIDLocked(formID); mapped != 0) {
                return mapped;
            }
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (form->As<RE::TESNPC>()) {
                return formID;
            }
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject()) {
                    return baseObj->GetFormID();
                }
            }
        }

        return 0;
    }

    std::string SpatialWorldIndex::GetName(RE::FormID formID) const {
        if (formID == 0 || IsSyntheticChildID(formID)) return {};
        if (formID == kPlayerRefID || formID == kPlayerBaseID) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (const char* name = player->GetDisplayFullName(); name && name[0] != '\0') {
                    return name;
                }
            }
            return "Player";
        }

        {
            std::shared_lock lock(m_mutex);
            if (const auto it = m_entriesByRef.find(formID);
                it != m_entriesByRef.end() && !it->second.nameDisplay.empty()) {
                return it->second.nameDisplay;
            }
            if (const auto mapIt = m_baseToRefMap.find(formID); mapIt != m_baseToRefMap.end()) {
                if (const auto refIt = m_entriesByRef.find(mapIt->second);
                    refIt != m_entriesByRef.end() && !refIt->second.nameDisplay.empty()) {
                    return refIt->second.nameDisplay;
                }
            }
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            return GetFormDisplayName(form);
        }
        return {};
    }

    RE::Actor* SpatialWorldIndex::GetActorFromFormId(RE::FormID formID) const {
        if (formID == 0 || IsSyntheticChildID(formID)) return nullptr;

        RE::FormID mappedRefID = 0;
        bool hasLoadedRef = false;
        {
            std::shared_lock lock(m_mutex);
            hasLoadedRef = m_loadedActors.contains(formID);
            mappedRefID = GetRefIDLocked(formID);
        }

        if (hasLoadedRef) {
            if (auto* loadedActor = RE::TESForm::LookupByID<RE::Actor>(formID);
                loadedActor && !loadedActor->IsDeleted() && !loadedActor->IsDead()) {
                return loadedActor;
            }
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form && mappedRefID != 0 && mappedRefID != formID && !IsSyntheticChildID(mappedRefID)) {
            form = RE::TESForm::LookupByID(mappedRefID);
        }

        if (!form || form->IsDeleted()) return nullptr;

        if (auto* actor = form->As<RE::Actor>()) return actor;

        if (form->As<RE::TESNPC>()) {
            if (mappedRefID != 0 && mappedRefID != formID && !IsSyntheticChildID(mappedRefID)) {
                if (auto* refForm = RE::TESForm::LookupByID<RE::Actor>(mappedRefID); refForm && !refForm->IsDeleted()) {
                    return refForm;
                }
            }

            if (const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot()) {
                for (const auto& sample : spatialSnap->AllSamples()) {
                    if (sample.refId.IsNull() || sample.IsDeleted()) continue;
                    if (sample.baseId.raw() == formID) {
                        if (auto* refActor = RE::TESForm::LookupByID<RE::Actor>(sample.refId.raw());
                            refActor && !refActor->IsDeleted()) {
                            return refActor;
                        }
                    }
                }
            }
        }

        return nullptr;
    }

    std::vector<NPCEntry> SpatialWorldIndex::SearchBySubstring(
        std::string_view query,
        std::size_t maxResults,
        NPCFilter filter) const {
        std::vector<NPCEntry> results;
        if (query.empty()) return results;

        std::shared_lock lock(m_mutex);
        const std::string queryLower = Foundation::StringUtils::ToLowerStd(query);
        std::unordered_set<RE::FormID> addedForms;

        for (const auto& [refID, entry] : m_entriesByRef) {
            if (!PassesFilter(entry, filter)) continue;
            if (entry.nameLower.find(queryLower) != std::string::npos) {
                if (addedForms.insert(refID).second) {
                    results.push_back(entry);
                }
            }
        }

        std::sort(results.begin(), results.end(), [&queryLower](const NPCEntry& a, const NPCEntry& b) {
            const bool aPrefix = a.nameLower.starts_with(queryLower);
            const bool bPrefix = b.nameLower.starts_with(queryLower);
            if (aPrefix != bPrefix) return aPrefix > bPrefix;
            if (a.nameLower.length() != b.nameLower.length()) return a.nameLower.length() < b.nameLower.length();
            return a.nameLower < b.nameLower;
        });

        if (results.size() > maxResults) {
            results.resize(maxResults);
        }

        return results;
    }

    std::vector<NPCEntry> SpatialWorldIndex::GetAllEntries(NPCFilter filter) const {
        std::shared_lock lock(m_mutex);
        std::vector<NPCEntry> result;
        result.reserve(m_entriesByRef.size());

        for (const auto& [refID, entry] : m_entriesByRef) {
            (void)refID;
            if (PassesFilter(entry, filter)) {
                result.push_back(entry);
            }
        }

        return result;
    }

    nlohmann::json SpatialWorldIndex::DumpIndexJson() const {
        std::shared_lock lock(m_mutex);
        nlohmann::json root;

        const auto now = std::chrono::system_clock::now();
        const auto inTimeT = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &inTimeT);
        std::ostringstream ssTime;
        ssTime << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        const std::size_t childCount = GetChildCountLocked();
        const std::size_t adultCount =
            m_entriesByRef.size() >= childCount ? (m_entriesByRef.size() - childCount) : 0;

        nlohmann::json meta;
        meta["dumpTimestamp"] = ssTime.str();
        meta["totalNPCs"] = m_entriesByRef.size();
        meta["adultCount"] = adultCount;
        meta["childCount"] = childCount;
        meta["loadedCount"] = m_loadedActors.size();
        meta["homeCellCount"] = m_homeCells.size();
        meta["innDestinationCount"] = m_innDestinations.size();
        meta["indexBuilt"] = m_indexBuilt.load(std::memory_order_acquire);
        root["meta"] = meta;

        nlohmann::json iniOverrides;
        iniOverrides["excludeExactNames"] =
            std::vector<std::string>(m_excludeExactNames.begin(), m_excludeExactNames.end());
        iniOverrides["excludeNamePatterns"] = m_excludeNamePatterns;
        iniOverrides["excludeExactEditorIDs"] =
            std::vector<std::string>(m_excludeExactEditorIDs.begin(), m_excludeExactEditorIDs.end());
        iniOverrides["excludeEditorIDPatterns"] = m_excludeEditorIDPatterns;
        iniOverrides["excludeExactPlugins"] =
            std::vector<std::string>(m_excludeExactPlugins.begin(), m_excludeExactPlugins.end());
        iniOverrides["excludePluginPatterns"] = m_excludePluginPatterns;

        std::vector<std::string> includeFormsHex;
        includeFormsHex.reserve(m_iniIncludeForms.size());
        for (const auto f : m_iniIncludeForms) {
            includeFormsHex.push_back(fmt::format("0x{:08X}", f));
        }
        iniOverrides["iniIncludeForms"] = includeFormsHex;

        std::vector<std::string> excludeFormsHex;
        excludeFormsHex.reserve(m_iniExcludeForms.size());
        for (const auto f : m_iniExcludeForms) {
            excludeFormsHex.push_back(fmt::format("0x{:08X}", f));
        }
        iniOverrides["iniExcludeForms"] = excludeFormsHex;
        root["iniOverrides"] = iniOverrides;

        nlohmann::json baseToRefJson = nlohmann::json::object();
        for (const auto& [baseID, refID] : m_baseToRefMap) {
            baseToRefJson[fmt::format("0x{:08X}", baseID)] = fmt::format("0x{:08X}", refID);
        }
        root["baseToRefMap"] = baseToRefJson;

        std::vector<NPCEntry> sortedEntries;
        sortedEntries.reserve(m_entriesByRef.size());
        for (const auto& [_, entry] : m_entriesByRef) {
            sortedEntries.push_back(entry);
        }
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const NPCEntry& a, const NPCEntry& b) {
            const int cmp = Foundation::StringUtils::CaseInsensitiveCompare(a.nameDisplay, b.nameDisplay);
            if (cmp != 0) return cmp < 0;
            return a.baseFormID < b.baseFormID;
        });

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto& entry : sortedEntries) {
            nlohmann::json item;
            item["name"] = entry.nameDisplay;
            item["refFormId"] = fmt::format("0x{:08X}", entry.refFormID);
            item["baseFormId"] = fmt::format("0x{:08X}", entry.baseFormID);
            item["editorId"] = entry.editorID;
            item["gender"] = (entry.sex == 0 ? "male" : "female");
            item["race"] = entry.raceName;
            item["isChild"] = entry.isChild;
            item["isUnique"] = entry.isUnique;
            item["pluginName"] = entry.pluginName;
            item["currentCell"] = entry.currentCell;
            item["placementCellId"] = fmt::format("0x{:08X}", entry.placementCellID);
            item["editorLocationId"] = fmt::format("0x{:08X}", entry.editorLocationID);
            item["crimeFactionId"] = fmt::format("0x{:08X}", entry.crimeFactionID);
            item["isLoaded"] = m_loadedActors.contains(entry.refFormID);
            entriesJson.push_back(item);
        }
        root["entries"] = entriesJson;

        nlohmann::json loadedJson = nlohmann::json::array();
        for (const auto& [refID, actor] : m_loadedActors) {
            (void)refID;
            if (!actor) continue;
            nlohmann::json lActor;
            lActor["name"] = actor->GetDisplayFullName();
            lActor["refFormId"] = fmt::format("0x{:08X}", actor->GetFormID());
            if (auto* base = actor->GetActorBase()) {
                lActor["baseFormId"] = fmt::format("0x{:08X}", base->GetFormID());
            }
            if (auto* cell = actor->GetParentCell()) {
                lActor["cell"] = cell->GetName();
            }
            lActor["isDead"] = actor->IsDead();
            lActor["isDisabled"] = actor->IsDisabled();
            const auto pos = actor->GetPosition();
            lActor["position"] = { { "x", pos.x }, { "y", pos.y }, { "z", pos.z } };
            loadedJson.push_back(lActor);
        }
        root["loadedActors"] = loadedJson;

        return root;
    }

    RE::FormID SpatialWorldIndex::GetNpcHome(RE::Actor* npc) const {
        return npc ? GetNpcHome(npc->GetFormID()) : 0;
    }

    RE::FormID SpatialWorldIndex::GetNpcHome(RE::FormID actorFormID) const {
        if (actorFormID == 0) return 0;
        std::shared_lock lock(m_mutex);
        if (const auto it = m_actorsWithHome.find(actorFormID); it != m_actorsWithHome.end()) {
            return it->second.homeCell;
        }
        return 0;
    }

    RE::FormID SpatialWorldIndex::GetNpcOriginalHouseCenterMarker(RE::Actor* npc) const {
        return npc ? GetNpcOriginalHouseCenterMarker(npc->GetFormID()) : 0;
    }

    RE::FormID SpatialWorldIndex::GetNpcOriginalHouseCenterMarker(RE::FormID actorFormID) const {
        if (actorFormID == 0) return 0;

        RE::FormID cellId = 0;
        RE::FormID markerId = 0;
        {
            std::shared_lock lock(m_mutex);
            const auto homeIt = m_actorsWithHome.find(actorFormID);
            if (homeIt == m_actorsWithHome.end() || homeIt->second.homeCell == 0) {
                MARAS_LOG_DEBUG("GetNpcOriginalHouseCenterMarker: NPC {:08X} has no home cell", actorFormID);
                return 0;
            }
            cellId = homeIt->second.homeCell;

            const auto cellIt = m_homeCells.find(cellId);
            if (cellIt == m_homeCells.end()) {
                MARAS_LOG_DEBUG("GetNpcOriginalHouseCenterMarker: Cell {:08X} not found in index", cellId);
                return 0;
            }
            markerId = cellIt->second.centerMarker;
        }

        MARAS_LOG_DEBUG("GetNpcOriginalHouseCenterMarker: NPC {:08X} -> Cell {:08X} -> Marker {:08X}",
                        actorFormID, cellId, markerId);
        return markerId;
    }

    std::vector<RE::FormID> SpatialWorldIndex::GetNpcBeds(RE::Actor* npc) const {
        return npc ? GetNpcBeds(npc->GetFormID()) : std::vector<RE::FormID>{};
    }

    std::vector<RE::FormID> SpatialWorldIndex::GetNpcBeds(RE::FormID actorFormID) const {
        if (actorFormID == 0 || actorFormID == kUnknownFatherFormID || IsSyntheticChildID(actorFormID)) {
            return {};
        }

        RE::FormID indexedBaseID = 0;
        {
            std::shared_lock lock(m_mutex);
            if (const auto it = m_entriesByRef.find(actorFormID); it != m_entriesByRef.end() && it->second.baseFormID != 0) {
                indexedBaseID = it->second.baseFormID;
            } else if (m_baseToRefMap.contains(actorFormID)) {
                indexedBaseID = actorFormID;
            } else {
                indexedBaseID = GetBaseIDLocked(actorFormID);
            }
            if (indexedBaseID != 0) {
                if (const auto bedIt = m_bedsWithOwners.find(indexedBaseID); bedIt != m_bedsWithOwners.end()) {
                    return bedIt->second;
                }
                return {};
            }
        }

        RE::FormID resolvedBaseID = 0;
        if (auto* form = RE::TESForm::LookupByID(actorFormID); form && !form->IsDeleted()) {
            if (form->As<RE::TESNPC>()) {
                resolvedBaseID = form->GetFormID();
            } else if (auto* actor = form->As<RE::Actor>()) {
                if (auto* base = actor->GetActorBase(); base && !base->IsDeleted()) {
                    resolvedBaseID = base->GetFormID();
                }
            } else if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject(); baseObj && !baseObj->IsDeleted()) {
                    if (auto* npc = baseObj->As<RE::TESNPC>()) {
                        resolvedBaseID = npc->GetFormID();
                    }
                }
            }
        }

        if (resolvedBaseID == 0 && (actorFormID & 0xFF000000u) != 0xFF000000u) {
            const RE::FormID fallbackBaseID = GetBaseID(actorFormID);
            if (fallbackBaseID != 0 && fallbackBaseID != actorFormID && HasPhysicalActor(fallbackBaseID)) {
                resolvedBaseID = fallbackBaseID;
            }
        }

        if (resolvedBaseID == 0) {
            return {};
        }

        std::shared_lock lock(m_mutex);
        if (const auto it = m_bedsWithOwners.find(resolvedBaseID); it != m_bedsWithOwners.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<RE::FormID> SpatialWorldIndex::GetCellDoors(RE::FormID cellFormID) const {
        std::shared_lock lock(m_mutex);
        if (const auto it = m_homeCells.find(cellFormID); it != m_homeCells.end()) {
            return it->second.doors;
        }
        return {};
    }

    void SpatialWorldIndex::LogIndex() const {
        std::map<RE::FormID, HomeCellData> homeCellsCopy;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> actorsByCell;
        std::map<RE::FormID, std::vector<RE::FormID>> bedsWithOwnersCopy;

        {
            std::shared_lock lock(m_mutex);
            homeCellsCopy = m_homeCells;
            for (const auto& [actorId, homeData] : m_actorsWithHome) {
                actorsByCell[homeData.homeCell].push_back(actorId);
            }
            bedsWithOwnersCopy = m_bedsWithOwners;
        }

        MARAS_LOG_INFO("SpatialWorldIndex: home cell index ({} cells, {} actors, {} bed owners)",
                       homeCellsCopy.size(), actorsByCell.size(), bedsWithOwnersCopy.size());

        std::unordered_map<RE::FormID, std::vector<RE::FormID>> bedsByCell;
        for (const auto& [ownerId, bedList] : bedsWithOwnersCopy) {
            (void)ownerId;
            for (const auto bedRefId : bedList) {
                if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(bedRefId)) {
                    if (auto* parentCell = ref->GetParentCell()) {
                        bedsByCell[parentCell->GetFormID()].push_back(bedRefId);
                    }
                }
            }
        }

        for (const auto& [cellId, cellData] : homeCellsCopy) {
            auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellId);
            const std::string cellName = GetFormNameForLog(cell);
            const std::string locStr = GetLocationInfo(cell);
            const std::string doorsStr = FormatFormIDList(cellData.doors);
            const std::string actorsStr = actorsByCell.contains(cellId) ? FormatFormIDList(actorsByCell.at(cellId)) : "";
            const std::string bedsStr = bedsByCell.contains(cellId) ? FormatFormIDList(bedsByCell.at(cellId)) : "";
            const std::string markerStr = cellData.centerMarker ? FormatFormID(cellData.centerMarker) : "(none)";

            MARAS_LOG_INFO("Cell {:08X} '{}' loc={} marker={} doors=[{}] actors=[{}] beds=[{}]",
                           cellId, cellName, locStr, markerStr, doorsStr, actorsStr, bedsStr);
        }

        MARAS_LOG_INFO("SpatialWorldIndex: actor to beds mapping ({} actors with beds):", bedsWithOwnersCopy.size());
        for (const auto& [actorId, bedList] : bedsWithOwnersCopy) {
            MARAS_LOG_INFO("  Actor {:08X} has beds=[{}]", actorId, FormatFormIDList(bedList));
        }
        if (bedsWithOwnersCopy.empty()) {
            MARAS_LOG_INFO("  (no beds with owners found)");
        }
    }

    std::string SpatialWorldIndex::GetLocationInfo(const RE::TESObjectCELL* cell) const {
        if (!cell) return "(no cell)";
        const auto* loc = cell->GetLocation();
        if (!loc) return "(no location)";

        const std::string lname = GetFormNameForLog(loc);
        const char* ledid = loc->GetFormEditorID();

        std::ostringstream ss;
        ss << "name='" << lname << "' edid='" << (ledid ? ledid : "(no edid)") << "'";
        return ss.str();
    }

    std::vector<Domain::Household::TravelDestinationCandidate> SpatialWorldIndex::GetInnDestinations() const {
        std::shared_lock lock(m_mutex);
        return m_innDestinations;
    }

}  // namespace MARAS
