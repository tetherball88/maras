#include "maras/engine/TaskDispatcher.h"

#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"

#include <atomic>
#include <vector>

namespace MARAS {

    namespace {
        struct PostLockTask {
            SessionEpoch expectedEpoch{ 0 };
            const char* contextTag{ "PostLock" };
            std::function<void()> fn;
        };

        thread_local std::uint32_t s_mainThreadTaskDepth = 0;
        thread_local std::uint32_t s_syncedVmCallDepth = 0;
        thread_local std::uint32_t s_papyrusLockDepth = 0;
        [[nodiscard]] std::vector<PostLockTask>& PostLockOutbox() noexcept {
            thread_local std::vector<PostLockTask> s_outbox;
            return s_outbox;
        }

        constinit std::atomic<void (*)() noexcept> g_pluginEventSinksResetter{ nullptr };

        void RunPostLockItem(PostLockTask& item) noexcept {
            const char* tag = item.contextTag ? item.contextTag : "PostLock";
            try {
                if (!IsSessionEpochCurrent(item.expectedEpoch)) {
                    MARAS_LOG_DEBUG("[TaskDispatcher] Dropped stale post-lock '{}' task (expected={}, current={})",
                                    tag,
                                    item.expectedEpoch,
                                    GetSessionEpoch());
                    return;
                }
                if (!item.fn) {
                    return;
                }

                if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                    item.fn();
                    return;
                }

                AddEpochTask(item.expectedEpoch, tag, std::move(item.fn));
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[TaskDispatcher] Exception in post-lock '{}': {}", tag, e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[TaskDispatcher] Unknown exception in post-lock '{}'", tag);
            }
        }
    }  // namespace

    void RegisterPluginEventSinksResetter(void (*resetter)() noexcept) noexcept {
        g_pluginEventSinksResetter.store(resetter, std::memory_order_release);
    }

    void ResetPluginEventSinks() noexcept {
        if (auto* resetter = g_pluginEventSinksResetter.load(std::memory_order_acquire)) {
            resetter();
        }
    }

    PapyrusDomainMutex& GetPapyrusMutex() noexcept {
        static PapyrusDomainMutex mtx;
        return mtx;
    }

    ScopedMainThreadTask::ScopedMainThreadTask() noexcept {
        ++s_mainThreadTaskDepth;
    }

    ScopedMainThreadTask::~ScopedMainThreadTask() noexcept {
        if (s_mainThreadTaskDepth > 0) {
            --s_mainThreadTaskDepth;
        }
    }

    bool IsInMainThreadTask() noexcept {
        return s_mainThreadTaskDepth > 0;
    }

    ScopedSyncedVmCall::ScopedSyncedVmCall() noexcept {
        ++s_syncedVmCallDepth;
    }

    ScopedSyncedVmCall::~ScopedSyncedVmCall() noexcept {
        if (s_syncedVmCallDepth > 0) {
            --s_syncedVmCallDepth;
        }
    }

    bool IsInSyncedVmCall() noexcept {
        return s_syncedVmCallDepth > 0;
    }

    void EnterPapyrusDomainLock() noexcept {
        ++s_papyrusLockDepth;
    }

    void ExitPapyrusDomainLockAndFlush() noexcept {
        if (s_papyrusLockDepth == 0) {
            return;
        }
        --s_papyrusLockDepth;
        auto& outbox = PostLockOutbox();
        if (s_papyrusLockDepth > 0 || outbox.empty()) {
            return;
        }

        auto queued = std::move(outbox);
        outbox.clear();
        for (auto& item : queued) {
            RunPostLockItem(item);
        }
    }

    bool IsPapyrusDomainLockedOnCurrentThread() noexcept {
        return s_papyrusLockDepth > 0;
    }

    void DispatchPostLock(SessionEpoch expectedEpoch, std::function<void()> fn, const char* contextTag) {
        if (!fn || !IsSessionEpochCurrent(expectedEpoch)) {
            return;
        }

        if (IsPapyrusDomainLockedOnCurrentThread()) {
            PostLockOutbox().push_back(PostLockTask{ expectedEpoch, contextTag, std::move(fn) });
            return;
        }

        if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            try {
                fn();
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[TaskDispatcher] Exception in immediate '{}': {}",
                                contextTag ? contextTag : "PostLock", e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[TaskDispatcher] Unknown exception in immediate '{}'",
                                contextTag ? contextTag : "PostLock");
            }
            return;
        }

        AddEpochTask(expectedEpoch, contextTag ? contextTag : "PostLock", std::move(fn));
    }

    void DispatchPostLock(std::function<void()> fn, const char* contextTag) {
        DispatchPostLock(GetSessionEpoch(), std::move(fn), contextTag);
    }

    void EmitModCallbackEvent(std::string eventName,
                              std::string strArg,
                              float numArg,
                              RE::FormID senderFormID) {
        const SessionEpoch expectedEpoch = GetSessionEpoch();
        DispatchPostLock(
            expectedEpoch,
            [eventName = std::move(eventName), strArg = std::move(strArg), numArg, senderFormID]() mutable {
                auto* src = SKSE::GetModCallbackEventSource();
                if (!src) {
                    MARAS_LOG_ERROR("[TaskDispatcher] ModCallbackEventSource unavailable for '{}'", eventName);
                    return;
                }
                RE::TESForm* sender = senderFormID != 0 ? RE::TESForm::LookupByID(senderFormID) : nullptr;
                SKSE::ModCallbackEvent ev{
                    RE::BSFixedString(eventName.c_str()),
                    RE::BSFixedString(strArg.c_str()),
                    numArg,
                    sender
                };
                src->SendEvent(&ev);
            },
            "EmitModCallbackEvent");
    }

    void QueueHUDMessage(std::string msg) {
        if (msg.empty()) {
            return;
        }
        const SessionEpoch expectedEpoch = GetSessionEpoch();
        DispatchPostLock(
            expectedEpoch,
            [expectedEpoch, msg = std::move(msg)]() mutable {
                if (DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                    RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
                    return;
                }
                AddEpochTask(expectedEpoch, "QueueHUDMessage", [msg = std::move(msg)]() {
                    RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
                });
            },
            "QueueHUDMessage");
    }

}  // namespace MARAS

namespace MARAS::Engine {

    namespace {
        constinit std::atomic<IEngineEventHooks*> g_engineEventHooks{ nullptr };
    }

    void SetEngineEventHooks(IEngineEventHooks* hooks) noexcept {
        g_engineEventHooks.store(hooks, std::memory_order_release);
    }

    IEngineEventHooks* GetEngineEventHooks() noexcept {
        return g_engineEventHooks.load(std::memory_order_acquire);
    }

}  // namespace MARAS::Engine
