#pragma once

#include "utils/Common.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace MARAS {

    using SessionEpoch = std::uint64_t;

    // Monotonic session generation counter incremented on every save load, revert, or new game.
    [[nodiscard]] SessionEpoch GetSessionEpoch() noexcept;
    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept;

    // Resets plugin-level input and actor init/3D load event sinks across save/revert boundaries.
    void ResetPluginEventSinks() noexcept;

    [[nodiscard]] inline bool IsSessionEpochCurrent(SessionEpoch expectedEpoch) noexcept {
        return expectedEpoch != 0 && GetSessionEpoch() == expectedEpoch;
    }

    // RAII scope marking execution inside an SKSE::TaskInterface::AddTask main-thread callback.
    class ScopedMainThreadTask {
    public:
        ScopedMainThreadTask() noexcept;
        ~ScopedMainThreadTask() noexcept;
        ScopedMainThreadTask(const ScopedMainThreadTask&) = delete;
        ScopedMainThreadTask& operator=(const ScopedMainThreadTask&) = delete;
    };

    [[nodiscard]] bool IsInMainThreadTask() noexcept;

    // RAII scope marking execution inside a Papyrus native wrapper synced via PapyrusDomainMutex.
    // Distinct from ScopedMainThreadTask so IsOnMainThread() stays strictly WinMain + BSTaskPool
    // while engine reconcilers use CanSyncEngineInline().
    class ScopedSyncedVmCall {
    public:
        ScopedSyncedVmCall() noexcept;
        ~ScopedSyncedVmCall() noexcept;
        ScopedSyncedVmCall(const ScopedSyncedVmCall&) = delete;
        ScopedSyncedVmCall& operator=(const ScopedSyncedVmCall&) = delete;
    };

    [[nodiscard]] bool IsInSyncedVmCall() noexcept;

    // Thread-local Papyrus domain lock tracking + post-lock outbox flush to avoid
    // holding GetPapyrusMutex() across SKSE ModCallbackEvents, VM latent returns, or engine calls.
    void EnterPapyrusDomainLock() noexcept;
    void ExitPapyrusDomainLockAndFlush() noexcept;
    [[nodiscard]] bool IsPapyrusDomainLockedOnCurrentThread() noexcept;

    void DispatchPostLock(SessionEpoch expectedEpoch,
                          std::function<void()> fn,
                          const char* contextTag = "PostLock");
    void DispatchPostLock(std::function<void()> fn,
                          const char* contextTag = "PostLock");

    void EmitModCallbackEvent(std::string eventName,
                              std::string strArg = {},
                              float numArg = 0.0f,
                              RE::FormID senderID = 0);
    inline void EmitModCallbackEvent(std::string eventName,
                                     std::string strArg,
                                     float numArg,
                                     RE::TESForm* sender) {
        EmitModCallbackEvent(std::move(eventName),
                             std::move(strArg),
                             numArg,
                             sender ? sender->GetFormID() : 0);
    }

    void QueueHUDMessage(std::string msg);

    // Schedules a task on the SKSE main game thread bound to `expectedEpoch`.
    // Automatically drops the task if the save session transitioned before execution,
    // and wraps execution in try/catch per SKSE_Source/AGENTS.md §6.
    // Note: `contextTag` must be a static string literal (const char*) so no temporary lifetime hazard exists.
    template <typename Fn>
    bool AddEpochTask(SessionEpoch expectedEpoch, const char* contextTag, Fn&& fn) {
        const char* tag = contextTag ? contextTag : "unnamed";
        if (!IsSessionEpochCurrent(expectedEpoch)) {
            MARAS_LOG_DEBUG("[SessionEpoch] Dropping pre-stale task '{}' (expected={}, current={})",
                            tag, expectedEpoch, GetSessionEpoch());
            return false;
        }
        const auto* taskIF = SKSE::GetTaskInterface();
        if (!taskIF) {
            MARAS_LOG_ERROR("[SessionEpoch] SKSE TaskInterface unavailable for '{}'", tag);
            return false;
        }
        taskIF->AddTask([expectedEpoch, tag, task = std::forward<Fn>(fn)]() mutable {
            ScopedMainThreadTask mainScope;
            const SessionEpoch currentEpoch = GetSessionEpoch();
            if (currentEpoch != expectedEpoch) {
                MARAS_LOG_DEBUG("[SessionEpoch] Dropped stale main-thread task '{}' across save boundary (expected={}, current={})",
                                tag, expectedEpoch, currentEpoch);
                return;
            }
            try {
                task();
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SessionEpoch] Unhandled exception in main-thread task '{}': {}", tag, e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[SessionEpoch] Unknown exception in main-thread task '{}'", tag);
            }
        });
        return true;
    }

    template <typename Fn>
    bool AddCurrentEpochTask(const char* contextTag, Fn&& fn) {
        return AddEpochTask(GetSessionEpoch(), contextTag, std::forward<Fn>(fn));
    }

    template <class LatentR, class Val>
    void DispatchLatentResult(SessionEpoch expectedEpoch,
                              RE::BSScript::Internal::VirtualMachine* vm,
                              RE::VMStackID stackID,
                              Val&& val,
                              const char* contextTag = "LatentResult") {
        if (!vm || stackID == 0) {
            return;
        }
        DispatchPostLock(
            expectedEpoch,
            [vm, stackID, result = std::forward<Val>(val)]() mutable {
                vm->ReturnLatentResult<LatentR>(stackID, std::move(result));
            },
            contextTag);
    }

    template <class FormT>
    void DispatchLatentFormResult(SessionEpoch expectedEpoch,
                                  RE::BSScript::Internal::VirtualMachine* vm,
                                  RE::VMStackID stackID,
                                  RE::FormID formID,
                                  const char* contextTag = "LatentFormResult") {
        if (!vm || stackID == 0) {
            return;
        }
        DispatchPostLock(
            expectedEpoch,
            [vm, stackID, formID]() {
                FormT* resolved = formID != 0 ? RE::TESForm::LookupByID<FormT>(formID) : nullptr;
                vm->ReturnLatentResult<FormT*>(stackID, resolved);
            },
            contextTag);
    }

}  // namespace MARAS
