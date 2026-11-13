#pragma once

#include <algorithm>
#include <cstdint>
#include <random>

namespace MARAS::Foundation {

    class IRng {
    public:
        virtual ~IRng() = default;

        [[nodiscard]] virtual float NextFloat(float minVal = 0.0f, float maxVal = 1.0f) = 0;
        [[nodiscard]] virtual std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) = 0;

        [[nodiscard]] bool RollPercent(float chancePercent) {
            if (chancePercent <= 0.0f) {
                return false;
            }
            if (chancePercent >= 100.0f) {
                return true;
            }
            return NextFloat(0.0f, 100.0f) < chancePercent;
        }
    };

    class DeterministicRng final : public IRng {
    public:
        explicit DeterministicRng(std::uint32_t seed = 0x5EED1234u) noexcept : engine_(seed) {}

        void Reseed(std::uint32_t seed) noexcept { engine_.seed(seed); }

        [[nodiscard]] float NextFloat(float minVal = 0.0f, float maxVal = 1.0f) override {
            if (maxVal <= minVal) {
                return minVal;
            }
            std::uniform_real_distribution<float> dist(minVal, maxVal);
            return dist(engine_);
        }

        [[nodiscard]] std::int32_t NextInt(std::int32_t minVal, std::int32_t maxVal) override {
            if (maxVal <= minVal) {
                return minVal;
            }
            std::uniform_int_distribution<std::int32_t> dist(minVal, maxVal);
            return dist(engine_);
        }

    private:
        std::mt19937 engine_;
    };

}  // namespace MARAS::Foundation
