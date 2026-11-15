#include "maras/engine/SleepEventSink.h"

#include "maras/domain/household/SpouseBuffRules.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <atomic>
#include <memory>
#include <vector>

namespace MARAS {

    namespace {

        class SleepRestedSyncCallback final : public RE::BSScript::IStackCallbackFunctor {
        public:
            SleepRestedSyncCallback(
                SessionEpoch epoch,
                Domain::Household::SleepRestedPlan plan,
                std::shared_ptr<std::atomic<int>> pending) noexcept
                : epoch_(epoch), plan_(plan), pending_(std::move(pending)) {}

            ~SleepRestedSyncCallback() override = default;

            void operator()(RE::BSScript::Variable) override {
                if (!pending_ || pending_->fetch_sub(1, std::memory_order_acq_rel) != 1) {
                    return;
                }
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
            std::shared_ptr<std::atomic<int>> pending_{};
        };

    }  // namespace

    SleepEventSink* SleepEventSink::GetSingleton() {
        static SleepEventSink instance;
        return &instance;
    }

    RE::BSEventNotifyControl SleepEventSink::ProcessEvent(
        const RE::TESSleepStopEvent* a_event,
        RE::BSTEventSource<RE::TESSleepStopEvent>*) {
        if (!a_event || a_event->interrupted) {
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
                        if (actor && !actor->IsDeleted() && !actor->IsDead() && !actor->IsDisabled() &&
                            !actor->IsPlayerRef() && actor->GetParentCell() == playerCell) {
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
                    tenant && !tenant->IsDeleted() && !tenant->IsDead() && !tenant->IsDisabled() &&
                    tenant->GetCurrentLocation() == currentLocation) {
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

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        if (!AddEpochTask(
                expectedEpoch,
                "SleepEventSink::DispatchRestedCleanup",
                [expectedEpoch, plan, needRemoveRested, needRemoveAdoption]() {
                    auto* sleepQuest = FormCache::GetSingleton().GetPlayerSleepQuest();
                    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                    auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
                    const RE::VMHandle handle = (sleepQuest && policy)
                        ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, sleepQuest)
                        : 0;

                    if (vm && policy && handle != 0 && handle != policy->EmptyHandle()) {
                        auto pending = std::make_shared<std::atomic<int>>(1);
                        auto makeCb = [&]() {
                            return RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>(
                                new SleepRestedSyncCallback(expectedEpoch, plan, pending));
                        };

                        if (needRemoveRested) {
                            pending->fetch_add(1, std::memory_order_acq_rel);
                            auto* args = RE::MakeFunctionArguments();
                            auto cb = makeCb();
                            if (!vm->DispatchMethodCall2(
                                    handle,
                                    RE::BSFixedString("PlayerSleepQuestScript"),
                                    RE::BSFixedString("RemoveRested"),
                                    args,
                                    cb)) {
                                pending->fetch_sub(1, std::memory_order_acq_rel);
                            }
                        }

                        if (needRemoveAdoption) {
                            pending->fetch_add(1, std::memory_order_acq_rel);
                            auto* args = RE::MakeFunctionArguments();
                            auto cb = makeCb();
                            if (!vm->DispatchMethodCall2(
                                    handle,
                                    RE::BSFixedString("PlayerSleepQuestScript"),
                                    RE::BSFixedString("RemoveAdoptionRested"),
                                    args,
                                    cb)) {
                                pending->fetch_sub(1, std::memory_order_acq_rel);
                            }
                        }

                        if (pending->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            MARAS_LOG_DEBUG("SleepEventSink: no PlayerSleepQuestScript rested callbacks pending; applying rested plan directly");
                            BuffReconciler::GetSingleton().ApplySleepRestedPlan(plan);
                        }
                        return;
                    }

                    MARAS_LOG_DEBUG("SleepEventSink: PlayerSleepQuestScript VM handle unavailable; applying rested plan directly");
                    BuffReconciler::GetSingleton().ApplySleepRestedPlan(plan);
                })) {
            MARAS_LOG_DEBUG("SleepEventSink: AddEpochTask unavailable; applying rested plan directly");
            BuffReconciler::GetSingleton().ApplySleepRestedPlan(plan);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
