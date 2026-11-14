#pragma once

// Core precompiled header shared strictly by pure STL modules (Layer 0-2) and maras_tests.
// Must NEVER include RE/*, SKSE/*, Windows.h, nlohmann/*, yaml-cpp/*, or spdlog/*.
// Must NEVER contain global or namespace-scope `using namespace` directives.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <compare>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <regex>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "maras/foundation/StrongIds.h"
