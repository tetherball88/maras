#pragma once

#include <cstdint>

namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {
    class LoggingService {
    public:
        static LoggingService& GetSingleton();

        // Persistent log level mapping: 0=trace,1=debug,2=info,3=warn,4=err,5=off
        void SetLogLevel(int32_t level);
        int32_t GetLogLevel() const;

        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        LoggingService();
        int32_t m_logLevel{2};  // default to info
    };
}
