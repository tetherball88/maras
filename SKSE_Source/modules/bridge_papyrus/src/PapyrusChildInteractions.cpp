#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/ChildInteractionCoordinator.h"

#include <array>
#include <cstdint>
#include <string>

namespace MARAS::PapyrusInterface {

    namespace {

        std::string SelectChildForcegreetEvent(
            RE::StaticFunctionTag*,
            RE::BGSLocation* akCurrentLoc,
            bool abHasAnimalCompanion,
            std::int32_t aiCritterChance,
            std::int32_t aiNameCallingChance) {
            return App::ChildInteractionCoordinator::GetSingleton().SelectChildForcegreetEvent(
                akCurrentLoc, abHasAnimalCompanion, aiCritterChance, aiNameCallingChance);
        }

        void CommitNameCallingForcegreet(RE::StaticFunctionTag*, std::int32_t aiEventNumber) {
            App::ChildInteractionCoordinator::GetSingleton().CommitNameCallingForcegreet(aiEventNumber);
        }

        std::string SelectGenericChildForcegreetFallback(
            RE::StaticFunctionTag*,
            RE::BGSLocation* akCurrentLoc) {
            return App::ChildInteractionCoordinator::GetSingleton().SelectGenericChildForcegreetFallback(
                akCurrentLoc);
        }

        std::int32_t EvaluateChildPetSuitability(
            RE::StaticFunctionTag*,
            RE::Actor* akPet,
            RE::BGSListForm* akDogRacesList,
            RE::BGSListForm* akAllowedRacesList,
            RE::TESFaction* akPotentialPetFaction) {
            return App::ChildInteractionCoordinator::GetSingleton().EvaluateChildPetSuitability(
                akPet, akDogRacesList, akAllowedRacesList, akPotentialPetFaction);
        }

        RE::Actor* SpawnChildTransientCritter(
            RE::StaticFunctionTag*,
            RE::Actor* akChild,
            RE::BGSListForm* akMaleCritters,
            RE::BGSListForm* akFemaleCritters) {
            return App::ChildInteractionCoordinator::GetSingleton().SpawnChildTransientCritter(
                akChild, akMaleCritters, akFemaleCritters);
        }

        bool AdoptChildTransientCritter(RE::StaticFunctionTag*, RE::Actor* akChild) {
            return App::ChildInteractionCoordinator::GetSingleton().AdoptChildTransientCritter(akChild);
        }

        void DismissChildTransientCritter(RE::StaticFunctionTag*, RE::Actor* akChild) {
            App::ChildInteractionCoordinator::GetSingleton().DismissChildTransientCritter(akChild);
        }

        std::int32_t GetChildPoorCounter(RE::StaticFunctionTag*) {
            return App::ChildInteractionCoordinator::GetSingleton().GetChildPoorCounter();
        }

        std::int32_t ProcessChildAllowance(
            RE::StaticFunctionTag*,
            RE::Actor* akChild,
            std::int32_t aiAllowanceAmount) {
            return App::ChildInteractionCoordinator::GetSingleton().ProcessChildAllowance(
                akChild, aiAllowanceAmount);
        }

        std::int32_t ProcessChildPoorGift(
            RE::StaticFunctionTag*,
            RE::Actor* akChild,
            RE::BGSListForm* akPoorGiftsList) {
            return App::ChildInteractionCoordinator::GetSingleton().ProcessChildPoorGift(
                akChild, akPoorGiftsList);
        }

        void ProcessChildRegularGift(
            RE::StaticFunctionTag*,
            RE::Actor* akChild,
            RE::BGSListForm* akList0000,
            RE::BGSListForm* akList0025,
            RE::BGSListForm* akList0050,
            RE::BGSListForm* akList0100,
            RE::BGSListForm* akList0250,
            RE::BGSListForm* akList0500,
            RE::BGSListForm* akList1000) {
            const std::array<RE::BGSListForm*, 7> lists{
                akList0000, akList0025, akList0050, akList0100, akList0250, akList0500, akList1000
            };
            App::ChildInteractionCoordinator::GetSingleton().ProcessChildRegularGift(akChild, lists);
        }

        void RefillChildChestFromLists(
            RE::StaticFunctionTag*,
            RE::TESObjectREFR* akChildChest,
            RE::BGSLocation* akHouseLoc,
            RE::BGSListForm* akJunkList,
            RE::BGSListForm* akList0000,
            RE::BGSListForm* akList0025,
            RE::BGSListForm* akList0050,
            RE::BGSListForm* akList0100,
            RE::BGSListForm* akList0250,
            RE::BGSListForm* akList0500,
            RE::BGSListForm* akList1000) {
            const std::array<RE::BGSListForm*, 7> lists{
                akList0000, akList0025, akList0050, akList0100, akList0250, akList0500, akList1000
            };
            App::ChildInteractionCoordinator::GetSingleton().RefillChildChestFromLists(
                akChildChest, akHouseLoc, akJunkList, lists);
        }

    }  // namespace

    void RegisterPapyrusChildInteractionFunctions(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            return;
        }
        RegisterSynced<SelectChildForcegreetEvent>(vm, "SelectChildForcegreetEvent");
        RegisterSynced<CommitNameCallingForcegreet>(vm, "CommitNameCallingForcegreet");
        RegisterSynced<SelectGenericChildForcegreetFallback>(vm, "SelectGenericChildForcegreetFallback");
        RegisterSynced<EvaluateChildPetSuitability>(vm, "EvaluateChildPetSuitability");
        RegisterSynced<SpawnChildTransientCritter>(vm, "SpawnChildTransientCritter");
        RegisterSynced<AdoptChildTransientCritter>(vm, "AdoptChildTransientCritter");
        RegisterSynced<DismissChildTransientCritter>(vm, "DismissChildTransientCritter");
        RegisterSynced<GetChildPoorCounter>(vm, "GetChildPoorCounter");
        RegisterSynced<ProcessChildAllowance>(vm, "ProcessChildAllowance");
        RegisterSynced<ProcessChildPoorGift>(vm, "ProcessChildPoorGift");
        RegisterSynced<ProcessChildRegularGift>(vm, "ProcessChildRegularGift");
        RegisterSynced<RefillChildChestFromLists>(vm, "RefillChildChestFromLists");
    }

}  // namespace MARAS::PapyrusInterface
