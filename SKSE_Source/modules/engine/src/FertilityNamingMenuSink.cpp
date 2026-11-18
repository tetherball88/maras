#include "maras/engine/FertilityNamingMenuSink.h"

#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef GetObject
#undef GetObject
#endif

namespace MARAS {

    namespace {

        constexpr std::string_view kCustomMenuName = "CustomMenu";
        constexpr std::string_view kFmrUtilityScript = "_JSW_BB_Utility";
        constexpr std::string_view kFmrUiExtBridgeScript = "_JSW_BB_UIExtensionsBridge";
        constexpr std::string_view kFmrGenerateNameFn = "GenerateName";
        constexpr std::string_view kFmrShowVrMenuFn = "ShowVRNameMenuSimple";
        constexpr std::string_view kFmrHandlerAliasScript = "_JSW_BB_HandlerQuestAliasScript";
        constexpr std::string_view kFmrConfigScript = "_JSW_BB_ConfigQuestScript";
        constexpr std::string_view kUiLibScript = "UILIB_1";
        constexpr std::string_view kUiLibShowTextInputFn = "ShowTextInput";

        // Known flat variable indices in UIMenuBase / UIListMenu / UILIB_1 PEX layouts:
        //   UIMenuBase (2 vars): [0]=_waitLock, [1]=::isResetting_var
        //   UIListMenu (12 vars, parentOffset=2): [4]=_resultString, [7]=_entryBuffer, [8]=_resultInt, [12]=_resultFloat
        //   UILIB_1 (8 vars, parentOffset=0): [4]=bMenuOpen, [5]=sInput
        constexpr std::uint32_t kUiMenuBaseWaitLockFlatIdx = 0;
        constexpr std::uint32_t kUiListMenuResultStringFlatIdx = 4;
        constexpr std::uint32_t kUiListMenuEntryBufferFlatIdx = 7;
        constexpr std::uint32_t kUiListMenuResultIntFlatIdx = 8;
        constexpr std::uint32_t kUiListMenuResultFloatFlatIdx = 12;
        constexpr std::uint32_t kUiLibMenuOpenFlatIdx = 4;
        constexpr std::uint32_t kUiLibInputFlatIdx = 5;

        enum class NamingInterceptMode : std::uint8_t {
            UiListMenu = 0,
            UiLibTextInput = 1
        };

        struct DetectedFmrNamingStack {
            RE::VMStackID stackId{ 0 };
            NamingInterceptMode mode{ NamingInterceptMode::UiListMenu };
            RE::FormID motherId{ 0 };
            bool isFemale{ false };
            bool cameFromMcmForceGrowth{ false };
        };

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

            RE::BSSpinLockGuard lk{ vm->attachedScriptsLock };
            const auto it = vm->attachedScripts.find(handle);
            if (it != vm->attachedScripts.end()) {
                for (const auto& attached : it->second) {
                    auto* candidate = attached.get();
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

        [[nodiscard]] std::optional<std::uint32_t> ResolveFlatVariableIndex(
            const RE::BSScript::Object* obj,
            std::string_view varName) {
            if (!obj || !obj->GetTypeInfo()) {
                return std::nullopt;
            }
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
            if (localIdx == kInvalid) {
                return std::nullopt;
            }
            return parentOffset + localIdx;
        }

        [[nodiscard]] RE::BSScript::Variable* GetWritableObjectVariable(
            RE::BSScript::Object* obj,
            std::string_view varName,
            std::uint32_t fallbackFlatIdx) {
            if (!obj || !obj->IsConstructed() || !obj->GetTypeInfo()) {
                return nullptr;
            }
            const std::uint32_t totalVars = obj->GetTypeInfo()->GetVariableCount();
            if (const auto resolved = ResolveFlatVariableIndex(obj, varName);
                resolved.has_value() && *resolved < totalVars) {
                return std::addressof(obj->variables[*resolved]);
            }
            if (fallbackFlatIdx < totalVars) {
                return std::addressof(obj->variables[fallbackFlatIdx]);
            }
            return nullptr;
        }

        [[nodiscard]] RE::BSScript::Variable* FindFrameVariable(
            RE::BSScript::Stack* stack,
            RE::BSScript::StackFrame* frame,
            std::string_view targetName) {
            if (!stack || !frame || !frame->owningFunction) {
                return nullptr;
            }
            const std::uint32_t pageHint = stack->GetPageForFrame(frame);
            if (pageHint == static_cast<std::uint32_t>(-1)) {
                return nullptr;
            }
            const auto& fn = frame->owningFunction;
            const std::uint32_t frameSize = fn->GetStackFrameSize();
            for (std::uint32_t idx = 0; idx < frameSize; ++idx) {
                RE::BSFixedString slotName;
                if (fn->GetVarNameForStackIndex(idx, slotName) && slotName.c_str() != nullptr &&
                    Foundation::StringUtils::CaseInsensitiveEquals(slotName.c_str(), targetName)) {
                    return std::addressof(stack->GetStackFrameVariable(frame, idx, pageHint));
                }
            }
            return nullptr;
        }

        [[nodiscard]] RE::FormID ResolveActorFormIdFromVariable(const RE::BSScript::Variable* var) {
            if (!var || !var->IsObject() || var->IsNoneObject()) {
                return 0;
            }
            const auto obj = var->GetObject();
            if (!obj) {
                return 0;
            }
            auto* actor = static_cast<RE::Actor*>(
                obj->Resolve(static_cast<RE::VMTypeID>(RE::Actor::FORMTYPE)));
            return actor ? actor->GetFormID() : 0;
        }

        [[nodiscard]] std::optional<DetectedFmrNamingStack> FindActiveFmrNamingStack() {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                return std::nullopt;
            }

            RE::BSSpinLockGuard lk{ vm->runningStacksLock };
            for (auto& [stackId, stack] : vm->allRunningStacks) {
                if (!stack || !stack->top) {
                    continue;
                }

                RE::BSScript::StackFrame* generateFrame = nullptr;
                RE::BSScript::StackFrame* vrMenuFrame = nullptr;
                bool hasUiLibFrame = false;

                for (auto* frame = stack->top; frame != nullptr; frame = frame->previousFrame) {
                    const auto& fn = frame->owningFunction;
                    if (!fn) {
                        continue;
                    }
                    const char* objName = fn->GetObjectTypeName().c_str();
                    const char* fnName = fn->GetName().c_str();
                    if (!objName || !fnName) {
                        continue;
                    }

                    if (Foundation::StringUtils::CaseInsensitiveEquals(objName, kFmrUtilityScript)) {
                        if (Foundation::StringUtils::CaseInsensitiveEquals(fnName, kFmrGenerateNameFn)) {
                            generateFrame = frame;
                        } else if (Foundation::StringUtils::CaseInsensitiveEquals(fnName, kFmrShowVrMenuFn)) {
                            vrMenuFrame = frame;
                        }
                    } else if (Foundation::StringUtils::CaseInsensitiveEquals(objName, kUiLibScript) &&
                               Foundation::StringUtils::CaseInsensitiveEquals(fnName, kUiLibShowTextInputFn)) {
                        hasUiLibFrame = true;
                    }
                }

                if (!generateFrame) {
                    continue;
                }

                const auto* msgVar = FindFrameVariable(stack.get(), generateFrame, "msg");
                const std::string_view promptText =
                    (msgVar && msgVar->IsString()) ? msgVar->GetString() : std::string_view{};
                const auto promptInfo = Domain::Lineage::ClassifyFertilityChildNamingPrompt(promptText);
                if (!promptInfo.isChildNamingPrompt) {
                    continue;
                }

                DetectedFmrNamingStack out{};
                out.stackId = stackId;
                out.mode = hasUiLibFrame && !vrMenuFrame
                               ? NamingInterceptMode::UiLibTextInput
                               : NamingInterceptMode::UiListMenu;
                out.isFemale = promptInfo.isFemale;

                if (vrMenuFrame) {
                    if (const auto* femVar = FindFrameVariable(stack.get(), vrMenuFrame, "isFemale");
                        femVar && femVar->IsBool()) {
                        out.isFemale = femVar->GetBool();
                    }
                }

                if (auto* callerFrame = generateFrame->previousFrame; callerFrame && callerFrame->owningFunction) {
                    const char* callerObj = callerFrame->owningFunction->GetObjectTypeName().c_str();
                    const char* callerState = callerFrame->owningFunction->GetStateName().c_str();
                    if (callerObj) {
                        if (Foundation::StringUtils::CaseInsensitiveEquals(callerObj, kFmrHandlerAliasScript)) {
                            out.motherId = ResolveActorFormIdFromVariable(
                                FindFrameVariable(stack.get(), callerFrame, "akActor"));
                        } else if (Foundation::StringUtils::CaseInsensitiveEquals(callerObj, kFmrConfigScript)) {
                            if (callerState &&
                                Foundation::StringUtils::CaseInsensitiveEquals(callerState, "PLAYER_GROWTH")) {
                                out.motherId = 0x14;
                                out.cameFromMcmForceGrowth = true;
                            } else if (callerState &&
                                       Foundation::StringUtils::CaseInsensitiveEquals(callerState, "NPC_GROWTH")) {
                                out.motherId = ResolveActorFormIdFromVariable(
                                    FindFrameVariable(stack.get(), callerFrame, "kActor"));
                                out.cameFromMcmForceGrowth = true;
                            }
                        }
                    }
                }

                return out;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::vector<std::string> ReadFmrExistingChildNames() {
            std::vector<std::string> names;
            auto* handlerQuest = FormCache::GetSingleton().GetFmrHandlerQuest();
            auto storageObj = FindQuestScriptObject(handlerQuest, "_JSW_BB_Storage");
            if (!storageObj || !storageObj->IsConstructed() || !storageObj->GetTypeInfo()) {
                return names;
            }

            const auto idxOpt = ResolveFlatVariableIndex(storageObj.get(), "PlayerChildName");
            if (!idxOpt.has_value() || *idxOpt >= storageObj->GetTypeInfo()->GetVariableCount()) {
                return names;
            }

            const auto& arrVar = storageObj->variables[*idxOpt];
            if (!arrVar.IsArray() || arrVar.IsNoneArray()) {
                return names;
            }

            const auto arr = arrVar.GetArray();
            if (!arr) {
                return names;
            }

            names.reserve(arr->size());
            for (std::uint32_t i = 0; i < arr->size(); ++i) {
                const auto& elem = (*arr)[i];
                if (elem.IsString() && !elem.GetString().empty()) {
                    names.emplace_back(elem.GetString());
                }
            }
            return names;
        }

        void UpdateLatestFmrChildNameFallback(const std::string& chosenName) {
            auto* handlerQuest = FormCache::GetSingleton().GetFmrHandlerQuest();
            auto storageObj = FindQuestScriptObject(handlerQuest, "_JSW_BB_Storage");
            if (!storageObj || !storageObj->IsConstructed() || !storageObj->GetTypeInfo()) {
                return;
            }
            const auto idxOpt = ResolveFlatVariableIndex(storageObj.get(), "PlayerChildName");
            if (!idxOpt.has_value() || *idxOpt >= storageObj->GetTypeInfo()->GetVariableCount()) {
                return;
            }
            auto& arrVar = storageObj->variables[*idxOpt];
            if (!arrVar.IsArray() || arrVar.IsNoneArray()) {
                return;
            }
            if (auto arr = arrVar.GetArray(); arr && arr->size() > 0) {
                (*arr)[arr->size() - 1].SetString(chosenName);
            }
        }

        void SuppressUiListMenuLoadSpam() {
            auto* listQuest = FormCache::GetSingleton().GetUiExtListMenuQuest();
            auto listObj = FindQuestScriptObject(listQuest, "UIListMenu");
            if (!listObj) {
                return;
            }
            if (auto* bufVar = GetWritableObjectVariable(
                    listObj.get(), "_entryBuffer", kUiListMenuEntryBufferFlatIdx)) {
                bufVar->SetSInt(0);
            }
        }

        void SendSkseModEvent(const char* eventName, const char* strArg, float numArg) {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent ev{
                    RE::BSFixedString(eventName),
                    RE::BSFixedString(strArg ? strArg : ""),
                    numArg,
                    nullptr
                };
                source->SendEvent(&ev);
            }
        }

        void CompleteFmrNamingIntercept(
            RE::VMStackID stackId,
            NamingInterceptMode mode,
            const std::string& chosenName) {
            if (mode == NamingInterceptMode::UiLibTextInput) {
                if (auto uiLibObj = FindQuestScriptObject(
                        FormCache::GetSingleton().GetFmrHandlerQuest(), "UILIB_1")) {
                    if (auto* inputVar = GetWritableObjectVariable(
                            uiLibObj.get(), "sInput", kUiLibInputFlatIdx)) {
                        inputVar->SetString(chosenName);
                    }
                    if (auto* openVar = GetWritableObjectVariable(
                            uiLibObj.get(), "bMenuOpen", kUiLibMenuOpenFlatIdx)) {
                        openVar->SetBool(false);
                    }
                }
                SendSkseModEvent("UILIB_1_textInputClose", chosenName.c_str(), 0.0f);
                return;
            }

            std::uint32_t patchedArrays = 0;
            if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
                RE::BSSpinLockGuard lk{ vm->runningStacksLock };
                const auto it = vm->allRunningStacks.find(stackId);
                if (it != vm->allRunningStacks.end() && it->second && it->second->top) {
                    auto* stack = it->second.get();
                    for (auto* frame = stack->top; frame != nullptr; frame = frame->previousFrame) {
                        const auto& fn = frame->owningFunction;
                        if (!fn) {
                            continue;
                        }
                        const char* objName = fn->GetObjectTypeName().c_str();
                        const char* fnName = fn->GetName().c_str();
                        if (!objName || !fnName) {
                            continue;
                        }

                        if (Foundation::StringUtils::CaseInsensitiveEquals(objName, "UIMenuBase") &&
                            Foundation::StringUtils::CaseInsensitiveEquals(fnName, "WaitLock")) {
                            if (auto* lockOutVar = FindFrameVariable(stack, frame, "lockOut");
                                lockOutVar && lockOutVar->IsInt()) {
                                lockOutVar->SetSInt(0);
                            }
                            continue;
                        }

                        // Patch names[0] / availableNames[0] / nameList[0] across _JSW_BB_UIExtensionsBridge::ChooseName,
                        // _JSW_BB_Utility::SelectAvailableName, and _JSW_BB_Utility::ShowVRNameMenuSimple
                        if (Foundation::StringUtils::CaseInsensitiveEquals(objName, kFmrUiExtBridgeScript) ||
                            Foundation::StringUtils::CaseInsensitiveEquals(objName, kFmrUtilityScript)) {
                            const std::uint32_t pageHint = stack->GetPageForFrame(frame);
                            if (pageHint != static_cast<std::uint32_t>(-1)) {
                                const std::uint32_t frameSize = fn->GetStackFrameSize();
                                for (std::uint32_t idx = 0; idx < frameSize; ++idx) {
                                    RE::BSFixedString slotName;
                                    if (!fn->GetVarNameForStackIndex(idx, slotName) ||
                                        !slotName.c_str() ||
                                        slotName.c_str()[0] == '\0' ||
                                        slotName.c_str()[0] == ':') {
                                        continue;
                                    }
                                    auto& var = stack->GetStackFrameVariable(frame, idx, pageHint);
                                    if (var.GetType().GetRawType() == RE::BSScript::TypeInfo::RawType::kStringArray &&
                                        !var.IsNoneArray()) {
                                        if (auto arr = var.GetArray(); arr && arr->size() > 0) {
                                            (*arr)[0].SetString(chosenName);
                                            ++patchedArrays;
                                        }
                                    }
                                }
                            }

                            if (Foundation::StringUtils::CaseInsensitiveEquals(objName, kFmrUtilityScript) &&
                                Foundation::StringUtils::CaseInsensitiveEquals(fnName, kFmrGenerateNameFn)) {
                                break;
                            }
                        }
                    }
                }
            }

            MARAS_LOG_DEBUG(
                "[FertilityNamingMenuSink] Patched {} candidate String[] array(s) on FMR stack {} with '{}'",
                patchedArrays,
                stackId,
                chosenName);

            if (auto listObj = FindQuestScriptObject(
                    FormCache::GetSingleton().GetUiExtListMenuQuest(), "UIListMenu")) {
                if (auto* resStr = GetWritableObjectVariable(
                        listObj.get(), "_resultString", kUiListMenuResultStringFlatIdx)) {
                    resStr->SetString(chosenName);
                }
                if (auto* resInt = GetWritableObjectVariable(
                        listObj.get(), "_resultInt", kUiListMenuResultIntFlatIdx)) {
                    resInt->SetSInt(0);
                }
                if (auto* resFlt = GetWritableObjectVariable(
                        listObj.get(), "_resultFloat", kUiListMenuResultFloatFlatIdx)) {
                    resFlt->SetFloat(0.0f);
                }
                if (auto* waitLock = GetWritableObjectVariable(
                        listObj.get(), "_waitLock", kUiMenuBaseWaitLockFlatIdx)) {
                    waitLock->SetBool(false);
                }
            }

            SendSkseModEvent("UIListMenu_SelectItem", "0", 0.0f);

            if (patchedArrays == 0) {
                MARAS_LOG_WARN(
                    "[FertilityNamingMenuSink] FMR naming stack {} already unwound before modal closed — applying PlayerChildName fallback '{}'",
                    stackId,
                    chosenName);
                UpdateLatestFmrChildNameFallback(chosenName);
            }
        }

    }  // namespace

    FertilityNamingMenuSink* FertilityNamingMenuSink::GetSingleton() noexcept {
        static FertilityNamingMenuSink instance;
        return &instance;
    }

    void FertilityNamingMenuSink::Revert() noexcept {
        activeStackId_.store(0, std::memory_order_release);
    }

    RE::BSEventNotifyControl FertilityNamingMenuSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event || !a_event->opening ||
            std::string_view(a_event->menuName.c_str()) != kCustomMenuName) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto detectedOpt = FindActiveFmrNamingStack();
        if (!detectedOpt.has_value()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const DetectedFmrNamingStack detected = *detectedOpt;
        std::uint32_t expectedIdle = 0;
        if (!activeStackId_.compare_exchange_strong(
                expectedIdle, detected.stackId, std::memory_order_acq_rel)) {
            if (expectedIdle == detected.stackId) {
                if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
                    msgQueue->AddMessage(kCustomMenuName.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (detected.mode == NamingInterceptMode::UiListMenu) {
            SuppressUiListMenuLoadSpam();
        }

        if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
            msgQueue->AddMessage(kCustomMenuName.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
            if (auto* ui = RE::UI::GetSingleton()) {
                if (ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
                    if (const auto jMenu = ui->GetMenu(RE::JournalMenu::MENU_NAME); jMenu && jMenu->uiMovie) {
                        RE::GFxValue unlockArg(true);
                        jMenu->uiMovie->Invoke("_root.ConfigPanelFader.configPanel.unlock", nullptr, &unlockArg, 1);
                    }
                    msgQueue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
                if (ui->IsMenuOpen("Pause Menu")) {
                    msgQueue->AddMessage("Pause Menu", RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
            }
        }

        Engine::IEngineEventHooks::FertilityChildNamingRequest req{};
        req.motherId = detected.motherId;
        req.isFemale = detected.isFemale;
        req.cameFromMcmForceGrowth = detected.cameFromMcmForceGrowth;
        req.existingNames = ReadFmrExistingChildNames();

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        const RE::VMStackID capturedStackId = detected.stackId;
        const NamingInterceptMode capturedMode = detected.mode;

        MARAS_LOG_INFO(
            "[FertilityNamingMenuSink] Intercepted FMR child naming CustomMenu (stack={}, mother={:08X}, female={}, fromMcmGrowth={}) — opening M.A.R.A.S. naming modal",
            capturedStackId,
            req.motherId,
            req.isFemale,
            req.cameFromMcmForceGrowth);

        auto resolveCallback = [this, expectedEpoch, capturedStackId, capturedMode](const std::string& chosenName) {
            auto runOnMain = [this, expectedEpoch, capturedStackId, capturedMode, chosenName]() {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    activeStackId_.store(0, std::memory_order_release);
                    return;
                }
                CompleteFmrNamingIntercept(capturedStackId, capturedMode, chosenName);
                activeStackId_.store(0, std::memory_order_release);
                MARAS_LOG_INFO(
                    "[FertilityNamingMenuSink] Resolved FMR child naming modal with '{}' (stack={})",
                    chosenName,
                    capturedStackId);
            };

            if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                runOnMain();
            } else {
                (void)AddEpochTask(expectedEpoch, "FertilityNamingMenuSink::ResolveName", std::move(runOnMain));
            }
        };

        auto dispatchHook = [req = std::move(req), resolveCallback = std::move(resolveCallback)]() mutable {
            if (auto* hooks = Engine::GetEngineEventHooks()) {
                hooks->OnFertilityChildNamingRequested(std::move(req), std::move(resolveCallback));
            } else {
                resolveCallback("Child");
            }
        };

        if (!AddCurrentEpochTask("FertilityNamingMenuSink::OpenModal", dispatchHook) &&
            DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            dispatchHook();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
