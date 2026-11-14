#include "maras/state/SessionEpoch.h"

#include "maras/foundation/Logging.h"

#include <atomic>

namespace MARAS::State {

    namespace {
        std::atomic<SessionEpoch> g_sessionEpoch{ 1 };
    }

    SessionEpoch GetSessionEpoch() noexcept {
        return g_sessionEpoch.load(std::memory_order_acquire);
    }

    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept {
        const SessionEpoch next = g_sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        Foundation::LogInfo("[SessionEpoch] Advanced session epoch to {} (reason: {})", next, reason);
        return next;
    }

}  // namespace MARAS::State
