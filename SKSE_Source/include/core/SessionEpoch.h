#pragma once

#include "utils/Common.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <string_view>
#include <utility>

namespace MARAS {

    using SessionEpoch = std::uint64_t;

    // Monotonic session generation counter incremented on every save load, revert, or new game.
    [[nodiscard]] SessionEpoch GetSessionEpoch() noexcept;
    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept;

    [[nodiscard]] inline bool IsSessionEpochCurrent(SessionEpoch expectedEpoch) noexcept {
        return expectedEpoch != 0 && GetSessionEpoch() == expectedEpoch;
    }

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

}  // namespace MARAS
