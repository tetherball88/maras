#include "maras/engine/DialogueEventSink.h"

#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/TaskDispatcher.h"

namespace MARAS {

    DialogueEventSink* DialogueEventSink::GetSingleton() {
        static DialogueEventSink singleton;
        return &singleton;
    }

    void DialogueEventSink::Revert() {
        waitingForSpeaker_.store(false, std::memory_order_release);
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
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!a_event->opening) {
            waitingForSpeaker_.store(false, std::memory_order_release);
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

        MARAS_LOG_DEBUG("DialogueEventSink: resolved speaker {:08X} ({})", npc->GetFormID(), npc->GetName());
        SendDialogueEvent(npc);
        return RE::BSEventNotifyControl::kContinue;
    }

    void DialogueEventSink::SendDialogueEvent(RE::Actor* npc) {
        FactionReconciler::GetSingleton().ReconcilePlayer();
        FactionReconciler::GetSingleton().ReconcileLoadedActor(npc);

        if (auto* hooks = Engine::GetEngineEventHooks(); hooks && npc) {
            hooks->OnDialogueOpened(npc->GetFormID());
        }

        EmitModCallbackEvent("maras_dialogue_opened", "", 0.0f, npc);
        MARAS_LOG_INFO("Sent maras_dialogue_opened for actor {:08X} ({})", npc->GetFormID(), npc->GetName());
    }

}  // namespace MARAS
