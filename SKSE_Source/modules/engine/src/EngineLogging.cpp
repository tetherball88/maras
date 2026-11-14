#include "maras/engine/EngineLogging.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <filesystem>

namespace MARAS {

    std::shared_ptr<spdlog::logger> g_Logger;

    namespace {
        void ForwardFoundationLog(Foundation::LogSeverity severity, std::string_view message) noexcept {
            switch (severity) {
                case Foundation::LogSeverity::Trace:
                    MARAS_LOG_TRACE("{}", message);
                    break;
                case Foundation::LogSeverity::Debug:
                    MARAS_LOG_DEBUG("{}", message);
                    break;
                case Foundation::LogSeverity::Info:
                    MARAS_LOG_INFO("{}", message);
                    break;
                case Foundation::LogSeverity::Warn:
                    MARAS_LOG_WARN("{}", message);
                    break;
                case Foundation::LogSeverity::Error:
                    MARAS_LOG_ERROR("{}", message);
                    break;
            }
        }
    }  // namespace

    namespace Engine {

        void InitializeFoundationLogSink() noexcept {
            Foundation::SetLogSink(&ForwardFoundationLog);
        }

        void InitializeFileLogger() noexcept {
            auto logDir = SKSE::log::log_directory();
            if (!logDir) {
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("MARAS: log directory unavailable");
                }
                return;
            }

            std::filesystem::path logPath = *logDir;
            if (!std::filesystem::is_directory(logPath)) {
                logPath = logPath.parent_path();
            }
            logPath /= "MARAS.log";

            std::error_code ec;
            std::filesystem::create_directories(logPath.parent_path(), ec);
            if (ec) {
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("MARAS: failed to create log folder (%s)", ec.message().c_str());
                }
                return;
            }

            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto logger = std::make_shared<spdlog::logger>("MARAS", std::move(sink));

            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::warn);
            logger->set_pattern("[%H:%M:%S] [%l] %v");

            spdlog::set_default_logger(logger);
            MARAS::g_Logger = std::move(logger);
            InitializeFoundationLogSink();
        }

        void ApplyLogLevel(std::int32_t level) noexcept {
            if (level < 0 || level > 5) {
                MARAS_LOG_WARN("Engine::ApplyLogLevel: invalid log level {}", level);
                return;
            }

            spdlog::level::level_enum lvl = spdlog::level::info;
            switch (level) {
                case 0:
                    lvl = spdlog::level::trace;
                    break;
                case 1:
                    lvl = spdlog::level::debug;
                    break;
                case 2:
                    lvl = spdlog::level::info;
                    break;
                case 3:
                    lvl = spdlog::level::warn;
                    break;
                case 4:
                    lvl = spdlog::level::err;
                    break;
                case 5:
                    lvl = spdlog::level::off;
                    break;
                default:
                    break;
            }

            if (const auto& logger = MARAS::GetLogger()) {
                logger->set_level(lvl);
                const auto flushLevel = (level <= 1) ? lvl : std::max(lvl, spdlog::level::warn);
                logger->flush_on(flushLevel);
            }
        }

    }  // namespace Engine

}  // namespace MARAS
