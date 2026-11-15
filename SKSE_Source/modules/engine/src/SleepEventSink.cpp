#include "maras/engine/SleepEventSink.h"

#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <vector>

namespace MARAS {

    namespace {

        class SleepRestedSyncCallback final : public RE::BSScript::IStackCallbackFunctor {
        public:
            SleepRestedSyncCallback(SessionEpoch epoch, Domain::Household::SleepRestedPlan plan) noexcept
                : epoch_(epoch), plan_(plan) {}

            ~SleepRestedSyncCallback() override = default;

            void operator()(RE::BSScript::Variable) override {
                if (!IsSessionEpochCurrent(epoch_)) {
                    return;
                }
                AddEpochTask(epoch_, "SleepEventSink::ApplySleepRestedPlan", [plan = plan_]() {
                    BuffReconciler::GetSingleton().ApplySleepRestedPlan(plan);
                });
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

        private:
            SessionEpoch epoch_{ 0 };
            Domain::Household::SleepRestedPlan plan_{};
        };

    }  // namespace

    SleepEventSink* SleepEventSink::GetSingleton() {
        static SleepEventSink instance;
        return &instance;
    }

    RE::BSEventNotifyControl SleepEventSink::ProcessEvent(
        const RE::TESSleepStopEvent* a_event,
        RE::BSTEventSource<RE::TESSleepStopEvent>*) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* hooks = Engine::GetEngineEventHooks();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!hooks || !player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* playerCell = player->GetParentCell();
        std::vector<RE::FormID> sameCellActorIds;
        if (playerCell) {
            if (auto* procLists = RE::ProcessLists::GetSingleton()) {
                for (const auto& handle : procLists->highActorHandles) {
                    if (auto actorPtr = handle.get()) {
                        auto* actor = actorPtr.get();
                        if (actor && !actor->IsDeleted() && !actor->IsPlayerRef() &&
                            actor->GetParentCell() == playerCell) {
                            sameCellActorIds.push_back(actor->GetFormID());
                        }
                    }
                }
            }
        }

        auto* currentLocation = player->GetCurrentLocation();
        const RE::FormID currentLocationId = currentLocation ? currentLocation->GetFormID() : 0;
        std::vector<RE::FormID> presentAtLocationRawIds;
        if (currentLocation && currentLocationId != 0) {
            std::vector<std::uint32_t> tenantIds;
            {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                if (snap && snap->household) {
                    if (const auto* house = snap->household->FindHouse(State::HouseId{ currentLocationId })) {
                        tenantIds = house->tenantOrder;
                    }
                }
            }
            for (const std::uint32_t rawId : tenantIds) {
                if (rawId == 0) {
                    continue;
                }
                if (auto* tenant = RE::TESForm::LookupByID<RE::Actor>(rawId);
                    tenant && !tenant->IsDeleted() && tenant->GetCurrentLocation() == currentLocation) {
                    presentAtLocationRawIds.push_back(rawId);
                }
            }
        }

        Foundation::ActorSex playerSex = Foundation::ActorSex::Male;
        if (auto* base = player->GetActorBase(); base && base->GetSex() == RE::SEXES::kFemale) {
            playerSex = Foundation::ActorSex::Female;
        }

        const auto plan = hooks->OnPlayerSlept(
            sameCellActorIds,
            currentLocationId,
            presentAtLocationRawIds,
            playerSex);

        if (plan.partnerBuff == Domain::Household::PartnerSleepBuff::None &&
            plan.childBuff == Domain::Household::ChildSleepBuff::None) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const bool needRemoveRested = plan.partnerBuff != Domain::Household::PartnerSleepBuff::None;
        const bool needRemoveAdoption = plan.childBuff != Domain::Household::ChildSleepBuff::None;

        auto* sleepQuest = FormCache::GetSingleton().GetPlayerSleepQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        const RE::VMHandle handle = (sleepQuest && policy)
            ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, sleepQuest)
            : 0;

        if (vm && policy && handle != 0 && handle != policy->EmptyHandle()) {
            const SessionEpoch expectedEpoch = GetSessionEpoch();
            bool callbackQueued = false;

            if (needRemoveRested) {
                auto* args = RE::MakeFunctionArguments();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
                if (!needRemoveAdoption) {
                    cb.reset(new SleepRestedSyncCallback(expectedEpoch, plan));
                }
                if (vm->DispatchMethodCall2(
                        handle,
                        RE::BSFixedString("PlayerSleepQuestScript"),
                        RE::BSFixedString("RemoveRested"),
                        args,
                        cb)) {
                    callbackQueued = !needRemoveAdoption;
                }
            }

            if (needRemoveAdoption) {
                auto* args = RE::MakeFunctionArguments();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(
                    new SleepRestedSyncCallback(expectedEpoch, plan));
                if (vm->DispatchMethodCall2(
                        handle,
                        RE::BSFixedString("PlayerSleepQuestScript"),
                        RE::BSFixedString("RemoveAdoptionRested"),
                        args,
                        cb)) {
                    callbackQueued = true;
                }
            }

            if (callbackQueued) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        MARAS_LOG_DEBUG("SleepEventSink: PlayerSleepQuestScript VM handle unavailable; applying rested plan directly");
        BuffReconciler::GetSingleton().ApplySleepRestedPlan(plan);
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
