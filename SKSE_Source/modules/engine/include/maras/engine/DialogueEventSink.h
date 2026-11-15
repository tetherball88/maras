#pragma once

#include "maras/engine/EngineLogging.h"

#include <atomic>

namespace MARAS {

    // Listens for "Dialogue Menu" open/close events, invokes IEngineEventHooks::OnDialogueOpened
    // (RelationshipInteractionCoordinator -> daily dialoguestarted affection + native No-AI
    // chance globals 0x5D0/0x5D1/0x15C), and emits "maras_dialogue_opened" with speaker as sender.
    //
    // When speaker is empty at open time (NPC still finishing a previous line), a flag is
    // set and resolved on the next frame task, input tick, or incoming UI event.
    class DialogueEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static DialogueEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

        // Attempts to resolve a deferred dialogue speaker if waitingForSpeaker_ is active.
        bool TryResolveDeferredSpeaker();

        [[nodiscard]] bool IsWaitingForSpeaker() const noexcept {
            return waitingForSpeaker_.load(std::memory_order_acquire);
        }

        // Resets deferred speaker wait state across save/load/new-game transitions.
        void Revert();

    private:
        DialogueEventSink() = default;
        DialogueEventSink(const DialogueEventSink&) = delete;
        DialogueEventSink(DialogueEventSink&&) = delete;
        DialogueEventSink& operator=(const DialogueEventSink&) = delete;
        DialogueEventSink& operator=(DialogueEventSink&&) = delete;

        void SendDialogueEvent(RE::Actor* npc);

        // Set when Dialogue Menu opens but speaker is not yet populated.
        // Cleared when speaker resolves or the menu closes.
        std::atomic<bool> waitingForSpeaker_{ false };
    };

}  // namespace MARAS
