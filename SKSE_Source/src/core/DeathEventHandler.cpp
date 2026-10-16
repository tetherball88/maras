#include "core/DeathEventHandler.h"
#include "core/DeceasedRegistry.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerChildrenService.h"
#include "utils/Common.h"

namespace MARAS {

    DeathEventHandler* DeathEventHandler::GetSingleton() {
        static DeathEventHandler singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl DeathEventHandler::ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>*)
    {
        if (!a_event || !a_event->dead || !a_event->actorDying) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* victim = a_event->actorDying->As<RE::Actor>();
        if (!victim) return RE::BSEventNotifyControl::kContinue;

        auto victimID = victim->GetFormID();
        auto* killer = a_event->actorKiller ? a_event->actorKiller->As<RE::Actor>() : nullptr;
        bool isPlayerKiller = (killer && killer->IsPlayerRef());

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        auto& deceasedReg = DeceasedRegistry::GetSingleton();

        // 1. Check if victim is a registered Child
        auto childEntryOpt = relMgr.GetChildEntry(victimID);
        if (childEntryOpt.has_value()) {
            MARAS_LOG_INFO("DeathEventHandler: Registered child {:08X} ('{}') died. Killer is player: {}",
                           victimID, victim->GetDisplayFullName(), isPlayerKiller);
            deceasedReg.RecordDeceasedChild(victim, childEntryOpt.value(), isPlayerKiller, killer);
            relMgr.RemoveChild(victimID);

            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent modEvent("maras_child_died", "", isPlayerKiller ? 1.0f : 0.0f, victim);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("DeathEventHandler: Dispatched maras_child_died event for child {:08X}", victimID);
            }

            return RE::BSEventNotifyControl::kContinue;
        }

        // 2. Check if victim is a Pet or Critter belonging to a child
        auto& childSvc = PlayerChildrenService::GetSingleton();
        RE::FormID petChildID = childSvc.GetChildForPetOrCritter(victimID);
        if (petChildID != 0) {
            while (petChildID != 0) {
                MARAS_LOG_INFO("DeathEventHandler: Animal {:08X} belonging to child {:08X} died", victimID, petChildID);
                if (childSvc.GetChildForPet(victimID) == petChildID) {
                    childSvc.RemovePet(petChildID);
                    childSvc.RecordPetRejected(petChildID);
                }
                if (childSvc.GetChildForCritter(victimID) == petChildID) {
                    childSvc.RemoveCritter(petChildID);
                    childSvc.RecordCritterRejected(petChildID);
                }
                if (isPlayerKiller) {
                    childSvc.IssueOrder(petChildID, 10.0f, 24.0f);
                }
                petChildID = childSvc.GetChildForPetOrCritter(victimID);
            }
            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent modEvent("maras_child_pet_died", "", isPlayerKiller ? 1.0f : 0.0f, victim);
                eventSource->SendEvent(&modEvent);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // 3. Check if victim is an Adult tracked by MARAS
        RE::FormID victimBaseID = 0;
        if (auto* actorBase = victim->GetActorBase()) {
            victimBaseID = actorBase->GetFormID();
        }
        bool isRegistered = relMgr.IsRegistered(victimID) || (victimBaseID != 0 && relMgr.IsRegistered(victimBaseID));

        if (isRegistered) {
            RE::FormID registeredID = relMgr.IsRegistered(victimID) ? victimID : victimBaseID;
            MARAS_LOG_INFO("DeathEventHandler: Tracked adult {:08X} ('{}') died. Killer is player: {}",
                           registeredID, victim->GetDisplayFullName(), isPlayerKiller);

            // Record in DeceasedRegistry BEFORE removing active records
            deceasedReg.RecordDeceasedAdult(victim, isPlayerKiller, killer);

            // Dispatch mod event to Papyrus for ANY tracked actor who died
            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent modEvent("maras_actor_died", "", isPlayerKiller ? 1.0f : 0.0f, victim);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("DeathEventHandler: Dispatched maras_actor_died event for {:08X}", registeredID);
            }

            // Perform death cleanup on active relationship records
            relMgr.HandleNPCDeath(registeredID);
        } else {
            // Unregistered actor: evaluate custody only if victim was a parent/guardian of a child
            relMgr.EvaluateChildCustodyOnDeath(victimID);
            if (victimBaseID != 0 && victimBaseID != victimID) {
                relMgr.EvaluateChildCustodyOnDeath(victimBaseID);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
