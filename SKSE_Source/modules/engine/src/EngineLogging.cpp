#include "maras/engine/EngineLogging.h"

#include "maras/foundation/StringUtils.h"
#include "maras/foundation/VersionInfo.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace MARAS {

    namespace {
        std::shared_ptr<spdlog::logger>& LoggerSlot() noexcept {
            static std::shared_ptr<spdlog::logger> s_logger;
            return s_logger;
        }
    }  // namespace

    const std::shared_ptr<spdlog::logger>& GetLogger() noexcept {
        return LoggerSlot();
    }

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
            MARAS::LoggerSlot() = std::move(logger);
            InitializeFoundationLogSink();
            MARAS_LOG_INFO("MARAS initialized (version: {})", GetInstalledModVersion());
        }

        std::string_view GetInstalledModVersion() noexcept {
            static const std::string s_cachedVersion = []() -> std::string {
                const std::filesystem::path candidates[] = {
                    "Data/SKSE/Plugins/MARAS/version.txt",
                    "SKSE/Plugins/MARAS/version.txt",
                };
                for (const auto& candidate : candidates) {
                    std::error_code ec;
                    if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
                        continue;
                    }
                    std::ifstream in(candidate);
                    std::string line;
                    if (in && std::getline(in, line)) {
                        const std::string_view trimmed = Foundation::StringUtils::TrimView(line);
                        if (!trimmed.empty() && trimmed.size() <= 64u) {
                            return std::string(trimmed);
                        }
                    }
                }
                return std::string(Foundation::kCompiledModVersion);
            }();
            return s_cachedVersion;
        }

        std::filesystem::path WriteMigrationReportFile(std::string_view reportText) noexcept {
            try {
                auto logDir = SKSE::log::log_directory();
                std::filesystem::path dirPath = logDir ? *logDir : std::filesystem::path("Data/SKSE/Plugins/MARAS");
                if (logDir && !std::filesystem::is_directory(dirPath)) {
                    dirPath = dirPath.parent_path();
                }
                std::error_code ec;
                std::filesystem::create_directories(dirPath, ec);
                std::filesystem::path outPath = dirPath / "MARAS_MigrationReport.log";
                std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                if (!out) {
                    MARAS_LOG_ERROR("WriteMigrationReportFile: failed to open '{}' for writing", outPath.string());
                    return {};
                }
                out.write(reportText.data(), static_cast<std::streamsize>(reportText.size()));
                out.flush();
                return std::filesystem::weakly_canonical(outPath, ec).empty() ? outPath
                                                                               : std::filesystem::weakly_canonical(outPath, ec);
            } catch (const std::exception& ex) {
                MARAS_LOG_ERROR("WriteMigrationReportFile: exception writing report: {}", ex.what());
                return {};
            }
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
