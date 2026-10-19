#pragma once

#include "utils/Common.h"

namespace MARAS {

// ---------------------------------------------------------------------------
// ConfirmationModal — Singleton UI modal manager.
// Registers as a LATENT Papyrus native so the Papyrus stack suspends
// instead of blocking the main game thread.
//
// Return codes (via vm->ReturnLatentResult):
//    0 = player accepted
//    1 = player declined / cancelled
//    2 = player chose "Decline (Explain)" — TriggerTextInput fired, decline cooldown set
//    3 = action blocked by active cooldown
//    4 = player not among actors (accepted by default)
//    5 = modal disabled in configuration
// ---------------------------------------------------------------------------
class ConfirmationModal {
public:
    static ConfirmationModal& GetSingleton();

    enum class ModalAction : uint8_t {
        Dating,
        Engaged,
        Lover,
        Married,
        ExDating,
        Jilted,
        ExLover,
        Divorced,
        LowAffectionDivorce,
        OpenRelationship,
        CloseRelationship,
        Custom
    };

    static ModalAction StringToModalAction(std::string_view actionStr);
    static std::string_view ModalActionToString(ModalAction action);

    bool IsOnCooldown(const char* actionType, RE::Actor* originator);

    // Atomic check-and-set for action cooldown.
    // Returns 0 if cooldown was free and has now been set; 1 if blocked by active cooldown.
    int32_t CheckAndSet(const char* actionType, const std::vector<RE::Actor*>& actors);

    // Latent Papyrus entry point.
    RE::BSScript::LatentStatus Show(RE::BSScript::Internal::VirtualMachine* vm,
                                    RE::VMStackID stackID,
                                    const char* actionType,
                                    const std::vector<RE::Actor*>& mainActors,
                                    const std::vector<RE::Actor*>& secondaryActors,
                                    const char* paramsJson);

    void SetCooldown(const std::string& key, int seconds);
    void SetCooldownAll(const std::vector<std::string>& keys, int seconds);
    void Revert();

private:
    ConfirmationModal() = default;
    ConfirmationModal(const ConfirmationModal&) = delete;
    ConfirmationModal& operator=(const ConfirmationModal&) = delete;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> _cooldowns;
    std::shared_mutex _cooldownMutex;

    static bool IsPlayer(RE::Actor* actor);
    static std::string BuildCooldownKey(const char* actionType, RE::FormID formID);
    static std::string FormatActorNames(const std::vector<RE::Actor*>& actors);
    static bool IsActionTypeEnabled(ModalAction action);
    static std::string BuildMessage(ModalAction action,
                                    const std::string& actionTypeRaw,
                                    const std::string& mainActorList,
                                    const std::string& secondaryActorList,
                                    const std::string& activity,
                                    const std::string& details);
};

}  // namespace MARAS
