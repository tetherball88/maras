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
        constexpr std::string_view kOptionSelectedEvent = "SKICP_optionSelected";
        constexpr std::string_view kFmrMcmScriptName = "_JSW_BB_ConfigQuestScript";
        constexpr std::string_view kBfMcmScriptName = "FWSystemConfig";

        [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> FindQuestScriptObject(
            RE::TESQuest* quest,
            const char* scriptClassName) {
            if (!quest || !scriptClassName || !*scriptClassName) {
                return nullptr;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!vm || !policy) {
                return nullptr;
            }
            const RE::VMHandle handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == 0 || handle == policy->EmptyHandle()) {
                return nullptr;
            }
            RE::BSTSmartPointer<RE::BSScript::Object> obj;
            if (!vm->FindBoundObject(handle, scriptClassName, obj) || !obj || !obj->IsValid()) {
                return nullptr;
            }
            return obj;
        }

        [[nodiscard]] bool HasNonEmptyCurrentPage(const RE::BSScript::Object* configObj) {
            if (!configObj) {
                return false;
            }
            const auto* pageVar = configObj->GetVariable(RE::BSFixedString("_currentPage"));
            return pageVar && pageVar->IsString() && !pageVar->GetString().empty();
        }

        [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> ResolveActiveMcmConfigObject() {
            auto& cache = FormCache::GetSingleton();

            // 1. Authoritative SkyUI SKI_ConfigManager._activeConfig
            if (auto mgrObj = FindQuestScriptObject(cache.GetSkyUiConfigManagerQuest(), "SKI_ConfigManager")) {
                const auto* activeVar = mgrObj->GetVariable(RE::BSFixedString("_activeConfig"));
                if (activeVar && activeVar->IsObject() && !activeVar->IsNoneObject()) {
                    if (auto activeObj = activeVar->GetObject(); activeObj && activeObj->IsValid()) {
                        return activeObj;
                    }
                }
            }

            // 2. Fallback: inspect cached FMR / Beeing Female MCM quests directly for an open _currentPage
            if (auto fmrObj = FindQuestScriptObject(cache.GetFmrMcmQuest(), "_JSW_BB_ConfigQuestScript")) {
                if (HasNonEmptyCurrentPage(fmrObj.get())) {
                    return fmrObj;
                }
            }
            if (auto bfObj = FindQuestScriptObject(cache.GetBeeingFemaleMcmQuest(), "FWSystemConfig")) {
                if (HasNonEmptyCurrentPage(bfObj.get())) {
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
                if (!a_params.movie || a_params.argCount < 3 || !a_params.args ||
                    !a_params.args[0].IsString() || !a_params.args[2].IsNumber()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const char* rawEvent = a_params.args[0].GetString();
                if (!rawEvent || std::string_view(rawEvent) != kOptionSelectedEvent) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const std::int32_t optionIndex = static_cast<std::int32_t>(a_params.args[2].GetNumber());
                if (optionIndex < 0 || optionIndex >= 128) {
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
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto* mapVar = activeConfig->GetVariable(RE::BSFixedString("_stateOptionMap"));
                if (!mapVar || !mapVar->IsArray() || mapVar->IsNoneArray()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto stateMap = mapVar->GetArray();
                if (!stateMap || static_cast<std::uint32_t>(optionIndex) >= stateMap->size()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const auto& stateElem = (*stateMap)[static_cast<std::uint32_t>(optionIndex)];
                if (!stateElem.IsString()) {
                    ForwardOriginalSendModEvent(a_params);
                    return;
                }

                const std::string_view optionState = stateElem.GetString();
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
        if (fileUrl && Foundation::StringUtils::Contains(fileUrl, "quest_journal")) {
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

        if (skseObj.HasMember(kOrigSendModEventMember)) {
            return;
        }

        RE::GFxValue origFn;
        if (!skseObj.GetMember("SendModEvent", &origFn) || origFn.IsUndefined() || origFn.IsNull()) {
            return;
        }

        if (!skseObj.SetMember(kOrigSendModEventMember, origFn)) {
            return;
        }

        auto interceptor = RE::make_gptr<SendModEventInterceptor>();
        RE::GFxValue wrappedFn;
        movie->CreateFunction(&wrappedFn, interceptor.get());
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
