#pragma once

#include "PCH.h"

namespace MARAS {
    class LoggingService {
    public:
        static LoggingService& GetSingleton();

        // Persistent log level mapping: 0=trace,1=debug,2=info,3=warn,4=err,5=off
        void SetLogLevel(int32_t level);
        int32_t GetLogLevel() const;

    private:
        LoggingService() = default;
    };
}

