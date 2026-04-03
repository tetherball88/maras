#include "core/DialogueEventSink.h"

#include "utils/Common.h"

namespace MARAS {

    DialogueEventSink* DialogueEventSink::GetSingleton() {
        static DialogueEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl DialogueEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent*          a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        // If we're waiting for speaker resolution, try on every incoming UI event
        if (waitingForSpeaker_.load()) {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
                MARAS_LOG_DEBUG("DialogueEventSink: menu closed before speaker resolved, aborting");
                waitingForSpeaker_.store(false);
            } else {
                auto* tm = RE::MenuTopicManager::GetSingleton();
                if (tm) {
                    auto speakerRef = tm->speaker.get();
                    if (speakerRef) {
                        auto* npc = speakerRef->As<RE::Actor>();
                        if (npc && npc != RE::PlayerCharacter::GetSingleton()) {
                            MARAS_LOG_DEBUG("DialogueEventSink: resolved deferred speaker {:08X} ({})", npc->GetFormID(), npc->GetName());
                            waitingForSpeaker_.store(false);
                            SendDialogueEvent(npc);
                        }
                    }
                }
            }
        }

        if (a_event->menuName != RE::DialogueMenu::MENU_NAME)
            return RE::BSEventNotifyControl::kContinue;

        if (!a_event->opening) {
            waitingForSpeaker_.store(false);
            MARAS_LOG_DEBUG("DialogueEventSink: Dialogue Menu closed");
            return RE::BSEventNotifyControl::kContinue;
        }

        MARAS_LOG_DEBUG("DialogueEventSink: Dialogue Menu opened, resolving speaker");

        auto* tm = RE::MenuTopicManager::GetSingleton();
        if (!tm) {
            MARAS_LOG_WARN("DialogueEventSink: MenuTopicManager is null");
            return RE::BSEventNotifyControl::kContinue;
        }

        auto speakerRef = tm->speaker.get();
        if (!speakerRef) {
            MARAS_LOG_DEBUG("DialogueEventSink: speaker handle is empty, waiting for NPC to finish speaking");
            waitingForSpeaker_.store(true);
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* npc = speakerRef->As<RE::Actor>();
        if (!npc) {
            MARAS_LOG_DEBUG("DialogueEventSink: speaker {:08X} is not an Actor (talking activator?)", speakerRef->GetFormID());
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
        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_ERROR("DialogueEventSink: ModCallbackEventSource unavailable");
            return;
        }

        SKSE::ModCallbackEvent modEvent{"maras_dialogue_opened", "", 0.0f, npc};
        eventSource->SendEvent(&modEvent);
        MARAS_LOG_INFO("Sent maras_dialogue_opened for actor {:08X} ({})", npc->GetFormID(), npc->GetName());
    }

}  // namespace MARAS
