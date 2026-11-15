#pragma once

#include "RE/Skyrim.h"

#include <cstdint>
#include <string_view>

namespace MARAS::App {

    /**
     * Layer 4 application coordinator for CK dialogue topic fragments (`TTM_DialogueManagerMisc`):
     * executes C++ state mutations for a `PromptKey` and returns compact effect codes
     * for the thin remaining Papyrus speech/scene/thread side effects.
     */
    class DialogueOutcomeCoordinator {
    public:
        static DialogueOutcomeCoordinator& GetSingleton();

        DialogueOutcomeCoordinator(const DialogueOutcomeCoordinator&) = delete;
        DialogueOutcomeCoordinator& operator=(const DialogueOutcomeCoordinator&) = delete;
        DialogueOutcomeCoordinator(DialogueOutcomeCoordinator&&) = delete;
        DialogueOutcomeCoordinator& operator=(DialogueOutcomeCoordinator&&) = delete;

        /// Executes OnBegin (`Fragment_0`) state transitions and returns `DialogueTopicBeginOutcome::PackPapyrusEffectCode()`.
        [[nodiscard]] std::int32_t ApplyDialoguePromptBegin(RE::Actor* speaker, std::string_view promptKey);

        /// Executes OnEnd (`Fragment_2`) state transitions and returns `DialogueEndSideEffect` as an int32 code.
        [[nodiscard]] std::int32_t ApplyDialoguePromptEnd(RE::Actor* speaker, std::string_view promptKey);

    private:
        DialogueOutcomeCoordinator() = default;
        ~DialogueOutcomeCoordinator() = default;
    };

}  // namespace MARAS::App
