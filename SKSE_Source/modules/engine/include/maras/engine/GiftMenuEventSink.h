#pragma once

#include "maras/domain/relationships/GiftAndIntimacyRules.h"
#include "maras/engine/EngineLogging.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MARAS {

    /**
     * Native GiftMenu + ContainerChanged tracker replacing TTM_ServiceGift.psc,
     * TTM_PlayerScript.OnItemRemoved, TTM_MainController OnMenuOpen/OnMenuClose("GiftMenu"),
     * and the 4 StorageUtil "Gift.*" keys.
     */
    class GiftMenuEventSink final
        : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        , public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    public:
        static GiftMenuEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent* a_event,
            RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

        void Revert();

    private:
        void RecordTransferredItemOnGameThread(
            std::uint64_t sessionId,
            RE::FormID destId,
            RE::FormID baseObjId,
            std::int32_t itemCount);
        void FinalizeGiftSession(std::uint64_t sessionId);

        GiftMenuEventSink() = default;
        ~GiftMenuEventSink() override = default;
        GiftMenuEventSink(const GiftMenuEventSink&) = delete;
        GiftMenuEventSink(GiftMenuEventSink&&) = delete;
        GiftMenuEventSink& operator=(const GiftMenuEventSink&) = delete;
        GiftMenuEventSink& operator=(GiftMenuEventSink&&) = delete;

        std::atomic<bool> giftMenuOpen_{ false };
        std::mutex mutex_;
        std::uint64_t menuSessionId_{ 0 };
        RE::FormID receiverId_{ 0 };
        std::vector<Domain::Relationships::GiftedItemEntry> items_;
    };

}  // namespace MARAS
