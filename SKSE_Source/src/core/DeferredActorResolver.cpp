#include "core/DeferredActorResolver.h"

#include "core/FactionReconciler.h"
#include "core/PackageReconciler.h"
#include "core/SessionEpoch.h"
#include "papyrus/PapyrusInterface.h"

#include <REX/W32/KERNEL32.h>
#include <utility>
#include <vector>

namespace MARAS {

    DeferredActorResolver& DeferredActorResolver::GetSingleton() {
        static DeferredActorResolver instance;
        return instance;
    }

    void DeferredActorResolver::CaptureMainThread() noexcept {
        const auto currentId = std::this_thread::get_id();
        const std::uint32_t osTid = REX::W32::GetCurrentThreadId();
        const bool wasCaptured = hasMainThread_.load(std::memory_order_acquire);
        const std::uint32_t prevOsTid = mainOsThreadId_.load(std::memory_order_acquire);
        if (!wasCaptured || prevOsTid != osTid ||
            mainThreadId_.load(std::memory_order_acquire) != currentId) {
            mainThreadId_.store(currentId, std::memory_order_release);
            mainOsThreadId_.store(osTid, std::memory_order_release);
            hasMainThread_.store(true, std::memory_order_release);
            if (!wasCaptured || prevOsTid != osTid) {
                MARAS_LOG_INFO("[DeferredActorResolver] Captured main game thread (OS TID={})", osTid);
            }
        }
    }

    bool DeferredActorResolver::IsOnMainThread() const noexcept {
        if (IsInMainThreadTask()) {
            return true;
        }
        if (!hasMainThread_.load(std::memory_order_acquire)) {
            return false;
        }
        return std::this_thread::get_id() == mainThreadId_.load(std::memory_order_acquire);
    }

    bool DeferredActorResolver::CanSyncEngineInline() const noexcept {
        return IsOnMainThread() || IsInSyncedVmCall();
    }

    void DeferredActorResolver::RequestSync(RE::FormID actorId, SyncFlags flags) {
        const std::uint8_t mask = static_cast<std::uint8_t>(flags);
        if (actorId == 0 || mask == 0) {
            return;
        }

        bool shouldSchedule = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingSync_[actorId] |= mask;
            if (!drainScheduled_) {
                drainScheduled_ = true;
                shouldSchedule = true;
            }
        }

        if (!shouldSchedule) {
            return;
        }

        const SessionEpoch epoch = GetSessionEpoch();
        if (!AddEpochTask(epoch, "DeferredActorResolver::DrainPending", []() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                DeferredActorResolver::GetSingleton().DrainPending();
            })) {
            std::lock_guard<std::mutex> lock(mutex_);
            drainScheduled_ = false;
        }
    }

    void DeferredActorResolver::MarkUnloaded(RE::FormID actorId, SyncFlags flags) {
        const std::uint8_t mask = static_cast<std::uint8_t>(flags);
        if (actorId == 0 || mask == 0) {
            return;
        }

        bool insertedNew = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto [it, inserted] = unloadedActors_.emplace(actorId, mask);
            if (!inserted) {
                it->second |= mask;
            }
            insertedNew = inserted;
        }

        if (insertedNew) {
            MARAS_LOG_DEBUG("[DeferredActorResolver] Deferred unresolved actor {:08X} (flags=0x{:X})",
                            actorId, mask);
        }
    }

    void DeferredActorResolver::OnActorLoaded(RE::Actor* actor) {
        if (!actor || actor->IsDeleted()) {
            return;
        }

        const RE::FormID actorId = actor->GetFormID();
        if (actorId == 0) {
            return;
        }

        std::uint8_t mask = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = unloadedActors_.find(actorId); it != unloadedActors_.end()) {
                mask |= it->second;
                unloadedActors_.erase(it);
            }
            if (auto it = pendingSync_.find(actorId); it != pendingSync_.end()) {
                mask |= it->second;
                pendingSync_.erase(it);
            }
        }

        if (mask == 0) {
            return;
        }

        MARAS_LOG_DEBUG("[DeferredActorResolver] Reconciling newly loaded actor {} ({:08X}, flags=0x{:X})",
                        actor->GetName() ? actor->GetName() : "Unknown", actorId, mask);
        ExecuteSync(actor, mask);
    }

    void DeferredActorResolver::Revert() {
        std::size_t clearedPending = 0;
        std::size_t clearedUnloaded = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clearedPending = pendingSync_.size();
            clearedUnloaded = unloadedActors_.size();
            pendingSync_.clear();
            unloadedActors_.clear();
            drainScheduled_ = false;
        }
        if (clearedPending > 0 || clearedUnloaded > 0) {
            MARAS_LOG_DEBUG("[DeferredActorResolver] Revert cleared {} pending and {} unloaded actor entries",
                            clearedPending, clearedUnloaded);
        }
    }

    std::size_t DeferredActorResolver::GetUnloadedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return unloadedActors_.size();
    }

    std::size_t DeferredActorResolver::GetPendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pendingSync_.size();
    }

    void DeferredActorResolver::DrainPending() {
        std::vector<std::pair<RE::FormID, std::uint8_t>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            drainScheduled_ = false;
            batch.reserve(pendingSync_.size());
            for (const auto& [actorId, mask] : pendingSync_) {
                batch.emplace_back(actorId, mask);
            }
            pendingSync_.clear();
        }

        if (batch.empty()) {
            return;
        }

        MARAS_LOG_DEBUG("[DeferredActorResolver] Draining {} queued actor sync request(s) on main thread",
                        batch.size());
        for (const auto& [actorId, mask] : batch) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor) {
                MarkUnloaded(actorId, static_cast<SyncFlags>(mask));
                continue;
            }
            if (actor->IsDeleted()) {
                continue;
            }
            ExecuteSync(actor, mask);
        }
    }

    void DeferredActorResolver::ExecuteSync(RE::Actor* actor, std::uint8_t flagMask) {
        if (!actor || flagMask == 0) {
            return;
        }
        if (HasSyncFlag(flagMask, SyncFlags::Factions)) {
            FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
        }
        if (HasSyncFlag(flagMask, SyncFlags::Package)) {
            PackageReconciler::GetSingleton().ReconcileLoadedActor(actor);
        }
    }

}  // namespace MARAS
