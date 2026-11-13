#include "maras/foundation/Logging.h"

#include <atomic>

namespace MARAS::Foundation {

    namespace {
        std::atomic<LogSinkFn> g_logSink{ nullptr };
    }

    void SetLogSink(LogSinkFn sink) noexcept {
        g_logSink.store(sink, std::memory_order_release);
    }

    LogSinkFn GetLogSink() noexcept {
        return g_logSink.load(std::memory_order_acquire);
    }

    void EmitLog(LogSeverity severity, std::string_view message) noexcept {
        if (const LogSinkFn sink = g_logSink.load(std::memory_order_acquire)) {
            sink(severity, message);
        }
    }

}  // namespace MARAS::Foundation
