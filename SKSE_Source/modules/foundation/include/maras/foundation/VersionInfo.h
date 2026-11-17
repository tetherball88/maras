#pragma once

#include <string_view>

namespace MARAS::Foundation {

    /// Compile-time fallback MARAS version when `Data/SKSE/Plugins/MARAS/version.txt` is absent.
    inline constexpr std::string_view kCompiledModVersion = "v2.5.2-dev";

}  // namespace MARAS::Foundation
