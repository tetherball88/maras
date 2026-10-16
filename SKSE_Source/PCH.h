#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// Standard library headers used pervasively across the plugin
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Third-party headers used across translation units
#include <nlohmann/json.hpp>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

// Required at global scope for CommonLibSSE's generated __MARASPlugin.cpp ("MARAS"sv)
using namespace std::string_view_literals;

namespace MARAS {
    using namespace std::string_view_literals;
}

