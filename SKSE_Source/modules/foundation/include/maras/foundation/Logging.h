#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace MARAS::Foundation {

    enum class LogSeverity : std::uint8_t {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4
    };

    using LogSinkFn = void (*)(LogSeverity severity, std::string_view message) noexcept;

    void SetLogSink(LogSinkFn sink) noexcept;
    [[nodiscard]] LogSinkFn GetLogSink() noexcept;
    void EmitLog(LogSeverity severity, std::string_view message) noexcept;

    template <typename... Args>
    void LogTrace(std::format_string<Args...> fmtStr, Args&&... args) noexcept {
        if (GetLogSink() != nullptr) {
            try {
                EmitLog(LogSeverity::Trace, std::format(fmtStr, std::forward<Args>(args)...));
            } catch (...) {
            }
        }
    }

    template <typename... Args>
    void LogDebug(std::format_string<Args...> fmtStr, Args&&... args) noexcept {
        if (GetLogSink() != nullptr) {
            try {
                EmitLog(LogSeverity::Debug, std::format(fmtStr, std::forward<Args>(args)...));
            } catch (...) {
            }
        }
    }

    template <typename... Args>
    void LogInfo(std::format_string<Args...> fmtStr, Args&&... args) noexcept {
        if (GetLogSink() != nullptr) {
            try {
                EmitLog(LogSeverity::Info, std::format(fmtStr, std::forward<Args>(args)...));
            } catch (...) {
            }
        }
    }

    template <typename... Args>
    void LogWarn(std::format_string<Args...> fmtStr, Args&&... args) noexcept {
        if (GetLogSink() != nullptr) {
            try {
                EmitLog(LogSeverity::Warn, std::format(fmtStr, std::forward<Args>(args)...));
            } catch (...) {
            }
        }
    }

    template <typename... Args>
    void LogError(std::format_string<Args...> fmtStr, Args&&... args) noexcept {
        if (GetLogSink() != nullptr) {
            try {
                EmitLog(LogSeverity::Error, std::format(fmtStr, std::forward<Args>(args)...));
            } catch (...) {
            }
        }
    }

}  // namespace MARAS::Foundation
