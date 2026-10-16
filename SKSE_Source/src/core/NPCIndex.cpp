#include "core/NPCIndex.h"

#include "core/NPCIndexReportWriter.h"
#include "utils/ActorUtils.h"
#include "utils/FormUtils.h"
#include "utils/ProcessUtils.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

namespace MARAS {

    namespace {
        // Validate a cached Actor* pointer to ensure it hasn't unloaded, died, or been recycled.
        RE::Actor* ValidateCachedActor(RE::Actor* cached) {
            if (!cached) return nullptr;
            auto formId = cached->GetFormID();
            if (formId == 0 || IsSyntheticChildID(formId)) return nullptr;
            auto* form = RE::TESForm::LookupByID(formId);
            if (form != cached || form->IsDeleted() || cached->IsDead()) return nullptr;
            return cached;
        }

        constexpr const char* kDefaultRaceName = "Unknown";

        bool IsHumanoidActor(RE::TESNPC* base) {
            if (!base) return false;

            // Check if base has ActorTypeNPC keyword (by EditorID or FormID)
            if (base->HasKeywordString("ActorTypeNPC")) {
                return true;
            }

            // Check if race has ActorTypeNPC keyword
            auto* race = base->GetRace();
            if (race && (race->HasKeywordString("ActorTypeNPC"))) {
                return true;
            }

            return false;
        }
    }  // namespace

    bool NPCIndex::MatchesExclusionRule(std::string_view value,
                                        const std::unordered_set<std::string>& exactSet,
                                        const std::vector<std::string>& patterns) {
        if (value.empty()) return false;
        std::string lowerStr(value);
        if (exactSet.contains(lowerStr)) {
            return true;
        }
        for (const auto& pattern : patterns) {
            if (StringUtils::MatchesPattern(value, pattern)) return true;
        }
        return false;
    }

    bool NPCIndex::IsExcludedName(std::string_view lowerName) const {
        return MatchesExclusionRule(lowerName, m_excludeExactNames, m_excludeNamePatterns);
    }

    std::string NPCIndex::GetEditorIDExclusionReason(std::string_view lowerEdid) const {
        if (lowerEdid.empty()) return "";
        if (lowerEdid.find("clone") != std::string_view::npos) return "CloneEditorID";
        if (lowerEdid.find("dead") != std::string_view::npos ||
            lowerEdid.find("corpse") != std::string_view::npos) {
            return "DeadEditorID";
        }
        if (m_excludeExactEditorIDs.contains(std::string(lowerEdid))) return "INI_ExcludeExactEditorID";
        for (const auto& pattern : m_excludeEditorIDPatterns) {
            if (StringUtils::MatchesPattern(lowerEdid, pattern)) return "INI_ExcludeEditorIDPattern";
        }
        return "";
    }

    bool NPCIndex::IsExcludedEditorID(std::string_view lowerEdid) const {
        return !GetEditorIDExclusionReason(lowerEdid).empty();
    }

    bool NPCIndex::IsExcludedOriginPlugin(RE::TESForm* form) const {
        if (!form || (m_excludeExactPlugins.empty() && m_excludePluginPatterns.empty())) return false;
        const auto* file = form->GetFile(0);
        if (!file || !file->fileName || *file->fileName == '\0') return false;
        std::string fileNameLower = StringUtils::ToLowerStd(file->fileName);
        return MatchesExclusionRule(fileNameLower, m_excludeExactPlugins, m_excludePluginPatterns);
    }

    bool NPCIndex::IsValidNPCCandidate(RE::TESNPC* base, RE::TESObjectREFR* refr, std::string* outReason) const {
        auto setReason = [&](std::string_view r) {
            if (outReason) *outReason = std::string(r);
        };

        if (!base) {
            setReason("NullBase");
            return false;
        }

        RE::FormID baseID = base->GetFormID();
        RE::FormID refID = refr ? refr->GetFormID() : 0;

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
            // Exclude actors spawned from leveled character lists
            if (refr->extraList.HasType(RE::ExtraDataType::kLeveledCreature)) {
                setReason("LeveledSpawn");
                return false;
            }
            // Exclude placed references whose base object is a Leveled NPC
            auto* baseObj = refr->GetBaseObject();
            if (baseObj && baseObj->GetFormType() == RE::FormType::LeveledNPC) {
                setReason("LeveledPlacedReference");
                return false;
            }
        }

        // Explicit INI Exclude
        bool isExplicitlyExcluded = m_iniExcludeForms.contains(baseID) || (refID != 0 && m_iniExcludeForms.contains(refID));
        if (isExplicitlyExcluded) {
            setReason("INI_ExcludeFormID");
            return false;
        }

        // Must not be Ghost or Chargen Preset
        if (base->IsGhost()) {
            setReason("Ghost");
            return false;
        }
        if (base->IsPreset()) {
            setReason("ChargenPreset");
            return false;
        }

        // Humanoid (ActorTypeNPC) check
        if (!IsHumanoidActor(base)) {
            setReason("NonHumanoid");
            return false;
        }

        // Name checks
        const char* nameStr = base->GetFullName();
        if (!nameStr || nameStr[0] == '\0' || strcmp(nameStr, "None") == 0) {
            nameStr = base->GetName();
        }
        if (!nameStr || nameStr[0] == '\0' || strcmp(nameStr, "None") == 0) {
            setReason("NoName");
            return false;
        }

        std::string nameLower = StringUtils::ToLowerStd(nameStr);
        if (IsExcludedName(nameLower)) {
            std::string reason = m_excludeExactNames.contains(nameLower) ? "INI_ExcludeExactName" : "INI_ExcludeNamePattern";
            setReason(reason);
            return false;
        }

        // EditorID checks (Base and Ref)
        const char* bEdid = base->GetFormEditorID();
        std::string bEdidStr = (bEdid && bEdid[0] != '\0') ? bEdid : "";
        if (!bEdidStr.empty()) {
            std::string bEdidReason = GetEditorIDExclusionReason(StringUtils::ToLowerStd(bEdidStr));
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
                std::string rEdidReason = GetEditorIDExclusionReason(StringUtils::ToLowerStd(rEdidStr));
                if (!rEdidReason.empty()) {
                    setReason(rEdidReason);
                    return false;
                }
            }
        }

        // Plugin Origin check
        if (IsExcludedOriginPlugin(base)) {
            const auto* file = base->GetFile(0);
            std::string pluginName = (file && file->fileName) ? file->fileName : "Plugin";
            setReason("INI_ExcludePlugin(" + pluginName + ")");
            return false;
        }
        if (refr && IsExcludedOriginPlugin(refr)) {
            const auto* file = refr->GetFile(0);
            std::string pluginName = (file && file->fileName) ? file->fileName : "Plugin";
            setReason("INI_ExcludePlugin(" + pluginName + ")");
            return false;
        }

        // Explicit INI Include bypasses uniqueness/persistence check
        bool isExplicitlyIncluded = m_iniIncludeForms.contains(baseID) || (refID != 0 && m_iniIncludeForms.contains(refID));
        if (isExplicitlyIncluded) {
            return true;
        }

        // UNIQUENESS OR PERSISTENT REFERENCE CHECK:
        // 1. If base is unique -> PASS
        if (base->IsUnique()) {
            return true;
        }

        // 2. If NOT unique, it MUST have a Persistent placed reference AND an EditorID -> PASS
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

    void NPCIndex::LoadOverrides() {
        const std::string iniPath = "Data/SKSE/Plugins/MARAS/npc_index_overrides.ini";
        std::ifstream file(iniPath);
        if (!file.is_open()) {
            MARAS_LOG_INFO("NPCIndex: No overrides INI found at '{}'", iniPath);
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
        std::vector<std::string>        excludeNamePatterns;
        std::unordered_set<std::string> excludeExactEditorIDs;
        std::vector<std::string>        excludeEditorIDPatterns;
        std::unordered_set<std::string> excludeExactPlugins;
        std::vector<std::string>        excludePluginPatterns;
        std::unordered_set<RE::FormID>  iniIncludeForms;
        std::unordered_set<RE::FormID>  iniExcludeForms;

        std::string currentSection;
        std::string line;

        while (std::getline(file, line)) {
            std::string trimmed = StringUtils::TrimStd(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                continue;
            }

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = StringUtils::ToLowerStd(trimmed.substr(1, trimmed.length() - 2));
                if (currentSection != "include" && currentSection != "exclude" &&
                    currentSection != "excludenames" && currentSection != "excludeeditorids" &&
                    currentSection != "excludeplugins") {
                    MARAS_LOG_WARN("NPCIndex: Unrecognized section [{}] in overrides INI", currentSection);
                }
                continue;
            }

            // Strip inline comments if any
            auto commentPos = trimmed.find_first_of(";#");
            if (commentPos != std::string::npos) {
                trimmed = StringUtils::TrimStd(trimmed.substr(0, commentPos));
                if (trimmed.empty()) continue;
            }

            // Strip surrounding double or single quotes if present (e.g. "Alik'r Warrior")
            if (trimmed.length() >= 2 &&
                ((trimmed.front() == '"' && trimmed.back() == '"') ||
                 (trimmed.front() == '\'' && trimmed.back() == '\''))) {
                trimmed = trimmed.substr(1, trimmed.length() - 2);
            }

            if (currentSection == "excludenames") {
                std::string lowerStr = StringUtils::ToLowerStd(trimmed);
                if (lowerStr.find('*') != std::string::npos || lowerStr.find('?') != std::string::npos) {
                    excludeNamePatterns.push_back(lowerStr);
                } else {
                    excludeExactNames.insert(lowerStr);
                }
                continue;
            }

            if (currentSection == "excludeeditorids") {
                std::string lowerStr = StringUtils::ToLowerStd(trimmed);
                if (lowerStr.find('*') != std::string::npos || lowerStr.find('?') != std::string::npos) {
                    excludeEditorIDPatterns.push_back(lowerStr);
                } else {
                    excludeExactEditorIDs.insert(lowerStr);
                }
                continue;
            }

            if (currentSection == "excludeplugins") {
                std::string lowerStr = StringUtils::ToLowerStd(trimmed);
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

            // Resolve FormID for [Include] or [Exclude]
            RE::FormID resolvedFormID = 0;

            // Format 1: __formData|Plugin|0xID or PluginName.esp|0xLocalID
            if (trimmed.starts_with("__formData|")) {
                auto opt = Utils::ParseAndResolveFormKey(trimmed);
                if (opt) resolvedFormID = *opt;
            } else if (trimmed.find('|') != std::string::npos) {
                auto pipePos = trimmed.find('|');
                std::string pluginName = trimmed.substr(0, pipePos);
                std::string hexID = trimmed.substr(pipePos + 1);
                auto optHex = Utils::HexStringToFormID(hexID);
                if (optHex) {
                    if (auto* form = Utils::LookupForm<RE::TESForm>(*optHex, pluginName, "NPCIndex")) {
                        resolvedFormID = form->GetFormID();
                    }
                }
            }
            // Format 2: Direct Hex FormID (0x00012345)
            else if (trimmed.starts_with("0x") || trimmed.starts_with("0X")) {
                auto opt = Utils::HexStringToFormID(trimmed);
                if (opt) resolvedFormID = *opt;
            }
            // Format 3: EditorID
            else {
                auto* form = RE::TESForm::LookupByEditorID(trimmed);
                if (form) {
                    resolvedFormID = form->GetFormID();
                }
            }

            if (resolvedFormID != 0) {
                if (currentSection == "include") {
                    iniIncludeForms.insert(resolvedFormID);
                    MARAS_LOG_DEBUG("NPCIndex: INI include rule added for FormID 0x{:08X}", resolvedFormID);
                } else if (currentSection == "exclude") {
                    iniExcludeForms.insert(resolvedFormID);
                    MARAS_LOG_DEBUG("NPCIndex: INI exclude rule added for FormID 0x{:08X}", resolvedFormID);
                }
            } else {
                MARAS_LOG_WARN("NPCIndex: Failed to resolve INI entry '{}' in section [{}]", trimmed, currentSection);
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

        MARAS_LOG_INFO("NPCIndex: Loaded INI overrides: {} includes, {} excludes, {} exact name filters, {} name patterns, {} exact editorID filters, {} editorID patterns",
                       m_iniIncludeForms.size(), m_iniExcludeForms.size(), m_excludeExactNames.size(), m_excludeNamePatterns.size(),
                       m_excludeExactEditorIDs.size(), m_excludeEditorIDPatterns.size());
    }

    void NPCIndex::BuildIndex() {
        std::unique_lock lock(m_mutex);

        auto startTime = std::chrono::steady_clock::now();
        MARAS_LOG_INFO("NPCIndex: Building centralized NPC index...");

        m_entriesByRef.clear();
        m_baseToRefMap.clear();
        m_nameToRefMap.clear();
        m_loadedActors.clear();
        m_allNames.clear();

        LoadOverrides();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("NPCIndex: TESDataHandler is null, aborting index build");
            return;
        }

        const bool shouldDumpReport = (Settings::GetSingleton().logLevel <= 1);
        std::vector<UniqueNPCInfo> uniqueBaseNPCs;
        std::vector<ExcludedNPCInfo> excludedUnique;
        std::vector<ExcludedNPCInfo> excludedNonUnique;

        // ── PHASE 1: Scan all base NPC records (TESNPC) using authoritative IsValidNPCCandidate ──
        const auto& npcArray = dataHandler->GetFormArray<RE::TESNPC>();
        for (auto* npc : npcArray) {
            if (!npc) continue;

            RE::FormID baseID = npc->GetFormID();
            const char* edid = npc->GetFormEditorID();
            std::string edidStr = (edid && edid[0] != '\0') ? edid : "";
            const char* nameStr = npc->GetFullName();
            if (!nameStr || nameStr[0] == '\0' || strcmp(nameStr, "None") == 0) {
                nameStr = npc->GetName();
            }
            std::string nameDisplay = (nameStr && nameStr[0] != '\0' && strcmp(nameStr, "None") != 0) ? nameStr : "(No Name)";
            bool isChild = npc->race ? npc->race->data.flags.any(RE::RACE_DATA::Flag::kChild) : false;
            bool isUnique = npc->IsUnique();

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

            std::string nameLower = StringUtils::ToLowerStd(nameDisplay);
            std::string raceName = (npc->race && npc->race->GetFullName()) ? npc->race->GetFullName() : kDefaultRaceName;

            NPCEntry entry;
            entry.baseFormID = baseID;
            entry.refFormID = baseID;  // will be upgraded to placed RefID in Phase 1.5/1.6
            entry.editorID = edidStr;
            entry.nameDisplay = nameDisplay;
            entry.nameLower = nameLower;
            entry.isChild = isChild;
            entry.isUnique = isUnique;
            entry.sex = static_cast<uint8_t>(npc->GetSex());
            entry.raceName = raceName;

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

        MARAS_LOG_INFO("NPCIndex: Phase 1 complete: {} unique/included base forms indexed", m_entriesByRef.size());

        // ── Helper lambda to resolve base FormIDs -> placed Actor Ref FormIDs ──
        int persistentResolvedCount = 0;
        std::vector<UpgradedRefInfo> upgradedUniqueRefs;
        auto resolveBaseFormIDs = [&](RE::TESObjectCELL* cell) {
            if (!cell) return;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref) return RE::BSContainer::ForEachResult::kContinue;

                RE::TESNPC* base = nullptr;
                auto* actor = ref->As<RE::Actor>();
                if (actor) {
                    base = actor->GetActorBase();
                } else if (auto* baseObj = ref->GetBaseObject()) {
                    base = baseObj->As<RE::TESNPC>();
                }
                if (!base) return RE::BSContainer::ForEachResult::kContinue;

                RE::FormID baseID = base->GetFormID();
                RE::FormID refID = ref->GetFormID();

                // Run authoritative filter on placed reference
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
                        m_baseToRefMap[baseID] = refID;
                        persistentResolvedCount++;
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
                    // Non-unique NPC with a persistent placed reference that passed all other filters!
                    const char* nameStr = base->GetFullName();
                    if (!nameStr || nameStr[0] == '\0' || strcmp(nameStr, "None") == 0) {
                        nameStr = base->GetName();
                    }
                    std::string nameDisplay = (nameStr && nameStr[0] != '\0' && strcmp(nameStr, "None") != 0) ? nameStr : "(No Name)";
                    std::string nameLower = StringUtils::ToLowerStd(nameDisplay);
                    const char* edid = base->GetFormEditorID();
                    std::string edidStr = (edid && edid[0] != '\0') ? edid : "";
                    bool isChild = (base->race && base->race->data.flags.any(RE::RACE_DATA::Flag::kChild));
                    std::string raceName = (base->race && base->race->GetFullName()) ? base->race->GetFullName() : kDefaultRaceName;

                    NPCEntry entry;
                    entry.baseFormID = baseID;
                    entry.refFormID = refID;
                    entry.editorID = edidStr;
                    entry.nameDisplay = nameDisplay;
                    entry.nameLower = nameLower;
                    entry.isChild = isChild;
                    entry.isUnique = false;
                    entry.sex = static_cast<uint8_t>(base->GetSex());
                    entry.raceName = raceName;

                    const auto* originFile = base->GetFile(0);
                    entry.pluginName = (originFile && originFile->fileName) ? originFile->fileName : "";

                    m_entriesByRef[baseID] = entry;
                    m_baseToRefMap[baseID] = refID;
                    persistentResolvedCount++;
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

        // ── PHASE 1.5: Persistent cell scan across all worldspaces ──
        const auto& worldspaces = dataHandler->GetFormArray<RE::TESWorldSpace>();
        for (auto* worldspace : worldspaces) {
            if (!worldspace || !worldspace->persistentCell) continue;
            resolveBaseFormIDs(worldspace->persistentCell);
        }

        // ── PHASE 1.6: Interior cell scan ──
        for (auto* cell : dataHandler->interiorCells) {
            if (!cell) continue;
            resolveBaseFormIDs(cell);
        }

        MARAS_LOG_INFO("NPCIndex: Phase 1.5 & 1.6 complete: {} base FormIDs upgraded to placed references",
                       persistentResolvedCount);

        if (shouldDumpReport) {
            // Dump detailed promotion report to Data/SKSE/Plugins/MARAS/dumps/npc_index_promotions.txt
            NPCIndexReportWriter::DumpPromotionsReport(std::move(uniqueBaseNPCs), std::move(upgradedUniqueRefs),
                                                       std::move(excludedUnique), std::move(excludedNonUnique));
        }

        // Re-key `m_entriesByRef` by placed refFormID so lookups by Ref FormID are O(1)
        // Only keep NPCs that have valid placed references in the game world, and purge unplaced base FormIDs from m_baseToRefMap.
        std::unordered_map<RE::FormID, NPCEntry> finalEntriesByRef;
        std::unordered_map<RE::FormID, RE::FormID> finalBaseToRefMap;
        finalEntriesByRef.reserve(m_entriesByRef.size());
        finalBaseToRefMap.reserve(m_entriesByRef.size());
        m_nameToRefMap.reserve(m_entriesByRef.size());
        m_allNames.reserve(m_entriesByRef.size());

        std::unordered_set<std::string> uniqueNames;
        for (auto& [origKey, entry] : m_entriesByRef) {
            // If entry was not upgraded to a reference ID, verify if refFormID is a valid placed reference
            if (entry.refFormID == 0 || entry.refFormID == entry.baseFormID) {
                auto* form = RE::TESForm::LookupByID(entry.refFormID);
                if (!form || (form->GetFormType() != RE::FormType::ActorCharacter && form->GetFormType() != RE::FormType::Reference)) {
                    continue; // Skip unplaced base forms
                }
            }

            RE::FormID primaryKey = entry.refFormID;

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

        // ── PHASE 2: Index currently loaded actors across all 4 process list tiers ──
        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedActorLocked(actor);
            return false;
        });

        m_indexBuilt.store(true, std::memory_order_release);
        size_t childCount = GetChildCountLocked();
        size_t adultCount = m_entriesByRef.size() >= childCount ? (m_entriesByRef.size() - childCount) : 0;
        auto endTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        MARAS_LOG_INFO("NPCIndex: Build complete in {} ms — {} total indexed NPCs ({} children, {} adults, {} currently loaded)",
                       elapsedMs, m_entriesByRef.size(), childCount, adultCount, m_loadedActors.size());
    }

    void NPCIndex::IndexLoadedActorLocked(RE::Actor* actor) {
        if (!actor) return;
        auto* base = actor->GetActorBase();
        if (!base) return;

        // Run authoritative filter on loaded actor
        if (!IsValidNPCCandidate(base, actor)) {
            return;
        }

        RE::FormID refID = actor->GetFormID();
        RE::FormID baseID = base->GetFormID();
        auto displayName = actor->GetDisplayFullName();
        if (!displayName || displayName[0] == '\0') return;

        std::string lowerName = StringUtils::ToLowerStd(displayName);

        // Cache live actor pointer by Ref FormID so duplicate display names do not collide
        m_loadedActors[refID] = actor;

        // Map base to active ref ID
        m_baseToRefMap[baseID] = refID;

        // Update or insert entry
        auto it = m_entriesByRef.find(refID);
        if (it != m_entriesByRef.end()) {
            if (auto* cell = actor->GetParentCell()) {
                const char* cname = cell->GetName();
                it->second.currentCell = (cname && cname[0]) ? cname : "";
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
            entry.sex = static_cast<uint8_t>(base->GetSex());
            const auto* race = actor->GetRace();
            entry.raceName = (race && race->GetFullName()) ? race->GetFullName() : kDefaultRaceName;

            const auto* originFile = base->GetFile(0);
            entry.pluginName = (originFile && originFile->fileName) ? originFile->fileName : "";

            if (auto* cell = actor->GetParentCell()) {
                const char* cname = cell->GetName();
                entry.currentCell = (cname && cname[0]) ? cname : "";
            }

            m_entriesByRef[refID] = entry;

            // Maintain unique m_allNames and multi-value m_nameToRefMap
            auto& refList = m_nameToRefMap[lowerName];
            if (refList.empty()) {
                m_allNames.push_back(lowerName);
            }
            if (std::find(refList.begin(), refList.end(), refID) == refList.end()) {
                refList.push_back(refID);
            }

            MARAS_LOG_INFO("NPCIndex: Dynamically indexed actor '{}' (Base ID 0x{:08X} -> Ref ID 0x{:08X}) in cell '{}'",
                           entry.nameDisplay, baseID, refID, entry.currentCell);
        }
    }

    void NPCIndex::RegisterLoadedActor(RE::Actor* actor) {
        if (!actor) return;
        std::unique_lock lock(m_mutex);
        IndexLoadedActorLocked(actor);
    }

    void NPCIndex::RefreshIndex() {
        if (!m_indexBuilt.load(std::memory_order_acquire)) {
            BuildIndex();
            return;
        }

        std::unique_lock lock(m_mutex);

        // Evict stale cached Actor* pointers
        size_t evicted = 0;
        for (auto it = m_loadedActors.begin(); it != m_loadedActors.end();) {
            if (!ValidateCachedActor(it->second)) {
                it = m_loadedActors.erase(it);
                evicted++;
            } else {
                ++it;
            }
        }

        // Sweep current process lists
        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedActorLocked(actor);
            return false;
        });

        MARAS_LOG_INFO("NPCIndex: Refreshed index ({} loaded actors, {} stale evicted)",
                       m_loadedActors.size(), evicted);
    }

    void NPCIndex::ClearLoadedActors() {
        std::unique_lock lock(m_mutex);
        m_loadedActors.clear();
        MARAS_LOG_INFO("NPCIndex: Cleared cached loaded actor pointers on pre-load");
    }

    void NPCIndex::ClearIndex() {
        std::unique_lock lock(m_mutex);
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
        MARAS_LOG_INFO("NPCIndex: Index cleared");
    }

    const NPCEntry* NPCIndex::FindByNameLocked(std::string_view searchTerm, NPCFilter filter) const {
        if (searchTerm.empty()) return nullptr;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // 1. Exact match in name map (checks all mapped references)
        auto exactIt = m_nameToRefMap.find(lowerSearch);
        if (exactIt != m_nameToRefMap.end()) {
            for (RE::FormID refID : exactIt->second) {
                auto entryIt = m_entriesByRef.find(refID);
                if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                    return &entryIt->second;
                }
            }
        }

        // 2. Fuzzy match with Levenshtein distance (threshold: 3)
        constexpr int kMaxFuzzyDist = 3;
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames, kMaxFuzzyDist);
        if (fuzzy) {
            auto nameIt = m_nameToRefMap.find(fuzzy.match);
            if (nameIt != m_nameToRefMap.end()) {
                for (RE::FormID refID : nameIt->second) {
                    auto entryIt = m_entriesByRef.find(refID);
                    if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                        return &entryIt->second;
                    }
                }
            }
        }

        // 3. Deterministic forward-substring match (prefer prefix match, then shorter name, then alphabetical)
        const NPCEntry* bestSubstringEntry = nullptr;
        for (const auto& [refID, entry] : m_entriesByRef) {
            if (!PassesFilter(entry, filter)) continue;
            if (entry.nameLower.find(lowerSearch) != std::string::npos) {
                if (!bestSubstringEntry) {
                    bestSubstringEntry = &entry;
                } else {
                    bool candPrefix = entry.nameLower.starts_with(lowerSearch);
                    bool bestPrefix = bestSubstringEntry->nameLower.starts_with(lowerSearch);
                    if (candPrefix != bestPrefix) {
                        if (candPrefix) bestSubstringEntry = &entry;
                    } else if (entry.nameLower.length() != bestSubstringEntry->nameLower.length()) {
                        if (entry.nameLower.length() < bestSubstringEntry->nameLower.length()) bestSubstringEntry = &entry;
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

        // 4. Live scan fallback across loaded process lists
        RE::Actor* bestLiveMatch = nullptr;
        int bestLiveDist = kMaxFuzzyDist + 1;
        const int searchLen = static_cast<int>(lowerSearch.length());

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor->IsDeleted() || actor->IsDead()) return false;
            auto displayName = actor->GetDisplayFullName();
            if (!displayName || displayName[0] == '\0') return false;

            std::string lowerName = StringUtils::ToLowerStd(displayName);

            // Exact match early exit
            if (lowerName == lowerSearch) {
                bestLiveMatch = actor;
                bestLiveDist = 0;
                return true;
            }

            // Substring fast path
            if (lowerName.find(lowerSearch) != std::string::npos) {
                if (1 < bestLiveDist) {
                    bestLiveDist = 1;
                    bestLiveMatch = actor;
                }
                return false;
            }

            // Length difference filter: skip Levenshtein if diff >= bestLiveDist
            const int nameLen = static_cast<int>(lowerName.length());
            const int lenDiff = std::abs(searchLen - nameLen);
            if (lenDiff >= bestLiveDist) {
                return false;
            }

            int dist = StringUtils::LevenshteinDistance(lowerSearch, lowerName);
            if (dist < bestLiveDist) {
                bestLiveDist = dist;
                bestLiveMatch = actor;
            }

            return false;
        });

        if (bestLiveMatch && bestLiveDist <= kMaxFuzzyDist) {
            auto it = m_entriesByRef.find(bestLiveMatch->GetFormID());
            if (it != m_entriesByRef.end() && PassesFilter(it->second, filter)) {
                return &it->second;
            }
        }

        return nullptr;
    }

    std::optional<NPCEntry> NPCIndex::FindByName(std::string_view searchTerm, NPCFilter filter) {
        std::shared_lock lock(m_mutex);
        if (const auto* entry = FindByNameLocked(searchTerm, filter)) {
            return *entry;
        }
        return std::nullopt;
    }

    RE::Actor* NPCIndex::FindActorByName(std::string_view searchTerm, NPCFilter filter) {
        std::shared_lock lock(m_mutex);
        if (searchTerm.empty()) return nullptr;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // 1. Check loaded actors cache via exact name map first (strictly require valid entry passing filter)
        auto exactIt = m_nameToRefMap.find(lowerSearch);
        if (exactIt != m_nameToRefMap.end()) {
            for (RE::FormID refID : exactIt->second) {
                auto loadedIt = m_loadedActors.find(refID);
                if (loadedIt != m_loadedActors.end()) {
                    if (auto* valid = ValidateCachedActor(loadedIt->second)) {
                        auto entryIt = m_entriesByRef.find(refID);
                        if (entryIt != m_entriesByRef.end() && PassesFilter(entryIt->second, filter)) {
                            return valid;
                        }
                    }
                }
            }
        }

        // 2. Find entry via FindByNameLocked under the same shared lock (no TOCTOU race)
        const auto* entry = FindByNameLocked(searchTerm, filter);
        if (entry) {
            return GetActorFromFormIdLocked(entry->refFormID);
        }

        return nullptr;
    }

    RE::Actor* NPCIndex::FindActorByNameNear(std::string_view searchTerm, RE::Actor* nearActor,
                                            bool allowSelf, NPCFilter filter) {
        if (!nearActor || !nearActor->GetParentCell()) {
            return FindActorByName(searchTerm, filter);
        }

        std::shared_lock lock(m_mutex);
        if (searchTerm.empty()) return nullptr;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);
        constexpr int kMaxDist = 3;
        auto nearPos = nearActor->GetPosition();

        struct Candidate {
            RE::Actor* actor;
            int nameScore;  // 0=exact, 1=substring, 2+=fuzzy
            float distSq;
        };

        std::vector<Candidate> candidates;

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor->IsDeleted() || actor->IsDead()) return false;
            if (!allowSelf && actor == nearActor) return false;

            auto displayName = actor->GetDisplayFullName();
            if (!displayName || displayName[0] == '\0') return false;

            // Apply filter
            if (filter == NPCFilter::AdultOnly && actor->IsChild()) return false;
            if (filter == NPCFilter::ChildOnly && !actor->IsChild()) return false;

            std::string lowerName = StringUtils::ToLowerStd(displayName);
            int nameScore = -1;

            if (lowerName == lowerSearch) {
                nameScore = 0;
            } else if (lowerName.find(lowerSearch) != std::string::npos ||
                       (lowerName.length() >= 4 && lowerSearch.find(lowerName) != std::string::npos)) {
                nameScore = 1;
            } else {
                int dist = StringUtils::LevenshteinDistance(lowerSearch, lowerName);
                if (dist <= kMaxDist) {
                    nameScore = 2 + dist;
                }
            }

            if (nameScore >= 0) {
                auto pos = actor->GetPosition();
                float dx = pos.x - nearPos.x;
                float dy = pos.y - nearPos.y;
                float dz = pos.z - nearPos.z;
                candidates.push_back({ actor, nameScore, dx * dx + dy * dy + dz * dz });
            }

            return false;
        });

        if (candidates.empty()) return nullptr;

        auto* best = &candidates[0];
        for (size_t i = 1; i < candidates.size(); ++i) {
            auto& c = candidates[i];
            if (c.nameScore < best->nameScore ||
                (c.nameScore == best->nameScore && c.distSq < best->distSq) ||
                (c.nameScore == best->nameScore && c.distSq == best->distSq && c.actor->GetFormID() < best->actor->GetFormID())) {
                best = &c;
            }
        }

        return best->actor;
    }

    std::optional<NPCEntry> NPCIndex::FindByFormID(RE::FormID formID) {
        if (formID == 0 || IsSyntheticChildID(formID)) return std::nullopt;
        std::shared_lock lock(m_mutex);

        // Try direct lookup by ref FormID
        auto it = m_entriesByRef.find(formID);
        if (it != m_entriesByRef.end()) {
            return it->second;
        }

        // Try mapping base FormID to ref FormID
        auto mapIt = m_baseToRefMap.find(formID);
        if (mapIt != m_baseToRefMap.end()) {
            auto refIt = m_entriesByRef.find(mapIt->second);
            if (refIt != m_entriesByRef.end()) {
                return refIt->second;
            }
        }

        return std::nullopt;
    }

    RE::FormID NPCIndex::GetRefIDLocked(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerBaseID || formID == kPlayerRefID) return kPlayerRefID;
        if (IsSyntheticChildID(formID)) return formID;

        auto it = m_entriesByRef.find(formID);
        if (it != m_entriesByRef.end() && it->second.refFormID != 0) {
            return it->second.refFormID;
        }

        auto mapIt = m_baseToRefMap.find(formID);
        if (mapIt != m_baseToRefMap.end() && mapIt->second != 0) {
            return mapIt->second;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (form) {
            if (form->As<RE::TESObjectREFR>()) {
                return formID;
            }

            if (form->As<RE::TESNPC>()) {
                return 0;
            }
        }

        return 0;
    }

    RE::FormID NPCIndex::GetRefID(RE::FormID formID) {
        if (formID == 0) return 0;
        std::shared_lock lock(m_mutex);
        return GetRefIDLocked(formID);
    }

    RE::FormID NPCIndex::GetBaseIDLocked(RE::FormID formID) const {
        if (formID == 0) return 0;
        if (formID == kPlayerRefID || formID == kPlayerBaseID) return kPlayerBaseID;
        if (IsSyntheticChildID(formID)) return 0;

        auto it = m_entriesByRef.find(formID);
        if (it != m_entriesByRef.end() && it->second.baseFormID != 0) {
            return it->second.baseFormID;
        }

        auto mapIt = m_baseToRefMap.find(formID);
        if (mapIt != m_baseToRefMap.end()) {
            return formID;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (form) {
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

    RE::FormID NPCIndex::GetBaseID(RE::FormID formID) {
        if (formID == 0) return 0;
        std::shared_lock lock(m_mutex);
        return GetBaseIDLocked(formID);
    }

    RE::Actor* NPCIndex::GetActorFromFormIdLocked(RE::FormID formID) const {
        if (formID == 0 || IsSyntheticChildID(formID)) return nullptr;

        auto loadedIt = m_loadedActors.find(formID);
        if (loadedIt != m_loadedActors.end()) {
            if (auto* valid = ValidateCachedActor(loadedIt->second)) {
                return valid;
            }
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
            RE::FormID mappedRefID = GetRefIDLocked(formID);
            if (mappedRefID != 0 && mappedRefID != formID && !IsSyntheticChildID(mappedRefID)) {
                form = RE::TESForm::LookupByID(mappedRefID);
            }
        }

        if (!form || form->IsDeleted()) return nullptr;

        auto* actor = form->As<RE::Actor>();
        if (actor) return actor;

        // If form is a base TESNPC, check mapped ref ID first, then search loaded process lists for an active reference
        auto* npc = form->As<RE::TESNPC>();
        if (npc) {
            RE::FormID mappedRefID = GetRefIDLocked(formID);
            if (mappedRefID != 0 && mappedRefID != formID && !IsSyntheticChildID(mappedRefID)) {
                if (auto* refForm = RE::TESForm::LookupByID<RE::Actor>(mappedRefID)) {
                    if (!refForm->IsDeleted()) return refForm;
                }
            }

            RE::Actor* found = nullptr;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* a) {
                if (!a || a->IsDeleted()) return false;
                auto* base = a->GetActorBase();
                if (base && base->GetFormID() == formID) {
                    found = a;
                    return true;
                }
                return false;
            });
            if (found) return found;
        }

        return nullptr;
    }

    RE::Actor* NPCIndex::GetActorFromFormId(RE::FormID formID) {
        if (formID == 0) return nullptr;
        std::shared_lock lock(m_mutex);
        return GetActorFromFormIdLocked(formID);
    }

    std::vector<NPCEntry> NPCIndex::SearchBySubstring(std::string_view query, size_t maxResults,
                                                     NPCFilter filter) {
        std::vector<NPCEntry> results;
        if (query.empty()) return results;

        std::shared_lock lock(m_mutex);
        std::string queryLower = StringUtils::ToLowerStd(query);

        std::unordered_set<RE::FormID> addedForms;

        for (const auto& [refID, entry] : m_entriesByRef) {
            if (!PassesFilter(entry, filter)) continue;

            if (entry.nameLower.find(queryLower) != std::string::npos) {
                if (addedForms.insert(refID).second) {
                    results.push_back(entry);
                }
            }
        }

        // Sort results by relevance: exact prefix matches first, then shorter names, then alphabetical
        std::sort(results.begin(), results.end(), [&queryLower](const NPCEntry& a, const NPCEntry& b) {
            bool aPrefix = a.nameLower.starts_with(queryLower);
            bool bPrefix = b.nameLower.starts_with(queryLower);
            if (aPrefix != bPrefix) return aPrefix > bPrefix;
            if (a.nameLower.length() != b.nameLower.length()) return a.nameLower.length() < b.nameLower.length();
            return a.nameLower < b.nameLower;
        });

        if (results.size() > maxResults) {
            results.resize(maxResults);
        }

        return results;
    }

    std::vector<NPCEntry> NPCIndex::GetAllEntries(NPCFilter filter) {
        std::shared_lock lock(m_mutex);
        std::vector<NPCEntry> result;
        result.reserve(m_entriesByRef.size());

        for (const auto& [refID, entry] : m_entriesByRef) {
            if (PassesFilter(entry, filter)) {
                result.push_back(entry);
            }
        }

        return result;
    }

    nlohmann::json NPCIndex::DumpIndexJson() const {
        std::shared_lock lock(m_mutex);
        nlohmann::json root;

        // Metadata & statistics
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &in_time_t);
        std::ostringstream ssTime;
        ssTime << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        size_t childCount = GetChildCountLocked();
        size_t adultCount = m_entriesByRef.size() >= childCount ? (m_entriesByRef.size() - childCount) : 0;

        nlohmann::json meta;
        meta["dumpTimestamp"] = ssTime.str();
        meta["totalNPCs"] = m_entriesByRef.size();
        meta["adultCount"] = adultCount;
        meta["childCount"] = childCount;
        meta["loadedCount"] = m_loadedActors.size();
        meta["indexBuilt"] = m_indexBuilt.load(std::memory_order_acquire);
        root["meta"] = meta;

        // INI Overrides & filter lists
        nlohmann::json iniOverrides;
        iniOverrides["excludeExactNames"] = std::vector<std::string>(m_excludeExactNames.begin(), m_excludeExactNames.end());
        iniOverrides["excludeNamePatterns"] = m_excludeNamePatterns;
        iniOverrides["excludeExactEditorIDs"] = std::vector<std::string>(m_excludeExactEditorIDs.begin(), m_excludeExactEditorIDs.end());
        iniOverrides["excludeEditorIDPatterns"] = m_excludeEditorIDPatterns;
        iniOverrides["excludeExactPlugins"] = std::vector<std::string>(m_excludeExactPlugins.begin(), m_excludeExactPlugins.end());
        iniOverrides["excludePluginPatterns"] = m_excludePluginPatterns;

        std::vector<std::string> includeFormsHex;
        includeFormsHex.reserve(m_iniIncludeForms.size());
        for (auto f : m_iniIncludeForms) {
            includeFormsHex.push_back(std::format("0x{:08X}", f));
        }
        iniOverrides["iniIncludeForms"] = includeFormsHex;

        std::vector<std::string> excludeFormsHex;
        excludeFormsHex.reserve(m_iniExcludeForms.size());
        for (auto f : m_iniExcludeForms) {
            excludeFormsHex.push_back(std::format("0x{:08X}", f));
        }
        iniOverrides["iniExcludeForms"] = excludeFormsHex;
        root["iniOverrides"] = iniOverrides;

        // Base to Ref Map
        nlohmann::json baseToRefJson = nlohmann::json::object();
        for (const auto& [baseID, refID] : m_baseToRefMap) {
            baseToRefJson[std::format("0x{:08X}", baseID)] = std::format("0x{:08X}", refID);
        }
        root["baseToRefMap"] = baseToRefJson;

        // Indexed NPC entries (sorted by display name)
        std::vector<NPCEntry> sortedEntries;
        sortedEntries.reserve(m_entriesByRef.size());
        for (const auto& [_, entry] : m_entriesByRef) {
            sortedEntries.push_back(entry);
        }
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const NPCEntry& a, const NPCEntry& b) {
            int cmp = StringUtils::CaseInsensitiveCompare(a.nameDisplay, b.nameDisplay);
            if (cmp != 0) return cmp < 0;
            return a.baseFormID < b.baseFormID;
        });

        nlohmann::json entriesJson = nlohmann::json::array();
        for (const auto& entry : sortedEntries) {
            nlohmann::json item;
            item["name"] = entry.nameDisplay;
            item["refFormId"] = std::format("0x{:08X}", entry.refFormID);
            item["baseFormId"] = std::format("0x{:08X}", entry.baseFormID);
            item["editorId"] = entry.editorID;
            item["gender"] = (entry.sex == 0 ? "male" : "female");
            item["race"] = entry.raceName;
            item["isChild"] = entry.isChild;
            item["isUnique"] = entry.isUnique;
            item["pluginName"] = entry.pluginName;
            item["currentCell"] = entry.currentCell;
            item["isLoaded"] = m_loadedActors.contains(entry.refFormID);
            entriesJson.push_back(item);
        }
        root["entries"] = entriesJson;

        // Loaded actors list
        nlohmann::json loadedJson = nlohmann::json::array();
        for (const auto& [refID, actor] : m_loadedActors) {
            if (!actor) continue;
            nlohmann::json lActor;
            lActor["name"] = actor->GetDisplayFullName();
            lActor["refFormId"] = std::format("0x{:08X}", actor->GetFormID());
            if (auto* base = actor->GetActorBase()) {
                lActor["baseFormId"] = std::format("0x{:08X}", base->GetFormID());
            }
            if (auto* cell = actor->GetParentCell()) {
                lActor["cell"] = cell->GetName();
            }
            lActor["isDead"] = actor->IsDead();
            lActor["isDisabled"] = actor->IsDisabled();
            auto pos = actor->GetPosition();
            lActor["position"] = { {"x", pos.x}, {"y", pos.y}, {"z", pos.z} };
            loadedJson.push_back(lActor);
        }
        root["loadedActors"] = loadedJson;

        return root;
    }

}  // namespace MARAS
