#pragma once

#include <cstdint>
#include <string_view>

namespace MARAS::State {

    using SessionEpoch = std::uint64_t;

    /// Monotonic session generation counter incremented on every save load, revert, or new game.
    [[nodiscard]] SessionEpoch GetSessionEpoch() noexcept;

    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept;

    [[nodiscard]] inline bool IsSessionEpochCurrent(SessionEpoch expectedEpoch) noexcept {
        return expectedEpoch != 0 && GetSessionEpoch() == expectedEpoch;
    }

}  // namespace MARAS::State
