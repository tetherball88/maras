#include "core/LoggingService.h"

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include "PCH.h"
#include "utils/Common.h"

namespace MARAS {

    LoggingService& LoggingService::GetSingleton() {
        static LoggingService instance;
        return instance;
    }

    LoggingService::LoggingService() {
        // Initialize current log level from MARAS logger
        if (auto logger = MARAS::GetLogger()) {
            auto lvl = logger->level();
            switch (lvl) {
                case spdlog::level::trace:
                    m_logLevel = 0;
                    break;
                case spdlog::level::debug:
                    m_logLevel = 1;
                    break;
                case spdlog::level::info:
                    m_logLevel = 2;
                    break;
                case spdlog::level::warn:
                    m_logLevel = 3;
                    break;
                case spdlog::level::err:
                    m_logLevel = 4;
                    break;
                case spdlog::level::off:
                    m_logLevel = 5;
                    break;
                default:
                    m_logLevel = 2;
                    break;
            }
        } else {
            // If logger not yet initialized, default to debug (level 1)
            m_logLevel = 1;
        }
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
        }

        m_logLevel = level;
        MARAS_LOG_INFO("Applied log level {} (mapped to {})", level, static_cast<int>(lvl));
    }

    int32_t LoggingService::GetLogLevel() const { return m_logLevel; }

    bool LoggingService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Persist a single int value
        std::int32_t stored = m_logLevel;
        if (!serialization->WriteRecordData(stored)) return false;
        MARAS_LOG_INFO("Saved log level {}", m_logLevel);
        return true;
    }

    bool LoggingService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        Revert();

        std::int32_t stored = 0;
        if (!serialization->ReadRecordData(stored)) {
            MARAS_LOG_WARN("LoggingService::Load - could not read log level data");
            return false;
        }

        if (stored < 0 || stored > 5) {
            MARAS_LOG_WARN("LoggingService::Load - invalid stored log level {}", stored);
            return false;
        }

        SetLogLevel(stored);
        MARAS_LOG_INFO("Loaded and applied log level {}", stored);
        return true;
    }

    void LoggingService::Revert() {
        // Default to info when reverting
        m_logLevel = 2;
        SetLogLevel(m_logLevel);
        MARAS_LOG_INFO("Reverted logging service to default level {}", m_logLevel);
    }

}  // namespace MARAS
