#include "maras/engine/DeathEventHandler.h"

#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <mutex>

namespace MARAS {

    DeathEventHandler* DeathEventHandler::GetSingleton() {
        static DeathEventHandler singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl DeathEventHandler::ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>*) {
        if (!a_event || !a_event->dead || !a_event->actorDying ||
            a_event->actorDying->GetFormType() != RE::FormType::ActorCharacter) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID victimID = a_event->actorDying->GetFormID();
        if (victimID == 0) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID killerID = a_event->actorKiller ? a_event->actorKiller->GetFormID() : 0;
        const bool isPlayerKiller = (killerID == kPlayerFormID);

        AddCurrentEpochTask("DeathEventHandler::ProcessEvent", [this, victimID, killerID, isPlayerKiller]() {
            HandleActorDeathOnMainThread(victimID, killerID, isPlayerKiller);
        });

        return RE::BSEventNotifyControl::kContinue;
    }

    void DeathEventHandler::HandleActorDeathOnMainThread(
        RE::FormID victimID,
        RE::FormID killerID,
        bool isPlayerKiller) {
        std::lock_guard papyrusLock(GetPapyrusMutex());

        auto* victim = RE::TESForm::LookupByID<RE::Actor>(victimID);
        if (!victim || victim->IsDeleted() || !victim->IsDead()) {
            return;
        }

        auto* killer = (killerID != 0) ? RE::TESForm::LookupByID<RE::Actor>(killerID) : nullptr;
        if (!isPlayerKiller && killer && killer->IsPlayerRef()) {
            isPlayerKiller = true;
        }

        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnActorDeath(victimID, killerID, isPlayerKiller);
        }
    }

}  // namespace MARAS
