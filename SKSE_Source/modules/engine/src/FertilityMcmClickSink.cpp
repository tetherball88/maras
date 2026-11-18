#include "maras/engine/FertilityMcmClickSink.h"

#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"

#include <string>
#include <string_view>

#ifdef GetObject
#undef GetObject
#endif

namespace MARAS {

    namespace {

        constexpr const char* kOrigSendModEventMember = "__marasOrigSendModEvent";
        constexpr const char* kWrappedMarkerMember = "__marasMcmWrapped";
        constexpr std::string_view kOptionSelectedEvent = "SKICP_optionSelected";
        constexpr std::string_view kFmrMcmScriptName = "_JSW_BB_ConfigQuestScript";
        constexpr std::string_view kBfMcmScriptName = "FWSystemConfig";

        // Known variable indices inside compiled SkyUI_SE.bsa PEX files (used as VM fallback):
        //   SKI_ConfigManager: var[8] = _activeConfig : SKI_ConfigBase
        //   SKI_ConfigBase:    var[1] = _stateOptionMap : String[], var[12] = _currentPage : String
        constexpr std::int32_t kSkiConfigMgrActiveConfigIdx = 8;
        constexpr std::int32_t kSkiConfigBaseStateMapIdx = 1;
        constexpr std::int32_t kSkiConfigBaseCurrentPageIdx = 12;

        [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> FindQuestScriptObject(
            RE::TESQuest* quest,
            const char* scriptClassName) {
            if (!quest || !scriptClassName || !*scriptClassName) {
                return nullptr;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);
            auto* policy = ivm ? ivm->GetObjectHandlePolicy() : nullptr;
            if (!vm || !ivm || !policy) {
                return nullptr;
            }
            const RE::VMHandle handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == 0 || handle == policy->EmptyHandle()) {
                return nullptr;
            }

            RE::BSTSmartPointer<RE::BSScript::Object> obj;
            if (ivm->FindBoundObject(handle, scriptClassName, obj) && obj && obj->GetTypeInfo() != nullptr) {
                return obj;
            }

            // Fallback: scan attachedScripts table case-insensitively in case FindBoundObject missed
            RE::BSSpinLockGuard lk{ vm->attachedScriptsLock };
            const auto it = vm->attachedScripts.find(handle);
            if (it != vm->attachedScripts.end()) {
                for (const auto& attached : it->second) {
                    auto candidate = attached.get();
                    const auto* ti = candidate ? candidate->GetTypeInfo() : nullptr;
                    const char* candidateName = ti ? ti->GetName() : nullptr;
                    if (candidateName &&
                        Foundation::StringUtils::CaseInsensitiveEquals(candidateName, scriptClassName)) {
                        return RE::BSTSmartPointer<RE::BSScript::Object>{ candidate };
                    }
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool ReadScriptVariable(
            const RE::BSTSmartPointer<RE::BSScript::Object>& obj,
            const char* declaringClassHint,
            std::string_view varName,
            std::int32_t hintClassVarIdx,
            RE::BSScript::Variable& outVar) {
            outVar.SetNone();
            if (!obj || !obj->GetTypeInfo()) {
                return false;
            }

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);

            // 1. Case-insensitive walk across ObjectTypeInfo hierarchy (avoids BSFixedString pointer-equality pitfall)
            constexpr auto kInvalid = static_cast<std::uint32_t>(-1);
            std::uint32_t localIdx = kInvalid;
            std::uint32_t parentOffset = 0;
            for (const auto* cls = obj->GetTypeInfo(); cls; cls = cls->GetParent()) {
                if (localIdx == kInvalid) {
                    const auto* vars = cls->GetVariableIter();
                    if (vars && cls->IsLinkedValid()) {
                        const std::uint32_t count = cls->GetNumVariables();
                        for (std::uint32_t i = 0; i < count; ++i) {
                            const char* rawName = vars[i].name.c_str();
                            if (rawName && Foundation::StringUtils::CaseInsensitiveEquals(rawName, varName)) {
                                localIdx = i;
                                break;
                            }
                        }
                    }
                } else {
                    parentOffset += cls->GetNumVariables();
                }
            }

            if (localIdx != kInvalid) {
                const std::uint32_t flatIdx = parentOffset + localIdx;
                if (ivm && ivm->GetVariableValue(obj, flatIdx, outVar) &&
                    outVar.GetType().GetRawType() != RE::BSScript::TypeInfo::RawType::kNone) {
                    return true;
                }
                if (obj->IsConstructed()) {
                    outVar = obj->variables[flatIdx];
                    if (outVar.GetType().GetRawType() != RE::BSScript::TypeInfo::RawType::kNone) {
                        return true;
                    }
                }
            }

            // 2. Native VM vtable fallback by (VMHandle, declaringClassName, classRelativeVarIndex)
            if (ivm && declaringClassHint && *declaringClassHint && hintClassVarIdx >= 0) {
                const RE::VMHandle handle = obj->GetHandle();
                if (handle != 0 &&
                    ivm->GetVariableValue(handle, RE::BSFixedString(declaringClassHint), hintClassVarIdx, outVar)) {
                    return true;
                }
            }

            return localIdx != kInvalid;
        }

        [[nodiscard]] bool HasNonEmptyCurrentPage(const RE::BSTSmartPointer<RE::BSScript::Object>& configObj) {
            RE::BSScript::Variable pageVar;
            if (!ReadScriptVariable(
                    configObj,
                    "SKI_ConfigBase",
                    "_currentPage",
                    kSkiConfigBaseCurrentPageIdx,
                    pageVar)) {
                return false;
            }
            return pageVar.IsString() && !pageVar.GetString().empty();
        }

        [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> ResolveActiveMcmConfigObject() {
            auto& cache = FormCache::GetSingleton();

            // 1. Authoritative SkyUI SKI_ConfigManager._activeConfig
            if (auto mgrObj = FindQuestScriptObject(cache.GetSkyUiConfigManagerQuest(), "SKI_ConfigManager")) {
                RE::BSScript::Variable activeVar;
                if (ReadScriptVariable(
                        mgrObj,
                        "SKI_ConfigManager",
                        "_activeConfig",
                        kSkiConfigMgrActiveConfigIdx,
                        activeVar) &&
                    activeVar.IsObject() && !activeVar.IsNoneObject()) {
                    if (auto activeObj = activeVar.GetObject(); activeObj && activeObj->GetTypeInfo() != nullptr) {
                        return activeObj;
                    }
                }
            }

            // 2. Fallback: inspect cached FMR / Beeing Female MCM quests directly for an open _currentPage
            if (auto fmrObj = FindQuestScriptObject(cache.GetFmrMcmQuest(), "_JSW_BB_ConfigQuestScript")) {
                if (HasNonEmptyCurrentPage(fmrObj)) {
                    return fmrObj;
                }
            }
            if (auto bfObj = FindQuestScriptObject(cache.GetBeeingFemaleMcmQuest(), "FWSystemConfig")) {
                if (HasNonEmptyCurrentPage(bfObj)) {
                    return bfObj;
                }
            }

            return nullptr;
        }

        void ForwardOriginalSendModEvent(RE::GFxFunctionHandler::Params& a_params) {
            if (!a_params.movie) {
                return;
            }
            RE::GFxValue skseObj;
            if (a_params.movie->GetVariable(&skseObj, "_global.skse") && skseObj.IsObject()) {
                skseObj.Invoke(kOrigSendModEventMember, a_params.retVal, a_params.args, a_params.argCount);
            }
        }

        class SendModEventInterceptor final : public RE::GFxFunctionHandler {
        public:
            void Call(Params& a_params) override {
                if (!a_params.movie || a_params.argCount < 1 || !a_params.args || !a_params.args[0].IsString()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const char* rawEvent = a_params.args[0].GetString();
                if (!rawEvent || std::string_view(rawEvent) != kOptionSelectedEvent) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                if (a_params.argCount < 3 || !a_params.args[2].IsNumber()) {
                    MARAS_LOG_DEBUG(
                        "[FertilityMcmClickSink] SKICP_optionSelected had unexpected args (argCount={})",
                        a_params.argCount);
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const std::int32_t optionIndex = static_cast<std::int32_t>(a_params.args[2].GetNumber());
                if (optionIndex < 0 || optionIndex >= 128) {
                    MARAS_LOG_DEBUG(
                        "[FertilityMcmClickSink] SKICP_optionSelected optionIndex={} out of [0..127]",
                        optionIndex);
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto activeConfig = ResolveActiveMcmConfigObject();
                const auto* typeInfo = activeConfig ? activeConfig->GetTypeInfo() : nullptr;
                const char* rawScriptName = typeInfo ? typeInfo->GetName() : nullptr;
                const std::string_view scriptName =
                    rawScriptName ? std::string_view(rawScriptName) : std::string_view{};

                if (!Foundation::StringUtils::CaseInsensitiveEquals(scriptName, kFmrMcmScriptName) &&
                    !Foundation::StringUtils::CaseInsensitiveEquals(scriptName, kBfMcmScriptName)) {
                    MARAS_LOG_DEBUG(
                        "[FertilityMcmClickSink] SKICP_optionSelected optionIndex={} activeConfig='{}' (not FMR/BF)",
                        optionIndex,
                        scriptName);
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                RE::BSScript::Variable mapVar;
                if (!ReadScriptVariable(
                        activeConfig,
                        "SKI_ConfigBase",
                        "_stateOptionMap",
                        kSkiConfigBaseStateMapIdx,
                        mapVar) ||
                    !mapVar.IsArray() || mapVar.IsNoneArray()) {
                    MARAS_LOG_WARN(
                        "[FertilityMcmClickSink] Failed to read _stateOptionMap on '{}' for optionIndex={}",
                        scriptName,
                        optionIndex);
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto stateMap = mapVar.GetArray();
                if (!stateMap || static_cast<std::uint32_t>(optionIndex) >= stateMap->size()) {
                    MARAS_LOG_WARN(
                        "[FertilityMcmClickSink] _stateOptionMap size={} out of bounds for optionIndex={} on '{}'",
                        stateMap ? stateMap->size() : 0u,
                        optionIndex,
                        scriptName);
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto& stateElem = (*stateMap)[static_cast<std::uint32_t>(optionIndex)];
                const std::string_view optionState =
                    stateElem.IsString() ? stateElem.GetString() : std::string_view{};
                MARAS_LOG_DEBUG(
                    "[FertilityMcmClickSink] SKICP_optionSelected script='{}' optionIndex={} state='{}'",
                    scriptName,
                    optionIndex,
                    optionState);

                const auto blocked = Domain::Lineage::ClassifyBlockedFertilityMcmClick(scriptName, optionState);
                if (!blocked.has_value()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const Domain::Lineage::BlockedFertilityMcmClick blockedVal = *blocked;
                const std::string stateCopy(optionState);
                const char* providerLabel =
                    (blockedVal.provider == Domain::Lineage::FertilityProvider::Fmr)
                        ? "Fertility Mode Reloaded"
                        : "Beeing Female NG";

                MARAS_LOG_INFO(
                    "[FertilityMcmClickSink] Intercepted and blocked {} MCM click '{}' (optionIndex={}) — showing M.A.R.A.S. alert modal",
                    providerLabel,
                    stateCopy,
                    optionIndex);

                // Unlock SkyUI's MCM panel cleanly before closing Journal Menu so SKI_ConfigManager state stays pristine
                RE::GFxValue unlockArg(true);
                a_params.movie->Invoke("_root.ConfigPanelFader.configPanel.unlock", nullptr, &unlockArg, 1);

                // Close Journal Menu (and Pause Menu if open) so M.A.R.A.S. <ModalHost /> can claim focus immediately
                if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
                    msgQueue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen("Pause Menu")) {
                        msgQueue->AddMessage("Pause Menu", RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    }
                }

                if (!AddCurrentEpochTask(
                        "FertilityMcmClickSink::OnBlockedClick",
                        [blockedVal, stateCopy]() {
                            if (auto* hooks = Engine::GetEngineEventHooks()) {
                                hooks->OnBlockedFertilityMcmClick(blockedVal, stateCopy);
                            }
                        }) &&
                    DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                    if (auto* hooks = Engine::GetEngineEventHooks()) {
                        hooks->OnBlockedFertilityMcmClick(blockedVal, stateCopy);
                    }
                }
            }
        };

    }  // namespace

    FertilityMcmClickSink* FertilityMcmClickSink::GetSingleton() noexcept {
        static FertilityMcmClickSink instance;
        return &instance;
    }

    bool FertilityMcmClickSink::RegisterScaleformCallback(RE::GFxMovieView* a_view, RE::GFxValue* /*a_root*/) {
        if (!a_view) {
            return true;
        }
        const auto* def = a_view->GetMovieDef();
        const char* fileUrl = def ? def->GetFileURL() : nullptr;
        if (fileUrl && (Foundation::StringUtils::Contains(fileUrl, "quest_journal") ||
                        Foundation::StringUtils::Contains(fileUrl, "configpanel"))) {
            InstallOnJournalMovie(a_view);
        }
        return true;
    }

    void FertilityMcmClickSink::InstallOnJournalMovie(RE::GFxMovieView* movie) {
        if (!movie) {
            return;
        }

        RE::GFxValue skseObj;
        if (!movie->GetVariable(&skseObj, "_global.skse") || !skseObj.IsObject()) {
            return;
        }

        RE::GFxValue currentFn;
        if (!skseObj.GetMember("SendModEvent", &currentFn) || currentFn.IsUndefined() || currentFn.IsNull()) {
            return;
        }

        // Check marker on the active SendModEvent function itself so we re-wrap if SKSE overwrote it on child SWF load
        if (currentFn.IsObject() && currentFn.HasMember(kWrappedMarkerMember)) {
            return;
        }

        if (!skseObj.SetMember(kOrigSendModEventMember, currentFn)) {
            return;
        }

        auto interceptor = RE::make_gptr<SendModEventInterceptor>();
        RE::GFxValue wrappedFn;
        movie->CreateFunction(&wrappedFn, interceptor.get());
        if (wrappedFn.IsObject()) {
            wrappedFn.SetMember(kWrappedMarkerMember, true);
        }
        if (skseObj.SetMember("SendModEvent", wrappedFn)) {
            MARAS_LOG_DEBUG("[FertilityMcmClickSink] Installed MCM click interceptor on Journal Menu movie");
        }
    }

    RE::BSEventNotifyControl FertilityMcmClickSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event || !a_event->opening || a_event->menuName != RE::JournalMenu::MENU_NAME) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (auto* ui = RE::UI::GetSingleton()) {
            if (const auto menu = ui->GetMenu(RE::JournalMenu::MENU_NAME); menu && menu->uiMovie) {
                InstallOnJournalMovie(menu->uiMovie.get());
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
