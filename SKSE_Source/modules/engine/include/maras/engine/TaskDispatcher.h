#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "maras/engine/EngineLogging.h"
#include "maras/state/SessionEpoch.h"

#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace MARAS {

    using SessionEpoch = State::SessionEpoch;
    using State::BumpSessionEpoch;
    using State::GetSessionEpoch;
    using State::IsSessionEpochCurrent;

    void RegisterPluginEventSinksResetter(void (*resetter)() noexcept) noexcept;
    void ResetPluginEventSinks() noexcept;

    class ScopedMainThreadTask {
    public:
        ScopedMainThreadTask() noexcept;
        ~ScopedMainThreadTask() noexcept;
        ScopedMainThreadTask(const ScopedMainThreadTask&) = delete;
        ScopedMainThreadTask& operator=(const ScopedMainThreadTask&) = delete;
    };

    [[nodiscard]] bool IsInMainThreadTask() noexcept;

    class ScopedSyncedVmCall {
    public:
        ScopedSyncedVmCall() noexcept;
        ~ScopedSyncedVmCall() noexcept;
        ScopedSyncedVmCall(const ScopedSyncedVmCall&) = delete;
        ScopedSyncedVmCall& operator=(const ScopedSyncedVmCall&) = delete;
    };

    [[nodiscard]] bool IsInSyncedVmCall() noexcept;

    void EnterPapyrusDomainLock() noexcept;
    void ExitPapyrusDomainLockAndFlush() noexcept;
    [[nodiscard]] bool IsPapyrusDomainLockedOnCurrentThread() noexcept;

    class PapyrusDomainMutex {
    public:
        void lock() {
            mtx_.lock();
            EnterPapyrusDomainLock();
        }

        bool try_lock() {
            if (!mtx_.try_lock()) {
                return false;
            }
            EnterPapyrusDomainLock();
            return true;
        }

        void unlock() noexcept {
            mtx_.unlock();
            ExitPapyrusDomainLockAndFlush();
        }

    private:
        std::recursive_mutex mtx_;
    };

    [[nodiscard]] PapyrusDomainMutex& GetPapyrusMutex() noexcept;

    void DispatchPostLock(SessionEpoch expectedEpoch,
                          std::function<void()> fn,
                          const char* contextTag = "PostLock");

    void DispatchPostLock(std::function<void()> fn,
                          const char* contextTag = "PostLock");

    void EmitModCallbackEvent(std::string eventName,
                              std::string strArg,
                              float numArg,
                              RE::FormID senderFormID = 0);

    inline void EmitModCallbackEvent(std::string eventName,
                                     std::string strArg,
                                     float numArg,
                                     RE::TESForm* sender) {
        EmitModCallbackEvent(std::move(eventName),
                             std::move(strArg),
                             numArg,
                             sender ? sender->GetFormID() : RE::FormID{ 0 });
    }

    void QueueHUDMessage(std::string msg);

    template <class Fn>
    bool AddEpochTask(SessionEpoch expectedEpoch, const char* contextTag, Fn&& fn) {
        if (!IsSessionEpochCurrent(expectedEpoch)) {
            MARAS_LOG_DEBUG("[TaskDispatcher] Dropped stale '{}' task before queue (expected={}, current={})",
                            contextTag ? contextTag : "Task", expectedEpoch, GetSessionEpoch());
            return false;
        }

        auto* taskIf = SKSE::GetTaskInterface();
        if (!taskIf) {
            MARAS_LOG_ERROR("[TaskDispatcher] SKSE::GetTaskInterface() unavailable for '{}'",
                            contextTag ? contextTag : "Task");
            return false;
        }

        std::string tagStr = contextTag ? contextTag : "Task";
        taskIf->AddTask([expectedEpoch, tagStr = std::move(tagStr), fn = std::forward<Fn>(fn)]() mutable {
            ScopedMainThreadTask mainScope;
            if (!IsSessionEpochCurrent(expectedEpoch)) {
                MARAS_LOG_DEBUG("[TaskDispatcher] Dropped stale '{}' task on execution (expected={}, current={})",
                                tagStr, expectedEpoch, GetSessionEpoch());
                return;
            }
            try {
                fn();
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[TaskDispatcher] Unhandled exception in main-thread task '{}': {}",
                                tagStr, e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[TaskDispatcher] Unhandled unknown exception in main-thread task '{}'",
                                tagStr);
            }
        });
        return true;
    }

    template <class Fn>
    bool AddCurrentEpochTask(const char* contextTag, Fn&& fn) {
        return AddEpochTask(GetSessionEpoch(), contextTag, std::forward<Fn>(fn));
    }

    template <class R>
    void DispatchLatentResult(SessionEpoch expectedEpoch,
                              RE::BSScript::Internal::VirtualMachine* vm,
                              RE::VMStackID stackID,
                              R value,
                              const char* contextTag = "LatentResult") {
        if (!vm || stackID == 0) {
            return;
        }
        // Always queue through AddEpochTask so vm->ReturnLatentResult never runs synchronously
        // before LockedLatentWrapper::Call returns LatentStatus::kStarted to the Papyrus VM.
        AddEpochTask(
            expectedEpoch,
            contextTag ? contextTag : "LatentResult",
            [vm, stackID, value = std::move(value)]() mutable {
                vm->ReturnLatentResult<R>(stackID, std::move(value));
            });
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
        // Always queue through AddEpochTask so vm->ReturnLatentResult never runs synchronously
        // before LockedLatentWrapper::Call returns LatentStatus::kStarted to the Papyrus VM.
        AddEpochTask(
            expectedEpoch,
            contextTag ? contextTag : "LatentFormResult",
            [vm, stackID, formID]() {
                FormT* resolved = formID != 0 ? RE::TESForm::LookupByID<FormT>(formID) : nullptr;
                vm->ReturnLatentResult<FormT*>(stackID, resolved);
            });
    }

}  // namespace MARAS
