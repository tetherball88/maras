#pragma once

#include <atomic>

namespace MARAS {

    // Listens for "Dialogue Menu" open/close events and fires the Papyrus ModCallbackEvent
    // "maras_dialogue_opened" with the speaking NPC as the sender.
    //
    // The sink receives ALL MenuOpenCloseEvents from RE::UI, not just Dialogue Menu ones.
    // When speaker is empty at open time (NPC still finishing a previous line), a flag is
    // set and the speaker is resolved on the next incoming MenuOpenCloseEvent instead.
    // No AddTask, no timer, no separate thread.
    class DialogueEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static DialogueEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

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
