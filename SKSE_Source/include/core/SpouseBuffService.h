#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/Common.h"

namespace MARAS {

    // SpouseBuffService is a stateless utility that computes spouse-related multipliers.
    // It exposes three functions used by gameplay code and Papyrus.
    class SpouseBuffService {
    public:
        // Returns the multiplier for a single spouse actor. Returns 0.0f for null actor.
        static float GetSpouseMultiplier(
            RE::Actor* spouse);  // affectionBuffMult is TODO; return placeholder multiplier

        // Returns a vector of multipliers for follower actors grouped by skill-type buckets.
        static std::vector<float> GetFollowersMultipliers(const std::vector<RE::Actor*>& followers);

        // Returns a vector of permanent multipliers aggregated by social class.
        static std::vector<float> GetPermanentMultipliers();
    };

}  // namespace MARAS
