#include "core/LoggingService.h"

#include "utils/Common.h"
#include "utils/Settings.h"

namespace MARAS {

    LoggingService& LoggingService::GetSingleton() {
        static LoggingService instance;
        return instance;
    }

    void LoggingService::SetLogLevel(int32_t level) {
        // Validate
        if (level < 0 || level > 5) {
            MARAS_LOG_WARN("LoggingService::SetLogLevel: invalid log level {}", level);
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
        }

        // Use MARAS::g_Logger instead of spdlog::default_logger()
        if (auto logger = MARAS::GetLogger()) {
            logger->set_level(lvl);
            const auto flushLevel = (level <= 1) ? lvl : std::max(lvl, spdlog::level::warn);
            logger->flush_on(flushLevel);
        }

        {
            std::unique_lock lock(Settings::GetSingleton().GetMutex());
            Settings::GetSingleton().logLevel = level;
        }
        MARAS_LOG_INFO("Applied log level {} (mapped to {})", level, static_cast<int>(lvl));
    }

    int32_t LoggingService::GetLogLevel() const {
        std::shared_lock lock(Settings::GetSingleton().GetMutex());
        return Settings::GetSingleton().logLevel;
    }

}  // namespace MARAS

