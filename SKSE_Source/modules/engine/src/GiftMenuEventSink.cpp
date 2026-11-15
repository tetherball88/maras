#include "maras/engine/GiftMenuEventSink.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/FormCache.h"
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
        receiverId_ = 0;
        items_.clear();
    }

    RE::BSEventNotifyControl GiftMenuEventSink::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event || a_event->menuName != RE::GiftMenu::MENU_NAME) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->opening) {
            std::lock_guard<std::mutex> lock(mutex_);
            receiverId_ = 0;
            items_.clear();
            giftMenuOpen_.store(true, std::memory_order_release);
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::FormID receiverId = 0;
        std::vector<Domain::Relationships::GiftedItemEntry> items;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            giftMenuOpen_.store(false, std::memory_order_release);
            receiverId = receiverId_;
            items = std::move(items_);
            receiverId_ = 0;
            items_.clear();
        }

        if (receiverId != 0 && !items.empty()) {
            const std::string playerName = ActorSvc::GetName(kPlayerFormID);
            const std::string receiverName = ActorSvc::GetName(receiverId);
            if (auto* hooks = Engine::GetEngineEventHooks()) {
                hooks->OnPlayerGiftCompleted(receiverId, playerName, receiverName, items);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
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

        auto* destActor = RE::TESForm::LookupByID<RE::Actor>(a_event->newContainer);
        auto* baseForm = RE::TESForm::LookupByID(a_event->baseObj);
        if (!destActor || destActor->IsDeleted() || !baseForm) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& fc = FormCache::GetSingleton();
        if (auto* childFac = fc.GetPlayerChildFaction(); childFac && destActor->IsInFaction(childFac)) {
            if (auto* childLogicQuest = fc.GetChildLogicQuest()) {
                if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
                    auto* policy = vm->GetObjectHandlePolicy();
                    const RE::VMHandle handle = policy
                        ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, childLogicQuest)
                        : 0;
                    if (policy && handle != 0 && handle != policy->EmptyHandle()) {
                        RE::Actor* childArg = destActor;
                        RE::TESForm* itemArg = baseForm;
                        std::int32_t countArg = a_event->itemCount;
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
                                destActor->GetFormID());
                        }
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID destId = destActor->GetFormID();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !Domain::Relationships::HasTrackedPlayerStatus(*snap, Foundation::ActorId{ destId })) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const char* rawName = baseForm->GetName();
        std::string itemName = (rawName && rawName[0] != '\0') ? std::string(rawName) : std::string{};
        if (itemName.empty()) {
            return RE::BSEventNotifyControl::kContinue;
        }
        const std::int32_t unitGold = baseForm->GetGoldValue();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!giftMenuOpen_.load(std::memory_order_acquire)) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (receiverId_ != 0 && receiverId_ != destId) {
                items_.clear();
            }
            receiverId_ = destId;
            items_.push_back(Domain::Relationships::GiftedItemEntry{
                .itemName = std::move(itemName),
                .count = a_event->itemCount,
                .unitGoldValue = unitGold,
            });
        }

        MARAS_LOG_TRACE("GiftMenuEventSink: Recorded gift item {:08X} x{} (gold={}) to {:08X}",
                        a_event->baseObj, a_event->itemCount, unitGold, destId);
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS
