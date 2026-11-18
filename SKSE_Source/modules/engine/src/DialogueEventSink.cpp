#include "maras/engine/DialogueEventSink.h"

#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/TaskDispatcher.h"

#include <algorithm>
#include <utility>

namespace MARAS {

    DialogueEventSink* DialogueEventSink::GetSingleton() {
        static DialogueEventSink singleton;
        return &singleton;
    }

    void DialogueEventSink::Revert() {
        waitingForSpeaker_.store(false, std::memory_order_release);
        std::lock_guard lock(deferredEvalMutex_);
        deferredPackageEvalActors_.clear();
    }

    void DialogueEventSink::DeferPackageEvalUntilDialogueClosed(RE::FormID actorId) {
        if (!HasPhysicalActor(actorId)) {
            return;
        }
        {
            std::lock_guard lock(deferredEvalMutex_);
            if (std::find(deferredPackageEvalActors_.begin(), deferredPackageEvalActors_.end(), actorId) ==
                deferredPackageEvalActors_.end()) {
                deferredPackageEvalActors_.push_back(actorId);
            }
        }
        if (auto* ui = RE::UI::GetSingleton(); !ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            FlushDeferredPackageEvals();
        }
    }

    void DialogueEventSink::FlushDeferredPackageEvals() {
        std::vector<RE::FormID> pending;
        {
            std::lock_guard lock(deferredEvalMutex_);
            if (deferredPackageEvalActors_.empty()) {
                return;
            }
            pending.swap(deferredPackageEvalActors_);
        }

        AddCurrentEpochTask("DialogueEventSink::FlushDeferredPackageEvals",
                            [actors = std::move(pending)]() {
                                std::lock_guard lock(GetPapyrusMutex());
                                for (const RE::FormID actorId : actors) {
                                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
                                    if (!actor || actor->IsDeleted() || actor->IsDead() || !actor->Is3DLoaded()) {
                                        continue;
                                    }
                                    actor->EvaluatePackage();
                                    MARAS_LOG_DEBUG(
                                        "DialogueEventSink: ran deferred post-dialogue EvaluatePackage for actor {:08X} ({})",
                                        actorId,
                                        actor->GetName());
                                }
                            });
    }

    bool DialogueEventSink::TryResolveDeferredSpeaker() {
        if (!waitingForSpeaker_.load(std::memory_order_acquire)) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
            if (waitingForSpeaker_.exchange(false, std::memory_order_acq_rel)) {
                MARAS_LOG_DEBUG("DialogueEventSink: menu closed before speaker resolved, aborting");
            }
            return false;
        }

        auto* tm = RE::MenuTopicManager::GetSingleton();
        if (!tm) {
            return false;
        }

        auto speakerRef = tm->speaker.get();
        if (!speakerRef) {
            return false;
        }

        auto* npc = speakerRef->As<RE::Actor>();
        if (!npc) {
            waitingForSpeaker_.store(false, std::memory_order_release);
            MARAS_LOG_DEBUG("DialogueEventSink: deferred speaker {:08X} is not an Actor, stopping wait",
                            speakerRef->GetFormID());
            return false;
        }

        if (npc == RE::PlayerCharacter::GetSingleton()) {
            waitingForSpeaker_.store(false, std::memory_order_release);
            MARAS_LOG_DEBUG("DialogueEventSink: deferred speaker is the player, skipping");
            return false;
        }

        if (waitingForSpeaker_.exchange(false, std::memory_order_acq_rel)) {
            MARAS_LOG_DEBUG("DialogueEventSink: resolved deferred speaker {:08X} ({})", npc->GetFormID(), npc->GetName());
            SendDialogueEvent(npc);
            return true;
        }
        return false;
    }

    RE::BSEventNotifyControl DialogueEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName != RE::DialogueMenu::MENU_NAME) {
            TryResolveDeferredSpeaker();
            if (auto* ui = RE::UI::GetSingleton(); !ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
                FlushDeferredPackageEvals();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!a_event->opening) {
            waitingForSpeaker_.store(false, std::memory_order_release);
            FlushDeferredPackageEvals();
            MARAS_LOG_DEBUG("DialogueEventSink: Dialogue Menu closed");
            return RE::BSEventNotifyControl::kContinue;
        }

        waitingForSpeaker_.store(false, std::memory_order_release);
        MARAS_LOG_DEBUG("DialogueEventSink: Dialogue Menu opened, resolving speaker");

        auto* tm = RE::MenuTopicManager::GetSingleton();
        if (!tm) {
            MARAS_LOG_WARN("DialogueEventSink: MenuTopicManager is null");
            return RE::BSEventNotifyControl::kContinue;
        }

        auto speakerRef = tm->speaker.get();
        if (!speakerRef) {
            MARAS_LOG_DEBUG("DialogueEventSink: speaker handle is empty, waiting for NPC to finish speaking");
            waitingForSpeaker_.store(true, std::memory_order_release);
            AddCurrentEpochTask("DialogueEventSink::ResolveDeferredSpeaker", []() {
                DialogueEventSink::GetSingleton()->TryResolveDeferredSpeaker();
            });
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* npc = speakerRef->As<RE::Actor>();
        if (!npc) {
            MARAS_LOG_DEBUG("DialogueEventSink: speaker {:08X} is not an Actor (talking activator?)",
                            speakerRef->GetFormID());
            return RE::BSEventNotifyControl::kContinue;
        }

        if (npc == RE::PlayerCharacter::GetSingleton()) {
            MARAS_LOG_DEBUG("DialogueEventSink: speaker is the player, skipping");
            return RE::BSEventNotifyControl::kContinue;
        }

        const char* speakerName = npc->GetDisplayFullName();
        if (!speakerName || speakerName[0] == '\0') {
            speakerName = npc->GetName();
        }
        MARAS_LOG_DEBUG("DialogueEventSink: resolved speaker {:08X} ({})", npc->GetFormID(), speakerName ? speakerName : "");
        SendDialogueEvent(npc);
        return RE::BSEventNotifyControl::kContinue;
    }

    void DialogueEventSink::SendDialogueEvent(RE::Actor* npc) {
        if (!npc) {
            return;
        }
        const RE::FormID npcId = npc->GetFormID();
        if (npcId == 0) {
            return;
        }

        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddCurrentEpochTask("DialogueEventSink::SendDialogueEvent", [npcId]() {
                if (!HasPhysicalActor(npcId)) {
                    return;
                }
                FactionReconciler::GetSingleton().ReconcilePlayer();
                FactionReconciler::GetSingleton().ReconcileActor(npcId);
                if (auto* hooks = Engine::GetEngineEventHooks()) {
                    hooks->OnDialogueOpened(npcId);
                }
                EmitModCallbackEvent("maras_dialogue_opened", "", 0.0f, npcId);
                MARAS_LOG_INFO("Sent maras_dialogue_opened for actor {:08X} (deferred to main thread)", npcId);
            });
            return;
        }

        FactionReconciler::GetSingleton().ReconcilePlayer();
        FactionReconciler::GetSingleton().ReconcileLoadedActor(npc);

        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnDialogueOpened(npcId);
        }

        EmitModCallbackEvent("maras_dialogue_opened", "", 0.0f, npc);
        MARAS_LOG_INFO("Sent maras_dialogue_opened for actor {:08X} ({})", npcId, npc->GetName());
    }

}  // namespace MARAS
