#include "maras/engine/GiftMenuEventSink.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <utility>

namespace MARAS {

    GiftMenuEventSink* GiftMenuEventSink::GetSingleton() {
        static GiftMenuEventSink instance;
        return &instance;
    }

    void GiftMenuEventSink::Revert() {
        giftMenuOpen_.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mutex_);
        ++menuSessionId_;
        receiverId_ = 0;
        items_.clear();
    }

    void GiftMenuEventSink::FinalizeGiftSession(std::uint64_t sessionId) {
        RE::FormID receiverId = 0;
        std::vector<Domain::Relationships::GiftedItemEntry> items;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessionId != menuSessionId_) {
                return;
            }
            ++menuSessionId_;
            giftMenuOpen_.store(false, std::memory_order_release);
            receiverId = receiverId_;
            items = std::move(items_);
            receiverId_ = 0;
            items_.clear();
        }

        if (receiverId != 0 && !items.empty() && HasPhysicalActor(receiverId)) {
            const std::string playerName = ActorSvc::GetName(kPlayerFormID);
            const std::string receiverName = ActorSvc::GetName(receiverId);
            if (auto* hooks = Engine::GetEngineEventHooks()) {
                hooks->OnPlayerGiftCompleted(receiverId, playerName, receiverName, items);
            }
        }
    }

    RE::BSEventNotifyControl GiftMenuEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event || a_event->menuName != RE::GiftMenu::MENU_NAME) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->opening) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++menuSessionId_;
            receiverId_ = 0;
            items_.clear();
            giftMenuOpen_.store(true, std::memory_order_release);
            return RE::BSEventNotifyControl::kContinue;
        }

        std::uint64_t closingSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            giftMenuOpen_.store(false, std::memory_order_release);
            closingSessionId = menuSessionId_;
        }

        if (!AddCurrentEpochTask(
                "GiftMenuEventSink::FinalizeGiftSession",
                [this, closingSessionId]() { FinalizeGiftSession(closingSessionId); }) &&
            DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            FinalizeGiftSession(closingSessionId);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void GiftMenuEventSink::RecordTransferredItemOnGameThread(
        std::uint64_t sessionId,
        RE::FormID destId,
        RE::FormID baseObjId,
        std::int32_t itemCount) {
        if (destId == 0 || baseObjId == 0 || itemCount <= 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessionId != menuSessionId_) {
                return;
            }
        }

        auto* destActor = RE::TESForm::LookupByID<RE::Actor>(destId);
        auto* baseForm = RE::TESForm::LookupByID(baseObjId);
        if (!destActor || destActor->IsDeleted() || !baseForm) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* childRec = (snap && snap->lineage)
                                   ? snap->lineage->FindChild(State::ChildId{ destId })
                                   : nullptr;
        const bool isPlayerChild =
            childRec != nullptr &&
            childRec->stage == Foundation::ChildStage::Child &&
            childRec->guardians.HasGuardian(State::ActorId::Player());

        if (isPlayerChild) {
            if (auto* childLogicQuest = FormCache::GetSingleton().GetChildLogicQuest()) {
                if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
                    auto* policy = vm->GetObjectHandlePolicy();
                    const RE::VMHandle handle = policy
                        ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, childLogicQuest)
                        : 0;
                    if (policy && handle != 0 && handle != policy->EmptyHandle()) {
                        RE::Actor* childArg = destActor;
                        RE::TESForm* itemArg = baseForm;
                        std::int32_t countArg = itemCount;
                        auto* args = RE::MakeFunctionArguments(
                            std::move(childArg), std::move(itemArg), std::move(countArg));
                        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> emptyCallback;
                        if (!vm->DispatchMethodCall2(
                                handle,
                                RE::BSFixedString("TTM_ServiceChildrenLogic"),
                                RE::BSFixedString("ChildReceiveGiftItem"),
                                args,
                                emptyCallback)) {
                            MARAS_LOG_WARN(
                                "GiftMenuEventSink: Failed to dispatch ChildReceiveGiftItem for child {:08X}",
                                destId);
                        }
                    }
                }
            }
            return;
        }

        if (!snap || !Domain::Relationships::HasTrackedPlayerStatus(*snap, Foundation::ActorId{ destId })) {
            return;
        }

        const char* rawName = baseForm->GetName();
        std::string itemName = (rawName && rawName[0] != '\0') ? std::string(rawName) : std::string{};
        const std::int32_t unitGold = baseForm->GetGoldValue();
        if (itemName.empty()) {
            if (unitGold <= 0) {
                return;
            }
            itemName = "Gift";
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessionId != menuSessionId_) {
                return;
            }
            if (receiverId_ != 0 && receiverId_ != destId) {
                items_.clear();
            }
            receiverId_ = destId;
            items_.push_back(Domain::Relationships::GiftedItemEntry{
                .itemName = std::move(itemName),
                .count = itemCount,
                .unitGoldValue = unitGold,
            });
        }

        MARAS_LOG_TRACE("GiftMenuEventSink: Recorded gift item {:08X} x{} (gold={}) to {:08X}",
                        baseObjId, itemCount, unitGold, destId);
    }

    RE::BSEventNotifyControl GiftMenuEventSink::ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
        if (!giftMenuOpen_.load(std::memory_order_acquire)) {
            return RE::BSEventNotifyControl::kContinue;
        }
        if (!a_event ||
            a_event->oldContainer != kPlayerFormID ||
            a_event->newContainer == 0 ||
            a_event->newContainer == kPlayerFormID ||
            a_event->itemCount <= 0 ||
            a_event->baseObj == 0) {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::uint64_t activeSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!giftMenuOpen_.load(std::memory_order_acquire)) {
                return RE::BSEventNotifyControl::kContinue;
            }
            activeSessionId = menuSessionId_;
        }

        const RE::FormID destId = a_event->newContainer;
        const RE::FormID baseObjId = a_event->baseObj;
        const std::int32_t itemCount = a_event->itemCount;
        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            RecordTransferredItemOnGameThread(activeSessionId, destId, baseObjId, itemCount);
        } else {
            AddCurrentEpochTask(
                "GiftMenuEventSink::RecordTransferredItem",
                [this, activeSessionId, destId, baseObjId, itemCount]() {
                    RecordTransferredItemOnGameThread(activeSessionId, destId, baseObjId, itemCount);
                });
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
