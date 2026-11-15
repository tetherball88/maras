#include "maras/state/SessionEpoch.h"

#include "maras/foundation/Logging.h"

#include <atomic>

namespace MARAS::State {

    namespace {
        std::atomic<SessionEpoch> g_sessionEpoch{ 1 };
        std::atomic<bool> g_legacyMigrationActive{ false };
    }

    SessionEpoch GetSessionEpoch() noexcept {
        return g_sessionEpoch.load(std::memory_order_acquire);
    }

    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept {
        g_legacyMigrationActive.store(false, std::memory_order_release);
        const SessionEpoch next = g_sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        Foundation::LogInfo("[SessionEpoch] Advanced session epoch to {} (reason: {})", next, reason);
        return next;
    }

    void SetLegacyMigrationActive(bool active) noexcept {
        g_legacyMigrationActive.store(active, std::memory_order_release);
    }

    bool IsLegacyMigrationActive() noexcept {
        return g_legacyMigrationActive.load(std::memory_order_acquire);
    }

}  // namespace MARAS::State
